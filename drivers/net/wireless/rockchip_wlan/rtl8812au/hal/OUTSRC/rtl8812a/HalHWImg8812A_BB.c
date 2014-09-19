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

#include "Mp_Precomp.h"
#include "../phydm_precomp.h"

#if (RTL8812A_SUPPORT == 1)
static BOOLEAN
CheckPositive(
    IN  PDM_ODM_T     pDM_Odm,
    IN  const u4Byte  Condition1,
    IN  const u4Byte  Condition2
    )
{
    u1Byte    _BoardType = ((pDM_Odm->BoardType & BIT4) >> 4) << 0 | // _GLNA
                           ((pDM_Odm->BoardType & BIT3) >> 3) << 1 | // _GPA 
                           ((pDM_Odm->BoardType & BIT7) >> 7) << 2 | // _ALNA
                           ((pDM_Odm->BoardType & BIT6) >> 6) << 3 | // _APA 
                           ((pDM_Odm->BoardType & BIT2) >> 2) << 4;  // _BT  

	u4Byte 	  cond1   = Condition1, cond2 = Condition2;
	u4Byte    driver1 = pDM_Odm->CutVersion       << 24 |  
		                pDM_Odm->SupportPlatform  << 16 | 
		                pDM_Odm->PackageType      << 12 | 
		                pDM_Odm->SupportInterface << 8  |
		                _BoardType;

	u4Byte    driver2 = pDM_Odm->TypeGLNA <<  0 |  
		                pDM_Odm->TypeGPA  <<  8 | 
		                pDM_Odm->TypeALNA << 16 | 
		                pDM_Odm->TypeAPA  << 24; 

    ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, 
                ("===> [8812A] CheckPositive (cond1, cond2) = (0x%X 0x%X)\n", cond1, cond2));
    ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, 
                ("===> [8812A] CheckPositive (driver1, driver2) = (0x%X 0x%X)\n", driver1, driver2));

    ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, 
                ("	(Platform, Interface) = (0x%X, 0x%X)\n", pDM_Odm->SupportPlatform, pDM_Odm->SupportInterface));
    ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, 
                ("	(Board, Package) = (0x%X, 0x%X)\n", pDM_Odm->BoardType, pDM_Odm->PackageType));


	//============== Value Defined Check ===============//
	//QFN Type [15:12] and Cut Version [27:24] need to do value check
	
	if(((cond1 & 0x0000F000) != 0) &&((cond1 & 0x0000F000) != (driver1 & 0x0000F000)))
		return FALSE;
	if(((cond1 & 0x0F000000) != 0) &&((cond1 & 0x0F000000) != (driver1 & 0x0F000000)))
		return FALSE;		

	//=============== Bit Defined Check ================//
    // We don't care [31:28] and [23:20]
    //
	cond1   &= 0x000F0FFF; 
	driver1 &= 0x000F0FFF; 

    if ((cond1 & driver1) == cond1) 
    {
        u4Byte bitMask = 0;
        if ((cond1 & 0x0F) == 0) // BoardType is DONTCARE
            return TRUE;

        if ((cond1 & BIT0) != 0) //GLNA
            bitMask |= 0x000000FF;
        if ((cond1 & BIT1) != 0) //GPA
            bitMask |= 0x0000FF00;
        if ((cond1 & BIT2) != 0) //ALNA
            bitMask |= 0x00FF0000;
        if ((cond1 & BIT3) != 0) //APA
            bitMask |= 0xFF000000;

        if ((cond2 & bitMask) == (driver2 & bitMask)) // BoardType of each RF path is matched
            return TRUE;
        else
            return FALSE;
    }
    else 
    {
        return FALSE;
    }
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

u4Byte Array_MP_8812A_AGC_TAB[] = { 
	0x80000001,0x00000000,0x40000000,0x00000000,
		0x81C, 0xFC000001,
		0x81C, 0xFB020001,
		0x81C, 0xFA040001,
		0x81C, 0xF9060001,
		0x81C, 0xF8080001,
		0x81C, 0xF70A0001,
		0x81C, 0xF60C0001,
		0x81C, 0xF50E0001,
		0x81C, 0xF4100001,
		0x81C, 0xF3120001,
		0x81C, 0xF2140001,
		0x81C, 0xF1160001,
		0x81C, 0xF0180001,
		0x81C, 0xEF1A0001,
		0x81C, 0xEE1C0001,
		0x81C, 0xED1E0001,
		0x81C, 0xEC200001,
		0x81C, 0xEB220001,
		0x81C, 0xEA240001,
		0x81C, 0xCD260001,
		0x81C, 0xCC280001,
		0x81C, 0xCB2A0001,
		0x81C, 0xCA2C0001,
		0x81C, 0xC92E0001,
		0x81C, 0xC8300001,
		0x81C, 0xA6320001,
		0x81C, 0xA5340001,
		0x81C, 0xA4360001,
		0x81C, 0xA3380001,
		0x81C, 0xA23A0001,
		0x81C, 0x883C0001,
		0x81C, 0x873E0001,
		0x81C, 0x86400001,
		0x81C, 0x85420001,
		0x81C, 0x84440001,
		0x81C, 0x83460001,
		0x81C, 0x82480001,
		0x81C, 0x814A0001,
		0x81C, 0x484C0001,
		0x81C, 0x474E0001,
		0x81C, 0x46500001,
		0x81C, 0x45520001,
		0x81C, 0x44540001,
		0x81C, 0x43560001,
		0x81C, 0x42580001,
		0x81C, 0x415A0001,
		0x81C, 0x255C0001,
		0x81C, 0x245E0001,
		0x81C, 0x23600001,
		0x81C, 0x22620001,
		0x81C, 0x21640001,
		0x81C, 0x21660001,
		0x81C, 0x21680001,
		0x81C, 0x216A0001,
		0x81C, 0x216C0001,
		0x81C, 0x216E0001,
		0x81C, 0x21700001,
		0x81C, 0x21720001,
		0x81C, 0x21740001,
		0x81C, 0x21760001,
		0x81C, 0x21780001,
		0x81C, 0x217A0001,
		0x81C, 0x217C0001,
		0x81C, 0x217E0001,
	0x90000001,0x00000005,0x40000000,0x00000000,
		0x81C, 0xF9000001,
		0x81C, 0xF8020001,
		0x81C, 0xF7040001,
		0x81C, 0xF6060001,
		0x81C, 0xF5080001,
		0x81C, 0xF40A0001,
		0x81C, 0xF30C0001,
		0x81C, 0xF20E0001,
		0x81C, 0xF1100001,
		0x81C, 0xF0120001,
		0x81C, 0xEF140001,
		0x81C, 0xEE160001,
		0x81C, 0xED180001,
		0x81C, 0xEC1A0001,
		0x81C, 0xEB1C0001,
		0x81C, 0xEA1E0001,
		0x81C, 0xCD200001,
		0x81C, 0xCC220001,
		0x81C, 0xCB240001,
		0x81C, 0xCA260001,
		0x81C, 0xC9280001,
		0x81C, 0xC82A0001,
		0x81C, 0xC72C0001,
		0x81C, 0xC62E0001,
		0x81C, 0xA5300001,
		0x81C, 0xA4320001,
		0x81C, 0xA3340001,
		0x81C, 0xA2360001,
		0x81C, 0x88380001,
		0x81C, 0x873A0001,
		0x81C, 0x863C0001,
		0x81C, 0x853E0001,
		0x81C, 0x84400001,
		0x81C, 0x83420001,
		0x81C, 0x82440001,
		0x81C, 0x81460001,
		0x81C, 0x48480001,
		0x81C, 0x474A0001,
		0x81C, 0x464C0001,
		0x81C, 0x454E0001,
		0x81C, 0x44500001,
		0x81C, 0x43520001,
		0x81C, 0x42540001,
		0x81C, 0x41560001,
		0x81C, 0x25580001,
		0x81C, 0x245A0001,
		0x81C, 0x235C0001,
		0x81C, 0x225E0001,
		0x81C, 0x21600001,
		0x81C, 0x21620001,
		0x81C, 0x21640001,
		0x81C, 0x21660001,
		0x81C, 0x21680001,
		0x81C, 0x216A0001,
		0x81C, 0x236C0001,
		0x81C, 0x226E0001,
		0x81C, 0x21700001,
		0x81C, 0x21720001,
		0x81C, 0x21740001,
		0x81C, 0x21760001,
		0x81C, 0x21780001,
		0x81C, 0x217A0001,
		0x81C, 0x217C0001,
		0x81C, 0x217E0001,
	0xA0000000,0x00000000,
		0x81C, 0xFF000001,
		0x81C, 0xFF020001,
		0x81C, 0xFF040001,
		0x81C, 0xFF060001,
		0x81C, 0xFF080001,
		0x81C, 0xFE0A0001,
		0x81C, 0xFD0C0001,
		0x81C, 0xFC0E0001,
		0x81C, 0xFB100001,
		0x81C, 0xFA120001,
		0x81C, 0xF9140001,
		0x81C, 0xF8160001,
		0x81C, 0xF7180001,
		0x81C, 0xF61A0001,
		0x81C, 0xF51C0001,
		0x81C, 0xF41E0001,
		0x81C, 0xF3200001,
		0x81C, 0xF2220001,
		0x81C, 0xF1240001,
		0x81C, 0xF0260001,
		0x81C, 0xEF280001,
		0x81C, 0xEE2A0001,
		0x81C, 0xED2C0001,
		0x81C, 0xEC2E0001,
		0x81C, 0xEB300001,
		0x81C, 0xEA320001,
		0x81C, 0xE9340001,
		0x81C, 0xE8360001,
		0x81C, 0xE7380001,
		0x81C, 0xE63A0001,
		0x81C, 0xE53C0001,
		0x81C, 0xC73E0001,
		0x81C, 0xC6400001,
		0x81C, 0xC5420001,
		0x81C, 0xC4440001,
		0x81C, 0xC3460001,
		0x81C, 0xC2480001,
		0x81C, 0xC14A0001,
		0x81C, 0xA74C0001,
		0x81C, 0xA64E0001,
		0x81C, 0xA5500001,
		0x81C, 0xA4520001,
		0x81C, 0xA3540001,
		0x81C, 0xA2560001,
		0x81C, 0xA1580001,
		0x81C, 0x675A0001,
		0x81C, 0x665C0001,
		0x81C, 0x655E0001,
		0x81C, 0x64600001,
		0x81C, 0x63620001,
		0x81C, 0x48640001,
		0x81C, 0x47660001,
		0x81C, 0x46680001,
		0x81C, 0x456A0001,
		0x81C, 0x446C0001,
		0x81C, 0x436E0001,
		0x81C, 0x42700001,
		0x81C, 0x41720001,
		0x81C, 0x41740001,
		0x81C, 0x41760001,
		0x81C, 0x41780001,
		0x81C, 0x417A0001,
		0x81C, 0x417C0001,
		0x81C, 0x417E0001,
	0xB0000000,0x00000000,
	0x80000004,0x00000000,0x40000000,0x00000000,
		0x81C, 0xFC800001,
		0x81C, 0xFB820001,
		0x81C, 0xFA840001,
		0x81C, 0xF9860001,
		0x81C, 0xF8880001,
		0x81C, 0xF78A0001,
		0x81C, 0xF68C0001,
		0x81C, 0xF58E0001,
		0x81C, 0xF4900001,
		0x81C, 0xF3920001,
		0x81C, 0xF2940001,
		0x81C, 0xF1960001,
		0x81C, 0xF0980001,
		0x81C, 0xEF9A0001,
		0x81C, 0xEE9C0001,
		0x81C, 0xED9E0001,
		0x81C, 0xECA00001,
		0x81C, 0xEBA20001,
		0x81C, 0xEAA40001,
		0x81C, 0xE9A60001,
		0x81C, 0xE8A80001,
		0x81C, 0xE7AA0001,
		0x81C, 0xE6AC0001,
		0x81C, 0xE5AE0001,
		0x81C, 0xE4B00001,
		0x81C, 0xE3B20001,
		0x81C, 0xA8B40001,
		0x81C, 0xA7B60001,
		0x81C, 0xA6B80001,
		0x81C, 0xA5BA0001,
		0x81C, 0xA4BC0001,
		0x81C, 0xA3BE0001,
		0x81C, 0xA2C00001,
		0x81C, 0xA1C20001,
		0x81C, 0x68C40001,
		0x81C, 0x67C60001,
		0x81C, 0x66C80001,
		0x81C, 0x65CA0001,
		0x81C, 0x64CC0001,
		0x81C, 0x47CE0001,
		0x81C, 0x46D00001,
		0x81C, 0x45D20001,
		0x81C, 0x44D40001,
		0x81C, 0x43D60001,
		0x81C, 0x42D80001,
		0x81C, 0x08DA0001,
		0x81C, 0x07DC0001,
		0x81C, 0x06DE0001,
		0x81C, 0x05E00001,
		0x81C, 0x04E20001,
		0x81C, 0x03E40001,
		0x81C, 0x02E60001,
		0x81C, 0x01E80001,
		0x81C, 0x01EA0001,
		0x81C, 0x01EC0001,
		0x81C, 0x01EE0001,
		0x81C, 0x01F00001,
		0x81C, 0x01F20001,
		0x81C, 0x01F40001,
		0x81C, 0x01F60001,
		0x81C, 0x01F80001,
		0x81C, 0x01FA0001,
		0x81C, 0x01FC0001,
		0x81C, 0x01FE0001,
	0xA0000000,0x00000000,
		0x81C, 0xFF800001,
		0x81C, 0xFF820001,
		0x81C, 0xFF840001,
		0x81C, 0xFE860001,
		0x81C, 0xFD880001,
		0x81C, 0xFC8A0001,
		0x81C, 0xFB8C0001,
		0x81C, 0xFA8E0001,
		0x81C, 0xF9900001,
		0x81C, 0xF8920001,
		0x81C, 0xF7940001,
		0x81C, 0xF6960001,
		0x81C, 0xF5980001,
		0x81C, 0xF49A0001,
		0x81C, 0xF39C0001,
		0x81C, 0xF29E0001,
		0x81C, 0xF1A00001,
		0x81C, 0xF0A20001,
		0x81C, 0xEFA40001,
		0x81C, 0xEEA60001,
		0x81C, 0xEDA80001,
		0x81C, 0xECAA0001,
		0x81C, 0xEBAC0001,
		0x81C, 0xEAAE0001,
		0x81C, 0xE9B00001,
		0x81C, 0xE8B20001,
		0x81C, 0xE7B40001,
		0x81C, 0xE6B60001,
		0x81C, 0xE5B80001,
		0x81C, 0xE4BA0001,
		0x81C, 0xE3BC0001,
		0x81C, 0xA8BE0001,
		0x81C, 0xA7C00001,
		0x81C, 0xA6C20001,
		0x81C, 0xA5C40001,
		0x81C, 0xA4C60001,
		0x81C, 0xA3C80001,
		0x81C, 0xA2CA0001,
		0x81C, 0xA1CC0001,
		0x81C, 0x68CE0001,
		0x81C, 0x67D00001,
		0x81C, 0x66D20001,
		0x81C, 0x65D40001,
		0x81C, 0x64D60001,
		0x81C, 0x47D80001,
		0x81C, 0x46DA0001,
		0x81C, 0x45DC0001,
		0x81C, 0x44DE0001,
		0x81C, 0x43E00001,
		0x81C, 0x42E20001,
		0x81C, 0x08E40001,
		0x81C, 0x07E60001,
		0x81C, 0x06E80001,
		0x81C, 0x05EA0001,
		0x81C, 0x04EC0001,
		0x81C, 0x03EE0001,
		0x81C, 0x02F00001,
		0x81C, 0x01F20001,
		0x81C, 0x01F40001,
		0x81C, 0x01F60001,
		0x81C, 0x01F80001,
		0x81C, 0x01FA0001,
		0x81C, 0x01FC0001,
		0x81C, 0x01FE0001,
	0xB0000000,0x00000000,
		0xC50, 0x00000022,
		0xC50, 0x00000020,
		0xE50, 0x00000022,
		0xE50, 0x00000020,

};

void
ODM_ReadAndConfig_MP_8812A_AGC_TAB(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
    u4Byte     i         = 0;
    u1Byte     cCond;
    BOOLEAN bMatched = TRUE, bSkipped = FALSE;
//ask by Luke.Lee
    u4Byte     ArrayLen    = sizeof(Array_MP_8812A_AGC_TAB)/sizeof(u4Byte);
    pu4Byte    Array       = Array_MP_8812A_AGC_TAB;
	
    ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_8812A_AGC_TAB\n"));

	while(( i+1) < ArrayLen)
	{
		u4Byte v1 = Array[i];
		u4Byte v2 = Array[i+1];

		if(v1 & (BIT31|BIT30)) //positive & negative condition
		{
			if(v1 & BIT31) // positive condition
			{
				cCond  = (u1Byte)((v1 & (BIT29|BIT28)) >> 28);
				if(cCond == COND_ENDIF) //end
				{
					bMatched = TRUE;
					bSkipped = FALSE;
				}
				else if(cCond == COND_ELSE) //else
				{
					bMatched = bSkipped?FALSE:TRUE;
				}
				else //if , else if
				{
					if(bSkipped)
						bMatched = FALSE;
					else
					{
						if(CheckPositive(pDM_Odm, v1, v2))
						{
							bMatched = TRUE;
							bSkipped = TRUE;
						}
						else
						{
							bMatched = FALSE;
							bSkipped = FALSE;
						}
					}
				}
			}
			else if(v1 & BIT30){ //negative condition
			//do nothing
			}
		}
		else
		{
			if(bMatched)
			odm_ConfigBB_AGC_8812A(pDM_Odm, v1, bMaskDWord, v2);
		}
	i = i + 2;
	}
}

u4Byte
ODM_GetVersion_MP_8812A_AGC_TAB(void)
{
	   return 49;
}

/******************************************************************************
*                           AGC_TAB_DIFF.TXT
******************************************************************************/

u4Byte Array_MP_8812A_AGC_TAB_DIFF_LB[] = { 
	0x80000004,0x00000000,0x40000000,0x00000000,
		0x81C, 0x47CE0001,
		0x81C, 0x46D00001,
		0x81C, 0x45D20001,
		0x81C, 0x44D40001,
		0x81C, 0x43D60001,
		0x81C, 0x42D80001,
		0x81C, 0x08DA0001,
		0x81C, 0x07DC0001,
		0x81C, 0x06DE0001,
		0x81C, 0x05E00001,
		0x81C, 0x04E20001,
		0x81C, 0x03E40001,
		0x81C, 0x02E60001,
	0xA0000000,0x00000000,
		0x81C, 0x47D80001,
		0x81C, 0x46DA0001,
		0x81C, 0x45DC0001,
		0x81C, 0x44DE0001,
		0x81C, 0x43E00001,
		0x81C, 0x42E20001,
		0x81C, 0x08E40001,
		0x81C, 0x07E60001,
		0x81C, 0x06E80001,
		0x81C, 0x05EA0001,
		0x81C, 0x04EC0001,
		0x81C, 0x03EE0001,
		0x81C, 0x02F00001,
	0xB0000000,0x00000000,
};

u4Byte Array_MP_8812A_AGC_TAB_DIFF_HB[] = { 
	0x80000004,0x00000000,0x40000000,0x00000000,
		0x81C, 0x45CE0001,
		0x81C, 0x44D00001,
		0x81C, 0x43D20001,
		0x81C, 0x42D40001,
		0x81C, 0x08D60001,
		0x81C, 0x07D80001,
		0x81C, 0x06DA0001,
		0x81C, 0x05DC0001,
		0x81C, 0x04DE0001,
		0x81C, 0x03E00001,
		0x81C, 0x02E20001,
		0x81C, 0x01E40001,
		0x81C, 0x01E60001,
	0xA0000000,0x00000000,
		0x81C, 0x45D80001,
		0x81C, 0x44DA0001,
		0x81C, 0x43DC0001,
		0x81C, 0x42DE0001,
		0x81C, 0x08E00001,
		0x81C, 0x07E20001,
		0x81C, 0x06E40001,
		0x81C, 0x05E60001,
		0x81C, 0x04E80001,
		0x81C, 0x03EA0001,
		0x81C, 0x02EC0001,
		0x81C, 0x01EE0001,
		0x81C, 0x01F00001,
	0xB0000000,0x00000000,
};

void
ODM_ReadAndConfig_MP_8812A_AGC_TAB_DIFF(
 	IN   PDM_ODM_T  pDM_Odm,
 	IN   u4Byte  	Array[],
 	IN   u4Byte  	ArrayLen 
 	)
{
    u4Byte     i         = 0;
    u1Byte     cCond;
    BOOLEAN bMatched = TRUE, bSkipped = FALSE;
//ask by Luke.Lee
	
    ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_8812A_AGC_TAB_DIFF\n"));

	while(( i+1) < ArrayLen)
	{
		u4Byte v1 = Array[i];
		u4Byte v2 = Array[i+1];

		if(v1 & (BIT31|BIT30)) //positive & negative condition
		{
			if(v1 & BIT31) // positive condition
			{
				cCond  = (u1Byte)((v1 & (BIT29|BIT28)) >> 28);
				if(cCond == COND_ENDIF) //end
				{
					bMatched = TRUE;
					bSkipped = FALSE;
				}
				else if(cCond == COND_ELSE) //else
				{
					bMatched = bSkipped?FALSE:TRUE;
				}
				else //if , else if
				{
					if(bSkipped)
						bMatched = FALSE;
					else
					{
						if(CheckPositive(pDM_Odm, v1, v2))
						{
							bMatched = TRUE;
							bSkipped = TRUE;
						}
						else
						{
							bMatched = FALSE;
							bSkipped = FALSE;
						}
					}
				}
			}
			else if(v1 & BIT30){ //negative condition
			//do nothing
			}
		}
		else
		{
			if(bMatched)
			odm_ConfigBB_AGC_8812A(pDM_Odm, v1, bMaskDWord, v2);
		}
	i = i + 2;
	}
}

u4Byte
ODM_GetVersion_MP_8812A_AGC_TAB_DIFF(void)
{
	   return 49;
}

/******************************************************************************
*                           PHY_REG.TXT
******************************************************************************/

u4Byte Array_MP_8812A_PHY_REG[] = { 
		0x800, 0x8020D010,
		0x804, 0x080112E0,
		0x808, 0x0E028233,
		0x80C, 0x12131113,
		0x810, 0x20101263,
		0x814, 0x020C3D10,
		0x818, 0x03A00385,
		0x820, 0x00000000,
		0x824, 0x00030FE0,
		0x828, 0x00000000,
		0x82C, 0x002083DD,
		0x830, 0x2EAAEEB8,
		0x834, 0x0037A706,
		0x838, 0x06C89B44,
		0x83C, 0x0000095B,
		0x840, 0xC0000001,
		0x844, 0x40003CDE,
		0x848, 0x6210FF8B,
		0x84C, 0x6CFDFFB8,
		0x850, 0x28874706,
		0x854, 0x0001520C,
		0x858, 0x8060E000,
		0x85C, 0x74210168,
		0x860, 0x6929C321,
		0x864, 0x79727432,
		0x868, 0x8CA7A314,
		0x86C, 0x338C2878,
		0x870, 0x03333333,
		0x874, 0x31602C2E,
		0x878, 0x00003152,
		0x87C, 0x000FC000,
		0x8A0, 0x00000013,
		0x8A4, 0x7F7F7F7F,
		0x8A8, 0xA202033E,
		0x8AC, 0x0FF0FA0A,
		0x8B0, 0x00000600,
		0x8B4, 0x000FC080,
		0x8B8, 0x6C0057FF,
		0x8BC, 0x4CA520A3,
		0x8C0, 0x27F00020,
		0x8C4, 0x00000000,
		0x8C8, 0x00012D69,
		0x8CC, 0x08248492,
		0x8D0, 0x0000B800,
		0x8DC, 0x00000000,
		0x8D4, 0x940008A0,
		0x8D8, 0x290B5612,
		0x8F8, 0x400002C0,
		0x8FC, 0x00000000,
		0x900, 0x00000701,
		0x90C, 0x00000000,
		0x910, 0x0000FC00,
		0x914, 0x00000404,
		0x918, 0x1C1028C0,
		0x91C, 0x64B11A1C,
		0x920, 0xE0767233,
		0x924, 0x055AA500,
		0x928, 0x00000004,
		0x92C, 0xFFFE0000,
		0x930, 0xFFFFFFFE,
		0x934, 0x001FFFFF,
		0x960, 0x00000000,
		0x964, 0x00000000,
		0x968, 0x00000000,
		0x96C, 0x00000000,
		0x970, 0x801FFFFF,
		0x978, 0x00000000,
		0x97C, 0x00000000,
		0x980, 0x00000000,
		0x984, 0x00000000,
		0x988, 0x00000000,
		0x990, 0x27100000,
		0x994, 0xFFFF0100,
		0x998, 0xFFFFFF5C,
		0x99C, 0xFFFFFFFF,
		0x9A0, 0x000000FF,
		0x9A4, 0x00080080,
		0x9A8, 0x00000000,
		0x9AC, 0x00000000,
		0x9B0, 0x81081008,
		0x9B4, 0x00000000,
		0x9B8, 0x01081008,
		0x9BC, 0x01081008,
		0x9D0, 0x00000000,
		0x9D4, 0x00000000,
		0x9D8, 0x00000000,
		0x9DC, 0x00000000,
		0x9E4, 0x00000003,
		0x9E8, 0x000002D5,
		0xA00, 0x00D047C8,
		0xA04, 0x01FF000C,
		0xA08, 0x8C838300,
		0xA0C, 0x2E7F000F,
		0xA10, 0x9500BB78,
		0xA14, 0x11144028,
		0xA18, 0x00881117,
		0xA1C, 0x89140F00,
		0xA20, 0x1A1B0000,
		0xA24, 0x090E1317,
		0xA28, 0x00000204,
		0xA2C, 0x00900000,
		0xA70, 0x101FFF00,
		0xA74, 0x00000008,
		0xA78, 0x00000900,
		0xA7C, 0x225B0606,
		0xA80, 0x218075B2,
		0xA84, 0x001F8C80,
		0xB00, 0x03100000,
		0xB04, 0x0000B000,
		0xB08, 0xAE0201EB,
		0xB0C, 0x01003207,
		0xB10, 0x00009807,
		0xB14, 0x01000000,
		0xB18, 0x00000002,
		0xB1C, 0x00000002,
		0xB20, 0x0000001F,
		0xB24, 0x03020100,
		0xB28, 0x07060504,
		0xB2C, 0x0B0A0908,
		0xB30, 0x0F0E0D0C,
		0xB34, 0x13121110,
		0xB38, 0x17161514,
		0xB3C, 0x0000003A,
		0xB40, 0x00000000,
		0xB44, 0x00000000,
		0xB48, 0x13000032,
		0xB4C, 0x48080000,
		0xB50, 0x00000000,
		0xB54, 0x00000000,
		0xB58, 0x00000000,
		0xB5C, 0x00000000,
		0xC00, 0x00000007,
		0xC04, 0x00042020,
		0xC08, 0x80410231,
		0xC0C, 0x00000000,
		0xC10, 0x00000100,
		0xC14, 0x01000000,
		0xC1C, 0x40000003,
		0xC20, 0x12121212,
		0xC24, 0x12121212,
		0xC28, 0x12121212,
		0xC2C, 0x12121212,
		0xC30, 0x12121212,
		0xC34, 0x12121212,
		0xC38, 0x12121212,
		0xC3C, 0x12121212,
		0xC40, 0x12121212,
		0xC44, 0x12121212,
		0xC48, 0x12121212,
		0xC4C, 0x12121212,
		0xC50, 0x00000020,
		0xC54, 0x0008121C,
		0xC58, 0x30000C1C,
		0xC5C, 0x00000058,
		0xC60, 0x34344443,
		0xC64, 0x07003333,
	0x80000008,0x00000000,0x40000000,0x00000000,
		0xC68, 0x59791979,
	0x90000008,0x05000000,0x40000000,0x00000000,
		0xC68, 0x59791979,
	0x90000002,0x00000000,0x40000000,0x00000000,
		0xC68, 0x59791979,
	0x90000004,0x00000000,0x40000000,0x00000000,
		0xC68, 0x59791979,
	0x90000001,0x00000000,0x40000000,0x00000000,
		0xC68, 0x59791979,
	0x90000001,0x00000005,0x40000000,0x00000000,
		0xC68, 0x59791979,
	0xA0000000,0x00000000,
		0xC68, 0x59799979,
	0xB0000000,0x00000000,
		0xC6C, 0x59795979,
		0xC70, 0x19795979,
		0xC74, 0x19795979,
		0xC78, 0x19791979,
		0xC7C, 0x19791979,
		0xC80, 0x19791979,
		0xC84, 0x19791979,
		0xC94, 0x0100005C,
		0xC98, 0x00000000,
		0xC9C, 0x00000000,
		0xCA0, 0x00000029,
		0xCA4, 0x08040201,
		0xCA8, 0x80402010,
		0xCB0, 0x77547777,
		0xCB4, 0x00000077,
		0xCB8, 0x00508242,
		0xE00, 0x00000007,
		0xE04, 0x00042020,
		0xE08, 0x80410231,
		0xE0C, 0x00000000,
		0xE10, 0x00000100,
		0xE14, 0x01000000,
		0xE1C, 0x40000003,
		0xE20, 0x12121212,
		0xE24, 0x12121212,
		0xE28, 0x12121212,
		0xE2C, 0x12121212,
		0xE30, 0x12121212,
		0xE34, 0x12121212,
		0xE38, 0x12121212,
		0xE3C, 0x12121212,
		0xE40, 0x12121212,
		0xE44, 0x12121212,
		0xE48, 0x12121212,
		0xE4C, 0x12121212,
		0xE50, 0x00000020,
		0xE54, 0x0008121C,
		0xE58, 0x30000C1C,
		0xE5C, 0x00000058,
		0xE60, 0x34344443,
		0xE64, 0x07003333,
		0xE68, 0x59791979,
		0xE6C, 0x59795979,
		0xE70, 0x19795979,
		0xE74, 0x19795979,
		0xE78, 0x19791979,
		0xE7C, 0x19791979,
		0xE80, 0x19791979,
		0xE84, 0x19791979,
		0xE94, 0x0100005C,
		0xE98, 0x00000000,
		0xE9C, 0x00000000,
		0xEA0, 0x00000029,
		0xEA4, 0x08040201,
		0xEA8, 0x80402010,
		0xEB0, 0x77547777,
		0xEB4, 0x00000077,
		0xEB8, 0x00508242,

};

void
ODM_ReadAndConfig_MP_8812A_PHY_REG(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
    u4Byte     i         = 0;
    u1Byte     cCond;
    BOOLEAN bMatched = TRUE, bSkipped = FALSE;
//ask by Luke.Lee
    u4Byte     ArrayLen    = sizeof(Array_MP_8812A_PHY_REG)/sizeof(u4Byte);
    pu4Byte    Array       = Array_MP_8812A_PHY_REG;
	
    ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_8812A_PHY_REG\n"));

	while(( i+1) < ArrayLen)
	{
		u4Byte v1 = Array[i];
		u4Byte v2 = Array[i+1];

		if(v1 & (BIT31|BIT30)) //positive & negative condition
		{
			if(v1 & BIT31) // positive condition
			{
				cCond  = (u1Byte)((v1 & (BIT29|BIT28)) >> 28);
				if(cCond == COND_ENDIF) //end
				{
					bMatched = TRUE;
					bSkipped = FALSE;
				}
				else if(cCond == COND_ELSE) //else
				{
					bMatched = bSkipped?FALSE:TRUE;
				}
				else //if , else if
				{
					if(bSkipped)
						bMatched = FALSE;
					else
					{
						if(CheckPositive(pDM_Odm, v1, v2))
						{
							bMatched = TRUE;
							bSkipped = TRUE;
						}
						else
						{
							bMatched = FALSE;
							bSkipped = FALSE;
						}
					}
				}
			}
			else if(v1 & BIT30){ //negative condition
			//do nothing
			}
		}
		else
		{
			if(bMatched)
			odm_ConfigBB_PHY_8812A(pDM_Odm, v1, bMaskDWord, v2);
		}
	i = i + 2;
	}
}

u4Byte
ODM_GetVersion_MP_8812A_PHY_REG(void)
{
	   return 49;
}

/******************************************************************************
*                           PHY_REG_MP.TXT
******************************************************************************/

u4Byte Array_MP_8812A_PHY_REG_MP[] = { 
		0x800, 0x8020D410,
		0x830, 0x2EAA8EB8,

};

void
ODM_ReadAndConfig_MP_8812A_PHY_REG_MP(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
    u4Byte     i         = 0;
    u1Byte     cCond;
    BOOLEAN bMatched = TRUE, bSkipped = FALSE;
//ask by Luke.Lee
    u4Byte     ArrayLen    = sizeof(Array_MP_8812A_PHY_REG_MP)/sizeof(u4Byte);
    pu4Byte    Array       = Array_MP_8812A_PHY_REG_MP;
	
    ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_8812A_PHY_REG_MP\n"));

	while(( i+1) < ArrayLen)
	{
		u4Byte v1 = Array[i];
		u4Byte v2 = Array[i+1];

		if(v1 & (BIT31|BIT30)) //positive & negative condition
		{
			if(v1 & BIT31) // positive condition
			{
				cCond  = (u1Byte)((v1 & (BIT29|BIT28)) >> 28);
				if(cCond == COND_ENDIF) //end
				{
					bMatched = TRUE;
					bSkipped = FALSE;
				}
				else if(cCond == COND_ELSE) //else
				{
					bMatched = bSkipped?FALSE:TRUE;
				}
				else //if , else if
				{
					if(bSkipped)
						bMatched = FALSE;
					else
					{
						if(CheckPositive(pDM_Odm, v1, v2))
						{
							bMatched = TRUE;
							bSkipped = TRUE;
						}
						else
						{
							bMatched = FALSE;
							bSkipped = FALSE;
						}
					}
				}
			}
			else if(v1 & BIT30){ //negative condition
			//do nothing
			}
		}
		else
		{
			if(bMatched)
			odm_ConfigBB_PHY_8812A(pDM_Odm, v1, bMaskDWord, v2);
		}
	i = i + 2;
	}
}

u4Byte
ODM_GetVersion_MP_8812A_PHY_REG_MP(void)
{
	   return 49;
}

/******************************************************************************
*                           PHY_REG_PG.TXT
******************************************************************************/

u4Byte Array_MP_8812A_PHY_REG_PG[] = { 
	0, 0, 0, 0x00000c20, 0xffffffff, 0x34363840,
	0, 0, 0, 0x00000c24, 0xffffffff, 0x42424444,
	0, 0, 0, 0x00000c28, 0xffffffff, 0x30323638,
	0, 0, 0, 0x00000c2c, 0xffffffff, 0x40424444,
	0, 0, 0, 0x00000c30, 0xffffffff, 0x28303236,
	0, 0, 1, 0x00000c34, 0xffffffff, 0x38404242,
	0, 0, 1, 0x00000c38, 0xffffffff, 0x26283034,
	0, 0, 0, 0x00000c3c, 0xffffffff, 0x40424444,
	0, 0, 0, 0x00000c40, 0xffffffff, 0x28303236,
	0, 0, 0, 0x00000c44, 0xffffffff, 0x42422426,
	0, 0, 1, 0x00000c48, 0xffffffff, 0x30343840,
	0, 0, 1, 0x00000c4c, 0xffffffff, 0x22242628,
	0, 1, 0, 0x00000e20, 0xffffffff, 0x34363840,
	0, 1, 0, 0x00000e24, 0xffffffff, 0x42424444,
	0, 1, 0, 0x00000e28, 0xffffffff, 0x30323638,
	0, 1, 0, 0x00000e2c, 0xffffffff, 0x40424444,
	0, 1, 0, 0x00000e30, 0xffffffff, 0x28303236,
	0, 1, 1, 0x00000e34, 0xffffffff, 0x38404242,
	0, 1, 1, 0x00000e38, 0xffffffff, 0x26283034,
	0, 1, 0, 0x00000e3c, 0xffffffff, 0x40424444,
	0, 1, 0, 0x00000e40, 0xffffffff, 0x28303236,
	0, 1, 0, 0x00000e44, 0xffffffff, 0x42422426,
	0, 1, 1, 0x00000e48, 0xffffffff, 0x30343840,
	0, 1, 1, 0x00000e4c, 0xffffffff, 0x22242628,
	1, 0, 0, 0x00000c24, 0xffffffff, 0x42424444,
	1, 0, 0, 0x00000c28, 0xffffffff, 0x30323640,
	1, 0, 0, 0x00000c2c, 0xffffffff, 0x40424444,
	1, 0, 0, 0x00000c30, 0xffffffff, 0x28303236,
	1, 0, 1, 0x00000c34, 0xffffffff, 0x38404242,
	1, 0, 1, 0x00000c38, 0xffffffff, 0x26283034,
	1, 0, 0, 0x00000c3c, 0xffffffff, 0x40424444,
	1, 0, 0, 0x00000c40, 0xffffffff, 0x28303236,
	1, 0, 0, 0x00000c44, 0xffffffff, 0x42422426,
	1, 0, 1, 0x00000c48, 0xffffffff, 0x30343840,
	1, 0, 1, 0x00000c4c, 0xffffffff, 0x22242628,
	1, 1, 0, 0x00000e24, 0xffffffff, 0x42424444,
	1, 1, 0, 0x00000e28, 0xffffffff, 0x30323640,
	1, 1, 0, 0x00000e2c, 0xffffffff, 0x40424444,
	1, 1, 0, 0x00000e30, 0xffffffff, 0x28303236,
	1, 1, 1, 0x00000e34, 0xffffffff, 0x38404242,
	1, 1, 1, 0x00000e38, 0xffffffff, 0x26283034,
	1, 1, 0, 0x00000e3c, 0xffffffff, 0x40424444,
	1, 1, 0, 0x00000e40, 0xffffffff, 0x28303236,
	1, 1, 0, 0x00000e44, 0xffffffff, 0x42422426,
	1, 1, 1, 0x00000e48, 0xffffffff, 0x30343840,
	1, 1, 1, 0x00000e4c, 0xffffffff, 0x22242628
};

void
ODM_ReadAndConfig_MP_8812A_PHY_REG_PG(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
    u4Byte     i         = 0;
    u4Byte     ArrayLen    = sizeof(Array_MP_8812A_PHY_REG_PG)/sizeof(u4Byte);
    pu4Byte    Array       = Array_MP_8812A_PHY_REG_PG;

    ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_8812A_PHY_REG_PG\n"));

	pDM_Odm->PhyRegPgVersion = 1;
	pDM_Odm->PhyRegPgValueType = PHY_REG_PG_EXACT_VALUE;

	for (i = 0; i < ArrayLen; i += 6 )
	{
	    u4Byte v1 = Array[i];
	    u4Byte v2 = Array[i+1];
	    u4Byte v3 = Array[i+2];
	    u4Byte v4 = Array[i+3];
	    u4Byte v5 = Array[i+4];
	    u4Byte v6 = Array[i+5];

	    odm_ConfigBB_PHY_REG_PG_8812A(pDM_Odm, v1, v2, v3, v4, v5, v6);
	}
}



/******************************************************************************
*                           PHY_REG_PG_ASUS.TXT
******************************************************************************/

u4Byte Array_MP_8812A_PHY_REG_PG_ASUS[] = { 
	0, 0, 0, 0x00000c20, 0xffffffff, 0x34343434,
	0, 0, 0, 0x00000c24, 0xffffffff, 0x32323232,
	0, 0, 0, 0x00000c28, 0xffffffff, 0x28303232,
	0, 0, 0, 0x00000c2c, 0xffffffff, 0x32323232,
	0, 0, 0, 0x00000c30, 0xffffffff, 0x28303232,
	0, 0, 1, 0x00000c34, 0xffffffff, 0x32323232,
	0, 0, 1, 0x00000c38, 0xffffffff, 0x26283032,
	0, 0, 0, 0x00000c3c, 0xffffffff, 0x32323232,
	0, 0, 0, 0x00000c40, 0xffffffff, 0x28303232,
	0, 0, 0, 0x00000c44, 0xffffffff, 0x32322426,
	0, 0, 1, 0x00000c48, 0xffffffff, 0x32323232,
	0, 0, 1, 0x00000c4c, 0xffffffff, 0x24262830,
	0, 1, 0, 0x00000e20, 0xffffffff, 0x34343434,
	0, 1, 0, 0x00000e24, 0xffffffff, 0x32323232,
	0, 1, 0, 0x00000e28, 0xffffffff, 0x28303232,
	0, 1, 0, 0x00000e2c, 0xffffffff, 0x32323232,
	0, 1, 0, 0x00000e30, 0xffffffff, 0x28303232,
	0, 1, 1, 0x00000e34, 0xffffffff, 0x32323232,
	0, 1, 1, 0x00000e38, 0xffffffff, 0x26283032,
	0, 1, 0, 0x00000e3c, 0xffffffff, 0x32323232,
	0, 1, 0, 0x00000e40, 0xffffffff, 0x28303232,
	0, 1, 0, 0x00000e44, 0xffffffff, 0x32322426,
	0, 1, 1, 0x00000e48, 0xffffffff, 0x32323232,
	0, 1, 1, 0x00000e4c, 0xffffffff, 0x24262830,
	1, 0, 0, 0x00000c24, 0xffffffff, 0x32323232,
	1, 0, 0, 0x00000c28, 0xffffffff, 0x28303232,
	1, 0, 0, 0x00000c2c, 0xffffffff, 0x32323232,
	1, 0, 0, 0x00000c30, 0xffffffff, 0x24262830,
	1, 0, 1, 0x00000c34, 0xffffffff, 0x32323232,
	1, 0, 1, 0x00000c38, 0xffffffff, 0x24262830,
	1, 0, 0, 0x00000c3c, 0xffffffff, 0x32323232,
	1, 0, 0, 0x00000c40, 0xffffffff, 0x24262830,
	1, 0, 0, 0x00000c44, 0xffffffff, 0x32322222,
	1, 0, 1, 0x00000c48, 0xffffffff, 0x28303232,
	1, 0, 1, 0x00000c4c, 0xffffffff, 0x22222426,
	1, 1, 0, 0x00000e24, 0xffffffff, 0x32323232,
	1, 1, 0, 0x00000e28, 0xffffffff, 0x28303232,
	1, 1, 0, 0x00000e2c, 0xffffffff, 0x32323232,
	1, 1, 0, 0x00000e30, 0xffffffff, 0x24262830,
	1, 1, 1, 0x00000e34, 0xffffffff, 0x32323232,
	1, 1, 1, 0x00000e38, 0xffffffff, 0x24262830,
	1, 1, 0, 0x00000e3c, 0xffffffff, 0x32323232,
	1, 1, 0, 0x00000e40, 0xffffffff, 0x24262830,
	1, 1, 0, 0x00000e44, 0xffffffff, 0x32322222,
	1, 1, 1, 0x00000e48, 0xffffffff, 0x28303232,
	1, 1, 1, 0x00000e4c, 0xffffffff, 0x22222426
};

void
ODM_ReadAndConfig_MP_8812A_PHY_REG_PG_ASUS(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
    u4Byte     i         = 0;
    u4Byte     ArrayLen    = sizeof(Array_MP_8812A_PHY_REG_PG_ASUS)/sizeof(u4Byte);
    pu4Byte    Array       = Array_MP_8812A_PHY_REG_PG_ASUS;

    ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_8812A_PHY_REG_PG_ASUS\n"));

	pDM_Odm->PhyRegPgVersion = 1;
	pDM_Odm->PhyRegPgValueType = PHY_REG_PG_EXACT_VALUE;

	for (i = 0; i < ArrayLen; i += 6 )
	{
	    u4Byte v1 = Array[i];
	    u4Byte v2 = Array[i+1];
	    u4Byte v3 = Array[i+2];
	    u4Byte v4 = Array[i+3];
	    u4Byte v5 = Array[i+4];
	    u4Byte v6 = Array[i+5];

	    odm_ConfigBB_PHY_REG_PG_8812A(pDM_Odm, v1, v2, v3, v4, v5, v6);
	}
}



/******************************************************************************
*                           PHY_REG_PG_NEC.TXT
******************************************************************************/

u4Byte Array_MP_8812A_PHY_REG_PG_NEC[] = { 
	0, 0, 0, 0x00000c20, 0xffffffff, 0x32323232,
	0, 0, 0, 0x00000c24, 0xffffffff, 0x32343434,
	0, 0, 0, 0x00000c28, 0xffffffff, 0x24262830,
	0, 0, 0, 0x00000c2c, 0xffffffff, 0x32343434,
	0, 0, 0, 0x00000c30, 0xffffffff, 0x24262830,
	0, 0, 1, 0x00000c34, 0xffffffff, 0x32343434,
	0, 0, 1, 0x00000c38, 0xffffffff, 0x24262830,
	0, 0, 0, 0x00000c3c, 0xffffffff, 0x32343434,
	0, 0, 0, 0x00000c40, 0xffffffff, 0x24262830,
	0, 0, 0, 0x00000c44, 0xffffffff, 0x34342022,
	0, 0, 1, 0x00000c48, 0xffffffff, 0x28303234,
	0, 0, 1, 0x00000c4c, 0xffffffff, 0x20222426,
	0, 1, 0, 0x00000e20, 0xffffffff, 0x32323232,
	0, 1, 0, 0x00000e24, 0xffffffff, 0x32343434,
	0, 1, 0, 0x00000e28, 0xffffffff, 0x24262830,
	0, 1, 0, 0x00000e2c, 0xffffffff, 0x32343434,
	0, 1, 0, 0x00000e30, 0xffffffff, 0x24262830,
	0, 1, 1, 0x00000e34, 0xffffffff, 0x32343434,
	0, 1, 1, 0x00000e38, 0xffffffff, 0x24262830,
	0, 1, 0, 0x00000e3c, 0xffffffff, 0x32343434,
	0, 1, 0, 0x00000e40, 0xffffffff, 0x24262830,
	0, 1, 0, 0x00000e44, 0xffffffff, 0x34342022,
	0, 1, 1, 0x00000e48, 0xffffffff, 0x28303234,
	0, 1, 1, 0x00000e4c, 0xffffffff, 0x20222426,
	1, 0, 0, 0x00000c24, 0xffffffff, 0x32343434,
	1, 0, 0, 0x00000c28, 0xffffffff, 0x24262830,
	1, 0, 0, 0x00000c2c, 0xffffffff, 0x32343434,
	1, 0, 0, 0x00000c30, 0xffffffff, 0x24262830,
	1, 0, 1, 0x00000c34, 0xffffffff, 0x28282828,
	1, 0, 1, 0x00000c38, 0xffffffff, 0x24262828,
	1, 0, 0, 0x00000c3c, 0xffffffff, 0x32343434,
	1, 0, 0, 0x00000c40, 0xffffffff, 0x24262830,
	1, 0, 0, 0x00000c44, 0xffffffff, 0x28282022,
	1, 0, 1, 0x00000c48, 0xffffffff, 0x28282828,
	1, 0, 1, 0x00000c4c, 0xffffffff, 0x20222426,
	1, 1, 0, 0x00000e24, 0xffffffff, 0x32343434,
	1, 1, 0, 0x00000e28, 0xffffffff, 0x24262830,
	1, 1, 0, 0x00000e2c, 0xffffffff, 0x32343434,
	1, 1, 0, 0x00000e30, 0xffffffff, 0x24262830,
	1, 1, 1, 0x00000e34, 0xffffffff, 0x28282828,
	1, 1, 1, 0x00000e38, 0xffffffff, 0x24262828,
	1, 1, 0, 0x00000e3c, 0xffffffff, 0x32343434,
	1, 1, 0, 0x00000e40, 0xffffffff, 0x24262830,
	1, 1, 0, 0x00000e44, 0xffffffff, 0x28282022,
	1, 1, 1, 0x00000e48, 0xffffffff, 0x28282828,
	1, 1, 1, 0x00000e4c, 0xffffffff, 0x20222426
};

void
ODM_ReadAndConfig_MP_8812A_PHY_REG_PG_NEC(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
    u4Byte     i         = 0;
    u4Byte     ArrayLen    = sizeof(Array_MP_8812A_PHY_REG_PG_NEC)/sizeof(u4Byte);
    pu4Byte    Array       = Array_MP_8812A_PHY_REG_PG_NEC;

    ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_8812A_PHY_REG_PG_NEC\n"));

	pDM_Odm->PhyRegPgVersion = 1;
	pDM_Odm->PhyRegPgValueType = PHY_REG_PG_EXACT_VALUE;

	for (i = 0; i < ArrayLen; i += 6 )
	{
	    u4Byte v1 = Array[i];
	    u4Byte v2 = Array[i+1];
	    u4Byte v3 = Array[i+2];
	    u4Byte v4 = Array[i+3];
	    u4Byte v5 = Array[i+4];
	    u4Byte v6 = Array[i+5];

	    odm_ConfigBB_PHY_REG_PG_8812A(pDM_Odm, v1, v2, v3, v4, v5, v6);
	}
}



#endif // end of HWIMG_SUPPORT

