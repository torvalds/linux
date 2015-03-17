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

#include "../odm_precomp.h"

#if (RTL8812A_SUPPORT == 1)
static BOOLEAN
CheckPositive(
    IN  PDM_ODM_T     pDM_Odm,
    IN  const u4Byte  Condition1,
    IN  const u4Byte  Condition2
    )
{
     u1Byte    _GLNA        = (pDM_Odm->BoardType & BIT4) >> 4;
     u1Byte    _GPA         = (pDM_Odm->BoardType & BIT3) >> 3;
     u1Byte    _ALNA        = (pDM_Odm->BoardType & BIT7) >> 7;
     u1Byte    _APA         = (pDM_Odm->BoardType & BIT6) >> 6;
     
     u1Byte     cBoard      = (u1Byte)((Condition1 &  bMaskByte0)               >>  0);
     u1Byte     cInterface  = (u1Byte)((Condition1 & (BIT11|BIT10|BIT9|BIT8))   >>  8);
     u1Byte     cPackage    = (u1Byte)((Condition1 & (BIT15|BIT14|BIT13|BIT12)) >> 12);
     u1Byte     cPlatform   = (u1Byte)((Condition1 & (BIT19|BIT18|BIT17|BIT16)) >> 16);
     u1Byte     cCut        = (u1Byte)((Condition1 & (BIT27|BIT26|BIT25|BIT24)) >> 24);
     u1Byte     cGLNA       = (cBoard & BIT0) >> 0;
     u1Byte     cGPA        = (cBoard & BIT1) >> 1;
     u1Byte     cALNA       = (cBoard & BIT2) >> 2;
     u1Byte     cAPA        = (cBoard & BIT3) >> 3;
     u1Byte     cTypeGLNA   = (u1Byte)((Condition2 & bMaskByte0) >>  0);
     u1Byte     cTypeGPA    = (u1Byte)((Condition2 & bMaskByte1) >>  8);
     u1Byte     cTypeALNA   = (u1Byte)((Condition2 & bMaskByte2) >> 16);
     u1Byte     cTypeAPA    = (u1Byte)((Condition2 & bMaskByte3) >> 24);
     
     ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, 
                 ("===> [8812A] CheckPositive(0x%X 0x%X)\n", Condition1, Condition2));
     ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, 
                 ("	(Platform, Interface) = (0x%X, 0x%X)", pDM_Odm->SupportPlatform, pDM_Odm->SupportInterface));
     ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, 
                 ("	(Board, Package) = (0x%X, 0x%X\n", pDM_Odm->BoardType, pDM_Odm->PackageType));
     
     if ((cPlatform  != pDM_Odm->SupportPlatform  && cPlatform  != 0) || 
         (cInterface != pDM_Odm->SupportInterface && cInterface != 0) || 
         (cCut       != pDM_Odm->CutVersion       && cCut       != 0))
         return FALSE;
     
     if (cPackage != pDM_Odm->PackageType && cPackage != 0)
         return FALSE;
     
     if (((_GLNA != 0) && (_GLNA == cGLNA) && (cTypeGLNA == pDM_Odm->TypeGLNA)) ||
         ((_GPA  != 0) && (_GPA  == cGPA ) && (cTypeGPA  == pDM_Odm->TypeGPA )) ||
         ((_ALNA != 0) && (_ALNA == cALNA) && (cTypeALNA == pDM_Odm->TypeALNA)) ||
         ((_APA  != 0) && (_APA  == cAPA ) && (cTypeAPA  == pDM_Odm->TypeAPA )))
         return TRUE;
     else 
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

u4Byte Array_MP_8812A_MAC_REG[] = { 
		0x010, 0x0000000C,
		0x025, 0x0000000F,
		0x072, 0x00000000,
		0x428, 0x0000000A,
		0x429, 0x00000010,
		0x430, 0x00000000,
		0x431, 0x00000000,
		0x432, 0x00000000,
		0x433, 0x00000001,
		0x434, 0x00000004,
		0x435, 0x00000005,
		0x436, 0x00000007,
		0x437, 0x00000008,
		0x43C, 0x00000004,
		0x43D, 0x00000005,
		0x43E, 0x00000007,
		0x43F, 0x00000008,
		0x440, 0x0000005D,
		0x441, 0x00000001,
		0x442, 0x00000000,
		0x444, 0x00000010,
		0x445, 0x00000000,
		0x446, 0x00000000,
		0x447, 0x00000000,
		0x448, 0x00000000,
		0x449, 0x000000F0,
		0x44A, 0x0000000F,
		0x44B, 0x0000003E,
		0x44C, 0x00000010,
		0x44D, 0x00000000,
		0x44E, 0x00000000,
		0x44F, 0x00000000,
		0x450, 0x00000000,
		0x451, 0x000000F0,
		0x452, 0x0000000F,
		0x453, 0x00000000,
		0x45B, 0x00000080,
		0x460, 0x00000066,
		0x461, 0x00000066,
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
		0x516, 0x0000000A,
		0x525, 0x0000004F,
		0x550, 0x00000010,
		0x551, 0x00000010,
		0x559, 0x00000002,
		0x55C, 0x00000050,
		0x55D, 0x000000FF,
		0x604, 0x00000001,
		0x605, 0x00000030,
		0x607, 0x00000003,
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
		0x638, 0x00000050,
		0x63C, 0x0000000A,
		0x63D, 0x0000000A,
		0x63E, 0x0000000E,
		0x63F, 0x0000000E,
		0x640, 0x00000080,
		0x642, 0x00000040,
		0x643, 0x00000000,
		0x652, 0x000000C8,
		0x66E, 0x00000005,
		0x700, 0x00000021,
		0x701, 0x00000043,
		0x702, 0x00000065,
		0x703, 0x00000087,
		0x708, 0x00000021,
		0x709, 0x00000043,
		0x70A, 0x00000065,
		0x70B, 0x00000087,
		0x718, 0x00000040,

};

void
ODM_ReadAndConfig_MP_8812A_MAC_REG(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
    #define READ_NEXT_PAIR(v1, v2, i) do { i += 2; v1 = Array[i]; v2 = Array[i+1]; } while(0)
    #define COND_ELSE  2
    #define COND_ENDIF 3
    u4Byte     i         = 0;
    u4Byte     ArrayLen    = sizeof(Array_MP_8812A_MAC_REG)/sizeof(u4Byte);
    pu4Byte    Array       = Array_MP_8812A_MAC_REG;
	
    ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_8812A_MAC_REG\n"));

    for (i = 0; i < ArrayLen; i += 2 )
    {
        u4Byte v1 = Array[i];
        u4Byte v2 = Array[i+1];
    
        // This (offset, data) pair doesn't care the condition.
        if ( v1 < 0x40000000 )
        {
           odm_ConfigMAC_8812A(pDM_Odm, v1, (u1Byte)v2);
           continue;
        }
        else
        {   // This line is the beginning of branch.
            BOOLEAN bMatched = TRUE;
            u1Byte  cCond  = (u1Byte)((v1 & (BIT29|BIT28)) >> 28);

            if (cCond == COND_ELSE) { // ELSE, ENDIF
                bMatched = TRUE;
                READ_NEXT_PAIR(v1, v2, i);
            } else if ( ! CheckPositive(pDM_Odm, v1, v2) ) { 
                bMatched = FALSE;
                READ_NEXT_PAIR(v1, v2, i);
                READ_NEXT_PAIR(v1, v2, i);
            } else {
                READ_NEXT_PAIR(v1, v2, i);
                if ( ! CheckNegative(pDM_Odm, v1, v2) )
                    bMatched = FALSE;
                else
                    bMatched = TRUE;
                READ_NEXT_PAIR(v1, v2, i);
            }

            if ( bMatched == FALSE )
            {   // Condition isn't matched. Discard the following (offset, data) pairs.
                while (v1 < 0x40000000 && i < ArrayLen -2)
                    READ_NEXT_PAIR(v1, v2, i);

                i -= 2; // prevent from for-loop += 2
            }
            else // Configure matched pairs and skip to end of if-else.
            {
                while (v1 < 0x40000000 && i < ArrayLen-2) {
                    odm_ConfigMAC_8812A(pDM_Odm, v1, (u1Byte)v2);
                    READ_NEXT_PAIR(v1, v2, i);
                }

                // Keeps reading until ENDIF.
                cCond = (u1Byte)((v1 & (BIT29|BIT28)) >> 28);
                while (cCond != COND_ENDIF && i < ArrayLen-2) {
                    READ_NEXT_PAIR(v1, v2, i);
                    cCond = (u1Byte)((v1 & (BIT29|BIT28)) >> 28);
                }
            }
        } 
    }
}

u4Byte
ODM_GetVersion_MP_8812A_MAC_REG(
)
{
	   return 40;
}

#endif // end of HWIMG_SUPPORT

