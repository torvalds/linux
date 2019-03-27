/*	$NetBSD: t_select.c,v 1.4 2017/01/13 21:18:33 christos Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundatiom
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

#include <assert.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <err.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <atf-c.h>

static sig_atomic_t keep_going = 1;

static void
sig_handler(int signum __unused)
{
	keep_going = 0;
}

static void
sigchld(int signum __unused)
{
}

static char
xtoa(uint8_t n)
{
	static const char xarray[] = "0123456789abcdef";
	assert(n < sizeof(xarray));
	return xarray[n];
}

static const char *
prmask(const sigset_t *m, char *buf, size_t len)
{
	size_t j = 2;
	assert(len >= 3 + sizeof(*m));
	buf[0] = '0';
	buf[1] = 'x';
#define N(p, a)	(((p) >> ((a) * 4)) & 0xf)
	for (size_t i = __arraycount(m->__bits); i > 0; i--) {
		uint32_t p = m->__bits[i - 1];
		for (size_t k = sizeof(p); k > 0; k--)
			buf[j++] = xtoa(N(p, k - 1));
	}
	buf[j] = '\0';
	return buf;
}

static __dead void
child(const struct timespec *ts)
{
	struct sigaction sa;
	sigset_t set, oset, nset;
	char obuf[sizeof(oset) + 3], nbuf[sizeof(nset) + 3];
	int fd;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = sig_handler;
	if ((fd = open("/dev/null", O_RDONLY)) == -1)
		err(1, "open");

	if (sigaction(SIGTERM, &sa, NULL) == -1)
		err(1, "sigaction");

	sigfillset(&set);
	if (sigprocmask(SIG_BLOCK, &set, NULL) == -1)
		err(1, "sigprocmask");

	if (sigprocmask(SIG_BLOCK, NULL, &oset) == -1)
		err(1, "sigprocmask");

	sigemptyset(&set);

	for (;;) {
		fd_set rset;
		FD_ZERO(&rset);
		FD_SET(fd, &rset);
		if (pselect(1, &rset, NULL, NULL, ts, &set) == -1) {
			if(errno == EINTR) {
				if (!keep_going)
					break;
			}
		}
		if (ts)
			break;
	}
	if (sigprocmask(SIG_BLOCK, NULL, &nset) == -1)
		err(1, "sigprocmask");
	if (memcmp(&oset, &nset, sizeof(oset)) != 0)
		atf_tc_fail("pselect() masks don't match "
		    "after timeout %s != %s",
		    prmask(&nset, nbuf, sizeof(nbuf)),
		    prmask(&oset, obuf, sizeof(obuf)));
	_exit(0);
}

ATF_TC(pselect_sigmask);
ATF_TC_HEAD(pselect_sigmask, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks pselect's temporary mask "
	    "setting when a signal is received (PR lib/43625)");
}

ATF_TC_BODY(pselect_sigmask, tc)
{
	pid_t pid;
	int status;

	signal(SIGCHLD, sigchld);

	switch (pid = fork()) {
	case 0:
		child(NULL);
		/*NOTREACHED*/
	case -1:
		err(1, "fork");
	default:
		sleep(1);
		if (kill(pid, SIGTERM) == -1)
			err(1, "kill");
		sleep(1);
		switch (waitpid(pid, &status, WNOHANG)) {
		case -1:
			err(1, "wait");
		case 0:
			if (kill(pid, SIGKILL) == -1)
				err(1, "kill");
			atf_tc_fail("pselect() did not receive signal");
			break;
		default:
			break;
		}
	}
}

ATF_TC(pselect_timeout);
ATF_TC_HEAD(pselect_timeout, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks pselect's temporary mask "
	    "setting when a timeout occurs");
}

ATF_TC_BODY(pselect_timeout, tc)
{
	pid_t pid;
	int status;
	static const struct timespec zero = { 0, 0 };

	signal(SIGCHLD, sigchld);

	switch (pid = fork()) {
	case 0:
		child(&zero);
		break;
	case -1:
		err(1, "fork");
	default:
		sleep(1);
		switch (waitpid(pid, &status, WNOHANG)) {
		case -1:
			err(1, "wait");
		case 0:
			if (kill(pid, SIGKILL) == -1)
				err(1, "kill");
			atf_tc_fail("pselect() did not receive signal");
			break;
		default:
			break;
		}
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, pselect_sigmask);
	ATF_TP_ADD_TC(tp, pselect_timeout);

	return atf_no_error();
}
