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

#ifndef ARCHIVE_CRYPTOR_PRIVATE_H_INCLUDED
#define ARCHIVE_CRYPTOR_PRIVATE_H_INCLUDED

/*
 * On systems that do not support any recognized crypto libraries,
 * the archive_cryptor.c file will normally define no usable symbols.
 *
 * But some compilers and linkers choke on empty object files, so
 * define a public symbol that will always exist.  This could
 * be removed someday if this file gains another always-present
 * symbol definition.
 */
int __libarchive_cryptor_build_hack(void);

#ifdef __APPLE__
# include <AvailabilityMacros.h>
# if MAC_OS_X_VERSION_MAX_ALLOWED >= 1080
#  define ARCHIVE_CRYPTOR_USE_Apple_CommonCrypto
# endif
#endif

#ifdef ARCHIVE_CRYPTOR_USE_Apple_CommonCrypto
#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonKeyDerivation.h>
#define AES_BLOCK_SIZE	16
#define AES_MAX_KEY_SIZE kCCKeySizeAES256

typedef struct {
	CCCryptorRef	ctx;
	uint8_t		key[AES_MAX_KEY_SIZE];
	unsigned	key_len;
	uint8_t		nonce[AES_BLOCK_SIZE];
	uint8_t		encr_buf[AES_BLOCK_SIZE];
	unsigned	encr_pos;
} archive_crypto_ctx;

#elif defined(_WIN32) && !defined(__CYGWIN__) && defined(HAVE_BCRYPT_H)
#include <bcrypt.h>

/* Common in other bcrypt implementations, but missing from VS2008. */
#ifndef BCRYPT_SUCCESS
#define BCRYPT_SUCCESS(r) ((NTSTATUS)(r) == STATUS_SUCCESS)
#endif

#define AES_MAX_KEY_SIZE 32
#define AES_BLOCK_SIZE 16
typedef struct {
	BCRYPT_ALG_HANDLE hAlg;
	BCRYPT_KEY_HANDLE hKey;
	PBYTE		keyObj;
	DWORD		keyObj_len;
	uint8_t		nonce[AES_BLOCK_SIZE];
	uint8_t		encr_buf[AES_BLOCK_SIZE];
	unsigned	encr_pos;
} archive_crypto_ctx;

#elif defined(HAVE_LIBNETTLE) && defined(HAVE_NETTLE_AES_H)
#if defined(HAVE_NETTLE_PBKDF2_H)
#include <nettle/pbkdf2.h>
#endif
#include <nettle/aes.h>

typedef struct {
	struct aes_ctx	ctx;
	uint8_t		key[AES_MAX_KEY_SIZE];
	unsigned	key_len;
	uint8_t		nonce[AES_BLOCK_SIZE];
	uint8_t		encr_buf[AES_BLOCK_SIZE];
	unsigned	encr_pos;
} archive_crypto_ctx;

#elif defined(HAVE_LIBCRYPTO)
#include "archive_openssl_evp_private.h"
#define AES_BLOCK_SIZE	16
#define AES_MAX_KEY_SIZE 32

typedef struct {
	EVP_CIPHER_CTX	*ctx;
	const EVP_CIPHER *type;
	uint8_t		key[AES_MAX_KEY_SIZE];
	unsigned	key_len;
	uint8_t		nonce[AES_BLOCK_SIZE];
	uint8_t		encr_buf[AES_BLOCK_SIZE];
	unsigned	encr_pos;
} archive_crypto_ctx;

#else

#define AES_BLOCK_SIZE	16
#define AES_MAX_KEY_SIZE 32
typedef int archive_crypto_ctx;

#endif

/* defines */
#define archive_pbkdf2_sha1(pw, pw_len, salt, salt_len, rounds, dk, dk_len)\
  __archive_cryptor.pbkdf2sha1(pw, pw_len, salt, salt_len, rounds, dk, dk_len)

#define archive_decrypto_aes_ctr_init(ctx, key, key_len) \
  __archive_cryptor.decrypto_aes_ctr_init(ctx, key, key_len)
#define archive_decrypto_aes_ctr_update(ctx, in, in_len, out, out_len) \
  __archive_cryptor.decrypto_aes_ctr_update(ctx, in, in_len, out, out_len)
#define archive_decrypto_aes_ctr_release(ctx) \
  __archive_cryptor.decrypto_aes_ctr_release(ctx)

#define archive_encrypto_aes_ctr_init(ctx, key, key_len) \
  __archive_cryptor.encrypto_aes_ctr_init(ctx, key, key_len)
#define archive_encrypto_aes_ctr_update(ctx, in, in_len, out, out_len) \
  __archive_cryptor.encrypto_aes_ctr_update(ctx, in, in_len, out, out_len)
#define archive_encrypto_aes_ctr_release(ctx) \
  __archive_cryptor.encrypto_aes_ctr_release(ctx)

/* Minimal interface to cryptographic functionality for internal use in
 * libarchive */
struct archive_cryptor
{
  /* PKCS5 PBKDF2 HMAC-SHA1 */
  int (*pbkdf2sha1)(const char *pw, size_t pw_len, const uint8_t *salt,
    size_t salt_len, unsigned rounds, uint8_t *derived_key,
    size_t derived_key_len);
  /* AES CTR mode(little endian version) */
  int (*decrypto_aes_ctr_init)(archive_crypto_ctx *, const uint8_t *, size_t);
  int (*decrypto_aes_ctr_update)(archive_crypto_ctx *, const uint8_t *,
    size_t, uint8_t *, size_t *);
  int (*decrypto_aes_ctr_release)(archive_crypto_ctx *);
  int (*encrypto_aes_ctr_init)(archive_crypto_ctx *, const uint8_t *, size_t);
  int (*encrypto_aes_ctr_update)(archive_crypto_ctx *, const uint8_t *,
    size_t, uint8_t *, size_t *);
  int (*encrypto_aes_ctr_release)(archive_crypto_ctx *);
};

extern const struct archive_cryptor __archive_cryptor;

#endif
