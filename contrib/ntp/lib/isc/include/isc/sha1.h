/*
 * Copyright (C) 2004-2007, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000, 2001  Internet Software Consortium.
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

#ifndef ISC_SHA1_H
#define ISC_SHA1_H 1

/* $Id: sha1.h,v 1.19 2009/02/06 23:47:42 tbox Exp $ */

/*	$NetBSD: sha1.h,v 1.2 1998/05/29 22:55:44 thorpej Exp $	*/

/*! \file isc/sha1.h
 * \brief SHA-1 in C
 * \author By Steve Reid <steve@edmweb.com>
 * \note 100% Public Domain
 */

#include <isc/lang.h>
#include <isc/platform.h>
#include <isc/types.h>

#define ISC_SHA1_DIGESTLENGTH 20U
#define ISC_SHA1_BLOCK_LENGTH 64U

#ifdef ISC_PLATFORM_OPENSSLHASH
#include <openssl/evp.h>

typedef EVP_MD_CTX isc_sha1_t;

#else

typedef struct {
	isc_uint32_t state[5];
	isc_uint32_t count[2];
	unsigned char buffer[ISC_SHA1_BLOCK_LENGTH];
} isc_sha1_t;
#endif

ISC_LANG_BEGINDECLS

void
isc_sha1_init(isc_sha1_t *ctx);

void
isc_sha1_invalidate(isc_sha1_t *ctx);

void
isc_sha1_update(isc_sha1_t *ctx, const unsigned char *data, unsigned int len);

void
isc_sha1_final(isc_sha1_t *ctx, unsigned char *digest);

ISC_LANG_ENDDECLS

#endif /* ISC_SHA1_H */
