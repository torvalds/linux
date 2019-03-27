/* $NetBSD: t_ttypty.c,v 1.2 2017/01/13 21:30:41 christos Exp $ */

/*-
 * Copyright (c) 2002, 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Luke Mewburn and Jaromir Dolecek.
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

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_ttypty.c,v 1.2 2017/01/13 21:30:41 christos Exp $");

#include <sys/event.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#include <atf-c.h>

#include "h_macros.h"

static void
h_check(bool check_master)
{
	char slavetty[1024];
	char buffer[128];
	struct kevent event[1];
	struct pollfd pfd;
	pid_t child;
	int status, kq, n;
	int amaster, aslave, acurrent;

	RL(openpty(&amaster, &aslave, slavetty, NULL, NULL));
	(void)printf("tty: openpty master %d slave %d tty '%s'\n",
	    amaster, aslave, slavetty);
	acurrent = check_master ? amaster : aslave;

	RL(child = fork());
	if (child == 0) {
		(void)sleep(1);

		n = read(check_master ? aslave : amaster, buffer, 128);
		(void)printf("tty: child  read '%.*s'\n", n, buffer);

		_exit(0);
	}

	pfd.fd = acurrent;
	pfd.events = POLLOUT;
	(void)printf("tty: polling ...\n");
	RL(poll(&pfd, 1, INFTIM));
	(void)printf("tty: returned from poll - %d\n", pfd.revents);

	RL(kq = kqueue());

	EV_SET(&event[0], acurrent, EVFILT_WRITE, EV_ADD|EV_ENABLE, 0, 0, 0);
	RL(kevent(kq, event, 1, NULL, 0, NULL));

	RL(n = kevent(kq, NULL, 0, event, 1, NULL));

	(void)printf("kevent num %d filt %d flags: %#x, fflags: %#x, data: "
	    "%" PRId64 "\n", n, event[0].filter, event[0].flags, event[0].fflags,
	    event[0].data);

	ATF_REQUIRE_EQ(event[0].filter, EVFILT_WRITE);

	RL(n = write(acurrent, "f00\n", 4));
	(void)printf("tty: wrote 'f00\\n' (wrote %d characters)\n", n);

	(void)waitpid(child, &status, 0);
	(void)printf("tty: successful end\n");
}

ATF_TC(master);
ATF_TC_HEAD(master, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks EVFILT_WRITE for master tty");
}
ATF_TC_BODY(master, tc)
{
	h_check(true);
}

ATF_TC(slave);
ATF_TC_HEAD(slave, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks EVFILT_WRITE for slave tty");
}
ATF_TC_BODY(slave, tc)
{
	h_check(false);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, master);
	ATF_TP_ADD_TC(tp, slave);

	return atf_no_error();
}
