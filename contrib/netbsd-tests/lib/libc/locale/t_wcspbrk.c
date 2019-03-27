/*	$NetBSD: t_wcspbrk.c,v 1.1 2011/11/21 23:50:45 joerg Exp $	*/

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
__RCSID("$NetBSD: t_wcspbrk.c,v 1.1 2011/11/21 23:50:45 joerg Exp $");

#include <atf-c.h>
#include <wchar.h>

ATF_TC(wcspbrk);
ATF_TC_HEAD(wcspbrk, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test wcspbrk(3)");
}

ATF_TC_BODY(wcspbrk, tc)
{
	static const wchar_t s[] = L"abcdefghijklmnop";

	ATF_CHECK_EQ(wcspbrk(s, L""), NULL);
	ATF_CHECK_EQ(wcspbrk(s, L"qrst"), NULL);
	ATF_CHECK_EQ(wcspbrk(s, L"a"), s);
	ATF_CHECK_EQ(wcspbrk(s, L"b"), s + 1);
	ATF_CHECK_EQ(wcspbrk(s, L"ab"), s);
	ATF_CHECK_EQ(wcspbrk(s, L"cdef"), s + 2);
	ATF_CHECK_EQ(wcspbrk(s, L"fedc"), s + 2);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, wcspbrk);
	return atf_no_error();
}
