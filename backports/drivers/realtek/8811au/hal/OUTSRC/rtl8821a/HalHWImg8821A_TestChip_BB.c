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
*                           AGC_TAB.TXT
******************************************************************************/

u4Byte Array_TC_8821A_AGC_TAB[] = { 
		0x81C, 0xBF000001,
		0x81C, 0xBF020001,
		0x81C, 0xBF040001,
		0x81C, 0xBF060001,
		0x81C, 0xBE080001,
		0x81C, 0xBD0A0001,
		0x81C, 0xBC0C0001,
		0x81C, 0xBA0E0001,
		0x81C, 0xB9100001,
		0x81C, 0xB8120001,
		0x81C, 0xB7140001,
		0x81C, 0xB6160001,
		0x81C, 0xB5180001,
		0x81C, 0xB41A0001,
		0x81C, 0xB31C0001,
		0x81C, 0xB21E0001,
		0x81C, 0xB1200001,
		0x81C, 0xB0220001,
		0x81C, 0xAF240001,
		0x81C, 0xAE260001,
		0x81C, 0xAD280001,
		0x81C, 0xAC2A0001,
		0x81C, 0xAB2C0001,
		0x81C, 0xAA2E0001,
		0x81C, 0xA9300001,
		0x81C, 0xA8320001,
		0x81C, 0xA7340001,
		0x81C, 0xA6360001,
		0x81C, 0xA5380001,
		0x81C, 0xA43A0001,
		0x81C, 0xA33C0001,
		0x81C, 0x673E0001,
		0x81C, 0x66400001,
		0x81C, 0x65420001,
		0x81C, 0x64440001,
		0x81C, 0x63460001,
		0x81C, 0x62480001,
		0x81C, 0x614A0001,
		0x81C, 0x474C0001,
		0x81C, 0x464E0001,
		0x81C, 0x45500001,
		0x81C, 0x44520001,
		0x81C, 0x43540001,
		0x81C, 0x42560001,
		0x81C, 0x41580001,
		0x81C, 0x285A0001,
		0x81C, 0x275C0001,
		0x81C, 0x265E0001,
		0x81C, 0x25600001,
		0x81C, 0x24620001,
		0x81C, 0x0A640001,
		0x81C, 0x09660001,
		0x81C, 0x08680001,
		0x81C, 0x076A0001,
		0x81C, 0x066C0001,
		0x81C, 0x056E0001,
		0x81C, 0x04700001,
		0x81C, 0x03720001,
		0x81C, 0x02740001,
		0x81C, 0x01760001,
		0x81C, 0x01780001,
		0x81C, 0x017A0001,
		0x81C, 0x017C0001,
		0x81C, 0x017E0001,
	0xFF0F02C0, 0xABCD,
		0x81C, 0xFB000101,
		0x81C, 0xFA020101,
		0x81C, 0xF9040101,
		0x81C, 0xF8060101,
		0x81C, 0xF7080101,
		0x81C, 0xF60A0101,
		0x81C, 0xF50C0101,
		0x81C, 0xF40E0101,
		0x81C, 0xF3100101,
		0x81C, 0xF2120101,
		0x81C, 0xF1140101,
		0x81C, 0xF0160101,
		0x81C, 0xEF180101,
		0x81C, 0xEE1A0101,
		0x81C, 0xED1C0101,
		0x81C, 0xEC1E0101,
		0x81C, 0xEB200101,
		0x81C, 0xEA220101,
		0x81C, 0xE9240101,
		0x81C, 0xE8260101,
		0x81C, 0xE7280101,
		0x81C, 0xE62A0101,
		0x81C, 0xE52C0101,
		0x81C, 0xE42E0101,
		0x81C, 0xE3300101,
		0x81C, 0xA5320101,
		0x81C, 0xA4340101,
		0x81C, 0xA3360101,
		0x81C, 0x87380101,
		0x81C, 0x863A0101,
		0x81C, 0x853C0101,
		0x81C, 0x843E0101,
		0x81C, 0x69400101,
		0x81C, 0x68420101,
		0x81C, 0x67440101,
		0x81C, 0x66460101,
		0x81C, 0x49480101,
		0x81C, 0x484A0101,
		0x81C, 0x474C0101,
		0x81C, 0x2A4E0101,
		0x81C, 0x29500101,
		0x81C, 0x28520101,
		0x81C, 0x27540101,
		0x81C, 0x26560101,
		0x81C, 0x25580101,
		0x81C, 0x245A0101,
		0x81C, 0x235C0101,
		0x81C, 0x055E0101,
		0x81C, 0x04600101,
		0x81C, 0x03620101,
		0x81C, 0x02640101,
		0x81C, 0x01660101,
		0x81C, 0x01680101,
		0x81C, 0x016A0101,
		0x81C, 0x016C0101,
		0x81C, 0x016E0101,
		0x81C, 0x01700101,
		0x81C, 0x01720101,
	0xCDCDCDCD, 0xCDCD,
		0x81C, 0xFF000101,
		0x81C, 0xFF020101,
		0x81C, 0xFE040101,
		0x81C, 0xFD060101,
		0x81C, 0xFC080101,
		0x81C, 0xFD0A0101,
		0x81C, 0xFC0C0101,
		0x81C, 0xFB0E0101,
		0x81C, 0xFA100101,
		0x81C, 0xF9120101,
		0x81C, 0xF8140101,
		0x81C, 0xF7160101,
		0x81C, 0xF6180101,
		0x81C, 0xF51A0101,
		0x81C, 0xF41C0101,
		0x81C, 0xF31E0101,
		0x81C, 0xF2200101,
		0x81C, 0xF1220101,
		0x81C, 0xF0240101,
		0x81C, 0xEF260101,
		0x81C, 0xEE280101,
		0x81C, 0xED2A0101,
		0x81C, 0xEC2C0101,
		0x81C, 0xEB2E0101,
		0x81C, 0xEA300101,
		0x81C, 0xE9320101,
		0x81C, 0xE8340101,
		0x81C, 0xE7360101,
		0x81C, 0xE6380101,
		0x81C, 0xE53A0101,
		0x81C, 0xE43C0101,
		0x81C, 0xE33E0101,
		0x81C, 0xA5400101,
		0x81C, 0xA4420101,
		0x81C, 0xA3440101,
		0x81C, 0x87460101,
		0x81C, 0x86480101,
		0x81C, 0x854A0101,
		0x81C, 0x844C0101,
		0x81C, 0x694E0101,
		0x81C, 0x68500101,
		0x81C, 0x67520101,
		0x81C, 0x66540101,
		0x81C, 0x49560101,
		0x81C, 0x48580101,
		0x81C, 0x475A0101,
		0x81C, 0x2A5C0101,
		0x81C, 0x295E0101,
		0x81C, 0x28600101,
		0x81C, 0x27620101,
		0x81C, 0x26640101,
		0x81C, 0x25660101,
		0x81C, 0x24680101,
		0x81C, 0x236A0101,
		0x81C, 0x056C0101,
		0x81C, 0x046E0101,
		0x81C, 0x03700101,
		0x81C, 0x02720101,
	0xFF0F02C0, 0xDEAD,
		0x81C, 0x01740101,
		0x81C, 0x01760101,
		0x81C, 0x01780101,
		0x81C, 0x017A0101,
		0x81C, 0x017C0101,
		0x81C, 0x017E0101,
		0xC50, 0x00000022,
		0xC50, 0x00000020,

};

void
ODM_ReadAndConfig_TC_8821A_AGC_TAB(
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
	u4Byte     ArrayLen    = sizeof(Array_TC_8821A_AGC_TAB)/sizeof(u4Byte);
	pu4Byte    Array       = Array_TC_8821A_AGC_TAB;


	hex += board;
	hex += _interface << 8;
	hex += platform << 16;
	hex += 0xFF000000;
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, ("===> ODM_ReadAndConfig_TC_8821A_AGC_TAB, hex = 0x%X\n", hex));

	for (i = 0; i < ArrayLen; i += 2 )
	{
	    u4Byte v1 = Array[i];
	    u4Byte v2 = Array[i+1];
	
	    // This (offset, data) pair meets the condition.
	    if ( v1 < 0xCDCDCDCD )
	    {
		    odm_ConfigBB_AGC_8821A(pDM_Odm, v1, bMaskDWord, v2);
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
		     		odm_ConfigBB_AGC_8821A(pDM_Odm, v1, bMaskDWord, v2);
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
*                           PHY_REG.TXT
******************************************************************************/

u4Byte Array_TC_8821A_PHY_REG[] = { 
		0x800, 0x0020D090,
		0x804, 0x080112E0,
		0x808, 0x0E028211,
		0x80C, 0x92131111,
		0x810, 0x20101261,
		0x814, 0x020C3D10,
		0x818, 0x03A00385,
		0x820, 0x00000000,
		0x824, 0x00030FE0,
		0x828, 0x00000000,
		0x82C, 0x002081DD,
		0x830, 0x2AAA8E24,
		0x834, 0x0037A706,
		0x838, 0x06489B44,
		0x83C, 0x0000095B,
		0x840, 0xC0000001,
		0x844, 0x40003CDE,
		0x848, 0x62103F8B,
		0x84C, 0x6CFDFFB8,
		0x850, 0x28874706,
		0x854, 0x0001520C,
		0x858, 0x8060E000,
		0x85C, 0x74210168,
		0x860, 0x6929C321,
		0x864, 0x79727432,
		0x868, 0x8CA7A314,
		0x86C, 0x888C2878,
		0x870, 0x08888888,
		0x874, 0x31612C2E,
		0x878, 0x00000152,
		0x87C, 0x000FD000,
		0x8A0, 0x00000013,
		0x8A4, 0x7F7F7F7F,
		0x8A8, 0xA2000338,
		0x8AC, 0x0FF0FA0A,
		0x8B4, 0x000FC080,
		0x8B8, 0x6C0057FF,
		0x8BC, 0x0CA52090,
		0x8C0, 0x1BF00020,
		0x8C4, 0x00000000,
		0x8C8, 0x00013169,
		0x8CC, 0x08248492,
		0x8D4, 0x940008A0,
		0x8D8, 0x290B5612,
		0x8F8, 0x400002C0,
		0x8FC, 0x00000000,
		0x900, 0x00000700,
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
		0x974, 0x000003FF,
		0x978, 0x00000000,
		0x97C, 0x00000000,
		0x980, 0x00000000,
		0x984, 0x00000000,
		0x988, 0x00000000,
		0x9A4, 0x00480080,
		0x9A8, 0x00000000,
		0x9AC, 0x00000000,
		0x9B0, 0x81081008,
		0x9B4, 0x01081008,
		0x9B8, 0x01081008,
		0x9BC, 0x01081008,
		0x9D0, 0x00000000,
		0x9D4, 0x00000000,
		0x9D8, 0x00000000,
		0x9DC, 0x00000000,
		0x9E0, 0x00005D00,
		0x9E4, 0x00000002,
		0x9E8, 0x00000000,
		0xA00, 0x00D047C8,
		0xA04, 0x01FF000C,
		0xA08, 0x8C8A8300,
		0xA0C, 0x2E68000F,
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
		0xA80, 0x21805490,
		0xA84, 0x001F0000,
		0xB00, 0x03100040,
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
		0xC20, 0x2C2C2C2C,
		0xC24, 0x30303030,
		0xC28, 0x30303030,
		0xC2C, 0x2C2C2C2C,
		0xC30, 0x2C2C2C2C,
		0xC34, 0x2C2C2C2C,
		0xC38, 0x2C2C2C2C,
		0xC3C, 0x2A2A2A2A,
		0xC40, 0x2A2A2A2A,
		0xC44, 0x2A2A2A2A,
		0xC48, 0x2A2A2A2A,
		0xC4C, 0x2A2A2A2A,
		0xC50, 0x00000020,
		0xC54, 0x001C1208,
		0xC58, 0x30000C1C,
		0xC5C, 0x00000058,
		0xC60, 0x34344443,
		0xC64, 0x07003333,
		0xC68, 0x19791979,
		0xC6C, 0x19791979,
		0xC70, 0x19791979,
		0xC74, 0x19791979,
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
		0xCB0, 0x77775747,
		0xCB4, 0x10000077,
		0xCB8, 0x00508240,

};

void
ODM_ReadAndConfig_TC_8821A_PHY_REG(
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
	u4Byte     ArrayLen    = sizeof(Array_TC_8821A_PHY_REG)/sizeof(u4Byte);
	pu4Byte    Array       = Array_TC_8821A_PHY_REG;


	hex += board;
	hex += _interface << 8;
	hex += platform << 16;
	hex += 0xFF000000;
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, ("===> ODM_ReadAndConfig_TC_8821A_PHY_REG, hex = 0x%X\n", hex));

	for (i = 0; i < ArrayLen; i += 2 )
	{
	    u4Byte v1 = Array[i];
	    u4Byte v2 = Array[i+1];
	
	    // This (offset, data) pair meets the condition.
	    if ( v1 < 0xCDCDCDCD )
	    {
		   	odm_ConfigBB_PHY_8821A(pDM_Odm, v1, bMaskDWord, v2);
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
		   			odm_ConfigBB_PHY_8821A(pDM_Odm, v1, bMaskDWord, v2);
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
*                           PHY_REG_PG.TXT
******************************************************************************/

u4Byte Array_TC_8821A_PHY_REG_PG[] = { 
	0, 0, 0, 0x00000c20, 0xffffffff, 0x32343638,
	0, 0, 0, 0x00000c24, 0xffffffff, 0x36363838,
	0, 0, 0, 0x00000c28, 0xffffffff, 0x28303234,
	0, 0, 0, 0x00000c2c, 0xffffffff, 0x34363838,
	0, 0, 0, 0x00000c30, 0xffffffff, 0x26283032,
	0, 0, 0, 0x00000c3c, 0xffffffff, 0x32343636,
	0, 0, 0, 0x00000c40, 0xffffffff, 0x24262830,
	0, 0, 0, 0x00000c44, 0x0000ffff, 0x00002022,
	1, 0, 0, 0x00000c24, 0xffffffff, 0x34343636,
	1, 0, 0, 0x00000c28, 0xffffffff, 0x26283032,
	1, 0, 0, 0x00000c2c, 0xffffffff, 0x32343636,
	1, 0, 0, 0x00000c30, 0xffffffff, 0x24262830,
	1, 0, 0, 0x00000c3c, 0xffffffff, 0x32343636,
	1, 0, 0, 0x00000c40, 0xffffffff, 0x24262830,
	1, 0, 0, 0x00000c44, 0x0000ffff, 0x00002022
};

void
ODM_ReadAndConfig_TC_8821A_PHY_REG_PG(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
	u4Byte     hex = 0;
	u4Byte     i           = 0;
	u2Byte     count       = 0;
	pu4Byte    ptr_array   = NULL;
	u1Byte     platform    = pDM_Odm->SupportPlatform;
	u1Byte     _interface   = pDM_Odm->SupportInterface;
	u1Byte     board       = pDM_Odm->BoardType;  
	u4Byte     ArrayLen    = sizeof(Array_TC_8821A_PHY_REG_PG)/sizeof(u4Byte);
	pu4Byte    Array       = Array_TC_8821A_PHY_REG_PG;

	pDM_Odm->PhyRegPgVersion = 1;
	pDM_Odm->PhyRegPgValueType = PHY_REG_PG_EXACT_VALUE;
	hex += board;
	hex += _interface << 8;
	hex += platform << 16;
	hex += 0xFF000000;
	for (i = 0; i < ArrayLen; i += 6 )
	{
	    u4Byte v1 = Array[i];
	    u4Byte v2 = Array[i+1];
	    u4Byte v3 = Array[i+2];
	    u4Byte v4 = Array[i+3];
	    u4Byte v5 = Array[i+4];
	    u4Byte v6 = Array[i+5];

	    // this line is a line of pure_body
	    if ( v1 < 0xCDCDCDCD )
	    {
		 	 odm_ConfigBB_PHY_REG_PG_8821A(pDM_Odm, v1, v2, v3, v4, v5, v6);
		 	 continue;
	    }
	    else
	    { // this line is the start of branch
	        if ( !CheckCondition(Array[i], hex) )
	        { // don't need the hw_body
	            i += 2; // skip the pair of expression
	            v1 = Array[i];
	            v2 = Array[i+1];
	            v3 = Array[i+2];
	            while (v2 != 0xDEAD)
	            {
	                i += 3;
	                v1 = Array[i];
	                v2 = Array[i+1];
	                v3 = Array[i+1];
	            }
	        }
	    }
	}
}



/******************************************************************************
*                           PHY_REG_PG_DNI.TXT
******************************************************************************/

u4Byte Array_TC_8821A_PHY_REG_PG_DNI[] = { 
	0, 0, 0, 0x00000c20, 0xffffffff, 0x38383838,
	0, 0, 0, 0x00000c24, 0xffffffff, 0x38383838,
	0, 0, 0, 0x00000c28, 0xffffffff, 0x30323436,
	0, 0, 0, 0x00000c2c, 0xffffffff, 0x38383838,
	0, 0, 0, 0x00000c30, 0xffffffff, 0x30323436,
	0, 0, 0, 0x00000c3c, 0xffffffff, 0x32323434,
	0, 0, 0, 0x00000c40, 0xffffffff, 0x28303032,
	0, 0, 0, 0x00000c44, 0x0000ffff, 0x00002828,
	1, 0, 0, 0x00000c24, 0xffffffff, 0x30303030,
	1, 0, 0, 0x00000c28, 0xffffffff, 0x30303030,
	1, 0, 0, 0x00000c2c, 0xffffffff, 0x32323434,
	1, 0, 0, 0x00000c30, 0xffffffff, 0x28303032,
	1, 0, 0, 0x00000c3c, 0xffffffff, 0x32323434,
	1, 0, 0, 0x00000c40, 0xffffffff, 0x28303032,
	1, 0, 0, 0x00000c44, 0x0000ffff, 0x00002828
};

void
ODM_ReadAndConfig_TC_8821A_PHY_REG_PG_DNI(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
	u4Byte     hex = 0;
	u4Byte     i           = 0;
	u2Byte     count       = 0;
	pu4Byte    ptr_array   = NULL;
	u1Byte     platform    = pDM_Odm->SupportPlatform;
	u1Byte     _interface   = pDM_Odm->SupportInterface;
	u1Byte     board       = pDM_Odm->BoardType;  
	u4Byte     ArrayLen    = sizeof(Array_TC_8821A_PHY_REG_PG)/sizeof(u4Byte);
	pu4Byte    Array       = Array_TC_8821A_PHY_REG_PG;

	pDM_Odm->PhyRegPgVersion = 1;
	pDM_Odm->PhyRegPgValueType = PHY_REG_PG_EXACT_VALUE;
	hex += board;
	hex += _interface << 8;
	hex += platform << 16;
	hex += 0xFF000000;
	for (i = 0; i < ArrayLen; i += 6 )
	{
	    u4Byte v1 = Array[i];
	    u4Byte v2 = Array[i+1];
	    u4Byte v3 = Array[i+2];
	    u4Byte v4 = Array[i+3];
	    u4Byte v5 = Array[i+4];
	    u4Byte v6 = Array[i+5];

	    // this line is a line of pure_body
	    if ( v1 < 0xCDCDCDCD )
	    {
		 	 odm_ConfigBB_PHY_REG_PG_8821A(pDM_Odm, v1, v2, v3, v4, v5, v6);
		 	 continue;
	    }
	    else
	    { // this line is the start of branch
	        if ( !CheckCondition(Array[i], hex) )
	        { // don't need the hw_body
	            i += 2; // skip the pair of expression
	            v1 = Array[i];
	            v2 = Array[i+1];
	            v3 = Array[i+2];
	            while (v2 != 0xDEAD)
	            {
	                i += 3;
	                v1 = Array[i];
	                v2 = Array[i+1];
	                v3 = Array[i+1];
	            }
	        }
	    }
	}
}



/******************************************************************************
*                           PHY_REG_PG_Sercomm.TXT
******************************************************************************/

u4Byte Array_TC_8821A_PHY_REG_PG_Sercomm[] = { 
	0, 0, 0, 0x00000c20, 0xffffffff, 0x38383838,
	0, 0, 0, 0x00000c24, 0xffffffff, 0x38383838,
	0, 0, 0, 0x00000c28, 0xffffffff, 0x30323436,
	0, 0, 0, 0x00000c2c, 0xffffffff, 0x38383838,
	0, 0, 0, 0x00000c30, 0xffffffff, 0x30323436,
	0, 0, 0, 0x00000c3c, 0xffffffff, 0x32323434,
	0, 0, 0, 0x00000c40, 0xffffffff, 0x28303032,
	0, 0, 0, 0x00000c44, 0x0000ffff, 0x00002828,
	1, 0, 0, 0x00000c24, 0xffffffff, 0x32323434,
	1, 0, 0, 0x00000c28, 0xffffffff, 0x30303032,
	1, 0, 0, 0x00000c2c, 0xffffffff, 0x32323434,
	1, 0, 0, 0x00000c30, 0xffffffff, 0x28303032,
	1, 0, 0, 0x00000c3c, 0xffffffff, 0x32323434,
	1, 0, 0, 0x00000c40, 0xffffffff, 0x28303032,
	1, 0, 0, 0x00000c44, 0x0000ffff, 0x00002828
};

void
ODM_ReadAndConfig_TC_8821A_PHY_REG_PG_Sercomm(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
	u4Byte     hex = 0;
	u4Byte     i           = 0;
	u2Byte     count       = 0;
	pu4Byte    ptr_array   = NULL;
	u1Byte     platform    = pDM_Odm->SupportPlatform;
	u1Byte     _interface   = pDM_Odm->SupportInterface;
	u1Byte     board       = pDM_Odm->BoardType;  
	u4Byte     ArrayLen    = sizeof(Array_TC_8821A_PHY_REG_PG)/sizeof(u4Byte);
	pu4Byte    Array       = Array_TC_8821A_PHY_REG_PG;

	pDM_Odm->PhyRegPgVersion = 1;
	pDM_Odm->PhyRegPgValueType = PHY_REG_PG_EXACT_VALUE;
	hex += board;
	hex += _interface << 8;
	hex += platform << 16;
	hex += 0xFF000000;
	for (i = 0; i < ArrayLen; i += 6 )
	{
	    u4Byte v1 = Array[i];
	    u4Byte v2 = Array[i+1];
	    u4Byte v3 = Array[i+2];
	    u4Byte v4 = Array[i+3];
	    u4Byte v5 = Array[i+4];
	    u4Byte v6 = Array[i+5];

	    // this line is a line of pure_body
	    if ( v1 < 0xCDCDCDCD )
	    {
		 	 odm_ConfigBB_PHY_REG_PG_8821A(pDM_Odm, v1, v2, v3, v4, v5, v6);
		 	 continue;
	    }
	    else
	    { // this line is the start of branch
	        if ( !CheckCondition(Array[i], hex) )
	        { // don't need the hw_body
	            i += 2; // skip the pair of expression
	            v1 = Array[i];
	            v2 = Array[i+1];
	            v3 = Array[i+2];
	            while (v2 != 0xDEAD)
	            {
	                i += 3;
	                v1 = Array[i];
	                v2 = Array[i+1];
	                v3 = Array[i+1];
	            }
	        }
	    }
	}
}



#endif // end of HWIMG_SUPPORT

