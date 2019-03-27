/*-
* Copyright (c) 2003-2007 Tim Kientzle
* Copyright (c) 2011 Andres Mejia
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

#ifndef ARCHIVE_CRYPTO_PRIVATE_H_INCLUDED
#define ARCHIVE_CRYPTO_PRIVATE_H_INCLUDED

/*
 * Crypto support in various Operating Systems:
 *
 * NetBSD:
 * - MD5 and SHA1 in libc: without _ after algorithm name
 * - SHA2 in libc: with _ after algorithm name
 *
 * OpenBSD:
 * - MD5, SHA1 and SHA2 in libc: without _ after algorithm name
 * - OpenBSD 4.4 and earlier have SHA2 in libc with _ after algorithm name
 *
 * DragonFly and FreeBSD:
 * - MD5 libmd: without _ after algorithm name
 * - SHA1, SHA256 and SHA512 in libmd: with _ after algorithm name
 *
 * Mac OS X (10.4 and later):
 * - MD5, SHA1 and SHA2 in libSystem: with CC_ prefix and _ after algorithm name
 *
 * OpenSSL:
 * - MD5, SHA1 and SHA2 in libcrypto: with _ after algorithm name
 *
 * Windows:
 * - MD5, SHA1 and SHA2 in archive_crypto.c using Windows crypto API
 */

/* libc crypto headers */
#if defined(ARCHIVE_CRYPTO_MD5_LIBC)
#include <md5.h>
#endif
#if defined(ARCHIVE_CRYPTO_RMD160_LIBC)
#include <rmd160.h>
#endif
#if defined(ARCHIVE_CRYPTO_SHA1_LIBC)
#include <sha1.h>
#endif
#if defined(ARCHIVE_CRYPTO_SHA256_LIBC) ||\
  defined(ARCHIVE_CRYPTO_SHA256_LIBC2) ||\
  defined(ARCHIVE_CRYPTO_SHA256_LIBC3) ||\
  defined(ARCHIVE_CRYPTO_SHA384_LIBC) ||\
  defined(ARCHIVE_CRYPTO_SHA384_LIBC2) ||\
  defined(ARCHIVE_CRYPTO_SHA384_LIBC3) ||\
  defined(ARCHIVE_CRYPTO_SHA512_LIBC) ||\
  defined(ARCHIVE_CRYPTO_SHA512_LIBC2) ||\
  defined(ARCHIVE_CRYPTO_SHA512_LIBC3)
#include <sha2.h>
#endif

/* libmd crypto headers */
#if defined(ARCHIVE_CRYPTO_MD5_LIBMD) ||\
  defined(ARCHIVE_CRYPTO_RMD160_LIBMD) ||\
  defined(ARCHIVE_CRYPTO_SHA1_LIBMD) ||\
  defined(ARCHIVE_CRYPTO_SHA256_LIBMD) ||\
  defined(ARCHIVE_CRYPTO_SHA512_LIBMD)
#define	ARCHIVE_CRYPTO_LIBMD 1
#endif

#if defined(ARCHIVE_CRYPTO_MD5_LIBMD)
#include <md5.h>
#endif
#if defined(ARCHIVE_CRYPTO_RMD160_LIBMD)
#include <ripemd.h>
#endif
#if defined(ARCHIVE_CRYPTO_SHA1_LIBMD)
#include <sha.h>
#endif
#if defined(ARCHIVE_CRYPTO_SHA256_LIBMD)
#include <sha256.h>
#endif
#if defined(ARCHIVE_CRYPTO_SHA512_LIBMD)
#include <sha512.h>
#endif

/* libSystem crypto headers */
#if defined(ARCHIVE_CRYPTO_MD5_LIBSYSTEM) ||\
  defined(ARCHIVE_CRYPTO_SHA1_LIBSYSTEM) ||\
  defined(ARCHIVE_CRYPTO_SHA256_LIBSYSTEM) ||\
  defined(ARCHIVE_CRYPTO_SHA384_LIBSYSTEM) ||\
  defined(ARCHIVE_CRYPTO_SHA512_LIBSYSTEM)
#include <CommonCrypto/CommonDigest.h>
#endif

/* Nettle crypto headers */
#if defined(ARCHIVE_CRYPTO_MD5_NETTLE)
#include <nettle/md5.h>
#endif
#if defined(ARCHIVE_CRYPTO_RMD160_NETTLE)
#include <nettle/ripemd160.h>
#endif
#if defined(ARCHIVE_CRYPTO_SHA1_NETTLE) ||\
  defined(ARCHIVE_CRYPTO_SHA256_NETTLE) ||\
  defined(ARCHIVE_CRYPTO_SHA384_NETTLE) ||\
  defined(ARCHIVE_CRYPTO_SHA512_NETTLE)
#include <nettle/sha.h>
#endif

/* OpenSSL crypto headers */
#if defined(ARCHIVE_CRYPTO_MD5_OPENSSL) ||\
  defined(ARCHIVE_CRYPTO_RMD160_OPENSSL) ||\
  defined(ARCHIVE_CRYPTO_SHA1_OPENSSL) ||\
  defined(ARCHIVE_CRYPTO_SHA256_OPENSSL) ||\
  defined(ARCHIVE_CRYPTO_SHA384_OPENSSL) ||\
  defined(ARCHIVE_CRYPTO_SHA512_OPENSSL)
#define	ARCHIVE_CRYPTO_OPENSSL 1
#include "archive_openssl_evp_private.h"
#endif

/* Windows crypto headers */
#if defined(ARCHIVE_CRYPTO_MD5_WIN)    ||\
  defined(ARCHIVE_CRYPTO_SHA1_WIN)   ||\
  defined(ARCHIVE_CRYPTO_SHA256_WIN) ||\
  defined(ARCHIVE_CRYPTO_SHA384_WIN) ||\
  defined(ARCHIVE_CRYPTO_SHA512_WIN)
#include <windows.h>
#include <wincrypt.h>
typedef struct {
  int   valid;
  HCRYPTPROV  cryptProv;
  HCRYPTHASH  hash;
} Digest_CTX;
#endif

/* typedefs */
#if defined(ARCHIVE_CRYPTO_MD5_LIBC)
typedef MD5_CTX archive_md5_ctx;
#elif defined(ARCHIVE_CRYPTO_MD5_LIBMD)
typedef MD5_CTX archive_md5_ctx;
#elif defined(ARCHIVE_CRYPTO_MD5_LIBSYSTEM)
typedef CC_MD5_CTX archive_md5_ctx;
#elif defined(ARCHIVE_CRYPTO_MD5_NETTLE)
typedef struct md5_ctx archive_md5_ctx;
#elif defined(ARCHIVE_CRYPTO_MD5_OPENSSL)
typedef EVP_MD_CTX *archive_md5_ctx;
#elif defined(ARCHIVE_CRYPTO_MD5_WIN)
typedef Digest_CTX archive_md5_ctx;
#else
typedef unsigned char archive_md5_ctx;
#endif

#if defined(ARCHIVE_CRYPTO_RMD160_LIBC)
typedef RMD160_CTX archive_rmd160_ctx;
#elif defined(ARCHIVE_CRYPTO_RMD160_LIBMD)
typedef RIPEMD160_CTX archive_rmd160_ctx;
#elif defined(ARCHIVE_CRYPTO_RMD160_NETTLE)
typedef struct ripemd160_ctx archive_rmd160_ctx;
#elif defined(ARCHIVE_CRYPTO_RMD160_OPENSSL)
typedef EVP_MD_CTX *archive_rmd160_ctx;
#else
typedef unsigned char archive_rmd160_ctx;
#endif

#if defined(ARCHIVE_CRYPTO_SHA1_LIBC)
typedef SHA1_CTX archive_sha1_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA1_LIBMD)
typedef SHA1_CTX archive_sha1_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA1_LIBSYSTEM)
typedef CC_SHA1_CTX archive_sha1_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA1_NETTLE)
typedef struct sha1_ctx archive_sha1_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA1_OPENSSL)
typedef EVP_MD_CTX *archive_sha1_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA1_WIN)
typedef Digest_CTX archive_sha1_ctx;
#else
typedef unsigned char archive_sha1_ctx;
#endif

#if defined(ARCHIVE_CRYPTO_SHA256_LIBC)
typedef SHA256_CTX archive_sha256_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA256_LIBC2)
typedef SHA256_CTX archive_sha256_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA256_LIBC3)
typedef SHA2_CTX archive_sha256_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA256_LIBMD)
typedef SHA256_CTX archive_sha256_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA256_LIBSYSTEM)
typedef CC_SHA256_CTX archive_sha256_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA256_NETTLE)
typedef struct sha256_ctx archive_sha256_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA256_OPENSSL)
typedef EVP_MD_CTX *archive_sha256_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA256_WIN)
typedef Digest_CTX archive_sha256_ctx;
#else
typedef unsigned char archive_sha256_ctx;
#endif

#if defined(ARCHIVE_CRYPTO_SHA384_LIBC)
typedef SHA384_CTX archive_sha384_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA384_LIBC2)
typedef SHA384_CTX archive_sha384_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA384_LIBC3)
typedef SHA2_CTX archive_sha384_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA384_LIBSYSTEM)
typedef CC_SHA512_CTX archive_sha384_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA384_NETTLE)
typedef struct sha384_ctx archive_sha384_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA384_OPENSSL)
typedef EVP_MD_CTX *archive_sha384_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA384_WIN)
typedef Digest_CTX archive_sha384_ctx;
#else
typedef unsigned char archive_sha384_ctx;
#endif

#if defined(ARCHIVE_CRYPTO_SHA512_LIBC)
typedef SHA512_CTX archive_sha512_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA512_LIBC2)
typedef SHA512_CTX archive_sha512_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA512_LIBC3)
typedef SHA2_CTX archive_sha512_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA512_LIBMD)
typedef SHA512_CTX archive_sha512_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA512_LIBSYSTEM)
typedef CC_SHA512_CTX archive_sha512_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA512_NETTLE)
typedef struct sha512_ctx archive_sha512_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA512_OPENSSL)
typedef EVP_MD_CTX *archive_sha512_ctx;
#elif defined(ARCHIVE_CRYPTO_SHA512_WIN)
typedef Digest_CTX archive_sha512_ctx;
#else
typedef unsigned char archive_sha512_ctx;
#endif

/* defines */
#if defined(ARCHIVE_CRYPTO_MD5_LIBC) ||\
  defined(ARCHIVE_CRYPTO_MD5_LIBMD) ||	\
  defined(ARCHIVE_CRYPTO_MD5_LIBSYSTEM) ||\
  defined(ARCHIVE_CRYPTO_MD5_NETTLE) ||\
  defined(ARCHIVE_CRYPTO_MD5_OPENSSL) ||\
  defined(ARCHIVE_CRYPTO_MD5_WIN)
#define ARCHIVE_HAS_MD5
#endif
#define archive_md5_init(ctx)\
  __archive_digest.md5init(ctx)
#define archive_md5_final(ctx, md)\
  __archive_digest.md5final(ctx, md)
#define archive_md5_update(ctx, buf, n)\
  __archive_digest.md5update(ctx, buf, n)

#if defined(ARCHIVE_CRYPTO_RMD160_LIBC) ||\
  defined(ARCHIVE_CRYPTO_RMD160_NETTLE) ||\
  defined(ARCHIVE_CRYPTO_RMD160_OPENSSL)
#define ARCHIVE_HAS_RMD160
#endif
#define archive_rmd160_init(ctx)\
  __archive_digest.rmd160init(ctx)
#define archive_rmd160_final(ctx, md)\
  __archive_digest.rmd160final(ctx, md)
#define archive_rmd160_update(ctx, buf, n)\
  __archive_digest.rmd160update(ctx, buf, n)

#if defined(ARCHIVE_CRYPTO_SHA1_LIBC) ||\
  defined(ARCHIVE_CRYPTO_SHA1_LIBMD) ||	\
  defined(ARCHIVE_CRYPTO_SHA1_LIBSYSTEM) ||\
  defined(ARCHIVE_CRYPTO_SHA1_NETTLE) ||\
  defined(ARCHIVE_CRYPTO_SHA1_OPENSSL) ||\
  defined(ARCHIVE_CRYPTO_SHA1_WIN)
#define ARCHIVE_HAS_SHA1
#endif
#define archive_sha1_init(ctx)\
  __archive_digest.sha1init(ctx)
#define archive_sha1_final(ctx, md)\
  __archive_digest.sha1final(ctx, md)
#define archive_sha1_update(ctx, buf, n)\
  __archive_digest.sha1update(ctx, buf, n)

#if defined(ARCHIVE_CRYPTO_SHA256_LIBC) ||\
  defined(ARCHIVE_CRYPTO_SHA256_LIBC2) ||\
  defined(ARCHIVE_CRYPTO_SHA256_LIBC3) ||\
  defined(ARCHIVE_CRYPTO_SHA256_LIBMD) ||\
  defined(ARCHIVE_CRYPTO_SHA256_LIBSYSTEM) ||\
  defined(ARCHIVE_CRYPTO_SHA256_NETTLE) ||\
  defined(ARCHIVE_CRYPTO_SHA256_OPENSSL) ||\
  defined(ARCHIVE_CRYPTO_SHA256_WIN)
#define ARCHIVE_HAS_SHA256
#endif
#define archive_sha256_init(ctx)\
  __archive_digest.sha256init(ctx)
#define archive_sha256_final(ctx, md)\
  __archive_digest.sha256final(ctx, md)
#define archive_sha256_update(ctx, buf, n)\
  __archive_digest.sha256update(ctx, buf, n)

#if defined(ARCHIVE_CRYPTO_SHA384_LIBC) ||\
  defined(ARCHIVE_CRYPTO_SHA384_LIBC2) ||\
  defined(ARCHIVE_CRYPTO_SHA384_LIBC3) ||\
  defined(ARCHIVE_CRYPTO_SHA384_LIBSYSTEM) ||\
  defined(ARCHIVE_CRYPTO_SHA384_NETTLE) ||\
  defined(ARCHIVE_CRYPTO_SHA384_OPENSSL) ||\
  defined(ARCHIVE_CRYPTO_SHA384_WIN)
#define ARCHIVE_HAS_SHA384
#endif
#define archive_sha384_init(ctx)\
  __archive_digest.sha384init(ctx)
#define archive_sha384_final(ctx, md)\
  __archive_digest.sha384final(ctx, md)
#define archive_sha384_update(ctx, buf, n)\
  __archive_digest.sha384update(ctx, buf, n)

#if defined(ARCHIVE_CRYPTO_SHA512_LIBC) ||\
  defined(ARCHIVE_CRYPTO_SHA512_LIBC2) ||\
  defined(ARCHIVE_CRYPTO_SHA512_LIBC3) ||\
  defined(ARCHIVE_CRYPTO_SHA512_LIBMD) ||\
  defined(ARCHIVE_CRYPTO_SHA512_LIBSYSTEM) ||\
  defined(ARCHIVE_CRYPTO_SHA512_NETTLE) ||\
  defined(ARCHIVE_CRYPTO_SHA512_OPENSSL) ||\
  defined(ARCHIVE_CRYPTO_SHA512_WIN)
#define ARCHIVE_HAS_SHA512
#endif
#define archive_sha512_init(ctx)\
  __archive_digest.sha512init(ctx)
#define archive_sha512_final(ctx, md)\
  __archive_digest.sha512final(ctx, md)
#define archive_sha512_update(ctx, buf, n)\
  __archive_digest.sha512update(ctx, buf, n)

/* Minimal interface to digest functionality for internal use in libarchive */
struct archive_digest
{
  /* Message Digest */
  int (*md5init)(archive_md5_ctx *ctx);
  int (*md5update)(archive_md5_ctx *, const void *, size_t);
  int (*md5final)(archive_md5_ctx *, void *);
  int (*rmd160init)(archive_rmd160_ctx *);
  int (*rmd160update)(archive_rmd160_ctx *, const void *, size_t);
  int (*rmd160final)(archive_rmd160_ctx *, void *);
  int (*sha1init)(archive_sha1_ctx *);
  int (*sha1update)(archive_sha1_ctx *, const void *, size_t);
  int (*sha1final)(archive_sha1_ctx *, void *);
  int (*sha256init)(archive_sha256_ctx *);
  int (*sha256update)(archive_sha256_ctx *, const void *, size_t);
  int (*sha256final)(archive_sha256_ctx *, void *);
  int (*sha384init)(archive_sha384_ctx *);
  int (*sha384update)(archive_sha384_ctx *, const void *, size_t);
  int (*sha384final)(archive_sha384_ctx *, void *);
  int (*sha512init)(archive_sha512_ctx *);
  int (*sha512update)(archive_sha512_ctx *, const void *, size_t);
  int (*sha512final)(archive_sha512_ctx *, void *);
};

extern const struct archive_digest __archive_digest;

#endif
