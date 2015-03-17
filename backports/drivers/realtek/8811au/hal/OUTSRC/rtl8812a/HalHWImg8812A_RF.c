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
*                           RadioA.TXT
******************************************************************************/

u4Byte Array_MP_8812A_RadioA[] = { 
		0x000, 0x00010000,
		0x018, 0x0001712A,
		0x056, 0x00051CF2,
		0x066, 0x00040000,
		0x01E, 0x00080000,
		0x089, 0x00000080,
	0x80000001,0x00000000,0x40000000,0x00000000,
		0x086, 0x00014B3A,
	0x90000001,0x00000005,0x40000000,0x00000000,
		0x086, 0x00014B3A,
	0xA0000000,0x00000000,
		0x086, 0x00014B38,
	0xB0000000,0x00000000,
	0x80000004,0x00000000,0x40000000,0x00000000,
		0x08B, 0x00080180,
	0xA0000000,0x00000000,
		0x08B, 0x00087180,
	0xB0000000,0x00000000,
		0x0B1, 0x0001FC1A,
		0x0B3, 0x000F0810,
		0x0B4, 0x0001A78D,
		0x0BA, 0x00086180,
		0x018, 0x00000006,
		0x0EF, 0x00002000,
	0x80000001,0x00000000,0x40000000,0x00000000,
		0x03B, 0x0003F218,
		0x03B, 0x00030A58,
		0x03B, 0x0002FA58,
		0x03B, 0x00022590,
		0x03B, 0x0001FA50,
		0x03B, 0x00010248,
		0x03B, 0x00008240,
	0x90000001,0x00000005,0x40000000,0x00000000,
		0x03B, 0x0003F218,
		0x03B, 0x00030A58,
		0x03B, 0x0002FA58,
		0x03B, 0x00022590,
		0x03B, 0x0001FA50,
		0x03B, 0x00010248,
		0x03B, 0x00008240,
	0xA0000000,0x00000000,
		0x03B, 0x00038A58,
		0x03B, 0x00037A58,
		0x03B, 0x0002A590,
		0x03B, 0x00027A50,
		0x03B, 0x00018248,
		0x03B, 0x00010240,
		0x03B, 0x00008240,
	0xB0000000,0x00000000,
		0x0EF, 0x00000100,
	0x80000002,0x00000000,0x40000000,0x00000000,
		0x034, 0x0000A4EE,
		0x034, 0x00009076,
		0x034, 0x00008073,
		0x034, 0x00007070,
		0x034, 0x0000606D,
		0x034, 0x0000506A,
		0x034, 0x00004049,
		0x034, 0x00003046,
		0x034, 0x00002028,
		0x034, 0x00001025,
		0x034, 0x00000022,
	0xA0000000,0x00000000,
		0x034, 0x0000ADF4,
		0x034, 0x00009DF1,
		0x034, 0x00008DEE,
		0x034, 0x00007DEB,
		0x034, 0x00006DE8,
		0x034, 0x00005CEC,
		0x034, 0x00004CE9,
		0x034, 0x000034EA,
		0x034, 0x000024E7,
		0x034, 0x0000146B,
		0x034, 0x0000006D,
	0xB0000000,0x00000000,
		0x0EF, 0x00000000,
		0x0EF, 0x000020A2,
		0x0DF, 0x00000080,
		0x035, 0x00000192,
		0x035, 0x00008192,
		0x035, 0x00010192,
		0x036, 0x00000024,
		0x036, 0x00008024,
		0x036, 0x00010024,
		0x036, 0x00018024,
		0x0EF, 0x00000000,
		0x051, 0x00000C21,
		0x052, 0x000006D9,
		0x053, 0x000FC649,
		0x054, 0x0000017E,
		0x0EF, 0x00000002,
		0x008, 0x00008400,
		0x018, 0x0001712A,
		0x0EF, 0x00001000,
		0x03A, 0x00000080,
		0x03B, 0x0003A02C,
		0x03C, 0x00004000,
		0x03A, 0x00000400,
		0x03B, 0x0003202C,
		0x03C, 0x00010000,
		0x03A, 0x000000A0,
		0x03B, 0x0002B064,
		0x03C, 0x00004000,
		0x03A, 0x000000D8,
		0x03B, 0x00023070,
		0x03C, 0x00004000,
		0x03A, 0x00000468,
		0x03B, 0x0001B870,
		0x03C, 0x00010000,
		0x03A, 0x00000098,
		0x03B, 0x00012085,
		0x03C, 0x000E4000,
		0x03A, 0x00000418,
		0x03B, 0x0000A080,
		0x03C, 0x000F0000,
		0x03A, 0x00000418,
		0x03B, 0x00002080,
		0x03C, 0x00010000,
		0x03A, 0x00000080,
		0x03B, 0x0007A02C,
		0x03C, 0x00004000,
		0x03A, 0x00000400,
		0x03B, 0x0007202C,
		0x03C, 0x00010000,
		0x03A, 0x000000A0,
		0x03B, 0x0006B064,
		0x03C, 0x00004000,
		0x03A, 0x000000D8,
		0x03B, 0x00023070,
		0x03C, 0x00004000,
		0x03A, 0x00000468,
		0x03B, 0x0005B870,
		0x03C, 0x00010000,
		0x03A, 0x00000098,
		0x03B, 0x00052085,
		0x03C, 0x000E4000,
		0x03A, 0x00000418,
		0x03B, 0x0004A080,
		0x03C, 0x000F0000,
		0x03A, 0x00000418,
		0x03B, 0x00042080,
		0x03C, 0x00010000,
		0x03A, 0x00000080,
		0x03B, 0x000BA02C,
		0x03C, 0x00004000,
		0x03A, 0x00000400,
		0x03B, 0x000B202C,
		0x03C, 0x00010000,
		0x03A, 0x000000A0,
		0x03B, 0x000AB064,
		0x03C, 0x00004000,
		0x03A, 0x000000D8,
		0x03B, 0x000A3070,
		0x03C, 0x00004000,
		0x03A, 0x00000468,
		0x03B, 0x0009B870,
		0x03C, 0x00010000,
		0x03A, 0x00000098,
		0x03B, 0x00092085,
		0x03C, 0x000E4000,
		0x03A, 0x00000418,
		0x03B, 0x0008A080,
		0x03C, 0x000F0000,
		0x03A, 0x00000418,
		0x03B, 0x00082080,
		0x03C, 0x00010000,
		0x0EF, 0x00001100,
	0x80000008,0x00000000,0x40000000,0x00000000,
		0x034, 0x0004A0B2,
		0x034, 0x000490AF,
		0x034, 0x00048070,
		0x034, 0x0004706D,
		0x034, 0x00046050,
		0x034, 0x0004504D,
		0x034, 0x0004404A,
		0x034, 0x00043047,
		0x034, 0x0004200A,
		0x034, 0x00041007,
		0x034, 0x00040004,
	0x90000008,0x05000000,0x40000000,0x00000000,
		0x034, 0x0004A0B2,
		0x034, 0x000490AF,
		0x034, 0x00048070,
		0x034, 0x0004706D,
		0x034, 0x0004604D,
		0x034, 0x0004504A,
		0x034, 0x00044047,
		0x034, 0x00043044,
		0x034, 0x00042007,
		0x034, 0x00041004,
		0x034, 0x00040001,
	0xA0000000,0x00000000,
		0x034, 0x0004ADF5,
		0x034, 0x00049DF2,
		0x034, 0x00048DEF,
		0x034, 0x00047DEC,
		0x034, 0x00046DE9,
		0x034, 0x00045DE6,
		0x034, 0x00044DE3,
		0x034, 0x000438C8,
		0x034, 0x000428C5,
		0x034, 0x000418C2,
		0x034, 0x000408C0,
	0xB0000000,0x00000000,
	0x80000008,0x00000000,0x40000000,0x00000000,
		0x034, 0x0002A0B2,
		0x034, 0x000290AF,
		0x034, 0x00028070,
		0x034, 0x0002706D,
		0x034, 0x00026050,
		0x034, 0x0002504D,
		0x034, 0x0002404A,
		0x034, 0x00023047,
		0x034, 0x0002200A,
		0x034, 0x00021007,
		0x034, 0x00020004,
	0x90000008,0x05000000,0x40000000,0x00000000,
		0x034, 0x0002A0B4,
		0x034, 0x000290B1,
		0x034, 0x00028072,
		0x034, 0x0002706F,
		0x034, 0x0002604F,
		0x034, 0x0002504C,
		0x034, 0x00024049,
		0x034, 0x00023046,
		0x034, 0x00022009,
		0x034, 0x00021006,
		0x034, 0x00020003,
	0xA0000000,0x00000000,
		0x034, 0x0002ADF5,
		0x034, 0x00029DF2,
		0x034, 0x00028DEF,
		0x034, 0x00027DEC,
		0x034, 0x00026DE9,
		0x034, 0x00025DE6,
		0x034, 0x00024DE3,
		0x034, 0x000238C8,
		0x034, 0x000228C5,
		0x034, 0x000218C2,
		0x034, 0x000208C0,
	0xB0000000,0x00000000,
	0x80000008,0x00000000,0x40000000,0x00000000,
		0x034, 0x0000A0B2,
		0x034, 0x000090AF,
		0x034, 0x00008070,
		0x034, 0x0000706D,
		0x034, 0x00006050,
		0x034, 0x0000504D,
		0x034, 0x0000404A,
		0x034, 0x00003047,
		0x034, 0x0000200A,
		0x034, 0x00001007,
		0x034, 0x00000004,
	0x90000008,0x05000000,0x40000000,0x00000000,
		0x034, 0x0000A0B2,
		0x034, 0x000090AF,
		0x034, 0x00008070,
		0x034, 0x0000706D,
		0x034, 0x0000604D,
		0x034, 0x0000504A,
		0x034, 0x00004047,
		0x034, 0x00003044,
		0x034, 0x00002007,
		0x034, 0x00001004,
		0x034, 0x00000001,
	0xA0000000,0x00000000,
		0x034, 0x0000AFF7,
		0x034, 0x00009DF7,
		0x034, 0x00008DF4,
		0x034, 0x00007DF1,
		0x034, 0x00006DEE,
		0x034, 0x00005DEB,
		0x034, 0x00004DE8,
		0x034, 0x000038CC,
		0x034, 0x000028C9,
		0x034, 0x000018C6,
		0x034, 0x000008C3,
	0xB0000000,0x00000000,
		0x0EF, 0x00000000,
	0x80000008,0x00000000,0x40000000,0x00000000,
		0x018, 0x0001712A,
		0x0EF, 0x00000040,
		0x035, 0x000001D4,
		0x035, 0x000081D4,
		0x035, 0x000101D4,
		0x035, 0x000201B4,
		0x035, 0x000281B4,
		0x035, 0x000301B4,
		0x035, 0x000401B4,
		0x035, 0x000481B4,
		0x035, 0x000501B4,
	0x90000008,0x05000000,0x40000000,0x00000000,
		0x018, 0x0001712A,
		0x0EF, 0x00000040,
		0x035, 0x000001D4,
		0x035, 0x000081D4,
		0x035, 0x000101D4,
		0x035, 0x000201B4,
		0x035, 0x000281B4,
		0x035, 0x000301B4,
		0x035, 0x000401B4,
		0x035, 0x000481B4,
		0x035, 0x000501B4,
	0xA0000000,0x00000000,
		0x018, 0x0001712A,
		0x0EF, 0x00000040,
		0x035, 0x00000188,
		0x035, 0x00008147,
		0x035, 0x00010147,
		0x035, 0x000201D7,
		0x035, 0x000281D7,
		0x035, 0x000301D7,
		0x035, 0x000401D8,
		0x035, 0x000481D8,
		0x035, 0x000501D8,
	0xB0000000,0x00000000,
		0x0EF, 0x00000000,
	0x80000008,0x00000000,0x40000000,0x00000000,
		0x018, 0x0001712A,
		0x0EF, 0x00000010,
		0x036, 0x00004BFB,
		0x036, 0x0000CBFB,
		0x036, 0x00014BFB,
		0x036, 0x0001CBFB,
		0x036, 0x00024F4B,
		0x036, 0x0002CF4B,
		0x036, 0x00034F4B,
		0x036, 0x0003CF4B,
		0x036, 0x00044F4B,
		0x036, 0x0004CF4B,
		0x036, 0x00054F4B,
		0x036, 0x0005CF4B,
	0x90000008,0x05000000,0x40000000,0x00000000,
		0x018, 0x0001712A,
		0x0EF, 0x00000010,
		0x036, 0x00004BFB,
		0x036, 0x0000CBFB,
		0x036, 0x00014BFB,
		0x036, 0x0001CBFB,
		0x036, 0x00024F4B,
		0x036, 0x0002CF4B,
		0x036, 0x00034F4B,
		0x036, 0x0003CF4B,
		0x036, 0x00044F4B,
		0x036, 0x0004CF4B,
		0x036, 0x00054F4B,
		0x036, 0x0005CF4B,
	0xA0000000,0x00000000,
		0x018, 0x0001712A,
		0x0EF, 0x00000010,
		0x036, 0x00084EB4,
		0x036, 0x0008CC35,
		0x036, 0x00094C35,
		0x036, 0x0009CC35,
		0x036, 0x000A4C35,
		0x036, 0x000ACC35,
		0x036, 0x000B4C35,
		0x036, 0x000BCC35,
		0x036, 0x000C4C34,
		0x036, 0x000CCC35,
		0x036, 0x000D4C35,
		0x036, 0x000DCC35,
	0xB0000000,0x00000000,
		0x0EF, 0x00000000,
		0x0EF, 0x00000008,
	0x80000008,0x00000000,0x40000000,0x00000000,
		0x03C, 0x000002CC,
		0x03C, 0x00000522,
		0x03C, 0x00000902,
	0x90000008,0x05000000,0x40000000,0x00000000,
		0x03C, 0x000002CC,
		0x03C, 0x00000522,
		0x03C, 0x00000902,
	0xA0000000,0x00000000,
		0x03C, 0x000002A8,
		0x03C, 0x000005A2,
		0x03C, 0x00000880,
	0xB0000000,0x00000000,
		0x0EF, 0x00000000,
		0x018, 0x0001712A,
		0x0EF, 0x00000002,
		0x0DF, 0x00000080,
		0x01F, 0x00040064,
	0x80000008,0x00000000,0x40000000,0x00000000,
		0x061, 0x000FDD43,
		0x062, 0x00038F4B,
		0x063, 0x00032117,
		0x064, 0x000194AC,
		0x065, 0x000931D1,
	0x90000008,0x05000000,0x40000000,0x00000000,
		0x061, 0x000FDD43,
		0x062, 0x00038F4B,
		0x063, 0x00032117,
		0x064, 0x000194AC,
		0x065, 0x000931D2,
	0xA0000000,0x00000000,
		0x061, 0x000E5D53,
		0x062, 0x00038FCD,
		0x063, 0x000114EB,
		0x064, 0x000196AC,
		0x065, 0x000911D7,
	0xB0000000,0x00000000,
		0x008, 0x00008400,
		0x01C, 0x000739D2,
		0x0B4, 0x0001E78D,
		0x018, 0x0001F12A,
		0x0FE, 0x00000000,
		0x0FE, 0x00000000,
		0x0FE, 0x00000000,
		0x0FE, 0x00000000,
		0x0B4, 0x0001A78D,
		0x018, 0x0001712A,

};

void
ODM_ReadAndConfig_MP_8812A_RadioA(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
    #define READ_NEXT_PAIR(v1, v2, i) do { i += 2; v1 = Array[i]; v2 = Array[i+1]; } while(0)
    #define COND_ELSE  2
    #define COND_ENDIF 3
    u4Byte     i         = 0;
    u4Byte     ArrayLen    = sizeof(Array_MP_8812A_RadioA)/sizeof(u4Byte);
    pu4Byte    Array       = Array_MP_8812A_RadioA;
	
    ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_8812A_RadioA\n"));

    for (i = 0; i < ArrayLen; i += 2 )
    {
        u4Byte v1 = Array[i];
        u4Byte v2 = Array[i+1];
    
        // This (offset, data) pair doesn't care the condition.
        if ( v1 < 0x40000000 )
        {
           odm_ConfigRF_RadioA_8812A(pDM_Odm, v1, v2);
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
                    odm_ConfigRF_RadioA_8812A(pDM_Odm, v1, v2);
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
ODM_GetVersion_MP_8812A_RadioA(
)
{
	   return 40;
}

/******************************************************************************
*                           RadioB.TXT
******************************************************************************/

u4Byte Array_MP_8812A_RadioB[] = { 
		0x056, 0x00051CF2,
		0x066, 0x00040000,
		0x089, 0x00000080,
	0x80000001,0x00000000,0x40000000,0x00000000,
		0x086, 0x00014B3A,
	0x90000001,0x00000005,0x40000000,0x00000000,
		0x086, 0x00014B3A,
	0xA0000000,0x00000000,
		0x086, 0x00014B38,
	0xB0000000,0x00000000,
	0x80000004,0x00000000,0x40000000,0x00000000,
		0x08B, 0x00080180,
	0xA0000000,0x00000000,
		0x08B, 0x00087180,
	0xB0000000,0x00000000,
		0x018, 0x00000006,
		0x0EF, 0x00002000,
	0x80000001,0x00000000,0x40000000,0x00000000,
		0x03B, 0x0003F218,
		0x03B, 0x00030A58,
		0x03B, 0x0002FA58,
		0x03B, 0x00022590,
		0x03B, 0x0001FA50,
		0x03B, 0x00010248,
		0x03B, 0x00008240,
	0x90000001,0x00000005,0x40000000,0x00000000,
		0x03B, 0x0003F218,
		0x03B, 0x00030A58,
		0x03B, 0x0002FA58,
		0x03B, 0x00022590,
		0x03B, 0x0001FA50,
		0x03B, 0x00010248,
		0x03B, 0x00008240,
	0xA0000000,0x00000000,
		0x03B, 0x00038A58,
		0x03B, 0x00037A58,
		0x03B, 0x0002A590,
		0x03B, 0x00027A50,
		0x03B, 0x00018248,
		0x03B, 0x00010240,
		0x03B, 0x00008240,
	0xB0000000,0x00000000,
		0x0EF, 0x00000100,
	0x80000002,0x00000000,0x40000000,0x00000000,
		0x034, 0x0000A4EE,
		0x034, 0x00009076,
		0x034, 0x00008073,
		0x034, 0x00007070,
		0x034, 0x0000606D,
		0x034, 0x0000506A,
		0x034, 0x00004049,
		0x034, 0x00003046,
		0x034, 0x00002028,
		0x034, 0x00001025,
		0x034, 0x00000022,
	0xA0000000,0x00000000,
		0x034, 0x0000ADF4,
		0x034, 0x00009DF1,
		0x034, 0x00008DEE,
		0x034, 0x00007DEB,
		0x034, 0x00006DE8,
		0x034, 0x00005CEC,
		0x034, 0x00004CE9,
		0x034, 0x000034EA,
		0x034, 0x000024E7,
		0x034, 0x0000146B,
		0x034, 0x0000006D,
	0xB0000000,0x00000000,
		0x0EF, 0x00000000,
		0x0EF, 0x000020A2,
		0x0DF, 0x00000080,
		0x035, 0x00000192,
		0x035, 0x00008192,
		0x035, 0x00010192,
		0x036, 0x00000024,
		0x036, 0x00008024,
		0x036, 0x00010024,
		0x036, 0x00018024,
		0x0EF, 0x00000000,
		0x051, 0x00000C21,
		0x052, 0x000006D9,
		0x053, 0x000FC649,
		0x054, 0x0000017E,
		0x0EF, 0x00000002,
		0x008, 0x00008400,
		0x018, 0x0001712A,
		0x0EF, 0x00001000,
		0x03A, 0x00000080,
		0x03B, 0x0003A02C,
		0x03C, 0x00004000,
		0x03A, 0x00000400,
		0x03B, 0x0003202C,
		0x03C, 0x00010000,
		0x03A, 0x000000A0,
		0x03B, 0x0002B064,
		0x03C, 0x00004000,
		0x03A, 0x000000D8,
		0x03B, 0x00023070,
		0x03C, 0x00004000,
		0x03A, 0x00000468,
		0x03B, 0x0001B870,
		0x03C, 0x00010000,
		0x03A, 0x00000098,
		0x03B, 0x00012085,
		0x03C, 0x000E4000,
		0x03A, 0x00000418,
		0x03B, 0x0000A080,
		0x03C, 0x000F0000,
		0x03A, 0x00000418,
		0x03B, 0x00002080,
		0x03C, 0x00010000,
		0x03A, 0x00000080,
		0x03B, 0x0007A02C,
		0x03C, 0x00004000,
		0x03A, 0x00000400,
		0x03B, 0x0007202C,
		0x03C, 0x00010000,
		0x03A, 0x000000A0,
		0x03B, 0x0006B064,
		0x03C, 0x00004000,
		0x03A, 0x000000D8,
		0x03B, 0x00063070,
		0x03C, 0x00004000,
		0x03A, 0x00000468,
		0x03B, 0x0005B870,
		0x03C, 0x00010000,
		0x03A, 0x00000098,
		0x03B, 0x00052085,
		0x03C, 0x000E4000,
		0x03A, 0x00000418,
		0x03B, 0x0004A080,
		0x03C, 0x000F0000,
		0x03A, 0x00000418,
		0x03B, 0x00042080,
		0x03C, 0x00010000,
		0x03A, 0x00000080,
		0x03B, 0x000BA02C,
		0x03C, 0x00004000,
		0x03A, 0x00000400,
		0x03B, 0x000B202C,
		0x03C, 0x00010000,
		0x03A, 0x000000A0,
		0x03B, 0x000AB064,
		0x03C, 0x00004000,
		0x03A, 0x000000D8,
		0x03B, 0x000A3070,
		0x03C, 0x00004000,
		0x03A, 0x00000468,
		0x03B, 0x0009B870,
		0x03C, 0x00010000,
		0x03A, 0x00000098,
		0x03B, 0x00092085,
		0x03C, 0x000E4000,
		0x03A, 0x00000418,
		0x03B, 0x0008A080,
		0x03C, 0x000F0000,
		0x03A, 0x00000418,
		0x03B, 0x00082080,
		0x03C, 0x00010000,
		0x0EF, 0x00001100,
	0x80000008,0x00000000,0x40000000,0x00000000,
		0x034, 0x0004A0B2,
		0x034, 0x000490AF,
		0x034, 0x00048070,
		0x034, 0x0004706D,
		0x034, 0x00046050,
		0x034, 0x0004504D,
		0x034, 0x0004404A,
		0x034, 0x00043047,
		0x034, 0x0004200A,
		0x034, 0x00041007,
		0x034, 0x00040004,
	0x90000008,0x05000000,0x40000000,0x00000000,
		0x034, 0x0004A0B1,
		0x034, 0x000490AE,
		0x034, 0x0004806F,
		0x034, 0x0004706C,
		0x034, 0x0004604C,
		0x034, 0x00045049,
		0x034, 0x00044046,
		0x034, 0x00043043,
		0x034, 0x00042006,
		0x034, 0x00041003,
		0x034, 0x00040000,
	0xA0000000,0x00000000,
		0x034, 0x0004ADF5,
		0x034, 0x00049DF2,
		0x034, 0x00048DEF,
		0x034, 0x00047DEC,
		0x034, 0x00046DE9,
		0x034, 0x00045DE6,
		0x034, 0x00044DE3,
		0x034, 0x000438C8,
		0x034, 0x000428C5,
		0x034, 0x000418C2,
		0x034, 0x000408C0,
	0xB0000000,0x00000000,
	0x80000008,0x00000000,0x40000000,0x00000000,
		0x034, 0x0002A0B2,
		0x034, 0x000290AF,
		0x034, 0x00028070,
		0x034, 0x0002706D,
		0x034, 0x00026050,
		0x034, 0x0002504D,
		0x034, 0x0002404A,
		0x034, 0x00023047,
		0x034, 0x0002200A,
		0x034, 0x00021007,
		0x034, 0x00020004,
	0x90000008,0x05000000,0x40000000,0x00000000,
		0x034, 0x0002A0B3,
		0x034, 0x000290B0,
		0x034, 0x00028071,
		0x034, 0x0002706E,
		0x034, 0x0002604E,
		0x034, 0x0002504B,
		0x034, 0x00024048,
		0x034, 0x00023045,
		0x034, 0x00022008,
		0x034, 0x00021005,
		0x034, 0x00020002,
	0xA0000000,0x00000000,
		0x034, 0x0002ADF5,
		0x034, 0x00029DF2,
		0x034, 0x00028DEF,
		0x034, 0x00027DEC,
		0x034, 0x00026DE9,
		0x034, 0x00025DE6,
		0x034, 0x00024DE3,
		0x034, 0x000238C8,
		0x034, 0x000228C5,
		0x034, 0x000218C2,
		0x034, 0x000208C0,
	0xB0000000,0x00000000,
	0x80000008,0x00000000,0x40000000,0x00000000,
		0x034, 0x0000A0B2,
		0x034, 0x000090AF,
		0x034, 0x00008070,
		0x034, 0x0000706D,
		0x034, 0x00006050,
		0x034, 0x0000504D,
		0x034, 0x0000404A,
		0x034, 0x00003047,
		0x034, 0x0000200A,
		0x034, 0x00001007,
		0x034, 0x00000004,
	0x90000008,0x05000000,0x40000000,0x00000000,
		0x034, 0x0000A0B3,
		0x034, 0x000090B0,
		0x034, 0x00008070,
		0x034, 0x0000706D,
		0x034, 0x0000604D,
		0x034, 0x0000504A,
		0x034, 0x00004047,
		0x034, 0x00003044,
		0x034, 0x00002007,
		0x034, 0x00001004,
		0x034, 0x00000001,
	0xA0000000,0x00000000,
		0x034, 0x0000AFF7,
		0x034, 0x00009DF7,
		0x034, 0x00008DF4,
		0x034, 0x00007DF1,
		0x034, 0x00006DEE,
		0x034, 0x00005DEB,
		0x034, 0x00004DE8,
		0x034, 0x000038CC,
		0x034, 0x000028C9,
		0x034, 0x000018C6,
		0x034, 0x000008C3,
	0xB0000000,0x00000000,
		0x0EF, 0x00000000,
	0x80000008,0x00000000,0x40000000,0x00000000,
		0x018, 0x0001712A,
		0x0EF, 0x00000040,
		0x035, 0x000001C5,
		0x035, 0x000081C5,
		0x035, 0x000101C5,
		0x035, 0x00020174,
		0x035, 0x00028174,
		0x035, 0x00030174,
		0x035, 0x00040185,
		0x035, 0x00048185,
		0x035, 0x00050185,
		0x0EF, 0x00000000,
	0x90000008,0x05000000,0x40000000,0x00000000,
		0x018, 0x0001712A,
		0x0EF, 0x00000040,
		0x035, 0x000001C5,
		0x035, 0x000081C5,
		0x035, 0x000101C5,
		0x035, 0x00020174,
		0x035, 0x00028174,
		0x035, 0x00030174,
		0x035, 0x00040185,
		0x035, 0x00048185,
		0x035, 0x00050185,
		0x0EF, 0x00000000,
	0xA0000000,0x00000000,
		0x018, 0x0001712A,
		0x0EF, 0x00000040,
		0x035, 0x00000188,
		0x035, 0x00008147,
		0x035, 0x00010147,
		0x035, 0x000201D7,
		0x035, 0x000281D7,
		0x035, 0x000301D7,
		0x035, 0x000401D8,
		0x035, 0x000481D8,
		0x035, 0x000501D8,
		0x0EF, 0x00000000,
	0xB0000000,0x00000000,
	0x80000008,0x00000000,0x40000000,0x00000000,
		0x018, 0x0001712A,
		0x0EF, 0x00000010,
		0x036, 0x00005B8B,
		0x036, 0x0000DB8B,
		0x036, 0x00015B8B,
		0x036, 0x0001DB8B,
		0x036, 0x000262DB,
		0x036, 0x0002E2DB,
		0x036, 0x000362DB,
		0x036, 0x0003E2DB,
		0x036, 0x0004553B,
		0x036, 0x0004D53B,
		0x036, 0x0005553B,
		0x036, 0x0005D53B,
	0x90000008,0x05000000,0x40000000,0x00000000,
		0x018, 0x0001712A,
		0x0EF, 0x00000010,
		0x036, 0x00005B8B,
		0x036, 0x0000DB8B,
		0x036, 0x00015B8B,
		0x036, 0x0001DB8B,
		0x036, 0x000262DB,
		0x036, 0x0002E2DB,
		0x036, 0x000362DB,
		0x036, 0x0003E2DB,
		0x036, 0x0004553B,
		0x036, 0x0004D53B,
		0x036, 0x0005553B,
		0x036, 0x0005D53B,
	0xA0000000,0x00000000,
		0x018, 0x0001712A,
		0x0EF, 0x00000010,
		0x036, 0x00084EB4,
		0x036, 0x0008CC35,
		0x036, 0x00094C35,
		0x036, 0x0009CC35,
		0x036, 0x000A4C35,
		0x036, 0x000ACC35,
		0x036, 0x000B4C35,
		0x036, 0x000BCC35,
		0x036, 0x000C4C34,
		0x036, 0x000CCC35,
		0x036, 0x000D4C35,
		0x036, 0x000DCC35,
	0xB0000000,0x00000000,
		0x0EF, 0x00000000,
		0x0EF, 0x00000008,
	0x80000008,0x00000000,0x40000000,0x00000000,
		0x03C, 0x000002DC,
		0x03C, 0x00000524,
		0x03C, 0x00000902,
	0x90000008,0x05000000,0x40000000,0x00000000,
		0x03C, 0x000002DC,
		0x03C, 0x00000524,
		0x03C, 0x00000902,
	0xA0000000,0x00000000,
		0x03C, 0x000002A8,
		0x03C, 0x000005A2,
		0x03C, 0x00000880,
	0xB0000000,0x00000000,
		0x0EF, 0x00000000,
		0x018, 0x0001712A,
		0x0EF, 0x00000002,
		0x0DF, 0x00000080,
	0x80000008,0x00000000,0x40000000,0x00000000,
		0x061, 0x000EAC43,
		0x062, 0x00038F47,
		0x063, 0x00031157,
		0x064, 0x0001C4AC,
		0x065, 0x000931D1,
	0x90000008,0x05000000,0x40000000,0x00000000,
		0x061, 0x000EAC43,
		0x062, 0x00038F47,
		0x063, 0x00031157,
		0x064, 0x0001C4AC,
		0x065, 0x000931D2,
	0x90000002,0x00000000,0x40000000,0x00000000,
		0x061, 0x000EAC43,
		0x062, 0x00038F47,
		0x063, 0x00031157,
		0x064, 0x0001C4AC,
		0x065, 0x000931D1,
	0xA0000000,0x00000000,
		0x061, 0x000E5D53,
		0x062, 0x00038FCD,
		0x063, 0x000114EB,
		0x064, 0x000196AC,
		0x065, 0x000911D7,
	0xB0000000,0x00000000,
		0x008, 0x00008400,

};

void
ODM_ReadAndConfig_MP_8812A_RadioB(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
    #define READ_NEXT_PAIR(v1, v2, i) do { i += 2; v1 = Array[i]; v2 = Array[i+1]; } while(0)
    #define COND_ELSE  2
    #define COND_ENDIF 3
    u4Byte     i         = 0;
    u4Byte     ArrayLen    = sizeof(Array_MP_8812A_RadioB)/sizeof(u4Byte);
    pu4Byte    Array       = Array_MP_8812A_RadioB;
	
    ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_8812A_RadioB\n"));

    for (i = 0; i < ArrayLen; i += 2 )
    {
        u4Byte v1 = Array[i];
        u4Byte v2 = Array[i+1];
    
        // This (offset, data) pair doesn't care the condition.
        if ( v1 < 0x40000000 )
        {
           odm_ConfigRF_RadioB_8812A(pDM_Odm, v1, v2);
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
                    odm_ConfigRF_RadioB_8812A(pDM_Odm, v1, v2);
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
ODM_GetVersion_MP_8812A_RadioB(
)
{
	   return 40;
}

/******************************************************************************
*                           TxPowerTrack_AP.TXT
******************************************************************************/

u1Byte gDeltaSwingTableIdx_MP_5GB_N_TxPowerTrack_AP_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 4, 5, 5, 6,  6,  7,  7,  8,  8,  9,  9, 10, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
};
u1Byte gDeltaSwingTableIdx_MP_5GB_P_TxPowerTrack_AP_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 16, 17, 17, 18, 19, 20, 20, 20},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 19, 20, 20, 20},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21},
};
u1Byte gDeltaSwingTableIdx_MP_5GA_N_TxPowerTrack_AP_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 4, 4, 5, 5, 6,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 6,  7,  7,  8,  8,  9, 10, 11, 11, 12, 13, 13, 14, 15, 16, 16, 16, 16, 16, 16, 16},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 16, 16, 16, 16, 16},
};
u1Byte gDeltaSwingTableIdx_MP_5GA_P_TxPowerTrack_AP_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 4, 5, 5, 6,  7,  8,  8,  9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 18, 19, 19, 19},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21},
};
u1Byte gDeltaSwingTableIdx_MP_2GB_N_TxPowerTrack_AP_8812A[]    = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  9,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_MP_2GB_P_TxPowerTrack_AP_8812A[]    = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};
u1Byte gDeltaSwingTableIdx_MP_2GA_N_TxPowerTrack_AP_8812A[]    = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  8,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_MP_2GA_P_TxPowerTrack_AP_8812A[]    = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};
u1Byte gDeltaSwingTableIdx_MP_2GCCKB_N_TxPowerTrack_AP_8812A[] = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  9,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_MP_2GCCKB_P_TxPowerTrack_AP_8812A[] = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};
u1Byte gDeltaSwingTableIdx_MP_2GCCKA_N_TxPowerTrack_AP_8812A[] = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  8,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_MP_2GCCKA_P_TxPowerTrack_AP_8812A[] = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};

void
ODM_ReadAndConfig_MP_8812A_TxPowerTrack_AP(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
	PODM_RF_CAL_T  pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_MP_8812A\n"));


	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GA_P, gDeltaSwingTableIdx_MP_2GA_P_TxPowerTrack_AP_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GA_N, gDeltaSwingTableIdx_MP_2GA_N_TxPowerTrack_AP_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GB_P, gDeltaSwingTableIdx_MP_2GB_P_TxPowerTrack_AP_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GB_N, gDeltaSwingTableIdx_MP_2GB_N_TxPowerTrack_AP_8812A, DELTA_SWINGIDX_SIZE);

	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_P, gDeltaSwingTableIdx_MP_2GCCKA_P_TxPowerTrack_AP_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_N, gDeltaSwingTableIdx_MP_2GCCKA_N_TxPowerTrack_AP_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_P, gDeltaSwingTableIdx_MP_2GCCKB_P_TxPowerTrack_AP_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_N, gDeltaSwingTableIdx_MP_2GCCKB_N_TxPowerTrack_AP_8812A, DELTA_SWINGIDX_SIZE);

	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GA_P, gDeltaSwingTableIdx_MP_5GA_P_TxPowerTrack_AP_8812A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GA_N, gDeltaSwingTableIdx_MP_5GA_N_TxPowerTrack_AP_8812A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GB_P, gDeltaSwingTableIdx_MP_5GB_P_TxPowerTrack_AP_8812A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GB_N, gDeltaSwingTableIdx_MP_5GB_N_TxPowerTrack_AP_8812A, DELTA_SWINGIDX_SIZE*3);
}

/******************************************************************************
*                           TxPowerTrack_PCIE.TXT
******************************************************************************/

u1Byte gDeltaSwingTableIdx_MP_5GB_N_TxPowerTrack_PCIE_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 2, 3, 3, 4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  9,  9,  9, 10, 10, 11, 11, 12, 12, 13},
	{0, 1, 1, 2, 2, 2, 3, 3, 4,  4,  4,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 12, 13, 13},
	{0, 1, 1, 2, 3, 3, 4, 4, 5,  6,  6,  7,  8,  9, 10, 11, 12, 12, 13, 14, 14, 14, 15, 16, 17, 17, 17, 18, 18, 18},
};
u1Byte gDeltaSwingTableIdx_MP_5GB_P_TxPowerTrack_PCIE_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  5,  5,  6,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  4,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  5,  5,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
};
u1Byte gDeltaSwingTableIdx_MP_5GA_N_TxPowerTrack_PCIE_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  4,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 12, 13, 13, 13},
	{0, 1, 1, 2, 2, 2, 3, 3, 4,  4,  5,  5,  6,  6,  7,  8,  9,  9, 10, 10, 11, 11, 11, 12, 12, 12, 12, 12, 13, 13},
	{0, 1, 1, 2, 2, 3, 3, 4, 5,  6,  7,  8,  8,  9, 10, 11, 12, 13, 14, 14, 15, 15, 15, 16, 16, 16, 17, 17, 18, 18},
};
u1Byte gDeltaSwingTableIdx_MP_5GA_P_TxPowerTrack_PCIE_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  4,  4,  5,  5,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  4,  5,  5,  6,  6,  7,  7,  8,  9,  9, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 3, 3, 4, 4, 5,  6,  6,  7,  7,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
};
u1Byte gDeltaSwingTableIdx_MP_2GB_N_TxPowerTrack_PCIE_8812A[]    = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  9,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_MP_2GB_P_TxPowerTrack_PCIE_8812A[]    = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};
u1Byte gDeltaSwingTableIdx_MP_2GA_N_TxPowerTrack_PCIE_8812A[]    = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  8,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_MP_2GA_P_TxPowerTrack_PCIE_8812A[]    = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};
u1Byte gDeltaSwingTableIdx_MP_2GCCKB_N_TxPowerTrack_PCIE_8812A[] = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  9,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_MP_2GCCKB_P_TxPowerTrack_PCIE_8812A[] = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};
u1Byte gDeltaSwingTableIdx_MP_2GCCKA_N_TxPowerTrack_PCIE_8812A[] = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  8,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_MP_2GCCKA_P_TxPowerTrack_PCIE_8812A[] = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};

void
ODM_ReadAndConfig_MP_8812A_TxPowerTrack_PCIE(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
	PODM_RF_CAL_T  pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_MP_8812A\n"));


	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GA_P, gDeltaSwingTableIdx_MP_2GA_P_TxPowerTrack_PCIE_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GA_N, gDeltaSwingTableIdx_MP_2GA_N_TxPowerTrack_PCIE_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GB_P, gDeltaSwingTableIdx_MP_2GB_P_TxPowerTrack_PCIE_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GB_N, gDeltaSwingTableIdx_MP_2GB_N_TxPowerTrack_PCIE_8812A, DELTA_SWINGIDX_SIZE);

	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_P, gDeltaSwingTableIdx_MP_2GCCKA_P_TxPowerTrack_PCIE_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_N, gDeltaSwingTableIdx_MP_2GCCKA_N_TxPowerTrack_PCIE_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_P, gDeltaSwingTableIdx_MP_2GCCKB_P_TxPowerTrack_PCIE_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_N, gDeltaSwingTableIdx_MP_2GCCKB_N_TxPowerTrack_PCIE_8812A, DELTA_SWINGIDX_SIZE);

	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GA_P, gDeltaSwingTableIdx_MP_5GA_P_TxPowerTrack_PCIE_8812A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GA_N, gDeltaSwingTableIdx_MP_5GA_N_TxPowerTrack_PCIE_8812A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GB_P, gDeltaSwingTableIdx_MP_5GB_P_TxPowerTrack_PCIE_8812A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GB_N, gDeltaSwingTableIdx_MP_5GB_N_TxPowerTrack_PCIE_8812A, DELTA_SWINGIDX_SIZE*3);
}

/******************************************************************************
*                           TxPowerTrack_RFE3.TXT
******************************************************************************/

u1Byte gDeltaSwingTableIdx_MP_5GB_N_TxPowerTrack_RFE3_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 3, 3, 4, 5, 5,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 12, 13, 13, 14, 15, 16, 16, 17, 17, 18, 18},
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  5,  6,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 12, 14, 13, 13, 14, 14, 14, 15, 15},
	{0, 1, 1, 2, 2, 3, 4, 5, 6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15, 16, 16},
};
u1Byte gDeltaSwingTableIdx_MP_5GB_P_TxPowerTrack_RFE3_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 3, 3, 4, 5, 6,  7,  7,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  5,  6,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  5,  6,  6,  7,  7,  8,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
};
u1Byte gDeltaSwingTableIdx_MP_5GA_N_TxPowerTrack_RFE3_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 3, 3, 4, 5, 5,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 13, 14, 15, 16, 16, 17, 17, 18, 18},
	{0, 1, 1, 2, 3, 3, 4, 4, 5,  6,  6,  7,  7,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 15, 16, 16, 17, 17},
	{0, 1, 1, 2, 3, 3, 4, 4, 5,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 12, 13, 13, 14, 14, 15, 15, 16, 17, 18, 18},
};
u1Byte gDeltaSwingTableIdx_MP_5GA_P_TxPowerTrack_RFE3_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 4, 5, 6,  7,  7,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 2, 3, 4, 4, 5, 5, 6,  7,  7,  8,  9,  9, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 2, 3, 4, 4, 5, 5, 6,  7,  7,  8,  9,  9, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
};
u1Byte gDeltaSwingTableIdx_MP_2GB_N_TxPowerTrack_RFE3_8812A[]    = {0, 1, 1, 2, 2, 3, 3, 4, 4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  9,  9, 10, 11, 12, 12, 13, 14, 15, 15};
u1Byte gDeltaSwingTableIdx_MP_2GB_P_TxPowerTrack_RFE3_8812A[]    = {0, 1, 1, 2, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  7,  7,  8,  9, 10, 10, 10, 10, 11, 11, 11, 11, 11};
u1Byte gDeltaSwingTableIdx_MP_2GA_N_TxPowerTrack_RFE3_8812A[]    = {0, 1, 1, 2, 2, 3, 4, 5, 6,  6,  6,  7,  7,  8,  8,  9, 10, 10, 11, 11, 12, 12, 13, 13, 13, 13, 14, 14, 15, 15};
u1Byte gDeltaSwingTableIdx_MP_2GA_P_TxPowerTrack_RFE3_8812A[]    = {0, 0, 1, 1, 1, 2, 2, 3, 3,  4,  4,  4,  5,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11};
u1Byte gDeltaSwingTableIdx_MP_2GCCKB_N_TxPowerTrack_RFE3_8812A[] = {0, 1, 1, 2, 2, 3, 3, 4, 4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  9,  9, 10, 11, 12, 12, 13, 14, 15, 15};
u1Byte gDeltaSwingTableIdx_MP_2GCCKB_P_TxPowerTrack_RFE3_8812A[] = {0, 1, 1, 2, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  7,  7,  8,  9, 10, 10, 10, 10, 11, 11, 11, 11, 11};
u1Byte gDeltaSwingTableIdx_MP_2GCCKA_N_TxPowerTrack_RFE3_8812A[] = {0, 1, 1, 2, 2, 3, 4, 5, 6,  6,  6,  7,  7,  8,  8,  9, 10, 10, 11, 11, 12, 12, 13, 13, 13, 13, 14, 14, 15, 15};
u1Byte gDeltaSwingTableIdx_MP_2GCCKA_P_TxPowerTrack_RFE3_8812A[] = {0, 0, 1, 1, 1, 2, 2, 3, 3,  4,  4,  4,  5,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11};

void
ODM_ReadAndConfig_MP_8812A_TxPowerTrack_RFE3(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
	PODM_RF_CAL_T  pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_MP_8812A\n"));


	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GA_P, gDeltaSwingTableIdx_MP_2GA_P_TxPowerTrack_RFE3_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GA_N, gDeltaSwingTableIdx_MP_2GA_N_TxPowerTrack_RFE3_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GB_P, gDeltaSwingTableIdx_MP_2GB_P_TxPowerTrack_RFE3_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GB_N, gDeltaSwingTableIdx_MP_2GB_N_TxPowerTrack_RFE3_8812A, DELTA_SWINGIDX_SIZE);

	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_P, gDeltaSwingTableIdx_MP_2GCCKA_P_TxPowerTrack_RFE3_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_N, gDeltaSwingTableIdx_MP_2GCCKA_N_TxPowerTrack_RFE3_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_P, gDeltaSwingTableIdx_MP_2GCCKB_P_TxPowerTrack_RFE3_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_N, gDeltaSwingTableIdx_MP_2GCCKB_N_TxPowerTrack_RFE3_8812A, DELTA_SWINGIDX_SIZE);

	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GA_P, gDeltaSwingTableIdx_MP_5GA_P_TxPowerTrack_RFE3_8812A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GA_N, gDeltaSwingTableIdx_MP_5GA_N_TxPowerTrack_RFE3_8812A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GB_P, gDeltaSwingTableIdx_MP_5GB_P_TxPowerTrack_RFE3_8812A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GB_N, gDeltaSwingTableIdx_MP_5GB_N_TxPowerTrack_RFE3_8812A, DELTA_SWINGIDX_SIZE*3);
}

/******************************************************************************
*                           TxPowerTrack_RFE4.TXT
******************************************************************************/

u1Byte gDeltaSwingTableIdx_MP_5GB_N_TxPowerTrack_RFE4_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 2, 3, 3, 4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  9,  9,  9, 10, 10, 11, 11, 12, 12, 13},
	{0, 1, 1, 2, 2, 2, 3, 3, 4,  4,  4,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 12, 13, 13},
	{0, 1, 1, 2, 3, 3, 4, 4, 5,  6,  6,  7,  8,  9, 10, 11, 12, 12, 13, 14, 14, 14, 15, 16, 17, 17, 17, 18, 18, 18},
};
u1Byte gDeltaSwingTableIdx_MP_5GB_P_TxPowerTrack_RFE4_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  5,  5,  6,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  4,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  5,  5,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
};
u1Byte gDeltaSwingTableIdx_MP_5GA_N_TxPowerTrack_RFE4_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  4,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 12, 13, 13, 13},
	{0, 1, 1, 2, 2, 2, 3, 3, 4,  4,  5,  5,  6,  6,  7,  8,  9,  9, 10, 10, 11, 11, 11, 12, 12, 12, 12, 12, 13, 13},
	{0, 1, 1, 2, 2, 3, 3, 4, 5,  6,  7,  8,  8,  9, 10, 11, 12, 13, 14, 14, 15, 15, 15, 16, 16, 16, 17, 17, 18, 18},
};
u1Byte gDeltaSwingTableIdx_MP_5GA_P_TxPowerTrack_RFE4_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  4,  4,  5,  5,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  4,  5,  5,  6,  6,  7,  7,  8,  9,  9, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 3, 3, 4, 4, 5,  6,  6,  7,  7,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
};
u1Byte gDeltaSwingTableIdx_MP_2GB_N_TxPowerTrack_RFE4_8812A[]    = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  9,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_MP_2GB_P_TxPowerTrack_RFE4_8812A[]    = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};
u1Byte gDeltaSwingTableIdx_MP_2GA_N_TxPowerTrack_RFE4_8812A[]    = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  8,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_MP_2GA_P_TxPowerTrack_RFE4_8812A[]    = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};
u1Byte gDeltaSwingTableIdx_MP_2GCCKB_N_TxPowerTrack_RFE4_8812A[] = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  9,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_MP_2GCCKB_P_TxPowerTrack_RFE4_8812A[] = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};
u1Byte gDeltaSwingTableIdx_MP_2GCCKA_N_TxPowerTrack_RFE4_8812A[] = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  8,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_MP_2GCCKA_P_TxPowerTrack_RFE4_8812A[] = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};

void
ODM_ReadAndConfig_MP_8812A_TxPowerTrack_RFE4(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
	PODM_RF_CAL_T  pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_MP_8812A\n"));


	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GA_P, gDeltaSwingTableIdx_MP_2GA_P_TxPowerTrack_RFE4_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GA_N, gDeltaSwingTableIdx_MP_2GA_N_TxPowerTrack_RFE4_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GB_P, gDeltaSwingTableIdx_MP_2GB_P_TxPowerTrack_RFE4_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GB_N, gDeltaSwingTableIdx_MP_2GB_N_TxPowerTrack_RFE4_8812A, DELTA_SWINGIDX_SIZE);

	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_P, gDeltaSwingTableIdx_MP_2GCCKA_P_TxPowerTrack_RFE4_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_N, gDeltaSwingTableIdx_MP_2GCCKA_N_TxPowerTrack_RFE4_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_P, gDeltaSwingTableIdx_MP_2GCCKB_P_TxPowerTrack_RFE4_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_N, gDeltaSwingTableIdx_MP_2GCCKB_N_TxPowerTrack_RFE4_8812A, DELTA_SWINGIDX_SIZE);

	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GA_P, gDeltaSwingTableIdx_MP_5GA_P_TxPowerTrack_RFE4_8812A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GA_N, gDeltaSwingTableIdx_MP_5GA_N_TxPowerTrack_RFE4_8812A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GB_P, gDeltaSwingTableIdx_MP_5GB_P_TxPowerTrack_RFE4_8812A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GB_N, gDeltaSwingTableIdx_MP_5GB_N_TxPowerTrack_RFE4_8812A, DELTA_SWINGIDX_SIZE*3);
}

/******************************************************************************
*                           TxPowerTrack_USB.TXT
******************************************************************************/

u1Byte gDeltaSwingTableIdx_MP_5GB_N_TxPowerTrack_USB_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 4, 5, 6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14},
	{0, 1, 1, 2, 2, 3, 4, 4, 6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14},
	{0, 1, 1, 2, 2, 3, 4, 5, 6,  6,  7,  8,  8,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 16, 16, 16, 16},
};
u1Byte gDeltaSwingTableIdx_MP_5GB_P_TxPowerTrack_USB_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 3, 4, 5,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 3, 3, 4, 5, 5,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 3, 3, 4, 5, 6,  7,  7,  8,  8,  9,  9, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
};
u1Byte gDeltaSwingTableIdx_MP_5GA_N_TxPowerTrack_USB_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 4, 5, 6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 15, 15, 15, 15, 15},
	{0, 1, 1, 2, 2, 3, 4, 5, 6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 15, 15, 15, 15, 15},
	{0, 1, 1, 2, 2, 3, 4, 5, 6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 15, 15, 15, 15, 15},
};
u1Byte gDeltaSwingTableIdx_MP_5GA_P_TxPowerTrack_USB_8812A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 4, 5, 6,  7,  7,  8,  8,  9, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 3, 3, 4, 5, 6,  7,  7,  8,  8,  9, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 3, 3, 4, 5, 6,  7,  7,  8,  8,  9, 10, 11, 11, 12, 12, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
};
u1Byte gDeltaSwingTableIdx_MP_2GB_N_TxPowerTrack_USB_8812A[]    = {0, 1, 1, 2, 2, 2, 3, 3, 3,  4,  4,  5,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11};
u1Byte gDeltaSwingTableIdx_MP_2GB_P_TxPowerTrack_USB_8812A[]    = {0, 1, 1, 2, 2, 2, 3, 3, 3,  4,  4,  4,  5,  5,  5,  6,  6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7};
u1Byte gDeltaSwingTableIdx_MP_2GA_N_TxPowerTrack_USB_8812A[]    = {0, 1, 1, 2, 2, 2, 3, 3, 3,  4,  4,  4,  5,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  9, 10, 10, 10, 10, 10, 10};
u1Byte gDeltaSwingTableIdx_MP_2GA_P_TxPowerTrack_USB_8812A[]    = {0, 1, 1, 2, 2, 2, 3, 3, 3,  4,  4,  4,  5,  5,  5,  6,  6,  6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7};
u1Byte gDeltaSwingTableIdx_MP_2GCCKB_N_TxPowerTrack_USB_8812A[] = {0, 1, 1, 2, 2, 2, 3, 3, 3,  4,  4,  5,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11};
u1Byte gDeltaSwingTableIdx_MP_2GCCKB_P_TxPowerTrack_USB_8812A[] = {0, 1, 1, 2, 2, 2, 3, 3, 3,  4,  4,  4,  5,  5,  5,  6,  6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7};
u1Byte gDeltaSwingTableIdx_MP_2GCCKA_N_TxPowerTrack_USB_8812A[] = {0, 1, 1, 2, 2, 2, 3, 3, 3,  4,  4,  4,  5,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  9, 10, 10, 10, 10, 10, 10};
u1Byte gDeltaSwingTableIdx_MP_2GCCKA_P_TxPowerTrack_USB_8812A[] = {0, 1, 1, 2, 2, 2, 3, 3, 3,  4,  4,  4,  5,  5,  5,  6,  6,  6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7};

void
ODM_ReadAndConfig_MP_8812A_TxPowerTrack_USB(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
	PODM_RF_CAL_T  pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_MP_8812A\n"));


	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GA_P, gDeltaSwingTableIdx_MP_2GA_P_TxPowerTrack_USB_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GA_N, gDeltaSwingTableIdx_MP_2GA_N_TxPowerTrack_USB_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GB_P, gDeltaSwingTableIdx_MP_2GB_P_TxPowerTrack_USB_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GB_N, gDeltaSwingTableIdx_MP_2GB_N_TxPowerTrack_USB_8812A, DELTA_SWINGIDX_SIZE);

	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_P, gDeltaSwingTableIdx_MP_2GCCKA_P_TxPowerTrack_USB_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_N, gDeltaSwingTableIdx_MP_2GCCKA_N_TxPowerTrack_USB_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_P, gDeltaSwingTableIdx_MP_2GCCKB_P_TxPowerTrack_USB_8812A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_N, gDeltaSwingTableIdx_MP_2GCCKB_N_TxPowerTrack_USB_8812A, DELTA_SWINGIDX_SIZE);

	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GA_P, gDeltaSwingTableIdx_MP_5GA_P_TxPowerTrack_USB_8812A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GA_N, gDeltaSwingTableIdx_MP_5GA_N_TxPowerTrack_USB_8812A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GB_P, gDeltaSwingTableIdx_MP_5GB_P_TxPowerTrack_USB_8812A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GB_N, gDeltaSwingTableIdx_MP_5GB_N_TxPowerTrack_USB_8812A, DELTA_SWINGIDX_SIZE*3);
}

/******************************************************************************
*                           TXPWR_LMT.TXT
******************************************************************************/

pu1Byte Array_MP_8812A_TXPWR_LMT[] = { 
	"FCC", "2.4G", "20M", "CCK", "1T", "01", "36", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "01", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "01", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "02", "36", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "02", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "02", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "03", "36", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "03", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "03", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "04", "36", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "04", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "04", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "05", "36", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "05", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "05", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "06", "36", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "06", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "06", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "07", "36", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "07", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "07", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "08", "36", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "08", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "08", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "09", "36", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "09", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "09", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "10", "36", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "10", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "10", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "11", "36", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "11", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "11", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "12", "63", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "12", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "12", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "13", "63", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "13", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "13", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "14", "63", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "14", "63", 
	"MKK", "2.4G", "20M", "CCK", "1T", "14", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "01", "34", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "01", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "01", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "02", "36", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "02", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "02", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "03", "36", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "03", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "03", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "04", "36", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "04", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "04", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "05", "36", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "05", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "05", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "06", "36", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "06", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "06", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "07", "36", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "07", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "07", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "08", "36", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "08", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "08", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "09", "36", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "09", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "09", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "10", "36", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "10", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "10", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "11", "32", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "11", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "11", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "12", "63", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "12", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "12", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "13", "63", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "13", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "13", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "14", "63", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "14", "63", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "14", "63",
	"FCC", "2.4G", "20M", "HT", "1T", "01", "34", 
	"ETSI", "2.4G", "20M", "HT", "1T", "01", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "01", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "02", "36", 
	"ETSI", "2.4G", "20M", "HT", "1T", "02", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "02", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "03", "36", 
	"ETSI", "2.4G", "20M", "HT", "1T", "03", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "03", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "04", "36", 
	"ETSI", "2.4G", "20M", "HT", "1T", "04", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "04", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "05", "36", 
	"ETSI", "2.4G", "20M", "HT", "1T", "05", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "05", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "06", "36", 
	"ETSI", "2.4G", "20M", "HT", "1T", "06", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "06", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "07", "36", 
	"ETSI", "2.4G", "20M", "HT", "1T", "07", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "07", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "08", "36", 
	"ETSI", "2.4G", "20M", "HT", "1T", "08", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "08", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "09", "36", 
	"ETSI", "2.4G", "20M", "HT", "1T", "09", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "09", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "10", "36", 
	"ETSI", "2.4G", "20M", "HT", "1T", "10", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "10", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "11", "32", 
	"ETSI", "2.4G", "20M", "HT", "1T", "11", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "11", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "12", "63", 
	"ETSI", "2.4G", "20M", "HT", "1T", "12", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "12", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "13", "63", 
	"ETSI", "2.4G", "20M", "HT", "1T", "13", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "13", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "14", "63", 
	"ETSI", "2.4G", "20M", "HT", "1T", "14", "63", 
	"MKK", "2.4G", "20M", "HT", "1T", "14", "63",
	"FCC", "2.4G", "20M", "HT", "2T", "01", "32", 
	"ETSI", "2.4G", "20M", "HT", "2T", "01", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "01", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "02", "34", 
	"ETSI", "2.4G", "20M", "HT", "2T", "02", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "02", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "03", "34", 
	"ETSI", "2.4G", "20M", "HT", "2T", "03", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "03", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "04", "34", 
	"ETSI", "2.4G", "20M", "HT", "2T", "04", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "04", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "05", "34", 
	"ETSI", "2.4G", "20M", "HT", "2T", "05", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "05", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "06", "34", 
	"ETSI", "2.4G", "20M", "HT", "2T", "06", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "06", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "07", "34", 
	"ETSI", "2.4G", "20M", "HT", "2T", "07", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "07", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "08", "34", 
	"ETSI", "2.4G", "20M", "HT", "2T", "08", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "08", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "09", "34", 
	"ETSI", "2.4G", "20M", "HT", "2T", "09", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "09", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "10", "34", 
	"ETSI", "2.4G", "20M", "HT", "2T", "10", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "10", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "11", "30", 
	"ETSI", "2.4G", "20M", "HT", "2T", "11", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "11", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "12", "63", 
	"ETSI", "2.4G", "20M", "HT", "2T", "12", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "12", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "13", "63", 
	"ETSI", "2.4G", "20M", "HT", "2T", "13", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "13", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "14", "63", 
	"ETSI", "2.4G", "20M", "HT", "2T", "14", "63", 
	"MKK", "2.4G", "20M", "HT", "2T", "14", "63",
	"FCC", "2.4G", "40M", "HT", "1T", "01", "63", 
	"ETSI", "2.4G", "40M", "HT", "1T", "01", "63", 
	"MKK", "2.4G", "40M", "HT", "1T", "01", "63",
	"FCC", "2.4G", "40M", "HT", "1T", "02", "63", 
	"ETSI", "2.4G", "40M", "HT", "1T", "02", "63", 
	"MKK", "2.4G", "40M", "HT", "1T", "02", "63",
	"FCC", "2.4G", "40M", "HT", "1T", "03", "32", 
	"ETSI", "2.4G", "40M", "HT", "1T", "03", "32", 
	"MKK", "2.4G", "40M", "HT", "1T", "03", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "04", "36", 
	"ETSI", "2.4G", "40M", "HT", "1T", "04", "32", 
	"MKK", "2.4G", "40M", "HT", "1T", "04", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "05", "36", 
	"ETSI", "2.4G", "40M", "HT", "1T", "05", "32", 
	"MKK", "2.4G", "40M", "HT", "1T", "05", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "06", "36", 
	"ETSI", "2.4G", "40M", "HT", "1T", "06", "32", 
	"MKK", "2.4G", "40M", "HT", "1T", "06", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "07", "36", 
	"ETSI", "2.4G", "40M", "HT", "1T", "07", "32", 
	"MKK", "2.4G", "40M", "HT", "1T", "07", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "08", "36", 
	"ETSI", "2.4G", "40M", "HT", "1T", "08", "32", 
	"MKK", "2.4G", "40M", "HT", "1T", "08", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "09", "36", 
	"ETSI", "2.4G", "40M", "HT", "1T", "09", "32", 
	"MKK", "2.4G", "40M", "HT", "1T", "09", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "10", "36", 
	"ETSI", "2.4G", "40M", "HT", "1T", "10", "32", 
	"MKK", "2.4G", "40M", "HT", "1T", "10", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "11", "32", 
	"ETSI", "2.4G", "40M", "HT", "1T", "11", "32", 
	"MKK", "2.4G", "40M", "HT", "1T", "11", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "12", "63", 
	"ETSI", "2.4G", "40M", "HT", "1T", "12", "32", 
	"MKK", "2.4G", "40M", "HT", "1T", "12", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "13", "63", 
	"ETSI", "2.4G", "40M", "HT", "1T", "13", "32", 
	"MKK", "2.4G", "40M", "HT", "1T", "13", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "14", "63", 
	"ETSI", "2.4G", "40M", "HT", "1T", "14", "63", 
	"MKK", "2.4G", "40M", "HT", "1T", "14", "63",
	"FCC", "2.4G", "40M", "HT", "2T", "01", "63", 
	"ETSI", "2.4G", "40M", "HT", "2T", "01", "63", 
	"MKK", "2.4G", "40M", "HT", "2T", "01", "63",
	"FCC", "2.4G", "40M", "HT", "2T", "02", "63", 
	"ETSI", "2.4G", "40M", "HT", "2T", "02", "63", 
	"MKK", "2.4G", "40M", "HT", "2T", "02", "63",
	"FCC", "2.4G", "40M", "HT", "2T", "03", "30", 
	"ETSI", "2.4G", "40M", "HT", "2T", "03", "30", 
	"MKK", "2.4G", "40M", "HT", "2T", "03", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "04", "34", 
	"ETSI", "2.4G", "40M", "HT", "2T", "04", "30", 
	"MKK", "2.4G", "40M", "HT", "2T", "04", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "05", "34", 
	"ETSI", "2.4G", "40M", "HT", "2T", "05", "30", 
	"MKK", "2.4G", "40M", "HT", "2T", "05", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "06", "34", 
	"ETSI", "2.4G", "40M", "HT", "2T", "06", "30", 
	"MKK", "2.4G", "40M", "HT", "2T", "06", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "07", "34", 
	"ETSI", "2.4G", "40M", "HT", "2T", "07", "30", 
	"MKK", "2.4G", "40M", "HT", "2T", "07", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "08", "34", 
	"ETSI", "2.4G", "40M", "HT", "2T", "08", "30", 
	"MKK", "2.4G", "40M", "HT", "2T", "08", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "09", "34", 
	"ETSI", "2.4G", "40M", "HT", "2T", "09", "30", 
	"MKK", "2.4G", "40M", "HT", "2T", "09", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "10", "34", 
	"ETSI", "2.4G", "40M", "HT", "2T", "10", "30", 
	"MKK", "2.4G", "40M", "HT", "2T", "10", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "11", "30", 
	"ETSI", "2.4G", "40M", "HT", "2T", "11", "30", 
	"MKK", "2.4G", "40M", "HT", "2T", "11", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "12", "63", 
	"ETSI", "2.4G", "40M", "HT", "2T", "12", "32", 
	"MKK", "2.4G", "40M", "HT", "2T", "12", "32",
	"FCC", "2.4G", "40M", "HT", "2T", "13", "63", 
	"ETSI", "2.4G", "40M", "HT", "2T", "13", "32", 
	"MKK", "2.4G", "40M", "HT", "2T", "13", "32",
	"FCC", "2.4G", "40M", "HT", "2T", "14", "63", 
	"ETSI", "2.4G", "40M", "HT", "2T", "14", "63", 
	"MKK", "2.4G", "40M", "HT", "2T", "14", "63",
	"FCC", "5G", "20M", "OFDM", "1T", "36", "30", 
	"ETSI", "5G", "20M", "OFDM", "1T", "36", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "36", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "40", "30", 
	"ETSI", "5G", "20M", "OFDM", "1T", "40", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "40", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "44", "30", 
	"ETSI", "5G", "20M", "OFDM", "1T", "44", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "44", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "48", "30", 
	"ETSI", "5G", "20M", "OFDM", "1T", "48", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "48", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "52", "36", 
	"ETSI", "5G", "20M", "OFDM", "1T", "52", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "52", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "56", "34", 
	"ETSI", "5G", "20M", "OFDM", "1T", "56", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "56", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "60", "32", 
	"ETSI", "5G", "20M", "OFDM", "1T", "60", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "60", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "64", "28", 
	"ETSI", "5G", "20M", "OFDM", "1T", "64", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "64", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "100", "30", 
	"ETSI", "5G", "20M", "OFDM", "1T", "100", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "100", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "114", "30", 
	"ETSI", "5G", "20M", "OFDM", "1T", "114", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "114", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "108", "32", 
	"ETSI", "5G", "20M", "OFDM", "1T", "108", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "108", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "112", "34", 
	"ETSI", "5G", "20M", "OFDM", "1T", "112", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "112", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "116", "34", 
	"ETSI", "5G", "20M", "OFDM", "1T", "116", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "116", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "120", "36", 
	"ETSI", "5G", "20M", "OFDM", "1T", "120", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "120", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "124", "34", 
	"ETSI", "5G", "20M", "OFDM", "1T", "124", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "124", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "128", "32", 
	"ETSI", "5G", "20M", "OFDM", "1T", "128", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "128", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "132", "30", 
	"ETSI", "5G", "20M", "OFDM", "1T", "132", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "132", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "136", "30", 
	"ETSI", "5G", "20M", "OFDM", "1T", "136", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "136", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "140", "28", 
	"ETSI", "5G", "20M", "OFDM", "1T", "140", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "140", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "149", "36", 
	"ETSI", "5G", "20M", "OFDM", "1T", "149", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "149", "63",
	"FCC", "5G", "20M", "OFDM", "1T", "153", "36", 
	"ETSI", "5G", "20M", "OFDM", "1T", "153", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "153", "63",
	"FCC", "5G", "20M", "OFDM", "1T", "157", "36", 
	"ETSI", "5G", "20M", "OFDM", "1T", "157", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "157", "63",
	"FCC", "5G", "20M", "OFDM", "1T", "161", "36", 
	"ETSI", "5G", "20M", "OFDM", "1T", "161", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "161", "63",
	"FCC", "5G", "20M", "OFDM", "1T", "165", "36", 
	"ETSI", "5G", "20M", "OFDM", "1T", "165", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "165", "63",
	"FCC", "5G", "20M", "HT", "1T", "36", "30", 
	"ETSI", "5G", "20M", "HT", "1T", "36", "32", 
	"MKK", "5G", "20M", "HT", "1T", "36", "32",
	"FCC", "5G", "20M", "HT", "1T", "40", "30", 
	"ETSI", "5G", "20M", "HT", "1T", "40", "32", 
	"MKK", "5G", "20M", "HT", "1T", "40", "32",
	"FCC", "5G", "20M", "HT", "1T", "44", "30", 
	"ETSI", "5G", "20M", "HT", "1T", "44", "32", 
	"MKK", "5G", "20M", "HT", "1T", "44", "32",
	"FCC", "5G", "20M", "HT", "1T", "48", "30", 
	"ETSI", "5G", "20M", "HT", "1T", "48", "32", 
	"MKK", "5G", "20M", "HT", "1T", "48", "32",
	"FCC", "5G", "20M", "HT", "1T", "52", "36", 
	"ETSI", "5G", "20M", "HT", "1T", "52", "32", 
	"MKK", "5G", "20M", "HT", "1T", "52", "32",
	"FCC", "5G", "20M", "HT", "1T", "56", "34", 
	"ETSI", "5G", "20M", "HT", "1T", "56", "32", 
	"MKK", "5G", "20M", "HT", "1T", "56", "32",
	"FCC", "5G", "20M", "HT", "1T", "60", "32", 
	"ETSI", "5G", "20M", "HT", "1T", "60", "32", 
	"MKK", "5G", "20M", "HT", "1T", "60", "32",
	"FCC", "5G", "20M", "HT", "1T", "64", "28", 
	"ETSI", "5G", "20M", "HT", "1T", "64", "32", 
	"MKK", "5G", "20M", "HT", "1T", "64", "32",
	"FCC", "5G", "20M", "HT", "1T", "100", "30", 
	"ETSI", "5G", "20M", "HT", "1T", "100", "32", 
	"MKK", "5G", "20M", "HT", "1T", "100", "32",
	"FCC", "5G", "20M", "HT", "1T", "114", "30", 
	"ETSI", "5G", "20M", "HT", "1T", "114", "32", 
	"MKK", "5G", "20M", "HT", "1T", "114", "32",
	"FCC", "5G", "20M", "HT", "1T", "108", "32", 
	"ETSI", "5G", "20M", "HT", "1T", "108", "32", 
	"MKK", "5G", "20M", "HT", "1T", "108", "32",
	"FCC", "5G", "20M", "HT", "1T", "112", "34", 
	"ETSI", "5G", "20M", "HT", "1T", "112", "32", 
	"MKK", "5G", "20M", "HT", "1T", "112", "32",
	"FCC", "5G", "20M", "HT", "1T", "116", "34", 
	"ETSI", "5G", "20M", "HT", "1T", "116", "32", 
	"MKK", "5G", "20M", "HT", "1T", "116", "32",
	"FCC", "5G", "20M", "HT", "1T", "120", "36", 
	"ETSI", "5G", "20M", "HT", "1T", "120", "32", 
	"MKK", "5G", "20M", "HT", "1T", "120", "32",
	"FCC", "5G", "20M", "HT", "1T", "124", "34", 
	"ETSI", "5G", "20M", "HT", "1T", "124", "32", 
	"MKK", "5G", "20M", "HT", "1T", "124", "32",
	"FCC", "5G", "20M", "HT", "1T", "128", "32", 
	"ETSI", "5G", "20M", "HT", "1T", "128", "32", 
	"MKK", "5G", "20M", "HT", "1T", "128", "32",
	"FCC", "5G", "20M", "HT", "1T", "132", "30", 
	"ETSI", "5G", "20M", "HT", "1T", "132", "32", 
	"MKK", "5G", "20M", "HT", "1T", "132", "32",
	"FCC", "5G", "20M", "HT", "1T", "136", "30", 
	"ETSI", "5G", "20M", "HT", "1T", "136", "32", 
	"MKK", "5G", "20M", "HT", "1T", "136", "32",
	"FCC", "5G", "20M", "HT", "1T", "140", "28", 
	"ETSI", "5G", "20M", "HT", "1T", "140", "32", 
	"MKK", "5G", "20M", "HT", "1T", "140", "32",
	"FCC", "5G", "20M", "HT", "1T", "149", "36", 
	"ETSI", "5G", "20M", "HT", "1T", "149", "32", 
	"MKK", "5G", "20M", "HT", "1T", "149", "63",
	"FCC", "5G", "20M", "HT", "1T", "153", "36", 
	"ETSI", "5G", "20M", "HT", "1T", "153", "32", 
	"MKK", "5G", "20M", "HT", "1T", "153", "63",
	"FCC", "5G", "20M", "HT", "1T", "157", "36", 
	"ETSI", "5G", "20M", "HT", "1T", "157", "32", 
	"MKK", "5G", "20M", "HT", "1T", "157", "63",
	"FCC", "5G", "20M", "HT", "1T", "161", "36", 
	"ETSI", "5G", "20M", "HT", "1T", "161", "32", 
	"MKK", "5G", "20M", "HT", "1T", "161", "63",
	"FCC", "5G", "20M", "HT", "1T", "165", "36", 
	"ETSI", "5G", "20M", "HT", "1T", "165", "32", 
	"MKK", "5G", "20M", "HT", "1T", "165", "63",
	"FCC", "5G", "20M", "HT", "2T", "36", "28", 
	"ETSI", "5G", "20M", "HT", "2T", "36", "30", 
	"MKK", "5G", "20M", "HT", "2T", "36", "30",
	"FCC", "5G", "20M", "HT", "2T", "40", "28", 
	"ETSI", "5G", "20M", "HT", "2T", "40", "30", 
	"MKK", "5G", "20M", "HT", "2T", "40", "30",
	"FCC", "5G", "20M", "HT", "2T", "44", "28", 
	"ETSI", "5G", "20M", "HT", "2T", "44", "30", 
	"MKK", "5G", "20M", "HT", "2T", "44", "30",
	"FCC", "5G", "20M", "HT", "2T", "48", "28", 
	"ETSI", "5G", "20M", "HT", "2T", "48", "30", 
	"MKK", "5G", "20M", "HT", "2T", "48", "30",
	"FCC", "5G", "20M", "HT", "2T", "52", "34", 
	"ETSI", "5G", "20M", "HT", "2T", "52", "30", 
	"MKK", "5G", "20M", "HT", "2T", "52", "30",
	"FCC", "5G", "20M", "HT", "2T", "56", "32", 
	"ETSI", "5G", "20M", "HT", "2T", "56", "30", 
	"MKK", "5G", "20M", "HT", "2T", "56", "30",
	"FCC", "5G", "20M", "HT", "2T", "60", "30", 
	"ETSI", "5G", "20M", "HT", "2T", "60", "30", 
	"MKK", "5G", "20M", "HT", "2T", "60", "30",
	"FCC", "5G", "20M", "HT", "2T", "64", "26", 
	"ETSI", "5G", "20M", "HT", "2T", "64", "30", 
	"MKK", "5G", "20M", "HT", "2T", "64", "30",
	"FCC", "5G", "20M", "HT", "2T", "100", "28", 
	"ETSI", "5G", "20M", "HT", "2T", "100", "30", 
	"MKK", "5G", "20M", "HT", "2T", "100", "30",
	"FCC", "5G", "20M", "HT", "2T", "114", "28", 
	"ETSI", "5G", "20M", "HT", "2T", "114", "30", 
	"MKK", "5G", "20M", "HT", "2T", "114", "30",
	"FCC", "5G", "20M", "HT", "2T", "108", "30", 
	"ETSI", "5G", "20M", "HT", "2T", "108", "30", 
	"MKK", "5G", "20M", "HT", "2T", "108", "30",
	"FCC", "5G", "20M", "HT", "2T", "112", "32", 
	"ETSI", "5G", "20M", "HT", "2T", "112", "30", 
	"MKK", "5G", "20M", "HT", "2T", "112", "30",
	"FCC", "5G", "20M", "HT", "2T", "116", "32", 
	"ETSI", "5G", "20M", "HT", "2T", "116", "30", 
	"MKK", "5G", "20M", "HT", "2T", "116", "30",
	"FCC", "5G", "20M", "HT", "2T", "120", "34", 
	"ETSI", "5G", "20M", "HT", "2T", "120", "30", 
	"MKK", "5G", "20M", "HT", "2T", "120", "30",
	"FCC", "5G", "20M", "HT", "2T", "124", "32", 
	"ETSI", "5G", "20M", "HT", "2T", "124", "30", 
	"MKK", "5G", "20M", "HT", "2T", "124", "30",
	"FCC", "5G", "20M", "HT", "2T", "128", "30", 
	"ETSI", "5G", "20M", "HT", "2T", "128", "30", 
	"MKK", "5G", "20M", "HT", "2T", "128", "30",
	"FCC", "5G", "20M", "HT", "2T", "132", "28", 
	"ETSI", "5G", "20M", "HT", "2T", "132", "30", 
	"MKK", "5G", "20M", "HT", "2T", "132", "30",
	"FCC", "5G", "20M", "HT", "2T", "136", "28", 
	"ETSI", "5G", "20M", "HT", "2T", "136", "30", 
	"MKK", "5G", "20M", "HT", "2T", "136", "30",
	"FCC", "5G", "20M", "HT", "2T", "140", "26", 
	"ETSI", "5G", "20M", "HT", "2T", "140", "30", 
	"MKK", "5G", "20M", "HT", "2T", "140", "30",
	"FCC", "5G", "20M", "HT", "2T", "149", "34", 
	"ETSI", "5G", "20M", "HT", "2T", "149", "30", 
	"MKK", "5G", "20M", "HT", "2T", "149", "63",
	"FCC", "5G", "20M", "HT", "2T", "153", "34", 
	"ETSI", "5G", "20M", "HT", "2T", "153", "30", 
	"MKK", "5G", "20M", "HT", "2T", "153", "63",
	"FCC", "5G", "20M", "HT", "2T", "157", "34", 
	"ETSI", "5G", "20M", "HT", "2T", "157", "30", 
	"MKK", "5G", "20M", "HT", "2T", "157", "63",
	"FCC", "5G", "20M", "HT", "2T", "161", "34", 
	"ETSI", "5G", "20M", "HT", "2T", "161", "30", 
	"MKK", "5G", "20M", "HT", "2T", "161", "63",
	"FCC", "5G", "20M", "HT", "2T", "165", "34", 
	"ETSI", "5G", "20M", "HT", "2T", "165", "30", 
	"MKK", "5G", "20M", "HT", "2T", "165", "63",
	"FCC", "5G", "40M", "HT", "1T", "38", "30", 
	"ETSI", "5G", "40M", "HT", "1T", "38", "32", 
	"MKK", "5G", "40M", "HT", "1T", "38", "32",
	"FCC", "5G", "40M", "HT", "1T", "46", "30", 
	"ETSI", "5G", "40M", "HT", "1T", "46", "32", 
	"MKK", "5G", "40M", "HT", "1T", "46", "32",
	"FCC", "5G", "40M", "HT", "1T", "54", "32", 
	"ETSI", "5G", "40M", "HT", "1T", "54", "32", 
	"MKK", "5G", "40M", "HT", "1T", "54", "32",
	"FCC", "5G", "40M", "HT", "1T", "62", "32", 
	"ETSI", "5G", "40M", "HT", "1T", "62", "32", 
	"MKK", "5G", "40M", "HT", "1T", "62", "32",
	"FCC", "5G", "40M", "HT", "1T", "102", "28", 
	"ETSI", "5G", "40M", "HT", "1T", "102", "32", 
	"MKK", "5G", "40M", "HT", "1T", "102", "32",
	"FCC", "5G", "40M", "HT", "1T", "110", "32", 
	"ETSI", "5G", "40M", "HT", "1T", "110", "32", 
	"MKK", "5G", "40M", "HT", "1T", "110", "32",
	"FCC", "5G", "40M", "HT", "1T", "118", "36", 
	"ETSI", "5G", "40M", "HT", "1T", "118", "32", 
	"MKK", "5G", "40M", "HT", "1T", "118", "32",
	"FCC", "5G", "40M", "HT", "1T", "126", "34", 
	"ETSI", "5G", "40M", "HT", "1T", "126", "32", 
	"MKK", "5G", "40M", "HT", "1T", "126", "32",
	"FCC", "5G", "40M", "HT", "1T", "134", "32", 
	"ETSI", "5G", "40M", "HT", "1T", "134", "32", 
	"MKK", "5G", "40M", "HT", "1T", "134", "32",
	"FCC", "5G", "40M", "HT", "1T", "151", "36", 
	"ETSI", "5G", "40M", "HT", "1T", "151", "32", 
	"MKK", "5G", "40M", "HT", "1T", "151", "63",
	"FCC", "5G", "40M", "HT", "1T", "159", "36", 
	"ETSI", "5G", "40M", "HT", "1T", "159", "32", 
	"MKK", "5G", "40M", "HT", "1T", "159", "63",
	"FCC", "5G", "40M", "HT", "2T", "38", "28", 
	"ETSI", "5G", "40M", "HT", "2T", "38", "30", 
	"MKK", "5G", "40M", "HT", "2T", "38", "30",
	"FCC", "5G", "40M", "HT", "2T", "46", "28", 
	"ETSI", "5G", "40M", "HT", "2T", "46", "30", 
	"MKK", "5G", "40M", "HT", "2T", "46", "30",
	"FCC", "5G", "40M", "HT", "2T", "54", "30", 
	"ETSI", "5G", "40M", "HT", "2T", "54", "30", 
	"MKK", "5G", "40M", "HT", "2T", "54", "30",
	"FCC", "5G", "40M", "HT", "2T", "62", "30", 
	"ETSI", "5G", "40M", "HT", "2T", "62", "30", 
	"MKK", "5G", "40M", "HT", "2T", "62", "30",
	"FCC", "5G", "40M", "HT", "2T", "102", "26", 
	"ETSI", "5G", "40M", "HT", "2T", "102", "30", 
	"MKK", "5G", "40M", "HT", "2T", "102", "30",
	"FCC", "5G", "40M", "HT", "2T", "110", "30", 
	"ETSI", "5G", "40M", "HT", "2T", "110", "30", 
	"MKK", "5G", "40M", "HT", "2T", "110", "30",
	"FCC", "5G", "40M", "HT", "2T", "118", "34", 
	"ETSI", "5G", "40M", "HT", "2T", "118", "30", 
	"MKK", "5G", "40M", "HT", "2T", "118", "30",
	"FCC", "5G", "40M", "HT", "2T", "126", "32", 
	"ETSI", "5G", "40M", "HT", "2T", "126", "30", 
	"MKK", "5G", "40M", "HT", "2T", "126", "30",
	"FCC", "5G", "40M", "HT", "2T", "134", "30", 
	"ETSI", "5G", "40M", "HT", "2T", "134", "30", 
	"MKK", "5G", "40M", "HT", "2T", "134", "30",
	"FCC", "5G", "40M", "HT", "2T", "151", "34", 
	"ETSI", "5G", "40M", "HT", "2T", "151", "30", 
	"MKK", "5G", "40M", "HT", "2T", "151", "63",
	"FCC", "5G", "40M", "HT", "2T", "159", "34", 
	"ETSI", "5G", "40M", "HT", "2T", "159", "30", 
	"MKK", "5G", "40M", "HT", "2T", "159", "63",
	"FCC", "5G", "80M", "VHT", "1T", "42", "30", 
	"ETSI", "5G", "80M", "VHT", "1T", "42", "32", 
	"MKK", "5G", "80M", "VHT", "1T", "42", "32",
	"FCC", "5G", "80M", "VHT", "1T", "58", "28", 
	"ETSI", "5G", "80M", "VHT", "1T", "58", "32", 
	"MKK", "5G", "80M", "VHT", "1T", "58", "32",
	"FCC", "5G", "80M", "VHT", "1T", "106", "30", 
	"ETSI", "5G", "80M", "VHT", "1T", "106", "32", 
	"MKK", "5G", "80M", "VHT", "1T", "106", "32",
	"FCC", "5G", "80M", "VHT", "1T", "122", "34", 
	"ETSI", "5G", "80M", "VHT", "1T", "122", "32", 
	"MKK", "5G", "80M", "VHT", "1T", "122", "32",
	"FCC", "5G", "80M", "VHT", "1T", "155", "36", 
	"ETSI", "5G", "80M", "VHT", "1T", "155", "32", 
	"MKK", "5G", "80M", "VHT", "1T", "155", "63",
	"FCC", "5G", "80M", "VHT", "2T", "42", "28", 
	"ETSI", "5G", "80M", "VHT", "2T", "42", "30", 
	"MKK", "5G", "80M", "VHT", "2T", "42", "30",
	"FCC", "5G", "80M", "VHT", "2T", "58", "26", 
	"ETSI", "5G", "80M", "VHT", "2T", "58", "30", 
	"MKK", "5G", "80M", "VHT", "2T", "58", "30",
	"FCC", "5G", "80M", "VHT", "2T", "106", "28", 
	"ETSI", "5G", "80M", "VHT", "2T", "106", "30", 
	"MKK", "5G", "80M", "VHT", "2T", "106", "30",
	"FCC", "5G", "80M", "VHT", "2T", "122", "32", 
	"ETSI", "5G", "80M", "VHT", "2T", "122", "30", 
	"MKK", "5G", "80M", "VHT", "2T", "122", "30",
	"FCC", "5G", "80M", "VHT", "2T", "155", "34", 
	"ETSI", "5G", "80M", "VHT", "2T", "155", "30", 
	"MKK", "5G", "80M", "VHT", "2T", "155", "63"
};

void
ODM_ReadAndConfig_MP_8812A_TXPWR_LMT(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
	u4Byte     i           = 0;
	u4Byte     ArrayLen    = sizeof(Array_MP_8812A_TXPWR_LMT)/sizeof(pu1Byte);
	pu1Byte    *Array      = Array_MP_8812A_TXPWR_LMT;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_8812A_TXPWR_LMT\n"));

	for (i = 0; i < ArrayLen; i += 7 )
	{
	    pu1Byte regulation = Array[i];
	    pu1Byte band = Array[i+1];
	    pu1Byte bandwidth = Array[i+2];
	    pu1Byte rate = Array[i+3];
	    pu1Byte rfPath = Array[i+4];
	    pu1Byte chnl = Array[i+5];
	    pu1Byte val = Array[i+6];
	
	 	 odm_ConfigBB_TXPWR_LMT_8812A(pDM_Odm, regulation, band, bandwidth, rate, rfPath, chnl, val);
	}

}

#endif // end of HWIMG_SUPPORT

