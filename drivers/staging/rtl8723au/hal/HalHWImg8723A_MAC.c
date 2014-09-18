/******************************************************************************
*
* Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
******************************************************************************/

#include "odm_precomp.h"

static bool CheckCondition(const u32  Condition, const u32  Hex)
{
	u32 _board     = (Hex & 0x000000FF);
	u32 _interface = (Hex & 0x0000FF00) >> 8;
	u32 _platform  = (Hex & 0x00FF0000) >> 16;
	u32 cond = Condition;

	if (Condition == 0xCDCDCDCD)
		return true;

	cond = Condition & 0x000000FF;
	if ((_board == cond) && cond != 0x00)
		return false;

	cond = Condition & 0x0000FF00;
	cond = cond >> 8;
	if ((_interface & cond) == 0 && cond != 0x07)
		return false;

	cond = Condition & 0x00FF0000;
	cond = cond >> 16;
	if ((_platform & cond) == 0 && cond != 0x0F)
		return false;
	return true;
}

/******************************************************************************
*                           MAC_REG.TXT
******************************************************************************/

static u32 Array_MAC_REG_8723A[] = {
		0x420, 0x00000080,
		0x423, 0x00000000,
		0x430, 0x00000000,
		0x431, 0x00000000,
		0x432, 0x00000000,
		0x433, 0x00000001,
		0x434, 0x00000004,
		0x435, 0x00000005,
		0x436, 0x00000006,
		0x437, 0x00000007,
		0x438, 0x00000000,
		0x439, 0x00000000,
		0x43A, 0x00000000,
		0x43B, 0x00000001,
		0x43C, 0x00000004,
		0x43D, 0x00000005,
		0x43E, 0x00000006,
		0x43F, 0x00000007,
		0x440, 0x0000005D,
		0x441, 0x00000001,
		0x442, 0x00000000,
		0x444, 0x00000015,
		0x445, 0x000000F0,
		0x446, 0x0000000F,
		0x447, 0x00000000,
		0x458, 0x00000041,
		0x459, 0x000000A8,
		0x45A, 0x00000072,
		0x45B, 0x000000B9,
		0x460, 0x00000066,
		0x461, 0x00000066,
		0x462, 0x00000008,
		0x463, 0x00000003,
		0x4C8, 0x000000FF,
		0x4C9, 0x00000008,
		0x4CC, 0x000000FF,
		0x4CD, 0x000000FF,
		0x4CE, 0x00000001,
		0x500, 0x00000026,
		0x501, 0x000000A2,
		0x502, 0x0000002F,
		0x503, 0x00000000,
		0x504, 0x00000028,
		0x505, 0x000000A3,
		0x506, 0x0000005E,
		0x507, 0x00000000,
		0x508, 0x0000002B,
		0x509, 0x000000A4,
		0x50A, 0x0000005E,
		0x50B, 0x00000000,
		0x50C, 0x0000004F,
		0x50D, 0x000000A4,
		0x50E, 0x00000000,
		0x50F, 0x00000000,
		0x512, 0x0000001C,
		0x514, 0x0000000A,
		0x515, 0x00000010,
		0x516, 0x0000000A,
		0x517, 0x00000010,
		0x51A, 0x00000016,
		0x524, 0x0000000F,
		0x525, 0x0000004F,
		0x546, 0x00000040,
		0x547, 0x00000000,
		0x550, 0x00000010,
		0x551, 0x00000010,
		0x559, 0x00000002,
		0x55A, 0x00000002,
		0x55D, 0x000000FF,
		0x605, 0x00000030,
		0x608, 0x0000000E,
		0x609, 0x0000002A,
		0x652, 0x00000020,
		0x63C, 0x0000000A,
		0x63D, 0x0000000A,
		0x63E, 0x0000000E,
		0x63F, 0x0000000E,
		0x66E, 0x00000005,
		0x700, 0x00000021,
		0x701, 0x00000043,
		0x702, 0x00000065,
		0x703, 0x00000087,
		0x708, 0x00000021,
		0x709, 0x00000043,
		0x70A, 0x00000065,
		0x70B, 0x00000087,
};

void ODM_ReadAndConfig_MAC_REG_8723A(struct dm_odm_t *pDM_Odm)
{
	#define READ_NEXT_PAIR(v1, v2, i)			\
		 do {						\
			i += 2; v1 = Array[i]; v2 = Array[i+1];	\
		 } while (0)

	u32     hex         = 0;
	u32     i           = 0;
	u8     platform    = 0x04;
	u8     interfaceValue   = pDM_Odm->SupportInterface;
	u8     board       = pDM_Odm->BoardType;
	u32     ArrayLen    = sizeof(Array_MAC_REG_8723A)/sizeof(u32);
	u32 *Array       = Array_MAC_REG_8723A;

	hex += board;
	hex += interfaceValue << 8;
	hex += platform << 16;
	hex += 0xFF000000;
	for (i = 0; i < ArrayLen; i += 2) {
		u32 v1 = Array[i];
		u32 v2 = Array[i+1];

		/*  This (offset, data) pair meets the condition. */
		if (v1 < 0xCDCDCDCD) {
			odm_ConfigMAC_8723A(pDM_Odm, v1, (u8)v2);
			continue;
		} else {
			if (!CheckCondition(Array[i], hex)) {
				/* Discard the following (offset, data) pairs. */
				READ_NEXT_PAIR(v1, v2, i);
				while (v2 != 0xDEAD &&
				       v2 != 0xCDEF &&
				       v2 != 0xCDCD && i < ArrayLen - 2)
					READ_NEXT_PAIR(v1, v2, i);
				i -= 2; /*  prevent from for-loop += 2 */
			} else {
				/*  Configure matched pairs and skip to end of if-else. */
				READ_NEXT_PAIR(v1, v2, i);
				while (v2 != 0xDEAD &&
				       v2 != 0xCDEF &&
				       v2 != 0xCDCD && i < ArrayLen - 2) {
					odm_ConfigMAC_8723A(pDM_Odm, v1, (u8)v2);
					READ_NEXT_PAIR(v1, v2, i);
				}

				while (v2 != 0xDEAD && i < ArrayLen - 2)
					READ_NEXT_PAIR(v1, v2, i);
			}
		}
	}
}
