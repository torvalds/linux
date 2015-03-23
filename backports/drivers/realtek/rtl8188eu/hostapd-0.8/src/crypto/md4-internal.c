/*
 * MD4 hash implementation
 * Copyright (c) 2006, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"

#include "common.h"
#include "crypto.h"

#define	MD4_BLOCK_LENGTH		64
#define	MD4_DIGEST_LENGTH		16

typedef struct MD4Context {
	u32 state[4];			/* state */
	u64 count;			/* number of bits, mod 2^64 */
	u8 buffer[MD4_BLOCK_LENGTH];	/* input buffer */
} MD4_CTX;


static void MD4Init(MD4_CTX *ctx);
static void MD4Update(MD4_CTX *ctx, const unsigned char *input, size_t len);
static void MD4Final(unsigned char digest[MD4_DIGEST_LENGTH], MD4_CTX *ctx);


int md4_vector(size_t num_elem, const u8 *addr[], const size_t *len, u8 *mac)
{
	MD4_CTX ctx;
	size_t i;

	MD4Init(&ctx);
	for (i = 0; i < num_elem; i++)
		MD4Update(&ctx, addr[i], len[i]);
	MD4Final(mac, &ctx);
	return 0;
}


/* ===== start - public domain MD4 implementation ===== */
/*	$OpenBSD: md4.c,v 1.7 2005/08/08 08:05:35 espie Exp $	*/

/*
 * This code implements the MD4 message-digest algorithm.
 * The algorithm is due to Ron Rivest.	This code was
 * written by Colin Plumb in 1993, no copyright is claimed.
 * This code is in the public domain; do with it what you wish.
 * Todd C. Miller modified the MD5 code to do MD4 based on RFC 1186.
 *
 * Equivalent code is available from RSA Data Security, Inc.
 * This code has been tested against that, and is equivalent,
 * except that you don't need to include two pages of legalese
 * with every copy.
 *
 * To compute the message digest of a chunk of bytes, declare an
 * MD4Context structure, pass it to MD4Init, call MD4Update as
 * needed on buffers full of bytes, and then call MD4Final, which
 * will fill a supplied 16-byte array with the digest.
 */

#define	MD4_DIGEST_STRING_LENGTH	(MD4_DIGEST_LENGTH * 2 + 1)


static void
MD4Transform(u32 state[4], const u8 block[MD4_BLOCK_LENGTH]);

#define PUT_64BIT_LE(cp, value) do {					\
	(cp)[7] = (value) >> 56;					\
	(cp)[6] = (value) >> 48;					\
	(cp)[5] = (value) >> 40;					\
	(cp)[4] = (value) >> 32;					\
	(cp)[3] = (value) >> 24;					\
	(cp)[2] = (value) >> 16;					\
	(cp)[1] = (value) >> 8;						\
	(cp)[0] = (value); } while (0)

#define PUT_32BIT_LE(cp, value) do {					\
	(cp)[3] = (value) >> 24;					\
	(cp)[2] = (value) >> 16;					\
	(cp)[1] = (value) >> 8;						\
	(cp)[0] = (value); } while (0)

static u8 PADDING[MD4_BLOCK_LENGTH] = {
	0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/*
 * Start MD4 accumulation.
 * Set bit count to 0 and buffer to mysterious initialization constants.
 */
static void MD4Init(MD4_CTX *ctx)
{
	ctx->count = 0;
	ctx->state[0] = 0x67452301;
	ctx->state[1] = 0xefcdab89;
	ctx->state[2] = 0x98badcfe;
	ctx->state[3] = 0x10325476;
}

/*
 * Update context to reflect the concatenation of another buffer full
 * of bytes.
 */
static void MD4Update(MD4_CTX *ctx, const unsigned char *input, size_t len)
{
	size_t have, need;

	/* Check how many bytes we already have and how many more we need. */
	have = (size_t)((ctx->count >> 3) & (MD4_BLOCK_LENGTH - 1));
	need = MD4_BLOCK_LENGTH - have;

	/* Update bitcount */
	ctx->count += (u64)len << 3;

	if (len >= need) {
		if (have != 0) {
			os_memcpy(ctx->buffer + have, input, need);
			MD4Transform(ctx->state, ctx->buffer);
			input += need;
			len -= need;
			have = 0;
		}

		/* Process data in MD4_BLOCK_LENGTH-byte chunks. */
		while (len >= MD4_BLOCK_LENGTH) {
			MD4Transform(ctx->state, input);
			input += MD4_BLOCK_LENGTH;
			len -= MD4_BLOCK_LENGTH;
		}
	}

	/* Handle any remaining bytes of data. */
	if (len != 0)
		os_memcpy(ctx->buffer + have, input, len);
}

/*
 * Pad pad to 64-byte boundary with the bit pattern
 * 1 0* (64-bit count of bits processed, MSB-first)
 */
static void MD4Pad(MD4_CTX *ctx)
{
	u8 count[8];
	size_t padlen;

	/* Convert count to 8 bytes in little endian order. */
	PUT_64BIT_LE(count, ctx->count);

	/* Pad out to 56 mod 64. */
	padlen = MD4_BLOCK_LENGTH -
	    ((ctx->count >> 3) & (MD4_BLOCK_LENGTH - 1));
	if (padlen < 1 + 8)
		padlen += MD4_BLOCK_LENGTH;
	MD4Update(ctx, PADDING, padlen - 8);		/* padlen - 8 <= 64 */
	MD4Update(ctx, count, 8);
}

/*
 * Final wrapup--call MD4Pad, fill in digest and zero out ctx.
 */
static void MD4Final(unsigned char digest[MD4_DIGEST_LENGTH], MD4_CTX *ctx)
{
	int i;

	MD4Pad(ctx);
	if (digest != NULL) {
		for (i = 0; i < 4; i++)
			PUT_32BIT_LE(digest + i * 4, ctx->state[i]);
		os_memset(ctx, 0, sizeof(*ctx));
	}
}


/* The three core functions - F1 is optimized somewhat */

/* #define F1(x, y, z) (x & y | ~x & z) */
#define F1(x, y, z) (z ^ (x & (y ^ z)))
#define F2(x, y, z) ((x & y) | (x & z) | (y & z))
#define F3(x, y, z) (x ^ y ^ z)

/* This is the central step in the MD4 algorithm. */
#define MD4STEP(f, w, x, y, z, data, s) \
	( w += f(x, y, z) + data,  w = w<<s | w>>(32-s) )

/*
 * The core of the MD4 algorithm, this alters an existing MD4 hash to
 * reflect the addition of 16 longwords of new data.  MD4Update blocks
 * the data and converts bytes into longwords for this routine.
 */
static void
MD4Transform(u32 state[4], const u8 block[MD4_BLOCK_LENGTH])
{
	u32 a, b, c, d, in[MD4_BLOCK_LENGTH / 4];

#if BYTE_ORDER == LITTLE_ENDIAN
	os_memcpy(in, block, sizeof(in));
#else
	for (a = 0; a < MD4_BLOCK_LENGTH / 4; a++) {
		in[a] = (u32)(
		    (u32)(block[a * 4 + 0]) |
		    (u32)(block[a * 4 + 1]) <<  8 |
		    (u32)(block[a * 4 + 2]) << 16 |
		    (u32)(block[a * 4 + 3]) << 24);
	}
#endif

	a = state[0];
	b = state[1];
	c = state[2];
	d = state[3];

	MD4STEP(F1, a, b, c, d, in[ 0],  3);
	MD4STEP(F1, d, a, b, c, in[ 1],  7);
	MD4STEP(F1, c, d, a, b, in[ 2], 11);
	MD4STEP(F1, b, c, d, a, in[ 3], 19);
	MD4STEP(F1, a, b, c, d, in[ 4],  3);
	MD4STEP(F1, d, a, b, c, in[ 5],  7);
	MD4STEP(F1, c, d, a, b, in[ 6], 11);
	MD4STEP(F1, b, c, d, a, in[ 7], 19);
	MD4STEP(F1, a, b, c, d, in[ 8],  3);
	MD4STEP(F1, d, a, b, c, in[ 9],  7);
	MD4STEP(F1, c, d, a, b, in[10], 11);
	MD4STEP(F1, b, c, d, a, in[11], 19);
	MD4STEP(F1, a, b, c, d, in[12],  3);
	MD4STEP(F1, d, a, b, c, in[13],  7);
	MD4STEP(F1, c, d, a, b, in[14], 11);
	MD4STEP(F1, b, c, d, a, in[15], 19);

	MD4STEP(F2, a, b, c, d, in[ 0] + 0x5a827999,  3);
	MD4STEP(F2, d, a, b, c, in[ 4] + 0x5a827999,  5);
	MD4STEP(F2, c, d, a, b, in[ 8] + 0x5a827999,  9);
	MD4STEP(F2, b, c, d, a, in[12] + 0x5a827999, 13);
	MD4STEP(F2, a, b, c, d, in[ 1] + 0x5a827999,  3);
	MD4STEP(F2, d, a, b, c, in[ 5] + 0x5a827999,  5);
	MD4STEP(F2, c, d, a, b, in[ 9] + 0x5a827999,  9);
	MD4STEP(F2, b, c, d, a, in[13] + 0x5a827999, 13);
	MD4STEP(F2, a, b, c, d, in[ 2] + 0x5a827999,  3);
	MD4STEP(F2, d, a, b, c, in[ 6] + 0x5a827999,  5);
	MD4STEP(F2, c, d, a, b, in[10] + 0x5a827999,  9);
	MD4STEP(F2, b, c, d, a, in[14] + 0x5a827999, 13);
	MD4STEP(F2, a, b, c, d, in[ 3] + 0x5a827999,  3);
	MD4STEP(F2, d, a, b, c, in[ 7] + 0x5a827999,  5);
	MD4STEP(F2, c, d, a, b, in[11] + 0x5a827999,  9);
	MD4STEP(F2, b, c, d, a, in[15] + 0x5a827999, 13);

	MD4STEP(F3, a, b, c, d, in[ 0] + 0x6ed9eba1,  3);
	MD4STEP(F3, d, a, b, c, in[ 8] + 0x6ed9eba1,  9);
	MD4STEP(F3, c, d, a, b, in[ 4] + 0x6ed9eba1, 11);
	MD4STEP(F3, b, c, d, a, in[12] + 0x6ed9eba1, 15);
	MD4STEP(F3, a, b, c, d, in[ 2] + 0x6ed9eba1,  3);
	MD4STEP(F3, d, a, b, c, in[10] + 0x6ed9eba1,  9);
	MD4STEP(F3, c, d, a, b, in[ 6] + 0x6ed9eba1, 11);
	MD4STEP(F3, b, c, d, a, in[14] + 0x6ed9eba1, 15);
	MD4STEP(F3, a, b, c, d, in[ 1] + 0x6ed9eba1,  3);
	MD4STEP(F3, d, a, b, c, in[ 9] + 0x6ed9eba1,  9);
	MD4STEP(F3, c, d, a, b, in[ 5] + 0x6ed9eba1, 11);
	MD4STEP(F3, b, c, d, a, in[13] + 0x6ed9eba1, 15);
	MD4STEP(F3, a, b, c, d, in[ 3] + 0x6ed9eba1,  3);
	MD4STEP(F3, d, a, b, c, in[11] + 0x6ed9eba1,  9);
	MD4STEP(F3, c, d, a, b, in[ 7] + 0x6ed9eba1, 11);
	MD4STEP(F3, b, c, d, a, in[15] + 0x6ed9eba1, 15);

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
}
/* ===== end - public domain MD4 implementation ===== */
