/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 2010 Denys Vlasenko
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

#define NEED_SHA512 (ENABLE_SHA512SUM || ENABLE_USE_BB_CRYPT_SHA)

/* gcc 4.2.1 optimizes rotr64 better with inline than with macro
 * (for rotX32, there is no difference). Why? My guess is that
 * macro requires clever common subexpression elimination heuristics
 * in gcc, while inline basically forces it to happen.
 */
//#define rotl32(x,n) (((x) << (n)) | ((x) >> (32 - (n))))
static ALWAYS_INLINE uint32_t rotl32(uint32_t x, unsigned n)
{
	return (x << n) | (x >> (32 - n));
}
//#define rotr32(x,n) (((x) >> (n)) | ((x) << (32 - (n))))
static ALWAYS_INLINE uint32_t rotr32(uint32_t x, unsigned n)
{
	return (x >> n) | (x << (32 - n));
}
/* rotr64 in needed for sha512 only: */
//#define rotr64(x,n) (((x) >> (n)) | ((x) << (64 - (n))))
static ALWAYS_INLINE uint64_t rotr64(uint64_t x, unsigned n)
{
	return (x >> n) | (x << (64 - n));
}

/* rotl64 only used for sha3 currently */
static ALWAYS_INLINE uint64_t rotl64(uint64_t x, unsigned n)
{
	return (x << n) | (x >> (64 - n));
}

/* Feed data through a temporary buffer.
 * The internal buffer remembers previous data until it has 64
 * bytes worth to pass on.
 */
static void FAST_FUNC common64_hash(md5_ctx_t *ctx, const void *buffer, size_t len)
{
	unsigned bufpos = ctx->total64 & 63;

	ctx->total64 += len;

	while (1) {
		unsigned remaining = 64 - bufpos;
		if (remaining > len)
			remaining = len;
		/* Copy data into aligned buffer */
		memcpy(ctx->wbuffer + bufpos, buffer, remaining);
		len -= remaining;
		buffer = (const char *)buffer + remaining;
		bufpos += remaining;
		/* Clever way to do "if (bufpos != N) break; ... ; bufpos = 0;" */
		bufpos -= 64;
		if (bufpos != 0)
			break;
		/* Buffer is filled up, process it */
		ctx->process_block(ctx);
		/*bufpos = 0; - already is */
	}
}

/* Process the remaining bytes in the buffer */
static void FAST_FUNC common64_end(md5_ctx_t *ctx, int swap_needed)
{
	unsigned bufpos = ctx->total64 & 63;
	/* Pad the buffer to the next 64-byte boundary with 0x80,0,0,0... */
	ctx->wbuffer[bufpos++] = 0x80;

	/* This loop iterates either once or twice, no more, no less */
	while (1) {
		unsigned remaining = 64 - bufpos;
		memset(ctx->wbuffer + bufpos, 0, remaining);
		/* Do we have enough space for the length count? */
		if (remaining >= 8) {
			/* Store the 64-bit counter of bits in the buffer */
			uint64_t t = ctx->total64 << 3;
			if (swap_needed)
				t = bb_bswap_64(t);
			/* wbuffer is suitably aligned for this */
			*(bb__aliased_uint64_t *) (&ctx->wbuffer[64 - 8]) = t;
		}
		ctx->process_block(ctx);
		if (remaining >= 8)
			break;
		bufpos = 0;
	}
}


/*
 * Compute MD5 checksum of strings according to the
 * definition of MD5 in RFC 1321 from April 1992.
 *
 * Written by Ulrich Drepper <drepper@gnu.ai.mit.edu>, 1995.
 *
 * Copyright (C) 1995-1999 Free Software Foundation, Inc.
 * Copyright (C) 2001 Manuel Novoa III
 * Copyright (C) 2003 Glenn L. McGrath
 * Copyright (C) 2003 Erik Andersen
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

/* 0: fastest, 3: smallest */
#if CONFIG_MD5_SMALL < 0
# define MD5_SMALL 0
#elif CONFIG_MD5_SMALL > 3
# define MD5_SMALL 3
#else
# define MD5_SMALL CONFIG_MD5_SMALL
#endif

/* These are the four functions used in the four steps of the MD5 algorithm
 * and defined in the RFC 1321.  The first function is a little bit optimized
 * (as found in Colin Plumbs public domain implementation).
 * #define FF(b, c, d) ((b & c) | (~b & d))
 */
#undef FF
#undef FG
#undef FH
#undef FI
#define FF(b, c, d) (d ^ (b & (c ^ d)))
#define FG(b, c, d) FF(d, b, c)
#define FH(b, c, d) (b ^ c ^ d)
#define FI(b, c, d) (c ^ (b | ~d))

/* Hash a single block, 64 bytes long and 4-byte aligned */
static void FAST_FUNC md5_process_block64(md5_ctx_t *ctx)
{
#if MD5_SMALL > 0
	/* Before we start, one word to the strange constants.
	   They are defined in RFC 1321 as
	   T[i] = (int)(2^32 * fabs(sin(i))), i=1..64
	 */
	static const uint32_t C_array[] = {
		/* round 1 */
		0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
		0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
		0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
		0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
		/* round 2 */
		0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
		0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
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
	static const char P_array[] ALIGN1 = {
# if MD5_SMALL > 1
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, /* 1 */
# endif
		1, 6, 11, 0, 5, 10, 15, 4, 9, 14, 3, 8, 13, 2, 7, 12, /* 2 */
		5, 8, 11, 14, 1, 4, 7, 10, 13, 0, 3, 6, 9, 12, 15, 2, /* 3 */
		0, 7, 14, 5, 12, 3, 10, 1, 8, 15, 6, 13, 4, 11, 2, 9  /* 4 */
	};
#endif
	uint32_t *words = (void*) ctx->wbuffer;
	uint32_t A = ctx->hash[0];
	uint32_t B = ctx->hash[1];
	uint32_t C = ctx->hash[2];
	uint32_t D = ctx->hash[3];

#if MD5_SMALL >= 2  /* 2 or 3 */

	static const char S_array[] ALIGN1 = {
		7, 12, 17, 22,
		5, 9, 14, 20,
		4, 11, 16, 23,
		6, 10, 15, 21
	};
	const uint32_t *pc;
	const char *pp;
	const char *ps;
	int i;
	uint32_t temp;

	if (BB_BIG_ENDIAN)
		for (i = 0; i < 16; i++)
			words[i] = SWAP_LE32(words[i]);

# if MD5_SMALL == 3
	pc = C_array;
	pp = P_array;
	ps = S_array - 4;

	for (i = 0; i < 64; i++) {
		if ((i & 0x0f) == 0)
			ps += 4;
		temp = A;
		switch (i >> 4) {
		case 0:
			temp += FF(B, C, D);
			break;
		case 1:
			temp += FG(B, C, D);
			break;
		case 2:
			temp += FH(B, C, D);
			break;
		default: /* case 3 */
			temp += FI(B, C, D);
		}
		temp += words[(int) (*pp++)] + *pc++;
		temp = rotl32(temp, ps[i & 3]);
		temp += B;
		A = D;
		D = C;
		C = B;
		B = temp;
	}
# else  /* MD5_SMALL == 2 */
	pc = C_array;
	pp = P_array;
	ps = S_array;

	for (i = 0; i < 16; i++) {
		temp = A + FF(B, C, D) + words[(int) (*pp++)] + *pc++;
		temp = rotl32(temp, ps[i & 3]);
		temp += B;
		A = D;
		D = C;
		C = B;
		B = temp;
	}
	ps += 4;
	for (i = 0; i < 16; i++) {
		temp = A + FG(B, C, D) + words[(int) (*pp++)] + *pc++;
		temp = rotl32(temp, ps[i & 3]);
		temp += B;
		A = D;
		D = C;
		C = B;
		B = temp;
	}
	ps += 4;
	for (i = 0; i < 16; i++) {
		temp = A + FH(B, C, D) + words[(int) (*pp++)] + *pc++;
		temp = rotl32(temp, ps[i & 3]);
		temp += B;
		A = D;
		D = C;
		C = B;
		B = temp;
	}
	ps += 4;
	for (i = 0; i < 16; i++) {
		temp = A + FI(B, C, D) + words[(int) (*pp++)] + *pc++;
		temp = rotl32(temp, ps[i & 3]);
		temp += B;
		A = D;
		D = C;
		C = B;
		B = temp;
	}
# endif
	/* Add checksum to the starting values */
	ctx->hash[0] += A;
	ctx->hash[1] += B;
	ctx->hash[2] += C;
	ctx->hash[3] += D;

#else  /* MD5_SMALL == 0 or 1 */

# if MD5_SMALL == 1
	const uint32_t *pc;
	const char *pp;
	int i;
# endif

	/* First round: using the given function, the context and a constant
	   the next context is computed.  Because the algorithm's processing
	   unit is a 32-bit word and it is determined to work on words in
	   little endian byte order we perhaps have to change the byte order
	   before the computation.  To reduce the work for the next steps
	   we save swapped words in WORDS array.  */
# undef OP
# define OP(a, b, c, d, s, T) \
	do { \
		a += FF(b, c, d) + (*words IF_BIG_ENDIAN(= SWAP_LE32(*words))) + T; \
		words++; \
		a = rotl32(a, s); \
		a += b; \
	} while (0)

	/* Round 1 */
# if MD5_SMALL == 1
	pc = C_array;
	for (i = 0; i < 4; i++) {
		OP(A, B, C, D, 7, *pc++);
		OP(D, A, B, C, 12, *pc++);
		OP(C, D, A, B, 17, *pc++);
		OP(B, C, D, A, 22, *pc++);
	}
# else
	OP(A, B, C, D, 7, 0xd76aa478);
	OP(D, A, B, C, 12, 0xe8c7b756);
	OP(C, D, A, B, 17, 0x242070db);
	OP(B, C, D, A, 22, 0xc1bdceee);
	OP(A, B, C, D, 7, 0xf57c0faf);
	OP(D, A, B, C, 12, 0x4787c62a);
	OP(C, D, A, B, 17, 0xa8304613);
	OP(B, C, D, A, 22, 0xfd469501);
	OP(A, B, C, D, 7, 0x698098d8);
	OP(D, A, B, C, 12, 0x8b44f7af);
	OP(C, D, A, B, 17, 0xffff5bb1);
	OP(B, C, D, A, 22, 0x895cd7be);
	OP(A, B, C, D, 7, 0x6b901122);
	OP(D, A, B, C, 12, 0xfd987193);
	OP(C, D, A, B, 17, 0xa679438e);
	OP(B, C, D, A, 22, 0x49b40821);
# endif
	words -= 16;

	/* For the second to fourth round we have the possibly swapped words
	   in WORDS.  Redefine the macro to take an additional first
	   argument specifying the function to use.  */
# undef OP
# define OP(f, a, b, c, d, k, s, T) \
	do { \
		a += f(b, c, d) + words[k] + T; \
		a = rotl32(a, s); \
		a += b; \
	} while (0)

	/* Round 2 */
# if MD5_SMALL == 1
	pp = P_array;
	for (i = 0; i < 4; i++) {
		OP(FG, A, B, C, D, (int) (*pp++), 5, *pc++);
		OP(FG, D, A, B, C, (int) (*pp++), 9, *pc++);
		OP(FG, C, D, A, B, (int) (*pp++), 14, *pc++);
		OP(FG, B, C, D, A, (int) (*pp++), 20, *pc++);
	}
# else
	OP(FG, A, B, C, D, 1, 5, 0xf61e2562);
	OP(FG, D, A, B, C, 6, 9, 0xc040b340);
	OP(FG, C, D, A, B, 11, 14, 0x265e5a51);
	OP(FG, B, C, D, A, 0, 20, 0xe9b6c7aa);
	OP(FG, A, B, C, D, 5, 5, 0xd62f105d);
	OP(FG, D, A, B, C, 10, 9, 0x02441453);
	OP(FG, C, D, A, B, 15, 14, 0xd8a1e681);
	OP(FG, B, C, D, A, 4, 20, 0xe7d3fbc8);
	OP(FG, A, B, C, D, 9, 5, 0x21e1cde6);
	OP(FG, D, A, B, C, 14, 9, 0xc33707d6);
	OP(FG, C, D, A, B, 3, 14, 0xf4d50d87);
	OP(FG, B, C, D, A, 8, 20, 0x455a14ed);
	OP(FG, A, B, C, D, 13, 5, 0xa9e3e905);
	OP(FG, D, A, B, C, 2, 9, 0xfcefa3f8);
	OP(FG, C, D, A, B, 7, 14, 0x676f02d9);
	OP(FG, B, C, D, A, 12, 20, 0x8d2a4c8a);
# endif

	/* Round 3 */
# if MD5_SMALL == 1
	for (i = 0; i < 4; i++) {
		OP(FH, A, B, C, D, (int) (*pp++), 4, *pc++);
		OP(FH, D, A, B, C, (int) (*pp++), 11, *pc++);
		OP(FH, C, D, A, B, (int) (*pp++), 16, *pc++);
		OP(FH, B, C, D, A, (int) (*pp++), 23, *pc++);
	}
# else
	OP(FH, A, B, C, D, 5, 4, 0xfffa3942);
	OP(FH, D, A, B, C, 8, 11, 0x8771f681);
	OP(FH, C, D, A, B, 11, 16, 0x6d9d6122);
	OP(FH, B, C, D, A, 14, 23, 0xfde5380c);
	OP(FH, A, B, C, D, 1, 4, 0xa4beea44);
	OP(FH, D, A, B, C, 4, 11, 0x4bdecfa9);
	OP(FH, C, D, A, B, 7, 16, 0xf6bb4b60);
	OP(FH, B, C, D, A, 10, 23, 0xbebfbc70);
	OP(FH, A, B, C, D, 13, 4, 0x289b7ec6);
	OP(FH, D, A, B, C, 0, 11, 0xeaa127fa);
	OP(FH, C, D, A, B, 3, 16, 0xd4ef3085);
	OP(FH, B, C, D, A, 6, 23, 0x04881d05);
	OP(FH, A, B, C, D, 9, 4, 0xd9d4d039);
	OP(FH, D, A, B, C, 12, 11, 0xe6db99e5);
	OP(FH, C, D, A, B, 15, 16, 0x1fa27cf8);
	OP(FH, B, C, D, A, 2, 23, 0xc4ac5665);
# endif

	/* Round 4 */
# if MD5_SMALL == 1
	for (i = 0; i < 4; i++) {
		OP(FI, A, B, C, D, (int) (*pp++), 6, *pc++);
		OP(FI, D, A, B, C, (int) (*pp++), 10, *pc++);
		OP(FI, C, D, A, B, (int) (*pp++), 15, *pc++);
		OP(FI, B, C, D, A, (int) (*pp++), 21, *pc++);
	}
# else
	OP(FI, A, B, C, D, 0, 6, 0xf4292244);
	OP(FI, D, A, B, C, 7, 10, 0x432aff97);
	OP(FI, C, D, A, B, 14, 15, 0xab9423a7);
	OP(FI, B, C, D, A, 5, 21, 0xfc93a039);
	OP(FI, A, B, C, D, 12, 6, 0x655b59c3);
	OP(FI, D, A, B, C, 3, 10, 0x8f0ccc92);
	OP(FI, C, D, A, B, 10, 15, 0xffeff47d);
	OP(FI, B, C, D, A, 1, 21, 0x85845dd1);
	OP(FI, A, B, C, D, 8, 6, 0x6fa87e4f);
	OP(FI, D, A, B, C, 15, 10, 0xfe2ce6e0);
	OP(FI, C, D, A, B, 6, 15, 0xa3014314);
	OP(FI, B, C, D, A, 13, 21, 0x4e0811a1);
	OP(FI, A, B, C, D, 4, 6, 0xf7537e82);
	OP(FI, D, A, B, C, 11, 10, 0xbd3af235);
	OP(FI, C, D, A, B, 2, 15, 0x2ad7d2bb);
	OP(FI, B, C, D, A, 9, 21, 0xeb86d391);
# undef OP
# endif
	/* Add checksum to the starting values */
	ctx->hash[0] += A;
	ctx->hash[1] += B;
	ctx->hash[2] += C;
	ctx->hash[3] += D;
#endif
}
#undef FF
#undef FG
#undef FH
#undef FI

/* Initialize structure containing state of computation.
 * (RFC 1321, 3.3: Step 3)
 */
void FAST_FUNC md5_begin(md5_ctx_t *ctx)
{
	ctx->hash[0] = 0x67452301;
	ctx->hash[1] = 0xefcdab89;
	ctx->hash[2] = 0x98badcfe;
	ctx->hash[3] = 0x10325476;
	ctx->total64 = 0;
	ctx->process_block = md5_process_block64;
}

/* Used also for sha1 and sha256 */
void FAST_FUNC md5_hash(md5_ctx_t *ctx, const void *buffer, size_t len)
{
	common64_hash(ctx, buffer, len);
}

/* Process the remaining bytes in the buffer and put result from CTX
 * in first 16 bytes following RESBUF.  The result is always in little
 * endian byte order, so that a byte-wise output yields to the wanted
 * ASCII representation of the message digest.
 */
unsigned FAST_FUNC md5_end(md5_ctx_t *ctx, void *resbuf)
{
	/* MD5 stores total in LE, need to swap on BE arches: */
	common64_end(ctx, /*swap_needed:*/ BB_BIG_ENDIAN);

	/* The MD5 result is in little endian byte order */
	if (BB_BIG_ENDIAN) {
		ctx->hash[0] = SWAP_LE32(ctx->hash[0]);
		ctx->hash[1] = SWAP_LE32(ctx->hash[1]);
		ctx->hash[2] = SWAP_LE32(ctx->hash[2]);
		ctx->hash[3] = SWAP_LE32(ctx->hash[3]);
	}

	memcpy(resbuf, ctx->hash, sizeof(ctx->hash[0]) * 4);
	return sizeof(ctx->hash[0]) * 4;
}


/*
 * SHA1 part is:
 * Copyright 2007 Rob Landley <rob@landley.net>
 *
 * Based on the public domain SHA-1 in C by Steve Reid <steve@edmweb.com>
 * from http://www.mirrors.wiretapped.net/security/cryptography/hashes/sha1/
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 *
 * ---------------------------------------------------------------------------
 *
 * SHA256 and SHA512 parts are:
 * Released into the Public Domain by Ulrich Drepper <drepper@redhat.com>.
 * Shrank by Denys Vlasenko.
 *
 * ---------------------------------------------------------------------------
 *
 * The best way to test random blocksizes is to go to coreutils/md5_sha1_sum.c
 * and replace "4096" with something like "2000 + time(NULL) % 2097",
 * then rebuild and compare "shaNNNsum bigfile" results.
 */

static void FAST_FUNC sha1_process_block64(sha1_ctx_t *ctx)
{
	static const uint32_t rconsts[] = {
		0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6
	};
	int i, j;
	int cnt;
	uint32_t W[16+16];
	uint32_t a, b, c, d, e;

	/* On-stack work buffer frees up one register in the main loop
	 * which otherwise will be needed to hold ctx pointer */
	for (i = 0; i < 16; i++)
		W[i] = W[i+16] = SWAP_BE32(((uint32_t*)ctx->wbuffer)[i]);

	a = ctx->hash[0];
	b = ctx->hash[1];
	c = ctx->hash[2];
	d = ctx->hash[3];
	e = ctx->hash[4];

	/* 4 rounds of 20 operations each */
	cnt = 0;
	for (i = 0; i < 4; i++) {
		j = 19;
		do {
			uint32_t work;

			work = c ^ d;
			if (i == 0) {
				work = (work & b) ^ d;
				if (j <= 3)
					goto ge16;
				/* Used to do SWAP_BE32 here, but this
				 * requires ctx (see comment above) */
				work += W[cnt];
			} else {
				if (i == 2)
					work = ((b | c) & d) | (b & c);
				else /* i = 1 or 3 */
					work ^= b;
 ge16:
				W[cnt] = W[cnt+16] = rotl32(W[cnt+13] ^ W[cnt+8] ^ W[cnt+2] ^ W[cnt], 1);
				work += W[cnt];
			}
			work += e + rotl32(a, 5) + rconsts[i];

			/* Rotate by one for next time */
			e = d;
			d = c;
			c = /* b = */ rotl32(b, 30);
			b = a;
			a = work;
			cnt = (cnt + 1) & 15;
		} while (--j >= 0);
	}

	ctx->hash[0] += a;
	ctx->hash[1] += b;
	ctx->hash[2] += c;
	ctx->hash[3] += d;
	ctx->hash[4] += e;
}

/* Constants for SHA512 from FIPS 180-2:4.2.3.
 * SHA256 constants from FIPS 180-2:4.2.2
 * are the most significant half of first 64 elements
 * of the same array.
 */
#undef K
#if NEED_SHA512
typedef uint64_t sha_K_int;
# define K(v) v
#else
typedef uint32_t sha_K_int;
# define K(v) (uint32_t)(v >> 32)
#endif
static const sha_K_int sha_K[] = {
	K(0x428a2f98d728ae22ULL), K(0x7137449123ef65cdULL),
	K(0xb5c0fbcfec4d3b2fULL), K(0xe9b5dba58189dbbcULL),
	K(0x3956c25bf348b538ULL), K(0x59f111f1b605d019ULL),
	K(0x923f82a4af194f9bULL), K(0xab1c5ed5da6d8118ULL),
	K(0xd807aa98a3030242ULL), K(0x12835b0145706fbeULL),
	K(0x243185be4ee4b28cULL), K(0x550c7dc3d5ffb4e2ULL),
	K(0x72be5d74f27b896fULL), K(0x80deb1fe3b1696b1ULL),
	K(0x9bdc06a725c71235ULL), K(0xc19bf174cf692694ULL),
	K(0xe49b69c19ef14ad2ULL), K(0xefbe4786384f25e3ULL),
	K(0x0fc19dc68b8cd5b5ULL), K(0x240ca1cc77ac9c65ULL),
	K(0x2de92c6f592b0275ULL), K(0x4a7484aa6ea6e483ULL),
	K(0x5cb0a9dcbd41fbd4ULL), K(0x76f988da831153b5ULL),
	K(0x983e5152ee66dfabULL), K(0xa831c66d2db43210ULL),
	K(0xb00327c898fb213fULL), K(0xbf597fc7beef0ee4ULL),
	K(0xc6e00bf33da88fc2ULL), K(0xd5a79147930aa725ULL),
	K(0x06ca6351e003826fULL), K(0x142929670a0e6e70ULL),
	K(0x27b70a8546d22ffcULL), K(0x2e1b21385c26c926ULL),
	K(0x4d2c6dfc5ac42aedULL), K(0x53380d139d95b3dfULL),
	K(0x650a73548baf63deULL), K(0x766a0abb3c77b2a8ULL),
	K(0x81c2c92e47edaee6ULL), K(0x92722c851482353bULL),
	K(0xa2bfe8a14cf10364ULL), K(0xa81a664bbc423001ULL),
	K(0xc24b8b70d0f89791ULL), K(0xc76c51a30654be30ULL),
	K(0xd192e819d6ef5218ULL), K(0xd69906245565a910ULL),
	K(0xf40e35855771202aULL), K(0x106aa07032bbd1b8ULL),
	K(0x19a4c116b8d2d0c8ULL), K(0x1e376c085141ab53ULL),
	K(0x2748774cdf8eeb99ULL), K(0x34b0bcb5e19b48a8ULL),
	K(0x391c0cb3c5c95a63ULL), K(0x4ed8aa4ae3418acbULL),
	K(0x5b9cca4f7763e373ULL), K(0x682e6ff3d6b2b8a3ULL),
	K(0x748f82ee5defb2fcULL), K(0x78a5636f43172f60ULL),
	K(0x84c87814a1f0ab72ULL), K(0x8cc702081a6439ecULL),
	K(0x90befffa23631e28ULL), K(0xa4506cebde82bde9ULL),
	K(0xbef9a3f7b2c67915ULL), K(0xc67178f2e372532bULL),
#if NEED_SHA512  /* [64]+ are used for sha512 only */
	K(0xca273eceea26619cULL), K(0xd186b8c721c0c207ULL),
	K(0xeada7dd6cde0eb1eULL), K(0xf57d4f7fee6ed178ULL),
	K(0x06f067aa72176fbaULL), K(0x0a637dc5a2c898a6ULL),
	K(0x113f9804bef90daeULL), K(0x1b710b35131c471bULL),
	K(0x28db77f523047d84ULL), K(0x32caab7b40c72493ULL),
	K(0x3c9ebe0a15c9bebcULL), K(0x431d67c49c100d4cULL),
	K(0x4cc5d4becb3e42b6ULL), K(0x597f299cfc657e2aULL),
	K(0x5fcb6fab3ad6faecULL), K(0x6c44198c4a475817ULL),
#endif
};
#undef K

#undef Ch
#undef Maj
#undef S0
#undef S1
#undef R0
#undef R1

static void FAST_FUNC sha256_process_block64(sha256_ctx_t *ctx)
{
	unsigned t;
	uint32_t W[64], a, b, c, d, e, f, g, h;
	const uint32_t *words = (uint32_t*) ctx->wbuffer;

	/* Operators defined in FIPS 180-2:4.1.2.  */
#define Ch(x, y, z) ((x & y) ^ (~x & z))
#define Maj(x, y, z) ((x & y) ^ (x & z) ^ (y & z))
#define S0(x) (rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22))
#define S1(x) (rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25))
#define R0(x) (rotr32(x, 7) ^ rotr32(x, 18) ^ (x >> 3))
#define R1(x) (rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10))

	/* Compute the message schedule according to FIPS 180-2:6.2.2 step 2.  */
	for (t = 0; t < 16; ++t)
		W[t] = SWAP_BE32(words[t]);
	for (/*t = 16*/; t < 64; ++t)
		W[t] = R1(W[t - 2]) + W[t - 7] + R0(W[t - 15]) + W[t - 16];

	a = ctx->hash[0];
	b = ctx->hash[1];
	c = ctx->hash[2];
	d = ctx->hash[3];
	e = ctx->hash[4];
	f = ctx->hash[5];
	g = ctx->hash[6];
	h = ctx->hash[7];

	/* The actual computation according to FIPS 180-2:6.2.2 step 3.  */
	for (t = 0; t < 64; ++t) {
		/* Need to fetch upper half of sha_K[t]
		 * (I hope compiler is clever enough to just fetch
		 * upper half)
		 */
		uint32_t K_t = NEED_SHA512 ? (sha_K[t] >> 32) : sha_K[t];
		uint32_t T1 = h + S1(e) + Ch(e, f, g) + K_t + W[t];
		uint32_t T2 = S0(a) + Maj(a, b, c);
		h = g;
		g = f;
		f = e;
		e = d + T1;
		d = c;
		c = b;
		b = a;
		a = T1 + T2;
	}
#undef Ch
#undef Maj
#undef S0
#undef S1
#undef R0
#undef R1
	/* Add the starting values of the context according to FIPS 180-2:6.2.2
	   step 4.  */
	ctx->hash[0] += a;
	ctx->hash[1] += b;
	ctx->hash[2] += c;
	ctx->hash[3] += d;
	ctx->hash[4] += e;
	ctx->hash[5] += f;
	ctx->hash[6] += g;
	ctx->hash[7] += h;
}

#if NEED_SHA512
static void FAST_FUNC sha512_process_block128(sha512_ctx_t *ctx)
{
	unsigned t;
	uint64_t W[80];
	/* On i386, having assignments here (not later as sha256 does)
	 * produces 99 bytes smaller code with gcc 4.3.1
	 */
	uint64_t a = ctx->hash[0];
	uint64_t b = ctx->hash[1];
	uint64_t c = ctx->hash[2];
	uint64_t d = ctx->hash[3];
	uint64_t e = ctx->hash[4];
	uint64_t f = ctx->hash[5];
	uint64_t g = ctx->hash[6];
	uint64_t h = ctx->hash[7];
	const uint64_t *words = (uint64_t*) ctx->wbuffer;

	/* Operators defined in FIPS 180-2:4.1.2.  */
#define Ch(x, y, z) ((x & y) ^ (~x & z))
#define Maj(x, y, z) ((x & y) ^ (x & z) ^ (y & z))
#define S0(x) (rotr64(x, 28) ^ rotr64(x, 34) ^ rotr64(x, 39))
#define S1(x) (rotr64(x, 14) ^ rotr64(x, 18) ^ rotr64(x, 41))
#define R0(x) (rotr64(x, 1) ^ rotr64(x, 8) ^ (x >> 7))
#define R1(x) (rotr64(x, 19) ^ rotr64(x, 61) ^ (x >> 6))

	/* Compute the message schedule according to FIPS 180-2:6.3.2 step 2.  */
	for (t = 0; t < 16; ++t)
		W[t] = SWAP_BE64(words[t]);
	for (/*t = 16*/; t < 80; ++t)
		W[t] = R1(W[t - 2]) + W[t - 7] + R0(W[t - 15]) + W[t - 16];

	/* The actual computation according to FIPS 180-2:6.3.2 step 3.  */
	for (t = 0; t < 80; ++t) {
		uint64_t T1 = h + S1(e) + Ch(e, f, g) + sha_K[t] + W[t];
		uint64_t T2 = S0(a) + Maj(a, b, c);
		h = g;
		g = f;
		f = e;
		e = d + T1;
		d = c;
		c = b;
		b = a;
		a = T1 + T2;
	}
#undef Ch
#undef Maj
#undef S0
#undef S1
#undef R0
#undef R1
	/* Add the starting values of the context according to FIPS 180-2:6.3.2
	   step 4.  */
	ctx->hash[0] += a;
	ctx->hash[1] += b;
	ctx->hash[2] += c;
	ctx->hash[3] += d;
	ctx->hash[4] += e;
	ctx->hash[5] += f;
	ctx->hash[6] += g;
	ctx->hash[7] += h;
}
#endif /* NEED_SHA512 */

void FAST_FUNC sha1_begin(sha1_ctx_t *ctx)
{
	ctx->hash[0] = 0x67452301;
	ctx->hash[1] = 0xefcdab89;
	ctx->hash[2] = 0x98badcfe;
	ctx->hash[3] = 0x10325476;
	ctx->hash[4] = 0xc3d2e1f0;
	ctx->total64 = 0;
	ctx->process_block = sha1_process_block64;
}

static const uint32_t init256[] = {
	0,
	0,
	0x6a09e667,
	0xbb67ae85,
	0x3c6ef372,
	0xa54ff53a,
	0x510e527f,
	0x9b05688c,
	0x1f83d9ab,
	0x5be0cd19,
};
#if NEED_SHA512
static const uint32_t init512_lo[] = {
	0,
	0,
	0xf3bcc908,
	0x84caa73b,
	0xfe94f82b,
	0x5f1d36f1,
	0xade682d1,
	0x2b3e6c1f,
	0xfb41bd6b,
	0x137e2179,
};
#endif /* NEED_SHA512 */

/* Initialize structure containing state of computation.
   (FIPS 180-2:5.3.2)  */
void FAST_FUNC sha256_begin(sha256_ctx_t *ctx)
{
	memcpy(&ctx->total64, init256, sizeof(init256));
	/*ctx->total64 = 0; - done by prepending two 32-bit zeros to init256 */
	ctx->process_block = sha256_process_block64;
}

#if NEED_SHA512
/* Initialize structure containing state of computation.
   (FIPS 180-2:5.3.3)  */
void FAST_FUNC sha512_begin(sha512_ctx_t *ctx)
{
	int i;
	/* Two extra iterations zero out ctx->total64[2] */
	uint64_t *tp = ctx->total64;
	for (i = 0; i < 2+8; i++)
		tp[i] = ((uint64_t)(init256[i]) << 32) + init512_lo[i];
	/*ctx->total64[0] = ctx->total64[1] = 0; - already done */
}

void FAST_FUNC sha512_hash(sha512_ctx_t *ctx, const void *buffer, size_t len)
{
	unsigned bufpos = ctx->total64[0] & 127;
	unsigned remaining;

	/* First increment the byte count.  FIPS 180-2 specifies the possible
	   length of the file up to 2^128 _bits_.
	   We compute the number of _bytes_ and convert to bits later.  */
	ctx->total64[0] += len;
	if (ctx->total64[0] < len)
		ctx->total64[1]++;
# if 0
	remaining = 128 - bufpos;

	/* Hash whole blocks */
	while (len >= remaining) {
		memcpy(ctx->wbuffer + bufpos, buffer, remaining);
		buffer = (const char *)buffer + remaining;
		len -= remaining;
		remaining = 128;
		bufpos = 0;
		sha512_process_block128(ctx);
	}

	/* Save last, partial blosk */
	memcpy(ctx->wbuffer + bufpos, buffer, len);
# else
	while (1) {
		remaining = 128 - bufpos;
		if (remaining > len)
			remaining = len;
		/* Copy data into aligned buffer */
		memcpy(ctx->wbuffer + bufpos, buffer, remaining);
		len -= remaining;
		buffer = (const char *)buffer + remaining;
		bufpos += remaining;
		/* Clever way to do "if (bufpos != N) break; ... ; bufpos = 0;" */
		bufpos -= 128;
		if (bufpos != 0)
			break;
		/* Buffer is filled up, process it */
		sha512_process_block128(ctx);
		/*bufpos = 0; - already is */
	}
# endif
}
#endif /* NEED_SHA512 */

/* Used also for sha256 */
unsigned FAST_FUNC sha1_end(sha1_ctx_t *ctx, void *resbuf)
{
	unsigned hash_size;

	/* SHA stores total in BE, need to swap on LE arches: */
	common64_end(ctx, /*swap_needed:*/ BB_LITTLE_ENDIAN);

	hash_size = (ctx->process_block == sha1_process_block64) ? 5 : 8;
	/* This way we do not impose alignment constraints on resbuf: */
	if (BB_LITTLE_ENDIAN) {
		unsigned i;
		for (i = 0; i < hash_size; ++i)
			ctx->hash[i] = SWAP_BE32(ctx->hash[i]);
	}
	hash_size *= sizeof(ctx->hash[0]);
	memcpy(resbuf, ctx->hash, hash_size);
	return hash_size;
}

#if NEED_SHA512
unsigned FAST_FUNC sha512_end(sha512_ctx_t *ctx, void *resbuf)
{
	unsigned bufpos = ctx->total64[0] & 127;

	/* Pad the buffer to the next 128-byte boundary with 0x80,0,0,0... */
	ctx->wbuffer[bufpos++] = 0x80;

	while (1) {
		unsigned remaining = 128 - bufpos;
		memset(ctx->wbuffer + bufpos, 0, remaining);
		if (remaining >= 16) {
			/* Store the 128-bit counter of bits in the buffer in BE format */
			uint64_t t;
			t = ctx->total64[0] << 3;
			t = SWAP_BE64(t);
			*(bb__aliased_uint64_t *) (&ctx->wbuffer[128 - 8]) = t;
			t = (ctx->total64[1] << 3) | (ctx->total64[0] >> 61);
			t = SWAP_BE64(t);
			*(bb__aliased_uint64_t *) (&ctx->wbuffer[128 - 16]) = t;
		}
		sha512_process_block128(ctx);
		if (remaining >= 16)
			break;
		bufpos = 0;
	}

	if (BB_LITTLE_ENDIAN) {
		unsigned i;
		for (i = 0; i < ARRAY_SIZE(ctx->hash); ++i)
			ctx->hash[i] = SWAP_BE64(ctx->hash[i]);
	}
	memcpy(resbuf, ctx->hash, sizeof(ctx->hash));
	return sizeof(ctx->hash);
}
#endif /* NEED_SHA512 */


/*
 * The Keccak sponge function, designed by Guido Bertoni, Joan Daemen,
 * Michael Peeters and Gilles Van Assche. For more information, feedback or
 * questions, please refer to our website: http://keccak.noekeon.org/
 *
 * Implementation by Ronny Van Keer,
 * hereby denoted as "the implementer".
 *
 * To the extent possible under law, the implementer has waived all copyright
 * and related or neighboring rights to the source code in this file.
 * http://creativecommons.org/publicdomain/zero/1.0/
 *
 * Busybox modifications (C) Lauri Kasanen, under the GPLv2.
 */

#if CONFIG_SHA3_SMALL < 0
# define SHA3_SMALL 0
#elif CONFIG_SHA3_SMALL > 1
# define SHA3_SMALL 1
#else
# define SHA3_SMALL CONFIG_SHA3_SMALL
#endif

#define OPTIMIZE_SHA3_FOR_32 0
/*
 * SHA3 can be optimized for 32-bit CPUs with bit-slicing:
 * every 64-bit word of state[] can be split into two 32-bit words
 * by even/odd bits. In this form, all rotations of sha3 round
 * are 32-bit - and there are lots of them.
 * However, it requires either splitting/combining state words
 * before/after sha3 round (code does this now)
 * or shuffling bits before xor'ing them into state and in sha3_end.
 * Without shuffling, bit-slicing results in -130 bytes of code
 * and marginal speedup (but of course it gives wrong result).
 * With shuffling it works, but +260 code bytes, and slower.
 * Disabled for now:
 */
#if 0 /* LONG_MAX == 0x7fffffff */
# undef OPTIMIZE_SHA3_FOR_32
# define OPTIMIZE_SHA3_FOR_32 1
#endif

#if OPTIMIZE_SHA3_FOR_32
/* This splits every 64-bit word into a pair of 32-bit words,
 * even bits go into first word, odd bits go to second one.
 * The conversion is done in-place.
 */
static void split_halves(uint64_t *state)
{
	/* Credit: Henry S. Warren, Hacker's Delight, Addison-Wesley, 2002 */
	uint32_t *s32 = (uint32_t*)state;
	uint32_t t, x0, x1;
	int i;
	for (i = 24; i >= 0; --i) {
		x0 = s32[0];
		t = (x0 ^ (x0 >> 1)) & 0x22222222; x0 = x0 ^ t ^ (t << 1);
		t = (x0 ^ (x0 >> 2)) & 0x0C0C0C0C; x0 = x0 ^ t ^ (t << 2);
		t = (x0 ^ (x0 >> 4)) & 0x00F000F0; x0 = x0 ^ t ^ (t << 4);
		t = (x0 ^ (x0 >> 8)) & 0x0000FF00; x0 = x0 ^ t ^ (t << 8);
		x1 = s32[1];
		t = (x1 ^ (x1 >> 1)) & 0x22222222; x1 = x1 ^ t ^ (t << 1);
		t = (x1 ^ (x1 >> 2)) & 0x0C0C0C0C; x1 = x1 ^ t ^ (t << 2);
		t = (x1 ^ (x1 >> 4)) & 0x00F000F0; x1 = x1 ^ t ^ (t << 4);
		t = (x1 ^ (x1 >> 8)) & 0x0000FF00; x1 = x1 ^ t ^ (t << 8);
		*s32++ = (x0 & 0x0000FFFF) | (x1 << 16);
		*s32++ = (x0 >> 16) | (x1 & 0xFFFF0000);
	}
}
/* The reverse operation */
static void combine_halves(uint64_t *state)
{
	uint32_t *s32 = (uint32_t*)state;
	uint32_t t, x0, x1;
	int i;
	for (i = 24; i >= 0; --i) {
		x0 = s32[0];
		x1 = s32[1];
		t = (x0 & 0x0000FFFF) | (x1 << 16);
		x1 = (x0 >> 16) | (x1 & 0xFFFF0000);
		x0 = t;
		t = (x0 ^ (x0 >> 8)) & 0x0000FF00; x0 = x0 ^ t ^ (t << 8);
		t = (x0 ^ (x0 >> 4)) & 0x00F000F0; x0 = x0 ^ t ^ (t << 4);
		t = (x0 ^ (x0 >> 2)) & 0x0C0C0C0C; x0 = x0 ^ t ^ (t << 2);
		t = (x0 ^ (x0 >> 1)) & 0x22222222; x0 = x0 ^ t ^ (t << 1);
		*s32++ = x0;
		t = (x1 ^ (x1 >> 8)) & 0x0000FF00; x1 = x1 ^ t ^ (t << 8);
		t = (x1 ^ (x1 >> 4)) & 0x00F000F0; x1 = x1 ^ t ^ (t << 4);
		t = (x1 ^ (x1 >> 2)) & 0x0C0C0C0C; x1 = x1 ^ t ^ (t << 2);
		t = (x1 ^ (x1 >> 1)) & 0x22222222; x1 = x1 ^ t ^ (t << 1);
		*s32++ = x1;
	}
}
#endif

/*
 * In the crypto literature this function is usually called Keccak-f().
 */
static void sha3_process_block72(uint64_t *state)
{
	enum { NROUNDS = 24 };

#if OPTIMIZE_SHA3_FOR_32
	/*
	static const uint32_t IOTA_CONST_0[NROUNDS] = {
		0x00000001UL,
		0x00000000UL,
		0x00000000UL,
		0x00000000UL,
		0x00000001UL,
		0x00000001UL,
		0x00000001UL,
		0x00000001UL,
		0x00000000UL,
		0x00000000UL,
		0x00000001UL,
		0x00000000UL,
		0x00000001UL,
		0x00000001UL,
		0x00000001UL,
		0x00000001UL,
		0x00000000UL,
		0x00000000UL,
		0x00000000UL,
		0x00000000UL,
		0x00000001UL,
		0x00000000UL,
		0x00000001UL,
		0x00000000UL,
	};
	** bits are in lsb: 0101 0000 1111 0100 1111 0001
	*/
	uint32_t IOTA_CONST_0bits = (uint32_t)(0x0050f4f1);
	static const uint32_t IOTA_CONST_1[NROUNDS] = {
		0x00000000UL,
		0x00000089UL,
		0x8000008bUL,
		0x80008080UL,
		0x0000008bUL,
		0x00008000UL,
		0x80008088UL,
		0x80000082UL,
		0x0000000bUL,
		0x0000000aUL,
		0x00008082UL,
		0x00008003UL,
		0x0000808bUL,
		0x8000000bUL,
		0x8000008aUL,
		0x80000081UL,
		0x80000081UL,
		0x80000008UL,
		0x00000083UL,
		0x80008003UL,
		0x80008088UL,
		0x80000088UL,
		0x00008000UL,
		0x80008082UL,
	};

	uint32_t *const s32 = (uint32_t*)state;
	unsigned round;

	split_halves(state);

	for (round = 0; round < NROUNDS; round++) {
		unsigned x;

		/* Theta */
		{
			uint32_t BC[20];
			for (x = 0; x < 10; ++x) {
				BC[x+10] = BC[x] = s32[x]^s32[x+10]^s32[x+20]^s32[x+30]^s32[x+40];
			}
			for (x = 0; x < 10; x += 2) {
				uint32_t ta, tb;
				ta = BC[x+8] ^ rotl32(BC[x+3], 1);
				tb = BC[x+9] ^ BC[x+2];
				s32[x+0] ^= ta;
				s32[x+1] ^= tb;
				s32[x+10] ^= ta;
				s32[x+11] ^= tb;
				s32[x+20] ^= ta;
				s32[x+21] ^= tb;
				s32[x+30] ^= ta;
				s32[x+31] ^= tb;
				s32[x+40] ^= ta;
				s32[x+41] ^= tb;
			}
		}
		/* RhoPi */
		{
			uint32_t t0a,t0b, t1a,t1b;
			t1a = s32[1*2+0];
			t1b = s32[1*2+1];

#define RhoPi(PI_LANE, ROT_CONST) \
	t0a = s32[PI_LANE*2+0];\
	t0b = s32[PI_LANE*2+1];\
	if (ROT_CONST & 1) {\
		s32[PI_LANE*2+0] = rotl32(t1b, ROT_CONST/2+1);\
		s32[PI_LANE*2+1] = ROT_CONST == 1 ? t1a : rotl32(t1a, ROT_CONST/2+0);\
	} else {\
		s32[PI_LANE*2+0] = rotl32(t1a, ROT_CONST/2);\
		s32[PI_LANE*2+1] = rotl32(t1b, ROT_CONST/2);\
	}\
	t1a = t0a; t1b = t0b;

			RhoPi(10, 1)
			RhoPi( 7, 3)
			RhoPi(11, 6)
			RhoPi(17,10)
			RhoPi(18,15)
			RhoPi( 3,21)
			RhoPi( 5,28)
			RhoPi(16,36)
			RhoPi( 8,45)
			RhoPi(21,55)
			RhoPi(24, 2)
			RhoPi( 4,14)
			RhoPi(15,27)
			RhoPi(23,41)
			RhoPi(19,56)
			RhoPi(13, 8)
			RhoPi(12,25)
			RhoPi( 2,43)
			RhoPi(20,62)
			RhoPi(14,18)
			RhoPi(22,39)
			RhoPi( 9,61)
			RhoPi( 6,20)
			RhoPi( 1,44)
#undef RhoPi
		}
		/* Chi */
		for (x = 0; x <= 40;) {
			uint32_t BC0, BC1, BC2, BC3, BC4;
			BC0 = s32[x + 0*2];
			BC1 = s32[x + 1*2];
			BC2 = s32[x + 2*2];
			s32[x + 0*2] = BC0 ^ ((~BC1) & BC2);
			BC3 = s32[x + 3*2];
			s32[x + 1*2] = BC1 ^ ((~BC2) & BC3);
			BC4 = s32[x + 4*2];
			s32[x + 2*2] = BC2 ^ ((~BC3) & BC4);
			s32[x + 3*2] = BC3 ^ ((~BC4) & BC0);
			s32[x + 4*2] = BC4 ^ ((~BC0) & BC1);
			x++;
			BC0 = s32[x + 0*2];
			BC1 = s32[x + 1*2];
			BC2 = s32[x + 2*2];
			s32[x + 0*2] = BC0 ^ ((~BC1) & BC2);
			BC3 = s32[x + 3*2];
			s32[x + 1*2] = BC1 ^ ((~BC2) & BC3);
			BC4 = s32[x + 4*2];
			s32[x + 2*2] = BC2 ^ ((~BC3) & BC4);
			s32[x + 3*2] = BC3 ^ ((~BC4) & BC0);
			s32[x + 4*2] = BC4 ^ ((~BC0) & BC1);
			x += 9;
		}
		/* Iota */
		s32[0] ^= IOTA_CONST_0bits & 1;
		IOTA_CONST_0bits >>= 1;
		s32[1] ^= IOTA_CONST_1[round];
	}

	combine_halves(state);
#else
	/* Native 64-bit algorithm */
	static const uint16_t IOTA_CONST[NROUNDS] = {
		/* Elements should be 64-bit, but top half is always zero
		 * or 0x80000000. We encode 63rd bits in a separate word below.
		 * Same is true for 31th bits, which lets us use 16-bit table
		 * instead of 64-bit. The speed penalty is lost in the noise.
		 */
		0x0001,
		0x8082,
		0x808a,
		0x8000,
		0x808b,
		0x0001,
		0x8081,
		0x8009,
		0x008a,
		0x0088,
		0x8009,
		0x000a,
		0x808b,
		0x008b,
		0x8089,
		0x8003,
		0x8002,
		0x0080,
		0x800a,
		0x000a,
		0x8081,
		0x8080,
		0x0001,
		0x8008,
	};
	/* bit for CONST[0] is in msb: 0011 0011 0000 0111 1101 1101 */
	const uint32_t IOTA_CONST_bit63 = (uint32_t)(0x3307dd00);
	/* bit for CONST[0] is in msb: 0001 0110 0011 1000 0001 1011 */
	const uint32_t IOTA_CONST_bit31 = (uint32_t)(0x16381b00);

	static const uint8_t ROT_CONST[24] = {
		1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14,
		27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44,
	};
	static const uint8_t PI_LANE[24] = {
		10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
		15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1,
	};
	/*static const uint8_t MOD5[10] = { 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, };*/

	unsigned x;
	unsigned round;

	if (BB_BIG_ENDIAN) {
		for (x = 0; x < 25; x++) {
			state[x] = SWAP_LE64(state[x]);
		}
	}

	for (round = 0; round < NROUNDS; ++round) {
		/* Theta */
		{
			uint64_t BC[10];
			for (x = 0; x < 5; ++x) {
				BC[x + 5] = BC[x] = state[x]
					^ state[x + 5] ^ state[x + 10]
					^ state[x + 15]	^ state[x + 20];
			}
			/* Using 2x5 vector above eliminates the need to use
			 * BC[MOD5[x+N]] trick below to fetch BC[(x+N) % 5],
			 * and the code is a bit _smaller_.
			 */
			for (x = 0; x < 5; ++x) {
				uint64_t temp = BC[x + 4] ^ rotl64(BC[x + 1], 1);
				state[x] ^= temp;
				state[x + 5] ^= temp;
				state[x + 10] ^= temp;
				state[x + 15] ^= temp;
				state[x + 20] ^= temp;
			}
		}

		/* Rho Pi */
		if (SHA3_SMALL) {
			uint64_t t1 = state[1];
			for (x = 0; x < 24; ++x) {
				uint64_t t0 = state[PI_LANE[x]];
				state[PI_LANE[x]] = rotl64(t1, ROT_CONST[x]);
				t1 = t0;
			}
		} else {
			/* Especially large benefit for 32-bit arch (75% faster):
			 * 64-bit rotations by non-constant usually are SLOW on those.
			 * We resort to unrolling here.
			 * This optimizes out PI_LANE[] and ROT_CONST[],
			 * but generates 300-500 more bytes of code.
			 */
			uint64_t t0;
			uint64_t t1 = state[1];
#define RhoPi_twice(x) \
	t0 = state[PI_LANE[x  ]]; \
	state[PI_LANE[x  ]] = rotl64(t1, ROT_CONST[x  ]); \
	t1 = state[PI_LANE[x+1]]; \
	state[PI_LANE[x+1]] = rotl64(t0, ROT_CONST[x+1]);
			RhoPi_twice(0); RhoPi_twice(2);
			RhoPi_twice(4); RhoPi_twice(6);
			RhoPi_twice(8); RhoPi_twice(10);
			RhoPi_twice(12); RhoPi_twice(14);
			RhoPi_twice(16); RhoPi_twice(18);
			RhoPi_twice(20); RhoPi_twice(22);
#undef RhoPi_twice
		}
		/* Chi */
# if LONG_MAX > 0x7fffffff
		for (x = 0; x <= 20; x += 5) {
			uint64_t BC0, BC1, BC2, BC3, BC4;
			BC0 = state[x + 0];
			BC1 = state[x + 1];
			BC2 = state[x + 2];
			state[x + 0] = BC0 ^ ((~BC1) & BC2);
			BC3 = state[x + 3];
			state[x + 1] = BC1 ^ ((~BC2) & BC3);
			BC4 = state[x + 4];
			state[x + 2] = BC2 ^ ((~BC3) & BC4);
			state[x + 3] = BC3 ^ ((~BC4) & BC0);
			state[x + 4] = BC4 ^ ((~BC0) & BC1);
		}
# else
		/* Reduced register pressure version
		 * for register-starved 32-bit arches
		 * (i386: -95 bytes, and it is _faster_)
		 */
		for (x = 0; x <= 40;) {
			uint32_t BC0, BC1, BC2, BC3, BC4;
			uint32_t *const s32 = (uint32_t*)state;
#  if SHA3_SMALL
 do_half:
#  endif
			BC0 = s32[x + 0*2];
			BC1 = s32[x + 1*2];
			BC2 = s32[x + 2*2];
			s32[x + 0*2] = BC0 ^ ((~BC1) & BC2);
			BC3 = s32[x + 3*2];
			s32[x + 1*2] = BC1 ^ ((~BC2) & BC3);
			BC4 = s32[x + 4*2];
			s32[x + 2*2] = BC2 ^ ((~BC3) & BC4);
			s32[x + 3*2] = BC3 ^ ((~BC4) & BC0);
			s32[x + 4*2] = BC4 ^ ((~BC0) & BC1);
			x++;
#  if SHA3_SMALL
			if (x & 1)
				goto do_half;
			x += 8;
#  else
			BC0 = s32[x + 0*2];
			BC1 = s32[x + 1*2];
			BC2 = s32[x + 2*2];
			s32[x + 0*2] = BC0 ^ ((~BC1) & BC2);
			BC3 = s32[x + 3*2];
			s32[x + 1*2] = BC1 ^ ((~BC2) & BC3);
			BC4 = s32[x + 4*2];
			s32[x + 2*2] = BC2 ^ ((~BC3) & BC4);
			s32[x + 3*2] = BC3 ^ ((~BC4) & BC0);
			s32[x + 4*2] = BC4 ^ ((~BC0) & BC1);
			x += 9;
#  endif
		}
# endif /* long is 32-bit */
		/* Iota */
		state[0] ^= IOTA_CONST[round]
			| (uint32_t)((IOTA_CONST_bit31 << round) & 0x80000000)
			| (uint64_t)((IOTA_CONST_bit63 << round) & 0x80000000) << 32;
	}

	if (BB_BIG_ENDIAN) {
		for (x = 0; x < 25; x++) {
			state[x] = SWAP_LE64(state[x]);
		}
	}
#endif
}

void FAST_FUNC sha3_begin(sha3_ctx_t *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	/* SHA3-512, user can override */
	ctx->input_block_bytes = (1600 - 512*2) / 8; /* 72 bytes */
}

void FAST_FUNC sha3_hash(sha3_ctx_t *ctx, const void *buffer, size_t len)
{
#if SHA3_SMALL
	const uint8_t *data = buffer;
	unsigned bufpos = ctx->bytes_queued;

	while (1) {
		unsigned remaining = ctx->input_block_bytes - bufpos;
		if (remaining > len)
			remaining = len;
		len -= remaining;
		/* XOR data into buffer */
		while (remaining != 0) {
			uint8_t *buf = (uint8_t*)ctx->state;
			buf[bufpos] ^= *data++;
			bufpos++;
			remaining--;
		}
		/* Clever way to do "if (bufpos != N) break; ... ; bufpos = 0;" */
		bufpos -= ctx->input_block_bytes;
		if (bufpos != 0)
			break;
		/* Buffer is filled up, process it */
		sha3_process_block72(ctx->state);
		/*bufpos = 0; - already is */
	}
	ctx->bytes_queued = bufpos + ctx->input_block_bytes;
#else
	/* +50 bytes code size, but a bit faster because of long-sized XORs */
	const uint8_t *data = buffer;
	unsigned bufpos = ctx->bytes_queued;
	unsigned iblk_bytes = ctx->input_block_bytes;

	/* If already data in queue, continue queuing first */
	if (bufpos != 0) {
		while (len != 0) {
			uint8_t *buf = (uint8_t*)ctx->state;
			buf[bufpos] ^= *data++;
			len--;
			bufpos++;
			if (bufpos == iblk_bytes) {
				bufpos = 0;
				goto do_block;
			}
		}
	}

	/* Absorb complete blocks */
	while (len >= iblk_bytes) {
		/* XOR data onto beginning of state[].
		 * We try to be efficient - operate one word at a time, not byte.
		 * Careful wrt unaligned access: can't just use "*(long*)data"!
		 */
		unsigned count = iblk_bytes / sizeof(long);
		long *buf = (long*)ctx->state;
		do {
			long v;
			move_from_unaligned_long(v, (long*)data);
			*buf++ ^= v;
			data += sizeof(long);
		} while (--count);
		len -= iblk_bytes;
 do_block:
		sha3_process_block72(ctx->state);
	}

	/* Queue remaining data bytes */
	while (len != 0) {
		uint8_t *buf = (uint8_t*)ctx->state;
		buf[bufpos] ^= *data++;
		bufpos++;
		len--;
	}

	ctx->bytes_queued = bufpos;
#endif
}

unsigned FAST_FUNC sha3_end(sha3_ctx_t *ctx, void *resbuf)
{
	/* Padding */
	uint8_t *buf = (uint8_t*)ctx->state;
	/*
	 * Keccak block padding is: add 1 bit after last bit of input,
	 * then add zero bits until the end of block, and add the last 1 bit
	 * (the last bit in the block) - the "10*1" pattern.
	 * SHA3 standard appends additional two bits, 01,  before that padding:
	 *
	 * SHA3-224(M) = KECCAK[448](M||01, 224)
	 * SHA3-256(M) = KECCAK[512](M||01, 256)
	 * SHA3-384(M) = KECCAK[768](M||01, 384)
	 * SHA3-512(M) = KECCAK[1024](M||01, 512)
	 * (M is the input, || is bit concatenation)
	 *
	 * The 6 below contains 01 "SHA3" bits and the first 1 "Keccak" bit:
	 */
	buf[ctx->bytes_queued]          ^= 6; /* bit pattern 00000110 */
	buf[ctx->input_block_bytes - 1] ^= 0x80;

	sha3_process_block72(ctx->state);

	/* Output */
	memcpy(resbuf, ctx->state, 64);
	return 64;
}
