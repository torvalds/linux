/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <errno.h>
#include "bio_lcl.h"
#include "internal/cryptlib.h"

/*
 * BIO_put and BIO_get both add to the digest, BIO_gets returns the digest
 */

static int nullf_write(BIO *h, const char *buf, int num);
static int nullf_read(BIO *h, char *buf, int size);
static int nullf_puts(BIO *h, const char *str);
static int nullf_gets(BIO *h, char *str, int size);
static long nullf_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static long nullf_callback_ctrl(BIO *h, int cmd, BIO_info_cb *fp);
static const BIO_METHOD methods_nullf = {
    BIO_TYPE_NULL_FILTER,
    "NULL filter",
    /* TODO: Convert to new style write function */
    bwrite_conv,
    nullf_write,
    /* TODO: Convert to new style read function */
    bread_conv,
    nullf_read,
    nullf_puts,
    nullf_gets,
    nullf_ctrl,
    NULL,
    NULL,
    nullf_callback_ctrl,
};

const BIO_METHOD *BIO_f_null(void)
{
    return &methods_nullf;
}

static int nullf_read(BIO *b, char *out, int outl)
{
    int ret = 0;

    if (out == NULL)
        return 0;
    if (b->next_bio == NULL)
        return 0;
    ret = BIO_read(b->next_bio, out, outl);
    BIO_clear_retry_flags(b);
    BIO_copy_next_retry(b);
    return ret;
}

static int nullf_write(BIO *b, const char *in, int inl)
{
    int ret = 0;

    if ((in == NULL) || (inl <= 0))
        return 0;
    if (b->next_bio == NULL)
        return 0;
    ret = BIO_write(b->next_bio, in, inl);
    BIO_clear_retry_flags(b);
    BIO_copy_next_retry(b);
    return ret;
}

static long nullf_ctrl(BIO *b, int cmd, long num, void *ptr)
{
    long ret;

    if (b->next_bio == NULL)
        return 0;
    switch (cmd) {
    case BIO_C_DO_STATE_MACHINE:
        BIO_clear_retry_flags(b);
        ret = BIO_ctrl(b->next_bio, cmd, num, ptr);
        BIO_copy_next_retry(b);
        break;
    case BIO_CTRL_DUP:
        ret = 0L;
        break;
    default:
        ret = BIO_ctrl(b->next_bio, cmd, num, ptr);
    }
    return ret;
}

static long nullf_callback_ctrl(BIO *b, int cmd, BIO_info_cb *fp)
{
    long ret = 1;

    if (b->next_bio == NULL)
        return 0;
    switch (cmd) {
    default:
        ret = BIO_callback_ctrl(b->next_bio, cmd, fp);
        break;
    }
    return ret;
}

static int nullf_gets(BIO *bp, char *buf, int size)
{
    if (bp->next_bio == NULL)
        return 0;
    return BIO_gets(bp->next_bio, buf, size);
}

static int nullf_puts(BIO *bp, const char *str)
{
    if (bp->next_bio == NULL)
        return 0;
    return BIO_puts(bp->next_bio, str);
}
