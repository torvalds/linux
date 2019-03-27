/* $NetBSD: t_random.c,v 1.3 2012/03/29 08:56:06 jruoho Exp $ */

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_random.c,v 1.3 2012/03/29 08:56:06 jruoho Exp $");

#include <atf-c.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * TODO: Add some general RNG tests (cf. the famous "diehard" tests?).
 */

ATF_TC(random_same);
ATF_TC_HEAD(random_same, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test that random(3) does not always return the same "
	    "value when the seed is initialized to zero");
}

#define MAX_ITER 10

ATF_TC_BODY(random_same, tc)
{
	long buf[MAX_ITER];
	size_t i, j;

	/*
	 * See CVE-2012-1577.
	 */
	srandom(0);

	for (i = 0; i < __arraycount(buf); i++) {

		buf[i] = random();

		for (j = 0; j < i; j++) {

			(void)fprintf(stderr, "i = %zu, j = %zu: "
			    "%ld vs. %ld\n", i, j, buf[i], buf[j]);

			ATF_CHECK(buf[i] != buf[j]);
		}
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, random_same);

	return atf_no_error();
}
