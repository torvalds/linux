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

#include "windlocl.h"
#include <stdio.h>
#include <err.h>
#include <assert.h>

#define MAX_LENGTH 10


struct testcase {
    unsigned int in_flags;
    size_t in_len;
    const char *in_ptr;
    int ret;
    size_t ucs2_len;
    uint16_t ucs2[MAX_LENGTH];
    unsigned int out_flags;
} testcases[] = {
    {
	WIND_RW_BOM,
	4, "\xff\xfe\x20\x00",
	0,
	1, { 0x0020 },
	WIND_RW_LE
    },
    {
	WIND_RW_BOM,
	4, "\xfe\xff\x00\x20",
	0,
	1, { 0x0020 },
	WIND_RW_BE
    },
    /* only BE BOM */
    {
	WIND_RW_BOM,
	2, "\xfe\xff",
	0,
	0, { 0 },
	WIND_RW_BE
    },
    /* no input */
    {
	WIND_RW_BOM,
	0, "",
	0,
	0, { 0 },
	WIND_RW_BOM
    },
    /* BOM only */
    {
	WIND_RW_BOM,
	2, "\xff\xfe",
	0,
	0, { 0 },
	WIND_RW_LE
    },
    /* water + z */
    {
	WIND_RW_BOM|WIND_RW_LE,
	4, "\x34\x6C\x7A\x00",
	0,
	2, { 0x6C34, 0x7a },
	WIND_RW_LE
    },
    /* water + z */
    {
	WIND_RW_LE,
	4, "\x34\x6C\x7A\x00",
	0,
	2, { 0x6C34, 0x7a },
	WIND_RW_LE
    },
    /* BOM + water + z */
    {
	WIND_RW_BOM,
	6, "\xFF\xFE\x34\x6C\x7A\x00",
	0,
	2, { 0x6C34, 0x7a },
	WIND_RW_LE
    },
    /* BOM + water + z */
    {
	WIND_RW_BOM,
	6, "\xFE\xFF\x6C\x34\x00\x7A",
	0,
	2, { 0x6C34, 0x7a },
	WIND_RW_BE
    },
    /* error, odd length */
    {
	WIND_RW_BOM,
	1, "\xfe",
	WIND_ERR_LENGTH_NOT_MOD2,
	0, { 0 },
	WIND_RW_BOM
    },
    /* error, missing BOM */
    {
	WIND_RW_BOM,
	2, "\x00\x20",
	WIND_ERR_NO_BOM,
	0, { 0 },
	WIND_RW_BOM
    },
    /* error, overrun */
    {
	WIND_RW_BE,
	4, "\x00\x20\x00\x20",
	WIND_ERR_OVERRUN,
	1, { 0x20 },
	WIND_RW_BOM
    }

};

int
main(void)
{
    unsigned int n, m, flags;
    uint16_t data[MAX_LENGTH];
    size_t datalen;
    int ret;

    for (n = 0; n < sizeof(testcases)/sizeof(testcases[0]); n++) {
	flags = testcases[n].in_flags;

	datalen = testcases[n].ucs2_len;
	assert(datalen < sizeof(data));

	ret = wind_ucs2read(testcases[n].in_ptr,
			    testcases[n].in_len,
			    &flags,
			    data,
			    &datalen);
	if (ret != testcases[n].ret)
	    errx(1, "testcases %u: wind_ucs2read: %d", n, ret);

	/* on error, skip all other tests */
	if (ret)
	    continue;

	if (flags != testcases[n].out_flags)
	    errx(1, "testcases %u: flags wrong", n);

	if (datalen != testcases[n].ucs2_len)
	    errx(1, "testcases %u: ucs len wrong", n);

	for (m = 0; m < datalen; m++)
	    if (testcases[n].ucs2[m] != data[m])
		errx(1, "testcases %u: char %u wrong", n, m);
    }

    return 0;
}
