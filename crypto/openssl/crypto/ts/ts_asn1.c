/*
 * Copyright 2006-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/ts.h>
#include <openssl/err.h>
#include <openssl/asn1t.h>
#include "ts_lcl.h"

ASN1_SEQUENCE(TS_MSG_IMPRINT) = {
        ASN1_SIMPLE(TS_MSG_IMPRINT, hash_algo, X509_ALGOR),
        ASN1_SIMPLE(TS_MSG_IMPRINT, hashed_msg, ASN1_OCTET_STRING)
} static_ASN1_SEQUENCE_END(TS_MSG_IMPRINT)

IMPLEMENT_ASN1_FUNCTIONS_const(TS_MSG_IMPRINT)
IMPLEMENT_ASN1_DUP_FUNCTION(TS_MSG_IMPRINT)
TS_MSG_IMPRINT *d2i_TS_MSG_IMPRINT_bio(BIO *bp, TS_MSG_IMPRINT **a)
{
    return ASN1_d2i_bio_of(TS_MSG_IMPRINT, TS_MSG_IMPRINT_new,
                           d2i_TS_MSG_IMPRINT, bp, a);
}

int i2d_TS_MSG_IMPRINT_bio(BIO *bp, TS_MSG_IMPRINT *a)
{
    return ASN1_i2d_bio_of_const(TS_MSG_IMPRINT, i2d_TS_MSG_IMPRINT, bp, a);
}
#ifndef OPENSSL_NO_STDIO
TS_MSG_IMPRINT *d2i_TS_MSG_IMPRINT_fp(FILE *fp, TS_MSG_IMPRINT **a)
{
    return ASN1_d2i_fp_of(TS_MSG_IMPRINT, TS_MSG_IMPRINT_new,
                          d2i_TS_MSG_IMPRINT, fp, a);
}

int i2d_TS_MSG_IMPRINT_fp(FILE *fp, TS_MSG_IMPRINT *a)
{
    return ASN1_i2d_fp_of_const(TS_MSG_IMPRINT, i2d_TS_MSG_IMPRINT, fp, a);
}
#endif

ASN1_SEQUENCE(TS_REQ) = {
        ASN1_SIMPLE(TS_REQ, version, ASN1_INTEGER),
        ASN1_SIMPLE(TS_REQ, msg_imprint, TS_MSG_IMPRINT),
        ASN1_OPT(TS_REQ, policy_id, ASN1_OBJECT),
        ASN1_OPT(TS_REQ, nonce, ASN1_INTEGER),
        ASN1_OPT(TS_REQ, cert_req, ASN1_FBOOLEAN),
        ASN1_IMP_SEQUENCE_OF_OPT(TS_REQ, extensions, X509_EXTENSION, 0)
} static_ASN1_SEQUENCE_END(TS_REQ)

IMPLEMENT_ASN1_FUNCTIONS_const(TS_REQ)
IMPLEMENT_ASN1_DUP_FUNCTION(TS_REQ)
TS_REQ *d2i_TS_REQ_bio(BIO *bp, TS_REQ **a)
{
    return ASN1_d2i_bio_of(TS_REQ, TS_REQ_new, d2i_TS_REQ, bp, a);
}

int i2d_TS_REQ_bio(BIO *bp, TS_REQ *a)
{
    return ASN1_i2d_bio_of_const(TS_REQ, i2d_TS_REQ, bp, a);
}
#ifndef OPENSSL_NO_STDIO
TS_REQ *d2i_TS_REQ_fp(FILE *fp, TS_REQ **a)
{
    return ASN1_d2i_fp_of(TS_REQ, TS_REQ_new, d2i_TS_REQ, fp, a);
}

int i2d_TS_REQ_fp(FILE *fp, TS_REQ *a)
{
    return ASN1_i2d_fp_of_const(TS_REQ, i2d_TS_REQ, fp, a);
}
#endif

ASN1_SEQUENCE(TS_ACCURACY) = {
        ASN1_OPT(TS_ACCURACY, seconds, ASN1_INTEGER),
        ASN1_IMP_OPT(TS_ACCURACY, millis, ASN1_INTEGER, 0),
        ASN1_IMP_OPT(TS_ACCURACY, micros, ASN1_INTEGER, 1)
} static_ASN1_SEQUENCE_END(TS_ACCURACY)

IMPLEMENT_ASN1_FUNCTIONS_const(TS_ACCURACY)
IMPLEMENT_ASN1_DUP_FUNCTION(TS_ACCURACY)

ASN1_SEQUENCE(TS_TST_INFO) = {
        ASN1_SIMPLE(TS_TST_INFO, version, ASN1_INTEGER),
        ASN1_SIMPLE(TS_TST_INFO, policy_id, ASN1_OBJECT),
        ASN1_SIMPLE(TS_TST_INFO, msg_imprint, TS_MSG_IMPRINT),
        ASN1_SIMPLE(TS_TST_INFO, serial, ASN1_INTEGER),
        ASN1_SIMPLE(TS_TST_INFO, time, ASN1_GENERALIZEDTIME),
        ASN1_OPT(TS_TST_INFO, accuracy, TS_ACCURACY),
        ASN1_OPT(TS_TST_INFO, ordering, ASN1_FBOOLEAN),
        ASN1_OPT(TS_TST_INFO, nonce, ASN1_INTEGER),
        ASN1_EXP_OPT(TS_TST_INFO, tsa, GENERAL_NAME, 0),
        ASN1_IMP_SEQUENCE_OF_OPT(TS_TST_INFO, extensions, X509_EXTENSION, 1)
} static_ASN1_SEQUENCE_END(TS_TST_INFO)

IMPLEMENT_ASN1_FUNCTIONS_const(TS_TST_INFO)
IMPLEMENT_ASN1_DUP_FUNCTION(TS_TST_INFO)
TS_TST_INFO *d2i_TS_TST_INFO_bio(BIO *bp, TS_TST_INFO **a)
{
    return ASN1_d2i_bio_of(TS_TST_INFO, TS_TST_INFO_new, d2i_TS_TST_INFO, bp,
                           a);
}

int i2d_TS_TST_INFO_bio(BIO *bp, TS_TST_INFO *a)
{
    return ASN1_i2d_bio_of_const(TS_TST_INFO, i2d_TS_TST_INFO, bp, a);
}
#ifndef OPENSSL_NO_STDIO
TS_TST_INFO *d2i_TS_TST_INFO_fp(FILE *fp, TS_TST_INFO **a)
{
    return ASN1_d2i_fp_of(TS_TST_INFO, TS_TST_INFO_new, d2i_TS_TST_INFO, fp,
                          a);
}

int i2d_TS_TST_INFO_fp(FILE *fp, TS_TST_INFO *a)
{
    return ASN1_i2d_fp_of_const(TS_TST_INFO, i2d_TS_TST_INFO, fp, a);
}
#endif

ASN1_SEQUENCE(TS_STATUS_INFO) = {
        ASN1_SIMPLE(TS_STATUS_INFO, status, ASN1_INTEGER),
        ASN1_SEQUENCE_OF_OPT(TS_STATUS_INFO, text, ASN1_UTF8STRING),
        ASN1_OPT(TS_STATUS_INFO, failure_info, ASN1_BIT_STRING)
} static_ASN1_SEQUENCE_END(TS_STATUS_INFO)

IMPLEMENT_ASN1_FUNCTIONS_const(TS_STATUS_INFO)
IMPLEMENT_ASN1_DUP_FUNCTION(TS_STATUS_INFO)

static int ts_resp_set_tst_info(TS_RESP *a)
{
    long status;

    status = ASN1_INTEGER_get(a->status_info->status);

    if (a->token) {
        if (status != 0 && status != 1) {
            TSerr(TS_F_TS_RESP_SET_TST_INFO, TS_R_TOKEN_PRESENT);
            return 0;
        }
        TS_TST_INFO_free(a->tst_info);
        a->tst_info = PKCS7_to_TS_TST_INFO(a->token);
        if (!a->tst_info) {
            TSerr(TS_F_TS_RESP_SET_TST_INFO,
                  TS_R_PKCS7_TO_TS_TST_INFO_FAILED);
            return 0;
        }
    } else if (status == 0 || status == 1) {
        TSerr(TS_F_TS_RESP_SET_TST_INFO, TS_R_TOKEN_NOT_PRESENT);
        return 0;
    }

    return 1;
}

static int ts_resp_cb(int op, ASN1_VALUE **pval, const ASN1_ITEM *it,
                      void *exarg)
{
    TS_RESP *ts_resp = (TS_RESP *)*pval;
    if (op == ASN1_OP_NEW_POST) {
        ts_resp->tst_info = NULL;
    } else if (op == ASN1_OP_FREE_POST) {
        TS_TST_INFO_free(ts_resp->tst_info);
    } else if (op == ASN1_OP_D2I_POST) {
        if (ts_resp_set_tst_info(ts_resp) == 0)
            return 0;
    }
    return 1;
}

ASN1_SEQUENCE_cb(TS_RESP, ts_resp_cb) = {
        ASN1_SIMPLE(TS_RESP, status_info, TS_STATUS_INFO),
        ASN1_OPT(TS_RESP, token, PKCS7),
} static_ASN1_SEQUENCE_END_cb(TS_RESP, TS_RESP)

IMPLEMENT_ASN1_FUNCTIONS_const(TS_RESP)

IMPLEMENT_ASN1_DUP_FUNCTION(TS_RESP)

TS_RESP *d2i_TS_RESP_bio(BIO *bp, TS_RESP **a)
{
    return ASN1_d2i_bio_of(TS_RESP, TS_RESP_new, d2i_TS_RESP, bp, a);
}

int i2d_TS_RESP_bio(BIO *bp, TS_RESP *a)
{
    return ASN1_i2d_bio_of_const(TS_RESP, i2d_TS_RESP, bp, a);
}
#ifndef OPENSSL_NO_STDIO
TS_RESP *d2i_TS_RESP_fp(FILE *fp, TS_RESP **a)
{
    return ASN1_d2i_fp_of(TS_RESP, TS_RESP_new, d2i_TS_RESP, fp, a);
}

int i2d_TS_RESP_fp(FILE *fp, TS_RESP *a)
{
    return ASN1_i2d_fp_of_const(TS_RESP, i2d_TS_RESP, fp, a);
}
#endif

ASN1_SEQUENCE(ESS_ISSUER_SERIAL) = {
        ASN1_SEQUENCE_OF(ESS_ISSUER_SERIAL, issuer, GENERAL_NAME),
        ASN1_SIMPLE(ESS_ISSUER_SERIAL, serial, ASN1_INTEGER)
} static_ASN1_SEQUENCE_END(ESS_ISSUER_SERIAL)

IMPLEMENT_ASN1_FUNCTIONS_const(ESS_ISSUER_SERIAL)
IMPLEMENT_ASN1_DUP_FUNCTION(ESS_ISSUER_SERIAL)

ASN1_SEQUENCE(ESS_CERT_ID) = {
        ASN1_SIMPLE(ESS_CERT_ID, hash, ASN1_OCTET_STRING),
        ASN1_OPT(ESS_CERT_ID, issuer_serial, ESS_ISSUER_SERIAL)
} static_ASN1_SEQUENCE_END(ESS_CERT_ID)

IMPLEMENT_ASN1_FUNCTIONS_const(ESS_CERT_ID)
IMPLEMENT_ASN1_DUP_FUNCTION(ESS_CERT_ID)

ASN1_SEQUENCE(ESS_SIGNING_CERT) = {
        ASN1_SEQUENCE_OF(ESS_SIGNING_CERT, cert_ids, ESS_CERT_ID),
        ASN1_SEQUENCE_OF_OPT(ESS_SIGNING_CERT, policy_info, POLICYINFO)
} static_ASN1_SEQUENCE_END(ESS_SIGNING_CERT)

IMPLEMENT_ASN1_FUNCTIONS_const(ESS_SIGNING_CERT)
IMPLEMENT_ASN1_DUP_FUNCTION(ESS_SIGNING_CERT)

ASN1_SEQUENCE(ESS_CERT_ID_V2) = {
        ASN1_OPT(ESS_CERT_ID_V2, hash_alg, X509_ALGOR),
        ASN1_SIMPLE(ESS_CERT_ID_V2, hash, ASN1_OCTET_STRING),
        ASN1_OPT(ESS_CERT_ID_V2, issuer_serial, ESS_ISSUER_SERIAL)
} static_ASN1_SEQUENCE_END(ESS_CERT_ID_V2)

IMPLEMENT_ASN1_FUNCTIONS_const(ESS_CERT_ID_V2)
IMPLEMENT_ASN1_DUP_FUNCTION(ESS_CERT_ID_V2)

ASN1_SEQUENCE(ESS_SIGNING_CERT_V2) = {
        ASN1_SEQUENCE_OF(ESS_SIGNING_CERT_V2, cert_ids, ESS_CERT_ID_V2),
        ASN1_SEQUENCE_OF_OPT(ESS_SIGNING_CERT_V2, policy_info, POLICYINFO)
} static_ASN1_SEQUENCE_END(ESS_SIGNING_CERT_V2)

IMPLEMENT_ASN1_FUNCTIONS_const(ESS_SIGNING_CERT_V2)
IMPLEMENT_ASN1_DUP_FUNCTION(ESS_SIGNING_CERT_V2)

/* Getting encapsulated TS_TST_INFO object from PKCS7. */
TS_TST_INFO *PKCS7_to_TS_TST_INFO(PKCS7 *token)
{
    PKCS7_SIGNED *pkcs7_signed;
    PKCS7 *enveloped;
    ASN1_TYPE *tst_info_wrapper;
    ASN1_OCTET_STRING *tst_info_der;
    const unsigned char *p;

    if (!PKCS7_type_is_signed(token)) {
        TSerr(TS_F_PKCS7_TO_TS_TST_INFO, TS_R_BAD_PKCS7_TYPE);
        return NULL;
    }
    if (PKCS7_get_detached(token)) {
        TSerr(TS_F_PKCS7_TO_TS_TST_INFO, TS_R_DETACHED_CONTENT);
        return NULL;
    }
    pkcs7_signed = token->d.sign;
    enveloped = pkcs7_signed->contents;
    if (OBJ_obj2nid(enveloped->type) != NID_id_smime_ct_TSTInfo) {
        TSerr(TS_F_PKCS7_TO_TS_TST_INFO, TS_R_BAD_PKCS7_TYPE);
        return NULL;
    }
    tst_info_wrapper = enveloped->d.other;
    if (tst_info_wrapper->type != V_ASN1_OCTET_STRING) {
        TSerr(TS_F_PKCS7_TO_TS_TST_INFO, TS_R_BAD_TYPE);
        return NULL;
    }
    tst_info_der = tst_info_wrapper->value.octet_string;
    p = tst_info_der->data;
    return d2i_TS_TST_INFO(NULL, &p, tst_info_der->length);
}
