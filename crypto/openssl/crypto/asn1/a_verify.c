/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <time.h>
#include <sys/types.h>

#include "internal/cryptlib.h"

#include <openssl/bn.h>
#include <openssl/x509.h>
#include <openssl/objects.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include "internal/asn1_int.h"
#include "internal/evp_int.h"

#ifndef NO_ASN1_OLD

int ASN1_verify(i2d_of_void *i2d, X509_ALGOR *a, ASN1_BIT_STRING *signature,
                char *data, EVP_PKEY *pkey)
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    const EVP_MD *type;
    unsigned char *p, *buf_in = NULL;
    int ret = -1, i, inl;

    if (ctx == NULL) {
        ASN1err(ASN1_F_ASN1_VERIFY, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    i = OBJ_obj2nid(a->algorithm);
    type = EVP_get_digestbyname(OBJ_nid2sn(i));
    if (type == NULL) {
        ASN1err(ASN1_F_ASN1_VERIFY, ASN1_R_UNKNOWN_MESSAGE_DIGEST_ALGORITHM);
        goto err;
    }

    if (signature->type == V_ASN1_BIT_STRING && signature->flags & 0x7) {
        ASN1err(ASN1_F_ASN1_VERIFY, ASN1_R_INVALID_BIT_STRING_BITS_LEFT);
        goto err;
    }

    inl = i2d(data, NULL);
    if (inl <= 0) {
        ASN1err(ASN1_F_ASN1_VERIFY, ERR_R_INTERNAL_ERROR);
        goto err;
    }
    buf_in = OPENSSL_malloc((unsigned int)inl);
    if (buf_in == NULL) {
        ASN1err(ASN1_F_ASN1_VERIFY, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    p = buf_in;

    i2d(data, &p);
    ret = EVP_VerifyInit_ex(ctx, type, NULL)
        && EVP_VerifyUpdate(ctx, (unsigned char *)buf_in, inl);

    OPENSSL_clear_free(buf_in, (unsigned int)inl);

    if (!ret) {
        ASN1err(ASN1_F_ASN1_VERIFY, ERR_R_EVP_LIB);
        goto err;
    }
    ret = -1;

    if (EVP_VerifyFinal(ctx, (unsigned char *)signature->data,
                        (unsigned int)signature->length, pkey) <= 0) {
        ASN1err(ASN1_F_ASN1_VERIFY, ERR_R_EVP_LIB);
        ret = 0;
        goto err;
    }
    ret = 1;
 err:
    EVP_MD_CTX_free(ctx);
    return ret;
}

#endif

int ASN1_item_verify(const ASN1_ITEM *it, X509_ALGOR *a,
                     ASN1_BIT_STRING *signature, void *asn, EVP_PKEY *pkey)
{
    EVP_MD_CTX *ctx = NULL;
    unsigned char *buf_in = NULL;
    int ret = -1, inl = 0;
    int mdnid, pknid;
    size_t inll = 0;

    if (!pkey) {
        ASN1err(ASN1_F_ASN1_ITEM_VERIFY, ERR_R_PASSED_NULL_PARAMETER);
        return -1;
    }

    if (signature->type == V_ASN1_BIT_STRING && signature->flags & 0x7) {
        ASN1err(ASN1_F_ASN1_ITEM_VERIFY, ASN1_R_INVALID_BIT_STRING_BITS_LEFT);
        return -1;
    }

    ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        ASN1err(ASN1_F_ASN1_ITEM_VERIFY, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    /* Convert signature OID into digest and public key OIDs */
    if (!OBJ_find_sigid_algs(OBJ_obj2nid(a->algorithm), &mdnid, &pknid)) {
        ASN1err(ASN1_F_ASN1_ITEM_VERIFY, ASN1_R_UNKNOWN_SIGNATURE_ALGORITHM);
        goto err;
    }
    if (mdnid == NID_undef) {
        if (!pkey->ameth || !pkey->ameth->item_verify) {
            ASN1err(ASN1_F_ASN1_ITEM_VERIFY,
                    ASN1_R_UNKNOWN_SIGNATURE_ALGORITHM);
            goto err;
        }
        ret = pkey->ameth->item_verify(ctx, it, asn, a, signature, pkey);
        /*
         * Return value of 2 means carry on, anything else means we exit
         * straight away: either a fatal error of the underlying verification
         * routine handles all verification.
         */
        if (ret != 2)
            goto err;
        ret = -1;
    } else {
        const EVP_MD *type = EVP_get_digestbynid(mdnid);

        if (type == NULL) {
            ASN1err(ASN1_F_ASN1_ITEM_VERIFY,
                    ASN1_R_UNKNOWN_MESSAGE_DIGEST_ALGORITHM);
            goto err;
        }

        /* Check public key OID matches public key type */
        if (EVP_PKEY_type(pknid) != pkey->ameth->pkey_id) {
            ASN1err(ASN1_F_ASN1_ITEM_VERIFY, ASN1_R_WRONG_PUBLIC_KEY_TYPE);
            goto err;
        }

        if (!EVP_DigestVerifyInit(ctx, NULL, type, NULL, pkey)) {
            ASN1err(ASN1_F_ASN1_ITEM_VERIFY, ERR_R_EVP_LIB);
            ret = 0;
            goto err;
        }

    }

    inl = ASN1_item_i2d(asn, &buf_in, it);
    if (inl <= 0) {
        ASN1err(ASN1_F_ASN1_ITEM_VERIFY, ERR_R_INTERNAL_ERROR);
        goto err;
    }
    if (buf_in == NULL) {
        ASN1err(ASN1_F_ASN1_ITEM_VERIFY, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    inll = inl;

    ret = EVP_DigestVerify(ctx, signature->data, (size_t)signature->length,
                           buf_in, inl);
    if (ret <= 0) {
        ASN1err(ASN1_F_ASN1_ITEM_VERIFY, ERR_R_EVP_LIB);
        goto err;
    }
    ret = 1;
 err:
    OPENSSL_clear_free(buf_in, inll);
    EVP_MD_CTX_free(ctx);
    return ret;
}
