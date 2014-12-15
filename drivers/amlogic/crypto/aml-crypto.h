/* Copyright (C) 2003-2006, Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _GEODE_AES_H_
#define _GEODE_AES_H_

/* driver logic flags */
#define TDES_KEY_LENGTH 32
#define TDES_MIN_BLOCK_SIZE 8

#define MODE_ECB 0
#define MODE_CBC 1
#define MODE_CTR 2

#define DIR_ENCRYPT 0
#define DIR_DECRYPT 1

#define INLINE_TYPE_NORMAL       0
#define INLINE_TYPE_TDES            1
#define INLINE_TYPE_DIVX              2
#define INLINE_TYPE_CRC              3
#define INLINE_TYPE_AES              4

#define NDMA_CNTL_REG0                             0x2270
    #define NDMA_ENABLE                 14
#define NDMA_TABLE_ADD_REG                         0x2272
#define NDMA_TDES_KEY_LO                           0x2273
#define NDMA_TDES_KEY_HI                           0x2274
#define NDMA_TDES_CONTROL                          0x2275
#define NDMA_AES_CONTROL                           0x2276
#define NDMA_AES_RK_FIFO                           0x2277
#define NDMA_CRC_OUT                               0x2278
#define NDMA_THREAD_REG                            0x2279
#define NDMA_THREAD_TABLE_START0                   0x2280
#define NDMA_THREAD_TABLE_CURR0                    0x2281
#define NDMA_THREAD_TABLE_END0                     0x2282
#define NDMA_THREAD_TABLE_START1                   0x2283
#define NDMA_THREAD_TABLE_CURR1                    0x2284
#define NDMA_THREAD_TABLE_END1                     0x2285
#define NDMA_THREAD_TABLE_START2                   0x2286
#define NDMA_THREAD_TABLE_CURR2                    0x2287
#define NDMA_THREAD_TABLE_END2                     0x2288
#define NDMA_THREAD_TABLE_START3                   0x2289
#define NDMA_THREAD_TABLE_CURR3                    0x228a
#define NDMA_THREAD_TABLE_END3                     0x228b
#define NDMA_CNTL_REG1                             0x228c
#define NDMA_AES_KEY_0                             0x2290
#define NDMA_AES_KEY_1                             0x2291
#define NDMA_AES_KEY_2                             0x2292
#define NDMA_AES_KEY_3                             0x2293
#define NDMA_AES_KEY_4                             0x2294
#define NDMA_AES_KEY_5                             0x2295
#define NDMA_AES_KEY_6                             0x2296
#define NDMA_AES_KEY_7                             0x2297
#define NDMA_AES_IV_0                              0x2298
#define NDMA_AES_IV_1                              0x2299
#define NDMA_AES_IV_2                              0x229a
#define NDMA_AES_IV_3                              0x229b
#define NDMA_AES_REG0                             0x229c

struct aml_crypto_ctx {
    unsigned long key[8];
    unsigned long iv[4];
    int key_len;
    void *src;
    void *dst;
    unsigned long mode;
    unsigned long dir;
    int len;
    int threadidx;

    unsigned int dma_src;
    unsigned int dma_dest;
};


#define AES_MAXNR 14
struct aes_key_st {
    unsigned int rd_key[4 *(AES_MAXNR + 1)];
    int rounds;
};
typedef struct aes_key_st AES_KEY;
#endif
