/*	$NetBSD: t_ttyio.c,v 1.3 2017/01/10 01:31:40 christos Exp $ */

/*
 * Copyright (c) 2001, 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Andrew Brown.
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
__RCSID("$NetBSD: t_ttyio.c,v 1.3 2017/01/10 01:31:40 christos Exp $");

#include <sys/types.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#if defined(__NetBSD__)
#include <util.h>
#elif defined(__bsdi__)
int openpty(int *, int *, char *, struct termios *, struct winsize *);
#elif defined(__FreeBSD__)
#include <libutil.h>
#else
#error where openpty?
#endif

#include <atf-c.h>

#define REQUIRE_ERRNO(x, v) ATF_REQUIRE_MSG(x != v, "%s: %s", #x, strerror(errno))

ATF_TC(ioctl);
ATF_TC_HEAD(ioctl, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks that ioctl calls are restarted "
		"properly after being interrupted");
}

/* ARGSUSED */
static void
sigchld(int nsig)
{
	REQUIRE_ERRNO(wait(NULL), -1);
}

ATF_TC_BODY(ioctl, tc)
{
	int m, s, rc;
	char name[128], buf[128];
	struct termios term;
	struct sigaction sa;

	/* unbuffer stdout */
	setbuf(stdout, NULL);

	/*
	 * Create default termios settings for later use
	 */
	memset(&term, 0, sizeof(term));
	term.c_iflag = TTYDEF_IFLAG;
	term.c_oflag = TTYDEF_OFLAG;
	term.c_cflag = TTYDEF_CFLAG;
	term.c_lflag = TTYDEF_LFLAG;
	cfsetspeed(&term, TTYDEF_SPEED);

	/* get a tty */
	REQUIRE_ERRNO(openpty(&m, &s, name, &term, NULL), -1);

	switch (fork()) {
	case -1:
		atf_tc_fail("fork(): %s", strerror(errno));
		/* NOTREACHED */
	case 0:
		/* wait for parent to get set up */
		(void)sleep(1);
		(void)printf("child1: exiting\n");
		exit(0);
		/* NOTREACHED */
	default:
		(void)printf("parent: spawned child1\n");
		break;
	}

	switch (fork()) {
	case -1:
		atf_tc_fail("fork(): %s", strerror(errno));
		/* NOTREACHED */
	case 0:
		/* wait for parent to get upset */
		(void)sleep(2);
		/* drain the tty q */
		if (read(m, buf, sizeof(buf)) == -1)
			err(1, "read");
		(void)printf("child2: exiting\n");
		exit(0);
		/* NOTREACHED */
	default:
		(void)printf("parent: spawned child2\n");
		break;
	}

	/* set up a restarting signal handler */
	(void)sigemptyset(&sa.sa_mask);
	sa.sa_handler = sigchld;
	sa.sa_flags = SA_RESTART;
	REQUIRE_ERRNO(sigaction(SIGCHLD, &sa, NULL), -1);
	
	/* put something in the output q */
	REQUIRE_ERRNO(write(s, "Hello world\n", 12), -1);

	/* ask for output to drain but don't drain it */
	rc = 0;
	if (tcsetattr(s, TCSADRAIN, &term) == -1) {
		(void)printf("parent: tcsetattr: %s\n", strerror(errno));
		rc = 1;
	}

	/* wait for last child */
	sa.sa_handler = SIG_DFL;
	REQUIRE_ERRNO(sigaction(SIGCHLD, &sa, NULL), -1);
	(void)wait(NULL);

	(void)close(s);
	ATF_REQUIRE_EQ(rc, 0);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, ioctl);

	return atf_no_error();
}
