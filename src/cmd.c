// SPDX-License-Identifier: BSD-3-Clause

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmd.h"
#include "utils.h"

#define READ		0
#define WRITE		1

/**
 * strscpy implementation
 */
char *strscpy(char *dest, const char *src, ssize_t size)
{
		ssize_t len;

		if (size <= 0)
			return (char *)-1;

		len = strlen(src);
		memcpy(dest, src, len);
		dest[len] = '\0';

		return dest;
}

/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir)
{
	// check if there is a next word (directory path should consist of a single word)
	if (dir->next_word != NULL)
		return 1;

	// check if the directory string is not NULL
	if (dir->string == NULL)
		return 0;

	if (chdir(dir->string) != 0)
		return 1;

	return 0;
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void)
{
	exit(0);
}

/**
 * Function used to concatenate the path of input/output/err files.
 */
char *get_path(simple_command_t *s, char *type)
{
	char *path = NULL;

	if (strcmp(type, "out") == 0) {
		word_t *parts = s->out;

		while (parts) {
			if (parts->expand) {
				char *env = getenv(parts->string);

				if (env) {
					if (!path)
						path = (char *)malloc(sizeof(char) * strlen(env) + 1);
					else
						path = (char *)realloc(path, sizeof(char) * (strlen(path) + strlen(env)) + 1);
					strcat(path, env);
				} else {
					if (!path)
						path = (char *)malloc(sizeof(char) * 2);
					else
						path = (char *)realloc(path, sizeof(char) * (strlen(path) + 1) + 1);
					strcat(path, "");
				}
			} else {
				if (!path)
					path = (char *)malloc(sizeof(char) * strlen(parts->string) + 1);
				else
					path = (char *)realloc(path, sizeof(char) * (strlen(path) + strlen(parts->string)) + 1);
				strcat(path, parts->string);
			}
			parts = parts->next_part;
		}
	} else if (strcmp(type, "err") == 0) {
		word_t *parts = s->err;

		while (parts) {
			if (parts->expand) {
				char *env = getenv(parts->string);

				if (env) {
					if (!path)
						path = (char *)malloc(sizeof(char) * strlen(env) + 1);
					else
						path = (char *)realloc(path, sizeof(char) * (strlen(path) + strlen(env)) + 1);
					strcat(path, env);
				} else {
					if (!path)
						path = (char *)malloc(sizeof(char) * 2);
					else
						path = (char *)realloc(path, sizeof(char) * (strlen(path) + 1) + 1);
					strcat(path, "");
				}
			} else {
				if (!path)
					path = (char *)malloc(sizeof(char) * strlen(parts->string) + 1);
				else
					path = (char *)realloc(path, sizeof(char) * (strlen(path) + strlen(parts->string)) + 1);
				strcat(path, parts->string);
			}
			parts = parts->next_part;
		}
	} else if (strcmp(type, "in") == 0) {
		word_t *parts = s->in;

		while (parts) {
			if (parts->expand) {
				char *env = getenv(parts->string);

				if (env) {
					if (!path)
						path = (char *)malloc(sizeof(char) * strlen(env) + 1);
					else
						path = (char *)realloc(path, sizeof(char) * (strlen(path) + strlen(env)) + 1);
					strcat(path, env);
				} else {
					if (!path)
						path = (char *)malloc(sizeof(char) * 2);
					else
						path = (char *)realloc(path, sizeof(char) * (strlen(path) + 1) + 1);
					strcat(path, "");
				}
			} else {
				if (!path)
					path = (char *)malloc(sizeof(char) * strlen(parts->string) + 1);
				else
					path = (char *)realloc(path, sizeof(char) * (strlen(path) + strlen(parts->string)) + 1);
				strcat(path, parts->string);
			}
			parts = parts->next_part;
		}
	}

	return path;
}

/**
 * Function used to build the parameters for the execvp function, when considering expanding.
 */
void adjust_params_expand(char *env, char **params, int len)
{
	if (env) {
		if (!params[len])
			params[len] = (char *)malloc(sizeof(char) * strlen(env) + 1);
		else
			params[len] = (char *)realloc(params[len], sizeof(char) * (strlen(params[len]) + strlen(env)) + 1);
		strcat(params[len], env);
	} else {
		if (!params[len])
			params[len] = (char *)malloc(sizeof(char) * 2);
		else
			params[len] = (char *)realloc(params[len], sizeof(char) * (strlen(params[len]) + 1) + 1);
		strcat(params[len], "");
	}
}

/**
 * Function used to build the parameters for the execvp function.
 */
void adjust_params(word_t *var, char **params, int len)
{
	if (!params[len])
		params[len] = (char *)malloc(sizeof(char) * strlen(var->string) + 1);
	else
		params[len] = (char *)realloc(params[len], sizeof(char) * (strlen(params[len]) + strlen(var->string)) + 1);
	strcat(params[len], var->string);
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	int ret = 0;

	if (s->verb->string == NULL)
		return 0;

	// check for environment variable assignment
	if (s->verb->next_part && strcmp(s->verb->next_part->string, "=") == 0) {
		if (!s->verb->next_part->next_part)
			return -1;

		// check if var is also an environment variable
		word_t *var = s->verb->next_part->next_part;
		char name[1024] = "";

		while (var) {
			if (var->expand) {
				char *env = getenv(var->string);

				if (env)
					strcat(name, env);
				else
					strcat(name, "");
			} else {
				strcat(name, var->string);
			}
			var = var->next_part;
		}
		setenv(s->verb->string, name, 1);
		return 0;
	}


	// built-in command
	if (strcmp(s->verb->string, "cd") == 0) {
		if (s->out) {
			char type[] = "out";
			char *path = get_path(s, type);

			int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

			if (fd == -1) {
				ret = -1;
				return ret;
			}
			close(fd);
		}

		if (s->err) {
			char type[] = "err";
			char *path = get_path(s, type);

			int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

			if (fd == -1) {
				ret = -1;
				return ret;
			}
			close(fd);
		}

		if (s->params)
			ret = shell_cd(s->params);
		else
			return 0;
	} else if (strcmp(s->verb->string, "exit") == 0) {
		ret = shell_exit();
	} else if (strcmp(s->verb->string, "quit") == 0) {
		ret = shell_exit();
	} else if (strcmp(s->verb->string, "pwd") == 0) {
		int pid = fork();

		if (pid == 0) {
			int fd = 1;
			// check if there is an output file
			if (s->out) {
				char type[] = "out";
				char *path = get_path(s, type);

				fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

				if (fd == -1) {
					ret = -1;
					return ret;
				}
				dup2(fd, 1);
				close(fd);
			}

			// print to the fd file descriptor
			char cwd[1024];

			getcwd(cwd, sizeof(cwd));
			strcat(cwd, "\n");
			write(1, cwd, strlen(cwd));
			return shell_exit();
		}

		pid_t status = 0;

		waitpid(pid, &status, 0);
		return WEXITSTATUS(status);

	} else {
		// create a child process
		int pid = fork();
		int ret = 0;

		if (pid == 0) {
			// check if the parameter is an environment variable

			// concatenate the params
			char **params = (char **)malloc(sizeof(char *));

			params[0] = (char *)malloc(sizeof(char) * strlen(s->verb->string) + 1);

			strscpy(params[0], s->verb->string, strlen(s->verb->string) + 1);
			int len = 1;

			// iterate through the parameters
			word_t *current_word = s->params;

			while (current_word) {
				params = (char **)realloc(params, (len + 1) * sizeof(char *));
				params[len] = NULL;

				word_t *var = current_word;

				while (var) {
					if (var->expand) {
						char *env = getenv(var->string);

						adjust_params_expand(env, params, len);
					} else {
						adjust_params(var, params, len);
					}
					var = var->next_part;
				}

				len++;
				current_word = current_word->next_word;
			}

			// add the last parameter as NULL
			params = (char **)realloc(params, (len + 1) * sizeof(char *));
			params[len] = NULL;

			if (s->in) {
				char type[] = "in";
				char *path = get_path(s, type);

				int fd = open(path, O_RDONLY);

				dup2(fd, 0);
				close(fd);
			}

			if (s->out) {
				char type[] = "out";
				char *pathout = get_path(s, type);

				strscpy(type, "err", 4);
				char *patherr = get_path(s, type);
				int fd = 1;

				// check for output file and/or appending file
				if (s->err && strcmp(patherr, pathout) == 0) {
					// first open the file to create it/empty it
					fd = open(pathout, O_WRONLY | O_CREAT | O_TRUNC, 0644);
					close(fd);
					fd = open(pathout, O_WRONLY | O_CREAT | O_APPEND, 0644);
				} else if (s->io_flags == IO_OUT_APPEND) {
					fd = open(pathout, O_WRONLY | O_CREAT | O_APPEND, 0644);
				} else {
					fd = open(pathout, O_WRONLY | O_CREAT | O_TRUNC, 0644);
				}

				dup2(fd, 1);
				close(fd);
			}

			// check for error file and/or appending flag
			if (s->err) {
				char type[] = "err";
				char *patherr = get_path(s, type);

				strscpy(type, "out", 4);
				char *pathout = get_path(s, type);
				int fd = 2;

				if (s->io_flags == IO_ERR_APPEND
				|| (s->out && strcmp(pathout, patherr) == 0)) {
					fd = open(patherr, O_WRONLY | O_CREAT | O_APPEND, 0644);
				} else {
					fd = open(patherr, O_WRONLY | O_CREAT | O_TRUNC, 0644);
				}

				dup2(fd, 2);
				close(fd);
			}

			// depending on wheter there are parameters or not, execute the command
			if (s->params) {
				ret = execvp(s->verb->string, params);
				fflush(stdout);
				fflush(stderr);

				if (ret != 0) {
					printf("Execution failed for '%s'\n", s->verb->string);
					exit(1);
				}
				return ret;
			}

			char *cmd = (char *)malloc(sizeof(char) * strlen(s->verb->string) + 1);

			strscpy(cmd, s->verb->string, strlen(s->verb->string) + 1);
			char * const default_params[] = {cmd, NULL};

			ret = execvp(cmd, default_params);

			fflush(stdout);
			fflush(stderr);

			if (ret != 0) {
				printf("Execution failed for '%s'\n", s->verb->string);
				exit(1);
			}
			return ret;

			perror("execvp");
		} else {
			pid_t status = 0;
			// wait for the child to finish
			waitpid(pid, &status, 0);
			return WEXITSTATUS(status);
		}
	}
	return ret;
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
							command_t *father)
{
	int pid1 = fork();

	if (pid1 == 0) {
		// Child process 1
		exit(parse_command(cmd1, level + 1, father));
	}

	int pid2 = fork();

	if (pid2 == 0) {
		// Child process 2
		exit(parse_command(cmd2, level + 1, father));
	}

	// Parent process
	int status1, status2;

	status1 = status2 = 0;

	int ret = waitpid(pid1, &status1, 0);

	ret += waitpid(pid2, &status2, 0);

	// depending on the return values of waitpid, return 1 or 0
	if (ret < 0)
		return 1;
	else
		return 0;
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	// create the anonymous pipe
	int pipefd[2];

	pipe(pipefd);

	// fork the child for the first command
	int pid = fork();

	if (pid == 0) {
		close(pipefd[READ]);
		dup2(pipefd[WRITE], 1);
		exit(parse_command(cmd1, level + 1, father));
	} else {
		pid_t ret = 0;

		// fork the child for the second command
		int pid2 = fork();

		if (pid2 == 0) {
			close(pipefd[WRITE]);
			dup2(pipefd[READ], 0);
			exit(parse_command(cmd2, level + 1, father));
		} else {
			close(pipefd[READ]);
			close(pipefd[WRITE]);

			// wait for the children to finish execution
			waitpid(pid, &ret, 0);
			waitpid(pid2, &ret, 0);
			return WEXITSTATUS(ret);
		}
	}
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	int ret = 0;

	if (c == NULL)
		return 0;

	if (c->op == OP_NONE) {
		if (c->scmd->verb && c->scmd->verb->string && strcmp(c->scmd->verb->string, "true") == 0) {
			int pid = fork();

			if (pid == 0) {
				char * const params[] = {"true", NULL};

				execvp("true", params);
			} else {
				int status = 0;

				waitpid(-1, &status, 0);
				return WEXITSTATUS(status);
			}
		}

		if (c->scmd->verb && c->scmd->verb->string && strcmp(c->scmd->verb->string, "false") == 0) {
			int pid = fork();

			if (pid == 0) {
				char * const params[] = {"false", NULL};

				execvp("false", params);
			} else {
				int status = 0;

				waitpid(-1, &status, 0);
				return WEXITSTATUS(status);
			}
		}

		ret += parse_simple(c->scmd, level, father);
		return ret;
	}

	switch (c->op) {
	case OP_SEQUENTIAL:
		parse_command(c->cmd1, level + 1, c);
		parse_command(c->cmd2, level + 1, c);
		break;

	case OP_PARALLEL:
		return run_in_parallel(c->cmd1, c->cmd2, level + 1, c);

	case OP_CONDITIONAL_NZERO:
		// only execute the second one if the first one failed
		if (parse_command(c->cmd1, level + 1, c) != 0)
			return parse_command(c->cmd2, level + 1, c);
		break;

	case OP_CONDITIONAL_ZERO:
		// only execute the second one if the first one succeeded
		if (parse_command(c->cmd1, level + 1, c) == 0)
			return parse_command(c->cmd2, level + 1, c);
		break;

	case OP_PIPE:
		return run_on_pipe(c->cmd1, c->cmd2, level + 1, c);

	default:
		return SHELL_EXIT;
	}

	return ret;
}
