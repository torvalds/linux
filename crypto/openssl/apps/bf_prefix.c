/*
 * Copyright 2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <openssl/bio.h>
#include "apps.h"

static int prefix_write(BIO *b, const char *out, size_t outl,
                        size_t *numwritten);
static int prefix_read(BIO *b, char *buf, size_t size, size_t *numread);
static int prefix_puts(BIO *b, const char *str);
static int prefix_gets(BIO *b, char *str, int size);
static long prefix_ctrl(BIO *b, int cmd, long arg1, void *arg2);
static int prefix_create(BIO *b);
static int prefix_destroy(BIO *b);
static long prefix_callback_ctrl(BIO *b, int cmd, BIO_info_cb *fp);

static BIO_METHOD *prefix_meth = NULL;

BIO_METHOD *apps_bf_prefix(void)
{
    if (prefix_meth == NULL) {
        if ((prefix_meth =
             BIO_meth_new(BIO_TYPE_FILTER, "Prefix filter")) == NULL
            || !BIO_meth_set_create(prefix_meth, prefix_create)
            || !BIO_meth_set_destroy(prefix_meth, prefix_destroy)
            || !BIO_meth_set_write_ex(prefix_meth, prefix_write)
            || !BIO_meth_set_read_ex(prefix_meth, prefix_read)
            || !BIO_meth_set_puts(prefix_meth, prefix_puts)
            || !BIO_meth_set_gets(prefix_meth, prefix_gets)
            || !BIO_meth_set_ctrl(prefix_meth, prefix_ctrl)
            || !BIO_meth_set_callback_ctrl(prefix_meth, prefix_callback_ctrl)) {
            BIO_meth_free(prefix_meth);
            prefix_meth = NULL;
        }
    }
    return prefix_meth;
}

typedef struct prefix_ctx_st {
    char *prefix;
    int linestart;               /* flag to indicate we're at the line start */
} PREFIX_CTX;

static int prefix_create(BIO *b)
{
    PREFIX_CTX *ctx = OPENSSL_zalloc(sizeof(*ctx));

    if (ctx == NULL)
        return 0;

    ctx->prefix = NULL;
    ctx->linestart = 1;
    BIO_set_data(b, ctx);
    BIO_set_init(b, 1);
    return 1;
}

static int prefix_destroy(BIO *b)
{
    PREFIX_CTX *ctx = BIO_get_data(b);

    OPENSSL_free(ctx->prefix);
    OPENSSL_free(ctx);
    return 1;
}

static int prefix_read(BIO *b, char *in, size_t size, size_t *numread)
{
    return BIO_read_ex(BIO_next(b), in, size, numread);
}

static int prefix_write(BIO *b, const char *out, size_t outl,
                        size_t *numwritten)
{
    PREFIX_CTX *ctx = BIO_get_data(b);

    if (ctx == NULL)
        return 0;

    /* If no prefix is set or if it's empty, we've got nothing to do here */
    if (ctx->prefix == NULL || *ctx->prefix == '\0') {
        /* We do note if what comes next will be a new line, though */
        if (outl > 0)
            ctx->linestart = (out[outl-1] == '\n');
        return BIO_write_ex(BIO_next(b), out, outl, numwritten);
    }

    *numwritten = 0;

    while (outl > 0) {
        size_t i;
        char c;

        /* If we know that we're at the start of the line, output the prefix */
        if (ctx->linestart) {
            size_t dontcare;

            if (!BIO_write_ex(BIO_next(b), ctx->prefix, strlen(ctx->prefix),
                              &dontcare))
                return 0;
            ctx->linestart = 0;
        }

        /* Now, go look for the next LF, or the end of the string */
        for (i = 0, c = '\0'; i < outl && (c = out[i]) != '\n'; i++)
            continue;
        if (c == '\n')
            i++;

        /* Output what we found so far */
        while (i > 0) {
            size_t num = 0;

            if (!BIO_write_ex(BIO_next(b), out, i, &num))
                return 0;
            out += num;
            outl -= num;
            *numwritten += num;
            i -= num;
        }

        /* If we found a LF, what follows is a new line, so take note */
        if (c == '\n')
            ctx->linestart = 1;
    }

    return 1;
}

static long prefix_ctrl(BIO *b, int cmd, long num, void *ptr)
{
    long ret = 0;

    switch (cmd) {
    case PREFIX_CTRL_SET_PREFIX:
        {
            PREFIX_CTX *ctx = BIO_get_data(b);

            if (ctx == NULL)
                break;

            OPENSSL_free(ctx->prefix);
            ctx->prefix = OPENSSL_strdup((const char *)ptr);
            ret = ctx->prefix != NULL;
        }
        break;
    default:
        if (BIO_next(b) != NULL)
            ret = BIO_ctrl(BIO_next(b), cmd, num, ptr);
        break;
    }
    return ret;
}

static long prefix_callback_ctrl(BIO *b, int cmd, BIO_info_cb *fp)
{
    return BIO_callback_ctrl(BIO_next(b), cmd, fp);
}

static int prefix_gets(BIO *b, char *buf, int size)
{
    return BIO_gets(BIO_next(b), buf, size);
}

static int prefix_puts(BIO *b, const char *str)
{
    return BIO_write(b, str, strlen(str));
}
