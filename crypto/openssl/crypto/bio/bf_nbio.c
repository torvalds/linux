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
#include <openssl/rand.h>

/*
 * BIO_put and BIO_get both add to the digest, BIO_gets returns the digest
 */

static int nbiof_write(BIO *h, const char *buf, int num);
static int nbiof_read(BIO *h, char *buf, int size);
static int nbiof_puts(BIO *h, const char *str);
static int nbiof_gets(BIO *h, char *str, int size);
static long nbiof_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int nbiof_new(BIO *h);
static int nbiof_free(BIO *data);
static long nbiof_callback_ctrl(BIO *h, int cmd, BIO_info_cb *fp);
typedef struct nbio_test_st {
    /* only set if we sent a 'should retry' error */
    int lrn;
    int lwn;
} NBIO_TEST;

static const BIO_METHOD methods_nbiof = {
    BIO_TYPE_NBIO_TEST,
    "non-blocking IO test filter",
    /* TODO: Convert to new style write function */
    bwrite_conv,
    nbiof_write,
    /* TODO: Convert to new style read function */
    bread_conv,
    nbiof_read,
    nbiof_puts,
    nbiof_gets,
    nbiof_ctrl,
    nbiof_new,
    nbiof_free,
    nbiof_callback_ctrl,
};

const BIO_METHOD *BIO_f_nbio_test(void)
{
    return &methods_nbiof;
}

static int nbiof_new(BIO *bi)
{
    NBIO_TEST *nt;

    if ((nt = OPENSSL_zalloc(sizeof(*nt))) == NULL) {
        BIOerr(BIO_F_NBIOF_NEW, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    nt->lrn = -1;
    nt->lwn = -1;
    bi->ptr = (char *)nt;
    bi->init = 1;
    return 1;
}

static int nbiof_free(BIO *a)
{
    if (a == NULL)
        return 0;
    OPENSSL_free(a->ptr);
    a->ptr = NULL;
    a->init = 0;
    a->flags = 0;
    return 1;
}

static int nbiof_read(BIO *b, char *out, int outl)
{
    int ret = 0;
    int num;
    unsigned char n;

    if (out == NULL)
        return 0;
    if (b->next_bio == NULL)
        return 0;

    BIO_clear_retry_flags(b);
    if (RAND_priv_bytes(&n, 1) <= 0)
        return -1;
    num = (n & 0x07);

    if (outl > num)
        outl = num;

    if (num == 0) {
        ret = -1;
        BIO_set_retry_read(b);
    } else {
        ret = BIO_read(b->next_bio, out, outl);
        if (ret < 0)
            BIO_copy_next_retry(b);
    }
    return ret;
}

static int nbiof_write(BIO *b, const char *in, int inl)
{
    NBIO_TEST *nt;
    int ret = 0;
    int num;
    unsigned char n;

    if ((in == NULL) || (inl <= 0))
        return 0;
    if (b->next_bio == NULL)
        return 0;
    nt = (NBIO_TEST *)b->ptr;

    BIO_clear_retry_flags(b);

    if (nt->lwn > 0) {
        num = nt->lwn;
        nt->lwn = 0;
    } else {
        if (RAND_priv_bytes(&n, 1) <= 0)
            return -1;
        num = (n & 7);
    }

    if (inl > num)
        inl = num;

    if (num == 0) {
        ret = -1;
        BIO_set_retry_write(b);
    } else {
        ret = BIO_write(b->next_bio, in, inl);
        if (ret < 0) {
            BIO_copy_next_retry(b);
            nt->lwn = inl;
        }
    }
    return ret;
}

static long nbiof_ctrl(BIO *b, int cmd, long num, void *ptr)
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
        break;
    }
    return ret;
}

static long nbiof_callback_ctrl(BIO *b, int cmd, BIO_info_cb *fp)
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

static int nbiof_gets(BIO *bp, char *buf, int size)
{
    if (bp->next_bio == NULL)
        return 0;
    return BIO_gets(bp->next_bio, buf, size);
}

static int nbiof_puts(BIO *bp, const char *str)
{
    if (bp->next_bio == NULL)
        return 0;
    return BIO_puts(bp->next_bio, str);
}
