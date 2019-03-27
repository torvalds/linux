/*
 * Copyright 2001-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/pkcs12.h>
#include "internal/x509_int.h"

X509_SIG *PKCS8_encrypt(int pbe_nid, const EVP_CIPHER *cipher,
                        const char *pass, int passlen,
                        unsigned char *salt, int saltlen, int iter,
                        PKCS8_PRIV_KEY_INFO *p8inf)
{
    X509_SIG *p8 = NULL;
    X509_ALGOR *pbe;

    if (pbe_nid == -1)
        pbe = PKCS5_pbe2_set(cipher, iter, salt, saltlen);
    else if (EVP_PBE_find(EVP_PBE_TYPE_PRF, pbe_nid, NULL, NULL, 0))
        pbe = PKCS5_pbe2_set_iv(cipher, iter, salt, saltlen, NULL, pbe_nid);
    else {
        ERR_clear_error();
        pbe = PKCS5_pbe_set(pbe_nid, iter, salt, saltlen);
    }
    if (!pbe) {
        PKCS12err(PKCS12_F_PKCS8_ENCRYPT, ERR_R_ASN1_LIB);
        return NULL;
    }
    p8 = PKCS8_set0_pbe(pass, passlen, p8inf, pbe);
    if (p8 == NULL) {
        X509_ALGOR_free(pbe);
        return NULL;
    }

    return p8;
}

X509_SIG *PKCS8_set0_pbe(const char *pass, int passlen,
                         PKCS8_PRIV_KEY_INFO *p8inf, X509_ALGOR *pbe)
{
    X509_SIG *p8;
    ASN1_OCTET_STRING *enckey;

    enckey =
        PKCS12_item_i2d_encrypt(pbe, ASN1_ITEM_rptr(PKCS8_PRIV_KEY_INFO),
                                pass, passlen, p8inf, 1);
    if (!enckey) {
        PKCS12err(PKCS12_F_PKCS8_SET0_PBE, PKCS12_R_ENCRYPT_ERROR);
        return NULL;
    }

    p8 = OPENSSL_zalloc(sizeof(*p8));

    if (p8 == NULL) {
        PKCS12err(PKCS12_F_PKCS8_SET0_PBE, ERR_R_MALLOC_FAILURE);
        ASN1_OCTET_STRING_free(enckey);
        return NULL;
    }
    p8->algor = pbe;
    p8->digest = enckey;

    return p8;
}
