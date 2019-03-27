/*
 * Copyright 2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#ifdef OPENSSL_NO_CT
# error "CT disabled"
#endif

#include <openssl/ct.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/tls1.h>
#include <openssl/x509.h>

#include "ct_locl.h"

SCT *SCT_new(void)
{
    SCT *sct = OPENSSL_zalloc(sizeof(*sct));

    if (sct == NULL) {
        CTerr(CT_F_SCT_NEW, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    sct->entry_type = CT_LOG_ENTRY_TYPE_NOT_SET;
    sct->version = SCT_VERSION_NOT_SET;
    return sct;
}

void SCT_free(SCT *sct)
{
    if (sct == NULL)
        return;

    OPENSSL_free(sct->log_id);
    OPENSSL_free(sct->ext);
    OPENSSL_free(sct->sig);
    OPENSSL_free(sct->sct);
    OPENSSL_free(sct);
}

void SCT_LIST_free(STACK_OF(SCT) *a)
{
    sk_SCT_pop_free(a, SCT_free);
}

int SCT_set_version(SCT *sct, sct_version_t version)
{
    if (version != SCT_VERSION_V1) {
        CTerr(CT_F_SCT_SET_VERSION, CT_R_UNSUPPORTED_VERSION);
        return 0;
    }
    sct->version = version;
    sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;
    return 1;
}

int SCT_set_log_entry_type(SCT *sct, ct_log_entry_type_t entry_type)
{
    sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;

    switch (entry_type) {
    case CT_LOG_ENTRY_TYPE_X509:
    case CT_LOG_ENTRY_TYPE_PRECERT:
        sct->entry_type = entry_type;
        return 1;
    case CT_LOG_ENTRY_TYPE_NOT_SET:
        break;
    }
    CTerr(CT_F_SCT_SET_LOG_ENTRY_TYPE, CT_R_UNSUPPORTED_ENTRY_TYPE);
    return 0;
}

int SCT_set0_log_id(SCT *sct, unsigned char *log_id, size_t log_id_len)
{
    if (sct->version == SCT_VERSION_V1 && log_id_len != CT_V1_HASHLEN) {
        CTerr(CT_F_SCT_SET0_LOG_ID, CT_R_INVALID_LOG_ID_LENGTH);
        return 0;
    }

    OPENSSL_free(sct->log_id);
    sct->log_id = log_id;
    sct->log_id_len = log_id_len;
    sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;
    return 1;
}

int SCT_set1_log_id(SCT *sct, const unsigned char *log_id, size_t log_id_len)
{
    if (sct->version == SCT_VERSION_V1 && log_id_len != CT_V1_HASHLEN) {
        CTerr(CT_F_SCT_SET1_LOG_ID, CT_R_INVALID_LOG_ID_LENGTH);
        return 0;
    }

    OPENSSL_free(sct->log_id);
    sct->log_id = NULL;
    sct->log_id_len = 0;
    sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;

    if (log_id != NULL && log_id_len > 0) {
        sct->log_id = OPENSSL_memdup(log_id, log_id_len);
        if (sct->log_id == NULL) {
            CTerr(CT_F_SCT_SET1_LOG_ID, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        sct->log_id_len = log_id_len;
    }
    return 1;
}


void SCT_set_timestamp(SCT *sct, uint64_t timestamp)
{
    sct->timestamp = timestamp;
    sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;
}

int SCT_set_signature_nid(SCT *sct, int nid)
{
    switch (nid) {
    case NID_sha256WithRSAEncryption:
        sct->hash_alg = TLSEXT_hash_sha256;
        sct->sig_alg = TLSEXT_signature_rsa;
        sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;
        return 1;
    case NID_ecdsa_with_SHA256:
        sct->hash_alg = TLSEXT_hash_sha256;
        sct->sig_alg = TLSEXT_signature_ecdsa;
        sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;
        return 1;
    default:
        CTerr(CT_F_SCT_SET_SIGNATURE_NID, CT_R_UNRECOGNIZED_SIGNATURE_NID);
        return 0;
    }
}

void SCT_set0_extensions(SCT *sct, unsigned char *ext, size_t ext_len)
{
    OPENSSL_free(sct->ext);
    sct->ext = ext;
    sct->ext_len = ext_len;
    sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;
}

int SCT_set1_extensions(SCT *sct, const unsigned char *ext, size_t ext_len)
{
    OPENSSL_free(sct->ext);
    sct->ext = NULL;
    sct->ext_len = 0;
    sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;

    if (ext != NULL && ext_len > 0) {
        sct->ext = OPENSSL_memdup(ext, ext_len);
        if (sct->ext == NULL) {
            CTerr(CT_F_SCT_SET1_EXTENSIONS, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        sct->ext_len = ext_len;
    }
    return 1;
}

void SCT_set0_signature(SCT *sct, unsigned char *sig, size_t sig_len)
{
    OPENSSL_free(sct->sig);
    sct->sig = sig;
    sct->sig_len = sig_len;
    sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;
}

int SCT_set1_signature(SCT *sct, const unsigned char *sig, size_t sig_len)
{
    OPENSSL_free(sct->sig);
    sct->sig = NULL;
    sct->sig_len = 0;
    sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;

    if (sig != NULL && sig_len > 0) {
        sct->sig = OPENSSL_memdup(sig, sig_len);
        if (sct->sig == NULL) {
            CTerr(CT_F_SCT_SET1_SIGNATURE, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        sct->sig_len = sig_len;
    }
    return 1;
}

sct_version_t SCT_get_version(const SCT *sct)
{
    return sct->version;
}

ct_log_entry_type_t SCT_get_log_entry_type(const SCT *sct)
{
    return sct->entry_type;
}

size_t SCT_get0_log_id(const SCT *sct, unsigned char **log_id)
{
    *log_id = sct->log_id;
    return sct->log_id_len;
}

uint64_t SCT_get_timestamp(const SCT *sct)
{
    return sct->timestamp;
}

int SCT_get_signature_nid(const SCT *sct)
{
    if (sct->version == SCT_VERSION_V1) {
        if (sct->hash_alg == TLSEXT_hash_sha256) {
            switch (sct->sig_alg) {
            case TLSEXT_signature_ecdsa:
                return NID_ecdsa_with_SHA256;
            case TLSEXT_signature_rsa:
                return NID_sha256WithRSAEncryption;
            default:
                return NID_undef;
            }
        }
    }
    return NID_undef;
}

size_t SCT_get0_extensions(const SCT *sct, unsigned char **ext)
{
    *ext = sct->ext;
    return sct->ext_len;
}

size_t SCT_get0_signature(const SCT *sct, unsigned char **sig)
{
    *sig = sct->sig;
    return sct->sig_len;
}

int SCT_is_complete(const SCT *sct)
{
    switch (sct->version) {
    case SCT_VERSION_NOT_SET:
        return 0;
    case SCT_VERSION_V1:
        return sct->log_id != NULL && SCT_signature_is_complete(sct);
    default:
        return sct->sct != NULL; /* Just need cached encoding */
    }
}

int SCT_signature_is_complete(const SCT *sct)
{
    return SCT_get_signature_nid(sct) != NID_undef &&
        sct->sig != NULL && sct->sig_len > 0;
}

sct_source_t SCT_get_source(const SCT *sct)
{
    return sct->source;
}

int SCT_set_source(SCT *sct, sct_source_t source)
{
    sct->source = source;
    sct->validation_status = SCT_VALIDATION_STATUS_NOT_SET;
    switch (source) {
    case SCT_SOURCE_TLS_EXTENSION:
    case SCT_SOURCE_OCSP_STAPLED_RESPONSE:
        return SCT_set_log_entry_type(sct, CT_LOG_ENTRY_TYPE_X509);
    case SCT_SOURCE_X509V3_EXTENSION:
        return SCT_set_log_entry_type(sct, CT_LOG_ENTRY_TYPE_PRECERT);
    case SCT_SOURCE_UNKNOWN:
        break;
    }
    /* if we aren't sure, leave the log entry type alone */
    return 1;
}

sct_validation_status_t SCT_get_validation_status(const SCT *sct)
{
    return sct->validation_status;
}

int SCT_validate(SCT *sct, const CT_POLICY_EVAL_CTX *ctx)
{
    int is_sct_valid = -1;
    SCT_CTX *sctx = NULL;
    X509_PUBKEY *pub = NULL, *log_pkey = NULL;
    const CTLOG *log;

    /*
     * With an unrecognized SCT version we don't know what such an SCT means,
     * let alone validate one.  So we return validation failure (0).
     */
    if (sct->version != SCT_VERSION_V1) {
        sct->validation_status = SCT_VALIDATION_STATUS_UNKNOWN_VERSION;
        return 0;
    }

    log = CTLOG_STORE_get0_log_by_id(ctx->log_store,
                                     sct->log_id, sct->log_id_len);

    /* Similarly, an SCT from an unknown log also cannot be validated. */
    if (log == NULL) {
        sct->validation_status = SCT_VALIDATION_STATUS_UNKNOWN_LOG;
        return 0;
    }

    sctx = SCT_CTX_new();
    if (sctx == NULL)
        goto err;

    if (X509_PUBKEY_set(&log_pkey, CTLOG_get0_public_key(log)) != 1)
        goto err;
    if (SCT_CTX_set1_pubkey(sctx, log_pkey) != 1)
        goto err;

    if (SCT_get_log_entry_type(sct) == CT_LOG_ENTRY_TYPE_PRECERT) {
        EVP_PKEY *issuer_pkey;

        if (ctx->issuer == NULL) {
            sct->validation_status = SCT_VALIDATION_STATUS_UNVERIFIED;
            goto end;
        }

        issuer_pkey = X509_get0_pubkey(ctx->issuer);

        if (X509_PUBKEY_set(&pub, issuer_pkey) != 1)
            goto err;
        if (SCT_CTX_set1_issuer_pubkey(sctx, pub) != 1)
            goto err;
    }

    SCT_CTX_set_time(sctx, ctx->epoch_time_in_ms);

    /*
     * XXX: Potential for optimization.  This repeats some idempotent heavy
     * lifting on the certificate for each candidate SCT, and appears to not
     * use any information in the SCT itself, only the certificate is
     * processed.  So it may make more sense to to do this just once, perhaps
     * associated with the shared (by all SCTs) policy eval ctx.
     *
     * XXX: Failure here is global (SCT independent) and represents either an
     * issue with the certificate (e.g. duplicate extensions) or an out of
     * memory condition.  When the certificate is incompatible with CT, we just
     * mark the SCTs invalid, rather than report a failure to determine the
     * validation status.  That way, callbacks that want to do "soft" SCT
     * processing will not abort handshakes with false positive internal
     * errors.  Since the function does not distinguish between certificate
     * issues (peer's fault) and internal problems (out fault) the safe thing
     * to do is to report a validation failure and let the callback or
     * application decide what to do.
     */
    if (SCT_CTX_set1_cert(sctx, ctx->cert, NULL) != 1)
        sct->validation_status = SCT_VALIDATION_STATUS_UNVERIFIED;
    else
        sct->validation_status = SCT_CTX_verify(sctx, sct) == 1 ?
            SCT_VALIDATION_STATUS_VALID : SCT_VALIDATION_STATUS_INVALID;

end:
    is_sct_valid = sct->validation_status == SCT_VALIDATION_STATUS_VALID;
err:
    X509_PUBKEY_free(pub);
    X509_PUBKEY_free(log_pkey);
    SCT_CTX_free(sctx);

    return is_sct_valid;
}

int SCT_LIST_validate(const STACK_OF(SCT) *scts, CT_POLICY_EVAL_CTX *ctx)
{
    int are_scts_valid = 1;
    int sct_count = scts != NULL ? sk_SCT_num(scts) : 0;
    int i;

    for (i = 0; i < sct_count; ++i) {
        int is_sct_valid = -1;
        SCT *sct = sk_SCT_value(scts, i);

        if (sct == NULL)
            continue;

        is_sct_valid = SCT_validate(sct, ctx);
        if (is_sct_valid < 0)
            return is_sct_valid;
        are_scts_valid &= is_sct_valid;
    }

    return are_scts_valid;
}
