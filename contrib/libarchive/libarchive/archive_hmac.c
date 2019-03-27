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
#include "archive_hmac_private.h"

/*
 * On systems that do not support any recognized crypto libraries,
 * the archive_hmac.c file is expected to define no usable symbols.
 *
 * But some compilers and linkers choke on empty object files, so
 * define a public symbol that will always exist.  This could
 * be removed someday if this file gains another always-present
 * symbol definition.
 */
int __libarchive_hmac_build_hack(void) {
	return 0;
}


#ifdef ARCHIVE_HMAC_USE_Apple_CommonCrypto

static int
__hmac_sha1_init(archive_hmac_sha1_ctx *ctx, const uint8_t *key, size_t key_len)
{
	CCHmacInit(ctx, kCCHmacAlgSHA1, key, key_len);
	return 0;
}

static void
__hmac_sha1_update(archive_hmac_sha1_ctx *ctx, const uint8_t *data,
    size_t data_len)
{
	CCHmacUpdate(ctx, data, data_len);
}

static void
__hmac_sha1_final(archive_hmac_sha1_ctx *ctx, uint8_t *out, size_t *out_len)
{
	CCHmacFinal(ctx, out);
	*out_len = 20;
}

static void
__hmac_sha1_cleanup(archive_hmac_sha1_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
}

#elif defined(_WIN32) && !defined(__CYGWIN__) && defined(HAVE_BCRYPT_H)

#ifndef BCRYPT_HASH_REUSABLE_FLAG
# define BCRYPT_HASH_REUSABLE_FLAG 0x00000020
#endif

static int
__hmac_sha1_init(archive_hmac_sha1_ctx *ctx, const uint8_t *key, size_t key_len)
{
	BCRYPT_ALG_HANDLE hAlg;
	BCRYPT_HASH_HANDLE hHash;
	DWORD hash_len;
	PBYTE hash;
	ULONG result;
	NTSTATUS status;

	ctx->hAlg = NULL;
	status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA1_ALGORITHM,
		MS_PRIMITIVE_PROVIDER, BCRYPT_ALG_HANDLE_HMAC_FLAG);
	if (!BCRYPT_SUCCESS(status))
		return -1;
	status = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_len,
		sizeof(hash_len), &result, 0);
	if (!BCRYPT_SUCCESS(status)) {
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return -1;
	}
	hash = (PBYTE)HeapAlloc(GetProcessHeap(), 0, hash_len);
	if (hash == NULL) {
		BCryptCloseAlgorithmProvider(hAlg, 0);
		return -1;
	}
	status = BCryptCreateHash(hAlg, &hHash, NULL, 0,
		(PUCHAR)key, (ULONG)key_len, BCRYPT_HASH_REUSABLE_FLAG);
	if (!BCRYPT_SUCCESS(status)) {
		BCryptCloseAlgorithmProvider(hAlg, 0);
		HeapFree(GetProcessHeap(), 0, hash);
		return -1;
	}

	ctx->hAlg = hAlg;
	ctx->hHash = hHash;
	ctx->hash_len = hash_len;
	ctx->hash = hash;

	return 0;
}

static void
__hmac_sha1_update(archive_hmac_sha1_ctx *ctx, const uint8_t *data,
	size_t data_len)
{
	BCryptHashData(ctx->hHash, (PUCHAR)(uintptr_t)data, (ULONG)data_len, 0);
}

static void
__hmac_sha1_final(archive_hmac_sha1_ctx *ctx, uint8_t *out, size_t *out_len)
{
	BCryptFinishHash(ctx->hHash, ctx->hash, ctx->hash_len, 0);
	if (ctx->hash_len == *out_len)
		memcpy(out, ctx->hash, *out_len);
}

static void
__hmac_sha1_cleanup(archive_hmac_sha1_ctx *ctx)
{
	if (ctx->hAlg != NULL) {
		BCryptCloseAlgorithmProvider(ctx->hAlg, 0);
		HeapFree(GetProcessHeap(), 0, ctx->hash);
		ctx->hAlg = NULL;
	}
}

#elif defined(HAVE_LIBNETTLE) && defined(HAVE_NETTLE_HMAC_H)

static int
__hmac_sha1_init(archive_hmac_sha1_ctx *ctx, const uint8_t *key, size_t key_len)
{
	hmac_sha1_set_key(ctx, key_len, key);
	return 0;
}

static void
__hmac_sha1_update(archive_hmac_sha1_ctx *ctx, const uint8_t *data,
    size_t data_len)
{
	hmac_sha1_update(ctx, data_len, data);
}

static void
__hmac_sha1_final(archive_hmac_sha1_ctx *ctx, uint8_t *out, size_t *out_len)
{
	hmac_sha1_digest(ctx, (unsigned)*out_len, out);
}

static void
__hmac_sha1_cleanup(archive_hmac_sha1_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
}

#elif defined(HAVE_LIBCRYPTO)

static int
__hmac_sha1_init(archive_hmac_sha1_ctx *ctx, const uint8_t *key, size_t key_len)
{
	*ctx = HMAC_CTX_new();
	if (*ctx == NULL)
		return -1;
	HMAC_Init_ex(*ctx, key, key_len, EVP_sha1(), NULL);
	return 0;
}

static void
__hmac_sha1_update(archive_hmac_sha1_ctx *ctx, const uint8_t *data,
    size_t data_len)
{
	HMAC_Update(*ctx, data, data_len);
}

static void
__hmac_sha1_final(archive_hmac_sha1_ctx *ctx, uint8_t *out, size_t *out_len)
{
	unsigned int len = (unsigned int)*out_len;
	HMAC_Final(*ctx, out, &len);
	*out_len = len;
}

static void
__hmac_sha1_cleanup(archive_hmac_sha1_ctx *ctx)
{
	HMAC_CTX_free(*ctx);
	*ctx = NULL;
}

#else

/* Stub */
static int
__hmac_sha1_init(archive_hmac_sha1_ctx *ctx, const uint8_t *key, size_t key_len)
{
	(void)ctx;/* UNUSED */
	(void)key;/* UNUSED */
	(void)key_len;/* UNUSED */
	return -1;
}

static void
__hmac_sha1_update(archive_hmac_sha1_ctx *ctx, const uint8_t *data,
    size_t data_len)
{
	(void)ctx;/* UNUSED */
	(void)data;/* UNUSED */
	(void)data_len;/* UNUSED */
}

static void
__hmac_sha1_final(archive_hmac_sha1_ctx *ctx, uint8_t *out, size_t *out_len)
{
	(void)ctx;/* UNUSED */
	(void)out;/* UNUSED */
	(void)out_len;/* UNUSED */
}

static void
__hmac_sha1_cleanup(archive_hmac_sha1_ctx *ctx)
{
	(void)ctx;/* UNUSED */
}

#endif

const struct archive_hmac __archive_hmac = {
	&__hmac_sha1_init,
	&__hmac_sha1_update,
	&__hmac_sha1_final,
	&__hmac_sha1_cleanup,
};
