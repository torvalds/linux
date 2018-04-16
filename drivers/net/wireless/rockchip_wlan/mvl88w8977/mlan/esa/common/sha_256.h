/** @file sha_256.h
 *
 *  @brief This file contains the SHA256 hash implementation and interface functions
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
#ifndef SHA256_H
#define SHA256_H

#include "wltypes.h"

#define SHA256_MAC_LEN                 32
#define HMAC_SHA256_MIN_SCRATCH_BUF   (500)
#define SHA256_MIN_SCRATCH_BUF        (400)

struct sha256_state {
	UINT64 length;
	UINT32 state[8], curlen;
	UINT8 buf[64];
};

void sha256_init(struct sha256_state *md);

/**
 * @brief	sha256_compress - Compress 512-bits.
 * @param priv	pointer to previous element
 * @param md: Pointer to the element holding hash state.
 * @param msgBuf: Pointer to the buffer containing the data to be hashed.
 * @param pScratchMem: Scratch memory; At least 288 bytes of free memory *
 *
 */
int sha256_compress(void *priv, struct sha256_state *md,
		    UINT8 *msgBuf, UINT8 *pScratchMem);

/**
 * sha256_vector - SHA256 hash for data vector
 * @param num_elem: Number of elements in the data vector
 * @param addr: Pointers to the data areas
 * @param len: Lengths of the data blocks
 * @param mac: Buffer for the hash
 * @param pScratchMem: Scratch memory; Buffer of SHA256_MIN_SCRATCH_BUF size
 */
void sha256_vector(void *priv, size_t num_elem,
		   UINT8 *addr[], size_t * len, UINT8 *mac, UINT8 *pScratchMem);

/**
 * hmac_sha256_vector - HMAC-SHA256 over data vector (RFC 2104)
 * @param key: Key for HMAC operations
 * @param key_len: Length of the key in bytes
 * @param num_elem: Number of elements in the data vector; including [0]
 * @param addr: Pointers to the data areas, [0] element must be left as spare
 * @param len: Lengths of the data blocks, [0] element must be left as spare
 * @param mac: Buffer for the hash (32 bytes)
 * @param pScratchMem: Scratch Memory; Buffer of HMAC_SHA256_MIN_SCRATCH_BUF size
 */
void hmac_sha256_vector(void *priv, UINT8 *key,
			size_t key_len,
			size_t num_elem,
			UINT8 *addr[],
			size_t * len, UINT8 *mac, UINT8 *pScratchMem);

#endif /* SHA256_H */
