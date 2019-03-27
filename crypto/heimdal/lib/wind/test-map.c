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
#include <stdlib.h>
#include <string.h>
#include <err.h>

#include "windlocl.h"

#define MAX_LENGTH 2

struct example {
    uint32_t in[MAX_LENGTH];
    size_t in_len;
    uint32_t out[MAX_LENGTH];
    size_t out_len;
};

static struct example cases[] = {
    {{0}, 0, {0}, 0},
    {{0x0041}, 1, {0x0061}, 1},
    {{0x0061}, 1, {0x0061}, 1},
    {{0x00AD}, 1, {0}, 0},
    {{0x00DF}, 1, {0x0073, 0x0073}, 2}
};

static int
try(const struct example *c)
{
    int ret;
    size_t out_len = c->out_len;
    uint32_t *tmp = malloc(out_len * sizeof(uint32_t));
    if (tmp == NULL && out_len != 0)
	err(1, "malloc");
    ret = _wind_stringprep_map(c->in, c->in_len, tmp, &out_len, WIND_PROFILE_NAME);
    if (ret) {
	printf("wind_stringprep_map failed\n");
	return 1;
    }
    if (out_len != c->out_len) {
	printf("wrong out len\n");
	free(tmp);
	return 1;
    }
    if (memcmp(c->out, tmp, out_len * sizeof(uint32_t)) != 0) {
	printf("wrong out data\n");
	free(tmp);
	return 1;
    }
    free(tmp);
    return 0;
}

int
main(void)
{
    unsigned i;
    unsigned failures = 0;

    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i)
	failures += try(&cases[i]);
    return failures != 0;
}

