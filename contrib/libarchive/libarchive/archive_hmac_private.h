/*-
* Copyright (c) 2014 Michihiro NAKAJIMA
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
* IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
* NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
* THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
* THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __LIBARCHIVE_BUILD
#error This header is only to be used internally to libarchive.
#endif

#ifndef ARCHIVE_HMAC_PRIVATE_H_INCLUDED
#define ARCHIVE_HMAC_PRIVATE_H_INCLUDED

/*
 * On systems that do not support any recognized crypto libraries,
 * the archive_hmac.c file is expected to define no usable symbols.
 *
 * But some compilers and linkers choke on empty object files, so
 * define a public symbol that will always exist.  This could
 * be removed someday if this file gains another always-present
 * symbol definition.
 */
int __libarchive_hmac_build_hack(void);

#ifdef __APPLE__
# include <AvailabilityMacros.h>
# if MAC_OS_X_VERSION_MAX_ALLOWED >= 1060
#  define ARCHIVE_HMAC_USE_Apple_CommonCrypto
# endif
#endif

#ifdef ARCHIVE_HMAC_USE_Apple_CommonCrypto
#include <CommonCrypto/CommonHMAC.h>

typedef	CCHmacContext archive_hmac_sha1_ctx;

#elif defined(_WIN32) && !defined(__CYGWIN__) && defined(HAVE_BCRYPT_H)
#include <bcrypt.h>

typedef struct {
	BCRYPT_ALG_HANDLE	hAlg;
	BCRYPT_HASH_HANDLE	hHash;
	DWORD				hash_len;
	PBYTE				hash;

} archive_hmac_sha1_ctx;

#elif defined(HAVE_LIBNETTLE) && defined(HAVE_NETTLE_HMAC_H)
#include <nettle/hmac.h>

typedef	struct hmac_sha1_ctx archive_hmac_sha1_ctx;

#elif defined(HAVE_LIBCRYPTO)
#include "archive_openssl_hmac_private.h"

typedef	HMAC_CTX* archive_hmac_sha1_ctx;

#else

typedef int archive_hmac_sha1_ctx;

#endif


/* HMAC */
#define archive_hmac_sha1_init(ctx, key, key_len)\
	__archive_hmac.__hmac_sha1_init(ctx, key, key_len)
#define archive_hmac_sha1_update(ctx, data, data_len)\
	__archive_hmac.__hmac_sha1_update(ctx, data, data_len)
#define archive_hmac_sha1_final(ctx, out, out_len)\
  	__archive_hmac.__hmac_sha1_final(ctx, out, out_len)
#define archive_hmac_sha1_cleanup(ctx)\
	__archive_hmac.__hmac_sha1_cleanup(ctx)


struct archive_hmac {
	/* HMAC */
	int (*__hmac_sha1_init)(archive_hmac_sha1_ctx *, const uint8_t *,
		size_t);
	void (*__hmac_sha1_update)(archive_hmac_sha1_ctx *, const uint8_t *,
		size_t);
	void (*__hmac_sha1_final)(archive_hmac_sha1_ctx *, uint8_t *, size_t *);
	void (*__hmac_sha1_cleanup)(archive_hmac_sha1_ctx *);
};

extern const struct archive_hmac __archive_hmac;
#endif /* ARCHIVE_HMAC_PRIVATE_H_INCLUDED */
