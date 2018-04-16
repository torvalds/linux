/** @file sha_256.c
 *
 *  @brief This file defines the SHA256 hash implementation and interface functions
 *
 * Copyright (C) 2014-2017, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

/******************************************************
Change log:
    03/07/2014: Initial version
******************************************************/
/*
 * SHA-256 hash implementation and interface functions
 *
 * Copyright ?2003-2006, Jouni Malinen <jkmaline@cc.hut.fi>
 *
 * Copyright ?2006-2007, Marvell International Ltd. and its affiliates
 * All rights reserved.
 *
 * 1. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 2. Neither the name of Jouni Malinen, Marvell nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ''AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS  INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "sha_256.h"
#include "hostsa_ext_def.h"
#include "authenticator.h"

#define WPA_GET_BE32(a) ((((UINT32) (a)[0]) << 24) |                    \
                         (((UINT32) (a)[1]) << 16) |                    \
                         (((UINT32) (a)[2]) << 8)  |                    \
                         ((UINT32) (a)[3]))

#define WPA_PUT_BE32(a, val)                          \
    do {                                              \
        (a)[0] = (UINT8) (((UINT32) (val)) >> 24);    \
        (a)[1] = (UINT8) (((UINT32) (val)) >> 16);    \
        (a)[2] = (UINT8) (((UINT32) (val)) >> 8);     \
        (a)[3] = (UINT8) (((UINT32) (val)) & 0xff);   \
    } while (0)

#define WPA_PUT_BE64(a, val)                          \
    do {                                              \
        (a)[0] = (UINT8) (((UINT64) (val)) >> 56);    \
        (a)[1] = (UINT8) (((UINT64) (val)) >> 48);    \
        (a)[2] = (UINT8) (((UINT64) (val)) >> 40);    \
        (a)[3] = (UINT8) (((UINT64) (val)) >> 32);    \
        (a)[4] = (UINT8) (((UINT64) (val)) >> 24);    \
        (a)[5] = (UINT8) (((UINT64) (val)) >> 16);    \
        (a)[6] = (UINT8) (((UINT64) (val)) >> 8);     \
        (a)[7] = (UINT8) (((UINT64) (val)) & 0xff);   \
    } while (0)

/**
 * @brief	hmac_sha256_vector - HMAC-SHA256 over data vector (RFC 2104)
 * @param priv	pointer to previous element
 * @param key: Key for HMAC operations
 * @param key_len: Length of the key in bytes
 * @param num_elem: Number of elements in the data vector; including [0] spare
 * @param addr: Pointers to the data areas, [0] element must be left as spare
 * @param len: Lengths of the data blocks, [0] element must be left as spare
 * @param mac: Buffer for the hash (32 bytes)
 * @param pScratchMem: Scratch Memory; At least a 492 byte buffer.
 */
void
hmac_sha256_vector(void *priv, UINT8 *key,
		   size_t key_len,
		   size_t num_elem,
		   UINT8 *addr[], size_t * len, UINT8 *mac, UINT8 *pScratchMem)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	size_t i;
	UINT8 *pKpad;		/* was UINT8 k_pad[64], padding - key XORd with ipad/opad */
	UINT8 *pTk;		/* was UINT8 tk[32] */
	UINT8 *pTmpBuf;
	UINT32 *ptrU32;

	pKpad = pScratchMem;	/* kpad = 64 bytes */
	pTk = pKpad + 64;	/* tk = 32 bytes */
	pTmpBuf = pTk + 32;	/* offset into the scratch buf = +96 bytes */

	/* if key is longer than 64 bytes reset it to key = SHA256(key) */
	if (key_len > 64) {
		/* pTmpBuf = At least 396 bytes */
		sha256_vector(priv, 1, &key, &key_len, pTk, pTmpBuf);
		key = pTk;
		key_len = 32;
	}

	/* the HMAC_SHA256 transform looks like:
	 *
	 * SHA256(K XOR opad, SHA256(K XOR ipad, text))
	 *
	 * where K is an n byte key
	 * ipad is the byte 0x36 repeated 64 times
	 * opad is the byte 0x5c repeated 64 times
	 * and text is the data being protected
	 */

	/* start out by storing key in ipad */
	memset(util_fns, pKpad, 0x00, 64);
	memcpy(util_fns, pKpad, key, key_len);

	/* XOR key with ipad values */
	ptrU32 = (UINT32 *)pKpad;
	for (i = 16; i > 0; i--) {
		*ptrU32++ ^= 0x36363636;
	}

	/* perform inner SHA256 */
	addr[0] = pKpad;
	len[0] = 64;

	/* pTmpBuf = At least 396 bytes */
	sha256_vector((void *)priv, num_elem, addr, len, mac, pTmpBuf);
	memset(util_fns, pKpad, 0x00, 64);
	memcpy(util_fns, pKpad, key, key_len);

	/* XOR key with opad values */
	ptrU32 = (UINT32 *)pKpad;
	for (i = 16; i > 0; i--) {
		*ptrU32++ ^= 0x5C5C5C5C;
	}

	/* perform outer SHA256 */
	addr[0] = pKpad;
	len[0] = 64;
	addr[1] = mac;
	len[1] = SHA256_MAC_LEN;

	/* pTmpBuf = At least 396 bytes */
	sha256_vector((void *)priv, 2, addr, len, mac, pTmpBuf);
}

static int sha256_process(void *priv, struct sha256_state *md,
			  const UINT8 *in,
			  unsigned int inlen, UINT8 *pScratchMem);

static int sha256_done(void *priv, struct sha256_state *md,
		       UINT8 *out, UINT8 *pScratchMem);

/**
 * @brief 	sha256_vector - SHA256 hash for data vector
 * @param priv	pointer to previous element
 * @param num_elem Number of elements in the data vector
 * @param addr Pointers to the data areas
 * @param len Lengths of the data blocks
 * @param mac Buffer for the hash
 * @param pScratchMem Scratch memory; At least (108 + 288) = 396 bytes  */
void
sha256_vector(void *priv, size_t num_elem,
	      UINT8 *addr[], size_t * len, UINT8 *mac, UINT8 *pScratchMem)
{
	UINT8 *pTmpBuf;
	size_t i;
	struct sha256_state *pCtx;

	/*
	 ** sizeof(struct sha256_state)
	 **
	 **    UINT64 length                      =  8
	 **    UINT32 state[8], curlen; = (9 * 4) = 36
	 **    UINT8 buf[64];                     = 64
	 **                                       -----
	 **                                        108
	 */
	pCtx = (struct sha256_state *)pScratchMem;
	pTmpBuf = pScratchMem + sizeof(struct sha256_state);

	sha256_init(pCtx);

	for (i = 0; i < num_elem; i++) {
		/* pTmpBuf = At least 288 bytes of memory */
		sha256_process((void *)priv, pCtx, addr[i], len[i], pTmpBuf);
	}
	sha256_done((void *)priv, pCtx, mac, pTmpBuf);
}

/* ===== start - public domain SHA256 implementation ===== */

/* This is based on SHA256 implementation in LibTomCrypt that was released into
 * public domain by Tom St Denis. */

/* the K array */
static const unsigned int K[64] = {
	0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL,
	0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL, 0xd807aa98UL, 0x12835b01UL,
	0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL,
	0xc19bf174UL, 0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
	0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL, 0x983e5152UL,
	0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL,
	0x06ca6351UL, 0x14292967UL, 0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL,
	0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
	0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL,
	0xd6990624UL, 0xf40e3585UL, 0x106aa070UL, 0x19a4c116UL, 0x1e376c08UL,
	0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL,
	0x682e6ff3UL, 0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
	0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};

/* Various logical functions */
#define RORc(x, y)                                                      \
    (((((unsigned int)(x) & 0xFFFFFFFFUL) >> (unsigned int)((y) & 31)) | \
      ((unsigned int)(x) << (unsigned int)(32 - ((y) & 31)))) & 0xFFFFFFFFUL)

#define Ch(x,y,z)       (z ^ (x & (y ^ z)))
#define Maj(x,y,z)      (((x | y) & z) | (x & y))
#define S(x, n)         RORc((x), (n))
#define R(x, n)         (((x)&0xFFFFFFFFUL)>>(n))
#define Sigma0(x)       (S(x, 2) ^ S(x, 13) ^ S(x, 22))
#define Sigma1(x)       (S(x, 6) ^ S(x, 11) ^ S(x, 25))
#define Gamma0(x)       (S(x, 7) ^ S(x, 18) ^ R(x, 3))
#define Gamma1(x)       (S(x, 17) ^ S(x, 19) ^ R(x, 10))
#ifndef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

/**
 * sha256_compress - Compress 512-bits.
 * @param md: Pointer to the element holding hash state.
 * @param msgBuf: Pointer to the buffer containing the data to be hashed.
 * @param pScratchMem: Scratch memory; At least 288 bytes of free memory *
 *
 */
int
sha256_compress(void *priv, struct sha256_state *md,
		UINT8 *msgBuf, UINT8 *pScratchMem)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	UINT32 *pW;		/* was UINT32 W[64] */
	UINT32 *pS;		/* was UINT32 S[8]  */
	UINT32 t0;
	UINT32 t1;
	UINT32 t;
	UINT32 i;
	UINT32 *ptrU32;

	/*   pW = (64 * 4) = 256
	 **   pS = (8 * 4)  =  32
	 **                 -----
	 **                   288
	 */
	ptrU32 = pW = (UINT32 *)pScratchMem;
	pS = pW + 64;

	/* copy state into S */
	memcpy(util_fns, (UINT8 *)pS, (UINT8 *)md->state, 32);

	/* copy the a message block of 512-bits into pW[0..15] */
	for (i = 16; i > 0; i--) {
		int a0, a1;
		a0 = *msgBuf++;
		a1 = *msgBuf++;
		a0 <<= 8;
		a0 |= a1;
		a1 = *msgBuf++;
		a0 <<= 8;
		a0 |= a1;
		a1 = *msgBuf++;
		a0 <<= 8;
		*ptrU32++ = a0 | a1;
	}

	/* fill pW[16..63] */
	for (i = 48; i > 0; i--) {
		*ptrU32 =
			(Gamma1(ptrU32[-2]) + ptrU32[-7] + Gamma0(ptrU32[-15]) +
			 ptrU32[-16]);
		ptrU32++;
	}

	/* Compress */
#define RND(a,b,c,d,e,f,g,h,i)                       \
    t0 = h + Sigma1(e) + Ch(e, f, g) + K[i] + pW[i]; \
    t1 = Sigma0(a) + Maj(a, b, c);                   \
    d += t0;                                         \
    h  = t0 + t1;

	for (i = 0; i < 64; ++i) {
		RND(pS[0], pS[1], pS[2], pS[3], pS[4], pS[5], pS[6], pS[7], i);
		t = pS[7];
		pS[7] = pS[6];
		pS[6] = pS[5];
		pS[5] = pS[4];
		pS[4] = pS[3];
		pS[3] = pS[2];
		pS[2] = pS[1];
		pS[1] = pS[0];
		pS[0] = t;
	}

	/* feedback */
	for (i = 0; i < 8; i++) {
		md->state[i] = md->state[i] + pS[i];
	}
	return 0;
}

/* Initialize the hash state */
void
sha256_init(struct sha256_state *md)
{
	md->curlen = 0;
	md->length = 0;
	md->state[0] = 0x6A09E667UL;
	md->state[1] = 0xBB67AE85UL;
	md->state[2] = 0x3C6EF372UL;
	md->state[3] = 0xA54FF53AUL;
	md->state[4] = 0x510E527FUL;
	md->state[5] = 0x9B05688CUL;
	md->state[6] = 0x1F83D9ABUL;
	md->state[7] = 0x5BE0CD19UL;
}

/**
   Process a block of memory though the hash
   @param md     The hash state
   @param in     The data to hash
   @param inlen  The length of the data (octets)
   @param pScratchMem Temporary Memory Buf; At least 288 bytes.
   @return CRYPT_OK if successful
*/
static int
sha256_process(void *priv, struct sha256_state *md,
	       const unsigned char *in, unsigned int inlen, UINT8 *pScratchMem)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	unsigned int n;
#define block_size 64

	if (md->curlen > sizeof(md->buf)) {
		return -1;
	}

	while (inlen > 0) {
		if (md->curlen == 0 && inlen >= block_size) {
			/* pScratchMem = At least 288 bytes of memory */
			if (sha256_compress
			    ((void *)priv, md, (UINT8 *)in, pScratchMem) < 0) {
				return -1;
			}
			md->length += block_size * 8;
			in += block_size;
			inlen -= block_size;
		} else {
			n = MIN(inlen, (block_size - md->curlen));
			memcpy(util_fns, md->buf + md->curlen, in, n);
			md->curlen += n;
			in += n;
			inlen -= n;
			if (md->curlen == block_size) {
				/* pScratchMem = At least 288 bytes of memory */
				if (sha256_compress
				    ((void *)priv, md, md->buf,
				     pScratchMem) < 0) {
					return -1;
				}
				md->length += 8 * block_size;
				md->curlen = 0;
			}
		}
	}

	return 0;
}

/**
   Terminate the hash to get the digest
   @param md  The hash state
   @param out [out] The destination of the hash (32 bytes)
   @param pScratchMem [in] Scratch memory; At least 288 bytes
   @return CRYPT_OK if successful
*/
static int
sha256_done(void *priv, struct sha256_state *md, UINT8 *out, UINT8 *pScratchMem)
{
	int i;
	UINT32 *ptrU32;
	UINT32 tmpU32;

	if (md->curlen >= sizeof(md->buf)) {
		return -1;
	}

	/* increase the length of the message */
	md->length += md->curlen * 8;

	/* append the '1' bit */
	md->buf[md->curlen++] = (unsigned char)0x80;

	/* if the length is currently above 56 bytes we append zeros
	 * then compress.  Then we can fall back to padding zeros and length
	 * encoding like normal.
	 */
	if (md->curlen > 56) {
		while (md->curlen < 64) {
			md->buf[md->curlen++] = (unsigned char)0;
		}
		/* pScratchMem = At least 288 bytes of memory */
		sha256_compress((void *)priv, md, md->buf, pScratchMem);
		md->curlen = 0;
	}

	/* pad upto 56 bytes of zeroes */
	while (md->curlen < 56) {
		md->buf[md->curlen++] = (unsigned char)0;
	}

	/* store length */
	ptrU32 = (UINT32 *)&md->length;
	for (i = 0; i < 2; i++) {
		tmpU32 = *ptrU32++;
		WPA_PUT_BE32(md->buf + 60 - 4 * i, tmpU32);
	}

	/* pScratchMem = At least 288 bytes of memory */
	sha256_compress((void *)priv, md, md->buf, pScratchMem);

	ptrU32 = md->state;
	/* copy output */
	for (i = 8; i > 0; i--) {
		tmpU32 = *ptrU32++;
		WPA_PUT_BE32(out, tmpU32);
		out += sizeof(UINT32);
	}
	return 0;
}

/* ===== end - public domain SHA256 implementation ===== */
