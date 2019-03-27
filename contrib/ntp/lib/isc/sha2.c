/*
 * Copyright (C) 2005-2007, 2009, 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id$ */

/*	$FreeBSD:  258945 2013-12-04 21:33:17Z roberto $	*/
/*	$KAME: sha2.c,v 1.8 2001/11/08 01:07:52 itojun Exp $	*/

/*
 * sha2.c
 *
 * Version 1.0.0beta1
 *
 * Written by Aaron D. Gifford <me@aarongifford.com>
 *
 * Copyright 2000 Aaron D. Gifford.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) AND CONTRIBUTOR(S) ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) OR CONTRIBUTOR(S) BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */


#include <config.h>

#include <isc/assertions.h>
#include <isc/platform.h>
#include <isc/sha2.h>
#include <isc/string.h>
#include <isc/util.h>

#ifdef ISC_PLATFORM_OPENSSLHASH

void
isc_sha224_init(isc_sha224_t *context) {
	if (context == (isc_sha224_t *)0) {
		return;
	}
	EVP_DigestInit(context, EVP_sha224());
}

void
isc_sha224_invalidate(isc_sha224_t *context) {
	EVP_MD_CTX_cleanup(context);
}

void
isc_sha224_update(isc_sha224_t *context, const isc_uint8_t* data, size_t len) {
	if (len == 0U) {
		/* Calling with no data is valid - we do nothing */
		return;
	}

	/* Sanity check: */
	REQUIRE(context != (isc_sha224_t *)0 && data != (isc_uint8_t*)0);

	EVP_DigestUpdate(context, (const void *) data, len);
}

void
isc_sha224_final(isc_uint8_t digest[], isc_sha224_t *context) {
	/* Sanity check: */
	REQUIRE(context != (isc_sha224_t *)0);

	/* If no digest buffer is passed, we don't bother doing this: */
	if (digest != (isc_uint8_t*)0) {
		EVP_DigestFinal(context, digest, NULL);
	} else {
		EVP_MD_CTX_cleanup(context);
	}
}

void
isc_sha256_init(isc_sha256_t *context) {
	if (context == (isc_sha256_t *)0) {
		return;
	}
	EVP_DigestInit(context, EVP_sha256());
}

void
isc_sha256_invalidate(isc_sha256_t *context) {
	EVP_MD_CTX_cleanup(context);
}

void
isc_sha256_update(isc_sha256_t *context, const isc_uint8_t *data, size_t len) {
	if (len == 0U) {
		/* Calling with no data is valid - we do nothing */
		return;
	}

	/* Sanity check: */
	REQUIRE(context != (isc_sha256_t *)0 && data != (isc_uint8_t*)0);

	EVP_DigestUpdate(context, (const void *) data, len);
}

void
isc_sha256_final(isc_uint8_t digest[], isc_sha256_t *context) {
	/* Sanity check: */
	REQUIRE(context != (isc_sha256_t *)0);

	/* If no digest buffer is passed, we don't bother doing this: */
	if (digest != (isc_uint8_t*)0) {
		EVP_DigestFinal(context, digest, NULL);
	} else {
		EVP_MD_CTX_cleanup(context);
	}
}

void
isc_sha512_init(isc_sha512_t *context) {
	if (context == (isc_sha512_t *)0) {
		return;
	}
	EVP_DigestInit(context, EVP_sha512());
}

void
isc_sha512_invalidate(isc_sha512_t *context) {
	EVP_MD_CTX_cleanup(context);
}

void isc_sha512_update(isc_sha512_t *context, const isc_uint8_t *data, size_t len) {
	if (len == 0U) {
		/* Calling with no data is valid - we do nothing */
		return;
	}

	/* Sanity check: */
	REQUIRE(context != (isc_sha512_t *)0 && data != (isc_uint8_t*)0);

	EVP_DigestUpdate(context, (const void *) data, len);
}

void isc_sha512_final(isc_uint8_t digest[], isc_sha512_t *context) {
	/* Sanity check: */
	REQUIRE(context != (isc_sha512_t *)0);

	/* If no digest buffer is passed, we don't bother doing this: */
	if (digest != (isc_uint8_t*)0) {
		EVP_DigestFinal(context, digest, NULL);
	} else {
		EVP_MD_CTX_cleanup(context);
	}
}

void
isc_sha384_init(isc_sha384_t *context) {
	if (context == (isc_sha384_t *)0) {
		return;
	}
	EVP_DigestInit(context, EVP_sha384());
}

void
isc_sha384_invalidate(isc_sha384_t *context) {
	EVP_MD_CTX_cleanup(context);
}

void
isc_sha384_update(isc_sha384_t *context, const isc_uint8_t* data, size_t len) {
	if (len == 0U) {
		/* Calling with no data is valid - we do nothing */
		return;
	}

	/* Sanity check: */
	REQUIRE(context != (isc_sha512_t *)0 && data != (isc_uint8_t*)0);

	EVP_DigestUpdate(context, (const void *) data, len);
}

void
isc_sha384_final(isc_uint8_t digest[], isc_sha384_t *context) {
	/* Sanity check: */
	REQUIRE(context != (isc_sha384_t *)0);

	/* If no digest buffer is passed, we don't bother doing this: */
	if (digest != (isc_uint8_t*)0) {
		EVP_DigestFinal(context, digest, NULL);
	} else {
		EVP_MD_CTX_cleanup(context);
	}
}

#else

/*
 * UNROLLED TRANSFORM LOOP NOTE:
 * You can define SHA2_UNROLL_TRANSFORM to use the unrolled transform
 * loop version for the hash transform rounds (defined using macros
 * later in this file).  Either define on the command line, for example:
 *
 *   cc -DISC_SHA2_UNROLL_TRANSFORM -o sha2 sha2.c sha2prog.c
 *
 * or define below:
 *
 *   \#define ISC_SHA2_UNROLL_TRANSFORM
 *
 */

/*** SHA-256/384/512 Machine Architecture Definitions *****************/
/*
 * BYTE_ORDER NOTE:
 *
 * Please make sure that your system defines BYTE_ORDER.  If your
 * architecture is little-endian, make sure it also defines
 * LITTLE_ENDIAN and that the two (BYTE_ORDER and LITTLE_ENDIAN) are
 * equivalent.
 *
 * If your system does not define the above, then you can do so by
 * hand like this:
 *
 *   \#define LITTLE_ENDIAN 1234
 *   \#define BIG_ENDIAN    4321
 *
 * And for little-endian machines, add:
 *
 *   \#define BYTE_ORDER LITTLE_ENDIAN
 *
 * Or for big-endian machines:
 *
 *   \#define BYTE_ORDER BIG_ENDIAN
 *
 * The FreeBSD machine this was written on defines BYTE_ORDER
 * appropriately by including <sys/types.h> (which in turn includes
 * <machine/endian.h> where the appropriate definitions are actually
 * made).
 */
#if !defined(BYTE_ORDER) || (BYTE_ORDER != LITTLE_ENDIAN && BYTE_ORDER != BIG_ENDIAN)
#ifndef BYTE_ORDER
#ifndef BIG_ENDIAN
#define BIG_ENDIAN 4321
#endif
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN 1234
#endif
#ifdef WORDS_BIGENDIAN
#define BYTE_ORDER BIG_ENDIAN
#else
#define BYTE_ORDER LITTLE_ENDIAN
#endif
#else
#error Define BYTE_ORDER to be equal to either LITTLE_ENDIAN or BIG_ENDIAN
#endif
#endif

/*** SHA-256/384/512 Various Length Definitions ***********************/
/* NOTE: Most of these are in sha2.h */
#define ISC_SHA256_SHORT_BLOCK_LENGTH	(ISC_SHA256_BLOCK_LENGTH - 8)
#define ISC_SHA384_SHORT_BLOCK_LENGTH	(ISC_SHA384_BLOCK_LENGTH - 16)
#define ISC_SHA512_SHORT_BLOCK_LENGTH	(ISC_SHA512_BLOCK_LENGTH - 16)


/*** ENDIAN REVERSAL MACROS *******************************************/
#if BYTE_ORDER == LITTLE_ENDIAN
#define REVERSE32(w,x)	{ \
	isc_uint32_t tmp = (w); \
	tmp = (tmp >> 16) | (tmp << 16); \
	(x) = ((tmp & 0xff00ff00UL) >> 8) | ((tmp & 0x00ff00ffUL) << 8); \
}
#ifdef WIN32
#define REVERSE64(w,x)	{ \
	isc_uint64_t tmp = (w); \
	tmp = (tmp >> 32) | (tmp << 32); \
	tmp = ((tmp & 0xff00ff00ff00ff00UL) >> 8) | \
	      ((tmp & 0x00ff00ff00ff00ffUL) << 8); \
	(x) = ((tmp & 0xffff0000ffff0000UL) >> 16) | \
	      ((tmp & 0x0000ffff0000ffffUL) << 16); \
}
#else
#define REVERSE64(w,x)	{ \
	isc_uint64_t tmp = (w); \
	tmp = (tmp >> 32) | (tmp << 32); \
	tmp = ((tmp & 0xff00ff00ff00ff00ULL) >> 8) | \
	      ((tmp & 0x00ff00ff00ff00ffULL) << 8); \
	(x) = ((tmp & 0xffff0000ffff0000ULL) >> 16) | \
	      ((tmp & 0x0000ffff0000ffffULL) << 16); \
}
#endif
#endif /* BYTE_ORDER == LITTLE_ENDIAN */

/*
 * Macro for incrementally adding the unsigned 64-bit integer n to the
 * unsigned 128-bit integer (represented using a two-element array of
 * 64-bit words):
 */
#define ADDINC128(w,n)	{ \
	(w)[0] += (isc_uint64_t)(n); \
	if ((w)[0] < (n)) { \
		(w)[1]++; \
	} \
}

/*** THE SIX LOGICAL FUNCTIONS ****************************************/
/*
 * Bit shifting and rotation (used by the six SHA-XYZ logical functions:
 *
 *   NOTE:  The naming of R and S appears backwards here (R is a SHIFT and
 *   S is a ROTATION) because the SHA-256/384/512 description document
 *   (see http://csrc.nist.gov/cryptval/shs/sha256-384-512.pdf) uses this
 *   same "backwards" definition.
 */
/* Shift-right (used in SHA-256, SHA-384, and SHA-512): */
#define R(b,x) 		((x) >> (b))
/* 32-bit Rotate-right (used in SHA-256): */
#define S32(b,x)	(((x) >> (b)) | ((x) << (32 - (b))))
/* 64-bit Rotate-right (used in SHA-384 and SHA-512): */
#define S64(b,x)	(((x) >> (b)) | ((x) << (64 - (b))))

/* Two of six logical functions used in SHA-256, SHA-384, and SHA-512: */
#define Ch(x,y,z)	(((x) & (y)) ^ ((~(x)) & (z)))
#define Maj(x,y,z)	(((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

/* Four of six logical functions used in SHA-256: */
#define Sigma0_256(x)	(S32(2,  (x)) ^ S32(13, (x)) ^ S32(22, (x)))
#define Sigma1_256(x)	(S32(6,  (x)) ^ S32(11, (x)) ^ S32(25, (x)))
#define sigma0_256(x)	(S32(7,  (x)) ^ S32(18, (x)) ^ R(3 ,   (x)))
#define sigma1_256(x)	(S32(17, (x)) ^ S32(19, (x)) ^ R(10,   (x)))

/* Four of six logical functions used in SHA-384 and SHA-512: */
#define Sigma0_512(x)	(S64(28, (x)) ^ S64(34, (x)) ^ S64(39, (x)))
#define Sigma1_512(x)	(S64(14, (x)) ^ S64(18, (x)) ^ S64(41, (x)))
#define sigma0_512(x)	(S64( 1, (x)) ^ S64( 8, (x)) ^ R( 7,   (x)))
#define sigma1_512(x)	(S64(19, (x)) ^ S64(61, (x)) ^ R( 6,   (x)))

/*** INTERNAL FUNCTION PROTOTYPES *************************************/
/* NOTE: These should not be accessed directly from outside this
 * library -- they are intended for private internal visibility/use
 * only.
 */
void isc_sha512_last(isc_sha512_t *);
void isc_sha256_transform(isc_sha256_t *, const isc_uint32_t*);
void isc_sha512_transform(isc_sha512_t *, const isc_uint64_t*);


/*** SHA-XYZ INITIAL HASH VALUES AND CONSTANTS ************************/
/* Hash constant words K for SHA-224 and SHA-256: */
static const isc_uint32_t K256[64] = {
	0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL,
	0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
	0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
	0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
	0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
	0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
	0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL,
	0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
	0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
	0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
	0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL,
	0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
	0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
	0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
	0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
	0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};

/* Initial hash value H for SHA-224: */
static const isc_uint32_t sha224_initial_hash_value[8] = {
	0xc1059ed8UL,
	0x367cd507UL,
	0x3070dd17UL,
	0xf70e5939UL,
	0xffc00b31UL,
	0x68581511UL,
	0x64f98fa7UL,
	0xbefa4fa4UL
};

/* Initial hash value H for SHA-256: */
static const isc_uint32_t sha256_initial_hash_value[8] = {
	0x6a09e667UL,
	0xbb67ae85UL,
	0x3c6ef372UL,
	0xa54ff53aUL,
	0x510e527fUL,
	0x9b05688cUL,
	0x1f83d9abUL,
	0x5be0cd19UL
};

#ifdef WIN32
/* Hash constant words K for SHA-384 and SHA-512: */
static const isc_uint64_t K512[80] = {
	0x428a2f98d728ae22UL, 0x7137449123ef65cdUL,
	0xb5c0fbcfec4d3b2fUL, 0xe9b5dba58189dbbcUL,
	0x3956c25bf348b538UL, 0x59f111f1b605d019UL,
	0x923f82a4af194f9bUL, 0xab1c5ed5da6d8118UL,
	0xd807aa98a3030242UL, 0x12835b0145706fbeUL,
	0x243185be4ee4b28cUL, 0x550c7dc3d5ffb4e2UL,
	0x72be5d74f27b896fUL, 0x80deb1fe3b1696b1UL,
	0x9bdc06a725c71235UL, 0xc19bf174cf692694UL,
	0xe49b69c19ef14ad2UL, 0xefbe4786384f25e3UL,
	0x0fc19dc68b8cd5b5UL, 0x240ca1cc77ac9c65UL,
	0x2de92c6f592b0275UL, 0x4a7484aa6ea6e483UL,
	0x5cb0a9dcbd41fbd4UL, 0x76f988da831153b5UL,
	0x983e5152ee66dfabUL, 0xa831c66d2db43210UL,
	0xb00327c898fb213fUL, 0xbf597fc7beef0ee4UL,
	0xc6e00bf33da88fc2UL, 0xd5a79147930aa725UL,
	0x06ca6351e003826fUL, 0x142929670a0e6e70UL,
	0x27b70a8546d22ffcUL, 0x2e1b21385c26c926UL,
	0x4d2c6dfc5ac42aedUL, 0x53380d139d95b3dfUL,
	0x650a73548baf63deUL, 0x766a0abb3c77b2a8UL,
	0x81c2c92e47edaee6UL, 0x92722c851482353bUL,
	0xa2bfe8a14cf10364UL, 0xa81a664bbc423001UL,
	0xc24b8b70d0f89791UL, 0xc76c51a30654be30UL,
	0xd192e819d6ef5218UL, 0xd69906245565a910UL,
	0xf40e35855771202aUL, 0x106aa07032bbd1b8UL,
	0x19a4c116b8d2d0c8UL, 0x1e376c085141ab53UL,
	0x2748774cdf8eeb99UL, 0x34b0bcb5e19b48a8UL,
	0x391c0cb3c5c95a63UL, 0x4ed8aa4ae3418acbUL,
	0x5b9cca4f7763e373UL, 0x682e6ff3d6b2b8a3UL,
	0x748f82ee5defb2fcUL, 0x78a5636f43172f60UL,
	0x84c87814a1f0ab72UL, 0x8cc702081a6439ecUL,
	0x90befffa23631e28UL, 0xa4506cebde82bde9UL,
	0xbef9a3f7b2c67915UL, 0xc67178f2e372532bUL,
	0xca273eceea26619cUL, 0xd186b8c721c0c207UL,
	0xeada7dd6cde0eb1eUL, 0xf57d4f7fee6ed178UL,
	0x06f067aa72176fbaUL, 0x0a637dc5a2c898a6UL,
	0x113f9804bef90daeUL, 0x1b710b35131c471bUL,
	0x28db77f523047d84UL, 0x32caab7b40c72493UL,
	0x3c9ebe0a15c9bebcUL, 0x431d67c49c100d4cUL,
	0x4cc5d4becb3e42b6UL, 0x597f299cfc657e2aUL,
	0x5fcb6fab3ad6faecUL, 0x6c44198c4a475817UL
};

/* Initial hash value H for SHA-384: */
static const isc_uint64_t sha384_initial_hash_value[8] = {
	0xcbbb9d5dc1059ed8UL,
	0x629a292a367cd507UL,
	0x9159015a3070dd17UL,
	0x152fecd8f70e5939UL,
	0x67332667ffc00b31UL,
	0x8eb44a8768581511UL,
	0xdb0c2e0d64f98fa7UL,
	0x47b5481dbefa4fa4UL
};

/* Initial hash value H for SHA-512: */
static const isc_uint64_t sha512_initial_hash_value[8] = {
	0x6a09e667f3bcc908U,
	0xbb67ae8584caa73bUL,
	0x3c6ef372fe94f82bUL,
	0xa54ff53a5f1d36f1UL,
	0x510e527fade682d1UL,
	0x9b05688c2b3e6c1fUL,
	0x1f83d9abfb41bd6bUL,
	0x5be0cd19137e2179UL
};
#else
/* Hash constant words K for SHA-384 and SHA-512: */
static const isc_uint64_t K512[80] = {
	0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL,
	0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
	0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
	0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
	0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
	0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
	0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL,
	0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
	0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
	0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
	0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL,
	0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
	0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL,
	0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
	0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
	0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
	0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL,
	0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
	0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL,
	0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
	0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
	0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
	0xd192e819d6ef5218ULL, 0xd69906245565a910ULL,
	0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
	0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
	0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
	0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
	0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
	0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL,
	0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
	0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL,
	0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
	0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
	0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
	0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
	0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
	0x28db77f523047d84ULL, 0x32caab7b40c72493ULL,
	0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
	0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
	0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

/* Initial hash value H for SHA-384: */
static const isc_uint64_t sha384_initial_hash_value[8] = {
	0xcbbb9d5dc1059ed8ULL,
	0x629a292a367cd507ULL,
	0x9159015a3070dd17ULL,
	0x152fecd8f70e5939ULL,
	0x67332667ffc00b31ULL,
	0x8eb44a8768581511ULL,
	0xdb0c2e0d64f98fa7ULL,
	0x47b5481dbefa4fa4ULL
};

/* Initial hash value H for SHA-512: */
static const isc_uint64_t sha512_initial_hash_value[8] = {
	0x6a09e667f3bcc908ULL,
	0xbb67ae8584caa73bULL,
	0x3c6ef372fe94f82bULL,
	0xa54ff53a5f1d36f1ULL,
	0x510e527fade682d1ULL,
	0x9b05688c2b3e6c1fULL,
	0x1f83d9abfb41bd6bULL,
	0x5be0cd19137e2179ULL
};
#endif


/*** SHA-224: *********************************************************/
void
isc_sha224_init(isc_sha224_t *context) {
	if (context == (isc_sha256_t *)0) {
		return;
	}
	memcpy(context->state, sha224_initial_hash_value,
	       ISC_SHA256_DIGESTLENGTH);
	memset(context->buffer, 0, ISC_SHA256_BLOCK_LENGTH);
	context->bitcount = 0;
}

void
isc_sha224_invalidate(isc_sha224_t *context) {
	memset(context, 0, sizeof(isc_sha224_t));
}

void
isc_sha224_update(isc_sha224_t *context, const isc_uint8_t* data, size_t len) {
	isc_sha256_update((isc_sha256_t *)context, data, len);
}

void
isc_sha224_final(isc_uint8_t digest[], isc_sha224_t *context) {
	isc_uint8_t sha256_digest[ISC_SHA256_DIGESTLENGTH];
	isc_sha256_final(sha256_digest, (isc_sha256_t *)context);
	memcpy(digest, sha256_digest, ISC_SHA224_DIGESTLENGTH);
	memset(sha256_digest, 0, ISC_SHA256_DIGESTLENGTH);
}

/*** SHA-256: *********************************************************/
void
isc_sha256_init(isc_sha256_t *context) {
	if (context == (isc_sha256_t *)0) {
		return;
	}
	memcpy(context->state, sha256_initial_hash_value,
	       ISC_SHA256_DIGESTLENGTH);
	memset(context->buffer, 0, ISC_SHA256_BLOCK_LENGTH);
	context->bitcount = 0;
}

void
isc_sha256_invalidate(isc_sha256_t *context) {
	memset(context, 0, sizeof(isc_sha256_t));
}

#ifdef ISC_SHA2_UNROLL_TRANSFORM

/* Unrolled SHA-256 round macros: */

#if BYTE_ORDER == LITTLE_ENDIAN

#define ROUND256_0_TO_15(a,b,c,d,e,f,g,h)	\
	REVERSE32(*data++, W256[j]); \
	T1 = (h) + Sigma1_256(e) + Ch((e), (f), (g)) + \
	     K256[j] + W256[j]; \
	(d) += T1; \
	(h) = T1 + Sigma0_256(a) + Maj((a), (b), (c)); \
	j++


#else /* BYTE_ORDER == LITTLE_ENDIAN */

#define ROUND256_0_TO_15(a,b,c,d,e,f,g,h)	\
	T1 = (h) + Sigma1_256(e) + Ch((e), (f), (g)) + \
	     K256[j] + (W256[j] = *data++); \
	(d) += T1; \
	(h) = T1 + Sigma0_256(a) + Maj((a), (b), (c)); \
	j++

#endif /* BYTE_ORDER == LITTLE_ENDIAN */

#define ROUND256(a,b,c,d,e,f,g,h)	\
	s0 = W256[(j+1)&0x0f]; \
	s0 = sigma0_256(s0); \
	s1 = W256[(j+14)&0x0f]; \
	s1 = sigma1_256(s1); \
	T1 = (h) + Sigma1_256(e) + Ch((e), (f), (g)) + K256[j] + \
	     (W256[j&0x0f] += s1 + W256[(j+9)&0x0f] + s0); \
	(d) += T1; \
	(h) = T1 + Sigma0_256(a) + Maj((a), (b), (c)); \
	j++

void isc_sha256_transform(isc_sha256_t *context, const isc_uint32_t* data) {
	isc_uint32_t	a, b, c, d, e, f, g, h, s0, s1;
	isc_uint32_t	T1, *W256;
	int		j;

	W256 = (isc_uint32_t*)context->buffer;

	/* Initialize registers with the prev. intermediate value */
	a = context->state[0];
	b = context->state[1];
	c = context->state[2];
	d = context->state[3];
	e = context->state[4];
	f = context->state[5];
	g = context->state[6];
	h = context->state[7];

	j = 0;
	do {
		/* Rounds 0 to 15 (unrolled): */
		ROUND256_0_TO_15(a,b,c,d,e,f,g,h);
		ROUND256_0_TO_15(h,a,b,c,d,e,f,g);
		ROUND256_0_TO_15(g,h,a,b,c,d,e,f);
		ROUND256_0_TO_15(f,g,h,a,b,c,d,e);
		ROUND256_0_TO_15(e,f,g,h,a,b,c,d);
		ROUND256_0_TO_15(d,e,f,g,h,a,b,c);
		ROUND256_0_TO_15(c,d,e,f,g,h,a,b);
		ROUND256_0_TO_15(b,c,d,e,f,g,h,a);
	} while (j < 16);

	/* Now for the remaining rounds to 64: */
	do {
		ROUND256(a,b,c,d,e,f,g,h);
		ROUND256(h,a,b,c,d,e,f,g);
		ROUND256(g,h,a,b,c,d,e,f);
		ROUND256(f,g,h,a,b,c,d,e);
		ROUND256(e,f,g,h,a,b,c,d);
		ROUND256(d,e,f,g,h,a,b,c);
		ROUND256(c,d,e,f,g,h,a,b);
		ROUND256(b,c,d,e,f,g,h,a);
	} while (j < 64);

	/* Compute the current intermediate hash value */
	context->state[0] += a;
	context->state[1] += b;
	context->state[2] += c;
	context->state[3] += d;
	context->state[4] += e;
	context->state[5] += f;
	context->state[6] += g;
	context->state[7] += h;

	/* Clean up */
	a = b = c = d = e = f = g = h = T1 = 0;
	/* Avoid compiler warnings */
	POST(a); POST(b); POST(c); POST(d); POST(e); POST(f);
	POST(g); POST(h); POST(T1);
}

#else /* ISC_SHA2_UNROLL_TRANSFORM */

void
isc_sha256_transform(isc_sha256_t *context, const isc_uint32_t* data) {
	isc_uint32_t	a, b, c, d, e, f, g, h, s0, s1;
	isc_uint32_t	T1, T2, *W256;
	int		j;

	W256 = (isc_uint32_t*)context->buffer;

	/* Initialize registers with the prev. intermediate value */
	a = context->state[0];
	b = context->state[1];
	c = context->state[2];
	d = context->state[3];
	e = context->state[4];
	f = context->state[5];
	g = context->state[6];
	h = context->state[7];

	j = 0;
	do {
#if BYTE_ORDER == LITTLE_ENDIAN
		/* Copy data while converting to host byte order */
		REVERSE32(*data++,W256[j]);
		/* Apply the SHA-256 compression function to update a..h */
		T1 = h + Sigma1_256(e) + Ch(e, f, g) + K256[j] + W256[j];
#else /* BYTE_ORDER == LITTLE_ENDIAN */
		/* Apply the SHA-256 compression function to update a..h with copy */
		T1 = h + Sigma1_256(e) + Ch(e, f, g) + K256[j] + (W256[j] = *data++);
#endif /* BYTE_ORDER == LITTLE_ENDIAN */
		T2 = Sigma0_256(a) + Maj(a, b, c);
		h = g;
		g = f;
		f = e;
		e = d + T1;
		d = c;
		c = b;
		b = a;
		a = T1 + T2;

		j++;
	} while (j < 16);

	do {
		/* Part of the message block expansion: */
		s0 = W256[(j+1)&0x0f];
		s0 = sigma0_256(s0);
		s1 = W256[(j+14)&0x0f];
		s1 = sigma1_256(s1);

		/* Apply the SHA-256 compression function to update a..h */
		T1 = h + Sigma1_256(e) + Ch(e, f, g) + K256[j] +
		     (W256[j&0x0f] += s1 + W256[(j+9)&0x0f] + s0);
		T2 = Sigma0_256(a) + Maj(a, b, c);
		h = g;
		g = f;
		f = e;
		e = d + T1;
		d = c;
		c = b;
		b = a;
		a = T1 + T2;

		j++;
	} while (j < 64);

	/* Compute the current intermediate hash value */
	context->state[0] += a;
	context->state[1] += b;
	context->state[2] += c;
	context->state[3] += d;
	context->state[4] += e;
	context->state[5] += f;
	context->state[6] += g;
	context->state[7] += h;

	/* Clean up */
	a = b = c = d = e = f = g = h = T1 = T2 = 0;
	/* Avoid compiler warnings */
	POST(a); POST(b); POST(c); POST(d); POST(e); POST(f);
	POST(g); POST(h); POST(T1); POST(T2);
}

#endif /* ISC_SHA2_UNROLL_TRANSFORM */

void
isc_sha256_update(isc_sha256_t *context, const isc_uint8_t *data, size_t len) {
	unsigned int	freespace, usedspace;

	if (len == 0U) {
		/* Calling with no data is valid - we do nothing */
		return;
	}

	/* Sanity check: */
	REQUIRE(context != (isc_sha256_t *)0 && data != (isc_uint8_t*)0);

	usedspace = (unsigned int)((context->bitcount >> 3) %
				   ISC_SHA256_BLOCK_LENGTH);
	if (usedspace > 0) {
		/* Calculate how much free space is available in the buffer */
		freespace = ISC_SHA256_BLOCK_LENGTH - usedspace;

		if (len >= freespace) {
			/* Fill the buffer completely and process it */
			memcpy(&context->buffer[usedspace], data, freespace);
			context->bitcount += freespace << 3;
			len -= freespace;
			data += freespace;
			isc_sha256_transform(context,
					     (isc_uint32_t*)context->buffer);
		} else {
			/* The buffer is not yet full */
			memcpy(&context->buffer[usedspace], data, len);
			context->bitcount += len << 3;
			/* Clean up: */
			usedspace = freespace = 0;
			/* Avoid compiler warnings: */
			POST(usedspace); POST(freespace);
			return;
		}
	}
	while (len >= ISC_SHA256_BLOCK_LENGTH) {
		/* Process as many complete blocks as we can */
		memcpy(context->buffer, data, ISC_SHA256_BLOCK_LENGTH);
		isc_sha256_transform(context, (isc_uint32_t*)context->buffer);
		context->bitcount += ISC_SHA256_BLOCK_LENGTH << 3;
		len -= ISC_SHA256_BLOCK_LENGTH;
		data += ISC_SHA256_BLOCK_LENGTH;
	}
	if (len > 0U) {
		/* There's left-overs, so save 'em */
		memcpy(context->buffer, data, len);
		context->bitcount += len << 3;
	}
	/* Clean up: */
	usedspace = freespace = 0;
	/* Avoid compiler warnings: */
	POST(usedspace); POST(freespace);
}

void
isc_sha256_final(isc_uint8_t digest[], isc_sha256_t *context) {
	isc_uint32_t	*d = (isc_uint32_t*)digest;
	unsigned int	usedspace;

	/* Sanity check: */
	REQUIRE(context != (isc_sha256_t *)0);

	/* If no digest buffer is passed, we don't bother doing this: */
	if (digest != (isc_uint8_t*)0) {
		usedspace = (unsigned int)((context->bitcount >> 3) %
					   ISC_SHA256_BLOCK_LENGTH);
#if BYTE_ORDER == LITTLE_ENDIAN
		/* Convert FROM host byte order */
		REVERSE64(context->bitcount,context->bitcount);
#endif
		if (usedspace > 0) {
			/* Begin padding with a 1 bit: */
			context->buffer[usedspace++] = 0x80;

			if (usedspace <= ISC_SHA256_SHORT_BLOCK_LENGTH) {
				/* Set-up for the last transform: */
				memset(&context->buffer[usedspace], 0,
				       ISC_SHA256_SHORT_BLOCK_LENGTH - usedspace);
			} else {
				if (usedspace < ISC_SHA256_BLOCK_LENGTH) {
					memset(&context->buffer[usedspace], 0,
					       ISC_SHA256_BLOCK_LENGTH -
					       usedspace);
				}
				/* Do second-to-last transform: */
				isc_sha256_transform(context,
					       (isc_uint32_t*)context->buffer);

				/* And set-up for the last transform: */
				memset(context->buffer, 0,
				       ISC_SHA256_SHORT_BLOCK_LENGTH);
			}
		} else {
			/* Set-up for the last transform: */
			memset(context->buffer, 0, ISC_SHA256_SHORT_BLOCK_LENGTH);

			/* Begin padding with a 1 bit: */
			*context->buffer = 0x80;
		}
		/* Set the bit count: */
		*(isc_uint64_t*)&context->buffer[ISC_SHA256_SHORT_BLOCK_LENGTH] = context->bitcount;

		/* Final transform: */
		isc_sha256_transform(context, (isc_uint32_t*)context->buffer);

#if BYTE_ORDER == LITTLE_ENDIAN
		{
			/* Convert TO host byte order */
			int	j;
			for (j = 0; j < 8; j++) {
				REVERSE32(context->state[j],context->state[j]);
				*d++ = context->state[j];
			}
		}
#else
		memcpy(d, context->state, ISC_SHA256_DIGESTLENGTH);
#endif
	}

	/* Clean up state data: */
	memset(context, 0, sizeof(*context));
	usedspace = 0;
	POST(usedspace);
}

/*** SHA-512: *********************************************************/
void
isc_sha512_init(isc_sha512_t *context) {
	if (context == (isc_sha512_t *)0) {
		return;
	}
	memcpy(context->state, sha512_initial_hash_value,
	       ISC_SHA512_DIGESTLENGTH);
	memset(context->buffer, 0, ISC_SHA512_BLOCK_LENGTH);
	context->bitcount[0] = context->bitcount[1] =  0;
}

void
isc_sha512_invalidate(isc_sha512_t *context) {
	memset(context, 0, sizeof(isc_sha512_t));
}

#ifdef ISC_SHA2_UNROLL_TRANSFORM

/* Unrolled SHA-512 round macros: */
#if BYTE_ORDER == LITTLE_ENDIAN

#define ROUND512_0_TO_15(a,b,c,d,e,f,g,h)	\
	REVERSE64(*data++, W512[j]); \
	T1 = (h) + Sigma1_512(e) + Ch((e), (f), (g)) + \
	     K512[j] + W512[j]; \
	(d) += T1, \
	(h) = T1 + Sigma0_512(a) + Maj((a), (b), (c)), \
	j++


#else /* BYTE_ORDER == LITTLE_ENDIAN */

#define ROUND512_0_TO_15(a,b,c,d,e,f,g,h)	\
	T1 = (h) + Sigma1_512(e) + Ch((e), (f), (g)) + \
	     K512[j] + (W512[j] = *data++); \
	(d) += T1; \
	(h) = T1 + Sigma0_512(a) + Maj((a), (b), (c)); \
	j++

#endif /* BYTE_ORDER == LITTLE_ENDIAN */

#define ROUND512(a,b,c,d,e,f,g,h)	\
	s0 = W512[(j+1)&0x0f]; \
	s0 = sigma0_512(s0); \
	s1 = W512[(j+14)&0x0f]; \
	s1 = sigma1_512(s1); \
	T1 = (h) + Sigma1_512(e) + Ch((e), (f), (g)) + K512[j] + \
	     (W512[j&0x0f] += s1 + W512[(j+9)&0x0f] + s0); \
	(d) += T1; \
	(h) = T1 + Sigma0_512(a) + Maj((a), (b), (c)); \
	j++

void isc_sha512_transform(isc_sha512_t *context, const isc_uint64_t* data) {
	isc_uint64_t	a, b, c, d, e, f, g, h, s0, s1;
	isc_uint64_t	T1, *W512 = (isc_uint64_t*)context->buffer;
	int		j;

	/* Initialize registers with the prev. intermediate value */
	a = context->state[0];
	b = context->state[1];
	c = context->state[2];
	d = context->state[3];
	e = context->state[4];
	f = context->state[5];
	g = context->state[6];
	h = context->state[7];

	j = 0;
	do {
		ROUND512_0_TO_15(a,b,c,d,e,f,g,h);
		ROUND512_0_TO_15(h,a,b,c,d,e,f,g);
		ROUND512_0_TO_15(g,h,a,b,c,d,e,f);
		ROUND512_0_TO_15(f,g,h,a,b,c,d,e);
		ROUND512_0_TO_15(e,f,g,h,a,b,c,d);
		ROUND512_0_TO_15(d,e,f,g,h,a,b,c);
		ROUND512_0_TO_15(c,d,e,f,g,h,a,b);
		ROUND512_0_TO_15(b,c,d,e,f,g,h,a);
	} while (j < 16);

	/* Now for the remaining rounds up to 79: */
	do {
		ROUND512(a,b,c,d,e,f,g,h);
		ROUND512(h,a,b,c,d,e,f,g);
		ROUND512(g,h,a,b,c,d,e,f);
		ROUND512(f,g,h,a,b,c,d,e);
		ROUND512(e,f,g,h,a,b,c,d);
		ROUND512(d,e,f,g,h,a,b,c);
		ROUND512(c,d,e,f,g,h,a,b);
		ROUND512(b,c,d,e,f,g,h,a);
	} while (j < 80);

	/* Compute the current intermediate hash value */
	context->state[0] += a;
	context->state[1] += b;
	context->state[2] += c;
	context->state[3] += d;
	context->state[4] += e;
	context->state[5] += f;
	context->state[6] += g;
	context->state[7] += h;

	/* Clean up */
	a = b = c = d = e = f = g = h = T1 = 0;
	/* Avoid compiler warnings */
	POST(a); POST(b); POST(c); POST(d); POST(e); POST(f);
	POST(g); POST(h); POST(T1);
}

#else /* ISC_SHA2_UNROLL_TRANSFORM */

void
isc_sha512_transform(isc_sha512_t *context, const isc_uint64_t* data) {
	isc_uint64_t	a, b, c, d, e, f, g, h, s0, s1;
	isc_uint64_t	T1, T2, *W512 = (isc_uint64_t*)context->buffer;
	int		j;

	/* Initialize registers with the prev. intermediate value */
	a = context->state[0];
	b = context->state[1];
	c = context->state[2];
	d = context->state[3];
	e = context->state[4];
	f = context->state[5];
	g = context->state[6];
	h = context->state[7];

	j = 0;
	do {
#if BYTE_ORDER == LITTLE_ENDIAN
		/* Convert TO host byte order */
		REVERSE64(*data++, W512[j]);
		/* Apply the SHA-512 compression function to update a..h */
		T1 = h + Sigma1_512(e) + Ch(e, f, g) + K512[j] + W512[j];
#else /* BYTE_ORDER == LITTLE_ENDIAN */
		/* Apply the SHA-512 compression function to update a..h with copy */
		T1 = h + Sigma1_512(e) + Ch(e, f, g) + K512[j] + (W512[j] = *data++);
#endif /* BYTE_ORDER == LITTLE_ENDIAN */
		T2 = Sigma0_512(a) + Maj(a, b, c);
		h = g;
		g = f;
		f = e;
		e = d + T1;
		d = c;
		c = b;
		b = a;
		a = T1 + T2;

		j++;
	} while (j < 16);

	do {
		/* Part of the message block expansion: */
		s0 = W512[(j+1)&0x0f];
		s0 = sigma0_512(s0);
		s1 = W512[(j+14)&0x0f];
		s1 =  sigma1_512(s1);

		/* Apply the SHA-512 compression function to update a..h */
		T1 = h + Sigma1_512(e) + Ch(e, f, g) + K512[j] +
		     (W512[j&0x0f] += s1 + W512[(j+9)&0x0f] + s0);
		T2 = Sigma0_512(a) + Maj(a, b, c);
		h = g;
		g = f;
		f = e;
		e = d + T1;
		d = c;
		c = b;
		b = a;
		a = T1 + T2;

		j++;
	} while (j < 80);

	/* Compute the current intermediate hash value */
	context->state[0] += a;
	context->state[1] += b;
	context->state[2] += c;
	context->state[3] += d;
	context->state[4] += e;
	context->state[5] += f;
	context->state[6] += g;
	context->state[7] += h;

	/* Clean up */
	a = b = c = d = e = f = g = h = T1 = T2 = 0;
	/* Avoid compiler warnings */
	POST(a); POST(b); POST(c); POST(d); POST(e); POST(f);
	POST(g); POST(h); POST(T1); POST(T2);
}

#endif /* ISC_SHA2_UNROLL_TRANSFORM */

void isc_sha512_update(isc_sha512_t *context, const isc_uint8_t *data, size_t len) {
	unsigned int	freespace, usedspace;

	if (len == 0U) {
		/* Calling with no data is valid - we do nothing */
		return;
	}

	/* Sanity check: */
	REQUIRE(context != (isc_sha512_t *)0 && data != (isc_uint8_t*)0);

	usedspace = (unsigned int)((context->bitcount[0] >> 3) %
				   ISC_SHA512_BLOCK_LENGTH);
	if (usedspace > 0) {
		/* Calculate how much free space is available in the buffer */
		freespace = ISC_SHA512_BLOCK_LENGTH - usedspace;

		if (len >= freespace) {
			/* Fill the buffer completely and process it */
			memcpy(&context->buffer[usedspace], data, freespace);
			ADDINC128(context->bitcount, freespace << 3);
			len -= freespace;
			data += freespace;
			isc_sha512_transform(context,
					     (isc_uint64_t*)context->buffer);
		} else {
			/* The buffer is not yet full */
			memcpy(&context->buffer[usedspace], data, len);
			ADDINC128(context->bitcount, len << 3);
			/* Clean up: */
			usedspace = freespace = 0;
			/* Avoid compiler warnings: */
			POST(usedspace); POST(freespace);
			return;
		}
	}
	while (len >= ISC_SHA512_BLOCK_LENGTH) {
		/* Process as many complete blocks as we can */
		memcpy(context->buffer, data, ISC_SHA512_BLOCK_LENGTH);
		isc_sha512_transform(context, (isc_uint64_t*)context->buffer);
		ADDINC128(context->bitcount, ISC_SHA512_BLOCK_LENGTH << 3);
		len -= ISC_SHA512_BLOCK_LENGTH;
		data += ISC_SHA512_BLOCK_LENGTH;
	}
	if (len > 0U) {
		/* There's left-overs, so save 'em */
		memcpy(context->buffer, data, len);
		ADDINC128(context->bitcount, len << 3);
	}
	/* Clean up: */
	usedspace = freespace = 0;
	/* Avoid compiler warnings: */
	POST(usedspace); POST(freespace);
}

void isc_sha512_last(isc_sha512_t *context) {
	unsigned int	usedspace;

	usedspace = (unsigned int)((context->bitcount[0] >> 3) %
				    ISC_SHA512_BLOCK_LENGTH);
#if BYTE_ORDER == LITTLE_ENDIAN
	/* Convert FROM host byte order */
	REVERSE64(context->bitcount[0],context->bitcount[0]);
	REVERSE64(context->bitcount[1],context->bitcount[1]);
#endif
	if (usedspace > 0) {
		/* Begin padding with a 1 bit: */
		context->buffer[usedspace++] = 0x80;

		if (usedspace <= ISC_SHA512_SHORT_BLOCK_LENGTH) {
			/* Set-up for the last transform: */
			memset(&context->buffer[usedspace], 0,
			       ISC_SHA512_SHORT_BLOCK_LENGTH - usedspace);
		} else {
			if (usedspace < ISC_SHA512_BLOCK_LENGTH) {
				memset(&context->buffer[usedspace], 0,
				       ISC_SHA512_BLOCK_LENGTH - usedspace);
			}
			/* Do second-to-last transform: */
			isc_sha512_transform(context,
					    (isc_uint64_t*)context->buffer);

			/* And set-up for the last transform: */
			memset(context->buffer, 0, ISC_SHA512_BLOCK_LENGTH - 2);
		}
	} else {
		/* Prepare for final transform: */
		memset(context->buffer, 0, ISC_SHA512_SHORT_BLOCK_LENGTH);

		/* Begin padding with a 1 bit: */
		*context->buffer = 0x80;
	}
	/* Store the length of input data (in bits): */
	*(isc_uint64_t*)&context->buffer[ISC_SHA512_SHORT_BLOCK_LENGTH] = context->bitcount[1];
	*(isc_uint64_t*)&context->buffer[ISC_SHA512_SHORT_BLOCK_LENGTH+8] = context->bitcount[0];

	/* Final transform: */
	isc_sha512_transform(context, (isc_uint64_t*)context->buffer);
}

void isc_sha512_final(isc_uint8_t digest[], isc_sha512_t *context) {
	isc_uint64_t	*d = (isc_uint64_t*)digest;

	/* Sanity check: */
	REQUIRE(context != (isc_sha512_t *)0);

	/* If no digest buffer is passed, we don't bother doing this: */
	if (digest != (isc_uint8_t*)0) {
		isc_sha512_last(context);

		/* Save the hash data for output: */
#if BYTE_ORDER == LITTLE_ENDIAN
		{
			/* Convert TO host byte order */
			int	j;
			for (j = 0; j < 8; j++) {
				REVERSE64(context->state[j],context->state[j]);
				*d++ = context->state[j];
			}
		}
#else
		memcpy(d, context->state, ISC_SHA512_DIGESTLENGTH);
#endif
	}

	/* Zero out state data */
	memset(context, 0, sizeof(*context));
}


/*** SHA-384: *********************************************************/
void
isc_sha384_init(isc_sha384_t *context) {
	if (context == (isc_sha384_t *)0) {
		return;
	}
	memcpy(context->state, sha384_initial_hash_value,
	       ISC_SHA512_DIGESTLENGTH);
	memset(context->buffer, 0, ISC_SHA384_BLOCK_LENGTH);
	context->bitcount[0] = context->bitcount[1] = 0;
}

void
isc_sha384_invalidate(isc_sha384_t *context) {
	memset(context, 0, sizeof(isc_sha384_t));
}

void
isc_sha384_update(isc_sha384_t *context, const isc_uint8_t* data, size_t len) {
	isc_sha512_update((isc_sha512_t *)context, data, len);
}

void
isc_sha384_final(isc_uint8_t digest[], isc_sha384_t *context) {
	isc_uint64_t	*d = (isc_uint64_t*)digest;

	/* Sanity check: */
	REQUIRE(context != (isc_sha384_t *)0);

	/* If no digest buffer is passed, we don't bother doing this: */
	if (digest != (isc_uint8_t*)0) {
		isc_sha512_last((isc_sha512_t *)context);

		/* Save the hash data for output: */
#if BYTE_ORDER == LITTLE_ENDIAN
		{
			/* Convert TO host byte order */
			int	j;
			for (j = 0; j < 6; j++) {
				REVERSE64(context->state[j],context->state[j]);
				*d++ = context->state[j];
			}
		}
#else
		memcpy(d, context->state, ISC_SHA384_DIGESTLENGTH);
#endif
	}

	/* Zero out state data */
	memset(context, 0, sizeof(*context));
}
#endif /* !ISC_PLATFORM_OPENSSLHASH */

/*
 * Constant used by SHA256/384/512_End() functions for converting the
 * digest to a readable hexadecimal character string:
 */
static const char *sha2_hex_digits = "0123456789abcdef";

char *
isc_sha224_end(isc_sha224_t *context, char buffer[]) {
	isc_uint8_t	digest[ISC_SHA224_DIGESTLENGTH], *d = digest;
	unsigned int	i;

	/* Sanity check: */
	REQUIRE(context != (isc_sha224_t *)0);

	if (buffer != (char*)0) {
		isc_sha224_final(digest, context);

		for (i = 0; i < ISC_SHA224_DIGESTLENGTH; i++) {
			*buffer++ = sha2_hex_digits[(*d & 0xf0) >> 4];
			*buffer++ = sha2_hex_digits[*d & 0x0f];
			d++;
		}
		*buffer = (char)0;
	} else {
#ifdef ISC_PLATFORM_OPENSSLHASH
		EVP_MD_CTX_cleanup(context);
#else
		memset(context, 0, sizeof(*context));
#endif
	}
	memset(digest, 0, ISC_SHA224_DIGESTLENGTH);
	return buffer;
}

char *
isc_sha224_data(const isc_uint8_t *data, size_t len,
		char digest[ISC_SHA224_DIGESTSTRINGLENGTH])
{
	isc_sha224_t context;

	isc_sha224_init(&context);
	isc_sha224_update(&context, data, len);
	return (isc_sha224_end(&context, digest));
}

char *
isc_sha256_end(isc_sha256_t *context, char buffer[]) {
	isc_uint8_t	digest[ISC_SHA256_DIGESTLENGTH], *d = digest;
	unsigned int	i;

	/* Sanity check: */
	REQUIRE(context != (isc_sha256_t *)0);

	if (buffer != (char*)0) {
		isc_sha256_final(digest, context);

		for (i = 0; i < ISC_SHA256_DIGESTLENGTH; i++) {
			*buffer++ = sha2_hex_digits[(*d & 0xf0) >> 4];
			*buffer++ = sha2_hex_digits[*d & 0x0f];
			d++;
		}
		*buffer = (char)0;
	} else {
#ifdef ISC_PLATFORM_OPENSSLHASH
		EVP_MD_CTX_cleanup(context);
#else
		memset(context, 0, sizeof(*context));
#endif
	}
	memset(digest, 0, ISC_SHA256_DIGESTLENGTH);
	return buffer;
}

char *
isc_sha256_data(const isc_uint8_t* data, size_t len,
		char digest[ISC_SHA256_DIGESTSTRINGLENGTH])
{
	isc_sha256_t context;

	isc_sha256_init(&context);
	isc_sha256_update(&context, data, len);
	return (isc_sha256_end(&context, digest));
}

char *
isc_sha512_end(isc_sha512_t *context, char buffer[]) {
	isc_uint8_t	digest[ISC_SHA512_DIGESTLENGTH], *d = digest;
	unsigned int	i;

	/* Sanity check: */
	REQUIRE(context != (isc_sha512_t *)0);

	if (buffer != (char*)0) {
		isc_sha512_final(digest, context);

		for (i = 0; i < ISC_SHA512_DIGESTLENGTH; i++) {
			*buffer++ = sha2_hex_digits[(*d & 0xf0) >> 4];
			*buffer++ = sha2_hex_digits[*d & 0x0f];
			d++;
		}
		*buffer = (char)0;
	} else {
#ifdef ISC_PLATFORM_OPENSSLHASH
		EVP_MD_CTX_cleanup(context);
#else
		memset(context, 0, sizeof(*context));
#endif
	}
	memset(digest, 0, ISC_SHA512_DIGESTLENGTH);
	return buffer;
}

char *
isc_sha512_data(const isc_uint8_t *data, size_t len,
		char digest[ISC_SHA512_DIGESTSTRINGLENGTH])
{
	isc_sha512_t 	context;

	isc_sha512_init(&context);
	isc_sha512_update(&context, data, len);
	return (isc_sha512_end(&context, digest));
}

char *
isc_sha384_end(isc_sha384_t *context, char buffer[]) {
	isc_uint8_t	digest[ISC_SHA384_DIGESTLENGTH], *d = digest;
	unsigned int	i;

	/* Sanity check: */
	REQUIRE(context != (isc_sha384_t *)0);

	if (buffer != (char*)0) {
		isc_sha384_final(digest, context);

		for (i = 0; i < ISC_SHA384_DIGESTLENGTH; i++) {
			*buffer++ = sha2_hex_digits[(*d & 0xf0) >> 4];
			*buffer++ = sha2_hex_digits[*d & 0x0f];
			d++;
		}
		*buffer = (char)0;
	} else {
#ifdef ISC_PLATFORM_OPENSSLHASH
		EVP_MD_CTX_cleanup(context);
#else
		memset(context, 0, sizeof(*context));
#endif
	}
	memset(digest, 0, ISC_SHA384_DIGESTLENGTH);
	return buffer;
}

char *
isc_sha384_data(const isc_uint8_t *data, size_t len,
		char digest[ISC_SHA384_DIGESTSTRINGLENGTH])
{
	isc_sha384_t context;

	isc_sha384_init(&context);
	isc_sha384_update(&context, data, len);
	return (isc_sha384_end(&context, digest));
}
