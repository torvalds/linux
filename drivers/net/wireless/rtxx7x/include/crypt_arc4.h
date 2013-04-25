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


#ifndef __CRYPT_ARC4_H__
#define __CRYPT_ARC4_H__

#include "rt_config.h"

/* ARC4 definition & structure */
#define ARC4_KEY_BLOCK_SIZE 256

typedef struct {
	UINT BlockIndex1;
	UINT BlockIndex2;
	UINT8 KeyBlock[256];
} ARC4_CTX_STRUC, *PARC4_CTX_STRUC;

/* ARC4 operations */
VOID ARC4_INIT(
	IN ARC4_CTX_STRUC * pARC4_CTX,
	IN PUCHAR pKey,
	IN UINT KeyLength);

VOID ARC4_Compute(
	IN ARC4_CTX_STRUC * pARC4_CTX,
	IN UINT8 InputBlock[],
	IN UINT InputBlockSize,
	OUT UINT8 OutputBlock[]);

VOID ARC4_Discard_KeyLength(
	IN ARC4_CTX_STRUC * pARC4_CTX,
	IN UINT Length);

#endif /* __CRYPT_ARC4_H__ */
