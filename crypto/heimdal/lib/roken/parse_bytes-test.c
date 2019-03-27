/*
 * Copyright (c) 1999 Kungliga Tekniska Högskolan
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
#include "parse_bytes.h"

static struct testcase {
    int canonicalp;
    int val;
    const char *def_unit;
    const char *str;
} tests[] = {
    {0, 0, NULL, "0 bytes"},
    {1, 0, NULL, "0"},
    {0, 1, NULL, "1"},
    {1, 1, NULL, "1 byte"},
    {0, 0, "kilobyte", "0"},
    {0, 1024, "kilobyte", "1"},
    {1, 1024, "kilobyte", "1 kilobyte"},
    {1, 1024 * 1024, NULL, "1 megabyte"},
    {0, 1025, NULL, "1 kilobyte 1"},
    {1, 1025, NULL, "1 kilobyte 1 byte"},
};

int
main(int argc, char **argv)
{
    int i;
    int ret = 0;

    for (i = 0; i < sizeof(tests)/sizeof(tests[0]); ++i) {
	char buf[256];
	int val = parse_bytes (tests[i].str, tests[i].def_unit);
	int len;

	if (val != tests[i].val) {
	    printf ("parse_bytes (%s, %s) = %d != %d\n",
		    tests[i].str,
		    tests[i].def_unit ? tests[i].def_unit : "none",
		    val, tests[i].val);
	    ++ret;
	}
	if (tests[i].canonicalp) {
	    len = unparse_bytes (tests[i].val, buf, sizeof(buf));
	    if (strcmp (tests[i].str, buf) != 0) {
		printf ("unparse_bytes (%d) = \"%s\" != \"%s\"\n",
			tests[i].val, buf, tests[i].str);
		++ret;
	    }
	}
    }
    if (ret) {
	printf ("%d errors\n", ret);
	return 1;
    } else
	return 0;
}
