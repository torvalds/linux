/** @file aes_cmac_rom.c
 *
 *  @brief This file defines aes cmac related function
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
#include "wl_macros.h"
#include "wltypes.h"
#include "hostsa_ext_def.h"
#include "authenticator.h"

#include "crypt_new_rom.h"
#include "aes_cmac_rom.h"

//#pragma diag_default 144
//#pragma arm section rwdata
// End - patch table entries

/* For CMAC Calculation */
static UINT8 const_Rb[16] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x87
};

void
mrvl_aes_128(UINT8 *key, UINT8 *input, UINT8 *output)
{
//    BOOLEAN weuEnabled;

#if 0
	if (IS_AEU_CLK_ENBLD()) {
		weuEnabled = TRUE;
	} else {
		weuEnabled = FALSE;
	}
#endif
	/* keyLen = 128 bit / 8 bits/byte / 8 for MRVL API encoding == 2 */
	MRVL_AesEncrypt(key, (128 / 8) / 8, input, output);

}

/* Basic Functions */
void
xor_128(UINT8 *a, UINT8 *b, UINT8 *out)
{
	int i;
	for (i = 0; i < 16; i++) {
		out[i] = a[i] ^ b[i];
	}
}

/* AES-CMAC Generation Function */

void
leftshift_onebit(UINT8 *input, UINT8 *output)
{
	int i;
	UINT8 overflow = 0;

	for (i = 15; i >= 0; i--) {
		output[i] = input[i] << 1;
		output[i] |= overflow;
		overflow = (input[i] & 0x80) ? 1 : 0;
	}
}

void
generate_subkey(phostsa_private priv, UINT8 *key, UINT8 *K1, UINT8 *K2)
{
	hostsa_util_fns *util_fns = &priv->util_fns;

	UINT8 L[16];
	UINT8 Z[16];
	UINT8 tmp[16];

	memset(util_fns, Z, 0x00, sizeof(Z));

	mrvl_aes_128(key, Z, L);

	if ((L[0] & 0x80) == 0) {
		/* If MSB(L) = 0, then K1 = L << 1 */
		leftshift_onebit(L, K1);
	} else {
		/* Else K1 = ( L << 1 ) (+) Rb */
		leftshift_onebit(L, tmp);
		xor_128(tmp, const_Rb, K1);
	}

	if ((K1[0] & 0x80) == 0) {
		leftshift_onebit(K1, K2);
	} else {
		leftshift_onebit(K1, tmp);
		xor_128(tmp, const_Rb, K2);
	}
}

void
padding(UINT8 *lastb, UINT8 *pad, int length)
{
	int j;

	/* original last block */
	for (j = 0; j < 16; j++) {
		if (j < length) {
			pad[j] = lastb[j];
		} else if (j == length) {
			pad[j] = 0x80;
		} else {
			pad[j] = 0x00;
		}
	}
}

void
mrvl_aes_cmac(phostsa_private priv, UINT8 *key, UINT8 *input, int length,
	      UINT8 *mac)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	UINT8 X[16];
	UINT8 Y[16];
	UINT8 M_last[16];
	UINT8 padded[16];
	UINT8 K1[16];
	UINT8 K2[16];
	int n, i, flag;

	generate_subkey(priv, key, K1, K2);

	n = (length + 15) / 16;	/* n is number of rounds */

	if (n == 0) {
		n = 1;
		flag = 0;
	} else {
		if ((length % 16) == 0) {
			/* last block is a complete block */
			flag = 1;
		} else {
			/* last block is not complete block */
			flag = 0;
		}
	}

	if (flag) {
		/* last block is complete block */
		xor_128(&input[16 * (n - 1)], K1, M_last);
	} else {
		padding(&input[16 * (n - 1)], padded, length % 16);
		xor_128(padded, K2, M_last);
	}

	memset(util_fns, X, 0x00, sizeof(X));

	for (i = 0; i < n - 1; i++) {
		xor_128(X, &input[16 * i], Y);	/* Y := Mi (+) X  */
		mrvl_aes_128(key, Y, X);	/* X := AES-128(KEY, Y); */
	}

	xor_128(X, M_last, Y);
	mrvl_aes_128(key, Y, X);

	for (i = 0; i < 16; i++) {
		mac[i] = X[i];
	}
}
