/*
 * Copyright 2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * Derived from the BLAKE2 reference implementation written by Samuel Neves.
 * Copyright 2012, Samuel Neves <sneves@dei.uc.pt>
 * More information about the BLAKE2 hash function and its implementations
 * can be found at https://blake2.net.
 */

#include "internal/cryptlib.h"

#ifndef OPENSSL_NO_BLAKE2

# include <openssl/evp.h>
# include <openssl/objects.h>
# include "blake2_locl.h"
# include "internal/evp_int.h"

static int init(EVP_MD_CTX *ctx)
{
    return BLAKE2s_Init(EVP_MD_CTX_md_data(ctx));
}

static int update(EVP_MD_CTX *ctx, const void *data, size_t count)
{
    return BLAKE2s_Update(EVP_MD_CTX_md_data(ctx), data, count);
}

static int final(EVP_MD_CTX *ctx, unsigned char *md)
{
    return BLAKE2s_Final(md, EVP_MD_CTX_md_data(ctx));
}

static const EVP_MD blake2s_md = {
    NID_blake2s256,
    0,
    BLAKE2S_DIGEST_LENGTH,
    0,
    init,
    update,
    final,
    NULL,
    NULL,
    BLAKE2S_BLOCKBYTES,
    sizeof(EVP_MD *) + sizeof(BLAKE2S_CTX),
};

const EVP_MD *EVP_blake2s256(void)
{
    return &blake2s_md;
}
#endif
