/*	$NetBSD: t_backtrace.c,v 1.16 2014/11/04 00:20:19 justin Exp $	*/

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_backtrace.c,v 1.16 2014/11/04 00:20:19 justin Exp $");

#include <atf-c.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>
#include <unistd.h>

#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof(a[0]))
#endif

void myfunc3(size_t ncalls);
void myfunc2(size_t ncalls);
void myfunc1(size_t origcalls, volatile size_t ncalls);
void myfunc(size_t ncalls);

volatile int prevent_inline;

void
myfunc3(size_t ncalls)
{
	static const struct {
		const char *name;
		bool is_optional;
	} frames[] = {
	    { "myfunc", false },
	    { "atfu_backtrace_fmt_basic_body", false },
	    { "atf_tc_run", false }, 
	    { "atf_tp_run", true },
	    { "run_tc", true },
	    { "controlled_main", true },
	    { "atf_tp_main", false },
	    { "main", true },
	    { "___start", true },
	};
	size_t j, nptrs, min_frames, max_frames;
	void *buffer[ncalls + 10];
	char **strings;

	min_frames = 0;
	max_frames = 0;
	for (j = 0; j < __arraycount(frames); ++j) {
		if (!frames[j].is_optional)
			++min_frames;
		++max_frames;
	}
	nptrs = backtrace(buffer, __arraycount(buffer));
	ATF_REQUIRE(nptrs != (size_t)-1);
	strings = backtrace_symbols_fmt(buffer, nptrs, "%n");

	ATF_CHECK(strings != NULL);

	printf("got nptrs=%zu ncalls=%zu (min_frames: %zu, max_frames: %zu)\n",
	    nptrs, ncalls, min_frames, max_frames);
	printf("backtrace is:\n");
	for (j = 0; j < nptrs; j++) {
		printf("#%zu: %s\n", j, strings[j]);
	}

	ATF_REQUIRE(nptrs >= ncalls + 2 + min_frames);
	ATF_REQUIRE(nptrs <= ncalls + 2 + max_frames);
	ATF_CHECK_STREQ(strings[0], "myfunc3");
	ATF_CHECK_STREQ(strings[1], "myfunc2");

	for (j = 2; j < ncalls + 2; j++)
		ATF_CHECK_STREQ(strings[j], "myfunc1");

	for (size_t i = 0; j < nptrs; i++, j++) {
		if (frames[i].is_optional &&
		    strcmp(strings[j], frames[i].name)) {
			--i;
			continue;
		}
		ATF_CHECK_STREQ(strings[j], frames[i].name);
	}

	free(strings);

	if (prevent_inline)
		vfork();
}

void
myfunc2(size_t ncalls)
{
	myfunc3(ncalls);

	if (prevent_inline)
		vfork();
}

void
myfunc1(size_t origcalls, volatile size_t ncalls)
{
	if (ncalls > 1)
		myfunc1(origcalls, ncalls - 1);
	else
		myfunc2(origcalls);

	if (prevent_inline)
		vfork();
}

void
myfunc(size_t ncalls)
{
	myfunc1(ncalls, ncalls);

	if (prevent_inline)
		vfork();
}

ATF_TC(backtrace_fmt_basic);
ATF_TC_HEAD(backtrace_fmt_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test backtrace_fmt(3)");
	atf_tc_set_md_var(tc, "require.files", "/proc/self");
}

ATF_TC_BODY(backtrace_fmt_basic, tc)
{
	myfunc(12);

	if (prevent_inline)
		vfork();
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, backtrace_fmt_basic);

	return atf_no_error();
}
