/*
 * Copyright 2006-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "e_os.h"
#include "internal/cryptlib.h"

#include <openssl/objects.h>
#include <openssl/ts.h>
#include <openssl/pkcs7.h>
#include <openssl/crypto.h>
#include "ts_lcl.h"

static ASN1_INTEGER *def_serial_cb(struct TS_resp_ctx *, void *);
static int def_time_cb(struct TS_resp_ctx *, void *, long *sec, long *usec);
static int def_extension_cb(struct TS_resp_ctx *, X509_EXTENSION *, void *);

static void ts_RESP_CTX_init(TS_RESP_CTX *ctx);
static void ts_RESP_CTX_cleanup(TS_RESP_CTX *ctx);
static int ts_RESP_check_request(TS_RESP_CTX *ctx);
static ASN1_OBJECT *ts_RESP_get_policy(TS_RESP_CTX *ctx);
static TS_TST_INFO *ts_RESP_create_tst_info(TS_RESP_CTX *ctx,
                                            ASN1_OBJECT *policy);
static int ts_RESP_process_extensions(TS_RESP_CTX *ctx);
static int ts_RESP_sign(TS_RESP_CTX *ctx);

static ESS_SIGNING_CERT *ess_SIGNING_CERT_new_init(X509 *signcert,
                                                   STACK_OF(X509) *certs);
static ESS_CERT_ID *ess_CERT_ID_new_init(X509 *cert, int issuer_needed);
static int ts_TST_INFO_content_new(PKCS7 *p7);
static int ess_add_signing_cert(PKCS7_SIGNER_INFO *si, ESS_SIGNING_CERT *sc);

static ESS_SIGNING_CERT_V2 *ess_signing_cert_v2_new_init(const EVP_MD *hash_alg,
                                                         X509 *signcert,
                                                         STACK_OF(X509)
                                                         *certs);
static ESS_CERT_ID_V2 *ess_cert_id_v2_new_init(const EVP_MD *hash_alg,
                                               X509 *cert, int issuer_needed);
static int ess_add_signing_cert_v2(PKCS7_SIGNER_INFO *si,
                                   ESS_SIGNING_CERT_V2 *sc);

static ASN1_GENERALIZEDTIME
*TS_RESP_set_genTime_with_precision(ASN1_GENERALIZEDTIME *, long, long,
                                    unsigned);

/* Default callback for response generation. */
static ASN1_INTEGER *def_serial_cb(struct TS_resp_ctx *ctx, void *data)
{
    ASN1_INTEGER *serial = ASN1_INTEGER_new();

    if (serial == NULL)
        goto err;
    if (!ASN1_INTEGER_set(serial, 1))
        goto err;
    return serial;

 err:
    TSerr(TS_F_DEF_SERIAL_CB, ERR_R_MALLOC_FAILURE);
    TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
                                "Error during serial number generation.");
    return NULL;
}

#if defined(OPENSSL_SYS_UNIX)

static int def_time_cb(struct TS_resp_ctx *ctx, void *data,
                       long *sec, long *usec)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        TSerr(TS_F_DEF_TIME_CB, TS_R_TIME_SYSCALL_ERROR);
        TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
                                    "Time is not available.");
        TS_RESP_CTX_add_failure_info(ctx, TS_INFO_TIME_NOT_AVAILABLE);
        return 0;
    }
    *sec = tv.tv_sec;
    *usec = tv.tv_usec;

    return 1;
}

#else

static int def_time_cb(struct TS_resp_ctx *ctx, void *data,
                       long *sec, long *usec)
{
    time_t t;
    if (time(&t) == (time_t)-1) {
        TSerr(TS_F_DEF_TIME_CB, TS_R_TIME_SYSCALL_ERROR);
        TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
                                    "Time is not available.");
        TS_RESP_CTX_add_failure_info(ctx, TS_INFO_TIME_NOT_AVAILABLE);
        return 0;
    }
    *sec = (long)t;
    *usec = 0;

    return 1;
}

#endif

static int def_extension_cb(struct TS_resp_ctx *ctx, X509_EXTENSION *ext,
                            void *data)
{
    TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
                                "Unsupported extension.");
    TS_RESP_CTX_add_failure_info(ctx, TS_INFO_UNACCEPTED_EXTENSION);
    return 0;
}

/* TS_RESP_CTX management functions. */

TS_RESP_CTX *TS_RESP_CTX_new(void)
{
    TS_RESP_CTX *ctx;

    if ((ctx = OPENSSL_zalloc(sizeof(*ctx))) == NULL) {
        TSerr(TS_F_TS_RESP_CTX_NEW, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    ctx->signer_md = EVP_sha256();

    ctx->serial_cb = def_serial_cb;
    ctx->time_cb = def_time_cb;
    ctx->extension_cb = def_extension_cb;

    return ctx;
}

void TS_RESP_CTX_free(TS_RESP_CTX *ctx)
{
    if (!ctx)
        return;

    X509_free(ctx->signer_cert);
    EVP_PKEY_free(ctx->signer_key);
    sk_X509_pop_free(ctx->certs, X509_free);
    sk_ASN1_OBJECT_pop_free(ctx->policies, ASN1_OBJECT_free);
    ASN1_OBJECT_free(ctx->default_policy);
    sk_EVP_MD_free(ctx->mds);   /* No EVP_MD_free method exists. */
    ASN1_INTEGER_free(ctx->seconds);
    ASN1_INTEGER_free(ctx->millis);
    ASN1_INTEGER_free(ctx->micros);
    OPENSSL_free(ctx);
}

int TS_RESP_CTX_set_signer_cert(TS_RESP_CTX *ctx, X509 *signer)
{
    if (X509_check_purpose(signer, X509_PURPOSE_TIMESTAMP_SIGN, 0) != 1) {
        TSerr(TS_F_TS_RESP_CTX_SET_SIGNER_CERT,
              TS_R_INVALID_SIGNER_CERTIFICATE_PURPOSE);
        return 0;
    }
    X509_free(ctx->signer_cert);
    ctx->signer_cert = signer;
    X509_up_ref(ctx->signer_cert);
    return 1;
}

int TS_RESP_CTX_set_signer_key(TS_RESP_CTX *ctx, EVP_PKEY *key)
{
    EVP_PKEY_free(ctx->signer_key);
    ctx->signer_key = key;
    EVP_PKEY_up_ref(ctx->signer_key);

    return 1;
}

int TS_RESP_CTX_set_signer_digest(TS_RESP_CTX *ctx, const EVP_MD *md)
{
    ctx->signer_md = md;
    return 1;
}

int TS_RESP_CTX_set_def_policy(TS_RESP_CTX *ctx, const ASN1_OBJECT *def_policy)
{
    ASN1_OBJECT_free(ctx->default_policy);
    if ((ctx->default_policy = OBJ_dup(def_policy)) == NULL)
        goto err;
    return 1;
 err:
    TSerr(TS_F_TS_RESP_CTX_SET_DEF_POLICY, ERR_R_MALLOC_FAILURE);
    return 0;
}

int TS_RESP_CTX_set_certs(TS_RESP_CTX *ctx, STACK_OF(X509) *certs)
{

    sk_X509_pop_free(ctx->certs, X509_free);
    ctx->certs = NULL;
    if (!certs)
        return 1;
    if ((ctx->certs = X509_chain_up_ref(certs)) == NULL) {
        TSerr(TS_F_TS_RESP_CTX_SET_CERTS, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    return 1;
}

int TS_RESP_CTX_add_policy(TS_RESP_CTX *ctx, const ASN1_OBJECT *policy)
{
    ASN1_OBJECT *copy = NULL;

    if (ctx->policies == NULL
        && (ctx->policies = sk_ASN1_OBJECT_new_null()) == NULL)
        goto err;
    if ((copy = OBJ_dup(policy)) == NULL)
        goto err;
    if (!sk_ASN1_OBJECT_push(ctx->policies, copy))
        goto err;

    return 1;
 err:
    TSerr(TS_F_TS_RESP_CTX_ADD_POLICY, ERR_R_MALLOC_FAILURE);
    ASN1_OBJECT_free(copy);
    return 0;
}

int TS_RESP_CTX_add_md(TS_RESP_CTX *ctx, const EVP_MD *md)
{
    if (ctx->mds == NULL
        && (ctx->mds = sk_EVP_MD_new_null()) == NULL)
        goto err;
    if (!sk_EVP_MD_push(ctx->mds, md))
        goto err;

    return 1;
 err:
    TSerr(TS_F_TS_RESP_CTX_ADD_MD, ERR_R_MALLOC_FAILURE);
    return 0;
}

#define TS_RESP_CTX_accuracy_free(ctx)          \
        ASN1_INTEGER_free(ctx->seconds);        \
        ctx->seconds = NULL;                    \
        ASN1_INTEGER_free(ctx->millis);         \
        ctx->millis = NULL;                     \
        ASN1_INTEGER_free(ctx->micros);         \
        ctx->micros = NULL;

int TS_RESP_CTX_set_accuracy(TS_RESP_CTX *ctx,
                             int secs, int millis, int micros)
{

    TS_RESP_CTX_accuracy_free(ctx);
    if (secs
        && ((ctx->seconds = ASN1_INTEGER_new()) == NULL
            || !ASN1_INTEGER_set(ctx->seconds, secs)))
        goto err;
    if (millis
        && ((ctx->millis = ASN1_INTEGER_new()) == NULL
            || !ASN1_INTEGER_set(ctx->millis, millis)))
        goto err;
    if (micros
        && ((ctx->micros = ASN1_INTEGER_new()) == NULL
            || !ASN1_INTEGER_set(ctx->micros, micros)))
        goto err;

    return 1;
 err:
    TS_RESP_CTX_accuracy_free(ctx);
    TSerr(TS_F_TS_RESP_CTX_SET_ACCURACY, ERR_R_MALLOC_FAILURE);
    return 0;
}

void TS_RESP_CTX_add_flags(TS_RESP_CTX *ctx, int flags)
{
    ctx->flags |= flags;
}

void TS_RESP_CTX_set_serial_cb(TS_RESP_CTX *ctx, TS_serial_cb cb, void *data)
{
    ctx->serial_cb = cb;
    ctx->serial_cb_data = data;
}

void TS_RESP_CTX_set_time_cb(TS_RESP_CTX *ctx, TS_time_cb cb, void *data)
{
    ctx->time_cb = cb;
    ctx->time_cb_data = data;
}

void TS_RESP_CTX_set_extension_cb(TS_RESP_CTX *ctx,
                                  TS_extension_cb cb, void *data)
{
    ctx->extension_cb = cb;
    ctx->extension_cb_data = data;
}

int TS_RESP_CTX_set_status_info(TS_RESP_CTX *ctx,
                                int status, const char *text)
{
    TS_STATUS_INFO *si = NULL;
    ASN1_UTF8STRING *utf8_text = NULL;
    int ret = 0;

    if ((si = TS_STATUS_INFO_new()) == NULL)
        goto err;
    if (!ASN1_INTEGER_set(si->status, status))
        goto err;
    if (text) {
        if ((utf8_text = ASN1_UTF8STRING_new()) == NULL
            || !ASN1_STRING_set(utf8_text, text, strlen(text)))
            goto err;
        if (si->text == NULL
            && (si->text = sk_ASN1_UTF8STRING_new_null()) == NULL)
            goto err;
        if (!sk_ASN1_UTF8STRING_push(si->text, utf8_text))
            goto err;
        utf8_text = NULL;       /* Ownership is lost. */
    }
    if (!TS_RESP_set_status_info(ctx->response, si))
        goto err;
    ret = 1;
 err:
    if (!ret)
        TSerr(TS_F_TS_RESP_CTX_SET_STATUS_INFO, ERR_R_MALLOC_FAILURE);
    TS_STATUS_INFO_free(si);
    ASN1_UTF8STRING_free(utf8_text);
    return ret;
}

int TS_RESP_CTX_set_status_info_cond(TS_RESP_CTX *ctx,
                                     int status, const char *text)
{
    int ret = 1;
    TS_STATUS_INFO *si = ctx->response->status_info;

    if (ASN1_INTEGER_get(si->status) == TS_STATUS_GRANTED) {
        ret = TS_RESP_CTX_set_status_info(ctx, status, text);
    }
    return ret;
}

int TS_RESP_CTX_add_failure_info(TS_RESP_CTX *ctx, int failure)
{
    TS_STATUS_INFO *si = ctx->response->status_info;
    if (si->failure_info == NULL
        && (si->failure_info = ASN1_BIT_STRING_new()) == NULL)
        goto err;
    if (!ASN1_BIT_STRING_set_bit(si->failure_info, failure, 1))
        goto err;
    return 1;
 err:
    TSerr(TS_F_TS_RESP_CTX_ADD_FAILURE_INFO, ERR_R_MALLOC_FAILURE);
    return 0;
}

TS_REQ *TS_RESP_CTX_get_request(TS_RESP_CTX *ctx)
{
    return ctx->request;
}

TS_TST_INFO *TS_RESP_CTX_get_tst_info(TS_RESP_CTX *ctx)
{
    return ctx->tst_info;
}

int TS_RESP_CTX_set_clock_precision_digits(TS_RESP_CTX *ctx,
                                           unsigned precision)
{
    if (precision > TS_MAX_CLOCK_PRECISION_DIGITS)
        return 0;
    ctx->clock_precision_digits = precision;
    return 1;
}

/* Main entry method of the response generation. */
TS_RESP *TS_RESP_create_response(TS_RESP_CTX *ctx, BIO *req_bio)
{
    ASN1_OBJECT *policy;
    TS_RESP *response;
    int result = 0;

    ts_RESP_CTX_init(ctx);

    if ((ctx->response = TS_RESP_new()) == NULL) {
        TSerr(TS_F_TS_RESP_CREATE_RESPONSE, ERR_R_MALLOC_FAILURE);
        goto end;
    }
    if ((ctx->request = d2i_TS_REQ_bio(req_bio, NULL)) == NULL) {
        TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
                                    "Bad request format or system error.");
        TS_RESP_CTX_add_failure_info(ctx, TS_INFO_BAD_DATA_FORMAT);
        goto end;
    }
    if (!TS_RESP_CTX_set_status_info(ctx, TS_STATUS_GRANTED, NULL))
        goto end;
    if (!ts_RESP_check_request(ctx))
        goto end;
    if ((policy = ts_RESP_get_policy(ctx)) == NULL)
        goto end;
    if ((ctx->tst_info = ts_RESP_create_tst_info(ctx, policy)) == NULL)
        goto end;
    if (!ts_RESP_process_extensions(ctx))
        goto end;
    if (!ts_RESP_sign(ctx))
        goto end;
    result = 1;

 end:
    if (!result) {
        TSerr(TS_F_TS_RESP_CREATE_RESPONSE, TS_R_RESPONSE_SETUP_ERROR);
        if (ctx->response != NULL) {
            if (TS_RESP_CTX_set_status_info_cond(ctx,
                                                 TS_STATUS_REJECTION,
                                                 "Error during response "
                                                 "generation.") == 0) {
                TS_RESP_free(ctx->response);
                ctx->response = NULL;
            }
        }
    }
    response = ctx->response;
    ctx->response = NULL;       /* Ownership will be returned to caller. */
    ts_RESP_CTX_cleanup(ctx);
    return response;
}

/* Initializes the variable part of the context. */
static void ts_RESP_CTX_init(TS_RESP_CTX *ctx)
{
    ctx->request = NULL;
    ctx->response = NULL;
    ctx->tst_info = NULL;
}

/* Cleans up the variable part of the context. */
static void ts_RESP_CTX_cleanup(TS_RESP_CTX *ctx)
{
    TS_REQ_free(ctx->request);
    ctx->request = NULL;
    TS_RESP_free(ctx->response);
    ctx->response = NULL;
    TS_TST_INFO_free(ctx->tst_info);
    ctx->tst_info = NULL;
}

/* Checks the format and content of the request. */
static int ts_RESP_check_request(TS_RESP_CTX *ctx)
{
    TS_REQ *request = ctx->request;
    TS_MSG_IMPRINT *msg_imprint;
    X509_ALGOR *md_alg;
    int md_alg_id;
    const ASN1_OCTET_STRING *digest;
    const EVP_MD *md = NULL;
    int i;

    if (TS_REQ_get_version(request) != 1) {
        TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
                                    "Bad request version.");
        TS_RESP_CTX_add_failure_info(ctx, TS_INFO_BAD_REQUEST);
        return 0;
    }

    msg_imprint = request->msg_imprint;
    md_alg = msg_imprint->hash_algo;
    md_alg_id = OBJ_obj2nid(md_alg->algorithm);
    for (i = 0; !md && i < sk_EVP_MD_num(ctx->mds); ++i) {
        const EVP_MD *current_md = sk_EVP_MD_value(ctx->mds, i);
        if (md_alg_id == EVP_MD_type(current_md))
            md = current_md;
    }
    if (!md) {
        TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
                                    "Message digest algorithm is "
                                    "not supported.");
        TS_RESP_CTX_add_failure_info(ctx, TS_INFO_BAD_ALG);
        return 0;
    }

    if (md_alg->parameter && ASN1_TYPE_get(md_alg->parameter) != V_ASN1_NULL) {
        TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
                                    "Superfluous message digest "
                                    "parameter.");
        TS_RESP_CTX_add_failure_info(ctx, TS_INFO_BAD_ALG);
        return 0;
    }
    digest = msg_imprint->hashed_msg;
    if (digest->length != EVP_MD_size(md)) {
        TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
                                    "Bad message digest.");
        TS_RESP_CTX_add_failure_info(ctx, TS_INFO_BAD_DATA_FORMAT);
        return 0;
    }

    return 1;
}

/* Returns the TSA policy based on the requested and acceptable policies. */
static ASN1_OBJECT *ts_RESP_get_policy(TS_RESP_CTX *ctx)
{
    ASN1_OBJECT *requested = ctx->request->policy_id;
    ASN1_OBJECT *policy = NULL;
    int i;

    if (ctx->default_policy == NULL) {
        TSerr(TS_F_TS_RESP_GET_POLICY, TS_R_INVALID_NULL_POINTER);
        return NULL;
    }
    if (!requested || !OBJ_cmp(requested, ctx->default_policy))
        policy = ctx->default_policy;

    /* Check if the policy is acceptable. */
    for (i = 0; !policy && i < sk_ASN1_OBJECT_num(ctx->policies); ++i) {
        ASN1_OBJECT *current = sk_ASN1_OBJECT_value(ctx->policies, i);
        if (!OBJ_cmp(requested, current))
            policy = current;
    }
    if (!policy) {
        TSerr(TS_F_TS_RESP_GET_POLICY, TS_R_UNACCEPTABLE_POLICY);
        TS_RESP_CTX_set_status_info(ctx, TS_STATUS_REJECTION,
                                    "Requested policy is not " "supported.");
        TS_RESP_CTX_add_failure_info(ctx, TS_INFO_UNACCEPTED_POLICY);
    }
    return policy;
}

/* Creates the TS_TST_INFO object based on the settings of the context. */
static TS_TST_INFO *ts_RESP_create_tst_info(TS_RESP_CTX *ctx,
                                            ASN1_OBJECT *policy)
{
    int result = 0;
    TS_TST_INFO *tst_info = NULL;
    ASN1_INTEGER *serial = NULL;
    ASN1_GENERALIZEDTIME *asn1_time = NULL;
    long sec, usec;
    TS_ACCURACY *accuracy = NULL;
    const ASN1_INTEGER *nonce;
    GENERAL_NAME *tsa_name = NULL;

    if ((tst_info = TS_TST_INFO_new()) == NULL)
        goto end;
    if (!TS_TST_INFO_set_version(tst_info, 1))
        goto end;
    if (!TS_TST_INFO_set_policy_id(tst_info, policy))
        goto end;
    if (!TS_TST_INFO_set_msg_imprint(tst_info, ctx->request->msg_imprint))
        goto end;
    if ((serial = ctx->serial_cb(ctx, ctx->serial_cb_data)) == NULL
        || !TS_TST_INFO_set_serial(tst_info, serial))
        goto end;
    if (!ctx->time_cb(ctx, ctx->time_cb_data, &sec, &usec)
        || (asn1_time =
            TS_RESP_set_genTime_with_precision(NULL, sec, usec,
                                        ctx->clock_precision_digits)) == NULL
        || !TS_TST_INFO_set_time(tst_info, asn1_time))
        goto end;

    if ((ctx->seconds || ctx->millis || ctx->micros)
        && (accuracy = TS_ACCURACY_new()) == NULL)
        goto end;
    if (ctx->seconds && !TS_ACCURACY_set_seconds(accuracy, ctx->seconds))
        goto end;
    if (ctx->millis && !TS_ACCURACY_set_millis(accuracy, ctx->millis))
        goto end;
    if (ctx->micros && !TS_ACCURACY_set_micros(accuracy, ctx->micros))
        goto end;
    if (accuracy && !TS_TST_INFO_set_accuracy(tst_info, accuracy))
        goto end;

    if ((ctx->flags & TS_ORDERING)
        && !TS_TST_INFO_set_ordering(tst_info, 1))
        goto end;

    if ((nonce = ctx->request->nonce) != NULL
        && !TS_TST_INFO_set_nonce(tst_info, nonce))
        goto end;

    if (ctx->flags & TS_TSA_NAME) {
        if ((tsa_name = GENERAL_NAME_new()) == NULL)
            goto end;
        tsa_name->type = GEN_DIRNAME;
        tsa_name->d.dirn =
            X509_NAME_dup(X509_get_subject_name(ctx->signer_cert));
        if (!tsa_name->d.dirn)
            goto end;
        if (!TS_TST_INFO_set_tsa(tst_info, tsa_name))
            goto end;
    }

    result = 1;
 end:
    if (!result) {
        TS_TST_INFO_free(tst_info);
        tst_info = NULL;
        TSerr(TS_F_TS_RESP_CREATE_TST_INFO, TS_R_TST_INFO_SETUP_ERROR);
        TS_RESP_CTX_set_status_info_cond(ctx, TS_STATUS_REJECTION,
                                         "Error during TSTInfo "
                                         "generation.");
    }
    GENERAL_NAME_free(tsa_name);
    TS_ACCURACY_free(accuracy);
    ASN1_GENERALIZEDTIME_free(asn1_time);
    ASN1_INTEGER_free(serial);

    return tst_info;
}

/* Processing the extensions of the request. */
static int ts_RESP_process_extensions(TS_RESP_CTX *ctx)
{
    STACK_OF(X509_EXTENSION) *exts = ctx->request->extensions;
    int i;
    int ok = 1;

    for (i = 0; ok && i < sk_X509_EXTENSION_num(exts); ++i) {
        X509_EXTENSION *ext = sk_X509_EXTENSION_value(exts, i);
        /*
         * The last argument was previously (void *)ctx->extension_cb,
         * but ISO C doesn't permit converting a function pointer to void *.
         * For lack of better information, I'm placing a NULL there instead.
         * The callback can pick its own address out from the ctx anyway...
         */
        ok = (*ctx->extension_cb) (ctx, ext, NULL);
    }

    return ok;
}

/* Functions for signing the TS_TST_INFO structure of the context. */
static int ts_RESP_sign(TS_RESP_CTX *ctx)
{
    int ret = 0;
    PKCS7 *p7 = NULL;
    PKCS7_SIGNER_INFO *si;
    STACK_OF(X509) *certs;      /* Certificates to include in sc. */
    ESS_SIGNING_CERT_V2 *sc2 = NULL;
    ESS_SIGNING_CERT *sc = NULL;
    ASN1_OBJECT *oid;
    BIO *p7bio = NULL;
    int i;

    if (!X509_check_private_key(ctx->signer_cert, ctx->signer_key)) {
        TSerr(TS_F_TS_RESP_SIGN, TS_R_PRIVATE_KEY_DOES_NOT_MATCH_CERTIFICATE);
        goto err;
    }

    if ((p7 = PKCS7_new()) == NULL) {
        TSerr(TS_F_TS_RESP_SIGN, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    if (!PKCS7_set_type(p7, NID_pkcs7_signed))
        goto err;
    if (!ASN1_INTEGER_set(p7->d.sign->version, 3))
        goto err;

    if (ctx->request->cert_req) {
        PKCS7_add_certificate(p7, ctx->signer_cert);
        if (ctx->certs) {
            for (i = 0; i < sk_X509_num(ctx->certs); ++i) {
                X509 *cert = sk_X509_value(ctx->certs, i);
                PKCS7_add_certificate(p7, cert);
            }
        }
    }

    if ((si = PKCS7_add_signature(p7, ctx->signer_cert,
                                  ctx->signer_key, ctx->signer_md)) == NULL) {
        TSerr(TS_F_TS_RESP_SIGN, TS_R_PKCS7_ADD_SIGNATURE_ERROR);
        goto err;
    }

    oid = OBJ_nid2obj(NID_id_smime_ct_TSTInfo);
    if (!PKCS7_add_signed_attribute(si, NID_pkcs9_contentType,
                                    V_ASN1_OBJECT, oid)) {
        TSerr(TS_F_TS_RESP_SIGN, TS_R_PKCS7_ADD_SIGNED_ATTR_ERROR);
        goto err;
    }

    certs = ctx->flags & TS_ESS_CERT_ID_CHAIN ? ctx->certs : NULL;
    if (ctx->ess_cert_id_digest == NULL
        || ctx->ess_cert_id_digest == EVP_sha1()) {
        if ((sc = ess_SIGNING_CERT_new_init(ctx->signer_cert, certs)) == NULL)
            goto err;

        if (!ess_add_signing_cert(si, sc)) {
            TSerr(TS_F_TS_RESP_SIGN, TS_R_ESS_ADD_SIGNING_CERT_ERROR);
            goto err;
        }
    } else {
        sc2 = ess_signing_cert_v2_new_init(ctx->ess_cert_id_digest,
                                           ctx->signer_cert, certs);
        if (sc2 == NULL)
            goto err;

        if (!ess_add_signing_cert_v2(si, sc2)) {
            TSerr(TS_F_TS_RESP_SIGN, TS_R_ESS_ADD_SIGNING_CERT_V2_ERROR);
            goto err;
        }
    }

    if (!ts_TST_INFO_content_new(p7))
        goto err;
    if ((p7bio = PKCS7_dataInit(p7, NULL)) == NULL) {
        TSerr(TS_F_TS_RESP_SIGN, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    if (!i2d_TS_TST_INFO_bio(p7bio, ctx->tst_info)) {
        TSerr(TS_F_TS_RESP_SIGN, TS_R_TS_DATASIGN);
        goto err;
    }
    if (!PKCS7_dataFinal(p7, p7bio)) {
        TSerr(TS_F_TS_RESP_SIGN, TS_R_TS_DATASIGN);
        goto err;
    }
    TS_RESP_set_tst_info(ctx->response, p7, ctx->tst_info);
    p7 = NULL;                  /* Ownership is lost. */
    ctx->tst_info = NULL;       /* Ownership is lost. */

    ret = 1;
 err:
    if (!ret)
        TS_RESP_CTX_set_status_info_cond(ctx, TS_STATUS_REJECTION,
                                         "Error during signature "
                                         "generation.");
    BIO_free_all(p7bio);
    ESS_SIGNING_CERT_V2_free(sc2);
    ESS_SIGNING_CERT_free(sc);
    PKCS7_free(p7);
    return ret;
}

static ESS_SIGNING_CERT *ess_SIGNING_CERT_new_init(X509 *signcert,
                                                   STACK_OF(X509) *certs)
{
    ESS_CERT_ID *cid;
    ESS_SIGNING_CERT *sc = NULL;
    int i;

    if ((sc = ESS_SIGNING_CERT_new()) == NULL)
        goto err;
    if (sc->cert_ids == NULL
        && (sc->cert_ids = sk_ESS_CERT_ID_new_null()) == NULL)
        goto err;

    if ((cid = ess_CERT_ID_new_init(signcert, 0)) == NULL
        || !sk_ESS_CERT_ID_push(sc->cert_ids, cid))
        goto err;
    for (i = 0; i < sk_X509_num(certs); ++i) {
        X509 *cert = sk_X509_value(certs, i);
        if ((cid = ess_CERT_ID_new_init(cert, 1)) == NULL
            || !sk_ESS_CERT_ID_push(sc->cert_ids, cid))
            goto err;
    }

    return sc;
 err:
    ESS_SIGNING_CERT_free(sc);
    TSerr(TS_F_ESS_SIGNING_CERT_NEW_INIT, ERR_R_MALLOC_FAILURE);
    return NULL;
}

static ESS_CERT_ID *ess_CERT_ID_new_init(X509 *cert, int issuer_needed)
{
    ESS_CERT_ID *cid = NULL;
    GENERAL_NAME *name = NULL;
    unsigned char cert_sha1[SHA_DIGEST_LENGTH];

    /* Call for side-effect of computing hash and caching extensions */
    X509_check_purpose(cert, -1, 0);
    if ((cid = ESS_CERT_ID_new()) == NULL)
        goto err;
    X509_digest(cert, EVP_sha1(), cert_sha1, NULL);
    if (!ASN1_OCTET_STRING_set(cid->hash, cert_sha1, SHA_DIGEST_LENGTH))
        goto err;

    /* Setting the issuer/serial if requested. */
    if (issuer_needed) {
        if (cid->issuer_serial == NULL
            && (cid->issuer_serial = ESS_ISSUER_SERIAL_new()) == NULL)
            goto err;
        if ((name = GENERAL_NAME_new()) == NULL)
            goto err;
        name->type = GEN_DIRNAME;
        if ((name->d.dirn = X509_NAME_dup(X509_get_issuer_name(cert))) == NULL)
            goto err;
        if (!sk_GENERAL_NAME_push(cid->issuer_serial->issuer, name))
            goto err;
        name = NULL;            /* Ownership is lost. */
        ASN1_INTEGER_free(cid->issuer_serial->serial);
        if (!(cid->issuer_serial->serial =
              ASN1_INTEGER_dup(X509_get_serialNumber(cert))))
            goto err;
    }

    return cid;
 err:
    GENERAL_NAME_free(name);
    ESS_CERT_ID_free(cid);
    TSerr(TS_F_ESS_CERT_ID_NEW_INIT, ERR_R_MALLOC_FAILURE);
    return NULL;
}

static int ts_TST_INFO_content_new(PKCS7 *p7)
{
    PKCS7 *ret = NULL;
    ASN1_OCTET_STRING *octet_string = NULL;

    /* Create new encapsulated NID_id_smime_ct_TSTInfo content. */
    if ((ret = PKCS7_new()) == NULL)
        goto err;
    if ((ret->d.other = ASN1_TYPE_new()) == NULL)
        goto err;
    ret->type = OBJ_nid2obj(NID_id_smime_ct_TSTInfo);
    if ((octet_string = ASN1_OCTET_STRING_new()) == NULL)
        goto err;
    ASN1_TYPE_set(ret->d.other, V_ASN1_OCTET_STRING, octet_string);
    octet_string = NULL;

    /* Add encapsulated content to signed PKCS7 structure. */
    if (!PKCS7_set_content(p7, ret))
        goto err;

    return 1;
 err:
    ASN1_OCTET_STRING_free(octet_string);
    PKCS7_free(ret);
    return 0;
}

static int ess_add_signing_cert(PKCS7_SIGNER_INFO *si, ESS_SIGNING_CERT *sc)
{
    ASN1_STRING *seq = NULL;
    unsigned char *p, *pp = NULL;
    int len;

    len = i2d_ESS_SIGNING_CERT(sc, NULL);
    if ((pp = OPENSSL_malloc(len)) == NULL) {
        TSerr(TS_F_ESS_ADD_SIGNING_CERT, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    p = pp;
    i2d_ESS_SIGNING_CERT(sc, &p);
    if ((seq = ASN1_STRING_new()) == NULL || !ASN1_STRING_set(seq, pp, len)) {
        TSerr(TS_F_ESS_ADD_SIGNING_CERT, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    OPENSSL_free(pp);
    pp = NULL;
    return PKCS7_add_signed_attribute(si,
                                      NID_id_smime_aa_signingCertificate,
                                      V_ASN1_SEQUENCE, seq);
 err:
    ASN1_STRING_free(seq);
    OPENSSL_free(pp);

    return 0;
}

static ESS_SIGNING_CERT_V2 *ess_signing_cert_v2_new_init(const EVP_MD *hash_alg,
                                                         X509 *signcert,
                                                         STACK_OF(X509) *certs)
{
    ESS_CERT_ID_V2 *cid = NULL;
    ESS_SIGNING_CERT_V2 *sc = NULL;
    int i;

    if ((sc = ESS_SIGNING_CERT_V2_new()) == NULL)
        goto err;
    if ((cid = ess_cert_id_v2_new_init(hash_alg, signcert, 0)) == NULL)
        goto err;
    if (!sk_ESS_CERT_ID_V2_push(sc->cert_ids, cid))
        goto err;
    cid = NULL;

    for (i = 0; i < sk_X509_num(certs); ++i) {
        X509 *cert = sk_X509_value(certs, i);

        if ((cid = ess_cert_id_v2_new_init(hash_alg, cert, 1)) == NULL)
            goto err;
        if (!sk_ESS_CERT_ID_V2_push(sc->cert_ids, cid))
            goto err;
        cid = NULL;
    }

    return sc;
 err:
    ESS_SIGNING_CERT_V2_free(sc);
    ESS_CERT_ID_V2_free(cid);
    TSerr(TS_F_ESS_SIGNING_CERT_V2_NEW_INIT, ERR_R_MALLOC_FAILURE);
    return NULL;
}

static ESS_CERT_ID_V2 *ess_cert_id_v2_new_init(const EVP_MD *hash_alg,
                                               X509 *cert, int issuer_needed)
{
    ESS_CERT_ID_V2 *cid = NULL;
    GENERAL_NAME *name = NULL;
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = sizeof(hash);
    X509_ALGOR *alg = NULL;

    memset(hash, 0, sizeof(hash));

    if ((cid = ESS_CERT_ID_V2_new()) == NULL)
        goto err;

    if (hash_alg != EVP_sha256()) {
        alg = X509_ALGOR_new();
        if (alg == NULL)
            goto err;
        X509_ALGOR_set_md(alg, hash_alg);
        if (alg->algorithm == NULL)
            goto err;
        cid->hash_alg = alg;
        alg = NULL;
    } else {
        cid->hash_alg = NULL;
    }

    if (!X509_digest(cert, hash_alg, hash, &hash_len))
        goto err;

    if (!ASN1_OCTET_STRING_set(cid->hash, hash, hash_len))
        goto err;

    if (issuer_needed) {
        if ((cid->issuer_serial = ESS_ISSUER_SERIAL_new()) == NULL)
            goto err;
        if ((name = GENERAL_NAME_new()) == NULL)
            goto err;
        name->type = GEN_DIRNAME;
        if ((name->d.dirn = X509_NAME_dup(X509_get_issuer_name(cert))) == NULL)
            goto err;
        if (!sk_GENERAL_NAME_push(cid->issuer_serial->issuer, name))
            goto err;
        name = NULL;            /* Ownership is lost. */
        ASN1_INTEGER_free(cid->issuer_serial->serial);
        cid->issuer_serial->serial =
                ASN1_INTEGER_dup(X509_get_serialNumber(cert));
        if (cid->issuer_serial->serial == NULL)
            goto err;
    }

    return cid;
 err:
    X509_ALGOR_free(alg);
    GENERAL_NAME_free(name);
    ESS_CERT_ID_V2_free(cid);
    TSerr(TS_F_ESS_CERT_ID_V2_NEW_INIT, ERR_R_MALLOC_FAILURE);
    return NULL;
}

static int ess_add_signing_cert_v2(PKCS7_SIGNER_INFO *si,
                                   ESS_SIGNING_CERT_V2 *sc)
{
    ASN1_STRING *seq = NULL;
    unsigned char *p, *pp = NULL;
    int len = i2d_ESS_SIGNING_CERT_V2(sc, NULL);

    if ((pp = OPENSSL_malloc(len)) == NULL) {
        TSerr(TS_F_ESS_ADD_SIGNING_CERT_V2, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    p = pp;
    i2d_ESS_SIGNING_CERT_V2(sc, &p);
    if ((seq = ASN1_STRING_new()) == NULL || !ASN1_STRING_set(seq, pp, len)) {
        TSerr(TS_F_ESS_ADD_SIGNING_CERT_V2, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    OPENSSL_free(pp);
    pp = NULL;
    return PKCS7_add_signed_attribute(si,
                                      NID_id_smime_aa_signingCertificateV2,
                                      V_ASN1_SEQUENCE, seq);
 err:
    ASN1_STRING_free(seq);
    OPENSSL_free(pp);
    return 0;
}

static ASN1_GENERALIZEDTIME *TS_RESP_set_genTime_with_precision(
        ASN1_GENERALIZEDTIME *asn1_time, long sec, long usec,
        unsigned precision)
{
    time_t time_sec = (time_t)sec;
    struct tm *tm = NULL, tm_result;
    char genTime_str[17 + TS_MAX_CLOCK_PRECISION_DIGITS];
    char *p = genTime_str;
    char *p_end = genTime_str + sizeof(genTime_str);

    if (precision > TS_MAX_CLOCK_PRECISION_DIGITS)
        goto err;

    if ((tm = OPENSSL_gmtime(&time_sec, &tm_result)) == NULL)
        goto err;

    /*
     * Put "genTime_str" in GeneralizedTime format.  We work around the
     * restrictions imposed by rfc3280 (i.e. "GeneralizedTime values MUST
     * NOT include fractional seconds") and OpenSSL related functions to
     * meet the rfc3161 requirement: "GeneralizedTime syntax can include
     * fraction-of-second details".
     */
    p += BIO_snprintf(p, p_end - p,
                      "%04d%02d%02d%02d%02d%02d",
                      tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                      tm->tm_hour, tm->tm_min, tm->tm_sec);
    if (precision > 0) {
        BIO_snprintf(p, 2 + precision, ".%06ld", usec);
        p += strlen(p);

        /*
         * To make things a bit harder, X.690 | ISO/IEC 8825-1 provides the
         * following restrictions for a DER-encoding, which OpenSSL
         * (specifically ASN1_GENERALIZEDTIME_check() function) doesn't
         * support: "The encoding MUST terminate with a "Z" (which means
         * "Zulu" time). The decimal point element, if present, MUST be the
         * point option ".". The fractional-seconds elements, if present,
         * MUST omit all trailing 0's; if the elements correspond to 0, they
         * MUST be wholly omitted, and the decimal point element also MUST be
         * omitted."
         */
        /*
         * Remove trailing zeros. The dot guarantees the exit condition of
         * this loop even if all the digits are zero.
         */
        while (*--p == '0')
             continue;
        if (*p != '.')
            ++p;
    }
    *p++ = 'Z';
    *p++ = '\0';

    if (asn1_time == NULL
        && (asn1_time = ASN1_GENERALIZEDTIME_new()) == NULL)
        goto err;
    if (!ASN1_GENERALIZEDTIME_set_string(asn1_time, genTime_str)) {
        ASN1_GENERALIZEDTIME_free(asn1_time);
        goto err;
    }
    return asn1_time;

 err:
    TSerr(TS_F_TS_RESP_SET_GENTIME_WITH_PRECISION, TS_R_COULD_NOT_SET_TIME);
    return NULL;
}

int TS_RESP_CTX_set_ess_cert_id_digest(TS_RESP_CTX *ctx, const EVP_MD *md)
{
    ctx->ess_cert_id_digest = md;
    return 1;
}
