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
#include <openssl/err.h>
#include "internal/poly1305.h"
#include "poly1305_local.h"
#include "internal/evp_int.h"

/* POLY1305 pkey context structure */

typedef struct {
    ASN1_OCTET_STRING ktmp;     /* Temp storage for key */
    POLY1305 ctx;
} POLY1305_PKEY_CTX;

static int pkey_poly1305_init(EVP_PKEY_CTX *ctx)
{
    POLY1305_PKEY_CTX *pctx;

    if ((pctx = OPENSSL_zalloc(sizeof(*pctx))) == NULL) {
        CRYPTOerr(CRYPTO_F_PKEY_POLY1305_INIT, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    pctx->ktmp.type = V_ASN1_OCTET_STRING;

    EVP_PKEY_CTX_set_data(ctx, pctx);
    EVP_PKEY_CTX_set0_keygen_info(ctx, NULL, 0);
    return 1;
}

static void pkey_poly1305_cleanup(EVP_PKEY_CTX *ctx)
{
    POLY1305_PKEY_CTX *pctx = EVP_PKEY_CTX_get_data(ctx);

    if (pctx != NULL) {
        OPENSSL_clear_free(pctx->ktmp.data, pctx->ktmp.length);
        OPENSSL_clear_free(pctx, sizeof(*pctx));
        EVP_PKEY_CTX_set_data(ctx, NULL);
    }
}

static int pkey_poly1305_copy(EVP_PKEY_CTX *dst, EVP_PKEY_CTX *src)
{
    POLY1305_PKEY_CTX *sctx, *dctx;

    /* allocate memory for dst->data and a new POLY1305_CTX in dst->data->ctx */
    if (!pkey_poly1305_init(dst))
        return 0;
    sctx = EVP_PKEY_CTX_get_data(src);
    dctx = EVP_PKEY_CTX_get_data(dst);
    if (ASN1_STRING_get0_data(&sctx->ktmp) != NULL &&
        !ASN1_STRING_copy(&dctx->ktmp, &sctx->ktmp)) {
        /* cleanup and free the POLY1305_PKEY_CTX in dst->data */
        pkey_poly1305_cleanup(dst);
        return 0;
    }
    memcpy(&dctx->ctx, &sctx->ctx, sizeof(POLY1305));
    return 1;
}

static int pkey_poly1305_keygen(EVP_PKEY_CTX *ctx, EVP_PKEY *pkey)
{
    ASN1_OCTET_STRING *key;
    POLY1305_PKEY_CTX *pctx = EVP_PKEY_CTX_get_data(ctx);

    if (ASN1_STRING_get0_data(&pctx->ktmp) == NULL)
        return 0;
    key = ASN1_OCTET_STRING_dup(&pctx->ktmp);
    if (key == NULL)
        return 0;
    return EVP_PKEY_assign_POLY1305(pkey, key);
}

static int int_update(EVP_MD_CTX *ctx, const void *data, size_t count)
{
    POLY1305_PKEY_CTX *pctx = EVP_PKEY_CTX_get_data(EVP_MD_CTX_pkey_ctx(ctx));

    Poly1305_Update(&pctx->ctx, data, count);
    return 1;
}

static int poly1305_signctx_init(EVP_PKEY_CTX *ctx, EVP_MD_CTX *mctx)
{
    POLY1305_PKEY_CTX *pctx = ctx->data;
    ASN1_OCTET_STRING *key = (ASN1_OCTET_STRING *)ctx->pkey->pkey.ptr;

    if (key->length != POLY1305_KEY_SIZE)
        return 0;
    EVP_MD_CTX_set_flags(mctx, EVP_MD_CTX_FLAG_NO_INIT);
    EVP_MD_CTX_set_update_fn(mctx, int_update);
    Poly1305_Init(&pctx->ctx, key->data);
    return 1;
}
static int poly1305_signctx(EVP_PKEY_CTX *ctx, unsigned char *sig, size_t *siglen,
                            EVP_MD_CTX *mctx)
{
    POLY1305_PKEY_CTX *pctx = ctx->data;

    *siglen = POLY1305_DIGEST_SIZE;
    if (sig != NULL)
        Poly1305_Final(&pctx->ctx, sig);
    return 1;
}

static int pkey_poly1305_ctrl(EVP_PKEY_CTX *ctx, int type, int p1, void *p2)
{
    POLY1305_PKEY_CTX *pctx = EVP_PKEY_CTX_get_data(ctx);
    const unsigned char *key;
    size_t len;

    switch (type) {

    case EVP_PKEY_CTRL_MD:
        /* ignore */
        break;

    case EVP_PKEY_CTRL_SET_MAC_KEY:
    case EVP_PKEY_CTRL_DIGESTINIT:
        if (type == EVP_PKEY_CTRL_SET_MAC_KEY) {
            /* user explicitly setting the key */
            key = p2;
            len = p1;
        } else {
            /* user indirectly setting the key via EVP_DigestSignInit */
            key = EVP_PKEY_get0_poly1305(EVP_PKEY_CTX_get0_pkey(ctx), &len);
        }
        if (key == NULL || len != POLY1305_KEY_SIZE ||
            !ASN1_OCTET_STRING_set(&pctx->ktmp, key, len))
            return 0;
        Poly1305_Init(&pctx->ctx, ASN1_STRING_get0_data(&pctx->ktmp));
        break;

    default:
        return -2;

    }
    return 1;
}

static int pkey_poly1305_ctrl_str(EVP_PKEY_CTX *ctx,
                                  const char *type, const char *value)
{
    if (value == NULL)
        return 0;
    if (strcmp(type, "key") == 0)
        return EVP_PKEY_CTX_str2ctrl(ctx, EVP_PKEY_CTRL_SET_MAC_KEY, value);
    if (strcmp(type, "hexkey") == 0)
        return EVP_PKEY_CTX_hex2ctrl(ctx, EVP_PKEY_CTRL_SET_MAC_KEY, value);
    return -2;
}

const EVP_PKEY_METHOD poly1305_pkey_meth = {
    EVP_PKEY_POLY1305,
    EVP_PKEY_FLAG_SIGCTX_CUSTOM, /* we don't deal with a separate MD */
    pkey_poly1305_init,
    pkey_poly1305_copy,
    pkey_poly1305_cleanup,

    0, 0,

    0,
    pkey_poly1305_keygen,

    0, 0,

    0, 0,

    0, 0,

    poly1305_signctx_init,
    poly1305_signctx,

    0, 0,

    0, 0,

    0, 0,

    0, 0,

    pkey_poly1305_ctrl,
    pkey_poly1305_ctrl_str
};
