/*
 * Copyright 2006-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "internal/cryptlib.h"

#include <openssl/aes.h>
#include "aes_locl.h"

#define N_WORDS (AES_BLOCK_SIZE / sizeof(unsigned long))
typedef struct {
    unsigned long data[N_WORDS];
} aes_block_t;

/* XXX: probably some better way to do this */
#if defined(__i386__) || defined(__x86_64__)
# define UNALIGNED_MEMOPS_ARE_FAST 1
#else
# define UNALIGNED_MEMOPS_ARE_FAST 0
#endif

#if UNALIGNED_MEMOPS_ARE_FAST
# define load_block(d, s)        (d) = *(const aes_block_t *)(s)
# define store_block(d, s)       *(aes_block_t *)(d) = (s)
#else
# define load_block(d, s)        memcpy((d).data, (s), AES_BLOCK_SIZE)
# define store_block(d, s)       memcpy((d), (s).data, AES_BLOCK_SIZE)
#endif

/* N.B. The IV for this mode is _twice_ the block size */

void AES_ige_encrypt(const unsigned char *in, unsigned char *out,
                     size_t length, const AES_KEY *key,
                     unsigned char *ivec, const int enc)
{
    size_t n;
    size_t len = length;

    if (length == 0)
        return;

    OPENSSL_assert(in && out && key && ivec);
    OPENSSL_assert((AES_ENCRYPT == enc) || (AES_DECRYPT == enc));
    OPENSSL_assert((length % AES_BLOCK_SIZE) == 0);

    len = length / AES_BLOCK_SIZE;

    if (AES_ENCRYPT == enc) {
        if (in != out &&
            (UNALIGNED_MEMOPS_ARE_FAST
             || ((size_t)in | (size_t)out | (size_t)ivec) % sizeof(long) ==
             0)) {
            aes_block_t *ivp = (aes_block_t *) ivec;
            aes_block_t *iv2p = (aes_block_t *) (ivec + AES_BLOCK_SIZE);

            while (len) {
                aes_block_t *inp = (aes_block_t *) in;
                aes_block_t *outp = (aes_block_t *) out;

                for (n = 0; n < N_WORDS; ++n)
                    outp->data[n] = inp->data[n] ^ ivp->data[n];
                AES_encrypt((unsigned char *)outp->data,
                            (unsigned char *)outp->data, key);
                for (n = 0; n < N_WORDS; ++n)
                    outp->data[n] ^= iv2p->data[n];
                ivp = outp;
                iv2p = inp;
                --len;
                in += AES_BLOCK_SIZE;
                out += AES_BLOCK_SIZE;
            }
            memcpy(ivec, ivp->data, AES_BLOCK_SIZE);
            memcpy(ivec + AES_BLOCK_SIZE, iv2p->data, AES_BLOCK_SIZE);
        } else {
            aes_block_t tmp, tmp2;
            aes_block_t iv;
            aes_block_t iv2;

            load_block(iv, ivec);
            load_block(iv2, ivec + AES_BLOCK_SIZE);

            while (len) {
                load_block(tmp, in);
                for (n = 0; n < N_WORDS; ++n)
                    tmp2.data[n] = tmp.data[n] ^ iv.data[n];
                AES_encrypt((unsigned char *)tmp2.data,
                            (unsigned char *)tmp2.data, key);
                for (n = 0; n < N_WORDS; ++n)
                    tmp2.data[n] ^= iv2.data[n];
                store_block(out, tmp2);
                iv = tmp2;
                iv2 = tmp;
                --len;
                in += AES_BLOCK_SIZE;
                out += AES_BLOCK_SIZE;
            }
            memcpy(ivec, iv.data, AES_BLOCK_SIZE);
            memcpy(ivec + AES_BLOCK_SIZE, iv2.data, AES_BLOCK_SIZE);
        }
    } else {
        if (in != out &&
            (UNALIGNED_MEMOPS_ARE_FAST
             || ((size_t)in | (size_t)out | (size_t)ivec) % sizeof(long) ==
             0)) {
            aes_block_t *ivp = (aes_block_t *) ivec;
            aes_block_t *iv2p = (aes_block_t *) (ivec + AES_BLOCK_SIZE);

            while (len) {
                aes_block_t tmp;
                aes_block_t *inp = (aes_block_t *) in;
                aes_block_t *outp = (aes_block_t *) out;

                for (n = 0; n < N_WORDS; ++n)
                    tmp.data[n] = inp->data[n] ^ iv2p->data[n];
                AES_decrypt((unsigned char *)tmp.data,
                            (unsigned char *)outp->data, key);
                for (n = 0; n < N_WORDS; ++n)
                    outp->data[n] ^= ivp->data[n];
                ivp = inp;
                iv2p = outp;
                --len;
                in += AES_BLOCK_SIZE;
                out += AES_BLOCK_SIZE;
            }
            memcpy(ivec, ivp->data, AES_BLOCK_SIZE);
            memcpy(ivec + AES_BLOCK_SIZE, iv2p->data, AES_BLOCK_SIZE);
        } else {
            aes_block_t tmp, tmp2;
            aes_block_t iv;
            aes_block_t iv2;

            load_block(iv, ivec);
            load_block(iv2, ivec + AES_BLOCK_SIZE);

            while (len) {
                load_block(tmp, in);
                tmp2 = tmp;
                for (n = 0; n < N_WORDS; ++n)
                    tmp.data[n] ^= iv2.data[n];
                AES_decrypt((unsigned char *)tmp.data,
                            (unsigned char *)tmp.data, key);
                for (n = 0; n < N_WORDS; ++n)
                    tmp.data[n] ^= iv.data[n];
                store_block(out, tmp);
                iv = tmp2;
                iv2 = tmp;
                --len;
                in += AES_BLOCK_SIZE;
                out += AES_BLOCK_SIZE;
            }
            memcpy(ivec, iv.data, AES_BLOCK_SIZE);
            memcpy(ivec + AES_BLOCK_SIZE, iv2.data, AES_BLOCK_SIZE);
        }
    }
}

/*
 * Note that its effectively impossible to do biIGE in anything other
 * than a single pass, so no provision is made for chaining.
 */

/* N.B. The IV for this mode is _four times_ the block size */

void AES_bi_ige_encrypt(const unsigned char *in, unsigned char *out,
                        size_t length, const AES_KEY *key,
                        const AES_KEY *key2, const unsigned char *ivec,
                        const int enc)
{
    size_t n;
    size_t len = length;
    unsigned char tmp[AES_BLOCK_SIZE];
    unsigned char tmp2[AES_BLOCK_SIZE];
    unsigned char tmp3[AES_BLOCK_SIZE];
    unsigned char prev[AES_BLOCK_SIZE];
    const unsigned char *iv;
    const unsigned char *iv2;

    OPENSSL_assert(in && out && key && ivec);
    OPENSSL_assert((AES_ENCRYPT == enc) || (AES_DECRYPT == enc));
    OPENSSL_assert((length % AES_BLOCK_SIZE) == 0);

    if (AES_ENCRYPT == enc) {
        /*
         * XXX: Do a separate case for when in != out (strictly should check
         * for overlap, too)
         */

        /* First the forward pass */
        iv = ivec;
        iv2 = ivec + AES_BLOCK_SIZE;
        while (len >= AES_BLOCK_SIZE) {
            for (n = 0; n < AES_BLOCK_SIZE; ++n)
                out[n] = in[n] ^ iv[n];
            AES_encrypt(out, out, key);
            for (n = 0; n < AES_BLOCK_SIZE; ++n)
                out[n] ^= iv2[n];
            iv = out;
            memcpy(prev, in, AES_BLOCK_SIZE);
            iv2 = prev;
            len -= AES_BLOCK_SIZE;
            in += AES_BLOCK_SIZE;
            out += AES_BLOCK_SIZE;
        }

        /* And now backwards */
        iv = ivec + AES_BLOCK_SIZE * 2;
        iv2 = ivec + AES_BLOCK_SIZE * 3;
        len = length;
        while (len >= AES_BLOCK_SIZE) {
            out -= AES_BLOCK_SIZE;
            /*
             * XXX: reduce copies by alternating between buffers
             */
            memcpy(tmp, out, AES_BLOCK_SIZE);
            for (n = 0; n < AES_BLOCK_SIZE; ++n)
                out[n] ^= iv[n];
            /*
             * hexdump(stdout, "out ^ iv", out, AES_BLOCK_SIZE);
             */
            AES_encrypt(out, out, key);
            /*
             * hexdump(stdout,"enc", out, AES_BLOCK_SIZE);
             */
            /*
             * hexdump(stdout,"iv2", iv2, AES_BLOCK_SIZE);
             */
            for (n = 0; n < AES_BLOCK_SIZE; ++n)
                out[n] ^= iv2[n];
            /*
             * hexdump(stdout,"out", out, AES_BLOCK_SIZE);
             */
            iv = out;
            memcpy(prev, tmp, AES_BLOCK_SIZE);
            iv2 = prev;
            len -= AES_BLOCK_SIZE;
        }
    } else {
        /* First backwards */
        iv = ivec + AES_BLOCK_SIZE * 2;
        iv2 = ivec + AES_BLOCK_SIZE * 3;
        in += length;
        out += length;
        while (len >= AES_BLOCK_SIZE) {
            in -= AES_BLOCK_SIZE;
            out -= AES_BLOCK_SIZE;
            memcpy(tmp, in, AES_BLOCK_SIZE);
            memcpy(tmp2, in, AES_BLOCK_SIZE);
            for (n = 0; n < AES_BLOCK_SIZE; ++n)
                tmp[n] ^= iv2[n];
            AES_decrypt(tmp, out, key);
            for (n = 0; n < AES_BLOCK_SIZE; ++n)
                out[n] ^= iv[n];
            memcpy(tmp3, tmp2, AES_BLOCK_SIZE);
            iv = tmp3;
            iv2 = out;
            len -= AES_BLOCK_SIZE;
        }

        /* And now forwards */
        iv = ivec;
        iv2 = ivec + AES_BLOCK_SIZE;
        len = length;
        while (len >= AES_BLOCK_SIZE) {
            memcpy(tmp, out, AES_BLOCK_SIZE);
            memcpy(tmp2, out, AES_BLOCK_SIZE);
            for (n = 0; n < AES_BLOCK_SIZE; ++n)
                tmp[n] ^= iv2[n];
            AES_decrypt(tmp, out, key);
            for (n = 0; n < AES_BLOCK_SIZE; ++n)
                out[n] ^= iv[n];
            memcpy(tmp3, tmp2, AES_BLOCK_SIZE);
            iv = tmp3;
            iv2 = out;
            len -= AES_BLOCK_SIZE;
            in += AES_BLOCK_SIZE;
            out += AES_BLOCK_SIZE;
        }
    }
}
