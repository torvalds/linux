/*	$NetBSD: t_mi_vector_hash.c,v 1.3 2011/07/07 11:12:18 jruoho Exp $	*/
/*-
 * Copyright (c) 2009 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_mi_vector_hash.c,v 1.3 2011/07/07 11:12:18 jruoho Exp $");

#include <atf-c.h>
#include <stdlib.h>
#include <string.h>

ATF_TC(mi_vector_hash_basic);
ATF_TC_HEAD(mi_vector_hash_basic, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test mi_vector_hash_vector_hash for consistent results");
}

static const struct testvector {
	const char *vector;
	uint32_t hashes[3];
} testv[] = {
	{ "hello, world", { 0xd38f7f21, 0xbf6be9ab, 0x37a0e989 } },
	{ "", { 0x9b2ec03d, 0xdb2b69ae, 0xbd49d10d } },
	{ "a", { 0x9454baa3, 0xb711c708, 0x29eec818 } },
	{ "ab", { 0x9a5dca90, 0xdd212644, 0x9879ac41 } },
	{ "abc", { 0x0b91c470, 0x4770cdf5, 0x251e4793 } },
	{ "abcd", { 0x5f128df3, 0xf5a667a6, 0x5ae61fa5 } },
	{ "abcde", { 0x4cbae281, 0x799c0ed5, 0x03a96866 } },
	{ "abcdef", { 0x507a54c8, 0xb6bd06f4, 0xde922732 } },
	{ "abcdefg", { 0xae2bca5d, 0x61e960ef, 0xb9e6762c } },
	{ "abcdefgh", { 0xd1021264, 0x87f6988f, 0x053f775e } },
	{ "abcdefghi", { 0xe380defc, 0xfc35a811, 0x3a7b0a5f } },
	{ "abcdefghij", { 0x9a504408, 0x70d2e89d, 0xc9cac242 } },
	{ "abcdefghijk", { 0x376117d0, 0x89f434d4, 0xe52b8e4c } },
	{ "abcdefghijkl", { 0x92253599, 0x7b6ff99e, 0x0b1b3ea5 } },
	{ "abcdefghijklm", { 0x92ee6a52, 0x55587d47, 0x3122b031 } },
	{ "abcdefghijklmn", { 0x827baf08, 0x1d0ada73, 0xfec330e0 } },
	{ "abcdefghijklmno", { 0x06ab787d, 0xc1ad17c2, 0x11dccf31 } },
	{ "abcdefghijklmnop", { 0x2cf18103, 0x638c9268, 0xfa1ecf51 } },
};

ATF_TC_BODY(mi_vector_hash_basic, tc)
{
	size_t i, j, len;
	uint32_t hashes[3];
	char buf[256];

	for (j = 0; j < 8; ++j) {
		for (i = 0; i < sizeof(testv) / sizeof(testv[0]); ++i) {
			len = strlen(testv[i].vector);
			strcpy(buf + j, testv[i].vector);
			mi_vector_hash(buf + j, len, 0, hashes);
			ATF_CHECK_EQ(hashes[0], testv[i].hashes[0]);
			ATF_CHECK_EQ(hashes[1], testv[i].hashes[1]);
			ATF_CHECK_EQ(hashes[2], testv[i].hashes[2]);
		}
	}
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, mi_vector_hash_basic);

	return atf_no_error();
}
