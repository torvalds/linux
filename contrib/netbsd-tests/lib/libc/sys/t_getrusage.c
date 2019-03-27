/* $NetBSD: t_getrusage.c,v 1.5 2017/01/13 20:31:06 christos Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
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
__RCSID("$NetBSD: t_getrusage.c,v 1.5 2017/01/13 20:31:06 christos Exp $");

#include <sys/resource.h>
#include <sys/time.h>

#include <atf-c.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>

static void		work(void);
static void		sighandler(int);

static const size_t	maxiter = 2000;

static void
sighandler(int signo __unused)
{
	/* Nothing. */
}

#ifdef __FreeBSD__
#define	asm __asm
#endif

static void
work(void)
{
	size_t n = UINT16_MAX * 10;

	while (n > 0) {
#ifdef __or1k__
		 asm volatile("l.nop");	/* Do something. */
#elif defined(__ia64__)
		 asm volatile("nop 0"); /* Do something. */
#else
		 asm volatile("nop");	/* Do something. */
#endif
		 n--;
	}
}

ATF_TC(getrusage_err);
ATF_TC_HEAD(getrusage_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test error conditions");
}

ATF_TC_BODY(getrusage_err, tc)
{
	struct rusage ru;

	errno = 0;

	ATF_REQUIRE(getrusage(INT_MAX, &ru) != 0);
	ATF_REQUIRE(errno == EINVAL);

	errno = 0;

	ATF_REQUIRE(getrusage(RUSAGE_SELF, (void *)0) != 0);
	ATF_REQUIRE(errno == EFAULT);
}

ATF_TC(getrusage_sig);
ATF_TC_HEAD(getrusage_sig, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test signal count with getrusage(2)");
}

ATF_TC_BODY(getrusage_sig, tc)
{
	struct rusage ru;
	const long n = 5;
	int i;

	/*
	 * Test that signals are recorded.
	 */
	ATF_REQUIRE(signal(SIGUSR1, sighandler) != SIG_ERR);

	for (i = 0; i < n; i++)
		ATF_REQUIRE(raise(SIGUSR1) == 0);

	(void)memset(&ru, 0, sizeof(struct rusage));
	ATF_REQUIRE(getrusage(RUSAGE_SELF, &ru) == 0);

	if (n != ru.ru_nsignals)
		atf_tc_fail("getrusage(2) did not record signals");
}

ATF_TC(getrusage_utime_back);
ATF_TC_HEAD(getrusage_utime_back, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test bogus values from getrusage(2)");
}

ATF_TC_BODY(getrusage_utime_back, tc)
{
	struct rusage ru1, ru2;
	size_t i;

	/*
	 * Test that two consecutive calls are sane.
	 */
#ifdef __NetBSD__
	atf_tc_expect_fail("PR kern/30115");
#endif

	for (i = 0; i < maxiter; i++) {

		(void)memset(&ru1, 0, sizeof(struct rusage));
		(void)memset(&ru2, 0, sizeof(struct rusage));

		work();

		ATF_REQUIRE(getrusage(RUSAGE_SELF, &ru1) == 0);

		work();

		ATF_REQUIRE(getrusage(RUSAGE_SELF, &ru2) == 0);

		if (timercmp(&ru2.ru_utime, &ru1.ru_utime, <) != 0)
			atf_tc_fail("user time went backwards");
	}

#ifdef __NetBSD__
	atf_tc_fail("anticipated error did not occur");
#endif
}

ATF_TC(getrusage_utime_zero);
ATF_TC_HEAD(getrusage_utime_zero, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test zero utime from getrusage(2)");
}

ATF_TC_BODY(getrusage_utime_zero, tc)
{
	struct rusage ru;
	size_t i;

#ifdef __FreeBSD__
	atf_tc_skip("this testcase passes/fails sporadically on FreeBSD/i386 "
	    "@ r273153 (at least)");
#endif

	/*
	 * Test that getrusage(2) does not return
	 * zero user time for the calling process.
	 *
	 * See also (duplicate) PR port-amd64/41734.
	 */
	atf_tc_expect_fail("PR kern/30115");

	for (i = 0; i < maxiter; i++) {

		work();

		(void)memset(&ru, 0, sizeof(struct rusage));

		ATF_REQUIRE(getrusage(RUSAGE_SELF, &ru) == 0);

		if (ru.ru_utime.tv_sec == 0 && ru.ru_utime.tv_usec == 0)
			atf_tc_fail("zero user time from getrusage(2)");
	}

	atf_tc_fail("anticipated error did not occur");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, getrusage_err);
	ATF_TP_ADD_TC(tp, getrusage_sig);
	ATF_TP_ADD_TC(tp, getrusage_utime_back);
	ATF_TP_ADD_TC(tp, getrusage_utime_zero);

	return atf_no_error();
}
