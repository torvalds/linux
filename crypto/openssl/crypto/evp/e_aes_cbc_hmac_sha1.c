/*
 * Copyright 2011-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/opensslconf.h>

#include <stdio.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/aes.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include "modes_lcl.h"
#include "internal/evp_int.h"
#include "internal/constant_time_locl.h"

typedef struct {
    AES_KEY ks;
    SHA_CTX head, tail, md;
    size_t payload_length;      /* AAD length in decrypt case */
    union {
        unsigned int tls_ver;
        unsigned char tls_aad[16]; /* 13 used */
    } aux;
} EVP_AES_HMAC_SHA1;

#define NO_PAYLOAD_LENGTH       ((size_t)-1)

#if     defined(AES_ASM) &&     ( \
        defined(__x86_64)       || defined(__x86_64__)  || \
        defined(_M_AMD64)       || defined(_M_X64)      )

extern unsigned int OPENSSL_ia32cap_P[];
# define AESNI_CAPABLE   (1<<(57-32))

int aesni_set_encrypt_key(const unsigned char *userKey, int bits,
                          AES_KEY *key);
int aesni_set_decrypt_key(const unsigned char *userKey, int bits,
                          AES_KEY *key);

void aesni_cbc_encrypt(const unsigned char *in,
                       unsigned char *out,
                       size_t length,
                       const AES_KEY *key, unsigned char *ivec, int enc);

void aesni_cbc_sha1_enc(const void *inp, void *out, size_t blocks,
                        const AES_KEY *key, unsigned char iv[16],
                        SHA_CTX *ctx, const void *in0);

void aesni256_cbc_sha1_dec(const void *inp, void *out, size_t blocks,
                           const AES_KEY *key, unsigned char iv[16],
                           SHA_CTX *ctx, const void *in0);

# define data(ctx) ((EVP_AES_HMAC_SHA1 *)EVP_CIPHER_CTX_get_cipher_data(ctx))

static int aesni_cbc_hmac_sha1_init_key(EVP_CIPHER_CTX *ctx,
                                        const unsigned char *inkey,
                                        const unsigned char *iv, int enc)
{
    EVP_AES_HMAC_SHA1 *key = data(ctx);
    int ret;

    if (enc)
        ret = aesni_set_encrypt_key(inkey,
                                    EVP_CIPHER_CTX_key_length(ctx) * 8,
                                    &key->ks);
    else
        ret = aesni_set_decrypt_key(inkey,
                                    EVP_CIPHER_CTX_key_length(ctx) * 8,
                                    &key->ks);

    SHA1_Init(&key->head);      /* handy when benchmarking */
    key->tail = key->head;
    key->md = key->head;

    key->payload_length = NO_PAYLOAD_LENGTH;

    return ret < 0 ? 0 : 1;
}

# define STITCHED_CALL
# undef  STITCHED_DECRYPT_CALL

# if !defined(STITCHED_CALL)
#  define aes_off 0
# endif

void sha1_block_data_order(void *c, const void *p, size_t len);

static void sha1_update(SHA_CTX *c, const void *data, size_t len)
{
    const unsigned char *ptr = data;
    size_t res;

    if ((res = c->num)) {
        res = SHA_CBLOCK - res;
        if (len < res)
            res = len;
        SHA1_Update(c, ptr, res);
        ptr += res;
        len -= res;
    }

    res = len % SHA_CBLOCK;
    len -= res;

    if (len) {
        sha1_block_data_order(c, ptr, len / SHA_CBLOCK);

        ptr += len;
        c->Nh += len >> 29;
        c->Nl += len <<= 3;
        if (c->Nl < (unsigned int)len)
            c->Nh++;
    }

    if (res)
        SHA1_Update(c, ptr, res);
}

# ifdef SHA1_Update
#  undef SHA1_Update
# endif
# define SHA1_Update sha1_update

# if !defined(OPENSSL_NO_MULTIBLOCK)

typedef struct {
    unsigned int A[8], B[8], C[8], D[8], E[8];
} SHA1_MB_CTX;
typedef struct {
    const unsigned char *ptr;
    int blocks;
} HASH_DESC;

void sha1_multi_block(SHA1_MB_CTX *, const HASH_DESC *, int);

typedef struct {
    const unsigned char *inp;
    unsigned char *out;
    int blocks;
    u64 iv[2];
} CIPH_DESC;

void aesni_multi_cbc_encrypt(CIPH_DESC *, void *, int);

static size_t tls1_1_multi_block_encrypt(EVP_AES_HMAC_SHA1 *key,
                                         unsigned char *out,
                                         const unsigned char *inp,
                                         size_t inp_len, int n4x)
{                               /* n4x is 1 or 2 */
    HASH_DESC hash_d[8], edges[8];
    CIPH_DESC ciph_d[8];
    unsigned char storage[sizeof(SHA1_MB_CTX) + 32];
    union {
        u64 q[16];
        u32 d[32];
        u8 c[128];
    } blocks[8];
    SHA1_MB_CTX *ctx;
    unsigned int frag, last, packlen, i, x4 = 4 * n4x, minblocks, processed =
        0;
    size_t ret = 0;
    u8 *IVs;
#  if defined(BSWAP8)
    u64 seqnum;
#  endif

    /* ask for IVs in bulk */
    if (RAND_bytes((IVs = blocks[0].c), 16 * x4) <= 0)
        return 0;

    ctx = (SHA1_MB_CTX *) (storage + 32 - ((size_t)storage % 32)); /* align */

    frag = (unsigned int)inp_len >> (1 + n4x);
    last = (unsigned int)inp_len + frag - (frag << (1 + n4x));
    if (last > frag && ((last + 13 + 9) % 64) < (x4 - 1)) {
        frag++;
        last -= x4 - 1;
    }

    packlen = 5 + 16 + ((frag + 20 + 16) & -16);

    /* populate descriptors with pointers and IVs */
    hash_d[0].ptr = inp;
    ciph_d[0].inp = inp;
    /* 5+16 is place for header and explicit IV */
    ciph_d[0].out = out + 5 + 16;
    memcpy(ciph_d[0].out - 16, IVs, 16);
    memcpy(ciph_d[0].iv, IVs, 16);
    IVs += 16;

    for (i = 1; i < x4; i++) {
        ciph_d[i].inp = hash_d[i].ptr = hash_d[i - 1].ptr + frag;
        ciph_d[i].out = ciph_d[i - 1].out + packlen;
        memcpy(ciph_d[i].out - 16, IVs, 16);
        memcpy(ciph_d[i].iv, IVs, 16);
        IVs += 16;
    }

#  if defined(BSWAP8)
    memcpy(blocks[0].c, key->md.data, 8);
    seqnum = BSWAP8(blocks[0].q[0]);
#  endif
    for (i = 0; i < x4; i++) {
        unsigned int len = (i == (x4 - 1) ? last : frag);
#  if !defined(BSWAP8)
        unsigned int carry, j;
#  endif

        ctx->A[i] = key->md.h0;
        ctx->B[i] = key->md.h1;
        ctx->C[i] = key->md.h2;
        ctx->D[i] = key->md.h3;
        ctx->E[i] = key->md.h4;

        /* fix seqnum */
#  if defined(BSWAP8)
        blocks[i].q[0] = BSWAP8(seqnum + i);
#  else
        for (carry = i, j = 8; j--;) {
            blocks[i].c[j] = ((u8 *)key->md.data)[j] + carry;
            carry = (blocks[i].c[j] - carry) >> (sizeof(carry) * 8 - 1);
        }
#  endif
        blocks[i].c[8] = ((u8 *)key->md.data)[8];
        blocks[i].c[9] = ((u8 *)key->md.data)[9];
        blocks[i].c[10] = ((u8 *)key->md.data)[10];
        /* fix length */
        blocks[i].c[11] = (u8)(len >> 8);
        blocks[i].c[12] = (u8)(len);

        memcpy(blocks[i].c + 13, hash_d[i].ptr, 64 - 13);
        hash_d[i].ptr += 64 - 13;
        hash_d[i].blocks = (len - (64 - 13)) / 64;

        edges[i].ptr = blocks[i].c;
        edges[i].blocks = 1;
    }

    /* hash 13-byte headers and first 64-13 bytes of inputs */
    sha1_multi_block(ctx, edges, n4x);
    /* hash bulk inputs */
#  define MAXCHUNKSIZE    2048
#  if     MAXCHUNKSIZE%64
#   error  "MAXCHUNKSIZE is not divisible by 64"
#  elif   MAXCHUNKSIZE
    /*
     * goal is to minimize pressure on L1 cache by moving in shorter steps,
     * so that hashed data is still in the cache by the time we encrypt it
     */
    minblocks = ((frag <= last ? frag : last) - (64 - 13)) / 64;
    if (minblocks > MAXCHUNKSIZE / 64) {
        for (i = 0; i < x4; i++) {
            edges[i].ptr = hash_d[i].ptr;
            edges[i].blocks = MAXCHUNKSIZE / 64;
            ciph_d[i].blocks = MAXCHUNKSIZE / 16;
        }
        do {
            sha1_multi_block(ctx, edges, n4x);
            aesni_multi_cbc_encrypt(ciph_d, &key->ks, n4x);

            for (i = 0; i < x4; i++) {
                edges[i].ptr = hash_d[i].ptr += MAXCHUNKSIZE;
                hash_d[i].blocks -= MAXCHUNKSIZE / 64;
                edges[i].blocks = MAXCHUNKSIZE / 64;
                ciph_d[i].inp += MAXCHUNKSIZE;
                ciph_d[i].out += MAXCHUNKSIZE;
                ciph_d[i].blocks = MAXCHUNKSIZE / 16;
                memcpy(ciph_d[i].iv, ciph_d[i].out - 16, 16);
            }
            processed += MAXCHUNKSIZE;
            minblocks -= MAXCHUNKSIZE / 64;
        } while (minblocks > MAXCHUNKSIZE / 64);
    }
#  endif
#  undef  MAXCHUNKSIZE
    sha1_multi_block(ctx, hash_d, n4x);

    memset(blocks, 0, sizeof(blocks));
    for (i = 0; i < x4; i++) {
        unsigned int len = (i == (x4 - 1) ? last : frag),
            off = hash_d[i].blocks * 64;
        const unsigned char *ptr = hash_d[i].ptr + off;

        off = (len - processed) - (64 - 13) - off; /* remainder actually */
        memcpy(blocks[i].c, ptr, off);
        blocks[i].c[off] = 0x80;
        len += 64 + 13;         /* 64 is HMAC header */
        len *= 8;               /* convert to bits */
        if (off < (64 - 8)) {
#  ifdef BSWAP4
            blocks[i].d[15] = BSWAP4(len);
#  else
            PUTU32(blocks[i].c + 60, len);
#  endif
            edges[i].blocks = 1;
        } else {
#  ifdef BSWAP4
            blocks[i].d[31] = BSWAP4(len);
#  else
            PUTU32(blocks[i].c + 124, len);
#  endif
            edges[i].blocks = 2;
        }
        edges[i].ptr = blocks[i].c;
    }

    /* hash input tails and finalize */
    sha1_multi_block(ctx, edges, n4x);

    memset(blocks, 0, sizeof(blocks));
    for (i = 0; i < x4; i++) {
#  ifdef BSWAP4
        blocks[i].d[0] = BSWAP4(ctx->A[i]);
        ctx->A[i] = key->tail.h0;
        blocks[i].d[1] = BSWAP4(ctx->B[i]);
        ctx->B[i] = key->tail.h1;
        blocks[i].d[2] = BSWAP4(ctx->C[i]);
        ctx->C[i] = key->tail.h2;
        blocks[i].d[3] = BSWAP4(ctx->D[i]);
        ctx->D[i] = key->tail.h3;
        blocks[i].d[4] = BSWAP4(ctx->E[i]);
        ctx->E[i] = key->tail.h4;
        blocks[i].c[20] = 0x80;
        blocks[i].d[15] = BSWAP4((64 + 20) * 8);
#  else
        PUTU32(blocks[i].c + 0, ctx->A[i]);
        ctx->A[i] = key->tail.h0;
        PUTU32(blocks[i].c + 4, ctx->B[i]);
        ctx->B[i] = key->tail.h1;
        PUTU32(blocks[i].c + 8, ctx->C[i]);
        ctx->C[i] = key->tail.h2;
        PUTU32(blocks[i].c + 12, ctx->D[i]);
        ctx->D[i] = key->tail.h3;
        PUTU32(blocks[i].c + 16, ctx->E[i]);
        ctx->E[i] = key->tail.h4;
        blocks[i].c[20] = 0x80;
        PUTU32(blocks[i].c + 60, (64 + 20) * 8);
#  endif
        edges[i].ptr = blocks[i].c;
        edges[i].blocks = 1;
    }

    /* finalize MACs */
    sha1_multi_block(ctx, edges, n4x);

    for (i = 0; i < x4; i++) {
        unsigned int len = (i == (x4 - 1) ? last : frag), pad, j;
        unsigned char *out0 = out;

        memcpy(ciph_d[i].out, ciph_d[i].inp, len - processed);
        ciph_d[i].inp = ciph_d[i].out;

        out += 5 + 16 + len;

        /* write MAC */
        PUTU32(out + 0, ctx->A[i]);
        PUTU32(out + 4, ctx->B[i]);
        PUTU32(out + 8, ctx->C[i]);
        PUTU32(out + 12, ctx->D[i]);
        PUTU32(out + 16, ctx->E[i]);
        out += 20;
        len += 20;

        /* pad */
        pad = 15 - len % 16;
        for (j = 0; j <= pad; j++)
            *(out++) = pad;
        len += pad + 1;

        ciph_d[i].blocks = (len - processed) / 16;
        len += 16;              /* account for explicit iv */

        /* arrange header */
        out0[0] = ((u8 *)key->md.data)[8];
        out0[1] = ((u8 *)key->md.data)[9];
        out0[2] = ((u8 *)key->md.data)[10];
        out0[3] = (u8)(len >> 8);
        out0[4] = (u8)(len);

        ret += len + 5;
        inp += frag;
    }

    aesni_multi_cbc_encrypt(ciph_d, &key->ks, n4x);

    OPENSSL_cleanse(blocks, sizeof(blocks));
    OPENSSL_cleanse(ctx, sizeof(*ctx));

    return ret;
}
# endif

static int aesni_cbc_hmac_sha1_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
                                      const unsigned char *in, size_t len)
{
    EVP_AES_HMAC_SHA1 *key = data(ctx);
    unsigned int l;
    size_t plen = key->payload_length, iv = 0, /* explicit IV in TLS 1.1 and
                                                * later */
        sha_off = 0;
# if defined(STITCHED_CALL)
    size_t aes_off = 0, blocks;

    sha_off = SHA_CBLOCK - key->md.num;
# endif

    key->payload_length = NO_PAYLOAD_LENGTH;

    if (len % AES_BLOCK_SIZE)
        return 0;

    if (EVP_CIPHER_CTX_encrypting(ctx)) {
        if (plen == NO_PAYLOAD_LENGTH)
            plen = len;
        else if (len !=
                 ((plen + SHA_DIGEST_LENGTH +
                   AES_BLOCK_SIZE) & -AES_BLOCK_SIZE))
            return 0;
        else if (key->aux.tls_ver >= TLS1_1_VERSION)
            iv = AES_BLOCK_SIZE;

# if defined(STITCHED_CALL)
        if (plen > (sha_off + iv)
            && (blocks = (plen - (sha_off + iv)) / SHA_CBLOCK)) {
            SHA1_Update(&key->md, in + iv, sha_off);

            aesni_cbc_sha1_enc(in, out, blocks, &key->ks,
                               EVP_CIPHER_CTX_iv_noconst(ctx),
                               &key->md, in + iv + sha_off);
            blocks *= SHA_CBLOCK;
            aes_off += blocks;
            sha_off += blocks;
            key->md.Nh += blocks >> 29;
            key->md.Nl += blocks <<= 3;
            if (key->md.Nl < (unsigned int)blocks)
                key->md.Nh++;
        } else {
            sha_off = 0;
        }
# endif
        sha_off += iv;
        SHA1_Update(&key->md, in + sha_off, plen - sha_off);

        if (plen != len) {      /* "TLS" mode of operation */
            if (in != out)
                memcpy(out + aes_off, in + aes_off, plen - aes_off);

            /* calculate HMAC and append it to payload */
            SHA1_Final(out + plen, &key->md);
            key->md = key->tail;
            SHA1_Update(&key->md, out + plen, SHA_DIGEST_LENGTH);
            SHA1_Final(out + plen, &key->md);

            /* pad the payload|hmac */
            plen += SHA_DIGEST_LENGTH;
            for (l = len - plen - 1; plen < len; plen++)
                out[plen] = l;
            /* encrypt HMAC|padding at once */
            aesni_cbc_encrypt(out + aes_off, out + aes_off, len - aes_off,
                              &key->ks, EVP_CIPHER_CTX_iv_noconst(ctx), 1);
        } else {
            aesni_cbc_encrypt(in + aes_off, out + aes_off, len - aes_off,
                              &key->ks, EVP_CIPHER_CTX_iv_noconst(ctx), 1);
        }
    } else {
        union {
            unsigned int u[SHA_DIGEST_LENGTH / sizeof(unsigned int)];
            unsigned char c[32 + SHA_DIGEST_LENGTH];
        } mac, *pmac;

        /* arrange cache line alignment */
        pmac = (void *)(((size_t)mac.c + 31) & ((size_t)0 - 32));

        if (plen != NO_PAYLOAD_LENGTH) { /* "TLS" mode of operation */
            size_t inp_len, mask, j, i;
            unsigned int res, maxpad, pad, bitlen;
            int ret = 1;
            union {
                unsigned int u[SHA_LBLOCK];
                unsigned char c[SHA_CBLOCK];
            } *data = (void *)key->md.data;
# if defined(STITCHED_DECRYPT_CALL)
            unsigned char tail_iv[AES_BLOCK_SIZE];
            int stitch = 0;
# endif

            if ((key->aux.tls_aad[plen - 4] << 8 | key->aux.tls_aad[plen - 3])
                >= TLS1_1_VERSION) {
                if (len < (AES_BLOCK_SIZE + SHA_DIGEST_LENGTH + 1))
                    return 0;

                /* omit explicit iv */
                memcpy(EVP_CIPHER_CTX_iv_noconst(ctx), in, AES_BLOCK_SIZE);

                in += AES_BLOCK_SIZE;
                out += AES_BLOCK_SIZE;
                len -= AES_BLOCK_SIZE;
            } else if (len < (SHA_DIGEST_LENGTH + 1))
                return 0;

# if defined(STITCHED_DECRYPT_CALL)
            if (len >= 1024 && ctx->key_len == 32) {
                /* decrypt last block */
                memcpy(tail_iv, in + len - 2 * AES_BLOCK_SIZE,
                       AES_BLOCK_SIZE);
                aesni_cbc_encrypt(in + len - AES_BLOCK_SIZE,
                                  out + len - AES_BLOCK_SIZE, AES_BLOCK_SIZE,
                                  &key->ks, tail_iv, 0);
                stitch = 1;
            } else
# endif
                /* decrypt HMAC|padding at once */
                aesni_cbc_encrypt(in, out, len, &key->ks,
                                  EVP_CIPHER_CTX_iv_noconst(ctx), 0);

            /* figure out payload length */
            pad = out[len - 1];
            maxpad = len - (SHA_DIGEST_LENGTH + 1);
            maxpad |= (255 - maxpad) >> (sizeof(maxpad) * 8 - 8);
            maxpad &= 255;

            mask = constant_time_ge(maxpad, pad);
            ret &= mask;
            /*
             * If pad is invalid then we will fail the above test but we must
             * continue anyway because we are in constant time code. However,
             * we'll use the maxpad value instead of the supplied pad to make
             * sure we perform well defined pointer arithmetic.
             */
            pad = constant_time_select(mask, pad, maxpad);

            inp_len = len - (SHA_DIGEST_LENGTH + pad + 1);

            key->aux.tls_aad[plen - 2] = inp_len >> 8;
            key->aux.tls_aad[plen - 1] = inp_len;

            /* calculate HMAC */
            key->md = key->head;
            SHA1_Update(&key->md, key->aux.tls_aad, plen);

# if defined(STITCHED_DECRYPT_CALL)
            if (stitch) {
                blocks = (len - (256 + 32 + SHA_CBLOCK)) / SHA_CBLOCK;
                aes_off = len - AES_BLOCK_SIZE - blocks * SHA_CBLOCK;
                sha_off = SHA_CBLOCK - plen;

                aesni_cbc_encrypt(in, out, aes_off, &key->ks, ctx->iv, 0);

                SHA1_Update(&key->md, out, sha_off);
                aesni256_cbc_sha1_dec(in + aes_off,
                                      out + aes_off, blocks, &key->ks,
                                      ctx->iv, &key->md, out + sha_off);

                sha_off += blocks *= SHA_CBLOCK;
                out += sha_off;
                len -= sha_off;
                inp_len -= sha_off;

                key->md.Nl += (blocks << 3); /* at most 18 bits */
                memcpy(ctx->iv, tail_iv, AES_BLOCK_SIZE);
            }
# endif

# if 1      /* see original reference version in #else */
            len -= SHA_DIGEST_LENGTH; /* amend mac */
            if (len >= (256 + SHA_CBLOCK)) {
                j = (len - (256 + SHA_CBLOCK)) & (0 - SHA_CBLOCK);
                j += SHA_CBLOCK - key->md.num;
                SHA1_Update(&key->md, out, j);
                out += j;
                len -= j;
                inp_len -= j;
            }

            /* but pretend as if we hashed padded payload */
            bitlen = key->md.Nl + (inp_len << 3); /* at most 18 bits */
#  ifdef BSWAP4
            bitlen = BSWAP4(bitlen);
#  else
            mac.c[0] = 0;
            mac.c[1] = (unsigned char)(bitlen >> 16);
            mac.c[2] = (unsigned char)(bitlen >> 8);
            mac.c[3] = (unsigned char)bitlen;
            bitlen = mac.u[0];
#  endif

            pmac->u[0] = 0;
            pmac->u[1] = 0;
            pmac->u[2] = 0;
            pmac->u[3] = 0;
            pmac->u[4] = 0;

            for (res = key->md.num, j = 0; j < len; j++) {
                size_t c = out[j];
                mask = (j - inp_len) >> (sizeof(j) * 8 - 8);
                c &= mask;
                c |= 0x80 & ~mask & ~((inp_len - j) >> (sizeof(j) * 8 - 8));
                data->c[res++] = (unsigned char)c;

                if (res != SHA_CBLOCK)
                    continue;

                /* j is not incremented yet */
                mask = 0 - ((inp_len + 7 - j) >> (sizeof(j) * 8 - 1));
                data->u[SHA_LBLOCK - 1] |= bitlen & mask;
                sha1_block_data_order(&key->md, data, 1);
                mask &= 0 - ((j - inp_len - 72) >> (sizeof(j) * 8 - 1));
                pmac->u[0] |= key->md.h0 & mask;
                pmac->u[1] |= key->md.h1 & mask;
                pmac->u[2] |= key->md.h2 & mask;
                pmac->u[3] |= key->md.h3 & mask;
                pmac->u[4] |= key->md.h4 & mask;
                res = 0;
            }

            for (i = res; i < SHA_CBLOCK; i++, j++)
                data->c[i] = 0;

            if (res > SHA_CBLOCK - 8) {
                mask = 0 - ((inp_len + 8 - j) >> (sizeof(j) * 8 - 1));
                data->u[SHA_LBLOCK - 1] |= bitlen & mask;
                sha1_block_data_order(&key->md, data, 1);
                mask &= 0 - ((j - inp_len - 73) >> (sizeof(j) * 8 - 1));
                pmac->u[0] |= key->md.h0 & mask;
                pmac->u[1] |= key->md.h1 & mask;
                pmac->u[2] |= key->md.h2 & mask;
                pmac->u[3] |= key->md.h3 & mask;
                pmac->u[4] |= key->md.h4 & mask;

                memset(data, 0, SHA_CBLOCK);
                j += 64;
            }
            data->u[SHA_LBLOCK - 1] = bitlen;
            sha1_block_data_order(&key->md, data, 1);
            mask = 0 - ((j - inp_len - 73) >> (sizeof(j) * 8 - 1));
            pmac->u[0] |= key->md.h0 & mask;
            pmac->u[1] |= key->md.h1 & mask;
            pmac->u[2] |= key->md.h2 & mask;
            pmac->u[3] |= key->md.h3 & mask;
            pmac->u[4] |= key->md.h4 & mask;

#  ifdef BSWAP4
            pmac->u[0] = BSWAP4(pmac->u[0]);
            pmac->u[1] = BSWAP4(pmac->u[1]);
            pmac->u[2] = BSWAP4(pmac->u[2]);
            pmac->u[3] = BSWAP4(pmac->u[3]);
            pmac->u[4] = BSWAP4(pmac->u[4]);
#  else
            for (i = 0; i < 5; i++) {
                res = pmac->u[i];
                pmac->c[4 * i + 0] = (unsigned char)(res >> 24);
                pmac->c[4 * i + 1] = (unsigned char)(res >> 16);
                pmac->c[4 * i + 2] = (unsigned char)(res >> 8);
                pmac->c[4 * i + 3] = (unsigned char)res;
            }
#  endif
            len += SHA_DIGEST_LENGTH;
# else      /* pre-lucky-13 reference version of above */
            SHA1_Update(&key->md, out, inp_len);
            res = key->md.num;
            SHA1_Final(pmac->c, &key->md);

            {
                unsigned int inp_blocks, pad_blocks;

                /* but pretend as if we hashed padded payload */
                inp_blocks =
                    1 + ((SHA_CBLOCK - 9 - res) >> (sizeof(res) * 8 - 1));
                res += (unsigned int)(len - inp_len);
                pad_blocks = res / SHA_CBLOCK;
                res %= SHA_CBLOCK;
                pad_blocks +=
                    1 + ((SHA_CBLOCK - 9 - res) >> (sizeof(res) * 8 - 1));
                for (; inp_blocks < pad_blocks; inp_blocks++)
                    sha1_block_data_order(&key->md, data, 1);
            }
# endif
            key->md = key->tail;
            SHA1_Update(&key->md, pmac->c, SHA_DIGEST_LENGTH);
            SHA1_Final(pmac->c, &key->md);

            /* verify HMAC */
            out += inp_len;
            len -= inp_len;
# if 1      /* see original reference version in #else */
            {
                unsigned char *p = out + len - 1 - maxpad - SHA_DIGEST_LENGTH;
                size_t off = out - p;
                unsigned int c, cmask;

                maxpad += SHA_DIGEST_LENGTH;
                for (res = 0, i = 0, j = 0; j < maxpad; j++) {
                    c = p[j];
                    cmask =
                        ((int)(j - off - SHA_DIGEST_LENGTH)) >> (sizeof(int) *
                                                                 8 - 1);
                    res |= (c ^ pad) & ~cmask; /* ... and padding */
                    cmask &= ((int)(off - 1 - j)) >> (sizeof(int) * 8 - 1);
                    res |= (c ^ pmac->c[i]) & cmask;
                    i += 1 & cmask;
                }
                maxpad -= SHA_DIGEST_LENGTH;

                res = 0 - ((0 - res) >> (sizeof(res) * 8 - 1));
                ret &= (int)~res;
            }
# else      /* pre-lucky-13 reference version of above */
            for (res = 0, i = 0; i < SHA_DIGEST_LENGTH; i++)
                res |= out[i] ^ pmac->c[i];
            res = 0 - ((0 - res) >> (sizeof(res) * 8 - 1));
            ret &= (int)~res;

            /* verify padding */
            pad = (pad & ~res) | (maxpad & res);
            out = out + len - 1 - pad;
            for (res = 0, i = 0; i < pad; i++)
                res |= out[i] ^ pad;

            res = (0 - res) >> (sizeof(res) * 8 - 1);
            ret &= (int)~res;
# endif
            return ret;
        } else {
# if defined(STITCHED_DECRYPT_CALL)
            if (len >= 1024 && ctx->key_len == 32) {
                if (sha_off %= SHA_CBLOCK)
                    blocks = (len - 3 * SHA_CBLOCK) / SHA_CBLOCK;
                else
                    blocks = (len - 2 * SHA_CBLOCK) / SHA_CBLOCK;
                aes_off = len - blocks * SHA_CBLOCK;

                aesni_cbc_encrypt(in, out, aes_off, &key->ks, ctx->iv, 0);
                SHA1_Update(&key->md, out, sha_off);
                aesni256_cbc_sha1_dec(in + aes_off,
                                      out + aes_off, blocks, &key->ks,
                                      ctx->iv, &key->md, out + sha_off);

                sha_off += blocks *= SHA_CBLOCK;
                out += sha_off;
                len -= sha_off;

                key->md.Nh += blocks >> 29;
                key->md.Nl += blocks <<= 3;
                if (key->md.Nl < (unsigned int)blocks)
                    key->md.Nh++;
            } else
# endif
                /* decrypt HMAC|padding at once */
                aesni_cbc_encrypt(in, out, len, &key->ks,
                                  EVP_CIPHER_CTX_iv_noconst(ctx), 0);

            SHA1_Update(&key->md, out, len);
        }
    }

    return 1;
}

static int aesni_cbc_hmac_sha1_ctrl(EVP_CIPHER_CTX *ctx, int type, int arg,
                                    void *ptr)
{
    EVP_AES_HMAC_SHA1 *key = data(ctx);

    switch (type) {
    case EVP_CTRL_AEAD_SET_MAC_KEY:
        {
            unsigned int i;
            unsigned char hmac_key[64];

            memset(hmac_key, 0, sizeof(hmac_key));

            if (arg > (int)sizeof(hmac_key)) {
                SHA1_Init(&key->head);
                SHA1_Update(&key->head, ptr, arg);
                SHA1_Final(hmac_key, &key->head);
            } else {
                memcpy(hmac_key, ptr, arg);
            }

            for (i = 0; i < sizeof(hmac_key); i++)
                hmac_key[i] ^= 0x36; /* ipad */
            SHA1_Init(&key->head);
            SHA1_Update(&key->head, hmac_key, sizeof(hmac_key));

            for (i = 0; i < sizeof(hmac_key); i++)
                hmac_key[i] ^= 0x36 ^ 0x5c; /* opad */
            SHA1_Init(&key->tail);
            SHA1_Update(&key->tail, hmac_key, sizeof(hmac_key));

            OPENSSL_cleanse(hmac_key, sizeof(hmac_key));

            return 1;
        }
    case EVP_CTRL_AEAD_TLS1_AAD:
        {
            unsigned char *p = ptr;
            unsigned int len;

            if (arg != EVP_AEAD_TLS1_AAD_LEN)
                return -1;

            len = p[arg - 2] << 8 | p[arg - 1];

            if (EVP_CIPHER_CTX_encrypting(ctx)) {
                key->payload_length = len;
                if ((key->aux.tls_ver =
                     p[arg - 4] << 8 | p[arg - 3]) >= TLS1_1_VERSION) {
                    if (len < AES_BLOCK_SIZE)
                        return 0;
                    len -= AES_BLOCK_SIZE;
                    p[arg - 2] = len >> 8;
                    p[arg - 1] = len;
                }
                key->md = key->head;
                SHA1_Update(&key->md, p, arg);

                return (int)(((len + SHA_DIGEST_LENGTH +
                               AES_BLOCK_SIZE) & -AES_BLOCK_SIZE)
                             - len);
            } else {
                memcpy(key->aux.tls_aad, ptr, arg);
                key->payload_length = arg;

                return SHA_DIGEST_LENGTH;
            }
        }
# if !defined(OPENSSL_NO_MULTIBLOCK)
    case EVP_CTRL_TLS1_1_MULTIBLOCK_MAX_BUFSIZE:
        return (int)(5 + 16 + ((arg + 20 + 16) & -16));
    case EVP_CTRL_TLS1_1_MULTIBLOCK_AAD:
        {
            EVP_CTRL_TLS1_1_MULTIBLOCK_PARAM *param =
                (EVP_CTRL_TLS1_1_MULTIBLOCK_PARAM *) ptr;
            unsigned int n4x = 1, x4;
            unsigned int frag, last, packlen, inp_len;

            if (arg < (int)sizeof(EVP_CTRL_TLS1_1_MULTIBLOCK_PARAM))
                return -1;

            inp_len = param->inp[11] << 8 | param->inp[12];

            if (EVP_CIPHER_CTX_encrypting(ctx)) {
                if ((param->inp[9] << 8 | param->inp[10]) < TLS1_1_VERSION)
                    return -1;

                if (inp_len) {
                    if (inp_len < 4096)
                        return 0; /* too short */

                    if (inp_len >= 8192 && OPENSSL_ia32cap_P[2] & (1 << 5))
                        n4x = 2; /* AVX2 */
                } else if ((n4x = param->interleave / 4) && n4x <= 2)
                    inp_len = param->len;
                else
                    return -1;

                key->md = key->head;
                SHA1_Update(&key->md, param->inp, 13);

                x4 = 4 * n4x;
                n4x += 1;

                frag = inp_len >> n4x;
                last = inp_len + frag - (frag << n4x);
                if (last > frag && ((last + 13 + 9) % 64 < (x4 - 1))) {
                    frag++;
                    last -= x4 - 1;
                }

                packlen = 5 + 16 + ((frag + 20 + 16) & -16);
                packlen = (packlen << n4x) - packlen;
                packlen += 5 + 16 + ((last + 20 + 16) & -16);

                param->interleave = x4;

                return (int)packlen;
            } else
                return -1;      /* not yet */
        }
    case EVP_CTRL_TLS1_1_MULTIBLOCK_ENCRYPT:
        {
            EVP_CTRL_TLS1_1_MULTIBLOCK_PARAM *param =
                (EVP_CTRL_TLS1_1_MULTIBLOCK_PARAM *) ptr;

            return (int)tls1_1_multi_block_encrypt(key, param->out,
                                                   param->inp, param->len,
                                                   param->interleave / 4);
        }
    case EVP_CTRL_TLS1_1_MULTIBLOCK_DECRYPT:
# endif
    default:
        return -1;
    }
}

static EVP_CIPHER aesni_128_cbc_hmac_sha1_cipher = {
# ifdef NID_aes_128_cbc_hmac_sha1
    NID_aes_128_cbc_hmac_sha1,
# else
    NID_undef,
# endif
    AES_BLOCK_SIZE, 16, AES_BLOCK_SIZE,
    EVP_CIPH_CBC_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1 |
        EVP_CIPH_FLAG_AEAD_CIPHER | EVP_CIPH_FLAG_TLS1_1_MULTIBLOCK,
    aesni_cbc_hmac_sha1_init_key,
    aesni_cbc_hmac_sha1_cipher,
    NULL,
    sizeof(EVP_AES_HMAC_SHA1),
    EVP_CIPH_FLAG_DEFAULT_ASN1 ? NULL : EVP_CIPHER_set_asn1_iv,
    EVP_CIPH_FLAG_DEFAULT_ASN1 ? NULL : EVP_CIPHER_get_asn1_iv,
    aesni_cbc_hmac_sha1_ctrl,
    NULL
};

static EVP_CIPHER aesni_256_cbc_hmac_sha1_cipher = {
# ifdef NID_aes_256_cbc_hmac_sha1
    NID_aes_256_cbc_hmac_sha1,
# else
    NID_undef,
# endif
    AES_BLOCK_SIZE, 32, AES_BLOCK_SIZE,
    EVP_CIPH_CBC_MODE | EVP_CIPH_FLAG_DEFAULT_ASN1 |
        EVP_CIPH_FLAG_AEAD_CIPHER | EVP_CIPH_FLAG_TLS1_1_MULTIBLOCK,
    aesni_cbc_hmac_sha1_init_key,
    aesni_cbc_hmac_sha1_cipher,
    NULL,
    sizeof(EVP_AES_HMAC_SHA1),
    EVP_CIPH_FLAG_DEFAULT_ASN1 ? NULL : EVP_CIPHER_set_asn1_iv,
    EVP_CIPH_FLAG_DEFAULT_ASN1 ? NULL : EVP_CIPHER_get_asn1_iv,
    aesni_cbc_hmac_sha1_ctrl,
    NULL
};

const EVP_CIPHER *EVP_aes_128_cbc_hmac_sha1(void)
{
    return (OPENSSL_ia32cap_P[1] & AESNI_CAPABLE ?
            &aesni_128_cbc_hmac_sha1_cipher : NULL);
}

const EVP_CIPHER *EVP_aes_256_cbc_hmac_sha1(void)
{
    return (OPENSSL_ia32cap_P[1] & AESNI_CAPABLE ?
            &aesni_256_cbc_hmac_sha1_cipher : NULL);
}
#else
const EVP_CIPHER *EVP_aes_128_cbc_hmac_sha1(void)
{
    return NULL;
}

const EVP_CIPHER *EVP_aes_256_cbc_hmac_sha1(void)
{
    return NULL;
}
#endif
