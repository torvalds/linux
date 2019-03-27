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
#include "windlocl.h"

#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

#include "roken.h"

#include "normalize_table.h"

static int
translation_cmp(const void *key, const void *data)
{
    const struct translation *t1 = (const struct translation *)key;
    const struct translation *t2 = (const struct translation *)data;

    return t1->key - t2->key;
}

enum { s_base  = 0xAC00};
enum { s_count = 11172};
enum { l_base  = 0x1100};
enum { l_count = 19};
enum { v_base  = 0x1161};
enum { v_count = 21};
enum { t_base  = 0x11A7};
enum { t_count = 28};
enum { n_count = v_count * t_count};

static int
hangul_decomp(const uint32_t *in, size_t in_len,
	      uint32_t *out, size_t *out_len)
{
    uint32_t u = *in;
    unsigned s_index;
    unsigned l, v, t;
    unsigned o;

    if (u < s_base || u >= s_base + s_count)
	return 0;
    s_index = u - s_base;
    l = l_base + s_index / n_count;
    v = v_base + (s_index % n_count) / t_count;
    t = t_base + s_index % t_count;
    o = 2;
    if (t != t_base)
	++o;
    if (*out_len < o)
	return WIND_ERR_OVERRUN;
    out[0] = l;
    out[1] = v;
    if (t != t_base)
	out[2] = t;
    *out_len = o;
    return 1;
}

static uint32_t
hangul_composition(const uint32_t *in, size_t in_len)
{
    if (in_len < 2)
	return 0;
    if (in[0] >= l_base && in[0] < l_base + l_count) {
	unsigned l_index = in[0] - l_base;
	unsigned v_index;

	if (in[1] < v_base || in[1] >= v_base + v_count)
	    return 0;
	v_index = in[1] - v_base;
	return (l_index * v_count + v_index) * t_count + s_base;
    } else if (in[0] >= s_base && in[0] < s_base + s_count) {
	unsigned s_index = in[0] - s_base;
	unsigned t_index;

	if (s_index % t_count != 0)
	    return 0;
	if (in[1] < t_base || in[1] >= t_base + t_count)
	    return 0;
	t_index = in[1] - t_base;
	return in[0] + t_index;
    }
    return 0;
}

static int
compat_decomp(const uint32_t *in, size_t in_len,
	      uint32_t *out, size_t *out_len)
{
    unsigned i;
    unsigned o = 0;

    for (i = 0; i < in_len; ++i) {
	struct translation ts = {in[i]};
	size_t sub_len = *out_len - o;
	int ret;

	ret = hangul_decomp(in + i, in_len - i,
			    out + o, &sub_len);
	if (ret) {
	    if (ret == WIND_ERR_OVERRUN)
		return ret;
	    o += sub_len;
	} else {
	    void *s = bsearch(&ts,
			      _wind_normalize_table,
			      _wind_normalize_table_size,
			      sizeof(_wind_normalize_table[0]),
			      translation_cmp);
	    if (s != NULL) {
		const struct translation *t = (const struct translation *)s;

		ret = compat_decomp(_wind_normalize_val_table + t->val_offset,
				    t->val_len,
				    out + o, &sub_len);
		if (ret)
		    return ret;
		o += sub_len;
	    } else {
		if (o >= *out_len)
		    return WIND_ERR_OVERRUN;
		out[o++] = in[i];

	    }
	}
    }
    *out_len = o;
    return 0;
}

static void
swap_char(uint32_t * a, uint32_t * b)
{
    uint32_t t;
    t = *a;
    *a = *b;
    *b = t;
}

/* Unicode 5.2.0 D109 Canonical Ordering for a sequence of code points
 * that all have Canonical_Combining_Class > 0 */
static void
canonical_reorder_sequence(uint32_t * a, size_t len)
{
    size_t i, j;

    if (len <= 1)
	return;

    for (i = 1; i < len; i++) {
	for (j = i;
	     j > 0 &&
		 _wind_combining_class(a[j]) < _wind_combining_class(a[j-1]);
	     j--)
	    swap_char(&a[j], &a[j-1]);
    }
}

static void
canonical_reorder(uint32_t *tmp, size_t tmp_len)
{
    size_t i;

    for (i = 0; i < tmp_len; ++i) {
	int cc = _wind_combining_class(tmp[i]);
	if (cc) {
	    size_t j;
	    for (j = i + 1;
		 j < tmp_len && _wind_combining_class(tmp[j]);
		 ++j)
		;
	    canonical_reorder_sequence(&tmp[i], j - i);
	    i = j;
	}
    }
}

static uint32_t
find_composition(const uint32_t *in, unsigned in_len)
{
    unsigned short canon_index = 0;
    uint32_t cur;
    unsigned n = 0;

    cur = hangul_composition(in, in_len);
    if (cur)
	return cur;

    do {
	const struct canon_node *c = &_wind_canon_table[canon_index];
	unsigned i;

	if (n % 5 == 0) {
	    cur = *in++;
	    if (in_len-- == 0)
		return c->val;
	}

	i = cur >> 16;
	if (i < c->next_start || i >= c->next_end)
	    canon_index = 0;
	else
	    canon_index =
		_wind_canon_next_table[c->next_offset + i - c->next_start];
	if (canon_index != 0) {
	    cur = (cur << 4) & 0xFFFFF;
	    ++n;
	}
    } while (canon_index != 0);
    return 0;
}

static int
combine(const uint32_t *in, size_t in_len,
	uint32_t *out, size_t *out_len)
{
    unsigned i;
    int ostarter;
    unsigned o = 0;
    int old_cc;

    for (i = 0; i < in_len;) {
	while (i < in_len && _wind_combining_class(in[i]) != 0) {
	    out[o++] = in[i++];
	}
	if (i < in_len) {
	    if (o >= *out_len)
		return WIND_ERR_OVERRUN;
	    ostarter = o;
	    out[o++] = in[i++];
	    old_cc   = -1;

	    while (i < in_len) {
		uint32_t comb;
		uint32_t v[2];
		int cc;

		v[0] = out[ostarter];
		v[1] = in[i];

		cc = _wind_combining_class(in[i]);
		if (old_cc != cc && (comb = find_composition(v, 2))) {
		    out[ostarter] = comb;
		} else if (cc == 0) {
		    break;
		} else {
		    if (o >= *out_len)
			return WIND_ERR_OVERRUN;
		    out[o++] = in[i];
		    old_cc   = cc;
		}
		++i;
	    }
	}
    }
    *out_len = o;
    return 0;
}

int
_wind_stringprep_normalize(const uint32_t *in, size_t in_len,
			   uint32_t *out, size_t *out_len)
{
    size_t tmp_len;
    uint32_t *tmp;
    int ret;

    if (in_len == 0) {
	*out_len = 0;
	return 0;
    }

    tmp_len = in_len * 4;
    if (tmp_len < MAX_LENGTH_CANON)
	tmp_len = MAX_LENGTH_CANON;
    tmp = malloc(tmp_len * sizeof(uint32_t));
    if (tmp == NULL)
	return ENOMEM;

    ret = compat_decomp(in, in_len, tmp, &tmp_len);
    if (ret) {
	free(tmp);
	return ret;
    }
    canonical_reorder(tmp, tmp_len);
    ret = combine(tmp, tmp_len, out, out_len);
    free(tmp);
    return ret;
}
