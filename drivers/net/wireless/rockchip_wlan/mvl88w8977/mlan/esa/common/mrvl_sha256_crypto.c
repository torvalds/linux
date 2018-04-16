/** @file Mrvl_sha256_crypto.c
 *
 *  @brief This file  defines sha256 crypto
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
#include <linux/string.h>

#include "wltypes.h"
#include "wl_macros.h"

#include "sha_256.h"
#include "mrvl_sha256_crypto.h"

#include "hostsa_ext_def.h"
#include "authenticator.h"

/* Helper function to allocate scratch memory and call sha256_vector() */
void
mrvl_sha256_crypto_vector(void *priv, size_t num_elem,
			  UINT8 *addr[], size_t * len, UINT8 *mac)
{

	//BufferDesc_t* pBufDesc = NULL;
	UINT8 buf[SHA256_MIN_SCRATCH_BUF] = { 0 };

	sha256_vector((void *)priv, num_elem, addr, len, mac, (UINT8 *)buf);

//    bml_FreeBuffer((UINT32)pBufDesc);
}

void
mrvl_sha256_crypto_kdf(void *priv, UINT8 *pKey,
		       UINT8 key_len,
		       char *label,
		       UINT8 label_len,
		       UINT8 *pContext,
		       UINT16 context_len, UINT8 *pOutput, UINT16 output_len)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	UINT8 *vectors[4 + 1];
	size_t vectLen[NELEMENTS(vectors)];
	UINT8 *pResult;
	UINT8 *pLoopResult;
	UINT8 iterations;
	UINT16 i;
	//BufferDesc_t* pBufDesc = NULL;
	UINT8 buf[HMAC_SHA256_MIN_SCRATCH_BUF] = { 0 };

	pResult = pContext + context_len;

	/*
	 ** Working memory layout:
	 **            | KDF-Len output --- >
	 **            |
	 ** [KDF Input][HMAC output#1][HMAC output#2][...]
	 **
	 */

	/* Number of SHA256 digests needed to meet the bit length output_len */
	iterations = (output_len + 255) / 256;

	pLoopResult = pResult;

	for (i = 1; i <= iterations; i++) {
		/* Skip vectors[0]; Used internally in hmac_sha256_vector function */
		vectors[1] = (UINT8 *)&i;
		vectLen[1] = sizeof(i);

		vectors[2] = (UINT8 *)label;
		vectLen[2] = label_len;

		vectors[3] = pContext;
		vectLen[3] = context_len;

		vectors[4] = (UINT8 *)&output_len;
		vectLen[4] = sizeof(output_len);

		/*
		 **
		 **  KDF input = (K, i || label || Context || Length)
		 **
		 */
		hmac_sha256_vector(priv, pKey, key_len,
				   NELEMENTS(vectors), vectors, vectLen,
				   pLoopResult, (UINT8 *)buf);

		/* Move the hmac output pointer so another digest can be appended
		 **  if more loop iterations are needed to get the output_len key
		 **  bit total
		 */
		pLoopResult += SHA256_MAC_LEN;
	}

//    bml_FreeBuffer((UINT32)pBufDesc);

	memcpy(util_fns, pOutput, pResult, output_len / 8);

}
