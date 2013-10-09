/*
 * ktapio.c - ring buffer transport in userspace
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2012-2013 Jovi Zhangwei <jovi.zhangwei@gmail.com>.
 *
 * ktap is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * ktap is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/signal.h>
#include <fcntl.h>
#include <pthread.h>

#define MAX_BUFLEN  131072
#define PATH_MAX 128

#define handle_error(str) do { perror(str); exit(-1); } while(0)

extern pid_t ktap_pid;

void sigfunc(int signo)
{
	/* should not not reach here */
}

static void block_sigint()
{
	sigset_t mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);

	pthread_sigmask(SIG_BLOCK, &mask, NULL);
}

static void *reader_thread(void *data)
{
	char buf[MAX_BUFLEN];
	char filename[PATH_MAX];
	const char *output = data; 
	int failed = 0, fd, out_fd, len;

	block_sigint();

	if (output) {
		out_fd = open(output, O_CREAT | O_WRONLY | O_TRUNC,
					S_IRUSR|S_IWUSR);
		if (out_fd < 0) {
			fprintf(stderr, "Cannot open output file %s\n", output);
			return NULL;
		}
	} else
		out_fd = 2;

	sprintf(filename, "/sys/kernel/debug/ktap/trace_pipe_%d", ktap_pid);

 open_again:
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		usleep(10000);

		if (failed++ == 10) {
			fprintf(stderr, "Cannot open file %s\n", filename);
			return NULL;
		}
		goto open_again;
	}

	while ((len = read(fd, buf, sizeof(buf))) > 0)
		write(out_fd, buf, len);

	close(fd);
	close(out_fd);

	return NULL;
}

int ktapio_create(const char *output)
{
	pthread_t reader;

	signal(SIGINT, sigfunc);

	if (pthread_create(&reader, NULL, reader_thread, (void *)output) < 0)
		handle_error("pthread_create reader_thread failed\n");

	return 0;
}

