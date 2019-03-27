/*
 * Copyright 2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifdef OPENSSL_NO_CT
# error "CT is disabled"
#endif

#include <openssl/ct.h>
#include <openssl/err.h>
#include <time.h>

#include "ct_locl.h"

/*
 * Number of seconds in the future that an SCT timestamp can be, by default,
 * without being considered invalid. This is added to time() when setting a
 * default value for CT_POLICY_EVAL_CTX.epoch_time_in_ms.
 * It can be overridden by calling CT_POLICY_EVAL_CTX_set_time().
 */
static const time_t SCT_CLOCK_DRIFT_TOLERANCE = 300;

CT_POLICY_EVAL_CTX *CT_POLICY_EVAL_CTX_new(void)
{
    CT_POLICY_EVAL_CTX *ctx = OPENSSL_zalloc(sizeof(CT_POLICY_EVAL_CTX));

    if (ctx == NULL) {
        CTerr(CT_F_CT_POLICY_EVAL_CTX_NEW, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    /* time(NULL) shouldn't ever fail, so don't bother checking for -1. */
    ctx->epoch_time_in_ms = (uint64_t)(time(NULL) + SCT_CLOCK_DRIFT_TOLERANCE) *
            1000;

    return ctx;
}

void CT_POLICY_EVAL_CTX_free(CT_POLICY_EVAL_CTX *ctx)
{
    if (ctx == NULL)
        return;
    X509_free(ctx->cert);
    X509_free(ctx->issuer);
    OPENSSL_free(ctx);
}

int CT_POLICY_EVAL_CTX_set1_cert(CT_POLICY_EVAL_CTX *ctx, X509 *cert)
{
    if (!X509_up_ref(cert))
        return 0;
    ctx->cert = cert;
    return 1;
}

int CT_POLICY_EVAL_CTX_set1_issuer(CT_POLICY_EVAL_CTX *ctx, X509 *issuer)
{
    if (!X509_up_ref(issuer))
        return 0;
    ctx->issuer = issuer;
    return 1;
}

void CT_POLICY_EVAL_CTX_set_shared_CTLOG_STORE(CT_POLICY_EVAL_CTX *ctx,
                                               CTLOG_STORE *log_store)
{
    ctx->log_store = log_store;
}

void CT_POLICY_EVAL_CTX_set_time(CT_POLICY_EVAL_CTX *ctx, uint64_t time_in_ms)
{
    ctx->epoch_time_in_ms = time_in_ms;
}

X509* CT_POLICY_EVAL_CTX_get0_cert(const CT_POLICY_EVAL_CTX *ctx)
{
    return ctx->cert;
}

X509* CT_POLICY_EVAL_CTX_get0_issuer(const CT_POLICY_EVAL_CTX *ctx)
{
    return ctx->issuer;
}

const CTLOG_STORE *CT_POLICY_EVAL_CTX_get0_log_store(const CT_POLICY_EVAL_CTX *ctx)
{
    return ctx->log_store;
}

uint64_t CT_POLICY_EVAL_CTX_get_time(const CT_POLICY_EVAL_CTX *ctx)
{
    return ctx->epoch_time_in_ms;
}
