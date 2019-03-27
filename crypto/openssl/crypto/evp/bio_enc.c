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
#include "internal/cryptlib.h"
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include "internal/bio.h"

static int enc_write(BIO *h, const char *buf, int num);
static int enc_read(BIO *h, char *buf, int size);
static long enc_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int enc_new(BIO *h);
static int enc_free(BIO *data);
static long enc_callback_ctrl(BIO *h, int cmd, BIO_info_cb *fps);
#define ENC_BLOCK_SIZE  (1024*4)
#define ENC_MIN_CHUNK   (256)
#define BUF_OFFSET      (ENC_MIN_CHUNK + EVP_MAX_BLOCK_LENGTH)

typedef struct enc_struct {
    int buf_len;
    int buf_off;
    int cont;                   /* <= 0 when finished */
    int finished;
    int ok;                     /* bad decrypt */
    EVP_CIPHER_CTX *cipher;
    unsigned char *read_start, *read_end;
    /*
     * buf is larger than ENC_BLOCK_SIZE because EVP_DecryptUpdate can return
     * up to a block more data than is presented to it
     */
    unsigned char buf[BUF_OFFSET + ENC_BLOCK_SIZE];
} BIO_ENC_CTX;

static const BIO_METHOD methods_enc = {
    BIO_TYPE_CIPHER,
    "cipher",
    /* TODO: Convert to new style write function */
    bwrite_conv,
    enc_write,
    /* TODO: Convert to new style read function */
    bread_conv,
    enc_read,
    NULL,                       /* enc_puts, */
    NULL,                       /* enc_gets, */
    enc_ctrl,
    enc_new,
    enc_free,
    enc_callback_ctrl,
};

const BIO_METHOD *BIO_f_cipher(void)
{
    return &methods_enc;
}

static int enc_new(BIO *bi)
{
    BIO_ENC_CTX *ctx;

    if ((ctx = OPENSSL_zalloc(sizeof(*ctx))) == NULL) {
        EVPerr(EVP_F_ENC_NEW, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    ctx->cipher = EVP_CIPHER_CTX_new();
    if (ctx->cipher == NULL) {
        OPENSSL_free(ctx);
        return 0;
    }
    ctx->cont = 1;
    ctx->ok = 1;
    ctx->read_end = ctx->read_start = &(ctx->buf[BUF_OFFSET]);
    BIO_set_data(bi, ctx);
    BIO_set_init(bi, 1);

    return 1;
}

static int enc_free(BIO *a)
{
    BIO_ENC_CTX *b;

    if (a == NULL)
        return 0;

    b = BIO_get_data(a);
    if (b == NULL)
        return 0;

    EVP_CIPHER_CTX_free(b->cipher);
    OPENSSL_clear_free(b, sizeof(BIO_ENC_CTX));
    BIO_set_data(a, NULL);
    BIO_set_init(a, 0);

    return 1;
}

static int enc_read(BIO *b, char *out, int outl)
{
    int ret = 0, i, blocksize;
    BIO_ENC_CTX *ctx;
    BIO *next;

    if (out == NULL)
        return 0;
    ctx = BIO_get_data(b);

    next = BIO_next(b);
    if ((ctx == NULL) || (next == NULL))
        return 0;

    /* First check if there are bytes decoded/encoded */
    if (ctx->buf_len > 0) {
        i = ctx->buf_len - ctx->buf_off;
        if (i > outl)
            i = outl;
        memcpy(out, &(ctx->buf[ctx->buf_off]), i);
        ret = i;
        out += i;
        outl -= i;
        ctx->buf_off += i;
        if (ctx->buf_len == ctx->buf_off) {
            ctx->buf_len = 0;
            ctx->buf_off = 0;
        }
    }

    blocksize = EVP_CIPHER_CTX_block_size(ctx->cipher);
    if (blocksize == 1)
        blocksize = 0;

    /*
     * At this point, we have room of outl bytes and an empty buffer, so we
     * should read in some more.
     */

    while (outl > 0) {
        if (ctx->cont <= 0)
            break;

        if (ctx->read_start == ctx->read_end) { /* time to read more data */
            ctx->read_end = ctx->read_start = &(ctx->buf[BUF_OFFSET]);
            i = BIO_read(next, ctx->read_start, ENC_BLOCK_SIZE);
            if (i > 0)
                ctx->read_end += i;
        } else {
            i = ctx->read_end - ctx->read_start;
        }

        if (i <= 0) {
            /* Should be continue next time we are called? */
            if (!BIO_should_retry(next)) {
                ctx->cont = i;
                i = EVP_CipherFinal_ex(ctx->cipher,
                                       ctx->buf, &(ctx->buf_len));
                ctx->ok = i;
                ctx->buf_off = 0;
            } else {
                ret = (ret == 0) ? i : ret;
                break;
            }
        } else {
            if (outl > ENC_MIN_CHUNK) {
                /*
                 * Depending on flags block cipher decrypt can write
                 * one extra block and then back off, i.e. output buffer
                 * has to accommodate extra block...
                 */
                int j = outl - blocksize, buf_len;

                if (!EVP_CipherUpdate(ctx->cipher,
                                      (unsigned char *)out, &buf_len,
                                      ctx->read_start, i > j ? j : i)) {
                    BIO_clear_retry_flags(b);
                    return 0;
                }
                ret += buf_len;
                out += buf_len;
                outl -= buf_len;

                if ((i -= j) <= 0) {
                    ctx->read_start = ctx->read_end;
                    continue;
                }
                ctx->read_start += j;
            }
            if (i > ENC_MIN_CHUNK)
                i = ENC_MIN_CHUNK;
            if (!EVP_CipherUpdate(ctx->cipher,
                                  ctx->buf, &ctx->buf_len,
                                  ctx->read_start, i)) {
                BIO_clear_retry_flags(b);
                ctx->ok = 0;
                return 0;
            }
            ctx->read_start += i;
            ctx->cont = 1;
            /*
             * Note: it is possible for EVP_CipherUpdate to decrypt zero
             * bytes because this is or looks like the final block: if this
             * happens we should retry and either read more data or decrypt
             * the final block
             */
            if (ctx->buf_len == 0)
                continue;
        }

        if (ctx->buf_len <= outl)
            i = ctx->buf_len;
        else
            i = outl;
        if (i <= 0)
            break;
        memcpy(out, ctx->buf, i);
        ret += i;
        ctx->buf_off = i;
        outl -= i;
        out += i;
    }

    BIO_clear_retry_flags(b);
    BIO_copy_next_retry(b);
    return ((ret == 0) ? ctx->cont : ret);
}

static int enc_write(BIO *b, const char *in, int inl)
{
    int ret = 0, n, i;
    BIO_ENC_CTX *ctx;
    BIO *next;

    ctx = BIO_get_data(b);
    next = BIO_next(b);
    if ((ctx == NULL) || (next == NULL))
        return 0;

    ret = inl;

    BIO_clear_retry_flags(b);
    n = ctx->buf_len - ctx->buf_off;
    while (n > 0) {
        i = BIO_write(next, &(ctx->buf[ctx->buf_off]), n);
        if (i <= 0) {
            BIO_copy_next_retry(b);
            return i;
        }
        ctx->buf_off += i;
        n -= i;
    }
    /* at this point all pending data has been written */

    if ((in == NULL) || (inl <= 0))
        return 0;

    ctx->buf_off = 0;
    while (inl > 0) {
        n = (inl > ENC_BLOCK_SIZE) ? ENC_BLOCK_SIZE : inl;
        if (!EVP_CipherUpdate(ctx->cipher,
                              ctx->buf, &ctx->buf_len,
                              (const unsigned char *)in, n)) {
            BIO_clear_retry_flags(b);
            ctx->ok = 0;
            return 0;
        }
        inl -= n;
        in += n;

        ctx->buf_off = 0;
        n = ctx->buf_len;
        while (n > 0) {
            i = BIO_write(next, &(ctx->buf[ctx->buf_off]), n);
            if (i <= 0) {
                BIO_copy_next_retry(b);
                return (ret == inl) ? i : ret - inl;
            }
            n -= i;
            ctx->buf_off += i;
        }
        ctx->buf_len = 0;
        ctx->buf_off = 0;
    }
    BIO_copy_next_retry(b);
    return ret;
}

static long enc_ctrl(BIO *b, int cmd, long num, void *ptr)
{
    BIO *dbio;
    BIO_ENC_CTX *ctx, *dctx;
    long ret = 1;
    int i;
    EVP_CIPHER_CTX **c_ctx;
    BIO *next;

    ctx = BIO_get_data(b);
    next = BIO_next(b);
    if (ctx == NULL)
        return 0;

    switch (cmd) {
    case BIO_CTRL_RESET:
        ctx->ok = 1;
        ctx->finished = 0;
        if (!EVP_CipherInit_ex(ctx->cipher, NULL, NULL, NULL, NULL,
                               EVP_CIPHER_CTX_encrypting(ctx->cipher)))
            return 0;
        ret = BIO_ctrl(next, cmd, num, ptr);
        break;
    case BIO_CTRL_EOF:         /* More to read */
        if (ctx->cont <= 0)
            ret = 1;
        else
            ret = BIO_ctrl(next, cmd, num, ptr);
        break;
    case BIO_CTRL_WPENDING:
        ret = ctx->buf_len - ctx->buf_off;
        if (ret <= 0)
            ret = BIO_ctrl(next, cmd, num, ptr);
        break;
    case BIO_CTRL_PENDING:     /* More to read in buffer */
        ret = ctx->buf_len - ctx->buf_off;
        if (ret <= 0)
            ret = BIO_ctrl(next, cmd, num, ptr);
        break;
    case BIO_CTRL_FLUSH:
        /* do a final write */
 again:
        while (ctx->buf_len != ctx->buf_off) {
            i = enc_write(b, NULL, 0);
            if (i < 0)
                return i;
        }

        if (!ctx->finished) {
            ctx->finished = 1;
            ctx->buf_off = 0;
            ret = EVP_CipherFinal_ex(ctx->cipher,
                                     (unsigned char *)ctx->buf,
                                     &(ctx->buf_len));
            ctx->ok = (int)ret;
            if (ret <= 0)
                break;

            /* push out the bytes */
            goto again;
        }

        /* Finally flush the underlying BIO */
        ret = BIO_ctrl(next, cmd, num, ptr);
        break;
    case BIO_C_GET_CIPHER_STATUS:
        ret = (long)ctx->ok;
        break;
    case BIO_C_DO_STATE_MACHINE:
        BIO_clear_retry_flags(b);
        ret = BIO_ctrl(next, cmd, num, ptr);
        BIO_copy_next_retry(b);
        break;
    case BIO_C_GET_CIPHER_CTX:
        c_ctx = (EVP_CIPHER_CTX **)ptr;
        *c_ctx = ctx->cipher;
        BIO_set_init(b, 1);
        break;
    case BIO_CTRL_DUP:
        dbio = (BIO *)ptr;
        dctx = BIO_get_data(dbio);
        dctx->cipher = EVP_CIPHER_CTX_new();
        if (dctx->cipher == NULL)
            return 0;
        ret = EVP_CIPHER_CTX_copy(dctx->cipher, ctx->cipher);
        if (ret)
            BIO_set_init(dbio, 1);
        break;
    default:
        ret = BIO_ctrl(next, cmd, num, ptr);
        break;
    }
    return ret;
}

static long enc_callback_ctrl(BIO *b, int cmd, BIO_info_cb *fp)
{
    long ret = 1;
    BIO *next = BIO_next(b);

    if (next == NULL)
        return 0;
    switch (cmd) {
    default:
        ret = BIO_callback_ctrl(next, cmd, fp);
        break;
    }
    return ret;
}

int BIO_set_cipher(BIO *b, const EVP_CIPHER *c, const unsigned char *k,
                   const unsigned char *i, int e)
{
    BIO_ENC_CTX *ctx;
    long (*callback) (struct bio_st *, int, const char *, int, long, long);

    ctx = BIO_get_data(b);
    if (ctx == NULL)
        return 0;

    callback = BIO_get_callback(b);

    if ((callback != NULL) &&
            (callback(b, BIO_CB_CTRL, (const char *)c, BIO_CTRL_SET, e,
                      0L) <= 0))
        return 0;

    BIO_set_init(b, 1);

    if (!EVP_CipherInit_ex(ctx->cipher, c, NULL, k, i, e))
        return 0;

    if (callback != NULL)
        return callback(b, BIO_CB_CTRL, (const char *)c, BIO_CTRL_SET, e, 1L);
    return 1;
}
