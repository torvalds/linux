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
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include "internal/evp_int.h"

static int init(EVP_MD_CTX *ctx)
{
    return 1;
}

static int update(EVP_MD_CTX *ctx, const void *data, size_t count)
{
    return 1;
}

static int final(EVP_MD_CTX *ctx, unsigned char *md)
{
    return 1;
}

static const EVP_MD null_md = {
    NID_undef,
    NID_undef,
    0,
    0,
    init,
    update,
    final,
    NULL,
    NULL,
    0,
    sizeof(EVP_MD *),
};

const EVP_MD *EVP_md_null(void)
{
    return &null_md;
}
