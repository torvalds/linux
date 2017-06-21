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

/*Image2HeaderVersion: 2.18*/
#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (RTL8188E_SUPPORT == 1)
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
				((pDM_Odm->BoardType & BIT2) >> 2) << 4;  /* _BT*/  

	u4Byte	cond1   = Condition1, cond2 = Condition2, cond3 = Condition3, cond4 = Condition4;
	u4Byte    driver1 = pDM_Odm->CutVersion       << 24 | 
				(pDM_Odm->SupportInterface & 0xF0) << 16 | 
				pDM_Odm->SupportPlatform  << 16 | 
				pDM_Odm->PackageType      << 12 | 
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
*                           MAC_REG.TXT
******************************************************************************/

u4Byte Array_MP_8188E_MAC_REG[] = { 
		0x026, 0x00000041,
		0x027, 0x00000035,
	0x80000002,	0x00000000,	0x40000000,	0x00000000,
		0x040, 0x0000000C,
	0x90000001,	0x00000000,	0x40000000,	0x00000000,
		0x040, 0x0000000C,
	0x90000001,	0x00000001,	0x40000000,	0x00000000,
		0x040, 0x0000000C,
	0x90000001,	0x00000002,	0x40000000,	0x00000000,
		0x040, 0x0000000C,
	0xA0000000,	0x00000000,
		0x040, 0x00000000,
	0xB0000000,	0x00000000,
		0x421, 0x0000000F,
		0x428, 0x0000000A,
		0x429, 0x00000010,
		0x430, 0x00000000,
		0x431, 0x00000001,
		0x432, 0x00000002,
		0x433, 0x00000004,
		0x434, 0x00000005,
		0x435, 0x00000006,
		0x436, 0x00000007,
		0x437, 0x00000008,
		0x438, 0x00000000,
		0x439, 0x00000000,
		0x43A, 0x00000001,
		0x43B, 0x00000002,
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
		0x480, 0x00000008,
		0x4C8, 0x000000FF,
		0x4C9, 0x00000008,
		0x4CC, 0x000000FF,
		0x4CD, 0x000000FF,
		0x4CE, 0x00000001,
		0x4D3, 0x00000001,
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
		0x516, 0x0000000A,
		0x525, 0x0000004F,
		0x550, 0x00000010,
		0x551, 0x00000010,
		0x559, 0x00000002,
		0x55D, 0x000000FF,
		0x605, 0x00000030,
		0x608, 0x0000000E,
		0x609, 0x0000002A,
		0x620, 0x000000FF,
		0x621, 0x000000FF,
		0x622, 0x000000FF,
		0x623, 0x000000FF,
		0x624, 0x000000FF,
		0x625, 0x000000FF,
		0x626, 0x000000FF,
		0x627, 0x000000FF,
		0x63C, 0x00000008,
		0x63D, 0x00000008,
		0x63E, 0x0000000C,
		0x63F, 0x0000000C,
		0x640, 0x00000040,
		0x652, 0x00000020,
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

void
ODM_ReadAndConfig_MP_8188E_MAC_REG(
	IN   PDM_ODM_T  pDM_Odm
)
{
	u4Byte     i         = 0;
	u1Byte     cCond;
	BOOLEAN bMatched = TRUE, bSkipped = FALSE;
	u4Byte     ArrayLen    = sizeof(Array_MP_8188E_MAC_REG)/sizeof(u4Byte);
	pu4Byte    Array       = Array_MP_8188E_MAC_REG;
	
	u4Byte	v1 = 0, v2 = 0, pre_v1 = 0, pre_v2 = 0;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_8188E_MAC_REG\n"));

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
				odm_ConfigMAC_8188E(pDM_Odm, v1, (u1Byte)v2);
		}
		i = i + 2;
	}
}

u4Byte
ODM_GetVersion_MP_8188E_MAC_REG(void)
{
	   return 70;
}

#endif /* end of HWIMG_SUPPORT*/

