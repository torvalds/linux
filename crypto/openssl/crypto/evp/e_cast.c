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

#ifndef OPENSSL_NO_CAST
# include <openssl/evp.h>
# include <openssl/objects.h>
# include "internal/evp_int.h"
# include <openssl/cast.h>

static int cast_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                         const unsigned char *iv, int enc);

typedef struct {
    CAST_KEY ks;
} EVP_CAST_KEY;

# define data(ctx)       EVP_C_DATA(EVP_CAST_KEY,ctx)

IMPLEMENT_BLOCK_CIPHER(cast5, ks, CAST, EVP_CAST_KEY,
                       NID_cast5, 8, CAST_KEY_LENGTH, 8, 64,
                       EVP_CIPH_VARIABLE_LENGTH, cast_init_key, NULL,
                       EVP_CIPHER_set_asn1_iv, EVP_CIPHER_get_asn1_iv, NULL)

static int cast_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                         const unsigned char *iv, int enc)
{
    CAST_set_key(&data(ctx)->ks, EVP_CIPHER_CTX_key_length(ctx), key);
    return 1;
}

#endif
