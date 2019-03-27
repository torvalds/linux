/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/cryptlib.h"
#include <openssl/hmac.h>
#include <openssl/opensslconf.h>
#include "hmac_lcl.h"

int HMAC_Init_ex(HMAC_CTX *ctx, const void *key, int len,
                 const EVP_MD *md, ENGINE *impl)
{
    int rv = 0;
    int i, j, reset = 0;
    unsigned char pad[HMAC_MAX_MD_CBLOCK_SIZE];

    /* If we are changing MD then we must have a key */
    if (md != NULL && md != ctx->md && (key == NULL || len < 0))
        return 0;

    if (md != NULL) {
        reset = 1;
        ctx->md = md;
    } else if (ctx->md) {
        md = ctx->md;
    } else {
        return 0;
    }

    if (key != NULL) {
        reset = 1;
        j = EVP_MD_block_size(md);
        if (!ossl_assert(j <= (int)sizeof(ctx->key)))
            return 0;
        if (j < len) {
            if (!EVP_DigestInit_ex(ctx->md_ctx, md, impl)
                    || !EVP_DigestUpdate(ctx->md_ctx, key, len)
                    || !EVP_DigestFinal_ex(ctx->md_ctx, ctx->key,
                                           &ctx->key_length))
                return 0;
        } else {
            if (len < 0 || len > (int)sizeof(ctx->key))
                return 0;
            memcpy(ctx->key, key, len);
            ctx->key_length = len;
        }
        if (ctx->key_length != HMAC_MAX_MD_CBLOCK_SIZE)
            memset(&ctx->key[ctx->key_length], 0,
                   HMAC_MAX_MD_CBLOCK_SIZE - ctx->key_length);
    }

    if (reset) {
        for (i = 0; i < HMAC_MAX_MD_CBLOCK_SIZE; i++)
            pad[i] = 0x36 ^ ctx->key[i];
        if (!EVP_DigestInit_ex(ctx->i_ctx, md, impl)
                || !EVP_DigestUpdate(ctx->i_ctx, pad, EVP_MD_block_size(md)))
            goto err;

        for (i = 0; i < HMAC_MAX_MD_CBLOCK_SIZE; i++)
            pad[i] = 0x5c ^ ctx->key[i];
        if (!EVP_DigestInit_ex(ctx->o_ctx, md, impl)
                || !EVP_DigestUpdate(ctx->o_ctx, pad, EVP_MD_block_size(md)))
            goto err;
    }
    if (!EVP_MD_CTX_copy_ex(ctx->md_ctx, ctx->i_ctx))
        goto err;
    rv = 1;
 err:
    if (reset)
        OPENSSL_cleanse(pad, sizeof(pad));
    return rv;
}

#if OPENSSL_API_COMPAT < 0x10100000L
int HMAC_Init(HMAC_CTX *ctx, const void *key, int len, const EVP_MD *md)
{
    if (key && md)
        HMAC_CTX_reset(ctx);
    return HMAC_Init_ex(ctx, key, len, md, NULL);
}
#endif

int HMAC_Update(HMAC_CTX *ctx, const unsigned char *data, size_t len)
{
    if (!ctx->md)
        return 0;
    return EVP_DigestUpdate(ctx->md_ctx, data, len);
}

int HMAC_Final(HMAC_CTX *ctx, unsigned char *md, unsigned int *len)
{
    unsigned int i;
    unsigned char buf[EVP_MAX_MD_SIZE];

    if (!ctx->md)
        goto err;

    if (!EVP_DigestFinal_ex(ctx->md_ctx, buf, &i))
        goto err;
    if (!EVP_MD_CTX_copy_ex(ctx->md_ctx, ctx->o_ctx))
        goto err;
    if (!EVP_DigestUpdate(ctx->md_ctx, buf, i))
        goto err;
    if (!EVP_DigestFinal_ex(ctx->md_ctx, md, len))
        goto err;
    return 1;
 err:
    return 0;
}

size_t HMAC_size(const HMAC_CTX *ctx)
{
    int size = EVP_MD_size((ctx)->md);

    return (size < 0) ? 0 : size;
}

HMAC_CTX *HMAC_CTX_new(void)
{
    HMAC_CTX *ctx = OPENSSL_zalloc(sizeof(HMAC_CTX));

    if (ctx != NULL) {
        if (!HMAC_CTX_reset(ctx)) {
            HMAC_CTX_free(ctx);
            return NULL;
        }
    }
    return ctx;
}

static void hmac_ctx_cleanup(HMAC_CTX *ctx)
{
    EVP_MD_CTX_reset(ctx->i_ctx);
    EVP_MD_CTX_reset(ctx->o_ctx);
    EVP_MD_CTX_reset(ctx->md_ctx);
    ctx->md = NULL;
    ctx->key_length = 0;
    OPENSSL_cleanse(ctx->key, sizeof(ctx->key));
}

void HMAC_CTX_free(HMAC_CTX *ctx)
{
    if (ctx != NULL) {
        hmac_ctx_cleanup(ctx);
        EVP_MD_CTX_free(ctx->i_ctx);
        EVP_MD_CTX_free(ctx->o_ctx);
        EVP_MD_CTX_free(ctx->md_ctx);
        OPENSSL_free(ctx);
    }
}

static int hmac_ctx_alloc_mds(HMAC_CTX *ctx)
{
    if (ctx->i_ctx == NULL)
        ctx->i_ctx = EVP_MD_CTX_new();
    if (ctx->i_ctx == NULL)
        return 0;
    if (ctx->o_ctx == NULL)
        ctx->o_ctx = EVP_MD_CTX_new();
    if (ctx->o_ctx == NULL)
        return 0;
    if (ctx->md_ctx == NULL)
        ctx->md_ctx = EVP_MD_CTX_new();
    if (ctx->md_ctx == NULL)
        return 0;
    return 1;
}

int HMAC_CTX_reset(HMAC_CTX *ctx)
{
    hmac_ctx_cleanup(ctx);
    if (!hmac_ctx_alloc_mds(ctx)) {
        hmac_ctx_cleanup(ctx);
        return 0;
    }
    return 1;
}

int HMAC_CTX_copy(HMAC_CTX *dctx, HMAC_CTX *sctx)
{
    if (!hmac_ctx_alloc_mds(dctx))
        goto err;
    if (!EVP_MD_CTX_copy_ex(dctx->i_ctx, sctx->i_ctx))
        goto err;
    if (!EVP_MD_CTX_copy_ex(dctx->o_ctx, sctx->o_ctx))
        goto err;
    if (!EVP_MD_CTX_copy_ex(dctx->md_ctx, sctx->md_ctx))
        goto err;
    memcpy(dctx->key, sctx->key, HMAC_MAX_MD_CBLOCK_SIZE);
    dctx->key_length = sctx->key_length;
    dctx->md = sctx->md;
    return 1;
 err:
    hmac_ctx_cleanup(dctx);
    return 0;
}

unsigned char *HMAC(const EVP_MD *evp_md, const void *key, int key_len,
                    const unsigned char *d, size_t n, unsigned char *md,
                    unsigned int *md_len)
{
    HMAC_CTX *c = NULL;
    static unsigned char m[EVP_MAX_MD_SIZE];
    static const unsigned char dummy_key[1] = {'\0'};

    if (md == NULL)
        md = m;
    if ((c = HMAC_CTX_new()) == NULL)
        goto err;

    /* For HMAC_Init_ex, NULL key signals reuse. */
    if (key == NULL && key_len == 0) {
        key = dummy_key;
    }

    if (!HMAC_Init_ex(c, key, key_len, evp_md, NULL))
        goto err;
    if (!HMAC_Update(c, d, n))
        goto err;
    if (!HMAC_Final(c, md, md_len))
        goto err;
    HMAC_CTX_free(c);
    return md;
 err:
    HMAC_CTX_free(c);
    return NULL;
}

void HMAC_CTX_set_flags(HMAC_CTX *ctx, unsigned long flags)
{
    EVP_MD_CTX_set_flags(ctx->i_ctx, flags);
    EVP_MD_CTX_set_flags(ctx->o_ctx, flags);
    EVP_MD_CTX_set_flags(ctx->md_ctx, flags);
}

const EVP_MD *HMAC_CTX_get_md(const HMAC_CTX *ctx)
{
    return ctx->md;
}
