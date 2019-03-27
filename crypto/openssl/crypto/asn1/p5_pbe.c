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
#include <openssl/asn1t.h>
#include <openssl/x509.h>
#include <openssl/rand.h>

/* PKCS#5 password based encryption structure */

ASN1_SEQUENCE(PBEPARAM) = {
        ASN1_SIMPLE(PBEPARAM, salt, ASN1_OCTET_STRING),
        ASN1_SIMPLE(PBEPARAM, iter, ASN1_INTEGER)
} ASN1_SEQUENCE_END(PBEPARAM)

IMPLEMENT_ASN1_FUNCTIONS(PBEPARAM)

/* Set an algorithm identifier for a PKCS#5 PBE algorithm */

int PKCS5_pbe_set0_algor(X509_ALGOR *algor, int alg, int iter,
                         const unsigned char *salt, int saltlen)
{
    PBEPARAM *pbe = NULL;
    ASN1_STRING *pbe_str = NULL;
    unsigned char *sstr = NULL;

    pbe = PBEPARAM_new();
    if (pbe == NULL) {
        ASN1err(ASN1_F_PKCS5_PBE_SET0_ALGOR, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    if (iter <= 0)
        iter = PKCS5_DEFAULT_ITER;
    if (!ASN1_INTEGER_set(pbe->iter, iter)) {
        ASN1err(ASN1_F_PKCS5_PBE_SET0_ALGOR, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    if (!saltlen)
        saltlen = PKCS5_SALT_LEN;

    sstr = OPENSSL_malloc(saltlen);
    if (sstr == NULL) {
        ASN1err(ASN1_F_PKCS5_PBE_SET0_ALGOR, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    if (salt)
        memcpy(sstr, salt, saltlen);
    else if (RAND_bytes(sstr, saltlen) <= 0)
        goto err;

    ASN1_STRING_set0(pbe->salt, sstr, saltlen);
    sstr = NULL;

    if (!ASN1_item_pack(pbe, ASN1_ITEM_rptr(PBEPARAM), &pbe_str)) {
        ASN1err(ASN1_F_PKCS5_PBE_SET0_ALGOR, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    PBEPARAM_free(pbe);
    pbe = NULL;

    if (X509_ALGOR_set0(algor, OBJ_nid2obj(alg), V_ASN1_SEQUENCE, pbe_str))
        return 1;

 err:
    OPENSSL_free(sstr);
    PBEPARAM_free(pbe);
    ASN1_STRING_free(pbe_str);
    return 0;
}

/* Return an algorithm identifier for a PKCS#5 PBE algorithm */

X509_ALGOR *PKCS5_pbe_set(int alg, int iter,
                          const unsigned char *salt, int saltlen)
{
    X509_ALGOR *ret;
    ret = X509_ALGOR_new();
    if (ret == NULL) {
        ASN1err(ASN1_F_PKCS5_PBE_SET, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    if (PKCS5_pbe_set0_algor(ret, alg, iter, salt, saltlen))
        return ret;

    X509_ALGOR_free(ret);
    return NULL;
}
