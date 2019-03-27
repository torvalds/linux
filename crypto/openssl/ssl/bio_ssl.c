/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <openssl/crypto.h>
#include "internal/bio.h"
#include <openssl/err.h>
#include "ssl_locl.h"

static int ssl_write(BIO *h, const char *buf, size_t size, size_t *written);
static int ssl_read(BIO *b, char *buf, size_t size, size_t *readbytes);
static int ssl_puts(BIO *h, const char *str);
static long ssl_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int ssl_new(BIO *h);
static int ssl_free(BIO *data);
static long ssl_callback_ctrl(BIO *h, int cmd, BIO_info_cb *fp);
typedef struct bio_ssl_st {
    SSL *ssl;                   /* The ssl handle :-) */
    /* re-negotiate every time the total number of bytes is this size */
    int num_renegotiates;
    unsigned long renegotiate_count;
    size_t byte_count;
    unsigned long renegotiate_timeout;
    unsigned long last_time;
} BIO_SSL;

static const BIO_METHOD methods_sslp = {
    BIO_TYPE_SSL,
    "ssl",
    ssl_write,
    NULL,                       /* ssl_write_old, */
    ssl_read,
    NULL,                       /* ssl_read_old,  */
    ssl_puts,
    NULL,                       /* ssl_gets,      */
    ssl_ctrl,
    ssl_new,
    ssl_free,
    ssl_callback_ctrl,
};

const BIO_METHOD *BIO_f_ssl(void)
{
    return &methods_sslp;
}

static int ssl_new(BIO *bi)
{
    BIO_SSL *bs = OPENSSL_zalloc(sizeof(*bs));

    if (bs == NULL) {
        BIOerr(BIO_F_SSL_NEW, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    BIO_set_init(bi, 0);
    BIO_set_data(bi, bs);
    /* Clear all flags */
    BIO_clear_flags(bi, ~0);

    return 1;
}

static int ssl_free(BIO *a)
{
    BIO_SSL *bs;

    if (a == NULL)
        return 0;
    bs = BIO_get_data(a);
    if (bs->ssl != NULL)
        SSL_shutdown(bs->ssl);
    if (BIO_get_shutdown(a)) {
        if (BIO_get_init(a))
            SSL_free(bs->ssl);
        /* Clear all flags */
        BIO_clear_flags(a, ~0);
        BIO_set_init(a, 0);
    }
    OPENSSL_free(bs);
    return 1;
}

static int ssl_read(BIO *b, char *buf, size_t size, size_t *readbytes)
{
    int ret = 1;
    BIO_SSL *sb;
    SSL *ssl;
    int retry_reason = 0;
    int r = 0;

    if (buf == NULL)
        return 0;
    sb = BIO_get_data(b);
    ssl = sb->ssl;

    BIO_clear_retry_flags(b);

    ret = ssl_read_internal(ssl, buf, size, readbytes);

    switch (SSL_get_error(ssl, ret)) {
    case SSL_ERROR_NONE:
        if (sb->renegotiate_count > 0) {
            sb->byte_count += *readbytes;
            if (sb->byte_count > sb->renegotiate_count) {
                sb->byte_count = 0;
                sb->num_renegotiates++;
                SSL_renegotiate(ssl);
                r = 1;
            }
        }
        if ((sb->renegotiate_timeout > 0) && (!r)) {
            unsigned long tm;

            tm = (unsigned long)time(NULL);
            if (tm > sb->last_time + sb->renegotiate_timeout) {
                sb->last_time = tm;
                sb->num_renegotiates++;
                SSL_renegotiate(ssl);
            }
        }

        break;
    case SSL_ERROR_WANT_READ:
        BIO_set_retry_read(b);
        break;
    case SSL_ERROR_WANT_WRITE:
        BIO_set_retry_write(b);
        break;
    case SSL_ERROR_WANT_X509_LOOKUP:
        BIO_set_retry_special(b);
        retry_reason = BIO_RR_SSL_X509_LOOKUP;
        break;
    case SSL_ERROR_WANT_ACCEPT:
        BIO_set_retry_special(b);
        retry_reason = BIO_RR_ACCEPT;
        break;
    case SSL_ERROR_WANT_CONNECT:
        BIO_set_retry_special(b);
        retry_reason = BIO_RR_CONNECT;
        break;
    case SSL_ERROR_SYSCALL:
    case SSL_ERROR_SSL:
    case SSL_ERROR_ZERO_RETURN:
    default:
        break;
    }

    BIO_set_retry_reason(b, retry_reason);

    return ret;
}

static int ssl_write(BIO *b, const char *buf, size_t size, size_t *written)
{
    int ret, r = 0;
    int retry_reason = 0;
    SSL *ssl;
    BIO_SSL *bs;

    if (buf == NULL)
        return 0;
    bs = BIO_get_data(b);
    ssl = bs->ssl;

    BIO_clear_retry_flags(b);

    ret = ssl_write_internal(ssl, buf, size, written);

    switch (SSL_get_error(ssl, ret)) {
    case SSL_ERROR_NONE:
        if (bs->renegotiate_count > 0) {
            bs->byte_count += *written;
            if (bs->byte_count > bs->renegotiate_count) {
                bs->byte_count = 0;
                bs->num_renegotiates++;
                SSL_renegotiate(ssl);
                r = 1;
            }
        }
        if ((bs->renegotiate_timeout > 0) && (!r)) {
            unsigned long tm;

            tm = (unsigned long)time(NULL);
            if (tm > bs->last_time + bs->renegotiate_timeout) {
                bs->last_time = tm;
                bs->num_renegotiates++;
                SSL_renegotiate(ssl);
            }
        }
        break;
    case SSL_ERROR_WANT_WRITE:
        BIO_set_retry_write(b);
        break;
    case SSL_ERROR_WANT_READ:
        BIO_set_retry_read(b);
        break;
    case SSL_ERROR_WANT_X509_LOOKUP:
        BIO_set_retry_special(b);
        retry_reason = BIO_RR_SSL_X509_LOOKUP;
        break;
    case SSL_ERROR_WANT_CONNECT:
        BIO_set_retry_special(b);
        retry_reason = BIO_RR_CONNECT;
    case SSL_ERROR_SYSCALL:
    case SSL_ERROR_SSL:
    default:
        break;
    }

    BIO_set_retry_reason(b, retry_reason);

    return ret;
}

static long ssl_ctrl(BIO *b, int cmd, long num, void *ptr)
{
    SSL **sslp, *ssl;
    BIO_SSL *bs, *dbs;
    BIO *dbio, *bio;
    long ret = 1;
    BIO *next;

    bs = BIO_get_data(b);
    next = BIO_next(b);
    ssl = bs->ssl;
    if ((ssl == NULL) && (cmd != BIO_C_SET_SSL))
        return 0;
    switch (cmd) {
    case BIO_CTRL_RESET:
        SSL_shutdown(ssl);

        if (ssl->handshake_func == ssl->method->ssl_connect)
            SSL_set_connect_state(ssl);
        else if (ssl->handshake_func == ssl->method->ssl_accept)
            SSL_set_accept_state(ssl);

        if (!SSL_clear(ssl)) {
            ret = 0;
            break;
        }

        if (next != NULL)
            ret = BIO_ctrl(next, cmd, num, ptr);
        else if (ssl->rbio != NULL)
            ret = BIO_ctrl(ssl->rbio, cmd, num, ptr);
        else
            ret = 1;
        break;
    case BIO_CTRL_INFO:
        ret = 0;
        break;
    case BIO_C_SSL_MODE:
        if (num)                /* client mode */
            SSL_set_connect_state(ssl);
        else
            SSL_set_accept_state(ssl);
        break;
    case BIO_C_SET_SSL_RENEGOTIATE_TIMEOUT:
        ret = bs->renegotiate_timeout;
        if (num < 60)
            num = 5;
        bs->renegotiate_timeout = (unsigned long)num;
        bs->last_time = (unsigned long)time(NULL);
        break;
    case BIO_C_SET_SSL_RENEGOTIATE_BYTES:
        ret = bs->renegotiate_count;
        if ((long)num >= 512)
            bs->renegotiate_count = (unsigned long)num;
        break;
    case BIO_C_GET_SSL_NUM_RENEGOTIATES:
        ret = bs->num_renegotiates;
        break;
    case BIO_C_SET_SSL:
        if (ssl != NULL) {
            ssl_free(b);
            if (!ssl_new(b))
                return 0;
        }
        BIO_set_shutdown(b, num);
        ssl = (SSL *)ptr;
        bs->ssl = ssl;
        bio = SSL_get_rbio(ssl);
        if (bio != NULL) {
            if (next != NULL)
                BIO_push(bio, next);
            BIO_set_next(b, bio);
            BIO_up_ref(bio);
        }
        BIO_set_init(b, 1);
        break;
    case BIO_C_GET_SSL:
        if (ptr != NULL) {
            sslp = (SSL **)ptr;
            *sslp = ssl;
        } else
            ret = 0;
        break;
    case BIO_CTRL_GET_CLOSE:
        ret = BIO_get_shutdown(b);
        break;
    case BIO_CTRL_SET_CLOSE:
        BIO_set_shutdown(b, (int)num);
        break;
    case BIO_CTRL_WPENDING:
        ret = BIO_ctrl(ssl->wbio, cmd, num, ptr);
        break;
    case BIO_CTRL_PENDING:
        ret = SSL_pending(ssl);
        if (ret == 0)
            ret = BIO_pending(ssl->rbio);
        break;
    case BIO_CTRL_FLUSH:
        BIO_clear_retry_flags(b);
        ret = BIO_ctrl(ssl->wbio, cmd, num, ptr);
        BIO_copy_next_retry(b);
        break;
    case BIO_CTRL_PUSH:
        if ((next != NULL) && (next != ssl->rbio)) {
            /*
             * We are going to pass ownership of next to the SSL object...but
             * we don't own a reference to pass yet - so up ref
             */
            BIO_up_ref(next);
            SSL_set_bio(ssl, next, next);
        }
        break;
    case BIO_CTRL_POP:
        /* Only detach if we are the BIO explicitly being popped */
        if (b == ptr) {
            /* This will clear the reference we obtained during push */
            SSL_set_bio(ssl, NULL, NULL);
        }
        break;
    case BIO_C_DO_STATE_MACHINE:
        BIO_clear_retry_flags(b);

        BIO_set_retry_reason(b, 0);
        ret = (int)SSL_do_handshake(ssl);

        switch (SSL_get_error(ssl, (int)ret)) {
        case SSL_ERROR_WANT_READ:
            BIO_set_flags(b, BIO_FLAGS_READ | BIO_FLAGS_SHOULD_RETRY);
            break;
        case SSL_ERROR_WANT_WRITE:
            BIO_set_flags(b, BIO_FLAGS_WRITE | BIO_FLAGS_SHOULD_RETRY);
            break;
        case SSL_ERROR_WANT_CONNECT:
            BIO_set_flags(b, BIO_FLAGS_IO_SPECIAL | BIO_FLAGS_SHOULD_RETRY);
            BIO_set_retry_reason(b, BIO_get_retry_reason(next));
            break;
        case SSL_ERROR_WANT_X509_LOOKUP:
            BIO_set_retry_special(b);
            BIO_set_retry_reason(b, BIO_RR_SSL_X509_LOOKUP);
            break;
        default:
            break;
        }
        break;
    case BIO_CTRL_DUP:
        dbio = (BIO *)ptr;
        dbs = BIO_get_data(dbio);
        SSL_free(dbs->ssl);
        dbs->ssl = SSL_dup(ssl);
        dbs->num_renegotiates = bs->num_renegotiates;
        dbs->renegotiate_count = bs->renegotiate_count;
        dbs->byte_count = bs->byte_count;
        dbs->renegotiate_timeout = bs->renegotiate_timeout;
        dbs->last_time = bs->last_time;
        ret = (dbs->ssl != NULL);
        break;
    case BIO_C_GET_FD:
        ret = BIO_ctrl(ssl->rbio, cmd, num, ptr);
        break;
    case BIO_CTRL_SET_CALLBACK:
        ret = 0; /* use callback ctrl */
        break;
    default:
        ret = BIO_ctrl(ssl->rbio, cmd, num, ptr);
        break;
    }
    return ret;
}

static long ssl_callback_ctrl(BIO *b, int cmd, BIO_info_cb *fp)
{
    SSL *ssl;
    BIO_SSL *bs;
    long ret = 1;

    bs = BIO_get_data(b);
    ssl = bs->ssl;
    switch (cmd) {
    case BIO_CTRL_SET_CALLBACK:
        ret = BIO_callback_ctrl(ssl->rbio, cmd, fp);
        break;
    default:
        ret = 0;
        break;
    }
    return ret;
}

static int ssl_puts(BIO *bp, const char *str)
{
    int n, ret;

    n = strlen(str);
    ret = BIO_write(bp, str, n);
    return ret;
}

BIO *BIO_new_buffer_ssl_connect(SSL_CTX *ctx)
{
#ifndef OPENSSL_NO_SOCK
    BIO *ret = NULL, *buf = NULL, *ssl = NULL;

    if ((buf = BIO_new(BIO_f_buffer())) == NULL)
        return NULL;
    if ((ssl = BIO_new_ssl_connect(ctx)) == NULL)
        goto err;
    if ((ret = BIO_push(buf, ssl)) == NULL)
        goto err;
    return ret;
 err:
    BIO_free(buf);
    BIO_free(ssl);
#endif
    return NULL;
}

BIO *BIO_new_ssl_connect(SSL_CTX *ctx)
{
#ifndef OPENSSL_NO_SOCK
    BIO *ret = NULL, *con = NULL, *ssl = NULL;

    if ((con = BIO_new(BIO_s_connect())) == NULL)
        return NULL;
    if ((ssl = BIO_new_ssl(ctx, 1)) == NULL)
        goto err;
    if ((ret = BIO_push(ssl, con)) == NULL)
        goto err;
    return ret;
 err:
    BIO_free(con);
#endif
    return NULL;
}

BIO *BIO_new_ssl(SSL_CTX *ctx, int client)
{
    BIO *ret;
    SSL *ssl;

    if ((ret = BIO_new(BIO_f_ssl())) == NULL)
        return NULL;
    if ((ssl = SSL_new(ctx)) == NULL) {
        BIO_free(ret);
        return NULL;
    }
    if (client)
        SSL_set_connect_state(ssl);
    else
        SSL_set_accept_state(ssl);

    BIO_set_ssl(ret, ssl, BIO_CLOSE);
    return ret;
}

int BIO_ssl_copy_session_id(BIO *t, BIO *f)
{
    BIO_SSL *tdata, *fdata;
    t = BIO_find_type(t, BIO_TYPE_SSL);
    f = BIO_find_type(f, BIO_TYPE_SSL);
    if ((t == NULL) || (f == NULL))
        return 0;
    tdata = BIO_get_data(t);
    fdata = BIO_get_data(f);
    if ((tdata->ssl == NULL) || (fdata->ssl == NULL))
        return 0;
    if (!SSL_copy_session_id(tdata->ssl, (fdata->ssl)))
        return 0;
    return 1;
}

void BIO_ssl_shutdown(BIO *b)
{
    BIO_SSL *bdata;

    for (; b != NULL; b = BIO_next(b)) {
        if (BIO_method_type(b) != BIO_TYPE_SSL)
            continue;
        bdata = BIO_get_data(b);
        if (bdata != NULL && bdata->ssl != NULL)
            SSL_shutdown(bdata->ssl);
    }
}
