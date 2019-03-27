/*
 * Copyright 2006-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "internal/cryptlib.h"
#include <openssl/objects.h>
#include <openssl/ts.h>
#include "ts_lcl.h"

TS_VERIFY_CTX *TS_VERIFY_CTX_new(void)
{
    TS_VERIFY_CTX *ctx = OPENSSL_zalloc(sizeof(*ctx));

    if (ctx == NULL)
        TSerr(TS_F_TS_VERIFY_CTX_NEW, ERR_R_MALLOC_FAILURE);
    return ctx;
}

void TS_VERIFY_CTX_init(TS_VERIFY_CTX *ctx)
{
    OPENSSL_assert(ctx != NULL);
    memset(ctx, 0, sizeof(*ctx));
}

void TS_VERIFY_CTX_free(TS_VERIFY_CTX *ctx)
{
    if (!ctx)
        return;

    TS_VERIFY_CTX_cleanup(ctx);
    OPENSSL_free(ctx);
}

int TS_VERIFY_CTX_add_flags(TS_VERIFY_CTX *ctx, int f)
{
    ctx->flags |= f;
    return ctx->flags;
}

int TS_VERIFY_CTX_set_flags(TS_VERIFY_CTX *ctx, int f)
{
    ctx->flags = f;
    return ctx->flags;
}

BIO *TS_VERIFY_CTX_set_data(TS_VERIFY_CTX *ctx, BIO *b)
{
    ctx->data = b;
    return ctx->data;
}

X509_STORE *TS_VERIFY_CTX_set_store(TS_VERIFY_CTX *ctx, X509_STORE *s)
{
    ctx->store = s;
    return ctx->store;
}

STACK_OF(X509) *TS_VERIFY_CTS_set_certs(TS_VERIFY_CTX *ctx,
                                        STACK_OF(X509) *certs)
{
    ctx->certs = certs;
    return ctx->certs;
}

unsigned char *TS_VERIFY_CTX_set_imprint(TS_VERIFY_CTX *ctx,
                                         unsigned char *hexstr, long len)
{
    ctx->imprint = hexstr;
    ctx->imprint_len = len;
    return ctx->imprint;
}

void TS_VERIFY_CTX_cleanup(TS_VERIFY_CTX *ctx)
{
    if (!ctx)
        return;

    X509_STORE_free(ctx->store);
    sk_X509_pop_free(ctx->certs, X509_free);

    ASN1_OBJECT_free(ctx->policy);

    X509_ALGOR_free(ctx->md_alg);
    OPENSSL_free(ctx->imprint);

    BIO_free_all(ctx->data);

    ASN1_INTEGER_free(ctx->nonce);

    GENERAL_NAME_free(ctx->tsa_name);

    TS_VERIFY_CTX_init(ctx);
}

TS_VERIFY_CTX *TS_REQ_to_TS_VERIFY_CTX(TS_REQ *req, TS_VERIFY_CTX *ctx)
{
    TS_VERIFY_CTX *ret = ctx;
    ASN1_OBJECT *policy;
    TS_MSG_IMPRINT *imprint;
    X509_ALGOR *md_alg;
    ASN1_OCTET_STRING *msg;
    const ASN1_INTEGER *nonce;

    OPENSSL_assert(req != NULL);
    if (ret)
        TS_VERIFY_CTX_cleanup(ret);
    else if ((ret = TS_VERIFY_CTX_new()) == NULL)
        return NULL;

    ret->flags = TS_VFY_ALL_IMPRINT & ~(TS_VFY_TSA_NAME | TS_VFY_SIGNATURE);

    if ((policy = req->policy_id) != NULL) {
        if ((ret->policy = OBJ_dup(policy)) == NULL)
            goto err;
    } else
        ret->flags &= ~TS_VFY_POLICY;

    imprint = req->msg_imprint;
    md_alg = imprint->hash_algo;
    if ((ret->md_alg = X509_ALGOR_dup(md_alg)) == NULL)
        goto err;
    msg = imprint->hashed_msg;
    ret->imprint_len = ASN1_STRING_length(msg);
    if ((ret->imprint = OPENSSL_malloc(ret->imprint_len)) == NULL)
        goto err;
    memcpy(ret->imprint, ASN1_STRING_get0_data(msg), ret->imprint_len);

    if ((nonce = req->nonce) != NULL) {
        if ((ret->nonce = ASN1_INTEGER_dup(nonce)) == NULL)
            goto err;
    } else
        ret->flags &= ~TS_VFY_NONCE;

    return ret;
 err:
    if (ctx)
        TS_VERIFY_CTX_cleanup(ctx);
    else
        TS_VERIFY_CTX_free(ret);
    return NULL;
}
