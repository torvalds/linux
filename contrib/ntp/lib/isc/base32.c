/*
 * Copyright (C) 2008, 2009  Internet Systems Consortium, Inc. ("ISC")
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: base32.c,v 1.6 2009/10/21 01:22:29 each Exp $ */

/*! \file */

#include <config.h>

#include <isc/base32.h>
#include <isc/buffer.h>
#include <isc/lex.h>
#include <isc/region.h>
#include <isc/string.h>
#include <isc/util.h>

#define RETERR(x) do { \
	isc_result_t _r = (x); \
	if (_r != ISC_R_SUCCESS) \
		return (_r); \
	} while (0)


/*@{*/
/*!
 * These static functions are also present in lib/dns/rdata.c.  I'm not
 * sure where they should go. -- bwelling
 */
static isc_result_t
str_totext(const char *source, isc_buffer_t *target);

static isc_result_t
mem_tobuffer(isc_buffer_t *target, void *base, unsigned int length);

/*@}*/

static const char base32[] =
	 "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567=abcdefghijklmnopqrstuvwxyz234567";
static const char base32hex[] =
	"0123456789ABCDEFGHIJKLMNOPQRSTUV=0123456789abcdefghijklmnopqrstuv";

static isc_result_t
base32_totext(isc_region_t *source, int wordlength, const char *wordbreak,
	      isc_buffer_t *target, const char base[])
{
	char buf[9];
	unsigned int loops = 0;

	if (wordlength >= 0 && wordlength < 8)
		wordlength = 8;

	memset(buf, 0, sizeof(buf));
	while (source->length > 0) {
		buf[0] = base[((source->base[0]>>3)&0x1f)];	/* 5 + */
		if (source->length == 1) {
			buf[1] = base[(source->base[0]<<2)&0x1c];
			buf[2] = buf[3] = buf[4] = '=';
			buf[5] = buf[6] = buf[7] = '=';
			RETERR(str_totext(buf, target));
			break;
		}
		buf[1] = base[((source->base[0]<<2)&0x1c)|	/* 3 = 8 */
			      ((source->base[1]>>6)&0x03)];	/* 2 + */
		buf[2] = base[((source->base[1]>>1)&0x1f)];	/* 5 + */
		if (source->length == 2) {
			buf[3] = base[(source->base[1]<<4)&0x10];
			buf[4] = buf[5] = buf[6] = buf[7] = '=';
			RETERR(str_totext(buf, target));
			break;
		}
		buf[3] = base[((source->base[1]<<4)&0x10)|	/* 1 = 8 */
			      ((source->base[2]>>4)&0x0f)];	/* 4 + */
		if (source->length == 3) {
			buf[4] = base[(source->base[2]<<1)&0x1e];
			buf[5] = buf[6] = buf[7] = '=';
			RETERR(str_totext(buf, target));
			break;
		}
		buf[4] = base[((source->base[2]<<1)&0x1e)|	/* 4 = 8 */
			      ((source->base[3]>>7)&0x01)];	/* 1 + */
		buf[5] = base[((source->base[3]>>2)&0x1f)];	/* 5 + */
		if (source->length == 4) {
			buf[6] = base[(source->base[3]<<3)&0x18];
			buf[7] = '=';
			RETERR(str_totext(buf, target));
			break;
		}
		buf[6] = base[((source->base[3]<<3)&0x18)|	/* 2 = 8 */
			      ((source->base[4]>>5)&0x07)];	/* 3 + */
		buf[7] = base[source->base[4]&0x1f];		/* 5 = 8 */
		RETERR(str_totext(buf, target));
		isc_region_consume(source, 5);

		loops++;
		if (source->length != 0 && wordlength >= 0 &&
		    (int)((loops + 1) * 8) >= wordlength)
		{
			loops = 0;
			RETERR(str_totext(wordbreak, target));
		}
	}
	if (source->length > 0)
		isc_region_consume(source, source->length);
	return (ISC_R_SUCCESS);
}

isc_result_t
isc_base32_totext(isc_region_t *source, int wordlength,
		  const char *wordbreak, isc_buffer_t *target)
{
	return (base32_totext(source, wordlength, wordbreak, target, base32));
}

isc_result_t
isc_base32hex_totext(isc_region_t *source, int wordlength,
		     const char *wordbreak, isc_buffer_t *target)
{
	return (base32_totext(source, wordlength, wordbreak, target,
			      base32hex));
}

/*%
 * State of a base32 decoding process in progress.
 */
typedef struct {
	int length;		/*%< Desired length of binary data or -1 */
	isc_buffer_t *target;	/*%< Buffer for resulting binary data */
	int digits;		/*%< Number of buffered base32 digits */
	isc_boolean_t seen_end;	/*%< True if "=" end marker seen */
	int val[8];
	const char *base;	/*%< Which encoding we are using */
	int seen_32;		/*%< Number of significant bytes if non zero */
} base32_decode_ctx_t;

static inline void
base32_decode_init(base32_decode_ctx_t *ctx, int length,
		   const char base[], isc_buffer_t *target)
{
	ctx->digits = 0;
	ctx->seen_end = ISC_FALSE;
	ctx->seen_32 = 0;
	ctx->length = length;
	ctx->target = target;
	ctx->base = base;
}

static inline isc_result_t
base32_decode_char(base32_decode_ctx_t *ctx, int c) {
	char *s;
	unsigned int last;

	if (ctx->seen_end)
		return (ISC_R_BADBASE32);
	if ((s = strchr(ctx->base, c)) == NULL)
		return (ISC_R_BADBASE32);
	last = s - ctx->base;
	/*
	 * Handle lower case.
	 */
	if (last > 32)
		last -= 33;
	/*
	 * Check that padding is contiguous.
	 */
	if (last != 32 && ctx->seen_32 != 0)
		return (ISC_R_BADBASE32);
	/*
	 * Check that padding starts at the right place and that
	 * bits that should be zero are.
	 * Record how many significant bytes in answer (seen_32).
	 */
	if (last == 32 && ctx->seen_32 == 0)
		switch (ctx->digits) {
		case 0:
		case 1:
			return (ISC_R_BADBASE32);
		case 2:
			if ((ctx->val[1]&0x03) != 0)
				return (ISC_R_BADBASE32);
			ctx->seen_32 = 1;
			break;
		case 3:
			return (ISC_R_BADBASE32);
		case 4:
			if ((ctx->val[3]&0x0f) != 0)
				return (ISC_R_BADBASE32);
			ctx->seen_32 = 3;
			break;
		case 5:
			if ((ctx->val[4]&0x01) != 0)
				return (ISC_R_BADBASE32);
			ctx->seen_32 = 3;
			break;
		case 6:
			return (ISC_R_BADBASE32);
		case 7:
			if ((ctx->val[6]&0x07) != 0)
				return (ISC_R_BADBASE32);
			ctx->seen_32 = 4;
			break;
		}
	/*
	 * Zero fill pad values.
	 */
	ctx->val[ctx->digits++] = (last == 32) ? 0 : last;

	if (ctx->digits == 8) {
		int n = 5;
		unsigned char buf[5];

		if (ctx->seen_32 != 0) {
			ctx->seen_end = ISC_TRUE;
			n = ctx->seen_32;
		}
		buf[0] = (ctx->val[0]<<3)|(ctx->val[1]>>2);
		buf[1] = (ctx->val[1]<<6)|(ctx->val[2]<<1)|(ctx->val[3]>>4);
		buf[2] = (ctx->val[3]<<4)|(ctx->val[4]>>1);
		buf[3] = (ctx->val[4]<<7)|(ctx->val[5]<<2)|(ctx->val[6]>>3);
		buf[4] = (ctx->val[6]<<5)|(ctx->val[7]);
		RETERR(mem_tobuffer(ctx->target, buf, n));
		if (ctx->length >= 0) {
			if (n > ctx->length)
				return (ISC_R_BADBASE32);
			else
				ctx->length -= n;
		}
		ctx->digits = 0;
	}
	return (ISC_R_SUCCESS);
}

static inline isc_result_t
base32_decode_finish(base32_decode_ctx_t *ctx) {
	if (ctx->length > 0)
		return (ISC_R_UNEXPECTEDEND);
	if (ctx->digits != 0)
		return (ISC_R_BADBASE32);
	return (ISC_R_SUCCESS);
}

static isc_result_t
base32_tobuffer(isc_lex_t *lexer, const char base[], isc_buffer_t *target,
		int length)
{
	base32_decode_ctx_t ctx;
	isc_textregion_t *tr;
	isc_token_t token;
	isc_boolean_t eol;

	base32_decode_init(&ctx, length, base, target);

	while (!ctx.seen_end && (ctx.length != 0)) {
		unsigned int i;

		if (length > 0)
			eol = ISC_FALSE;
		else
			eol = ISC_TRUE;
		RETERR(isc_lex_getmastertoken(lexer, &token,
					      isc_tokentype_string, eol));
		if (token.type != isc_tokentype_string)
			break;
		tr = &token.value.as_textregion;
		for (i = 0; i < tr->length; i++)
			RETERR(base32_decode_char(&ctx, tr->base[i]));
	}
	if (ctx.length < 0 && !ctx.seen_end)
		isc_lex_ungettoken(lexer, &token);
	RETERR(base32_decode_finish(&ctx));
	return (ISC_R_SUCCESS);
}

isc_result_t
isc_base32_tobuffer(isc_lex_t *lexer, isc_buffer_t *target, int length) {
	return (base32_tobuffer(lexer, base32, target, length));
}

isc_result_t
isc_base32hex_tobuffer(isc_lex_t *lexer, isc_buffer_t *target, int length) {
	return (base32_tobuffer(lexer, base32hex, target, length));
}

static isc_result_t
base32_decodestring(const char *cstr, const char base[], isc_buffer_t *target) {
	base32_decode_ctx_t ctx;

	base32_decode_init(&ctx, -1, base, target);
	for (;;) {
		int c = *cstr++;
		if (c == '\0')
			break;
		if (c == ' ' || c == '\t' || c == '\n' || c== '\r')
			continue;
		RETERR(base32_decode_char(&ctx, c));
	}
	RETERR(base32_decode_finish(&ctx));
	return (ISC_R_SUCCESS);
}

isc_result_t
isc_base32_decodestring(const char *cstr, isc_buffer_t *target) {
	return (base32_decodestring(cstr, base32, target));
}

isc_result_t
isc_base32hex_decodestring(const char *cstr, isc_buffer_t *target) {
	return (base32_decodestring(cstr, base32hex, target));
}

static isc_result_t
base32_decoderegion(isc_region_t *source, const char base[], isc_buffer_t *target) {
	base32_decode_ctx_t ctx;

	base32_decode_init(&ctx, -1, base, target);
	while (source->length != 0) {
		int c = *source->base;
		RETERR(base32_decode_char(&ctx, c));
		isc_region_consume(source, 1);
	}
	RETERR(base32_decode_finish(&ctx));
	return (ISC_R_SUCCESS);
}

isc_result_t
isc_base32_decoderegion(isc_region_t *source, isc_buffer_t *target) {
	return (base32_decoderegion(source, base32, target));
}

isc_result_t
isc_base32hex_decoderegion(isc_region_t *source, isc_buffer_t *target) {
	return (base32_decoderegion(source, base32hex, target));
}

static isc_result_t
str_totext(const char *source, isc_buffer_t *target) {
	unsigned int l;
	isc_region_t region;

	isc_buffer_availableregion(target, &region);
	l = strlen(source);

	if (l > region.length)
		return (ISC_R_NOSPACE);

	memcpy(region.base, source, l);
	isc_buffer_add(target, l);
	return (ISC_R_SUCCESS);
}

static isc_result_t
mem_tobuffer(isc_buffer_t *target, void *base, unsigned int length) {
	isc_region_t tr;

	isc_buffer_availableregion(target, &tr);
	if (length > tr.length)
		return (ISC_R_NOSPACE);
	memcpy(tr.base, base, length);
	isc_buffer_add(target, length);
	return (ISC_R_SUCCESS);
}
