/*
 * Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdlib.h>
#include <string.h>
#include <openssl/opensslconf.h>
#include <openssl/ripemd.h>

/*
 * DO EXAMINE COMMENTS IN crypto/md5/md5_locl.h & crypto/md5/md5_dgst.c
 * FOR EXPLANATIONS ON FOLLOWING "CODE."
 */
#ifdef RMD160_ASM
# if defined(__i386) || defined(__i386__) || defined(_M_IX86)
#  define ripemd160_block_data_order ripemd160_block_asm_data_order
# endif
#endif

void ripemd160_block_data_order(RIPEMD160_CTX *c, const void *p, size_t num);

#define DATA_ORDER_IS_LITTLE_ENDIAN

#define HASH_LONG               RIPEMD160_LONG
#define HASH_CTX                RIPEMD160_CTX
#define HASH_CBLOCK             RIPEMD160_CBLOCK
#define HASH_UPDATE             RIPEMD160_Update
#define HASH_TRANSFORM          RIPEMD160_Transform
#define HASH_FINAL              RIPEMD160_Final
#define HASH_MAKE_STRING(c,s)   do {    \
        unsigned long ll;               \
        ll=(c)->A; (void)HOST_l2c(ll,(s));      \
        ll=(c)->B; (void)HOST_l2c(ll,(s));      \
        ll=(c)->C; (void)HOST_l2c(ll,(s));      \
        ll=(c)->D; (void)HOST_l2c(ll,(s));      \
        ll=(c)->E; (void)HOST_l2c(ll,(s));      \
        } while (0)
#define HASH_BLOCK_DATA_ORDER   ripemd160_block_data_order

#include "internal/md32_common.h"

/*
 * Transformed F2 and F4 are courtesy of Wei Dai
 */
#define F1(x,y,z)       ((x) ^ (y) ^ (z))
#define F2(x,y,z)       ((((y) ^ (z)) & (x)) ^ (z))
#define F3(x,y,z)       (((~(y)) | (x)) ^ (z))
#define F4(x,y,z)       ((((x) ^ (y)) & (z)) ^ (y))
#define F5(x,y,z)       (((~(z)) | (y)) ^ (x))

#define RIPEMD160_A     0x67452301L
#define RIPEMD160_B     0xEFCDAB89L
#define RIPEMD160_C     0x98BADCFEL
#define RIPEMD160_D     0x10325476L
#define RIPEMD160_E     0xC3D2E1F0L

#include "rmdconst.h"

#define RIP1(a,b,c,d,e,w,s) { \
        a+=F1(b,c,d)+X(w); \
        a=ROTATE(a,s)+e; \
        c=ROTATE(c,10); }

#define RIP2(a,b,c,d,e,w,s,K) { \
        a+=F2(b,c,d)+X(w)+K; \
        a=ROTATE(a,s)+e; \
        c=ROTATE(c,10); }

#define RIP3(a,b,c,d,e,w,s,K) { \
        a+=F3(b,c,d)+X(w)+K; \
        a=ROTATE(a,s)+e; \
        c=ROTATE(c,10); }

#define RIP4(a,b,c,d,e,w,s,K) { \
        a+=F4(b,c,d)+X(w)+K; \
        a=ROTATE(a,s)+e; \
        c=ROTATE(c,10); }

#define RIP5(a,b,c,d,e,w,s,K) { \
        a+=F5(b,c,d)+X(w)+K; \
        a=ROTATE(a,s)+e; \
        c=ROTATE(c,10); }
