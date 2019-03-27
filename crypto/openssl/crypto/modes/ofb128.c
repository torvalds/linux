/*
 * Copyright 2008-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <openssl/crypto.h>
#include "modes_lcl.h"
#include <string.h>

/*
 * The input and output encrypted as though 128bit ofb mode is being used.
 * The extra state information to record how much of the 128bit block we have
 * used is contained in *num;
 */
void CRYPTO_ofb128_encrypt(const unsigned char *in, unsigned char *out,
                           size_t len, const void *key,
                           unsigned char ivec[16], int *num, block128_f block)
{
    unsigned int n;
    size_t l = 0;

    n = *num;

#if !defined(OPENSSL_SMALL_FOOTPRINT)
    if (16 % sizeof(size_t) == 0) { /* always true actually */
        do {
            while (n && len) {
                *(out++) = *(in++) ^ ivec[n];
                --len;
                n = (n + 1) % 16;
            }
# if defined(STRICT_ALIGNMENT)
            if (((size_t)in | (size_t)out | (size_t)ivec) % sizeof(size_t) !=
                0)
                break;
# endif
            while (len >= 16) {
                (*block) (ivec, ivec, key);
                for (; n < 16; n += sizeof(size_t))
                    *(size_t *)(out + n) =
                        *(size_t *)(in + n) ^ *(size_t *)(ivec + n);
                len -= 16;
                out += 16;
                in += 16;
                n = 0;
            }
            if (len) {
                (*block) (ivec, ivec, key);
                while (len--) {
                    out[n] = in[n] ^ ivec[n];
                    ++n;
                }
            }
            *num = n;
            return;
        } while (0);
    }
    /* the rest would be commonly eliminated by x86* compiler */
#endif
    while (l < len) {
        if (n == 0) {
            (*block) (ivec, ivec, key);
        }
        out[l] = in[l] ^ ivec[n];
        ++l;
        n = (n + 1) % 16;
    }

    *num = n;
}
