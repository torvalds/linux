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

#include "windlocl.h"

#include <stdlib.h>

#include "bidi_table.h"

static int
range_entry_cmp(const void *a, const void *b)
{
    const struct range_entry *ea = (const struct range_entry*)a;
    const struct range_entry *eb = (const struct range_entry*)b;

    if (ea->start >= eb->start && ea->start < eb->start + eb->len)
	return 0;
    return ea->start - eb->start;
}

static int
is_ral(uint32_t cp)
{
    struct range_entry ee = {cp};
    void *s = bsearch(&ee, _wind_ral_table, _wind_ral_table_size,
		      sizeof(_wind_ral_table[0]),
		      range_entry_cmp);
    return s != NULL;
}

static int
is_l(uint32_t cp)
{
    struct range_entry ee = {cp};
    void *s = bsearch(&ee, _wind_l_table, _wind_l_table_size,
		      sizeof(_wind_l_table[0]),
		      range_entry_cmp);
    return s != NULL;
}

int
_wind_stringprep_testbidi(const uint32_t *in, size_t in_len, wind_profile_flags flags)
{
    size_t i;
    unsigned ral = 0;
    unsigned l   = 0;

    if ((flags & (WIND_PROFILE_NAME|WIND_PROFILE_SASL)) == 0)
	return 0;

    for (i = 0; i < in_len; ++i) {
	ral |= is_ral(in[i]);
	l   |= is_l(in[i]);
    }
    if (ral) {
	if (l)
	    return 1;
	if (!is_ral(in[0]) || !is_ral(in[in_len - 1]))
	    return 1;
    }
    return 0;
}
