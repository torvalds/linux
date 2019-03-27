/*	$NetBSD: cltest.c,v 1.6 2015/01/22 05:44:28 christos Exp $	*/

/*-
 * Copyright (c) 2015 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/cdefs.h>
__RCSID("$NetBSD: cltest.c,v 1.6 2015/01/22 05:44:28 christos Exp $");

#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <err.h>
#ifdef HAVE_UTIL_H
#include <util.h>
#endif

static __dead void
usage(int c)
{
	warnx("Unknown option `%c'", (char)c);
	fprintf(stderr, "Usage: %s [-u] [-a <addr>] [-m <msg>] [-p <port>]\n",
	    getprogname());
	exit(EXIT_FAILURE);
}

static void
getaddr(const char *a, in_port_t p, struct sockaddr_storage *ss,
    socklen_t *slen)
{
	int c;

	memset(ss, 0, sizeof(*ss));
	p = htons(p);

	if (strchr(a, ':')) {
		struct sockaddr_in6 *s6 = (void *)ss;
		c = inet_pton(AF_INET6, a, &s6->sin6_addr);
		s6->sin6_family = AF_INET6;
		*slen = sizeof(*s6);
		s6->sin6_port = p;
	} else {
		struct sockaddr_in *s = (void *)ss;
		c = inet_pton(AF_INET, a, &s->sin_addr);
		s->sin_family = AF_INET;
		*slen = sizeof(*s);
		s->sin_port = p;
	}
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
	ss->ss_len = (uint8_t)*slen;
#endif
	if (c == -1)
		err(EXIT_FAILURE, "Invalid address `%s'", a);
}

int
main(int argc, char *argv[])
{
	int sfd;
	int c;
	struct sockaddr_storage ss;
	const char *msg = "hello";
	const char *addr = "127.0.0.1";
	int type = SOCK_STREAM;
	in_port_t port = 6161;
	socklen_t slen;
	char buf[128];

	while ((c = getopt(argc, argv, "a:m:p:u")) != -1) {
		switch (c) {
		case 'a':
			addr = optarg;
			break;
		case 'm':
			msg = optarg;
			break;
		case 'p':
			port = (in_port_t)atoi(optarg);
			break;
		case 'u':
			type = SOCK_DGRAM;
			break;
		default:
			usage(c);
		}
	}

	getaddr(addr, port, &ss, &slen);

	if ((sfd = socket(AF_INET, type, 0)) == -1)
		err(EXIT_FAILURE, "socket");

	sockaddr_snprintf(buf, sizeof(buf), "%a:%p", (const void *)&ss);
	printf("connecting to: %s\n", buf);
	if (connect(sfd, (const void *)&ss, slen) == -1)
		err(EXIT_FAILURE, "connect");

	size_t len = strlen(msg) + 1;
	if (write(sfd, msg, len) != (ssize_t)len)
		err(EXIT_FAILURE, "write");
	return 0;
}
