/*	$NetBSD: h_client.c,v 1.8 2012/04/20 05:15:11 jruoho Exp $	*/

/*
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/poll.h>
#include <sys/select.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{

	if (argc != 2) {
		errx(1, "need testname as param");
	}

	if (strcmp(argv[1], "select_timeout") == 0) {
		fd_set rfds;
		struct timeval tv;
		int pipefd[2];
		int rv;

		tv.tv_sec = 0;
		tv.tv_usec = 1;

		if (pipe(pipefd) == -1)
			err(EXIT_FAILURE, "pipe");
		FD_ZERO(&rfds);
		FD_SET(pipefd[0], &rfds);

		rv = select(pipefd[0]+1, &rfds, NULL, NULL, &tv);
		if (rv == -1)
			err(EXIT_FAILURE, "select");
		if (rv != 0)
			errx(EXIT_FAILURE, "select succesful");

		if (FD_ISSET(pipefd[0], &rfds))
			errx(EXIT_FAILURE, "stdin fileno is still set");
		return EXIT_SUCCESS;
	} else if (strcmp(argv[1], "select_allunset") == 0) {
		fd_set fds;
		struct timeval tv;
		int rv;

		tv.tv_sec = 0;
		tv.tv_usec = 1;

		FD_ZERO(&fds);

		rv = select(100, &fds, &fds, &fds, &tv);
		if (rv == -1)
			err(EXIT_FAILURE, "select");
		if (rv != 0)
			errx(EXIT_FAILURE, "select succesful");

		rv = select(0, NULL, NULL, NULL, &tv);
		if (rv == -1)
			err(EXIT_FAILURE, "select2");
		if (rv != 0)
			errx(EXIT_FAILURE, "select2 succesful");

		return EXIT_SUCCESS;
	} else if (strcmp(argv[1], "invafd") == 0) {
		struct pollfd pfd[2];
		int fd, rv;

		fd = open("/rump/dev/null", O_RDWR);
		if (fd == -1)
			err(EXIT_FAILURE, "open");
		close(fd);

		pfd[0].fd = STDIN_FILENO;
		pfd[0].events = POLLIN;
		pfd[1].fd = fd;
		pfd[1].events = POLLIN;

		if ((rv = poll(pfd, 2, INFTIM)) != 1)
			errx(EXIT_FAILURE, "poll unexpected rv %d (%d)",
			    rv, errno);
		if (pfd[1].revents != POLLNVAL || pfd[0].revents != 0)
			errx(EXIT_FAILURE, "poll unexpected revents");

		return EXIT_SUCCESS;
	} else if (strcmp(argv[1], "fdoff8") == 0) {

		(void)closefrom(0);

		int fd;

		do {
			if ((fd = open("/dev/null", O_RDWR)) == -1)
				err(EXIT_FAILURE, "open1");
		} while (fd < 7);
		fd = open("/dev/null", O_RDWR);
		if (fd != -1 || errno != ENFILE)
			errx(EXIT_FAILURE, "unexpected fd8 %d %d", fd, errno);
		if (fcntl(0, F_MAXFD) != 7)
			errx(EXIT_FAILURE, "fd leak?");
		if ((fd = open("/rump/dev/null", O_RDWR)) != 8)
			errx(EXIT_FAILURE, "rump open %d %d", fd, errno);
		return EXIT_SUCCESS;
	} else {
		return ENOTSUP;
	}
}
