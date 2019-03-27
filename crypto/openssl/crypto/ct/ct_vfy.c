/*
 * Copyright 2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <string.h>

#include <openssl/ct.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include "ct_locl.h"

typedef enum sct_signature_type_t {
    SIGNATURE_TYPE_NOT_SET = -1,
    SIGNATURE_TYPE_CERT_TIMESTAMP,
    SIGNATURE_TYPE_TREE_HASH
} SCT_SIGNATURE_TYPE;

/*
 * Update encoding for SCT signature verification/generation to supplied
 * EVP_MD_CTX.
 */
static int sct_ctx_update(EVP_MD_CTX *ctx, const SCT_CTX *sctx, const SCT *sct)
{
    unsigned char tmpbuf[12];
    unsigned char *p, *der;
    size_t derlen;
    /*+
     * digitally-signed struct {
     *   (1 byte) Version sct_version;
     *   (1 byte) SignatureType signature_type = certificate_timestamp;
     *   (8 bytes) uint64 timestamp;
     *   (2 bytes) LogEntryType entry_type;
     *   (? bytes) select(entry_type) {
     *     case x509_entry: ASN.1Cert;
     *     case precert_entry: PreCert;
     *   } signed_entry;
     *   (2 bytes + sct->ext_len) CtExtensions extensions;
     * }
     */
    if (sct->entry_type == CT_LOG_ENTRY_TYPE_NOT_SET)
        return 0;
    if (sct->entry_type == CT_LOG_ENTRY_TYPE_PRECERT && sctx->ihash == NULL)
        return 0;

    p = tmpbuf;
    *p++ = sct->version;
    *p++ = SIGNATURE_TYPE_CERT_TIMESTAMP;
    l2n8(sct->timestamp, p);
    s2n(sct->entry_type, p);

    if (!EVP_DigestUpdate(ctx, tmpbuf, p - tmpbuf))
        return 0;

    if (sct->entry_type == CT_LOG_ENTRY_TYPE_X509) {
        der = sctx->certder;
        derlen = sctx->certderlen;
    } else {
        if (!EVP_DigestUpdate(ctx, sctx->ihash, sctx->ihashlen))
            return 0;
        der = sctx->preder;
        derlen = sctx->prederlen;
    }

    /* If no encoding available, fatal error */
    if (der == NULL)
        return 0;

    /* Include length first */
    p = tmpbuf;
    l2n3(derlen, p);

    if (!EVP_DigestUpdate(ctx, tmpbuf, 3))
        return 0;
    if (!EVP_DigestUpdate(ctx, der, derlen))
        return 0;

    /* Add any extensions */
    p = tmpbuf;
    s2n(sct->ext_len, p);
    if (!EVP_DigestUpdate(ctx, tmpbuf, 2))
        return 0;

    if (sct->ext_len && !EVP_DigestUpdate(ctx, sct->ext, sct->ext_len))
        return 0;

    return 1;
}

int SCT_CTX_verify(const SCT_CTX *sctx, const SCT *sct)
{
    EVP_MD_CTX *ctx = NULL;
    int ret = 0;

    if (!SCT_is_complete(sct) || sctx->pkey == NULL ||
        sct->entry_type == CT_LOG_ENTRY_TYPE_NOT_SET ||
        (sct->entry_type == CT_LOG_ENTRY_TYPE_PRECERT && sctx->ihash == NULL)) {
        CTerr(CT_F_SCT_CTX_VERIFY, CT_R_SCT_NOT_SET);
        return 0;
    }
    if (sct->version != SCT_VERSION_V1) {
        CTerr(CT_F_SCT_CTX_VERIFY, CT_R_SCT_UNSUPPORTED_VERSION);
        return 0;
    }
    if (sct->log_id_len != sctx->pkeyhashlen ||
        memcmp(sct->log_id, sctx->pkeyhash, sctx->pkeyhashlen) != 0) {
        CTerr(CT_F_SCT_CTX_VERIFY, CT_R_SCT_LOG_ID_MISMATCH);
        return 0;
    }
    if (sct->timestamp > sctx->epoch_time_in_ms) {
        CTerr(CT_F_SCT_CTX_VERIFY, CT_R_SCT_FUTURE_TIMESTAMP);
        return 0;
    }

    ctx = EVP_MD_CTX_new();
    if (ctx == NULL)
        goto end;

    if (!EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, sctx->pkey))
        goto end;

    if (!sct_ctx_update(ctx, sctx, sct))
        goto end;

    /* Verify signature */
    ret = EVP_DigestVerifyFinal(ctx, sct->sig, sct->sig_len);
    /* If ret < 0 some other error: fall through without setting error */
    if (ret == 0)
        CTerr(CT_F_SCT_CTX_VERIFY, CT_R_SCT_INVALID_SIGNATURE);

end:
    EVP_MD_CTX_free(ctx);
    return ret;
}
