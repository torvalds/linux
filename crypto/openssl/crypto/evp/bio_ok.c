/*
 * Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*-
        From: Arne Ansper

        Why BIO_f_reliable?

        I wrote function which took BIO* as argument, read data from it
        and processed it. Then I wanted to store the input file in
        encrypted form. OK I pushed BIO_f_cipher to the BIO stack
        and everything was OK. BUT if user types wrong password
        BIO_f_cipher outputs only garbage and my function crashes. Yes
        I can and I should fix my function, but BIO_f_cipher is
        easy way to add encryption support to many existing applications
        and it's hard to debug and fix them all.

        So I wanted another BIO which would catch the incorrect passwords and
        file damages which cause garbage on BIO_f_cipher's output.

        The easy way is to push the BIO_f_md and save the checksum at
        the end of the file. However there are several problems with this
        approach:

        1) you must somehow separate checksum from actual data.
        2) you need lot's of memory when reading the file, because you
        must read to the end of the file and verify the checksum before
        letting the application to read the data.

        BIO_f_reliable tries to solve both problems, so that you can
        read and write arbitrary long streams using only fixed amount
        of memory.

        BIO_f_reliable splits data stream into blocks. Each block is prefixed
        with it's length and suffixed with it's digest. So you need only
        several Kbytes of memory to buffer single block before verifying
        it's digest.

        BIO_f_reliable goes further and adds several important capabilities:

        1) the digest of the block is computed over the whole stream
        -- so nobody can rearrange the blocks or remove or replace them.

        2) to detect invalid passwords right at the start BIO_f_reliable
        adds special prefix to the stream. In order to avoid known plain-text
        attacks this prefix is generated as follows:

                *) digest is initialized with random seed instead of
                standardized one.
                *) same seed is written to output
                *) well-known text is then hashed and the output
                of the digest is also written to output.

        reader can now read the seed from stream, hash the same string
        and then compare the digest output.

        Bad things: BIO_f_reliable knows what's going on in EVP_Digest. I
        initially wrote and tested this code on x86 machine and wrote the
        digests out in machine-dependent order :( There are people using
        this code and I cannot change this easily without making existing
        data files unreadable.

*/

#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include "internal/cryptlib.h"
#include <openssl/buffer.h>
#include "internal/bio.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include "internal/evp_int.h"

static int ok_write(BIO *h, const char *buf, int num);
static int ok_read(BIO *h, char *buf, int size);
static long ok_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int ok_new(BIO *h);
static int ok_free(BIO *data);
static long ok_callback_ctrl(BIO *h, int cmd, BIO_info_cb *fp);

static __owur int sig_out(BIO *b);
static __owur int sig_in(BIO *b);
static __owur int block_out(BIO *b);
static __owur int block_in(BIO *b);
#define OK_BLOCK_SIZE   (1024*4)
#define OK_BLOCK_BLOCK  4
#define IOBS            (OK_BLOCK_SIZE+ OK_BLOCK_BLOCK+ 3*EVP_MAX_MD_SIZE)
#define WELLKNOWN "The quick brown fox jumped over the lazy dog's back."

typedef struct ok_struct {
    size_t buf_len;
    size_t buf_off;
    size_t buf_len_save;
    size_t buf_off_save;
    int cont;                   /* <= 0 when finished */
    int finished;
    EVP_MD_CTX *md;
    int blockout;               /* output block is ready */
    int sigio;                  /* must process signature */
    unsigned char buf[IOBS];
} BIO_OK_CTX;

static const BIO_METHOD methods_ok = {
    BIO_TYPE_CIPHER,
    "reliable",
    /* TODO: Convert to new style write function */
    bwrite_conv,
    ok_write,
    /* TODO: Convert to new style read function */
    bread_conv,
    ok_read,
    NULL,                       /* ok_puts, */
    NULL,                       /* ok_gets, */
    ok_ctrl,
    ok_new,
    ok_free,
    ok_callback_ctrl,
};

const BIO_METHOD *BIO_f_reliable(void)
{
    return &methods_ok;
}

static int ok_new(BIO *bi)
{
    BIO_OK_CTX *ctx;

    if ((ctx = OPENSSL_zalloc(sizeof(*ctx))) == NULL) {
        EVPerr(EVP_F_OK_NEW, ERR_R_MALLOC_FAILURE);
        return 0;
    }

    ctx->cont = 1;
    ctx->sigio = 1;
    ctx->md = EVP_MD_CTX_new();
    if (ctx->md == NULL) {
        OPENSSL_free(ctx);
        return 0;
    }
    BIO_set_init(bi, 0);
    BIO_set_data(bi, ctx);

    return 1;
}

static int ok_free(BIO *a)
{
    BIO_OK_CTX *ctx;

    if (a == NULL)
        return 0;

    ctx = BIO_get_data(a);

    EVP_MD_CTX_free(ctx->md);
    OPENSSL_clear_free(ctx, sizeof(BIO_OK_CTX));
    BIO_set_data(a, NULL);
    BIO_set_init(a, 0);

    return 1;
}

static int ok_read(BIO *b, char *out, int outl)
{
    int ret = 0, i, n;
    BIO_OK_CTX *ctx;
    BIO *next;

    if (out == NULL)
        return 0;

    ctx = BIO_get_data(b);
    next = BIO_next(b);

    if ((ctx == NULL) || (next == NULL) || (BIO_get_init(b) == 0))
        return 0;

    while (outl > 0) {

        /* copy clean bytes to output buffer */
        if (ctx->blockout) {
            i = ctx->buf_len - ctx->buf_off;
            if (i > outl)
                i = outl;
            memcpy(out, &(ctx->buf[ctx->buf_off]), i);
            ret += i;
            out += i;
            outl -= i;
            ctx->buf_off += i;

            /* all clean bytes are out */
            if (ctx->buf_len == ctx->buf_off) {
                ctx->buf_off = 0;

                /*
                 * copy start of the next block into proper place
                 */
                if (ctx->buf_len_save - ctx->buf_off_save > 0) {
                    ctx->buf_len = ctx->buf_len_save - ctx->buf_off_save;
                    memmove(ctx->buf, &(ctx->buf[ctx->buf_off_save]),
                            ctx->buf_len);
                } else {
                    ctx->buf_len = 0;
                }
                ctx->blockout = 0;
            }
        }

        /* output buffer full -- cancel */
        if (outl == 0)
            break;

        /* no clean bytes in buffer -- fill it */
        n = IOBS - ctx->buf_len;
        i = BIO_read(next, &(ctx->buf[ctx->buf_len]), n);

        if (i <= 0)
            break;              /* nothing new */

        ctx->buf_len += i;

        /* no signature yet -- check if we got one */
        if (ctx->sigio == 1) {
            if (!sig_in(b)) {
                BIO_clear_retry_flags(b);
                return 0;
            }
        }

        /* signature ok -- check if we got block */
        if (ctx->sigio == 0) {
            if (!block_in(b)) {
                BIO_clear_retry_flags(b);
                return 0;
            }
        }

        /* invalid block -- cancel */
        if (ctx->cont <= 0)
            break;

    }

    BIO_clear_retry_flags(b);
    BIO_copy_next_retry(b);
    return ret;
}

static int ok_write(BIO *b, const char *in, int inl)
{
    int ret = 0, n, i;
    BIO_OK_CTX *ctx;
    BIO *next;

    if (inl <= 0)
        return inl;

    ctx = BIO_get_data(b);
    next = BIO_next(b);
    ret = inl;

    if ((ctx == NULL) || (next == NULL) || (BIO_get_init(b) == 0))
        return 0;

    if (ctx->sigio && !sig_out(b))
        return 0;

    do {
        BIO_clear_retry_flags(b);
        n = ctx->buf_len - ctx->buf_off;
        while (ctx->blockout && n > 0) {
            i = BIO_write(next, &(ctx->buf[ctx->buf_off]), n);
            if (i <= 0) {
                BIO_copy_next_retry(b);
                if (!BIO_should_retry(b))
                    ctx->cont = 0;
                return i;
            }
            ctx->buf_off += i;
            n -= i;
        }

        /* at this point all pending data has been written */
        ctx->blockout = 0;
        if (ctx->buf_len == ctx->buf_off) {
            ctx->buf_len = OK_BLOCK_BLOCK;
            ctx->buf_off = 0;
        }

        if ((in == NULL) || (inl <= 0))
            return 0;

        n = (inl + ctx->buf_len > OK_BLOCK_SIZE + OK_BLOCK_BLOCK) ?
            (int)(OK_BLOCK_SIZE + OK_BLOCK_BLOCK - ctx->buf_len) : inl;

        memcpy(&ctx->buf[ctx->buf_len], in, n);
        ctx->buf_len += n;
        inl -= n;
        in += n;

        if (ctx->buf_len >= OK_BLOCK_SIZE + OK_BLOCK_BLOCK) {
            if (!block_out(b)) {
                BIO_clear_retry_flags(b);
                return 0;
            }
        }
    } while (inl > 0);

    BIO_clear_retry_flags(b);
    BIO_copy_next_retry(b);
    return ret;
}

static long ok_ctrl(BIO *b, int cmd, long num, void *ptr)
{
    BIO_OK_CTX *ctx;
    EVP_MD *md;
    const EVP_MD **ppmd;
    long ret = 1;
    int i;
    BIO *next;

    ctx = BIO_get_data(b);
    next = BIO_next(b);

    switch (cmd) {
    case BIO_CTRL_RESET:
        ctx->buf_len = 0;
        ctx->buf_off = 0;
        ctx->buf_len_save = 0;
        ctx->buf_off_save = 0;
        ctx->cont = 1;
        ctx->finished = 0;
        ctx->blockout = 0;
        ctx->sigio = 1;
        ret = BIO_ctrl(next, cmd, num, ptr);
        break;
    case BIO_CTRL_EOF:         /* More to read */
        if (ctx->cont <= 0)
            ret = 1;
        else
            ret = BIO_ctrl(next, cmd, num, ptr);
        break;
    case BIO_CTRL_PENDING:     /* More to read in buffer */
    case BIO_CTRL_WPENDING:    /* More to read in buffer */
        ret = ctx->blockout ? ctx->buf_len - ctx->buf_off : 0;
        if (ret <= 0)
            ret = BIO_ctrl(next, cmd, num, ptr);
        break;
    case BIO_CTRL_FLUSH:
        /* do a final write */
        if (ctx->blockout == 0)
            if (!block_out(b))
                return 0;

        while (ctx->blockout) {
            i = ok_write(b, NULL, 0);
            if (i < 0) {
                ret = i;
                break;
            }
        }

        ctx->finished = 1;
        ctx->buf_off = ctx->buf_len = 0;
        ctx->cont = (int)ret;

        /* Finally flush the underlying BIO */
        ret = BIO_ctrl(next, cmd, num, ptr);
        break;
    case BIO_C_DO_STATE_MACHINE:
        BIO_clear_retry_flags(b);
        ret = BIO_ctrl(next, cmd, num, ptr);
        BIO_copy_next_retry(b);
        break;
    case BIO_CTRL_INFO:
        ret = (long)ctx->cont;
        break;
    case BIO_C_SET_MD:
        md = ptr;
        if (!EVP_DigestInit_ex(ctx->md, md, NULL))
            return 0;
        BIO_set_init(b, 1);
        break;
    case BIO_C_GET_MD:
        if (BIO_get_init(b)) {
            ppmd = ptr;
            *ppmd = EVP_MD_CTX_md(ctx->md);
        } else
            ret = 0;
        break;
    default:
        ret = BIO_ctrl(next, cmd, num, ptr);
        break;
    }
    return ret;
}

static long ok_callback_ctrl(BIO *b, int cmd, BIO_info_cb *fp)
{
    long ret = 1;
    BIO *next;

    next = BIO_next(b);

    if (next == NULL)
        return 0;

    switch (cmd) {
    default:
        ret = BIO_callback_ctrl(next, cmd, fp);
        break;
    }

    return ret;
}

static void longswap(void *_ptr, size_t len)
{
    const union {
        long one;
        char little;
    } is_endian = {
        1
    };

    if (is_endian.little) {
        size_t i;
        unsigned char *p = _ptr, c;

        for (i = 0; i < len; i += 4) {
            c = p[0], p[0] = p[3], p[3] = c;
            c = p[1], p[1] = p[2], p[2] = c;
        }
    }
}

static int sig_out(BIO *b)
{
    BIO_OK_CTX *ctx;
    EVP_MD_CTX *md;
    const EVP_MD *digest;
    int md_size;
    void *md_data;

    ctx = BIO_get_data(b);
    md = ctx->md;
    digest = EVP_MD_CTX_md(md);
    md_size = EVP_MD_size(digest);
    md_data = EVP_MD_CTX_md_data(md);

    if (ctx->buf_len + 2 * md_size > OK_BLOCK_SIZE)
        return 1;

    if (!EVP_DigestInit_ex(md, digest, NULL))
        goto berr;
    /*
     * FIXME: there's absolutely no guarantee this makes any sense at all,
     * particularly now EVP_MD_CTX has been restructured.
     */
    if (RAND_bytes(md_data, md_size) <= 0)
        goto berr;
    memcpy(&(ctx->buf[ctx->buf_len]), md_data, md_size);
    longswap(&(ctx->buf[ctx->buf_len]), md_size);
    ctx->buf_len += md_size;

    if (!EVP_DigestUpdate(md, WELLKNOWN, strlen(WELLKNOWN)))
        goto berr;
    if (!EVP_DigestFinal_ex(md, &(ctx->buf[ctx->buf_len]), NULL))
        goto berr;
    ctx->buf_len += md_size;
    ctx->blockout = 1;
    ctx->sigio = 0;
    return 1;
 berr:
    BIO_clear_retry_flags(b);
    return 0;
}

static int sig_in(BIO *b)
{
    BIO_OK_CTX *ctx;
    EVP_MD_CTX *md;
    unsigned char tmp[EVP_MAX_MD_SIZE];
    int ret = 0;
    const EVP_MD *digest;
    int md_size;
    void *md_data;

    ctx = BIO_get_data(b);
    md = ctx->md;
    digest = EVP_MD_CTX_md(md);
    md_size = EVP_MD_size(digest);
    md_data = EVP_MD_CTX_md_data(md);

    if ((int)(ctx->buf_len - ctx->buf_off) < 2 * md_size)
        return 1;

    if (!EVP_DigestInit_ex(md, digest, NULL))
        goto berr;
    memcpy(md_data, &(ctx->buf[ctx->buf_off]), md_size);
    longswap(md_data, md_size);
    ctx->buf_off += md_size;

    if (!EVP_DigestUpdate(md, WELLKNOWN, strlen(WELLKNOWN)))
        goto berr;
    if (!EVP_DigestFinal_ex(md, tmp, NULL))
        goto berr;
    ret = memcmp(&(ctx->buf[ctx->buf_off]), tmp, md_size) == 0;
    ctx->buf_off += md_size;
    if (ret == 1) {
        ctx->sigio = 0;
        if (ctx->buf_len != ctx->buf_off) {
            memmove(ctx->buf, &(ctx->buf[ctx->buf_off]),
                    ctx->buf_len - ctx->buf_off);
        }
        ctx->buf_len -= ctx->buf_off;
        ctx->buf_off = 0;
    } else {
        ctx->cont = 0;
    }
    return 1;
 berr:
    BIO_clear_retry_flags(b);
    return 0;
}

static int block_out(BIO *b)
{
    BIO_OK_CTX *ctx;
    EVP_MD_CTX *md;
    unsigned long tl;
    const EVP_MD *digest;
    int md_size;

    ctx = BIO_get_data(b);
    md = ctx->md;
    digest = EVP_MD_CTX_md(md);
    md_size = EVP_MD_size(digest);

    tl = ctx->buf_len - OK_BLOCK_BLOCK;
    ctx->buf[0] = (unsigned char)(tl >> 24);
    ctx->buf[1] = (unsigned char)(tl >> 16);
    ctx->buf[2] = (unsigned char)(tl >> 8);
    ctx->buf[3] = (unsigned char)(tl);
    if (!EVP_DigestUpdate(md,
                          (unsigned char *)&(ctx->buf[OK_BLOCK_BLOCK]), tl))
        goto berr;
    if (!EVP_DigestFinal_ex(md, &(ctx->buf[ctx->buf_len]), NULL))
        goto berr;
    ctx->buf_len += md_size;
    ctx->blockout = 1;
    return 1;
 berr:
    BIO_clear_retry_flags(b);
    return 0;
}

static int block_in(BIO *b)
{
    BIO_OK_CTX *ctx;
    EVP_MD_CTX *md;
    unsigned long tl = 0;
    unsigned char tmp[EVP_MAX_MD_SIZE];
    int md_size;

    ctx = BIO_get_data(b);
    md = ctx->md;
    md_size = EVP_MD_size(EVP_MD_CTX_md(md));

    assert(sizeof(tl) >= OK_BLOCK_BLOCK); /* always true */
    tl = ctx->buf[0];
    tl <<= 8;
    tl |= ctx->buf[1];
    tl <<= 8;
    tl |= ctx->buf[2];
    tl <<= 8;
    tl |= ctx->buf[3];

    if (ctx->buf_len < tl + OK_BLOCK_BLOCK + md_size)
        return 1;

    if (!EVP_DigestUpdate(md,
                          (unsigned char *)&(ctx->buf[OK_BLOCK_BLOCK]), tl))
        goto berr;
    if (!EVP_DigestFinal_ex(md, tmp, NULL))
        goto berr;
    if (memcmp(&(ctx->buf[tl + OK_BLOCK_BLOCK]), tmp, md_size) == 0) {
        /* there might be parts from next block lurking around ! */
        ctx->buf_off_save = tl + OK_BLOCK_BLOCK + md_size;
        ctx->buf_len_save = ctx->buf_len;
        ctx->buf_off = OK_BLOCK_BLOCK;
        ctx->buf_len = tl + OK_BLOCK_BLOCK;
        ctx->blockout = 1;
    } else {
        ctx->cont = 0;
    }
    return 1;
 berr:
    BIO_clear_retry_flags(b);
    return 0;
}
