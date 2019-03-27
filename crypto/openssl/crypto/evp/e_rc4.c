/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"

#ifndef OPENSSL_NO_RC4

# include <openssl/evp.h>
# include <openssl/objects.h>
# include <openssl/rc4.h>

# include "internal/evp_int.h"

typedef struct {
    RC4_KEY ks;                 /* working key */
} EVP_RC4_KEY;

# define data(ctx) ((EVP_RC4_KEY *)EVP_CIPHER_CTX_get_cipher_data(ctx))

static int rc4_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                        const unsigned char *iv, int enc);
static int rc4_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                      const unsigned char *in, size_t inl);
static const EVP_CIPHER r4_cipher = {
    NID_rc4,
    1, EVP_RC4_KEY_SIZE, 0,
    EVP_CIPH_VARIABLE_LENGTH,
    rc4_init_key,
    rc4_cipher,
    NULL,
    sizeof(EVP_RC4_KEY),
    NULL,
    NULL,
    NULL,
    NULL
};

static const EVP_CIPHER r4_40_cipher = {
    NID_rc4_40,
    1, 5 /* 40 bit */ , 0,
    EVP_CIPH_VARIABLE_LENGTH,
    rc4_init_key,
    rc4_cipher,
    NULL,
    sizeof(EVP_RC4_KEY),
    NULL,
    NULL,
    NULL,
    NULL
};

const EVP_CIPHER *EVP_rc4(void)
{
    return &r4_cipher;
}

const EVP_CIPHER *EVP_rc4_40(void)
{
    return &r4_40_cipher;
}

static int rc4_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                        const unsigned char *iv, int enc)
{
    RC4_set_key(&data(ctx)->ks, EVP_CIPHER_CTX_key_length(ctx), key);
    return 1;
}

static int rc4_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                      const unsigned char *in, size_t inl)
{
    RC4(&data(ctx)->ks, inl, in, out);
    return 1;
}
#endif
