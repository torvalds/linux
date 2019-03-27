/*	$NetBSD: t_ether_aton.c,v 1.1 2011/11/01 22:36:53 pgoyette Exp $	*/

/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
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
__RCSID("$NetBSD: t_ether_aton.c,v 1.1 2011/11/01 22:36:53 pgoyette Exp $");

#include <atf-c.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <err.h>
#include <string.h>
#include <errno.h>

#ifndef __NetBSD__
#ifdef __linux__
#include <netinet/ether.h>
#endif
#include <net/ethernet.h>
#endif

#ifdef __NetBSD__
#define ETHER_ADDR_LEN 6

int ether_aton_r(u_char *dest, size_t len, const char *str);
#endif

static const struct {
	u_char res[ETHER_ADDR_LEN];
	const char *str;
	int error;
} tests[] = {
	{ { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab }, "01:23:45:67:89:ab", 0 },
#ifdef __NetBSD__
	{ { 0x00, 0x01, 0x22, 0x03, 0x14, 0x05 }, "0:1:22-3:14:05", 0 },
	{ { 0x00, 0x01, 0x22, 0x03, 0x14, 0x05 }, "000122031405", 0 },
	{ { 0x0a, 0x0B, 0xcc, 0xdD, 0xEE, 0x0f }, "0a0BccdDEE0f", 0 },
#endif
#define ZERO { 0, 0, 0, 0, 0, 0 }
	{ ZERO,	"0:1:2-3:04:05:06", ENAMETOOLONG },
	{ ZERO,	"0:1:2-3:04:", ENOBUFS },
	{ ZERO,	"0:1:2-3:04:x7", EINVAL },
	{ ZERO,	"1:x-3:04:05:7", EINVAL },
	{ ZERO,	NULL, 0 },
};

ATF_TC(tc_ether_aton);
ATF_TC_HEAD(tc_ether_aton, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check that ether_aton(3) works");  
}
 
ATF_TC_BODY(tc_ether_aton, tc)
{
#ifdef __NetBSD__
	u_char dest[ETHER_ADDR_LEN];
#else
	struct ether_addr dest;
#endif
	size_t t;
#ifdef __NetBSD__
	int e, r;
#else
	int e;
	struct ether_addr *r;
#endif
	const char *s;

	for (t = 0; tests[t].str; t++) {
		s = tests[t].str;
		if ((e = tests[t].error) == 0) {
#ifdef __NetBSD__
			if (ether_aton_r(dest, sizeof(dest), s) != e)
				atf_tc_fail("failed on `%s'", s);
			if (memcmp(dest, tests[t].res, sizeof(dest)) != 0)
				atf_tc_fail("unexpected result on `%s'", s);
#else
			if (ether_aton_r(s, &dest) == NULL && e == 0)
				atf_tc_fail("failed on `%s'", s);
			if (memcmp(&dest, tests[t].res, sizeof(dest)) != 0)
				atf_tc_fail("unexpected result on `%s'", s);
#endif
		} else {
#ifdef __NetBSD__
			if ((r = ether_aton_r(dest, sizeof(dest), s)) != e)
				atf_tc_fail("unexpectedly succeeded on `%s' "
				    "(%d != %d)", s, r, e);
#else
			if ((r = ether_aton_r(s, &dest)) != NULL && e != 0)
				atf_tc_fail("unexpectedly succeeded on `%s' "
				    "(%p != %d)", s, r, e);
#endif
		}
	}
}

ATF_TP_ADD_TCS(tp)
{       
 
	ATF_TP_ADD_TC(tp, tc_ether_aton);
        
        return atf_no_error();
}       
