/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "internal/cryptlib.h"
#ifdef OPENSSL_NO_RSA
NON_EMPTY_TRANSLATION_UNIT
#else

# include <stdio.h>
# include <openssl/evp.h>
# include <openssl/objects.h>
# include <openssl/x509.h>
# include <openssl/rsa.h>

int EVP_OpenInit(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *type,
                 const unsigned char *ek, int ekl, const unsigned char *iv,
                 EVP_PKEY *priv)
{
    unsigned char *key = NULL;
    int i, size = 0, ret = 0;

    if (type) {
        EVP_CIPHER_CTX_reset(ctx);
        if (!EVP_DecryptInit_ex(ctx, type, NULL, NULL, NULL))
            return 0;
    }

    if (!priv)
        return 1;

    if (EVP_PKEY_id(priv) != EVP_PKEY_RSA) {
        EVPerr(EVP_F_EVP_OPENINIT, EVP_R_PUBLIC_KEY_NOT_RSA);
        goto err;
    }

    size = EVP_PKEY_size(priv);
    key = OPENSSL_malloc(size + 2);
    if (key == NULL) {
        /* ERROR */
        EVPerr(EVP_F_EVP_OPENINIT, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    i = EVP_PKEY_decrypt_old(key, ek, ekl, priv);
    if ((i <= 0) || !EVP_CIPHER_CTX_set_key_length(ctx, i)) {
        /* ERROR */
        goto err;
    }
    if (!EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv))
        goto err;

    ret = 1;
 err:
    OPENSSL_clear_free(key, size);
    return ret;
}

int EVP_OpenFinal(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl)
{
    int i;

    i = EVP_DecryptFinal_ex(ctx, out, outl);
    if (i)
        i = EVP_DecryptInit_ex(ctx, NULL, NULL, NULL, NULL);
    return i;
}
#endif
