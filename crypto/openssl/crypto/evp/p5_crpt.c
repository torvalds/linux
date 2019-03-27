/*
 * Copyright 1999-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <stdlib.h>
#include "internal/cryptlib.h"
#include <openssl/x509.h>
#include <openssl/evp.h>

/*
 * Doesn't do anything now: Builtin PBE algorithms in static table.
 */

void PKCS5_PBE_add(void)
{
}

int PKCS5_PBE_keyivgen(EVP_CIPHER_CTX *cctx, const char *pass, int passlen,
                       ASN1_TYPE *param, const EVP_CIPHER *cipher,
                       const EVP_MD *md, int en_de)
{
    EVP_MD_CTX *ctx;
    unsigned char md_tmp[EVP_MAX_MD_SIZE];
    unsigned char key[EVP_MAX_KEY_LENGTH], iv[EVP_MAX_IV_LENGTH];
    int i;
    PBEPARAM *pbe;
    int saltlen, iter;
    unsigned char *salt;
    int mdsize;
    int rv = 0;

    /* Extract useful info from parameter */
    if (param == NULL || param->type != V_ASN1_SEQUENCE ||
        param->value.sequence == NULL) {
        EVPerr(EVP_F_PKCS5_PBE_KEYIVGEN, EVP_R_DECODE_ERROR);
        return 0;
    }

    pbe = ASN1_TYPE_unpack_sequence(ASN1_ITEM_rptr(PBEPARAM), param);
    if (pbe == NULL) {
        EVPerr(EVP_F_PKCS5_PBE_KEYIVGEN, EVP_R_DECODE_ERROR);
        return 0;
    }

    if (!pbe->iter)
        iter = 1;
    else
        iter = ASN1_INTEGER_get(pbe->iter);
    salt = pbe->salt->data;
    saltlen = pbe->salt->length;

    if (!pass)
        passlen = 0;
    else if (passlen == -1)
        passlen = strlen(pass);

    ctx = EVP_MD_CTX_new();
    if (ctx == NULL) {
        EVPerr(EVP_F_PKCS5_PBE_KEYIVGEN, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    if (!EVP_DigestInit_ex(ctx, md, NULL))
        goto err;
    if (!EVP_DigestUpdate(ctx, pass, passlen))
        goto err;
    if (!EVP_DigestUpdate(ctx, salt, saltlen))
        goto err;
    PBEPARAM_free(pbe);
    if (!EVP_DigestFinal_ex(ctx, md_tmp, NULL))
        goto err;
    mdsize = EVP_MD_size(md);
    if (mdsize < 0)
        return 0;
    for (i = 1; i < iter; i++) {
        if (!EVP_DigestInit_ex(ctx, md, NULL))
            goto err;
        if (!EVP_DigestUpdate(ctx, md_tmp, mdsize))
            goto err;
        if (!EVP_DigestFinal_ex(ctx, md_tmp, NULL))
            goto err;
    }
    OPENSSL_assert(EVP_CIPHER_key_length(cipher) <= (int)sizeof(md_tmp));
    memcpy(key, md_tmp, EVP_CIPHER_key_length(cipher));
    OPENSSL_assert(EVP_CIPHER_iv_length(cipher) <= 16);
    memcpy(iv, md_tmp + (16 - EVP_CIPHER_iv_length(cipher)),
           EVP_CIPHER_iv_length(cipher));
    if (!EVP_CipherInit_ex(cctx, cipher, NULL, key, iv, en_de))
        goto err;
    OPENSSL_cleanse(md_tmp, EVP_MAX_MD_SIZE);
    OPENSSL_cleanse(key, EVP_MAX_KEY_LENGTH);
    OPENSSL_cleanse(iv, EVP_MAX_IV_LENGTH);
    rv = 1;
 err:
    EVP_MD_CTX_free(ctx);
    return rv;
}
