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
#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/conf.h>
#include <openssl/x509v3.h>
#include "ext_dat.h"

static STACK_OF(CONF_VALUE) *i2v_BASIC_CONSTRAINTS(X509V3_EXT_METHOD *method,
                                                   BASIC_CONSTRAINTS *bcons,
                                                   STACK_OF(CONF_VALUE)
                                                   *extlist);
static BASIC_CONSTRAINTS *v2i_BASIC_CONSTRAINTS(X509V3_EXT_METHOD *method,
                                                X509V3_CTX *ctx,
                                                STACK_OF(CONF_VALUE) *values);

const X509V3_EXT_METHOD v3_bcons = {
    NID_basic_constraints, 0,
    ASN1_ITEM_ref(BASIC_CONSTRAINTS),
    0, 0, 0, 0,
    0, 0,
    (X509V3_EXT_I2V) i2v_BASIC_CONSTRAINTS,
    (X509V3_EXT_V2I)v2i_BASIC_CONSTRAINTS,
    NULL, NULL,
    NULL
};

ASN1_SEQUENCE(BASIC_CONSTRAINTS) = {
        ASN1_OPT(BASIC_CONSTRAINTS, ca, ASN1_FBOOLEAN),
        ASN1_OPT(BASIC_CONSTRAINTS, pathlen, ASN1_INTEGER)
} ASN1_SEQUENCE_END(BASIC_CONSTRAINTS)

IMPLEMENT_ASN1_FUNCTIONS(BASIC_CONSTRAINTS)

static STACK_OF(CONF_VALUE) *i2v_BASIC_CONSTRAINTS(X509V3_EXT_METHOD *method,
                                                   BASIC_CONSTRAINTS *bcons,
                                                   STACK_OF(CONF_VALUE)
                                                   *extlist)
{
    X509V3_add_value_bool("CA", bcons->ca, &extlist);
    X509V3_add_value_int("pathlen", bcons->pathlen, &extlist);
    return extlist;
}

static BASIC_CONSTRAINTS *v2i_BASIC_CONSTRAINTS(X509V3_EXT_METHOD *method,
                                                X509V3_CTX *ctx,
                                                STACK_OF(CONF_VALUE) *values)
{
    BASIC_CONSTRAINTS *bcons = NULL;
    CONF_VALUE *val;
    int i;

    if ((bcons = BASIC_CONSTRAINTS_new()) == NULL) {
        X509V3err(X509V3_F_V2I_BASIC_CONSTRAINTS, ERR_R_MALLOC_FAILURE);
        return NULL;
    }
    for (i = 0; i < sk_CONF_VALUE_num(values); i++) {
        val = sk_CONF_VALUE_value(values, i);
        if (strcmp(val->name, "CA") == 0) {
            if (!X509V3_get_value_bool(val, &bcons->ca))
                goto err;
        } else if (strcmp(val->name, "pathlen") == 0) {
            if (!X509V3_get_value_int(val, &bcons->pathlen))
                goto err;
        } else {
            X509V3err(X509V3_F_V2I_BASIC_CONSTRAINTS, X509V3_R_INVALID_NAME);
            X509V3_conf_err(val);
            goto err;
        }
    }
    return bcons;
 err:
    BASIC_CONSTRAINTS_free(bcons);
    return NULL;
}
