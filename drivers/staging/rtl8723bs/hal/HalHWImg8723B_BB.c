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

static bool CheckPositive(
	PDM_ODM_T pDM_Odm, const u32 Condition1, const u32 Condition2
)
{
	u8 _BoardType =
		((pDM_Odm->BoardType & BIT4) >> 4) << 0 | /*  _GLNA */
		((pDM_Odm->BoardType & BIT3) >> 3) << 1 | /*  _GPA */
		((pDM_Odm->BoardType & BIT7) >> 7) << 2 | /*  _ALNA */
		((pDM_Odm->BoardType & BIT6) >> 6) << 3 | /*  _APA */
		((pDM_Odm->BoardType & BIT2) >> 2) << 4;  /*  _BT */

	u32 cond1 = Condition1, cond2 = Condition2;
	u32 driver1 =
		pDM_Odm->CutVersion << 24 |
		pDM_Odm->SupportPlatform << 16 |
		pDM_Odm->PackageType << 12 |
		pDM_Odm->SupportInterface << 8  |
		_BoardType;

	u32 driver2 =
		pDM_Odm->TypeGLNA << 0 |
		pDM_Odm->TypeGPA << 8 |
		pDM_Odm->TypeALNA << 16 |
		pDM_Odm->TypeAPA << 24;

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_INIT,
		ODM_DBG_TRACE,
		(
			"===> [8812A] CheckPositive (cond1, cond2) = (0x%X 0x%X)\n",
			cond1,
			cond2
		)
	);
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_INIT,
		ODM_DBG_TRACE,
		(
			"===> [8812A] CheckPositive (driver1, driver2) = (0x%X 0x%X)\n",
			driver1,
			driver2
		)
	);

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_INIT,
		ODM_DBG_TRACE,
		("	(Platform, Interface) = (0x%X, 0x%X)\n",
			pDM_Odm->SupportPlatform,
			pDM_Odm->SupportInterface
		)
	);
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_INIT,
		ODM_DBG_TRACE,
		(
			"	(Board, Package) = (0x%X, 0x%X)\n",
			pDM_Odm->BoardType,
			pDM_Odm->PackageType
		)
	);


	/*  Value Defined Check =============== */
	/* QFN Type [15:12] and Cut Version [27:24] need to do value check */

	if (((cond1 & 0x0000F000) != 0) && ((cond1 & 0x0000F000) != (driver1 & 0x0000F000)))
		return false;
	if (((cond1 & 0x0F000000) != 0) && ((cond1 & 0x0F000000) != (driver1 & 0x0F000000)))
		return false;

	/*  Bit Defined Check ================ */
	/*  We don't care [31:28] and [23:20] */
	/*  */
	cond1   &= 0x000F0FFF;
	driver1 &= 0x000F0FFF;

	if ((cond1 & driver1) == cond1) {
		u32 bitMask = 0;

		if ((cond1 & 0x0F) == 0) /*  BoardType is DONTCARE */
			return true;

		if ((cond1 & BIT0) != 0) /* GLNA */
			bitMask |= 0x000000FF;
		if ((cond1 & BIT1) != 0) /* GPA */
			bitMask |= 0x0000FF00;
		if ((cond1 & BIT2) != 0) /* ALNA */
			bitMask |= 0x00FF0000;
		if ((cond1 & BIT3) != 0) /* APA */
			bitMask |= 0xFF000000;

		/*  BoardType of each RF path is matched */
		if ((cond2 & bitMask) == (driver2 & bitMask))
			return true;
	}
	return false;
}

static bool CheckNegative(
	PDM_ODM_T pDM_Odm, const u32  Condition1, const u32 Condition2
)
{
	return true;
}

/******************************************************************************
*                           AGC_TAB.TXT
******************************************************************************/

static u32 Array_MP_8723B_AGC_TAB[] = {
		0xC78, 0xFD000001,
		0xC78, 0xFC010001,
		0xC78, 0xFB020001,
		0xC78, 0xFA030001,
		0xC78, 0xF9040001,
		0xC78, 0xF8050001,
		0xC78, 0xF7060001,
		0xC78, 0xF6070001,
		0xC78, 0xF5080001,
		0xC78, 0xF4090001,
		0xC78, 0xF30A0001,
		0xC78, 0xF20B0001,
		0xC78, 0xF10C0001,
		0xC78, 0xF00D0001,
		0xC78, 0xEF0E0001,
		0xC78, 0xEE0F0001,
		0xC78, 0xED100001,
		0xC78, 0xEC110001,
		0xC78, 0xEB120001,
		0xC78, 0xEA130001,
		0xC78, 0xE9140001,
		0xC78, 0xE8150001,
		0xC78, 0xE7160001,
		0xC78, 0xE6170001,
		0xC78, 0xE5180001,
		0xC78, 0xE4190001,
		0xC78, 0xE31A0001,
		0xC78, 0xA51B0001,
		0xC78, 0xA41C0001,
		0xC78, 0xA31D0001,
		0xC78, 0x671E0001,
		0xC78, 0x661F0001,
		0xC78, 0x65200001,
		0xC78, 0x64210001,
		0xC78, 0x63220001,
		0xC78, 0x4A230001,
		0xC78, 0x49240001,
		0xC78, 0x48250001,
		0xC78, 0x47260001,
		0xC78, 0x46270001,
		0xC78, 0x45280001,
		0xC78, 0x44290001,
		0xC78, 0x432A0001,
		0xC78, 0x422B0001,
		0xC78, 0x292C0001,
		0xC78, 0x282D0001,
		0xC78, 0x272E0001,
		0xC78, 0x262F0001,
		0xC78, 0x0A300001,
		0xC78, 0x09310001,
		0xC78, 0x08320001,
		0xC78, 0x07330001,
		0xC78, 0x06340001,
		0xC78, 0x05350001,
		0xC78, 0x04360001,
		0xC78, 0x03370001,
		0xC78, 0x02380001,
		0xC78, 0x01390001,
		0xC78, 0x013A0001,
		0xC78, 0x013B0001,
		0xC78, 0x013C0001,
		0xC78, 0x013D0001,
		0xC78, 0x013E0001,
		0xC78, 0x013F0001,
		0xC78, 0xFC400001,
		0xC78, 0xFB410001,
		0xC78, 0xFA420001,
		0xC78, 0xF9430001,
		0xC78, 0xF8440001,
		0xC78, 0xF7450001,
		0xC78, 0xF6460001,
		0xC78, 0xF5470001,
		0xC78, 0xF4480001,
		0xC78, 0xF3490001,
		0xC78, 0xF24A0001,
		0xC78, 0xF14B0001,
		0xC78, 0xF04C0001,
		0xC78, 0xEF4D0001,
		0xC78, 0xEE4E0001,
		0xC78, 0xED4F0001,
		0xC78, 0xEC500001,
		0xC78, 0xEB510001,
		0xC78, 0xEA520001,
		0xC78, 0xE9530001,
		0xC78, 0xE8540001,
		0xC78, 0xE7550001,
		0xC78, 0xE6560001,
		0xC78, 0xE5570001,
		0xC78, 0xE4580001,
		0xC78, 0xE3590001,
		0xC78, 0xA65A0001,
		0xC78, 0xA55B0001,
		0xC78, 0xA45C0001,
		0xC78, 0xA35D0001,
		0xC78, 0x675E0001,
		0xC78, 0x665F0001,
		0xC78, 0x65600001,
		0xC78, 0x64610001,
		0xC78, 0x63620001,
		0xC78, 0x62630001,
		0xC78, 0x61640001,
		0xC78, 0x48650001,
		0xC78, 0x47660001,
		0xC78, 0x46670001,
		0xC78, 0x45680001,
		0xC78, 0x44690001,
		0xC78, 0x436A0001,
		0xC78, 0x426B0001,
		0xC78, 0x286C0001,
		0xC78, 0x276D0001,
		0xC78, 0x266E0001,
		0xC78, 0x256F0001,
		0xC78, 0x24700001,
		0xC78, 0x09710001,
		0xC78, 0x08720001,
		0xC78, 0x07730001,
		0xC78, 0x06740001,
		0xC78, 0x05750001,
		0xC78, 0x04760001,
		0xC78, 0x03770001,
		0xC78, 0x02780001,
		0xC78, 0x01790001,
		0xC78, 0x017A0001,
		0xC78, 0x017B0001,
		0xC78, 0x017C0001,
		0xC78, 0x017D0001,
		0xC78, 0x017E0001,
		0xC78, 0x017F0001,
		0xC50, 0x69553422,
		0xC50, 0x69553420,
		0x824, 0x00390204,

};

void ODM_ReadAndConfig_MP_8723B_AGC_TAB(PDM_ODM_T pDM_Odm)
{
	u32 i = 0;
	u32 ArrayLen = sizeof(Array_MP_8723B_AGC_TAB)/sizeof(u32);
	u32 *Array = Array_MP_8723B_AGC_TAB;

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_INIT,
		ODM_DBG_LOUD,
		("===> ODM_ReadAndConfig_MP_8723B_AGC_TAB\n")
	);

	for (i = 0; i < ArrayLen; i += 2) {
		u32 v1 = Array[i];
		u32 v2 = Array[i+1];

		/*  This (offset, data) pair doesn't care the condition. */
		if (v1 < 0x40000000) {
			odm_ConfigBB_AGC_8723B(pDM_Odm, v1, bMaskDWord, v2);
			continue;
		} else {
			/*  This line is the beginning of branch. */
			bool bMatched = true;
			u8  cCond  = (u8)((v1 & (BIT29|BIT28)) >> 28);

			if (cCond == COND_ELSE) { /*  ELSE, ENDIF */
				bMatched = true;
				READ_NEXT_PAIR(v1, v2, i);
			} else if (!CheckPositive(pDM_Odm, v1, v2)) {
				bMatched = false;
				READ_NEXT_PAIR(v1, v2, i);
				READ_NEXT_PAIR(v1, v2, i);
			} else {
				READ_NEXT_PAIR(v1, v2, i);
				if (!CheckNegative(pDM_Odm, v1, v2))
					bMatched = false;
				else
					bMatched = true;
				READ_NEXT_PAIR(v1, v2, i);
			}

			if (bMatched == false) {
				/*  Condition isn't matched.
				*   Discard the following (offset, data) pairs.
				*/
				while (v1 < 0x40000000 && i < ArrayLen-2)
				READ_NEXT_PAIR(v1, v2, i);

				i -= 2; /*  prevent from for-loop += 2 */
			} else {
				/*  Configure matched pairs and skip to end of if-else. */
				while (v1 < 0x40000000 && i < ArrayLen-2) {
					odm_ConfigBB_AGC_8723B(pDM_Odm, v1, bMaskDWord, v2);
					READ_NEXT_PAIR(v1, v2, i);
				}

				/*  Keeps reading until ENDIF. */
				cCond = (u8)((v1 & (BIT29|BIT28)) >> 28);
				while (cCond != COND_ENDIF && i < ArrayLen-2) {
					READ_NEXT_PAIR(v1, v2, i);
					cCond = (u8)((v1 & (BIT29|BIT28)) >> 28);
				}
			}
		}
	}
}

/******************************************************************************
*                           PHY_REG.TXT
******************************************************************************/

static u32 Array_MP_8723B_PHY_REG[] = {
		0x800, 0x80040000,
		0x804, 0x00000003,
		0x808, 0x0000FC00,
		0x80C, 0x0000000A,
		0x810, 0x10001331,
		0x814, 0x020C3D10,
		0x818, 0x02200385,
		0x81C, 0x00000000,
		0x820, 0x01000100,
		0x824, 0x00190204,
		0x828, 0x00000000,
		0x82C, 0x00000000,
		0x830, 0x00000000,
		0x834, 0x00000000,
		0x838, 0x00000000,
		0x83C, 0x00000000,
		0x840, 0x00010000,
		0x844, 0x00000000,
		0x848, 0x00000000,
		0x84C, 0x00000000,
		0x850, 0x00000000,
		0x854, 0x00000000,
		0x858, 0x569A11A9,
		0x85C, 0x01000014,
		0x860, 0x66F60110,
		0x864, 0x061F0649,
		0x868, 0x00000000,
		0x86C, 0x27272700,
		0x870, 0x07000760,
		0x874, 0x25004000,
		0x878, 0x00000808,
		0x87C, 0x00000000,
		0x880, 0xB0000C1C,
		0x884, 0x00000001,
		0x888, 0x00000000,
		0x88C, 0xCCC000C0,
		0x890, 0x00000800,
		0x894, 0xFFFFFFFE,
		0x898, 0x40302010,
		0x89C, 0x00706050,
		0x900, 0x00000000,
		0x904, 0x00000023,
		0x908, 0x00000000,
		0x90C, 0x81121111,
		0x910, 0x00000002,
		0x914, 0x00000201,
		0xA00, 0x00D047C8,
		0xA04, 0x80FF800C,
		0xA08, 0x8C838300,
		0xA0C, 0x2E7F120F,
		0xA10, 0x9500BB78,
		0xA14, 0x1114D028,
		0xA18, 0x00881117,
		0xA1C, 0x89140F00,
		0xA20, 0x1A1B0000,
		0xA24, 0x090E1317,
		0xA28, 0x00000204,
		0xA2C, 0x00D30000,
		0xA70, 0x101FBF00,
		0xA74, 0x00000007,
		0xA78, 0x00000900,
		0xA7C, 0x225B0606,
		0xA80, 0x21806490,
		0xB2C, 0x00000000,
		0xC00, 0x48071D40,
		0xC04, 0x03A05611,
		0xC08, 0x000000E4,
		0xC0C, 0x6C6C6C6C,
		0xC10, 0x08800000,
		0xC14, 0x40000100,
		0xC18, 0x08800000,
		0xC1C, 0x40000100,
		0xC20, 0x00000000,
		0xC24, 0x00000000,
		0xC28, 0x00000000,
		0xC2C, 0x00000000,
		0xC30, 0x69E9AC44,
		0xC34, 0x469652AF,
		0xC38, 0x49795994,
		0xC3C, 0x0A97971C,
		0xC40, 0x1F7C403F,
		0xC44, 0x000100B7,
		0xC48, 0xEC020107,
		0xC4C, 0x007F037F,
		0xC50, 0x69553420,
		0xC54, 0x43BC0094,
		0xC58, 0x00013149,
		0xC5C, 0x00250492,
		0xC60, 0x00000000,
		0xC64, 0x7112848B,
		0xC68, 0x47C00BFF,
		0xC6C, 0x00000036,
		0xC70, 0x2C7F000D,
		0xC74, 0x020610DB,
		0xC78, 0x0000001F,
		0xC7C, 0x00B91612,
		0xC80, 0x390000E4,
		0xC84, 0x20F60000,
		0xC88, 0x40000100,
		0xC8C, 0x20200000,
		0xC90, 0x00020E1A,
		0xC94, 0x00000000,
		0xC98, 0x00020E1A,
		0xC9C, 0x00007F7F,
		0xCA0, 0x00000000,
		0xCA4, 0x000300A0,
		0xCA8, 0x00000000,
		0xCAC, 0x00000000,
		0xCB0, 0x00000000,
		0xCB4, 0x00000000,
		0xCB8, 0x00000000,
		0xCBC, 0x28000000,
		0xCC0, 0x00000000,
		0xCC4, 0x00000000,
		0xCC8, 0x00000000,
		0xCCC, 0x00000000,
		0xCD0, 0x00000000,
		0xCD4, 0x00000000,
		0xCD8, 0x64B22427,
		0xCDC, 0x00766932,
		0xCE0, 0x00222222,
		0xCE4, 0x00000000,
		0xCE8, 0x37644302,
		0xCEC, 0x2F97D40C,
		0xD00, 0x00000740,
		0xD04, 0x40020401,
		0xD08, 0x0000907F,
		0xD0C, 0x20010201,
		0xD10, 0xA0633333,
		0xD14, 0x3333BC53,
		0xD18, 0x7A8F5B6F,
		0xD2C, 0xCC979975,
		0xD30, 0x00000000,
		0xD34, 0x80608000,
		0xD38, 0x00000000,
		0xD3C, 0x00127353,
		0xD40, 0x00000000,
		0xD44, 0x00000000,
		0xD48, 0x00000000,
		0xD4C, 0x00000000,
		0xD50, 0x6437140A,
		0xD54, 0x00000000,
		0xD58, 0x00000282,
		0xD5C, 0x30032064,
		0xD60, 0x4653DE68,
		0xD64, 0x04518A3C,
		0xD68, 0x00002101,
		0xD6C, 0x2A201C16,
		0xD70, 0x1812362E,
		0xD74, 0x322C2220,
		0xD78, 0x000E3C24,
		0xE00, 0x2D2D2D2D,
		0xE04, 0x2D2D2D2D,
		0xE08, 0x0390272D,
		0xE10, 0x2D2D2D2D,
		0xE14, 0x2D2D2D2D,
		0xE18, 0x2D2D2D2D,
		0xE1C, 0x2D2D2D2D,
		0xE28, 0x00000000,
		0xE30, 0x1000DC1F,
		0xE34, 0x10008C1F,
		0xE38, 0x02140102,
		0xE3C, 0x681604C2,
		0xE40, 0x01007C00,
		0xE44, 0x01004800,
		0xE48, 0xFB000000,
		0xE4C, 0x000028D1,
		0xE50, 0x1000DC1F,
		0xE54, 0x10008C1F,
		0xE58, 0x02140102,
		0xE5C, 0x28160D05,
		0xE60, 0x00000008,
		0xE68, 0x001B2556,
		0xE6C, 0x00C00096,
		0xE70, 0x00C00096,
		0xE74, 0x01000056,
		0xE78, 0x01000014,
		0xE7C, 0x01000056,
		0xE80, 0x01000014,
		0xE84, 0x00C00096,
		0xE88, 0x01000056,
		0xE8C, 0x00C00096,
		0xED0, 0x00C00096,
		0xED4, 0x00C00096,
		0xED8, 0x00C00096,
		0xEDC, 0x000000D6,
		0xEE0, 0x000000D6,
		0xEEC, 0x01C00016,
		0xF14, 0x00000003,
		0xF4C, 0x00000000,
		0xF00, 0x00000300,
		0x820, 0x01000100,
		0x800, 0x83040000,

};

void ODM_ReadAndConfig_MP_8723B_PHY_REG(PDM_ODM_T pDM_Odm)
{
	u32 i = 0;
	u32 ArrayLen = sizeof(Array_MP_8723B_PHY_REG)/sizeof(u32);
	u32 *Array = Array_MP_8723B_PHY_REG;

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_INIT,
		ODM_DBG_LOUD,
		("===> ODM_ReadAndConfig_MP_8723B_PHY_REG\n")
	);

	for (i = 0; i < ArrayLen; i += 2) {
		u32 v1 = Array[i];
		u32 v2 = Array[i+1];

		/*  This (offset, data) pair doesn't care the condition. */
		if (v1 < 0x40000000) {
			odm_ConfigBB_PHY_8723B(pDM_Odm, v1, bMaskDWord, v2);
			continue;
		} else {
			/*  This line is the beginning of branch. */
			bool bMatched = true;
			u8  cCond  = (u8)((v1 & (BIT29|BIT28)) >> 28);

			if (cCond == COND_ELSE) { /*  ELSE, ENDIF */
				bMatched = true;
				READ_NEXT_PAIR(v1, v2, i);
			} else if (!CheckPositive(pDM_Odm, v1, v2)) {
				bMatched = false;
				READ_NEXT_PAIR(v1, v2, i);
				READ_NEXT_PAIR(v1, v2, i);
			} else {
				READ_NEXT_PAIR(v1, v2, i);
				if (!CheckNegative(pDM_Odm, v1, v2))
					bMatched = false;
				else
					bMatched = true;
				READ_NEXT_PAIR(v1, v2, i);
			}

			if (bMatched == false) {
				/*  Condition isn't matched.
				*   Discard the following (offset, data) pairs.
				*/
				while (v1 < 0x40000000 && i < ArrayLen-2)
				READ_NEXT_PAIR(v1, v2, i);

					i -= 2; /*  prevent from for-loop += 2 */
			} else { /*  Configure matched pairs and skip to end of if-else. */
				while (v1 < 0x40000000 && i < ArrayLen-2) {
					odm_ConfigBB_PHY_8723B(pDM_Odm, v1, bMaskDWord, v2);
					READ_NEXT_PAIR(v1, v2, i);
				}

				/*  Keeps reading until ENDIF. */
				cCond = (u8)((v1 & (BIT29|BIT28)) >> 28);
				while (cCond != COND_ENDIF && i < ArrayLen-2) {
					READ_NEXT_PAIR(v1, v2, i);
					cCond = (u8)((v1 & (BIT29|BIT28)) >> 28);
				}
			}
		}
	}
}

/******************************************************************************
*                           PHY_REG_PG.TXT
******************************************************************************/

static u32 Array_MP_8723B_PHY_REG_PG[] = {
	0, 0, 0, 0x00000e08, 0x0000ff00, 0x00003800,
	0, 0, 0, 0x0000086c, 0xffffff00, 0x32343600,
	0, 0, 0, 0x00000e00, 0xffffffff, 0x40424444,
	0, 0, 0, 0x00000e04, 0xffffffff, 0x28323638,
	0, 0, 0, 0x00000e10, 0xffffffff, 0x38404244,
	0, 0, 0, 0x00000e14, 0xffffffff, 0x26303436
};

void ODM_ReadAndConfig_MP_8723B_PHY_REG_PG(PDM_ODM_T pDM_Odm)
{
	u32 i = 0;
	u32 ArrayLen = sizeof(Array_MP_8723B_PHY_REG_PG)/sizeof(u32);
	u32 *Array = Array_MP_8723B_PHY_REG_PG;

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_INIT,
		ODM_DBG_LOUD,
		("===> ODM_ReadAndConfig_MP_8723B_PHY_REG_PG\n")
	);

	pDM_Odm->PhyRegPgVersion = 1;
	pDM_Odm->PhyRegPgValueType = PHY_REG_PG_EXACT_VALUE;

	for (i = 0; i < ArrayLen; i += 6) {
		u32 v1 = Array[i];
		u32 v2 = Array[i+1];
		u32 v3 = Array[i+2];
		u32 v4 = Array[i+3];
		u32 v5 = Array[i+4];
		u32 v6 = Array[i+5];

		odm_ConfigBB_PHY_REG_PG_8723B(pDM_Odm, v1, v2, v3, v4, v5, v6);
	}
}
