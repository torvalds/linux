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
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/objects.h>
#include <openssl/buffer.h>
#include "internal/asn1_int.h"
#include "internal/evp_int.h"

#ifndef NO_ASN1_OLD

int ASN1_sign(i2d_of_void *i2d, X509_ALGOR *algor1, X509_ALGOR *algor2,
              ASN1_BIT_STRING *signature, char *data, EVP_PKEY *pkey,
              const EVP_MD *type)
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned char *p, *buf_in = NULL, *buf_out = NULL;
    int i, inl = 0, outl = 0;
    size_t inll = 0, outll = 0;
    X509_ALGOR *a;

    if (ctx == NULL) {
        ASN1err(ASN1_F_ASN1_SIGN, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    for (i = 0; i < 2; i++) {
        if (i == 0)
            a = algor1;
        else
            a = algor2;
        if (a == NULL)
            continue;
        if (type->pkey_type == NID_dsaWithSHA1) {
            /*
             * special case: RFC 2459 tells us to omit 'parameters' with
             * id-dsa-with-sha1
             */
            ASN1_TYPE_free(a->parameter);
            a->parameter = NULL;
        } else if ((a->parameter == NULL) ||
                   (a->parameter->type != V_ASN1_NULL)) {
            ASN1_TYPE_free(a->parameter);
            if ((a->parameter = ASN1_TYPE_new()) == NULL)
                goto err;
            a->parameter->type = V_ASN1_NULL;
        }
        ASN1_OBJECT_free(a->algorithm);
        a->algorithm = OBJ_nid2obj(type->pkey_type);
        if (a->algorithm == NULL) {
            ASN1err(ASN1_F_ASN1_SIGN, ASN1_R_UNKNOWN_OBJECT_TYPE);
            goto err;
        }
        if (a->algorithm->length == 0) {
            ASN1err(ASN1_F_ASN1_SIGN,
                    ASN1_R_THE_ASN1_OBJECT_IDENTIFIER_IS_NOT_KNOWN_FOR_THIS_MD);
            goto err;
        }
    }
    inl = i2d(data, NULL);
    if (inl <= 0) {
        ASN1err(ASN1_F_ASN1_SIGN, ERR_R_INTERNAL_ERROR);
        goto err;
    }
    inll = (size_t)inl;
    buf_in = OPENSSL_malloc(inll);
    outll = outl = EVP_PKEY_size(pkey);
    buf_out = OPENSSL_malloc(outll);
    if (buf_in == NULL || buf_out == NULL) {
        outl = 0;
        ASN1err(ASN1_F_ASN1_SIGN, ERR_R_MALLOC_FAILURE);
        goto err;
    }
    p = buf_in;

    i2d(data, &p);
    if (!EVP_SignInit_ex(ctx, type, NULL)
        || !EVP_SignUpdate(ctx, (unsigned char *)buf_in, inl)
        || !EVP_SignFinal(ctx, (unsigned char *)buf_out,
                          (unsigned int *)&outl, pkey)) {
        outl = 0;
        ASN1err(ASN1_F_ASN1_SIGN, ERR_R_EVP_LIB);
        goto err;
    }
    OPENSSL_free(signature->data);
    signature->data = buf_out;
    buf_out = NULL;
    signature->length = outl;
    /*
     * In the interests of compatibility, I'll make sure that the bit string
     * has a 'not-used bits' value of 0
     */
    signature->flags &= ~(ASN1_STRING_FLAG_BITS_LEFT | 0x07);
    signature->flags |= ASN1_STRING_FLAG_BITS_LEFT;
 err:
    EVP_MD_CTX_free(ctx);
    OPENSSL_clear_free((char *)buf_in, inll);
    OPENSSL_clear_free((char *)buf_out, outll);
    return outl;
}

#endif

int ASN1_item_sign(const ASN1_ITEM *it, X509_ALGOR *algor1,
                   X509_ALGOR *algor2, ASN1_BIT_STRING *signature, void *asn,
                   EVP_PKEY *pkey, const EVP_MD *type)
{
    int rv;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();

    if (ctx == NULL) {
        ASN1err(ASN1_F_ASN1_ITEM_SIGN, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    if (!EVP_DigestSignInit(ctx, NULL, type, NULL, pkey)) {
        EVP_MD_CTX_free(ctx);
        return 0;
    }

    rv = ASN1_item_sign_ctx(it, algor1, algor2, signature, asn, ctx);

    EVP_MD_CTX_free(ctx);
    return rv;
}

int ASN1_item_sign_ctx(const ASN1_ITEM *it,
                       X509_ALGOR *algor1, X509_ALGOR *algor2,
                       ASN1_BIT_STRING *signature, void *asn, EVP_MD_CTX *ctx)
{
    const EVP_MD *type;
    EVP_PKEY *pkey;
    unsigned char *buf_in = NULL, *buf_out = NULL;
    size_t inl = 0, outl = 0, outll = 0;
    int signid, paramtype, buf_len = 0;
    int rv;

    type = EVP_MD_CTX_md(ctx);
    pkey = EVP_PKEY_CTX_get0_pkey(EVP_MD_CTX_pkey_ctx(ctx));

    if (pkey == NULL) {
        ASN1err(ASN1_F_ASN1_ITEM_SIGN_CTX, ASN1_R_CONTEXT_NOT_INITIALISED);
        goto err;
    }

    if (pkey->ameth == NULL) {
        ASN1err(ASN1_F_ASN1_ITEM_SIGN_CTX, ASN1_R_DIGEST_AND_KEY_TYPE_NOT_SUPPORTED);
        goto err;
    }

    if (pkey->ameth->item_sign) {
        rv = pkey->ameth->item_sign(ctx, it, asn, algor1, algor2, signature);
        if (rv == 1)
            outl = signature->length;
        /*-
         * Return value meanings:
         * <=0: error.
         *   1: method does everything.
         *   2: carry on as normal.
         *   3: ASN1 method sets algorithm identifiers: just sign.
         */
        if (rv <= 0)
            ASN1err(ASN1_F_ASN1_ITEM_SIGN_CTX, ERR_R_EVP_LIB);
        if (rv <= 1)
            goto err;
    } else {
        rv = 2;
    }

    if (rv == 2) {
        if (type == NULL) {
            ASN1err(ASN1_F_ASN1_ITEM_SIGN_CTX, ASN1_R_CONTEXT_NOT_INITIALISED);
            goto err;
        }
        if (!OBJ_find_sigid_by_algs(&signid,
                                    EVP_MD_nid(type),
                                    pkey->ameth->pkey_id)) {
            ASN1err(ASN1_F_ASN1_ITEM_SIGN_CTX,
                    ASN1_R_DIGEST_AND_KEY_TYPE_NOT_SUPPORTED);
            goto err;
        }

        if (pkey->ameth->pkey_flags & ASN1_PKEY_SIGPARAM_NULL)
            paramtype = V_ASN1_NULL;
        else
            paramtype = V_ASN1_UNDEF;

        if (algor1)
            X509_ALGOR_set0(algor1, OBJ_nid2obj(signid), paramtype, NULL);
        if (algor2)
            X509_ALGOR_set0(algor2, OBJ_nid2obj(signid), paramtype, NULL);

    }

    buf_len = ASN1_item_i2d(asn, &buf_in, it);
    if (buf_len <= 0) {
        outl = 0;
        ASN1err(ASN1_F_ASN1_ITEM_SIGN_CTX, ERR_R_INTERNAL_ERROR);
        goto err;
    }
    inl = buf_len;
    outll = outl = EVP_PKEY_size(pkey);
    buf_out = OPENSSL_malloc(outll);
    if (buf_in == NULL || buf_out == NULL) {
        outl = 0;
        ASN1err(ASN1_F_ASN1_ITEM_SIGN_CTX, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    if (!EVP_DigestSign(ctx, buf_out, &outl, buf_in, inl)) {
        outl = 0;
        ASN1err(ASN1_F_ASN1_ITEM_SIGN_CTX, ERR_R_EVP_LIB);
        goto err;
    }
    OPENSSL_free(signature->data);
    signature->data = buf_out;
    buf_out = NULL;
    signature->length = outl;
    /*
     * In the interests of compatibility, I'll make sure that the bit string
     * has a 'not-used bits' value of 0
     */
    signature->flags &= ~(ASN1_STRING_FLAG_BITS_LEFT | 0x07);
    signature->flags |= ASN1_STRING_FLAG_BITS_LEFT;
 err:
    OPENSSL_clear_free((char *)buf_in, inl);
    OPENSSL_clear_free((char *)buf_out, outll);
    return outl;
}
