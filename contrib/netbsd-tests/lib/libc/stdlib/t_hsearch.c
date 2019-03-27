/* $NetBSD: t_hsearch.c,v 1.4 2014/07/20 20:17:21 christos Exp $ */

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

/*
 * Copyright (c) 2001 Christopher G. Demetriou
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *          This product includes software developed for the
 *          NetBSD Project.  See http://www.NetBSD.org/ for
 *          information about NetBSD.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * <<Id: LICENSE,v 1.2 2000/06/14 15:57:33 cgd Exp>>
 */

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_hsearch.c,v 1.4 2014/07/20 20:17:21 christos Exp $");

#include <errno.h>
#include <search.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <atf-c.h>

#define REQUIRE_ERRNO(x) ATF_REQUIRE_MSG(x, "%s", strerror(errno))

ATF_TC(hsearch_basic);
ATF_TC_HEAD(hsearch_basic, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks basic insertions and searching");
}

ATF_TC_BODY(hsearch_basic, tc)
{
	ENTRY e, *ep;
	char ch[2];
	int i;

	REQUIRE_ERRNO(hcreate(16) != 0);

	/* ch[1] should be constant from here on down. */
	ch[1] = '\0';

	/* Basic insertions.  Check enough that there'll be collisions. */
	for (i = 0; i < 26; i++) {
		ch[0] = 'a' + i;
		e.key = strdup(ch);	/* ptr to provided key is kept! */
		ATF_REQUIRE(e.key != NULL);
		e.data = (void *)(intptr_t)i;

		ep = hsearch(e, ENTER);

		ATF_REQUIRE(ep != NULL);
		ATF_REQUIRE_STREQ(ep->key, ch);
		ATF_REQUIRE_EQ((intptr_t)ep->data, i);
	}

	/* e.key should be constant from here on down. */
	e.key = ch;

	/* Basic lookups. */
	for (i = 0; i < 26; i++) {
		ch[0] = 'a' + i;

		ep = hsearch(e, FIND);

		ATF_REQUIRE(ep != NULL);
		ATF_REQUIRE_STREQ(ep->key, ch);
		ATF_REQUIRE_EQ((intptr_t)ep->data, i);
	}

#ifdef __NetBSD__
	hdestroy1(free, NULL);
#else
	hdestroy();
#endif
}

ATF_TC(hsearch_duplicate);
ATF_TC_HEAD(hsearch_duplicate, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks that inserting duplicate "
	    "doesn't overwrite existing data");
}

ATF_TC_BODY(hsearch_duplicate, tc)
{
	ENTRY e, *ep;

	REQUIRE_ERRNO(hcreate(16));

	e.key = __UNCONST("a");
	e.data = (void *)(intptr_t) 0;

	ep = hsearch(e, ENTER);

	ATF_REQUIRE(ep != NULL);
	ATF_REQUIRE_STREQ(ep->key, "a");
	ATF_REQUIRE_EQ((intptr_t)ep->data, 0);

	e.data = (void *)(intptr_t)12345;

	ep = hsearch(e, ENTER);
	ep = hsearch(e, FIND);

	ATF_REQUIRE(ep != NULL);
	ATF_REQUIRE_STREQ(ep->key, "a");
	ATF_REQUIRE_EQ((intptr_t)ep->data, 0);

	hdestroy();
}

ATF_TC(hsearch_nonexistent);
ATF_TC_HEAD(hsearch_nonexistent, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Checks searching for non-existent entry");
}

ATF_TC_BODY(hsearch_nonexistent, tc)
{
	ENTRY e, *ep;

	REQUIRE_ERRNO(hcreate(16));

	e.key = __UNCONST("A");
	ep = hsearch(e, FIND);
	ATF_REQUIRE_EQ(ep, NULL);

	hdestroy();
}

ATF_TC(hsearch_two);
ATF_TC_HEAD(hsearch_two, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Checks that searching doesn't overwrite previous search results");
}

ATF_TC_BODY(hsearch_two, tc)
{
	ENTRY e, *ep, *ep2;

	REQUIRE_ERRNO(hcreate(16));

	e.key = __UNCONST("a");
	e.data = (void*)(intptr_t)0;

	ep = hsearch(e, ENTER);

	ATF_REQUIRE(ep != NULL);
	ATF_REQUIRE_STREQ(ep->key, "a");
	ATF_REQUIRE_EQ((intptr_t)ep->data, 0);

	e.key = __UNCONST("b");
	e.data = (void*)(intptr_t)1;

	ep = hsearch(e, ENTER);

	ATF_REQUIRE(ep != NULL);
	ATF_REQUIRE_STREQ(ep->key, "b");
	ATF_REQUIRE_EQ((intptr_t)ep->data, 1);

	e.key = __UNCONST("a");
	ep = hsearch(e, FIND);

	e.key = __UNCONST("b");
	ep2 = hsearch(e, FIND);

	ATF_REQUIRE(ep != NULL);
	ATF_REQUIRE_STREQ(ep->key, "a");
	ATF_REQUIRE_EQ((intptr_t)ep->data, 0);

	ATF_REQUIRE(ep2 != NULL);
	ATF_REQUIRE_STREQ(ep2->key, "b");
	ATF_REQUIRE_EQ((intptr_t)ep2->data, 1);

	hdestroy();
}

#if defined(__FreeBSD__) && 1100027 <= __FreeBSD_version
ATF_TC(hsearch_r_basic);
ATF_TC_HEAD(hsearch_r_basic, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks basic insertions and searching");
}

ATF_TC_BODY(hsearch_r_basic, tc)
{
	ENTRY e, *ep;
	char ch[2];
	int i;
	struct hsearch_data t;

	REQUIRE_ERRNO(hcreate_r(16, &t) != 0);

	/* ch[1] should be constant from here on down. */
	ch[1] = '\0';

	/* Basic insertions.  Check enough that there'll be collisions. */
	for (i = 0; i < 26; i++) {
		ch[0] = 'a' + i;
		e.key = strdup(ch);	/* ptr to provided key is kept! */
		ATF_REQUIRE(e.key != NULL);
		e.data = (void *)(intptr_t)i;

		ATF_REQUIRE(hsearch_r(e, ENTER, &ep, &t) == 1);
		ATF_REQUIRE(ep != NULL);
		ATF_REQUIRE_STREQ(ep->key, ch);
		ATF_REQUIRE_EQ((intptr_t)ep->data, i);
	}

	/* e.key should be constant from here on down. */
	e.key = ch;

	/* Basic lookups. */
	for (i = 0; i < 26; i++) {
		ch[0] = 'a' + i;

		ATF_REQUIRE(hsearch_r(e, FIND, &ep, &t) == 1);
		ATF_REQUIRE(ep != NULL);
		ATF_REQUIRE_STREQ(ep->key, ch);
		ATF_REQUIRE_EQ((intptr_t)ep->data, i);
	}

#ifdef __NetBSD__
	hdestroy1_r(&t, free, NULL);
#else
	hdestroy_r(&t);
#endif
}
#endif

#if defined(__FreeBSD__) && 1100027 <= __FreeBSD_version
ATF_TC(hsearch_r_duplicate);
ATF_TC_HEAD(hsearch_r_duplicate, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks that inserting duplicate "
	    "doesn't overwrite existing data");
}

ATF_TC_BODY(hsearch_r_duplicate, tc)
{
	ENTRY e, *ep;
	struct hsearch_data t;

	REQUIRE_ERRNO(hcreate_r(16, &t));

	e.key = __UNCONST("a");
	e.data = (void *)(intptr_t) 0;

	ATF_REQUIRE(hsearch_r(e, ENTER, &ep, &t) == 1);
	ATF_REQUIRE(ep != NULL);
	ATF_REQUIRE_STREQ(ep->key, "a");
	ATF_REQUIRE_EQ((intptr_t)ep->data, 0);

	e.data = (void *)(intptr_t)12345;

	ATF_REQUIRE(hsearch_r(e, ENTER, &ep, &t) == 1);
	ATF_REQUIRE(hsearch_r(e, FIND, &ep, &t) == 1);

	ATF_REQUIRE(ep != NULL);
	ATF_REQUIRE_STREQ(ep->key, "a");
	ATF_REQUIRE_EQ((intptr_t)ep->data, 0);

	hdestroy_r(&t);
}

ATF_TC(hsearch_r_nonexistent);
ATF_TC_HEAD(hsearch_r_nonexistent, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Checks searching for non-existent entry");
}

ATF_TC_BODY(hsearch_r_nonexistent, tc)
{
	ENTRY e, *ep;
	struct hsearch_data t;

	REQUIRE_ERRNO(hcreate_r(16, &t));

#ifdef __FreeBSD__
	atf_tc_expect_fail("behavior doesn't match docs; see bug # 216872");
#endif
	e.key = __UNCONST("A");
	ATF_REQUIRE(hsearch_r(e, FIND, &ep, &t) == 1);
	ATF_REQUIRE_EQ(ep, NULL);

	hdestroy_r(&t);
}

ATF_TC(hsearch_r_two);
ATF_TC_HEAD(hsearch_r_two, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Checks that searching doesn't overwrite previous search results");
}

ATF_TC_BODY(hsearch_r_two, tc)
{
	ENTRY e, *ep, *ep2;
	struct hsearch_data t;

	REQUIRE_ERRNO(hcreate_r(16, &t));

	e.key = __UNCONST("a");
	e.data = (void*)(intptr_t)0;

	ATF_REQUIRE(hsearch_r(e, ENTER, &ep, &t) == 1);
	ATF_REQUIRE(ep != NULL);
	ATF_REQUIRE_STREQ(ep->key, "a");
	ATF_REQUIRE_EQ((intptr_t)ep->data, 0);

	e.key = __UNCONST("b");
	e.data = (void*)(intptr_t)1;

	ATF_REQUIRE(hsearch_r(e, ENTER, &ep, &t) == 1);
	ATF_REQUIRE(ep != NULL);
	ATF_REQUIRE_STREQ(ep->key, "b");
	ATF_REQUIRE_EQ((intptr_t)ep->data, 1);

	e.key = __UNCONST("a");
	ATF_REQUIRE(hsearch_r(e, FIND, &ep, &t) == 1);

	e.key = __UNCONST("b");
	ATF_REQUIRE(hsearch_r(e, FIND, &ep2, &t) == 1);

	ATF_REQUIRE(ep != NULL);
	ATF_REQUIRE_STREQ(ep->key, "a");
	ATF_REQUIRE_EQ((intptr_t)ep->data, 0);

	ATF_REQUIRE(ep2 != NULL);
	ATF_REQUIRE_STREQ(ep2->key, "b");
	ATF_REQUIRE_EQ((intptr_t)ep2->data, 1);

	hdestroy_r(&t);
}
#endif

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, hsearch_basic);
	ATF_TP_ADD_TC(tp, hsearch_duplicate);
	ATF_TP_ADD_TC(tp, hsearch_nonexistent);
	ATF_TP_ADD_TC(tp, hsearch_two);

#if defined(__FreeBSD__) && 1100027 <= __FreeBSD_version
	ATF_TP_ADD_TC(tp, hsearch_r_basic);
	ATF_TP_ADD_TC(tp, hsearch_r_duplicate);
	ATF_TP_ADD_TC(tp, hsearch_r_nonexistent);
	ATF_TP_ADD_TC(tp, hsearch_r_two);
#endif

	return atf_no_error();
}
