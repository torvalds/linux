/* $NetBSD: t_getprotoent.c,v 1.2 2012/04/04 10:03:53 joerg Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_getprotoent.c,v 1.2 2012/04/04 10:03:53 joerg Exp $");

#include <atf-c.h>
#include <netdb.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static const struct {
	const char *name;
	int	    number;
} protos[] = {

	{ "icmp", 1 },	{ "tcp", 6 },	{ "udp", 17 },	{ "gre", 47 },
	{ "esp", 50 },	{ "ah", 51 },	{ "sctp", 132},	{ "ipv6-icmp", 58 }
};

ATF_TC(endprotoent_rewind);
ATF_TC_HEAD(endprotoent_rewind, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check that endprotoent(3) rewinds");
}

ATF_TC_BODY(endprotoent_rewind, tc)
{
	struct protoent *p;
	int i = 0;

	setprotoent(0);

	while ((p = getprotoent()) != NULL && i <= 10) {
		ATF_REQUIRE(p->p_proto == i);
		i++;
	}

	i = 0;
	endprotoent();

	while ((p = getprotoent()) != NULL && i <= 10) {
		ATF_REQUIRE(p->p_proto == i);
		i++;
	}

	endprotoent();
}

ATF_TC(getprotobyname_basic);
ATF_TC_HEAD(getprotobyname_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A naive test of getprotobyname(3)");
}

ATF_TC_BODY(getprotobyname_basic, tc)
{
	struct protoent *p;
	size_t i;

	for (i = 0; i < __arraycount(protos); i++) {

		p = getprotobyname(protos[i].name);

		ATF_REQUIRE(p != NULL);
		ATF_REQUIRE(p->p_proto == protos[i].number);
		ATF_REQUIRE(strcmp(p->p_name, protos[i].name) == 0);
	}

	endprotoent();
}

ATF_TC(getprotobyname_err);
ATF_TC_HEAD(getprotobyname_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test EOF from getprotobyname(3)");
}

ATF_TC_BODY(getprotobyname_err, tc)
{
	static const char * name[] =
	    { "xxx", "yyy", "xyz", ".as.d}9x.._?!!#\xa4,\xa8^//&%%,",
	      "0", "", "tCp", "uDp", "t c p", "tcp ", " tcp" };

	size_t i;

	for (i = 0; i < __arraycount(name); i++)
		ATF_REQUIRE(getprotobyname(name[i]) == NULL);

	endprotoent();
}

ATF_TC(getprotobynumber_basic);
ATF_TC_HEAD(getprotobynumber_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A naive test of getprotobynumber(3)");
}

ATF_TC_BODY(getprotobynumber_basic, tc)
{
	struct protoent *p;
	size_t i;

	/*
	 * No ATF_CHECK() due static storage.
	 */
	for (i = 0; i < __arraycount(protos); i++) {

		p = getprotobynumber(protos[i].number);

		ATF_REQUIRE(p != NULL);
		ATF_REQUIRE(p->p_proto == protos[i].number);
		ATF_REQUIRE(strcmp(p->p_name, protos[i].name) == 0);
	}

	endprotoent();
}

ATF_TC(getprotobynumber_err);
ATF_TC_HEAD(getprotobynumber_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test EOF from getprotobynumber(3)");
}

ATF_TC_BODY(getprotobynumber_err, tc)
{
	static const int number[] = { -1, -99999, INT_MAX, 1000000000 };
	size_t i;

	for (i = 0; i < __arraycount(number); i++)
		ATF_REQUIRE(getprotobynumber(number[i]) == NULL);

	endprotoent();
}

ATF_TC(getprotoent_next);
ATF_TC_HEAD(getprotoent_next, tc)
{
	atf_tc_set_md_var(tc, "descr", "getprotoent(3) returns next line?");
}

ATF_TC_BODY(getprotoent_next, tc)
{
	struct protoent *p;
	int i = 0;

	/*
	 * The range [0, 60] is already reserved by IANA.
	 */
	while ((p = getprotoent()) != NULL && i <= 60) {
		ATF_CHECK(p->p_proto == i);
		i++;
	}

	endprotoent();
}

ATF_TC(setprotoent_rewind);
ATF_TC_HEAD(setprotoent_rewind, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check that setprotoent(3) rewinds");
}

ATF_TC_BODY(setprotoent_rewind, tc)
{
	struct protoent *p;

	setprotoent(0);

	p = getprotoent();
	ATF_REQUIRE(p->p_proto == 0);

	p = getprotoent();
	ATF_REQUIRE(p->p_proto == 1);

	p = getprotoent();
	ATF_REQUIRE(p->p_proto == 2);

	setprotoent(0);

	p = getprotoent();
	ATF_REQUIRE(p->p_proto == 0);

	p = getprotoent();
	ATF_REQUIRE(p->p_proto == 1);

	p = getprotoent();
	ATF_REQUIRE(p->p_proto == 2);

	endprotoent();
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, getprotobyname_basic);
	ATF_TP_ADD_TC(tp, getprotobyname_err);
	ATF_TP_ADD_TC(tp, getprotobynumber_basic);
	ATF_TP_ADD_TC(tp, getprotobynumber_err);
	ATF_TP_ADD_TC(tp, endprotoent_rewind);
	ATF_TP_ADD_TC(tp, getprotoent_next);
	ATF_TP_ADD_TC(tp, setprotoent_rewind);

	return atf_no_error();
}
