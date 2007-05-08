/*
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include "user.h"
#include "mconsole.h"
#include "os.h"
#include "choose-mode.h"
#include "mode.h"

struct dog_data {
	int stdin;
	int stdout;
	int close_me[2];
};

static void pre_exec(void *d)
{
	struct dog_data *data = d;

	dup2(data->stdin, 0);
	dup2(data->stdout, 1);
	dup2(data->stdout, 2);
	os_close_file(data->stdin);
	os_close_file(data->stdout);
	os_close_file(data->close_me[0]);
	os_close_file(data->close_me[1]);
}

int start_watchdog(int *in_fd_ret, int *out_fd_ret, char *sock)
{
	struct dog_data data;
	int in_fds[2], out_fds[2], pid, n, err;
	char pid_buf[sizeof("nnnnn\0")], c;
	char *pid_args[] = { "/usr/bin/uml_watchdog", "-pid", pid_buf, NULL };
	char *mconsole_args[] = { "/usr/bin/uml_watchdog", "-mconsole", NULL,
				  NULL };
	char **args = NULL;

	err = os_pipe(in_fds, 1, 0);
	if(err < 0){
		printk("harddog_open - os_pipe failed, err = %d\n", -err);
		goto out;
	}

	err = os_pipe(out_fds, 1, 0);
	if(err < 0){
		printk("harddog_open - os_pipe failed, err = %d\n", -err);
		goto out_close_in;
	}

	data.stdin = out_fds[0];
	data.stdout = in_fds[1];
	data.close_me[0] = out_fds[1];
	data.close_me[1] = in_fds[0];

	if(sock != NULL){
		mconsole_args[2] = sock;
		args = mconsole_args;
	}
	else {
		/* XXX The os_getpid() is not SMP correct */
		sprintf(pid_buf, "%d", CHOOSE_MODE(tracing_pid, os_getpid()));
		args = pid_args;
	}

	pid = run_helper(pre_exec, &data, args, NULL);

	os_close_file(out_fds[0]);
	os_close_file(in_fds[1]);

	if(pid < 0){
		err = -pid;
		printk("harddog_open - run_helper failed, errno = %d\n", -err);
		goto out_close_out;
	}

	n = os_read_file(in_fds[0], &c, sizeof(c));
	if(n == 0){
		printk("harddog_open - EOF on watchdog pipe\n");
		helper_wait(pid);
		err = -EIO;
		goto out_close_out;
	}
	else if(n < 0){
		printk("harddog_open - read of watchdog pipe failed, "
		       "err = %d\n", -n);
		helper_wait(pid);
		err = n;
		goto out_close_out;
	}
	*in_fd_ret = in_fds[0];
	*out_fd_ret = out_fds[1];
	return 0;

 out_close_in:
	os_close_file(in_fds[0]);
	os_close_file(in_fds[1]);
 out_close_out:
	os_close_file(out_fds[0]);
	os_close_file(out_fds[1]);
 out:
	return err;
}

void stop_watchdog(int in_fd, int out_fd)
{
	os_close_file(in_fd);
	os_close_file(out_fd);
}

int ping_watchdog(int fd)
{
	int n;
	char c = '\n';

	n = os_write_file(fd, &c, sizeof(c));
	if(n != sizeof(c)){
		printk("ping_watchdog - write failed, err = %d\n", -n);
		if(n < 0)
			return n;
		return -EIO;
	}
	return 1;

}
