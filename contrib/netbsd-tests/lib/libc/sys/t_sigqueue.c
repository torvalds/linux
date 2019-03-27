/* $NetBSD: t_sigqueue.c,v 1.7 2017/01/13 20:44:10 christos Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
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

#include <sys/cdefs.h>
__RCSID("$NetBSD: t_sigqueue.c,v 1.7 2017/01/13 20:44:10 christos Exp $");

#include <atf-c.h>
#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <unistd.h>

static void	handler(int, siginfo_t *, void *);

#define VALUE (int)0xc001dad1
static int value;

static void
handler(int signo __unused, siginfo_t *info, void *data __unused)
{
	value = info->si_value.sival_int;
	kill(0, SIGINFO);
}

ATF_TC(sigqueue_basic);
ATF_TC_HEAD(sigqueue_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks sigqueue(3) sigval delivery");
}

ATF_TC_BODY(sigqueue_basic, tc)
{
	struct sigaction sa;
	union sigval sv;

	sa.sa_sigaction = handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;

	if (sigaction(SIGUSR1, &sa, NULL) != 0)
		atf_tc_fail("sigaction failed");

	sv.sival_int = VALUE;

#ifdef __FreeBSD__
	/*
	 * From kern_sig.c:
	 * Specification says sigqueue can only send signal to single process.
	 */
	if (sigqueue(getpid(), SIGUSR1, sv) != 0)
#else
	if (sigqueue(0, SIGUSR1, sv) != 0)
#endif
		atf_tc_fail("sigqueue failed");

	sched_yield();
	ATF_REQUIRE_EQ(sv.sival_int, value);
}

ATF_TC(sigqueue_err);
ATF_TC_HEAD(sigqueue_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from sigqueue(3)");
}

ATF_TC_BODY(sigqueue_err, tc)
{
	static union sigval sv;

	errno = 0;
	ATF_REQUIRE_ERRNO(EINVAL, sigqueue(getpid(), -1, sv) == -1);
}

static int signals[] = {
	SIGINT, SIGRTMIN + 1, SIGINT, SIGRTMIN + 0, SIGRTMIN + 2,
	SIGQUIT, SIGRTMIN + 1
};
#ifdef __arraycount
#define CNT	__arraycount(signals)
#else
#define CNT	(sizeof(signals) / sizeof(signals[0]))
#endif

static sig_atomic_t count = 0;
static int delivered[CNT];

static void
myhandler(int signo, siginfo_t *info, void *context __unused)
{
	delivered[count++] = signo;
	printf("Signal #%zu: signo: %d\n", (size_t)count, signo);
}

static int
asc(const void *a, const void *b)
{
	const int *ia = a, *ib = b;
	return *ib - *ia;
}

/*
 * given a array of signals to be delivered in tosend of size len
 * place in ordered the signals to be delivered in delivery order
 * and return the number of signals that should be delivered
 */
static size_t
sigorder(int *ordered, const int *tosend, size_t len)
{
	memcpy(ordered, tosend, len * sizeof(*tosend));
	qsort(ordered, len, sizeof(*ordered), asc);
	if (len == 1)
		return len;

#ifdef __FreeBSD__
	/*
	 * Don't dedupe signal numbers (bug 212173)
	 *
	 * Per kib's comment..
	 *
	 * "
	 * OTOH, FreeBSD behaviour is to treat all signals as realtime while
	 * there is no mem shortage and siginfo can be allocated.  In
	 * particular, signals < SIGRTMIN are not collapsed when queued more
	 * than once.
	 * "
	 */

	return len;
#else

	size_t i, j;
	for (i = 0, j = 0; i < len - 1; i++) {
		if (ordered[i] >= SIGRTMIN)
			continue;
		if (j == 0)
			j = i + 1;
		while (ordered[i] == ordered[j] && j < len)
			j++;
		if (j == len)
			break;
		ordered[i + 1] = ordered[j];
	}
	return i + 1;
#endif
}

ATF_TC(sigqueue_rt);
ATF_TC_HEAD(sigqueue_rt, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test queuing of real-time signals");
}

ATF_TC_BODY(sigqueue_rt, tc)
{
	pid_t pid;
	union sigval val;
	struct sigaction act;
	int ordered[CNT];
	struct sigaction oact[CNT];
	size_t ndelivered;

	ndelivered = sigorder(ordered, signals, CNT);

	act.sa_flags = SA_SIGINFO;
	act.sa_sigaction = myhandler;
	sigemptyset(&act.sa_mask);
	for (size_t i = 0; i < ndelivered; i++)
		ATF_REQUIRE(sigaction(ordered[i], &act, &oact[i]) != -1);

	val.sival_int = 0;
	pid = getpid();

	sigset_t mask, orig;
	sigemptyset(&mask);
	for (size_t i = 0; i < CNT; i++)
		if (sigaddset(&mask, signals[i]) == -1)
			warn("sigaddset");

	ATF_REQUIRE(sigprocmask(SIG_BLOCK, &mask, &orig) != -1);
	
	for (size_t i = 0; i < CNT; i++)
		ATF_REQUIRE(sigqueue(pid, signals[i], val) != -1);
	
	ATF_REQUIRE(sigprocmask(SIG_UNBLOCK, &mask, &orig) != -1);
	sleep(1);
	ATF_CHECK_MSG((size_t)count == ndelivered,
	    "count %zu != ndelivered %zu", (size_t)count, ndelivered);
	for (size_t i = 0; i < ndelivered; i++)
		ATF_REQUIRE_MSG(ordered[i] == delivered[i],
		    "%zu: ordered %d != delivered %d",
		    i, ordered[i], delivered[i]);

	if ((size_t)count > ndelivered)
		for (size_t i = ndelivered; i < (size_t)count; i++)
			printf("Undelivered signal #%zu: %d\n", i, ordered[i]);

	for (size_t i = 0; i < ndelivered; i++)
		ATF_REQUIRE(sigaction(signals[i], &oact[i], NULL) != -1);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, sigqueue_basic);
	ATF_TP_ADD_TC(tp, sigqueue_err);
	ATF_TP_ADD_TC(tp, sigqueue_rt);

	return atf_no_error();
}
