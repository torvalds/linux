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
#include <openssl/pkcs12.h>

/* Simplified PKCS#12 routines */

static int parse_pk12(PKCS12 *p12, const char *pass, int passlen,
                      EVP_PKEY **pkey, STACK_OF(X509) *ocerts);

static int parse_bags(const STACK_OF(PKCS12_SAFEBAG) *bags, const char *pass,
                      int passlen, EVP_PKEY **pkey, STACK_OF(X509) *ocerts);

static int parse_bag(PKCS12_SAFEBAG *bag, const char *pass, int passlen,
                     EVP_PKEY **pkey, STACK_OF(X509) *ocerts);

/*
 * Parse and decrypt a PKCS#12 structure returning user key, user cert and
 * other (CA) certs. Note either ca should be NULL, *ca should be NULL, or it
 * should point to a valid STACK structure. pkey and cert can be passed
 * uninitialised.
 */

int PKCS12_parse(PKCS12 *p12, const char *pass, EVP_PKEY **pkey, X509 **cert,
                 STACK_OF(X509) **ca)
{
    STACK_OF(X509) *ocerts = NULL;
    X509 *x = NULL;

    if (pkey)
        *pkey = NULL;
    if (cert)
        *cert = NULL;

    /* Check for NULL PKCS12 structure */

    if (!p12) {
        PKCS12err(PKCS12_F_PKCS12_PARSE,
                  PKCS12_R_INVALID_NULL_PKCS12_POINTER);
        return 0;
    }

    /* Check the mac */

    /*
     * If password is zero length or NULL then try verifying both cases to
     * determine which password is correct. The reason for this is that under
     * PKCS#12 password based encryption no password and a zero length
     * password are two different things...
     */

    if (!pass || !*pass) {
        if (PKCS12_verify_mac(p12, NULL, 0))
            pass = NULL;
        else if (PKCS12_verify_mac(p12, "", 0))
            pass = "";
        else {
            PKCS12err(PKCS12_F_PKCS12_PARSE, PKCS12_R_MAC_VERIFY_FAILURE);
            goto err;
        }
    } else if (!PKCS12_verify_mac(p12, pass, -1)) {
        PKCS12err(PKCS12_F_PKCS12_PARSE, PKCS12_R_MAC_VERIFY_FAILURE);
        goto err;
    }

    /* Allocate stack for other certificates */
    ocerts = sk_X509_new_null();

    if (!ocerts) {
        PKCS12err(PKCS12_F_PKCS12_PARSE, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    if (!parse_pk12(p12, pass, -1, pkey, ocerts)) {
        PKCS12err(PKCS12_F_PKCS12_PARSE, PKCS12_R_PARSE_ERROR);
        goto err;
    }

    while ((x = sk_X509_pop(ocerts))) {
        if (pkey && *pkey && cert && !*cert) {
            ERR_set_mark();
            if (X509_check_private_key(x, *pkey)) {
                *cert = x;
                x = NULL;
            }
            ERR_pop_to_mark();
        }

        if (ca && x) {
            if (!*ca)
                *ca = sk_X509_new_null();
            if (!*ca)
                goto err;
            if (!sk_X509_push(*ca, x))
                goto err;
            x = NULL;
        }
        X509_free(x);
    }

    sk_X509_pop_free(ocerts, X509_free);

    return 1;

 err:

    if (pkey) {
        EVP_PKEY_free(*pkey);
        *pkey = NULL;
    }
    if (cert) {
        X509_free(*cert);
        *cert = NULL;
    }
    X509_free(x);
    sk_X509_pop_free(ocerts, X509_free);
    return 0;

}

/* Parse the outer PKCS#12 structure */

static int parse_pk12(PKCS12 *p12, const char *pass, int passlen,
                      EVP_PKEY **pkey, STACK_OF(X509) *ocerts)
{
    STACK_OF(PKCS7) *asafes;
    STACK_OF(PKCS12_SAFEBAG) *bags;
    int i, bagnid;
    PKCS7 *p7;

    if ((asafes = PKCS12_unpack_authsafes(p12)) == NULL)
        return 0;
    for (i = 0; i < sk_PKCS7_num(asafes); i++) {
        p7 = sk_PKCS7_value(asafes, i);
        bagnid = OBJ_obj2nid(p7->type);
        if (bagnid == NID_pkcs7_data) {
            bags = PKCS12_unpack_p7data(p7);
        } else if (bagnid == NID_pkcs7_encrypted) {
            bags = PKCS12_unpack_p7encdata(p7, pass, passlen);
        } else
            continue;
        if (!bags) {
            sk_PKCS7_pop_free(asafes, PKCS7_free);
            return 0;
        }
        if (!parse_bags(bags, pass, passlen, pkey, ocerts)) {
            sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free);
            sk_PKCS7_pop_free(asafes, PKCS7_free);
            return 0;
        }
        sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free);
    }
    sk_PKCS7_pop_free(asafes, PKCS7_free);
    return 1;
}

static int parse_bags(const STACK_OF(PKCS12_SAFEBAG) *bags, const char *pass,
                      int passlen, EVP_PKEY **pkey, STACK_OF(X509) *ocerts)
{
    int i;
    for (i = 0; i < sk_PKCS12_SAFEBAG_num(bags); i++) {
        if (!parse_bag(sk_PKCS12_SAFEBAG_value(bags, i),
                       pass, passlen, pkey, ocerts))
            return 0;
    }
    return 1;
}

static int parse_bag(PKCS12_SAFEBAG *bag, const char *pass, int passlen,
                     EVP_PKEY **pkey, STACK_OF(X509) *ocerts)
{
    PKCS8_PRIV_KEY_INFO *p8;
    X509 *x509;
    const ASN1_TYPE *attrib;
    ASN1_BMPSTRING *fname = NULL;
    ASN1_OCTET_STRING *lkid = NULL;

    if ((attrib = PKCS12_SAFEBAG_get0_attr(bag, NID_friendlyName)))
        fname = attrib->value.bmpstring;

    if ((attrib = PKCS12_SAFEBAG_get0_attr(bag, NID_localKeyID)))
        lkid = attrib->value.octet_string;

    switch (PKCS12_SAFEBAG_get_nid(bag)) {
    case NID_keyBag:
        if (!pkey || *pkey)
            return 1;
        *pkey = EVP_PKCS82PKEY(PKCS12_SAFEBAG_get0_p8inf(bag));
        if (*pkey == NULL)
            return 0;
        break;

    case NID_pkcs8ShroudedKeyBag:
        if (!pkey || *pkey)
            return 1;
        if ((p8 = PKCS12_decrypt_skey(bag, pass, passlen)) == NULL)
            return 0;
        *pkey = EVP_PKCS82PKEY(p8);
        PKCS8_PRIV_KEY_INFO_free(p8);
        if (!(*pkey))
            return 0;
        break;

    case NID_certBag:
        if (PKCS12_SAFEBAG_get_bag_nid(bag) != NID_x509Certificate)
            return 1;
        if ((x509 = PKCS12_SAFEBAG_get1_cert(bag)) == NULL)
            return 0;
        if (lkid && !X509_keyid_set1(x509, lkid->data, lkid->length)) {
            X509_free(x509);
            return 0;
        }
        if (fname) {
            int len, r;
            unsigned char *data;
            len = ASN1_STRING_to_UTF8(&data, fname);
            if (len >= 0) {
                r = X509_alias_set1(x509, data, len);
                OPENSSL_free(data);
                if (!r) {
                    X509_free(x509);
                    return 0;
                }
            }
        }

        if (!sk_X509_push(ocerts, x509)) {
            X509_free(x509);
            return 0;
        }

        break;

    case NID_safeContentsBag:
        return parse_bags(PKCS12_SAFEBAG_get0_safes(bag), pass, passlen, pkey,
                          ocerts);

    default:
        return 1;
    }
    return 1;
}
