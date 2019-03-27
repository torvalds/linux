/*	$NetBSD: t_tls_dynamic.c,v 1.3 2012/01/17 20:34:57 joerg Exp $	*/
/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Joerg Sonnenberger.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: t_tls_dynamic.c,v 1.3 2012/01/17 20:34:57 joerg Exp $");

#include <atf-c.h>
#include <pthread.h>
#include <unistd.h>

#ifdef __NetBSD__
#include <sys/tls.h>
#endif

#ifdef __HAVE_NO___THREAD
#define	__thread
#endif

ATF_TC(t_tls_dynamic);

ATF_TC_HEAD(t_tls_dynamic, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test (un)initialized TLS variables in dynamic binaries");
}

void testf_dso_helper(int, int);

extern __thread int var1;
extern __thread int var2;
extern __thread pid_t (*dso_var1)(void);

__thread int *var3 = &optind;
int var4_helper;
__thread int *var4 = &var4_helper;

static void *
testf(void *dummy)
{
	ATF_CHECK_EQ(var1, 1);
	ATF_CHECK_EQ(var2, 0);
	testf_dso_helper(2, 2);
	ATF_CHECK_EQ(var1, 2);
	ATF_CHECK_EQ(var2, 2);
	testf_dso_helper(3, 3);
	ATF_CHECK_EQ(var1, 3);
	ATF_CHECK_EQ(var2, 3);
	ATF_CHECK_EQ(var3, &optind);
	ATF_CHECK_EQ(var4, &var4_helper);
	ATF_CHECK_EQ(dso_var1, getpid);

	return NULL;
}

ATF_TC_BODY(t_tls_dynamic, tc)
{
	pthread_t t;

#ifdef __HAVE_NO___THREAD
	atf_tc_skip("no TLS support on this platform");
#endif

	testf(NULL);

	pthread_create(&t, 0, testf, 0);
	pthread_join(t, NULL);

	pthread_create(&t, 0, testf, 0);
	pthread_join(t, NULL);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, t_tls_dynamic);

	return atf_no_error();
}
