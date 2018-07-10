/* This file is not used by busybox right now.
 * However, the code here seems to be a tiny bit smaller
 * than one in md5.c. Need to investigate which one
 * is better overall...
 * Hint: grep for md5prime to find places where you can switch
 * md5.c/md5prime.c
 */

/*
 * MD5C.C - RSA Data Security, Inc., MD5 message-digest algorithm
 *
 * Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
 * rights reserved.
 *
 * License to copy and use this software is granted provided that it
 * is identified as the "RSA Data Security, Inc. MD5 Message-Digest
 * Algorithm" in all material mentioning or referencing this software
 * or this function.
 *
 * License is also granted to make and use derivative works provided
 * that such works are identified as "derived from the RSA Data
 * Security, Inc. MD5 Message-Digest Algorithm" in all material
 * mentioning or referencing the derived work.
 *
 * RSA Data Security, Inc. makes no representations concerning either
 * the merchantability of this software or the suitability of this
 * software for any particular purpose. It is provided "as is"
 * without express or implied warranty of any kind.
 *
 * These notices must be retained in any copies of any part of this
 * documentation and/or software.
 *
 * $FreeBSD: src/lib/libmd/md5c.c,v 1.9.2.1 1999/08/29 14:57:12 peter Exp $
 *
 * This code is the same as the code published by RSA Inc.  It has been
 * edited for clarity and style only.
 *
 * ----------------------------------------------------------------------------
 * The md5_crypt() function was taken from freeBSD's libcrypt and contains
 * this license:
 *    "THE BEER-WARE LICENSE" (Revision 42):
 *     <phk@login.dknet.dk> wrote this file.  As long as you retain this notice you
 *     can do whatever you want with this stuff. If we meet some day, and you think
 *     this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 *
 * $FreeBSD: src/lib/libcrypt/crypt.c,v 1.7.2.1 1999/08/29 14:56:33 peter Exp $
 *
 * ----------------------------------------------------------------------------
 * On April 19th, 2001 md5_crypt() was modified to make it reentrant
 * by Erik Andersen <andersen@uclibc.org>
 *
 * June 28, 2001             Manuel Novoa III
 *
 * "Un-inlined" code using loops and static const tables in order to
 * reduce generated code size (on i386 from approx 4k to approx 2.5k).
 *
 * June 29, 2001             Manuel Novoa III
 *
 * Completely removed static PADDING array.
 *
 * Reintroduced the loop unrolling in md5_transform and added the
 * MD5_SMALL option for configurability.  Define below as:
 *       0    fully unrolled loops
 *       1    partially unrolled (4 ops per loop)
 *       2    no unrolling -- introduces the need to swap 4 variables (slow)
 *       3    no unrolling and all 4 loops merged into one with switch
 *               in each loop (glacial)
 * On i386, sizes are roughly (-Os -fno-builtin):
 *     0: 3k     1: 2.5k     2: 2.2k     3: 2k
 *
 * Since SuSv3 does not require crypt_r, modified again August 7, 2002
 * by Erik Andersen to remove reentrance stuff...
 */

#include "libbb.h"

/* 1: fastest, 3: smallest */
#if CONFIG_MD5_SMALL < 1
# define MD5_SMALL 1
#elif CONFIG_MD5_SMALL > 3
# define MD5_SMALL 3
#else
# define MD5_SMALL CONFIG_MD5_SMALL
#endif

#if BB_LITTLE_ENDIAN
#define memcpy32_cpu2le memcpy
#define memcpy32_le2cpu memcpy
#else
/* Encodes input (uint32_t) into output (unsigned char).
 * Assumes len is a multiple of 4. */
static void
memcpy32_cpu2le(unsigned char *output, uint32_t *input, unsigned len)
{
	unsigned i, j;
	for (i = 0, j = 0; j < len; i++, j += 4) {
		output[j] = input[i];
		output[j+1] = (input[i] >> 8);
		output[j+2] = (input[i] >> 16);
		output[j+3] = (input[i] >> 24);
	}
}
/* Decodes input (unsigned char) into output (uint32_t).
 * Assumes len is a multiple of 4. */
static void
memcpy32_le2cpu(uint32_t *output, const unsigned char *input, unsigned len)
{
	unsigned i, j;
	for (i = 0, j = 0; j < len; i++, j += 4)
		output[i] = ((uint32_t)input[j])
			| (((uint32_t)input[j+1]) << 8)
			| (((uint32_t)input[j+2]) << 16)
			| (((uint32_t)input[j+3]) << 24);
}
#endif /* i386 */

/* F, G, H and I are basic MD5 functions. */
#define F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | ~(z)))

/* rotl32 rotates x left n bits. */
#define rotl32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

/*
 * FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4.
 * Rotation is separate from addition to prevent recomputation.
 */
#define FF(a, b, c, d, x, s, ac) { \
	(a) += F((b), (c), (d)) + (x) + (uint32_t)(ac); \
	(a) = rotl32((a), (s)); \
	(a) += (b); \
	}
#define GG(a, b, c, d, x, s, ac) { \
	(a) += G((b), (c), (d)) + (x) + (uint32_t)(ac); \
	(a) = rotl32((a), (s)); \
	(a) += (b); \
	}
#define HH(a, b, c, d, x, s, ac) { \
	(a) += H((b), (c), (d)) + (x) + (uint32_t)(ac); \
	(a) = rotl32((a), (s)); \
	(a) += (b); \
	}
#define II(a, b, c, d, x, s, ac) { \
	(a) += I((b), (c), (d)) + (x) + (uint32_t)(ac); \
	(a) = rotl32((a), (s)); \
	(a) += (b); \
	}

/* MD5 basic transformation. Transforms state based on block. */
static void md5_transform(uint32_t state[4], const unsigned char block[64])
{
	uint32_t a, b, c, d, x[16];
#if MD5_SMALL > 1
	uint32_t temp;
	const unsigned char *ps;

	static const unsigned char S[] = {
		7, 12, 17, 22,
		5, 9, 14, 20,
		4, 11, 16, 23,
		6, 10, 15, 21
	};
#endif /* MD5_SMALL > 1 */

#if MD5_SMALL > 0
	const uint32_t *pc;
	const unsigned char *pp;
	int i;

	static const uint32_t C[] = {
		/* round 1 */
		0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
		0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
		0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
		0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
		/* round 2 */
		0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
		0xd62f105d, 0x2441453,  0xd8a1e681, 0xe7d3fbc8,
		0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
		0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
		/* round 3 */
		0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
		0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
		0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x4881d05,
		0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
		/* round 4 */
		0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
		0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
		0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
		0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
	};
	static const unsigned char P[] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, /* 1 */
		1, 6, 11, 0, 5, 10, 15, 4, 9, 14, 3, 8, 13, 2, 7, 12, /* 2 */
		5, 8, 11, 14, 1, 4, 7, 10, 13, 0, 3, 6, 9, 12, 15, 2, /* 3 */
		0, 7, 14, 5, 12, 3, 10, 1, 8, 15, 6, 13, 4, 11, 2, 9  /* 4 */
	};

#endif /* MD5_SMALL > 0 */

	memcpy32_le2cpu(x, block, 64);

	a = state[0];
	b = state[1];
	c = state[2];
	d = state[3];

#if MD5_SMALL > 2
	pc = C;
	pp = P;
	ps = S - 4;
	for (i = 0; i < 64; i++) {
		if ((i & 0x0f) == 0) ps += 4;
		temp = a;
		switch (i>>4) {
			case 0:
				temp += F(b, c, d);
				break;
			case 1:
				temp += G(b, c, d);
				break;
			case 2:
				temp += H(b, c, d);
				break;
			case 3:
				temp += I(b, c, d);
				break;
		}
		temp += x[*pp++] + *pc++;
		temp = rotl32(temp, ps[i & 3]);
		temp += b;
		a = d; d = c; c = b; b = temp;
	}
#elif MD5_SMALL > 1
	pc = C;
	pp = P;
	ps = S;
	/* Round 1 */
	for (i = 0; i < 16; i++) {
		FF(a, b, c, d, x[*pp], ps[i & 0x3], *pc); pp++; pc++;
		temp = d; d = c; c = b; b = a; a = temp;
	}
	/* Round 2 */
	ps += 4;
	for (; i < 32; i++) {
		GG(a, b, c, d, x[*pp], ps[i & 0x3], *pc); pp++; pc++;
		temp = d; d = c; c = b; b = a; a = temp;
	}
	/* Round 3 */
	ps += 4;
	for (; i < 48; i++) {
		HH(a, b, c, d, x[*pp], ps[i & 0x3], *pc); pp++; pc++;
		temp = d; d = c; c = b; b = a; a = temp;
	}
	/* Round 4 */
	ps += 4;
	for (; i < 64; i++) {
		II(a, b, c, d, x[*pp], ps[i & 0x3], *pc); pp++; pc++;
		temp = d; d = c; c = b; b = a; a = temp;
	}
#elif MD5_SMALL > 0
	pc = C;
	pp = P;
	/* Round 1 */
	for (i = 0; i < 4; i++) {
		FF(a, b, c, d, x[*pp],  7, *pc); pp++; pc++;
		FF(d, a, b, c, x[*pp], 12, *pc); pp++; pc++;
		FF(c, d, a, b, x[*pp], 17, *pc); pp++; pc++;
		FF(b, c, d, a, x[*pp], 22, *pc); pp++; pc++;
	}
	/* Round 2 */
	for (i = 0; i < 4; i++) {
		GG(a, b, c, d, x[*pp],  5, *pc); pp++; pc++;
		GG(d, a, b, c, x[*pp],  9, *pc); pp++; pc++;
		GG(c, d, a, b, x[*pp], 14, *pc); pp++; pc++;
		GG(b, c, d, a, x[*pp], 20, *pc); pp++; pc++;
	}
	/* Round 3 */
	for (i = 0; i < 4; i++) {
		HH(a, b, c, d, x[*pp],  4, *pc); pp++; pc++;
		HH(d, a, b, c, x[*pp], 11, *pc); pp++; pc++;
		HH(c, d, a, b, x[*pp], 16, *pc); pp++; pc++;
		HH(b, c, d, a, x[*pp], 23, *pc); pp++; pc++;
	}
	/* Round 4 */
	for (i = 0; i < 4; i++) {
		II(a, b, c, d, x[*pp],  6, *pc); pp++; pc++;
		II(d, a, b, c, x[*pp], 10, *pc); pp++; pc++;
		II(c, d, a, b, x[*pp], 15, *pc); pp++; pc++;
		II(b, c, d, a, x[*pp], 21, *pc); pp++; pc++;
	}
#else
	/* Round 1 */
#define S11 7
#define S12 12
#define S13 17
#define S14 22
	FF(a, b, c, d, x[ 0], S11, 0xd76aa478); /* 1 */
	FF(d, a, b, c, x[ 1], S12, 0xe8c7b756); /* 2 */
	FF(c, d, a, b, x[ 2], S13, 0x242070db); /* 3 */
	FF(b, c, d, a, x[ 3], S14, 0xc1bdceee); /* 4 */
	FF(a, b, c, d, x[ 4], S11, 0xf57c0faf); /* 5 */
	FF(d, a, b, c, x[ 5], S12, 0x4787c62a); /* 6 */
	FF(c, d, a, b, x[ 6], S13, 0xa8304613); /* 7 */
	FF(b, c, d, a, x[ 7], S14, 0xfd469501); /* 8 */
	FF(a, b, c, d, x[ 8], S11, 0x698098d8); /* 9 */
	FF(d, a, b, c, x[ 9], S12, 0x8b44f7af); /* 10 */
	FF(c, d, a, b, x[10], S13, 0xffff5bb1); /* 11 */
	FF(b, c, d, a, x[11], S14, 0x895cd7be); /* 12 */
	FF(a, b, c, d, x[12], S11, 0x6b901122); /* 13 */
	FF(d, a, b, c, x[13], S12, 0xfd987193); /* 14 */
	FF(c, d, a, b, x[14], S13, 0xa679438e); /* 15 */
	FF(b, c, d, a, x[15], S14, 0x49b40821); /* 16 */
	/* Round 2 */
#define S21 5
#define S22 9
#define S23 14
#define S24 20
	GG(a, b, c, d, x[ 1], S21, 0xf61e2562); /* 17 */
	GG(d, a, b, c, x[ 6], S22, 0xc040b340); /* 18 */
	GG(c, d, a, b, x[11], S23, 0x265e5a51); /* 19 */
	GG(b, c, d, a, x[ 0], S24, 0xe9b6c7aa); /* 20 */
	GG(a, b, c, d, x[ 5], S21, 0xd62f105d); /* 21 */
	GG(d, a, b, c, x[10], S22,  0x2441453); /* 22 */
	GG(c, d, a, b, x[15], S23, 0xd8a1e681); /* 23 */
	GG(b, c, d, a, x[ 4], S24, 0xe7d3fbc8); /* 24 */
	GG(a, b, c, d, x[ 9], S21, 0x21e1cde6); /* 25 */
	GG(d, a, b, c, x[14], S22, 0xc33707d6); /* 26 */
	GG(c, d, a, b, x[ 3], S23, 0xf4d50d87); /* 27 */
	GG(b, c, d, a, x[ 8], S24, 0x455a14ed); /* 28 */
	GG(a, b, c, d, x[13], S21, 0xa9e3e905); /* 29 */
	GG(d, a, b, c, x[ 2], S22, 0xfcefa3f8); /* 30 */
	GG(c, d, a, b, x[ 7], S23, 0x676f02d9); /* 31 */
	GG(b, c, d, a, x[12], S24, 0x8d2a4c8a); /* 32 */
	/* Round 3 */
#define S31 4
#define S32 11
#define S33 16
#define S34 23
	HH(a, b, c, d, x[ 5], S31, 0xfffa3942); /* 33 */
	HH(d, a, b, c, x[ 8], S32, 0x8771f681); /* 34 */
	HH(c, d, a, b, x[11], S33, 0x6d9d6122); /* 35 */
	HH(b, c, d, a, x[14], S34, 0xfde5380c); /* 36 */
	HH(a, b, c, d, x[ 1], S31, 0xa4beea44); /* 37 */
	HH(d, a, b, c, x[ 4], S32, 0x4bdecfa9); /* 38 */
	HH(c, d, a, b, x[ 7], S33, 0xf6bb4b60); /* 39 */
	HH(b, c, d, a, x[10], S34, 0xbebfbc70); /* 40 */
	HH(a, b, c, d, x[13], S31, 0x289b7ec6); /* 41 */
	HH(d, a, b, c, x[ 0], S32, 0xeaa127fa); /* 42 */
	HH(c, d, a, b, x[ 3], S33, 0xd4ef3085); /* 43 */
	HH(b, c, d, a, x[ 6], S34,  0x4881d05); /* 44 */
	HH(a, b, c, d, x[ 9], S31, 0xd9d4d039); /* 45 */
	HH(d, a, b, c, x[12], S32, 0xe6db99e5); /* 46 */
	HH(c, d, a, b, x[15], S33, 0x1fa27cf8); /* 47 */
	HH(b, c, d, a, x[ 2], S34, 0xc4ac5665); /* 48 */
	/* Round 4 */
#define S41 6
#define S42 10
#define S43 15
#define S44 21
	II(a, b, c, d, x[ 0], S41, 0xf4292244); /* 49 */
	II(d, a, b, c, x[ 7], S42, 0x432aff97); /* 50 */
	II(c, d, a, b, x[14], S43, 0xab9423a7); /* 51 */
	II(b, c, d, a, x[ 5], S44, 0xfc93a039); /* 52 */
	II(a, b, c, d, x[12], S41, 0x655b59c3); /* 53 */
	II(d, a, b, c, x[ 3], S42, 0x8f0ccc92); /* 54 */
	II(c, d, a, b, x[10], S43, 0xffeff47d); /* 55 */
	II(b, c, d, a, x[ 1], S44, 0x85845dd1); /* 56 */
	II(a, b, c, d, x[ 8], S41, 0x6fa87e4f); /* 57 */
	II(d, a, b, c, x[15], S42, 0xfe2ce6e0); /* 58 */
	II(c, d, a, b, x[ 6], S43, 0xa3014314); /* 59 */
	II(b, c, d, a, x[13], S44, 0x4e0811a1); /* 60 */
	II(a, b, c, d, x[ 4], S41, 0xf7537e82); /* 61 */
	II(d, a, b, c, x[11], S42, 0xbd3af235); /* 62 */
	II(c, d, a, b, x[ 2], S43, 0x2ad7d2bb); /* 63 */
	II(b, c, d, a, x[ 9], S44, 0xeb86d391); /* 64 */
#endif

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;

	/* Zeroize sensitive information. */
	memset(x, 0, sizeof(x));
}


/* MD5 initialization. */
void FAST_FUNC md5_begin(md5_ctx_t *context)
{
	context->count[0] = context->count[1] = 0;
	/* Load magic initialization constants.  */
	context->state[0] = 0x67452301;
	context->state[1] = 0xefcdab89;
	context->state[2] = 0x98badcfe;
	context->state[3] = 0x10325476;
}

/*
 * MD5 block update operation. Continues an MD5 message-digest
 * operation, processing another message block, and updating
 * the context.
 */
void FAST_FUNC md5_hash(const void *buffer, size_t inputLen, md5_ctx_t *context)
{
	unsigned i, idx, partLen;
	const unsigned char *input = buffer;

	/* Compute number of bytes mod 64 */
	idx = (context->count[0] >> 3) & 0x3F;

	/* Update number of bits */
	context->count[0] += (inputLen << 3);
	if (context->count[0] < (inputLen << 3))
		context->count[1]++;
	context->count[1] += (inputLen >> 29);

	/* Transform as many times as possible. */
	i = 0;
	partLen = 64 - idx;
	if (inputLen >= partLen) {
		memcpy(&context->buffer[idx], input, partLen);
		md5_transform(context->state, context->buffer);
		for (i = partLen; i + 63 < inputLen; i += 64)
			md5_transform(context->state, &input[i]);
		idx = 0;
	}

	/* Buffer remaining input */
	memcpy(&context->buffer[idx], &input[i], inputLen - i);
}

/*
 * MD5 finalization. Ends an MD5 message-digest operation,
 * writing the message digest.
 */
unsigned FAST_FUNC md5_end(void *digest, md5_ctx_t *context)
{
	unsigned idx, padLen;
	unsigned char bits[8];
	unsigned char padding[64];

	/* Add padding followed by original length. */
	memset(padding, 0, sizeof(padding));
	padding[0] = 0x80;
	/* save number of bits */
	memcpy32_cpu2le(bits, context->count, 8);
	/* pad out to 56 mod 64 */
	idx = (context->count[0] >> 3) & 0x3f;
	padLen = (idx < 56) ? (56 - idx) : (120 - idx);
	md5_hash(padding, padLen, context);
	/* append length (before padding) */
	md5_hash(bits, 8, context);

	/* Store state in digest */
	memcpy32_cpu2le(digest, context->state, 16);
	return 16;
}
