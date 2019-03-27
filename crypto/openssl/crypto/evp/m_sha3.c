/*
 * Copyright 2017-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/objects.h>
#include "internal/evp_int.h"
#include "evp_locl.h"

size_t SHA3_absorb(uint64_t A[5][5], const unsigned char *inp, size_t len,
                   size_t r);
void SHA3_squeeze(uint64_t A[5][5], unsigned char *out, size_t len, size_t r);

#define KECCAK1600_WIDTH 1600

typedef struct {
    uint64_t A[5][5];
    size_t block_size;          /* cached ctx->digest->block_size */
    size_t md_size;             /* output length, variable in XOF */
    size_t num;                 /* used bytes in below buffer */
    unsigned char buf[KECCAK1600_WIDTH / 8 - 32];
    unsigned char pad;
} KECCAK1600_CTX;

static int init(EVP_MD_CTX *evp_ctx, unsigned char pad)
{
    KECCAK1600_CTX *ctx = evp_ctx->md_data;
    size_t bsz = evp_ctx->digest->block_size;

    if (bsz <= sizeof(ctx->buf)) {
        memset(ctx->A, 0, sizeof(ctx->A));

        ctx->num = 0;
        ctx->block_size = bsz;
        ctx->md_size = evp_ctx->digest->md_size;
        ctx->pad = pad;

        return 1;
    }

    return 0;
}

static int sha3_init(EVP_MD_CTX *evp_ctx)
{
    return init(evp_ctx, '\x06');
}

static int shake_init(EVP_MD_CTX *evp_ctx)
{
    return init(evp_ctx, '\x1f');
}

static int sha3_update(EVP_MD_CTX *evp_ctx, const void *_inp, size_t len)
{
    KECCAK1600_CTX *ctx = evp_ctx->md_data;
    const unsigned char *inp = _inp;
    size_t bsz = ctx->block_size;
    size_t num, rem;

    if (len == 0)
        return 1;

    if ((num = ctx->num) != 0) {      /* process intermediate buffer? */
        rem = bsz - num;

        if (len < rem) {
            memcpy(ctx->buf + num, inp, len);
            ctx->num += len;
            return 1;
        }
        /*
         * We have enough data to fill or overflow the intermediate
         * buffer. So we append |rem| bytes and process the block,
         * leaving the rest for later processing...
         */
        memcpy(ctx->buf + num, inp, rem);
        inp += rem, len -= rem;
        (void)SHA3_absorb(ctx->A, ctx->buf, bsz, bsz);
        ctx->num = 0;
        /* ctx->buf is processed, ctx->num is guaranteed to be zero */
    }

    if (len >= bsz)
        rem = SHA3_absorb(ctx->A, inp, len, bsz);
    else
        rem = len;

    if (rem) {
        memcpy(ctx->buf, inp + len - rem, rem);
        ctx->num = rem;
    }

    return 1;
}

static int sha3_final(EVP_MD_CTX *evp_ctx, unsigned char *md)
{
    KECCAK1600_CTX *ctx = evp_ctx->md_data;
    size_t bsz = ctx->block_size;
    size_t num = ctx->num;

    /*
     * Pad the data with 10*1. Note that |num| can be |bsz - 1|
     * in which case both byte operations below are performed on
     * same byte...
     */
    memset(ctx->buf + num, 0, bsz - num);
    ctx->buf[num] = ctx->pad;
    ctx->buf[bsz - 1] |= 0x80;

    (void)SHA3_absorb(ctx->A, ctx->buf, bsz, bsz);

    SHA3_squeeze(ctx->A, md, ctx->md_size, bsz);

    return 1;
}

static int shake_ctrl(EVP_MD_CTX *evp_ctx, int cmd, int p1, void *p2)
{
    KECCAK1600_CTX *ctx = evp_ctx->md_data;

    switch (cmd) {
    case EVP_MD_CTRL_XOF_LEN:
        ctx->md_size = p1;
        return 1;
    default:
        return 0;
    }
}

#if defined(OPENSSL_CPUID_OBJ) && defined(__s390__) && defined(KECCAK1600_ASM)
/*
 * IBM S390X support
 */
# include "s390x_arch.h"

# define S390X_SHA3_FC(ctx)     ((ctx)->pad)

# define S390X_sha3_224_CAPABLE ((OPENSSL_s390xcap_P.kimd[0] &      \
                                  S390X_CAPBIT(S390X_SHA3_224)) &&  \
                                 (OPENSSL_s390xcap_P.klmd[0] &      \
                                  S390X_CAPBIT(S390X_SHA3_224)))
# define S390X_sha3_256_CAPABLE ((OPENSSL_s390xcap_P.kimd[0] &      \
                                  S390X_CAPBIT(S390X_SHA3_256)) &&  \
                                 (OPENSSL_s390xcap_P.klmd[0] &      \
                                  S390X_CAPBIT(S390X_SHA3_256)))
# define S390X_sha3_384_CAPABLE ((OPENSSL_s390xcap_P.kimd[0] &      \
                                  S390X_CAPBIT(S390X_SHA3_384)) &&  \
                                 (OPENSSL_s390xcap_P.klmd[0] &      \
                                  S390X_CAPBIT(S390X_SHA3_384)))
# define S390X_sha3_512_CAPABLE ((OPENSSL_s390xcap_P.kimd[0] &      \
                                  S390X_CAPBIT(S390X_SHA3_512)) &&  \
                                 (OPENSSL_s390xcap_P.klmd[0] &      \
                                  S390X_CAPBIT(S390X_SHA3_512)))
# define S390X_shake128_CAPABLE ((OPENSSL_s390xcap_P.kimd[0] &      \
                                  S390X_CAPBIT(S390X_SHAKE_128)) && \
                                 (OPENSSL_s390xcap_P.klmd[0] &      \
                                  S390X_CAPBIT(S390X_SHAKE_128)))
# define S390X_shake256_CAPABLE ((OPENSSL_s390xcap_P.kimd[0] &      \
                                  S390X_CAPBIT(S390X_SHAKE_256)) && \
                                 (OPENSSL_s390xcap_P.klmd[0] &      \
                                  S390X_CAPBIT(S390X_SHAKE_256)))

/* Convert md-size to block-size. */
# define S390X_KECCAK1600_BSZ(n) ((KECCAK1600_WIDTH - ((n) << 1)) >> 3)

static int s390x_sha3_init(EVP_MD_CTX *evp_ctx)
{
    KECCAK1600_CTX *ctx = evp_ctx->md_data;
    const size_t bsz = evp_ctx->digest->block_size;

    /*-
     * KECCAK1600_CTX structure's pad field is used to store the KIMD/KLMD
     * function code.
     */
    switch (bsz) {
    case S390X_KECCAK1600_BSZ(224):
        ctx->pad = S390X_SHA3_224;
        break;
    case S390X_KECCAK1600_BSZ(256):
        ctx->pad = S390X_SHA3_256;
        break;
    case S390X_KECCAK1600_BSZ(384):
        ctx->pad = S390X_SHA3_384;
        break;
    case S390X_KECCAK1600_BSZ(512):
        ctx->pad = S390X_SHA3_512;
        break;
    default:
        return 0;
    }

    memset(ctx->A, 0, sizeof(ctx->A));
    ctx->num = 0;
    ctx->block_size = bsz;
    ctx->md_size = evp_ctx->digest->md_size;
    return 1;
}

static int s390x_shake_init(EVP_MD_CTX *evp_ctx)
{
    KECCAK1600_CTX *ctx = evp_ctx->md_data;
    const size_t bsz = evp_ctx->digest->block_size;

    /*-
     * KECCAK1600_CTX structure's pad field is used to store the KIMD/KLMD
     * function code.
     */
    switch (bsz) {
    case S390X_KECCAK1600_BSZ(128):
        ctx->pad = S390X_SHAKE_128;
        break;
    case S390X_KECCAK1600_BSZ(256):
        ctx->pad = S390X_SHAKE_256;
        break;
    default:
        return 0;
    }

    memset(ctx->A, 0, sizeof(ctx->A));
    ctx->num = 0;
    ctx->block_size = bsz;
    ctx->md_size = evp_ctx->digest->md_size;
    return 1;
}

static int s390x_sha3_update(EVP_MD_CTX *evp_ctx, const void *_inp, size_t len)
{
    KECCAK1600_CTX *ctx = evp_ctx->md_data;
    const unsigned char *inp = _inp;
    const size_t bsz = ctx->block_size;
    size_t num, rem;

    if (len == 0)
        return 1;

    if ((num = ctx->num) != 0) {
        rem = bsz - num;

        if (len < rem) {
            memcpy(ctx->buf + num, inp, len);
            ctx->num += len;
            return 1;
        }
        memcpy(ctx->buf + num, inp, rem);
        inp += rem;
        len -= rem;
        s390x_kimd(ctx->buf, bsz, ctx->pad, ctx->A);
        ctx->num = 0;
    }
    rem = len % bsz;

    s390x_kimd(inp, len - rem, ctx->pad, ctx->A);

    if (rem) {
        memcpy(ctx->buf, inp + len - rem, rem);
        ctx->num = rem;
    }
    return 1;
}

static int s390x_sha3_final(EVP_MD_CTX *evp_ctx, unsigned char *md)
{
    KECCAK1600_CTX *ctx = evp_ctx->md_data;

    s390x_klmd(ctx->buf, ctx->num, NULL, 0, ctx->pad, ctx->A);
    memcpy(md, ctx->A, ctx->md_size);
    return 1;
}

static int s390x_shake_final(EVP_MD_CTX *evp_ctx, unsigned char *md)
{
    KECCAK1600_CTX *ctx = evp_ctx->md_data;

    s390x_klmd(ctx->buf, ctx->num, md, ctx->md_size, ctx->pad, ctx->A);
    return 1;
}

# define EVP_MD_SHA3(bitlen)                         \
const EVP_MD *EVP_sha3_##bitlen(void)                \
{                                                    \
    static const EVP_MD s390x_sha3_##bitlen##_md = { \
        NID_sha3_##bitlen,                           \
        NID_RSA_SHA3_##bitlen,                       \
        bitlen / 8,                                  \
        EVP_MD_FLAG_DIGALGID_ABSENT,                 \
        s390x_sha3_init,                             \
        s390x_sha3_update,                           \
        s390x_sha3_final,                            \
        NULL,                                        \
        NULL,                                        \
        (KECCAK1600_WIDTH - bitlen * 2) / 8,         \
        sizeof(KECCAK1600_CTX),                      \
    };                                               \
    static const EVP_MD sha3_##bitlen##_md = {       \
        NID_sha3_##bitlen,                           \
        NID_RSA_SHA3_##bitlen,                       \
        bitlen / 8,                                  \
        EVP_MD_FLAG_DIGALGID_ABSENT,                 \
        sha3_init,                                   \
        sha3_update,                                 \
        sha3_final,                                  \
        NULL,                                        \
        NULL,                                        \
        (KECCAK1600_WIDTH - bitlen * 2) / 8,         \
        sizeof(KECCAK1600_CTX),                      \
    };                                               \
    return S390X_sha3_##bitlen##_CAPABLE ?           \
           &s390x_sha3_##bitlen##_md :               \
           &sha3_##bitlen##_md;                      \
}

# define EVP_MD_SHAKE(bitlen)                        \
const EVP_MD *EVP_shake##bitlen(void)                \
{                                                    \
    static const EVP_MD s390x_shake##bitlen##_md = { \
        NID_shake##bitlen,                           \
        0,                                           \
        bitlen / 8,                                  \
        EVP_MD_FLAG_XOF,                             \
        s390x_shake_init,                            \
        s390x_sha3_update,                           \
        s390x_shake_final,                           \
        NULL,                                        \
        NULL,                                        \
        (KECCAK1600_WIDTH - bitlen * 2) / 8,         \
        sizeof(KECCAK1600_CTX),                      \
        shake_ctrl                                   \
    };                                               \
    static const EVP_MD shake##bitlen##_md = {       \
        NID_shake##bitlen,                           \
        0,                                           \
        bitlen / 8,                                  \
        EVP_MD_FLAG_XOF,                             \
        shake_init,                                  \
        sha3_update,                                 \
        sha3_final,                                  \
        NULL,                                        \
        NULL,                                        \
        (KECCAK1600_WIDTH - bitlen * 2) / 8,         \
        sizeof(KECCAK1600_CTX),                      \
        shake_ctrl                                   \
    };                                               \
    return S390X_shake##bitlen##_CAPABLE ?           \
           &s390x_shake##bitlen##_md :               \
           &shake##bitlen##_md;                      \
}

#else

# define EVP_MD_SHA3(bitlen)                    \
const EVP_MD *EVP_sha3_##bitlen(void)           \
{                                               \
    static const EVP_MD sha3_##bitlen##_md = {  \
        NID_sha3_##bitlen,                      \
        NID_RSA_SHA3_##bitlen,                  \
        bitlen / 8,                             \
        EVP_MD_FLAG_DIGALGID_ABSENT,            \
        sha3_init,                              \
        sha3_update,                            \
        sha3_final,                             \
        NULL,                                   \
        NULL,                                   \
        (KECCAK1600_WIDTH - bitlen * 2) / 8,    \
        sizeof(KECCAK1600_CTX),                 \
    };                                          \
    return &sha3_##bitlen##_md;                 \
}

# define EVP_MD_SHAKE(bitlen)                   \
const EVP_MD *EVP_shake##bitlen(void)           \
{                                               \
    static const EVP_MD shake##bitlen##_md = {  \
        NID_shake##bitlen,                      \
        0,                                      \
        bitlen / 8,                             \
        EVP_MD_FLAG_XOF,                        \
        shake_init,                             \
        sha3_update,                            \
        sha3_final,                             \
        NULL,                                   \
        NULL,                                   \
        (KECCAK1600_WIDTH - bitlen * 2) / 8,    \
        sizeof(KECCAK1600_CTX),                 \
        shake_ctrl                              \
    };                                          \
    return &shake##bitlen##_md;                 \
}
#endif

EVP_MD_SHA3(224)
EVP_MD_SHA3(256)
EVP_MD_SHA3(384)
EVP_MD_SHA3(512)

EVP_MD_SHAKE(128)
EVP_MD_SHAKE(256)
