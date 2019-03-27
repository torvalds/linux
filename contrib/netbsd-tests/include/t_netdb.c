/*	$NetBSD: t_netdb.c,v 1.2 2011/04/25 20:51:14 njoly Exp $ */

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
__RCSID("$NetBSD: t_netdb.c,v 1.2 2011/04/25 20:51:14 njoly Exp $");

#include <atf-c.h>
#include <netdb.h>

ATF_TC(netdb_constants);
ATF_TC_HEAD(netdb_constants, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test constants in <netdb.h>");
}

ATF_TC_BODY(netdb_constants, tc)
{
	bool fail;

	/*
	 * The following definitions should be available
	 * according to IEEE Std 1003.1-2008, issue 7.
	 */
	atf_tc_expect_fail("PR standards/44777");

	fail = true;

#ifdef AI_PASSIVE
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("AI_PASSIVE not defined");

	fail = true;

#ifdef AI_CANONNAME
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("AI_CANONNAME not defined");

	fail = true;

#ifdef AI_NUMERICHOST
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("AI_NUMERICHOST not defined");

	fail = true;

#ifdef AI_NUMERICSERV
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("AI_NUMERICSERV not defined");

	fail = true;

#ifdef AI_V4MAPPED
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("AI_V4MAPPED not defined");

	fail = true;

#ifdef AI_ALL
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("AI_ALL not defined");

	fail = true;

#ifdef AI_ADDRCONFIG
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("AI_ADDRCONFIG not defined");

	fail = true;

#ifdef NI_NOFQDN
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("NI_NOFQDN not defined");

	fail = true;

#ifdef NI_NUMERICHOST
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("NI_NUMERICHOST not defined");

	fail = true;

#ifdef NI_NAMEREQD
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("NI_NAMEREQD not defined");

	fail = true;

#ifdef NI_NUMERICSERV
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("NI_NUMERICSERV not defined");

	fail = true;

#ifdef NI_NUMERICSCOPE
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("NI_NUMERICSCOPE not defined");

	fail = true;

#ifdef NI_DGRAM
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("NI_DGRAM not defined");

	fail = true;

#ifdef EAI_AGAIN
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EAI_AGAIN not defined");

	fail = true;

#ifdef EAI_BADFLAGS
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EAI_BADFLAGS not defined");

	fail = true;

#ifdef EAI_FAIL
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EAI_FAIL not defined");

	fail = true;

#ifdef EAI_FAMILY
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EAI_FAMILY not defined");

	fail = true;

#ifdef EAI_MEMORY
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EAI_MEMORY not defined");

	fail = true;

#ifdef EAI_NONAME
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EAI_NONAME not defined");

	fail = true;

#ifdef EAI_SERVICE
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EAI_SERVICE not defined");

	fail = true;

#ifdef EAI_SOCKTYPE
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EAI_SOCKTYPE not defined");

	fail = true;

#ifdef EAI_SYSTEM
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EAI_SYSTEM not defined");

	fail = true;

#ifdef EAI_OVERFLOW
	fail = false;
#endif
	if (fail != false)
		atf_tc_fail_nonfatal("EAI_OVERFLOW not defined");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, netdb_constants);

	return atf_no_error();
}
