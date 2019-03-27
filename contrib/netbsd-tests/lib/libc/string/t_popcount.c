/*	$NetBSD: t_popcount.c,v 1.4 2011/07/07 08:27:36 jruoho Exp $	*/
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
__RCSID("$NetBSD: t_popcount.c,v 1.4 2011/07/07 08:27:36 jruoho Exp $");

#include <atf-c.h>
#include <strings.h>

static unsigned int byte_count[256];

static void
popcount_init(const char *cfg_var)
{
	unsigned int i, j;

	if (strcasecmp(cfg_var, "YES")  == 0 ||
	    strcasecmp(cfg_var, "Y")    == 0 ||
	    strcasecmp(cfg_var, "1")    == 0 ||
	    strcasecmp(cfg_var, "T")    == 0 ||
	    strcasecmp(cfg_var, "TRUE") == 0) {
		for (i = 0; i < 256; ++i) {
			byte_count[i] = 0;
			for (j = i; j != 0; j >>= 1) {
				if (j & 1)
					++byte_count[i];
			}
		}
		return;
	}

	atf_tc_skip("config variable \"run_popcount\" not set to YES/TRUE");
}

unsigned int test_parts[256] = {
	0x318e53e6U, 0x11710316U, 0x62608ffaU, 0x67e0f562U,
	0xe432e82cU, 0x9862e8b2U, 0x7d96a627U, 0x3f74ad31U,
	0x3cecf906U, 0xcdc0dcb4U, 0x241dab64U, 0x31e6133eU,
	0x23086ad4U, 0x721d5a91U, 0xc483da53U, 0x6a62af52U,
	0xf3f5c386U, 0xe0de3f77U, 0x65afe528U, 0xf4816485U,
	0x40ccbf08U, 0x25df49c1U, 0xae5a6ee0U, 0xab36ccadU,
	0x87e1ec29U, 0x60ca2407U, 0x49d62e47U, 0xa09f2df5U,
	0xaf4c1c68U, 0x8ef08d50U, 0x624cfd2fU, 0xa6a36f20U,
	0x68aaf879U, 0x0fe9deabU, 0x5c9a4060U, 0x215d8f08U,
	0x55e84712U, 0xea1f1681U, 0x3a10b8a1U, 0x08e06632U,
	0xcbc875e2U, 0x31e53258U, 0xcd3807a4U, 0xb9d17516U,
	0x8fbfd9abU, 0x6651b555U, 0x550fb381U, 0x05061b9dU,
	0x35aef3f2U, 0x9175078cU, 0xae0f14daU, 0x92a2d5f8U,
	0x70d968feU, 0xe86f41c5U, 0x5cfaf39fU, 0x8499b18dU,
	0xb33f879aU, 0x0a68ad3dU, 0x9323ecc1U, 0x060037ddU,
	0xb91a5051U, 0xa0dbebf6U, 0x3e6aa6f1U, 0x7b422b5bU,
	0x599e811eU, 0x199f7594U, 0xca453365U, 0x1cda6f48U,
	0xe9c75d2cU, 0x6a873217U, 0x79c45d72U, 0x143b8e37U,
	0xa11df26eU, 0xaf31f80aU, 0x311bf759U, 0x2378563cU,
	0x9ab95fa5U, 0xfcf4d47cU, 0x1f7db268U, 0xd64b09e1U,
	0xad7936daU, 0x7a59005cU, 0x45b173d3U, 0xc1a71b32U,
	0x7d9f0de2U, 0xa9ac3792U, 0x9e7f9966U, 0x7f0b8080U,
	0xece6c06fU, 0x78d92a3cU, 0x6d5f8f6cU, 0xc50ca544U,
	0x5d8ded27U, 0xd27a8462U, 0x4bcd13ccU, 0xd49075f2U,
	0xa8d52acfU, 0x41915d97U, 0x564f7062U, 0xefb046e2U,
	0xe296277aU, 0x605b0ea3U, 0x10b2c3a1U, 0x4e8e5c66U,
	0x4bd8ec04U, 0x29935be9U, 0x381839f3U, 0x555d8824U,
	0xd6befddbU, 0x5d8d6d6eU, 0xb2fdb7b4U, 0xb471c8fcU,
	0xc2fd325bU, 0x932d2487U, 0xbdbbadefU, 0x66c8895dU,
	0x5d77857aU, 0x259f1cc0U, 0x302037faU, 0xda9aa7a8U,
	0xb112c6aaU, 0x78f74192U, 0xfd4da741U, 0xfa5765c1U,
	0x6ea1bc5cU, 0xd283f39cU, 0x268ae67dU, 0xdedcd134U,
	0xbbf92410U, 0x6b45fb55U, 0x2f75ac71U, 0x64bf2ca5U,
	0x8b99675aU, 0x3f4923b6U, 0x7e610550U, 0x04b1c06dU,
	0x8f92e7c6U, 0x45cb608bU, 0x2d06d1f2U, 0x79cf387aU,
	0xfd3ed225U, 0x243eee20U, 0x2cbefc6fU, 0x8286cbaaU,
	0x70d4c182U, 0x054e3cc6U, 0xb66c5362U, 0x0c73fa5dU,
	0x539948feU, 0xec638563U, 0x0cf04ab6U, 0xec7b52f4U,
	0x58eeffceU, 0x6fe8049aU, 0xb3b33332U, 0x2e33bfdbU,
	0xcc817567U, 0x71ac57c8U, 0x4bab3ac7U, 0x327c558bU,
	0x82a6d279U, 0x5adf71daU, 0x1074a656U, 0x3c533c1fU,
	0x82fdbe69U, 0x21b4f6afU, 0xd59580e8U, 0x0de824ebU,
	0xa510941bU, 0x7cd91144U, 0xa8c10631U, 0x4c839267U,
	0x5d503c2fU, 0xe1567d55U, 0x23910cc7U, 0xdb1bdc34U,
	0x2a866704U, 0x33e21f0cU, 0x5c7681b4U, 0x818651caU,
	0xb1d18162U, 0x225ad014U, 0xadf7d6baU, 0xac548d9bU,
	0xe94736e5U, 0x2279c5f1U, 0x33215d2cU, 0xdc8ab90eU,
	0xf5e3d7f2U, 0xedcb15cfU, 0xc9a43c4cU, 0xfc678fc6U,
	0x43796b95U, 0x3f8b700cU, 0x867bbc72U, 0x81f71fecU,
	0xd00cad7dU, 0x302c458fU, 0x8ae21accU, 0x05850ce8U,
	0x7764d8e8U, 0x8a36cd68U, 0x40b44bd7U, 0x1cffaeb7U,
	0x2b248f34U, 0x1eefdbafU, 0x574d7437U, 0xe86cd935U,
	0xf53dd1c8U, 0x1b022513U, 0xef2d249bU, 0x94fb2b08U,
	0x15d3eff8U, 0x14245e1bU, 0x82aa8425U, 0x53959028U,
	0x9c5f9b80U, 0x325e0c82U, 0x3e236c24U, 0x74e1dd36U,
	0x9890df3fU, 0xaf9701a2U, 0x023b3413U, 0x7634c67eU,
	0x55cf5e45U, 0x56d2a95bU, 0xb6db869bU, 0xac19e260U,
	0xdd310740U, 0x26d68f84U, 0x45bebf17U, 0xe4a7728fU,
	0xf082e66eU, 0xb2fe3c10U, 0x2db1fa2cU, 0x4b3dfcfaU,
	0xc7b3a672U, 0xaeadc67bU, 0x6cce6f2bU, 0x8263dbbfU,
	0xd9724d5bU, 0xbcc767b5U, 0x8d563798U, 0x2db764b4U,
	0x76e0cee7U, 0xd34f9a67U, 0x035c810aU, 0x3f56bdc1U,
	0x5b3f2c84U, 0x0baca8c0U, 0xfe979a77U, 0x484ca775U,
	0xbdc7f104U, 0xc06c3efbU, 0xdbc5f32cU, 0x44b017e7U,
};

ATF_TC(popcount_basic);
ATF_TC_HEAD(popcount_basic, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test popcount results");
	atf_tc_set_md_var(tc, "timeout", "0");
}

ATF_TC_BODY(popcount_basic, tc)
{
	unsigned int i, r;

	popcount_init(atf_tc_get_config_var_wd(tc, "run_popcount", "NO"));

	for (i = 0; i < 0xffffffff; ++i) {
		r = byte_count[i & 255] + byte_count[(i >> 8) & 255]
		    + byte_count[(i >> 16) & 255]
		    + byte_count[(i >> 24) & 255];

		ATF_CHECK_EQ(r, popcount(i));
	}
	ATF_CHECK_EQ(popcount(0xffffffff), 32);
}

ATF_TC(popcountll_basic);
ATF_TC_HEAD(popcountll_basic, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test popcountll results");
	atf_tc_set_md_var(tc, "timeout", "0");
}

ATF_TC_BODY(popcountll_basic, tc)
{
	unsigned int i, j, r, r2, p;
	unsigned long long v;

	popcount_init(atf_tc_get_config_var_wd(tc, "run_popcount", "NO"));

	for (j = 0; j < 256; ++j) {
		p = test_parts[j];
		r2 = byte_count[p & 255] + byte_count[(p >> 8) & 255]
		    + byte_count[(p >> 16) & 255]
		    + byte_count[(p >> 24) & 255];

		for (i = 0; i < 0xffffffff; ++i) {
			r = byte_count[i & 255] + byte_count[(i >> 8) & 255]
			    + byte_count[(i >> 16) & 255]
			    + byte_count[(i >> 24) & 255] + r2;

			v = (((unsigned long long)i) << 32) + p;
			ATF_CHECK_EQ(r, popcountll(v));
			v = (((unsigned long long)p) << 32) + i;
			ATF_CHECK_EQ(r, popcountll(v));
		}
	}

	ATF_CHECK_EQ(popcountll(0xffffffffffffffffULL), 64);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, popcount_basic);
	ATF_TP_ADD_TC(tp, popcountll_basic);

	return atf_no_error();
}
