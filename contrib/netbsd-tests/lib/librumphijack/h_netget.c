/*      $NetBSD: h_netget.c,v 1.2 2012/04/17 09:23:21 jruoho Exp $	*/

/*-
 * Copyright (c) 2011 Antti Kantee.  All Rights Reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * simple utility to fetch a webpage.  we wouldn't need this
 * if we had something like netcat in base
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: h_netget.c,v 1.2 2012/04/17 09:23:21 jruoho Exp $");

#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#include <netinet/in.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GETSTR "GET / HTTP/1.0\n\n"

int
main(int argc, char *argv[])
{
	char buf[8192];
	struct sockaddr_in sin;
	ssize_t n;
	int s, fd;

	setprogname(argv[0]);
	if (argc != 4) {
		fprintf(stderr, "usage: %s address port savefile\n",
		    getprogname());
		return EXIT_FAILURE;
	}

	s = socket(PF_INET, SOCK_STREAM, 0);
	if (s == -1)
		err(EXIT_FAILURE, "socket");

	memset(&sin, 0, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(atoi(argv[2]));
	sin.sin_addr.s_addr = inet_addr(argv[1]);

	fd = open(argv[3], O_CREAT | O_RDWR, 0644);
	if (fd == -1)
		err(EXIT_FAILURE, "open");
	if (ftruncate(fd, 0) == -1)
		err(EXIT_FAILURE, "ftruncate savefile");

	if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) == -1)
		err(EXIT_FAILURE, "connect");

	if (write(s, GETSTR, strlen(GETSTR)) != strlen(GETSTR))
		err(EXIT_FAILURE, "socket write");

	for (;;) {
		n = read(s, buf, sizeof(buf));
		if (n == 0)
			break;
		if (n == -1)
			err(EXIT_FAILURE, "socket read");

		if (write(fd, buf, n) != n)
			err(EXIT_FAILURE, "write file");
	}

	return EXIT_SUCCESS;
}
