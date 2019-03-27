/* $NetBSD: t_fenv.c,v 1.3 2015/12/22 14:20:59 christos Exp $ */

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Martin Husemann.
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
__RCSID("$NetBSD: t_fenv.c,v 1.3 2015/12/22 14:20:59 christos Exp $");

#include <atf-c.h>

#include <fenv.h>
#ifdef __HAVE_FENV

#include <ieeefp.h>
#include <stdlib.h>


#if __arm__ && !__SOFTFP__
	/*
	 * Some NEON fpus do not implement IEEE exception handling,
	 * skip these tests if running on them and compiled for
	 * hard float.
	 */
#define	FPU_EXC_PREREQ()						\
	if (0 == fpsetmask(fpsetmask(FP_X_INV)))			\
		atf_tc_skip("FPU does not implement exception handling");

	/*
	 * Same as above: some don't allow configuring the rounding mode.
	 */
#define	FPU_RND_PREREQ()						\
	if (0 == fpsetround(fpsetround(FP_RZ)))				\
		atf_tc_skip("FPU does not implement configurable "	\
		    "rounding modes");
#endif

#ifndef FPU_EXC_PREREQ
#define	FPU_EXC_PREREQ()	/* nothing */
#endif
#ifndef FPU_RND_PREREQ
#define	FPU_RND_PREREQ()	/* nothing */
#endif


ATF_TC(fegetround);

ATF_TC_HEAD(fegetround, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "verify the fegetround() function agrees with the legacy "
	    "fpsetround");
}

ATF_TC_BODY(fegetround, tc)
{
	FPU_RND_PREREQ();

	fpsetround(FP_RZ);
	ATF_CHECK(fegetround() == FE_TOWARDZERO);
	fpsetround(FP_RM);
	ATF_CHECK(fegetround() == FE_DOWNWARD);
	fpsetround(FP_RN);
	ATF_CHECK(fegetround() == FE_TONEAREST);
	fpsetround(FP_RP);
	ATF_CHECK(fegetround() == FE_UPWARD);
}

ATF_TC(fesetround);

ATF_TC_HEAD(fesetround, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "verify the fesetround() function agrees with the legacy "
	    "fpgetround");
}

ATF_TC_BODY(fesetround, tc)
{
	FPU_RND_PREREQ();

	fesetround(FE_TOWARDZERO);
	ATF_CHECK(fpgetround() == FP_RZ);
	fesetround(FE_DOWNWARD);
	ATF_CHECK(fpgetround() == FP_RM);
	fesetround(FE_TONEAREST);
	ATF_CHECK(fpgetround() == FP_RN);
	fesetround(FE_UPWARD);
	ATF_CHECK(fpgetround() == FP_RP);
}

ATF_TC(fegetexcept);

ATF_TC_HEAD(fegetexcept, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "verify the fegetexcept() function agrees with the legacy "
	    "fpsetmask()");
}

ATF_TC_BODY(fegetexcept, tc)
{
	FPU_EXC_PREREQ();

	fpsetmask(0);
	ATF_CHECK(fegetexcept() == 0);

	fpsetmask(FP_X_INV|FP_X_DZ|FP_X_OFL|FP_X_UFL|FP_X_IMP);
	ATF_CHECK(fegetexcept() == (FE_INVALID|FE_DIVBYZERO|FE_OVERFLOW
	    |FE_UNDERFLOW|FE_INEXACT));

	fpsetmask(FP_X_INV);
	ATF_CHECK(fegetexcept() == FE_INVALID);

	fpsetmask(FP_X_DZ);
	ATF_CHECK(fegetexcept() == FE_DIVBYZERO);

	fpsetmask(FP_X_OFL);
	ATF_CHECK(fegetexcept() == FE_OVERFLOW);

	fpsetmask(FP_X_UFL);
	ATF_CHECK(fegetexcept() == FE_UNDERFLOW);

	fpsetmask(FP_X_IMP);
	ATF_CHECK(fegetexcept() == FE_INEXACT);
}

ATF_TC(feenableexcept);

ATF_TC_HEAD(feenableexcept, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "verify the feenableexcept() function agrees with the legacy "
	    "fpgetmask()");
}

ATF_TC_BODY(feenableexcept, tc)
{
	FPU_EXC_PREREQ();

	fedisableexcept(FE_ALL_EXCEPT);
	ATF_CHECK(fpgetmask() == 0);

	feenableexcept(FE_UNDERFLOW);
	ATF_CHECK(fpgetmask() == FP_X_UFL);

	fedisableexcept(FE_ALL_EXCEPT);
	feenableexcept(FE_OVERFLOW);
	ATF_CHECK(fpgetmask() == FP_X_OFL);

	fedisableexcept(FE_ALL_EXCEPT);
	feenableexcept(FE_DIVBYZERO);
	ATF_CHECK(fpgetmask() == FP_X_DZ);

	fedisableexcept(FE_ALL_EXCEPT);
	feenableexcept(FE_INEXACT);
	ATF_CHECK(fpgetmask() == FP_X_IMP);

	fedisableexcept(FE_ALL_EXCEPT);
	feenableexcept(FE_INVALID);
	ATF_CHECK(fpgetmask() == FP_X_INV);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, fegetround);
	ATF_TP_ADD_TC(tp, fesetround);
	ATF_TP_ADD_TC(tp, fegetexcept);
	ATF_TP_ADD_TC(tp, feenableexcept);

	return atf_no_error();
}

#else	/* no fenv.h support */

ATF_TC(t_nofenv);

ATF_TC_HEAD(t_nofenv, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "dummy test case - no fenv.h support");
}


ATF_TC_BODY(t_nofenv, tc)
{
	atf_tc_skip("no fenv.h support on this architecture");
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, t_nofenv);
	return atf_no_error();
}

#endif
