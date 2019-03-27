/*
 * Copyright (c) 2008 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <assert.h>
#include "windlocl.h"

#define MAX_LENGTH 10

struct testcase {
    uint32_t in[MAX_LENGTH];
    size_t ilen;
    uint32_t out[MAX_LENGTH];
    size_t olen;
};

static const struct testcase testcases[] = {
    { { 0x20 }, 1, { 0 }, 0 },
    { { 0x20, 0x61 }, 2, { 0x20, 0x61, 0x20}, 3 },
    { { 0x20, 0x61, 0x20 }, 3, { 0x20, 0x61, 0x20}, 3 },
    { { 0x20, 0x61, 0x20, 0x61 }, 4, { 0x20, 0x61, 0x20, 0x20, 0x61, 0x20}, 6 }
};

static const struct testcase testcases2[] = {
    { { 0x20 }, 1, { 0x20 }, 1 },
    { { 0x20, 0x41 }, 2, { 0x20, 0x61}, 2 }
};


int
main(void)
{
    uint32_t out[MAX_LENGTH];
    unsigned failures = 0;
    unsigned i;
    size_t olen;
    int ret;


    for (i = 0; i < sizeof(testcases)/sizeof(testcases[0]); ++i) {
	const struct testcase *t = &testcases[i];

	olen = sizeof(out)/sizeof(out[0]);
	assert(olen > t->olen);

	ret = _wind_ldap_case_exact_attribute(t->in, t->ilen, out, &olen);
	if (ret) {
	    printf("wlcea: %u: %d\n", i, ret);
	    ++failures;
	    continue;
	}
	if (olen != t->olen) {
	    printf("len wlcea: %u %u != %u\n", i,
		   (unsigned)olen, (unsigned)t->olen);
	    failures++;
	    continue;
	}
	if (memcmp(t->out, out, sizeof(out[0]) * olen) != 0) {
	    printf("memcmp wlcea: %u\n", i);
	    failures++;
	    continue;
	}
    }

    for (i = 0; i < sizeof(testcases2)/sizeof(testcases2[0]); ++i) {
	const struct testcase *t = &testcases2[i];

	olen = sizeof(out)/sizeof(out[0]);
	assert(olen > t->olen);

	ret = wind_stringprep(t->in, t->ilen, out, &olen,
			      WIND_PROFILE_LDAP_CASE);

	if (ret) {
	    printf("wsplc: %u: %d\n", i, ret);
	    ++failures;
	    continue;
	}

	if (olen != t->olen) {
	    printf("strlen wsplc: %u: %d\n", i, ret);
	    ++failures;
	    continue;
	}
	if (memcmp(t->out, out, sizeof(out[0]) * olen) != 0) {
	    printf("memcmp wsplc: %u\n", i);
	    failures++;
	    continue;
	}
    }

    return failures != 0;
}
