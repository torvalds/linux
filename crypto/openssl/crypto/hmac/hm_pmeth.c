/*
 * Copyright 2007-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/err.h>
#include "internal/evp_int.h"

/* HMAC pkey context structure */

typedef struct {
    const EVP_MD *md;           /* MD for HMAC use */
    ASN1_OCTET_STRING ktmp;     /* Temp storage for key */
    HMAC_CTX *ctx;
} HMAC_PKEY_CTX;

static int pkey_hmac_init(EVP_PKEY_CTX *ctx)
{
    HMAC_PKEY_CTX *hctx;

    if ((hctx = OPENSSL_zalloc(sizeof(*hctx))) == NULL) {
        CRYPTOerr(CRYPTO_F_PKEY_HMAC_INIT, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    hctx->ktmp.type = V_ASN1_OCTET_STRING;
    hctx->ctx = HMAC_CTX_new();
    if (hctx->ctx == NULL) {
        OPENSSL_free(hctx);
        return 0;
    }

    ctx->data = hctx;
    ctx->keygen_info_count = 0;

    return 1;
}

static void pkey_hmac_cleanup(EVP_PKEY_CTX *ctx);

static int pkey_hmac_copy(EVP_PKEY_CTX *dst, EVP_PKEY_CTX *src)
{
    HMAC_PKEY_CTX *sctx, *dctx;

    /* allocate memory for dst->data and a new HMAC_CTX in dst->data->ctx */
    if (!pkey_hmac_init(dst))
        return 0;
    sctx = EVP_PKEY_CTX_get_data(src);
    dctx = EVP_PKEY_CTX_get_data(dst);
    dctx->md = sctx->md;
    if (!HMAC_CTX_copy(dctx->ctx, sctx->ctx))
        goto err;
    if (sctx->ktmp.data) {
        if (!ASN1_OCTET_STRING_set(&dctx->ktmp,
                                   sctx->ktmp.data, sctx->ktmp.length))
            goto err;
    }
    return 1;
err:
    /* release HMAC_CTX in dst->data->ctx and memory allocated for dst->data */
    pkey_hmac_cleanup (dst);
    return 0;
}

static void pkey_hmac_cleanup(EVP_PKEY_CTX *ctx)
{
    HMAC_PKEY_CTX *hctx = EVP_PKEY_CTX_get_data(ctx);

    if (hctx != NULL) {
        HMAC_CTX_free(hctx->ctx);
        OPENSSL_clear_free(hctx->ktmp.data, hctx->ktmp.length);
        OPENSSL_free(hctx);
        EVP_PKEY_CTX_set_data(ctx, NULL);
    }
}

static int pkey_hmac_keygen(EVP_PKEY_CTX *ctx, EVP_PKEY *pkey)
{
    ASN1_OCTET_STRING *hkey = NULL;
    HMAC_PKEY_CTX *hctx = ctx->data;
    if (!hctx->ktmp.data)
        return 0;
    hkey = ASN1_OCTET_STRING_dup(&hctx->ktmp);
    if (!hkey)
        return 0;
    EVP_PKEY_assign(pkey, EVP_PKEY_HMAC, hkey);

    return 1;
}

static int int_update(EVP_MD_CTX *ctx, const void *data, size_t count)
{
    HMAC_PKEY_CTX *hctx = EVP_MD_CTX_pkey_ctx(ctx)->data;
    if (!HMAC_Update(hctx->ctx, data, count))
        return 0;
    return 1;
}

static int hmac_signctx_init(EVP_PKEY_CTX *ctx, EVP_MD_CTX *mctx)
{
    HMAC_PKEY_CTX *hctx = ctx->data;
    HMAC_CTX_set_flags(hctx->ctx,
                       EVP_MD_CTX_test_flags(mctx, ~EVP_MD_CTX_FLAG_NO_INIT));
    EVP_MD_CTX_set_flags(mctx, EVP_MD_CTX_FLAG_NO_INIT);
    EVP_MD_CTX_set_update_fn(mctx, int_update);
    return 1;
}

static int hmac_signctx(EVP_PKEY_CTX *ctx, unsigned char *sig, size_t *siglen,
                        EVP_MD_CTX *mctx)
{
    unsigned int hlen;
    HMAC_PKEY_CTX *hctx = ctx->data;
    int l = EVP_MD_CTX_size(mctx);

    if (l < 0)
        return 0;
    *siglen = l;
    if (!sig)
        return 1;

    if (!HMAC_Final(hctx->ctx, sig, &hlen))
        return 0;
    *siglen = (size_t)hlen;
    return 1;
}

static int pkey_hmac_ctrl(EVP_PKEY_CTX *ctx, int type, int p1, void *p2)
{
    HMAC_PKEY_CTX *hctx = ctx->data;
    ASN1_OCTET_STRING *key;
    switch (type) {

    case EVP_PKEY_CTRL_SET_MAC_KEY:
        if ((!p2 && p1 > 0) || (p1 < -1))
            return 0;
        if (!ASN1_OCTET_STRING_set(&hctx->ktmp, p2, p1))
            return 0;
        break;

    case EVP_PKEY_CTRL_MD:
        hctx->md = p2;
        break;

    case EVP_PKEY_CTRL_DIGESTINIT:
        key = (ASN1_OCTET_STRING *)ctx->pkey->pkey.ptr;
        if (!HMAC_Init_ex(hctx->ctx, key->data, key->length, hctx->md,
                          ctx->engine))
            return 0;
        break;

    default:
        return -2;

    }
    return 1;
}

static int pkey_hmac_ctrl_str(EVP_PKEY_CTX *ctx,
                              const char *type, const char *value)
{
    if (!value) {
        return 0;
    }
    if (strcmp(type, "key") == 0)
        return EVP_PKEY_CTX_str2ctrl(ctx, EVP_PKEY_CTRL_SET_MAC_KEY, value);
    if (strcmp(type, "hexkey") == 0)
        return EVP_PKEY_CTX_hex2ctrl(ctx, EVP_PKEY_CTRL_SET_MAC_KEY, value);
    return -2;
}

const EVP_PKEY_METHOD hmac_pkey_meth = {
    EVP_PKEY_HMAC,
    0,
    pkey_hmac_init,
    pkey_hmac_copy,
    pkey_hmac_cleanup,

    0, 0,

    0,
    pkey_hmac_keygen,

    0, 0,

    0, 0,

    0, 0,

    hmac_signctx_init,
    hmac_signctx,

    0, 0,

    0, 0,

    0, 0,

    0, 0,

    pkey_hmac_ctrl,
    pkey_hmac_ctrl_str
};
