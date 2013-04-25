/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#include "rt_config.h"


/* Basic operations */
#define SHR(x,n) (x >> n) /* SHR(x)^n, right shift n bits , x is w-bit word, 0 <= n <= w */
#define ROTR(x,n,w) ((x >> n) | (x << (w - n))) /* ROTR(x)^n, circular right shift n bits , x is w-bit word, 0 <= n <= w */
#define ROTL(x,n,w) ((x << n) | (x >> (w - n))) /* ROTL(x)^n, circular left shift n bits , x is w-bit word, 0 <= n <= w */
#define ROTR32(x,n) ROTR(x,n,32) /* 32 bits word */
#define ROTL32(x,n) ROTL(x,n,32) /* 32 bits word */ 

/* Basic functions */
#define Ch(x,y,z) ((x & y) ^ ((~x) & z))
#define Maj(x,y,z) ((x & y) ^ (x & z) ^ (y & z))
#define Parity(x,y,z) (x ^ y ^ z)

#ifdef SHA1_SUPPORT
/* SHA1 constants */
#define SHA1_MASK 0x0000000f
static const UINT32 SHA1_K[4] = {
    0x5a827999UL, 0x6ed9eba1UL, 0x8f1bbcdcUL, 0xca62c1d6UL
};
static const UINT32 SHA1_DefaultHashValue[5] = {
    0x67452301UL, 0xefcdab89UL, 0x98badcfeUL, 0x10325476UL, 0xc3d2e1f0UL
};
#endif /* SHA1_SUPPORT */


#ifdef SHA256_SUPPORT
/* SHA256 functions */
#define Zsigma_256_0(x) (ROTR32(x,2) ^ ROTR32(x,13) ^ ROTR32(x,22))
#define Zsigma_256_1(x) (ROTR32(x,6) ^ ROTR32(x,11) ^ ROTR32(x,25))
#define Sigma_256_0(x)  (ROTR32(x,7) ^ ROTR32(x,18) ^ SHR(x,3))
#define Sigma_256_1(x)  (ROTR32(x,17) ^ ROTR32(x,19) ^ SHR(x,10))
/* SHA256 constants */
static const UINT32 SHA256_K[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 
    0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL, 
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL, 
    0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL, 
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL, 
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL, 
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 
    0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL, 
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL, 
    0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 
    0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL, 
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL, 
    0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL, 
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL, 
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};
static const UINT32 SHA256_DefaultHashValue[8] = {
    0x6a09e667UL, 0xbb67ae85UL, 0x3c6ef372UL, 0xa54ff53aUL,
    0x510e527fUL, 0x9b05688cUL, 0x1f83d9abUL, 0x5be0cd19UL
};
#endif /* SHA256_SUPPORT */


#ifdef SHA1_SUPPORT
/*
========================================================================
Routine Description:
    Initial SHA1_CTX_STRUC

Arguments:
    pSHA_CTX        Pointer to SHA1_CTX_STRUC

Return Value:
    None

Note:
    None
========================================================================
*/
VOID RT_SHA1_Init (
    IN  SHA1_CTX_STRUC *pSHA_CTX)
{
    NdisMoveMemory(pSHA_CTX->HashValue, SHA1_DefaultHashValue, 
        sizeof(SHA1_DefaultHashValue));
    NdisZeroMemory(pSHA_CTX->Block, SHA1_BLOCK_SIZE);
    pSHA_CTX->MessageLen = 0;
    pSHA_CTX->BlockLen   = 0;
} /* End of RT_SHA1_Init */


/*
========================================================================
Routine Description:
    SHA1 computation for one block (512 bits)

Arguments:
    pSHA_CTX        Pointer to SHA1_CTX_STRUC

Return Value:
    None

Note:
    None
========================================================================
*/
VOID RT_SHA1_Hash (
    IN  SHA1_CTX_STRUC *pSHA_CTX)
{
    UINT32 W_i,t;
    UINT32 W[80];
    UINT32 a,b,c,d,e,T,f_t = 0;
  
    /* Prepare the message schedule, {W_i}, 0 < t < 15 */
    NdisMoveMemory(W, pSHA_CTX->Block, SHA1_BLOCK_SIZE);
    for (W_i = 0; W_i < 16; W_i++) {
        W[W_i] = cpu2be32(W[W_i]); /* Endian Swap */
    } /* End of for */

    for (W_i = 16; W_i < 80; W_i++) {
        W[W_i] = ROTL32((W[W_i - 3] ^ W[W_i - 8] ^ W[W_i - 14] ^ W[W_i - 16]),1);
    } /* End of for */

        
    /* SHA256 hash computation */
    /* Initialize the working variables */
    a = pSHA_CTX->HashValue[0];
    b = pSHA_CTX->HashValue[1];
    c = pSHA_CTX->HashValue[2];
    d = pSHA_CTX->HashValue[3];
    e = pSHA_CTX->HashValue[4];

    /* 80 rounds */
    for (t = 0;t < 20;t++) {
        f_t = Ch(b,c,d);
        T = ROTL32(a,5) + f_t + e + SHA1_K[0] + W[t];
        e = d;
        d = c;
        c = ROTL32(b,30);
        b = a;
        a = T;
     } /* End of for */
    for (t = 20;t < 40;t++) {
        f_t = Parity(b,c,d);
        T = ROTL32(a,5) + f_t + e + SHA1_K[1] + W[t];
        e = d;
        d = c;
        c = ROTL32(b,30);
        b = a;
        a = T;
     } /* End of for */
    for (t = 40;t < 60;t++) {
        f_t = Maj(b,c,d);
        T = ROTL32(a,5) + f_t + e + SHA1_K[2] + W[t];
        e = d;
        d = c;
        c = ROTL32(b,30);
        b = a;
        a = T;
     } /* End of for */
    for (t = 60;t < 80;t++) {
        f_t = Parity(b,c,d);
        T = ROTL32(a,5) + f_t + e + SHA1_K[3] + W[t];
        e = d;
        d = c;
        c = ROTL32(b,30);
        b = a;
        a = T;
     } /* End of for */


     /* Compute the i^th intermediate hash value H^(i) */
     pSHA_CTX->HashValue[0] += a;
     pSHA_CTX->HashValue[1] += b;
     pSHA_CTX->HashValue[2] += c;
     pSHA_CTX->HashValue[3] += d;
     pSHA_CTX->HashValue[4] += e;

    NdisZeroMemory(pSHA_CTX->Block, SHA1_BLOCK_SIZE);
    pSHA_CTX->BlockLen = 0;
} /* End of RT_SHA1_Hash */


/*
========================================================================
Routine Description:
    The message is appended to block. If block size > 64 bytes, the SHA1_Hash 
will be called.

Arguments:
    pSHA_CTX        Pointer to SHA1_CTX_STRUC
    message         Message context
    messageLen      The length of message in bytes

Return Value:
    None

Note:
    None
========================================================================
*/
VOID RT_SHA1_Append (
    IN  SHA1_CTX_STRUC *pSHA_CTX, 
    IN  const UINT8 Message[], 
    IN  UINT MessageLen)
{
    UINT appendLen = 0;
    UINT diffLen   = 0;
    
    while (appendLen != MessageLen) {
        diffLen = MessageLen - appendLen;
        if ((pSHA_CTX->BlockLen + diffLen) <  SHA1_BLOCK_SIZE) {
            NdisMoveMemory(pSHA_CTX->Block + pSHA_CTX->BlockLen, 
                Message + appendLen, diffLen);
            pSHA_CTX->BlockLen += diffLen;
            appendLen += diffLen;
        } 
        else
        {
            NdisMoveMemory(pSHA_CTX->Block + pSHA_CTX->BlockLen, 
                Message + appendLen, SHA1_BLOCK_SIZE - pSHA_CTX->BlockLen);
            appendLen += (SHA1_BLOCK_SIZE - pSHA_CTX->BlockLen);
            pSHA_CTX->BlockLen = SHA1_BLOCK_SIZE;
            RT_SHA1_Hash(pSHA_CTX);
        } /* End of if */
    } /* End of while */
    pSHA_CTX->MessageLen += MessageLen;
} /* End of RT_SHA1_Append */


/*
========================================================================
Routine Description:
    1. Append bit 1 to end of the message
    2. Append the length of message in rightmost 64 bits
    3. Transform the Hash Value to digest message

Arguments:
    pSHA_CTX        Pointer to SHA1_CTX_STRUC

Return Value:
    digestMessage   Digest message

Note:
    None
========================================================================
*/
VOID RT_SHA1_End (
    IN  SHA1_CTX_STRUC *pSHA_CTX, 
    OUT UINT8 DigestMessage[])
{
    UINT index;
    UINT64 message_length_bits;

    /* Append bit 1 to end of the message */
    NdisFillMemory(pSHA_CTX->Block + pSHA_CTX->BlockLen, 1, 0x80);

    /* 55 = 64 - 8 - 1: append 1 bit(1 byte) and message length (8 bytes) */
    if (pSHA_CTX->BlockLen > 55)
        RT_SHA1_Hash(pSHA_CTX);
    /* End of if */

    /* Append the length of message in rightmost 64 bits */
    message_length_bits = pSHA_CTX->MessageLen*8;
    message_length_bits = cpu2be64(message_length_bits);       
    NdisMoveMemory(&pSHA_CTX->Block[56], &message_length_bits, 8);
    RT_SHA1_Hash(pSHA_CTX);

    /* Return message digest, transform the UINT32 hash value to bytes */
    for (index = 0; index < 5;index++)
        pSHA_CTX->HashValue[index] = cpu2be32(pSHA_CTX->HashValue[index]);
        /* End of for */
    NdisMoveMemory(DigestMessage, pSHA_CTX->HashValue, SHA1_DIGEST_SIZE);
} /* End of RT_SHA1_End */


/*
========================================================================
Routine Description:
    SHA1 algorithm

Arguments:
    message         Message context
    messageLen      The length of message in bytes

Return Value:
    digestMessage   Digest message

Note:
    None
========================================================================
*/
VOID RT_SHA1 (
    IN  const UINT8 Message[], 
    IN  UINT MessageLen, 
    OUT UINT8 DigestMessage[])
{

    SHA1_CTX_STRUC sha_ctx;

    NdisZeroMemory(&sha_ctx, sizeof(SHA1_CTX_STRUC));
    RT_SHA1_Init(&sha_ctx);
    RT_SHA1_Append(&sha_ctx, Message, MessageLen);
    RT_SHA1_End(&sha_ctx, DigestMessage);
} /* End of RT_SHA1 */
#endif /* SHA1_SUPPORT */


#ifdef SHA256_SUPPORT
/*
========================================================================
Routine Description:
    Initial SHA256_CTX_STRUC

Arguments:
    pSHA_CTX    Pointer to SHA256_CTX_STRUC

Return Value:
    None

Note:
    None
========================================================================
*/
VOID RT_SHA256_Init (
    IN  SHA256_CTX_STRUC *pSHA_CTX)
{
    NdisMoveMemory(pSHA_CTX->HashValue, SHA256_DefaultHashValue, 
        sizeof(SHA256_DefaultHashValue));
    NdisZeroMemory(pSHA_CTX->Block, SHA256_BLOCK_SIZE);
    pSHA_CTX->MessageLen = 0;
    pSHA_CTX->BlockLen   = 0;
} /* End of RT_SHA256_Init */


/*
========================================================================
Routine Description:
    SHA256 computation for one block (512 bits)

Arguments:
    pSHA_CTX    Pointer to SHA256_CTX_STRUC

Return Value:
    None

Note:
    None
========================================================================
*/
VOID RT_SHA256_Hash (
    IN  SHA256_CTX_STRUC *pSHA_CTX)
{
    UINT32 W_i,t;
    UINT32 W[64];
    UINT32 a,b,c,d,e,f,g,h,T1,T2;
    
    /* Prepare the message schedule, {W_i}, 0 < t < 15 */
    NdisMoveMemory(W, pSHA_CTX->Block, SHA256_BLOCK_SIZE);
    for (W_i = 0; W_i < 16; W_i++)
        W[W_i] = cpu2be32(W[W_i]); /* Endian Swap */
        /* End of for */
 
    /* SHA256 hash computation */
    /* Initialize the working variables */
    a = pSHA_CTX->HashValue[0];
    b = pSHA_CTX->HashValue[1];
    c = pSHA_CTX->HashValue[2];
    d = pSHA_CTX->HashValue[3];
    e = pSHA_CTX->HashValue[4];
    f = pSHA_CTX->HashValue[5];
    g = pSHA_CTX->HashValue[6];
    h = pSHA_CTX->HashValue[7];

    /* 64 rounds */
    for (t = 0;t < 64;t++) {
        if (t > 15) /* Prepare the message schedule, {W_i}, 16 < t < 63 */
            W[t] = Sigma_256_1(W[t-2]) + W[t-7] + Sigma_256_0(W[t-15]) + W[t-16];
            /* End of if */
        T1 = h + Zsigma_256_1(e) + Ch(e,f,g) + SHA256_K[t] + W[t];
        T2 = Zsigma_256_0(a) + Maj(a,b,c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
     } /* End of for */

     /* Compute the i^th intermediate hash value H^(i) */
     pSHA_CTX->HashValue[0] += a;
     pSHA_CTX->HashValue[1] += b;
     pSHA_CTX->HashValue[2] += c;
     pSHA_CTX->HashValue[3] += d;
     pSHA_CTX->HashValue[4] += e;
     pSHA_CTX->HashValue[5] += f;
     pSHA_CTX->HashValue[6] += g;
     pSHA_CTX->HashValue[7] += h;

    NdisZeroMemory(pSHA_CTX->Block, SHA256_BLOCK_SIZE);
    pSHA_CTX->BlockLen = 0;
} /* End of RT_SHA256_Hash */


/*
========================================================================
Routine Description:
    The message is appended to block. If block size > 64 bytes, the SHA256_Hash 
will be called.

Arguments:
    pSHA_CTX    Pointer to SHA256_CTX_STRUC
    message     Message context
    messageLen  The length of message in bytes

Return Value:
    None

Note:
    None
========================================================================
*/
VOID RT_SHA256_Append (
    IN  SHA256_CTX_STRUC *pSHA_CTX, 
    IN  const UINT8 Message[], 
    IN  UINT MessageLen)
{
    UINT appendLen = 0;
    UINT diffLen   = 0;
    
    while (appendLen != MessageLen) {
        diffLen = MessageLen - appendLen;
        if ((pSHA_CTX->BlockLen + diffLen) <  SHA256_BLOCK_SIZE) {
            NdisMoveMemory(pSHA_CTX->Block + pSHA_CTX->BlockLen, 
                Message + appendLen, diffLen);
            pSHA_CTX->BlockLen += diffLen;
            appendLen += diffLen;
        } 
        else
        {
            NdisMoveMemory(pSHA_CTX->Block + pSHA_CTX->BlockLen, 
                Message + appendLen, SHA256_BLOCK_SIZE - pSHA_CTX->BlockLen);
            appendLen += (SHA256_BLOCK_SIZE - pSHA_CTX->BlockLen);
            pSHA_CTX->BlockLen = SHA256_BLOCK_SIZE;
            RT_SHA256_Hash(pSHA_CTX);
        } /* End of if */
    } /* End of while */
    pSHA_CTX->MessageLen += MessageLen;
} /* End of RT_SHA256_Append */


/*
========================================================================
Routine Description:
    1. Append bit 1 to end of the message
    2. Append the length of message in rightmost 64 bits
    3. Transform the Hash Value to digest message

Arguments:
    pSHA_CTX        Pointer to SHA256_CTX_STRUC

Return Value:
    digestMessage   Digest message

Note:
    None
========================================================================
*/
VOID RT_SHA256_End (
    IN  SHA256_CTX_STRUC *pSHA_CTX, 
    OUT UINT8 DigestMessage[])
{
    UINT index;
    UINT64 message_length_bits;

    /* Append bit 1 to end of the message */
    NdisFillMemory(pSHA_CTX->Block + pSHA_CTX->BlockLen, 1, 0x80);

    /* 55 = 64 - 8 - 1: append 1 bit(1 byte) and message length (8 bytes) */
    if (pSHA_CTX->BlockLen > 55)
        RT_SHA256_Hash(pSHA_CTX);
    /* End of if */

    /* Append the length of message in rightmost 64 bits */
    message_length_bits = pSHA_CTX->MessageLen*8;
    message_length_bits = cpu2be64(message_length_bits);       
    NdisMoveMemory(&pSHA_CTX->Block[56], &message_length_bits, 8);
    RT_SHA256_Hash(pSHA_CTX);

    /* Return message digest, transform the UINT32 hash value to bytes */
    for (index = 0; index < 8;index++)
        pSHA_CTX->HashValue[index] = cpu2be32(pSHA_CTX->HashValue[index]);
        /* End of for */
    NdisMoveMemory(DigestMessage, pSHA_CTX->HashValue, SHA256_DIGEST_SIZE);
} /* End of RT_SHA256_End */


/*
========================================================================
Routine Description:
    SHA256 algorithm

Arguments:
    message         Message context
    messageLen      The length of message in bytes

Return Value:
    digestMessage   Digest message

Note:
    None
========================================================================
*/
VOID RT_SHA256 (
    IN  const UINT8 Message[], 
    IN  UINT MessageLen, 
    OUT UINT8 DigestMessage[])
{
    SHA256_CTX_STRUC sha_ctx;

    NdisZeroMemory(&sha_ctx, sizeof(SHA256_CTX_STRUC));
    RT_SHA256_Init(&sha_ctx);
    RT_SHA256_Append(&sha_ctx, Message, MessageLen);
    RT_SHA256_End(&sha_ctx, DigestMessage);
} /* End of RT_SHA256 */
#endif /* SHA256_SUPPORT */


/* End of crypt_sha2.c */

