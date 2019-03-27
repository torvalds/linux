/* $NetBSD: t_floatunditf.c,v 1.6 2014/11/04 00:20:19 justin Exp $ */

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
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

#include <atf-c.h>
#include <inttypes.h>
#include <math.h>

#ifdef __HAVE_LONG_DOUBLE
static const struct {
	uint64_t u64;
	long double ld;
} testcases[] = {
	{ 0xffffffffffffffffULL, 0xf.fffffffffffffffp+60L },
	{ 0xfffffffffffffffeULL, 0xf.ffffffffffffffep+60L },
	{ 0xfffffffffffffffdULL, 0xf.ffffffffffffffdp+60L },
	{ 0xfffffffffffffffcULL, 0xf.ffffffffffffffcp+60L },
	{ 0x7fffffffffffffffULL, 0xf.ffffffffffffffep+59L },
	{ 0x3fffffffffffffffULL, 0xf.ffffffffffffffcp+58L },
	{ 0x1fffffffffffffffULL, 0xf.ffffffffffffff8p+57L },
	{ 0xfffffffffffffffULL, 0xf.ffffffffffffffp+56L },
	{ 0x7ffffffffffffffULL, 0xf.fffffffffffffep+55L },
	{ 0x3ffffffffffffffULL, 0xf.fffffffffffffcp+54L },
	{ 0x1ffffffffffffffULL, 0xf.fffffffffffff8p+53L },
	{ 0xffffffffffffffULL, 0xf.fffffffffffffp+52L },
	{ 0x7fffffffffffffULL, 0xf.ffffffffffffep+51L },
	{ 0x3fffffffffffffULL, 0xf.ffffffffffffcp+50L },
	{ 0x1fffffffffffffULL, 0xf.ffffffffffff8p+49L },
	{ 0xfffffffffffffULL, 0xf.ffffffffffffp+48L },
	{ 0x7ffffffffffffULL, 0xf.fffffffffffep+47L },
	{ 0x3ffffffffffffULL, 0xf.fffffffffffcp+46L },
	{ 0x1ffffffffffffULL, 0xf.fffffffffff8p+45L },
	{ 0xffffffffffffULL, 0xf.fffffffffffp+44L },
	{ 0x7fffffffffffULL, 0xf.ffffffffffep+43L },
	{ 0x3fffffffffffULL, 0xf.ffffffffffcp+42L },
	{ 0x1fffffffffffULL, 0xf.ffffffffff8p+41L },
	{ 0xfffffffffffULL, 0xf.ffffffffffp+40L },
	{ 0x7ffffffffffULL, 0xf.fffffffffep+39L },
	{ 0x3ffffffffffULL, 0xf.fffffffffcp+38L },
	{ 0x1ffffffffffULL, 0xf.fffffffff8p+37L },
	{ 0xffffffffffULL, 0xf.fffffffffp+36L },
	{ 0x7fffffffffULL, 0xf.ffffffffep+35L },
	{ 0x3fffffffffULL, 0xf.ffffffffcp+34L },
	{ 0x1fffffffffULL, 0xf.ffffffff8p+33L },
	{ 0xfffffffffULL, 0xf.ffffffffp+32L },
	{ 0x7ffffffffULL, 0xf.fffffffep+31L },
	{ 0x3ffffffffULL, 0xf.fffffffcp+30L },
	{ 0x1ffffffffULL, 0xf.fffffff8p+29L },
	{ 0xffffffffULL, 0xf.fffffffp+28L },
	{ 0x7fffffffULL, 0xf.ffffffep+27L },
	{ 0x3fffffffULL, 0xf.ffffffcp+26L },
	{ 0x1fffffffULL, 0xf.ffffff8p+25L },
	{ 0xfffffffULL, 0xf.ffffffp+24L },
	{ 0x7ffffffULL, 0xf.fffffep+23L },
	{ 0x3ffffffULL, 0xf.fffffcp+22L },
	{ 0x1ffffffULL, 0xf.fffff8p+21L },
	{ 0xffffffULL, 0xf.fffffp+20L },
	{ 0x7fffffULL, 0xf.ffffep+19L },
	{ 0x3fffffULL, 0xf.ffffcp+18L },
	{ 0x1fffffULL, 0xf.ffff8p+17L },
	{ 0xfffffULL, 0xf.ffffp+16L },
	{ 0x7ffffULL, 0xf.fffep+15L },
	{ 0x3ffffULL, 0xf.fffcp+14L },
	{ 0x1ffffULL, 0xf.fff8p+13L },
	{ 0xffffULL, 0xf.fffp+12L },
	{ 0x7fffULL, 0xf.ffep+11L },
	{ 0x3fffULL, 0xf.ffcp+10L },
	{ 0x1fffULL, 0xf.ff8p+9L },
	{ 0xfffULL, 0xf.ffp+8L },
	{ 0x7ffULL, 0xf.fep+7L },
	{ 0x3ffULL, 0xf.fcp+6L },
	{ 0x1ffULL, 0xf.f8p+5L },
	{ 0xffULL, 0xf.fp+4L },
	{ 0x7fULL, 0xf.ep+3L },
	{ 0x3fULL, 0xf.cp+2L },
	{ 0x1fULL, 0xf.8p+1L },
	{ 0xfULL, 0xfp+0L },
	{ 0x7ULL, 0xep-1L },
	{ 0x3ULL, 0xcp-2L },
	{ 0x1ULL, 0x8p-3L },
};
#endif

ATF_TC(floatunditf);
ATF_TC_HEAD(floatunditf, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Verify that uint64 -> long double conversion works");
}

ATF_TC_BODY(floatunditf, tc)
{
#ifndef __HAVE_LONG_DOUBLE
	atf_tc_skip("Requires long double support");
#else
	size_t i;

#if defined(__FreeBSD__) && defined(__i386__)
	atf_tc_expect_fail("the floating point error on FreeBSD/i386 doesn't "
	    "match the expected floating point error on NetBSD");
#endif

	for (i = 0; i < __arraycount(testcases); ++i)
		ATF_CHECK_MSG(
		    testcases[i].ld == (long double)testcases[i].u64,
		    "#%zu: expected %.20Lf, got %.20Lf\n", i,
		    testcases[i].ld,
		    (long double)testcases[i].u64);
#endif
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, floatunditf);
	return atf_no_error();
}
