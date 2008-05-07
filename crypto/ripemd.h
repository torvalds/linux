/*
 * Common values for RIPEMD algorithms
 */

#ifndef _CRYPTO_RMD_H
#define _CRYPTO_RMD_H

#define RMD128_DIGEST_SIZE      16
#define RMD128_BLOCK_SIZE       64

#define RMD160_DIGEST_SIZE      20
#define RMD160_BLOCK_SIZE       64

#define RMD256_DIGEST_SIZE      32
#define RMD256_BLOCK_SIZE       64

#define RMD320_DIGEST_SIZE      40
#define RMD320_BLOCK_SIZE       64

#define RMD_H0  0x67452301UL
#define RMD_H1  0xefcdab89UL
#define RMD_H2  0x98badcfeUL
#define RMD_H3  0x10325476UL
#define RMD_H4  0xc3d2e1f0UL

#endif
