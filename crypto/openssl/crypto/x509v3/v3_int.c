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

const X509V3_EXT_METHOD v3_crl_num = {
    NID_crl_number, 0, ASN1_ITEM_ref(ASN1_INTEGER),
    0, 0, 0, 0,
    (X509V3_EXT_I2S)i2s_ASN1_INTEGER,
    0,
    0, 0, 0, 0, NULL
};

const X509V3_EXT_METHOD v3_delta_crl = {
    NID_delta_crl, 0, ASN1_ITEM_ref(ASN1_INTEGER),
    0, 0, 0, 0,
    (X509V3_EXT_I2S)i2s_ASN1_INTEGER,
    0,
    0, 0, 0, 0, NULL
};

static void *s2i_asn1_int(X509V3_EXT_METHOD *meth, X509V3_CTX *ctx,
                          const char *value)
{
    return s2i_ASN1_INTEGER(meth, value);
}

const X509V3_EXT_METHOD v3_inhibit_anyp = {
    NID_inhibit_any_policy, 0, ASN1_ITEM_ref(ASN1_INTEGER),
    0, 0, 0, 0,
    (X509V3_EXT_I2S)i2s_ASN1_INTEGER,
    (X509V3_EXT_S2I)s2i_asn1_int,
    0, 0, 0, 0, NULL
};
