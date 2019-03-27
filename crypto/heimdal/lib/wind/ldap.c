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
#include <assert.h>

static int
put_char(uint32_t *out, size_t *o, uint32_t c, size_t out_len)
{
    if (*o >= out_len)
	return 1;
    out[*o] = c;
    (*o)++;
    return 0;
}

int
_wind_ldap_case_exact_attribute(const uint32_t *tmp,
				size_t olen,
				uint32_t *out,
				size_t *out_len)
{
    size_t o = 0, i = 0;

    if (olen == 0) {
	*out_len = 0;
	return 0;
    }

    if (put_char(out, &o, 0x20, *out_len))
	return WIND_ERR_OVERRUN;
    while(i < olen && tmp[i] == 0x20) /* skip initial spaces */
	i++;

    while (i < olen) {
	if (tmp[i] == 0x20) {
	    if (put_char(out, &o, 0x20, *out_len) ||
		put_char(out, &o, 0x20, *out_len))
		return WIND_ERR_OVERRUN;
	    while(i < olen && tmp[i] == 0x20) /* skip middle spaces */
		i++;
	} else {
	    if (put_char(out, &o, tmp[i++], *out_len))
		return WIND_ERR_OVERRUN;
	}
    }
    assert(o > 0);

    /* only one spaces at the end */
    if (o == 1 && out[0] == 0x20)
	o = 0;
    else if (out[o - 1] == 0x20) {
	if (out[o - 2] == 0x20)
	    o--;
    } else
	put_char(out, &o, 0x20, *out_len);

    *out_len = o;

    return 0;
}
