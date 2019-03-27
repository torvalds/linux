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

#ifndef OPENSSL_NO_DES

# include <openssl/evp.h>
# include <openssl/objects.h>
# include "internal/evp_int.h"
# include <openssl/des.h>

static int desx_cbc_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                             const unsigned char *iv, int enc);
static int desx_cbc_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                           const unsigned char *in, size_t inl);

typedef struct {
    DES_key_schedule ks;        /* key schedule */
    DES_cblock inw;
    DES_cblock outw;
} DESX_CBC_KEY;

# define data(ctx) EVP_C_DATA(DESX_CBC_KEY,ctx)

static const EVP_CIPHER d_xcbc_cipher = {
    NID_desx_cbc,
    8, 24, 8,
    EVP_CIPH_CBC_MODE,
    desx_cbc_init_key,
    desx_cbc_cipher,
    NULL,
    sizeof(DESX_CBC_KEY),
    EVP_CIPHER_set_asn1_iv,
    EVP_CIPHER_get_asn1_iv,
    NULL,
    NULL
};

const EVP_CIPHER *EVP_desx_cbc(void)
{
    return &d_xcbc_cipher;
}

static int desx_cbc_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                             const unsigned char *iv, int enc)
{
    DES_cblock *deskey = (DES_cblock *)key;

    DES_set_key_unchecked(deskey, &data(ctx)->ks);
    memcpy(&data(ctx)->inw[0], &key[8], 8);
    memcpy(&data(ctx)->outw[0], &key[16], 8);

    return 1;
}

static int desx_cbc_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                           const unsigned char *in, size_t inl)
{
    while (inl >= EVP_MAXCHUNK) {
        DES_xcbc_encrypt(in, out, (long)EVP_MAXCHUNK, &data(ctx)->ks,
                         (DES_cblock *)EVP_CIPHER_CTX_iv_noconst(ctx),
                         &data(ctx)->inw, &data(ctx)->outw,
                         EVP_CIPHER_CTX_encrypting(ctx));
        inl -= EVP_MAXCHUNK;
        in += EVP_MAXCHUNK;
        out += EVP_MAXCHUNK;
    }
    if (inl)
        DES_xcbc_encrypt(in, out, (long)inl, &data(ctx)->ks,
                         (DES_cblock *)EVP_CIPHER_CTX_iv_noconst(ctx),
                         &data(ctx)->inw, &data(ctx)->outw,
                         EVP_CIPHER_CTX_encrypting(ctx));
    return 1;
}
#endif
