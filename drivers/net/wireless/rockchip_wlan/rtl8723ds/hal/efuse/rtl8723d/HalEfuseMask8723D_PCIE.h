/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/

/******************************************************************************
*                           MPCIE.TXT
******************************************************************************/


u2Byte
EFUSE_GetArrayLen_MP_8723D_MPCIE(VOID);

VOID
EFUSE_GetMaskArray_MP_8723D_MPCIE(
	IN	OUT pu1Byte Array
);

BOOLEAN
EFUSE_IsAddressMasked_MP_8723D_MPCIE(/* TC: Test Chip, MP: MP Chip */
	IN   u2Byte  Offset
);
