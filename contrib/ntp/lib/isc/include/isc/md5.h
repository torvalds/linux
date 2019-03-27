/*
 * Copyright (C) 2004-2007, 2009, 2010  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: md5.h,v 1.20 2010/01/07 23:48:54 tbox Exp $ */

/*! \file isc/md5.h
 * \brief This is the header file for the MD5 message-digest algorithm.
 *
 * The algorithm is due to Ron Rivest.  This code was
 * written by Colin Plumb in 1993, no copyright is claimed.
 * This code is in the public domain; do with it what you wish.
 *
 * Equivalent code is available from RSA Data Security, Inc.
 * This code has been tested against that, and is equivalent,
 * except that you don't need to include two pages of legalese
 * with every copy.
 *
 * To compute the message digest of a chunk of bytes, declare an
 * MD5Context structure, pass it to MD5Init, call MD5Update as
 * needed on buffers full of bytes, and then call MD5Final, which
 * will fill a supplied 16-byte array with the digest.
 *
 * Changed so as no longer to depend on Colin Plumb's `usual.h'
 * header definitions; now uses stuff from dpkg's config.h
 *  - Ian Jackson <ijackson@nyx.cs.du.edu>.
 * Still in the public domain.
 */

#ifndef ISC_MD5_H
#define ISC_MD5_H 1

#include <isc/lang.h>
#include <isc/platform.h>
#include <isc/types.h>

#define ISC_MD5_DIGESTLENGTH 16U
#define ISC_MD5_BLOCK_LENGTH 64U

#ifdef ISC_PLATFORM_OPENSSLHASH
#include <openssl/evp.h>

typedef EVP_MD_CTX isc_md5_t;

#else

typedef struct {
	isc_uint32_t buf[4];
	isc_uint32_t bytes[2];
	isc_uint32_t in[16];
} isc_md5_t;
#endif

ISC_LANG_BEGINDECLS

void
isc_md5_init(isc_md5_t *ctx);

void
isc_md5_invalidate(isc_md5_t *ctx);

void
isc_md5_update(isc_md5_t *ctx, const unsigned char *buf, unsigned int len);

void
isc_md5_final(isc_md5_t *ctx, unsigned char *digest);

ISC_LANG_ENDDECLS

#endif /* ISC_MD5_H */
