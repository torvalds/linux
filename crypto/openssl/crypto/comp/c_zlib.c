/*
 * Copyright 1998-2016 The OpenSSL Project Authors. All Rights Reserved.
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
#include "internal/comp.h"
#include <openssl/err.h>
#include "internal/cryptlib_int.h"
#include "internal/bio.h"
#include "comp_lcl.h"

COMP_METHOD *COMP_zlib(void);

static COMP_METHOD zlib_method_nozlib = {
    NID_undef,
    "(undef)",
    NULL,
    NULL,
    NULL,
    NULL,
};

#ifndef ZLIB
# undef ZLIB_SHARED
#else

# include <zlib.h>

static int zlib_stateful_init(COMP_CTX *ctx);
static void zlib_stateful_finish(COMP_CTX *ctx);
static int zlib_stateful_compress_block(COMP_CTX *ctx, unsigned char *out,
                                        unsigned int olen, unsigned char *in,
                                        unsigned int ilen);
static int zlib_stateful_expand_block(COMP_CTX *ctx, unsigned char *out,
                                      unsigned int olen, unsigned char *in,
                                      unsigned int ilen);

/* memory allocations functions for zlib initialisation */
static void *zlib_zalloc(void *opaque, unsigned int no, unsigned int size)
{
    void *p;

    p = OPENSSL_zalloc(no * size);
    return p;
}

static void zlib_zfree(void *opaque, void *address)
{
    OPENSSL_free(address);
}


static COMP_METHOD zlib_stateful_method = {
    NID_zlib_compression,
    LN_zlib_compression,
    zlib_stateful_init,
    zlib_stateful_finish,
    zlib_stateful_compress_block,
    zlib_stateful_expand_block
};

/*
 * When OpenSSL is built on Windows, we do not want to require that
 * the ZLIB.DLL be available in order for the OpenSSL DLLs to
 * work.  Therefore, all ZLIB routines are loaded at run time
 * and we do not link to a .LIB file when ZLIB_SHARED is set.
 */
# if defined(OPENSSL_SYS_WINDOWS) || defined(OPENSSL_SYS_WIN32)
#  include <windows.h>
# endif                         /* !(OPENSSL_SYS_WINDOWS ||
                                 * OPENSSL_SYS_WIN32) */

# ifdef ZLIB_SHARED
#  include "internal/dso.h"

/* Function pointers */
typedef int (*compress_ft) (Bytef *dest, uLongf * destLen,
                            const Bytef *source, uLong sourceLen);
typedef int (*inflateEnd_ft) (z_streamp strm);
typedef int (*inflate_ft) (z_streamp strm, int flush);
typedef int (*inflateInit__ft) (z_streamp strm,
                                const char *version, int stream_size);
typedef int (*deflateEnd_ft) (z_streamp strm);
typedef int (*deflate_ft) (z_streamp strm, int flush);
typedef int (*deflateInit__ft) (z_streamp strm, int level,
                                const char *version, int stream_size);
typedef const char *(*zError__ft) (int err);
static compress_ft p_compress = NULL;
static inflateEnd_ft p_inflateEnd = NULL;
static inflate_ft p_inflate = NULL;
static inflateInit__ft p_inflateInit_ = NULL;
static deflateEnd_ft p_deflateEnd = NULL;
static deflate_ft p_deflate = NULL;
static deflateInit__ft p_deflateInit_ = NULL;
static zError__ft p_zError = NULL;

static int zlib_loaded = 0;     /* only attempt to init func pts once */
static DSO *zlib_dso = NULL;

#  define compress                p_compress
#  define inflateEnd              p_inflateEnd
#  define inflate                 p_inflate
#  define inflateInit_            p_inflateInit_
#  define deflateEnd              p_deflateEnd
#  define deflate                 p_deflate
#  define deflateInit_            p_deflateInit_
#  define zError                  p_zError
# endif                         /* ZLIB_SHARED */

struct zlib_state {
    z_stream istream;
    z_stream ostream;
};

static int zlib_stateful_init(COMP_CTX *ctx)
{
    int err;
    struct zlib_state *state = OPENSSL_zalloc(sizeof(*state));

    if (state == NULL)
        goto err;

    state->istream.zalloc = zlib_zalloc;
    state->istream.zfree = zlib_zfree;
    state->istream.opaque = Z_NULL;
    state->istream.next_in = Z_NULL;
    state->istream.next_out = Z_NULL;
    err = inflateInit_(&state->istream, ZLIB_VERSION, sizeof(z_stream));
    if (err != Z_OK)
        goto err;

    state->ostream.zalloc = zlib_zalloc;
    state->ostream.zfree = zlib_zfree;
    state->ostream.opaque = Z_NULL;
    state->ostream.next_in = Z_NULL;
    state->ostream.next_out = Z_NULL;
    err = deflateInit_(&state->ostream, Z_DEFAULT_COMPRESSION,
                       ZLIB_VERSION, sizeof(z_stream));
    if (err != Z_OK)
        goto err;

    ctx->data = state;
    return 1;
 err:
    OPENSSL_free(state);
    return 0;
}

static void zlib_stateful_finish(COMP_CTX *ctx)
{
    struct zlib_state *state = ctx->data;
    inflateEnd(&state->istream);
    deflateEnd(&state->ostream);
    OPENSSL_free(state);
}

static int zlib_stateful_compress_block(COMP_CTX *ctx, unsigned char *out,
                                        unsigned int olen, unsigned char *in,
                                        unsigned int ilen)
{
    int err = Z_OK;
    struct zlib_state *state = ctx->data;

    if (state == NULL)
        return -1;

    state->ostream.next_in = in;
    state->ostream.avail_in = ilen;
    state->ostream.next_out = out;
    state->ostream.avail_out = olen;
    if (ilen > 0)
        err = deflate(&state->ostream, Z_SYNC_FLUSH);
    if (err != Z_OK)
        return -1;
    return olen - state->ostream.avail_out;
}

static int zlib_stateful_expand_block(COMP_CTX *ctx, unsigned char *out,
                                      unsigned int olen, unsigned char *in,
                                      unsigned int ilen)
{
    int err = Z_OK;
    struct zlib_state *state = ctx->data;

    if (state == NULL)
        return 0;

    state->istream.next_in = in;
    state->istream.avail_in = ilen;
    state->istream.next_out = out;
    state->istream.avail_out = olen;
    if (ilen > 0)
        err = inflate(&state->istream, Z_SYNC_FLUSH);
    if (err != Z_OK)
        return -1;
    return olen - state->istream.avail_out;
}

#endif

COMP_METHOD *COMP_zlib(void)
{
    COMP_METHOD *meth = &zlib_method_nozlib;

#ifdef ZLIB_SHARED
    /* LIBZ may be externally defined, and we should respect that value */
# ifndef LIBZ
#  if defined(OPENSSL_SYS_WINDOWS) || defined(OPENSSL_SYS_WIN32)
#   define LIBZ "ZLIB1"
#  elif defined(OPENSSL_SYS_VMS)
#   define LIBZ "LIBZ"
#  else
#   define LIBZ "z"
#  endif
# endif

    if (!zlib_loaded) {
        zlib_dso = DSO_load(NULL, LIBZ, NULL, 0);
        if (zlib_dso != NULL) {
            p_compress = (compress_ft) DSO_bind_func(zlib_dso, "compress");
            p_inflateEnd
                = (inflateEnd_ft) DSO_bind_func(zlib_dso, "inflateEnd");
            p_inflate = (inflate_ft) DSO_bind_func(zlib_dso, "inflate");
            p_inflateInit_
                = (inflateInit__ft) DSO_bind_func(zlib_dso, "inflateInit_");
            p_deflateEnd
                = (deflateEnd_ft) DSO_bind_func(zlib_dso, "deflateEnd");
            p_deflate = (deflate_ft) DSO_bind_func(zlib_dso, "deflate");
            p_deflateInit_
                = (deflateInit__ft) DSO_bind_func(zlib_dso, "deflateInit_");
            p_zError = (zError__ft) DSO_bind_func(zlib_dso, "zError");

            if (p_compress && p_inflateEnd && p_inflate
                && p_inflateInit_ && p_deflateEnd
                && p_deflate && p_deflateInit_ && p_zError)
                zlib_loaded++;

            if (!OPENSSL_init_crypto(OPENSSL_INIT_ZLIB, NULL)) {
                comp_zlib_cleanup_int();
                return meth;
            }
            if (zlib_loaded)
                meth = &zlib_stateful_method;
        }
    }
#endif
#if defined(ZLIB)
    meth = &zlib_stateful_method;
#endif

    return meth;
}

void comp_zlib_cleanup_int(void)
{
#ifdef ZLIB_SHARED
    DSO_free(zlib_dso);
    zlib_dso = NULL;
#endif
}

#ifdef ZLIB

/* Zlib based compression/decompression filter BIO */

typedef struct {
    unsigned char *ibuf;        /* Input buffer */
    int ibufsize;               /* Buffer size */
    z_stream zin;               /* Input decompress context */
    unsigned char *obuf;        /* Output buffer */
    int obufsize;               /* Output buffer size */
    unsigned char *optr;        /* Position in output buffer */
    int ocount;                 /* Amount of data in output buffer */
    int odone;                  /* deflate EOF */
    int comp_level;             /* Compression level to use */
    z_stream zout;              /* Output compression context */
} BIO_ZLIB_CTX;

# define ZLIB_DEFAULT_BUFSIZE 1024

static int bio_zlib_new(BIO *bi);
static int bio_zlib_free(BIO *bi);
static int bio_zlib_read(BIO *b, char *out, int outl);
static int bio_zlib_write(BIO *b, const char *in, int inl);
static long bio_zlib_ctrl(BIO *b, int cmd, long num, void *ptr);
static long bio_zlib_callback_ctrl(BIO *b, int cmd, BIO_info_cb *fp);

static const BIO_METHOD bio_meth_zlib = {
    BIO_TYPE_COMP,
    "zlib",
    /* TODO: Convert to new style write function */
    bwrite_conv,
    bio_zlib_write,
    /* TODO: Convert to new style read function */
    bread_conv,
    bio_zlib_read,
    NULL,                      /* bio_zlib_puts, */
    NULL,                      /* bio_zlib_gets, */
    bio_zlib_ctrl,
    bio_zlib_new,
    bio_zlib_free,
    bio_zlib_callback_ctrl
};

const BIO_METHOD *BIO_f_zlib(void)
{
    return &bio_meth_zlib;
}

static int bio_zlib_new(BIO *bi)
{
    BIO_ZLIB_CTX *ctx;
# ifdef ZLIB_SHARED
    (void)COMP_zlib();
    if (!zlib_loaded) {
        COMPerr(COMP_F_BIO_ZLIB_NEW, COMP_R_ZLIB_NOT_SUPPORTED);
        return 0;
    }
# endif
    ctx = OPENSSL_zalloc(sizeof(*ctx));
    if (ctx == NULL) {
        COMPerr(COMP_F_BIO_ZLIB_NEW, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    ctx->ibufsize = ZLIB_DEFAULT_BUFSIZE;
    ctx->obufsize = ZLIB_DEFAULT_BUFSIZE;
    ctx->zin.zalloc = Z_NULL;
    ctx->zin.zfree = Z_NULL;
    ctx->zout.zalloc = Z_NULL;
    ctx->zout.zfree = Z_NULL;
    ctx->comp_level = Z_DEFAULT_COMPRESSION;
    BIO_set_init(bi, 1);
    BIO_set_data(bi, ctx);

    return 1;
}

static int bio_zlib_free(BIO *bi)
{
    BIO_ZLIB_CTX *ctx;
    if (!bi)
        return 0;
    ctx = BIO_get_data(bi);
    if (ctx->ibuf) {
        /* Destroy decompress context */
        inflateEnd(&ctx->zin);
        OPENSSL_free(ctx->ibuf);
    }
    if (ctx->obuf) {
        /* Destroy compress context */
        deflateEnd(&ctx->zout);
        OPENSSL_free(ctx->obuf);
    }
    OPENSSL_free(ctx);
    BIO_set_data(bi, NULL);
    BIO_set_init(bi, 0);

    return 1;
}

static int bio_zlib_read(BIO *b, char *out, int outl)
{
    BIO_ZLIB_CTX *ctx;
    int ret;
    z_stream *zin;
    BIO *next = BIO_next(b);

    if (!out || !outl)
        return 0;
    ctx = BIO_get_data(b);
    zin = &ctx->zin;
    BIO_clear_retry_flags(b);
    if (!ctx->ibuf) {
        ctx->ibuf = OPENSSL_malloc(ctx->ibufsize);
        if (ctx->ibuf == NULL) {
            COMPerr(COMP_F_BIO_ZLIB_READ, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        inflateInit(zin);
        zin->next_in = ctx->ibuf;
        zin->avail_in = 0;
    }

    /* Copy output data directly to supplied buffer */
    zin->next_out = (unsigned char *)out;
    zin->avail_out = (unsigned int)outl;
    for (;;) {
        /* Decompress while data available */
        while (zin->avail_in) {
            ret = inflate(zin, 0);
            if ((ret != Z_OK) && (ret != Z_STREAM_END)) {
                COMPerr(COMP_F_BIO_ZLIB_READ, COMP_R_ZLIB_INFLATE_ERROR);
                ERR_add_error_data(2, "zlib error:", zError(ret));
                return 0;
            }
            /* If EOF or we've read everything then return */
            if ((ret == Z_STREAM_END) || !zin->avail_out)
                return outl - zin->avail_out;
        }

        /*
         * No data in input buffer try to read some in, if an error then
         * return the total data read.
         */
        ret = BIO_read(next, ctx->ibuf, ctx->ibufsize);
        if (ret <= 0) {
            /* Total data read */
            int tot = outl - zin->avail_out;
            BIO_copy_next_retry(b);
            if (ret < 0)
                return (tot > 0) ? tot : ret;
            return tot;
        }
        zin->avail_in = ret;
        zin->next_in = ctx->ibuf;
    }
}

static int bio_zlib_write(BIO *b, const char *in, int inl)
{
    BIO_ZLIB_CTX *ctx;
    int ret;
    z_stream *zout;
    BIO *next = BIO_next(b);

    if (!in || !inl)
        return 0;
    ctx = BIO_get_data(b);
    if (ctx->odone)
        return 0;
    zout = &ctx->zout;
    BIO_clear_retry_flags(b);
    if (!ctx->obuf) {
        ctx->obuf = OPENSSL_malloc(ctx->obufsize);
        /* Need error here */
        if (ctx->obuf == NULL) {
            COMPerr(COMP_F_BIO_ZLIB_WRITE, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        ctx->optr = ctx->obuf;
        ctx->ocount = 0;
        deflateInit(zout, ctx->comp_level);
        zout->next_out = ctx->obuf;
        zout->avail_out = ctx->obufsize;
    }
    /* Obtain input data directly from supplied buffer */
    zout->next_in = (void *)in;
    zout->avail_in = inl;
    for (;;) {
        /* If data in output buffer write it first */
        while (ctx->ocount) {
            ret = BIO_write(next, ctx->optr, ctx->ocount);
            if (ret <= 0) {
                /* Total data written */
                int tot = inl - zout->avail_in;
                BIO_copy_next_retry(b);
                if (ret < 0)
                    return (tot > 0) ? tot : ret;
                return tot;
            }
            ctx->optr += ret;
            ctx->ocount -= ret;
        }

        /* Have we consumed all supplied data? */
        if (!zout->avail_in)
            return inl;

        /* Compress some more */

        /* Reset buffer */
        ctx->optr = ctx->obuf;
        zout->next_out = ctx->obuf;
        zout->avail_out = ctx->obufsize;
        /* Compress some more */
        ret = deflate(zout, 0);
        if (ret != Z_OK) {
            COMPerr(COMP_F_BIO_ZLIB_WRITE, COMP_R_ZLIB_DEFLATE_ERROR);
            ERR_add_error_data(2, "zlib error:", zError(ret));
            return 0;
        }
        ctx->ocount = ctx->obufsize - zout->avail_out;
    }
}

static int bio_zlib_flush(BIO *b)
{
    BIO_ZLIB_CTX *ctx;
    int ret;
    z_stream *zout;
    BIO *next = BIO_next(b);

    ctx = BIO_get_data(b);
    /* If no data written or already flush show success */
    if (!ctx->obuf || (ctx->odone && !ctx->ocount))
        return 1;
    zout = &ctx->zout;
    BIO_clear_retry_flags(b);
    /* No more input data */
    zout->next_in = NULL;
    zout->avail_in = 0;
    for (;;) {
        /* If data in output buffer write it first */
        while (ctx->ocount) {
            ret = BIO_write(next, ctx->optr, ctx->ocount);
            if (ret <= 0) {
                BIO_copy_next_retry(b);
                return ret;
            }
            ctx->optr += ret;
            ctx->ocount -= ret;
        }
        if (ctx->odone)
            return 1;

        /* Compress some more */

        /* Reset buffer */
        ctx->optr = ctx->obuf;
        zout->next_out = ctx->obuf;
        zout->avail_out = ctx->obufsize;
        /* Compress some more */
        ret = deflate(zout, Z_FINISH);
        if (ret == Z_STREAM_END)
            ctx->odone = 1;
        else if (ret != Z_OK) {
            COMPerr(COMP_F_BIO_ZLIB_FLUSH, COMP_R_ZLIB_DEFLATE_ERROR);
            ERR_add_error_data(2, "zlib error:", zError(ret));
            return 0;
        }
        ctx->ocount = ctx->obufsize - zout->avail_out;
    }
}

static long bio_zlib_ctrl(BIO *b, int cmd, long num, void *ptr)
{
    BIO_ZLIB_CTX *ctx;
    int ret, *ip;
    int ibs, obs;
    BIO *next = BIO_next(b);

    if (next == NULL)
        return 0;
    ctx = BIO_get_data(b);
    switch (cmd) {

    case BIO_CTRL_RESET:
        ctx->ocount = 0;
        ctx->odone = 0;
        ret = 1;
        break;

    case BIO_CTRL_FLUSH:
        ret = bio_zlib_flush(b);
        if (ret > 0)
            ret = BIO_flush(next);
        break;

    case BIO_C_SET_BUFF_SIZE:
        ibs = -1;
        obs = -1;
        if (ptr != NULL) {
            ip = ptr;
            if (*ip == 0)
                ibs = (int)num;
            else
                obs = (int)num;
        } else {
            ibs = (int)num;
            obs = ibs;
        }

        if (ibs != -1) {
            OPENSSL_free(ctx->ibuf);
            ctx->ibuf = NULL;
            ctx->ibufsize = ibs;
        }

        if (obs != -1) {
            OPENSSL_free(ctx->obuf);
            ctx->obuf = NULL;
            ctx->obufsize = obs;
        }
        ret = 1;
        break;

    case BIO_C_DO_STATE_MACHINE:
        BIO_clear_retry_flags(b);
        ret = BIO_ctrl(next, cmd, num, ptr);
        BIO_copy_next_retry(b);
        break;

    default:
        ret = BIO_ctrl(next, cmd, num, ptr);
        break;

    }

    return ret;
}

static long bio_zlib_callback_ctrl(BIO *b, int cmd, BIO_info_cb *fp)
{
    BIO *next = BIO_next(b);
    if (next == NULL)
        return 0;
    return BIO_callback_ctrl(next, cmd, fp);
}

#endif
