/*	$NetBSD: srvtest.c,v 1.10 2015/05/30 22:40:38 christos Exp $	*/

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
__RCSID("$NetBSD: srvtest.c,v 1.10 2015/05/30 22:40:38 christos Exp $");

#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <err.h>

#include "blacklist.h"
#ifdef BLDEBUG
#include "bl.h"
static void *b;
#endif

#ifndef INFTIM
#define INFTIM -1
#endif

static void
process_tcp(int afd)
{
	ssize_t n;
	char buffer[256];

	memset(buffer, 0, sizeof(buffer));

	if ((n = read(afd, buffer, sizeof(buffer))) == -1)
		err(1, "read");
	buffer[sizeof(buffer) - 1] = '\0';
	printf("%s: sending %d %s\n", getprogname(), afd, buffer);
#ifdef BLDEBUG
	blacklist_r(b, 1, afd, buffer);
#else
	blacklist(1, afd, buffer);
#endif
	exit(0);
}

static void
process_udp(int afd)
{
	ssize_t n;
	char buffer[256];
	struct sockaddr_storage ss;
	socklen_t slen;

	memset(buffer, 0, sizeof(buffer));

	slen = (socklen_t)sizeof(ss);
	memset(&ss, 0, sizeof(ss));
	if ((n = recvfrom(afd, buffer, sizeof(buffer), 0, (void *)&ss,
		&slen)) == -1)
		err(1, "recvfrom");
	buffer[sizeof(buffer) - 1] = '\0';
	printf("%s: sending %d %s\n", getprogname(), afd, buffer);
	blacklist_sa(1, afd, (void *)&ss, slen, buffer);
	exit(0);
}
static int
cr(int af, int type, in_port_t p)
{
	int sfd;
	struct sockaddr_storage ss;
	socklen_t slen;
	sfd = socket(af == AF_INET ? PF_INET : PF_INET6, type, 0);
	if (sfd == -1)
		err(1, "socket");

	p = htons(p);
	memset(&ss, 0, sizeof(ss));
	if (af == AF_INET) {
		struct sockaddr_in *s = (void *)&ss;
		s->sin_family = AF_INET;
		slen = sizeof(*s);
		s->sin_port = p;
	} else {
		struct sockaddr_in6 *s6 = (void *)&ss;
		s6->sin6_family = AF_INET6;
		slen = sizeof(*s6);
		s6->sin6_port = p;
	}
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
	ss.ss_len = (uint8_t)slen;
#endif
     
	if (bind(sfd, (const void *)&ss, slen) == -1)
		err(1, "bind");

	if (type != SOCK_DGRAM)
		if (listen(sfd, 5) == -1)
			err(1, "listen");
	return sfd;
}

static void
handle(int type, int sfd)
{
	struct sockaddr_storage ss;
	socklen_t alen = sizeof(ss);
	int afd;

	if (type != SOCK_DGRAM) {
		if ((afd = accept(sfd, (void *)&ss, &alen)) == -1)
			err(1, "accept");
	} else
		afd = sfd;

	/* Create child process */
	switch (fork()) {
	case -1:
		err(1, "fork");
	case 0:
		if (type == SOCK_DGRAM)
			process_udp(afd);
		else
			process_tcp(afd);
		break;
	default:
		close(afd);
		break;
	}
}

static __dead void
usage(int c)
{
	warnx("Unknown option `%c'", (char)c);
	fprintf(stderr, "Usage: %s [-u] [-p <num>]\n", getprogname());
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
#ifdef __linux__
#define NUMFD 1
#else
#define NUMFD 2
#endif
	struct pollfd pfd[NUMFD];
	int type = SOCK_STREAM, c;
	in_port_t port = 6161;

	signal(SIGCHLD, SIG_IGN);

#ifdef BLDEBUG
	b = bl_create(false, "blsock", vsyslog);
#endif

	while ((c = getopt(argc, argv, "up:")) != -1)
		switch (c) {
		case 'u':
			type = SOCK_DGRAM;
			break;
		case 'p':
			port = (in_port_t)atoi(optarg);
			break;
		default:
			usage(c);
		}

	pfd[0].fd = cr(AF_INET, type, port);
	pfd[0].events = POLLIN;
#if NUMFD > 1
	pfd[1].fd = cr(AF_INET6, type, port);
	pfd[1].events = POLLIN;
#endif

	for (;;) {
		if (poll(pfd, __arraycount(pfd), INFTIM) == -1)
			err(1, "poll");
		for (size_t i = 0; i < __arraycount(pfd); i++) {
			if ((pfd[i].revents & POLLIN) == 0)
				continue;
			handle(type, pfd[i].fd);
		}
	}
}
