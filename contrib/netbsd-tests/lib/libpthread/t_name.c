/* $NetBSD: t_name.c,v 1.1 2010/07/16 15:42:53 jmmv Exp $ */

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
__RCSID("$NetBSD: t_name.c,v 1.1 2010/07/16 15:42:53 jmmv Exp $");

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#include "h_common.h"

#define NAME_TOO_LONG   "12345678901234567890123456789012"  /* 32 chars */
#define NAME_JUST_RIGHT "1234567890123456789012345678901"   /* 31 chars */

#define CONST_NAME "xyzzy"
char non_const_name[] = CONST_NAME;

static void *
threadfunc(void *arg)
{
	pthread_t self = pthread_self();
	char retname[32];

	PTHREAD_REQUIRE(pthread_getname_np(self, retname, sizeof(retname)));
	ATF_REQUIRE_STREQ(retname, NAME_JUST_RIGHT);

	PTHREAD_REQUIRE(pthread_setname_np(self, non_const_name, NULL));
	(void) memset(non_const_name, 0, sizeof(non_const_name));
	PTHREAD_REQUIRE(pthread_getname_np(self, retname, sizeof(retname)));
	ATF_REQUIRE_STREQ(retname, CONST_NAME);

	return NULL;
}

ATF_TC(name);
ATF_TC_HEAD(name, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks pthread_{,attr}_{get,set}name_np() API");
}
ATF_TC_BODY(name, tc)
{
	pthread_t thr, self = pthread_self();
	pthread_attr_t attr;
	char retname[32];

	PTHREAD_REQUIRE(pthread_attr_init(&attr));
	PTHREAD_REQUIRE(pthread_attr_getname_np(&attr, retname,
		sizeof(retname), NULL));
	ATF_REQUIRE_EQ(retname[0], '\0');
	ATF_REQUIRE_EQ(pthread_attr_setname_np(&attr, NAME_TOO_LONG, NULL), EINVAL);
	PTHREAD_REQUIRE(pthread_attr_setname_np(&attr, "%s",
	    __UNCONST(NAME_JUST_RIGHT)));

	(void) strcpy(retname, "foo");
	PTHREAD_REQUIRE(pthread_getname_np(self, retname, sizeof(retname)));
	ATF_REQUIRE_EQ(retname[0], '\0');

	PTHREAD_REQUIRE(pthread_create(&thr, &attr, threadfunc, NULL));
	PTHREAD_REQUIRE(pthread_join(thr, NULL));

	ATF_REQUIRE_EQ(pthread_getname_np(thr, retname, sizeof(retname)), ESRCH);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, name);

	return atf_no_error();
}
