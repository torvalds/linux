/*
 * Copyright 2001-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <time.h>
#include "internal/cryptlib.h"
#include <openssl/asn1.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <openssl/ocsp.h>
#include "ocsp_lcl.h"

/*
 * Utility functions related to sending OCSP requests and extracting relevant
 * information from the response.
 */

/*
 * Add an OCSP_CERTID to an OCSP request. Return new OCSP_ONEREQ pointer:
 * useful if we want to add extensions.
 */

OCSP_ONEREQ *OCSP_request_add0_id(OCSP_REQUEST *req, OCSP_CERTID *cid)
{
    OCSP_ONEREQ *one = NULL;

    if ((one = OCSP_ONEREQ_new()) == NULL)
        return NULL;
    OCSP_CERTID_free(one->reqCert);
    one->reqCert = cid;
    if (req && !sk_OCSP_ONEREQ_push(req->tbsRequest.requestList, one)) {
        one->reqCert = NULL; /* do not free on error */
        goto err;
    }
    return one;
 err:
    OCSP_ONEREQ_free(one);
    return NULL;
}

/* Set requestorName from an X509_NAME structure */

int OCSP_request_set1_name(OCSP_REQUEST *req, X509_NAME *nm)
{
    GENERAL_NAME *gen;

    gen = GENERAL_NAME_new();
    if (gen == NULL)
        return 0;
    if (!X509_NAME_set(&gen->d.directoryName, nm)) {
        GENERAL_NAME_free(gen);
        return 0;
    }
    gen->type = GEN_DIRNAME;
    GENERAL_NAME_free(req->tbsRequest.requestorName);
    req->tbsRequest.requestorName = gen;
    return 1;
}

/* Add a certificate to an OCSP request */

int OCSP_request_add1_cert(OCSP_REQUEST *req, X509 *cert)
{
    OCSP_SIGNATURE *sig;
    if (req->optionalSignature == NULL)
        req->optionalSignature = OCSP_SIGNATURE_new();
    sig = req->optionalSignature;
    if (sig == NULL)
        return 0;
    if (cert == NULL)
        return 1;
    if (sig->certs == NULL
        && (sig->certs = sk_X509_new_null()) == NULL)
        return 0;

    if (!sk_X509_push(sig->certs, cert))
        return 0;
    X509_up_ref(cert);
    return 1;
}

/*
 * Sign an OCSP request set the requestorName to the subject name of an
 * optional signers certificate and include one or more optional certificates
 * in the request. Behaves like PKCS7_sign().
 */

int OCSP_request_sign(OCSP_REQUEST *req,
                      X509 *signer,
                      EVP_PKEY *key,
                      const EVP_MD *dgst,
                      STACK_OF(X509) *certs, unsigned long flags)
{
    int i;
    X509 *x;

    if (!OCSP_request_set1_name(req, X509_get_subject_name(signer)))
        goto err;

    if ((req->optionalSignature = OCSP_SIGNATURE_new()) == NULL)
        goto err;
    if (key) {
        if (!X509_check_private_key(signer, key)) {
            OCSPerr(OCSP_F_OCSP_REQUEST_SIGN,
                    OCSP_R_PRIVATE_KEY_DOES_NOT_MATCH_CERTIFICATE);
            goto err;
        }
        if (!OCSP_REQUEST_sign(req, key, dgst))
            goto err;
    }

    if (!(flags & OCSP_NOCERTS)) {
        if (!OCSP_request_add1_cert(req, signer))
            goto err;
        for (i = 0; i < sk_X509_num(certs); i++) {
            x = sk_X509_value(certs, i);
            if (!OCSP_request_add1_cert(req, x))
                goto err;
        }
    }

    return 1;
 err:
    OCSP_SIGNATURE_free(req->optionalSignature);
    req->optionalSignature = NULL;
    return 0;
}

/* Get response status */

int OCSP_response_status(OCSP_RESPONSE *resp)
{
    return ASN1_ENUMERATED_get(resp->responseStatus);
}

/*
 * Extract basic response from OCSP_RESPONSE or NULL if no basic response
 * present.
 */

OCSP_BASICRESP *OCSP_response_get1_basic(OCSP_RESPONSE *resp)
{
    OCSP_RESPBYTES *rb;
    rb = resp->responseBytes;
    if (!rb) {
        OCSPerr(OCSP_F_OCSP_RESPONSE_GET1_BASIC, OCSP_R_NO_RESPONSE_DATA);
        return NULL;
    }
    if (OBJ_obj2nid(rb->responseType) != NID_id_pkix_OCSP_basic) {
        OCSPerr(OCSP_F_OCSP_RESPONSE_GET1_BASIC, OCSP_R_NOT_BASIC_RESPONSE);
        return NULL;
    }

    return ASN1_item_unpack(rb->response, ASN1_ITEM_rptr(OCSP_BASICRESP));
}

const ASN1_OCTET_STRING *OCSP_resp_get0_signature(const OCSP_BASICRESP *bs)
{
    return bs->signature;
}

const X509_ALGOR *OCSP_resp_get0_tbs_sigalg(const OCSP_BASICRESP *bs)
{
    return &bs->signatureAlgorithm;
}

const OCSP_RESPDATA *OCSP_resp_get0_respdata(const OCSP_BASICRESP *bs)
{
    return &bs->tbsResponseData;
}

/*
 * Return number of OCSP_SINGLERESP responses present in a basic response.
 */

int OCSP_resp_count(OCSP_BASICRESP *bs)
{
    if (!bs)
        return -1;
    return sk_OCSP_SINGLERESP_num(bs->tbsResponseData.responses);
}

/* Extract an OCSP_SINGLERESP response with a given index */

OCSP_SINGLERESP *OCSP_resp_get0(OCSP_BASICRESP *bs, int idx)
{
    if (!bs)
        return NULL;
    return sk_OCSP_SINGLERESP_value(bs->tbsResponseData.responses, idx);
}

const ASN1_GENERALIZEDTIME *OCSP_resp_get0_produced_at(const OCSP_BASICRESP* bs)
{
    return bs->tbsResponseData.producedAt;
}

const STACK_OF(X509) *OCSP_resp_get0_certs(const OCSP_BASICRESP *bs)
{
    return bs->certs;
}

int OCSP_resp_get0_id(const OCSP_BASICRESP *bs,
                      const ASN1_OCTET_STRING **pid,
                      const X509_NAME **pname)
{
    const OCSP_RESPID *rid = &bs->tbsResponseData.responderId;

    if (rid->type == V_OCSP_RESPID_NAME) {
        *pname = rid->value.byName;
        *pid = NULL;
    } else if (rid->type == V_OCSP_RESPID_KEY) {
        *pid = rid->value.byKey;
        *pname = NULL;
    } else {
        return 0;
    }
    return 1;
}

int OCSP_resp_get1_id(const OCSP_BASICRESP *bs,
                      ASN1_OCTET_STRING **pid,
                      X509_NAME **pname)
{
    const OCSP_RESPID *rid = &bs->tbsResponseData.responderId;

    if (rid->type == V_OCSP_RESPID_NAME) {
        *pname = X509_NAME_dup(rid->value.byName);
        *pid = NULL;
    } else if (rid->type == V_OCSP_RESPID_KEY) {
        *pid = ASN1_OCTET_STRING_dup(rid->value.byKey);
        *pname = NULL;
    } else {
        return 0;
    }
    if (*pname == NULL && *pid == NULL)
        return 0;
    return 1;
}

/* Look single response matching a given certificate ID */

int OCSP_resp_find(OCSP_BASICRESP *bs, OCSP_CERTID *id, int last)
{
    int i;
    STACK_OF(OCSP_SINGLERESP) *sresp;
    OCSP_SINGLERESP *single;
    if (!bs)
        return -1;
    if (last < 0)
        last = 0;
    else
        last++;
    sresp = bs->tbsResponseData.responses;
    for (i = last; i < sk_OCSP_SINGLERESP_num(sresp); i++) {
        single = sk_OCSP_SINGLERESP_value(sresp, i);
        if (!OCSP_id_cmp(id, single->certId))
            return i;
    }
    return -1;
}

/*
 * Extract status information from an OCSP_SINGLERESP structure. Note: the
 * revtime and reason values are only set if the certificate status is
 * revoked. Returns numerical value of status.
 */

int OCSP_single_get0_status(OCSP_SINGLERESP *single, int *reason,
                            ASN1_GENERALIZEDTIME **revtime,
                            ASN1_GENERALIZEDTIME **thisupd,
                            ASN1_GENERALIZEDTIME **nextupd)
{
    int ret;
    OCSP_CERTSTATUS *cst;
    if (!single)
        return -1;
    cst = single->certStatus;
    ret = cst->type;
    if (ret == V_OCSP_CERTSTATUS_REVOKED) {
        OCSP_REVOKEDINFO *rev = cst->value.revoked;
        if (revtime)
            *revtime = rev->revocationTime;
        if (reason) {
            if (rev->revocationReason)
                *reason = ASN1_ENUMERATED_get(rev->revocationReason);
            else
                *reason = -1;
        }
    }
    if (thisupd)
        *thisupd = single->thisUpdate;
    if (nextupd)
        *nextupd = single->nextUpdate;
    return ret;
}

/*
 * This function combines the previous ones: look up a certificate ID and if
 * found extract status information. Return 0 is successful.
 */

int OCSP_resp_find_status(OCSP_BASICRESP *bs, OCSP_CERTID *id, int *status,
                          int *reason,
                          ASN1_GENERALIZEDTIME **revtime,
                          ASN1_GENERALIZEDTIME **thisupd,
                          ASN1_GENERALIZEDTIME **nextupd)
{
    int i;
    OCSP_SINGLERESP *single;
    i = OCSP_resp_find(bs, id, -1);
    /* Maybe check for multiple responses and give an error? */
    if (i < 0)
        return 0;
    single = OCSP_resp_get0(bs, i);
    i = OCSP_single_get0_status(single, reason, revtime, thisupd, nextupd);
    if (status)
        *status = i;
    return 1;
}

/*
 * Check validity of thisUpdate and nextUpdate fields. It is possible that
 * the request will take a few seconds to process and/or the time won't be
 * totally accurate. Therefore to avoid rejecting otherwise valid time we
 * allow the times to be within 'nsec' of the current time. Also to avoid
 * accepting very old responses without a nextUpdate field an optional maxage
 * parameter specifies the maximum age the thisUpdate field can be.
 */

int OCSP_check_validity(ASN1_GENERALIZEDTIME *thisupd,
                        ASN1_GENERALIZEDTIME *nextupd, long nsec, long maxsec)
{
    int ret = 1;
    time_t t_now, t_tmp;
    time(&t_now);
    /* Check thisUpdate is valid and not more than nsec in the future */
    if (!ASN1_GENERALIZEDTIME_check(thisupd)) {
        OCSPerr(OCSP_F_OCSP_CHECK_VALIDITY, OCSP_R_ERROR_IN_THISUPDATE_FIELD);
        ret = 0;
    } else {
        t_tmp = t_now + nsec;
        if (X509_cmp_time(thisupd, &t_tmp) > 0) {
            OCSPerr(OCSP_F_OCSP_CHECK_VALIDITY, OCSP_R_STATUS_NOT_YET_VALID);
            ret = 0;
        }

        /*
         * If maxsec specified check thisUpdate is not more than maxsec in
         * the past
         */
        if (maxsec >= 0) {
            t_tmp = t_now - maxsec;
            if (X509_cmp_time(thisupd, &t_tmp) < 0) {
                OCSPerr(OCSP_F_OCSP_CHECK_VALIDITY, OCSP_R_STATUS_TOO_OLD);
                ret = 0;
            }
        }
    }

    if (!nextupd)
        return ret;

    /* Check nextUpdate is valid and not more than nsec in the past */
    if (!ASN1_GENERALIZEDTIME_check(nextupd)) {
        OCSPerr(OCSP_F_OCSP_CHECK_VALIDITY, OCSP_R_ERROR_IN_NEXTUPDATE_FIELD);
        ret = 0;
    } else {
        t_tmp = t_now - nsec;
        if (X509_cmp_time(nextupd, &t_tmp) < 0) {
            OCSPerr(OCSP_F_OCSP_CHECK_VALIDITY, OCSP_R_STATUS_EXPIRED);
            ret = 0;
        }
    }

    /* Also don't allow nextUpdate to precede thisUpdate */
    if (ASN1_STRING_cmp(nextupd, thisupd) < 0) {
        OCSPerr(OCSP_F_OCSP_CHECK_VALIDITY,
                OCSP_R_NEXTUPDATE_BEFORE_THISUPDATE);
        ret = 0;
    }

    return ret;
}

const OCSP_CERTID *OCSP_SINGLERESP_get0_id(const OCSP_SINGLERESP *single)
{
    return single->certId;
}
