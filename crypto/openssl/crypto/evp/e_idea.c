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

#ifndef OPENSSL_NO_IDEA
# include <openssl/evp.h>
# include <openssl/objects.h>
# include "internal/evp_int.h"
# include <openssl/idea.h>

/* Can't use IMPLEMENT_BLOCK_CIPHER because IDEA_ecb_encrypt is different */

typedef struct {
    IDEA_KEY_SCHEDULE ks;
} EVP_IDEA_KEY;

static int idea_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                         const unsigned char *iv, int enc);

/*
 * NB IDEA_ecb_encrypt doesn't take an 'encrypt' argument so we treat it as a
 * special case
 */

static int idea_ecb_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                           const unsigned char *in, size_t inl)
{
    BLOCK_CIPHER_ecb_loop()
        IDEA_ecb_encrypt(in + i, out + i, &EVP_C_DATA(EVP_IDEA_KEY,ctx)->ks);
    return 1;
}

BLOCK_CIPHER_func_cbc(idea, IDEA, EVP_IDEA_KEY, ks)
BLOCK_CIPHER_func_ofb(idea, IDEA, 64, EVP_IDEA_KEY, ks)
BLOCK_CIPHER_func_cfb(idea, IDEA, 64, EVP_IDEA_KEY, ks)

BLOCK_CIPHER_defs(idea, IDEA_KEY_SCHEDULE, NID_idea, 8, 16, 8, 64,
                  0, idea_init_key, NULL,
                  EVP_CIPHER_set_asn1_iv, EVP_CIPHER_get_asn1_iv, NULL)

static int idea_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
                         const unsigned char *iv, int enc)
{
    if (!enc) {
        if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_OFB_MODE)
            enc = 1;
        else if (EVP_CIPHER_CTX_mode(ctx) == EVP_CIPH_CFB_MODE)
            enc = 1;
    }
    if (enc)
        IDEA_set_encrypt_key(key, &EVP_C_DATA(EVP_IDEA_KEY,ctx)->ks);
    else {
        IDEA_KEY_SCHEDULE tmp;

        IDEA_set_encrypt_key(key, &tmp);
        IDEA_set_decrypt_key(&tmp, &EVP_C_DATA(EVP_IDEA_KEY,ctx)->ks);
        OPENSSL_cleanse((unsigned char *)&tmp, sizeof(IDEA_KEY_SCHEDULE));
    }
    return 1;
}

#endif
