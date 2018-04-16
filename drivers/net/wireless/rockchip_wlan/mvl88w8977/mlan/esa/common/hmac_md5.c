/** @file hmac_md5.c
 *
 *  @brief This file defines algorithm for hmac md5
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

#include "wltypes.h"
#include "md5.h"

#include "hostsa_ext_def.h"
#include "authenticator.h"

void
Mrvl_hmac_md5(void *priv, UINT8 *text_data, int text_len, UINT8 *key,
	      int key_len, void *digest)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	//BufferDesc_t * pDesc = NULL;
	UINT8 buf[400] = { 0 };
	Mrvl_MD5_CTX *context;
	unsigned char *k_pad;	/* padding - key XORd with i/opad */
	int i;
#if 0
	/* Wait forever ensures a buffer */
	pDesc = (BufferDesc_t *) bml_AllocBuffer(ramHook_encrPoolConfig,
						 400, BML_WAIT_FOREVER);
#endif
	/* WLAN buffers are aligned, so k_pad start at a UINT32 boundary */
	//k_pad = (unsigned char *)BML_DATA_PTR(pDesc);
	k_pad = (unsigned char *)buf;
	context = (Mrvl_MD5_CTX *)(k_pad + 64);

	/* if key is longer than 64 bytes reset it to key=MD5(key) */
	if (key_len > 64) {
		Mrvl_MD5_CTX tctx;

		wpa_MD5Init(&tctx);
		wpa_MD5Update((void *)priv, &tctx, key, key_len);
		wpa_MD5Final((void *)priv, context->buffer, &tctx);

		key = context->buffer;
		key_len = 16;
	}

	/* the HMAC_MD5 transform looks like: */
	/* */
	/*  MD5(K XOR opad, MD5(K XOR ipad, text)) */
	/* */
	/* where K is an n byte key */
	/* ipad is the byte 0x36 repeated 64 times */
	/* opad is the byte 0x5c repeated 64 times */
	/* and text is the data being protected */

	/* start out by storing key in pads */
	memset(util_fns, k_pad, 0, 64);
	memcpy(util_fns, k_pad, key, key_len);

	/* XOR key with ipad and opad values */
	for (i = 0; i < 16; i++) {
		((UINT32 *)k_pad)[i] ^= 0x36363636;
	}

	/* perform inner MD5 */
	wpa_MD5Init(context);	/* init context for 1st pass */
	wpa_MD5Update((void *)priv, context, k_pad, 64);	/* start with inner pad */
	wpa_MD5Update((void *)priv, context, text_data, text_len);	/* then text of datagram */
	wpa_MD5Final((void *)priv, digest, context);	/* finish up 1st pass */

	/* start out by storing key in pads */
	memset(util_fns, k_pad, 0, 64);
	memcpy(util_fns, k_pad, key, key_len);

	for (i = 0; i < 16; i++) {
		((UINT32 *)k_pad)[i] ^= 0x5c5c5c5c;
	}

	/* perform outer MD5 */
	wpa_MD5Init(context);	/* init context for 2nd pass */
	wpa_MD5Update((void *)priv, context, k_pad, 64);
	/* start with outer pad */
	wpa_MD5Update((void *)priv, context, digest, 16);
	/* then results of 1st hash */
	wpa_MD5Final((void *)priv, digest, context);	/* finish up 2nd pass */

//    bml_FreeBuffer((UINT32)pDesc);
}
