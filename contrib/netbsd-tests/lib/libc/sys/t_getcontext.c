/* $NetBSD: t_getcontext.c,v 1.3 2011/07/14 04:59:14 jruoho Exp $ */

/*-
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
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
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_getcontext.c,v 1.3 2011/07/14 04:59:14 jruoho Exp $");

#include <atf-c.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ucontext.h>

#define STACKSZ (10*1024)
#define DEPTH 3

static int calls;

static void
run(int n, ...)
{
	va_list va;
	int i, ia;

	ATF_REQUIRE_EQ(n, DEPTH - calls - 1);

	va_start(va, n);
#ifdef __FreeBSD__
#if defined(__amd64__) || defined(__sparc64__)
	for (i = 0; i < 5; i++) {
#elif defined(__aarch64__) || defined(__riscv)
	for (i = 0; i < 7; i++) {
#else
	for (i = 0; i < 9; i++) {
#endif
#else
	for (i = 0; i < 9; i++) {
#endif
		ia = va_arg(va, int);
		ATF_REQUIRE_EQ(i, ia);
	}
	va_end(va);

	calls++;
}

ATF_TC(getcontext_err);
ATF_TC_HEAD(getcontext_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from getcontext(2)");
}

ATF_TC_BODY(getcontext_err, tc)
{

	errno = 0;
	ATF_REQUIRE_ERRNO(EFAULT, getcontext((void *)-1) == -1);
}

ATF_TC(setcontext_err);
ATF_TC_HEAD(setcontext_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from setcontext(2)");
}

ATF_TC_BODY(setcontext_err, tc)
{

	errno = 0;
	ATF_REQUIRE_ERRNO(EFAULT, setcontext((void *)-1) == -1);
}

ATF_TC(setcontext_link);
ATF_TC_HEAD(setcontext_link, tc)
{

	atf_tc_set_md_var(tc, "descr",
	"Checks get/make/setcontext(), context linking via uc_link(), "
	    "and argument passing to the new context");
}

ATF_TC_BODY(setcontext_link, tc)
{
	ucontext_t uc[DEPTH];
	ucontext_t save;
	volatile int i = 0; /* avoid longjmp clobbering */

	for (i = 0; i < DEPTH; ++i) {
		ATF_REQUIRE_EQ(getcontext(&uc[i]), 0);

		uc[i].uc_stack.ss_sp = malloc(STACKSZ);
		uc[i].uc_stack.ss_size = STACKSZ;
		uc[i].uc_link = (i > 0) ? &uc[i - 1] : &save;

#ifdef __FreeBSD__
#if defined(__amd64__) || defined(__sparc64__)
		/*
		 * FreeBSD/amd64 and FreeBSD/sparc64 only permit up to
		 * 6 arguments.
		 */
		makecontext(&uc[i], (void *)run, 6, i,
			0, 1, 2, 3, 4);
#elif defined(__aarch64__) || defined(__riscv)
		/*
		 * FreeBSD/arm64 and FreeBSD/riscv64 only permit up to
		 * 8 arguments.
		 */
		makecontext(&uc[i], (void *)run, 8, i,
			0, 1, 2, 3, 4, 5, 6);
#else
		makecontext(&uc[i], (void *)run, 10, i,
			0, 1, 2, 3, 4, 5, 6, 7, 8);
#endif
#else
		makecontext(&uc[i], (void *)run, 10, i,
			0, 1, 2, 3, 4, 5, 6, 7, 8);
#endif
	}

	ATF_REQUIRE_EQ(getcontext(&save), 0);

	if (calls == 0)
		ATF_REQUIRE_EQ(setcontext(&uc[DEPTH-1]), 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, getcontext_err);
	ATF_TP_ADD_TC(tp, setcontext_err);
	ATF_TP_ADD_TC(tp, setcontext_link);

	return atf_no_error();
}
