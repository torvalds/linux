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

#include "archive_platform.h"

#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include "archive.h"
#include "archive_cryptor_private.h"

/*
 * On systems that do not support any recognized crypto libraries,
 * this file will normally define no usable symbols.
 *
 * But some compilers and linkers choke on empty object files, so
 * define a public symbol that will always exist.  This could
 * be removed someday if this file gains another always-present
 * symbol definition.
 */
int __libarchive_cryptor_build_hack(void) {
	return 0;
}

#ifdef ARCHIVE_CRYPTOR_USE_Apple_CommonCrypto

static int
pbkdf2_sha1(const char *pw, size_t pw_len, const uint8_t *salt,
    size_t salt_len, unsigned rounds, uint8_t *derived_key,
    size_t derived_key_len)
{
	CCKeyDerivationPBKDF(kCCPBKDF2, (const char *)pw,
	    pw_len, salt, salt_len, kCCPRFHmacAlgSHA1, rounds,
	    derived_key, derived_key_len);
	return 0;
}

#elif defined(_WIN32) && !defined(__CYGWIN__) && defined(HAVE_BCRYPT_H)
#ifdef _MSC_VER
#pragma comment(lib, "Bcrypt.lib")
#endif

static int
pbkdf2_sha1(const char *pw, size_t pw_len, const uint8_t *salt,
	size_t salt_len, unsigned rounds, uint8_t *derived_key,
	size_t derived_key_len)
{
	NTSTATUS status;
	BCRYPT_ALG_HANDLE hAlg;

	status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA1_ALGORITHM,
		MS_PRIMITIVE_PROVIDER, BCRYPT_ALG_HANDLE_HMAC_FLAG);
	if (!BCRYPT_SUCCESS(status))
		return -1;

	status = BCryptDeriveKeyPBKDF2(hAlg,
		(PUCHAR)(uintptr_t)pw, (ULONG)pw_len,
		(PUCHAR)(uintptr_t)salt, (ULONG)salt_len, rounds,
		(PUCHAR)derived_key, (ULONG)derived_key_len, 0);

	BCryptCloseAlgorithmProvider(hAlg, 0);

	return (BCRYPT_SUCCESS(status)) ? 0: -1;
}

#elif defined(HAVE_LIBNETTLE) && defined(HAVE_NETTLE_PBKDF2_H)

static int
pbkdf2_sha1(const char *pw, size_t pw_len, const uint8_t *salt,
    size_t salt_len, unsigned rounds, uint8_t *derived_key,
    size_t derived_key_len) {
	pbkdf2_hmac_sha1((unsigned)pw_len, (const uint8_t *)pw, rounds,
	    salt_len, salt, derived_key_len, derived_key);
	return 0;
}

#elif defined(HAVE_LIBCRYPTO) && defined(HAVE_PKCS5_PBKDF2_HMAC_SHA1)

static int
pbkdf2_sha1(const char *pw, size_t pw_len, const uint8_t *salt,
    size_t salt_len, unsigned rounds, uint8_t *derived_key,
    size_t derived_key_len) {

	PKCS5_PBKDF2_HMAC_SHA1(pw, pw_len, salt, salt_len, rounds,
	    derived_key_len, derived_key);
	return 0;
}

#else

/* Stub */
static int
pbkdf2_sha1(const char *pw, size_t pw_len, const uint8_t *salt,
    size_t salt_len, unsigned rounds, uint8_t *derived_key,
    size_t derived_key_len) {
	(void)pw; /* UNUSED */
	(void)pw_len; /* UNUSED */
	(void)salt; /* UNUSED */
	(void)salt_len; /* UNUSED */
	(void)rounds; /* UNUSED */
	(void)derived_key; /* UNUSED */
	(void)derived_key_len; /* UNUSED */
	return -1; /* UNSUPPORTED */
}

#endif

#ifdef ARCHIVE_CRYPTOR_USE_Apple_CommonCrypto
# if MAC_OS_X_VERSION_MAX_ALLOWED < 1090
#  define kCCAlgorithmAES kCCAlgorithmAES128
# endif

static int
aes_ctr_init(archive_crypto_ctx *ctx, const uint8_t *key, size_t key_len)
{
	CCCryptorStatus r;

	ctx->key_len = key_len;
	memcpy(ctx->key, key, key_len);
	memset(ctx->nonce, 0, sizeof(ctx->nonce));
	ctx->encr_pos = AES_BLOCK_SIZE;
	r = CCCryptorCreateWithMode(kCCEncrypt, kCCModeECB, kCCAlgorithmAES,
	    ccNoPadding, NULL, key, key_len, NULL, 0, 0, 0, &ctx->ctx);
	return (r == kCCSuccess)? 0: -1;
}

static int
aes_ctr_encrypt_counter(archive_crypto_ctx *ctx)
{
	CCCryptorRef ref = ctx->ctx;
	CCCryptorStatus r;

	r = CCCryptorReset(ref, NULL);
	if (r != kCCSuccess && r != kCCUnimplemented)
		return -1;
	r = CCCryptorUpdate(ref, ctx->nonce, AES_BLOCK_SIZE, ctx->encr_buf,
	    AES_BLOCK_SIZE, NULL);
	return (r == kCCSuccess)? 0: -1;
}

static int
aes_ctr_release(archive_crypto_ctx *ctx)
{
	memset(ctx->key, 0, ctx->key_len);
	memset(ctx->nonce, 0, sizeof(ctx->nonce));
	return 0;
}

#elif defined(_WIN32) && !defined(__CYGWIN__) && defined(HAVE_BCRYPT_H)

static int
aes_ctr_init(archive_crypto_ctx *ctx, const uint8_t *key, size_t key_len)
{
	BCRYPT_ALG_HANDLE hAlg;
	BCRYPT_KEY_HANDLE hKey;
	DWORD keyObj_len, aes_key_len;
	PBYTE keyObj;
	ULONG result;
	NTSTATUS status;
	BCRYPT_KEY_LENGTHS_STRUCT key_lengths;

	ctx->hAlg = NULL;
	ctx->hKey = NULL;
	ctx->keyObj = NULL;
	switch (key_len) {
	case 16: aes_key_len = 128; break;
	case 24: aes_key_len = 192; break;
	case 32: aes_key_len = 256; break;
	default: return -1;
	}
	status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM,
		MS_PRIMITIVE_PROVIDER, 0);
	if (!BCRYPT_SUCCESS(status))
		return -1;
	status = BCryptGetProperty(hAlg, BCRYPT_KEY_LENGTHS, (PUCHAR)&key_lengths,
		sizeof(key_lengths), &result, 0);
	if (!BCRYPT_SUCCESS(status)) {
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return -1;
	}
	if (key_lengths.dwMinLength > aes_key_len
		|| key_lengths.dwMaxLength < aes_key_len) {
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return -1;
	}
	status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&keyObj_len,
		sizeof(keyObj_len), &result, 0);
	if (!BCRYPT_SUCCESS(status)) {
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return -1;
	}
	keyObj = (PBYTE)HeapAlloc(GetProcessHeap(), 0, keyObj_len);
	if (keyObj == NULL) {
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return -1;
	}
	status = BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
		(PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
	if (!BCRYPT_SUCCESS(status)) {
		BCryptCloseAlgorithmProvider(hAlg, 0);
		HeapFree(GetProcessHeap(), 0, keyObj);
		return -1;
	}
	status = BCryptGenerateSymmetricKey(hAlg, &hKey,
		keyObj, keyObj_len,
		(PUCHAR)(uintptr_t)key, (ULONG)key_len, 0);
	if (!BCRYPT_SUCCESS(status)) {
		BCryptCloseAlgorithmProvider(hAlg, 0);
		HeapFree(GetProcessHeap(), 0, keyObj);
		return -1;
	}

	ctx->hAlg = hAlg;
	ctx->hKey = hKey;
	ctx->keyObj = keyObj;
	ctx->keyObj_len = keyObj_len;
	ctx->encr_pos = AES_BLOCK_SIZE;

	return 0;
}

static int
aes_ctr_encrypt_counter(archive_crypto_ctx *ctx)
{
	NTSTATUS status;
	ULONG result;

	status = BCryptEncrypt(ctx->hKey, (PUCHAR)ctx->nonce, AES_BLOCK_SIZE,
		NULL, NULL, 0, (PUCHAR)ctx->encr_buf, AES_BLOCK_SIZE,
		&result, 0);
	return BCRYPT_SUCCESS(status) ? 0 : -1;
}

static int
aes_ctr_release(archive_crypto_ctx *ctx)
{

	if (ctx->hAlg != NULL) {
		BCryptCloseAlgorithmProvider(ctx->hAlg, 0);
		ctx->hAlg = NULL;
		BCryptDestroyKey(ctx->hKey);
		ctx->hKey = NULL;
		HeapFree(GetProcessHeap(), 0, ctx->keyObj);
		ctx->keyObj = NULL;
	}
	memset(ctx, 0, sizeof(*ctx));
	return 0;
}

#elif defined(HAVE_LIBNETTLE) && defined(HAVE_NETTLE_AES_H)

static int
aes_ctr_init(archive_crypto_ctx *ctx, const uint8_t *key, size_t key_len)
{
	ctx->key_len = key_len;
	memcpy(ctx->key, key, key_len);
	memset(ctx->nonce, 0, sizeof(ctx->nonce));
	ctx->encr_pos = AES_BLOCK_SIZE;
	memset(&ctx->ctx, 0, sizeof(ctx->ctx));
	return 0;
}

static int
aes_ctr_encrypt_counter(archive_crypto_ctx *ctx)
{
	aes_set_encrypt_key(&ctx->ctx, ctx->key_len, ctx->key);
	aes_encrypt(&ctx->ctx, AES_BLOCK_SIZE, ctx->encr_buf, ctx->nonce);
	return 0;
}

static int
aes_ctr_release(archive_crypto_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	return 0;
}

#elif defined(HAVE_LIBCRYPTO)

static int
aes_ctr_init(archive_crypto_ctx *ctx, const uint8_t *key, size_t key_len)
{
	if ((ctx->ctx = EVP_CIPHER_CTX_new()) == NULL)
		return -1;

	switch (key_len) {
	case 16: ctx->type = EVP_aes_128_ecb(); break;
	case 24: ctx->type = EVP_aes_192_ecb(); break;
	case 32: ctx->type = EVP_aes_256_ecb(); break;
	default: ctx->type = NULL; return -1;
	}

	ctx->key_len = key_len;
	memcpy(ctx->key, key, key_len);
	memset(ctx->nonce, 0, sizeof(ctx->nonce));
	ctx->encr_pos = AES_BLOCK_SIZE;
#if OPENSSL_VERSION_NUMBER  >= 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
	if (!EVP_CIPHER_CTX_reset(ctx->ctx)) {
		EVP_CIPHER_CTX_free(ctx->ctx);
		ctx->ctx = NULL;
	}
#else
	EVP_CIPHER_CTX_init(ctx->ctx);
#endif
	return 0;
}

static int
aes_ctr_encrypt_counter(archive_crypto_ctx *ctx)
{
	int outl = 0;
	int r;

	r = EVP_EncryptInit_ex(ctx->ctx, ctx->type, NULL, ctx->key, NULL);
	if (r == 0)
		return -1;
	r = EVP_EncryptUpdate(ctx->ctx, ctx->encr_buf, &outl, ctx->nonce,
	    AES_BLOCK_SIZE);
	if (r == 0 || outl != AES_BLOCK_SIZE)
		return -1;
	return 0;
}

static int
aes_ctr_release(archive_crypto_ctx *ctx)
{
	EVP_CIPHER_CTX_free(ctx->ctx);
	memset(ctx->key, 0, ctx->key_len);
	memset(ctx->nonce, 0, sizeof(ctx->nonce));
	return 0;
}

#else

#define ARCHIVE_CRYPTOR_STUB
/* Stub */
static int
aes_ctr_init(archive_crypto_ctx *ctx, const uint8_t *key, size_t key_len)
{
	(void)ctx; /* UNUSED */
	(void)key; /* UNUSED */
	(void)key_len; /* UNUSED */
	return -1;
}

static int
aes_ctr_encrypt_counter(archive_crypto_ctx *ctx)
{
	(void)ctx; /* UNUSED */
	return -1;
}

static int
aes_ctr_release(archive_crypto_ctx *ctx)
{
	(void)ctx; /* UNUSED */
	return 0;
}

#endif

#ifdef ARCHIVE_CRYPTOR_STUB
static int
aes_ctr_update(archive_crypto_ctx *ctx, const uint8_t * const in,
    size_t in_len, uint8_t * const out, size_t *out_len)
{
	(void)ctx; /* UNUSED */
	(void)in; /* UNUSED */
	(void)in_len; /* UNUSED */
	(void)out; /* UNUSED */
	(void)out_len; /* UNUSED */
	aes_ctr_encrypt_counter(ctx); /* UNUSED */ /* Fix unused function warning */
	return -1;
}

#else
static void
aes_ctr_increase_counter(archive_crypto_ctx *ctx)
{
	uint8_t *const nonce = ctx->nonce;
	int j;

	for (j = 0; j < 8; j++) {
		if (++nonce[j])
			break;
	}
}

static int
aes_ctr_update(archive_crypto_ctx *ctx, const uint8_t * const in,
    size_t in_len, uint8_t * const out, size_t *out_len)
{
	uint8_t *const ebuf = ctx->encr_buf;
	unsigned pos = ctx->encr_pos;
	unsigned max = (unsigned)((in_len < *out_len)? in_len: *out_len);
	unsigned i;

	for (i = 0; i < max; ) {
		if (pos == AES_BLOCK_SIZE) {
			aes_ctr_increase_counter(ctx);
			if (aes_ctr_encrypt_counter(ctx) != 0)
				return -1;
			while (max -i >= AES_BLOCK_SIZE) {
				for (pos = 0; pos < AES_BLOCK_SIZE; pos++)
					out[i+pos] = in[i+pos] ^ ebuf[pos];
				i += AES_BLOCK_SIZE;
				aes_ctr_increase_counter(ctx);
				if (aes_ctr_encrypt_counter(ctx) != 0)
					return -1;
			}
			pos = 0;
			if (i >= max)
				break;
		}
		out[i] = in[i] ^ ebuf[pos++];
		i++;
	}
	ctx->encr_pos = pos;
	*out_len = i;

	return 0;
}
#endif /* ARCHIVE_CRYPTOR_STUB */


const struct archive_cryptor __archive_cryptor =
{
  &pbkdf2_sha1,
  &aes_ctr_init,
  &aes_ctr_update,
  &aes_ctr_release,
  &aes_ctr_init,
  &aes_ctr_update,
  &aes_ctr_release,
};
