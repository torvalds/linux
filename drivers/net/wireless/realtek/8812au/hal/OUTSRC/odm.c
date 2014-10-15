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

//============================================================
// include files
//============================================================

#include "odm_precomp.h"


const u2Byte dB_Invert_Table[8][12] = {
	{	1,		1,		1,		2,		2,		2,		2,		3,		3,		3,		4,		4},
	{	4,		5,		6,		6,		7,		8,		9,		10,		11,		13,		14,		16},
	{	18,		20,		22,		25,		28,		32,		35,		40,		45,		50,		56,		63},
	{	71,		79,		89,		100,	112,	126,	141,	158,	178,	200,	224,	251},
	{	282,	316,	355,	398,	447,	501,	562,	631,	708,	794,	891,	1000},
	{	1122,	1259,	1413,	1585,	1778,	1995,	2239,	2512,	2818,	3162,	3548,	3981},
	{	4467,	5012,	5623,	6310,	7079,	7943,	8913,	10000,	11220,	12589,	14125,	15849},
	{	17783,	19953,	22387,	25119,	28184,	31623,	35481,	39811,	44668,	50119,	56234,	65535}};


//============================================================
// Global var
//============================================================

u4Byte	OFDMSwingTable[OFDM_TABLE_SIZE] = {
	0x7f8001fe,	// 0, +6.0dB
	0x788001e2,	// 1, +5.5dB
	0x71c001c7,	// 2, +5.0dB
	0x6b8001ae,	// 3, +4.5dB
	0x65400195,	// 4, +4.0dB
	0x5fc0017f,	// 5, +3.5dB
	0x5a400169,	// 6, +3.0dB
	0x55400155,	// 7, +2.5dB
	0x50800142,	// 8, +2.0dB
	0x4c000130,	// 9, +1.5dB
	0x47c0011f,	// 10, +1.0dB
	0x43c0010f,	// 11, +0.5dB
	0x40000100,	// 12, +0dB
	0x3c8000f2,	// 13, -0.5dB
	0x390000e4,	// 14, -1.0dB
	0x35c000d7,	// 15, -1.5dB
	0x32c000cb,	// 16, -2.0dB
	0x300000c0,	// 17, -2.5dB
	0x2d4000b5,	// 18, -3.0dB
	0x2ac000ab,	// 19, -3.5dB
	0x288000a2,	// 20, -4.0dB
	0x26000098,	// 21, -4.5dB
	0x24000090,	// 22, -5.0dB
	0x22000088,	// 23, -5.5dB
	0x20000080,	// 24, -6.0dB
	0x1e400079,	// 25, -6.5dB
	0x1c800072,	// 26, -7.0dB
	0x1b00006c,	// 27. -7.5dB
	0x19800066,	// 28, -8.0dB
	0x18000060,	// 29, -8.5dB
	0x16c0005b,	// 30, -9.0dB
	0x15800056,	// 31, -9.5dB
	0x14400051,	// 32, -10.0dB
	0x1300004c,	// 33, -10.5dB
	0x12000048,	// 34, -11.0dB
	0x11000044,	// 35, -11.5dB
	0x10000040,	// 36, -12.0dB
};

u1Byte	CCKSwingTable_Ch1_Ch13[CCK_TABLE_SIZE][8] = {
	{0x36, 0x35, 0x2e, 0x25, 0x1c, 0x12, 0x09, 0x04},	// 0, +0dB
	{0x33, 0x32, 0x2b, 0x23, 0x1a, 0x11, 0x08, 0x04},	// 1, -0.5dB
	{0x30, 0x2f, 0x29, 0x21, 0x19, 0x10, 0x08, 0x03},	// 2, -1.0dB
	{0x2d, 0x2d, 0x27, 0x1f, 0x18, 0x0f, 0x08, 0x03},	// 3, -1.5dB
	{0x2b, 0x2a, 0x25, 0x1e, 0x16, 0x0e, 0x07, 0x03},	// 4, -2.0dB 
	{0x28, 0x28, 0x22, 0x1c, 0x15, 0x0d, 0x07, 0x03},	// 5, -2.5dB
	{0x26, 0x25, 0x21, 0x1b, 0x14, 0x0d, 0x06, 0x03},	// 6, -3.0dB
	{0x24, 0x23, 0x1f, 0x19, 0x13, 0x0c, 0x06, 0x03},	// 7, -3.5dB
	{0x22, 0x21, 0x1d, 0x18, 0x11, 0x0b, 0x06, 0x02},	// 8, -4.0dB 
	{0x20, 0x20, 0x1b, 0x16, 0x11, 0x08, 0x05, 0x02},	// 9, -4.5dB
	{0x1f, 0x1e, 0x1a, 0x15, 0x10, 0x0a, 0x05, 0x02},	// 10, -5.0dB 
	{0x1d, 0x1c, 0x18, 0x14, 0x0f, 0x0a, 0x05, 0x02},	// 11, -5.5dB
	{0x1b, 0x1a, 0x17, 0x13, 0x0e, 0x09, 0x04, 0x02},	// 12, -6.0dB <== default
	{0x1a, 0x19, 0x16, 0x12, 0x0d, 0x09, 0x04, 0x02},	// 13, -6.5dB
	{0x18, 0x17, 0x15, 0x11, 0x0c, 0x08, 0x04, 0x02},	// 14, -7.0dB 
	{0x17, 0x16, 0x13, 0x10, 0x0c, 0x08, 0x04, 0x02},	// 15, -7.5dB
	{0x16, 0x15, 0x12, 0x0f, 0x0b, 0x07, 0x04, 0x01},	// 16, -8.0dB 
	{0x14, 0x14, 0x11, 0x0e, 0x0b, 0x07, 0x03, 0x02},	// 17, -8.5dB
	{0x13, 0x13, 0x10, 0x0d, 0x0a, 0x06, 0x03, 0x01},	// 18, -9.0dB 
	{0x12, 0x12, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01},	// 19, -9.5dB
	{0x11, 0x11, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01},	// 20, -10.0dB
	{0x10, 0x10, 0x0e, 0x0b, 0x08, 0x05, 0x03, 0x01},	// 21, -10.5dB
	{0x0f, 0x0f, 0x0d, 0x0b, 0x08, 0x05, 0x03, 0x01},	// 22, -11.0dB
	{0x0e, 0x0e, 0x0c, 0x0a, 0x08, 0x05, 0x02, 0x01},	// 23, -11.5dB
	{0x0d, 0x0d, 0x0c, 0x0a, 0x07, 0x05, 0x02, 0x01},	// 24, -12.0dB
	{0x0d, 0x0c, 0x0b, 0x09, 0x07, 0x04, 0x02, 0x01},	// 25, -12.5dB
	{0x0c, 0x0c, 0x0a, 0x09, 0x06, 0x04, 0x02, 0x01},	// 26, -13.0dB
	{0x0b, 0x0b, 0x0a, 0x08, 0x06, 0x04, 0x02, 0x01},	// 27, -13.5dB
	{0x0b, 0x0a, 0x09, 0x08, 0x06, 0x04, 0x02, 0x01},	// 28, -14.0dB
	{0x0a, 0x0a, 0x09, 0x07, 0x05, 0x03, 0x02, 0x01},	// 29, -14.5dB
	{0x0a, 0x09, 0x08, 0x07, 0x05, 0x03, 0x02, 0x01},	// 30, -15.0dB
	{0x09, 0x09, 0x08, 0x06, 0x05, 0x03, 0x01, 0x01},	// 31, -15.5dB
	{0x09, 0x08, 0x07, 0x06, 0x04, 0x03, 0x01, 0x01}	// 32, -16.0dB
};


u1Byte	CCKSwingTable_Ch14[CCK_TABLE_SIZE][8] = {
	{0x36, 0x35, 0x2e, 0x1b, 0x00, 0x00, 0x00, 0x00},	// 0, +0dB  
	{0x33, 0x32, 0x2b, 0x19, 0x00, 0x00, 0x00, 0x00},	// 1, -0.5dB 
	{0x30, 0x2f, 0x29, 0x18, 0x00, 0x00, 0x00, 0x00},	// 2, -1.0dB  
	{0x2d, 0x2d, 0x17, 0x17, 0x00, 0x00, 0x00, 0x00},	// 3, -1.5dB
	{0x2b, 0x2a, 0x25, 0x15, 0x00, 0x00, 0x00, 0x00},	// 4, -2.0dB  
	{0x28, 0x28, 0x24, 0x14, 0x00, 0x00, 0x00, 0x00},	// 5, -2.5dB
	{0x26, 0x25, 0x21, 0x13, 0x00, 0x00, 0x00, 0x00},	// 6, -3.0dB  
	{0x24, 0x23, 0x1f, 0x12, 0x00, 0x00, 0x00, 0x00},	// 7, -3.5dB  
	{0x22, 0x21, 0x1d, 0x11, 0x00, 0x00, 0x00, 0x00},	// 8, -4.0dB  
	{0x20, 0x20, 0x1b, 0x10, 0x00, 0x00, 0x00, 0x00},	// 9, -4.5dB
	{0x1f, 0x1e, 0x1a, 0x0f, 0x00, 0x00, 0x00, 0x00},	// 10, -5.0dB  
	{0x1d, 0x1c, 0x18, 0x0e, 0x00, 0x00, 0x00, 0x00},	// 11, -5.5dB
	{0x1b, 0x1a, 0x17, 0x0e, 0x00, 0x00, 0x00, 0x00},	// 12, -6.0dB  <== default
	{0x1a, 0x19, 0x16, 0x0d, 0x00, 0x00, 0x00, 0x00},	// 13, -6.5dB 
	{0x18, 0x17, 0x15, 0x0c, 0x00, 0x00, 0x00, 0x00},	// 14, -7.0dB  
	{0x17, 0x16, 0x13, 0x0b, 0x00, 0x00, 0x00, 0x00},	// 15, -7.5dB
	{0x16, 0x15, 0x12, 0x0b, 0x00, 0x00, 0x00, 0x00},	// 16, -8.0dB  
	{0x14, 0x14, 0x11, 0x0a, 0x00, 0x00, 0x00, 0x00},	// 17, -8.5dB
	{0x13, 0x13, 0x10, 0x0a, 0x00, 0x00, 0x00, 0x00},	// 18, -9.0dB  
	{0x12, 0x12, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00},	// 19, -9.5dB
	{0x11, 0x11, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00},	// 20, -10.0dB
	{0x10, 0x10, 0x0e, 0x08, 0x00, 0x00, 0x00, 0x00},	// 21, -10.5dB
	{0x0f, 0x0f, 0x0d, 0x08, 0x00, 0x00, 0x00, 0x00},	// 22, -11.0dB
	{0x0e, 0x0e, 0x0c, 0x07, 0x00, 0x00, 0x00, 0x00},	// 23, -11.5dB
	{0x0d, 0x0d, 0x0c, 0x07, 0x00, 0x00, 0x00, 0x00},	// 24, -12.0dB
	{0x0d, 0x0c, 0x0b, 0x06, 0x00, 0x00, 0x00, 0x00},	// 25, -12.5dB
	{0x0c, 0x0c, 0x0a, 0x06, 0x00, 0x00, 0x00, 0x00},	// 26, -13.0dB
	{0x0b, 0x0b, 0x0a, 0x06, 0x00, 0x00, 0x00, 0x00},	// 27, -13.5dB
	{0x0b, 0x0a, 0x09, 0x05, 0x00, 0x00, 0x00, 0x00},	// 28, -14.0dB
	{0x0a, 0x0a, 0x09, 0x05, 0x00, 0x00, 0x00, 0x00},	// 29, -14.5dB
	{0x0a, 0x09, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00},	// 30, -15.0dB
	{0x09, 0x09, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00},	// 31, -15.5dB
	{0x09, 0x08, 0x07, 0x04, 0x00, 0x00, 0x00, 0x00}	// 32, -16.0dB
};


u4Byte OFDMSwingTable_New[OFDM_TABLE_SIZE] = {
	0x0b40002d, // 0,  -15.0dB	
	0x0c000030, // 1,  -14.5dB
	0x0cc00033, // 2,  -14.0dB
	0x0d800036, // 3,  -13.5dB
	0x0e400039, // 4,  -13.0dB    
	0x0f00003c, // 5,  -12.5dB
	0x10000040, // 6,  -12.0dB
	0x11000044, // 7,  -11.5dB
	0x12000048, // 8,  -11.0dB
	0x1300004c, // 9,  -10.5dB
	0x14400051, // 10, -10.0dB
	0x15800056, // 11, -9.5dB
	0x16c0005b, // 12, -9.0dB
	0x18000060, // 13, -8.5dB
	0x19800066, // 14, -8.0dB
	0x1b00006c, // 15, -7.5dB
	0x1c800072, // 16, -7.0dB
	0x1e400079, // 17, -6.5dB
	0x20000080, // 18, -6.0dB
	0x22000088, // 19, -5.5dB
	0x24000090, // 20, -5.0dB
	0x26000098, // 21, -4.5dB
	0x288000a2, // 22, -4.0dB
	0x2ac000ab, // 23, -3.5dB
	0x2d4000b5, // 24, -3.0dB
	0x300000c0, // 25, -2.5dB
	0x32c000cb, // 26, -2.0dB
	0x35c000d7, // 27, -1.5dB
	0x390000e4, // 28, -1.0dB
	0x3c8000f2, // 29, -0.5dB
	0x40000100, // 30, +0dB
	0x43c0010f, // 31, +0.5dB
	0x47c0011f, // 32, +1.0dB
	0x4c000130, // 33, +1.5dB
	0x50800142, // 34, +2.0dB
	0x55400155, // 35, +2.5dB
	0x5a400169, // 36, +3.0dB
	0x5fc0017f, // 37, +3.5dB
	0x65400195, // 38, +4.0dB
	0x6b8001ae, // 39, +4.5dB
	0x71c001c7, // 40, +5.0dB
	0x788001e2, // 41, +5.5dB
	0x7f8001fe  // 42, +6.0dB
};               


u1Byte CCKSwingTable_Ch1_Ch13_New[CCK_TABLE_SIZE][8] = {
	{0x09, 0x08, 0x07, 0x06, 0x04, 0x03, 0x01, 0x01},	//  0, -16.0dB
	{0x09, 0x09, 0x08, 0x06, 0x05, 0x03, 0x01, 0x01},	//  1, -15.5dB
	{0x0a, 0x09, 0x08, 0x07, 0x05, 0x03, 0x02, 0x01},	//  2, -15.0dB
	{0x0a, 0x0a, 0x09, 0x07, 0x05, 0x03, 0x02, 0x01},	//  3, -14.5dB
	{0x0b, 0x0a, 0x09, 0x08, 0x06, 0x04, 0x02, 0x01},	//  4, -14.0dB
	{0x0b, 0x0b, 0x0a, 0x08, 0x06, 0x04, 0x02, 0x01},	//  5, -13.5dB
	{0x0c, 0x0c, 0x0a, 0x09, 0x06, 0x04, 0x02, 0x01},	//  6, -13.0dB
	{0x0d, 0x0c, 0x0b, 0x09, 0x07, 0x04, 0x02, 0x01},	//  7, -12.5dB
	{0x0d, 0x0d, 0x0c, 0x0a, 0x07, 0x05, 0x02, 0x01},	//  8, -12.0dB
	{0x0e, 0x0e, 0x0c, 0x0a, 0x08, 0x05, 0x02, 0x01},	//  9, -11.5dB
	{0x0f, 0x0f, 0x0d, 0x0b, 0x08, 0x05, 0x03, 0x01},	// 10, -11.0dB
	{0x10, 0x10, 0x0e, 0x0b, 0x08, 0x05, 0x03, 0x01},	// 11, -10.5dB
	{0x11, 0x11, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01},	// 12, -10.0dB
	{0x12, 0x12, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01},	// 13, -9.5dB
	{0x13, 0x13, 0x10, 0x0d, 0x0a, 0x06, 0x03, 0x01},	// 14, -9.0dB 
	{0x14, 0x14, 0x11, 0x0e, 0x0b, 0x07, 0x03, 0x02},	// 15, -8.5dB
	{0x16, 0x15, 0x12, 0x0f, 0x0b, 0x07, 0x04, 0x01},	// 16, -8.0dB 
	{0x17, 0x16, 0x13, 0x10, 0x0c, 0x08, 0x04, 0x02},	// 17, -7.5dB
	{0x18, 0x17, 0x15, 0x11, 0x0c, 0x08, 0x04, 0x02},	// 18, -7.0dB 
	{0x1a, 0x19, 0x16, 0x12, 0x0d, 0x09, 0x04, 0x02},	// 19, -6.5dB
    {0x1b, 0x1a, 0x17, 0x13, 0x0e, 0x09, 0x04, 0x02},	// 20, -6.0dB 
	{0x1d, 0x1c, 0x18, 0x14, 0x0f, 0x0a, 0x05, 0x02},	// 21, -5.5dB
	{0x1f, 0x1e, 0x1a, 0x15, 0x10, 0x0a, 0x05, 0x02},	// 22, -5.0dB 
	{0x20, 0x20, 0x1b, 0x16, 0x11, 0x08, 0x05, 0x02},	// 23, -4.5dB
	{0x22, 0x21, 0x1d, 0x18, 0x11, 0x0b, 0x06, 0x02},	// 24, -4.0dB 
	{0x24, 0x23, 0x1f, 0x19, 0x13, 0x0c, 0x06, 0x03},	// 25, -3.5dB
	{0x26, 0x25, 0x21, 0x1b, 0x14, 0x0d, 0x06, 0x03},	// 26, -3.0dB
	{0x28, 0x28, 0x22, 0x1c, 0x15, 0x0d, 0x07, 0x03},	// 27, -2.5dB
	{0x2b, 0x2a, 0x25, 0x1e, 0x16, 0x0e, 0x07, 0x03},	// 28, -2.0dB 
	{0x2d, 0x2d, 0x27, 0x1f, 0x18, 0x0f, 0x08, 0x03},	// 29, -1.5dB
	{0x30, 0x2f, 0x29, 0x21, 0x19, 0x10, 0x08, 0x03},	// 30, -1.0dB
	{0x33, 0x32, 0x2b, 0x23, 0x1a, 0x11, 0x08, 0x04},	// 31, -0.5dB
	{0x36, 0x35, 0x2e, 0x25, 0x1c, 0x12, 0x09, 0x04} 	// 32, +0dB
};                                                                  


u1Byte CCKSwingTable_Ch14_New[CCK_TABLE_SIZE][8]= {
	{0x09, 0x08, 0x07, 0x04, 0x00, 0x00, 0x00, 0x00},	//  0, -16.0dB
	{0x09, 0x09, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00},	//  1, -15.5dB
	{0x0a, 0x09, 0x08, 0x05, 0x00, 0x00, 0x00, 0x00},	//  2, -15.0dB
	{0x0a, 0x0a, 0x09, 0x05, 0x00, 0x00, 0x00, 0x00},	//  3, -14.5dB
	{0x0b, 0x0a, 0x09, 0x05, 0x00, 0x00, 0x00, 0x00},	//  4, -14.0dB
	{0x0b, 0x0b, 0x0a, 0x06, 0x00, 0x00, 0x00, 0x00},	//  5, -13.5dB
	{0x0c, 0x0c, 0x0a, 0x06, 0x00, 0x00, 0x00, 0x00},	//  6, -13.0dB
	{0x0d, 0x0c, 0x0b, 0x06, 0x00, 0x00, 0x00, 0x00},	//  7, -12.5dB
	{0x0d, 0x0d, 0x0c, 0x07, 0x00, 0x00, 0x00, 0x00},	//  8, -12.0dB
	{0x0e, 0x0e, 0x0c, 0x07, 0x00, 0x00, 0x00, 0x00},	//  9, -11.5dB
	{0x0f, 0x0f, 0x0d, 0x08, 0x00, 0x00, 0x00, 0x00},	// 10, -11.0dB
	{0x10, 0x10, 0x0e, 0x08, 0x00, 0x00, 0x00, 0x00},	// 11, -10.5dB
	{0x11, 0x11, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00},	// 12, -10.0dB
	{0x12, 0x12, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00},	// 13, -9.5dB
	{0x13, 0x13, 0x10, 0x0a, 0x00, 0x00, 0x00, 0x00},	// 14, -9.0dB  
	{0x14, 0x14, 0x11, 0x0a, 0x00, 0x00, 0x00, 0x00},	// 15, -8.5dB
	{0x16, 0x15, 0x12, 0x0b, 0x00, 0x00, 0x00, 0x00},	// 16, -8.0dB  
	{0x17, 0x16, 0x13, 0x0b, 0x00, 0x00, 0x00, 0x00},	// 17, -7.5dB
	{0x18, 0x17, 0x15, 0x0c, 0x00, 0x00, 0x00, 0x00},	// 18, -7.0dB  
	{0x1a, 0x19, 0x16, 0x0d, 0x00, 0x00, 0x00, 0x00},	// 19, -6.5dB 
	{0x1b, 0x1a, 0x17, 0x0e, 0x00, 0x00, 0x00, 0x00},	// 20, -6.0dB  
	{0x1d, 0x1c, 0x18, 0x0e, 0x00, 0x00, 0x00, 0x00},	// 21, -5.5dB
	{0x1f, 0x1e, 0x1a, 0x0f, 0x00, 0x00, 0x00, 0x00},	// 22, -5.0dB  
	{0x20, 0x20, 0x1b, 0x10, 0x00, 0x00, 0x00, 0x00},	// 23, -4.5dB
	{0x22, 0x21, 0x1d, 0x11, 0x00, 0x00, 0x00, 0x00},	// 24, -4.0dB  
	{0x24, 0x23, 0x1f, 0x12, 0x00, 0x00, 0x00, 0x00},	// 25, -3.5dB  
	{0x26, 0x25, 0x21, 0x13, 0x00, 0x00, 0x00, 0x00},	// 26, -3.0dB  
	{0x28, 0x28, 0x24, 0x14, 0x00, 0x00, 0x00, 0x00},	// 27, -2.5dB
	{0x2b, 0x2a, 0x25, 0x15, 0x00, 0x00, 0x00, 0x00},	// 28, -2.0dB  
	{0x2d, 0x2d, 0x17, 0x17, 0x00, 0x00, 0x00, 0x00},	// 29, -1.5dB
	{0x30, 0x2f, 0x29, 0x18, 0x00, 0x00, 0x00, 0x00},	// 30, -1.0dB  
	{0x33, 0x32, 0x2b, 0x19, 0x00, 0x00, 0x00, 0x00},	// 31, -0.5dB 
	{0x36, 0x35, 0x2e, 0x1b, 0x00, 0x00, 0x00, 0x00} 	// 32, +0dB	
};

u4Byte TxScalingTable_Jaguar[TXSCALE_TABLE_SIZE] =
{
	0x081, // 0,  -12.0dB
	0x088, // 1,  -11.5dB
	0x090, // 2,  -11.0dB
	0x099, // 3,  -10.5dB
	0x0A2, // 4,  -10.0dB
	0x0AC, // 5,  -9.5dB
	0x0B6, // 6,  -9.0dB
	0x0C0, // 7,  -8.5dB
	0x0CC, // 8,  -8.0dB
	0x0D8, // 9,  -7.5dB
	0x0E5, // 10, -7.0dB
	0x0F2, // 11, -6.5dB
	0x101, // 12, -6.0dB
	0x110, // 13, -5.5dB
	0x120, // 14, -5.0dB
	0x131, // 15, -4.5dB
	0x143, // 16, -4.0dB
	0x156, // 17, -3.5dB
	0x16A, // 18, -3.0dB
	0x180, // 19, -2.5dB
	0x197, // 20, -2.0dB
	0x1AF, // 21, -1.5dB
	0x1C8, // 22, -1.0dB
	0x1E3, // 23, -0.5dB
	0x200, // 24, +0  dB
	0x21E, // 25, +0.5dB
	0x23E, // 26, +1.0dB
	0x261, // 27, +1.5dB
	0x285, // 28, +2.0dB
	0x2AB, // 29, +2.5dB
	0x2D3, // 30, +3.0dB
	0x2FE, // 31, +3.5dB
	0x32B, // 32, +4.0dB
	0x35C, // 33, +4.5dB
	0x38E, // 34, +5.0dB
	0x3C4, // 35, +5.5dB
	0x3FE  // 36, +6.0dB	
};

#ifdef AP_BUILD_WORKAROUND

unsigned int TxPwrTrk_OFDM_SwingTbl[TxPwrTrk_OFDM_SwingTbl_Len] = {
	/*  +6.0dB */ 0x7f8001fe,
	/*  +5.5dB */ 0x788001e2,
	/*  +5.0dB */ 0x71c001c7,
	/*  +4.5dB */ 0x6b8001ae,
	/*  +4.0dB */ 0x65400195,
	/*  +3.5dB */ 0x5fc0017f,
	/*  +3.0dB */ 0x5a400169,
	/*  +2.5dB */ 0x55400155,
	/*  +2.0dB */ 0x50800142,
	/*  +1.5dB */ 0x4c000130,
	/*  +1.0dB */ 0x47c0011f,
	/*  +0.5dB */ 0x43c0010f,
	/*   0.0dB */ 0x40000100,
	/*  -0.5dB */ 0x3c8000f2,
	/*  -1.0dB */ 0x390000e4,
	/*  -1.5dB */ 0x35c000d7,
	/*  -2.0dB */ 0x32c000cb,
	/*  -2.5dB */ 0x300000c0,
	/*  -3.0dB */ 0x2d4000b5,
	/*  -3.5dB */ 0x2ac000ab,
	/*  -4.0dB */ 0x288000a2,
	/*  -4.5dB */ 0x26000098,
	/*  -5.0dB */ 0x24000090,
	/*  -5.5dB */ 0x22000088,
	/*  -6.0dB */ 0x20000080,
	/*  -6.5dB */ 0x1a00006c,
	/*  -7.0dB */ 0x1c800072,
	/*  -7.5dB */ 0x18000060,
	/*  -8.0dB */ 0x19800066,
	/*  -8.5dB */ 0x15800056,
	/*  -9.0dB */ 0x26c0005b,
	/*  -9.5dB */ 0x14400051,
	/* -10.0dB */ 0x24400051,
	/* -10.5dB */ 0x1300004c,
	/* -11.0dB */ 0x12000048,
	/* -11.5dB */ 0x11000044,
	/* -12.0dB */ 0x10000040
};
#endif

//============================================================
// Local Function predefine.
//============================================================

//START------------COMMON INFO RELATED---------------//
VOID
odm_CommonInfoSelfInit(
	IN		PDM_ODM_T		pDM_Odm
	);

VOID
odm_CommonInfoSelfUpdate(
	IN		PDM_ODM_T		pDM_Odm
	);

VOID
odm_CmnInfoInit_Debug(
	IN		PDM_ODM_T		pDM_Odm
	);

VOID
odm_CmnInfoHook_Debug(
	IN		PDM_ODM_T		pDM_Odm
	);

VOID
odm_CmnInfoUpdate_Debug(
	IN		PDM_ODM_T		pDM_Odm
	);
VOID
odm_BasicDbgMessage
(
	IN		PDM_ODM_T		pDM_Odm
	);

//END------------COMMON INFO RELATED---------------//

//START---------------DIG---------------------------//

//Remove by Yuchen

//END---------------DIG---------------------------//

//START-------BB POWER SAVE-----------------------//
//Remove BB power Saving by YuChen
//END---------BB POWER SAVE-----------------------//

//START-----------------PSD-----------------------//
#if(DM_ODM_SUPPORT_TYPE & (ODM_WIN)) 
//============================================================
// Function predefine.
//============================================================

//Remove PathDiversity related funtion predefine to odm_PathDiv.h

//Start-------------------- RX High Power------------------------//
VOID	odm_RXHPInit(	IN		PDM_ODM_T		pDM_Odm);
VOID	odm_RXHP(	IN		PDM_ODM_T		pDM_Odm);
VOID	odm_Write_RXHP(	IN	PDM_ODM_T	pDM_Odm);

VOID	odm_PSD_RXHP(		IN	PDM_ODM_T	pDM_Odm);
VOID	odm_PSD_RXHPCallback(	PRT_TIMER		pTimer);
VOID	odm_PSD_RXHPWorkitemCallback(	IN PVOID            pContext);
//End--------------------- RX High Power -----------------------//

VOID	odm_PathDivInit_92D(	IN	PDM_ODM_T 	pDM_Odm);

VOID
odm_SetRespPath_92C(
	IN	PADAPTER	Adapter,
	IN	u1Byte	DefaultRespPath 
	);

#endif
//END-------------------PSD-----------------------//

VOID
odm_RefreshRateAdaptiveMaskMP(
	IN		PDM_ODM_T		pDM_Odm
	);

VOID
odm_RefreshRateAdaptiveMaskCE(
	IN		PDM_ODM_T		pDM_Odm
	);

VOID
odm_RefreshRateAdaptiveMaskAPADSL(
	IN		PDM_ODM_T		pDM_Odm
	);

VOID
ODM_DynamicATCSwitch_init(
	IN 		PDM_ODM_T 		pDM_Odm
	);

VOID
ODM_DynamicATCSwitch(
	IN 		PDM_ODM_T 		pDM_Odm
	);

VOID
odm_Write_CrystalCap(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u1Byte			CrystalCap
);

VOID 
odm_DynamicTxPowerInit(
	IN		PDM_ODM_T		pDM_Odm
	);

VOID
odm_DynamicTxPowerRestorePowerIndex(
	IN	PDM_ODM_T	pDM_Odm
	);

VOID 
odm_DynamicTxPowerNIC(
	IN	PDM_ODM_T	pDM_Odm
	);

#if(DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
VOID
odm_DynamicTxPowerSavePowerIndex(
	IN		PDM_ODM_T		pDM_Odm
	);

VOID
odm_DynamicTxPowerWritePowerIndex(
	IN	PDM_ODM_T	pDM_Odm, 
	IN 	u1Byte		Value);

VOID 
odm_DynamicTxPower_92C(
	IN	PDM_ODM_T	pDM_Odm
	);

VOID 
odm_DynamicTxPower_92D(
	IN	PDM_ODM_T	pDM_Odm
	);
//remove dig by Yuchen

#endif


VOID
odm_RSSIMonitorInit(
	IN	PDM_ODM_T	pDM_Odm
	);

VOID
odm_RSSIMonitorCheckMP(
	IN	PDM_ODM_T	pDM_Odm
	);

VOID 
odm_RSSIMonitorCheckCE(
	IN		PDM_ODM_T		pDM_Odm
	);
VOID 
odm_RSSIMonitorCheckAP(
	IN		PDM_ODM_T		pDM_Odm
	);



VOID
odm_RSSIMonitorCheck(
	IN		PDM_ODM_T		pDM_Odm
	);
VOID 
odm_DynamicTxPower(
	IN		PDM_ODM_T		pDM_Odm
	);

VOID 
odm_DynamicTxPowerAP(
	IN		PDM_ODM_T		pDM_Odm
	);


VOID
odm_SwAntDivInit(
	IN		PDM_ODM_T		pDM_Odm
	);

VOID
odm_SwAntDivInit_NIC(
	IN		PDM_ODM_T		pDM_Odm
	);

VOID
odm_SwAntDetectInit(
	IN 		PDM_ODM_T 		pDM_Odm
	);

VOID
odm_SwAntDivChkAntSwitch(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u1Byte			Step
	);

VOID
odm_SwAntDivChkAntSwitchNIC(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u1Byte		Step
	);


#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID
odm_SwAntDivChkAntSwitchCallback(
	PRT_TIMER		pTimer
);
VOID
odm_SwAntDivChkAntSwitchWorkitemCallback(
    IN PVOID            pContext
    );
VOID
ODM_UpdateInitRateWorkItemCallback(
    IN PVOID            pContext
    );
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
VOID odm_SwAntDivChkAntSwitchCallback(void *FunctionContext);
#elif (DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
VOID odm_SwAntDivChkAntSwitchCallback(void *FunctionContext);
#endif



VOID
odm_GlobalAdapterCheck(
	IN		VOID
	);

VOID
odm_RefreshBasicRateMask(
	IN		PDM_ODM_T		pDM_Odm	
	);

VOID
odm_RefreshRateAdaptiveMask(
	IN		PDM_ODM_T		pDM_Odm
	);

VOID
ODM_TXPowerTrackingCheck(
	IN		PDM_ODM_T		pDM_Odm
	);

VOID
odm_TXPowerTrackingCheckAP(
	IN		PDM_ODM_T		pDM_Odm
	);

VOID
odm_RateAdaptiveMaskInit(
	IN	PDM_ODM_T	pDM_Odm
	);

VOID
odm_TXPowerTrackingThermalMeterInit(
	IN	PDM_ODM_T	pDM_Odm
	);


VOID
odm_IQCalibrate(
		IN	PDM_ODM_T	pDM_Odm 
		);

VOID
odm_TXPowerTrackingInit(
	IN	PDM_ODM_T	pDM_Odm
	);

VOID
odm_TXPowerTrackingCheckMP(
	IN	PDM_ODM_T	pDM_Odm
	);


VOID
odm_TXPowerTrackingCheckCE(
	IN	PDM_ODM_T	pDM_Odm
	);

#if(DM_ODM_SUPPORT_TYPE & (ODM_WIN)) 

VOID
ODM_RateAdaptiveStateApInit(
	IN	PADAPTER	Adapter	,
	IN	PRT_WLAN_STA  pEntry
	);

VOID 
odm_TXPowerTrackingCallbackThermalMeter92C(
            IN PADAPTER	Adapter
            );

VOID
odm_TXPowerTrackingCallbackRXGainThermalMeter92D(
	IN PADAPTER 	Adapter
	);

VOID
odm_TXPowerTrackingCallbackThermalMeter92D(
            IN PADAPTER	Adapter
            );

VOID
odm_TXPowerTrackingDirectCall92C(
            IN	PADAPTER		Adapter
            );

VOID
odm_TXPowerTrackingThermalMeterCheck(
	IN	PADAPTER		Adapter
	);

#endif

//Remove Edca by Yu Chen


#define 	RxDefaultAnt1		0x65a9
#define	RxDefaultAnt2		0x569a

VOID
odm_InitHybridAntDiv(
	IN PDM_ODM_T	pDM_Odm 
	);

BOOLEAN
odm_StaDefAntSel(
	IN PDM_ODM_T	pDM_Odm,
	IN u4Byte		OFDM_Ant1_Cnt,
	IN u4Byte		OFDM_Ant2_Cnt,
	IN u4Byte		CCK_Ant1_Cnt,
	IN u4Byte		CCK_Ant2_Cnt,
	OUT u1Byte		*pDefAnt 
	);

VOID
odm_SetRxIdleAnt(
	IN	PDM_ODM_T	pDM_Odm,
	IN	u1Byte	Ant,
	IN   BOOLEAN   bDualPath                     
);



VOID
odm_HwAntDiv(
	IN	PDM_ODM_T	pDM_Odm
);


//============================================================
//3 Export Interface
//============================================================

//
// 2011/09/21 MH Add to describe different team necessary resource allocate??
//
VOID
ODM_DMInit(
	IN		PDM_ODM_T		pDM_Odm
	)
{

	//2012.05.03 Luke: For all IC series
	odm_CommonInfoSelfInit(pDM_Odm);
	odm_CmnInfoInit_Debug(pDM_Odm);
	odm_DIGInit(pDM_Odm);	
	odm_AdaptivityInit(pDM_Odm);
	odm_RateAdaptiveMaskInit(pDM_Odm);
	odm_RSSIMonitorInit(pDM_Odm);
	
#if (RTL8192E_SUPPORT == 1)
	if(pDM_Odm->SupportICType==ODM_RTL8192E)
	{
		odm_PrimaryCCA_Check_Init(pDM_Odm);
	}
#endif

//#if (MP_DRIVER != 1)
	if ( *(pDM_Odm->mp_mode) != 1)
	    odm_PathDiversityInit(pDM_Odm);
//#endif
	ODM_EdcaTurboInit(pDM_Odm);

	if(pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
	{
		odm_TXPowerTrackingInit(pDM_Odm);
//#if (MP_DRIVER != 1)
		if ( *(pDM_Odm->mp_mode) != 1)
			ODM_AntDivInit(pDM_Odm);
//#endif
	}
	else if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
	{
		odm_DynamicBBPowerSavingInit(pDM_Odm);
		odm_DynamicTxPowerInit(pDM_Odm);
		odm_TXPowerTrackingInit(pDM_Odm);
		//ODM_EdcaTurboInit(pDM_Odm);
//#if (MP_DRIVER != 1)
		if ( *(pDM_Odm->mp_mode) != 1) {
		if(pDM_Odm->SupportICType==ODM_RTL8723A)
			odm_SwAntDivInit(pDM_Odm);	
		else if(pDM_Odm->SupportICType & (ODM_RTL8192C|ODM_RTL8192D))
		{
			if(pDM_Odm->AntDivType == HW_ANTDIV)
			odm_InitHybridAntDiv(pDM_Odm);
			else
			odm_SwAntDivInit(pDM_Odm);
		}
		else
			ODM_AntDivInit(pDM_Odm);
	
		if(pDM_Odm->SupportICType == ODM_RTL8723B)
			odm_SwAntDetectInit(pDM_Odm);
		}
//#endif

//2010.05.30 LukeLee: For CE platform, files in IC subfolders may not be included to be compiled,
// so compile flags must be left here to prevent from compile errors
#if (RTL8188E_SUPPORT == 1)
		if(pDM_Odm->SupportICType==ODM_RTL8188E)
		{
			odm_PrimaryCCA_Init(pDM_Odm);    // Gary
			ODM_RAInfo_Init_all(pDM_Odm);
		}	
#endif		

//2010.05.30 LukeLee: Following are not incorporated into ODM structure yet.
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		if(pDM_Odm->SupportICType&ODM_RTL8723A)
			odm_PSDMonitorInit(pDM_Odm);
		
		if(!(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8188E)))
		{
			odm_RXHPInit(pDM_Odm);	
		}
		if(pDM_Odm->SupportICType==ODM_RTL8192D)
		{
			odm_PathDivInit_92D(pDM_Odm); //92D Path Div Init   //Neil Chen
		}	
#endif
	}

	ODM_DynamicATCSwitch_init(pDM_Odm);
	ODM_ClearTxPowerTrackingState(pDM_Odm);

}

//
// 2011/09/20 MH This is the entry pointer for all team to execute HW out source DM.
// You can not add any dummy function here, be care, you can only use DM structure
// to perform any new ODM_DM.
//
VOID
ODM_DMWatchdog(
	IN		PDM_ODM_T		pDM_Odm
	)
{	
	if((pDM_Odm->SupportICType == ODM_RTL8821) && (pDM_Odm->SupportInterface == ODM_ITRF_USB))
	{
		if(pDM_Odm->RSSI_Min > 25)
			ODM_Write1Byte(pDM_Odm, 0x4CF, 0x02);
		else if(pDM_Odm->RSSI_Min < 20)
			ODM_Write1Byte(pDM_Odm, 0x4CF, 0x00);
	}


	odm_CommonInfoSelfUpdate(pDM_Odm);
	odm_BasicDbgMessage(pDM_Odm);
	odm_FalseAlarmCounterStatistics(pDM_Odm);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): RSSI=0x%x\n",pDM_Odm->RSSI_Min));

	odm_RSSIMonitorCheck(pDM_Odm);

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
//#ifdef CONFIG_PLATFORM_SPRD
	//For CE Platform(SPRD or Tablet)
	//8723A or 8189ES platform
	//NeilChen--2012--08--24--
	//Fix Leave LPS issue
	if( 	(adapter_to_pwrctl(pDM_Odm->Adapter)->pwr_mode != PS_MODE_ACTIVE) // in LPS mode
		//&&( 			
		//	(pDM_Odm->SupportICType & (ODM_RTL8723A ) )||
		//   	(pDM_Odm->SupportICType & (ODM_RTL8188E) &&((pDM_Odm->SupportInterface  == ODM_ITRF_SDIO)) ) 
	  	//)	
	)
	{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("----Step1: odm_DIG is in LPS mode\n"));				
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("---Step2: 8723AS is in LPS mode\n"));
			odm_DIGbyRSSI_LPS(pDM_Odm);
	}		
	else				
//#endif
#endif
	{
		odm_DIG(pDM_Odm);
	}

	{
		pDIG_T	pDM_DigTable = &pDM_Odm->DM_DigTable;
		odm_Adaptivity(pDM_Odm, pDM_DigTable->CurIGValue);
	}
	odm_CCKPacketDetectionThresh(pDM_Odm);

	if(*(pDM_Odm->pbPowerSaving)==TRUE)
		return;

	
	odm_RefreshRateAdaptiveMask(pDM_Odm);
	odm_RefreshBasicRateMask(pDM_Odm);
	odm_DynamicBBPowerSaving(pDM_Odm);	
	odm_EdcaTurboCheck(pDM_Odm);
	odm_PathDiversity(pDM_Odm);
	ODM_DynamicATCSwitch(pDM_Odm);
	odm_DynamicTxPower(pDM_Odm);	

#if (RTL8192E_SUPPORT == 1)
        if(pDM_Odm->SupportICType==ODM_RTL8192E)
                odm_DynamicPrimaryCCA_Check(pDM_Odm); 
#endif
	 //if(pDM_Odm->SupportICType == ODM_RTL8192E)
	 //        return;

	
//#if (MP_DRIVER != 1)		
if ( *(pDM_Odm->mp_mode) != 1) {
	if(pDM_Odm->SupportICType==ODM_RTL8723A)
	{
		odm_SwAntDivChkAntSwitch(pDM_Odm, SWAW_STEP_PEAK);
	}
	else if(pDM_Odm->SupportICType & (ODM_RTL8192C|ODM_RTL8192D))
	{
		if(pDM_Odm->AntDivType == HW_ANTDIV)
			odm_HwAntDiv(pDM_Odm);
		else
			odm_SwAntDivChkAntSwitch(pDM_Odm, SWAW_STEP_PEAK);
	}
	else
		ODM_AntDiv(pDM_Odm);
}
//#endif

	if(pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
	{
		ODM_TXPowerTrackingCheck(pDM_Odm);

		odm_IQCalibrate(pDM_Odm);
	}
	else if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
	{
		ODM_TXPowerTrackingCheck(pDM_Odm);

		//odm_EdcaTurboCheck(pDM_Odm);

		#if( DM_ODM_SUPPORT_TYPE & (ODM_WIN))	
		if(!(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8188E)))
		        odm_RXHP(pDM_Odm);	
		#endif

	//2010.05.30 LukeLee: For CE platform, files in IC subfolders may not be included to be compiled,
	// so compile flags must be left here to prevent from compile errors
#if (RTL8192D_SUPPORT == 1)
	        if(pDM_Odm->SupportICType==ODM_RTL8192D)
	                ODM_DynamicEarlyMode(pDM_Odm);
#endif
	        odm_DynamicBBPowerSaving(pDM_Odm);
#if (RTL8188E_SUPPORT == 1)
	        if(pDM_Odm->SupportICType==ODM_RTL8188E)
	                odm_DynamicPrimaryCCA(pDM_Odm);	
#endif

	}
	pDM_Odm->PhyDbgInfo.NumQryBeaconPkt = 0;

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	odm_dtc(pDM_Odm);
#endif
}


//
// Init /.. Fixed HW value. Only init time.
//
VOID
ODM_CmnInfoInit(
	IN		PDM_ODM_T		pDM_Odm,
	IN		ODM_CMNINFO_E	CmnInfo,
	IN		u4Byte			Value	
	)
{
	//
	// This section is used for init value
	//
	switch	(CmnInfo)
	{
		//
		// Fixed ODM value.
		//
		case	ODM_CMNINFO_ABILITY:
			pDM_Odm->SupportAbility = (u4Byte)Value;
			break;

		case	ODM_CMNINFO_RF_TYPE:
			pDM_Odm->RFType = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_PLATFORM:
			pDM_Odm->SupportPlatform = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_INTERFACE:
			pDM_Odm->SupportInterface = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_MP_TEST_CHIP:
			pDM_Odm->bIsMPChip= (u1Byte)Value;
			break;
            
		case	ODM_CMNINFO_IC_TYPE:
			pDM_Odm->SupportICType = Value;
			break;

		case	ODM_CMNINFO_CUT_VER:
			pDM_Odm->CutVersion = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_FAB_VER:
			pDM_Odm->FabVersion = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_RFE_TYPE:
			pDM_Odm->RFEType = (u1Byte)Value;
			break;

		case    ODM_CMNINFO_RF_ANTENNA_TYPE:
			pDM_Odm->AntDivType= (u1Byte)Value;
			break;

		case	ODM_CMNINFO_BOARD_TYPE:
			pDM_Odm->BoardType = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_PACKAGE_TYPE:
			pDM_Odm->PackageType = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_EXT_LNA:
			pDM_Odm->ExtLNA = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_5G_EXT_LNA:
			pDM_Odm->ExtLNA5G = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_EXT_PA:
			pDM_Odm->ExtPA = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_5G_EXT_PA:
			pDM_Odm->ExtPA5G = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_GPA:
			pDM_Odm->TypeGPA= (ODM_TYPE_GPA_E)Value;
			break;
		case	ODM_CMNINFO_APA:
			pDM_Odm->TypeAPA= (ODM_TYPE_APA_E)Value;
			break;
		case	ODM_CMNINFO_GLNA:
			pDM_Odm->TypeGLNA= (ODM_TYPE_GLNA_E)Value;
			break;
		case	ODM_CMNINFO_ALNA:
			pDM_Odm->TypeALNA= (ODM_TYPE_ALNA_E)Value;
			break;

		case	ODM_CMNINFO_EXT_TRSW:
			pDM_Odm->ExtTRSW = (u1Byte)Value;
			break;
		case 	ODM_CMNINFO_PATCH_ID:
			pDM_Odm->PatchID = (u1Byte)Value;
			break;
		case 	ODM_CMNINFO_BINHCT_TEST:
			pDM_Odm->bInHctTest = (BOOLEAN)Value;
			break;
		case 	ODM_CMNINFO_BWIFI_TEST:
			pDM_Odm->bWIFITest = (BOOLEAN)Value;
			break;	

		case	ODM_CMNINFO_SMART_CONCURRENT:
			pDM_Odm->bDualMacSmartConcurrent = (BOOLEAN )Value;
			break;
		
		//To remove the compiler warning, must add an empty default statement to handle the other values.	
		default:
			//do nothing
			break;	
		
	}

}


VOID
ODM_CmnInfoHook(
	IN		PDM_ODM_T		pDM_Odm,
	IN		ODM_CMNINFO_E	CmnInfo,
	IN		PVOID			pValue	
	)
{
	//
	// Hook call by reference pointer.
	//
	switch	(CmnInfo)
	{
		//
		// Dynamic call by reference pointer.
		//
		case	ODM_CMNINFO_MAC_PHY_MODE:
			pDM_Odm->pMacPhyMode = (u1Byte *)pValue;
			break;
		
		case	ODM_CMNINFO_TX_UNI:
			pDM_Odm->pNumTxBytesUnicast = (u8Byte *)pValue;
			break;

		case	ODM_CMNINFO_RX_UNI:
			pDM_Odm->pNumRxBytesUnicast = (u8Byte *)pValue;
			break;

		case	ODM_CMNINFO_WM_MODE:
			pDM_Odm->pWirelessMode = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_BAND:
			pDM_Odm->pBandType = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_SEC_CHNL_OFFSET:
			pDM_Odm->pSecChOffset = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_SEC_MODE:
			pDM_Odm->pSecurity = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_BW:
			pDM_Odm->pBandWidth = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_CHNL:
			pDM_Odm->pChannel = (u1Byte *)pValue;
			break;
		
		case	ODM_CMNINFO_DMSP_GET_VALUE:
			pDM_Odm->pbGetValueFromOtherMac = (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_BUDDY_ADAPTOR:
			pDM_Odm->pBuddyAdapter = (PADAPTER *)pValue;
			break;

		case	ODM_CMNINFO_DMSP_IS_MASTER:
			pDM_Odm->pbMasterOfDMSP = (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_SCAN:
			pDM_Odm->pbScanInProcess = (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_POWER_SAVING:
			pDM_Odm->pbPowerSaving = (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_ONE_PATH_CCA:
			pDM_Odm->pOnePathCCA = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_DRV_STOP:
			pDM_Odm->pbDriverStopped =  (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_PNP_IN:
			pDM_Odm->pbDriverIsGoingToPnpSetPowerSleep =  (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_INIT_ON:
			pDM_Odm->pinit_adpt_in_progress =  (BOOLEAN *)pValue;
			break;

		case	ODM_CMNINFO_ANT_TEST:
			pDM_Odm->pAntennaTest =  (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_NET_CLOSED:
			pDM_Odm->pbNet_closed = (BOOLEAN *)pValue;
			break;

		case 	ODM_CMNINFO_FORCED_RATE:
			pDM_Odm->pForcedDataRate = (pu2Byte)pValue;
			break;

		case  ODM_CMNINFO_FORCED_IGI_LB:
			pDM_Odm->pu1ForcedIgiLb = (u1Byte *)pValue;
			break;

		case	ODM_CMNINFO_MP_MODE:
			pDM_Odm->mp_mode = (u1Byte *)pValue;
			break;

		//case	ODM_CMNINFO_RTSTA_AID:
		//	pDM_Odm->pAidMap =  (u1Byte *)pValue;
		//	break;

		//case	ODM_CMNINFO_BT_COEXIST:
		//	pDM_Odm->BTCoexist = (BOOLEAN *)pValue;		

		//case	ODM_CMNINFO_STA_STATUS:
			//pDM_Odm->pODM_StaInfo[] = (PSTA_INFO_T)pValue;
			//break;

		//case	ODM_CMNINFO_PHY_STATUS:
		//	pDM_Odm->pPhyInfo = (ODM_PHY_INFO *)pValue;
		//	break;

		//case	ODM_CMNINFO_MAC_STATUS:
		//	pDM_Odm->pMacInfo = (ODM_MAC_INFO *)pValue;
		//	break;
		//To remove the compiler warning, must add an empty default statement to handle the other values.				
		default:
			//do nothing
			break;

	}

}


VOID
ODM_CmnInfoPtrArrayHook(
	IN		PDM_ODM_T		pDM_Odm,
	IN		ODM_CMNINFO_E	CmnInfo,
	IN		u2Byte			Index,
	IN		PVOID			pValue	
	)
{
	//
	// Hook call by reference pointer.
	//
	switch	(CmnInfo)
	{
		//
		// Dynamic call by reference pointer.
		//		
		case	ODM_CMNINFO_STA_STATUS:
			pDM_Odm->pODM_StaInfo[Index] = (PSTA_INFO_T)pValue;
			break;		
		//To remove the compiler warning, must add an empty default statement to handle the other values.				
		default:
			//do nothing
			break;
	}
	
}


//
// Update Band/CHannel/.. The values are dynamic but non-per-packet.
//
VOID
ODM_CmnInfoUpdate(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u4Byte			CmnInfo,
	IN		u8Byte			Value	
	)
{
	//
	// This init variable may be changed in run time.
	//
	switch	(CmnInfo)
	{
		case ODM_CMNINFO_LINK_IN_PROGRESS:
			pDM_Odm->bLinkInProcess = (BOOLEAN)Value;
			break;
		
		case	ODM_CMNINFO_ABILITY:
			pDM_Odm->SupportAbility = (u4Byte)Value;
			break;

		case	ODM_CMNINFO_RF_TYPE:
			pDM_Odm->RFType = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_WIFI_DIRECT:
			pDM_Odm->bWIFI_Direct = (BOOLEAN)Value;
			break;

		case	ODM_CMNINFO_WIFI_DISPLAY:
			pDM_Odm->bWIFI_Display = (BOOLEAN)Value;
			break;

		case	ODM_CMNINFO_LINK:
			pDM_Odm->bLinked = (BOOLEAN)Value;
			break;
			
		case	ODM_CMNINFO_STATION_STATE:
			pDM_Odm->bsta_state = (BOOLEAN)Value;
			break;
			
		case	ODM_CMNINFO_RSSI_MIN:
			pDM_Odm->RSSI_Min= (u1Byte)Value;
			break;

		case	ODM_CMNINFO_DBG_COMP:
			pDM_Odm->DebugComponents = Value;
			break;

		case	ODM_CMNINFO_DBG_LEVEL:
			pDM_Odm->DebugLevel = (u4Byte)Value;
			break;
		case	ODM_CMNINFO_RA_THRESHOLD_HIGH:
			pDM_Odm->RateAdaptive.HighRSSIThresh = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_RA_THRESHOLD_LOW:
			pDM_Odm->RateAdaptive.LowRSSIThresh = (u1Byte)Value;
			break;
		// The following is for BT HS mode and BT coexist mechanism.
		case ODM_CMNINFO_BT_DISABLED:
			pDM_Odm->bBtDisabled = (BOOLEAN)Value;
			break;
			
		case ODM_CMNINFO_BT_HS_CONNECT_PROCESS:
			pDM_Odm->bBtConnectProcess = (BOOLEAN)Value;
			break;
		
		case ODM_CMNINFO_BT_HS_RSSI:
			pDM_Odm->btHsRssi = (u1Byte)Value;
			break;
			
		case	ODM_CMNINFO_BT_OPERATION:
			pDM_Odm->bBtHsOperation = (BOOLEAN)Value;
			break;

		case	ODM_CMNINFO_BT_LIMITED_DIG:
			pDM_Odm->bBtLimitedDig = (BOOLEAN)Value;
			break;	

		case	ODM_CMNINFO_BT_DISABLE_EDCA:
			pDM_Odm->bBtDisableEdcaTurbo = (BOOLEAN)Value;
			break;
			
/*
		case	ODM_CMNINFO_OP_MODE:
			pDM_Odm->OPMode = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_WM_MODE:
			pDM_Odm->WirelessMode = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_BAND:
			pDM_Odm->BandType = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_SEC_CHNL_OFFSET:
			pDM_Odm->SecChOffset = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_SEC_MODE:
			pDM_Odm->Security = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_BW:
			pDM_Odm->BandWidth = (u1Byte)Value;
			break;

		case	ODM_CMNINFO_CHNL:
			pDM_Odm->Channel = (u1Byte)Value;
			break;			
*/	
                default:
			//do nothing
			break;
	}

	
}

VOID
odm_CommonInfoSelfInit(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	pFAT_T			pDM_FatTable = &pDM_Odm->DM_FatTable;
	pDM_Odm->bCckHighPower = (BOOLEAN) ODM_GetBBReg(pDM_Odm, ODM_REG(CCK_RPT_FORMAT,pDM_Odm), ODM_BIT(CCK_RPT_FORMAT,pDM_Odm));		
	pDM_Odm->RFPathRxEnable = (u1Byte) ODM_GetBBReg(pDM_Odm, ODM_REG(BB_RX_PATH,pDM_Odm), ODM_BIT(BB_RX_PATH,pDM_Odm));
#if (DM_ODM_SUPPORT_TYPE != ODM_CE)	
	pDM_Odm->pbNet_closed = &pDM_Odm->BOOLEAN_temp;
#endif

	ODM_InitDebugSetting(pDM_Odm);

	if(pDM_Odm->SupportICType==ODM_RTL8723A)
	{
		pDM_Odm->AntDivType = SW_ANTDIV;
	}
	else if(pDM_Odm->SupportICType & (ODM_RTL8192C|ODM_RTL8192D))
	{
           #if(defined(CONFIG_HW_ANTENNA_DIVERSITY))	
		pDM_Odm->AntDivType = HW_ANTDIV;
           #elif (defined(CONFIG_SW_ANTENNA_DIVERSITY))
		pDM_Odm->AntDivType = SW_ANTDIV;
           #endif
	}
	pDM_Odm->TxRate = 0xFF;
#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))

	if(pDM_Odm->SupportICType==ODM_RTL8723B)
	{
		if((!pDM_Odm->DM_SWAT_Table.ANTA_ON || !pDM_Odm->DM_SWAT_Table.ANTB_ON))
			pDM_Odm->SupportAbility &= ~(ODM_BB_ANT_DIV);
	}

#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)

	#if(defined(CONFIG_NOT_SUPPORT_ANTDIV)) 
		pDM_Odm->SupportAbility &= ~(ODM_BB_ANT_DIV);
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Disable AntDiv function] : Not Support 2.4G & 5G Antenna Diversity\n"));
	#elif(defined(CONFIG_2G5G_SUPPORT_ANTDIV)) 
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Enable AntDiv function] : 2.4G & 5G Support Antenna Diversity Simultaneously \n"));
		pDM_FatTable->AntDiv_2G_5G = (ODM_ANTDIV_2G|ODM_ANTDIV_5G);
		if(pDM_Odm->SupportICType & ODM_ANTDIV_SUPPORT)
			pDM_Odm->SupportAbility |= ODM_BB_ANT_DIV;
		if(*pDM_Odm->pBandType == ODM_BAND_5G )
		{
			#if ( defined(CONFIG_5G_CGCS_RX_DIVERSITY) )
				pDM_Odm->AntDivType = CGCS_RX_HW_ANTDIV; 
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 5G] : AntDiv Type = CGCS_RX_HW_ANTDIV\n"));
			#elif( defined(CONFIG_5G_CG_TRX_DIVERSITY) )
				pDM_Odm->AntDivType = CG_TRX_HW_ANTDIV;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 5G] : AntDiv Type = CG_TRX_HW_ANTDIV\n"));
			#elif( defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY) )
				pDM_Odm->AntDivType = CG_TRX_SMART_ANTDIV;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 5G] : AntDiv Type = CG_SMART_ANTDIV\n"));
			#endif
		}		
		else 	if(*pDM_Odm->pBandType == ODM_BAND_2_4G )
	        {
			#if ( defined(CONFIG_2G_CGCS_RX_DIVERSITY) )
				pDM_Odm->AntDivType = CGCS_RX_HW_ANTDIV;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 2.4G] : AntDiv Type = CGCS_RX_HW_ANTDIV\n"));
			#elif( defined(CONFIG_2G_CG_TRX_DIVERSITY) )
				pDM_Odm->AntDivType = CG_TRX_HW_ANTDIV;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 2.4G] : AntDiv Type = CG_TRX_HW_ANTDIV\n"));
			#elif( defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY) )
				pDM_Odm->AntDivType = CG_TRX_SMART_ANTDIV;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 2.4G] : AntDiv Type = CG_SMART_ANTDIV\n"));
			#endif
		}
        #elif(defined(CONFIG_5G_SUPPORT_ANTDIV))
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Enable AntDiv function] : Only 5G Support Antenna Diversity\n"));
		pDM_FatTable->AntDiv_2G_5G = (ODM_ANTDIV_5G);
		if(*pDM_Odm->pBandType == ODM_BAND_5G )
		{
			if(pDM_Odm->SupportICType & ODM_ANTDIV_5G_SUPPORT_IC)
				pDM_Odm->SupportAbility |= ODM_BB_ANT_DIV;	
			#if ( defined(CONFIG_5G_CGCS_RX_DIVERSITY) )
				pDM_Odm->AntDivType = CGCS_RX_HW_ANTDIV;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 5G] : AntDiv Type = CGCS_RX_HW_ANTDIV\n"));
			#elif( defined(CONFIG_5G_CG_TRX_DIVERSITY) )
				pDM_Odm->AntDivType = CG_TRX_HW_ANTDIV;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 5G] : AntDiv Type = CG_TRX_HW_ANTDIV\n"));
			#elif( defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY) )
				pDM_Odm->AntDivType = CG_TRX_SMART_ANTDIV;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 5G] : AntDiv Type = CG_SMART_ANTDIV\n"));
			#endif
	        }
		else if(*pDM_Odm->pBandType == ODM_BAND_2_4G )
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("Not Support 2G AntDivType\n"));
			pDM_Odm->SupportAbility &= ~(ODM_BB_ANT_DIV);
		}
	#elif(defined(CONFIG_2G_SUPPORT_ANTDIV)) 
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Enable AntDiv function] : Only 2.4G Support Antenna Diversity\n"));
		pDM_FatTable->AntDiv_2G_5G = (ODM_ANTDIV_2G);
		if(*pDM_Odm->pBandType == ODM_BAND_2_4G )
		{
			if(pDM_Odm->SupportICType & ODM_ANTDIV_2G_SUPPORT_IC)
				pDM_Odm->SupportAbility |= ODM_BB_ANT_DIV;
			#if ( defined(CONFIG_2G_CGCS_RX_DIVERSITY) )
				pDM_Odm->AntDivType = CGCS_RX_HW_ANTDIV;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 2.4G] : AntDiv Type = CGCS_RX_HW_ANTDIV\n"));
			#elif( defined(CONFIG_2G_CG_TRX_DIVERSITY) )
				pDM_Odm->AntDivType = CG_TRX_HW_ANTDIV;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 2.4G] : AntDiv Type = CG_TRX_HW_ANTDIV\n"));
			#elif( defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY) )
				pDM_Odm->AntDivType = CG_TRX_SMART_ANTDIV;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ 2.4G] : AntDiv Type = CG_SMART_ANTDIV\n"));
                        #endif
	        }
		else if(*pDM_Odm->pBandType == ODM_BAND_5G )
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("Not Support 5G AntDivType\n"));
			pDM_Odm->SupportAbility &= ~(ODM_BB_ANT_DIV);
		}
	#endif
#endif //#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
#endif //#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))

}

VOID
odm_CommonInfoSelfUpdate(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	u1Byte	EntryCnt=0;
	u1Byte	i;
	PSTA_INFO_T   	pEntry;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

	PADAPTER	Adapter =  pDM_Odm->Adapter;
	PMGNT_INFO	pMgntInfo = &Adapter->MgntInfo;

	pEntry = pDM_Odm->pODM_StaInfo[0];
	if(pMgntInfo->mAssoc)
	{
		pEntry->bUsed=TRUE;
		for (i=0; i<6; i++)
			pEntry->MacAddr[i] = pMgntInfo->Bssid[i];
	}
	else
	{
		pEntry->bUsed=FALSE;
		for (i=0; i<6; i++)
			pEntry->MacAddr[i] = 0;
	}
#endif


	if(*(pDM_Odm->pBandWidth) == ODM_BW40M)
	{
		if(*(pDM_Odm->pSecChOffset) == 1)
			pDM_Odm->ControlChannel = *(pDM_Odm->pChannel) -2;
		else if(*(pDM_Odm->pSecChOffset) == 2)
			pDM_Odm->ControlChannel = *(pDM_Odm->pChannel) +2;
	}
	else
		pDM_Odm->ControlChannel = *(pDM_Odm->pChannel);

	for (i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
	{
		pEntry = pDM_Odm->pODM_StaInfo[i];
		if(IS_STA_VALID(pEntry))
			EntryCnt++;
	}
	if(EntryCnt == 1)
		pDM_Odm->bOneEntryOnly = TRUE;
	else
		pDM_Odm->bOneEntryOnly = FALSE;
}

VOID
odm_CmnInfoInit_Debug(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("odm_CmnInfoInit_Debug==>\n"));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("SupportPlatform=%d\n",pDM_Odm->SupportPlatform) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("SupportAbility=0x%x\n",pDM_Odm->SupportAbility) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("SupportInterface=%d\n",pDM_Odm->SupportInterface) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("SupportICType=0x%x\n",pDM_Odm->SupportICType) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("CutVersion=%d\n",pDM_Odm->CutVersion) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("FabVersion=%d\n",pDM_Odm->FabVersion) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("RFType=%d\n",pDM_Odm->RFType) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("BoardType=%d\n",pDM_Odm->BoardType) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("ExtLNA=%d\n",pDM_Odm->ExtLNA) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("ExtPA=%d\n",pDM_Odm->ExtPA) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("ExtTRSW=%d\n",pDM_Odm->ExtTRSW) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("PatchID=%d\n",pDM_Odm->PatchID) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("bInHctTest=%d\n",pDM_Odm->bInHctTest) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("bWIFITest=%d\n",pDM_Odm->bWIFITest) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("bDualMacSmartConcurrent=%d\n",pDM_Odm->bDualMacSmartConcurrent) );

}

VOID
odm_CmnInfoHook_Debug(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("odm_CmnInfoHook_Debug==>\n"));	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("pNumTxBytesUnicast=%llu\n",*(pDM_Odm->pNumTxBytesUnicast)) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("pNumRxBytesUnicast=%llu\n",*(pDM_Odm->pNumRxBytesUnicast)) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("pWirelessMode=0x%x\n",*(pDM_Odm->pWirelessMode)) );	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("pSecChOffset=%d\n",*(pDM_Odm->pSecChOffset)) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("pSecurity=%d\n",*(pDM_Odm->pSecurity)) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("pBandWidth=%d\n",*(pDM_Odm->pBandWidth)) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("pChannel=%d\n",*(pDM_Odm->pChannel)) );

	if(pDM_Odm->SupportICType==ODM_RTL8192D)
	{
		if(pDM_Odm->pBandType)
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("pBandType=%d\n",*(pDM_Odm->pBandType)) );
		if(pDM_Odm->pMacPhyMode)
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("pMacPhyMode=%d\n",*(pDM_Odm->pMacPhyMode)) );
		if(pDM_Odm->pBuddyAdapter)
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("pbGetValueFromOtherMac=%d\n",*(pDM_Odm->pbGetValueFromOtherMac)) );
		if(pDM_Odm->pBuddyAdapter)
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("pBuddyAdapter=%p\n",*(pDM_Odm->pBuddyAdapter)) );
		if(pDM_Odm->pbMasterOfDMSP)
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("pbMasterOfDMSP=%d\n",*(pDM_Odm->pbMasterOfDMSP)) );
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("pbScanInProcess=%d\n",*(pDM_Odm->pbScanInProcess)) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("pbPowerSaving=%d\n",*(pDM_Odm->pbPowerSaving)) );

	if(pDM_Odm->SupportPlatform & (ODM_AP|ODM_ADSL))
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("pOnePathCCA=%d\n",*(pDM_Odm->pOnePathCCA)) );
}

VOID
odm_CmnInfoUpdate_Debug(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("odm_CmnInfoUpdate_Debug==>\n"));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("bWIFI_Direct=%d\n",pDM_Odm->bWIFI_Direct) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("bWIFI_Display=%d\n",pDM_Odm->bWIFI_Display) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("bLinked=%d\n",pDM_Odm->bLinked) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("RSSI_Min=%d\n",pDM_Odm->RSSI_Min) );
}

VOID
odm_BasicDbgMessage
(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	PFALSE_ALARM_STATISTICS FalseAlmCnt = &(pDM_Odm->FalseAlmCnt);
	pDIG_T	pDM_DigTable = &pDM_Odm->DM_DigTable;
	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("odm_BasicDbgMsg==>\n"));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("bLinked = %d, RSSI_Min = %d, CurrentIGI = 0x%x \n",
		pDM_Odm->bLinked, pDM_Odm->RSSI_Min, pDM_DigTable->CurIGValue) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("Cnt_Cck_fail = %d, Cnt_Ofdm_fail = %d, Total False Alarm = %d\n",	
		FalseAlmCnt->Cnt_Cck_fail, FalseAlmCnt->Cnt_Ofdm_fail, FalseAlmCnt->Cnt_all));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("RxRate = 0x%x, RSSI_A = %d, RSSI_B = %d\n", 
		pDM_Odm->RxRate, pDM_Odm->RSSI_A, pDM_Odm->RSSI_B));
	//ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("RSSI_C = %d, RSSI_D = %d\n", pDM_Odm->RSSI_C, pDM_Odm->RSSI_D));

}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID
ODM_InitAllWorkItems(IN PDM_ODM_T	pDM_Odm )
{
#if USE_WORKITEM
	PADAPTER		pAdapter = pDM_Odm->Adapter;

	ODM_InitializeWorkItem(	pDM_Odm, 
							&pDM_Odm->DM_SWAT_Table.SwAntennaSwitchWorkitem_8723B, 
							(RT_WORKITEM_CALL_BACK)ODM_SW_AntDiv_WorkitemCallback,
							(PVOID)pAdapter,
							"AntennaSwitchWorkitem");
	
	ODM_InitializeWorkItem(	pDM_Odm, 
							&pDM_Odm->DM_SWAT_Table.SwAntennaSwitchWorkitem, 
							(RT_WORKITEM_CALL_BACK)odm_SwAntDivChkAntSwitchWorkitemCallback,
							(PVOID)pAdapter,
							"AntennaSwitchWorkitem");
	

	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->PathDivSwitchWorkitem), 
		(RT_WORKITEM_CALL_BACK)odm_PathDivChkAntSwitchWorkitemCallback, 
		(PVOID)pAdapter,
		"SWAS_WorkItem");

	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->CCKPathDiversityWorkitem), 
		(RT_WORKITEM_CALL_BACK)odm_CCKTXPathDiversityWorkItemCallback, 
		(PVOID)pAdapter,
		"CCKTXPathDiversityWorkItem");

	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->MPT_DIGWorkitem), 
		(RT_WORKITEM_CALL_BACK)odm_MPT_DIGWorkItemCallback, 
		(PVOID)pAdapter,
		"MPT_DIGWorkitem");

	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->RaRptWorkitem), 
		(RT_WORKITEM_CALL_BACK)ODM_UpdateInitRateWorkItemCallback, 
		(PVOID)pAdapter,
		"RaRptWorkitem");
	
#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))
#if (RTL8188E_SUPPORT == 1)
	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->FastAntTrainingWorkitem), 
		(RT_WORKITEM_CALL_BACK)odm_FastAntTrainingWorkItemCallback, 
		(PVOID)pAdapter,
		"FastAntTrainingWorkitem");
#endif
#endif
	ODM_InitializeWorkItem(
		pDM_Odm,
		&(pDM_Odm->DM_RXHP_Table.PSDTimeWorkitem), 
		(RT_WORKITEM_CALL_BACK)odm_PSD_RXHPWorkitemCallback, 
		(PVOID)pAdapter,
		"PSDRXHP_WorkItem");  
#endif
}

VOID
ODM_FreeAllWorkItems(IN PDM_ODM_T	pDM_Odm )
{
#if USE_WORKITEM
	ODM_FreeWorkItem(	&(pDM_Odm->DM_SWAT_Table.SwAntennaSwitchWorkitem_8723B));
	
	ODM_FreeWorkItem(	&(pDM_Odm->DM_SWAT_Table.SwAntennaSwitchWorkitem));

	ODM_FreeWorkItem(&(pDM_Odm->PathDivSwitchWorkitem));      

	ODM_FreeWorkItem(&(pDM_Odm->CCKPathDiversityWorkitem));
	
	ODM_FreeWorkItem(&(pDM_Odm->FastAntTrainingWorkitem));

	ODM_FreeWorkItem(&(pDM_Odm->MPT_DIGWorkitem));

	ODM_FreeWorkItem(&(pDM_Odm->RaRptWorkitem));

	ODM_FreeWorkItem((&pDM_Odm->DM_RXHP_Table.PSDTimeWorkitem));
#endif

}
#endif

/*
VOID
odm_FindMinimumRSSI(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	u4Byte	i;
	u1Byte	RSSI_Min = 0xFF;

	for(i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
	{
//		if(pDM_Odm->pODM_StaInfo[i] != NULL)
		if(IS_STA_VALID(pDM_Odm->pODM_StaInfo[i]) )
		{
			if(pDM_Odm->pODM_StaInfo[i]->RSSI_Ave < RSSI_Min)
			{
				RSSI_Min = pDM_Odm->pODM_StaInfo[i]->RSSI_Ave;
			}
		}
	}

	pDM_Odm->RSSI_Min = RSSI_Min;

}

VOID
odm_IsLinked(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	u4Byte i;
	BOOLEAN Linked = FALSE;
	
	for(i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
	{
			if(IS_STA_VALID(pDM_Odm->pODM_StaInfo[i]) )
			{			
				Linked = TRUE;
				break;
			}
		
	}

	pDM_Odm->bLinked = Linked;
}
*/


//3============================================================
//3 DIG
//3============================================================
/*-----------------------------------------------------------------------------
 * Function:	odm_DIGInit()
 *
 * Overview:	Set DIG scheme init value.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *
 *---------------------------------------------------------------------------*/

//Remove DIG by yuchen

VOID
ODM_DynamicATCSwitch_init(
	IN 		PDM_ODM_T 		pDM_Odm
)
{
	PADAPTER		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN )

	pDM_Odm->CrystalCap = pHalData->CrystalCap;
	pDM_Odm->bATCStatus = (u1Byte)ODM_GetBBReg(pDM_Odm, rOFDM1_CFOTracking, BIT11);
	pDM_Odm->CFOThreshold = CFO_Threshold_Xtal;

#endif
}

VOID
ODM_DynamicATCSwitch(
	IN 		PDM_ODM_T 		pDM_Odm
)
{
	PADAPTER		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u1Byte 			CrystalCap,ATC_status_temp = 0;
	u4Byte			packet_count;
	int				CFO_kHz_A,CFO_kHz_B,CFO_ave = 0, Adjust_Xtal = 0;
	int				CFO_ave_diff;

#if (MP_DRIVER == 1)
	if ( *(pDM_Odm->mp_mode) == 1)
		pDM_Odm->bLinked = TRUE;
#endif 

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN )

	if(!(pDM_Odm->SupportAbility & ODM_BB_DYNAMIC_ATC))
		return;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_ATC, ODM_DBG_LOUD, ("=========> ODM_DynamicATCSwitch()\n"));

	//2 No link!
	//
	if(!pDM_Odm->bLinked)
	{	
		//3 
		//3 1.Enable ATC
		if(pDM_Odm->bATCStatus == ATC_Status_Off)
		{
			if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
				ODM_SetBBReg(pDM_Odm, rOFDM1_CFOTracking, BIT11, ATC_Status_On);
			
			if(pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
				ODM_SetBBReg(pDM_Odm, rFc_area_Jaguar, BIT14, ATC_Status_On);
			
			pDM_Odm->bATCStatus = ATC_Status_On;
		}

		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_ATC, ODM_DBG_LOUD, ("ODM_DynamicATCSwitch(): No link!!\n"));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_ATC, ODM_DBG_LOUD, ("ODM_DynamicATCSwitch(): ATCStatus = %d\n", pDM_Odm->bATCStatus));

		//3 2.Disable CFO tracking for BT
		if(!pDM_Odm->bBtDisabled)
		{
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_ATC, ODM_DBG_LOUD, ("ODM_DynamicATCSwitch(): Disable CFO tracking for BT!!\n"));
			return;
		}

		//3 3.Reset Crystal Cap.
		if(pDM_Odm->CrystalCap != pHalData->CrystalCap)
		{
			pDM_Odm->CrystalCap = pHalData->CrystalCap;
			CrystalCap = pDM_Odm->CrystalCap & 0x3f;
			odm_Write_CrystalCap(pDM_Odm,CrystalCap);
		}
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_ATC, ODM_DBG_LOUD, ("ODM_DynamicATCSwitch(): CrystalCap = 0x%x\n", pDM_Odm->CrystalCap));
		
	}
	else
	{

	//2 Initialization
	//
		//3 1. Calculate CFO for path-A & path-B
		CFO_kHz_A =  (int)(pDM_Odm->CFO_tail[0] * 3125)  / 1280;
		CFO_kHz_B =  (int)(pDM_Odm->CFO_tail[1] * 3125)  / 1280;
		packet_count = pDM_Odm->packetCount;
		
		//3 2.No new packet
		if(packet_count == pDM_Odm->packetCount_pre)
		{
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_ATC, ODM_DBG_LOUD, ("ODM_DynamicATCSwitch(): packet counter doesn't change\n"));
			return;
		}
		pDM_Odm->packetCount_pre = packet_count;
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_ATC, ODM_DBG_LOUD, ("ODM_DynamicATCSwitch(): packet counter = %d\n", pDM_Odm->packetCount));
		
		//3 3.Average CFO
		if(pDM_Odm->RFType == ODM_1T1R)
			CFO_ave = CFO_kHz_A;
		else
			CFO_ave = (int)(CFO_kHz_A + CFO_kHz_B) >> 1;

		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_ATC, ODM_DBG_LOUD, ("ODM_DynamicATCSwitch(): CFO_kHz_A = %dkHz, CFO_kHz_B = %dkHz, CFO_ave = %dkHz\n", 
						CFO_kHz_A, CFO_kHz_B, CFO_ave));

		//3 4.Avoid abnormal large CFO
		CFO_ave_diff = (pDM_Odm->CFO_ave_pre >= CFO_ave)?(pDM_Odm->CFO_ave_pre - CFO_ave):(CFO_ave - pDM_Odm->CFO_ave_pre);
		if(CFO_ave_diff > 20 && pDM_Odm->largeCFOHit == 0)
		{
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_ATC, ODM_DBG_LOUD, ("ODM_DynamicATCSwitch(): first large CFO hit\n"));
			pDM_Odm->largeCFOHit = 1;
			return;
		}
		else
			pDM_Odm->largeCFOHit = 0;
		pDM_Odm->CFO_ave_pre = CFO_ave;

	//2 CFO tracking by adjusting Xtal cap.
	//
		if (pDM_Odm->bBtDisabled)
		{
			//3 1.Dynamic Xtal threshold
			if(CFO_ave >= -pDM_Odm->CFOThreshold && CFO_ave <= pDM_Odm->CFOThreshold && pDM_Odm->bIsfreeze == 0)
			{
				if (pDM_Odm->CFOThreshold == CFO_Threshold_Xtal)
				{
					pDM_Odm->CFOThreshold = CFO_Threshold_Xtal + 10;
					pDM_Odm->bIsfreeze = 1;
				}
				else
					pDM_Odm->CFOThreshold = CFO_Threshold_Xtal;
			}
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_ATC, ODM_DBG_LOUD, ("ODM_DynamicATCSwitch(): Dynamic threshold = %d\n", pDM_Odm->CFOThreshold));
			
		//3	 2.Calculate Xtal offset
			if(CFO_ave > pDM_Odm->CFOThreshold && pDM_Odm->CrystalCap < 0x3f)
				Adjust_Xtal =  ((CFO_ave - CFO_Threshold_Xtal) >> 2) + 1;
			else if(CFO_ave < (-pDM_Odm->CFOThreshold) && pDM_Odm->CrystalCap > 0)
				Adjust_Xtal =  ((CFO_ave + CFO_Threshold_Xtal) >> 2) - 1;
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_ATC, ODM_DBG_LOUD, ("ODM_DynamicATCSwitch(): Crystal cap = 0x%x, Crystal cap offset = %d\n", pDM_Odm->CrystalCap, Adjust_Xtal));

			//3 3.Adjudt Crystal Cap.
			if(Adjust_Xtal != 0)
			{
				pDM_Odm->bIsfreeze = 0;
				pDM_Odm->CrystalCap = pDM_Odm->CrystalCap + Adjust_Xtal;

				if(pDM_Odm->CrystalCap > 0x3f)
					pDM_Odm->CrystalCap = 0x3f;
				else if (pDM_Odm->CrystalCap < 0)
					pDM_Odm->CrystalCap = 0;

				CrystalCap = pDM_Odm->CrystalCap & 0x3f;
				odm_Write_CrystalCap(pDM_Odm,CrystalCap);
	
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_ATC, ODM_DBG_LOUD, ("ODM_DynamicATCSwitch(): New crystal cap = 0x%x \n", pDM_Odm->CrystalCap));
			}
		}
		else if(pDM_Odm->CrystalCap != pHalData->CrystalCap)
		{
			//3 Reset Xtal Cap when BT is enable
			pDM_Odm->CrystalCap = pHalData->CrystalCap;
			CrystalCap = pDM_Odm->CrystalCap & 0x3f;
			odm_Write_CrystalCap(pDM_Odm,CrystalCap);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_ATC, ODM_DBG_LOUD, ("ODM_DynamicATCSwitch(): Disable CFO tracking for BT!! (CrystalCap is reset)\n"));
		}
		else
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_ATC, ODM_DBG_LOUD, ("ODM_DynamicATCSwitch(): Disable CFO tracking for BT!! (CrystalCap is unchanged)\n"));
		if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES){
			//2 Dynamic ATC switch
			//
				//3 1.Enable ATC when CFO is larger then 80kHz
				if(CFO_ave < CFO_Threshold_ATC && CFO_ave > -CFO_Threshold_ATC)
				{
					if(pDM_Odm->bATCStatus == ATC_Status_On)
					{
					ODM_SetBBReg(pDM_Odm, rOFDM1_CFOTracking, BIT11, ATC_Status_Off);
					pDM_Odm->bATCStatus = ATC_Status_Off;
					}
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_ATC, ODM_DBG_LOUD, ("ODM_DynamicATCSwitch(): Disable ATC!!\n"));
				}
				else
				{
					if(pDM_Odm->bATCStatus == ATC_Status_Off)
					{
						ODM_SetBBReg(pDM_Odm, rOFDM1_CFOTracking, BIT11, ATC_Status_On);
						pDM_Odm->bATCStatus = ATC_Status_On;
					}
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_DYNAMIC_ATC, ODM_DBG_LOUD, ("ODM_DynamicATCSwitch(): Enable ATC!!\n"));
				}
		}
	}
#endif
}

VOID
odm_Write_CrystalCap(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u1Byte			CrystalCap
)
{
	PADAPTER		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	if(IS_HARDWARE_TYPE_8192D(Adapter))
	{
		PHY_SetBBReg(Adapter, 0x24, 0xF0, CrystalCap & 0x0F);
		PHY_SetBBReg(Adapter, 0x28, 0xF0000000, ((CrystalCap & 0xF0) >> 4));
	}

	if(IS_HARDWARE_TYPE_8188E(Adapter))
	{
		// write 0x24[16:11] = 0x24[22:17] = CrystalCap
		PHY_SetBBReg(Adapter, REG_AFE_XTAL_CTRL, 0x7ff800, (CrystalCap | (CrystalCap << 6)));
	}
	
	if(IS_HARDWARE_TYPE_8812(Adapter))
	{
		// write 0x2C[30:25] = 0x2C[24:19] = CrystalCap
		CrystalCap = CrystalCap & 0x3F;
		PHY_SetBBReg(Adapter, REG_MAC_PHY_CTRL, 0x7FF80000, (CrystalCap | (CrystalCap << 6)));
	}	
	
	//only for B-cut
	if ((IS_HARDWARE_TYPE_8723A(Adapter) && pHalData->EEPROMVersion >= 0x01) ||
		IS_HARDWARE_TYPE_8723B(Adapter) ||IS_HARDWARE_TYPE_8192E(Adapter) || IS_HARDWARE_TYPE_8821(Adapter))
	{
		// 0x2C[23:18] = 0x2C[17:12] = CrystalCap
		CrystalCap = CrystalCap & 0x3F;
		PHY_SetBBReg(Adapter, REG_MAC_PHY_CTRL, 0xFFF000, (CrystalCap | (CrystalCap << 6)));	
	}
	
	if(IS_HARDWARE_TYPE_8723AE(Adapter))
		PHY_SetBBReg(Adapter, REG_LDOA15_CTRL, bMaskDWord, 0x01572505);				

}


//Remove DIG and FA check by Yu Chen


//3============================================================
//3 BB Power Save
//3============================================================

//Remove BB power saving by Yuchen

//3============================================================
//3 RATR MASK
//3============================================================
//3============================================================
//3 Rate Adaptive
//3============================================================

VOID
odm_RateAdaptiveMaskInit(
	IN 	PDM_ODM_T	pDM_Odm
	)
{
	PODM_RATE_ADAPTIVE	pOdmRA = &pDM_Odm->RateAdaptive;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PMGNT_INFO		pMgntInfo = &pDM_Odm->Adapter->MgntInfo;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pDM_Odm->Adapter);

	pMgntInfo->Ratr_State = DM_RATR_STA_INIT;

	if (pMgntInfo->DM_Type == DM_Type_ByDriver)
		pHalData->bUseRAMask = TRUE;
	else
		pHalData->bUseRAMask = FALSE;	

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	pOdmRA->Type = DM_Type_ByDriver;
	if (pOdmRA->Type == DM_Type_ByDriver)
		pDM_Odm->bUseRAMask = _TRUE;
	else
		pDM_Odm->bUseRAMask = _FALSE;	
#endif

	pOdmRA->RATRState = DM_RATR_STA_INIT;
	pOdmRA->LdpcThres = 35;
	pOdmRA->bUseLdpc = FALSE;
	pOdmRA->HighRSSIThresh = 50;
	pOdmRA->LowRSSIThresh = 20;
}

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN) 
VOID
ODM_RateAdaptiveStateApInit(	
	IN	PADAPTER		Adapter	,
	IN	PRT_WLAN_STA  	pEntry
	)
{
	pEntry->Ratr_State = DM_RATR_STA_INIT;
}
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
u4Byte ODM_Get_Rate_Bitmap(
	IN	PDM_ODM_T	pDM_Odm,	
	IN	u4Byte		macid,
	IN	u4Byte 		ra_mask,	
	IN	u1Byte 		rssi_level)
{
	PSTA_INFO_T   	pEntry;
	u4Byte 	rate_bitmap = 0;
	u1Byte 	WirelessMode;
	//u1Byte 	WirelessMode =*(pDM_Odm->pWirelessMode);
	
	
	pEntry = pDM_Odm->pODM_StaInfo[macid];
	if(!IS_STA_VALID(pEntry))
		return ra_mask;

	WirelessMode = pEntry->wireless_mode;
	
	switch(WirelessMode)
	{
		case ODM_WM_B:
			if(ra_mask & 0x0000000c)		//11M or 5.5M enable				
				rate_bitmap = 0x0000000d;
			else
				rate_bitmap = 0x0000000f;
			break;
			
		case (ODM_WM_G):
		case (ODM_WM_A):
			if(rssi_level == DM_RATR_STA_HIGH)
				rate_bitmap = 0x00000f00;
			else
				rate_bitmap = 0x00000ff0;
			break;
			
		case (ODM_WM_B|ODM_WM_G):
			if(rssi_level == DM_RATR_STA_HIGH)
				rate_bitmap = 0x00000f00;
			else if(rssi_level == DM_RATR_STA_MIDDLE)
				rate_bitmap = 0x00000ff0;
			else
				rate_bitmap = 0x00000ff5;
			break;		

		case (ODM_WM_B|ODM_WM_G|ODM_WM_N24G)	:
		case (ODM_WM_B|ODM_WM_N24G)	:
		case (ODM_WM_G|ODM_WM_N24G)	:
		case (ODM_WM_A|ODM_WM_N5G)	:
			{					
				if (	pDM_Odm->RFType == ODM_1T2R ||pDM_Odm->RFType == ODM_1T1R)
				{
					if(rssi_level == DM_RATR_STA_HIGH)
					{
						rate_bitmap = 0x000f0000;
					}
					else if(rssi_level == DM_RATR_STA_MIDDLE)
					{
						rate_bitmap = 0x000ff000;
					}
					else{
						if (*(pDM_Odm->pBandWidth) == ODM_BW40M)
							rate_bitmap = 0x000ff015;
						else
							rate_bitmap = 0x000ff005;
					}				
				}
				else
				{
					if(rssi_level == DM_RATR_STA_HIGH)
					{		
						rate_bitmap = 0x0f8f0000;
					}
					else if(rssi_level == DM_RATR_STA_MIDDLE)
					{
						rate_bitmap = 0x0f8ff000;
					}
					else
					{
						if (*(pDM_Odm->pBandWidth) == ODM_BW40M)
							rate_bitmap = 0x0f8ff015;
						else
							rate_bitmap = 0x0f8ff005;
					}					
				}
			}
			break;

		case (ODM_WM_AC|ODM_WM_G):
			if(rssi_level == 1)
				rate_bitmap = 0xfc3f0000;
			else if(rssi_level == 2)
				rate_bitmap = 0xfffff000;
			else
				rate_bitmap = 0xffffffff;
			break;

		case (ODM_WM_AC|ODM_WM_A):

			if (pDM_Odm->RFType == RF_1T1R)
			{
				if(rssi_level == 1)				// add by Gary for ac-series
					rate_bitmap = 0x003f8000;
				else if (rssi_level == 2)
					rate_bitmap = 0x003ff000;
				else
					rate_bitmap = 0x003ff010;
			}
			else
			{
				if(rssi_level == 1)				// add by Gary for ac-series
					rate_bitmap = 0xfe3f8000;       // VHT 2SS MCS3~9
				else if (rssi_level == 2)
					rate_bitmap = 0xfffff000;       // VHT 2SS MCS0~9
				else
					rate_bitmap = 0xfffff010;       // All
			}
			break;
			
		default:
			if(pDM_Odm->RFType == RF_1T2R)
				rate_bitmap = 0x000fffff;
			else
				rate_bitmap = 0x0fffffff;
			break;	

	}

	//printk("%s ==> rssi_level:0x%02x, WirelessMode:0x%02x, rate_bitmap:0x%08x \n",__FUNCTION__,rssi_level,WirelessMode,rate_bitmap);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, (" ==> rssi_level:0x%02x, WirelessMode:0x%02x, rate_bitmap:0x%08x \n",rssi_level,WirelessMode,rate_bitmap));

	return (ra_mask&rate_bitmap);
	
}	
#endif


VOID
odm_RefreshBasicRateMask(
	IN		PDM_ODM_T		pDM_Odm	
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER		Adapter	 =  pDM_Odm->Adapter;
	static u1Byte		Stage = 0;
	u1Byte			CurStage = 0;
	OCTET_STRING 	osRateSet;
	PMGNT_INFO		pMgntInfo = GetDefaultMgntInfo(Adapter);
	u1Byte 			RateSet[5] = {MGN_1M, MGN_2M, MGN_5_5M, MGN_11M, MGN_6M};

	if(pDM_Odm->SupportICType != ODM_RTL8812 && pDM_Odm->SupportICType != ODM_RTL8821 )
		return;

	if(pDM_Odm->bLinked == FALSE)	// unlink Default port information
		CurStage = 0;	
	else if(pDM_Odm->RSSI_Min < 40)	// link RSSI  < 40%
		CurStage = 1;
	else if(pDM_Odm->RSSI_Min > 45)	// link RSSI > 45%
		CurStage = 3;	
	else
		CurStage = 2;					// link  25% <= RSSI <= 30%

	if(CurStage != Stage)
	{
		if(CurStage == 1)
		{
			FillOctetString(osRateSet, RateSet, 5);
			FilterSupportRate(pMgntInfo->mBrates, &osRateSet, FALSE);
			Adapter->HalFunc.SetHwRegHandler(Adapter, HW_VAR_BASIC_RATE, (pu1Byte)&osRateSet);
		}
		else if(CurStage == 3 && (Stage == 1 || Stage == 2))
		{
			Adapter->HalFunc.SetHwRegHandler( Adapter, HW_VAR_BASIC_RATE, (pu1Byte)(&pMgntInfo->mBrates) );
		}
	}
	
	Stage = CurStage;
#endif
}

/*-----------------------------------------------------------------------------
 * Function:	odm_RefreshRateAdaptiveMask()
 *
 * Overview:	Update rate table mask according to rssi
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	05/27/2009	hpfan	Create Version 0.  
 *
 *---------------------------------------------------------------------------*/
VOID
odm_RefreshRateAdaptiveMask(
	IN		PDM_ODM_T		pDM_Odm
	)
{

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_TRACE, ("odm_RefreshRateAdaptiveMask()---------->\n"));	
	if (!(pDM_Odm->SupportAbility & ODM_BB_RA_MASK))
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_TRACE, ("odm_RefreshRateAdaptiveMask(): Return cos not supported\n"));
		return;	
	}
	//
	// 2011/09/29 MH In HW integration first stage, we provide 4 different handle to operate
	// at the same time. In the stage2/3, we need to prive universal interface and merge all
	// HW dynamic mechanism.
	//
	switch	(pDM_Odm->SupportPlatform)
	{
		case	ODM_WIN:
			odm_RefreshRateAdaptiveMaskMP(pDM_Odm);
			break;

		case	ODM_CE:
			odm_RefreshRateAdaptiveMaskCE(pDM_Odm);
			break;

		case	ODM_AP:
		case	ODM_ADSL:
			odm_RefreshRateAdaptiveMaskAPADSL(pDM_Odm);
			break;
	}
	
}

VOID
odm_RefreshRateAdaptiveMaskMP(
	IN		PDM_ODM_T		pDM_Odm	
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER				pAdapter	 =  pDM_Odm->Adapter;
	PADAPTER 				pTargetAdapter = NULL;
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(pAdapter);
	PMGNT_INFO				pMgntInfo = GetDefaultMgntInfo(pAdapter);
	PODM_RATE_ADAPTIVE		pRA = &pDM_Odm->RateAdaptive;

	if(pAdapter->bDriverStopped)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_TRACE, ("<---- odm_RefreshRateAdaptiveMask(): driver is going to unload\n"));
		return;
	}

	if(!pHalData->bUseRAMask)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("<---- odm_RefreshRateAdaptiveMask(): driver does not control rate adaptive mask\n"));
		return;
	}

	// if default port is connected, update RA table for default port (infrastructure mode only)
	if(pMgntInfo->mAssoc && (!ACTING_AS_AP(pAdapter)))
	{
	
		if(pHalData->UndecoratedSmoothedPWDB < pRA->LdpcThres)
		{
			pRA->bUseLdpc = TRUE;
			pRA->bLowerRtsRate = TRUE;
			if((pDM_Odm->SupportICType == ODM_RTL8821) && (pDM_Odm->CutVersion == ODM_CUT_A))
				MgntSet_TX_LDPC(pAdapter,0,TRUE);
			//DbgPrint("RSSI=%d, bUseLdpc = TRUE\n", pHalData->UndecoratedSmoothedPWDB);
		}
		else if(pHalData->UndecoratedSmoothedPWDB > (pRA->LdpcThres-5))
		{
			pRA->bUseLdpc = FALSE;
			pRA->bLowerRtsRate = FALSE;
			if((pDM_Odm->SupportICType == ODM_RTL8821) && (pDM_Odm->CutVersion == ODM_CUT_A))
				MgntSet_TX_LDPC(pAdapter,0,FALSE);
			//DbgPrint("RSSI=%d, bUseLdpc = FALSE\n", pHalData->UndecoratedSmoothedPWDB);
		}
	
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("odm_RefreshRateAdaptiveMask(): Infrasture Mode\n"));
		if( ODM_RAStateCheck(pDM_Odm, pHalData->UndecoratedSmoothedPWDB, pMgntInfo->bSetTXPowerTrainingByOid, &pMgntInfo->Ratr_State) )
		{
			ODM_PRINT_ADDR(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Target AP addr : "), pMgntInfo->Bssid);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RSSI:%d, RSSI_LEVEL:%d\n", pHalData->UndecoratedSmoothedPWDB, pMgntInfo->Ratr_State));
			pAdapter->HalFunc.UpdateHalRAMaskHandler(pAdapter, pMgntInfo->mMacId, NULL, pMgntInfo->Ratr_State);
		}
	}

	//
	// The following part configure AP/VWifi/IBSS rate adaptive mask.
	//

	if(pMgntInfo->mIbss) 	// Target: AP/IBSS peer.
		pTargetAdapter = GetDefaultAdapter(pAdapter);
	else
		pTargetAdapter = GetFirstAPAdapter(pAdapter);

	// if extension port (softap) is started, updaet RA table for more than one clients associate
	if(pTargetAdapter != NULL)
	{
		int	i;
		PRT_WLAN_STA	pEntry;

		for(i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++)
		{
			pEntry = AsocEntry_EnumStation(pTargetAdapter, i);
			if(NULL != pEntry)
			{
				if(pEntry->bAssociated)
				{
					if(ODM_RAStateCheck(pDM_Odm, pEntry->rssi_stat.UndecoratedSmoothedPWDB, pMgntInfo->bSetTXPowerTrainingByOid, &pEntry->Ratr_State) )
					{
						ODM_PRINT_ADDR(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Target STA addr : "), pEntry->MacAddr);
						ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RSSI:%d, RSSI_LEVEL:%d\n", pEntry->rssi_stat.UndecoratedSmoothedPWDB, pEntry->Ratr_State));
						pAdapter->HalFunc.UpdateHalRAMaskHandler(pTargetAdapter, pEntry->AssociatedMacId, pEntry, pEntry->Ratr_State);
					}
				}
			}
		}
	}

	if(pMgntInfo->bSetTXPowerTrainingByOid)
		pMgntInfo->bSetTXPowerTrainingByOid = FALSE;	
#endif	// #if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
}


VOID
odm_RefreshRateAdaptiveMaskCE(
	IN		PDM_ODM_T		pDM_Odm	
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	u1Byte	i;
	PADAPTER	pAdapter	 =  pDM_Odm->Adapter;
	PODM_RATE_ADAPTIVE		pRA = &pDM_Odm->RateAdaptive;

	if(pAdapter->bDriverStopped)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_TRACE, ("<---- odm_RefreshRateAdaptiveMask(): driver is going to unload\n"));
		return;
	}

	if(!pDM_Odm->bUseRAMask)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("<---- odm_RefreshRateAdaptiveMask(): driver does not control rate adaptive mask\n"));
		return;
	}

	//printk("==> %s \n",__FUNCTION__);

	for(i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++){
		PSTA_INFO_T pstat = pDM_Odm->pODM_StaInfo[i];
		if(IS_STA_VALID(pstat) ) {
			if(IS_MCAST( pstat->hwaddr))  //if(psta->mac_id ==1)
				 continue;
			if(IS_MCAST( pstat->hwaddr))
				continue;

			#if((RTL8812A_SUPPORT==1)||(RTL8821A_SUPPORT==1))
			if((pDM_Odm->SupportICType == ODM_RTL8812)||(pDM_Odm->SupportICType == ODM_RTL8821))
			{
				if(pstat->rssi_stat.UndecoratedSmoothedPWDB < pRA->LdpcThres)
				{
					pRA->bUseLdpc = TRUE;
					pRA->bLowerRtsRate = TRUE;
					if((pDM_Odm->SupportICType == ODM_RTL8821) && (pDM_Odm->CutVersion == ODM_CUT_A))
						Set_RA_LDPC_8812(pstat, TRUE);
					//DbgPrint("RSSI=%d, bUseLdpc = TRUE\n", pHalData->UndecoratedSmoothedPWDB);
				}
				else if(pstat->rssi_stat.UndecoratedSmoothedPWDB > (pRA->LdpcThres-5))
				{
					pRA->bUseLdpc = FALSE;
					pRA->bLowerRtsRate = FALSE;
					if((pDM_Odm->SupportICType == ODM_RTL8821) && (pDM_Odm->CutVersion == ODM_CUT_A))
						Set_RA_LDPC_8812(pstat, FALSE);
					//DbgPrint("RSSI=%d, bUseLdpc = FALSE\n", pHalData->UndecoratedSmoothedPWDB);
				}
			}
			#endif

			if( TRUE == ODM_RAStateCheck(pDM_Odm, pstat->rssi_stat.UndecoratedSmoothedPWDB, FALSE , &pstat->rssi_level) )
			{
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RSSI:%d, RSSI_LEVEL:%d\n", pstat->rssi_stat.UndecoratedSmoothedPWDB, pstat->rssi_level));
				//printk("RSSI:%d, RSSI_LEVEL:%d\n", pstat->rssi_stat.UndecoratedSmoothedPWDB, pstat->rssi_level);
				rtw_hal_update_ra_mask(pstat, pstat->rssi_level);
			}
		
		}
	}			
	
#endif
}

VOID
odm_RefreshRateAdaptiveMaskAPADSL(
	IN		PDM_ODM_T		pDM_Odm
	)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	struct rtl8192cd_priv *priv = pDM_Odm->priv;
	struct stat_info	*pstat;

	if (!priv->pmib->dot11StationConfigEntry.autoRate) 
		return;

	if (list_empty(&priv->asoc_list))
		return;

	list_for_each_entry(pstat, &priv->asoc_list, asoc_list) {
		if(ODM_RAStateCheck(pDM_Odm, (s4Byte)pstat->rssi, FALSE, &pstat->rssi_level) ) {
			ODM_PRINT_ADDR(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Target STA addr : "), pstat->hwaddr);
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RSSI:%d, RSSI_LEVEL:%d\n", pstat->rssi, pstat->rssi_level));

#ifdef CONFIG_RTL_88E_SUPPORT
			if (GET_CHIP_VER(priv)==VERSION_8188E) {
#ifdef TXREPORT
				add_RATid(priv, pstat);
#endif
			} else
#endif
			{
#if defined(CONFIG_RTL_92D_SUPPORT) || defined(CONFIG_RTL_92C_SUPPORT)			
			add_update_RATid(priv, pstat);
#endif
		        }
	        }
	}
#endif
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID
ODM_DynamicARFBSelect(
	IN		PDM_ODM_T		pDM_Odm,
	IN 		u1Byte			rate,
	IN  		BOOLEAN			Collision_State	
)
{

	if(pDM_Odm->SupportICType != ODM_RTL8192E)
		return;

	if (rate >= DESC_RATEMCS8  && rate <= DESC_RATEMCS12){
		if (Collision_State == 1){
			if(rate == DESC_RATEMCS12){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x0);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x07060501);	
			}
			else if(rate == DESC_RATEMCS11){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x0);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x07070605);	
			}
			else if(rate == DESC_RATEMCS10){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x0);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x08080706);	
			}
			else if(rate == DESC_RATEMCS9){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x0);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x08080707);	
			}
			else{

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x0);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x09090808);	
			}
		}
		else{   // Collision_State == 0
			if(rate == DESC_RATEMCS12){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x05010000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x09080706);	
			}
			else if(rate == DESC_RATEMCS11){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x06050000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x09080807);	
			}
			else if(rate == DESC_RATEMCS10){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x07060000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x0a090908);	
			}
			else if(rate == DESC_RATEMCS9){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x07070000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x0a090808);	
			}
			else{

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x08080000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x0b0a0909);	
			}
		}
	}
	else{  // MCS13~MCS15,  1SS, G-mode
		if (Collision_State == 1){
			if(rate == DESC_RATEMCS15){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x00000000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x05040302);	
			}
			else if(rate == DESC_RATEMCS14){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x00000000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x06050302);	
			}
			else if(rate == DESC_RATEMCS13){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x00000000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x07060502);	
			}
			else{

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x00000000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x06050402);	
			}
		}
		else{   // Collision_State == 0
  			if(rate == DESC_RATEMCS15){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x03020000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x07060504);	
			}
			else if(rate == DESC_RATEMCS14){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x03020000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x08070605);	
			}
			else if(rate == DESC_RATEMCS13){

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x05020000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x09080706);	
			}
			else{

				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E, 0x04020000);
				ODM_Write4Byte(pDM_Odm, REG_DARFRC_8192E+4, 0x08070605);	
			}


		}

	}	

}

#endif

// Return Value: BOOLEAN
// - TRUE: RATRState is changed.
BOOLEAN 
ODM_RAStateCheck(
	IN		PDM_ODM_T		pDM_Odm,
	IN		s4Byte			RSSI,
	IN		BOOLEAN			bForceUpdate,
	OUT		pu1Byte			pRATRState
	)
{
	PODM_RATE_ADAPTIVE pRA = &pDM_Odm->RateAdaptive;
	const u1Byte GoUpGap = 5;
	u1Byte HighRSSIThreshForRA = pRA->HighRSSIThresh;
	u1Byte LowRSSIThreshForRA = pRA->LowRSSIThresh;
	u1Byte RATRState;

	// Threshold Adjustment: 
	// when RSSI state trends to go up one or two levels, make sure RSSI is high enough.
	// Here GoUpGap is added to solve the boundary's level alternation issue.
	switch (*pRATRState)
	{
		case DM_RATR_STA_INIT:
		case DM_RATR_STA_HIGH:
			break;

		case DM_RATR_STA_MIDDLE:
			HighRSSIThreshForRA += GoUpGap;
			break;

		case DM_RATR_STA_LOW:
			HighRSSIThreshForRA += GoUpGap;
			LowRSSIThreshForRA += GoUpGap;
			break;

		default: 
			ODM_RT_ASSERT(pDM_Odm, FALSE, ("wrong rssi level setting %d !", *pRATRState) );
			break;
	}

	// Decide RATRState by RSSI.
	if(RSSI > HighRSSIThreshForRA)
		RATRState = DM_RATR_STA_HIGH;
	else if(RSSI > LowRSSIThreshForRA)
		RATRState = DM_RATR_STA_MIDDLE;
	else
		RATRState = DM_RATR_STA_LOW;
	//printk("==>%s,RATRState:0x%02x ,RSSI:%d \n",__FUNCTION__,RATRState,RSSI);

	if( *pRATRState!=RATRState || bForceUpdate)
	{
		ODM_RT_TRACE( pDM_Odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RSSI Level %d -> %d\n", *pRATRState, RATRState) );
		*pRATRState = RATRState;
		return TRUE;
	}

	return FALSE;
}


//============================================================

//3============================================================
//3 Dynamic Tx Power
//3============================================================

VOID 
odm_DynamicTxPowerInit(
	IN		PDM_ODM_T		pDM_Odm	
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER	Adapter = pDM_Odm->Adapter;
	PMGNT_INFO			pMgntInfo = &Adapter->MgntInfo;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);

	#if DEV_BUS_TYPE==RT_USB_INTERFACE					
	if(RT_GetInterfaceSelection(Adapter) == INTF_SEL1_USB_High_Power)
	{
		odm_DynamicTxPowerSavePowerIndex(pDM_Odm);
		pMgntInfo->bDynamicTxPowerEnable = TRUE;
	}		
	else	
	#else
	//so 92c pci do not need dynamic tx power? vivi check it later
	if(IS_HARDWARE_TYPE_8192D(Adapter))
		pMgntInfo->bDynamicTxPowerEnable = TRUE;
	else
		pMgntInfo->bDynamicTxPowerEnable = FALSE;
	#endif
	

	pHalData->LastDTPLvl = TxHighPwrLevel_Normal;
	pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PADAPTER	Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	pdmpriv->bDynamicTxPowerEnable = _FALSE;

	#if (RTL8192C_SUPPORT==1) 
	#ifdef CONFIG_USB_HCI

	#ifdef CONFIG_INTEL_PROXIM
	if((pHalData->BoardType == BOARD_USB_High_PA)||(Adapter->proximity.proxim_support==_TRUE))
	#else
	if(pHalData->BoardType == BOARD_USB_High_PA)
	#endif

	{
		//odm_SavePowerIndex(Adapter);
		odm_DynamicTxPowerSavePowerIndex(pDM_Odm);
		pdmpriv->bDynamicTxPowerEnable = _TRUE;
	}		
	else	
	#else
		pdmpriv->bDynamicTxPowerEnable = _FALSE;
	#endif
	#endif
	
	pdmpriv->LastDTPLvl = TxHighPwrLevel_Normal;
	pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;	
	
#endif
	
}

VOID
odm_DynamicTxPowerSavePowerIndex(
	IN		PDM_ODM_T		pDM_Odm	
	)
{	
	u1Byte		index;
	u4Byte		Power_Index_REG[6] = {0xc90, 0xc91, 0xc92, 0xc98, 0xc99, 0xc9a};
	
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)	
	PADAPTER	Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);	
	for(index = 0; index< 6; index++)
		pHalData->PowerIndex_backup[index] = PlatformEFIORead1Byte(Adapter, Power_Index_REG[index]);
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)	
	PADAPTER	Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	for(index = 0; index< 6; index++)
		pdmpriv->PowerIndex_backup[index] = rtw_read8(Adapter, Power_Index_REG[index]);
#endif
}

VOID
odm_DynamicTxPowerRestorePowerIndex(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	u1Byte			index;
	PADAPTER		Adapter = pDM_Odm->Adapter;

#if (DM_ODM_SUPPORT_TYPE & (ODM_CE|ODM_WIN))
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u4Byte			Power_Index_REG[6] = {0xc90, 0xc91, 0xc92, 0xc98, 0xc99, 0xc9a};
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	for(index = 0; index< 6; index++)
		PlatformEFIOWrite1Byte(Adapter, Power_Index_REG[index], pHalData->PowerIndex_backup[index]);
#elif(DM_ODM_SUPPORT_TYPE == ODM_CE)	
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	for(index = 0; index< 6; index++)
		rtw_write8(Adapter, Power_Index_REG[index], pdmpriv->PowerIndex_backup[index]);
#endif
#endif
}

VOID
odm_DynamicTxPowerWritePowerIndex(
	IN	PDM_ODM_T	pDM_Odm, 
	IN 	u1Byte		Value)
{

	u1Byte			index;
	u4Byte			Power_Index_REG[6] = {0xc90, 0xc91, 0xc92, 0xc98, 0xc99, 0xc9a};
	
	for(index = 0; index< 6; index++)
		//PlatformEFIOWrite1Byte(Adapter, Power_Index_REG[index], Value);
		ODM_Write1Byte(pDM_Odm, Power_Index_REG[index], Value);

}


VOID 
odm_DynamicTxPower(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	// 
	// For AP/ADSL use prtl8192cd_priv
	// For CE/NIC use PADAPTER
	//
	//PADAPTER		pAdapter = pDM_Odm->Adapter;
//	prtl8192cd_priv	priv		= pDM_Odm->priv;

	if (!(pDM_Odm->SupportAbility & ODM_BB_DYNAMIC_TXPWR))
		return;

	//
	// 2011/09/29 MH In HW integration first stage, we provide 4 different handle to operate
	// at the same time. In the stage2/3, we need to prive universal interface and merge all
	// HW dynamic mechanism.
	//
	switch	(pDM_Odm->SupportPlatform)
	{
		case	ODM_WIN:
		case	ODM_CE:
			odm_DynamicTxPowerNIC(pDM_Odm);
			break;	
		case	ODM_AP:
			odm_DynamicTxPowerAP(pDM_Odm);
			break;		

		case	ODM_ADSL:
			//odm_DIGAP(pDM_Odm);
			break;	
	}

	
}


VOID 
odm_DynamicTxPowerNIC(
	IN		PDM_ODM_T		pDM_Odm
	)
{	
	if (!(pDM_Odm->SupportAbility & ODM_BB_DYNAMIC_TXPWR))
		return;
	
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))

	if(pDM_Odm->SupportICType == ODM_RTL8192C)	
	{
		odm_DynamicTxPower_92C(pDM_Odm);
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8192D)
	{
		odm_DynamicTxPower_92D(pDM_Odm);
	}
	else if (pDM_Odm->SupportICType == ODM_RTL8821)
	{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
		PADAPTER		Adapter	 =  pDM_Odm->Adapter;
		PMGNT_INFO		pMgntInfo = GetDefaultMgntInfo(Adapter);

		if (pMgntInfo->RegRspPwr == 1)
		{
			if(pDM_Odm->RSSI_Min > 60)
			{
				ODM_SetMACReg(pDM_Odm, ODM_REG_RESP_TX_11AC, BIT20|BIT19|BIT18, 1); // Resp TXAGC offset = -3dB

			}
			else if(pDM_Odm->RSSI_Min < 55)
			{
				ODM_SetMACReg(pDM_Odm, ODM_REG_RESP_TX_11AC, BIT20|BIT19|BIT18, 0); // Resp TXAGC offset = 0dB
			}
		}
#endif
	}
#endif	
}

VOID 
odm_DynamicTxPowerAP(
	IN		PDM_ODM_T		pDM_Odm

	)
{	
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	prtl8192cd_priv	priv		= pDM_Odm->priv;
	s4Byte i;

	if(!priv->pshare->rf_ft_var.tx_pwr_ctrl)
		return;
	
#ifdef HIGH_POWER_EXT_PA
	if(pDM_Odm->ExtPA)
		tx_power_control(priv);
#endif		

	/*
	 *	Check if station is near by to use lower tx power
	 */

	if ((priv->up_time % 3) == 0 )  {
		for(i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++){
			PSTA_INFO_T pstat = pDM_Odm->pODM_StaInfo[i];
			if(IS_STA_VALID(pstat) ) {
				if ((pstat->hp_level == 0) && (pstat->rssi > TX_POWER_NEAR_FIELD_THRESH_AP+4))
					pstat->hp_level = 1;
				else if ((pstat->hp_level == 1) && (pstat->rssi < TX_POWER_NEAR_FIELD_THRESH_AP))
					pstat->hp_level = 0;
			}
		}
	}

#endif	
}


VOID 
odm_DynamicTxPower_92C(
	IN	PDM_ODM_T	pDM_Odm
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER Adapter = pDM_Odm->Adapter;
	PMGNT_INFO			pMgntInfo = &Adapter->MgntInfo;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	s4Byte				UndecoratedSmoothedPWDB;

	// 2012/01/12 MH According to Luke's suggestion, only high power will support the feature.
	if (pDM_Odm->ExtPA == FALSE)
		return;

	// STA not connected and AP not connected
	if((!pMgntInfo->bMediaConnect) &&	
		(pHalData->EntryMinUndecoratedSmoothedPWDB == 0))
	{
		ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("Not connected to any \n"));
		pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;

		//the LastDTPlvl should reset when disconnect, 
		//otherwise the tx power level wouldn't change when disconnect and connect again.
		// Maddest 20091220.
		 pHalData->LastDTPLvl=TxHighPwrLevel_Normal;
		return;
	}

#if (INTEL_PROXIMITY_SUPPORT == 1)
	// Intel set fixed tx power 
	if(pMgntInfo->IntelProximityModeInfo.PowerOutput > 0)
	{
		switch(pMgntInfo->IntelProximityModeInfo.PowerOutput){
			case 1:
				pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_100;
				ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_100\n"));
				break;
			case 2:
				pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_70;
				ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_70\n"));
				break;
			case 3:
				pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_50;
				ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_50\n"));
				break;
			case 4:
				pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_35;
				ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_35\n"));
				break;
			case 5:
				pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_15;
				ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_15\n"));
				break;
			default:
				pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_100;
				ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_100\n"));
				break;
		}		
	}
	else
#endif		
	{ 
		if(	(pMgntInfo->bDynamicTxPowerEnable != TRUE) ||
			(pHalData->DMFlag & HAL_DM_HIPWR_DISABLE) ||
			pMgntInfo->IOTAction & HT_IOT_ACT_DISABLE_HIGH_POWER)
		{
			pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
		}
		else
		{
			if(pMgntInfo->bMediaConnect)	// Default port
			{
				if(ACTING_AS_AP(Adapter) || ACTING_AS_IBSS(Adapter))
				{
					UndecoratedSmoothedPWDB = pHalData->EntryMinUndecoratedSmoothedPWDB;
					ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("AP Client PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
				}
				else
				{
					UndecoratedSmoothedPWDB = pHalData->UndecoratedSmoothedPWDB;
					ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("STA Default Port PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
				}
			}
			else // associated entry pwdb
			{	
				UndecoratedSmoothedPWDB = pHalData->EntryMinUndecoratedSmoothedPWDB;
				ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("AP Ext Port PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
			}
				
			if(UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL2)
			{
				pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Level2;
				ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_Level1 (TxPwr=0x0)\n"));
			}
			else if((UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL2-3)) &&
				(UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL1) )
			{
				pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Level1;
				ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_Level1 (TxPwr=0x10)\n"));
			}
			else if(UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL1-5))
			{
				pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
				ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_Normal\n"));
			}
		}
	}
	if( pHalData->DynamicTxHighPowerLvl != pHalData->LastDTPLvl )
	{
		ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("PHY_SetTxPowerLevel8192C() Channel = %d \n" , pHalData->CurrentChannel));
		PHY_SetTxPowerLevel8192C(Adapter, pHalData->CurrentChannel);
		if(	(pHalData->DynamicTxHighPowerLvl == TxHighPwrLevel_Normal) &&
			(pHalData->LastDTPLvl == TxHighPwrLevel_Level1 || pHalData->LastDTPLvl == TxHighPwrLevel_Level2)) //TxHighPwrLevel_Normal
			odm_DynamicTxPowerRestorePowerIndex(pDM_Odm);
		else if(pHalData->DynamicTxHighPowerLvl == TxHighPwrLevel_Level1)
			odm_DynamicTxPowerWritePowerIndex(pDM_Odm, 0x14);
		else if(pHalData->DynamicTxHighPowerLvl == TxHighPwrLevel_Level2)
			odm_DynamicTxPowerWritePowerIndex(pDM_Odm, 0x10);
	}
	pHalData->LastDTPLvl = pHalData->DynamicTxHighPowerLvl;

	
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

	#if (RTL8192C_SUPPORT==1) 
	PADAPTER Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	int	UndecoratedSmoothedPWDB;

	if(!pdmpriv->bDynamicTxPowerEnable)
		return;

#ifdef CONFIG_INTEL_PROXIM
	if(Adapter->proximity.proxim_on== _TRUE){
		struct proximity_priv *prox_priv=Adapter->proximity.proximity_priv;
		// Intel set fixed tx power 
		printk("\n %s  Adapter->proximity.proxim_on=%d prox_priv->proxim_modeinfo->power_output=%d \n",__FUNCTION__,Adapter->proximity.proxim_on,prox_priv->proxim_modeinfo->power_output);
		if(prox_priv!=NULL){
			if(prox_priv->proxim_modeinfo->power_output> 0)	
			{
				switch(prox_priv->proxim_modeinfo->power_output)
				{
					case 1:
						pdmpriv->DynamicTxHighPowerLvl  = TxHighPwrLevel_100;
						printk("TxHighPwrLevel_100\n");
						break;
					case 2:
						pdmpriv->DynamicTxHighPowerLvl  = TxHighPwrLevel_70;
						printk("TxHighPwrLevel_70\n");
						break;
					case 3:
						pdmpriv->DynamicTxHighPowerLvl  = TxHighPwrLevel_50;
						printk("TxHighPwrLevel_50\n");
						break;
					case 4:
						pdmpriv->DynamicTxHighPowerLvl  = TxHighPwrLevel_35;
						printk("TxHighPwrLevel_35\n");
						break;
					case 5:
						pdmpriv->DynamicTxHighPowerLvl  = TxHighPwrLevel_15;
						printk("TxHighPwrLevel_15\n");
						break;
					default:
						pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_100;
						printk("TxHighPwrLevel_100\n");
						break;
				}		
			}
		}
	}
	else
#endif	
	{
		// STA not connected and AP not connected
		if((check_fwstate(pmlmepriv, _FW_LINKED) != _TRUE) &&	
			(pdmpriv->EntryMinUndecoratedSmoothedPWDB == 0))
		{
			//ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("Not connected to any \n"));
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;

			//the LastDTPlvl should reset when disconnect, 
			//otherwise the tx power level wouldn't change when disconnect and connect again.
			// Maddest 20091220.
			pdmpriv->LastDTPLvl=TxHighPwrLevel_Normal;
			return;
		}
		
		if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)	// Default port
		{
		#if 0
			//todo: AP Mode
			if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE) ||
			       (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE))
			{
				UndecoratedSmoothedPWDB = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
				//ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("AP Client PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
			}
			else
			{
				UndecoratedSmoothedPWDB = pdmpriv->UndecoratedSmoothedPWDB;
				//ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("STA Default Port PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
			}
		#else
		UndecoratedSmoothedPWDB = pdmpriv->EntryMinUndecoratedSmoothedPWDB;	
		#endif
		}
		else // associated entry pwdb
		{	
			UndecoratedSmoothedPWDB = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
			//ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("AP Ext Port PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
		}
			
		if(UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL2)
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Level2;
			//ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_Level1 (TxPwr=0x0)\n"));
		}
		else if((UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL2-3)) &&
			(UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL1) )
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Level1;
			//ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_Level1 (TxPwr=0x10)\n"));
		}
		else if(UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL1-5))
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
			//ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_Normal\n"));
		}
	}
	if( (pdmpriv->DynamicTxHighPowerLvl != pdmpriv->LastDTPLvl) )
	{
		PHY_SetTxPowerLevel8192C(Adapter, pHalData->CurrentChannel);
		if(pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_Normal) // HP1 -> Normal  or HP2 -> Normal
			odm_DynamicTxPowerRestorePowerIndex(pDM_Odm);
		else if(pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_Level1)
			odm_DynamicTxPowerWritePowerIndex(pDM_Odm, 0x14);
		else if(pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_Level2)
			odm_DynamicTxPowerWritePowerIndex(pDM_Odm, 0x10);
	}
	pdmpriv->LastDTPLvl = pdmpriv->DynamicTxHighPowerLvl;
	#endif
#endif	// #if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

}


VOID 
odm_DynamicTxPower_92D(
	IN	PDM_ODM_T	pDM_Odm
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER Adapter = pDM_Odm->Adapter;
	PMGNT_INFO			pMgntInfo = &Adapter->MgntInfo;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	s4Byte				UndecoratedSmoothedPWDB;

	PADAPTER	BuddyAdapter = Adapter->BuddyAdapter;
	BOOLEAN		bGetValueFromBuddyAdapter = dm_DualMacGetParameterFromBuddyAdapter(Adapter);
	u1Byte		HighPowerLvlBackForMac0 = TxHighPwrLevel_Level1;

	// 2012/01/12 MH According to Luke's suggestion, only high power will support the feature.
	if (pDM_Odm->ExtPA == FALSE)
		return;

	// If dynamic high power is disabled.
	if( (pMgntInfo->bDynamicTxPowerEnable != TRUE) ||
		(pHalData->DMFlag & HAL_DM_HIPWR_DISABLE) ||
		pMgntInfo->IOTAction & HT_IOT_ACT_DISABLE_HIGH_POWER)
	{
		pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
		return;
	}

	// STA not connected and AP not connected
	if((!pMgntInfo->bMediaConnect) &&	
		(pHalData->EntryMinUndecoratedSmoothedPWDB == 0))
	{
		ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("Not connected to any \n"));
		pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;

		//the LastDTPlvl should reset when disconnect, 
		//otherwise the tx power level wouldn't change when disconnect and connect again.
		// Maddest 20091220.
		 pHalData->LastDTPLvl=TxHighPwrLevel_Normal;
		return;
	}
	
	if(pMgntInfo->bMediaConnect)	// Default port
	{
		if(ACTING_AS_AP(Adapter) || pMgntInfo->mIbss)
		{
			UndecoratedSmoothedPWDB = pHalData->EntryMinUndecoratedSmoothedPWDB;
			ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("AP Client PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
		}
		else
		{
			UndecoratedSmoothedPWDB = pHalData->UndecoratedSmoothedPWDB;
			ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("STA Default Port PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
		}
	}
	else // associated entry pwdb
	{	
		UndecoratedSmoothedPWDB = pHalData->EntryMinUndecoratedSmoothedPWDB;
		ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("AP Ext Port PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
	}
	
	if(IS_HARDWARE_TYPE_8192D(Adapter) && GET_HAL_DATA(Adapter)->CurrentBandType == 1){
		if(UndecoratedSmoothedPWDB >= 0x33)
		{
			pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Level2;
			ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("5G:TxHighPwrLevel_Level2 (TxPwr=0x0)\n"));
		}
		else if((UndecoratedSmoothedPWDB <0x33) &&
			(UndecoratedSmoothedPWDB >= 0x2b) )
		{
			pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Level1;
			ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("5G:TxHighPwrLevel_Level1 (TxPwr=0x10)\n"));
		}
		else if(UndecoratedSmoothedPWDB < 0x2b)
		{
			pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
			ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("5G:TxHighPwrLevel_Normal\n"));
		}

	}
	else
	
	{
		if(UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL2)
		{
			pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Level1;
			ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_Level1 (TxPwr=0x0)\n"));
		}
		else if((UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL2-3)) &&
			(UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL1) )
		{
			pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Level1;
			ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_Level1 (TxPwr=0x10)\n"));
		}
		else if(UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL1-5))
		{
			pHalData->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
			ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_Normal\n"));
		}

	}

//sherry  delete flag 20110517
	if(bGetValueFromBuddyAdapter)
	{
		ODM_RT_TRACE(pDM_Odm,COMP_MLME,DBG_LOUD,("dm_DynamicTxPower() mac 0 for mac 1 \n"));
		if(Adapter->DualMacDMSPControl.bChangeTxHighPowerLvlForAnotherMacOfDMSP)
		{
			ODM_RT_TRACE(pDM_Odm,COMP_MLME,DBG_LOUD,("dm_DynamicTxPower() change value \n"));
			HighPowerLvlBackForMac0 = pHalData->DynamicTxHighPowerLvl;
			pHalData->DynamicTxHighPowerLvl = Adapter->DualMacDMSPControl.CurTxHighLvlForAnotherMacOfDMSP;
			PHY_SetTxPowerLevel8192C(Adapter, pHalData->CurrentChannel);
			pHalData->DynamicTxHighPowerLvl = HighPowerLvlBackForMac0;
			Adapter->DualMacDMSPControl.bChangeTxHighPowerLvlForAnotherMacOfDMSP = FALSE;
		}						
	}

	if( (pHalData->DynamicTxHighPowerLvl != pHalData->LastDTPLvl) )
	{
			ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("PHY_SetTxPowerLevel8192S() Channel = %d \n" , pHalData->CurrentChannel));
			if(Adapter->DualMacSmartConcurrent == TRUE)
			{
				if(BuddyAdapter == NULL)
				{
					ODM_RT_TRACE(pDM_Odm,COMP_MLME,DBG_LOUD,("dm_DynamicTxPower() BuddyAdapter == NULL case \n"));
					if(!Adapter->bSlaveOfDMSP)
					{
						PHY_SetTxPowerLevel8192C(Adapter, pHalData->CurrentChannel);
					}
				}
				else
				{
					if(pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY)
					{
						ODM_RT_TRACE(pDM_Odm,COMP_MLME,DBG_LOUD,("dm_DynamicTxPower() BuddyAdapter DMSP \n"));
						if(Adapter->bSlaveOfDMSP)
						{
							ODM_RT_TRACE(pDM_Odm,COMP_MLME,DBG_LOUD,("dm_DynamicTxPower() bslave case  \n"));
							BuddyAdapter->DualMacDMSPControl.bChangeTxHighPowerLvlForAnotherMacOfDMSP = TRUE;
							BuddyAdapter->DualMacDMSPControl.CurTxHighLvlForAnotherMacOfDMSP = pHalData->DynamicTxHighPowerLvl;
						}
						else
						{
							ODM_RT_TRACE(pDM_Odm,COMP_MLME,DBG_LOUD,("dm_DynamicTxPower() master case  \n"));					
							if(!bGetValueFromBuddyAdapter)
							{
								ODM_RT_TRACE(pDM_Odm,COMP_MLME,DBG_LOUD,("dm_DynamicTxPower() mac 0 for mac 0 \n"));
								PHY_SetTxPowerLevel8192C(Adapter, pHalData->CurrentChannel);
							}
						}
					}
					else
					{
						ODM_RT_TRACE(pDM_Odm,COMP_MLME,DBG_LOUD,("dm_DynamicTxPower() BuddyAdapter DMDP\n"));
						PHY_SetTxPowerLevel8192C(Adapter, pHalData->CurrentChannel);
					}
				}
			}
			else
			{
				PHY_SetTxPowerLevel8192C(Adapter, pHalData->CurrentChannel);
			}

		}
	pHalData->LastDTPLvl = pHalData->DynamicTxHighPowerLvl;
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
#if (RTL8192D_SUPPORT==1) 
	PADAPTER Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);

	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	DM_ODM_T 		*podmpriv = &pHalData->odmpriv;
	int	UndecoratedSmoothedPWDB;
	#if (RTL8192D_EASY_SMART_CONCURRENT == 1)
	PADAPTER	BuddyAdapter = Adapter->BuddyAdapter;
	BOOLEAN		bGetValueFromBuddyAdapter = DualMacGetParameterFromBuddyAdapter(Adapter);
	u8		HighPowerLvlBackForMac0 = TxHighPwrLevel_Level1;
	#endif

	// If dynamic high power is disabled.
	if( (pdmpriv->bDynamicTxPowerEnable != _TRUE) ||
		(!(podmpriv->SupportAbility& ODM_BB_DYNAMIC_TXPWR)) )
	{
		pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
		return;
	}

	// STA not connected and AP not connected
	if((check_fwstate(pmlmepriv, _FW_LINKED) != _TRUE) &&	
		(pdmpriv->EntryMinUndecoratedSmoothedPWDB == 0))
	{
		//ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("Not connected to any \n"));
		pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
		//the LastDTPlvl should reset when disconnect, 
		//otherwise the tx power level wouldn't change when disconnect and connect again.
		// Maddest 20091220.
		pdmpriv->LastDTPLvl=TxHighPwrLevel_Normal;
		return;
	}
		
	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)	// Default port
	{
	#if 0
		//todo: AP Mode
		if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE) ||
	       (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE))
		{
			UndecoratedSmoothedPWDB = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
			//ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("AP Client PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
		}
		else
		{
			UndecoratedSmoothedPWDB = pdmpriv->UndecoratedSmoothedPWDB;
			//ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("STA Default Port PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
		}
	#else
	UndecoratedSmoothedPWDB = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
	#endif
	}
	else // associated entry pwdb
	{	
		UndecoratedSmoothedPWDB = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
		//ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("AP Ext Port PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
	}
#if TX_POWER_FOR_5G_BAND == 1
	if(pHalData->CurrentBandType92D == BAND_ON_5G){
		if(UndecoratedSmoothedPWDB >= 0x33)
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Level2;
			//ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("5G:TxHighPwrLevel_Level2 (TxPwr=0x0)\n"));
		}
		else if((UndecoratedSmoothedPWDB <0x33) &&
			(UndecoratedSmoothedPWDB >= 0x2b) )
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Level1;
			//ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("5G:TxHighPwrLevel_Level1 (TxPwr=0x10)\n"));
		}
		else if(UndecoratedSmoothedPWDB < 0x2b)
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
			//ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("5G:TxHighPwrLevel_Normal\n"));
		}
	}
	else
#endif
	{
		if(UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL2)
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Level2;
			//ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_Level1 (TxPwr=0x0)\n"));
		}
		else if((UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL2-3)) &&
			(UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL1) )
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Level1;
			//ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_Level1 (TxPwr=0x10)\n"));
		}
		else if(UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL1-5))
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
			//ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_Normal\n"));
		}
	}
#if (RTL8192D_EASY_SMART_CONCURRENT == 1)
	if(bGetValueFromBuddyAdapter)
	{
		//ODM_RT_TRACE(pDM_Odm,COMP_MLME,DBG_LOUD,("dm_DynamicTxPower() mac 0 for mac 1 \n"));
		if(Adapter->DualMacDMSPControl.bChangeTxHighPowerLvlForAnotherMacOfDMSP)
		{
			//ODM_RT_TRACE(pDM_Odm,COMP_MLME,DBG_LOUD,("dm_DynamicTxPower() change value \n"));
			HighPowerLvlBackForMac0 = pHalData->DynamicTxHighPowerLvl;
			pHalData->DynamicTxHighPowerLvl = Adapter->DualMacDMSPControl.CurTxHighLvlForAnotherMacOfDMSP;
			PHY_SetTxPowerLevel8192D(Adapter, pHalData->CurrentChannel);
			pHalData->DynamicTxHighPowerLvl = HighPowerLvlBackForMac0;
			Adapter->DualMacDMSPControl.bChangeTxHighPowerLvlForAnotherMacOfDMSP = _FALSE;
		}						
	}
#endif

	if( (pdmpriv->DynamicTxHighPowerLvl != pdmpriv->LastDTPLvl) )
	{
		//ODM_RT_TRACE(pDM_Odm,COMP_HIPWR, DBG_LOUD, ("PHY_SetTxPowerLevel8192S() Channel = %d \n" , pHalData->CurrentChannel));
#if (RTL8192D_EASY_SMART_CONCURRENT == 1)
		if(BuddyAdapter == NULL)
		{
			//ODM_RT_TRACE(pDM_Odm,COMP_MLME,DBG_LOUD,("dm_DynamicTxPower() BuddyAdapter == NULL case \n"));
			if(!Adapter->bSlaveOfDMSP)
			{
				PHY_SetTxPowerLevel8192D(Adapter, pHalData->CurrentChannel);
			}
		}
		else
		{
			if(pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY)
			{
				//ODM_RT_TRACE(pDM_Odm,COMP_MLME,DBG_LOUD,("dm_DynamicTxPower() BuddyAdapter DMSP \n"));
				if(Adapter->bSlaveOfDMSP)
				{
					//ODM_RT_TRACE(pDM_Odm,COMP_MLME,DBG_LOUD,("dm_DynamicTxPower() bslave case  \n"));
					BuddyAdapter->DualMacDMSPControl.bChangeTxHighPowerLvlForAnotherMacOfDMSP = _TRUE;
					BuddyAdapter->DualMacDMSPControl.CurTxHighLvlForAnotherMacOfDMSP = pHalData->DynamicTxHighPowerLvl;
				}
				else
				{
					//ODM_RT_TRACE(pDM_Odm,COMP_MLME,DBG_LOUD,("dm_DynamicTxPower() master case  \n"));					
					if(!bGetValueFromBuddyAdapter)
					{
						//ODM_RT_TRACE(pDM_Odm,COMP_MLME,DBG_LOUD,("dm_DynamicTxPower() mac 0 for mac 0 \n"));
						PHY_SetTxPowerLevel8192D(Adapter, pHalData->CurrentChannel);
					}
				}
			}
			else
			{
				//ODM_RT_TRACE(pDM_Odm,COMP_MLME,DBG_LOUD,("dm_DynamicTxPower() BuddyAdapter DMDP\n"));
				PHY_SetTxPowerLevel8192D(Adapter, pHalData->CurrentChannel);
			}
		}
#else
		PHY_SetTxPowerLevel8192D(Adapter, pHalData->CurrentChannel);
#endif
	}
	pdmpriv->LastDTPLvl = pdmpriv->DynamicTxHighPowerLvl;
#endif	
#endif	// #if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

}


//3============================================================
//3 RSSI Monitor
//3============================================================

VOID
odm_RSSIDumpToRegister(
	IN	PDM_ODM_T	pDM_Odm
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER		Adapter = pDM_Odm->Adapter;

	if(pDM_Odm->SupportICType == ODM_RTL8812)
	{
		PlatformEFIOWrite1Byte(Adapter, rA_RSSIDump_Jaguar, Adapter->RxStats.RxRSSIPercentage[0]);
		PlatformEFIOWrite1Byte(Adapter, rB_RSSIDump_Jaguar, Adapter->RxStats.RxRSSIPercentage[1]);

		// Rx EVM
		PlatformEFIOWrite1Byte(Adapter, rS1_RXevmDump_Jaguar, Adapter->RxStats.RxEVMdbm[0]);
		PlatformEFIOWrite1Byte(Adapter, rS2_RXevmDump_Jaguar, Adapter->RxStats.RxEVMdbm[1]);

		// Rx SNR
		PlatformEFIOWrite1Byte(Adapter, rA_RXsnrDump_Jaguar, (u1Byte)(Adapter->RxStats.RxSNRdB[0]));
		PlatformEFIOWrite1Byte(Adapter, rB_RXsnrDump_Jaguar, (u1Byte)(Adapter->RxStats.RxSNRdB[1]));

		// Rx Cfo_Short
		PlatformEFIOWrite2Byte(Adapter, rA_CfoShortDump_Jaguar, Adapter->RxStats.RxCfoShort[0]);
		PlatformEFIOWrite2Byte(Adapter, rB_CfoShortDump_Jaguar, Adapter->RxStats.RxCfoShort[1]);

		// Rx Cfo_Tail
		PlatformEFIOWrite2Byte(Adapter, rA_CfoLongDump_Jaguar, Adapter->RxStats.RxCfoTail[0]);
		PlatformEFIOWrite2Byte(Adapter, rB_CfoLongDump_Jaguar, Adapter->RxStats.RxCfoTail[1]);
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8192E)
	{
		PlatformEFIOWrite1Byte(Adapter, rA_RSSIDump_92E, Adapter->RxStats.RxRSSIPercentage[0]);
		PlatformEFIOWrite1Byte(Adapter, rB_RSSIDump_92E, Adapter->RxStats.RxRSSIPercentage[1]);
		// Rx EVM
		PlatformEFIOWrite1Byte(Adapter, rS1_RXevmDump_92E, Adapter->RxStats.RxEVMdbm[0]);
		PlatformEFIOWrite1Byte(Adapter, rS2_RXevmDump_92E, Adapter->RxStats.RxEVMdbm[1]);
		// Rx SNR
		PlatformEFIOWrite1Byte(Adapter, rA_RXsnrDump_92E, (u1Byte)(Adapter->RxStats.RxSNRdB[0]));
		PlatformEFIOWrite1Byte(Adapter, rB_RXsnrDump_92E, (u1Byte)(Adapter->RxStats.RxSNRdB[1]));
		// Rx Cfo_Short
		PlatformEFIOWrite2Byte(Adapter, rA_CfoShortDump_92E, Adapter->RxStats.RxCfoShort[0]);
		PlatformEFIOWrite2Byte(Adapter, rB_CfoShortDump_92E, Adapter->RxStats.RxCfoShort[1]);
		// Rx Cfo_Tail
		PlatformEFIOWrite2Byte(Adapter, rA_CfoLongDump_92E, Adapter->RxStats.RxCfoTail[0]);
		PlatformEFIOWrite2Byte(Adapter, rB_CfoLongDump_92E, Adapter->RxStats.RxCfoTail[1]);
	 }
#endif
}


VOID
odm_RSSIMonitorInit(
	IN	PDM_ODM_T	pDM_Odm
	)
{
	pRA_T		pRA_Table = &pDM_Odm->DM_RA_Table;

   	pRA_Table->firstconnect = FALSE;

}

VOID
odm_RSSIMonitorCheck(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	// 
	// For AP/ADSL use prtl8192cd_priv
	// For CE/NIC use PADAPTER
	//

	if (!(pDM_Odm->SupportAbility & ODM_BB_RSSI_MONITOR))
		return;
	
	//
	// 2011/09/29 MH In HW integration first stage, we provide 4 different handle to operate
	// at the same time. In the stage2/3, we need to prive universal interface and merge all
	// HW dynamic mechanism.
	//
	switch	(pDM_Odm->SupportPlatform)
	{
		case	ODM_WIN:
			odm_RSSIMonitorCheckMP(pDM_Odm);
			break;

		case	ODM_CE:
			odm_RSSIMonitorCheckCE(pDM_Odm);
			break;

		case	ODM_AP:
			odm_RSSIMonitorCheckAP(pDM_Odm);
			break;		

		case	ODM_ADSL:
			//odm_DIGAP(pDM_Odm);
			break;	
	}
	
}	// odm_RSSIMonitorCheck


VOID
odm_RSSIMonitorCheckMP(
	IN	PDM_ODM_T	pDM_Odm
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PRT_WLAN_STA	pEntry;
	u1Byte			i;
	s4Byte			tmpEntryMaxPWDB=0, tmpEntryMinPWDB=0xff;
	u1Byte			H2C_Parameter[4] ={0};
	PMGNT_INFO		pMgntInfo = &Adapter->MgntInfo;
	u8Byte			curTxOkCnt = 0, curRxOkCnt = 0;	
	u1Byte			STBC_TX = 0;
	BOOLEAN			FirstConnect;                                                    
	pRA_T			pRA_Table = &pDM_Odm->DM_RA_Table;      
#if (BEAMFORMING_SUPPORT == 1)	
	BEAMFORMING_CAP Beamform_cap = BEAMFORMING_CAP_NONE;
	u1Byte			TxBF_EN = 0;
#endif

	RT_DISP(FDM, DM_PWDB, ("pHalData->UndecoratedSmoothedPWDB = 0x%x( %d)\n", 
		pHalData->UndecoratedSmoothedPWDB,
		pHalData->UndecoratedSmoothedPWDB));

	curTxOkCnt = Adapter->TxStats.NumTxBytesUnicast - pMgntInfo->lastTxOkCnt;
	curRxOkCnt = Adapter->RxStats.NumRxBytesUnicast - pMgntInfo->lastRxOkCnt;
	pMgntInfo->lastTxOkCnt = curTxOkCnt;
	pMgntInfo->lastRxOkCnt = curRxOkCnt;

	RT_DISP(FDM, DM_PWDB, ("Tx = %d Rx = %d\n", curTxOkCnt, curRxOkCnt));

       FirstConnect = (pHalData->bLinked) && (pRA_Table->firstconnect == FALSE);    
	pRA_Table->firstconnect = pHalData->bLinked;                                               
       H2C_Parameter[3] |= FirstConnect << 5;

	if(pDM_Odm->SupportICType == ODM_RTL8188E && (pMgntInfo->CustomerID==RT_CID_819x_HP))
	{
		if(curRxOkCnt >(curTxOkCnt*6))
			PlatformEFIOWrite4Byte(Adapter, REG_ARFR0, 0x8f015);
		else
			PlatformEFIOWrite4Byte(Adapter, REG_ARFR0, 0xff015);
	}	

	if(pDM_Odm->SupportICType == ODM_RTL8812 || pDM_Odm->SupportICType == ODM_RTL8821)
	{
		if(curRxOkCnt >(curTxOkCnt*6))
			H2C_Parameter[3]=0x01;
		else
			H2C_Parameter[3]=0x00;
	}

	for(i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++)
	{
		if(IsAPModeExist(Adapter)  && GetFirstExtAdapter(Adapter) != NULL)
		{
			pEntry = AsocEntry_EnumStation(GetFirstExtAdapter(Adapter), i);
		}
		else
		{
			pEntry = AsocEntry_EnumStation(GetDefaultAdapter(Adapter), i);
		}

		if(pEntry != NULL)
		{
			if(pEntry->bAssociated)
			{
			
				RT_DISP_ADDR(FDM, DM_PWDB, ("pEntry->MacAddr ="), pEntry->MacAddr);
				RT_DISP(FDM, DM_PWDB, ("pEntry->rssi = 0x%x(%d)\n", 
					pEntry->rssi_stat.UndecoratedSmoothedPWDB, pEntry->rssi_stat.UndecoratedSmoothedPWDB));

				if(pDM_Odm->SupportICType == ODM_RTL8192E || pDM_Odm->SupportICType == ODM_RTL8812)
				{

#if (BEAMFORMING_SUPPORT == 1)
					Beamform_cap = Beamforming_GetEntryBeamCapByMacId(pMgntInfo, pEntry->AssociatedMacId);
					if(Beamform_cap & (BEAMFORMER_CAP_HT_EXPLICIT |BEAMFORMER_CAP_VHT_SU))
						TxBF_EN = 1;
					else
						TxBF_EN = 0;
	
					H2C_Parameter[3] |= TxBF_EN << 6; 
					
					if(TxBF_EN)
						STBC_TX = 0;
					else
#endif
					{
						if(IS_WIRELESS_MODE_AC(Adapter))
							STBC_TX = TEST_FLAG(pEntry->VHTInfo.STBC, STBC_VHT_ENABLE_TX);
						else
							STBC_TX = TEST_FLAG(pEntry->HTInfo.STBC, STBC_HT_ENABLE_TX);
					}

					H2C_Parameter[3] |= STBC_TX << 1;
				}

				if(pEntry->rssi_stat.UndecoratedSmoothedPWDB < tmpEntryMinPWDB)
					tmpEntryMinPWDB = pEntry->rssi_stat.UndecoratedSmoothedPWDB;
				if(pEntry->rssi_stat.UndecoratedSmoothedPWDB > tmpEntryMaxPWDB)
					tmpEntryMaxPWDB = pEntry->rssi_stat.UndecoratedSmoothedPWDB;

				H2C_Parameter[2] = (u1Byte)(pEntry->rssi_stat.UndecoratedSmoothedPWDB & 0xFF);
				H2C_Parameter[1] = 0x20;   // fw v12 cmdid 5:use max macid ,for nic ,default macid is 0 ,max macid is 1
				H2C_Parameter[0] = (pEntry->AssociatedMacId);
				if(pDM_Odm->SupportICType == ODM_RTL8812)
					ODM_FillH2CCmd(Adapter, ODM_H2C_RSSI_REPORT, 4, H2C_Parameter);
				else if(pDM_Odm->SupportICType == ODM_RTL8192E)
					ODM_FillH2CCmd(Adapter, ODM_H2C_RSSI_REPORT, 4, H2C_Parameter);
				else	
					ODM_FillH2CCmd(Adapter, ODM_H2C_RSSI_REPORT, 3, H2C_Parameter);
			}
		}
		else
		{
			break;
		}
	}

	if(tmpEntryMaxPWDB != 0)	// If associated entry is found
	{
		pHalData->EntryMaxUndecoratedSmoothedPWDB = tmpEntryMaxPWDB;
		RT_DISP(FDM, DM_PWDB, ("EntryMaxPWDB = 0x%x(%d)\n",	tmpEntryMaxPWDB, tmpEntryMaxPWDB));
	}
	else
	{
		pHalData->EntryMaxUndecoratedSmoothedPWDB = 0;
	}
	
	if(tmpEntryMinPWDB != 0xff) // If associated entry is found
	{
		pHalData->EntryMinUndecoratedSmoothedPWDB = tmpEntryMinPWDB;
		RT_DISP(FDM, DM_PWDB, ("EntryMinPWDB = 0x%x(%d)\n", tmpEntryMinPWDB, tmpEntryMinPWDB));

	}
	else
	{
		pHalData->EntryMinUndecoratedSmoothedPWDB = 0;
	}

	// Indicate Rx signal strength to FW.
	if(pHalData->bUseRAMask)
	{
		if(pDM_Odm->SupportICType == ODM_RTL8192E || pDM_Odm->SupportICType == ODM_RTL8812)
		{
			PRT_HIGH_THROUGHPUT 		pHTInfo = GET_HT_INFO(pMgntInfo);
			PRT_VERY_HIGH_THROUGHPUT	pVHTInfo = GET_VHT_INFO(pMgntInfo);

#if (BEAMFORMING_SUPPORT == 1)
			
			Beamform_cap = Beamforming_GetEntryBeamCapByMacId(pMgntInfo, pMgntInfo->mMacId);

			if(Beamform_cap & (BEAMFORMER_CAP_HT_EXPLICIT |BEAMFORMER_CAP_VHT_SU))
				TxBF_EN = 1;
			else
				TxBF_EN = 0;

			H2C_Parameter[3] |= TxBF_EN << 6; 

			if(TxBF_EN)
				STBC_TX = 0;
			else
#endif
			{
				if(IS_WIRELESS_MODE_AC(Adapter))
					STBC_TX = TEST_FLAG(pVHTInfo->VhtCurStbc, STBC_VHT_ENABLE_TX);
				else
					STBC_TX = TEST_FLAG(pHTInfo->HtCurStbc, STBC_HT_ENABLE_TX);
			}

			H2C_Parameter[3] |= STBC_TX << 1;
		}
		
		H2C_Parameter[2] = (u1Byte)(pHalData->UndecoratedSmoothedPWDB & 0xFF);
		H2C_Parameter[1] = 0x20;	// fw v12 cmdid 5:use max macid ,for nic ,default macid is 0 ,max macid is 1
		H2C_Parameter[0] = 0;		// fw v12 cmdid 5:use max macid ,for nic ,default macid is 0 ,max macid is 1
		if(pDM_Odm->SupportICType == ODM_RTL8812)
			ODM_FillH2CCmd(Adapter, ODM_H2C_RSSI_REPORT, 4, H2C_Parameter);
		else  if(pDM_Odm->SupportICType == ODM_RTL8192E)
			ODM_FillH2CCmd(Adapter, ODM_H2C_RSSI_REPORT, 4, H2C_Parameter);	
		else	
			ODM_FillH2CCmd(Adapter, ODM_H2C_RSSI_REPORT, 3, H2C_Parameter);
	}
	else
	{
		PlatformEFIOWrite1Byte(Adapter, 0x4fe, (u1Byte)pHalData->UndecoratedSmoothedPWDB);
	}

	if((pDM_Odm->SupportICType == ODM_RTL8812)||(pDM_Odm->SupportICType == ODM_RTL8192E))
		odm_RSSIDumpToRegister(pDM_Odm);

	odm_FindMinimumRSSI(Adapter);
	ODM_CmnInfoUpdate(&pHalData->DM_OutSrc ,ODM_CMNINFO_LINK, (u8Byte)pHalData->bLinked);
	ODM_CmnInfoUpdate(&pHalData->DM_OutSrc ,ODM_CMNINFO_RSSI_MIN, (u8Byte)pHalData->MinUndecoratedPWDBForDM);
#endif	// #if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
}

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
//
//sherry move from DUSC to here 20110517
//
static VOID
FindMinimumRSSI_Dmsp(
	IN	PADAPTER	pAdapter
)
{
#if 0
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	s32	Rssi_val_min_back_for_mac0;
	BOOLEAN		bGetValueFromBuddyAdapter = dm_DualMacGetParameterFromBuddyAdapter(pAdapter);
	BOOLEAN		bRestoreRssi = _FALSE;
	PADAPTER	BuddyAdapter = pAdapter->BuddyAdapter;

	if(pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY)
	{
		if(BuddyAdapter!= NULL)
		{
			if(pHalData->bSlaveOfDMSP)
			{
				//ODM_RT_TRACE(pDM_Odm,COMP_EASY_CONCURRENT,DBG_LOUD,("bSlavecase of dmsp\n"));
				BuddyAdapter->DualMacDMSPControl.RssiValMinForAnotherMacOfDMSP = pdmpriv->MinUndecoratedPWDBForDM;
			}
			else
			{
				if(bGetValueFromBuddyAdapter)
				{
					//ODM_RT_TRACE(pDM_Odm,COMP_EASY_CONCURRENT,DBG_LOUD,("get new RSSI\n"));
					bRestoreRssi = _TRUE;
					Rssi_val_min_back_for_mac0 = pdmpriv->MinUndecoratedPWDBForDM;
					pdmpriv->MinUndecoratedPWDBForDM = pAdapter->DualMacDMSPControl.RssiValMinForAnotherMacOfDMSP;
				}
			}
		}
		
	}

	if(bRestoreRssi)
	{
		bRestoreRssi = _FALSE;
		pdmpriv->MinUndecoratedPWDBForDM = Rssi_val_min_back_for_mac0;
	}
#endif
}

static void
FindMinimumRSSI(
IN	PADAPTER	pAdapter
	)
{	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;	
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);

	//1 1.Determine the minimum RSSI 

	if((pDM_Odm->bLinked != _TRUE) &&
		(pdmpriv->EntryMinUndecoratedSmoothedPWDB == 0))
	{
		pdmpriv->MinUndecoratedPWDBForDM = 0;
		//ODM_RT_TRACE(pDM_Odm,COMP_BB_POWERSAVING, DBG_LOUD, ("Not connected to any \n"));
	}
	else
	{
		pdmpriv->MinUndecoratedPWDBForDM = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
	}

	//DBG_8192C("%s=>MinUndecoratedPWDBForDM(%d)\n",__FUNCTION__,pdmpriv->MinUndecoratedPWDBForDM);
	//ODM_RT_TRACE(pDM_Odm,COMP_DIG, DBG_LOUD, ("MinUndecoratedPWDBForDM =%d\n",pHalData->MinUndecoratedPWDBForDM));
}
#endif

VOID
odm_RSSIMonitorCheckCE(
	IN		PDM_ODM_T		pDM_Odm
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PADAPTER	Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(Adapter);
	int	i;
	int	tmpEntryMaxPWDB=0, tmpEntryMinPWDB=0xff;
	u8 	sta_cnt=0;
	u32	UL_DL_STATE = 0, STBC_TX = 0, TxBF_EN = 0;
	u32	PWDB_rssi[NUM_STA]={0};//[0~15]:MACID, [16~31]:PWDB_rssi
	BOOLEAN			FirstConnect = FALSE;
	pRA_T			pRA_Table = &pDM_Odm->DM_RA_Table;

	if(pDM_Odm->bLinked != _TRUE)
		return;

	#if((RTL8812A_SUPPORT==1)||(RTL8821A_SUPPORT==1))
	if((pDM_Odm->SupportICType == ODM_RTL8812)||(pDM_Odm->SupportICType == ODM_RTL8821))
	{
		u64	curTxOkCnt = pdvobjpriv->traffic_stat.cur_tx_bytes;
		u64	curRxOkCnt = pdvobjpriv->traffic_stat.cur_rx_bytes;

		if(curRxOkCnt >(curTxOkCnt*6))
			UL_DL_STATE = 1;
		else
			UL_DL_STATE = 0;
	}
	#endif

       FirstConnect = (pDM_Odm->bLinked) && (pRA_Table->firstconnect == FALSE);    
	pRA_Table->firstconnect = pDM_Odm->bLinked;

	//if(check_fwstate(&Adapter->mlmepriv, WIFI_AP_STATE|WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == _TRUE)
	{
		#if 1
		struct sta_info *psta;
		
		for(i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++) {
			if (IS_STA_VALID(psta = pDM_Odm->pODM_StaInfo[i]))
			{
                        		if(IS_MCAST( psta->hwaddr))  //if(psta->mac_id ==1)
						 continue;
								
					if(psta->rssi_stat.UndecoratedSmoothedPWDB == (-1))
						 continue;
								
					if(psta->rssi_stat.UndecoratedSmoothedPWDB < tmpEntryMinPWDB)
						tmpEntryMinPWDB = psta->rssi_stat.UndecoratedSmoothedPWDB;

					if(psta->rssi_stat.UndecoratedSmoothedPWDB > tmpEntryMaxPWDB)
						tmpEntryMaxPWDB = psta->rssi_stat.UndecoratedSmoothedPWDB;

					#if 0
					DBG_871X("%s mac_id:%u, mac:"MAC_FMT", rssi:%d\n", __func__,
						psta->mac_id, MAC_ARG(psta->hwaddr), psta->rssi_stat.UndecoratedSmoothedPWDB);
					#endif

					if(psta->rssi_stat.UndecoratedSmoothedPWDB != (-1)) {

#ifdef CONFIG_80211N_HT
						if(pDM_Odm->SupportICType == ODM_RTL8192E || pDM_Odm->SupportICType == ODM_RTL8812)
						{
#ifdef CONFIG_BEAMFORMING
							BEAMFORMING_CAP Beamform_cap = beamforming_get_entry_beam_cap_by_mac_id(&Adapter->mlmepriv, psta->mac_id);

							if(Beamform_cap & (BEAMFORMER_CAP_HT_EXPLICIT |BEAMFORMER_CAP_VHT_SU))
								TxBF_EN = 1;
							else
								TxBF_EN = 0;

							if (TxBF_EN) {
								STBC_TX = 0;
							}
							else
#endif
							{
#ifdef CONFIG_80211AC_VHT
								if(IsSupportedVHT(psta->wireless_mode))
									STBC_TX = TEST_FLAG(psta->vhtpriv.stbc_cap, STBC_VHT_ENABLE_TX);
								else	
#endif
									STBC_TX = TEST_FLAG(psta->htpriv.stbc_cap, STBC_HT_ENABLE_TX);
							}
						}
#endif

						if(pDM_Odm->SupportICType == ODM_RTL8192D)
							PWDB_rssi[sta_cnt++] = (psta->mac_id | (psta->rssi_stat.UndecoratedSmoothedPWDB<<16) | ((Adapter->stapriv.asoc_sta_count+1) << 8));
						else if ((pDM_Odm->SupportICType == ODM_RTL8192E)||(pDM_Odm->SupportICType == ODM_RTL8812)||(pDM_Odm->SupportICType == ODM_RTL8821))
							PWDB_rssi[sta_cnt++] = (((u8)(psta->mac_id&0xFF)) | ((psta->rssi_stat.UndecoratedSmoothedPWDB&0x7F)<<16) |(STBC_TX << 25) | (FirstConnect << 29) | (TxBF_EN << 30));
						else
							PWDB_rssi[sta_cnt++] = (psta->mac_id | (psta->rssi_stat.UndecoratedSmoothedPWDB<<16) );
					}
			}
		}
		#else
		_irqL irqL;
		_list	*plist, *phead;
		struct sta_info *psta;
		struct sta_priv *pstapriv = &Adapter->stapriv;
		u8 bcast_addr[ETH_ALEN]= {0xff,0xff,0xff,0xff,0xff,0xff};

		_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);

		for(i=0; i< NUM_STA; i++)
		{
			phead = &(pstapriv->sta_hash[i]);
			plist = get_next(phead);
		
			while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
			{
				psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);

				plist = get_next(plist);

				if(_rtw_memcmp(psta->hwaddr, bcast_addr, ETH_ALEN) || 
					_rtw_memcmp(psta->hwaddr, myid(&Adapter->eeprompriv), ETH_ALEN))
					continue;

				if(psta->state & WIFI_ASOC_STATE)
				{
					
					if(psta->rssi_stat.UndecoratedSmoothedPWDB < tmpEntryMinPWDB)
						tmpEntryMinPWDB = psta->rssi_stat.UndecoratedSmoothedPWDB;

					if(psta->rssi_stat.UndecoratedSmoothedPWDB > tmpEntryMaxPWDB)
						tmpEntryMaxPWDB = psta->rssi_stat.UndecoratedSmoothedPWDB;

					if(psta->rssi_stat.UndecoratedSmoothedPWDB != (-1)){
						//printk("%s==> mac_id(%d),rssi(%d)\n",__FUNCTION__,psta->mac_id,psta->rssi_stat.UndecoratedSmoothedPWDB);
						#if(RTL8192D_SUPPORT==1)
						PWDB_rssi[sta_cnt++] = (psta->mac_id | (psta->rssi_stat.UndecoratedSmoothedPWDB<<16) | ((Adapter->stapriv.asoc_sta_count+1) << 8));
						#else
						PWDB_rssi[sta_cnt++] = (psta->mac_id | (psta->rssi_stat.UndecoratedSmoothedPWDB<<16) );
						#endif
					}
				}
			
			}

		}
	
		_exit_critical_bh(&pstapriv->sta_hash_lock, &irqL);
		#endif

		//printk("%s==> sta_cnt(%d)\n",__FUNCTION__,sta_cnt);

		for(i=0; i< sta_cnt; i++)
		{
			if(PWDB_rssi[i] != (0)){
				if(pHalData->fw_ractrl == _TRUE)// Report every sta's RSSI to FW
				{
					#if(RTL8192D_SUPPORT==1)
					if(pDM_Odm->SupportICType == ODM_RTL8192D){
						FillH2CCmd92D(Adapter, H2C_RSSI_REPORT, 3, (u8 *)(&PWDB_rssi[i]));		
					}
					#endif
					
					#if((RTL8192C_SUPPORT==1)||(RTL8723A_SUPPORT==1))
					if((pDM_Odm->SupportICType == ODM_RTL8192C)||(pDM_Odm->SupportICType == ODM_RTL8723A)){
						rtl8192c_set_rssi_cmd(Adapter, (u8*)&PWDB_rssi[i]);
					}
					#endif
					
					#if((RTL8812A_SUPPORT==1)||(RTL8821A_SUPPORT==1))
					if((pDM_Odm->SupportICType == ODM_RTL8812)||(pDM_Odm->SupportICType == ODM_RTL8821)){	
						PWDB_rssi[i] |= (UL_DL_STATE << 24);
						rtl8812_set_rssi_cmd(Adapter, (u8 *)(&PWDB_rssi[i]));
					}
					#endif
					#if(RTL8192E_SUPPORT==1)
					if(pDM_Odm->SupportICType == ODM_RTL8192E){
						rtl8192e_set_rssi_cmd(Adapter, (u8 *)(&PWDB_rssi[i]));
					}
					#endif
					#if(RTL8723B_SUPPORT==1)
					if(pDM_Odm->SupportICType == ODM_RTL8723B){
						rtl8723b_set_rssi_cmd(Adapter, (u8 *)(&PWDB_rssi[i]));
					}
					#endif
				}
				else{
					#if((RTL8188E_SUPPORT==1)&&(RATE_ADAPTIVE_SUPPORT == 1))
					if(pDM_Odm->SupportICType == ODM_RTL8188E){
						ODM_RA_SetRSSI_8188E(
						&(pHalData->odmpriv), (PWDB_rssi[i]&0xFF), (u8)((PWDB_rssi[i]>>16) & 0xFF));
					}
					#endif
				}
			}
		}		
	}



	if(tmpEntryMaxPWDB != 0)	// If associated entry is found
	{
		pdmpriv->EntryMaxUndecoratedSmoothedPWDB = tmpEntryMaxPWDB;		
	}
	else
	{
		pdmpriv->EntryMaxUndecoratedSmoothedPWDB = 0;
	}

	if(tmpEntryMinPWDB != 0xff) // If associated entry is found
	{
		pdmpriv->EntryMinUndecoratedSmoothedPWDB = tmpEntryMinPWDB;		
	}
	else
	{
		pdmpriv->EntryMinUndecoratedSmoothedPWDB = 0;
	}

	FindMinimumRSSI(Adapter);//get pdmpriv->MinUndecoratedPWDBForDM

	#if(RTL8192D_SUPPORT==1)
	FindMinimumRSSI_Dmsp(Adapter);
	#endif
	pDM_Odm->RSSI_Min = pdmpriv->MinUndecoratedPWDBForDM;
	//ODM_CmnInfoUpdate(&pHalData->odmpriv ,ODM_CMNINFO_RSSI_MIN, pdmpriv->MinUndecoratedPWDBForDM);
#endif//if (DM_ODM_SUPPORT_TYPE == ODM_CE)
}
VOID
odm_RSSIMonitorCheckAP(
	IN		PDM_ODM_T		pDM_Odm
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
#ifdef CONFIG_RTL_92C_SUPPORT || defined(CONFIG_RTL_92D_SUPPORT)

	u4Byte i;
	PSTA_INFO_T pstat;

	for(i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
	{
		pstat = pDM_Odm->pODM_StaInfo[i];
		if(IS_STA_VALID(pstat) )
		{			
#ifdef STA_EXT
			if (REMAP_AID(pstat) < (FW_NUM_STAT - 1))
#endif
				add_update_rssi(pDM_Odm->priv, pstat);

		}		
	}
#endif
#endif

}



VOID
ODM_InitAllTimers(
	IN PDM_ODM_T	pDM_Odm 
	)
{
#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))
#if (RTL8723B_SUPPORT == 1)||(RTL8821A_SUPPORT == 1)
	ODM_InitializeTimer(pDM_Odm,&pDM_Odm->DM_SWAT_Table.SwAntennaSwitchTimer_8723B,
	(RT_TIMER_CALL_BACK)ODM_SW_AntDiv_Callback, NULL, "SwAntennaSwitchTimer_8723B");
#endif
#endif

#if(defined(CONFIG_SW_ANTENNA_DIVERSITY))
	ODM_InitializeTimer(pDM_Odm,&pDM_Odm->DM_SWAT_Table.SwAntennaSwitchTimer,
		(RT_TIMER_CALL_BACK)odm_SwAntDivChkAntSwitchCallback, NULL, "SwAntennaSwitchTimer");
#endif
	
#if (!(DM_ODM_SUPPORT_TYPE == ODM_CE))
#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))
#if (RTL8188E_SUPPORT == 1)
	ODM_InitializeTimer(pDM_Odm,&pDM_Odm->FastAntTrainingTimer,
		(RT_TIMER_CALL_BACK)odm_FastAntTrainingCallback, NULL, "FastAntTrainingTimer");
#endif
#endif
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	ODM_InitializeTimer(pDM_Odm, &pDM_Odm->PSDTimer, 
		(RT_TIMER_CALL_BACK)dm_PSDMonitorCallback, NULL, "PSDTimer");
	//
	//Path Diversity
	//Neil Chen--2011--06--16--  / 2012/02/23 MH Revise Arch.
	//
	ODM_InitializeTimer(pDM_Odm, &pDM_Odm->PathDivSwitchTimer, 
		(RT_TIMER_CALL_BACK)odm_PathDivChkAntSwitchCallback, NULL, "PathDivTimer");

	ODM_InitializeTimer(pDM_Odm, &pDM_Odm->CCKPathDiversityTimer, 
		(RT_TIMER_CALL_BACK)odm_CCKTXPathDiversityCallback, NULL, "CCKPathDiversityTimer");

	ODM_InitializeTimer(pDM_Odm, &pDM_Odm->MPT_DIGTimer, 
		(RT_TIMER_CALL_BACK)odm_MPT_DIGCallback, NULL, "MPT_DIGTimer");

	ODM_InitializeTimer(pDM_Odm, &pDM_Odm->DM_RXHP_Table.PSDTimer,
		(RT_TIMER_CALL_BACK)odm_PSD_RXHPCallback, NULL, "PSDRXHPTimer");  
#endif	
}

VOID
ODM_CancelAllTimers(
	IN PDM_ODM_T	pDM_Odm 
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	//
	// 2012/01/12 MH Temp BSOD fix. We need to find NIC allocate mem fail reason in 
	// win7 platform.
	//
	HAL_ADAPTER_STS_CHK(pDM_Odm)
#endif	
#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))
#if (RTL8723B_SUPPORT == 1)||(RTL8821A_SUPPORT == 1)
	ODM_CancelTimer(pDM_Odm,&pDM_Odm->DM_SWAT_Table.SwAntennaSwitchTimer_8723B);
#endif
#endif

#if(defined(CONFIG_SW_ANTENNA_DIVERSITY))
	ODM_CancelTimer(pDM_Odm,&pDM_Odm->DM_SWAT_Table.SwAntennaSwitchTimer);
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

#if (RTL8188E_SUPPORT == 1)
	ODM_CancelTimer(pDM_Odm,&pDM_Odm->FastAntTrainingTimer);
#endif
	ODM_CancelTimer(pDM_Odm, &pDM_Odm->PSDTimer);	
	//
	//Path Diversity
	//Neil Chen--2011--06--16--  / 2012/02/23 MH Revise Arch.
	//
	ODM_CancelTimer(pDM_Odm, &pDM_Odm->PathDivSwitchTimer);

	ODM_CancelTimer(pDM_Odm, &pDM_Odm->CCKPathDiversityTimer);

	ODM_CancelTimer(pDM_Odm, &pDM_Odm->MPT_DIGTimer);

	ODM_CancelTimer(pDM_Odm, &pDM_Odm->DM_RXHP_Table.PSDTimer);
#endif	
}


VOID
ODM_ReleaseAllTimers(
	IN PDM_ODM_T	pDM_Odm 
	)
{
#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))
#if (RTL8723B_SUPPORT == 1)||(RTL8821A_SUPPORT == 1)
	ODM_ReleaseTimer(pDM_Odm,&pDM_Odm->DM_SWAT_Table.SwAntennaSwitchTimer_8723B);
#endif
#endif

#if(defined(CONFIG_SW_ANTENNA_DIVERSITY))
	ODM_ReleaseTimer(pDM_Odm,&pDM_Odm->DM_SWAT_Table.SwAntennaSwitchTimer);
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

#if (RTL8188E_SUPPORT == 1)
	ODM_ReleaseTimer(pDM_Odm,&pDM_Odm->FastAntTrainingTimer);
#endif

	ODM_ReleaseTimer(pDM_Odm, &pDM_Odm->PSDTimer);
	//
	//Path Diversity
	//Neil Chen--2011--06--16--  / 2012/02/23 MH Revise Arch.
	//
	ODM_ReleaseTimer(pDM_Odm, &pDM_Odm->PathDivSwitchTimer);

	ODM_ReleaseTimer(pDM_Odm, &pDM_Odm->CCKPathDiversityTimer);

	ODM_ReleaseTimer(pDM_Odm, &pDM_Odm->MPT_DIGTimer);

	ODM_ReleaseTimer(pDM_Odm, &pDM_Odm->DM_RXHP_Table.PSDTimer); 
#endif	
}


//3============================================================
//3 Tx Power Tracking
//3============================================================

VOID
odm_IQCalibrate(
		IN	PDM_ODM_T	pDM_Odm 
		)
{
	PADAPTER	Adapter = pDM_Odm->Adapter;
	
	if(!IS_HARDWARE_TYPE_JAGUAR(Adapter))
		return;
	else if(IS_HARDWARE_TYPE_8812AU(Adapter))
		return;
#if (RTL8821A_SUPPORT == 1)
	if(pDM_Odm->bLinked)
	{
		if((*pDM_Odm->pChannel != pDM_Odm->preChannel) && (!*pDM_Odm->pbScanInProcess))
		{
			pDM_Odm->preChannel = *pDM_Odm->pChannel;
			pDM_Odm->LinkedInterval = 0;
		}

		if(pDM_Odm->LinkedInterval < 3)
			pDM_Odm->LinkedInterval++;
		
		if(pDM_Odm->LinkedInterval == 2)
		{
			// Mark out IQK flow to prevent tx stuck. by Maddest 20130306
			// Open it verified by James 20130715
			PHY_IQCalibrate_8821A(Adapter, FALSE);
		}
	}
	else
		pDM_Odm->LinkedInterval = 0;
#endif
}


VOID
odm_TXPowerTrackingInit(
	IN	PDM_ODM_T	pDM_Odm 
	)
{
	odm_TXPowerTrackingThermalMeterInit(pDM_Odm);
}	

u1Byte 
getSwingIndex(
	IN	PDM_ODM_T	pDM_Odm 
	)
{
	PADAPTER		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u1Byte 			i = 0;
	u4Byte 			bbSwing;
	u4Byte 			swingTableSize;
	pu4Byte 			pSwingTable;

	if (pDM_Odm->SupportICType == ODM_RTL8188E || pDM_Odm->SupportICType == ODM_RTL8723B ||
		pDM_Odm->SupportICType == ODM_RTL8192E) 
	{
		bbSwing = PHY_QueryBBReg(Adapter, rOFDM0_XATxIQImbalance, 0xFFC00000);

		pSwingTable = OFDMSwingTable_New;
		swingTableSize = OFDM_TABLE_SIZE;
	} else {
#if ((RTL8812A_SUPPORT==1)||(RTL8821A_SUPPORT==1))
		if (pDM_Odm->SupportICType == ODM_RTL8812 || pDM_Odm->SupportICType == ODM_RTL8821)
		{
			bbSwing = PHY_GetTxBBSwing_8812A(Adapter, pHalData->CurrentBandType, ODM_RF_PATH_A);
			pSwingTable = TxScalingTable_Jaguar;
			swingTableSize = TXSCALE_TABLE_SIZE;
		}
		else
#endif
		{
			bbSwing = 0;
			pSwingTable = OFDMSwingTable;
			swingTableSize = OFDM_TABLE_SIZE;
		}
	}

	for (i = 0; i < swingTableSize; ++i) {
		u4Byte tableValue = pSwingTable[i];
		
		if (tableValue >= 0x100000 )
			tableValue >>= 22;
		if (bbSwing == tableValue)
			break;
	}
	return i;
}

VOID
odm_TXPowerTrackingThermalMeterInit(
	IN	PDM_ODM_T	pDM_Odm 
	)
{
	u1Byte defaultSwingIndex = getSwingIndex(pDM_Odm);
	u1Byte 			p = 0;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	#if	MP_DRIVER != 1					//for mp driver, turn off txpwrtracking as default
	pHalData->TxPowerTrackControl = TRUE;		
	#endif
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PADAPTER			Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);


	if (pDM_Odm->SupportICType >= ODM_RTL8188E) 
	{
		pDM_Odm->RFCalibrateInfo.bTXPowerTracking = _TRUE;
		pDM_Odm->RFCalibrateInfo.TXPowercount = 0;
		pDM_Odm->RFCalibrateInfo.bTXPowerTrackingInit = _FALSE;
		//#if	(MP_DRIVER != 1)		//for mp driver, turn off txpwrtracking as default
		if ( *(pDM_Odm->mp_mode) != 1)
			pDM_Odm->RFCalibrateInfo.TxPowerTrackControl = _TRUE;
		//#endif//#if	(MP_DRIVER != 1)
		MSG_8192C("pDM_Odm TxPowerTrackControl = %d\n", pDM_Odm->RFCalibrateInfo.TxPowerTrackControl);
	}
	else
	{
		struct dm_priv	*pdmpriv = &pHalData->dmpriv;

		pdmpriv->bTXPowerTracking = _TRUE;
		pdmpriv->TXPowercount = 0;
		pdmpriv->bTXPowerTrackingInit = _FALSE;
		//#if	(MP_DRIVER != 1)		//for mp driver, turn off txpwrtracking as default

		if (*(pDM_Odm->mp_mode) != 1)
			pdmpriv->TxPowerTrackControl = _TRUE;
		//#endif//#if	(MP_DRIVER != 1)

		//MSG_8192C("pdmpriv->TxPowerTrackControl = %d\n", pdmpriv->TxPowerTrackControl);
	}
	
#elif (DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
	#ifdef RTL8188E_SUPPORT
	{
		pDM_Odm->RFCalibrateInfo.bTXPowerTracking = _TRUE;
		pDM_Odm->RFCalibrateInfo.TXPowercount = 0;
		pDM_Odm->RFCalibrateInfo.bTXPowerTrackingInit = _FALSE;
		pDM_Odm->RFCalibrateInfo.TxPowerTrackControl = _TRUE;
	}
	#endif
#endif

	pDM_Odm->RFCalibrateInfo.TxPowerTrackControl = TRUE;
	pDM_Odm->RFCalibrateInfo.ThermalValue = pHalData->EEPROMThermalMeter;
	pDM_Odm->RFCalibrateInfo.ThermalValue_IQK = pHalData->EEPROMThermalMeter;
	pDM_Odm->RFCalibrateInfo.ThermalValue_LCK = pHalData->EEPROMThermalMeter;	

	// The index of "0 dB" in SwingTable.
	if (pDM_Odm->SupportICType == ODM_RTL8188E || pDM_Odm->SupportICType == ODM_RTL8723B ||
		pDM_Odm->SupportICType == ODM_RTL8192E) 
	{
		pDM_Odm->DefaultOfdmIndex = (defaultSwingIndex >= OFDM_TABLE_SIZE) ? 30 : defaultSwingIndex;
		pDM_Odm->DefaultCckIndex = 20;	
	}
	else
	{
		pDM_Odm->DefaultOfdmIndex = (defaultSwingIndex >= TXSCALE_TABLE_SIZE) ? 24 : defaultSwingIndex;
		pDM_Odm->DefaultCckIndex = 24;	
	}

	pDM_Odm->BbSwingIdxCckBase = pDM_Odm->DefaultCckIndex;
	pDM_Odm->RFCalibrateInfo.CCK_index = pDM_Odm->DefaultCckIndex;
	
	for (p = ODM_RF_PATH_A; p < MAX_RF_PATH; ++p)
	{
		pDM_Odm->BbSwingIdxOfdmBase[p] = pDM_Odm->DefaultOfdmIndex;		
	   	pDM_Odm->RFCalibrateInfo.OFDM_index[p] = pDM_Odm->DefaultOfdmIndex;		
		pDM_Odm->RFCalibrateInfo.DeltaPowerIndex[p] = 0;
		pDM_Odm->RFCalibrateInfo.DeltaPowerIndexLast[p] = 0;
		pDM_Odm->RFCalibrateInfo.PowerIndexOffset[p] = 0;
	}

}


VOID
ODM_TXPowerTrackingCheck(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	//
	// 2011/09/29 MH In HW integration first stage, we provide 4 different handle to operate
	// at the same time. In the stage2/3, we need to prive universal interface and merge all
	// HW dynamic mechanism.
	//
	switch	(pDM_Odm->SupportPlatform)
	{
		case	ODM_WIN:
			odm_TXPowerTrackingCheckMP(pDM_Odm);
			break;

		case	ODM_CE:
			odm_TXPowerTrackingCheckCE(pDM_Odm);
			break;

		case	ODM_AP:
			odm_TXPowerTrackingCheckAP(pDM_Odm);		
			break;		

		case	ODM_ADSL:
			//odm_DIGAP(pDM_Odm);
			break;	
	}

}

VOID
odm_TXPowerTrackingCheckCE(
	IN		PDM_ODM_T		pDM_Odm 
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PADAPTER	Adapter = pDM_Odm->Adapter;
	#if( (RTL8192C_SUPPORT==1) ||  (RTL8723A_SUPPORT==1) )
	if(IS_HARDWARE_TYPE_8192C(Adapter)){
		rtl8192c_odm_CheckTXPowerTracking(Adapter);
		return;
	}
	#endif

	#if (RTL8192D_SUPPORT==1) 
	if(IS_HARDWARE_TYPE_8192D(Adapter)){	
		#if (RTL8192D_EASY_SMART_CONCURRENT == 1)
		if(!Adapter->bSlaveOfDMSP)
		#endif
			rtl8192d_odm_CheckTXPowerTracking(Adapter);
		return;	
	}
	#endif

	#if(((RTL8188E_SUPPORT==1) ||  (RTL8812A_SUPPORT==1) ||  (RTL8821A_SUPPORT==1) ||  (RTL8192E_SUPPORT==1)  ||  (RTL8723B_SUPPORT==1)  ))
	if(!(pDM_Odm->SupportAbility & ODM_RF_TX_PWR_TRACK))
	{
		return;
	}

	if(!pDM_Odm->RFCalibrateInfo.TM_Trigger)		//at least delay 1 sec
	{
		//pHalData->TxPowerCheckCnt++;	//cosa add for debug
		if(IS_HARDWARE_TYPE_8188E(Adapter) || IS_HARDWARE_TYPE_JAGUAR(Adapter) || IS_HARDWARE_TYPE_8192E(Adapter)||IS_HARDWARE_TYPE_8723B(Adapter))
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_T_METER_NEW, (BIT17 | BIT16), 0x03);
		else
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_T_METER_OLD, bRFRegOffsetMask, 0x60);
		
		//DBG_871X("Trigger Thermal Meter!!\n");
		
		pDM_Odm->RFCalibrateInfo.TM_Trigger = 1;
		return;
	}
	else
	{
		//DBG_871X("Schedule TxPowerTracking direct call!!\n");
		ODM_TXPowerTrackingCallback_ThermalMeter(Adapter);
		pDM_Odm->RFCalibrateInfo.TM_Trigger = 0;
	}
	#endif
#endif	
}

VOID
odm_TXPowerTrackingCheckMP(
	IN		PDM_ODM_T		pDM_Odm 
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PADAPTER	Adapter = pDM_Odm->Adapter;

	if (ODM_CheckPowerStatus(Adapter) == FALSE) 
	{
		RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD, ("===>ODM_CheckPowerStatus() return FALSE\n"));
		return;
	}

	if(IS_HARDWARE_TYPE_8723A(Adapter))
		return;

	if(!Adapter->bSlaveOfDMSP || Adapter->DualMacSmartConcurrent == FALSE)
		odm_TXPowerTrackingThermalMeterCheck(Adapter);
	else {
		RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD, ("!Adapter->bSlaveOfDMSP || Adapter->DualMacSmartConcurrent == FALSE\n"));
	}
#endif
	
}


VOID
odm_TXPowerTrackingCheckAP(
	IN		PDM_ODM_T		pDM_Odm
	)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	prtl8192cd_priv	priv		= pDM_Odm->priv;

	if ( (priv->pmib->dot11RFEntry.ther) && ((priv->up_time % priv->pshare->rf_ft_var.tpt_period) == 0)){
#ifdef CONFIG_RTL_92D_SUPPORT
		if (GET_CHIP_VER(priv)==VERSION_8192D){
			tx_power_tracking_92D(priv);
		} else 
#endif
		{
#ifdef CONFIG_RTL_92C_SUPPORT			
			tx_power_tracking(priv);
#endif
		}
	}
#endif	

}



//antenna mapping info
// 1: right-side antenna
// 2/0: left-side antenna
//PDM_SWAT_Table->CCK_Ant1_Cnt /OFDM_Ant1_Cnt:  for right-side antenna:   Ant:1    RxDefaultAnt1
//PDM_SWAT_Table->CCK_Ant2_Cnt /OFDM_Ant2_Cnt:  for left-side antenna:     Ant:0    RxDefaultAnt2
// We select left antenna as default antenna in initial process, modify it as needed
//

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

VOID
odm_TXPowerTrackingThermalMeterCheck(
	IN	PADAPTER		Adapter
	)
{
#ifndef AP_BUILD_WORKAROUND
	static u1Byte			TM_Trigger = 0;

	if(!(GET_HAL_DATA(Adapter)->DM_OutSrc.SupportAbility & ODM_RF_TX_PWR_TRACK))
	{
		RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,
			("===>odm_TXPowerTrackingThermalMeterCheck(),pMgntInfo->bTXPowerTracking is FALSE, return!!\n"));
		return;
	}

	if(!TM_Trigger)		//at least delay 1 sec
	{
		if(IS_HARDWARE_TYPE_8192D(Adapter))
			PHY_SetRFReg(Adapter, ODM_RF_PATH_A, RF_T_METER_92D, BIT17 | BIT16, 0x03);
		else if(IS_HARDWARE_TYPE_8188E(Adapter) || IS_HARDWARE_TYPE_JAGUAR(Adapter) || IS_HARDWARE_TYPE_8192E(Adapter) ||
			    IS_HARDWARE_TYPE_8723B(Adapter))
			PHY_SetRFReg(Adapter, ODM_RF_PATH_A, RF_T_METER_88E, BIT17 | BIT16, 0x03);
		else
			PHY_SetRFReg(Adapter, ODM_RF_PATH_A, RF_T_METER, bRFRegOffsetMask, 0x60);
		
		RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Trigger Thermal Meter!!\n"));
		
		TM_Trigger = 1;
		return;
	}
	else
	{
		RT_TRACE(COMP_POWER_TRACKING, DBG_LOUD,("Schedule TxPowerTracking direct call!!\n"));		
		odm_TXPowerTrackingDirectCall(Adapter); //Using direct call is instead, added by Roger, 2009.06.18.
		TM_Trigger = 0;
	}
#endif
}

// Only for 8723A SW ANT DIV INIT--2012--07--17
VOID
odm_SwAntDivInit_NIC_8723A(
	IN	PDM_ODM_T		pDM_Odm)
{
	pSWAT_T		pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	PADAPTER		Adapter = pDM_Odm->Adapter;
	
	u1Byte 			btAntNum=BT_GetPgAntNum(Adapter);

	if(IS_HARDWARE_TYPE_8723A(Adapter))
	{
		pDM_SWAT_Table->ANTA_ON =TRUE;
		
		// Set default antenna B status by PG
		if(btAntNum == 2)
			pDM_SWAT_Table->ANTB_ON = TRUE;
		else if(btAntNum == 1)
			pDM_SWAT_Table->ANTB_ON = FALSE;
		else
			pDM_SWAT_Table->ANTB_ON = TRUE;
	}	
	
}

#endif //end #ifMP



//3============================================================
//3 SW Antenna Diversity
//3============================================================
#if(defined(CONFIG_SW_ANTENNA_DIVERSITY))
VOID
odm_SwAntDivInit(
	IN		PDM_ODM_T		pDM_Odm
	)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	odm_SwAntDivInit_NIC(pDM_Odm);
#elif(DM_ODM_SUPPORT_TYPE == ODM_AP)
	dm_SW_AntennaSwitchInit(pDM_Odm->priv);
#endif
}

VOID
odm_SwAntDivInit_NIC(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	pSWAT_T		pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;


// Init SW ANT DIV mechanism for 8723AE/AU/AS
// Neil Chen--2012--07--17---
// CE/AP/ADSL no using SW ANT DIV for 8723A Series IC
//#if (DM_ODM_SUPPORT_TYPE==ODM_WIN)
#if (RTL8723A_SUPPORT==1) 
	if(pDM_Odm->SupportICType == ODM_RTL8723A)
	{
		odm_SwAntDivInit_NIC_8723A(pDM_Odm);	
	}	
#endif
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("SWAS:Init SW Antenna Switch\n"));
	pDM_SWAT_Table->RSSI_sum_A = 0;
	pDM_SWAT_Table->RSSI_cnt_A = 0;
	pDM_SWAT_Table->RSSI_sum_B = 0;
	pDM_SWAT_Table->RSSI_cnt_B = 0;
	pDM_SWAT_Table->CurAntenna = MAIN_ANT;
	pDM_SWAT_Table->PreAntenna = MAIN_ANT;
	pDM_SWAT_Table->try_flag = 0xff;
	pDM_SWAT_Table->PreRSSI = 0;
	pDM_SWAT_Table->SWAS_NoLink_State = 0;
	pDM_SWAT_Table->bTriggerAntennaSwitch = 0;
	pDM_SWAT_Table->SelectAntennaMap=0xAA;
	pDM_SWAT_Table->lastTxOkCnt = 0;
	pDM_SWAT_Table->lastRxOkCnt = 0;
	pDM_SWAT_Table->TXByteCnt_A = 0;
	pDM_SWAT_Table->TXByteCnt_B = 0;
	pDM_SWAT_Table->RXByteCnt_A = 0;
	pDM_SWAT_Table->RXByteCnt_B = 0;
	pDM_SWAT_Table->TrafficLoad = TRAFFIC_LOW;
	pDM_SWAT_Table->SWAS_NoLink_BK_Reg860 = ODM_Read4Byte(pDM_Odm, 0x860);
	
}

//
// 20100514 Joseph: 
// Add new function to reset the state of antenna diversity before link.
//
VOID
ODM_SwAntDivResetBeforeLink(
	IN		PDM_ODM_T		pDM_Odm
	)
{

	pSWAT_T		pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;

	pDM_SWAT_Table->SWAS_NoLink_State = 0;

}

//
// 20100514 Luke/Joseph:
// Add new function to reset antenna diversity state after link.
//
VOID
ODM_SwAntDivRestAfterLink(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	pSWAT_T		pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	pFAT_T		pDM_FatTable = &pDM_Odm->DM_FatTable;
	u4Byte             i;

	if(pDM_Odm->SupportICType == ODM_RTL8723A)
	{
	    pDM_SWAT_Table->RSSI_cnt_A = 0;
	    pDM_SWAT_Table->RSSI_cnt_B = 0;
	    pDM_Odm->RSSI_test = FALSE;
	    pDM_SWAT_Table->try_flag = 0xff;
	    pDM_SWAT_Table->RSSI_Trying = 0;
	    pDM_SWAT_Table->SelectAntennaMap=0xAA;
	
	}
	else if(pDM_Odm->SupportICType & (ODM_RTL8723B|ODM_RTL8821))
	{
		pDM_Odm->RSSI_test = FALSE;
		pDM_SWAT_Table->try_flag = 0xff;
		pDM_SWAT_Table->RSSI_Trying = 0;
		pDM_SWAT_Table->Double_chk_flag= 0;
		
		pDM_FatTable->RxIdleAnt=MAIN_ANT;
		
		for (i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
		{
			pDM_FatTable->MainAnt_Sum[i] = 0;
			pDM_FatTable->AuxAnt_Sum[i] = 0;
			pDM_FatTable->MainAnt_Cnt[i] = 0;
			pDM_FatTable->AuxAnt_Cnt[i] = 0;
		}

	}
}

void
odm_SwAntDetectInit(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	pSWAT_T		pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
#if (RTL8723B_SUPPORT == 1)
	pDM_SWAT_Table->SWAS_NoLink_BK_Reg92c = ODM_Read4Byte(pDM_Odm, rDPDT_control);
#endif
	pDM_SWAT_Table->PreAntenna = MAIN_ANT;
	pDM_SWAT_Table->CurAntenna = MAIN_ANT;
	pDM_SWAT_Table->SWAS_NoLink_State = 0;
}

VOID
ODM_SwAntDivChkPerPktRssi(
	IN PDM_ODM_T	pDM_Odm,
	IN u1Byte		StationID,
	IN PODM_PHY_INFO_T pPhyInfo
	)
{	
	SWAT_T		*pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	
	if(!(pDM_Odm->SupportAbility & (ODM_BB_ANT_DIV)))
		return;

// temporary Fix 8723A MP SW ANT DIV Bug --NeilChen--2012--07--11
#if (DM_ODM_SUPPORT_TYPE==ODM_WIN)
	if(pDM_Odm->SupportICType == ODM_RTL8723A)
	{
		//if(StationID == pDM_SWAT_Table->RSSI_target)
		//{
		//1 RSSI for SW Antenna Switch
		if(pDM_SWAT_Table->CurAntenna == MAIN_ANT)
		{
			pDM_SWAT_Table->RSSI_sum_A += pPhyInfo->RxPWDBAll;
			pDM_SWAT_Table->RSSI_cnt_A++;
		}
		else
		{
			pDM_SWAT_Table->RSSI_sum_B += pPhyInfo->RxPWDBAll;
			pDM_SWAT_Table->RSSI_cnt_B++;

		}
		//}
	}
	else
	{
		if(StationID == pDM_SWAT_Table->RSSI_target)
		{
			//1 RSSI for SW Antenna Switch
			if(pDM_SWAT_Table->CurAntenna == MAIN_ANT)
			{
				pDM_SWAT_Table->RSSI_sum_A += pPhyInfo->RxPWDBAll;
				pDM_SWAT_Table->RSSI_cnt_A++;
			}
			else
			{
				pDM_SWAT_Table->RSSI_sum_B += pPhyInfo->RxPWDBAll;
				pDM_SWAT_Table->RSSI_cnt_B++;

			}
		}
	}
#else	
	if(StationID == pDM_SWAT_Table->RSSI_target)
	{
		//1 RSSI for SW Antenna Switch
		if(pDM_SWAT_Table->CurAntenna == MAIN_ANT)
		{
			pDM_SWAT_Table->RSSI_sum_A += pPhyInfo->RxPWDBAll;
			pDM_SWAT_Table->RSSI_cnt_A++;
		}
		else
		{
			pDM_SWAT_Table->RSSI_sum_B += pPhyInfo->RxPWDBAll;
			pDM_SWAT_Table->RSSI_cnt_B++;

		}
	}
#endif
}

//
VOID
odm_SwAntDivChkAntSwitch(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u1Byte			Step
	)
{
	// 
	// For AP/ADSL use prtl8192cd_priv
	// For CE/NIC use PADAPTER
	//
	prtl8192cd_priv	priv		= pDM_Odm->priv;

	//
	// 2011/09/29 MH In HW integration first stage, we provide 4 different handle to operate
	// at the same time. In the stage2/3, we need to prive universal interface and merge all
	// HW dynamic mechanism.
	//
	switch	(pDM_Odm->SupportPlatform)
	{
		case	ODM_WIN:	
			odm_SwAntDivChkAntSwitchNIC(pDM_Odm, Step);
			break;
		case	ODM_CE:
			odm_SwAntDivChkAntSwitchNIC(pDM_Odm, Step);
			break;

		case	ODM_AP:
		case	ODM_ADSL:
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP |ODM_ADSL))
			if (priv->pshare->rf_ft_var.antSw_enable && (priv->up_time % 4==1))
				dm_SW_AntennaSwitch(priv, SWAW_STEP_PEAK);
#endif		
			break;			
	}

}

//
// 20100514 Luke/Joseph:
// Add new function for antenna diversity after link.
// This is the main function of antenna diversity after link.
// This function is called in HalDmWatchDog() and ODM_SwAntDivChkAntSwitchCallback().
// HalDmWatchDog() calls this function with SWAW_STEP_PEAK to initialize the antenna test.
// In SWAW_STEP_PEAK, another antenna and a 500ms timer will be set for testing.
// After 500ms, ODM_SwAntDivChkAntSwitchCallback() calls this function to compare the signal just
// listened on the air with the RSSI of original antenna.
// It chooses the antenna with better RSSI.
// There is also a aged policy for error trying. Each error trying will cost more 5 seconds waiting 
// penalty to get next try.


VOID
ODM_SetAntenna(
	IN 	PDM_ODM_T	pDM_Odm,
	IN	u1Byte		Antenna)
{
	ODM_SetBBReg(pDM_Odm, 0x860, BIT8|BIT9, Antenna); 
}

VOID
odm_SwAntDivChkAntSwitchNIC(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u1Byte		Step
	)
{
#if ((RTL8192C_SUPPORT==1)||(RTL8723A_SUPPORT==1))
	//PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

#if (DM_ODM_SUPPORT_TYPE==ODM_WIN)
	PADAPTER		Adapter=pDM_Odm->Adapter;
#endif

	pSWAT_T		pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	s4Byte			curRSSI=100, RSSI_A, RSSI_B;
	u1Byte			nextAntenna=AUX_ANT;
	//static u8Byte		lastTxOkCnt=0, lastRxOkCnt=0;
	u8Byte			curTxOkCnt=0, curRxOkCnt=0;
	//static u8Byte		TXByteCnt_A=0, TXByteCnt_B=0, RXByteCnt_A=0, RXByteCnt_B=0;
	u8Byte			CurByteCnt=0, PreByteCnt=0;
	//static u1Byte		TrafficLoad = TRAFFIC_LOW;
	u1Byte			Score_A=0, Score_B=0;       //A: Main; B: AUX
	u1Byte			i;

	if(!(pDM_Odm->SupportAbility & ODM_BB_ANT_DIV))
		return;

	if (pDM_Odm->SupportICType & (ODM_RTL8192D|ODM_RTL8188E))
		return;

	if((pDM_Odm->SupportICType == ODM_RTL8192C) &&(pDM_Odm->RFType == ODM_2T2R))
		return;

	if(pDM_Odm->SupportPlatform & ODM_WIN)
	{
		if(*(pDM_Odm->pAntennaTest))
			return;
	}

	if((pDM_SWAT_Table->ANTA_ON == FALSE) ||(pDM_SWAT_Table->ANTB_ON == FALSE))
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,
				("odm_SwAntDivChkAntSwitch(): No AntDiv Mechanism, Antenna A or B is off\n"));
		return;
	}

	// Radio off: Status reset to default and return.
	if(*(pDM_Odm->pbPowerSaving)==TRUE) //pHalData->eRFPowerState==eRfOff
	{
		ODM_SwAntDivRestAfterLink(pDM_Odm);
		return;
	}


	// Handling step mismatch condition.
	// Peak step is not finished at last time. Recover the variable and check again.
	if(	Step != pDM_SWAT_Table->try_flag	)
	{
		ODM_SwAntDivRestAfterLink(pDM_Odm);
	}

#if  (DM_ODM_SUPPORT_TYPE &( ODM_WIN| ODM_CE ))

	if(pDM_SWAT_Table->try_flag == 0xff)
	{
		pDM_SWAT_Table->RSSI_target = 0xff;
		
		#if(DM_ODM_SUPPORT_TYPE & ODM_CE)
		{
			u1Byte			index = 0;
			PSTA_INFO_T		pEntry = NULL;
			
			
			for(index=0; index<ODM_ASSOCIATE_ENTRY_NUM; index++)
			{					
				pEntry =  pDM_Odm->pODM_StaInfo[index];
				if(IS_STA_VALID(pEntry) ) {
					break;
				}
			}
			if(pEntry == NULL)
			{
				ODM_SwAntDivRestAfterLink(pDM_Odm);
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("odm_SwAntDivChkAntSwitch(): No Link.\n"));
				return;
			}
			else
			{
				pDM_SWAT_Table->RSSI_target = index;
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("odm_SwAntDivChkAntSwitch(): RSSI_target is PEER STA\n"));
			}
                }
		#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN) 
		{
			PADAPTER	pAdapter	 =  pDM_Odm->Adapter;
			PMGNT_INFO	pMgntInfo=&pAdapter->MgntInfo;
			
			// Select RSSI checking target
			if(pMgntInfo->mAssoc && !ACTING_AS_AP(pAdapter))
			{
				// Target: Infrastructure mode AP.
				//pDM_SWAT_Table->RSSI_target = NULL;
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("odm_SwAntDivChkAntSwitch(): RSSI_target is DEF AP!\n"));
			}
			else
			{
				u1Byte			index = 0;
				PSTA_INFO_T		pEntry = NULL;
				PADAPTER		pTargetAdapter = NULL;
			
				if(pMgntInfo->mIbss )
				{
					// Target: AP/IBSS peer.
					pTargetAdapter = pAdapter;
				}
				else
				{
					pTargetAdapter = GetFirstAPAdapter(pAdapter);
				}

				if(pTargetAdapter != NULL)
				{			
					for(index=0; index<ODM_ASSOCIATE_ENTRY_NUM; index++)
					{					
						
						pEntry = AsocEntry_EnumStation(pTargetAdapter, index);
						if(pEntry != NULL)
						{
							if(pEntry->bAssociated)
								break;			
						}
						
					}
					
				}

				if(pEntry == NULL)
				{
					ODM_SwAntDivRestAfterLink(pDM_Odm);
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("odm_SwAntDivChkAntSwitch(): No Link.\n"));
					return;
				}
				else
				{
					//pDM_SWAT_Table->RSSI_target = pEntry;
					pDM_SWAT_Table->RSSI_target = index;
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("odm_SwAntDivChkAntSwitch(): RSSI_target is PEER STA\n"));
				}
			}//end if(pMgntInfo->mAssoc && !ACTING_AS_AP(Adapter))

		}
		#endif

		pDM_SWAT_Table->RSSI_cnt_A = 0;
		pDM_SWAT_Table->RSSI_cnt_B = 0;
		pDM_SWAT_Table->try_flag = 0;
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("odm_SwAntDivChkAntSwitch(): Set try_flag to 0 prepare for peak!\n"));
		return;
	}
	else
	{

// To Fix 8723A SW ANT DIV Bug issue
#if (DM_ODM_SUPPORT_TYPE==ODM_WIN)
		if (pDM_Odm->SupportICType & ODM_RTL8723A)
		{
			curTxOkCnt = Adapter->TxStats.NumTxBytesUnicast - pDM_SWAT_Table->lastTxOkCnt;
			curRxOkCnt =Adapter->RxStats.NumRxBytesUnicast - pDM_SWAT_Table->lastRxOkCnt;
			pDM_SWAT_Table->lastTxOkCnt = Adapter->TxStats.NumTxBytesUnicast;
			pDM_SWAT_Table->lastRxOkCnt = Adapter->RxStats.NumRxBytesUnicast;
		}
#else	
		curTxOkCnt = *(pDM_Odm->pNumTxBytesUnicast) - pDM_SWAT_Table->lastTxOkCnt;
		curRxOkCnt = *(pDM_Odm->pNumRxBytesUnicast) - pDM_SWAT_Table->lastRxOkCnt;
		pDM_SWAT_Table->lastTxOkCnt = *(pDM_Odm->pNumTxBytesUnicast);
		pDM_SWAT_Table->lastRxOkCnt = *(pDM_Odm->pNumRxBytesUnicast);
#endif	
		if(pDM_SWAT_Table->try_flag == 1)
		{
			if(pDM_SWAT_Table->CurAntenna == MAIN_ANT)
			{
				pDM_SWAT_Table->TXByteCnt_A += curTxOkCnt;
				pDM_SWAT_Table->RXByteCnt_A += curRxOkCnt;
			}
			else
			{
				pDM_SWAT_Table->TXByteCnt_B += curTxOkCnt;
				pDM_SWAT_Table->RXByteCnt_B += curRxOkCnt;
			}
		
			nextAntenna = (pDM_SWAT_Table->CurAntenna == MAIN_ANT)? AUX_ANT : MAIN_ANT;
			pDM_SWAT_Table->RSSI_Trying--;
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("RSSI_Trying = %d\n",pDM_SWAT_Table->RSSI_Trying));
			if(pDM_SWAT_Table->RSSI_Trying == 0)
			{
				CurByteCnt = (pDM_SWAT_Table->CurAntenna == MAIN_ANT)? (pDM_SWAT_Table->TXByteCnt_A+pDM_SWAT_Table->RXByteCnt_A) : (pDM_SWAT_Table->TXByteCnt_B+pDM_SWAT_Table->RXByteCnt_B);
				PreByteCnt = (pDM_SWAT_Table->CurAntenna == MAIN_ANT)? (pDM_SWAT_Table->TXByteCnt_B+pDM_SWAT_Table->RXByteCnt_B) : (pDM_SWAT_Table->TXByteCnt_A+pDM_SWAT_Table->RXByteCnt_A);
				
				if(pDM_SWAT_Table->TrafficLoad == TRAFFIC_HIGH)
					//CurByteCnt = PlatformDivision64(CurByteCnt, 9);
					PreByteCnt = PreByteCnt*9;
				else if(pDM_SWAT_Table->TrafficLoad == TRAFFIC_LOW)
					//CurByteCnt = PlatformDivision64(CurByteCnt, 2);
					PreByteCnt = PreByteCnt*2;

				if(pDM_SWAT_Table->RSSI_cnt_A > 0)
					RSSI_A = pDM_SWAT_Table->RSSI_sum_A/pDM_SWAT_Table->RSSI_cnt_A; 
				else
					RSSI_A = 0;
				if(pDM_SWAT_Table->RSSI_cnt_B > 0)
					RSSI_B = pDM_SWAT_Table->RSSI_sum_B/pDM_SWAT_Table->RSSI_cnt_B; 
				else
					RSSI_B = 0;
				curRSSI = (pDM_SWAT_Table->CurAntenna == MAIN_ANT)? RSSI_A : RSSI_B;
				pDM_SWAT_Table->PreRSSI =  (pDM_SWAT_Table->CurAntenna == MAIN_ANT)? RSSI_B : RSSI_A;
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Luke:PreRSSI = %d, CurRSSI = %d\n",pDM_SWAT_Table->PreRSSI, curRSSI));
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("SWAS: preAntenna= %s, curAntenna= %s \n", 
				(pDM_SWAT_Table->PreAntenna == MAIN_ANT?"MAIN":"AUX"), (pDM_SWAT_Table->CurAntenna == MAIN_ANT?"MAIN":"AUX")));
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Luke:RSSI_A= %d, RSSI_cnt_A = %d, RSSI_B= %d, RSSI_cnt_B = %d\n",
					RSSI_A, pDM_SWAT_Table->RSSI_cnt_A, RSSI_B, pDM_SWAT_Table->RSSI_cnt_B));
			}

		}
		else
		{
		
			if(pDM_SWAT_Table->RSSI_cnt_A > 0)
				RSSI_A = pDM_SWAT_Table->RSSI_sum_A/pDM_SWAT_Table->RSSI_cnt_A; 
			else
				RSSI_A = 0;
			if(pDM_SWAT_Table->RSSI_cnt_B > 0)
				RSSI_B = pDM_SWAT_Table->RSSI_sum_B/pDM_SWAT_Table->RSSI_cnt_B; 
			else
				RSSI_B = 0;
			curRSSI = (pDM_SWAT_Table->CurAntenna == MAIN_ANT)? RSSI_A : RSSI_B;
			pDM_SWAT_Table->PreRSSI =  (pDM_SWAT_Table->PreAntenna == MAIN_ANT)? RSSI_A : RSSI_B;
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Ekul:PreRSSI = %d, CurRSSI = %d\n", pDM_SWAT_Table->PreRSSI, curRSSI));
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("SWAS: preAntenna= %s, curAntenna= %s \n", 
			(pDM_SWAT_Table->PreAntenna == MAIN_ANT?"MAIN":"AUX"), (pDM_SWAT_Table->CurAntenna == MAIN_ANT?"MAIN":"AUX")));

			ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Ekul:RSSI_A= %d, RSSI_cnt_A = %d, RSSI_B= %d, RSSI_cnt_B = %d\n",
				RSSI_A, pDM_SWAT_Table->RSSI_cnt_A, RSSI_B, pDM_SWAT_Table->RSSI_cnt_B));
			//RT_TRACE(COMP_SWAS, DBG_LOUD, ("Ekul:curTxOkCnt = %d\n", curTxOkCnt));
			//RT_TRACE(COMP_SWAS, DBG_LOUD, ("Ekul:curRxOkCnt = %d\n", curRxOkCnt));
		}

		//1 Trying State
		if((pDM_SWAT_Table->try_flag == 1)&&(pDM_SWAT_Table->RSSI_Trying == 0))
		{

			if(pDM_SWAT_Table->TestMode == TP_MODE)
			{
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("SWAS: TestMode = TP_MODE"));
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("TRY:CurByteCnt = %lld,", CurByteCnt));
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("TRY:PreByteCnt = %lld\n",PreByteCnt));		
				if(CurByteCnt < PreByteCnt)
				{
					if(pDM_SWAT_Table->CurAntenna == MAIN_ANT)
						pDM_SWAT_Table->SelectAntennaMap=pDM_SWAT_Table->SelectAntennaMap<<1;
					else
						pDM_SWAT_Table->SelectAntennaMap=(pDM_SWAT_Table->SelectAntennaMap<<1)+1;
				}
				else
				{
					if(pDM_SWAT_Table->CurAntenna == MAIN_ANT)
						pDM_SWAT_Table->SelectAntennaMap=(pDM_SWAT_Table->SelectAntennaMap<<1)+1;
					else
						pDM_SWAT_Table->SelectAntennaMap=pDM_SWAT_Table->SelectAntennaMap<<1;
				}
				for (i= 0; i<8; i++)
				{
					if(((pDM_SWAT_Table->SelectAntennaMap>>i)&BIT0) == 1)
						Score_A++;
					else
						Score_B++;
				}
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("SelectAntennaMap=%x\n ",pDM_SWAT_Table->SelectAntennaMap));
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Score_A=%d, Score_B=%d\n", Score_A, Score_B));
			
				if(pDM_SWAT_Table->CurAntenna == MAIN_ANT)
				{
					nextAntenna = (Score_A > Score_B)?MAIN_ANT:AUX_ANT;
				}
				else
				{
					nextAntenna = (Score_B > Score_A)?AUX_ANT:MAIN_ANT;
				}
				//RT_TRACE(COMP_SWAS, DBG_LOUD, ("nextAntenna=%s\n",(nextAntenna==Antenna_A)?"A":"B"));
				//RT_TRACE(COMP_SWAS, DBG_LOUD, ("preAntenna= %s, curAntenna= %s \n", 
				//(DM_SWAT_Table.PreAntenna == Antenna_A?"A":"B"), (DM_SWAT_Table.CurAntenna == Antenna_A?"A":"B")));

				if(nextAntenna != pDM_SWAT_Table->CurAntenna)
				{
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("SWAS: Switch back to another antenna"));
				}
				else
				{
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("SWAS: current anntena is good\n"));
				}	
			}

			if(pDM_SWAT_Table->TestMode == RSSI_MODE)
			{	
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("SWAS: TestMode = RSSI_MODE"));
				pDM_SWAT_Table->SelectAntennaMap=0xAA;
				if(curRSSI < pDM_SWAT_Table->PreRSSI) //Current antenna is worse than previous antenna
				{
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("SWAS: Switch back to another antenna"));
					nextAntenna = (pDM_SWAT_Table->CurAntenna == MAIN_ANT)? AUX_ANT : MAIN_ANT;
				}
				else // current anntena is good
				{
					nextAntenna =pDM_SWAT_Table->CurAntenna;
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("SWAS: current anntena is good\n"));
				}
			}
			pDM_SWAT_Table->try_flag = 0;
			pDM_Odm->RSSI_test = FALSE;
			pDM_SWAT_Table->RSSI_sum_A = 0;
			pDM_SWAT_Table->RSSI_cnt_A = 0;
			pDM_SWAT_Table->RSSI_sum_B = 0;
			pDM_SWAT_Table->RSSI_cnt_B = 0;
			pDM_SWAT_Table->TXByteCnt_A = 0;
			pDM_SWAT_Table->TXByteCnt_B = 0;
			pDM_SWAT_Table->RXByteCnt_A = 0;
			pDM_SWAT_Table->RXByteCnt_B = 0;
			
		}

		//1 Normal State
		else if(pDM_SWAT_Table->try_flag == 0)
		{
			if(pDM_SWAT_Table->TrafficLoad == TRAFFIC_HIGH)
			{
				if ((curTxOkCnt+curRxOkCnt) > 3750000)//if(PlatformDivision64(curTxOkCnt+curRxOkCnt, 2) > 1875000)
					pDM_SWAT_Table->TrafficLoad = TRAFFIC_HIGH;
				else
					pDM_SWAT_Table->TrafficLoad = TRAFFIC_LOW;
			}
			else if(pDM_SWAT_Table->TrafficLoad == TRAFFIC_LOW)
				{
				if ((curTxOkCnt+curRxOkCnt) > 3750000) //if(PlatformDivision64(curTxOkCnt+curRxOkCnt, 2) > 1875000)
					pDM_SWAT_Table->TrafficLoad = TRAFFIC_HIGH;
				else
					pDM_SWAT_Table->TrafficLoad = TRAFFIC_LOW;
			}
			if(pDM_SWAT_Table->TrafficLoad == TRAFFIC_HIGH)
				pDM_SWAT_Table->bTriggerAntennaSwitch = 0;
			//RT_TRACE(COMP_SWAS, DBG_LOUD, ("Normal:TrafficLoad = %llu\n", curTxOkCnt+curRxOkCnt));

			//Prepare To Try Antenna		
					nextAntenna = (pDM_SWAT_Table->CurAntenna == MAIN_ANT)? AUX_ANT : MAIN_ANT;
					pDM_SWAT_Table->try_flag = 1;
					pDM_Odm->RSSI_test = TRUE;
			if((curRxOkCnt+curTxOkCnt) > 1000)
			{
				pDM_SWAT_Table->RSSI_Trying = 4;
				pDM_SWAT_Table->TestMode = TP_MODE;
				}
				else
				{
				pDM_SWAT_Table->RSSI_Trying = 2;
				pDM_SWAT_Table->TestMode = RSSI_MODE;

			}
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("SWAS: Normal State -> Begin Trying!\n"));
			
			
			pDM_SWAT_Table->RSSI_sum_A = 0;
			pDM_SWAT_Table->RSSI_cnt_A = 0;
			pDM_SWAT_Table->RSSI_sum_B = 0;
			pDM_SWAT_Table->RSSI_cnt_B = 0;
		}
	}

	//1 4.Change TRX antenna
	if(nextAntenna != pDM_SWAT_Table->CurAntenna)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("SWAS: Change TX Antenna!\n "));
		//PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, 0x300, nextAntenna);		
		#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)			
		ODM_SetAntenna(pDM_Odm,nextAntenna);		
		#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
		{
			BOOLEAN bEnqueue;			
			bEnqueue = (pDM_Odm->SupportInterface ==  ODM_ITRF_PCIE)?FALSE :TRUE;			
			rtw_antenna_select_cmd(pDM_Odm->Adapter, nextAntenna, bEnqueue);
		}
		#endif
		
	}

	//1 5.Reset Statistics
	pDM_SWAT_Table->PreAntenna = pDM_SWAT_Table->CurAntenna;
	pDM_SWAT_Table->CurAntenna = nextAntenna;
	pDM_SWAT_Table->PreRSSI = curRSSI;

	//1 6.Set next timer
	{
		//PADAPTER		pAdapter = pDM_Odm->Adapter;
		//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	

	if(pDM_SWAT_Table->RSSI_Trying == 0)
		return;

	if(pDM_SWAT_Table->RSSI_Trying%2 == 0)
	{
		if(pDM_SWAT_Table->TestMode == TP_MODE)
		{
			if(pDM_SWAT_Table->TrafficLoad == TRAFFIC_HIGH)
			{
				ODM_SetTimer(pDM_Odm,&pDM_SWAT_Table->SwAntennaSwitchTimer, 10 ); //ms
				
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("dm_SW_AntennaSwitch(): Test another antenna for 10 ms\n"));
			}
			else if(pDM_SWAT_Table->TrafficLoad == TRAFFIC_LOW)
			{
				ODM_SetTimer(pDM_Odm,&pDM_SWAT_Table->SwAntennaSwitchTimer, 50 ); //ms
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("dm_SW_AntennaSwitch(): Test another antenna for 50 ms\n"));
			}
		}
		else
		{
			ODM_SetTimer(pDM_Odm,&pDM_SWAT_Table->SwAntennaSwitchTimer, 500 ); //ms
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("dm_SW_AntennaSwitch(): Test another antenna for 500 ms\n"));
		}
	}
	else
	{
		if(pDM_SWAT_Table->TestMode == TP_MODE)
		{
			if(pDM_SWAT_Table->TrafficLoad == TRAFFIC_HIGH)
				ODM_SetTimer(pDM_Odm,&pDM_SWAT_Table->SwAntennaSwitchTimer, 90 ); //ms
			else if(pDM_SWAT_Table->TrafficLoad == TRAFFIC_LOW)
				ODM_SetTimer(pDM_Odm,&pDM_SWAT_Table->SwAntennaSwitchTimer, 100 ); //ms
		}
		else
			ODM_SetTimer(pDM_Odm,&pDM_SWAT_Table->SwAntennaSwitchTimer, 500 ); //ms 
	}
	}
#endif	// #if (DM_ODM_SUPPORT_TYPE  & (ODM_WIN|ODM_CE))
#endif	// #if (RTL8192C_SUPPORT==1) 
}


// Remove odm_SwAntDivSelectScanChnl() and odm_SwAntDivConstructScanChnl() to odm_PathDiv.c


//
// 20100514 Luke/Joseph:
// Callback function for 500ms antenna test trying.
//
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID
odm_SwAntDivChkAntSwitchCallback(
	PRT_TIMER		pTimer
)
{
	PADAPTER		Adapter = (PADAPTER)pTimer->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	pSWAT_T		pDM_SWAT_Table = &pHalData->DM_OutSrc.DM_SWAT_Table;

	#if DEV_BUS_TYPE==RT_PCI_INTERFACE
	#if USE_WORKITEM
	ODM_ScheduleWorkItem(&pDM_SWAT_Table->SwAntennaSwitchWorkitem);
	#else
	odm_SwAntDivChkAntSwitch(&pHalData->DM_OutSrc, SWAW_STEP_DETERMINE);
	#endif
	#else
	ODM_ScheduleWorkItem(&pDM_SWAT_Table->SwAntennaSwitchWorkitem);
	#endif
	
}
VOID
odm_SwAntDivChkAntSwitchWorkitemCallback(
    IN PVOID            pContext
    )
{

	PADAPTER		pAdapter = (PADAPTER)pContext;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	odm_SwAntDivChkAntSwitch(&pHalData->DM_OutSrc, SWAW_STEP_DETERMINE);

}
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
VOID odm_SwAntDivChkAntSwitchCallback(void *FunctionContext)
{
	PDM_ODM_T	pDM_Odm= (PDM_ODM_T)FunctionContext;
	PADAPTER	padapter = pDM_Odm->Adapter;
	if(padapter->net_closed == _TRUE)
	    return;
	odm_SwAntDivChkAntSwitch(pDM_Odm, SWAW_STEP_DETERMINE);	
}
#elif (DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
VOID odm_SwAntDivChkAntSwitchCallback(void *FunctionContext)
{
	PDM_ODM_T	pDM_Odm= (PDM_ODM_T)FunctionContext;
	odm_SwAntDivChkAntSwitch(pDM_Odm, SWAW_STEP_DETERMINE);
}
#endif

#else //#if(defined(CONFIG_SW_ANTENNA_DIVERSITY))

VOID odm_SwAntDivInit(	IN		PDM_ODM_T		pDM_Odm	) {}
VOID ODM_SwAntDivChkPerPktRssi(
	IN PDM_ODM_T	pDM_Odm,
	IN u1Byte		StationID,
	IN PODM_PHY_INFO_T pPhyInfo
	) {}
VOID odm_SwAntDivChkAntSwitch(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u1Byte			Step
	) {}
VOID ODM_SwAntDivResetBeforeLink(	IN		PDM_ODM_T		pDM_Odm	){}
VOID ODM_SwAntDivRestAfterLink(	IN		PDM_ODM_T		pDM_Odm	){}
VOID odm_SwAntDetectInit(	IN		PDM_ODM_T		pDM_Odm){}
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID odm_SwAntDivChkAntSwitchCallback(	PRT_TIMER		pTimer){}
VOID odm_SwAntDivChkAntSwitchWorkitemCallback(    IN PVOID            pContext    ){}
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
VOID odm_SwAntDivChkAntSwitchCallback(void *FunctionContext){}
#elif (DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
VOID odm_SwAntDivChkAntSwitchCallback(void *FunctionContext){}
#endif

#endif //#if(defined(CONFIG_SW_ANTENNA_DIVERSITY))



#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
BOOLEAN
ODM_SwAntDivCheckBeforeLink(
	IN		PDM_ODM_T		pDM_Odm
	)
{

#if (RT_MEM_SIZE_LEVEL != RT_MEM_SIZE_MINIMUM)

	PADAPTER		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE*	pHalData = GET_HAL_DATA(Adapter);
	PMGNT_INFO		pMgntInfo = &Adapter->MgntInfo;
	pSWAT_T			pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	pFAT_T	pDM_FatTable = &pDM_Odm->DM_FatTable;
	s1Byte			Score = 0;
	PRT_WLAN_BSS	pTmpBssDesc, pTestBssDesc;
	s4Byte 			power_diff = 0, power_target = 10;
	u1Byte			index, counter = 0;
	static u1Byte		ScanChannel;
	u8Byte			tStamp_diff = 0;		


	if (pDM_Odm->Adapter == NULL)  //For BSOD when plug/unplug fast.  //By YJ,120413
	{	// The ODM structure is not initialized.
		return FALSE;
	}

	// Retrieve antenna detection registry info, added by Roger, 2012.11.27.
	if(!IS_ANT_DETECT_SUPPORT_RSSI(Adapter))
			return FALSE;

	// Since driver is going to set BB register, it shall check if there is another thread controlling BB/RF.
	PlatformAcquireSpinLock(Adapter, RT_RF_STATE_SPINLOCK);
	if(pHalData->eRFPowerState!=eRfOn || pMgntInfo->RFChangeInProgress || pMgntInfo->bMediaConnect)
	{
		PlatformReleaseSpinLock(Adapter, RT_RF_STATE_SPINLOCK);
	
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, 
				("ODM_SwAntDivCheckBeforeLink(): RFChangeInProgress(%x), eRFPowerState(%x)\n", 
				pMgntInfo->RFChangeInProgress, pHalData->eRFPowerState));
	
		pDM_SWAT_Table->SWAS_NoLink_State = 0;
		
		return FALSE;
	}
	else
	{
		PlatformReleaseSpinLock(Adapter, RT_RF_STATE_SPINLOCK);
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("pDM_SWAT_Table->SWAS_NoLink_State = %d\n", pDM_SWAT_Table->SWAS_NoLink_State));
	//1 Run AntDiv mechanism "Before Link" part.
	if(pDM_SWAT_Table->SWAS_NoLink_State == 0)
	{
		//1 Prepare to do Scan again to check current antenna state.

		// Set check state to next step.
		pDM_SWAT_Table->SWAS_NoLink_State = 1;
	
		// Copy Current Scan list.
		pMgntInfo->tmpNumBssDesc = pMgntInfo->NumBssDesc;
		PlatformMoveMemory((PVOID)Adapter->MgntInfo.tmpbssDesc, (PVOID)pMgntInfo->bssDesc, sizeof(RT_WLAN_BSS)*MAX_BSS_DESC);
		
		// Go back to scan function again.
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SwAntDivCheckBeforeLink: Scan one more time\n"));
		pMgntInfo->ScanStep=0;
		pMgntInfo->bScanAntDetect = TRUE;
		ScanChannel = odm_SwAntDivSelectScanChnl(Adapter);

		
		if(pDM_Odm->SupportICType & (ODM_RTL8188E|ODM_RTL8821))
		{
			if(pDM_FatTable->RxIdleAnt == MAIN_ANT)
				ODM_UpdateRxIdleAnt(pDM_Odm, AUX_ANT);
			else
				ODM_UpdateRxIdleAnt(pDM_Odm, MAIN_ANT);
			if(ScanChannel == 0)
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, 
					("ODM_SwAntDivCheckBeforeLink(): No AP List Avaiable, Using Ant(%s)\n", (pDM_FatTable->RxIdleAnt==MAIN_ANT)?"AUX_ANT":"MAIN_ANT"));

				if(IS_5G_WIRELESS_MODE(pMgntInfo->dot11CurrentWirelessMode))
				{
					pDM_SWAT_Table->Ant5G = pDM_FatTable->RxIdleAnt;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pDM_SWAT_Table->Ant5G=%s\n", (pDM_FatTable->RxIdleAnt==MAIN_ANT)?"MAIN_ANT":"AUX_ANT"));
				}
				else
				{
					pDM_SWAT_Table->Ant2G = pDM_FatTable->RxIdleAnt;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pDM_SWAT_Table->Ant2G=%s\n", (pDM_FatTable->RxIdleAnt==MAIN_ANT)?"MAIN_ANT":"AUX_ANT"));
				}
				return FALSE;
			}

			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, 
				("ODM_SwAntDivCheckBeforeLink: Change to %s for testing.\n", ((pDM_FatTable->RxIdleAnt == MAIN_ANT)?"MAIN_ANT":"AUX_ANT")));
		}
		else if(pDM_Odm->SupportICType & (ODM_RTL8192C|ODM_RTL8723B))
		{
			// Switch Antenna to another one.
			pDM_SWAT_Table->PreAntenna = pDM_SWAT_Table->CurAntenna;
			pDM_SWAT_Table->CurAntenna = (pDM_SWAT_Table->CurAntenna==MAIN_ANT)?AUX_ANT:MAIN_ANT;
			
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, 
				("ODM_SwAntDivCheckBeforeLink: Change to Ant(%s) for testing.\n", (pDM_SWAT_Table->CurAntenna==MAIN_ANT)?"MAIN":"AUX"));
			//PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, 0x300, DM_SWAT_Table.CurAntenna);
			if(pDM_Odm->SupportICType == ODM_RTL8192C)
			{
				pDM_SWAT_Table->SWAS_NoLink_BK_Reg860 = ((pDM_SWAT_Table->SWAS_NoLink_BK_Reg860 & 0xfffffcff) | (pDM_SWAT_Table->CurAntenna<<8));
				ODM_SetBBReg(pDM_Odm,  rFPGA0_XA_RFInterfaceOE, bMaskDWord, pDM_SWAT_Table->SWAS_NoLink_BK_Reg860);
			}
			else if(pDM_Odm->SupportICType == ODM_RTL8723B)
			{
				pDM_SWAT_Table->SWAS_NoLink_BK_Reg92c = ((pDM_SWAT_Table->SWAS_NoLink_BK_Reg92c & 0xfffffffc) | (pDM_SWAT_Table->CurAntenna));
				ODM_SetBBReg(pDM_Odm,  rfe_ctrl_anta_src, 0xff, 0x77);
				ODM_SetBBReg(pDM_Odm,  rDPDT_control, bMaskDWord, pDM_SWAT_Table->SWAS_NoLink_BK_Reg92c);
			}
		}
		
		odm_SwAntDivConstructScanChnl(Adapter, ScanChannel);
		PlatformSetTimer(Adapter, &pMgntInfo->ScanTimer, 5);

		return TRUE;
	}
	else
	{
		//1 ScanComple() is called after antenna swiched.
		//1 Check scan result and determine which antenna is going
		//1 to be used.

		for(index = 0; index < pMgntInfo->tmpNumBssDesc; index++)
		{
			pTmpBssDesc = &(pMgntInfo->tmpbssDesc[index]); // Antenna 1
			pTestBssDesc = &(pMgntInfo->bssDesc[index]); // Antenna 2

			if(PlatformCompareMemory(pTestBssDesc->bdBssIdBuf, pTmpBssDesc->bdBssIdBuf, 6)!=0)
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SwAntDivCheckBeforeLink(): ERROR!! This shall not happen.\n"));
				continue;
			}

			if(pDM_Odm->SupportICType != ODM_RTL8723B)
			{
				if(pTmpBssDesc->ChannelNumber == ScanChannel)
				{
			if(pTmpBssDesc->RecvSignalPower > pTestBssDesc->RecvSignalPower)
			{
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SwAntDivCheckBeforeLink: Compare scan entry: Score++\n"));
						RT_PRINT_STR(COMP_SCAN, DBG_WARNING, "GetScanInfo(): new Bss SSID:", pTmpBssDesc->bdSsIdBuf, pTmpBssDesc->bdSsIdLen);
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("at ch %d, Original: %d, Test: %d\n\n", pTmpBssDesc->ChannelNumber, pTmpBssDesc->RecvSignalPower, pTestBssDesc->RecvSignalPower));
			
				Score++;
				PlatformMoveMemory(pTestBssDesc, pTmpBssDesc, sizeof(RT_WLAN_BSS));
			}
			else if(pTmpBssDesc->RecvSignalPower < pTestBssDesc->RecvSignalPower)
			{
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SwAntDivCheckBeforeLink: Compare scan entry: Score--\n"));
						RT_PRINT_STR(COMP_SCAN, DBG_WARNING, "GetScanInfo(): new Bss SSID:", pTmpBssDesc->bdSsIdBuf, pTmpBssDesc->bdSsIdLen);
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("at ch %d, Original: %d, Test: %d\n\n", pTmpBssDesc->ChannelNumber, pTmpBssDesc->RecvSignalPower, pTestBssDesc->RecvSignalPower));
				Score--;
			}
					else
					{
						if(pTestBssDesc->bdTstamp - pTmpBssDesc->bdTstamp < 5000)
						{
							RT_PRINT_STR(COMP_SCAN, DBG_WARNING, "GetScanInfo(): new Bss SSID:", pTmpBssDesc->bdSsIdBuf, pTmpBssDesc->bdSsIdLen);
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("at ch %d, Original: %d, Test: %d\n", pTmpBssDesc->ChannelNumber, pTmpBssDesc->RecvSignalPower, pTestBssDesc->RecvSignalPower));
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("The 2nd Antenna didn't get this AP\n\n"));
						}
					}
				}
			}
			else
			{ 
				if(pTmpBssDesc->ChannelNumber == ScanChannel)
				{
					if(pTmpBssDesc->RecvSignalPower > pTestBssDesc->RecvSignalPower)
					{
						counter++;
						power_diff = power_diff + (pTmpBssDesc->RecvSignalPower - pTestBssDesc->RecvSignalPower); 
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Original: %d, Test: %d\n", pTmpBssDesc->RecvSignalPower, pTestBssDesc->RecvSignalPower));
						ODM_PRINT_ADDR(pDM_Odm,ODM_COMP_ANT_DIV, DBG_LOUD, ("SSID:"), pTmpBssDesc->bdSsIdBuf);
						ODM_PRINT_ADDR(pDM_Odm,ODM_COMP_ANT_DIV, DBG_LOUD, ("BSSID:"), pTmpBssDesc->bdBssIdBuf);
						PlatformMoveMemory(pTestBssDesc, pTmpBssDesc, sizeof(RT_WLAN_BSS));
					}
					else if(pTestBssDesc->RecvSignalPower > pTmpBssDesc->RecvSignalPower)
					{
						counter++;
						power_diff = power_diff + (pTestBssDesc->RecvSignalPower - pTmpBssDesc->RecvSignalPower);
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Original: %d, Test: %d\n", pTmpBssDesc->RecvSignalPower, pTestBssDesc->RecvSignalPower));
						ODM_PRINT_ADDR(pDM_Odm,ODM_COMP_ANT_DIV, DBG_LOUD, ("SSID:"), pTmpBssDesc->bdSsIdBuf);
						ODM_PRINT_ADDR(pDM_Odm,ODM_COMP_ANT_DIV, DBG_LOUD, ("BSSID:"), pTmpBssDesc->bdBssIdBuf)
					}
					else if(pTestBssDesc->bdTstamp > pTmpBssDesc->bdTstamp)
					{
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("time_diff: %lld\n", (pTestBssDesc->bdTstamp-pTmpBssDesc->bdTstamp)/1000));
						if(pTestBssDesc->bdTstamp - pTmpBssDesc->bdTstamp > 5000)
						{
							counter++;
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Original: %d, Test: %d\n", pTmpBssDesc->RecvSignalPower, pTestBssDesc->RecvSignalPower));
							ODM_PRINT_ADDR(pDM_Odm,ODM_COMP_ANT_DIV, DBG_LOUD, ("SSID:"), pTmpBssDesc->bdSsIdBuf);
							ODM_PRINT_ADDR(pDM_Odm,ODM_COMP_ANT_DIV, DBG_LOUD, ("BSSID:"), pTmpBssDesc->bdBssIdBuf)
						}
					}
				}
			}
		}

		if(pDM_Odm->SupportICType == ODM_RTL8723B)
		{ 
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("counter: %d power_diff: %d\n", counter, power_diff));

			if(counter != 0)
				power_diff = power_diff / counter;

			if(power_diff <= power_target && counter != 0) 
				Score++;
		}

		if(pDM_Odm->SupportICType & (ODM_RTL8188E|ODM_RTL8821))
		{
			if(pMgntInfo->NumBssDesc!=0 && Score<0)
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,
							("ODM_SwAntDivCheckBeforeLink(): Using Ant(%s)\n", (pDM_FatTable->RxIdleAnt==MAIN_ANT)?"MAIN_ANT":"AUX_ANT"));
			}
			else
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, 
						("ODM_SwAntDivCheckBeforeLink(): Remain Ant(%s)\n", (pDM_FatTable->RxIdleAnt==MAIN_ANT)?"AUX_ANT":"MAIN_ANT"));

				if(pDM_FatTable->RxIdleAnt == MAIN_ANT)
					ODM_UpdateRxIdleAnt(pDM_Odm, AUX_ANT);
				else
					ODM_UpdateRxIdleAnt(pDM_Odm, MAIN_ANT);
			}
			
			if(IS_5G_WIRELESS_MODE(pMgntInfo->dot11CurrentWirelessMode))
			{
				pDM_SWAT_Table->Ant5G = pDM_FatTable->RxIdleAnt;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pDM_SWAT_Table->Ant5G=%s\n", (pDM_FatTable->RxIdleAnt==MAIN_ANT)?"MAIN_ANT":"AUX_ANT"));
			}
			else
			{
				pDM_SWAT_Table->Ant2G = pDM_FatTable->RxIdleAnt;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pDM_SWAT_Table->Ant2G=%s\n", (pDM_FatTable->RxIdleAnt==MAIN_ANT)?"MAIN_ANT":"AUX_ANT"));
			}
		}
		else if(pDM_Odm->SupportICType == ODM_RTL8723B)
		{
			pDM_SWAT_Table->CurAntenna = pDM_SWAT_Table->PreAntenna;
			pDM_SWAT_Table->SWAS_NoLink_BK_Reg92c = ((pDM_SWAT_Table->SWAS_NoLink_BK_Reg92c & 0xfffffffc) | (pDM_SWAT_Table->CurAntenna));
			ODM_SetBBReg(pDM_Odm,  rfe_ctrl_anta_src, 0xff, 0x77);
			ODM_SetBBReg(pDM_Odm,  rDPDT_control,bMaskDWord, pDM_SWAT_Table->SWAS_NoLink_BK_Reg92c);

			if(counter != 0)
			{
				if(pMgntInfo->NumBssDesc != 0 && Score > 0)
				{
					if(pDM_Odm->DM_SWAT_Table.ANTB_ON == FALSE)
					{
						pDM_Odm->DM_SWAT_Table.ANTA_ON = TRUE;
						pDM_Odm->DM_SWAT_Table.ANTB_ON = TRUE;
					}
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("ODM_SwAntDivCheckBeforeLink(): Dual antenna\n"));
				}
				else
				{
					if(pDM_Odm->DM_SWAT_Table.ANTB_ON == TRUE)
					{
						pDM_Odm->DM_SWAT_Table.ANTA_ON = TRUE;
						pDM_Odm->DM_SWAT_Table.ANTB_ON = FALSE;
						BT_SetBtCoexAntNum(Adapter, BT_COEX_ANT_TYPE_DETECTED, 1);
					}
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SwAntDivCheckBeforeLink(): Single antenna\n"));
				}
			}
			else
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("ODM_SwAntDivCheckBeforeLink(): Igone result\n"));
		}
		else if(pDM_Odm->SupportICType == ODM_RTL8192C)
		{
			if(pMgntInfo->NumBssDesc!=0 && Score<=0)
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,
					("ODM_SwAntDivCheckBeforeLink(): Using Ant(%s)\n", (pDM_SWAT_Table->CurAntenna==MAIN_ANT)?"MAIN":"AUX"));

				pDM_SWAT_Table->PreAntenna = pDM_SWAT_Table->CurAntenna;
			}
			else
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, 
					("ODM_SwAntDivCheckBeforeLink(): Remain Ant(%s)\n", (pDM_SWAT_Table->CurAntenna==MAIN_ANT)?"AUX":"MAIN"));

				pDM_SWAT_Table->CurAntenna = pDM_SWAT_Table->PreAntenna;

				//PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, 0x300, DM_SWAT_Table.CurAntenna);
				pDM_SWAT_Table->SWAS_NoLink_BK_Reg860 = ((pDM_SWAT_Table->SWAS_NoLink_BK_Reg860 & 0xfffffcff) | (pDM_SWAT_Table->CurAntenna<<8));
				PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, bMaskDWord, pDM_SWAT_Table->SWAS_NoLink_BK_Reg860);
			}
		}
		
		// Check state reset to default and wait for next time.
		pDM_SWAT_Table->SWAS_NoLink_State = 0;
		pMgntInfo->bScanAntDetect = FALSE;

		return FALSE;
	}

#else
		return	FALSE;
#endif

return FALSE;
}

#endif //#if(DM_ODM_SUPPORT_TYPE==ODM_WIN)


//3============================================================
//3 SW Antenna Diversity
//3============================================================

#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))
VOID
odm_InitHybridAntDiv_88C_92D(
	IN PDM_ODM_T	pDM_Odm 
	)
{

#if((DM_ODM_SUPPORT_TYPE==ODM_AP)||(DM_ODM_SUPPORT_TYPE==ODM_ADSL))
	struct rtl8192cd_priv *priv=pDM_Odm->priv;
#endif
	SWAT_T			*pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	u1Byte                  bTxPathSel=0;	        //0:Path-A   1:Path-B
	u1Byte			i;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("odm_InitHybridAntDiv==============>\n"));

	//whether to do antenna diversity or not
#if(DM_ODM_SUPPORT_TYPE==ODM_AP)
	if(priv==NULL)	return;
	if(!priv->pshare->rf_ft_var.antHw_enable)
		return;	
	
	#ifdef SW_ANT_SWITCH
	priv->pshare->rf_ft_var.antSw_enable =0;
	#endif
#endif

	if((pDM_Odm->SupportICType != ODM_RTL8192C) && (pDM_Odm->SupportICType != ODM_RTL8192D))
		return;


	bTxPathSel=(pDM_Odm->RFType==ODM_1T1R)?FALSE:TRUE;

	ODM_SetBBReg(pDM_Odm,ODM_REG_BB_PWR_SAV1_11N, BIT23, 0); //No update ANTSEL during GNT_BT=1
	ODM_SetBBReg(pDM_Odm,ODM_REG_TX_ANT_CTRL_11N, BIT21, 1); //TX atenna selection from tx_info
	ODM_SetBBReg(pDM_Odm,ODM_REG_ANTSEL_PIN_11N, BIT23, 1);	//enable LED[1:0] pin as ANTSEL
	ODM_SetBBReg(pDM_Odm,ODM_REG_ANTSEL_CTRL_11N, BIT8|BIT9, 0x01);	// 0x01: left antenna, 0x02: right antenna
	// check HW setting: ANTSEL pin connection
	#if(DM_ODM_SUPPORT_TYPE==ODM_AP)
	ODM_Write2Byte(pDM_Odm,ODM_REG_RF_PIN_11N, (ODM_Read2Byte(pDM_Odm,0x804)&0xf0ff )| BIT(8) );	// b11-b8=0001,update RFPin setting
	#endif
	
	// only AP support different path selection temperarly
	if(!bTxPathSel){                 //PATH-A
		ODM_SetBBReg(pDM_Odm,ODM_REG_PIN_CTRL_11N, BIT8|BIT9, 0 ); // ANTSEL as HW control
		ODM_SetBBReg(pDM_Odm,ODM_REG_ANTSEL_PATH_11N, BIT13, 1);	 //select TX ANTESEL from path A
	}
	else	{
		ODM_SetBBReg(pDM_Odm,ODM_REG_PIN_CTRL_11N, BIT24|BIT25, 0 ); // ANTSEL as HW control
		ODM_SetBBReg(pDM_Odm,ODM_REG_ANTSEL_PATH_11N, BIT13, 0);		 //select ANTESEL from path B
	}

	//Set OFDM HW RX Antenna Diversity
	ODM_SetBBReg(pDM_Odm,ODM_REG_ANTDIV_PARA1_11N, 0x7FF, 0x0c0); //Pwdb threshold=8dB
	ODM_SetBBReg(pDM_Odm,ODM_REG_ANTDIV_PARA1_11N, BIT11, 0); //Switch to another antenna by checking pwdb threshold
	ODM_SetBBReg(pDM_Odm,ODM_REG_ANTDIV_PARA3_11N, BIT23, 1);	// Decide final antenna by comparing 2 antennas' pwdb
	
	//Set CCK HW RX Antenna Diversity
	ODM_SetBBReg(pDM_Odm,ODM_REG_CCK_ANTDIV_PARA2_11N, BIT4, 0); //Antenna diversity decision period = 32 sample
	ODM_SetBBReg(pDM_Odm,ODM_REG_CCK_ANTDIV_PARA2_11N, 0xf, 0xf); //Threshold for antenna diversity. Check another antenna power if input power < ANT_lim*4
	ODM_SetBBReg(pDM_Odm,ODM_REG_CCK_ANTDIV_PARA3_11N, BIT13, 1); //polarity ana_A=1 and ana_B=0
	ODM_SetBBReg(pDM_Odm,ODM_REG_CCK_ANTDIV_PARA4_11N, 0x1f, 0x8); //default antenna power = inpwr*(0.5 + r_ant_step/16)


	//Enable HW Antenna Diversity
	if(!bTxPathSel)                 //PATH-A
		ODM_SetBBReg(pDM_Odm,ODM_REG_IGI_A_11N, BIT7,1);	// Enable Hardware antenna switch
	else
		ODM_SetBBReg(pDM_Odm,ODM_REG_IGI_B_11N, BIT7,1);	// Enable Hardware antenna switch
	ODM_SetBBReg(pDM_Odm,ODM_REG_CCK_ANTDIV_PARA1_11N, BIT15, 1);//Enable antenna diversity

	pDM_SWAT_Table->CurAntenna=0;			//choose left antenna as default antenna
	pDM_SWAT_Table->PreAntenna=0;
	for(i=0; i<ASSOCIATE_ENTRY_NUM ; i++)
	{
		pDM_SWAT_Table->CCK_Ant1_Cnt[i] = 0;
		pDM_SWAT_Table->CCK_Ant2_Cnt[i] = 0;
		pDM_SWAT_Table->OFDM_Ant1_Cnt[i] = 0;
		pDM_SWAT_Table->OFDM_Ant2_Cnt[i] = 0;
		pDM_SWAT_Table->RSSI_Ant1_Sum[i] = 0;
		pDM_SWAT_Table->RSSI_Ant2_Sum[i] = 0;
	}
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("<==============odm_InitHybridAntDiv\n"));
}


VOID
odm_InitHybridAntDiv(
	IN PDM_ODM_T	pDM_Odm 
	)
{
	if(!(pDM_Odm->SupportAbility & ODM_BB_ANT_DIV))
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("Return: Not Support HW AntDiv\n"));
		return;
	}
	
	if(pDM_Odm->SupportICType & (ODM_RTL8192C | ODM_RTL8192D))
	{
#if ((RTL8192C_SUPPORT == 1)||(RTL8192D_SUPPORT == 1))
		odm_InitHybridAntDiv_88C_92D(pDM_Odm);
#endif
	}
}


BOOLEAN
odm_StaDefAntSel(
	IN PDM_ODM_T	pDM_Odm,
	IN u4Byte		OFDM_Ant1_Cnt,
	IN u4Byte		OFDM_Ant2_Cnt,
	IN u4Byte		CCK_Ant1_Cnt,
	IN u4Byte		CCK_Ant2_Cnt,
	OUT u1Byte		*pDefAnt 

	)
{
#if 1
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("odm_StaDefAntSelect==============>\n"));

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("OFDM_Ant1_Cnt:%d, OFDM_Ant2_Cnt:%d\n",OFDM_Ant1_Cnt,OFDM_Ant2_Cnt));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("CCK_Ant1_Cnt:%d, CCK_Ant2_Cnt:%d\n",CCK_Ant1_Cnt,CCK_Ant2_Cnt));

	
	if(((OFDM_Ant1_Cnt+OFDM_Ant2_Cnt)==0)&&((CCK_Ant1_Cnt + CCK_Ant2_Cnt) <10)){
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("odm_StaDefAntSelect Fail: No enough packet info!\n"));
		return	FALSE;
	}

	if(OFDM_Ant1_Cnt || OFDM_Ant2_Cnt )	{
		//if RX OFDM packet number larger than 0
		if(OFDM_Ant1_Cnt > OFDM_Ant2_Cnt)
			(*pDefAnt)=1;
		else
			(*pDefAnt)=0;
	}
	// else if RX CCK packet number larger than 10
	else if((CCK_Ant1_Cnt + CCK_Ant2_Cnt) >=10 )
	{
		if(CCK_Ant1_Cnt > (5*CCK_Ant2_Cnt))
			(*pDefAnt)=1;
		else if(CCK_Ant2_Cnt > (5*CCK_Ant1_Cnt))
			(*pDefAnt)=0;
		else if(CCK_Ant1_Cnt > CCK_Ant2_Cnt)
			(*pDefAnt)=0;
		else
			(*pDefAnt)=1;

	}

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("TxAnt = %s\n",((*pDefAnt)==1)?"Ant1":"Ant2"));
	
#endif
	//u4Byte antsel = ODM_GetBBReg(pDM_Odm, 0xc88, bMaskByte0);
	//(*pDefAnt)= (u1Byte) antsel;
	


	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("<==============odm_StaDefAntSelect\n"));

	return TRUE;

	
}


VOID
odm_SetRxIdleAnt(
	IN	PDM_ODM_T	pDM_Odm,
	IN	u1Byte	Ant,
	IN   BOOLEAN   bDualPath                     
)
{
	SWAT_T			*pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;

	//ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("odm_SetRxIdleAnt==============>\n"));

	if(Ant != pDM_SWAT_Table->RxIdleAnt)
	{
	//for path-A
	if(Ant==1) 
			ODM_SetBBReg(pDM_Odm,ODM_REG_RX_DEFUALT_A_11N, 0xFFFF, 0x65a9);   //right-side antenna
	else
			ODM_SetBBReg(pDM_Odm,ODM_REG_RX_DEFUALT_A_11N, 0xFFFF, 0x569a);   //left-side antenna

	//for path-B
	if(bDualPath){
		if(Ant==0) 
				ODM_SetBBReg(pDM_Odm,ODM_REG_RX_DEFUALT_A_11N, 0xFFFF0000, 0x65a9);   //right-side antenna
		else 
				ODM_SetBBReg(pDM_Odm,ODM_REG_RX_DEFUALT_A_11N, 0xFFFF0000, 0x569a);  //left-side antenna
		}
	}
		pDM_SWAT_Table->RxIdleAnt = Ant;
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("RxIdleAnt: %s  Reg858=0x%x\n",(Ant==1)?"Ant1":"Ant2",(Ant==1)?0x65a9:0x569a));

	//ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("<==============odm_SetRxIdleAnt\n"));

	}
		
VOID
ODM_AntselStatistics_88C(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u1Byte			MacId,
	IN		u4Byte			PWDBAll,
	IN		BOOLEAN			isCCKrate
)
{
	SWAT_T			*pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;

	if(pDM_SWAT_Table->antsel == 1)
	{
		if(isCCKrate)
			pDM_SWAT_Table->CCK_Ant1_Cnt[MacId]++;
		else
		{
			pDM_SWAT_Table->OFDM_Ant1_Cnt[MacId]++;
			pDM_SWAT_Table->RSSI_Ant1_Sum[MacId] += PWDBAll;
		}
	}
	else
	{
		if(isCCKrate)
			pDM_SWAT_Table->CCK_Ant2_Cnt[MacId]++;
		else
		{
			pDM_SWAT_Table->OFDM_Ant2_Cnt[MacId]++;
			pDM_SWAT_Table->RSSI_Ant2_Sum[MacId] += PWDBAll;
		}
	}

}




#if(DM_ODM_SUPPORT_TYPE==ODM_WIN)
VOID
ODM_SetTxAntByTxInfo_88C_92D(
	IN		PDM_ODM_T		pDM_Odm,
	IN		pu1Byte			pDesc,
	IN		u1Byte			macId	
)
{
	SWAT_T			*pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	u1Byte			antsel;

	if(!(pDM_Odm->SupportAbility&ODM_BB_ANT_DIV)) 
		return;

	if(pDM_SWAT_Table->RxIdleAnt == 1)
		antsel=(pDM_SWAT_Table->TxAnt[macId] == 1)?0:1;
	else
		antsel=(pDM_SWAT_Table->TxAnt[macId] == 1)?1:0;
	
	SET_TX_DESC_ANTSEL_A_92C(pDesc, antsel);
	//SET_TX_DESC_ANTSEL_B_92C(pDesc, antsel);
	//ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("SET_TX_DESC_ANTSEL_A_92C=%d\n", pDM_SWAT_Table->TxAnt[macId]));
}
#elif(DM_ODM_SUPPORT_TYPE==ODM_CE)
VOID
ODM_SetTxAntByTxInfo_88C_92D(
	IN		PDM_ODM_T		pDM_Odm
)
{

}
#elif(DM_ODM_SUPPORT_TYPE==ODM_AP)
VOID
ODM_SetTxAntByTxInfo_88C_92D(
	IN		PDM_ODM_T		pDM_Odm
)
{

}
#endif

VOID
odm_HwAntDiv_92C_92D(
	IN	PDM_ODM_T	pDM_Odm
)
{
	SWAT_T			*pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	u4Byte			RSSI_Min=0xFF, RSSI, RSSI_Ant1, RSSI_Ant2;
	u1Byte			RxIdleAnt, i;
	BOOLEAN		bRet=FALSE;
	PSTA_INFO_T   	pEntry;
	
#if (DM_ODM_SUPPORT_TYPE==ODM_AP)
	struct rtl8192cd_priv *priv=pDM_Odm->priv;
	//if test, return
	if(priv->pshare->rf_ft_var.CurAntenna & 0x80)
		return;	
#endif	

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("odm_HwAntDiv==============>\n"));
	
	if(!(pDM_Odm->SupportAbility&ODM_BB_ANT_DIV))                                    //if don't support antenna diveristy
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("odm_HwAntDiv: Not supported!\n"));
		return;
	}

	if((pDM_Odm->SupportICType != ODM_RTL8192C) && (pDM_Odm->SupportICType != ODM_RTL8192D))
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("Return: IC Type is not 92C or 92D\n"));
		return;
	}
	
#if (DM_ODM_SUPPORT_TYPE&(ODM_WIN|ODM_CE))
	if(!pDM_Odm->bLinked)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("Return: bLinked is FALSE\n"));
		return;
	}
#endif

	for (i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
	{
		pEntry = pDM_Odm->pODM_StaInfo[i];
		if(IS_STA_VALID(pEntry))
		{

			RSSI_Ant1 = (pDM_SWAT_Table->OFDM_Ant1_Cnt[i] == 0)?0:(pDM_SWAT_Table->RSSI_Ant1_Sum[i]/pDM_SWAT_Table->OFDM_Ant1_Cnt[i]);
			RSSI_Ant2 = (pDM_SWAT_Table->OFDM_Ant2_Cnt[i] == 0)?0:(pDM_SWAT_Table->RSSI_Ant2_Sum[i]/pDM_SWAT_Table->OFDM_Ant2_Cnt[i]);

			ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("RSSI_Ant1=%d,  RSSI_Ant2=%d\n", RSSI_Ant1, RSSI_Ant2));
		
			if(RSSI_Ant1 ||RSSI_Ant2) 
			{
#if (DM_ODM_SUPPORT_TYPE==ODM_AP)		
				if(pDM_Odm->pODM_StaInfo[i]->expire_to)
#endif
				{
					RSSI = (RSSI_Ant1 < RSSI_Ant2) ? RSSI_Ant1 : RSSI_Ant2;
					if((!RSSI) || ( RSSI < RSSI_Min) ) {
						pDM_SWAT_Table->TargetSTA = i;
						RSSI_Min = RSSI;
					}
				}
	}
			///STA: found out default antenna
			bRet=odm_StaDefAntSel(pDM_Odm, 
						 pDM_SWAT_Table->OFDM_Ant1_Cnt[i], 
						 pDM_SWAT_Table->OFDM_Ant2_Cnt[i], 
						 pDM_SWAT_Table->CCK_Ant1_Cnt[i], 
						 pDM_SWAT_Table->CCK_Ant2_Cnt[i], 
						 &pDM_SWAT_Table->TxAnt[i]);

			//if Tx antenna selection: successful
			if(bRet){	
				pDM_SWAT_Table->RSSI_Ant1_Sum[i] = 0;
				pDM_SWAT_Table->RSSI_Ant2_Sum[i] = 0;
				pDM_SWAT_Table->OFDM_Ant1_Cnt[i] = 0;
				pDM_SWAT_Table->OFDM_Ant2_Cnt[i] = 0; 
				pDM_SWAT_Table->CCK_Ant1_Cnt[i] = 0; 
				pDM_SWAT_Table->CCK_Ant2_Cnt[i] = 0; 
			}
		}
	}
	
	//set RX Idle Ant
	RxIdleAnt = pDM_SWAT_Table->TxAnt[pDM_SWAT_Table->TargetSTA];
	odm_SetRxIdleAnt(pDM_Odm, RxIdleAnt, FALSE);

#if (DM_ODM_SUPPORT_TYPE==ODM_AP)
#ifdef TX_SHORTCUT
	if (!priv->pmib->dot11OperationEntry.disable_txsc) {
		plist = phead->next;
		while(plist != phead)	{
			pstat = list_entry(plist, struct stat_info, asoc_list);
			if(pstat->expire_to) {
				for (i=0; i<TX_SC_ENTRY_NUM; i++) {
					struct tx_desc *pdesc= &(pstat->tx_sc_ent[i].hwdesc1);	
					pdesc->Dword2 &= set_desc(~ (BIT(24)|BIT(25)));
					if((pstat->CurAntenna^priv->pshare->rf_ft_var.CurAntenna)&1)
						pdesc->Dword2 |= set_desc(BIT(24)|BIT(25));
					pdesc= &(pstat->tx_sc_ent[i].hwdesc2);	
					pdesc->Dword2 &= set_desc(~ (BIT(24)|BIT(25)));
					if((pstat->CurAntenna^priv->pshare->rf_ft_var.CurAntenna)&1)
						pdesc->Dword2 |= set_desc(BIT(24)|BIT(25));					
				}
			}		

			if (plist == plist->next)
				break;
			plist = plist->next;
		};
	}
#endif	
#endif
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("<==============odm_HwAntDiv\n"));
	
}

VOID
odm_HwAntDiv(
	IN	PDM_ODM_T	pDM_Odm
)
{	

	PADAPTER		pAdapter	= pDM_Odm->Adapter;

#if(DM_ODM_SUPPORT_TYPE==ODM_WIN)
	if(pAdapter->MgntInfo.AntennaTest)
		return;
#endif
	if(!(pDM_Odm->SupportAbility & ODM_BB_ANT_DIV))
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("Return: Not Support HW AntDiv\n"));
		return;
	}
	
	if(pDM_Odm->SupportICType & (ODM_RTL8192C | ODM_RTL8192D))
	{
#if ((RTL8192C_SUPPORT == 1)||(RTL8192D_SUPPORT == 1))
		odm_HwAntDiv_92C_92D(pDM_Odm);
#endif
	}
}


#if(DM_ODM_SUPPORT_TYPE==ODM_AP)
#if 0
VOID
odm_HwAntDiv(
	IN	PDM_ODM_T	pDM_Odm
)
{
	struct rtl8192cd_priv *priv=pDM_Odm->priv;
	struct stat_info	*pstat, *pstat_min=NULL;
	struct list_head	*phead, *plist;
	int rssi_min= 0xff, i;
	u1Byte	idleAnt=priv->pshare->rf_ft_var.CurAntenna;	
	u1Byte	nextAnt;
	BOOLEAN		bRet=FALSE;
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("odm_HwAntDiv==============>\n"));

	if((!priv->pshare->rf_ft_var.antHw_enable) ||(!(pDM_Odm->SupportAbility & ODM_BB_ANT_DIV)))
		return;
	
	//if test, return
	if(priv->pshare->rf_ft_var.CurAntenna & 0x80)
		return;
	
	phead = &priv->asoc_list;
	plist = phead->next;
	////=========================
	//find mimum rssi sta
	////=========================
	while(plist != phead)	{
		pstat = list_entry(plist, struct stat_info, asoc_list);
		if((pstat->expire_to) && (pstat->AntRSSI[0] || pstat->AntRSSI[1])) {
			int rssi = (pstat->AntRSSI[0] < pstat->AntRSSI[1]) ? pstat->AntRSSI[0] : pstat->AntRSSI[1];
			if((!pstat_min) || ( rssi < rssi_min) ) {
				pstat_min = pstat;
				rssi_min = rssi;
			}
		}
		///STA: found out default antenna
		bRet=odm_StaDefAntSel(pDM_Odm,
						pstat->hwRxAntSel[1],
						pstat->hwRxAntSel[0],
						pstat->cckPktCount[1],
						pstat->cckPktCount[0],
						&nextAnt
						);
		
		//if default antenna selection: successful
		if(bRet){	
			pstat->CurAntenna = nextAnt;
			//update rssi
			for(i=0; i<2; i++) {
				if(pstat->cckPktCount[i]==0 && pstat->hwRxAntSel[i]==0)
					pstat->AntRSSI[i] = 0;
			}
			if(pstat->AntRSSI[idleAnt]==0)
				pstat->AntRSSI[idleAnt] = pstat->AntRSSI[idleAnt^1];
			// reset variables
			pstat->hwRxAntSel[1] = pstat->hwRxAntSel[0] =0;
			pstat->cckPktCount[1]= pstat->cckPktCount[0] =0;
		}

		if (plist == plist->next)
			break;
		plist = plist->next;
		
	};
	////=========================
	//Choose  RX Idle antenna according to minmum rssi
	////=========================
	if(pstat_min)	{
		if(priv->pshare->rf_ft_var.CurAntenna!=pstat_min->CurAntenna)
			odm_SetRxIdleAnt(pDM_Odm,pstat_min->CurAntenna,TRUE);
		priv->pshare->rf_ft_var.CurAntenna = pstat_min->CurAntenna;
	}


#ifdef TX_SHORTCUT
	if (!priv->pmib->dot11OperationEntry.disable_txsc) {
		plist = phead->next;
		while(plist != phead)	{
			pstat = list_entry(plist, struct stat_info, asoc_list);
			if(pstat->expire_to) {
				for (i=0; i<TX_SC_ENTRY_NUM; i++) {
					struct tx_desc *pdesc= &(pstat->tx_sc_ent[i].hwdesc1);	
					pdesc->Dword2 &= set_desc(~ (BIT(24)|BIT(25)));
					if((pstat->CurAntenna^priv->pshare->rf_ft_var.CurAntenna)&1)
						pdesc->Dword2 |= set_desc(BIT(24)|BIT(25));
					pdesc= &(pstat->tx_sc_ent[i].hwdesc2);	
					pdesc->Dword2 &= set_desc(~ (BIT(24)|BIT(25)));
					if((pstat->CurAntenna^priv->pshare->rf_ft_var.CurAntenna)&1)
						pdesc->Dword2 |= set_desc(BIT(24)|BIT(25));					
				}
			}		

			if (plist == plist->next)
				break;
			plist = plist->next;
		};
	}
#endif	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,"<==============odm_HwAntDiv\n");
}
#endif

u1Byte
ODM_Diversity_AntennaSelect(
	IN	PDM_ODM_T	pDM_Odm,
	IN	u1Byte	*data
)
{
	struct rtl8192cd_priv *priv=pDM_Odm->priv;

	int ant = _atoi(data, 16);

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("ODM_Diversity_AntennaSelect==============>\n"));

	#ifdef PCIE_POWER_SAVING
	PCIeWakeUp(priv, POWER_DOWN_T0);
	#endif

	if (ant==AUX_ANT || ant==MAIN_ANT) 
	{
		if ( !priv->pshare->rf_ft_var.antSw_select) {
			ODM_Write4Byte(pDM_Odm,0x870, ODM_Read4Byte(pDM_Odm,0x870) | BIT(8)| BIT(9) );  //  ANTSEL A as SW control
			ODM_Write1Byte(pDM_Odm,0xc50, ODM_Read1Byte(pDM_Odm,0xc50) & (~ BIT(7)));	// rx OFDM SW control
			PHY_SetBBReg(priv, 0x860, 0x300, ant);
		} else {
			ODM_Write4Byte(pDM_Odm,0x870, ODM_Read4Byte(pDM_Odm,0x870) | BIT(24)| BIT(25) ); // ANTSEL B as HW control
			PHY_SetBBReg(priv, 0x864, 0x300, ant);
			ODM_Write1Byte(pDM_Odm,0xc58, ODM_Read1Byte(pDM_Odm,0xc58) & (~ BIT(7)));		// rx OFDM SW control
		}

		ODM_Write1Byte(pDM_Odm,0xa01, ODM_Read1Byte(pDM_Odm,0xa01) & (~ BIT(7)));	// rx CCK SW control
		ODM_Write4Byte(pDM_Odm,0x80c, ODM_Read4Byte(pDM_Odm,0x80c) & (~ BIT(21))); // select ant by tx desc
		ODM_Write4Byte(pDM_Odm,0x858, 0x569a569a);

		priv->pshare->rf_ft_var.antHw_enable = 0;
		priv->pshare->rf_ft_var.CurAntenna  = (ant%2);

		#ifdef SW_ANT_SWITCH
		priv->pshare->rf_ft_var.antSw_enable = 0;
		priv->pshare->DM_SWAT_Table.CurAntenna = ant;
		priv->pshare->RSSI_test =0;
		#endif
	}
	else if(ant==0){

		if ( !priv->pshare->rf_ft_var.antSw_select)  {
			ODM_Write4Byte(pDM_Odm,0x870, ODM_Read4Byte(pDM_Odm,0x870) & ~(BIT(8)| BIT(9)) );
			ODM_Write1Byte(pDM_Odm,0xc50, ODM_Read1Byte(pDM_Odm,0xc50) | BIT(7));	// OFDM HW control
		} else {
			ODM_Write4Byte(pDM_Odm,0x870, ODM_Read4Byte(pDM_Odm,0x870) & ~(BIT(24)| BIT(25)) );
			ODM_Write1Byte(pDM_Odm,0xc58, ODM_Read1Byte(pDM_Odm,0xc58) | BIT(7));	// OFDM HW control
		}

		ODM_Write1Byte(pDM_Odm,0xa01, ODM_Read1Byte(pDM_Odm,0xa01) | BIT(7));	// CCK HW control
		ODM_Write4Byte(pDM_Odm,0x80c, ODM_Read4Byte(pDM_Odm,0x80c) | BIT(21) ); // by tx desc
		priv->pshare->rf_ft_var.CurAntenna = 0;
		ODM_Write4Byte(pDM_Odm,0x858, 0x569a569a);
		priv->pshare->rf_ft_var.antHw_enable = 1;
#ifdef SW_ANT_SWITCH
		priv->pshare->rf_ft_var.antSw_enable = 0;
		priv->pshare->RSSI_test =0;
#endif
	}
#ifdef SW_ANT_SWITCH
	else if(ant==3) {
		if(!priv->pshare->rf_ft_var.antSw_enable) {
			
			dm_SW_AntennaSwitchInit(priv);
			ODM_Write4Byte(pDM_Odm,0x858, 0x569a569a);
			priv->pshare->lastTxOkCnt = priv->net_stats.tx_bytes;
			priv->pshare->lastRxOkCnt = priv->net_stats.rx_bytes;
		}
		if ( !priv->pshare->rf_ft_var.antSw_select)
			ODM_Write1Byte(pDM_Odm,0xc50, ODM_Read1Byte(pDM_Odm,0xc50) & (~ BIT(7)));	// rx OFDM SW control
		else
			ODM_Write1Byte(pDM_Odm,0xc58, ODM_Read1Byte(pDM_Odm,0xc58) & (~ BIT(7)));	// rx OFDM SW control

		ODM_Write1Byte(pDM_Odm,0xa01, ODM_Read1Byte(pDM_Odm,0xa01) & (~ BIT(7)));		// rx CCK SW control
		ODM_Write4Byte(pDM_Odm,0x80c, ODM_Read4Byte(pDM_Odm,0x80c) & (~ BIT(21))); 	// select ant by tx desc
		priv->pshare->rf_ft_var.antHw_enable = 0;
		priv->pshare->rf_ft_var.antSw_enable = 1;

	}
#endif
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("<==============ODM_Diversity_AntennaSelect\n"));

	return 1;
}
#endif

#else //#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))

VOID odm_InitHybridAntDiv(	IN PDM_ODM_T	pDM_Odm 	){}
VOID odm_HwAntDiv(	IN	PDM_ODM_T	pDM_Odm){}
#if(DM_ODM_SUPPORT_TYPE==ODM_WIN)
VOID ODM_SetTxAntByTxInfo_88C_92D(
	IN		PDM_ODM_T		pDM_Odm,
	IN		pu1Byte			pDesc,
	IN		u1Byte			macId	
){}
#elif(DM_ODM_SUPPORT_TYPE==ODM_CE)
VOID ODM_SetTxAntByTxInfo_88C_92D(	IN		PDM_ODM_T		pDM_Odm){ }
#elif(DM_ODM_SUPPORT_TYPE==ODM_AP)
VOID ODM_SetTxAntByTxInfo_88C_92D(	IN		PDM_ODM_T		pDM_Odm){ }
#endif

#endif //#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))



//============================================================
//EDCA Turbo
//============================================================

//Remove Edca by Yuchen


#if( DM_ODM_SUPPORT_TYPE == ODM_WIN) 
//
// 2011/07/26 MH Add an API for testing IQK fail case.
//
BOOLEAN
ODM_CheckPowerStatus(
	IN	PADAPTER		Adapter)
{

	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T			pDM_Odm = &pHalData->DM_OutSrc;
	RT_RF_POWER_STATE 	rtState;
	PMGNT_INFO			pMgntInfo	= &(Adapter->MgntInfo);

	// 2011/07/27 MH We are not testing ready~~!! We may fail to get correct value when init sequence.
	if (pMgntInfo->init_adpt_in_progress == TRUE)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("ODM_CheckPowerStatus Return TRUE, due to initadapter"));
		return	TRUE;
	}
	
	//
	//	2011/07/19 MH We can not execute tx pwoer tracking/ LLC calibrate or IQK.
	//
	Adapter->HalFunc.GetHwRegHandler(Adapter, HW_VAR_RF_STATE, (pu1Byte)(&rtState));	
	if(Adapter->bDriverStopped || Adapter->bDriverIsGoingToPnpSetPowerSleep || rtState == eRfOff)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("ODM_CheckPowerStatus Return FALSE, due to %d/%d/%d\n", 
		Adapter->bDriverStopped, Adapter->bDriverIsGoingToPnpSetPowerSleep, rtState));
		return	FALSE;
	}
	return	TRUE;
}
#endif

// need to ODM CE Platform
//move to here for ANT detection mechanism using

#if ((DM_ODM_SUPPORT_TYPE == ODM_WIN)||(DM_ODM_SUPPORT_TYPE == ODM_CE))
u4Byte
GetPSDData(
	IN PDM_ODM_T	pDM_Odm,
	unsigned int 	point,
	u1Byte initial_gain_psd)
{
	//unsigned int	val, rfval;
	//int	psd_report;
	u4Byte	psd_report;
	
	//HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	//Debug Message
	//val = PHY_QueryBBReg(Adapter,0x908, bMaskDWord);
	//DbgPrint("Reg908 = 0x%x\n",val);
	//val = PHY_QueryBBReg(Adapter,0xDF4, bMaskDWord);
	//rfval = PHY_QueryRFReg(Adapter, ODM_RF_PATH_A, 0x00, bRFRegOffsetMask);
	//DbgPrint("RegDF4 = 0x%x, RFReg00 = 0x%x\n",val, rfval);
	//DbgPrint("PHYTXON = %x, OFDMCCA_PP = %x, CCKCCA_PP = %x, RFReg00 = %x\n",
		//(val&BIT25)>>25, (val&BIT14)>>14, (val&BIT15)>>15, rfval);

	//Set DCO frequency index, offset=(40MHz/SamplePts)*point
	ODM_SetBBReg(pDM_Odm, 0x808, 0x3FF, point);

	//Start PSD calculation, Reg808[22]=0->1
	ODM_SetBBReg(pDM_Odm, 0x808, BIT22, 1);
	//Need to wait for HW PSD report
	ODM_StallExecution(1000);
	ODM_SetBBReg(pDM_Odm, 0x808, BIT22, 0);
	//Read PSD report, Reg8B4[15:0]
	psd_report = ODM_GetBBReg(pDM_Odm,0x8B4, bMaskDWord) & 0x0000FFFF;
	
#if 1//(DEV_BUS_TYPE == RT_PCI_INTERFACE) && ( (RT_PLATFORM == PLATFORM_LINUX) || (RT_PLATFORM == PLATFORM_MACOSX))
	psd_report = (u4Byte) (ConvertTo_dB(psd_report))+(u4Byte)(initial_gain_psd-0x1c);
#else
	psd_report = (int) (20*log10((double)psd_report))+(int)(initial_gain_psd-0x1c);
#endif

	return psd_report;
	
}

u4Byte 
ConvertTo_dB(
	u4Byte 	Value)
{
	u1Byte i;
	u1Byte j;
	u4Byte dB;

	Value = Value & 0xFFFF;
	
	for (i=0;i<8;i++)
	{
		if (Value <= dB_Invert_Table[i][11])
		{
			break;
		}
	}

	if (i >= 8)
	{
		return (96);	// maximum 96 dB
	}

	for (j=0;j<12;j++)
	{
		if (Value <= dB_Invert_Table[i][j])
		{
			break;
		}
	}

	dB = i*12 + j + 1;

	return (dB);
}

#endif

//
// LukeLee: 
// PSD function will be moved to FW in future IC, but now is only implemented in MP platform
// So PSD function will not be incorporated to common ODM
//
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

#define	AFH_PSD		1	//0:normal PSD scan, 1: only do 20 pts PSD
#define	MODE_40M		0	//0:20M, 1:40M
#define	PSD_TH2		3  
#define	PSD_CHMIN		20   // Minimum channel number for BT AFH
#define	SIR_STEP_SIZE	3
#define   Smooth_Size_1 	5
#define	Smooth_TH_1	3
#define   Smooth_Size_2 	10
#define	Smooth_TH_2	4
#define   Smooth_Size_3 	20
#define	Smooth_TH_3	4
#define   Smooth_Step_Size 5
#define	Adaptive_SIR	1
//#if(RTL8723_FPGA_VERIFICATION == 1)
//#define	PSD_RESCAN		1
//#else
//#define	PSD_RESCAN		4
//#endif
#define	SCAN_INTERVAL	1500 //ms
#define	SYN_Length		5    // for 92D
	
#define	LNA_Low_Gain_1                      0x64
#define	LNA_Low_Gain_2                      0x5A
#define	LNA_Low_Gain_3                      0x58

#define	pw_th_10dB					0x0
#define	pw_th_16dB					0x3

#define	FA_RXHP_TH1                           5000
#define	FA_RXHP_TH2                           1500
#define	FA_RXHP_TH3                             800
#define	FA_RXHP_TH4                             600
#define	FA_RXHP_TH5                             500

#define	Idle_Mode					0
#define	High_TP_Mode				1
#define	Low_TP_Mode				2


VOID
odm_PSDMonitorInit(
	IN PDM_ODM_T	pDM_Odm)
{
#if (DEV_BUS_TYPE == RT_PCI_INTERFACE)|(DEV_BUS_TYPE == RT_USB_INTERFACE)
	//HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	//PSD Monitor Setting
	//Which path in ADC/DAC is turnned on for PSD: both I/Q
	ODM_SetBBReg(pDM_Odm, ODM_PSDREG, BIT10|BIT11, 0x3);
	//Ageraged number: 8
	ODM_SetBBReg(pDM_Odm, ODM_PSDREG, BIT12|BIT13, 0x1);
	pDM_Odm->bPSDinProcess = FALSE;
	pDM_Odm->bUserAssignLevel = FALSE;
	pDM_Odm->bPSDactive = FALSE;
	//pDM_Odm->bDMInitialGainEnable=TRUE;		//change the initialization to DIGinit
	//Set Debug Port
	//PHY_SetBBReg(Adapter, 0x908, bMaskDWord, 0x803);
	//PHY_SetBBReg(Adapter, 0xB34, bMaskByte0, 0x00); // pause PSD
	//PHY_SetBBReg(Adapter, 0xB38, bMaskByte0, 10); //rescan
	//PHY_SetBBReg(Adapter, 0xB38, bMaskByte2|bMaskByte3, 100); //interval

	//PlatformSetTimer( Adapter, &pHalData->PSDTriggerTimer, 0); //ms
#endif
}

VOID
PatchDCTone(
	IN	PDM_ODM_T	pDM_Odm,
	pu4Byte		PSD_report,
	u1Byte 		initial_gain_psd
)
{
	//HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	//PADAPTER	pAdapter;
	
	u4Byte	psd_report;

	//2 Switch to CH11 to patch CH9 and CH13 DC tone
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, 0x3FF, 11);
	
	if(pDM_Odm->SupportICType== ODM_RTL8192D)
	{
		if((*(pDM_Odm->pMacPhyMode) == ODM_SMSP)||(*(pDM_Odm->pMacPhyMode) == ODM_DMSP))
		{
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_CHNLBW, 0x3FF, 11);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x25, 0xfffff, 0x643BC);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x26, 0xfffff, 0xFC038);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x27, 0xfffff, 0x77C1A);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x2B, 0xfffff, 0x41289);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x2C, 0xfffff, 0x01840);
		}
		else
		{
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x25, 0xfffff, 0x643BC);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x26, 0xfffff, 0xFC038);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x27, 0xfffff, 0x77C1A);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2B, 0xfffff, 0x41289);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2C, 0xfffff, 0x01840);
		}
	}
	
	//Ch9 DC tone patch
	psd_report = GetPSDData(pDM_Odm, 96, initial_gain_psd);
	PSD_report[50] = psd_report;
	//Ch13 DC tone patch
	psd_report = GetPSDData(pDM_Odm, 32, initial_gain_psd);
	PSD_report[70] = psd_report;
	
	//2 Switch to CH3 to patch CH1 and CH5 DC tone
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, 0x3FF, 3);

	
	if(pDM_Odm->SupportICType==ODM_RTL8192D)
	{
		if((*(pDM_Odm->pMacPhyMode) == ODM_SMSP)||(*(pDM_Odm->pMacPhyMode) == ODM_DMSP))
		{
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_CHNLBW, 0x3FF, 3);
			//PHY_SetRFReg(Adapter, ODM_RF_PATH_B, 0x25, 0xfffff, 0x643BC);
			//PHY_SetRFReg(Adapter, ODM_RF_PATH_B, 0x26, 0xfffff, 0xFC038);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x27, 0xfffff, 0x07C1A);
			//PHY_SetRFReg(Adapter, ODM_RF_PATH_B, 0x2B, 0xfffff, 0x61289);
			//PHY_SetRFReg(Adapter, ODM_RF_PATH_B, 0x2C, 0xfffff, 0x01C41);
		}
		else
		{
			//PHY_SetRFReg(Adapter, ODM_RF_PATH_A, 0x25, 0xfffff, 0x643BC);
			//PHY_SetRFReg(Adapter, ODM_RF_PATH_A, 0x26, 0xfffff, 0xFC038);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x27, 0xfffff, 0x07C1A);
			//PHY_SetRFReg(Adapter, ODM_RF_PATH_A, 0x2B, 0xfffff, 0x61289);
			//PHY_SetRFReg(Adapter, ODM_RF_PATH_A, 0x2C, 0xfffff, 0x01C41);
		}
	}
	
	//Ch1 DC tone patch
	psd_report = GetPSDData(pDM_Odm, 96, initial_gain_psd);
	PSD_report[10] = psd_report;
	//Ch5 DC tone patch
	psd_report = GetPSDData(pDM_Odm, 32, initial_gain_psd);
	PSD_report[30] = psd_report;

}


VOID
GoodChannelDecision(
	PDM_ODM_T	pDM_Odm,
	pu4Byte		PSD_report,
	pu1Byte		PSD_bitmap,
	u1Byte 		RSSI_BT,
	pu1Byte		PSD_bitmap_memory)
{
	pRXHP_T			pRX_HP_Table = &pDM_Odm->DM_RXHP_Table;
	//s4Byte	TH1 =  SSBT-0x15;    // modify TH by Neil Chen
	s4Byte	TH1= RSSI_BT+0x14;
	s4Byte	TH2 = RSSI_BT+85;
	//u2Byte    TH3;
//	s4Byte	RegB34;
	u1Byte	bitmap, Smooth_size[3], Smooth_TH[3];
	//u1Byte	psd_bit;
	u4Byte	i,n,j, byte_idx, bit_idx, good_cnt, good_cnt_smoothing, Smooth_Interval[3];
	int 		start_byte_idx,start_bit_idx,cur_byte_idx, cur_bit_idx,NOW_byte_idx ;
	
//	RegB34 = PHY_QueryBBReg(Adapter,0xB34, bMaskDWord)&0xFF;

	if((pDM_Odm->SupportICType == ODM_RTL8192C)||(pDM_Odm->SupportICType == ODM_RTL8192D))
       {
            TH1 = RSSI_BT + 0x14;  
	}

	Smooth_size[0]=Smooth_Size_1;
	Smooth_size[1]=Smooth_Size_2;
	Smooth_size[2]=Smooth_Size_3;
	Smooth_TH[0]=Smooth_TH_1;
	Smooth_TH[1]=Smooth_TH_2;
	Smooth_TH[2]=Smooth_TH_3;
	Smooth_Interval[0]=16;
	Smooth_Interval[1]=15;
	Smooth_Interval[2]=13;
	good_cnt = 0;
	if(pDM_Odm->SupportICType==ODM_RTL8723A)
	{
		//2 Threshold  

		if(RSSI_BT >=41)
			TH1 = 113;	
		else if(RSSI_BT >=38)   // >= -15dBm
			TH1 = 105;                              //0x69
		else if((RSSI_BT >=33)&(RSSI_BT <38))
			TH1 = 99+(RSSI_BT-33);         //0x63
		else if((RSSI_BT >=26)&(RSSI_BT<33))
			TH1 = 99-(33-RSSI_BT)+2;     //0x5e
	 	else if((RSSI_BT >=24)&(RSSI_BT<26))
			TH1 = 88-((RSSI_BT-24)*3);   //0x58
		else if((RSSI_BT >=18)&(RSSI_BT<24))
			TH1 = 77+((RSSI_BT-18)*2);
		else if((RSSI_BT >=14)&(RSSI_BT<18))
			TH1 = 63+((RSSI_BT-14)*2);
		else if((RSSI_BT >=8)&(RSSI_BT<14))
			TH1 = 58+((RSSI_BT-8)*2);
		else if((RSSI_BT >=3)&(RSSI_BT<8))
			TH1 = 52+(RSSI_BT-3);
		else
			TH1 = 51;
	}

	for (i = 0; i< 10; i++)
		PSD_bitmap[i] = 0;
	

	 // Add By Gary
       for (i=0; i<80; i++)
	   	pRX_HP_Table->PSD_bitmap_RXHP[i] = 0;
	// End



	if(pDM_Odm->SupportICType==ODM_RTL8723A)
	{
		TH1 =TH1-SIR_STEP_SIZE;
	}
	while (good_cnt < PSD_CHMIN)
	{
		good_cnt = 0;
		if(pDM_Odm->SupportICType==ODM_RTL8723A)
		{
		if(TH1 ==TH2)
			break;
		if((TH1+SIR_STEP_SIZE) < TH2)
			TH1 += SIR_STEP_SIZE;
		else
			TH1 = TH2;
		}
		else
		{
			if(TH1==(RSSI_BT+0x1E))
             		     break;    
   			if((TH1+2) < (RSSI_BT+0x1E))
				TH1+=3;
		     	else
				TH1 = RSSI_BT+0x1E;	
             
		}
		ODM_RT_TRACE(pDM_Odm,COMP_PSD,DBG_LOUD,("PSD: decision threshold is: %d", TH1));
			 
		for (i = 0; i< 80; i++)
		{
			if((s4Byte)(PSD_report[i]) < TH1)
			{
				byte_idx = i / 8;
				bit_idx = i -8*byte_idx;
				bitmap = PSD_bitmap[byte_idx];
				PSD_bitmap[byte_idx] = bitmap | (u1Byte) (1 << bit_idx);
			}
		}

#if DBG
		ODM_RT_TRACE(pDM_Odm,COMP_PSD, DBG_LOUD,("PSD: before smoothing\n"));
		for(n=0;n<10;n++)
		{
			//DbgPrint("PSD_bitmap[%u]=%x\n", n, PSD_bitmap[n]);
			for (i = 0; i<8; i++)
				ODM_RT_TRACE(pDM_Odm,COMP_PSD, DBG_LOUD,("PSD_bitmap[%u] =   %d\n", 2402+n*8+i, (PSD_bitmap[n]&BIT(i))>>i));
		}
#endif
	
		//1 Start of smoothing function

		for (j=0;j<3;j++)
		{
			start_byte_idx=0;
			start_bit_idx=0;
			for(n=0; n<Smooth_Interval[j]; n++)
			{
				good_cnt_smoothing = 0;
				cur_bit_idx = start_bit_idx;
				cur_byte_idx = start_byte_idx;
				for ( i=0; i < Smooth_size[j]; i++)
				{
					NOW_byte_idx = cur_byte_idx + (i+cur_bit_idx)/8;
					if ( (PSD_bitmap[NOW_byte_idx]& BIT( (cur_bit_idx + i)%8)) != 0)
						good_cnt_smoothing++;

				}

				if( good_cnt_smoothing < Smooth_TH[j] )
				{
					cur_bit_idx = start_bit_idx;
					cur_byte_idx = start_byte_idx;
					for ( i=0; i< Smooth_size[j] ; i++)
					{	
						NOW_byte_idx = cur_byte_idx + (i+cur_bit_idx)/8;				
						PSD_bitmap[NOW_byte_idx] = PSD_bitmap[NOW_byte_idx] & (~BIT( (cur_bit_idx + i)%8));
					}
				}
				start_bit_idx =  start_bit_idx + Smooth_Step_Size;
				while ( (start_bit_idx)  > 7 )
				{
					start_byte_idx= start_byte_idx+start_bit_idx/8;
					start_bit_idx = start_bit_idx%8;
				}
			}

			ODM_RT_TRACE(	pDM_Odm,COMP_PSD, DBG_LOUD,("PSD: after %u smoothing", j+1));
			for(n=0;n<10;n++)
			{
				for (i = 0; i<8; i++)
				{
					ODM_RT_TRACE(pDM_Odm,COMP_PSD, DBG_LOUD,("PSD_bitmap[%u] =   %d\n", 2402+n*8+i, (PSD_bitmap[n]&BIT(i))>>i));
					
					if ( ((PSD_bitmap[n]&BIT(i))>>i) ==1)  //----- Add By Gary
					{
	                                   pRX_HP_Table->PSD_bitmap_RXHP[8*n+i] = 1;
					}                                                  // ------end by Gary
				}
			}

		}

	
		good_cnt = 0;
		for ( i = 0; i < 10; i++)
		{
			for (n = 0; n < 8; n++)
				if((PSD_bitmap[i]& BIT(n)) != 0)
					good_cnt++;
		}
		ODM_RT_TRACE(pDM_Odm,COMP_PSD, DBG_LOUD,("PSD: good channel cnt = %u",good_cnt));
	}

	//RT_TRACE(COMP_PSD, DBG_LOUD,("PSD: SSBT=%d, TH2=%d, TH1=%d",SSBT,TH2,TH1));
	for (i = 0; i <10; i++)
		ODM_RT_TRACE(pDM_Odm,COMP_PSD, DBG_LOUD,("PSD: PSD_bitmap[%u]=%x",i,PSD_bitmap[i]));
/*	
	//Update bitmap memory
	for(i = 0; i < 80; i++)
	{
		byte_idx = i / 8;
		bit_idx = i -8*byte_idx;
		psd_bit = (PSD_bitmap[byte_idx] & BIT(bit_idx)) >> bit_idx;
		bitmap = PSD_bitmap_memory[i]; 
		PSD_bitmap_memory[i] = (bitmap << 1) |psd_bit;
	}
*/
}



VOID
odm_PSD_Monitor(
	PDM_ODM_T	pDM_Odm
)
{
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	//PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;

	unsigned int 		pts, start_point, stop_point;
	u1Byte			initial_gain ;
	static u1Byte		PSD_bitmap_memory[80], init_memory = 0;
	static u1Byte 		psd_cnt=0;
	static u4Byte		PSD_report[80], PSD_report_tmp;
	static u8Byte		lastTxOkCnt=0, lastRxOkCnt=0;
	u1Byte 			H2C_PSD_DATA[5]={0,0,0,0,0};
	static u1Byte		H2C_PSD_DATA_last[5] ={0,0,0,0,0};
	u1Byte			idx[20]={96,99,102,106,109,112,115,118,122,125,
					0,3,6,10,13,16,19,22,26,29};
	u1Byte			n, i, channel, BBReset,tone_idx;
	u1Byte			PSD_bitmap[10], SSBT=0,initial_gain_psd=0, RSSI_BT=0, initialGainUpper;
	s4Byte    			PSD_skip_start, PSD_skip_stop;
	u4Byte			CurrentChannel, RXIQI, RxIdleLowPwr, wlan_channel;
	u4Byte			ReScan, Interval, Is40MHz;
	u8Byte			curTxOkCnt, curRxOkCnt;
	int 				cur_byte_idx, cur_bit_idx;
	PADAPTER		Adapter = pDM_Odm->Adapter;
	PMGNT_INFO      	pMgntInfo = &Adapter->MgntInfo;
	
	if( (*(pDM_Odm->pbScanInProcess)) ||
		pDM_Odm->bLinkInProcess)
	{
		if((pDM_Odm->SupportICType==ODM_RTL8723A)&(pDM_Odm->SupportInterface==ODM_ITRF_PCIE))
		{
			ODM_SetTimer( pDM_Odm, &pDM_Odm->PSDTimer, 1500); //ms	
			//psd_cnt=0;
		}
		return;
	}

	if(pDM_Odm->bBtHsOperation)
	{
		ReScan = 1;
		Interval = SCAN_INTERVAL;
	}
	else
	{
	ReScan = PSD_RESCAN;
	Interval = SCAN_INTERVAL;
	}

	//1 Initialization
	if(init_memory == 0)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("Init memory\n"));
		for(i = 0; i < 80; i++)
			PSD_bitmap_memory[i] = 0xFF; // channel is always good
		init_memory = 1;
	}
	if(psd_cnt == 0)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("Enter dm_PSD_Monitor\n"));
		for(i = 0; i < 80; i++)
			PSD_report[i] = 0;
	}

	//1 Backup Current Settings
	CurrentChannel = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask);
/*
	if(pDM_Odm->SupportICType==ODM_RTL8192D)
	{
		//2 Record Current synthesizer parameters based on current channel
		if((*pDM_Odm->MacPhyMode92D == SINGLEMAC_SINGLEPHY)||(*pDM_Odm->MacPhyMode92D == DUALMAC_SINGLEPHY))
		{
			SYN_RF25 = ODM_GetRFReg(Adapter, ODM_RF_PATH_B, 0x25, bMaskDWord);
			SYN_RF26 = ODM_GetRFReg(Adapter, ODM_RF_PATH_B, 0x26, bMaskDWord);
			SYN_RF27 = ODM_GetRFReg(Adapter, ODM_RF_PATH_B, 0x27, bMaskDWord);
			SYN_RF2B = ODM_GetRFReg(Adapter, ODM_RF_PATH_B, 0x2B, bMaskDWord);
			SYN_RF2C = ODM_GetRFReg(Adapter, ODM_RF_PATH_B, 0x2C, bMaskDWord);
       	}
		else     // DualMAC_DualPHY 2G
		{
			SYN_RF25 = ODM_GetRFReg(Adapter, ODM_RF_PATH_A, 0x25, bMaskDWord);
			SYN_RF26 = ODM_GetRFReg(Adapter, ODM_RF_PATH_A, 0x26, bMaskDWord);
			SYN_RF27 = ODM_GetRFReg(Adapter, ODM_RF_PATH_A, 0x27, bMaskDWord);
			SYN_RF2B = ODM_GetRFReg(Adapter, ODM_RF_PATH_A, 0x2B, bMaskDWord);
			SYN_RF2C = ODM_GetRFReg(Adapter, ODM_RF_PATH_A, 0x2C, bMaskDWord);
		}
	}
*/
	//RXIQI = PHY_QueryBBReg(Adapter, 0xC14, bMaskDWord);
	RXIQI = ODM_GetBBReg(pDM_Odm, 0xC14, bMaskDWord);

	//RxIdleLowPwr = (PHY_QueryBBReg(Adapter, 0x818, bMaskDWord)&BIT28)>>28;
	RxIdleLowPwr = (ODM_GetBBReg(pDM_Odm, 0x818, bMaskDWord)&BIT28)>>28;

	//2???
	if(CHNL_RUN_ABOVE_40MHZ(pMgntInfo))
		Is40MHz = TRUE;
	else
		Is40MHz = FALSE;

	ODM_RT_TRACE(pDM_Odm,	ODM_COMP_PSD, DBG_LOUD,("PSD Scan Start\n"));
	//1 Turn off CCK
	//PHY_SetBBReg(Adapter, rFPGA0_RFMOD, BIT24, 0);
	ODM_SetBBReg(pDM_Odm, rFPGA0_RFMOD, BIT24, 0);
	//1 Turn off TX
	//Pause TX Queue
	//PlatformEFIOWrite1Byte(Adapter, REG_TXPAUSE, 0xFF);
	ODM_Write1Byte(pDM_Odm,REG_TXPAUSE, 0xFF);
	
	//Force RX to stop TX immediately
	//PHY_SetRFReg(Adapter, ODM_RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x32E13);

	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x32E13);
	//1 Turn off RX
	//Rx AGC off  RegC70[0]=0, RegC7C[20]=0
	//PHY_SetBBReg(Adapter, 0xC70, BIT0, 0);
	//PHY_SetBBReg(Adapter, 0xC7C, BIT20, 0);

	ODM_SetBBReg(pDM_Odm, 0xC70, BIT0, 0);
	ODM_SetBBReg(pDM_Odm, 0xC7C, BIT20, 0);

	
	//Turn off CCA
	//PHY_SetBBReg(Adapter, 0xC14, bMaskDWord, 0x0);
	ODM_SetBBReg(pDM_Odm, 0xC14, bMaskDWord, 0x0);
	
	//BB Reset
	//BBReset = PlatformEFIORead1Byte(Adapter, 0x02);
	BBReset = ODM_Read1Byte(pDM_Odm, 0x02);
	
	//PlatformEFIOWrite1Byte(Adapter, 0x02, BBReset&(~BIT0));
	//PlatformEFIOWrite1Byte(Adapter, 0x02, BBReset|BIT0);
	ODM_SetBBReg(pDM_Odm, 0x87C, BIT31, 1); //clock gated to prevent from AGC table mess 
	ODM_Write1Byte(pDM_Odm,  0x02, BBReset&(~BIT0));
	ODM_Write1Byte(pDM_Odm,  0x02, BBReset|BIT0);
	ODM_SetBBReg(pDM_Odm, 0x87C, BIT31, 0);
	
	//1 Leave RX idle low power
	//PHY_SetBBReg(Adapter, 0x818, BIT28, 0x0);

	ODM_SetBBReg(pDM_Odm, 0x818, BIT28, 0x0);
	//1 Fix initial gain
	//if (IS_HARDWARE_TYPE_8723AE(Adapter))
	//RSSI_BT = pHalData->RSSI_BT;
       //else if((IS_HARDWARE_TYPE_8192C(Adapter))||(IS_HARDWARE_TYPE_8192D(Adapter)))      // Add by Gary
       //    RSSI_BT = RSSI_BT_new;

	if((pDM_Odm->SupportICType==ODM_RTL8723A)&(pDM_Odm->SupportInterface==ODM_ITRF_PCIE))
	RSSI_BT=pDM_Odm->RSSI_BT;		//need to check C2H to pDM_Odm RSSI BT

	if(RSSI_BT>=47)
		RSSI_BT=47;
	   
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("PSD: RSSI_BT= %d\n", RSSI_BT));
	
	if(pDM_Odm->SupportICType==ODM_RTL8723A)
	{
	       //Neil add--2011--10--12
		//2 Initial Gain index 
		if(RSSI_BT >=35)   // >= -15dBm
			initial_gain_psd = RSSI_BT*2;
		else if((RSSI_BT >=33)&(RSSI_BT<35))
			initial_gain_psd = RSSI_BT*2+6;
		else if((RSSI_BT >=24)&(RSSI_BT<33))
			initial_gain_psd = 70-(33-RSSI_BT);
	 	else if((RSSI_BT >=19)&(RSSI_BT<24))
			initial_gain_psd = 64-((24-RSSI_BT)*4);
		else if((RSSI_BT >=14)&(RSSI_BT<19))
			initial_gain_psd = 44-((18-RSSI_BT)*2);
		else if((RSSI_BT >=8)&(RSSI_BT<14))
			initial_gain_psd = 35-(14-RSSI_BT);
		else
			initial_gain_psd = 0x1B;
	}
	else
	{
	
		//need to do	
         	initial_gain_psd = pDM_Odm->RSSI_Min;    // PSD report based on RSSI
           	//}  	
	}
	//if(RSSI_BT<0x17)
	//	RSSI_BT +=3;
	//DbgPrint("PSD: RSSI_BT= %d\n", RSSI_BT);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("PSD: RSSI_BT= %d\n", RSSI_BT));

	//initialGainUpper = 0x5E;  //Modify by neil chen
	
	if(pDM_Odm->bUserAssignLevel)
	{
		pDM_Odm->bUserAssignLevel = FALSE;
		initialGainUpper = 0x7f;
	}
	else
	{
		initialGainUpper = 0x5E;
	}
	
	/*
	if (initial_gain_psd < 0x1a)
		initial_gain_psd = 0x1a;
	if (initial_gain_psd > initialGainUpper)
		initial_gain_psd = initialGainUpper;
	*/

	//if(pDM_Odm->SupportICType==ODM_RTL8723A)
	SSBT = RSSI_BT  * 2 +0x3E;
	
	
	//if(IS_HARDWARE_TYPE_8723AE(Adapter))
	//	SSBT = RSSI_BT  * 2 +0x3E;
	//else if((IS_HARDWARE_TYPE_8192C(Adapter))||(IS_HARDWARE_TYPE_8192D(Adapter)))   // Add by Gary
	//{
	//	RSSI_BT = initial_gain_psd;
	//	SSBT = RSSI_BT;
	//}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("PSD: SSBT= %d\n", SSBT));
	ODM_RT_TRACE(	pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("PSD: initial gain= 0x%x\n", initial_gain_psd));
	//DbgPrint("PSD: SSBT= %d", SSBT);
	//need to do
	//pMgntInfo->bDMInitialGainEnable = FALSE;
	pDM_Odm->bDMInitialGainEnable = FALSE;
	initial_gain =(u1Byte) (ODM_GetBBReg(pDM_Odm, 0xc50, bMaskDWord) & 0x7F);
	
        // make sure the initial gain is under the correct range.
	//initial_gain_psd &= 0x7f;
	ODM_Write_DIG(pDM_Odm, initial_gain_psd);
	//1 Turn off 3-wire
	ODM_SetBBReg(pDM_Odm, 0x88c, BIT20|BIT21|BIT22|BIT23, 0xF);

	//pts value = 128, 256, 512, 1024
	pts = 128;

	if(pts == 128)
	{
		ODM_SetBBReg(pDM_Odm, 0x808, BIT14|BIT15, 0x0);
		start_point = 64;
		stop_point = 192;
	}
	else if(pts == 256)
	{
		ODM_SetBBReg(pDM_Odm, 0x808, BIT14|BIT15, 0x1);
		start_point = 128;
		stop_point = 384;
	}
	else if(pts == 512)
	{
		ODM_SetBBReg(pDM_Odm, 0x808, BIT14|BIT15, 0x2);
		start_point = 256;
		stop_point = 768;
	}
	else
	{
		ODM_SetBBReg(pDM_Odm, 0x808, BIT14|BIT15, 0x3);
		start_point = 512;
		stop_point = 1536;
	}
	

//3 Skip WLAN channels if WLAN busy

	curTxOkCnt = *(pDM_Odm->pNumTxBytesUnicast) - lastTxOkCnt;
	curRxOkCnt = *(pDM_Odm->pNumRxBytesUnicast) - lastRxOkCnt;
	lastTxOkCnt = *(pDM_Odm->pNumTxBytesUnicast);
	lastRxOkCnt = *(pDM_Odm->pNumRxBytesUnicast);	

	PSD_skip_start=80;
	PSD_skip_stop = 0;
	wlan_channel = CurrentChannel & 0x0f;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD,DBG_LOUD,("PSD: current channel: %x, BW:%d \n", wlan_channel, Is40MHz));
	if(pDM_Odm->SupportICType==ODM_RTL8723A)
	{
		if(pDM_Odm->bBtHsOperation)
		{
			if(pDM_Odm->bLinked)
			{
				if(Is40MHz)
				{
					PSD_skip_start = ((wlan_channel-1)*5 -Is40MHz*10)-2;  // Modify by Neil to add 10 chs to mask
					PSD_skip_stop = (PSD_skip_start + (1+Is40MHz)*20)+4;
				}
				else
				{
					PSD_skip_start = ((wlan_channel-1)*5 -Is40MHz*10)-10;  // Modify by Neil to add 10 chs to mask
					PSD_skip_stop = (PSD_skip_start + (1+Is40MHz)*20)+18; 
				}
			}
			else
			{
				// mask for 40MHz
				PSD_skip_start = ((wlan_channel-1)*5 -Is40MHz*10)-2;  // Modify by Neil to add 10 chs to mask
				PSD_skip_stop = (PSD_skip_start + (1+Is40MHz)*20)+4;
			}
			if(PSD_skip_start < 0)
				PSD_skip_start = 0;
			if(PSD_skip_stop >80)
				PSD_skip_stop = 80;
		}
		else
		{
			if((curRxOkCnt+curTxOkCnt) > 5)
			{
				if(Is40MHz)
				{
					PSD_skip_start = ((wlan_channel-1)*5 -Is40MHz*10)-2;  // Modify by Neil to add 10 chs to mask
					PSD_skip_stop = (PSD_skip_start + (1+Is40MHz)*20)+4;
				}
				else
				{
					PSD_skip_start = ((wlan_channel-1)*5 -Is40MHz*10)-10;  // Modify by Neil to add 10 chs to mask
					PSD_skip_stop = (PSD_skip_start + (1+Is40MHz)*20)+18; 
				}
				
				if(PSD_skip_start < 0)
					PSD_skip_start = 0;
				if(PSD_skip_stop >80)
					PSD_skip_stop = 80;
			}
		}
	}
#if 0	
	else
	{
		if((curRxOkCnt+curTxOkCnt) > 1000)
		{
			PSD_skip_start = (wlan_channel-1)*5 -Is40MHz*10;
			PSD_skip_stop = PSD_skip_start + (1+Is40MHz)*20;
		}
	}   
#endif  //Reove RXHP Issue
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD,DBG_LOUD,("PSD: Skip tone from %d to %d \n", PSD_skip_start, PSD_skip_stop));

 	for (n=0;n<80;n++)
 	{
 		if((n%20)==0)
 		{
			channel = (n/20)*4 + 1;
					
					ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, 0x3FF, channel);
				}
		tone_idx = n%20;
		if ((n>=PSD_skip_start) && (n<PSD_skip_stop))
		{	
			PSD_report[n] = SSBT;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD,DBG_LOUD,("PSD:Tone %d skipped \n", n));
		}
		else
		{
			PSD_report_tmp =  GetPSDData(pDM_Odm, idx[tone_idx], initial_gain_psd);

			if ( PSD_report_tmp > PSD_report[n])
				PSD_report[n] = PSD_report_tmp;
				
		}
	}

	PatchDCTone(pDM_Odm, PSD_report, initial_gain_psd);
      
       //----end
	//1 Turn on RX
	//Rx AGC on
	ODM_SetBBReg(pDM_Odm, 0xC70, BIT0, 1);
	ODM_SetBBReg(pDM_Odm, 0xC7C, BIT20, 1);
	//CCK on
	ODM_SetBBReg(pDM_Odm, rFPGA0_RFMOD, BIT24, 1);
	//1 Turn on TX
	//Resume TX Queue
	
	ODM_Write1Byte(pDM_Odm,REG_TXPAUSE, 0x00);
	//Turn on 3-wire
	ODM_SetBBReg(pDM_Odm, 0x88c, BIT20|BIT21|BIT22|BIT23, 0x0);
	//1 Restore Current Settings
	//Resume DIG
	pDM_Odm->bDMInitialGainEnable = TRUE;
	
	ODM_Write_DIG(pDM_Odm, initial_gain);

	// restore originl center frequency
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, CurrentChannel);

	//Turn on CCA
	ODM_SetBBReg(pDM_Odm, 0xC14, bMaskDWord, RXIQI);
	//Restore RX idle low power
	if(RxIdleLowPwr == TRUE)
		ODM_SetBBReg(pDM_Odm, 0x818, BIT28, 1);
	
	psd_cnt++;
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("PSD:psd_cnt = %d \n",psd_cnt));
	if (psd_cnt < ReScan)
		ODM_SetTimer(pDM_Odm, &pDM_Odm->PSDTimer, Interval);		
	else
	{
		psd_cnt = 0;
		for(i=0;i<80;i++)
			//DbgPrint("psd_report[%d]=     %d \n", 2402+i, PSD_report[i]);
			RT_TRACE(	COMP_PSD, DBG_LOUD,("psd_report[%d]=     %d \n", 2402+i, PSD_report[i]));


		GoodChannelDecision(pDM_Odm, PSD_report, PSD_bitmap,RSSI_BT, PSD_bitmap_memory);

		if(pDM_Odm->SupportICType==ODM_RTL8723A)
		{
			cur_byte_idx=0;
			cur_bit_idx=0;

			//2 Restore H2C PSD Data to Last Data
		  	H2C_PSD_DATA_last[0] = H2C_PSD_DATA[0];
			H2C_PSD_DATA_last[1] = H2C_PSD_DATA[1];
			H2C_PSD_DATA_last[2] = H2C_PSD_DATA[2];
			H2C_PSD_DATA_last[3] = H2C_PSD_DATA[3];
			H2C_PSD_DATA_last[4] = H2C_PSD_DATA[4];

	
			//2 Translate 80bit channel map to 40bit channel	
			for ( i=0;i<5;i++)
			{
				for(n=0;n<8;n++)
				{
					cur_byte_idx = i*2 + n/4;
					cur_bit_idx = (n%4)*2;
					if ( ((PSD_bitmap[cur_byte_idx]& BIT(cur_bit_idx)) != 0) && ((PSD_bitmap[cur_byte_idx]& BIT(cur_bit_idx+1)) != 0))
						H2C_PSD_DATA[i] = H2C_PSD_DATA[i] | (u1Byte) (1 << n);
				}
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("H2C_PSD_DATA[%d]=0x%x\n" ,i, H2C_PSD_DATA[i]));
			}
	
			//3 To Compare the difference
			for ( i=0;i<5;i++)
			{
				if(H2C_PSD_DATA[i] !=H2C_PSD_DATA_last[i])
				{
					FillH2CCmd(Adapter, H2C_92C_PSD_RESULT, 5, H2C_PSD_DATA);
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_PSD, DBG_LOUD,("Need to Update the AFH Map \n"));
					break;
				}
				else
				{
					if(i==5)
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("Not need to Update\n"));	
				}
			}
			if(pDM_Odm->bBtHsOperation)
			{
				ODM_SetTimer(pDM_Odm, &pDM_Odm->PSDTimer, 10000);
				ODM_RT_TRACE(	pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("Leave dm_PSD_Monitor\n"));		
			}
			else
			{
				ODM_SetTimer(pDM_Odm, &pDM_Odm->PSDTimer, 1500);
				ODM_RT_TRACE(	pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("Leave dm_PSD_Monitor\n"));		
		}
	}
    }
}
/*
//Neil for Get BT RSSI
// Be Triggered by BT C2H CMD
VOID
ODM_PSDGetRSSI(
	IN	u1Byte	RSSI_BT)
{


}

*/

VOID
ODM_PSDMonitor(
	IN	PDM_ODM_T	pDM_Odm
	)
{
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	
	//if(IS_HARDWARE_TYPE_8723AE(Adapter))
	
	if(pDM_Odm->SupportICType == ODM_RTL8723A)   //may need to add other IC type
	{
		if(pDM_Odm->SupportInterface==ODM_ITRF_PCIE)
		{
			if(pDM_Odm->bBtDisabled) //need to check upper layer connection
			{
				pDM_Odm->bPSDactive=FALSE;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD, ("odm_PSDMonitor, return for BT is disabled!!!\n"));
		   		return; 
			}

			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD, ("odm_PSDMonitor\n"));
		//{
			pDM_Odm->bPSDinProcess = TRUE;
	 		pDM_Odm->bPSDactive=TRUE;
			odm_PSD_Monitor(pDM_Odm);
			pDM_Odm->bPSDinProcess = FALSE;
		}	
	}	

}
VOID
odm_PSDMonitorCallback(
	PRT_TIMER		pTimer
)
{
	PADAPTER		Adapter = (PADAPTER)pTimer->Adapter;
       HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	PlatformScheduleWorkItem(&pHalData->PSDMonitorWorkitem);
}

VOID
odm_PSDMonitorWorkItemCallback(
    IN PVOID            pContext
    )
{
	PADAPTER	Adapter = (PADAPTER)pContext;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;

	ODM_PSDMonitor(pDM_Odm);
}

//Remove by Yuchen (seperate to odm_DIG.c)

 //cosa debug tool need to modify

VOID
ODM_PSDDbgControl(
	IN	PADAPTER	Adapter,
	IN	u4Byte		mode,
	IN	u4Byte		btRssi
	)
{
#if (DEV_BUS_TYPE == RT_PCI_INTERFACE)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;

	ODM_RT_TRACE(pDM_Odm,COMP_PSD, DBG_LOUD, (" Monitor mode=%d, btRssi=%d\n", mode, btRssi));
	if(mode)
	{
		pDM_Odm->RSSI_BT = (u1Byte)btRssi;
		pDM_Odm->bUserAssignLevel = TRUE;
		ODM_SetTimer( pDM_Odm, &pDM_Odm->PSDTimer, 0); //ms		
	}
	else
	{
		ODM_CancelTimer(pDM_Odm, &pDM_Odm->PSDTimer);
	}
#endif
}


//#if(DEV_BUS_TYPE == RT_PCI_INTERFACE)|(DEV_BUS_TYPE == RT_USB_INTERFACE)

void	odm_RXHPInit(
	IN		PDM_ODM_T		pDM_Odm)
{
#if (DEV_BUS_TYPE == RT_PCI_INTERFACE)|(DEV_BUS_TYPE == RT_USB_INTERFACE)
	pRXHP_T			pRX_HP_Table  = &pDM_Odm->DM_RXHP_Table;
   	u1Byte			index;

	pRX_HP_Table->RXHP_enable = TRUE;
	pRX_HP_Table->RXHP_flag = 0;
	pRX_HP_Table->PSD_func_trigger = 0;
	pRX_HP_Table->Pre_IGI = 0x20;
	pRX_HP_Table->Cur_IGI = 0x20;
	pRX_HP_Table->Cur_pw_th = pw_th_10dB;
	pRX_HP_Table->Pre_pw_th = pw_th_10dB;
	for(index=0; index<80; index++)
		pRX_HP_Table->PSD_bitmap_RXHP[index] = 1;

#if(DEV_BUS_TYPE == RT_USB_INTERFACE)
	pRX_HP_Table->TP_Mode = Idle_Mode;
#endif
#endif
}

void odm_RXHP(
	IN		PDM_ODM_T		pDM_Odm)
{
#if( DM_ODM_SUPPORT_TYPE & (ODM_WIN))
#if (DEV_BUS_TYPE == RT_PCI_INTERFACE) | (DEV_BUS_TYPE == RT_USB_INTERFACE)
	PADAPTER	Adapter =  pDM_Odm->Adapter;
	PMGNT_INFO	pMgntInfo = &(Adapter->MgntInfo);
	pDIG_T		pDM_DigTable = &pDM_Odm->DM_DigTable;
	pRXHP_T		pRX_HP_Table  = &pDM_Odm->DM_RXHP_Table;
       PFALSE_ALARM_STATISTICS		FalseAlmCnt = &(pDM_Odm->FalseAlmCnt);
	
	u1Byte              	i, j, sum;
	u1Byte			Is40MHz;
	s1Byte              	Intf_diff_idx, MIN_Intf_diff_idx = 16;   
       s4Byte              	cur_channel;    
       u1Byte              	ch_map_intf_5M[17] = {0};     
       static u4Byte		FA_TH = 0;	
	static u1Byte      	psd_intf_flag = 0;
	static s4Byte      	curRssi = 0;                
       static s4Byte  		preRssi = 0;                                                                
	static u1Byte		PSDTriggerCnt = 1;
	
	u1Byte			RX_HP_enable = (u1Byte)(ODM_GetBBReg(pDM_Odm, rOFDM0_XAAGCCore2, bMaskDWord)>>31);   // for debug!!

#if(DEV_BUS_TYPE == RT_USB_INTERFACE)	
	static s8Byte  		lastTxOkCnt = 0, lastRxOkCnt = 0;  
       s8Byte			curTxOkCnt, curRxOkCnt;
	s8Byte			curTPOkCnt;
	s8Byte			TP_Acc3, TP_Acc5;
	static s8Byte		TP_Buff[5] = {0};
	static u1Byte		pre_state = 0, pre_state_flag = 0;
	static u1Byte		Intf_HighTP_flag = 0, De_counter = 16; 
	static u1Byte		TP_Degrade_flag = 0;
#endif	   
	static u1Byte		LatchCnt = 0;
	
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8188E))
		return;
	//AGC RX High Power Mode is only applied on 2G band in 92D!!!
	if(pDM_Odm->SupportICType == ODM_RTL8192D)
	{
		if(*(pDM_Odm->pBandType) != ODM_BAND_2_4G)
			return;
	}

	if(!(pDM_Odm->SupportAbility==ODM_BB_RXHP))
		return;


	//RX HP ON/OFF
	if(RX_HP_enable == 1)
		pRX_HP_Table->RXHP_enable = FALSE;
	else
		pRX_HP_Table->RXHP_enable = TRUE;

	if(pRX_HP_Table->RXHP_enable == FALSE)
	{
		if(pRX_HP_Table->RXHP_flag == 1)
		{
			pRX_HP_Table->RXHP_flag = 0;
			psd_intf_flag = 0;
		}
		return;
	}

#if(DEV_BUS_TYPE == RT_USB_INTERFACE)	
	//2 Record current TP for USB interface
	curTxOkCnt = *(pDM_Odm->pNumTxBytesUnicast)-lastTxOkCnt;
	curRxOkCnt = *(pDM_Odm->pNumRxBytesUnicast)-lastRxOkCnt;
	lastTxOkCnt = *(pDM_Odm->pNumTxBytesUnicast);
	lastRxOkCnt = *(pDM_Odm->pNumRxBytesUnicast);

	curTPOkCnt = curTxOkCnt+curRxOkCnt;
	TP_Buff[0] = curTPOkCnt;    // current TP  
	TP_Acc3 = PlatformDivision64((TP_Buff[1]+TP_Buff[2]+TP_Buff[3]), 3);
	TP_Acc5 = PlatformDivision64((TP_Buff[0]+TP_Buff[1]+TP_Buff[2]+TP_Buff[3]+TP_Buff[4]), 5);
	
	if(TP_Acc5 < 1000)
		pRX_HP_Table->TP_Mode = Idle_Mode;
	else if((1000 < TP_Acc5)&&(TP_Acc5 < 3750000))
		pRX_HP_Table->TP_Mode = Low_TP_Mode;
	else
		pRX_HP_Table->TP_Mode = High_TP_Mode;

	ODM_RT_TRACE(pDM_Odm, 	ODM_COMP_RXHP, ODM_DBG_LOUD, ("RX HP TP Mode = %d\n", pRX_HP_Table->TP_Mode));
	// Since TP result would be sampled every 2 sec, it needs to delay 4sec to wait PSD processing.
	// When LatchCnt = 0, we would Get PSD result.
	if(TP_Degrade_flag == 1)
	{
		LatchCnt--;
		if(LatchCnt == 0)
		{
			TP_Degrade_flag = 0;
		}
	}
	// When PSD function triggered by TP degrade 20%, and Interference Flag = 1
	// Set a De_counter to wait IGI = upper bound. If time is UP, the Interference flag will be pull down.
	if(Intf_HighTP_flag == 1)
	{
		De_counter--;
		if(De_counter == 0)
		{
			Intf_HighTP_flag = 0;
			psd_intf_flag = 0;
		}
	}
#endif

	//2 AGC RX High Power Mode by PSD only applied to STA Mode
	//3 NOT applied 1. Ad Hoc Mode.
	//3 NOT applied 2. AP Mode
	if ((pMgntInfo->mAssoc) && (!pMgntInfo->mIbss) && (!ACTING_AS_AP(Adapter)))
	{    
		Is40MHz = *(pDM_Odm->pBandWidth);
		curRssi = pDM_Odm->RSSI_Min;
		cur_channel = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, 0x0fff) & 0x0f;
		ODM_RT_TRACE(pDM_Odm, 	ODM_COMP_RXHP, ODM_DBG_LOUD, ("RXHP RX HP flag = %d\n", pRX_HP_Table->RXHP_flag));
		ODM_RT_TRACE(pDM_Odm,	ODM_COMP_RXHP, ODM_DBG_LOUD, ("RXHP FA = %d\n", FalseAlmCnt->Cnt_all));
		ODM_RT_TRACE(pDM_Odm,	ODM_COMP_RXHP, ODM_DBG_LOUD, ("RXHP cur RSSI = %d, pre RSSI=%d\n", curRssi, preRssi));
		ODM_RT_TRACE(pDM_Odm,	ODM_COMP_RXHP, ODM_DBG_LOUD, ("RXHP current CH = %d\n", cur_channel));
		ODM_RT_TRACE(pDM_Odm,	ODM_COMP_RXHP, ODM_DBG_LOUD, ("RXHP Is 40MHz = %d\n", Is40MHz));
       	//2 PSD function would be triggered 
       	//3 1. Every 4 sec for PCIE
       	//3 2. Before TP Mode (Idle TP<4kbps) for USB
       	//3 3. After TP Mode (High TP) for USB 
		if((curRssi > 68) && (pRX_HP_Table->RXHP_flag == 0))	// Only RSSI>TH and RX_HP_flag=0 will Do PSD process 
		{
#if (DEV_BUS_TYPE == RT_USB_INTERFACE)
			//2 Before TP Mode ==> PSD would be trigger every 4 sec
			if(pRX_HP_Table->TP_Mode == Idle_Mode)		//2.1 less wlan traffic <4kbps
			{
#endif
				if(PSDTriggerCnt == 1)       
				{    	
					odm_PSD_RXHP(pDM_Odm);
					pRX_HP_Table->PSD_func_trigger = 1;
					PSDTriggerCnt = 0;
				}
				else
				{
             				PSDTriggerCnt++;
				}
#if(DEV_BUS_TYPE == RT_USB_INTERFACE)
			}	
			//2 After TP Mode ==> Check if TP degrade larger than 20% would trigger PSD function
			if(pRX_HP_Table->TP_Mode == High_TP_Mode)
			{
				if((pre_state_flag == 0)&&(LatchCnt == 0)) 
				{
					// TP var < 5%
					if((((curTPOkCnt-TP_Acc3)*20)<(TP_Acc3))&&(((curTPOkCnt-TP_Acc3)*20)>(-TP_Acc3)))
					{
						pre_state++;
						if(pre_state == 3)      // hit pre_state condition => consecutive 3 times
						{
							pre_state_flag = 1;
							pre_state = 0;
						}

					}
					else
					{
						pre_state = 0;
					}
				}
				//3 If pre_state_flag=1 ==> start to monitor TP degrade 20%
				if(pre_state_flag == 1)		
				{
					if(((TP_Acc3-curTPOkCnt)*5)>(TP_Acc3))      // degrade 20%
					{
						odm_PSD_RXHP(pDM_Odm);
						pRX_HP_Table->PSD_func_trigger = 1;
						TP_Degrade_flag = 1;
						LatchCnt = 2;
						pre_state_flag = 0;
					}
					else if(((TP_Buff[2]-curTPOkCnt)*5)>TP_Buff[2])
					{
						odm_PSD_RXHP(pDM_Odm);
						pRX_HP_Table->PSD_func_trigger = 1;
						TP_Degrade_flag = 1;
						LatchCnt = 2;
						pre_state_flag = 0;
					}
					else if(((TP_Buff[3]-curTPOkCnt)*5)>TP_Buff[3])
					{
						odm_PSD_RXHP(pDM_Odm);
						pRX_HP_Table->PSD_func_trigger = 1;
						TP_Degrade_flag = 1;
						LatchCnt = 2;
						pre_state_flag = 0;
					}
				}
			}
#endif
}

#if (DEV_BUS_TYPE == RT_USB_INTERFACE)
		for (i=0;i<4;i++)
		{
			TP_Buff[4-i] = TP_Buff[3-i];
		}
#endif
		//2 Update PSD bitmap according to PSD report 
		if((pRX_HP_Table->PSD_func_trigger == 1)&&(LatchCnt == 0))
    		{	
           		//2 Separate 80M bandwidth into 16 group with smaller 5M BW.
			for (i = 0 ; i < 16 ; i++)
           		{
				sum = 0;
				for(j = 0; j < 5 ; j++)
                			sum += pRX_HP_Table->PSD_bitmap_RXHP[5*i + j];
            
                		if(sum < 5)
                		{
                			ch_map_intf_5M[i] = 1;  // interference flag
                		}
           		}
			//=============just for debug=========================
			//for(i=0;i<16;i++)
				//DbgPrint("RX HP: ch_map_intf_5M[%d] = %d\n", i, ch_map_intf_5M[i]);
			//===============================================
			//2 Mask target channel 5M index
	    		for(i = 0; i < (4+4*Is40MHz) ; i++)
           		{
				ch_map_intf_5M[cur_channel - (1+2*Is40MHz) + i] = 0;  
           		}
				
           		psd_intf_flag = 0;
	    		for(i = 0; i < 16; i++)
           		{
         			if(ch_map_intf_5M[i] == 1)
	              	{
	              		psd_intf_flag = 1;            // interference is detected!!!	
	              		break;
         			}
	    		}
				
#if (DEV_BUS_TYPE == RT_USB_INTERFACE)
			if(pRX_HP_Table->TP_Mode!=Idle_Mode)
			{
				if(psd_intf_flag == 1)     // to avoid psd_intf_flag always 1
				{
					Intf_HighTP_flag = 1;
					De_counter = 32;     // 0x1E -> 0x3E needs 32 times by each IGI step =1
				}
			}
#endif
			ODM_RT_TRACE(pDM_Odm,	ODM_COMP_RXHP, ODM_DBG_LOUD, ("RX HP psd_intf_flag = %d\n", psd_intf_flag));
			//2 Distance between target channel and interference
           		for(i = 0; i < 16; i++)
          		{
				if(ch_map_intf_5M[i] == 1)
                		{
					Intf_diff_idx = ((cur_channel+Is40MHz-(i+1))>0) ? (s1Byte)(cur_channel-2*Is40MHz-(i-2)) : (s1Byte)((i+1)-(cur_channel+2*Is40MHz));  
                      		if(Intf_diff_idx < MIN_Intf_diff_idx)
						MIN_Intf_diff_idx = Intf_diff_idx;    // the min difference index between interference and target
		  		}
	    		}
	    		ODM_RT_TRACE(pDM_Odm,	ODM_COMP_RXHP, ODM_DBG_LOUD, ("RX HP MIN_Intf_diff_idx = %d\n", MIN_Intf_diff_idx)); 
			//2 Choose False Alarm Threshold
			switch (MIN_Intf_diff_idx){
      				case 0: 
	   			case 1:
	        		case 2:
	        		case 3:	 	 
                 			FA_TH = FA_RXHP_TH1;  
                     		break;
	        		case 4:				// CH5
	        		case 5:				// CH6
		   			FA_TH = FA_RXHP_TH2;	
               			break;
	        		case 6:				// CH7
	        		case 7:				// CH8
		      			FA_TH = FA_RXHP_TH3;
                    			break; 
               		case 8:				// CH9
	        		case 9:				//CH10
		      			FA_TH = FA_RXHP_TH4;
                    			break; 	
	        		case 10:
	        		case 11:
	        		case 12:
	        		case 13:	 
	        		case 14:
	      			case 15:	 	
		      			FA_TH = FA_RXHP_TH5;
                    			break;  		
       		}	
			ODM_RT_TRACE(pDM_Odm,	ODM_COMP_RXHP, ODM_DBG_LOUD, ("RX HP FA_TH = %d\n", FA_TH));
			pRX_HP_Table->PSD_func_trigger = 0;
		}
		//1 Monitor RSSI variation to choose the suitable IGI or Exit AGC RX High Power Mode
         	if(pRX_HP_Table->RXHP_flag == 1)
         	{
              	if ((curRssi > 80)&&(preRssi < 80))
              	{ 
                   		pRX_HP_Table->Cur_IGI = LNA_Low_Gain_1;
              	}
              	else if ((curRssi < 80)&&(preRssi > 80))
              	{
                   		pRX_HP_Table->Cur_IGI = LNA_Low_Gain_2;
			}
	       	else if ((curRssi > 72)&&(preRssi < 72))
	      		{
                		pRX_HP_Table->Cur_IGI = LNA_Low_Gain_2;
	       	}
              	else if ((curRssi < 72)&&( preRssi > 72))
	     		{
                   		pRX_HP_Table->Cur_IGI = LNA_Low_Gain_3;
	       	}
	       	else if (curRssi < 68)		 //RSSI is NOT large enough!!==> Exit AGC RX High Power Mode
	       	{
                   		pRX_HP_Table->Cur_pw_th = pw_th_10dB;
				pRX_HP_Table->RXHP_flag = 0;    // Back to Normal DIG Mode		  
				psd_intf_flag = 0;
			}
		}
		else    // pRX_HP_Table->RXHP_flag == 0
		{
			//1 Decide whether to enter AGC RX High Power Mode
			if ((curRssi > 70) && (psd_intf_flag == 1) && (FalseAlmCnt->Cnt_all > FA_TH) &&  
				(pDM_DigTable->CurIGValue == pDM_DigTable->rx_gain_range_max))
			{
             			if (curRssi > 80)
             			{
					pRX_HP_Table->Cur_IGI = LNA_Low_Gain_1;
				}
				else if (curRssi > 72) 
              		{
               			pRX_HP_Table->Cur_IGI = LNA_Low_Gain_2;
				}
             			else
            			{
                   			pRX_HP_Table->Cur_IGI = LNA_Low_Gain_3;
				}
           			pRX_HP_Table->Cur_pw_th = pw_th_16dB;		//RegC54[9:8]=2'b11: to enter AGC Flow 3
				pRX_HP_Table->First_time_enter = TRUE;
				pRX_HP_Table->RXHP_flag = 1;    //	RXHP_flag=1: AGC RX High Power Mode, RXHP_flag=0: Normal DIG Mode
			}
		}
		preRssi = curRssi; 
		odm_Write_RXHP(pDM_Odm);	
	}
#endif //#if( DM_ODM_SUPPORT_TYPE & (ODM_WIN))
#endif //#if (DEV_BUS_TYPE == RT_PCI_INTERFACE) | (DEV_BUS_TYPE == RT_USB_INTERFACE)
}

void odm_Write_RXHP(
	IN	PDM_ODM_T	pDM_Odm)
{
	pRXHP_T		pRX_HP_Table = &pDM_Odm->DM_RXHP_Table;
	u4Byte		currentIGI;

	if(pRX_HP_Table->Cur_IGI != pRX_HP_Table->Pre_IGI)
	{
		ODM_SetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, bMaskByte0, pRX_HP_Table->Cur_IGI);
	     	ODM_SetBBReg(pDM_Odm, rOFDM0_XBAGCCore1, bMaskByte0, pRX_HP_Table->Cur_IGI);	
	}
	
	if(pRX_HP_Table->Cur_pw_th != pRX_HP_Table->Pre_pw_th)
{
		ODM_SetBBReg(pDM_Odm, rOFDM0_XAAGCCore2, BIT8|BIT9, pRX_HP_Table->Cur_pw_th);  // RegC54[9:8]=2'b11:  AGC Flow 3
	}

	if(pRX_HP_Table->RXHP_flag == 0)
	{
		pRX_HP_Table->Cur_IGI = 0x20;
	}
	else
	{
		currentIGI = ODM_GetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, bMaskByte0);
		if(currentIGI<0x50)
		{
			ODM_SetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, bMaskByte0, pRX_HP_Table->Cur_IGI);
	     		ODM_SetBBReg(pDM_Odm, rOFDM0_XBAGCCore1, bMaskByte0, pRX_HP_Table->Cur_IGI);	
		}
	}
	pRX_HP_Table->Pre_IGI = pRX_HP_Table->Cur_IGI;
	pRX_HP_Table->Pre_pw_th = pRX_HP_Table->Cur_pw_th;

}

VOID
odm_PSD_RXHP(
	IN	PDM_ODM_T	pDM_Odm
)
{
	pRXHP_T			pRX_HP_Table  = &pDM_Odm->DM_RXHP_Table;
	PADAPTER		Adapter =  pDM_Odm->Adapter;
	PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);
	unsigned int 		pts, start_point, stop_point, initial_gain ;
	static u1Byte		PSD_bitmap_memory[80], init_memory = 0;
	static u1Byte 		psd_cnt=0;
	static u4Byte		PSD_report[80], PSD_report_tmp;
	static u8Byte		lastTxOkCnt=0, lastRxOkCnt=0;
	u1Byte			idx[20]={96,99,102,106,109,112,115,118,122,125,
					0,3,6,10,13,16,19,22,26,29};
	u1Byte			n, i, channel, BBReset,tone_idx;
	u1Byte			PSD_bitmap[10]/*, SSBT=0*/,initial_gain_psd=0, RSSI_BT=0, initialGainUpper;
	s4Byte    			PSD_skip_start, PSD_skip_stop;
	u4Byte			CurrentChannel, RXIQI, RxIdleLowPwr, wlan_channel;
	u4Byte			ReScan, Interval, Is40MHz;
	u8Byte			curTxOkCnt, curRxOkCnt;
	//--------------2G band synthesizer for 92D switch RF channel using----------------- 
	u1Byte			group_idx=0;
	u4Byte			SYN_RF25=0, SYN_RF26=0, SYN_RF27=0, SYN_RF2B=0, SYN_RF2C=0;
	u4Byte			SYN[5] = {0x25, 0x26, 0x27, 0x2B, 0x2C};    // synthesizer RF register for 2G channel
	u4Byte			SYN_group[3][5] = {{0x643BC, 0xFC038, 0x77C1A, 0x41289, 0x01840},     // For CH1,2,4,9,10.11.12   {0x643BC, 0xFC038, 0x77C1A, 0x41289, 0x01840}
									    {0x643BC, 0xFC038, 0x07C1A, 0x41289, 0x01840},     // For CH3,13,14
									    {0x243BC, 0xFC438, 0x07C1A, 0x4128B, 0x0FC41}};   // For Ch5,6,7,8
       //--------------------- Add by Gary for Debug setting ----------------------
  	u1Byte                 RSSI_BT_new = (u1Byte) ODM_GetBBReg(pDM_Odm, 0xB9C, 0xFF);
       u1Byte                 rssi_ctrl = (u1Byte) ODM_GetBBReg(pDM_Odm, 0xB38, 0xFF);
       //---------------------------------------------------------------------
	
	if(pMgntInfo->bScanInProgress)
	{
		return;
	}

	ReScan = PSD_RESCAN;
	Interval = SCAN_INTERVAL;


	//1 Initialization
	if(init_memory == 0)
	{
		RT_TRACE(	COMP_PSD, DBG_LOUD,("Init memory\n"));
		for(i = 0; i < 80; i++)
			PSD_bitmap_memory[i] = 0xFF; // channel is always good
		init_memory = 1;
	}
	if(psd_cnt == 0)
	{
		RT_TRACE(COMP_PSD, DBG_LOUD,("Enter dm_PSD_Monitor\n"));
		for(i = 0; i < 80; i++)
			PSD_report[i] = 0;
	}

	//1 Backup Current Settings
	CurrentChannel = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask);
	if(pDM_Odm->SupportICType == ODM_RTL8192D)
	{
		//2 Record Current synthesizer parameters based on current channel
		if((*(pDM_Odm->pMacPhyMode)==ODM_SMSP)||(*(pDM_Odm->pMacPhyMode)==ODM_DMSP))
		{
			SYN_RF25 = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x25, bMaskDWord);
			SYN_RF26 = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x26, bMaskDWord);
			SYN_RF27 = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x27, bMaskDWord);
			SYN_RF2B = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x2B, bMaskDWord);
			SYN_RF2C = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x2C, bMaskDWord);
       	}
		else     // DualMAC_DualPHY 2G
		{
			SYN_RF25 = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x25, bMaskDWord);
			SYN_RF26 = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x26, bMaskDWord);
			SYN_RF27 = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x27, bMaskDWord);
			SYN_RF2B = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2B, bMaskDWord);
			SYN_RF2C = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2C, bMaskDWord);
		}
	}
	RXIQI = ODM_GetBBReg(pDM_Odm, 0xC14, bMaskDWord);
	RxIdleLowPwr = (ODM_GetBBReg(pDM_Odm, 0x818, bMaskDWord)&BIT28)>>28;
	Is40MHz = *(pDM_Odm->pBandWidth);
	ODM_RT_TRACE(pDM_Odm,	COMP_PSD, DBG_LOUD,("PSD Scan Start\n"));
	//1 Turn off CCK
	ODM_SetBBReg(pDM_Odm, rFPGA0_RFMOD, BIT24, 0);
	//1 Turn off TX
	//Pause TX Queue
	ODM_Write1Byte(pDM_Odm, REG_TXPAUSE, 0xFF);
	//Force RX to stop TX immediately
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x32E13);
	//1 Turn off RX
	//Rx AGC off  RegC70[0]=0, RegC7C[20]=0
	ODM_SetBBReg(pDM_Odm, 0xC70, BIT0, 0);
	ODM_SetBBReg(pDM_Odm, 0xC7C, BIT20, 0);
	//Turn off CCA
	ODM_SetBBReg(pDM_Odm, 0xC14, bMaskDWord, 0x0);
	//BB Reset
	ODM_SetBBReg(pDM_Odm, 0x87C, BIT31, 1); //clock gated to prevent from AGC table mess 
	BBReset = ODM_Read1Byte(pDM_Odm, 0x02);
	ODM_Write1Byte(pDM_Odm, 0x02, BBReset&(~BIT0));
	ODM_Write1Byte(pDM_Odm, 0x02, BBReset|BIT0);
	ODM_SetBBReg(pDM_Odm, 0x87C, BIT31, 0);
	//1 Leave RX idle low power
	ODM_SetBBReg(pDM_Odm, 0x818, BIT28, 0x0);
	//1 Fix initial gain
      	RSSI_BT = RSSI_BT_new;
	RT_TRACE(COMP_PSD, DBG_LOUD,("PSD: RSSI_BT= %d\n", RSSI_BT));
	
	if(rssi_ctrl == 1)        // just for debug!!
		initial_gain_psd = RSSI_BT_new; 
     	else
		initial_gain_psd = pDM_Odm->RSSI_Min;    // PSD report based on RSSI
	
	RT_TRACE(COMP_PSD, DBG_LOUD,("PSD: RSSI_BT= %d\n", RSSI_BT));
	
	initialGainUpper = 0x54;
	
	RSSI_BT = initial_gain_psd;
	//SSBT = RSSI_BT;
	
	//RT_TRACE(	COMP_PSD, DBG_LOUD,("PSD: SSBT= %d\n", SSBT));
	RT_TRACE(	COMP_PSD, DBG_LOUD,("PSD: initial gain= 0x%x\n", initial_gain_psd));
	
	pDM_Odm->bDMInitialGainEnable = FALSE;		
	initial_gain = ODM_GetBBReg(pDM_Odm, 0xc50, bMaskDWord) & 0x7F;
	//ODM_SetBBReg(pDM_Odm, 0xc50, 0x7F, initial_gain_psd);	
	ODM_Write_DIG(pDM_Odm, initial_gain_psd);
	//1 Turn off 3-wire
	ODM_SetBBReg(pDM_Odm, 0x88c, BIT20|BIT21|BIT22|BIT23, 0xF);

	//pts value = 128, 256, 512, 1024
	pts = 128;

	if(pts == 128)
	{
		ODM_SetBBReg(pDM_Odm, 0x808, BIT14|BIT15, 0x0);
		start_point = 64;
		stop_point = 192;
	}
	else if(pts == 256)
	{
		ODM_SetBBReg(pDM_Odm, 0x808, BIT14|BIT15, 0x1);
		start_point = 128;
		stop_point = 384;
	}
	else if(pts == 512)
	{
		ODM_SetBBReg(pDM_Odm, 0x808, BIT14|BIT15, 0x2);
		start_point = 256;
		stop_point = 768;
	}
	else
	{
		ODM_SetBBReg(pDM_Odm, 0x808, BIT14|BIT15, 0x3);
		start_point = 512;
		stop_point = 1536;
	}
	

//3 Skip WLAN channels if WLAN busy
	curTxOkCnt = *(pDM_Odm->pNumTxBytesUnicast) - lastTxOkCnt;
	curRxOkCnt = *(pDM_Odm->pNumRxBytesUnicast) - lastRxOkCnt;
	lastTxOkCnt = *(pDM_Odm->pNumTxBytesUnicast);
	lastRxOkCnt = *(pDM_Odm->pNumRxBytesUnicast);
	
	PSD_skip_start=80;
	PSD_skip_stop = 0;
	wlan_channel = CurrentChannel & 0x0f;

	RT_TRACE(COMP_PSD,DBG_LOUD,("PSD: current channel: %x, BW:%d \n", wlan_channel, Is40MHz));
	
	if((curRxOkCnt+curTxOkCnt) > 1000)
	{
		PSD_skip_start = (wlan_channel-1)*5 -Is40MHz*10;
		PSD_skip_stop = PSD_skip_start + (1+Is40MHz)*20;
	}

	RT_TRACE(COMP_PSD,DBG_LOUD,("PSD: Skip tone from %d to %d \n", PSD_skip_start, PSD_skip_stop));

 	for (n=0;n<80;n++)
 	{
 		if((n%20)==0)
 		{
			channel = (n/20)*4 + 1;
			if(pDM_Odm->SupportICType == ODM_RTL8192D)
			{
				switch(channel)
				{
					case 1: 
					case 9:
						group_idx = 0;
						break;
					case 5:
						group_idx = 2;
						break;
					case 13:
				 		group_idx = 1;
						break;
				}
				if((*(pDM_Odm->pMacPhyMode)==ODM_SMSP)||(*(pDM_Odm->pMacPhyMode)==ODM_DMSP))   
		{
					for(i = 0; i < SYN_Length; i++)
						ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, SYN[i], bMaskDWord, SYN_group[group_idx][i]);

					ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, 0x3FF, channel);
					ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_CHNLBW, 0x3FF, channel);
				}
				else  // DualMAC_DualPHY 2G
			{
					for(i = 0; i < SYN_Length; i++)
						ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, SYN[i], bMaskDWord, SYN_group[group_idx][i]);   
					
					ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, 0x3FF, channel);
				}
			}
			else
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, 0x3FF, channel);
			}	
		tone_idx = n%20;
		if ((n>=PSD_skip_start) && (n<PSD_skip_stop))
		{	
			PSD_report[n] = initial_gain_psd;//SSBT;
			ODM_RT_TRACE(pDM_Odm,COMP_PSD,DBG_LOUD,("PSD:Tone %d skipped \n", n));
		}
		else
		{
			PSD_report_tmp =  GetPSDData(pDM_Odm, idx[tone_idx], initial_gain_psd);

			if ( PSD_report_tmp > PSD_report[n])
				PSD_report[n] = PSD_report_tmp;
				
		}
	}

	PatchDCTone(pDM_Odm, PSD_report, initial_gain_psd);
      
       //----end
	//1 Turn on RX
	//Rx AGC on
	ODM_SetBBReg(pDM_Odm, 0xC70, BIT0, 1);
	ODM_SetBBReg(pDM_Odm, 0xC7C, BIT20, 1);
	//CCK on
	ODM_SetBBReg(pDM_Odm, rFPGA0_RFMOD, BIT24, 1);
	//1 Turn on TX
	//Resume TX Queue
	ODM_Write1Byte(pDM_Odm, REG_TXPAUSE, 0x00);
	//Turn on 3-wire
	ODM_SetBBReg(pDM_Odm, 0x88c, BIT20|BIT21|BIT22|BIT23, 0x0);
	//1 Restore Current Settings
	//Resume DIG
	pDM_Odm->bDMInitialGainEnable= TRUE;
	//ODM_SetBBReg(pDM_Odm, 0xc50, 0x7F, initial_gain);
	ODM_Write_DIG(pDM_Odm,(u1Byte) initial_gain);
	// restore originl center frequency
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, CurrentChannel);
	if(pDM_Odm->SupportICType == ODM_RTL8192D)
	{
		if((*(pDM_Odm->pMacPhyMode)==ODM_SMSP)||(*(pDM_Odm->pMacPhyMode)==ODM_DMSP))
		{
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_CHNLBW, bMaskDWord, CurrentChannel);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x25, bMaskDWord, SYN_RF25);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x26, bMaskDWord, SYN_RF26);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x27, bMaskDWord, SYN_RF27);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x2B, bMaskDWord, SYN_RF2B);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x2C, bMaskDWord, SYN_RF2C);
		}
		else     // DualMAC_DualPHY
		{
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x25, bMaskDWord, SYN_RF25);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x26, bMaskDWord, SYN_RF26);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x27, bMaskDWord, SYN_RF27);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2B, bMaskDWord, SYN_RF2B);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2C, bMaskDWord, SYN_RF2C);
		}
	}
	//Turn on CCA
	ODM_SetBBReg(pDM_Odm, 0xC14, bMaskDWord, RXIQI);
	//Restore RX idle low power
	if(RxIdleLowPwr == TRUE)
		ODM_SetBBReg(pDM_Odm, 0x818, BIT28, 1);
	
	psd_cnt++;
	//gPrint("psd cnt=%d\n", psd_cnt);
	ODM_RT_TRACE(pDM_Odm,COMP_PSD, DBG_LOUD,("PSD:psd_cnt = %d \n",psd_cnt));
	if (psd_cnt < ReScan)
	{
		ODM_SetTimer(pDM_Odm, &pRX_HP_Table->PSDTimer, Interval);  //ms
	}
	else
			{	
		psd_cnt = 0;
		for(i=0;i<80;i++)
			RT_TRACE(	COMP_PSD, DBG_LOUD,("psd_report[%d]=     %d \n", 2402+i, PSD_report[i]));
			//DbgPrint("psd_report[%d]=     %d \n", 2402+i, PSD_report[i]);

		GoodChannelDecision(pDM_Odm, PSD_report, PSD_bitmap,RSSI_BT, PSD_bitmap_memory);

			}
		}

VOID
odm_PSD_RXHPCallback(
	PRT_TIMER		pTimer
)
{
	PADAPTER		Adapter = (PADAPTER)pTimer->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	pRXHP_T			pRX_HP_Table  = &pDM_Odm->DM_RXHP_Table;
	
#if DEV_BUS_TYPE==RT_PCI_INTERFACE
	#if USE_WORKITEM
	ODM_ScheduleWorkItem(&pRX_HP_Table->PSDTimeWorkitem);
	#else
	odm_PSD_RXHP(pDM_Odm);
	#endif
#else
	ODM_ScheduleWorkItem(&pRX_HP_Table->PSDTimeWorkitem);
#endif
	
	}

VOID
odm_PSD_RXHPWorkitemCallback(
    IN PVOID            pContext
    )
{
	PADAPTER	pAdapter = (PADAPTER)pContext;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	
	odm_PSD_RXHP(pDM_Odm);
}

#endif //#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

//Remove PathDiversity related function to odm_PathDiv.c

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN| ODM_CE))

VOID
odm_PHY_SaveAFERegisters(
	IN	PDM_ODM_T	pDM_Odm,
	IN	pu4Byte		AFEReg,
	IN	pu4Byte		AFEBackup,
	IN	u4Byte		RegisterNum
	)
{
	u4Byte	i;
	
	//RT_DISP(FINIT, INIT_IQK, ("Save ADDA parameters.\n"));
	for( i = 0 ; i < RegisterNum ; i++){
		AFEBackup[i] = ODM_GetBBReg(pDM_Odm, AFEReg[i], bMaskDWord);
	}
}

VOID
odm_PHY_ReloadAFERegisters(
	IN	PDM_ODM_T	pDM_Odm,
	IN	pu4Byte		AFEReg,
	IN	pu4Byte		AFEBackup,
	IN	u4Byte		RegiesterNum
	)
{
	u4Byte	i;

	//RT_DISP(FINIT, INIT_IQK, ("Reload ADDA power saving parameters !\n"));
	for(i = 0 ; i < RegiesterNum; i++)
	{
	
		ODM_SetBBReg(pDM_Odm, AFEReg[i], bMaskDWord, AFEBackup[i]);
	}
}

//
// Description:
//	Set Single/Dual Antenna default setting for products that do not do detection in advance.
//
// Added by Joseph, 2012.03.22
//
VOID
ODM_SingleDualAntennaDefaultSetting(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	pSWAT_T		pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	PADAPTER	pAdapter	 =  pDM_Odm->Adapter;
	u1Byte btAntNum = 2;
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	btAntNum=BT_GetPgAntNum(pAdapter);
#elif (DM_ODM_SUPPORT_TYPE & (ODM_CE))
#ifdef CONFIG_BT_COEXIST
	btAntNum = hal_btcoex_GetPgAntNum(pAdapter);
#endif
#endif

	// Set default antenna A and B status
	if(btAntNum == 2)
	{
		pDM_SWAT_Table->ANTA_ON=TRUE;
		pDM_SWAT_Table->ANTB_ON=TRUE;
		//RT_TRACE(COMP_ANTENNA, DBG_LOUD, ("Dual antenna\n"));
	}
#ifdef CONFIG_BT_COEXIST
	else if(btAntNum == 1)
	{// Set antenna A as default
		pDM_SWAT_Table->ANTA_ON=TRUE;
		pDM_SWAT_Table->ANTB_ON=FALSE;
		//RT_TRACE(COMP_ANTENNA, DBG_LOUD, ("Single antenna\n"));
	}
	else
	{
		//RT_ASSERT(FALSE, ("Incorrect antenna number!!\n"));
	}
#endif
}



//2 8723A ANT DETECT
//
// Description:
//	Implement IQK single tone for RF DPK loopback and BB PSD scanning. 
//	This function is cooperated with BB team Neil. 
//
// Added by Roger, 2011.12.15
//
BOOLEAN
ODM_SingleDualAntennaDetection(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u1Byte			mode
	)
{
	PADAPTER	pAdapter	 =  pDM_Odm->Adapter;
	pSWAT_T		pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	u4Byte		CurrentChannel,RfLoopReg;
	u1Byte		n;
	u4Byte		Reg88c, Regc08, Reg874, Regc50, Reg948=0, Regb2c=0, Reg92c=0, AFE_rRx_Wait_CCA=0;
	u1Byte		initial_gain = 0x5a;
	u4Byte		PSD_report_tmp;
	u4Byte		AntA_report = 0x0, AntB_report = 0x0,AntO_report=0x0;
	BOOLEAN		bResult = TRUE;
	u4Byte		AFE_Backup[16];
	u4Byte		AFE_REG_8723A[16] = {
					rRx_Wait_CCA, 	rTx_CCK_RFON, 
					rTx_CCK_BBON, 	rTx_OFDM_RFON,
					rTx_OFDM_BBON, 	rTx_To_Rx,
					rTx_To_Tx, 		rRx_CCK, 
					rRx_OFDM, 		rRx_Wait_RIFS, 
					rRx_TO_Rx,		rStandby,
					rSleep,			rPMPD_ANAEN, 	
					rFPGA0_XCD_SwitchControl, rBlue_Tooth};

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection()============> \n"));	

	
	if(!(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C|ODM_RTL8723B)))
		return bResult;

	// Retrieve antenna detection registry info, added by Roger, 2012.11.27.
	if(!IS_ANT_DETECT_SUPPORT_SINGLE_TONE(pAdapter))
		return bResult;

	if(pDM_Odm->SupportICType == ODM_RTL8192C)
	{
		//Which path in ADC/DAC is turnned on for PSD: both I/Q
		ODM_SetBBReg(pDM_Odm, 0x808, BIT10|BIT11, 0x3);
		//Ageraged number: 8
		ODM_SetBBReg(pDM_Odm, 0x808, BIT12|BIT13, 0x1);
		//pts = 128;
		ODM_SetBBReg(pDM_Odm, 0x808, BIT14|BIT15, 0x0);
	}

	//1 Backup Current RF/BB Settings	
	
	CurrentChannel = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, ODM_CHANNEL, bRFRegOffsetMask);
	RfLoopReg = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x00, bRFRegOffsetMask);
	if(!(pDM_Odm->SupportICType == ODM_RTL8723B))
	ODM_SetBBReg(pDM_Odm, rFPGA0_XA_RFInterfaceOE, ODM_DPDT, Antenna_A);  // change to Antenna A
#if (RTL8723B_SUPPORT == 1)
	else
	{
		Reg92c = ODM_GetBBReg(pDM_Odm, 0x92c, bMaskDWord);
		Reg948 = ODM_GetBBReg(pDM_Odm, rS0S1_PathSwitch, bMaskDWord);
		Regb2c = ODM_GetBBReg(pDM_Odm, AGC_table_select, bMaskDWord);
		ODM_SetBBReg(pDM_Odm, rDPDT_control, 0x3, 0x1);
		ODM_SetBBReg(pDM_Odm, rfe_ctrl_anta_src, 0xff, 0x77);
		ODM_SetBBReg(pDM_Odm, rS0S1_PathSwitch, 0x3ff, 0x000);
		ODM_SetBBReg(pDM_Odm, AGC_table_select, BIT31, 0x0);
	}
#endif
	ODM_StallExecution(10);
	
	//Store A Path Register 88c, c08, 874, c50
	Reg88c = ODM_GetBBReg(pDM_Odm, rFPGA0_AnalogParameter4, bMaskDWord);
	Regc08 = ODM_GetBBReg(pDM_Odm, rOFDM0_TRMuxPar, bMaskDWord);
	Reg874 = ODM_GetBBReg(pDM_Odm, rFPGA0_XCD_RFInterfaceSW, bMaskDWord);
	Regc50 = ODM_GetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, bMaskDWord);	
	
	// Store AFE Registers
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
	odm_PHY_SaveAFERegisters(pDM_Odm, AFE_REG_8723A, AFE_Backup, 16);	
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
		AFE_rRx_Wait_CCA = ODM_GetBBReg(pDM_Odm, rRx_Wait_CCA,bMaskDWord);
	
	//Set PSD 128 pts
	ODM_SetBBReg(pDM_Odm, rFPGA0_PSDFunction, BIT14|BIT15, 0x0);  //128 pts
	
	// To SET CH1 to do
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, ODM_CHANNEL, bRFRegOffsetMask, 0x7401);     //Channel 1
	
	// AFE all on step
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
	{
		ODM_SetBBReg(pDM_Odm, rRx_Wait_CCA, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rTx_CCK_RFON, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rTx_CCK_BBON, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rTx_OFDM_RFON, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rTx_OFDM_BBON, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rTx_To_Rx, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rTx_To_Tx, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rRx_CCK, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rRx_OFDM, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rRx_Wait_RIFS, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rRx_TO_Rx, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rStandby, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rSleep, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rPMPD_ANAEN, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rFPGA0_XCD_SwitchControl, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rBlue_Tooth, bMaskDWord, 0x6FDB25A4);
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
	{
		ODM_SetBBReg(pDM_Odm, rRx_Wait_CCA, bMaskDWord, 0x01c00016);
	}

	// 3 wire Disable
	ODM_SetBBReg(pDM_Odm, rFPGA0_AnalogParameter4, bMaskDWord, 0xCCF000C0);
	
	//BB IQK Setting
	ODM_SetBBReg(pDM_Odm, rOFDM0_TRMuxPar, bMaskDWord, 0x000800E4);
	ODM_SetBBReg(pDM_Odm, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, 0x22208000);

	//IQK setting tone@ 4.34Mhz
	ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_A, bMaskDWord, 0x10008C1C);
	ODM_SetBBReg(pDM_Odm, rTx_IQK, bMaskDWord, 0x01007c00);	

	//Page B init
	ODM_SetBBReg(pDM_Odm, rConfig_AntA, bMaskDWord, 0x00080000);
	ODM_SetBBReg(pDM_Odm, rConfig_AntA, bMaskDWord, 0x0f600000);
	ODM_SetBBReg(pDM_Odm, rRx_IQK, bMaskDWord, 0x01004800);
	ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_A, bMaskDWord, 0x10008c1f);
	ODM_SetBBReg(pDM_Odm, rTx_IQK_PI_A, bMaskDWord, 0x82150008);
	ODM_SetBBReg(pDM_Odm, rRx_IQK_PI_A, bMaskDWord, 0x28150008);
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Rsp, bMaskDWord, 0x001028d0);	

	//RF loop Setting
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x0, 0xFFFFF, 0x50008);	
	
	//IQK Single tone start
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskH3Bytes, 0x808000);
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);
	
	ODM_StallExecution(10000);

	// PSD report of antenna A
	PSD_report_tmp=0x0;
	for (n=0;n<2;n++)
 	{
 		PSD_report_tmp =  GetPSDData(pDM_Odm, 14, initial_gain);	
		if(PSD_report_tmp >AntA_report)
			AntA_report=PSD_report_tmp;
	}

	 // change to Antenna B
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
		ODM_SetBBReg(pDM_Odm, rFPGA0_XA_RFInterfaceOE, ODM_DPDT, Antenna_B); 
#if (RTL8723B_SUPPORT == 1)
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
		ODM_SetBBReg(pDM_Odm, rDPDT_control, 0x3, 0x2);
#endif

	ODM_StallExecution(10);	

	// PSD report of antenna B
	PSD_report_tmp=0x0;
	for (n=0;n<2;n++)
 	{
 		PSD_report_tmp =  GetPSDData(pDM_Odm, 14, initial_gain);	
		if(PSD_report_tmp > AntB_report)
			AntB_report=PSD_report_tmp;
	}

	// change to open case
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
		ODM_SetBBReg(pDM_Odm, rFPGA0_XA_RFInterfaceOE, ODM_DPDT, 0);  // change to Antenna A
#if (RTL8723B_SUPPORT == 1)
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
		ODM_SetBBReg(pDM_Odm, rDPDT_control, 0x3, 0x0);
#endif

	ODM_StallExecution(10);	
	
	// PSD report of open case
	PSD_report_tmp=0x0;
	for (n=0;n<2;n++)
 	{
 		PSD_report_tmp =  GetPSDData(pDM_Odm, 14, initial_gain);	
		if(PSD_report_tmp > AntO_report)
			AntO_report=PSD_report_tmp;
	}

	//Close IQK Single Tone function
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, bMaskH3Bytes, 0x000000);	

	//1 Return to antanna A
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
		ODM_SetBBReg(pDM_Odm, rFPGA0_XA_RFInterfaceOE, ODM_DPDT, Antenna_A);  // change to Antenna A
#if (RTL8723B_SUPPORT == 1)
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
	{
		// external DPDT
		ODM_SetBBReg(pDM_Odm, rDPDT_control, bMaskDWord, Reg92c);

		//internal S0/S1
		ODM_SetBBReg(pDM_Odm, rS0S1_PathSwitch, bMaskDWord, Reg948);
		ODM_SetBBReg(pDM_Odm, AGC_table_select, bMaskDWord, Regb2c);
	}
#endif
	ODM_SetBBReg(pDM_Odm, rFPGA0_AnalogParameter4, bMaskDWord, Reg88c);
	ODM_SetBBReg(pDM_Odm, rOFDM0_TRMuxPar, bMaskDWord, Regc08);
	ODM_SetBBReg(pDM_Odm, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, Reg874);
	ODM_SetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, 0x7F, 0x40);
	ODM_SetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, bMaskDWord, Regc50);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask,CurrentChannel);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x00, bRFRegOffsetMask,RfLoopReg);

	//Reload AFE Registers
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
	odm_PHY_ReloadAFERegisters(pDM_Odm, AFE_REG_8723A, AFE_Backup, 16);	
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
		ODM_SetBBReg(pDM_Odm, rRx_Wait_CCA, bMaskDWord, AFE_rRx_Wait_CCA);

	if(pDM_Odm->SupportICType == ODM_RTL8723A)
	{
		//2 Test Ant B based on Ant A is ON
		if(mode==ANTTESTB)
		{
			if(AntA_report >=	100)
			{
				if(AntB_report > (AntA_report+1))
				{
					pDM_SWAT_Table->ANTB_ON=FALSE;
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Single Antenna A\n"));		
				}	
				else
				{
					pDM_SWAT_Table->ANTB_ON=TRUE;
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Dual Antenna is A and B\n"));	
				}	
			}
			else
			{
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Need to check again\n"));
				pDM_SWAT_Table->ANTB_ON=FALSE; // Set Antenna B off as default 
				bResult = FALSE;
			}
		}	
		//2 Test Ant A and B based on DPDT Open
		else if(mode==ANTTESTALL)
		{
			if((AntO_report >=100) && (AntO_report <=118))
			{
				if(AntA_report > (AntO_report+1))
				{
					pDM_SWAT_Table->ANTA_ON=FALSE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("Ant A is OFF\n"));
				}	
				else
				{
					pDM_SWAT_Table->ANTA_ON=TRUE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("Ant A is ON\n"));
				}

				if(AntB_report > (AntO_report+2))
				{
					pDM_SWAT_Table->ANTB_ON=FALSE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("Ant B is OFF\n"));
				}	
				else
				{
					pDM_SWAT_Table->ANTB_ON=TRUE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("Ant B is ON\n"));
				}
				
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("psd_report_A[%d]= %d \n", 2416, AntA_report));	
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("psd_report_B[%d]= %d \n", 2416, AntB_report));	
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("psd_report_O[%d]= %d \n", 2416, AntO_report));
				
				pDM_Odm->AntDetectedInfo.bAntDetected= TRUE;
				pDM_Odm->AntDetectedInfo.dBForAntA = AntA_report;
				pDM_Odm->AntDetectedInfo.dBForAntB = AntB_report;
				pDM_Odm->AntDetectedInfo.dBForAntO = AntO_report;
				
				}
			else
				{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("return FALSE!!\n"));
				bResult = FALSE;
			}
		}
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8192C)
	{
		if(AntA_report >=	100)
		{
			if(AntB_report > (AntA_report+2))
			{
				pDM_SWAT_Table->ANTA_ON=FALSE;
				pDM_SWAT_Table->ANTB_ON=TRUE;
				ODM_SetBBReg(pDM_Odm,  rFPGA0_XA_RFInterfaceOE, 0x300, Antenna_B);
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Single Antenna B\n"));		
			}	
			else if(AntA_report > (AntB_report+2))
			{
				pDM_SWAT_Table->ANTA_ON=TRUE;
				pDM_SWAT_Table->ANTB_ON=FALSE;
				ODM_SetBBReg(pDM_Odm,  rFPGA0_XA_RFInterfaceOE, 0x300, Antenna_A);
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Single Antenna A\n"));
			}	
			else
			{
				pDM_SWAT_Table->ANTA_ON=TRUE;
				pDM_SWAT_Table->ANTB_ON=TRUE;
				RT_TRACE(COMP_ANTENNA, DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Dual Antenna \n"));
			}
		}
		else
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Need to check again\n"));
			pDM_SWAT_Table->ANTA_ON=TRUE; // Set Antenna A on as default 
			pDM_SWAT_Table->ANTB_ON=FALSE; // Set Antenna B off as default 
			bResult = FALSE;
		}
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("psd_report_A[%d]= %d \n", 2416, AntA_report));	
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("psd_report_B[%d]= %d \n", 2416, AntB_report));	
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("psd_report_O[%d]= %d \n", 2416, AntO_report));
		
		//2 Test Ant B based on Ant A is ON
		if(mode==ANTTESTB)
		{
			if(AntA_report >=100 && AntA_report <= 116)
			{
				if(AntB_report >= (AntA_report+4) && AntB_report > 116)
				{
					pDM_SWAT_Table->ANTB_ON=FALSE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Single Antenna A\n"));		
				}	
				else if(AntB_report >=100 && AntB_report <= 116)
				{
					pDM_SWAT_Table->ANTB_ON=TRUE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Dual Antenna is A and B\n"));	
				}
				else
				{
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Need to check again\n"));
					pDM_SWAT_Table->ANTB_ON=FALSE; // Set Antenna B off as default 
					bResult = FALSE;
				}
			}
			else
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Need to check again\n"));
				pDM_SWAT_Table->ANTB_ON=FALSE; // Set Antenna B off as default 
				bResult = FALSE;
			}
		}	
		//2 Test Ant A and B based on DPDT Open
		else if(mode==ANTTESTALL)
		{
			if((AntA_report >= 100) && (AntB_report >= 100) && (AntA_report <= 120) && (AntB_report <= 120))
			{
				if((AntA_report - AntB_report < 2) || (AntB_report - AntA_report < 2))
				{
					pDM_SWAT_Table->ANTA_ON=TRUE;
					pDM_SWAT_Table->ANTB_ON=TRUE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("ODM_SingleDualAntennaDetection(): Dual Antenna\n"));
				}
				else if(((AntA_report - AntB_report >= 2) && (AntA_report - AntB_report <= 4)) || 
					((AntB_report - AntA_report >= 2) && (AntB_report - AntA_report <= 4)))
				{
					pDM_SWAT_Table->ANTA_ON=FALSE;
					pDM_SWAT_Table->ANTB_ON=FALSE;
					bResult = FALSE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Need to check again\n"));
				}
				else
				{
					pDM_SWAT_Table->ANTA_ON = TRUE;
					pDM_SWAT_Table->ANTB_ON=FALSE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("ODM_SingleDualAntennaDetection(): Single Antenna A\n"));
				}
				
				pDM_Odm->AntDetectedInfo.bAntDetected= TRUE;
				pDM_Odm->AntDetectedInfo.dBForAntA = AntA_report;
				pDM_Odm->AntDetectedInfo.dBForAntB = AntB_report;
				pDM_Odm->AntDetectedInfo.dBForAntO = AntO_report;
				
			}
			else
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("return FALSE!!\n"));
				bResult = FALSE;
			}
		}
	}
		
	return bResult;

}


#endif   // end odm_CE

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN| ODM_CE))

VOID
odm_Set_RA_DM_ARFB_by_Noisy(
	IN	PDM_ODM_T	pDM_Odm
)
{
	//DbgPrint("DM_ARFB ====> \n");
	if (pDM_Odm->bNoisyState){
		ODM_Write4Byte(pDM_Odm,0x430,0x00000000);
		ODM_Write4Byte(pDM_Odm,0x434,0x05040200);
		//DbgPrint("DM_ARFB ====> Noisy State\n");
	}
	else{
		ODM_Write4Byte(pDM_Odm,0x430,0x02010000);
		ODM_Write4Byte(pDM_Odm,0x434,0x07050403);
		//DbgPrint("DM_ARFB ====> Clean State\n");
	}
	
}

VOID
ODM_UpdateNoisyState(
	IN	PDM_ODM_T	pDM_Odm,
	IN 	BOOLEAN 	bNoisyStateFromC2H
	)
{
	//DbgPrint("Get C2H Command! NoisyState=0x%x\n ", bNoisyStateFromC2H);
	if(pDM_Odm->SupportICType == ODM_RTL8821  || pDM_Odm->SupportICType == ODM_RTL8812  || 
	   pDM_Odm->SupportICType == ODM_RTL8723B || pDM_Odm->SupportICType == ODM_RTL8192E || pDM_Odm->SupportICType == ODM_RTL8188E)
	{
		pDM_Odm->bNoisyState = bNoisyStateFromC2H;
	}
	odm_Set_RA_DM_ARFB_by_Noisy(pDM_Odm);
};

u4Byte
Set_RA_DM_Ratrbitmap_by_Noisy(
	IN	PDM_ODM_T	pDM_Odm,
	IN	WIRELESS_MODE	WirelessMode,
	IN	u4Byte			ratr_bitmap,
	IN	u1Byte			rssi_level
)
{
	u4Byte ret_bitmap = ratr_bitmap;
	switch (WirelessMode)
	{
		case WIRELESS_MODE_AC_24G :
		case WIRELESS_MODE_AC_5G :
		case WIRELESS_MODE_AC_ONLY:
			if (pDM_Odm->bNoisyState){ // in Noisy State
				if (rssi_level==1)
					ret_bitmap&=0xfe3f0e08;
				else if (rssi_level==2)
					ret_bitmap&=0xff3f8f8c;
				else if (rssi_level==3)
					ret_bitmap&=0xffffffcc ;
				else
					ret_bitmap&=0xffffffff ;
			}
			else{                                   // in SNR State
				if (rssi_level==1){
					ret_bitmap&=0xfc3e0c08;
				}
				else if (rssi_level==2){
					ret_bitmap&=0xfe3f0e08;
				}
				else if (rssi_level==3){
					ret_bitmap&=0xffbfefcc;
				}
				else{
					ret_bitmap&=0x0fffffff;
				}
			}
			break;
		case WIRELESS_MODE_B:
		case WIRELESS_MODE_A:
		case WIRELESS_MODE_G:
		case WIRELESS_MODE_N_24G:
		case WIRELESS_MODE_N_5G:
			if (pDM_Odm->bNoisyState){
				if (rssi_level==1)
					ret_bitmap&=0x0f0e0c08;
				else if (rssi_level==2)
					ret_bitmap&=0x0f8f0e0c;
				else if (rssi_level==3)
					ret_bitmap&=0x0fefefcc ;
				else
					ret_bitmap&=0xffffffff ;
			}
			else{
				if (rssi_level==1){
					ret_bitmap&=0x0f8f0e08;
				}
				else if (rssi_level==2){
					ret_bitmap&=0x0fcf8f8c;
				}
				else if (rssi_level==3){
					ret_bitmap&=0x0fffffcc;
				}
				else{
					ret_bitmap&=0x0fffffff;
				}
			}
			break;
		default:
			break;
	}
	//DbgPrint("DM_RAMask ====> rssi_LV = %d, BITMAP = %x \n", rssi_level, ret_bitmap);
	return ret_bitmap;

}



VOID
ODM_UpdateInitRate(
	IN	PDM_ODM_T	pDM_Odm,
	IN	u1Byte		Rate
	)
{
	u1Byte			p = 0;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("Get C2H Command! Rate=0x%x\n", Rate));
	
	if(pDM_Odm->SupportICType == ODM_RTL8821  || pDM_Odm->SupportICType == ODM_RTL8812  || 
	   pDM_Odm->SupportICType == ODM_RTL8723B || pDM_Odm->SupportICType == ODM_RTL8192E || pDM_Odm->SupportICType == ODM_RTL8188E)
	{
		pDM_Odm->TxRate = Rate;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#if DEV_BUS_TYPE==RT_PCI_INTERFACE
		#if USE_WORKITEM
		PlatformScheduleWorkItem(&pDM_Odm->RaRptWorkitem);
		#else
		if(pDM_Odm->SupportICType == ODM_RTL8821)
		{
			ODM_TxPwrTrackSetPwr8821A(pDM_Odm, MIX_MODE, ODM_RF_PATH_A, 0);
		}
		else if(pDM_Odm->SupportICType == ODM_RTL8812)
		{
			for (p = ODM_RF_PATH_A; p < MAX_PATH_NUM_8812A; p++) 		
			{
				ODM_TxPwrTrackSetPwr8812A(pDM_Odm, MIX_MODE, p, 0);
			}
		}
		else if(pDM_Odm->SupportICType == ODM_RTL8723B)
		{
			ODM_TxPwrTrackSetPwr_8723B(pDM_Odm, MIX_MODE, ODM_RF_PATH_A, 0);
		}
		else if(pDM_Odm->SupportICType == ODM_RTL8192E)
		{
			for (p = ODM_RF_PATH_A; p < MAX_PATH_NUM_8192E; p++) 		
			{
				ODM_TxPwrTrackSetPwr92E(pDM_Odm, MIX_MODE, p, 0);
			}
		}
		else if(pDM_Odm->SupportICType == ODM_RTL8188E)
		{
			ODM_TxPwrTrackSetPwr88E(pDM_Odm, MIX_MODE, ODM_RF_PATH_A, 0);
		}
		#endif
	#else
		PlatformScheduleWorkItem(&pDM_Odm->RaRptWorkitem);
	#endif	
#endif
	}
	else
		return;
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID
ODM_UpdateInitRateWorkItemCallback(
    IN PVOID            pContext
    )
{
	PADAPTER	Adapter = (PADAPTER)pContext;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;

	u1Byte			p = 0;	

	if(pDM_Odm->SupportICType == ODM_RTL8821)
	{
		ODM_TxPwrTrackSetPwr8821A(pDM_Odm, MIX_MODE, ODM_RF_PATH_A, 0);
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8812)
	{
		for (p = ODM_RF_PATH_A; p < MAX_PATH_NUM_8812A; p++)    //DOn't know how to include &c
		{
			ODM_TxPwrTrackSetPwr8812A(pDM_Odm, MIX_MODE, p, 0);
		}
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
	{
			ODM_TxPwrTrackSetPwr_8723B(pDM_Odm, MIX_MODE, ODM_RF_PATH_A, 0);
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8192E)
	{
		for (p = ODM_RF_PATH_A; p < MAX_PATH_NUM_8192E; p++)    //DOn't know how to include &c
		{
			ODM_TxPwrTrackSetPwr92E(pDM_Odm, MIX_MODE, p, 0);
		}
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8188E)
	{
			ODM_TxPwrTrackSetPwr88E(pDM_Odm, MIX_MODE, ODM_RF_PATH_A, 0);
	}
}
#endif
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
/* Justin: According to the current RRSI to adjust Response Frame TX power, 2012/11/05 */
void odm_dtc(PDM_ODM_T pDM_Odm)
{
#ifdef CONFIG_DM_RESP_TXAGC
	#define DTC_BASE            35	/* RSSI higher than this value, start to decade TX power */
	#define DTC_DWN_BASE       (DTC_BASE-5)	/* RSSI lower than this value, start to increase TX power */

	/* RSSI vs TX power step mapping: decade TX power */
	static const u8 dtc_table_down[]={
		DTC_BASE,
		(DTC_BASE+5),
		(DTC_BASE+10),
		(DTC_BASE+15),
		(DTC_BASE+20),
		(DTC_BASE+25)
	};

	/* RSSI vs TX power step mapping: increase TX power */
	static const u8 dtc_table_up[]={
		DTC_DWN_BASE,
		(DTC_DWN_BASE-5),
		(DTC_DWN_BASE-10),
		(DTC_DWN_BASE-15),
		(DTC_DWN_BASE-15),
		(DTC_DWN_BASE-20),
		(DTC_DWN_BASE-20),
		(DTC_DWN_BASE-25),
		(DTC_DWN_BASE-25),
		(DTC_DWN_BASE-30),
		(DTC_DWN_BASE-35)
	};

	u8 i;
	u8 dtc_steps=0;
	u8 sign;
	u8 resp_txagc=0;

	#if 0
	/* As DIG is disabled, DTC is also disable */
	if(!(pDM_Odm->SupportAbility & ODM_XXXXXX))
		return;
	#endif

	if (DTC_BASE < pDM_Odm->RSSI_Min) {
		/* need to decade the CTS TX power */
		sign = 1;
		for (i=0;i<ARRAY_SIZE(dtc_table_down);i++)
		{
			if ((dtc_table_down[i] >= pDM_Odm->RSSI_Min) || (dtc_steps >= 6))
				break;
			else
				dtc_steps++;
		}
	}
#if 0
	else if (DTC_DWN_BASE > pDM_Odm->RSSI_Min)
	{
		/* needs to increase the CTS TX power */
		sign = 0;
		dtc_steps = 1;
		for (i=0;i<ARRAY_SIZE(dtc_table_up);i++)
		{
			if ((dtc_table_up[i] <= pDM_Odm->RSSI_Min) || (dtc_steps>=10))
				break;
			else
				dtc_steps++;
		}
	}
#endif
	else
	{
		sign = 0;
		dtc_steps = 0;
	}

	resp_txagc = dtc_steps | (sign << 4);
	resp_txagc = resp_txagc | (resp_txagc << 5);
	ODM_Write1Byte(pDM_Odm, 0x06d9, resp_txagc);

	DBG_871X("%s RSSI_Min:%u, set RESP_TXAGC to %s %u\n", 
		__func__, pDM_Odm->RSSI_Min, sign?"minus":"plus", dtc_steps);
#endif /* CONFIG_RESP_TXAGC_ADJUST */
}

#endif /* #if (DM_ODM_SUPPORT_TYPE == ODM_CE) */

