/*
 * Copyright 2006-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * Experimental ASN1 BIO. When written through the data is converted to an
 * ASN1 string type: default is OCTET STRING. Additional functions can be
 * provided to add prefix and suffix data.
 */

#include <string.h>
#include "internal/bio.h"
#include <openssl/asn1.h>
#include "internal/cryptlib.h"

/* Must be large enough for biggest tag+length */
#define DEFAULT_ASN1_BUF_SIZE 20

typedef enum {
    ASN1_STATE_START,
    ASN1_STATE_PRE_COPY,
    ASN1_STATE_HEADER,
    ASN1_STATE_HEADER_COPY,
    ASN1_STATE_DATA_COPY,
    ASN1_STATE_POST_COPY,
    ASN1_STATE_DONE
} asn1_bio_state_t;

typedef struct BIO_ASN1_EX_FUNCS_st {
    asn1_ps_func *ex_func;
    asn1_ps_func *ex_free_func;
} BIO_ASN1_EX_FUNCS;

typedef struct BIO_ASN1_BUF_CTX_t {
    /* Internal state */
    asn1_bio_state_t state;
    /* Internal buffer */
    unsigned char *buf;
    /* Size of buffer */
    int bufsize;
    /* Current position in buffer */
    int bufpos;
    /* Current buffer length */
    int buflen;
    /* Amount of data to copy */
    int copylen;
    /* Class and tag to use */
    int asn1_class, asn1_tag;
    asn1_ps_func *prefix, *prefix_free, *suffix, *suffix_free;
    /* Extra buffer for prefix and suffix data */
    unsigned char *ex_buf;
    int ex_len;
    int ex_pos;
    void *ex_arg;
} BIO_ASN1_BUF_CTX;

static int asn1_bio_write(BIO *h, const char *buf, int num);
static int asn1_bio_read(BIO *h, char *buf, int size);
static int asn1_bio_puts(BIO *h, const char *str);
static int asn1_bio_gets(BIO *h, char *str, int size);
static long asn1_bio_ctrl(BIO *h, int cmd, long arg1, void *arg2);
static int asn1_bio_new(BIO *h);
static int asn1_bio_free(BIO *data);
static long asn1_bio_callback_ctrl(BIO *h, int cmd, BIO_info_cb *fp);

static int asn1_bio_init(BIO_ASN1_BUF_CTX *ctx, int size);
static int asn1_bio_flush_ex(BIO *b, BIO_ASN1_BUF_CTX *ctx,
                             asn1_ps_func *cleanup, asn1_bio_state_t next);
static int asn1_bio_setup_ex(BIO *b, BIO_ASN1_BUF_CTX *ctx,
                             asn1_ps_func *setup,
                             asn1_bio_state_t ex_state,
                             asn1_bio_state_t other_state);

static const BIO_METHOD methods_asn1 = {
    BIO_TYPE_ASN1,
    "asn1",
    /* TODO: Convert to new style write function */
    bwrite_conv,
    asn1_bio_write,
    /* TODO: Convert to new style read function */
    bread_conv,
    asn1_bio_read,
    asn1_bio_puts,
    asn1_bio_gets,
    asn1_bio_ctrl,
    asn1_bio_new,
    asn1_bio_free,
    asn1_bio_callback_ctrl,
};

const BIO_METHOD *BIO_f_asn1(void)
{
    return &methods_asn1;
}

static int asn1_bio_new(BIO *b)
{
    BIO_ASN1_BUF_CTX *ctx = OPENSSL_zalloc(sizeof(*ctx));

    if (ctx == NULL)
        return 0;
    if (!asn1_bio_init(ctx, DEFAULT_ASN1_BUF_SIZE)) {
        OPENSSL_free(ctx);
        return 0;
    }
    BIO_set_data(b, ctx);
    BIO_set_init(b, 1);

    return 1;
}

static int asn1_bio_init(BIO_ASN1_BUF_CTX *ctx, int size)
{
    if ((ctx->buf = OPENSSL_malloc(size)) == NULL) {
        ASN1err(ASN1_F_ASN1_BIO_INIT, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    ctx->bufsize = size;
    ctx->asn1_class = V_ASN1_UNIVERSAL;
    ctx->asn1_tag = V_ASN1_OCTET_STRING;
    ctx->state = ASN1_STATE_START;
    return 1;
}

static int asn1_bio_free(BIO *b)
{
    BIO_ASN1_BUF_CTX *ctx;

    if (b == NULL)
        return 0;

    ctx = BIO_get_data(b);
    if (ctx == NULL)
        return 0;

    OPENSSL_free(ctx->buf);
    OPENSSL_free(ctx);
    BIO_set_data(b, NULL);
    BIO_set_init(b, 0);

    return 1;
}

static int asn1_bio_write(BIO *b, const char *in, int inl)
{
    BIO_ASN1_BUF_CTX *ctx;
    int wrmax, wrlen, ret;
    unsigned char *p;
    BIO *next;

    ctx = BIO_get_data(b);
    next = BIO_next(b);
    if (in == NULL || inl < 0 || ctx == NULL || next == NULL)
        return 0;

    wrlen = 0;
    ret = -1;

    for (;;) {
        switch (ctx->state) {
            /* Setup prefix data, call it */
        case ASN1_STATE_START:
            if (!asn1_bio_setup_ex(b, ctx, ctx->prefix,
                                   ASN1_STATE_PRE_COPY, ASN1_STATE_HEADER))
                return 0;
            break;

            /* Copy any pre data first */
        case ASN1_STATE_PRE_COPY:

            ret = asn1_bio_flush_ex(b, ctx, ctx->prefix_free,
                                    ASN1_STATE_HEADER);

            if (ret <= 0)
                goto done;

            break;

        case ASN1_STATE_HEADER:
            ctx->buflen = ASN1_object_size(0, inl, ctx->asn1_tag) - inl;
            if (!ossl_assert(ctx->buflen <= ctx->bufsize))
                return 0;
            p = ctx->buf;
            ASN1_put_object(&p, 0, inl, ctx->asn1_tag, ctx->asn1_class);
            ctx->copylen = inl;
            ctx->state = ASN1_STATE_HEADER_COPY;

            break;

        case ASN1_STATE_HEADER_COPY:
            ret = BIO_write(next, ctx->buf + ctx->bufpos, ctx->buflen);
            if (ret <= 0)
                goto done;

            ctx->buflen -= ret;
            if (ctx->buflen)
                ctx->bufpos += ret;
            else {
                ctx->bufpos = 0;
                ctx->state = ASN1_STATE_DATA_COPY;
            }

            break;

        case ASN1_STATE_DATA_COPY:

            if (inl > ctx->copylen)
                wrmax = ctx->copylen;
            else
                wrmax = inl;
            ret = BIO_write(next, in, wrmax);
            if (ret <= 0)
                goto done;
            wrlen += ret;
            ctx->copylen -= ret;
            in += ret;
            inl -= ret;

            if (ctx->copylen == 0)
                ctx->state = ASN1_STATE_HEADER;

            if (inl == 0)
                goto done;

            break;

        case ASN1_STATE_POST_COPY:
        case ASN1_STATE_DONE:
            BIO_clear_retry_flags(b);
            return 0;

        }

    }

 done:
    BIO_clear_retry_flags(b);
    BIO_copy_next_retry(b);

    return (wrlen > 0) ? wrlen : ret;

}

static int asn1_bio_flush_ex(BIO *b, BIO_ASN1_BUF_CTX *ctx,
                             asn1_ps_func *cleanup, asn1_bio_state_t next)
{
    int ret;

    if (ctx->ex_len <= 0)
        return 1;
    for (;;) {
        ret = BIO_write(BIO_next(b), ctx->ex_buf + ctx->ex_pos, ctx->ex_len);
        if (ret <= 0)
            break;
        ctx->ex_len -= ret;
        if (ctx->ex_len > 0)
            ctx->ex_pos += ret;
        else {
            if (cleanup)
                cleanup(b, &ctx->ex_buf, &ctx->ex_len, &ctx->ex_arg);
            ctx->state = next;
            ctx->ex_pos = 0;
            break;
        }
    }
    return ret;
}

static int asn1_bio_setup_ex(BIO *b, BIO_ASN1_BUF_CTX *ctx,
                             asn1_ps_func *setup,
                             asn1_bio_state_t ex_state,
                             asn1_bio_state_t other_state)
{
    if (setup && !setup(b, &ctx->ex_buf, &ctx->ex_len, &ctx->ex_arg)) {
        BIO_clear_retry_flags(b);
        return 0;
    }
    if (ctx->ex_len > 0)
        ctx->state = ex_state;
    else
        ctx->state = other_state;
    return 1;
}

static int asn1_bio_read(BIO *b, char *in, int inl)
{
    BIO *next = BIO_next(b);
    if (next == NULL)
        return 0;
    return BIO_read(next, in, inl);
}

static int asn1_bio_puts(BIO *b, const char *str)
{
    return asn1_bio_write(b, str, strlen(str));
}

static int asn1_bio_gets(BIO *b, char *str, int size)
{
    BIO *next = BIO_next(b);
    if (next == NULL)
        return 0;
    return BIO_gets(next, str, size);
}

static long asn1_bio_callback_ctrl(BIO *b, int cmd, BIO_info_cb *fp)
{
    BIO *next = BIO_next(b);
    if (next == NULL)
        return 0;
    return BIO_callback_ctrl(next, cmd, fp);
}

static long asn1_bio_ctrl(BIO *b, int cmd, long arg1, void *arg2)
{
    BIO_ASN1_BUF_CTX *ctx;
    BIO_ASN1_EX_FUNCS *ex_func;
    long ret = 1;
    BIO *next;

    ctx = BIO_get_data(b);
    if (ctx == NULL)
        return 0;
    next = BIO_next(b);
    switch (cmd) {

    case BIO_C_SET_PREFIX:
        ex_func = arg2;
        ctx->prefix = ex_func->ex_func;
        ctx->prefix_free = ex_func->ex_free_func;
        break;

    case BIO_C_GET_PREFIX:
        ex_func = arg2;
        ex_func->ex_func = ctx->prefix;
        ex_func->ex_free_func = ctx->prefix_free;
        break;

    case BIO_C_SET_SUFFIX:
        ex_func = arg2;
        ctx->suffix = ex_func->ex_func;
        ctx->suffix_free = ex_func->ex_free_func;
        break;

    case BIO_C_GET_SUFFIX:
        ex_func = arg2;
        ex_func->ex_func = ctx->suffix;
        ex_func->ex_free_func = ctx->suffix_free;
        break;

    case BIO_C_SET_EX_ARG:
        ctx->ex_arg = arg2;
        break;

    case BIO_C_GET_EX_ARG:
        *(void **)arg2 = ctx->ex_arg;
        break;

    case BIO_CTRL_FLUSH:
        if (next == NULL)
            return 0;

        /* Call post function if possible */
        if (ctx->state == ASN1_STATE_HEADER) {
            if (!asn1_bio_setup_ex(b, ctx, ctx->suffix,
                                   ASN1_STATE_POST_COPY, ASN1_STATE_DONE))
                return 0;
        }

        if (ctx->state == ASN1_STATE_POST_COPY) {
            ret = asn1_bio_flush_ex(b, ctx, ctx->suffix_free,
                                    ASN1_STATE_DONE);
            if (ret <= 0)
                return ret;
        }

        if (ctx->state == ASN1_STATE_DONE)
            return BIO_ctrl(next, cmd, arg1, arg2);
        else {
            BIO_clear_retry_flags(b);
            return 0;
        }

    default:
        if (next == NULL)
            return 0;
        return BIO_ctrl(next, cmd, arg1, arg2);

    }

    return ret;
}

static int asn1_bio_set_ex(BIO *b, int cmd,
                           asn1_ps_func *ex_func, asn1_ps_func *ex_free_func)
{
    BIO_ASN1_EX_FUNCS extmp;
    extmp.ex_func = ex_func;
    extmp.ex_free_func = ex_free_func;
    return BIO_ctrl(b, cmd, 0, &extmp);
}

static int asn1_bio_get_ex(BIO *b, int cmd,
                           asn1_ps_func **ex_func,
                           asn1_ps_func **ex_free_func)
{
    BIO_ASN1_EX_FUNCS extmp;
    int ret;
    ret = BIO_ctrl(b, cmd, 0, &extmp);
    if (ret > 0) {
        *ex_func = extmp.ex_func;
        *ex_free_func = extmp.ex_free_func;
    }
    return ret;
}

int BIO_asn1_set_prefix(BIO *b, asn1_ps_func *prefix,
                        asn1_ps_func *prefix_free)
{
    return asn1_bio_set_ex(b, BIO_C_SET_PREFIX, prefix, prefix_free);
}

int BIO_asn1_get_prefix(BIO *b, asn1_ps_func **pprefix,
                        asn1_ps_func **pprefix_free)
{
    return asn1_bio_get_ex(b, BIO_C_GET_PREFIX, pprefix, pprefix_free);
}

int BIO_asn1_set_suffix(BIO *b, asn1_ps_func *suffix,
                        asn1_ps_func *suffix_free)
{
    return asn1_bio_set_ex(b, BIO_C_SET_SUFFIX, suffix, suffix_free);
}

int BIO_asn1_get_suffix(BIO *b, asn1_ps_func **psuffix,
                        asn1_ps_func **psuffix_free)
{
    return asn1_bio_get_ex(b, BIO_C_GET_SUFFIX, psuffix, psuffix_free);
}
