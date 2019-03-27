/*	$NetBSD: t_tls_static.c,v 1.2 2012/01/17 20:34:57 joerg Exp $	*/
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
__RCSID("$NetBSD: t_tls_static.c,v 1.2 2012/01/17 20:34:57 joerg Exp $");

#include <atf-c.h>
#include <pthread.h>

#ifdef __NetBSD__
#include <sys/tls.h>
#endif

#ifdef __HAVE_NO___THREAD
#define	__thread
#endif

ATF_TC(t_tls_static);

ATF_TC_HEAD(t_tls_static, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test (un)initialized TLS variables in static binaries");
}

void testf_helper(void);

__thread int var1 = 1;
__thread int var2;

static void *
testf(void *dummy)
{
	ATF_CHECK_EQ(var1, 1);
	ATF_CHECK_EQ(var2, 0);
	testf_helper();
	ATF_CHECK_EQ(var1, -1);
	ATF_CHECK_EQ(var2, -1);

	return NULL;
}

ATF_TC_BODY(t_tls_static, tc)
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
	ATF_TP_ADD_TC(tp, t_tls_static);

	return atf_no_error();
}
