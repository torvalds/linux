/*-
 * Copyright (c) 2016 Christos Zoulas
 * All rights reserved.
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
/*
 * DER (Distinguished Encoding Rules) Parser
 *
 * Sources:
 * https://en.wikipedia.org/wiki/X.690
 * http://fm4dd.com/openssl/certexamples.htm
 * http://blog.engelke.com/2014/10/17/parsing-ber-and-der-encoded-asn-1-objects/
 */
#ifndef TEST_DER
#include "file.h"

#ifndef lint
FILE_RCSID("@(#)$File: der.c,v 1.13 2018/06/23 15:15:26 christos Exp $")
#endif
#endif

#include <sys/types.h>

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef TEST_DER
#include "magic.h"
#include "der.h"
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <err.h>
#endif

#define DER_BAD	((uint32_t)-1)

#define DER_CLASS_UNIVERSAL	0
#define	DER_CLASS_APPLICATION	1
#define	DER_CLASS_CONTEXT	2
#define	DER_CLASS_PRIVATE	3
#ifdef DEBUG_DER
static const char der_class[] = "UACP";
#endif

#define DER_TYPE_PRIMITIVE	0
#define DER_TYPE_CONSTRUCTED	1
#ifdef DEBUG_DER
static const char der_type[] = "PC";
#endif

#define	DER_TAG_EOC			0x00
#define	DER_TAG_BOOLEAN			0x01
#define	DER_TAG_INTEGER			0x02
#define	DER_TAG_BIT STRING		0x03
#define	DER_TAG_OCTET_STRING		0x04
#define	DER_TAG_NULL			0x05
#define	DER_TAG_OBJECT_IDENTIFIER	0x06
#define	DER_TAG_OBJECT_DESCRIPTOR	0x07
#define	DER_TAG_EXTERNAL		0x08
#define	DER_TAG_REAL			0x09
#define	DER_TAG_ENUMERATED		0x0a
#define	DER_TAG_EMBEDDED_PDV		0x0b
#define	DER_TAG_UTF8_STRING		0x0c
#define	DER_TAG_RELATIVE_OID		0x0d
#define DER_TAG_RESERVED_1		0x0e
#define DER_TAG_RESERVED_2		0x0f
#define	DER_TAG_SEQUENCE		0x10
#define	DER_TAG_SET			0x11
#define	DER_TAG_NUMERIC_STRING		0x12
#define	DER_TAG_PRINTABLE_STRING	0x13
#define	DER_TAG_T61_STRING		0x14
#define	DER_TAG_VIDEOTEX_STRING		0x15
#define	DER_TAG_IA5_STRING		0x16
#define	DER_TAG_UTCTIME			0x17
#define	DER_TAG_GENERALIZED_TIME	0x18
#define	DER_TAG_GRAPHIC_STRING		0x19
#define	DER_TAG_VISIBLE_STRING		0x1a
#define	DER_TAG_GENERAL_STRING		0x1b
#define	DER_TAG_UNIVERSAL_STRING	0x1c
#define	DER_TAG_CHARACTER_STRING	0x1d
#define	DER_TAG_BMP_STRING		0x1e
#define	DER_TAG_LONG			0x1f

static const char *der__tag[] = {
	"eoc", "bool", "int", "bit_str", "octet_str",
	"null", "obj_id", "obj_desc", "ext", "real",
	"enum", "embed", "utf8_str", "oid", "res1",
	"res2", "seq", "set", "num_str", "prt_str",
	"t61_str", "vid_str", "ia5_str", "utc_time",
	"gen_time", "gr_str", "vis_str", "gen_str",
	"char_str", "bmp_str", "long"
};

#ifdef DEBUG_DER
#define DPRINTF(a) printf a
#else
#define DPRINTF(a)
#endif

#ifdef TEST_DER
static uint8_t
getclass(uint8_t c)
{
	return c >> 6;
}

static uint8_t
gettype(uint8_t c)
{
	return (c >> 5) & 1;
}
#endif

static uint32_t
gettag(const uint8_t *c, size_t *p, size_t l)
{
	uint32_t tag;

	if (*p >= l)
		return DER_BAD;

	tag = c[(*p)++] & 0x1f;

	if (tag != 0x1f)
		return tag;

	if (*p >= l)
		return DER_BAD;

	while (c[*p] >= 0x80) {
		tag = tag * 128 + c[(*p)++] - 0x80;
		if (*p >= l)
			return DER_BAD;
	}
	return tag;
}

/*
 * Read the length of a DER tag from the input.
 *
 * `c` is the input, `p` is an output parameter that specifies how much of the
 * input we consumed, and `l` is the maximum input length.
 *
 * Returns the length, or DER_BAD if the end of the input is reached or the
 * length exceeds the remaining input.
 */
static uint32_t
getlength(const uint8_t *c, size_t *p, size_t l)
{
	uint8_t digits, i;
	size_t len;
	int is_onebyte_result;

	if (*p >= l)
		return DER_BAD;

	/*
	 * Digits can either be 0b0 followed by the result, or 0b1
	 * followed by the number of digits of the result. In either case,
	 * we verify that we can read so many bytes from the input.
	 */
	is_onebyte_result = (c[*p] & 0x80) == 0;
	digits = c[(*p)++] & 0x7f;
	if (*p + digits >= l)
		return DER_BAD;

	if (is_onebyte_result)
		return digits;

	/*
	 * Decode len. We've already verified that we're allowed to read
	 * `digits` bytes.
	 */
	len = 0;
	for (i = 0; i < digits; i++)
		len = (len << 8) | c[(*p)++];

	if (len > UINT32_MAX - *p || *p + len >= l)
		return DER_BAD;
	return CAST(uint32_t, len);
}

static const char *
der_tag(char *buf, size_t len, uint32_t tag)
{
	if (tag < DER_TAG_LONG) 
		strlcpy(buf, der__tag[tag], len);
	else
		snprintf(buf, len, "%#x", tag);
	return buf;
}

#ifndef TEST_DER
static int
der_data(char *buf, size_t blen, uint32_t tag, const void *q, uint32_t len)
{
	const uint8_t *d = CAST(const uint8_t *, q);
	switch (tag) {
	case DER_TAG_PRINTABLE_STRING:
	case DER_TAG_UTF8_STRING:
	case DER_TAG_IA5_STRING:
	case DER_TAG_UTCTIME:
		return snprintf(buf, blen, "%.*s", len, (const char *)q);
	default:
		break;
	}
		
	for (uint32_t i = 0; i < len; i++) {
		uint32_t z = i << 1;
		if (z < blen - 2)
			snprintf(buf + z, blen - z, "%.2x", d[i]);
	}
	return len * 2;
}

int32_t
der_offs(struct magic_set *ms, struct magic *m, size_t nbytes)
{
	const uint8_t *b = RCAST(const uint8_t *, ms->search.s);
	size_t offs = 0, len = ms->search.s_len ? ms->search.s_len : nbytes;

	if (gettag(b, &offs, len) == DER_BAD)
		return -1;
	DPRINTF(("%s1: %d %zu %u\n", __func__, ms->offset, offs, m->offset));

	uint32_t tlen = getlength(b, &offs, len);
	if (tlen == DER_BAD)
		return -1;
	DPRINTF(("%s2: %d %zu %u\n", __func__, ms->offset, offs, tlen));

	offs += ms->offset + m->offset;
	DPRINTF(("cont_level = %d\n", m->cont_level));
#ifdef DEBUG_DER
	for (size_t i = 0; i < m->cont_level; i++)
		printf("cont_level[%zu] = %u\n", i, ms->c.li[i].off);
#endif
	if (m->cont_level != 0) {
		if (offs + tlen > nbytes)
			return -1;
		ms->c.li[m->cont_level - 1].off = CAST(int, offs + tlen);
		DPRINTF(("cont_level[%u] = %u\n", m->cont_level - 1,
		    ms->c.li[m->cont_level - 1].off));
	}
	return CAST(int32_t, offs);
}

int
der_cmp(struct magic_set *ms, struct magic *m)
{
	const uint8_t *b = RCAST(const uint8_t *, ms->search.s);
	const char *s = m->value.s;
	size_t offs = 0, len = ms->search.s_len;
	uint32_t tag, tlen;
	char buf[128];

	tag = gettag(b, &offs, len);
	if (tag == DER_BAD)
		return -1;

	tlen = getlength(b, &offs, len);
	if (tlen == DER_BAD)
		return -1;

	der_tag(buf, sizeof(buf), tag);
	if ((ms->flags & MAGIC_DEBUG) != 0)
		fprintf(stderr, "%s: tag %p got=%s exp=%s\n", __func__, b,
		    buf, s);
	size_t slen = strlen(buf);

	if (strncmp(buf, s, slen) != 0)
		return 0;

	s += slen;

again:
	switch (*s) {
	case '\0':
		return 1;
	case '=':
		s++;
		goto val;
	default:
		if (!isdigit((unsigned char)*s))
			return 0;

		slen = 0;
		do
			slen = slen * 10 + *s - '0';
		while (isdigit((unsigned char)*++s));
		if ((ms->flags & MAGIC_DEBUG) != 0)
			fprintf(stderr, "%s: len %zu %u\n", __func__,
			    slen, tlen);
		if (tlen != slen)
			return 0;
		goto again;
	}
val:
	DPRINTF(("%s: before data %zu %u\n", __func__, offs, tlen));
	der_data(buf, sizeof(buf), tag, b + offs, tlen);
	if ((ms->flags & MAGIC_DEBUG) != 0)
		fprintf(stderr, "%s: data %s %s\n", __func__, buf, s);
	if (strcmp(buf, s) != 0 && strcmp("x", s) != 0)
		return 0;
	strlcpy(ms->ms_value.s, buf, sizeof(ms->ms_value.s));
	return 1;
}
#endif

#ifdef TEST_DER
static void
printtag(uint32_t tag, const void *q, uint32_t len)
{
	const uint8_t *d = q;
	switch (tag) {
	case DER_TAG_PRINTABLE_STRING:
	case DER_TAG_UTF8_STRING:
		printf("%.*s\n", len, (const char *)q);
		return;
	default:
		break;
	}
		
	for (uint32_t i = 0; i < len; i++)
		printf("%.2x", d[i]);
	printf("\n");
}

static void
printdata(size_t level, const void *v, size_t x, size_t l)
{
	const uint8_t *p = v, *ep = p + l;
	size_t ox;
	char buf[128];

	while (p + x < ep) {
		const uint8_t *q;
		uint8_t c = getclass(p[x]);
		uint8_t t = gettype(p[x]);
		ox = x;
		if (x != 0)
		printf("%.2x %.2x %.2x\n", p[x - 1], p[x], p[x + 1]);
		uint32_t tag = gettag(p, &x, ep - p + x);
		if (p + x >= ep)
			break;
		uint32_t len = getlength(p, &x, ep - p + x);
		
		printf("%zu %zu-%zu %c,%c,%s,%u:", level, ox, x,
		    der_class[c], der_type[t],
		    der_tag(buf, sizeof(buf), tag), len);
		q = p + x;
		if (p + len > ep)
			errx(EXIT_FAILURE, "corrupt der");
		printtag(tag, q, len);
		if (t != DER_TYPE_PRIMITIVE)
			printdata(level + 1, p, x, len + x);
		x += len;
	}
}

int
main(int argc, char *argv[])
{
	int fd;
	struct stat st;
	size_t l;
	void *p;

	if ((fd = open(argv[1], O_RDONLY)) == -1)
		err(EXIT_FAILURE, "open `%s'", argv[1]);
	if (fstat(fd, &st) == -1)
		err(EXIT_FAILURE, "stat `%s'", argv[1]);
	l = (size_t)st.st_size;
	if ((p = mmap(NULL, l, PROT_READ, MAP_FILE, fd, 0)) == MAP_FAILED)
		err(EXIT_FAILURE, "mmap `%s'", argv[1]);

	printdata(0, p, 0, l);
	munmap(p, l);
	return 0;
}
#endif
