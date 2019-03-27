/*
 * Copyright (c) 2004 Kungliga Tekniska Högskolan
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

#include <config.h>

#include "roken.h"
#include "parse_time.h"
#include "test-mem.h"
#include "err.h"

static struct testcase {
    size_t size;
    int    val;
    char  *str;
} tests[] = {
    { 8, 1,		"1 second" },
    { 17, 61,		"1 minute 1 second" },
    { 18, 62,		"1 minute 2 seconds" },
    { 8, 60,		"1 minute" },
    { 6, 3600,	 	"1 hour" },
    { 15, 3601,	 	"1 hour 1 second" },
    { 16, 3602,	 	"1 hour 2 seconds" }
};

int
main(int argc, char **argv)
{
    size_t sz;
    size_t buf_sz;
    int i, j;

    for (i = 0; i < sizeof(tests)/sizeof(tests[0]); ++i) {
	char *buf;

	sz = unparse_time(tests[i].val, NULL, 0);
	if  (sz != tests[i].size)
	    errx(1, "sz (%lu) != tests[%d].size (%lu)",
		 (unsigned long)sz, i, (unsigned long)tests[i].size);

	for (buf_sz = 0; buf_sz < tests[i].size + 2; buf_sz++) {

	    buf = rk_test_mem_alloc(RK_TM_OVERRUN, "overrun",
				    NULL, buf_sz);
	    sz = unparse_time(tests[i].val, buf, buf_sz);
	    if (sz != tests[i].size)
		errx(1, "sz (%lu) != tests[%d].size (%lu) with in size %lu",
		     (unsigned long)sz, i,
		     (unsigned long)tests[i].size,
		     (unsigned long)buf_sz);
	    if (buf_sz > 0 && memcmp(buf, tests[i].str, buf_sz - 1) != 0)
		errx(1, "test %i wrong result %s vs %s", i, buf, tests[i].str);
	    if (buf_sz > 0 && buf[buf_sz - 1] != '\0')
		errx(1, "test %i not zero terminated", i);
	    rk_test_mem_free("overrun");

	    buf = rk_test_mem_alloc(RK_TM_UNDERRUN, "underrun",
				    NULL, tests[i].size);
	    sz = unparse_time(tests[i].val, buf, min(buf_sz, tests[i].size));
	    if (sz != tests[i].size)
		errx(1, "sz (%lu) != tests[%d].size (%lu) with insize %lu",
		     (unsigned long)sz, i,
		     (unsigned long)tests[i].size,
		     (unsigned long)buf_sz);
	    if (buf_sz > 0 && strncmp(buf, tests[i].str, min(buf_sz, tests[i].size) - 1) != 0)
		errx(1, "test %i wrong result %s vs %s", i, buf, tests[i].str);
	    if (buf_sz > 0 && buf[min(buf_sz, tests[i].size) - 1] != '\0')
		errx(1, "test %i not zero terminated", i);
	    rk_test_mem_free("underrun");
	}

	buf = rk_test_mem_alloc(RK_TM_OVERRUN, "overrun",
				tests[i].str, tests[i].size + 1);
	j = parse_time(buf, "s");
	if (j != tests[i].val)
	    errx(1, "parse_time failed for test %d", i);
	rk_test_mem_free("overrun");

	buf = rk_test_mem_alloc(RK_TM_UNDERRUN, "underrun",
				tests[i].str, tests[i].size + 1);
	j = parse_time(buf, "s");
	if (j != tests[i].val)
	    errx(1, "parse_time failed for test %d", i);
	rk_test_mem_free("underrun");

    }
    return 0;
}
