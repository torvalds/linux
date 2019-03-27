/* $NetBSD: t_wait_noproc.c,v 1.5 2016/11/09 17:50:19 kamil Exp $ */

/*-
 * Copyright (c) 2016 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_wait_noproc.c,v 1.5 2016/11/09 17:50:19 kamil Exp $");

#include <sys/wait.h>
#include <sys/resource.h>

#include <errno.h>
#include <stdio.h>

#include <atf-c.h>

#ifndef TWAIT_OPTION
#define TWAIT_OPTION 0
#endif

#if TWAIT_OPTION == 0
ATF_TC(wait);
ATF_TC_HEAD(wait, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that wait(2) returns ECHILD for no child");
}

ATF_TC_BODY(wait, tc)
{
	ATF_REQUIRE_ERRNO(ECHILD, wait(NULL) == -1);
}
#endif

ATF_TC(waitpid);
ATF_TC_HEAD(waitpid, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that waitpid(2) returns ECHILD for WAIT_ANY and option %s",
	    ___STRING(TWAIT_OPTION));
}

ATF_TC_BODY(waitpid, tc)
{
	ATF_REQUIRE_ERRNO(ECHILD, waitpid(WAIT_ANY, NULL, TWAIT_OPTION) == -1);
}

ATF_TC(waitid);
ATF_TC_HEAD(waitid, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that waitid(2) returns ECHILD for P_ALL and option %s",
	    ___STRING(TWAIT_OPTION));
}

ATF_TC_BODY(waitid, tc)
{
	ATF_REQUIRE_ERRNO(ECHILD,
	    waitid(P_ALL, 0, NULL,
	        WTRAPPED | WEXITED | TWAIT_OPTION) == -1);
}

ATF_TC(wait3);
ATF_TC_HEAD(wait3, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that wait3(2) returns ECHILD for no child");
}

ATF_TC_BODY(wait3, tc)
{
	ATF_REQUIRE_ERRNO(ECHILD, wait3(NULL, TWAIT_OPTION, NULL) == -1);
}

ATF_TC(wait4);
ATF_TC_HEAD(wait4, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that wait4(2) returns ECHILD for WAIT_ANY and option %s",
	    ___STRING(TWAIT_OPTION));
}

ATF_TC_BODY(wait4, tc)
{
	ATF_REQUIRE_ERRNO(ECHILD,
	    wait4(WAIT_ANY, NULL, TWAIT_OPTION, NULL) == -1);
}

ATF_TC(wait6);
ATF_TC_HEAD(wait6, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that wait6(2) returns ECHILD for P_ALL and option %s",
	    ___STRING(TWAIT_OPTION));
}

ATF_TC_BODY(wait6, tc)
{
	ATF_REQUIRE_ERRNO(ECHILD,
	    wait6(P_ALL, 0, NULL,
	        WTRAPPED | WEXITED | TWAIT_OPTION, NULL, NULL) == -1);
}

/*
 * Generator of valid combinations of options
 * Usage: i = 0; while ((o = get_options_wait6(i++)) != -1) {}
 */
static int
get_options6(size_t pos)
{
	int rv = 0;
	size_t n;

	/*
	 * waitid(2) must specify at least one of WEXITED, WUNTRACED,
	 * WSTOPPED, WTRAPPED or WCONTINUED. Single option WNOWAIT
	 * isn't valid.
	 */

	const int matrix[] = {
		WNOWAIT,	/* First in order to blacklist it easily */
		WEXITED,
		WUNTRACED,
		WSTOPPED,	/* SUS compatibility, equal to WUNTRACED */
		WTRAPPED,
		WCONTINUED
	};

	const size_t M = (1 << __arraycount(matrix)) - 1;

	/* Skip empty and sole WNOWAIT option */
	pos+=2;

	if (pos > M)
		return -1;

	for (n = 0; n < __arraycount(matrix); n++) {
		if (pos & __BIT(n))
			rv |= matrix[n];
	}

	return rv;
}

/*
 * Generator of valid combinations of options
 * Usage: i = 0; while ((o = get_options_wait4(i++)) != -1) {}
 */
static int
get_options4(size_t pos)
{
	int rv = 0;
	size_t n;

	const int special[] = {
		0,
#ifdef	__NetBSD__
		WALLSIG,
		WALTSIG,
		__WALL,		/* Linux compatibility, equal to WALLSIG */
		__WCLONE	/* Linux compatibility, equal to WALTSIG */
#endif
	};

	const int matrix[] = {
		WNOWAIT,
		WEXITED,
		WUNTRACED,
		WSTOPPED,	/* SUS compatibility, equal to WUNTRACED */
		WTRAPPED,
		WCONTINUED
	};

	const size_t M = (1 << __arraycount(special)) - 1;

	if (pos < __arraycount(special))
		return special[pos];

	pos -= __arraycount(special);

	++pos; /* Don't start with empty mask */

	if (pos > M)
		return -1;

	for (n = 0; n < __arraycount(special); n++) {
		if (pos & __BIT(n))
			rv |= matrix[n];
	}

	return rv;
}

ATF_TC(waitpid_options);
ATF_TC_HEAD(waitpid_options, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that waitpid(2) returns ECHILD for WAIT_ANY and valid "
	    "combination of options with%s WNOHANG",
	    TWAIT_OPTION == 0 ? "out" : "");
}

ATF_TC_BODY(waitpid_options, tc)
{
	size_t i = 0;
	int o;

	while((o = get_options4(i++)) != -1) {
		printf("Testing waitpid(2) with options %x\n", o);

		ATF_REQUIRE_ERRNO(ECHILD,
		    waitpid(WAIT_ANY, NULL, o | TWAIT_OPTION) == -1);
	}
}

ATF_TC(waitid_options);
ATF_TC_HEAD(waitid_options, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that waitid(2) returns ECHILD for P_ALL and valid "
	    "combination of options with%s WNOHANG",
	    TWAIT_OPTION == 0 ? "out" : "");
}

ATF_TC_BODY(waitid_options, tc)
{
	size_t i = 0;
	int o;

	while((o = get_options6(i++)) != -1) {
		printf("Testing waitid(2) with options %x\n", o);

		ATF_REQUIRE_ERRNO(ECHILD,
		    waitid(P_ALL, 0, NULL, o | TWAIT_OPTION) == -1);
	}
}

ATF_TC(wait3_options);
ATF_TC_HEAD(wait3_options, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that wait3(2) returns ECHILD for no child");
}

ATF_TC_BODY(wait3_options, tc)
{
	size_t i = 0;
	int o;

	while((o = get_options4(i++)) != -1) {
		printf("Testing wait3(2) with options %x\n", o);

		ATF_REQUIRE_ERRNO(ECHILD,
		    wait3(NULL, o | TWAIT_OPTION, NULL) == -1);
	}
}

ATF_TC(wait4_options);
ATF_TC_HEAD(wait4_options, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that wait4(2) returns ECHILD for WAIT_ANY and option %s",
	    ___STRING(TWAIT_OPTION));
}

ATF_TC_BODY(wait4_options, tc)
{
	size_t i = 0;
	int o;

	while((o = get_options4(i++)) != -1) {
		printf("Testing wait4(2) with options %x\n", o);

		ATF_REQUIRE_ERRNO(ECHILD,
		    wait4(WAIT_ANY, NULL, o | TWAIT_OPTION, NULL) == -1);
	}
}

ATF_TC(wait6_options);
ATF_TC_HEAD(wait6_options, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that wait6(2) returns ECHILD for P_ALL and option %s",
	    ___STRING(TWAIT_OPTION));
}

ATF_TC_BODY(wait6_options, tc)
{
	size_t i = 0;
	int o;

	while((o = get_options6(i++)) != -1) {
		printf("Testing wait6(2) with options %x\n", o);

		ATF_REQUIRE_ERRNO(ECHILD,
		    wait6(P_ALL, 0, NULL, o | TWAIT_OPTION, NULL, NULL) == -1);
	}
}

ATF_TP_ADD_TCS(tp)
{

#if TWAIT_OPTION == 0
	ATF_TP_ADD_TC(tp, wait);
#endif
	ATF_TP_ADD_TC(tp, waitpid);
	ATF_TP_ADD_TC(tp, waitid);
	ATF_TP_ADD_TC(tp, wait3);
	ATF_TP_ADD_TC(tp, wait4);
	ATF_TP_ADD_TC(tp, wait6);

	ATF_TP_ADD_TC(tp, waitpid_options);
	ATF_TP_ADD_TC(tp, waitid_options);
	ATF_TP_ADD_TC(tp, wait3_options);
	ATF_TP_ADD_TC(tp, wait4_options);
	ATF_TP_ADD_TC(tp, wait6_options);

	return atf_no_error();
}
