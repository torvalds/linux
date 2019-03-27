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
#include "p12_lcl.h"

static int pkcs12_add_bag(STACK_OF(PKCS12_SAFEBAG) **pbags,
                          PKCS12_SAFEBAG *bag);

static int copy_bag_attr(PKCS12_SAFEBAG *bag, EVP_PKEY *pkey, int nid)
{
    int idx;
    X509_ATTRIBUTE *attr;
    idx = EVP_PKEY_get_attr_by_NID(pkey, nid, -1);
    if (idx < 0)
        return 1;
    attr = EVP_PKEY_get_attr(pkey, idx);
    if (!X509at_add1_attr(&bag->attrib, attr))
        return 0;
    return 1;
}

PKCS12 *PKCS12_create(const char *pass, const char *name, EVP_PKEY *pkey, X509 *cert,
                      STACK_OF(X509) *ca, int nid_key, int nid_cert, int iter,
                      int mac_iter, int keytype)
{
    PKCS12 *p12 = NULL;
    STACK_OF(PKCS7) *safes = NULL;
    STACK_OF(PKCS12_SAFEBAG) *bags = NULL;
    PKCS12_SAFEBAG *bag = NULL;
    int i;
    unsigned char keyid[EVP_MAX_MD_SIZE];
    unsigned int keyidlen = 0;

    /* Set defaults */
    if (!nid_cert)
#ifdef OPENSSL_NO_RC2
        nid_cert = NID_pbe_WithSHA1And3_Key_TripleDES_CBC;
#else
        nid_cert = NID_pbe_WithSHA1And40BitRC2_CBC;
#endif
    if (!nid_key)
        nid_key = NID_pbe_WithSHA1And3_Key_TripleDES_CBC;
    if (!iter)
        iter = PKCS12_DEFAULT_ITER;
    if (!mac_iter)
        mac_iter = 1;

    if (!pkey && !cert && !ca) {
        PKCS12err(PKCS12_F_PKCS12_CREATE, PKCS12_R_INVALID_NULL_ARGUMENT);
        return NULL;
    }

    if (pkey && cert) {
        if (!X509_check_private_key(cert, pkey))
            return NULL;
        X509_digest(cert, EVP_sha1(), keyid, &keyidlen);
    }

    if (cert) {
        bag = PKCS12_add_cert(&bags, cert);
        if (name && !PKCS12_add_friendlyname(bag, name, -1))
            goto err;
        if (keyidlen && !PKCS12_add_localkeyid(bag, keyid, keyidlen))
            goto err;
    }

    /* Add all other certificates */
    for (i = 0; i < sk_X509_num(ca); i++) {
        if (!PKCS12_add_cert(&bags, sk_X509_value(ca, i)))
            goto err;
    }

    if (bags && !PKCS12_add_safe(&safes, bags, nid_cert, iter, pass))
        goto err;

    sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free);
    bags = NULL;

    if (pkey) {
        bag = PKCS12_add_key(&bags, pkey, keytype, iter, nid_key, pass);

        if (!bag)
            goto err;

        if (!copy_bag_attr(bag, pkey, NID_ms_csp_name))
            goto err;
        if (!copy_bag_attr(bag, pkey, NID_LocalKeySet))
            goto err;

        if (name && !PKCS12_add_friendlyname(bag, name, -1))
            goto err;
        if (keyidlen && !PKCS12_add_localkeyid(bag, keyid, keyidlen))
            goto err;
    }

    if (bags && !PKCS12_add_safe(&safes, bags, -1, 0, NULL))
        goto err;

    sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free);
    bags = NULL;

    p12 = PKCS12_add_safes(safes, 0);

    if (!p12)
        goto err;

    sk_PKCS7_pop_free(safes, PKCS7_free);

    safes = NULL;

    if ((mac_iter != -1) &&
        !PKCS12_set_mac(p12, pass, -1, NULL, 0, mac_iter, NULL))
        goto err;

    return p12;

 err:
    PKCS12_free(p12);
    sk_PKCS7_pop_free(safes, PKCS7_free);
    sk_PKCS12_SAFEBAG_pop_free(bags, PKCS12_SAFEBAG_free);
    return NULL;

}

PKCS12_SAFEBAG *PKCS12_add_cert(STACK_OF(PKCS12_SAFEBAG) **pbags, X509 *cert)
{
    PKCS12_SAFEBAG *bag = NULL;
    char *name;
    int namelen = -1;
    unsigned char *keyid;
    int keyidlen = -1;

    /* Add user certificate */
    if ((bag = PKCS12_SAFEBAG_create_cert(cert)) == NULL)
        goto err;

    /*
     * Use friendlyName and localKeyID in certificate. (if present)
     */

    name = (char *)X509_alias_get0(cert, &namelen);

    if (name && !PKCS12_add_friendlyname(bag, name, namelen))
        goto err;

    keyid = X509_keyid_get0(cert, &keyidlen);

    if (keyid && !PKCS12_add_localkeyid(bag, keyid, keyidlen))
        goto err;

    if (!pkcs12_add_bag(pbags, bag))
        goto err;

    return bag;

 err:
    PKCS12_SAFEBAG_free(bag);
    return NULL;

}

PKCS12_SAFEBAG *PKCS12_add_key(STACK_OF(PKCS12_SAFEBAG) **pbags,
                               EVP_PKEY *key, int key_usage, int iter,
                               int nid_key, const char *pass)
{

    PKCS12_SAFEBAG *bag = NULL;
    PKCS8_PRIV_KEY_INFO *p8 = NULL;

    /* Make a PKCS#8 structure */
    if ((p8 = EVP_PKEY2PKCS8(key)) == NULL)
        goto err;
    if (key_usage && !PKCS8_add_keyusage(p8, key_usage))
        goto err;
    if (nid_key != -1) {
        bag = PKCS12_SAFEBAG_create_pkcs8_encrypt(nid_key, pass, -1, NULL, 0,
                                                  iter, p8);
        PKCS8_PRIV_KEY_INFO_free(p8);
    } else
        bag = PKCS12_SAFEBAG_create0_p8inf(p8);

    if (!bag)
        goto err;

    if (!pkcs12_add_bag(pbags, bag))
        goto err;

    return bag;

 err:
    PKCS12_SAFEBAG_free(bag);
    return NULL;

}

int PKCS12_add_safe(STACK_OF(PKCS7) **psafes, STACK_OF(PKCS12_SAFEBAG) *bags,
                    int nid_safe, int iter, const char *pass)
{
    PKCS7 *p7 = NULL;
    int free_safes = 0;

    if (!*psafes) {
        *psafes = sk_PKCS7_new_null();
        if (!*psafes)
            return 0;
        free_safes = 1;
    } else
        free_safes = 0;

    if (nid_safe == 0)
#ifdef OPENSSL_NO_RC2
        nid_safe = NID_pbe_WithSHA1And3_Key_TripleDES_CBC;
#else
        nid_safe = NID_pbe_WithSHA1And40BitRC2_CBC;
#endif

    if (nid_safe == -1)
        p7 = PKCS12_pack_p7data(bags);
    else
        p7 = PKCS12_pack_p7encdata(nid_safe, pass, -1, NULL, 0, iter, bags);
    if (!p7)
        goto err;

    if (!sk_PKCS7_push(*psafes, p7))
        goto err;

    return 1;

 err:
    if (free_safes) {
        sk_PKCS7_free(*psafes);
        *psafes = NULL;
    }
    PKCS7_free(p7);
    return 0;

}

static int pkcs12_add_bag(STACK_OF(PKCS12_SAFEBAG) **pbags,
                          PKCS12_SAFEBAG *bag)
{
    int free_bags;
    if (!pbags)
        return 1;
    if (!*pbags) {
        *pbags = sk_PKCS12_SAFEBAG_new_null();
        if (!*pbags)
            return 0;
        free_bags = 1;
    } else
        free_bags = 0;

    if (!sk_PKCS12_SAFEBAG_push(*pbags, bag)) {
        if (free_bags) {
            sk_PKCS12_SAFEBAG_free(*pbags);
            *pbags = NULL;
        }
        return 0;
    }

    return 1;

}

PKCS12 *PKCS12_add_safes(STACK_OF(PKCS7) *safes, int nid_p7)
{
    PKCS12 *p12;
    if (nid_p7 <= 0)
        nid_p7 = NID_pkcs7_data;
    p12 = PKCS12_init(nid_p7);

    if (!p12)
        return NULL;

    if (!PKCS12_pack_authsafes(p12, safes)) {
        PKCS12_free(p12);
        return NULL;
    }

    return p12;

}
