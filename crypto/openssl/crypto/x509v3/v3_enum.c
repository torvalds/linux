/*
 * Copyright 1999-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/x509v3.h>
#include "ext_dat.h"

static ENUMERATED_NAMES crl_reasons[] = {
    {CRL_REASON_UNSPECIFIED, "Unspecified", "unspecified"},
    {CRL_REASON_KEY_COMPROMISE, "Key Compromise", "keyCompromise"},
    {CRL_REASON_CA_COMPROMISE, "CA Compromise", "CACompromise"},
    {CRL_REASON_AFFILIATION_CHANGED, "Affiliation Changed",
     "affiliationChanged"},
    {CRL_REASON_SUPERSEDED, "Superseded", "superseded"},
    {CRL_REASON_CESSATION_OF_OPERATION,
     "Cessation Of Operation", "cessationOfOperation"},
    {CRL_REASON_CERTIFICATE_HOLD, "Certificate Hold", "certificateHold"},
    {CRL_REASON_REMOVE_FROM_CRL, "Remove From CRL", "removeFromCRL"},
    {CRL_REASON_PRIVILEGE_WITHDRAWN, "Privilege Withdrawn",
     "privilegeWithdrawn"},
    {CRL_REASON_AA_COMPROMISE, "AA Compromise", "AACompromise"},
    {-1, NULL, NULL}
};

const X509V3_EXT_METHOD v3_crl_reason = {
    NID_crl_reason, 0, ASN1_ITEM_ref(ASN1_ENUMERATED),
    0, 0, 0, 0,
    (X509V3_EXT_I2S)i2s_ASN1_ENUMERATED_TABLE,
    0,
    0, 0, 0, 0,
    crl_reasons
};

char *i2s_ASN1_ENUMERATED_TABLE(X509V3_EXT_METHOD *method,
                                const ASN1_ENUMERATED *e)
{
    ENUMERATED_NAMES *enam;
    long strval;

    strval = ASN1_ENUMERATED_get(e);
    for (enam = method->usr_data; enam->lname; enam++) {
        if (strval == enam->bitnum)
            return OPENSSL_strdup(enam->lname);
    }
    return i2s_ASN1_ENUMERATED(method, e);
}
