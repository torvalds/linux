/*
 * Copyright 2006-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/objects.h>
#include <openssl/ts.h>
#include <openssl/pkcs7.h>
#include "ts_lcl.h"

int TS_RESP_set_status_info(TS_RESP *a, TS_STATUS_INFO *status_info)
{
    TS_STATUS_INFO *new_status_info;

    if (a->status_info == status_info)
        return 1;
    new_status_info = TS_STATUS_INFO_dup(status_info);
    if (new_status_info == NULL) {
        TSerr(TS_F_TS_RESP_SET_STATUS_INFO, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    TS_STATUS_INFO_free(a->status_info);
    a->status_info = new_status_info;

    return 1;
}

TS_STATUS_INFO *TS_RESP_get_status_info(TS_RESP *a)
{
    return a->status_info;
}

/* Caller loses ownership of PKCS7 and TS_TST_INFO objects. */
void TS_RESP_set_tst_info(TS_RESP *a, PKCS7 *p7, TS_TST_INFO *tst_info)
{
    PKCS7_free(a->token);
    a->token = p7;
    TS_TST_INFO_free(a->tst_info);
    a->tst_info = tst_info;
}

PKCS7 *TS_RESP_get_token(TS_RESP *a)
{
    return a->token;
}

TS_TST_INFO *TS_RESP_get_tst_info(TS_RESP *a)
{
    return a->tst_info;
}

int TS_TST_INFO_set_version(TS_TST_INFO *a, long version)
{
    return ASN1_INTEGER_set(a->version, version);
}

long TS_TST_INFO_get_version(const TS_TST_INFO *a)
{
    return ASN1_INTEGER_get(a->version);
}

int TS_TST_INFO_set_policy_id(TS_TST_INFO *a, ASN1_OBJECT *policy)
{
    ASN1_OBJECT *new_policy;

    if (a->policy_id == policy)
        return 1;
    new_policy = OBJ_dup(policy);
    if (new_policy == NULL) {
        TSerr(TS_F_TS_TST_INFO_SET_POLICY_ID, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    ASN1_OBJECT_free(a->policy_id);
    a->policy_id = new_policy;
    return 1;
}

ASN1_OBJECT *TS_TST_INFO_get_policy_id(TS_TST_INFO *a)
{
    return a->policy_id;
}

int TS_TST_INFO_set_msg_imprint(TS_TST_INFO *a, TS_MSG_IMPRINT *msg_imprint)
{
    TS_MSG_IMPRINT *new_msg_imprint;

    if (a->msg_imprint == msg_imprint)
        return 1;
    new_msg_imprint = TS_MSG_IMPRINT_dup(msg_imprint);
    if (new_msg_imprint == NULL) {
        TSerr(TS_F_TS_TST_INFO_SET_MSG_IMPRINT, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    TS_MSG_IMPRINT_free(a->msg_imprint);
    a->msg_imprint = new_msg_imprint;
    return 1;
}

TS_MSG_IMPRINT *TS_TST_INFO_get_msg_imprint(TS_TST_INFO *a)
{
    return a->msg_imprint;
}

int TS_TST_INFO_set_serial(TS_TST_INFO *a, const ASN1_INTEGER *serial)
{
    ASN1_INTEGER *new_serial;

    if (a->serial == serial)
        return 1;
    new_serial = ASN1_INTEGER_dup(serial);
    if (new_serial == NULL) {
        TSerr(TS_F_TS_TST_INFO_SET_SERIAL, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    ASN1_INTEGER_free(a->serial);
    a->serial = new_serial;
    return 1;
}

const ASN1_INTEGER *TS_TST_INFO_get_serial(const TS_TST_INFO *a)
{
    return a->serial;
}

int TS_TST_INFO_set_time(TS_TST_INFO *a, const ASN1_GENERALIZEDTIME *gtime)
{
    ASN1_GENERALIZEDTIME *new_time;

    if (a->time == gtime)
        return 1;
    new_time = ASN1_STRING_dup(gtime);
    if (new_time == NULL) {
        TSerr(TS_F_TS_TST_INFO_SET_TIME, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    ASN1_GENERALIZEDTIME_free(a->time);
    a->time = new_time;
    return 1;
}

const ASN1_GENERALIZEDTIME *TS_TST_INFO_get_time(const TS_TST_INFO *a)
{
    return a->time;
}

int TS_TST_INFO_set_accuracy(TS_TST_INFO *a, TS_ACCURACY *accuracy)
{
    TS_ACCURACY *new_accuracy;

    if (a->accuracy == accuracy)
        return 1;
    new_accuracy = TS_ACCURACY_dup(accuracy);
    if (new_accuracy == NULL) {
        TSerr(TS_F_TS_TST_INFO_SET_ACCURACY, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    TS_ACCURACY_free(a->accuracy);
    a->accuracy = new_accuracy;
    return 1;
}

TS_ACCURACY *TS_TST_INFO_get_accuracy(TS_TST_INFO *a)
{
    return a->accuracy;
}

int TS_ACCURACY_set_seconds(TS_ACCURACY *a, const ASN1_INTEGER *seconds)
{
    ASN1_INTEGER *new_seconds;

    if (a->seconds == seconds)
        return 1;
    new_seconds = ASN1_INTEGER_dup(seconds);
    if (new_seconds == NULL) {
        TSerr(TS_F_TS_ACCURACY_SET_SECONDS, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    ASN1_INTEGER_free(a->seconds);
    a->seconds = new_seconds;
    return 1;
}

const ASN1_INTEGER *TS_ACCURACY_get_seconds(const TS_ACCURACY *a)
{
    return a->seconds;
}

int TS_ACCURACY_set_millis(TS_ACCURACY *a, const ASN1_INTEGER *millis)
{
    ASN1_INTEGER *new_millis = NULL;

    if (a->millis == millis)
        return 1;
    if (millis != NULL) {
        new_millis = ASN1_INTEGER_dup(millis);
        if (new_millis == NULL) {
            TSerr(TS_F_TS_ACCURACY_SET_MILLIS, ERR_R_MALLOC_FAILURE);
            return 0;
        }
    }
    ASN1_INTEGER_free(a->millis);
    a->millis = new_millis;
    return 1;
}

const ASN1_INTEGER *TS_ACCURACY_get_millis(const TS_ACCURACY *a)
{
    return a->millis;
}

int TS_ACCURACY_set_micros(TS_ACCURACY *a, const ASN1_INTEGER *micros)
{
    ASN1_INTEGER *new_micros = NULL;

    if (a->micros == micros)
        return 1;
    if (micros != NULL) {
        new_micros = ASN1_INTEGER_dup(micros);
        if (new_micros == NULL) {
            TSerr(TS_F_TS_ACCURACY_SET_MICROS, ERR_R_MALLOC_FAILURE);
            return 0;
        }
    }
    ASN1_INTEGER_free(a->micros);
    a->micros = new_micros;
    return 1;
}

const ASN1_INTEGER *TS_ACCURACY_get_micros(const TS_ACCURACY *a)
{
    return a->micros;
}

int TS_TST_INFO_set_ordering(TS_TST_INFO *a, int ordering)
{
    a->ordering = ordering ? 0xFF : 0x00;
    return 1;
}

int TS_TST_INFO_get_ordering(const TS_TST_INFO *a)
{
    return a->ordering ? 1 : 0;
}

int TS_TST_INFO_set_nonce(TS_TST_INFO *a, const ASN1_INTEGER *nonce)
{
    ASN1_INTEGER *new_nonce;

    if (a->nonce == nonce)
        return 1;
    new_nonce = ASN1_INTEGER_dup(nonce);
    if (new_nonce == NULL) {
        TSerr(TS_F_TS_TST_INFO_SET_NONCE, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    ASN1_INTEGER_free(a->nonce);
    a->nonce = new_nonce;
    return 1;
}

const ASN1_INTEGER *TS_TST_INFO_get_nonce(const TS_TST_INFO *a)
{
    return a->nonce;
}

int TS_TST_INFO_set_tsa(TS_TST_INFO *a, GENERAL_NAME *tsa)
{
    GENERAL_NAME *new_tsa;

    if (a->tsa == tsa)
        return 1;
    new_tsa = GENERAL_NAME_dup(tsa);
    if (new_tsa == NULL) {
        TSerr(TS_F_TS_TST_INFO_SET_TSA, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    GENERAL_NAME_free(a->tsa);
    a->tsa = new_tsa;
    return 1;
}

GENERAL_NAME *TS_TST_INFO_get_tsa(TS_TST_INFO *a)
{
    return a->tsa;
}

STACK_OF(X509_EXTENSION) *TS_TST_INFO_get_exts(TS_TST_INFO *a)
{
    return a->extensions;
}

void TS_TST_INFO_ext_free(TS_TST_INFO *a)
{
    if (!a)
        return;
    sk_X509_EXTENSION_pop_free(a->extensions, X509_EXTENSION_free);
    a->extensions = NULL;
}

int TS_TST_INFO_get_ext_count(TS_TST_INFO *a)
{
    return X509v3_get_ext_count(a->extensions);
}

int TS_TST_INFO_get_ext_by_NID(TS_TST_INFO *a, int nid, int lastpos)
{
    return X509v3_get_ext_by_NID(a->extensions, nid, lastpos);
}

int TS_TST_INFO_get_ext_by_OBJ(TS_TST_INFO *a, const ASN1_OBJECT *obj, int lastpos)
{
    return X509v3_get_ext_by_OBJ(a->extensions, obj, lastpos);
}

int TS_TST_INFO_get_ext_by_critical(TS_TST_INFO *a, int crit, int lastpos)
{
    return X509v3_get_ext_by_critical(a->extensions, crit, lastpos);
}

X509_EXTENSION *TS_TST_INFO_get_ext(TS_TST_INFO *a, int loc)
{
    return X509v3_get_ext(a->extensions, loc);
}

X509_EXTENSION *TS_TST_INFO_delete_ext(TS_TST_INFO *a, int loc)
{
    return X509v3_delete_ext(a->extensions, loc);
}

int TS_TST_INFO_add_ext(TS_TST_INFO *a, X509_EXTENSION *ex, int loc)
{
    return X509v3_add_ext(&a->extensions, ex, loc) != NULL;
}

void *TS_TST_INFO_get_ext_d2i(TS_TST_INFO *a, int nid, int *crit, int *idx)
{
    return X509V3_get_d2i(a->extensions, nid, crit, idx);
}

int TS_STATUS_INFO_set_status(TS_STATUS_INFO *a, int i)
{
    return ASN1_INTEGER_set(a->status, i);
}

const ASN1_INTEGER *TS_STATUS_INFO_get0_status(const TS_STATUS_INFO *a)
{
    return a->status;
}

const STACK_OF(ASN1_UTF8STRING) *
TS_STATUS_INFO_get0_text(const TS_STATUS_INFO *a)
{
    return a->text;
}

const ASN1_BIT_STRING *TS_STATUS_INFO_get0_failure_info(const TS_STATUS_INFO *a)
{
    return a->failure_info;
}
