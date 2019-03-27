/*
 * Copyright 2003-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/asn1t.h>
#include <openssl/conf.h>
#include <openssl/x509v3.h>
#include "ext_dat.h"

static void *v2i_POLICY_MAPPINGS(const X509V3_EXT_METHOD *method,
                                 X509V3_CTX *ctx, STACK_OF(CONF_VALUE) *nval);
static STACK_OF(CONF_VALUE) *i2v_POLICY_MAPPINGS(const X509V3_EXT_METHOD
                                                 *method, void *pmps, STACK_OF(CONF_VALUE)
                                                 *extlist);

const X509V3_EXT_METHOD v3_policy_mappings = {
    NID_policy_mappings, 0,
    ASN1_ITEM_ref(POLICY_MAPPINGS),
    0, 0, 0, 0,
    0, 0,
    i2v_POLICY_MAPPINGS,
    v2i_POLICY_MAPPINGS,
    0, 0,
    NULL
};

ASN1_SEQUENCE(POLICY_MAPPING) = {
        ASN1_SIMPLE(POLICY_MAPPING, issuerDomainPolicy, ASN1_OBJECT),
        ASN1_SIMPLE(POLICY_MAPPING, subjectDomainPolicy, ASN1_OBJECT)
} ASN1_SEQUENCE_END(POLICY_MAPPING)

ASN1_ITEM_TEMPLATE(POLICY_MAPPINGS) =
        ASN1_EX_TEMPLATE_TYPE(ASN1_TFLG_SEQUENCE_OF, 0, POLICY_MAPPINGS,
                                                                POLICY_MAPPING)
ASN1_ITEM_TEMPLATE_END(POLICY_MAPPINGS)

IMPLEMENT_ASN1_ALLOC_FUNCTIONS(POLICY_MAPPING)

static STACK_OF(CONF_VALUE) *i2v_POLICY_MAPPINGS(const X509V3_EXT_METHOD
                                                 *method, void *a, STACK_OF(CONF_VALUE)
                                                 *ext_list)
{
    POLICY_MAPPINGS *pmaps = a;
    POLICY_MAPPING *pmap;
    int i;
    char obj_tmp1[80];
    char obj_tmp2[80];

    for (i = 0; i < sk_POLICY_MAPPING_num(pmaps); i++) {
        pmap = sk_POLICY_MAPPING_value(pmaps, i);
        i2t_ASN1_OBJECT(obj_tmp1, 80, pmap->issuerDomainPolicy);
        i2t_ASN1_OBJECT(obj_tmp2, 80, pmap->subjectDomainPolicy);
        X509V3_add_value(obj_tmp1, obj_tmp2, &ext_list);
    }
    return ext_list;
}

static void *v2i_POLICY_MAPPINGS(const X509V3_EXT_METHOD *method,
                                 X509V3_CTX *ctx, STACK_OF(CONF_VALUE) *nval)
{
    POLICY_MAPPING *pmap = NULL;
    ASN1_OBJECT *obj1 = NULL, *obj2 = NULL;
    CONF_VALUE *val;
    POLICY_MAPPINGS *pmaps;
    const int num = sk_CONF_VALUE_num(nval);
    int i;

    if ((pmaps = sk_POLICY_MAPPING_new_reserve(NULL, num)) == NULL) {
        X509V3err(X509V3_F_V2I_POLICY_MAPPINGS, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    for (i = 0; i < num; i++) {
        val = sk_CONF_VALUE_value(nval, i);
        if (!val->value || !val->name) {
            X509V3err(X509V3_F_V2I_POLICY_MAPPINGS,
                      X509V3_R_INVALID_OBJECT_IDENTIFIER);
            X509V3_conf_err(val);
            goto err;
        }
        obj1 = OBJ_txt2obj(val->name, 0);
        obj2 = OBJ_txt2obj(val->value, 0);
        if (!obj1 || !obj2) {
            X509V3err(X509V3_F_V2I_POLICY_MAPPINGS,
                      X509V3_R_INVALID_OBJECT_IDENTIFIER);
            X509V3_conf_err(val);
            goto err;
        }
        pmap = POLICY_MAPPING_new();
        if (pmap == NULL) {
            X509V3err(X509V3_F_V2I_POLICY_MAPPINGS, ERR_R_MALLOC_FAILURE);
            goto err;
        }
        pmap->issuerDomainPolicy = obj1;
        pmap->subjectDomainPolicy = obj2;
        obj1 = obj2 = NULL;
        sk_POLICY_MAPPING_push(pmaps, pmap); /* no failure as it was reserved */
    }
    return pmaps;
 err:
    ASN1_OBJECT_free(obj1);
    ASN1_OBJECT_free(obj2);
    sk_POLICY_MAPPING_pop_free(pmaps, POLICY_MAPPING_free);
    return NULL;
}
