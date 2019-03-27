/*
 * Copyright 1999-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/conf.h>
#include <openssl/asn1.h>
#include <openssl/asn1t.h>
#include <openssl/x509v3.h>
#include "ext_dat.h"

static STACK_OF(CONF_VALUE) *i2v_AUTHORITY_INFO_ACCESS(X509V3_EXT_METHOD
                                                       *method, AUTHORITY_INFO_ACCESS
                                                       *ainfo, STACK_OF(CONF_VALUE)
                                                       *ret);
static AUTHORITY_INFO_ACCESS *v2i_AUTHORITY_INFO_ACCESS(X509V3_EXT_METHOD
                                                        *method,
                                                        X509V3_CTX *ctx,
                                                        STACK_OF(CONF_VALUE)
                                                        *nval);

const X509V3_EXT_METHOD v3_info = { NID_info_access, X509V3_EXT_MULTILINE,
    ASN1_ITEM_ref(AUTHORITY_INFO_ACCESS),
    0, 0, 0, 0,
    0, 0,
    (X509V3_EXT_I2V) i2v_AUTHORITY_INFO_ACCESS,
    (X509V3_EXT_V2I)v2i_AUTHORITY_INFO_ACCESS,
    0, 0,
    NULL
};

const X509V3_EXT_METHOD v3_sinfo = { NID_sinfo_access, X509V3_EXT_MULTILINE,
    ASN1_ITEM_ref(AUTHORITY_INFO_ACCESS),
    0, 0, 0, 0,
    0, 0,
    (X509V3_EXT_I2V) i2v_AUTHORITY_INFO_ACCESS,
    (X509V3_EXT_V2I)v2i_AUTHORITY_INFO_ACCESS,
    0, 0,
    NULL
};

ASN1_SEQUENCE(ACCESS_DESCRIPTION) = {
        ASN1_SIMPLE(ACCESS_DESCRIPTION, method, ASN1_OBJECT),
        ASN1_SIMPLE(ACCESS_DESCRIPTION, location, GENERAL_NAME)
} ASN1_SEQUENCE_END(ACCESS_DESCRIPTION)

IMPLEMENT_ASN1_FUNCTIONS(ACCESS_DESCRIPTION)

ASN1_ITEM_TEMPLATE(AUTHORITY_INFO_ACCESS) =
        ASN1_EX_TEMPLATE_TYPE(ASN1_TFLG_SEQUENCE_OF, 0, GeneralNames, ACCESS_DESCRIPTION)
ASN1_ITEM_TEMPLATE_END(AUTHORITY_INFO_ACCESS)

IMPLEMENT_ASN1_FUNCTIONS(AUTHORITY_INFO_ACCESS)

static STACK_OF(CONF_VALUE) *i2v_AUTHORITY_INFO_ACCESS(
    X509V3_EXT_METHOD *method, AUTHORITY_INFO_ACCESS *ainfo,
    STACK_OF(CONF_VALUE) *ret)
{
    ACCESS_DESCRIPTION *desc;
    int i, nlen;
    char objtmp[80], *ntmp;
    CONF_VALUE *vtmp;
    STACK_OF(CONF_VALUE) *tret = ret;

    for (i = 0; i < sk_ACCESS_DESCRIPTION_num(ainfo); i++) {
        STACK_OF(CONF_VALUE) *tmp;

        desc = sk_ACCESS_DESCRIPTION_value(ainfo, i);
        tmp = i2v_GENERAL_NAME(method, desc->location, tret);
        if (tmp == NULL)
            goto err;
        tret = tmp;
        vtmp = sk_CONF_VALUE_value(tret, i);
        i2t_ASN1_OBJECT(objtmp, sizeof(objtmp), desc->method);
        nlen = strlen(objtmp) + 3 + strlen(vtmp->name) + 1;
        ntmp = OPENSSL_malloc(nlen);
        if (ntmp == NULL)
            goto err;
        BIO_snprintf(ntmp, nlen, "%s - %s", objtmp, vtmp->name);
        OPENSSL_free(vtmp->name);
        vtmp->name = ntmp;
    }
    if (ret == NULL && tret == NULL)
        return sk_CONF_VALUE_new_null();

    return tret;
 err:
    X509V3err(X509V3_F_I2V_AUTHORITY_INFO_ACCESS, ERR_R_MALLOC_FAILURE);
    if (ret == NULL && tret != NULL)
        sk_CONF_VALUE_pop_free(tret, X509V3_conf_free);
    return NULL;
}

static AUTHORITY_INFO_ACCESS *v2i_AUTHORITY_INFO_ACCESS(X509V3_EXT_METHOD
                                                        *method,
                                                        X509V3_CTX *ctx,
                                                        STACK_OF(CONF_VALUE)
                                                        *nval)
{
    AUTHORITY_INFO_ACCESS *ainfo = NULL;
    CONF_VALUE *cnf, ctmp;
    ACCESS_DESCRIPTION *acc;
    int i, objlen;
    const int num = sk_CONF_VALUE_num(nval);
    char *objtmp, *ptmp;

    if ((ainfo = sk_ACCESS_DESCRIPTION_new_reserve(NULL, num)) == NULL) {
        X509V3err(X509V3_F_V2I_AUTHORITY_INFO_ACCESS, ERR_R_MALLOC_FAILURE);
        return NULL;
    }
    for (i = 0; i < num; i++) {
        cnf = sk_CONF_VALUE_value(nval, i);
        if ((acc = ACCESS_DESCRIPTION_new()) == NULL) {
            X509V3err(X509V3_F_V2I_AUTHORITY_INFO_ACCESS,
                      ERR_R_MALLOC_FAILURE);
            goto err;
        }
        sk_ACCESS_DESCRIPTION_push(ainfo, acc); /* Cannot fail due to reserve */
        ptmp = strchr(cnf->name, ';');
        if (!ptmp) {
            X509V3err(X509V3_F_V2I_AUTHORITY_INFO_ACCESS,
                      X509V3_R_INVALID_SYNTAX);
            goto err;
        }
        objlen = ptmp - cnf->name;
        ctmp.name = ptmp + 1;
        ctmp.value = cnf->value;
        if (!v2i_GENERAL_NAME_ex(acc->location, method, ctx, &ctmp, 0))
            goto err;
        if ((objtmp = OPENSSL_strndup(cnf->name, objlen)) == NULL) {
            X509V3err(X509V3_F_V2I_AUTHORITY_INFO_ACCESS,
                      ERR_R_MALLOC_FAILURE);
            goto err;
        }
        acc->method = OBJ_txt2obj(objtmp, 0);
        if (!acc->method) {
            X509V3err(X509V3_F_V2I_AUTHORITY_INFO_ACCESS,
                      X509V3_R_BAD_OBJECT);
            ERR_add_error_data(2, "value=", objtmp);
            OPENSSL_free(objtmp);
            goto err;
        }
        OPENSSL_free(objtmp);

    }
    return ainfo;
 err:
    sk_ACCESS_DESCRIPTION_pop_free(ainfo, ACCESS_DESCRIPTION_free);
    return NULL;
}

int i2a_ACCESS_DESCRIPTION(BIO *bp, const ACCESS_DESCRIPTION *a)
{
    i2a_ASN1_OBJECT(bp, a->method);
    return 2;
}
