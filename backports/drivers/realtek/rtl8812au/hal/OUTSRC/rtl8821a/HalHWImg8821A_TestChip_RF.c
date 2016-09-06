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

//#include "Mp_Precomp.h"
#include "../odm_precomp.h"

#if (RTL8821A_SUPPORT == 1)
static BOOLEAN
CheckCondition(
    const u4Byte  Condition,
    const u4Byte  Hex
    )
{
    u4Byte _board     = (Hex & 0x000000FF);
    u4Byte _interface = (Hex & 0x0000FF00) >> 8;
    u4Byte _platform  = (Hex & 0x00FF0000) >> 16;
    u4Byte cond = Condition;

    if ( Condition == 0xCDCDCDCD )
        return TRUE;

    cond = Condition & 0x000000FF;
    if ( (_board != cond) && (cond != 0xFF) )
        return FALSE;

    cond = Condition & 0x0000FF00;
    cond = cond >> 8;
    if ( ((_interface & cond) == 0) && (cond != 0x07) )
        return FALSE;

    cond = Condition & 0x00FF0000;
    cond = cond >> 16;
    if ( ((_platform & cond) == 0) && (cond != 0x0F) )
        return FALSE;
    return TRUE;
}


/******************************************************************************
*                           RadioA.TXT
******************************************************************************/

u4Byte Array_TC_8821A_RadioA[] = { 
		0x018, 0x0001712A,
		0x056, 0x00051CF2,
		0x066, 0x00040000,
		0x01C, 0x000739D2,
		0x01E, 0x000F8000,
		0x082, 0x00000830,
		0x083, 0x00021800,
		0x084, 0x00028000,
		0x085, 0x00048000,
		0x086, 0x00094838,
		0x087, 0x00044980,
		0x088, 0x00048000,
		0x089, 0x0000D480,
		0x08A, 0x00042240,
		0x08B, 0x000F0380,
		0x08C, 0x00090000,
		0x08D, 0x00022852,
		0x08E, 0x00065540,
		0x08F, 0x00088001,
		0x0EF, 0x00020000,
		0x03E, 0x00000380,
		0x03F, 0x00090018,
		0x03E, 0x00020380,
		0x03F, 0x000A0018,
		0x03E, 0x00040308,
		0x03F, 0x000A0018,
		0x03E, 0x00060018,
		0x03F, 0x000A0018,
		0x0EF, 0x00000000,
		0x018, 0x0001712A,
		0x0EF, 0x00001000,
		0x03A, 0x000000C4,
		0x03B, 0x0003A000,
		0x03A, 0x000001FC,
		0x03B, 0x00032000,
		0x03A, 0x000001B4,
		0x03B, 0x0002A000,
		0x03A, 0x000001C4,
		0x03B, 0x000221C8,
		0x03A, 0x00000188,
		0x03B, 0x0001A001,
		0x03A, 0x000000C4,
		0x03B, 0x0007A000,
		0x03A, 0x000001FC,
		0x03B, 0x00072000,
		0x03A, 0x000001B4,
		0x03B, 0x0006A000,
		0x03A, 0x000001C4,
		0x03B, 0x000621C8,
		0x03A, 0x00000188,
		0x03B, 0x0005A001,
		0x03A, 0x000000C4,
		0x03B, 0x000BA000,
		0x03A, 0x000001FC,
		0x03B, 0x000B2000,
		0x03A, 0x000001B4,
		0x03B, 0x000AA000,
		0x03A, 0x000001C4,
		0x03B, 0x000A21C8,
		0x03A, 0x00000188,
		0x03B, 0x0009A001,
		0x0EF, 0x00000000,
		0x0EF, 0x00001100,
	0xFF0F02C0, 0xABCD,
		0x034, 0x0004A8B1,
		0x034, 0x000498AE,
		0x034, 0x000484AE,
		0x034, 0x000474AB,
		0x034, 0x0004648E,
		0x034, 0x0004548B,
		0x034, 0x0004408D,
		0x034, 0x0004308A,
		0x034, 0x00042087,
		0x034, 0x00041084,
		0x034, 0x00040081,
	0xCDCDCDCD, 0xCDCD,
		0x034, 0x0004ADF7,
		0x034, 0x00049DF3,
		0x034, 0x00048DEF,
		0x034, 0x00047DEC,
		0x034, 0x00046DE9,
		0x034, 0x00045CCB,
		0x034, 0x0004488D,
		0x034, 0x0004348D,
		0x034, 0x0004248A,
		0x034, 0x0004108D,
		0x034, 0x0004008A,
	0xFF0F02C0, 0xDEAD,
	0xFF0F02C0, 0xABCD,
		0x034, 0x0002A8F0,
		0x034, 0x000298AF,
		0x034, 0x000284AF,
		0x034, 0x000274AC,
		0x034, 0x0002648E,
		0x034, 0x0002548B,
		0x034, 0x0002408D,
		0x034, 0x0002308A,
		0x034, 0x00022087,
		0x034, 0x00021084,
		0x034, 0x00020081,
	0xCDCDCDCD, 0xCDCD,
		0x034, 0x0002ADF7,
		0x034, 0x00029DF2,
		0x034, 0x00028DEE,
		0x034, 0x00027DEB,
		0x034, 0x00026CCD,
		0x034, 0x00025CCA,
		0x034, 0x0002488C,
		0x034, 0x0002384C,
		0x034, 0x00022849,
		0x034, 0x00021449,
		0x034, 0x0002004D,
	0xFF0F02C0, 0xDEAD,
	0xFF0F02C0, 0xABCD,
		0x034, 0x0000A9EF,
		0x034, 0x000098EF,
		0x034, 0x000088AE,
		0x034, 0x000074AE,
		0x034, 0x000064AB,
		0x034, 0x0000548E,
		0x034, 0x0000448B,
		0x034, 0x0000308D,
		0x034, 0x0000208A,
		0x034, 0x00001087,
		0x034, 0x00000084,
	0xCDCDCDCD, 0xCDCD,
		0x034, 0x0000ADF7,
		0x034, 0x00009DF4,
		0x034, 0x00008DF1,
		0x034, 0x00007DEE,
		0x034, 0x00006DCD,
		0x034, 0x00005CCD,
		0x034, 0x00004CCA,
		0x034, 0x0000388C,
		0x034, 0x00002888,
		0x034, 0x00001488,
		0x034, 0x00000486,
	0xFF0F02C0, 0xDEAD,
		0x0EF, 0x00000000,
		0x018, 0x0001712A,
		0x0EF, 0x00000040,
		0x035, 0x00000145,
		0x035, 0x00008145,
		0x035, 0x00010145,
		0x035, 0x00020196,
		0x035, 0x00028196,
		0x035, 0x00030196,
		0x035, 0x000401C7,
		0x035, 0x000481C7,
		0x035, 0x000501C7,
		0x0EF, 0x00000000,
		0x018, 0x0001712A,
		0x0EF, 0x00000010,
		0x036, 0x000056B3,
		0x036, 0x0000D6B3,
		0x036, 0x000156B3,
		0x036, 0x0001D6B3,
		0x036, 0x00026634,
		0x036, 0x0002E634,
		0x036, 0x00036634,
		0x036, 0x0003E634,
		0x036, 0x000467B4,
		0x036, 0x0004E7B4,
		0x036, 0x000567B4,
		0x036, 0x0005E7B4,
		0x0EF, 0x00000000,
		0x0EF, 0x00000008,
		0x03C, 0x0000022A,
		0x03C, 0x00000594,
	0xFF0F02C0, 0xABCD,
		0x03C, 0x00000820,
	0xCDCDCDCD, 0xCDCD,
		0x03C, 0x00000900,
	0xFF0F02C0, 0xDEAD,
		0x0EF, 0x00000000,
		0x018, 0x0001712A,
		0x0EF, 0x00000002,
		0x008, 0x00002000,
		0x0EF, 0x00000000,
		0x0DF, 0x000000C0,
		0x01F, 0x00040064,
		0x058, 0x00081184,
		0x059, 0x0006016C,
		0x061, 0x000EAD53,
		0x062, 0x00093BC4,
		0x063, 0x000714E9,
		0x064, 0x0001C67C,
	0xFF0F02C0, 0xABCD,
		0x065, 0x00091011,
	0xCDCDCDCD, 0xCDCD,
		0x065, 0x00091016,
	0xFF0F02C0, 0xDEAD,
		0x018, 0x00000006,
		0x0EF, 0x00002000,
		0x03B, 0x00039258,
		0x03B, 0x00031258,
		0x03B, 0x00029258,
		0x03B, 0x00027A58,
		0x03B, 0x0001FA58,
		0x03B, 0x00012590,
		0x03B, 0x00008248,
		0x03B, 0x00000A40,
		0x0EF, 0x00000000,
		0x0EF, 0x00000100,
		0x034, 0x0000ADF2,
		0x035, 0x00004800,
		0x034, 0x00009DEF,
		0x035, 0x00003C00,
		0x034, 0x00008DEC,
		0x035, 0x00003000,
		0x034, 0x00007DE9,
		0x035, 0x00002400,
		0x034, 0x00006CEC,
		0x035, 0x00003000,
		0x034, 0x00005CE9,
		0x035, 0x00002400,
		0x034, 0x000044EC,
		0x035, 0x00003000,
		0x034, 0x000034E9,
		0x035, 0x00002400,
		0x034, 0x0000246C,
		0x035, 0x00003000,
		0x034, 0x00001469,
		0x035, 0x00002400,
		0x034, 0x0000006C,
		0x035, 0x00003000,
		0x0EF, 0x00000000,
		0x0ED, 0x00000010,
		0x044, 0x0000ADF2,
		0x044, 0x00009DEF,
		0x044, 0x00008DEC,
		0x044, 0x00007DE9,
		0x044, 0x00006CEC,
		0x044, 0x00005CE9,
		0x044, 0x000044EC,
		0x044, 0x000034E9,
		0x044, 0x0000246C,
		0x044, 0x00001469,
		0x044, 0x0000006C,
		0x0ED, 0x00000000,
		0x0ED, 0x00000001,
		0x040, 0x00038DA7,
		0x040, 0x000300C2,
		0x040, 0x000288E2,
		0x040, 0x000200B8,
		0x040, 0x000188A5,
		0x040, 0x00010FBC,
		0x040, 0x00008F71,
		0x040, 0x00000240,
		0x0ED, 0x00000000,
		0x0EF, 0x000020A2,
		0x0DF, 0x00000080,
		0x035, 0x00000120,
		0x035, 0x00008120,
		0x035, 0x00010120,
		0x036, 0x00000085,
		0x036, 0x00008085,
		0x036, 0x00010085,
		0x036, 0x00018085,
		0x0EF, 0x00000000,
		0x051, 0x00000C31,
		0x052, 0x00000622,
		0x053, 0x000FC70B,
		0x054, 0x0000017E,
		0x056, 0x00051DF3,
		0x051, 0x00000C01,
		0x052, 0x000006D6,
		0x053, 0x000FC649,
	0xFF0F0104, 0xABCD,
		0x0DD, 0x00000060,
	0xCDCDCDCD, 0xCDCD,
		0x0DD, 0x00000040,
	0xFF0F0104, 0xDEAD,
		0x070, 0x00049661,
		0x071, 0x0007843E,
		0x072, 0x00000382,
		0x074, 0x00051400,
		0x035, 0x00000160,
		0x035, 0x00008160,
		0x035, 0x00010160,
		0x036, 0x00000124,
		0x036, 0x00008124,
		0x036, 0x00010124,
		0x036, 0x00018124,
		0x0ED, 0x0000000C,
		0x045, 0x00000140,
		0x045, 0x00008140,
		0x045, 0x00010140,
		0x046, 0x00000124,
		0x046, 0x00008124,
		0x046, 0x00010124,
		0x046, 0x00018124,
		0x0DF, 0x00000088,
		0x0B3, 0x000F0618,
		0x0B4, 0x00013E4C,
		0x0B7, 0x00030008,
		0x018, 0x0001F12A,
		0x0FE, 0x00000000,
		0x0FE, 0x00000000,
		0x0FE, 0x00000000,
		0x0FE, 0x00000000,
		0x0B3, 0x000F0E18,
		0x0B4, 0x0001364C,

};

void
ODM_ReadAndConfig_TC_8821A_RadioA(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
	#define READ_NEXT_PAIR(v1, v2, i) do { i += 2; v1 = Array[i]; v2 = Array[i+1]; } while(0)

	u4Byte     hex         = 0;
	u4Byte     i           = 0;
	u2Byte     count       = 0;
	pu4Byte    ptr_array   = NULL;
	u1Byte     platform    = pDM_Odm->SupportPlatform;
	u1Byte     _interface   = pDM_Odm->SupportInterface;
	u1Byte     board       = pDM_Odm->BoardType;  
	u4Byte     ArrayLen    = sizeof(Array_TC_8821A_RadioA)/sizeof(u4Byte);
	pu4Byte    Array       = Array_TC_8821A_RadioA;


	hex += board;
	hex += _interface << 8;
	hex += platform << 16;
	hex += 0xFF000000;
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, ("===> ODM_ReadAndConfig_TC_8821A_RadioA, hex = 0x%X\n", hex));

	for (i = 0; i < ArrayLen; i += 2 )
	{
	    u4Byte v1 = Array[i];
	    u4Byte v2 = Array[i+1];
	
	    // This (offset, data) pair meets the condition.
	    if ( v1 < 0xCDCDCDCD )
	    {
		    odm_ConfigRF_RadioA_8821A(pDM_Odm, v1, v2);
		    continue;
	 	}
		else
		{ // This line is the start line of branch.
		    if ( !CheckCondition(Array[i], hex) )
		    { // Discard the following (offset, data) pairs.
		        READ_NEXT_PAIR(v1, v2, i);
		        while (v2 != 0xDEAD && 
		               v2 != 0xCDEF && 
		               v2 != 0xCDCD && i < ArrayLen -2)
		        {
		            READ_NEXT_PAIR(v1, v2, i);
		        }
		        i -= 2; // prevent from for-loop += 2
		    }
		    else // Configure matched pairs and skip to end of if-else.
		    {
		        READ_NEXT_PAIR(v1, v2, i);
		        while (v2 != 0xDEAD && 
		               v2 != 0xCDEF && 
		               v2 != 0xCDCD && i < ArrayLen -2)
		        {
		    		odm_ConfigRF_RadioA_8821A(pDM_Odm, v1, v2);
		            READ_NEXT_PAIR(v1, v2, i);
		        }

		        while (v2 != 0xDEAD && i < ArrayLen -2)
		        {
		            READ_NEXT_PAIR(v1, v2, i);
		        }
		        
		    }
		}	
	}

}

/******************************************************************************
*                           TxPowerTrack_AP.TXT
******************************************************************************/

u1Byte gDeltaSwingTableIdx_TC_5GB_N__8821A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 4, 5, 5, 6,  6,  7,  7,  8,  8,  9,  9, 10, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 19, 20, 20, 20},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21},
};
u1Byte gDeltaSwingTableIdx_TC_5GB_P__8821A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 16, 17, 17, 18, 19, 20, 20, 20},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 19, 20, 20, 20},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21},
};
u1Byte gDeltaSwingTableIdx_TC_5GA_N__8821A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 4, 4, 5, 5, 6,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 6,  7,  7,  8,  8,  9, 10, 11, 11, 12, 13, 13, 14, 15, 16, 16, 16, 16, 16, 16, 16},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 16, 16, 16, 16, 16},
};
u1Byte gDeltaSwingTableIdx_TC_5GA_P__8821A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 4, 5, 5, 6,  7,  8,  8,  9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 18, 19, 19, 19},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7,  8,  9,  9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21},
};
u1Byte gDeltaSwingTableIdx_TC_2GB_N__8821A[]    = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  9,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_TC_2GB_P__8821A[]    = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};
u1Byte gDeltaSwingTableIdx_TC_2GA_N__8821A[]    = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  8,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_TC_2GA_P__8821A[]    = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};
u1Byte gDeltaSwingTableIdx_TC_2GCCKB_N__8821A[] = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  9,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_TC_2GCCKB_P__8821A[] = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};
u1Byte gDeltaSwingTableIdx_TC_2GCCKA_N__8821A[] = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  8,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_TC_2GCCKA_P__8821A[] = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};

void
ODM_ReadAndConfig_TC_8821A_TxPowerTrack_AP(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
	PODM_RF_CAL_T  pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, ("===> ODM_ReadAndConfig_MP_TC_8821A\n"));


	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GA_P, gDeltaSwingTableIdx_TC_2GA_P__8821A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GA_N, gDeltaSwingTableIdx_TC_2GA_N__8821A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GB_P, gDeltaSwingTableIdx_TC_2GB_P__8821A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GB_N, gDeltaSwingTableIdx_TC_2GB_N__8821A, DELTA_SWINGIDX_SIZE);

	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_P, gDeltaSwingTableIdx_TC_2GCCKA_P__8821A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_N, gDeltaSwingTableIdx_TC_2GCCKA_N__8821A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_P, gDeltaSwingTableIdx_TC_2GCCKB_P__8821A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_N, gDeltaSwingTableIdx_TC_2GCCKB_N__8821A, DELTA_SWINGIDX_SIZE);

	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GA_P, gDeltaSwingTableIdx_TC_5GA_P__8821A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GA_N, gDeltaSwingTableIdx_TC_5GA_N__8821A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GB_P, gDeltaSwingTableIdx_TC_5GB_P__8821A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GB_N, gDeltaSwingTableIdx_TC_5GB_N__8821A, DELTA_SWINGIDX_SIZE*3);
}

/******************************************************************************
*                           TxPowerTrack_PCIE.TXT
******************************************************************************/

u1Byte gDeltaSwingTableIdx_TC_5GB_N_PCIE_8821A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 2, 3, 3, 4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  9,  9,  9, 10, 10, 11, 11, 12, 12, 13},
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  4,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  5,  5,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
};
u1Byte gDeltaSwingTableIdx_TC_5GB_P_PCIE_8821A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  5,  5,  6,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  4,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  5,  5,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
};
u1Byte gDeltaSwingTableIdx_TC_5GA_N_PCIE_8821A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  4,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 12, 13, 13, 13},
	{0, 1, 1, 2, 2, 2, 3, 3, 4,  4,  5,  5,  6,  6,  7,  8,  9,  9, 10, 10, 11, 11, 11, 12, 12, 12, 12, 12, 13, 13},
	{0, 1, 1, 2, 2, 3, 3, 4, 5,  6,  7,  8,  8,  9, 10, 11, 12, 13, 14, 14, 15, 15, 15, 16, 16, 16, 17, 17, 18, 18},
};
u1Byte gDeltaSwingTableIdx_TC_5GA_P_PCIE_8821A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  4,  4,  5,  5,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 2, 3, 3, 4, 4,  4,  5,  5,  6,  6,  7,  7,  8,  9,  9, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 3, 3, 4, 4, 5,  6,  6,  7,  7,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
};
u1Byte gDeltaSwingTableIdx_TC_2GB_N_PCIE_8821A[]    = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  9,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_TC_2GB_P_PCIE_8821A[]    = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};
u1Byte gDeltaSwingTableIdx_TC_2GA_N_PCIE_8821A[]    = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  8,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_TC_2GA_P_PCIE_8821A[]    = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};
u1Byte gDeltaSwingTableIdx_TC_2GCCKB_N_PCIE_8821A[] = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  9,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_TC_2GCCKB_P_PCIE_8821A[] = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};
u1Byte gDeltaSwingTableIdx_TC_2GCCKA_N_PCIE_8821A[] = {0, 1, 1, 1, 2, 2, 2, 3, 3,  3,  4,  4,  5,  5,  5,  6,  6,  6,  7,  8,  8,  9,  9,  9, 10, 10, 10, 10, 11, 11};
u1Byte gDeltaSwingTableIdx_TC_2GCCKA_P_PCIE_8821A[] = {0, 0, 1, 1, 2, 2, 2, 2, 3,  3,  3,  4,  4,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9,  9,  9,  9};

void
ODM_ReadAndConfig_TC_8821A_TxPowerTrack_PCIE(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
	PODM_RF_CAL_T  pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, ("===> ODM_ReadAndConfig_MP_TC_8821A\n"));


	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GA_P, gDeltaSwingTableIdx_TC_2GA_P_PCIE_8821A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GA_N, gDeltaSwingTableIdx_TC_2GA_N_PCIE_8821A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GB_P, gDeltaSwingTableIdx_TC_2GB_P_PCIE_8821A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GB_N, gDeltaSwingTableIdx_TC_2GB_N_PCIE_8821A, DELTA_SWINGIDX_SIZE);

	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_P, gDeltaSwingTableIdx_TC_2GCCKA_P_PCIE_8821A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_N, gDeltaSwingTableIdx_TC_2GCCKA_N_PCIE_8821A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_P, gDeltaSwingTableIdx_TC_2GCCKB_P_PCIE_8821A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_N, gDeltaSwingTableIdx_TC_2GCCKB_N_PCIE_8821A, DELTA_SWINGIDX_SIZE);

	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GA_P, gDeltaSwingTableIdx_TC_5GA_P_PCIE_8821A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GA_N, gDeltaSwingTableIdx_TC_5GA_N_PCIE_8821A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GB_P, gDeltaSwingTableIdx_TC_5GB_P_PCIE_8821A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GB_N, gDeltaSwingTableIdx_TC_5GB_N_PCIE_8821A, DELTA_SWINGIDX_SIZE*3);
}

/******************************************************************************
*                           TxPowerTrack_USB.TXT
******************************************************************************/

u1Byte gDeltaSwingTableIdx_TC_5GB_N_USB_8821A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 4, 5, 6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14},
	{0, 1, 1, 2, 3, 3, 4, 5, 5,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 3, 3, 4, 5, 6,  7,  7,  8,  8,  9,  9, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
};
u1Byte gDeltaSwingTableIdx_TC_5GB_P_USB_8821A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 3, 4, 5,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 3, 3, 4, 5, 5,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 3, 3, 4, 5, 6,  7,  7,  8,  8,  9,  9, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
};
u1Byte gDeltaSwingTableIdx_TC_5GA_N_USB_8821A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 4, 5, 6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 15, 15, 15, 15, 15},
	{0, 1, 1, 2, 2, 3, 4, 5, 6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 15, 15, 15, 15, 15},
	{0, 1, 1, 2, 2, 3, 4, 5, 6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 15, 15, 15, 15, 15},
};
u1Byte gDeltaSwingTableIdx_TC_5GA_P_USB_8821A[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 4, 5, 6,  7,  7,  8,  8,  9, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 3, 3, 4, 5, 6,  7,  7,  8,  8,  9, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
	{0, 1, 1, 2, 3, 3, 4, 5, 6,  7,  7,  8,  8,  9, 10, 11, 11, 12, 12, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
};
u1Byte gDeltaSwingTableIdx_TC_2GB_N_USB_8821A[]    = {0, 1, 1, 2, 2, 2, 3, 3, 3,  4,  4,  5,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11};
u1Byte gDeltaSwingTableIdx_TC_2GB_P_USB_8821A[]    = {0, 1, 1, 2, 2, 2, 3, 3, 3,  4,  4,  4,  5,  5,  5,  6,  6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7};
u1Byte gDeltaSwingTableIdx_TC_2GA_N_USB_8821A[]    = {0, 1, 1, 2, 2, 2, 3, 3, 3,  4,  4,  4,  5,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  9, 10, 10, 10, 10, 10, 10};
u1Byte gDeltaSwingTableIdx_TC_2GA_P_USB_8821A[]    = {0, 1, 1, 2, 2, 2, 3, 3, 3,  4,  4,  4,  5,  5,  5,  6,  6,  6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7};
u1Byte gDeltaSwingTableIdx_TC_2GCCKB_N_USB_8821A[] = {0, 1, 1, 2, 2, 2, 3, 3, 3,  4,  4,  5,  5,  5,  6,  6,  7,  7,  8,  8,  9,  9, 10, 10, 11, 11, 11, 11, 11, 11};
u1Byte gDeltaSwingTableIdx_TC_2GCCKB_P_USB_8821A[] = {0, 1, 1, 2, 2, 2, 3, 3, 3,  4,  4,  4,  5,  5,  5,  6,  6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7};
u1Byte gDeltaSwingTableIdx_TC_2GCCKA_N_USB_8821A[] = {0, 1, 1, 2, 2, 2, 3, 3, 3,  4,  4,  4,  5,  5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  9, 10, 10, 10, 10, 10, 10};
u1Byte gDeltaSwingTableIdx_TC_2GCCKA_P_USB_8821A[] = {0, 1, 1, 2, 2, 2, 3, 3, 3,  4,  4,  4,  5,  5,  5,  6,  6,  6,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7};

void
ODM_ReadAndConfig_TC_8821A_TxPowerTrack_USB(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
	PODM_RF_CAL_T  pRFCalibrateInfo = &(pDM_Odm->RFCalibrateInfo);

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, ("===> ODM_ReadAndConfig_MP_TC_8821A\n"));


	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GA_P, gDeltaSwingTableIdx_TC_2GA_P_USB_8821A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GA_N, gDeltaSwingTableIdx_TC_2GA_N_USB_8821A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GB_P, gDeltaSwingTableIdx_TC_2GB_P_USB_8821A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GB_N, gDeltaSwingTableIdx_TC_2GB_N_USB_8821A, DELTA_SWINGIDX_SIZE);

	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_P, gDeltaSwingTableIdx_TC_2GCCKA_P_USB_8821A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKA_N, gDeltaSwingTableIdx_TC_2GCCKA_N_USB_8821A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_P, gDeltaSwingTableIdx_TC_2GCCKB_P_USB_8821A, DELTA_SWINGIDX_SIZE);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_2GCCKB_N, gDeltaSwingTableIdx_TC_2GCCKB_N_USB_8821A, DELTA_SWINGIDX_SIZE);

	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GA_P, gDeltaSwingTableIdx_TC_5GA_P_USB_8821A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GA_N, gDeltaSwingTableIdx_TC_5GA_N_USB_8821A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GB_P, gDeltaSwingTableIdx_TC_5GB_P_USB_8821A, DELTA_SWINGIDX_SIZE*3);
	ODM_MoveMemory(pDM_Odm, pRFCalibrateInfo->DeltaSwingTableIdx_5GB_N, gDeltaSwingTableIdx_TC_5GB_N_USB_8821A, DELTA_SWINGIDX_SIZE*3);
}

/******************************************************************************
*                           TXPWR_LMT.TXT
******************************************************************************/

pu1Byte Array_TC_8821A_TXPWR_LMT[] = { 
	"FCC", "2.4G", "20M", "CCK", "1T", "01", "34", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "01", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "01", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "02", "34", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "02", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "02", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "03", "34", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "03", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "03", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "04", "34", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "04", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "04", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "05", "34", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "05", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "05", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "06", "34", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "06", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "06", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "07", "34", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "07", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "07", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "08", "34", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "08", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "08", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "09", "34", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "09", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "09", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "10", "34", 
	"ETSI", "2.4G", "20M", "CCK", "1T", "10", "32", 
	"MKK", "2.4G", "20M", "CCK", "1T", "10", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "11", "34", 
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
	"FCC", "2.4G", "20M", "OFDM", "1T", "01", "32", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "01", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "01", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "02", "34", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "02", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "02", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "03", "34", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "03", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "03", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "04", "34", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "04", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "04", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "05", "34", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "05", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "05", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "06", "34", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "06", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "06", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "07", "34", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "07", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "07", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "08", "34", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "08", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "08", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "09", "34", 
	"ETSI", "2.4G", "20M", "OFDM", "1T", "09", "32", 
	"MKK", "2.4G", "20M", "OFDM", "1T", "09", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "10", "34", 
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
	"FCC", "2.4G", "20M", "HT", "1T", "01", "32", 
	"ETSI", "2.4G", "20M", "HT", "1T", "01", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "01", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "02", "34", 
	"ETSI", "2.4G", "20M", "HT", "1T", "02", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "02", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "03", "34", 
	"ETSI", "2.4G", "20M", "HT", "1T", "03", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "03", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "04", "34", 
	"ETSI", "2.4G", "20M", "HT", "1T", "04", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "04", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "05", "34", 
	"ETSI", "2.4G", "20M", "HT", "1T", "05", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "05", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "06", "34", 
	"ETSI", "2.4G", "20M", "HT", "1T", "06", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "06", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "07", "34", 
	"ETSI", "2.4G", "20M", "HT", "1T", "07", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "07", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "08", "34", 
	"ETSI", "2.4G", "20M", "HT", "1T", "08", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "08", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "09", "34", 
	"ETSI", "2.4G", "20M", "HT", "1T", "09", "32", 
	"MKK", "2.4G", "20M", "HT", "1T", "09", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "10", "34", 
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
	"FCC", "2.4G", "20M", "HT", "2T", "01", "30", 
	"ETSI", "2.4G", "20M", "HT", "2T", "01", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "01", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "02", "32", 
	"ETSI", "2.4G", "20M", "HT", "2T", "02", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "02", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "03", "32", 
	"ETSI", "2.4G", "20M", "HT", "2T", "03", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "03", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "04", "32", 
	"ETSI", "2.4G", "20M", "HT", "2T", "04", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "04", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "05", "32", 
	"ETSI", "2.4G", "20M", "HT", "2T", "05", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "05", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "06", "32", 
	"ETSI", "2.4G", "20M", "HT", "2T", "06", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "06", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "07", "32", 
	"ETSI", "2.4G", "20M", "HT", "2T", "07", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "07", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "08", "32", 
	"ETSI", "2.4G", "20M", "HT", "2T", "08", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "08", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "09", "32", 
	"ETSI", "2.4G", "20M", "HT", "2T", "09", "32", 
	"MKK", "2.4G", "20M", "HT", "2T", "09", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "10", "32", 
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
	"FCC", "2.4G", "40M", "HT", "1T", "04", "34", 
	"ETSI", "2.4G", "40M", "HT", "1T", "04", "32", 
	"MKK", "2.4G", "40M", "HT", "1T", "04", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "05", "34", 
	"ETSI", "2.4G", "40M", "HT", "1T", "05", "32", 
	"MKK", "2.4G", "40M", "HT", "1T", "05", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "06", "34", 
	"ETSI", "2.4G", "40M", "HT", "1T", "06", "32", 
	"MKK", "2.4G", "40M", "HT", "1T", "06", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "07", "34", 
	"ETSI", "2.4G", "40M", "HT", "1T", "07", "32", 
	"MKK", "2.4G", "40M", "HT", "1T", "07", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "08", "34", 
	"ETSI", "2.4G", "40M", "HT", "1T", "08", "32", 
	"MKK", "2.4G", "40M", "HT", "1T", "08", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "09", "34", 
	"ETSI", "2.4G", "40M", "HT", "1T", "09", "32", 
	"MKK", "2.4G", "40M", "HT", "1T", "09", "32",
	"FCC", "2.4G", "40M", "HT", "1T", "10", "34", 
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
	"FCC", "2.4G", "40M", "HT", "2T", "04", "32", 
	"ETSI", "2.4G", "40M", "HT", "2T", "04", "30", 
	"MKK", "2.4G", "40M", "HT", "2T", "04", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "05", "32", 
	"ETSI", "2.4G", "40M", "HT", "2T", "05", "30", 
	"MKK", "2.4G", "40M", "HT", "2T", "05", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "06", "32", 
	"ETSI", "2.4G", "40M", "HT", "2T", "06", "30", 
	"MKK", "2.4G", "40M", "HT", "2T", "06", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "07", "32", 
	"ETSI", "2.4G", "40M", "HT", "2T", "07", "30", 
	"MKK", "2.4G", "40M", "HT", "2T", "07", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "08", "32", 
	"ETSI", "2.4G", "40M", "HT", "2T", "08", "30", 
	"MKK", "2.4G", "40M", "HT", "2T", "08", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "09", "32", 
	"ETSI", "2.4G", "40M", "HT", "2T", "09", "30", 
	"MKK", "2.4G", "40M", "HT", "2T", "09", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "10", "32", 
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
	"FCC", "5G", "20M", "OFDM", "1T", "52", "34", 
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
	"FCC", "5G", "20M", "OFDM", "1T", "120", "34", 
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
	"FCC", "5G", "20M", "OFDM", "1T", "149", "34", 
	"ETSI", "5G", "20M", "OFDM", "1T", "149", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "149", "63",
	"FCC", "5G", "20M", "OFDM", "1T", "153", "34", 
	"ETSI", "5G", "20M", "OFDM", "1T", "153", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "153", "63",
	"FCC", "5G", "20M", "OFDM", "1T", "157", "34", 
	"ETSI", "5G", "20M", "OFDM", "1T", "157", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "157", "63",
	"FCC", "5G", "20M", "OFDM", "1T", "161", "34", 
	"ETSI", "5G", "20M", "OFDM", "1T", "161", "32", 
	"MKK", "5G", "20M", "OFDM", "1T", "161", "63",
	"FCC", "5G", "20M", "OFDM", "1T", "165", "34", 
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
	"FCC", "5G", "20M", "HT", "1T", "52", "34", 
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
	"FCC", "5G", "20M", "HT", "1T", "120", "34", 
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
	"FCC", "5G", "20M", "HT", "1T", "149", "34", 
	"ETSI", "5G", "20M", "HT", "1T", "149", "32", 
	"MKK", "5G", "20M", "HT", "1T", "149", "63",
	"FCC", "5G", "20M", "HT", "1T", "153", "34", 
	"ETSI", "5G", "20M", "HT", "1T", "153", "32", 
	"MKK", "5G", "20M", "HT", "1T", "153", "63",
	"FCC", "5G", "20M", "HT", "1T", "157", "34", 
	"ETSI", "5G", "20M", "HT", "1T", "157", "32", 
	"MKK", "5G", "20M", "HT", "1T", "157", "63",
	"FCC", "5G", "20M", "HT", "1T", "161", "34", 
	"ETSI", "5G", "20M", "HT", "1T", "161", "32", 
	"MKK", "5G", "20M", "HT", "1T", "161", "63",
	"FCC", "5G", "20M", "HT", "1T", "165", "34", 
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
	"FCC", "5G", "40M", "HT", "1T", "118", "34", 
	"ETSI", "5G", "40M", "HT", "1T", "118", "32", 
	"MKK", "5G", "40M", "HT", "1T", "118", "32",
	"FCC", "5G", "40M", "HT", "1T", "126", "34", 
	"ETSI", "5G", "40M", "HT", "1T", "126", "32", 
	"MKK", "5G", "40M", "HT", "1T", "126", "32",
	"FCC", "5G", "40M", "HT", "1T", "134", "32", 
	"ETSI", "5G", "40M", "HT", "1T", "134", "32", 
	"MKK", "5G", "40M", "HT", "1T", "134", "32",
	"FCC", "5G", "40M", "HT", "1T", "151", "34", 
	"ETSI", "5G", "40M", "HT", "1T", "151", "32", 
	"MKK", "5G", "40M", "HT", "1T", "151", "63",
	"FCC", "5G", "40M", "HT", "1T", "159", "34", 
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
	"FCC", "5G", "80M", "VHT", "1T", "155", "34", 
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
ODM_ReadAndConfig_TC_8821A_TXPWR_LMT(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
	u4Byte     i           = 0;
	u4Byte     ArrayLen    = sizeof(Array_TC_8821A_TXPWR_LMT)/sizeof(pu1Byte);
	pu1Byte    *Array      = Array_TC_8821A_TXPWR_LMT;


	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, ("===> ODM_ReadAndConfig_TC_8821A_TXPWR_LMT\n"));

	for (i = 0; i < ArrayLen; i += 7 )
	{
	    pu1Byte regulation = Array[i];
	    pu1Byte band = Array[i+1];
	    pu1Byte bandwidth = Array[i+2];
	    pu1Byte rate = Array[i+3];
	    pu1Byte rfPath = Array[i+4];
	    pu1Byte chnl = Array[i+5];
	    pu1Byte val = Array[i+6];
	
	 	 odm_ConfigBB_TXPWR_LMT_8821A(pDM_Odm, regulation, band, bandwidth, rate, rfPath, chnl, val);
	}

}

#endif // end of HWIMG_SUPPORT

