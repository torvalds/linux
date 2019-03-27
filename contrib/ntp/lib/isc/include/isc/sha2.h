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

/* $Id: sha2.h,v 1.12 2009/10/22 02:21:31 each Exp $ */

/*	$FreeBSD:  258945 2013-12-04 21:33:17Z roberto $	*/
/*	$KAME: sha2.h,v 1.3 2001/03/12 08:27:48 itojun Exp $	*/

/*
 * sha2.h
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

#ifndef ISC_SHA2_H
#define ISC_SHA2_H

#include <isc/lang.h>
#include <isc/platform.h>
#include <isc/types.h>

/*** SHA-224/256/384/512 Various Length Definitions ***********************/

#define ISC_SHA224_BLOCK_LENGTH		64U
#define ISC_SHA224_DIGESTLENGTH	28U
#define ISC_SHA224_DIGESTSTRINGLENGTH	(ISC_SHA224_DIGESTLENGTH * 2 + 1)
#define ISC_SHA256_BLOCK_LENGTH		64U
#define ISC_SHA256_DIGESTLENGTH	32U
#define ISC_SHA256_DIGESTSTRINGLENGTH	(ISC_SHA256_DIGESTLENGTH * 2 + 1)
#define ISC_SHA384_BLOCK_LENGTH		128
#define ISC_SHA384_DIGESTLENGTH	48U
#define ISC_SHA384_DIGESTSTRINGLENGTH	(ISC_SHA384_DIGESTLENGTH * 2 + 1)
#define ISC_SHA512_BLOCK_LENGTH		128U
#define ISC_SHA512_DIGESTLENGTH	64U
#define ISC_SHA512_DIGESTSTRINGLENGTH	(ISC_SHA512_DIGESTLENGTH * 2 + 1)

/*** SHA-256/384/512 Context Structures *******************************/

#ifdef ISC_PLATFORM_OPENSSLHASH
#include <openssl/evp.h>

typedef EVP_MD_CTX isc_sha256_t;
typedef EVP_MD_CTX isc_sha512_t;

#else

/*
 * Keep buffer immediately after bitcount to preserve alignment.
 */
typedef struct {
	isc_uint32_t	state[8];
	isc_uint64_t	bitcount;
	isc_uint8_t	buffer[ISC_SHA256_BLOCK_LENGTH];
} isc_sha256_t;

/*
 * Keep buffer immediately after bitcount to preserve alignment.
 */
typedef struct {
	isc_uint64_t	state[8];
	isc_uint64_t	bitcount[2];
	isc_uint8_t	buffer[ISC_SHA512_BLOCK_LENGTH];
} isc_sha512_t;
#endif

typedef isc_sha256_t isc_sha224_t;
typedef isc_sha512_t isc_sha384_t;

ISC_LANG_BEGINDECLS

/*** SHA-224/256/384/512 Function Prototypes ******************************/

void isc_sha224_init (isc_sha224_t *);
void isc_sha224_invalidate (isc_sha224_t *);
void isc_sha224_update (isc_sha224_t *, const isc_uint8_t *, size_t);
void isc_sha224_final (isc_uint8_t[ISC_SHA224_DIGESTLENGTH], isc_sha224_t *);
char *isc_sha224_end (isc_sha224_t *, char[ISC_SHA224_DIGESTSTRINGLENGTH]);
char *isc_sha224_data (const isc_uint8_t *, size_t, char[ISC_SHA224_DIGESTSTRINGLENGTH]);

void isc_sha256_init (isc_sha256_t *);
void isc_sha256_invalidate (isc_sha256_t *);
void isc_sha256_update (isc_sha256_t *, const isc_uint8_t *, size_t);
void isc_sha256_final (isc_uint8_t[ISC_SHA256_DIGESTLENGTH], isc_sha256_t *);
char *isc_sha256_end (isc_sha256_t *, char[ISC_SHA256_DIGESTSTRINGLENGTH]);
char *isc_sha256_data (const isc_uint8_t *, size_t, char[ISC_SHA256_DIGESTSTRINGLENGTH]);

void isc_sha384_init (isc_sha384_t *);
void isc_sha384_invalidate (isc_sha384_t *);
void isc_sha384_update (isc_sha384_t *, const isc_uint8_t *, size_t);
void isc_sha384_final (isc_uint8_t[ISC_SHA384_DIGESTLENGTH], isc_sha384_t *);
char *isc_sha384_end (isc_sha384_t *, char[ISC_SHA384_DIGESTSTRINGLENGTH]);
char *isc_sha384_data (const isc_uint8_t *, size_t, char[ISC_SHA384_DIGESTSTRINGLENGTH]);

void isc_sha512_init (isc_sha512_t *);
void isc_sha512_invalidate (isc_sha512_t *);
void isc_sha512_update (isc_sha512_t *, const isc_uint8_t *, size_t);
void isc_sha512_final (isc_uint8_t[ISC_SHA512_DIGESTLENGTH], isc_sha512_t *);
char *isc_sha512_end (isc_sha512_t *, char[ISC_SHA512_DIGESTSTRINGLENGTH]);
char *isc_sha512_data (const isc_uint8_t *, size_t, char[ISC_SHA512_DIGESTSTRINGLENGTH]);

ISC_LANG_ENDDECLS

#endif /* ISC_SHA2_H */
