/*
 * Copyright 2016-2017 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * Derived from the BLAKE2 reference implementation written by Samuel Neves.
 * Copyright 2012, Samuel Neves <sneves@dei.uc.pt>
 * More information about the BLAKE2 hash function and its implementations
 * can be found at https://blake2.net.
 */

#include <string.h>

static ossl_inline uint32_t load32(const uint8_t *src)
{
    const union {
        long one;
        char little;
    } is_endian = { 1 };

    if (is_endian.little) {
        uint32_t w;
        memcpy(&w, src, sizeof(w));
        return w;
    } else {
        uint32_t w = ((uint32_t)src[0])
                   | ((uint32_t)src[1] <<  8)
                   | ((uint32_t)src[2] << 16)
                   | ((uint32_t)src[3] << 24);
        return w;
    }
}

static ossl_inline uint64_t load64(const uint8_t *src)
{
    const union {
        long one;
        char little;
    } is_endian = { 1 };

    if (is_endian.little) {
        uint64_t w;
        memcpy(&w, src, sizeof(w));
        return w;
    } else {
        uint64_t w = ((uint64_t)src[0])
                   | ((uint64_t)src[1] <<  8)
                   | ((uint64_t)src[2] << 16)
                   | ((uint64_t)src[3] << 24)
                   | ((uint64_t)src[4] << 32)
                   | ((uint64_t)src[5] << 40)
                   | ((uint64_t)src[6] << 48)
                   | ((uint64_t)src[7] << 56);
        return w;
    }
}

static ossl_inline void store32(uint8_t *dst, uint32_t w)
{
    const union {
        long one;
        char little;
    } is_endian = { 1 };

    if (is_endian.little) {
        memcpy(dst, &w, sizeof(w));
    } else {
        uint8_t *p = (uint8_t *)dst;
        int i;

        for (i = 0; i < 4; i++)
            p[i] = (uint8_t)(w >> (8 * i));
    }
}

static ossl_inline void store64(uint8_t *dst, uint64_t w)
{
    const union {
        long one;
        char little;
    } is_endian = { 1 };

    if (is_endian.little) {
        memcpy(dst, &w, sizeof(w));
    } else {
        uint8_t *p = (uint8_t *)dst;
        int i;

        for (i = 0; i < 8; i++)
            p[i] = (uint8_t)(w >> (8 * i));
    }
}

static ossl_inline uint64_t load48(const uint8_t *src)
{
    uint64_t w = ((uint64_t)src[0])
               | ((uint64_t)src[1] <<  8)
               | ((uint64_t)src[2] << 16)
               | ((uint64_t)src[3] << 24)
               | ((uint64_t)src[4] << 32)
               | ((uint64_t)src[5] << 40);
    return w;
}

static ossl_inline void store48(uint8_t *dst, uint64_t w)
{
    uint8_t *p = (uint8_t *)dst;
    p[0] = (uint8_t)w;
    p[1] = (uint8_t)(w>>8);
    p[2] = (uint8_t)(w>>16);
    p[3] = (uint8_t)(w>>24);
    p[4] = (uint8_t)(w>>32);
    p[5] = (uint8_t)(w>>40);
}

static ossl_inline uint32_t rotr32(const uint32_t w, const unsigned int c)
{
    return (w >> c) | (w << (32 - c));
}

static ossl_inline uint64_t rotr64(const uint64_t w, const unsigned int c)
{
    return (w >> c) | (w << (64 - c));
}
