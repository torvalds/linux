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
* You should have received a copy of the GNU General Public License along with 
* this program; if not, write to the Free Software Foundation, Inc., 
* 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA 
* 
* 
******************************************************************************/

/*Image2HeaderVersion: 2.26*/
#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (RTL8188F_SUPPORT == 1)
static BOOLEAN
CheckPositive(
	IN  PDM_ODM_T     pDM_Odm,
	IN  const u4Byte  Condition1,
	IN  const u4Byte  Condition2,
	IN	const u4Byte  Condition3,
	IN	const u4Byte  Condition4
)
{
	u1Byte    _BoardType = ((pDM_Odm->BoardType & BIT4) >> 4) << 0 | /* _GLNA*/
				((pDM_Odm->BoardType & BIT3) >> 3) << 1 | /* _GPA*/ 
				((pDM_Odm->BoardType & BIT7) >> 7) << 2 | /* _ALNA*/
				((pDM_Odm->BoardType & BIT6) >> 6) << 3 | /* _APA */
				((pDM_Odm->BoardType & BIT2) >> 2) << 4 | /* _BT*/  
				((pDM_Odm->BoardType & BIT1) >> 1) << 5;  /* _NGFF*/  

	u4Byte	cond1   = Condition1, cond2 = Condition2, cond3 = Condition3, cond4 = Condition4;

	u1Byte	cut_version_for_para   = (pDM_Odm->CutVersion == ODM_CUT_A) ? 15 : pDM_Odm->CutVersion;
	u1Byte	pkg_type_for_para   = (pDM_Odm->PackageType == 0) ? 15 : pDM_Odm->PackageType;

	u4Byte    driver1 = cut_version_for_para       << 24 | 
				(pDM_Odm->SupportInterface & 0xF0) << 16 | 
				pDM_Odm->SupportPlatform  << 16 | 
				pkg_type_for_para      << 12 | 
				(pDM_Odm->SupportInterface & 0x0F) << 8  |
				_BoardType;

	u4Byte    driver2 = (pDM_Odm->TypeGLNA & 0xFF) <<  0 |  
				(pDM_Odm->TypeGPA & 0xFF)  <<  8 | 
				(pDM_Odm->TypeALNA & 0xFF) << 16 | 
				(pDM_Odm->TypeAPA & 0xFF)  << 24; 

u4Byte    driver3 = 0;

	u4Byte    driver4 = (pDM_Odm->TypeGLNA & 0xFF00) >>  8 |
				(pDM_Odm->TypeGPA & 0xFF00) |
				(pDM_Odm->TypeALNA & 0xFF00) << 8 |
				(pDM_Odm->TypeAPA & 0xFF00)  << 16;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, 
	("===> CheckPositive (cond1, cond2, cond3, cond4) = (0x%X 0x%X 0x%X 0x%X)\n", cond1, cond2, cond3, cond4));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, 
	("===> CheckPositive (driver1, driver2, driver3, driver4) = (0x%X 0x%X 0x%X 0x%X)\n", driver1, driver2, driver3, driver4));

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, 
	("	(Platform, Interface) = (0x%X, 0x%X)\n", pDM_Odm->SupportPlatform, pDM_Odm->SupportInterface));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, 
	("	(Board, Package) = (0x%X, 0x%X)\n", pDM_Odm->BoardType, pDM_Odm->PackageType));


	/*============== Value Defined Check ===============*/
	/*QFN Type [15:12] and Cut Version [27:24] need to do value check*/
	
	if (((cond1 & 0x0000F000) != 0) && ((cond1 & 0x0000F000) != (driver1 & 0x0000F000)))
		return FALSE;
	if (((cond1 & 0x0F000000) != 0) && ((cond1 & 0x0F000000) != (driver1 & 0x0F000000)))
		return FALSE;

	/*=============== Bit Defined Check ================*/
	/* We don't care [31:28] */

	cond1   &= 0x00FF0FFF; 
	driver1 &= 0x00FF0FFF; 

	if ((cond1 & driver1) == cond1) {
		u4Byte bitMask = 0;

		if ((cond1 & 0x0F) == 0) /* BoardType is DONTCARE*/
			return TRUE;

		if ((cond1 & BIT0) != 0) /*GLNA*/
			bitMask |= 0x000000FF;
		if ((cond1 & BIT1) != 0) /*GPA*/
			bitMask |= 0x0000FF00;
		if ((cond1 & BIT2) != 0) /*ALNA*/
			bitMask |= 0x00FF0000;
		if ((cond1 & BIT3) != 0) /*APA*/
			bitMask |= 0xFF000000;

		if (((cond2 & bitMask) == (driver2 & bitMask)) && ((cond4 & bitMask) == (driver4 & bitMask)))  /* BoardType of each RF path is matched*/
			return TRUE;
		else
			return FALSE;
	} else
		return FALSE;
}
static BOOLEAN
CheckNegative(
	IN  PDM_ODM_T     pDM_Odm,
	IN  const u4Byte  Condition1,
	IN  const u4Byte  Condition2
)
{
	return TRUE;
}

/******************************************************************************
*                           AGC_TAB.TXT
******************************************************************************/

u4Byte Array_MP_8188F_AGC_TAB[] = { 
		0xC78, 0xFC000001,
		0xC78, 0xFB010001,
		0xC78, 0xFA020001,
		0xC78, 0xF9030001,
		0xC78, 0xF8040001,
		0xC78, 0xF7050001,
		0xC78, 0xF6060001,
		0xC78, 0xF5070001,
		0xC78, 0xF4080001,
		0xC78, 0xF3090001,
		0xC78, 0xF20A0001,
		0xC78, 0xF10B0001,
		0xC78, 0xF00C0001,
		0xC78, 0xEF0D0001,
		0xC78, 0xEE0E0001,
		0xC78, 0xED0F0001,
		0xC78, 0xEC100001,
		0xC78, 0xEB110001,
		0xC78, 0xEA120001,
		0xC78, 0xE9130001,
		0xC78, 0xE8140001,
		0xC78, 0xE7150001,
		0xC78, 0xE6160001,
		0xC78, 0xE5170001,
		0xC78, 0xE4180001,
		0xC78, 0xE3190001,
		0xC78, 0xE21A0001,
		0xC78, 0xE11B0001,
		0xC78, 0xE01C0001,
		0xC78, 0xC21D0001,
		0xC78, 0xC11E0001,
		0xC78, 0xC01F0001,
		0xC78, 0xA5200001,
		0xC78, 0xA4210001,
		0xC78, 0xA3220001,
		0xC78, 0xA2230001,
		0xC78, 0xA1240001,
		0xC78, 0xA0250001,
		0xC78, 0x65260001,
		0xC78, 0x64270001,
		0xC78, 0x63280001,
		0xC78, 0x62290001,
		0xC78, 0x612A0001,
		0xC78, 0x442B0001,
		0xC78, 0x432C0001,
		0xC78, 0x422D0001,
		0xC78, 0x412E0001,
		0xC78, 0x402F0001,
		0xC78, 0x21300001,
		0xC78, 0x20310001,
		0xC78, 0x05320001,
		0xC78, 0x04330001,
		0xC78, 0x03340001,
		0xC78, 0x02350001,
		0xC78, 0x01360001,
		0xC78, 0x00370001,
		0xC78, 0x00380001,
		0xC78, 0x00390001,
		0xC78, 0x003A0001,
		0xC78, 0x003B0001,
		0xC78, 0x003C0001,
		0xC78, 0x003D0001,
		0xC78, 0x003E0001,
		0xC78, 0x003F0001,
		0xC50, 0x69553422,
		0xC50, 0x69553420,

};

void
ODM_ReadAndConfig_MP_8188F_AGC_TAB(
	IN   PDM_ODM_T  pDM_Odm
)
{
	u4Byte     i         = 0;
	u1Byte     cCond;
	BOOLEAN bMatched = TRUE, bSkipped = FALSE;
	u4Byte     ArrayLen    = sizeof(Array_MP_8188F_AGC_TAB)/sizeof(u4Byte);
	pu4Byte    Array       = Array_MP_8188F_AGC_TAB;
	
	u4Byte	v1 = 0, v2 = 0, pre_v1 = 0, pre_v2 = 0;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_8188F_AGC_TAB\n"));

	while ((i + 1) < ArrayLen) {
		v1 = Array[i];
		v2 = Array[i + 1];

		if (v1 & (BIT31 | BIT30)) {/*positive & negative condition*/
			if (v1 & BIT31) {/* positive condition*/
				cCond  = (u1Byte)((v1 & (BIT29|BIT28)) >> 28);
				if (cCond == COND_ENDIF) {/*end*/
					bMatched = TRUE;
					bSkipped = FALSE;
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("ENDIF\n"));
				} else if (cCond == COND_ELSE) { /*else*/
					bMatched = bSkipped?FALSE:TRUE;
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("ELSE\n"));
				} else {/*if , else if*/
					pre_v1 = v1;
					pre_v2 = v2;
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("IF or ELSE IF\n"));
				}
			} else if (v1 & BIT30) { /*negative condition*/
				if (bSkipped == FALSE) {
					if (CheckPositive(pDM_Odm, pre_v1, pre_v2, v1, v2)) {
						bMatched = TRUE;
						bSkipped = TRUE;
					} else {
						bMatched = FALSE;
						bSkipped = FALSE;
					}
				} else
					bMatched = FALSE;
			}
		} else {
			if (bMatched)
				odm_ConfigBB_AGC_8188F(pDM_Odm, v1, bMaskDWord, v2);
		}
		i = i + 2;
	}
}

u4Byte
ODM_GetVersion_MP_8188F_AGC_TAB(void)
{
	   return 36;
}

/******************************************************************************
*                           PHY_REG.TXT
******************************************************************************/

u4Byte Array_MP_8188F_PHY_REG[] = { 
		0x800, 0x80045700,
		0x804, 0x00000001,
		0x808, 0x0000FC00,
		0x80C, 0x0000000A,
		0x810, 0x10001331,
		0x814, 0x020C3D10,
		0x818, 0x00200385,
		0x81C, 0x00000000,
		0x820, 0x01000100,
		0x824, 0x00390204,
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
		0x850, 0x00030000,
		0x854, 0x00000000,
		0x858, 0x569A569A,
		0x85C, 0x569A569A,
		0x860, 0x00000130,
		0x864, 0x00000000,
		0x868, 0x00000000,
		0x86C, 0x27272700,
		0x870, 0x00000000,
		0x874, 0x25004000,
		0x878, 0x00000808,
		0x87C, 0x004F0201,
		0x880, 0xB0000B1E,
		0x884, 0x00000007,
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
		0x948, 0x99000000,
		0x94C, 0x00000010,
		0x950, 0x20003000,
		0x954, 0x4A880000,
		0x958, 0x4BC5D87A,
		0x95C, 0x04EB9B79,
		0x96C, 0x00000003,
		0xA00, 0x00D046C8,
		0xA04, 0x80FF800C,
	0x80000400,	0x00000000,	0x40000000,	0x00000000,
		0xA08, 0x8C038300,
	0xA0000000,	0x00000000,
		0xA08, 0x8C898300,
	0xB0000000,	0x00000000,
		0xA0C, 0x2E7F120F,
		0xA10, 0x9500BB78,
		0xA14, 0x1114D028,
		0xA18, 0x00881117,
		0xA1C, 0x89140F00,
		0xA20, 0xD1D80000,
		0xA24, 0x5A7DA0BD,
		0xA28, 0x0000223B,
		0xA2C, 0x00D30000,
		0xA70, 0x101FBF00,
		0xA74, 0x00000007,
	0x80000400,	0x00000000,	0x40000000,	0x00000000,
		0xA78, 0x00008900,
	0xA0000000,	0x00000000,
		0xA78, 0x00000900,
	0xB0000000,	0x00000000,
		0xA7C, 0x225B0606,
		0xA80, 0x218075B1,
		0xA84, 0x00120000,
		0xA88, 0x040C0000,
		0xA8C, 0x12345678,
		0xA90, 0xABCDEF00,
		0xA94, 0x001B1B89,
		0xA98, 0x05100000,
		0xA9C, 0x3F000000,
		0xAA0, 0x00000000,
		0xB2C, 0x00000000,
		0xC00, 0x48071D40,
		0xC04, 0x03A05611,
		0xC08, 0x000000E4,
		0xC0C, 0x6C6C6C6C,
		0xC10, 0x18800000,
		0xC14, 0x40000100,
		0xC18, 0x08800000,
		0xC1C, 0x40000100,
		0xC20, 0x00000000,
		0xC24, 0x00000000,
		0xC28, 0x00000000,
		0xC2C, 0x00000000,
		0xC30, 0x69E9CC4A,
		0xC34, 0x31000040,
		0xC38, 0x21688080,
		0xC3C, 0x00001714,
		0xC40, 0x1F78403F,
		0xC44, 0x00010036,
		0xC48, 0xEC020107,
		0xC4C, 0x007F037F,
		0xC50, 0x69553420,
		0xC54, 0x43BC0094,
		0xC58, 0x00013169,
		0xC5C, 0x00250492,
		0xC60, 0x00000000,
		0xC64, 0x7112848B,
		0xC68, 0x47C07BFF,
		0xC6C, 0x00000036,
		0xC70, 0x2C7F000D,
		0xC74, 0x020600DB,
		0xC78, 0x0000001F,
		0xC7C, 0x00B91612,
		0xC80, 0x390000E4,
	0x80000400,	0x00000000,	0x40000000,	0x00000000,
		0xC84, 0x21F60000,
	0xA0000000,	0x00000000,
		0xC84, 0x11F60000,
	0xB0000000,	0x00000000,
		0xC88, 0x40000100,
		0xC8C, 0x20200000,
		0xC90, 0x00091521,
		0xC94, 0x00000000,
		0xC98, 0x00121820,
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
		0xCE4, 0x10000000,
		0xCE8, 0x37644302,
		0xCEC, 0x2F97D40C,
		0xD00, 0x04030740,
		0xD04, 0x40020401,
		0xD08, 0x0000907F,
		0xD0C, 0x20010201,
		0xD10, 0xA0633333,
		0xD14, 0x3333BC53,
		0xD18, 0x7A8F5B6F,
		0xD2C, 0xCB979975,
		0xD30, 0x00000000,
		0xD34, 0x40608000,
		0xD38, 0x98000000,
		0xD3C, 0x40127353,
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
		0xE60, 0x021400A0,
		0xE64, 0x281600A0,
		0xE6C, 0x01C00010,
		0xE70, 0x01C00010,
		0xE74, 0x02000010,
		0xE78, 0x02000010,
		0xE7C, 0x02000010,
		0xE80, 0x02000010,
		0xE84, 0x01C00010,
		0xE88, 0x02000010,
		0xE8C, 0x01C00010,
		0xED0, 0x01C00010,
		0xED4, 0x01C00010,
		0xED8, 0x01C00010,
		0xEDC, 0x00000010,
		0xEE0, 0x00000010,
		0xEEC, 0x03C00010,
		0xF14, 0x00000003,
		0xF4C, 0x00000000,
		0xF00, 0x00000300,

};

void
ODM_ReadAndConfig_MP_8188F_PHY_REG(
	IN   PDM_ODM_T  pDM_Odm
)
{
	u4Byte     i         = 0;
	u1Byte     cCond;
	BOOLEAN bMatched = TRUE, bSkipped = FALSE;
	u4Byte     ArrayLen    = sizeof(Array_MP_8188F_PHY_REG)/sizeof(u4Byte);
	pu4Byte    Array       = Array_MP_8188F_PHY_REG;
	
	u4Byte	v1 = 0, v2 = 0, pre_v1 = 0, pre_v2 = 0;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_8188F_PHY_REG\n"));

	while ((i + 1) < ArrayLen) {
		v1 = Array[i];
		v2 = Array[i + 1];

		if (v1 & (BIT31 | BIT30)) {/*positive & negative condition*/
			if (v1 & BIT31) {/* positive condition*/
				cCond  = (u1Byte)((v1 & (BIT29|BIT28)) >> 28);
				if (cCond == COND_ENDIF) {/*end*/
					bMatched = TRUE;
					bSkipped = FALSE;
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("ENDIF\n"));
				} else if (cCond == COND_ELSE) { /*else*/
					bMatched = bSkipped?FALSE:TRUE;
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("ELSE\n"));
				} else {/*if , else if*/
					pre_v1 = v1;
					pre_v2 = v2;
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("IF or ELSE IF\n"));
				}
			} else if (v1 & BIT30) { /*negative condition*/
				if (bSkipped == FALSE) {
					if (CheckPositive(pDM_Odm, pre_v1, pre_v2, v1, v2)) {
						bMatched = TRUE;
						bSkipped = TRUE;
					} else {
						bMatched = FALSE;
						bSkipped = FALSE;
					}
				} else
					bMatched = FALSE;
			}
		} else {
			if (bMatched)
				odm_ConfigBB_PHY_8188F(pDM_Odm, v1, bMaskDWord, v2);
		}
		i = i + 2;
	}
}

u4Byte
ODM_GetVersion_MP_8188F_PHY_REG(void)
{
	   return 36;
}

/******************************************************************************
*                           PHY_REG_PG.TXT
******************************************************************************/

u4Byte Array_MP_8188F_PHY_REG_PG[] = { 
	0, 0, 0, 0x00000e08, 0x0000ff00, 0x00003200,
	0, 0, 0, 0x0000086c, 0xffffff00, 0x32323200,
	0, 0, 0, 0x00000e00, 0xffffffff, 0x34363636,
	0, 0, 0, 0x00000e04, 0xffffffff, 0x28303234,
	0, 0, 0, 0x00000e10, 0xffffffff, 0x30343434,
	0, 0, 0, 0x00000e14, 0xffffffff, 0x26262830
};

void
ODM_ReadAndConfig_MP_8188F_PHY_REG_PG(
	IN   PDM_ODM_T  pDM_Odm
)
{
	u4Byte     i         = 0;
	u4Byte     ArrayLen    = sizeof(Array_MP_8188F_PHY_REG_PG)/sizeof(u4Byte);
	pu4Byte    Array       = Array_MP_8188F_PHY_REG_PG;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	PlatformZeroMemory(pHalData->BufOfLinesPwrByRate, MAX_LINES_HWCONFIG_TXT*MAX_BYTES_LINE_HWCONFIG_TXT);
	pHalData->nLinesReadPwrByRate = ArrayLen/6;
#endif

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_8188F_PHY_REG_PG\n"));

	pDM_Odm->PhyRegPgVersion = 1;
	pDM_Odm->PhyRegPgValueType = PHY_REG_PG_EXACT_VALUE;

	for (i = 0; i < ArrayLen; i += 6) {
		u4Byte v1 = Array[i];
		u4Byte v2 = Array[i+1];
		u4Byte v3 = Array[i+2];
		u4Byte v4 = Array[i+3];
		u4Byte v5 = Array[i+4];
		u4Byte v6 = Array[i+5];

	    odm_ConfigBB_PHY_REG_PG_8188F(pDM_Odm, v1, v2, v3, v4, v5, v6);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	rsprintf((char *)pHalData->BufOfLinesPwrByRate[i/6], 100, "%s, %s, %s, 0x%X, 0x%08X, 0x%08X,",
		(v1 == 0?"2.4G":"  5G"), (v2 == 0?"A":"B"), (v3 == 0?"1Tx":"2Tx"), v4, v5, v6);
#endif
	}
}



#endif /* end of HWIMG_SUPPORT*/

