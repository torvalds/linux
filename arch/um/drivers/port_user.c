/*
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{linux.intel,addtoit}.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <netinet/in.h>
#include "chan_user.h"
#include "os.h"
#include "port.h"
#include "um_malloc.h"

struct port_chan {
	int raw;
	struct termios tt;
	void *kernel_data;
	char dev[sizeof("32768\0")];
};

static void *port_init(char *str, int device, const struct chan_opts *opts)
{
	struct port_chan *data;
	void *kern_data;
	char *end;
	int port;

	if (*str != ':') {
		printk(UM_KERN_ERR "port_init : channel type 'port' must "
		       "specify a port number\n");
		return NULL;
	}
	str++;
	port = strtoul(str, &end, 0);
	if ((*end != '\0') || (end == str)) {
		printk(UM_KERN_ERR "port_init : couldn't parse port '%s'\n",
		       str);
		return NULL;
	}

	kern_data = port_data(port);
	if (kern_data == NULL)
		return NULL;

	data = uml_kmalloc(sizeof(*data), UM_GFP_KERNEL);
	if (data == NULL)
		goto err;

	*data = ((struct port_chan) { .raw  		= opts->raw,
				      .kernel_data 	= kern_data });
	sprintf(data->dev, "%d", port);

	return data;
 err:
	port_kern_free(kern_data);
	return NULL;
}

static void port_free(void *d)
{
	struct port_chan *data = d;

	port_kern_free(data->kernel_data);
	kfree(data);
}

static int port_open(int input, int output, int primary, void *d,
		     char **dev_out)
{
	struct port_chan *data = d;
	int fd, err;

	fd = port_wait(data->kernel_data);
	if ((fd >= 0) && data->raw) {
		CATCH_EINTR(err = tcgetattr(fd, &data->tt));
		if (err)
			return err;

		err = raw(fd);
		if (err)
			return err;
	}
	*dev_out = data->dev;
	return fd;
}

static void port_close(int fd, void *d)
{
	struct port_chan *data = d;

	port_remove_dev(data->kernel_data);
	os_close_file(fd);
}

const struct chan_ops port_ops = {
	.type		= "port",
	.init		= port_init,
	.open		= port_open,
	.close		= port_close,
	.read	        = generic_read,
	.write		= generic_write,
	.console_write	= generic_console_write,
	.window_size	= generic_window_size,
	.free		= port_free,
	.winch		= 1,
};

int port_listen_fd(int port)
{
	struct sockaddr_in addr;
	int fd, err, arg;

	fd = socket(PF_INET, SOCK_STREAM, 0);
	if (fd == -1)
		return -errno;

	arg = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &arg, sizeof(arg)) < 0) {
		err = -errno;
		goto out;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		err = -errno;
		goto out;
	}

	if (listen(fd, 1) < 0) {
		err = -errno;
		goto out;
	}

	err = os_set_fd_block(fd, 0);
	if (err < 0)
		goto out;

	return fd;
 out:
	close(fd);
	return err;
}

struct port_pre_exec_data {
	int sock_fd;
	int pipe_fd;
};

static void port_pre_exec(void *arg)
{
	struct port_pre_exec_data *data = arg;

	dup2(data->sock_fd, 0);
	dup2(data->sock_fd, 1);
	dup2(data->sock_fd, 2);
	close(data->sock_fd);
	dup2(data->pipe_fd, 3);
	shutdown(3, SHUT_RD);
	close(data->pipe_fd);
}

int port_connection(int fd, int *socket, int *pid_out)
{
	int new, err;
	char *argv[] = { "/usr/sbin/in.telnetd", "-L",
			 "/usr/lib/uml/port-helper", NULL };
	struct port_pre_exec_data data;

	new = accept(fd, NULL, 0);
	if (new < 0)
		return -errno;

	err = os_pipe(socket, 0, 0);
	if (err < 0)
		goto out_close;

	data = ((struct port_pre_exec_data)
		{ .sock_fd  		= new,
		  .pipe_fd 		= socket[1] });

	err = run_helper(port_pre_exec, &data, argv);
	if (err < 0)
		goto out_shutdown;

	*pid_out = err;
	return new;

 out_shutdown:
	shutdown(socket[0], SHUT_RDWR);
	close(socket[0]);
	shutdown(socket[1], SHUT_RDWR);
	close(socket[1]);
 out_close:
	close(new);
	return err;
}
