/* $NetBSD: t_fpu.c,v 1.3 2017/01/16 16:27:43 christos Exp $ */

/*
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
__RCSID("$NetBSD: t_fpu.c,v 1.3 2017/01/16 16:27:43 christos Exp $");

/*
 * This is adapted from part of csw/cstest of the MPD implementation by
 * the University of Arizona CS department (http://www.cs.arizona.edu/sr/)
 * which is in the public domain:
 *
 * "The MPD system is in the public domain and you may use and distribute it
 *  as you wish.  We ask that you retain credits referencing the University
 *  of Arizona and that you identify any changes you make.
 *
 *  We can't provide a warranty with MPD; it's up to you to determine its
 *  suitability and reliability for your needs.  We would like to hear of
 *  any problems you encounter but we cannot promise a timely correction."
 *
 * It was changed to use pthread_create() and sched_yield() instead of
 * the internal MPD context switching primitives by Ignatios Souvatzis
 * <is@netbsd.org>.
 */

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <atf-c.h>

#include "h_common.h"

#define N_RECURSE 10

static void recurse(void);

int recursion_depth = 0;
pthread_mutex_t recursion_depth_lock;

static void *
stir(void *p)
{
	double *q = (double *)p;
	double x = *q++;
	double y = *q++;
	double z = *q++;

	for (;;) {
		x = sin ((y = cos (x + y + .4)) - (z = cos (x + z + .6)));
		ATF_REQUIRE_MSG(sched_yield() == 0,
		    "sched_yield failed: %s", strerror(errno));
	}
}

static double
mul3(double x, double y, double z)
{
	ATF_REQUIRE_MSG(sched_yield() == 0,
	    "sched_yield failed: %s", strerror(errno));

	return x * y * z;
}

static void *
bar(void *p)
{
	double d;
	int rc;

	d = mul3(mul3(2., 3., 5.), mul3(7., 11., 13.), mul3(17., 19., 23.));
	ATF_REQUIRE_EQ(d, 223092870.);

	PTHREAD_REQUIRE(pthread_mutex_lock(&recursion_depth_lock));
	rc = recursion_depth++;
	PTHREAD_REQUIRE(pthread_mutex_unlock(&recursion_depth_lock));

	if (rc < N_RECURSE)
		recurse();
	else
		atf_tc_pass();

	/* NOTREACHED */
	return NULL;
}

static void
recurse(void) {
	pthread_t s2;
	PTHREAD_REQUIRE(pthread_create(&s2, 0, bar, 0));
	sleep(20); /* XXX must be long enough for our slowest machine */
}

ATF_TC(fpu);
ATF_TC_HEAD(fpu, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks that thread context switches will leave the "
		"floating point computations unharmed");
}
ATF_TC_BODY(fpu, tc)
{
	double stirseed[] = { 1.7, 3.2, 2.4 };
	pthread_t s5;

	printf("Testing threaded floating point computations...\n");

	PTHREAD_REQUIRE(pthread_mutex_init(&recursion_depth_lock, 0));

	PTHREAD_REQUIRE(pthread_create(&s5, 0, stir, stirseed));
	recurse();

	atf_tc_fail("exiting from main");
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, fpu);

	return atf_no_error();
}
