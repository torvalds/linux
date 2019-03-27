/*	$NetBSD: t_hmac.c,v 1.1 2016/07/02 14:52:09 christos Exp $	*/

/*-
 * Copyright (c) 2016 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_hmac.c,v 1.1 2016/07/02 14:52:09 christos Exp $");

#include <atf-c.h>
#include <string.h>
#include <stdlib.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

static void
test(void)
{
	uint8_t         tmp1[EVP_MAX_MD_SIZE];
	uint8_t         tmp2[EVP_MAX_MD_SIZE];
	uint8_t key[256];
	uint8_t data[4096];
	unsigned int tmp1len;
	size_t tmp2len;
	int stop;
	void *e1;
	const void *evps[] = {
		EVP_md2(),
		EVP_md4(),
		EVP_md5(),
		EVP_ripemd160(),
		EVP_sha1(),
		EVP_sha224(),
		EVP_sha256(),
		EVP_sha384(),
		EVP_sha512(),
	};
	const char *names[] = {
		"md2",
		"md4",
		"md5",
		"rmd160",
		"sha1",
		"sha224",
		"sha256",
		"sha384",
		"sha512",
	};

	for (size_t k = 0; k < sizeof(key); k++)
		key[k] = k;
	for (size_t d = 0; d < sizeof(data); d++)
		data[d] = d % 256;

	for (size_t t = 0; t < __arraycount(names); t++)
	    for (size_t i = 1; i < sizeof(key); i += 9)
		for (size_t j = 3; j < sizeof(data); j += 111) {
			stop = 0;
#ifdef DEBUG
			printf("%s: keysize = %zu datasize = %zu\n", names[t],
			    i, j);
#endif
			memset(tmp1, 0, sizeof(tmp1));
			memset(tmp2, 0, sizeof(tmp2));
			e1 = HMAC(evps[t], key, i, data, j, tmp1, &tmp1len);
			ATF_REQUIRE(e1 != NULL);
			tmp2len = hmac(names[t], key, i, data, j, tmp2,
			    sizeof(tmp2));
			ATF_REQUIRE_MSG(tmp1len == tmp2len, "hash %s len %u "
			    "!= %zu", names[t], tmp1len, tmp2len);
			for (size_t k = 0; k < tmp2len; k++)
				if (tmp1[k] != tmp2[k]) {
#ifdef DEBUG
					printf("%zu %.2x %.2x\n",
					    k, tmp1[k], tmp2[k]);
#endif
					stop = 1;
					break;
				}
			ATF_REQUIRE_MSG(!stop, "hash %s failed for "
				"keylen=%zu, datalen=%zu", names[t], i, j);
		}
}

ATF_TC(t_hmac);

ATF_TC_HEAD(t_hmac, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test hmac functions for consistent results");
}

ATF_TC_BODY(t_hmac, tc)
{
	test();
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, t_hmac);
	return atf_no_error();
}

