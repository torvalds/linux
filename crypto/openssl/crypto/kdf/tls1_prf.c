/*
 * Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/kdf.h>
#include <openssl/evp.h>
#include "internal/evp_int.h"

static int tls1_prf_alg(const EVP_MD *md,
                        const unsigned char *sec, size_t slen,
                        const unsigned char *seed, size_t seed_len,
                        unsigned char *out, size_t olen);

#define TLS1_PRF_MAXBUF 1024

/* TLS KDF pkey context structure */

typedef struct {
    /* Digest to use for PRF */
    const EVP_MD *md;
    /* Secret value to use for PRF */
    unsigned char *sec;
    size_t seclen;
    /* Buffer of concatenated seed data */
    unsigned char seed[TLS1_PRF_MAXBUF];
    size_t seedlen;
} TLS1_PRF_PKEY_CTX;

static int pkey_tls1_prf_init(EVP_PKEY_CTX *ctx)
{
    TLS1_PRF_PKEY_CTX *kctx;

    if ((kctx = OPENSSL_zalloc(sizeof(*kctx))) == NULL) {
        KDFerr(KDF_F_PKEY_TLS1_PRF_INIT, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    ctx->data = kctx;

    return 1;
}

static void pkey_tls1_prf_cleanup(EVP_PKEY_CTX *ctx)
{
    TLS1_PRF_PKEY_CTX *kctx = ctx->data;
    OPENSSL_clear_free(kctx->sec, kctx->seclen);
    OPENSSL_cleanse(kctx->seed, kctx->seedlen);
    OPENSSL_free(kctx);
}

static int pkey_tls1_prf_ctrl(EVP_PKEY_CTX *ctx, int type, int p1, void *p2)
{
    TLS1_PRF_PKEY_CTX *kctx = ctx->data;
    switch (type) {
    case EVP_PKEY_CTRL_TLS_MD:
        kctx->md = p2;
        return 1;

    case EVP_PKEY_CTRL_TLS_SECRET:
        if (p1 < 0)
            return 0;
        if (kctx->sec != NULL)
            OPENSSL_clear_free(kctx->sec, kctx->seclen);
        OPENSSL_cleanse(kctx->seed, kctx->seedlen);
        kctx->seedlen = 0;
        kctx->sec = OPENSSL_memdup(p2, p1);
        if (kctx->sec == NULL)
            return 0;
        kctx->seclen  = p1;
        return 1;

    case EVP_PKEY_CTRL_TLS_SEED:
        if (p1 == 0 || p2 == NULL)
            return 1;
        if (p1 < 0 || p1 > (int)(TLS1_PRF_MAXBUF - kctx->seedlen))
            return 0;
        memcpy(kctx->seed + kctx->seedlen, p2, p1);
        kctx->seedlen += p1;
        return 1;

    default:
        return -2;

    }
}

static int pkey_tls1_prf_ctrl_str(EVP_PKEY_CTX *ctx,
                                  const char *type, const char *value)
{
    if (value == NULL) {
        KDFerr(KDF_F_PKEY_TLS1_PRF_CTRL_STR, KDF_R_VALUE_MISSING);
        return 0;
    }
    if (strcmp(type, "md") == 0) {
        TLS1_PRF_PKEY_CTX *kctx = ctx->data;

        const EVP_MD *md = EVP_get_digestbyname(value);
        if (md == NULL) {
            KDFerr(KDF_F_PKEY_TLS1_PRF_CTRL_STR, KDF_R_INVALID_DIGEST);
            return 0;
        }
        kctx->md = md;
        return 1;
    }
    if (strcmp(type, "secret") == 0)
        return EVP_PKEY_CTX_str2ctrl(ctx, EVP_PKEY_CTRL_TLS_SECRET, value);
    if (strcmp(type, "hexsecret") == 0)
        return EVP_PKEY_CTX_hex2ctrl(ctx, EVP_PKEY_CTRL_TLS_SECRET, value);
    if (strcmp(type, "seed") == 0)
        return EVP_PKEY_CTX_str2ctrl(ctx, EVP_PKEY_CTRL_TLS_SEED, value);
    if (strcmp(type, "hexseed") == 0)
        return EVP_PKEY_CTX_hex2ctrl(ctx, EVP_PKEY_CTRL_TLS_SEED, value);

    KDFerr(KDF_F_PKEY_TLS1_PRF_CTRL_STR, KDF_R_UNKNOWN_PARAMETER_TYPE);
    return -2;
}

static int pkey_tls1_prf_derive(EVP_PKEY_CTX *ctx, unsigned char *key,
                                size_t *keylen)
{
    TLS1_PRF_PKEY_CTX *kctx = ctx->data;
    if (kctx->md == NULL) {
        KDFerr(KDF_F_PKEY_TLS1_PRF_DERIVE, KDF_R_MISSING_MESSAGE_DIGEST);
        return 0;
    }
    if (kctx->sec == NULL) {
        KDFerr(KDF_F_PKEY_TLS1_PRF_DERIVE, KDF_R_MISSING_SECRET);
        return 0;
    }
    if (kctx->seedlen == 0) {
        KDFerr(KDF_F_PKEY_TLS1_PRF_DERIVE, KDF_R_MISSING_SEED);
        return 0;
    }
    return tls1_prf_alg(kctx->md, kctx->sec, kctx->seclen,
                        kctx->seed, kctx->seedlen,
                        key, *keylen);
}

const EVP_PKEY_METHOD tls1_prf_pkey_meth = {
    EVP_PKEY_TLS1_PRF,
    0,
    pkey_tls1_prf_init,
    0,
    pkey_tls1_prf_cleanup,

    0, 0,
    0, 0,

    0,
    0,

    0,
    0,

    0, 0,

    0, 0, 0, 0,

    0, 0,

    0, 0,

    0,
    pkey_tls1_prf_derive,
    pkey_tls1_prf_ctrl,
    pkey_tls1_prf_ctrl_str
};

static int tls1_prf_P_hash(const EVP_MD *md,
                           const unsigned char *sec, size_t sec_len,
                           const unsigned char *seed, size_t seed_len,
                           unsigned char *out, size_t olen)
{
    int chunk;
    EVP_MD_CTX *ctx = NULL, *ctx_tmp = NULL, *ctx_init = NULL;
    EVP_PKEY *mac_key = NULL;
    unsigned char A1[EVP_MAX_MD_SIZE];
    size_t A1_len;
    int ret = 0;

    chunk = EVP_MD_size(md);
    if (!ossl_assert(chunk > 0))
        goto err;

    ctx = EVP_MD_CTX_new();
    ctx_tmp = EVP_MD_CTX_new();
    ctx_init = EVP_MD_CTX_new();
    if (ctx == NULL || ctx_tmp == NULL || ctx_init == NULL)
        goto err;
    EVP_MD_CTX_set_flags(ctx_init, EVP_MD_CTX_FLAG_NON_FIPS_ALLOW);
    mac_key = EVP_PKEY_new_raw_private_key(EVP_PKEY_HMAC, NULL, sec, sec_len);
    if (mac_key == NULL)
        goto err;
    if (!EVP_DigestSignInit(ctx_init, NULL, md, NULL, mac_key))
        goto err;
    if (!EVP_MD_CTX_copy_ex(ctx, ctx_init))
        goto err;
    if (seed != NULL && !EVP_DigestSignUpdate(ctx, seed, seed_len))
        goto err;
    if (!EVP_DigestSignFinal(ctx, A1, &A1_len))
        goto err;

    for (;;) {
        /* Reinit mac contexts */
        if (!EVP_MD_CTX_copy_ex(ctx, ctx_init))
            goto err;
        if (!EVP_DigestSignUpdate(ctx, A1, A1_len))
            goto err;
        if (olen > (size_t)chunk && !EVP_MD_CTX_copy_ex(ctx_tmp, ctx))
            goto err;
        if (seed && !EVP_DigestSignUpdate(ctx, seed, seed_len))
            goto err;

        if (olen > (size_t)chunk) {
            size_t mac_len;
            if (!EVP_DigestSignFinal(ctx, out, &mac_len))
                goto err;
            out += mac_len;
            olen -= mac_len;
            /* calc the next A1 value */
            if (!EVP_DigestSignFinal(ctx_tmp, A1, &A1_len))
                goto err;
        } else {                /* last one */

            if (!EVP_DigestSignFinal(ctx, A1, &A1_len))
                goto err;
            memcpy(out, A1, olen);
            break;
        }
    }
    ret = 1;
 err:
    EVP_PKEY_free(mac_key);
    EVP_MD_CTX_free(ctx);
    EVP_MD_CTX_free(ctx_tmp);
    EVP_MD_CTX_free(ctx_init);
    OPENSSL_cleanse(A1, sizeof(A1));
    return ret;
}

static int tls1_prf_alg(const EVP_MD *md,
                        const unsigned char *sec, size_t slen,
                        const unsigned char *seed, size_t seed_len,
                        unsigned char *out, size_t olen)
{

    if (EVP_MD_type(md) == NID_md5_sha1) {
        size_t i;
        unsigned char *tmp;
        if (!tls1_prf_P_hash(EVP_md5(), sec, slen/2 + (slen & 1),
                         seed, seed_len, out, olen))
            return 0;

        if ((tmp = OPENSSL_malloc(olen)) == NULL) {
            KDFerr(KDF_F_TLS1_PRF_ALG, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        if (!tls1_prf_P_hash(EVP_sha1(), sec + slen/2, slen/2 + (slen & 1),
                         seed, seed_len, tmp, olen)) {
            OPENSSL_clear_free(tmp, olen);
            return 0;
        }
        for (i = 0; i < olen; i++)
            out[i] ^= tmp[i];
        OPENSSL_clear_free(tmp, olen);
        return 1;
    }
    if (!tls1_prf_P_hash(md, sec, slen, seed, seed_len, out, olen))
        return 0;

    return 1;
}
