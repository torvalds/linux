/*
 * Copyright (C) 2005-2007, 2009  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: hmacsha.h,v 1.9 2009/02/06 23:47:42 tbox Exp $ */

/*! \file isc/hmacsha.h
 * This is the header file for the HMAC-SHA1, HMAC-SHA224, HMAC-SHA256,
 * HMAC-SHA334 and HMAC-SHA512 hash algorithm described in RFC 2104.
 */

#ifndef ISC_HMACSHA_H
#define ISC_HMACSHA_H 1

#include <isc/lang.h>
#include <isc/platform.h>
#include <isc/sha1.h>
#include <isc/sha2.h>
#include <isc/types.h>

#define ISC_HMACSHA1_KEYLENGTH ISC_SHA1_BLOCK_LENGTH
#define ISC_HMACSHA224_KEYLENGTH ISC_SHA224_BLOCK_LENGTH
#define ISC_HMACSHA256_KEYLENGTH ISC_SHA256_BLOCK_LENGTH
#define ISC_HMACSHA384_KEYLENGTH ISC_SHA384_BLOCK_LENGTH
#define ISC_HMACSHA512_KEYLENGTH ISC_SHA512_BLOCK_LENGTH

#ifdef ISC_PLATFORM_OPENSSLHASH
#include <openssl/hmac.h>

typedef HMAC_CTX isc_hmacsha1_t;
typedef HMAC_CTX isc_hmacsha224_t;
typedef HMAC_CTX isc_hmacsha256_t;
typedef HMAC_CTX isc_hmacsha384_t;
typedef HMAC_CTX isc_hmacsha512_t;

#else

typedef struct {
	isc_sha1_t sha1ctx;
	unsigned char key[ISC_HMACSHA1_KEYLENGTH];
} isc_hmacsha1_t;

typedef struct {
	isc_sha224_t sha224ctx;
	unsigned char key[ISC_HMACSHA224_KEYLENGTH];
} isc_hmacsha224_t;

typedef struct {
	isc_sha256_t sha256ctx;
	unsigned char key[ISC_HMACSHA256_KEYLENGTH];
} isc_hmacsha256_t;

typedef struct {
	isc_sha384_t sha384ctx;
	unsigned char key[ISC_HMACSHA384_KEYLENGTH];
} isc_hmacsha384_t;

typedef struct {
	isc_sha512_t sha512ctx;
	unsigned char key[ISC_HMACSHA512_KEYLENGTH];
} isc_hmacsha512_t;
#endif

ISC_LANG_BEGINDECLS

void
isc_hmacsha1_init(isc_hmacsha1_t *ctx, const unsigned char *key,
		  unsigned int len);

void
isc_hmacsha1_invalidate(isc_hmacsha1_t *ctx);

void
isc_hmacsha1_update(isc_hmacsha1_t *ctx, const unsigned char *buf,
		    unsigned int len);

void
isc_hmacsha1_sign(isc_hmacsha1_t *ctx, unsigned char *digest, size_t len);

isc_boolean_t
isc_hmacsha1_verify(isc_hmacsha1_t *ctx, unsigned char *digest, size_t len);


void
isc_hmacsha224_init(isc_hmacsha224_t *ctx, const unsigned char *key,
		    unsigned int len);

void
isc_hmacsha224_invalidate(isc_hmacsha224_t *ctx);

void
isc_hmacsha224_update(isc_hmacsha224_t *ctx, const unsigned char *buf,
		      unsigned int len);

void
isc_hmacsha224_sign(isc_hmacsha224_t *ctx, unsigned char *digest, size_t len);

isc_boolean_t
isc_hmacsha224_verify(isc_hmacsha224_t *ctx, unsigned char *digest, size_t len);


void
isc_hmacsha256_init(isc_hmacsha256_t *ctx, const unsigned char *key,
		    unsigned int len);

void
isc_hmacsha256_invalidate(isc_hmacsha256_t *ctx);

void
isc_hmacsha256_update(isc_hmacsha256_t *ctx, const unsigned char *buf,
		      unsigned int len);

void
isc_hmacsha256_sign(isc_hmacsha256_t *ctx, unsigned char *digest, size_t len);

isc_boolean_t
isc_hmacsha256_verify(isc_hmacsha256_t *ctx, unsigned char *digest, size_t len);


void
isc_hmacsha384_init(isc_hmacsha384_t *ctx, const unsigned char *key,
		    unsigned int len);

void
isc_hmacsha384_invalidate(isc_hmacsha384_t *ctx);

void
isc_hmacsha384_update(isc_hmacsha384_t *ctx, const unsigned char *buf,
		      unsigned int len);

void
isc_hmacsha384_sign(isc_hmacsha384_t *ctx, unsigned char *digest, size_t len);

isc_boolean_t
isc_hmacsha384_verify(isc_hmacsha384_t *ctx, unsigned char *digest, size_t len);


void
isc_hmacsha512_init(isc_hmacsha512_t *ctx, const unsigned char *key,
		    unsigned int len);

void
isc_hmacsha512_invalidate(isc_hmacsha512_t *ctx);

void
isc_hmacsha512_update(isc_hmacsha512_t *ctx, const unsigned char *buf,
		      unsigned int len);

void
isc_hmacsha512_sign(isc_hmacsha512_t *ctx, unsigned char *digest, size_t len);

isc_boolean_t
isc_hmacsha512_verify(isc_hmacsha512_t *ctx, unsigned char *digest, size_t len);

ISC_LANG_ENDDECLS

#endif /* ISC_HMACSHA_H */
