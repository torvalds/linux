/*	$NetBSD: t_bluetooth.c,v 1.2 2011/04/07 08:29:50 plunky Exp $	*/

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Iain Hibbert.
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

#include <bluetooth.h>
#include <string.h>

ATF_TC(check_bt_aton);

ATF_TC_HEAD(check_bt_aton, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test bt_aton results");
}

ATF_TC_BODY(check_bt_aton, tc)
{
	bdaddr_t bdaddr;

	ATF_CHECK_EQ(bt_aton("0a:0b:0c:0d:0e", &bdaddr), 0);
	ATF_CHECK_EQ(bt_aton("0a:0b:0c:0d0:0e:0f", &bdaddr), 0);
	ATF_CHECK_EQ(bt_aton("0a:0b:0c:0d:0e:0f:00", &bdaddr), 0);
	ATF_CHECK_EQ(bt_aton("0a:0b:0c:0d:0e:0f\n", &bdaddr), 0);
	ATF_CHECK_EQ(bt_aton(" 0a:0b:0c:0d:0e:0f", &bdaddr), 0);
	ATF_CHECK_EQ(bt_aton("0a:0b:0x:0d:0e:0f", &bdaddr), 0);

	ATF_REQUIRE(bt_aton("0a:0b:0c:0d:0e:0f", &bdaddr));
	ATF_CHECK_EQ(bdaddr.b[0], 0x0f);
	ATF_CHECK_EQ(bdaddr.b[1], 0x0e);
	ATF_CHECK_EQ(bdaddr.b[2], 0x0d);
	ATF_CHECK_EQ(bdaddr.b[3], 0x0c);
	ATF_CHECK_EQ(bdaddr.b[4], 0x0b);
	ATF_CHECK_EQ(bdaddr.b[5], 0x0a);
}

ATF_TC(check_bt_ntoa);

ATF_TC_HEAD(check_bt_ntoa, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test bt_ntoa results");
}

ATF_TC_BODY(check_bt_ntoa, tc)
{
	bdaddr_t bdaddr = { { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 } };

	ATF_CHECK_STREQ(bt_ntoa(&bdaddr, NULL), "55:44:33:22:11:00");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, check_bt_aton);
	ATF_TP_ADD_TC(tp, check_bt_ntoa);

	return atf_no_error();
}
