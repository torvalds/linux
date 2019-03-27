/*
 * Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "bio_lcl.h"
#include "internal/thread_once.h"

CRYPTO_RWLOCK *bio_type_lock = NULL;
static CRYPTO_ONCE bio_type_init = CRYPTO_ONCE_STATIC_INIT;

DEFINE_RUN_ONCE_STATIC(do_bio_type_init)
{
    bio_type_lock = CRYPTO_THREAD_lock_new();
    return bio_type_lock != NULL;
}

int BIO_get_new_index(void)
{
    static CRYPTO_REF_COUNT bio_count = BIO_TYPE_START;
    int newval;

    if (!RUN_ONCE(&bio_type_init, do_bio_type_init)) {
        BIOerr(BIO_F_BIO_GET_NEW_INDEX, ERR_R_MALLOC_FAILURE);
        return -1;
    }
    if (!CRYPTO_UP_REF(&bio_count, &newval, bio_type_lock))
        return -1;
    return newval;
}

BIO_METHOD *BIO_meth_new(int type, const char *name)
{
    BIO_METHOD *biom = OPENSSL_zalloc(sizeof(BIO_METHOD));

    if (biom == NULL
            || (biom->name = OPENSSL_strdup(name)) == NULL) {
        OPENSSL_free(biom);
        BIOerr(BIO_F_BIO_METH_NEW, ERR_R_MALLOC_FAILURE);
        return NULL;
    }
    biom->type = type;
    return biom;
}

void BIO_meth_free(BIO_METHOD *biom)
{
    if (biom != NULL) {
        OPENSSL_free(biom->name);
        OPENSSL_free(biom);
    }
}

int (*BIO_meth_get_write(const BIO_METHOD *biom)) (BIO *, const char *, int)
{
    return biom->bwrite_old;
}

int (*BIO_meth_get_write_ex(const BIO_METHOD *biom)) (BIO *, const char *, size_t,
                                                size_t *)
{
    return biom->bwrite;
}

/* Conversion for old style bwrite to new style */
int bwrite_conv(BIO *bio, const char *data, size_t datal, size_t *written)
{
    int ret;

    if (datal > INT_MAX)
        datal = INT_MAX;

    ret = bio->method->bwrite_old(bio, data, (int)datal);

    if (ret <= 0) {
        *written = 0;
        return ret;
    }

    *written = (size_t)ret;

    return 1;
}

int BIO_meth_set_write(BIO_METHOD *biom,
                       int (*bwrite) (BIO *, const char *, int))
{
    biom->bwrite_old = bwrite;
    biom->bwrite = bwrite_conv;
    return 1;
}

int BIO_meth_set_write_ex(BIO_METHOD *biom,
                       int (*bwrite) (BIO *, const char *, size_t, size_t *))
{
    biom->bwrite_old = NULL;
    biom->bwrite = bwrite;
    return 1;
}

int (*BIO_meth_get_read(const BIO_METHOD *biom)) (BIO *, char *, int)
{
    return biom->bread_old;
}

int (*BIO_meth_get_read_ex(const BIO_METHOD *biom)) (BIO *, char *, size_t, size_t *)
{
    return biom->bread;
}

/* Conversion for old style bread to new style */
int bread_conv(BIO *bio, char *data, size_t datal, size_t *readbytes)
{
    int ret;

    if (datal > INT_MAX)
        datal = INT_MAX;

    ret = bio->method->bread_old(bio, data, (int)datal);

    if (ret <= 0) {
        *readbytes = 0;
        return ret;
    }

    *readbytes = (size_t)ret;

    return 1;
}

int BIO_meth_set_read(BIO_METHOD *biom,
                      int (*bread) (BIO *, char *, int))
{
    biom->bread_old = bread;
    biom->bread = bread_conv;
    return 1;
}

int BIO_meth_set_read_ex(BIO_METHOD *biom,
                         int (*bread) (BIO *, char *, size_t, size_t *))
{
    biom->bread_old = NULL;
    biom->bread = bread;
    return 1;
}

int (*BIO_meth_get_puts(const BIO_METHOD *biom)) (BIO *, const char *)
{
    return biom->bputs;
}

int BIO_meth_set_puts(BIO_METHOD *biom,
                      int (*bputs) (BIO *, const char *))
{
    biom->bputs = bputs;
    return 1;
}

int (*BIO_meth_get_gets(const BIO_METHOD *biom)) (BIO *, char *, int)
{
    return biom->bgets;
}

int BIO_meth_set_gets(BIO_METHOD *biom,
                      int (*bgets) (BIO *, char *, int))
{
    biom->bgets = bgets;
    return 1;
}

long (*BIO_meth_get_ctrl(const BIO_METHOD *biom)) (BIO *, int, long, void *)
{
    return biom->ctrl;
}

int BIO_meth_set_ctrl(BIO_METHOD *biom,
                      long (*ctrl) (BIO *, int, long, void *))
{
    biom->ctrl = ctrl;
    return 1;
}

int (*BIO_meth_get_create(const BIO_METHOD *biom)) (BIO *)
{
    return biom->create;
}

int BIO_meth_set_create(BIO_METHOD *biom, int (*create) (BIO *))
{
    biom->create = create;
    return 1;
}

int (*BIO_meth_get_destroy(const BIO_METHOD *biom)) (BIO *)
{
    return biom->destroy;
}

int BIO_meth_set_destroy(BIO_METHOD *biom, int (*destroy) (BIO *))
{
    biom->destroy = destroy;
    return 1;
}

long (*BIO_meth_get_callback_ctrl(const BIO_METHOD *biom)) (BIO *, int, BIO_info_cb *)
{
    return biom->callback_ctrl;
}

int BIO_meth_set_callback_ctrl(BIO_METHOD *biom,
                               long (*callback_ctrl) (BIO *, int,
                                                      BIO_info_cb *))
{
    biom->callback_ctrl = callback_ctrl;
    return 1;
}
