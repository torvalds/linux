/*	$NetBSD: h_exec.c,v 1.6 2011/02/16 17:57:44 pooka Exp $	*/

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
#include <sys/socket.h>

#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rump/rumpclient.h>
#include <rump/rump_syscalls.h>

int
main(int argc, char *argv[])
{
	struct sockaddr_in sin;
	socklen_t slen;
	int s1, s2;
	char buf[12];
	char *eargv[4];
	char *ename;
	extern char **environ;

	if (rumpclient_init() == -1)
		err(1, "init");

	if (argc > 1) {
		if (strcmp(argv[1], "_didexec") == 0) {
			rumpclient_daemon(0, 0); /* detach-me-notnot */
			s2 = atoi(argv[2]);
			slen = sizeof(sin);
			/* see below */
			rump_sys_accept(s2, (struct sockaddr *)&sin, &slen);
		}
	}

	/* open and listenize two TCP4 suckets */
	if ((s1 = rump_sys_socket(PF_INET, SOCK_STREAM, 0)) == -1)
		err(1, "socket 1");
	if ((s2 = rump_sys_socket(PF_INET, SOCK_STREAM, 0)) == -1)
		err(1, "socket 2");

	memset(&sin, 0, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(1234);

	if (rump_sys_bind(s1, (struct sockaddr *)&sin, sizeof(sin)) == -1)
		err(1, "bind1");
	sin.sin_port = htons(2345);
	if (rump_sys_bind(s2, (struct sockaddr *)&sin, sizeof(sin)) == -1)
		err(1, "bind2");

	if (rump_sys_listen(s1, 1) == -1)
		err(1, "listen1");
	if (rump_sys_listen(s2, 1) == -1)
		err(1, "listen2");

	if (argc == 1) {
		rumpclient_daemon(0, 0);
		slen = sizeof(sin);
		/*
		 * "pause()", but conveniently gets rid of this helper
		 * since we were called with RUMPCLIENT_RETRYCONN_DIE set
		 */
		rump_sys_accept(s2, (struct sockaddr *)&sin, &slen);
	}

	if (argc == 3 && strcmp(argv[2], "cloexec1") == 0) {
		if (rump_sys_fcntl(s1, F_SETFD, FD_CLOEXEC) == -1) {
			err(1, "cloexec failed");
		}
	}

	sprintf(buf, "%d", s2);

	if (argc == 3 && strcmp(argv[2], "vfork_please") == 0) {
		switch (rumpclient_vfork()) {
		case 0:
			ename = __UNCONST("fourchette");
			break;
		case -1:
			err(1, "vfork");
		default:
			ename = __UNCONST("h_ution");
			break;
		}
	} else {
		ename = __UNCONST("h_ution");
	}

	/* omstart! */
	eargv[0] = ename;
	eargv[1] = __UNCONST("_didexec");
	eargv[2] = buf;
	eargv[3] = NULL;
	if (rumpclient_exec(argv[1], __UNCONST(eargv), environ) == -1)
		err(1, "exec");
}
