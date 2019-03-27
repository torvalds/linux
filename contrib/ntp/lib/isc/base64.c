/*
 * Copyright (C) 2004, 2005, 2007, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1998-2001, 2003  Internet Software Consortium.
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

/* $Id: base64.c,v 1.34 2009/10/21 23:48:05 tbox Exp $ */

/*! \file */

#include <config.h>

#include <isc/base64.h>
#include <isc/buffer.h>
#include <isc/lex.h>
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

static const char base64[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
/*@}*/

isc_result_t
isc_base64_totext(isc_region_t *source, int wordlength,
		  const char *wordbreak, isc_buffer_t *target)
{
	char buf[5];
	unsigned int loops = 0;

	if (wordlength < 4)
		wordlength = 4;

	memset(buf, 0, sizeof(buf));
	while (source->length > 2) {
		buf[0] = base64[(source->base[0]>>2)&0x3f];
		buf[1] = base64[((source->base[0]<<4)&0x30)|
				((source->base[1]>>4)&0x0f)];
		buf[2] = base64[((source->base[1]<<2)&0x3c)|
				((source->base[2]>>6)&0x03)];
		buf[3] = base64[source->base[2]&0x3f];
		RETERR(str_totext(buf, target));
		isc_region_consume(source, 3);

		loops++;
		if (source->length != 0 &&
		    (int)((loops + 1) * 4) >= wordlength)
		{
			loops = 0;
			RETERR(str_totext(wordbreak, target));
		}
	}
	if (source->length == 2) {
		buf[0] = base64[(source->base[0]>>2)&0x3f];
		buf[1] = base64[((source->base[0]<<4)&0x30)|
				((source->base[1]>>4)&0x0f)];
		buf[2] = base64[((source->base[1]<<2)&0x3c)];
		buf[3] = '=';
		RETERR(str_totext(buf, target));
		isc_region_consume(source, 2);
	} else if (source->length == 1) {
		buf[0] = base64[(source->base[0]>>2)&0x3f];
		buf[1] = base64[((source->base[0]<<4)&0x30)];
		buf[2] = buf[3] = '=';
		RETERR(str_totext(buf, target));
		isc_region_consume(source, 1);
	}
	return (ISC_R_SUCCESS);
}

/*%
 * State of a base64 decoding process in progress.
 */
typedef struct {
	int length;		/*%< Desired length of binary data or -1 */
	isc_buffer_t *target;	/*%< Buffer for resulting binary data */
	int digits;		/*%< Number of buffered base64 digits */
	isc_boolean_t seen_end;	/*%< True if "=" end marker seen */
	int val[4];
} base64_decode_ctx_t;

static inline void
base64_decode_init(base64_decode_ctx_t *ctx, int length, isc_buffer_t *target)
{
	ctx->digits = 0;
	ctx->seen_end = ISC_FALSE;
	ctx->length = length;
	ctx->target = target;
}

static inline isc_result_t
base64_decode_char(base64_decode_ctx_t *ctx, int c) {
	char *s;

	if (ctx->seen_end)
		return (ISC_R_BADBASE64);
	if ((s = strchr(base64, c)) == NULL)
		return (ISC_R_BADBASE64);
	ctx->val[ctx->digits++] = s - base64;
	if (ctx->digits == 4) {
		int n;
		unsigned char buf[3];
		if (ctx->val[0] == 64 || ctx->val[1] == 64)
			return (ISC_R_BADBASE64);
		if (ctx->val[2] == 64 && ctx->val[3] != 64)
			return (ISC_R_BADBASE64);
		/*
		 * Check that bits that should be zero are.
		 */
		if (ctx->val[2] == 64 && (ctx->val[1] & 0xf) != 0)
			return (ISC_R_BADBASE64);
		/*
		 * We don't need to test for ctx->val[2] != 64 as
		 * the bottom two bits of 64 are zero.
		 */
		if (ctx->val[3] == 64 && (ctx->val[2] & 0x3) != 0)
			return (ISC_R_BADBASE64);
		n = (ctx->val[2] == 64) ? 1 :
			(ctx->val[3] == 64) ? 2 : 3;
		if (n != 3) {
			ctx->seen_end = ISC_TRUE;
			if (ctx->val[2] == 64)
				ctx->val[2] = 0;
			if (ctx->val[3] == 64)
				ctx->val[3] = 0;
		}
		buf[0] = (ctx->val[0]<<2)|(ctx->val[1]>>4);
		buf[1] = (ctx->val[1]<<4)|(ctx->val[2]>>2);
		buf[2] = (ctx->val[2]<<6)|(ctx->val[3]);
		RETERR(mem_tobuffer(ctx->target, buf, n));
		if (ctx->length >= 0) {
			if (n > ctx->length)
				return (ISC_R_BADBASE64);
			else
				ctx->length -= n;
		}
		ctx->digits = 0;
	}
	return (ISC_R_SUCCESS);
}

static inline isc_result_t
base64_decode_finish(base64_decode_ctx_t *ctx) {
	if (ctx->length > 0)
		return (ISC_R_UNEXPECTEDEND);
	if (ctx->digits != 0)
		return (ISC_R_BADBASE64);
	return (ISC_R_SUCCESS);
}

isc_result_t
isc_base64_tobuffer(isc_lex_t *lexer, isc_buffer_t *target, int length) {
	base64_decode_ctx_t ctx;
	isc_textregion_t *tr;
	isc_token_t token;
	isc_boolean_t eol;

	base64_decode_init(&ctx, length, target);

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
			RETERR(base64_decode_char(&ctx, tr->base[i]));
	}
	if (ctx.length < 0 && !ctx.seen_end)
		isc_lex_ungettoken(lexer, &token);
	RETERR(base64_decode_finish(&ctx));
	return (ISC_R_SUCCESS);
}

isc_result_t
isc_base64_decodestring(const char *cstr, isc_buffer_t *target) {
	base64_decode_ctx_t ctx;

	base64_decode_init(&ctx, -1, target);
	for (;;) {
		int c = *cstr++;
		if (c == '\0')
			break;
		if (c == ' ' || c == '\t' || c == '\n' || c== '\r')
			continue;
		RETERR(base64_decode_char(&ctx, c));
	}
	RETERR(base64_decode_finish(&ctx));
	return (ISC_R_SUCCESS);
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
