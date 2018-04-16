/** @file aes_cmac_rom.h
 *
 *  @brief This file contains the define for aes_cmac_rom
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
#ifndef _AES_CMAC_ROM_H_
#define _AES_CMAC_ROM_H_

#include "wltypes.h"

extern BOOLEAN (*mrvl_aes_cmac_hook) (UINT8 *key, UINT8 *input, int length,
				      UINT8 *mac);
extern void mrvl_aes_cmac(phostsa_private priv, UINT8 *key, UINT8 *input,
			  int length, UINT8 *mac);

extern BOOLEAN (*mrvl_aes_128_hook) (UINT8 *key, UINT8 *input, UINT8 *output);
extern void mrvl_aes_128(UINT8 *key, UINT8 *input, UINT8 *output);

/* RAM Linkages */
extern void (*rom_hal_EnableWEU) (void);
extern void (*rom_hal_DisableWEU) (void);

extern void xor_128(UINT8 *a, UINT8 *b, UINT8 *out);
extern void leftshift_onebit(UINT8 *input, UINT8 *output);
extern void padding(UINT8 *lastb, UINT8 *pad, int length);

#endif
