/*
 * Copyright 1998-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/objects.h>
#include <openssl/comp.h>
#include <openssl/err.h>
#include "comp_lcl.h"

COMP_CTX *COMP_CTX_new(COMP_METHOD *meth)
{
    COMP_CTX *ret;

    if ((ret = OPENSSL_zalloc(sizeof(*ret))) == NULL) {
        COMPerr(COMP_F_COMP_CTX_NEW, ERR_R_MALLOC_FAILURE);
        return NULL;
    }
    ret->meth = meth;
    if ((ret->meth->init != NULL) && !ret->meth->init(ret)) {
        OPENSSL_free(ret);
        ret = NULL;
    }
    return ret;
}

const COMP_METHOD *COMP_CTX_get_method(const COMP_CTX *ctx)
{
    return ctx->meth;
}

int COMP_get_type(const COMP_METHOD *meth)
{
    return meth->type;
}

const char *COMP_get_name(const COMP_METHOD *meth)
{
    return meth->name;
}

void COMP_CTX_free(COMP_CTX *ctx)
{
    if (ctx == NULL)
        return;
    if (ctx->meth->finish != NULL)
        ctx->meth->finish(ctx);

    OPENSSL_free(ctx);
}

int COMP_compress_block(COMP_CTX *ctx, unsigned char *out, int olen,
                        unsigned char *in, int ilen)
{
    int ret;
    if (ctx->meth->compress == NULL) {
        return -1;
    }
    ret = ctx->meth->compress(ctx, out, olen, in, ilen);
    if (ret > 0) {
        ctx->compress_in += ilen;
        ctx->compress_out += ret;
    }
    return ret;
}

int COMP_expand_block(COMP_CTX *ctx, unsigned char *out, int olen,
                      unsigned char *in, int ilen)
{
    int ret;

    if (ctx->meth->expand == NULL) {
        return -1;
    }
    ret = ctx->meth->expand(ctx, out, olen, in, ilen);
    if (ret > 0) {
        ctx->expand_in += ilen;
        ctx->expand_out += ret;
    }
    return ret;
}

int COMP_CTX_get_type(const COMP_CTX* comp)
{
    return comp->meth ? comp->meth->type : NID_undef;
}
