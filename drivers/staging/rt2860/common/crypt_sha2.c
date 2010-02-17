/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
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

#include "../crypt_sha2.h"

/* Basic operations */
#define SHR(x,n) (x >> n)	/* SHR(x)^n, right shift n bits , x is w-bit word, 0 <= n <= w */
#define ROTR(x,n,w) ((x >> n) | (x << (w - n)))	/* ROTR(x)^n, circular right shift n bits , x is w-bit word, 0 <= n <= w */
#define ROTL(x,n,w) ((x << n) | (x >> (w - n)))	/* ROTL(x)^n, circular left shift n bits , x is w-bit word, 0 <= n <= w */
#define ROTR32(x,n) ROTR(x,n,32)	/* 32 bits word */
#define ROTL32(x,n) ROTL(x,n,32)	/* 32 bits word */

/* Basic functions */
#define Ch(x,y,z) ((x & y) ^ ((~x) & z))
#define Maj(x,y,z) ((x & y) ^ (x & z) ^ (y & z))
#define Parity(x,y,z) (x ^ y ^ z)

#ifdef SHA1_SUPPORT
/* SHA1 constants */
#define SHA1_MASK 0x0000000f
static const u32 SHA1_K[4] = {
	0x5a827999UL, 0x6ed9eba1UL, 0x8f1bbcdcUL, 0xca62c1d6UL
};

static const u32 SHA1_DefaultHashValue[5] = {
	0x67452301UL, 0xefcdab89UL, 0x98badcfeUL, 0x10325476UL, 0xc3d2e1f0UL
};

/*
========================================================================
Routine Description:
    Initial struct rt_sha1_ctx

Arguments:
    pSHA_CTX        Pointer to struct rt_sha1_ctx

Return Value:
    None

Note:
    None
========================================================================
*/
void RT_SHA1_Init(struct rt_sha1_ctx *pSHA_CTX)
{
	NdisMoveMemory(pSHA_CTX->HashValue, SHA1_DefaultHashValue,
		       sizeof(SHA1_DefaultHashValue));
	NdisZeroMemory(pSHA_CTX->Block, SHA1_BLOCK_SIZE);
	pSHA_CTX->MessageLen = 0;
	pSHA_CTX->BlockLen = 0;
}				/* End of RT_SHA1_Init */

/*
========================================================================
Routine Description:
    SHA1 computation for one block (512 bits)

Arguments:
    pSHA_CTX        Pointer to struct rt_sha1_ctx

Return Value:
    None

Note:
    None
========================================================================
*/
void SHA1_Hash(struct rt_sha1_ctx *pSHA_CTX)
{
	u32 W_i, t, s;
	u32 W[16];
	u32 a, b, c, d, e, T, f_t = 0;

	/* Prepare the message schedule, {W_i}, 0 < t < 15 */
	NdisMoveMemory(W, pSHA_CTX->Block, SHA1_BLOCK_SIZE);
	for (W_i = 0; W_i < 16; W_i++)
		W[W_i] = cpu2be32(W[W_i]);	/* Endian Swap */
	/* End of for */

	/* SHA256 hash computation */
	/* Initialize the working variables */
	a = pSHA_CTX->HashValue[0];
	b = pSHA_CTX->HashValue[1];
	c = pSHA_CTX->HashValue[2];
	d = pSHA_CTX->HashValue[3];
	e = pSHA_CTX->HashValue[4];

	/* 80 rounds */
	for (t = 0; t < 80; t++) {
		s = t & SHA1_MASK;
		if (t > 15) {	/* Prepare the message schedule, {W_i}, 16 < t < 79 */
			W[s] =
			    (W[(s + 13) & SHA1_MASK]) ^ (W[(s + 8) & SHA1_MASK])
			    ^ (W[(s + 2) & SHA1_MASK]) ^ W[s];
			W[s] = ROTL32(W[s], 1);
		}		/* End of if */
		switch (t / 20) {
		case 0:
			f_t = Ch(b, c, d);
			break;
		case 1:
			f_t = Parity(b, c, d);
			break;
		case 2:
			f_t = Maj(b, c, d);
			break;
		case 3:
			f_t = Parity(b, c, d);
			break;
		}		/* End of switch */
		T = ROTL32(a, 5) + f_t + e + SHA1_K[t / 20] + W[s];
		e = d;
		d = c;
		c = ROTL32(b, 30);
		b = a;
		a = T;
	}			/* End of for */

	/* Compute the i^th intermediate hash value H^(i) */
	pSHA_CTX->HashValue[0] += a;
	pSHA_CTX->HashValue[1] += b;
	pSHA_CTX->HashValue[2] += c;
	pSHA_CTX->HashValue[3] += d;
	pSHA_CTX->HashValue[4] += e;

	NdisZeroMemory(pSHA_CTX->Block, SHA1_BLOCK_SIZE);
	pSHA_CTX->BlockLen = 0;
}				/* End of SHA1_Hash */

/*
========================================================================
Routine Description:
    The message is appended to block. If block size > 64 bytes, the SHA1_Hash
will be called.

Arguments:
    pSHA_CTX        Pointer to struct rt_sha1_ctx
    message         Message context
    messageLen      The length of message in bytes

Return Value:
    None

Note:
    None
========================================================================
*/
void SHA1_Append(struct rt_sha1_ctx *pSHA_CTX,
		 IN const u8 Message[], u32 MessageLen)
{
	u32 appendLen = 0;
	u32 diffLen = 0;

	while (appendLen != MessageLen) {
		diffLen = MessageLen - appendLen;
		if ((pSHA_CTX->BlockLen + diffLen) < SHA1_BLOCK_SIZE) {
			NdisMoveMemory(pSHA_CTX->Block + pSHA_CTX->BlockLen,
				       Message + appendLen, diffLen);
			pSHA_CTX->BlockLen += diffLen;
			appendLen += diffLen;
		} else {
			NdisMoveMemory(pSHA_CTX->Block + pSHA_CTX->BlockLen,
				       Message + appendLen,
				       SHA1_BLOCK_SIZE - pSHA_CTX->BlockLen);
			appendLen += (SHA1_BLOCK_SIZE - pSHA_CTX->BlockLen);
			pSHA_CTX->BlockLen = SHA1_BLOCK_SIZE;
			SHA1_Hash(pSHA_CTX);
		}		/* End of if */
	}			/* End of while */
	pSHA_CTX->MessageLen += MessageLen;
}				/* End of SHA1_Append */

/*
========================================================================
Routine Description:
    1. Append bit 1 to end of the message
    2. Append the length of message in rightmost 64 bits
    3. Transform the Hash Value to digest message

Arguments:
    pSHA_CTX        Pointer to struct rt_sha1_ctx

Return Value:
    digestMessage   Digest message

Note:
    None
========================================================================
*/
void SHA1_End(struct rt_sha1_ctx *pSHA_CTX, u8 DigestMessage[])
{
	u32 index;
	u64 message_length_bits;

	/* Append bit 1 to end of the message */
	NdisFillMemory(pSHA_CTX->Block + pSHA_CTX->BlockLen, 1, 0x80);

	/* 55 = 64 - 8 - 1: append 1 bit(1 byte) and message length (8 bytes) */
	if (pSHA_CTX->BlockLen > 55)
		SHA1_Hash(pSHA_CTX);
	/* End of if */

	/* Append the length of message in rightmost 64 bits */
	message_length_bits = pSHA_CTX->MessageLen * 8;
	message_length_bits = cpu2be64(message_length_bits);
	NdisMoveMemory(&pSHA_CTX->Block[56], &message_length_bits, 8);
	SHA1_Hash(pSHA_CTX);

	/* Return message digest, transform the u32 hash value to bytes */
	for (index = 0; index < 5; index++)
		pSHA_CTX->HashValue[index] =
		    cpu2be32(pSHA_CTX->HashValue[index]);
	/* End of for */
	NdisMoveMemory(DigestMessage, pSHA_CTX->HashValue, SHA1_DIGEST_SIZE);
}				/* End of SHA1_End */

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
void RT_SHA1(IN const u8 Message[],
	     u32 MessageLen, u8 DigestMessage[])
{

	struct rt_sha1_ctx sha_ctx;

	NdisZeroMemory(&sha_ctx, sizeof(struct rt_sha1_ctx));
	RT_SHA1_Init(&sha_ctx);
	SHA1_Append(&sha_ctx, Message, MessageLen);
	SHA1_End(&sha_ctx, DigestMessage);
}				/* End of RT_SHA1 */
#endif /* SHA1_SUPPORT */

/* End of crypt_sha2.c */
