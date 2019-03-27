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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include "windlocl.h"

#define MAX_LENGTH 10

struct test {
    unsigned len;
    uint32_t vals[MAX_LENGTH];
};

static struct test passing_cases[] = {
    {0, {0}},
    {1, {0x0041}},
    {1, {0x05be}},
};

static struct test failing_cases[] = {
    {2, {0x05be, 0x0041}},
    {3, {0x05be, 0x0041, 0x05be}},
};

int
main(void)
{
    unsigned i;
    unsigned failures = 0;

    for (i = 0; i < sizeof(passing_cases)/sizeof(passing_cases[0]); ++i) {
	const struct test *t = &passing_cases[i];
	if (_wind_stringprep_testbidi(t->vals, t->len, WIND_PROFILE_NAME)) {
	    printf ("passing case %u failed\n", i);
	    ++failures;
	}
    }

    for (i = 0; i < sizeof(failing_cases)/sizeof(failing_cases[0]); ++i) {
	const struct test *t = &failing_cases[i];
	if (!_wind_stringprep_testbidi(t->vals, t->len, WIND_PROFILE_NAME)) {
	    printf ("failing case %u passed\n", i);
	    ++failures;
	}
    }

    return failures != 0;
}
