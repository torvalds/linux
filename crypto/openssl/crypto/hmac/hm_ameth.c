/*
 * Copyright 2007-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include <openssl/evp.h>
#include "internal/asn1_int.h"
#include "internal/evp_int.h"

/*
 * HMAC "ASN1" method. This is just here to indicate the maximum HMAC output
 * length and to free up an HMAC key.
 */

static int hmac_size(const EVP_PKEY *pkey)
{
    return EVP_MAX_MD_SIZE;
}

static void hmac_key_free(EVP_PKEY *pkey)
{
    ASN1_OCTET_STRING *os = EVP_PKEY_get0(pkey);
    if (os) {
        if (os->data)
            OPENSSL_cleanse(os->data, os->length);
        ASN1_OCTET_STRING_free(os);
    }
}

static int hmac_pkey_ctrl(EVP_PKEY *pkey, int op, long arg1, void *arg2)
{
    switch (op) {
    case ASN1_PKEY_CTRL_DEFAULT_MD_NID:
        *(int *)arg2 = NID_sha256;
        return 1;

    default:
        return -2;
    }
}

static int hmac_pkey_public_cmp(const EVP_PKEY *a, const EVP_PKEY *b)
{
    return ASN1_OCTET_STRING_cmp(EVP_PKEY_get0(a), EVP_PKEY_get0(b));
}

static int hmac_set_priv_key(EVP_PKEY *pkey, const unsigned char *priv,
                             size_t len)
{
    ASN1_OCTET_STRING *os;

    if (pkey->pkey.ptr != NULL)
        return 0;

    os = ASN1_OCTET_STRING_new();
    if (os == NULL)
        return 0;


    if (!ASN1_OCTET_STRING_set(os, priv, len)) {
        ASN1_OCTET_STRING_free(os);
        return 0;
    }

    pkey->pkey.ptr = os;
    return 1;
}

static int hmac_get_priv_key(const EVP_PKEY *pkey, unsigned char *priv,
                             size_t *len)
{
    ASN1_OCTET_STRING *os = (ASN1_OCTET_STRING *)pkey->pkey.ptr;

    if (priv == NULL) {
        *len = ASN1_STRING_length(os);
        return 1;
    }

    if (os == NULL || *len < (size_t)ASN1_STRING_length(os))
        return 0;

    *len = ASN1_STRING_length(os);
    memcpy(priv, ASN1_STRING_get0_data(os), *len);

    return 1;
}

const EVP_PKEY_ASN1_METHOD hmac_asn1_meth = {
    EVP_PKEY_HMAC,
    EVP_PKEY_HMAC,
    0,

    "HMAC",
    "OpenSSL HMAC method",

    0, 0, hmac_pkey_public_cmp, 0,

    0, 0, 0,

    hmac_size,
    0, 0,
    0, 0, 0, 0, 0, 0, 0,

    hmac_key_free,
    hmac_pkey_ctrl,
    NULL,
    NULL,

    NULL,
    NULL,
    NULL,

    NULL,
    NULL,
    NULL,

    hmac_set_priv_key,
    NULL,
    hmac_get_priv_key,
    NULL,
};
