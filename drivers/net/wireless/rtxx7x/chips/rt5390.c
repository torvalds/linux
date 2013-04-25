/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#include "rt_config.h"


#ifndef RTMP_RF_RW_SUPPORT
#error "You Should Enable compile flag RTMP_RF_RW_SUPPORT for this chip"
#endif /* RTMP_RF_RW_SUPPORT */
#ifdef RTMP_FLASH_SUPPORT
UCHAR RT5390_EeBuffer[EEPROM_SIZE] = { 
0x92, 0x30, 0x02, 0x01, 0x00, 0x0c, 0x43, 0x30, 0x92, 0x00, 0x92, 0x30, 0x14, 0x18, 0x01, 0x80, 
0x00, 0x00, 0x92, 0x30, 0x14, 0x18, 0x00, 0x00, 0x01, 0x00, 0x6a, 0xff, 0x13, 0x02, 0xff, 0xff, 
0xff, 0xff, 0xc1, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
0x8e, 0x75, 0x01, 0x43, 0x22, 0x08, 0x27, 0x00, 0xff, 0xff, 0x16, 0x01, 0xff, 0xff, 0xd9, 0xfa, 
0xcc, 0x88, 0xff, 0xff, 0x0a, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 
0xff, 0xff, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 
0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x1d, 0x1a, 
0x15, 0x11, 0x0f, 0x0d, 0x0a, 0x07, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x88, 0x88, 
0xcc, 0xcc, 0xaa, 0x88, 0xcc, 0xcc, 0xaa, 0x88, 0xcc, 0xcc, 0xaa, 0x88, 0xcc, 0xcc, 0xaa, 0x88, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, };
#endif /* RTMP_FLASH_SUPPORT */

REG_PAIR RF5390RegTable[] = 
{
/*	{RF_R00,		0x20},  //Read only */
	{RF_R01,		0x0F}, 
	{RF_R02,		0x80}, 
	{RF_R03,		0x88}, /* vcocal_double_step */ 
/*	{RF_R04,		0x51}, // Read only */
	{RF_R05,		0x10}, 
	{RF_R06,		0xA0}, 
	{RF_R07,		0x00}, 
/*	{RF_R08,		0xF1}, // By channel plan */
/*	{RF_R09,		0x02}, // By channel plan */

	{RF_R10,		0x53},
	{RF_R11,		0x4A},
	{RF_R12,		0x46},
	{RF_R13,		0x9F},
	{RF_R14,		0x00}, 
	{RF_R15,		0x00}, 
	{RF_R16,		0x00}, 
	/*{RF_R17,		0x00}, // Based on the frequency offset in the EEPROM */
	{RF_R18,		0x03}, 
	{RF_R19,		0x00}, /* Spare */

	{RF_R20,		0x00}, 
	{RF_R21,		0x00}, /* Spare */
	{RF_R22,		0x20},	
	{RF_R23,		0x00}, /* Spare */
	{RF_R24,		0x00}, /* Spare */
	{RF_R25,		0xC0}, 
	{RF_R26,		0x00}, /* Spare */
	{RF_R27,		0x09}, 
	{RF_R28,		0x00}, 
	{RF_R29,		0x10}, 

	{RF_R30,		0x10},
	{RF_R31,		0x80}, 
	{RF_R32,		0x80}, 
	{RF_R33,		0x00}, /* Spare */
	{RF_R34,		0x07}, 
	{RF_R35,		0x12}, 
	{RF_R36,		0x00}, 
	{RF_R37,		0x08}, 
	{RF_R38,		0x85}, 
	{RF_R39,		0x1B}, 

	{RF_R40,		0x0B}, 
	{RF_R41,		0xBB}, 
	{RF_R42,		0xD2}, 
	{RF_R43,		0x9A}, 
	{RF_R44,		0x0E},
	{RF_R45,		0xA2}, 
	{RF_R46,		0x7B}, 
	{RF_R47,		0x00}, 
	{RF_R48,		0x10}, 
	{RF_R49,		0x94},

/*	{RF_R50,		0x00}, // NC */
/*	{RF_R51,		0x00}, // NC */
 	{RF_R52,		0x38}, 
	{RF_R53,		0x84}, /* RT5370 only. RT5390, RT5370F and RT5390F will re-write to 0x00 */
	{RF_R54,		0x78},
	{RF_R55,		0x44},
	{RF_R56,		0x22}, 
	{RF_R57,		0x80},
	{RF_R58,		0x7F}, 
	{RF_R59,		0x8F}, 

	{RF_R60,		0x45}, 
	{RF_R61,		0xB5}, 
	{RF_R62,		0x00}, /* Spare */
	{RF_R63,		0x00}, /* Spare */
};

#define NUM_RF_5390_REG_PARMS (sizeof(RF5390RegTable) / sizeof(REG_PAIR))

/* 5390U (5370 using PCIe interface) */

REG_PAIR RF5390URegTable[] = 
{
/*	{RF_R00,		0x20},    Read only */
	{RF_R01,		0x0F}, 
	{RF_R02,		0x80}, 
	{RF_R03,		0x88}, /* vcocal_double_step */
/*	{RF_R04,		0x51},    Read only */
	{RF_R05,		0x10}, 
	{RF_R06,		0xE0}, 
	{RF_R07,		0x00}, 
/*	{RF_R08,		0xF1},    By channel plan */
/*	{RF_R09,		0x02},    By channel plan */

	{RF_R10,		0x53},
	{RF_R11,		0x4A},
	{RF_R12,		0x46},
	{RF_R13,		0x9F},
	{RF_R14,		0x00}, 
	{RF_R15,		0x00}, 
	{RF_R16,		0x00}, 
/*	{RF_R17,		0x00},    Based on the frequency offset in the EEPROM */
	{RF_R18,		0x03}, 
	{RF_R19,		0x00}, /* Spare */

	{RF_R20,		0x00}, 
	{RF_R21,		0x00}, /* Spare */
	{RF_R22,		0x20},	
	{RF_R23,		0x00}, /* Spare */
	{RF_R24,		0x00}, /* Spare */
	{RF_R25,		0x80}, 
	{RF_R26,		0x00}, /* Spare */
	{RF_R27,		0x09}, 
	{RF_R28,		0x00}, 
	{RF_R29,		0x10}, 

	{RF_R30,		0x10},
	{RF_R31,		0x80}, 
	{RF_R32,		0x80}, 
	{RF_R33,		0x00}, /* Spare */
	{RF_R34,		0x07}, 
	{RF_R35,		0x12}, 
	{RF_R36,		0x00}, 
	{RF_R37,		0x08}, 
	{RF_R38,		0x85}, 
	{RF_R39,		0x1B}, 

	{RF_R40,		0x0B}, 
	{RF_R41,		0xBB}, 
	{RF_R42,		0xD2}, 
	{RF_R43,		0x9A}, 
	{RF_R44,		0x0E},
	{RF_R45,		0xA2}, 
	{RF_R46,		0x73}, 
	{RF_R47,		0x00}, 
	{RF_R48,		0x10}, 
	{RF_R49,		0x94},

/*	{RF_R50,		0x00},    NC */
/*	{RF_R51,		0x00},    NC */
	{RF_R52,		0x38}, 
	{RF_R53,		0x00},
	{RF_R54,		0x78},
	{RF_R55,		0x23},
	{RF_R56,		0x22}, 
	{RF_R57,		0x80},
	{RF_R58,		0x7F}, 
	{RF_R59,		0x07}, 

	{RF_R60,		0x45}, 
	{RF_R61,		0xD1}, 
	{RF_R62,		0x00}, /* Spare */
	{RF_R63,		0x00}, /* Spare */
};

#define NUM_RF_5390U_REG_PARMS (sizeof(RF5390URegTable) / sizeof(REG_PAIR))

REG_PAIR RF5392RegTable[] = 
{
/*	{RF_R00,		0x20}, // Read only */
	{RF_R01,		0x07},
	{RF_R02,		0x80},
	{RF_R03,		0x88}, /* vcocal_double_step */
/*	{RF_R04,		0x51}, // Read only */
	{RF_R05,		0x10},
	{RF_R06,		0xE0}, /* 20101018 update */
	{RF_R07,		0x00}, 
/*	{RF_R08,		0xF1}, // By channel plan */
/*	{RF_R09,		0x02}, // By channel plan */
	{RF_R10,		0x53},
	{RF_R11,		0x4A},
	{RF_R12,		0x46},
	{RF_R13,		0x9F},
	{RF_R14,		0x00}, 
	{RF_R15,		0x00}, 
	{RF_R16,		0x00}, 
/*	{RF_R17,		0x00}, // Based on the frequency offset in the EEPROM */
	{RF_R18,		0x03}, 
	{RF_R19,		0x4D}, /* Spare */
	{RF_R20,		0x00}, 
	{RF_R21,		0x8D}, /* Spare 20101018 update. */
	{RF_R22,		0x20},	
	{RF_R23,		0x0B}, /* Spare 20101018 update. */
	{RF_R24,		0x44}, /* Spare */
	{RF_R25,		0x80}, /* 20101018 update. */
	{RF_R26,		0x82}, /* Spare */
	{RF_R27,		0x09}, 
	{RF_R28,		0x00}, 
	{RF_R29,		0x10}, 
	{RF_R30,		0x10},
	{RF_R31,		0x80}, 
	{RF_R32,		0x80}, 
	{RF_R33,		0xC0}, /* Spare */
	{RF_R34,		0x07}, 
	{RF_R35,		0x12}, 
	{RF_R36,		0x00}, 
	{RF_R37,		0x08}, 
	{RF_R38,		0x89}, /* 20101118 update. */
	{RF_R39,		0x1B}, 

	{RF_R40,		0x0F}, /* 20101118 update. */
	{RF_R41,		0xBB}, 
	{RF_R42,		0xD5}, /* 20101018 update.*/
	{RF_R43,		0x9B}, /* 20101018 update. */
	{RF_R44,		0x0E},
	{RF_R45,		0xA2}, 
	{RF_R46,		0x73}, 
	{RF_R47,		0x0C}, 
	{RF_R48,		0x10}, 
	{RF_R49,		0x94},
	{RF_R50,		0x94}, /* 5392_todo */
	{RF_R51,		0x3A}, /* 20101018 update */
	{RF_R52,		0x48}, /* 20101018 update. */
	{RF_R53,		0x44}, /* 20101018 update. */
	{RF_R54,		0x38},
	{RF_R55,		0x43},
	{RF_R56,		0xA1}, /* 20101018 update. */
	{RF_R57,		0x00}, /* 20101018 update.*/
	{RF_R58,		0x39}, 
	{RF_R59,		0x07}, /* 20101018 update. */
	{RF_R60,		0x45}, /* 20101018 update.*/
	{RF_R61,		0x91}, /* 20101018 update. */
	{RF_R62,		0x39}, /* Spare */
	{RF_R63,		0x00}, /* Spare */
};

#define NUM_RF_5392_REG_PARMS (sizeof(RF5392RegTable) / sizeof(REG_PAIR))


RTMP_REG_PAIR	RT5390_MACRegTable[] =	{
	{TX_SW_CFG0,		0x404},   // 2010-07-20
};

UCHAR RT5390_NUM_MAC_REG_PARMS = (sizeof(RT5390_MACRegTable) / sizeof(RTMP_REG_PAIR));

#ifdef RTMP_INTERNAL_TX_ALC
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)

/* The Tx power tuning entry */

TX_POWER_TUNING_ENTRY_STRUCT RT5390_TxPowerTuningTable[] = 
{
/*	idxTxPowerTable		Tx power control over RF		Tx power control over MAC */
/*	(zero-based array)		{ RF R49[5:0]: Tx0 ALC},		{MAC 0x1314~0x1324} */
/*     0       */				{0x00,					-15}, 
/*     1       */ 				{0x01,					-15}, 
/*     2       */ 				{0x00,					-14}, 
/*     3       */ 				{0x01,					-14}, 
/*     4       */ 				{0x00,					-13}, 
/*     5       */				{0x01,					-13}, 
/*     6       */ 				{0x00,					-12}, 
/*     7       */ 				{0x01,					-12}, 
/*     8       */ 				{0x00,					-11}, 
/*     9       */ 				{0x01,					-11}, 
/*     10     */ 				{0x00,					-10}, 
/*     11     */ 				{0x01,					-10}, 
/*     12     */ 				{0x00,					-9}, 
/*     13     */ 				{0x01,					-9}, 
/*     14     */ 				{0x00,					-8}, 
/*     15     */ 				{0x01,					-8}, 
/*     16     */ 				{0x00,					-7}, 
/*     17     */ 				{0x01,					-7}, 
/*     18     */ 				{0x00,					-6}, 
/*     19     */ 				{0x01,					-6}, 
/*     20     */ 				{0x00,					-5}, 
/*     21     */ 				{0x01,					-5}, 
/*     22     */ 				{0x00,					-4}, 
/*     23     */ 				{0x01,					-4}, 
/*     24     */ 				{0x00,					-3}, 
/*     25     */ 				{0x01,					-3}, 
/*     26     */ 				{0x00,					-2}, 
/*     27     */ 				{0x01,					-2}, 
/*     28     */ 				{0x00,					-1}, 
/*     29     */ 				{0x01,					-1}, 
/*     30     */ 				{0x00,					0}, 
/*     31     */ 				{0x01,					0}, 
/*     32     */ 				{0x02,					0}, 
/*     33     */ 				{0x03,					0}, 
/*     34     */ 				{0x04,					0}, 
/*     35     */ 				{0x05,					0}, 
/*     36     */ 				{0x06,					0}, 
/*     37     */ 				{0x07,					0}, 
/*     38     */ 				{0x08,					0}, 
/*     39     */ 				{0x09,					0}, 
/*     40     */ 				{0x0A,					0}, 
/*     41     */ 				{0x0B,					0}, 
/*     42     */ 				{0x0C,					0}, 
/*     43     */ 				{0x0D,					0}, 
/*     44     */ 				{0x0E,					0}, 
/*     45     */ 				{0x0F,					0}, 
/*     46     */ 				{0x0F,					0}, 
/*     47     */ 				{0x10,					0}, 
/*     48     */ 				{0x11,					0}, 
/*     49     */ 				{0x12,					0}, 
/*     50     */ 				{0x13,					0}, 
/*     51     */ 				{0x14,					0}, 
/*     52     */ 				{0x15,					0}, 
/*     53     */ 				{0x16,					0}, 
/*     54     */ 				{0x17,					0}, 
/*     55     */ 				{0x18,					0}, 
/*     56     */ 				{0x19,					0}, 
/*     57     */ 				{0x1A,					0}, 
/*     58     */ 				{0x1B,					0}, 
/*     59     */ 				{0x1C,					0}, 
/*     60     */ 				{0x1D,					0}, 
/*     61     */ 				{0x1E,					0}, 
/*     62     */ 				{0x1F,					0}, 
/*     63     */                                 	{0x20,                                       0}, 
/*     64     */                                 	{0x21,                                       0}, 
/*     65     */                                 	{0x22,                                       0}, 
/*     66     */                                 	{0x23,                                       0}, 
/*     67     */                                 	{0x24,                                       0}, 
/*     68     */                                 	{0x25,                                       0}, 
/*     69     */                                 	{0x26,                                       0}, 
/*     70     */                                 	{0x27,                                       0}, 
/*     71     */                                 	{0x27-1,                                   1}, 
/*     72     */                                 	{0x27,                                       1}, 
/*     73     */                                 	{0x27-1,                                   2}, 
/*     74     */                                 	{0x27,                                       2}, 
/*     75     */                                 	{0x27-1,                                   3}, 
/*     76     */                       		{0x27,                                       3}, 
/*     77     */                       		{0x27-1,                                   4}, 
/*     78     */                       		{0x27,                                       4}, 
/*     79     */                       		{0x27-1,                                   5}, 
/*     80     */                       		{0x27,                                       5}, 
/*     81     */                       		{0x27-1,                                   6}, 
/*     82     */                       		{0x27,                                       6}, 
/*     83     */                       		{0x27-1,                                   7}, 
/*     84     */                       		{0x27,                                       7}, 
/*     85     */                       		{0x27-1,                                   8}, 
/*     86     */                       		{0x27,                                       8}, 
/*     87     */                       		{0x27-1,                                   9}, 
/*     88     */                       		{0x27,                                       9}, 
/*     89     */                       		{0x27-1,                                   10}, 
/*     90     */                       		{0x27,                                       10}, 
/*     91     */                       		{0x27-1,                                   11}, 
/*     92     */                       		{0x27,                                       11}, 
/*     93     */                       		{0x27-1,                                   12}, 
/*     94     */                       		{0x27,                                       12}, 
/*     95     */                      		{0x27-1,                                   13}, 
/*     96     */                       		{0x27,                                       13}, 
/*     97     */                       		{0x27-1,                                   14}, 
/*     98     */                       		{0x27,                                       14}, 
/*     99     */                       		{0x27-1,                                   15}, 
/*     100   */                        		{0x27,                                       15}, 
};
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */
ULONG TssiRatioTable[][2] = 
	{
/*	{numerator,	denominator}	Power delta (dBm)	Ratio		Index */
	{955,		10000}, 		/* -12			0.0955	0 */
	{1161, 		10000},		/* -11			0.1161	1 */
	{1413,		10000}, 		/* -10			0.1413	2 */
	{1718,		10000},		/* -9			0.1718	3 */
	{2089, 		10000},		/* -8			0.2089	4 */
	{2541, 		10000}, 		/* -7 			0.2541	5 */
	{3090, 		10000}, 		/* -6 			0.3090	6 */
	{3758, 		10000}, 		/* -5 			0.3758	7 */
	{4571, 		10000}, 		/* -4 			0.4571	8 */
	{5559, 		10000}, 		/* -3 			0.5559	9 */
	{6761, 		10000}, 		/* -2 			0.6761	10 */
	{8222, 		10000}, 		/* -1 			0.8222	11 */
	{1, 			1}, 			/* 0	 			1		12 */
	{12162, 		10000}, 		/* 1				1.2162	13 */
	{14791, 		10000}, 		/* 2				1.4791	14 */
	{17989, 		10000}, 		/* 3				1.7989	15 */
	{21878, 		10000}, 		/* 4				2.1878	16 */
	{26607, 		10000}, 		/* 5				2.6607	17 */
	{32359, 		10000}, 		/* 6				3.2359	18 */
	{39355, 		10000}, 		/* 7				3.9355	19 */
	{47863, 		10000}, 		/* 8				4.7863	20 */
	{58210, 		10000}, 		/* 9				5.8210	21 */
	{70795, 		10000}, 		/* 10			7.0795	22 */
	{86099, 		10000}, 		/* 11			8.6099	23 */
	{104713, 		10000}, 		/* 12			10.4713	24 */
};


#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)

/* The desired TSSI over CCK (with extended TSSI information) */
CHAR RT5390_desiredTSSIOverCCKExt[NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET + 1][4];

/* The desired TSSI over OFDM (with extended TSSI information) */
CHAR RT5390_desiredTSSIOverOFDMExt[NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET + 1][8];

/* The desired TSSI over HT (with extended TSSI information) */
CHAR RT5390_desiredTSSIOverHTExt[NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET + 1][8];

/* The desired TSSI over HT using STBC (with extended TSSI information) */
CHAR RT5390_desiredTSSIOverHTUsingSTBCExt[NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET + 1][8];

#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */

#endif /* RTMP_INTERNAL_TX_ALC */



/*
========================================================================
Routine Description:
	Initialize RT5390.

Arguments:
	pAd					- WLAN control block pointer

Return Value:
	None

Note:
========================================================================
*/
VOID RT5390_Init(
	IN PRTMP_ADAPTER		pAd)
{
#ifdef RTMP_INTERNAL_TX_ALC
	extern TX_POWER_TUNING_ENTRY_STRUCT *TxPowerTuningTable;
#endif /* RTMP_INTERNAL_TX_ALC */
	RTMP_CHIP_OP *pChipOps = &pAd->chipOps;
	RTMP_CHIP_CAP *pChipCap = &pAd->chipCap;


	/* ??? */
/*	pAd->RfIcType = RFIC_3320; */

	/* init capability */
	pChipCap->MaxNumOfRfId = 63;
	pChipCap->MaxNumOfBbpId = 255;
	
	pChipCap->pRFRegTable = RF5390RegTable;
	if (IS_RT5392(pAd))
		pChipCap->pRFRegTable = RF5392RegTable;
		
	pChipCap->pBBPRegTable = NULL; /* The same with BBPRegTable */
	pChipCap->bbpRegTbSize = 0; // warning!! by sw
	pChipCap->SnrFormula = SNR_FORMULA3;
	pChipCap->RfReg17WtMethod = RF_REG_WT_METHOD_STEP_ON;
	pChipOps->AsicRfInit = NICInitRT5390RFRegisters;
	pChipOps->AsicHaltAction = RT5390HaltAction;
	pChipOps->AsicRfTurnOff = RT5390LoadRFSleepModeSetup;
	pChipOps->AsicReverseRfFromSleepMode = RT5390ReverseRFSleepModeSetup;
	pChipCap->FlgIsHwWapiSup = TRUE;
	pChipCap->FlgIsVcoReCalSup = FALSE;/*is RT5390 need to do VCORecalibration ?*/
	pChipCap->FlgIsHwAntennaDiversitySup = FALSE; /*Do RT5390 support HwAntennaDiversity  ?*/
	/* init operator */
	pChipOps->AsicBbpInit = NICInitRT5390BbpRegisters;
	pChipOps->AsicMacInit = NICInitRT5390MacRegisters;
/*	pChipOps->AsicEeBufferInit = RT5390_AsicEeBufferInit; */
	pChipOps->RxSensitivityTuning = RT5390_RxSensitivityTuning;
/*	pChipOps->ChipResumeMsduTransmission = RT5390_ChipResumeMsduTransmission;*/
#ifdef CONFIG_STA_SUPPORT
	pChipOps->ChipStaBBPAdjust = RT5390_ChipStaBBPAdjust;
#endif /* CONFIG_STA_SUPPORT */
	pChipOps->ChipBBPAdjust = RT5390_ChipBBPAdjust;
	pChipOps->ChipSwitchChannel = RT5390_ChipSwitchChannel;

#ifdef RTMP_INTERNAL_TX_ALC
	pChipCap->TxAlcTxPowerUpperBound = 61;
	pChipCap->TxAlcMaxMCS = 7;
	if (IS_RT5392(pAd))
		pChipCap->TxAlcMaxMCS = 15;
	TxPowerTuningTable = RT5390_TxPowerTuningTable;

	pChipOps->InitDesiredTSSITable = RT5390_InitDesiredTSSITable;
	if (IS_RT5392(pAd))
	{
		pChipOps->ATETssiCalibration = NULL;	
		pChipOps->ATETssiCalibrationExtend = NULL;
	}
	else
	{
#ifdef RALINK_ATE	
		pChipOps->ATETssiCalibration = RT5390_ATETssiCalibration;	
		pChipOps->ATETssiCalibrationExtend = RT5390_ATETssiCalibrationExtend;
#endif /* RALINK_ATE */		
	}	
	pChipOps->AsicTxAlcGetAutoAgcOffset = RT5390_AsicTxAlcGetAutoAgcOffset;
#endif /* RTMP_INTERNAL_TX_ALC */

#ifdef RTMP_TEMPERATURE_COMPENSATION
	if (IS_RT5392(pAd))
		pChipOps->ATEReadExternalTSSI = RT5392_ATEReadExternalTSSI;
#endif /* RTMP_TEMPERATURE_COMPENSATION */

#ifdef RTMP_FREQ_CALIBRATION_SUPPORT
#ifdef CONFIG_STA_SUPPORT
	pChipOps->AsicFreqCalInit = InitFrequencyCalibration;
	pChipOps->AsicFreqCalStop = StopFrequencyCalibration;
	pChipOps->AsicFreqCal = FrequencyCalibration;
	pChipOps->AsicFreqOffsetGet = GetFrequencyOffset;
#endif /* CONFIG_STA_SUPPORT */
#endif /* RTMP_FREQ_CALIBRATION_SUPPORT */
	pChipOps->SetRxAnt = RT5390SetRxAnt;
	pChipOps->RTMPSetAGCInitValue = RT5390_RTMPSetAGCInitValue;
	pChipOps->AsicResetBbpAgent = RT5390_AsicResetBbpAgent;
	RtmpChipBcnSpecInit(pAd);
}



VOID NICInitRT5390RFRegisters(IN PRTMP_ADAPTER pAd)
{
		INT i;
		ULONG		RfReg = 0, data;
	/* Init RF calibration */
	/* Driver should toggle RF R30 bit7 before init RF registers */
	RT30xxReadRFRegister(pAd, RF_R02, (PUCHAR)&RfReg);
	RfReg = ((RfReg & ~0x80) | 0x80); /* rescal_en (initiate calbration) */
	RT30xxWriteRFRegister(pAd, RF_R02, (UCHAR)RfReg);
		
	RTMPusecDelay(1000);
		
	RfReg = ((RfReg & ~0x80) | 0x00); /* rescal_en (initiate calbration) */
	RT30xxWriteRFRegister(pAd, RF_R02, (UCHAR)RfReg);

	DBGPRINT(RT_DEBUG_TRACE, ("%s: Initialize the RF registers to the default values", __FUNCTION__));
		
	/* Initialize RF register to default value */
	if (IS_RT5392(pAd))
	{
		/* Initialize RF register to default value */
		for (i = 0; i < NUM_RF_5392_REG_PARMS; i++)
		{
#ifdef RT5370 /* For RT5372 */
			if (RF5392RegTable[i].Register == RF_R23)
			{
				RF5392RegTable[i].Value = 0x0f; /* 20101018 update. */
			}
			else if (RF5392RegTable[i].Register == RF_R24)
			{
				RF5392RegTable[i].Value = 0x3e; /* 20101018 update. */
			}
			else if (RF5392RegTable[i].Register == RF_R51)
			{
				RF5392RegTable[i].Value = 0x32; /* 20101018 update. */
			}
			else if (RF5392RegTable[i].Register == RF_R53)
			{
				RF5392RegTable[i].Value = 0x22; /* 20101018 update. */
			}
			else if (RF5392RegTable[i].Register == RF_R56)
			{
				RF5392RegTable[i].Value = 0xc1; /* 20101018 update. */
			}
			else if (RF5392RegTable[i].Register == RF_R59)
			{
				RF5392RegTable[i].Value = 0x0f; /* 20101018 update. */
			}
#endif /* RT5370 */	
				RT30xxWriteRFRegister(pAd, RF5392RegTable[i].Register, RF5392RegTable[i].Value);
		}
	}
	else if (IS_RT5390U(pAd))
	{
		DBGPRINT(RT_DEBUG_TRACE, ("%s: Initialize the RF registers to the default values (5390U)", __FUNCTION__));

		// Initialize RF register to default value
		for (i = 0; i < NUM_RF_5390U_REG_PARMS; i++)
		{	
			RT30xxWriteRFRegister(pAd, RF5390URegTable[i].Register, RF5390URegTable[i].Value);
		}
	}
	else if (IS_RT5390(pAd) && !IS_MINI_CARD(pAd))
	{
		/* Initialize RF register to default value */
	for (i = 0; i < NUM_RF_5390_REG_PARMS; i++)
	{
#ifdef RT5370
			if (IS_RT5390F(pAd) && (RF5390RegTable[i].Register == RF_R06)) /* >= RT5390F */
			{
				RF5390RegTable[i].Value = 0xE0;
			}
			else if (IS_RT5390F(pAd) && (RF5390RegTable[i].Register == RF_R25)) /* >= RT5390F */
			{
				RF5390RegTable[i].Value = 0x80;
			}
			else if (IS_RT5390F(pAd) && (RF5390RegTable[i].Register == RF_R40)) /* >= RT5390F */
			{
				RF5390RegTable[i].Value = 0x0B;
			}
			else if (IS_RT5390F(pAd) && (RF5390RegTable[i].Register == RF_R46)) /* >= RT5390F */
			{
				RF5390RegTable[i].Value = 0x73;
			}
			else if (IS_RT5390F(pAd) && (RF5390RegTable[i].Register == RF_R53)) /* >= RT5390F */
			{
				RF5390RegTable[i].Value = 0x00;
			}
			else if (IS_RT5390F(pAd) && (RF5390RegTable[i].Register == RF_R56)) /* >= RT5390F */
			{
				RF5390RegTable[i].Value = 0x42;
			}
			else if (IS_RT5390F(pAd) && (RF5390RegTable[i].Register == RF_R61)) /* >= RT5390F */
			{
				RF5390RegTable[i].Value = 0xD1;
			}
#endif /* RT5370 */

		RT30xxWriteRFRegister(pAd, RF5390RegTable[i].Register, RF5390RegTable[i].Value);
	}
	}

			/* 
		        Where to add the following codes?
			 RT5390BC8, Disable RF_R40 bit[6] to save power consumption
		*/
		if (pAd->NicConfig2.field.CoexBit == TRUE)
		{
			RT30xxReadRFRegister(pAd, RF_R40, (PUCHAR)&RfReg);
			RfReg &= (~0x40);
			RT30xxWriteRFRegister(pAd, RF_R40, (UCHAR)RfReg);
		}
		/* Give bbp filter initial value   Moved here from RTMPFilterCalibration( ) */
	pAd->Mlme.CaliBW20RfR24 = 0x1F;
	pAd->Mlme.CaliBW40RfR24 = 0x2F; /* Bit[5] must be 1 for BW 40 */
	/* For RF filter Calibration */
	/* RTMPFilterCalibration(pAd); */

	/* Initialize RF R27 register, set RF R27 must be behind RTMPFilterCalibration() */
	if ((pAd->MACVersion & 0xffff) < 0x0211)
		RT30xxWriteRFRegister(pAd, RF_R27, 0x3);

	/* set led open drain enable */
	RTMP_IO_READ32(pAd, OPT_14, &data);
	data |= 0x01;
	RTMP_IO_WRITE32(pAd, OPT_14, data);

	RTMP_IO_WRITE32(pAd, TX_SW_CFG1, 0);
	RTMP_IO_WRITE32(pAd, TX_SW_CFG2, 0x0);

	/* set default antenna as main */
	RT5390SetRxAnt(pAd, pAd->RxAnt.Pair1PrimaryRxAnt);

	/* patch RSSI inaccurate issue, due to design change */
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R79, 0x13);
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R80, 0x05);
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R81, 0x33);

	/* enable DC filter */
	if ((pAd->MACVersion & 0xffff) >= 0x0211)
	{
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R103, 0xc0);
	}
	/*
		From RT3071 Power Sequence v1.1 document, the Normal Operation Setting Registers as follow :
	 	BBP_R138 / RF_R1 / RF_R15 / RF_R17 / RF_R20 / RF_R21.
		add by johnli, RF power sequence setup, load RF normal operation-mode setup 
	*/
	RT5390LoadRFNormalModeSetup(pAd);
	
	/* adjust some BBP register contents */
	/* also can put these BBP registers to pBBPRegTable */
	
}


#ifdef RTMP_FLASH_SUPPORT

VOID RT5390_AsicEeBufferInit(
	IN	RTMP_ADAPTER *pAd)
{
	extern UCHAR *EeBuffer;


	EeBuffer = RT5390_EeBuffer;
}

#endif /* RTMP_FLASH_SUPPORT */


/*
 Antenna divesity use GPIO3 and EESK pin for control
 Antenna and EEPROM access are both using EESK pin,
 Therefor we should avoid accessing EESK at the same time
 Then restore antenna after EEPROM access
 The original name of this function is AsicSetRxAnt(), now change to 
*/
VOID RT5390SetRxAnt(
	IN PRTMP_ADAPTER	pAd,
	IN UCHAR			Ant)
{
	UINT32	Value;

	if (/*(!pAd->NicConfig2.field.AntDiversity) || */
		(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS))	||
		(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS))	||
		(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RADIO_OFF)) ||
		(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)))
	{
		return;
	}

if (IS_RT5390(pAd)
		)
	{
		UCHAR BbpValue = 0;
		
		if (Ant == 0) /* 0: Main antenna */
		{
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R152, &BbpValue);
			BbpValue = ((BbpValue & ~0x80) | (0x80));
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R152, BbpValue);

			DBGPRINT(RT_DEBUG_TRACE, ("AsicSetRxAnt, switch to main antenna\n"));
		}
		else /* 1: Aux. antenna */
		{
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R152, &BbpValue);
			BbpValue = ((BbpValue & ~0x80) | (0x00));
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R152, BbpValue);

			DBGPRINT(RT_DEBUG_TRACE, ("AsicSetRxAnt, switch to aux. antenna\n"));
		}
	}
}

/*
	add by johnli, RF power sequence setup

	==========================================================================
	Description:

	Load RF normal operation-mode setup
	
	==========================================================================
 */
VOID RT5390LoadRFNormalModeSetup(
	IN PRTMP_ADAPTER 	pAd)
{
	UCHAR RFValue, bbpreg = 0;

	/* improve power consumption */
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R138, &bbpreg);
	if (pAd->Antenna.field.TxPath == 1)
	{
		/* turn off tx DAC_1 */
		bbpreg = (bbpreg | 0x20);
	}
	if (pAd->Antenna.field.RxPath == 1)
	{
		/* turn off tx ADC_1 */
		bbpreg &= (~0x2);
	}
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R138, bbpreg);
	
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
	if (IS_RT5390(pAd))
	{
		
		RT30xxReadRFRegister(pAd, RF_R38, (PUCHAR)&RFValue);
		RFValue = ((RFValue & ~0x20) | 0x00); /* rx_lo1_en (enable RX LO1, 0: LO1 follows TR switch) */
		RT30xxWriteRFRegister(pAd, RF_R38, (UCHAR)RFValue);

		RT30xxReadRFRegister(pAd, RF_R39, (PUCHAR)&RFValue);
		RFValue = ((RFValue & ~0x80) | 0x00); /* rx_lo2_en (enable RX LO2, 0: LO2 follows TR switch) */
		RT30xxWriteRFRegister(pAd, RF_R39, (UCHAR)RFValue);

		
		/* Avoid data lost and CRC error */
		
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &bbpreg);
		bbpreg = ((bbpreg & ~0x40) | 0x40); /* MAC interface control (MAC_IF_80M, 1: 80 MHz) */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, bbpreg);

		RT30xxReadRFRegister(pAd, RF_R30, (PUCHAR)&RFValue);
		RFValue = ((RFValue & ~0x18) | 0x10); /* rxvcm (Rx BB filter VCM) */
		RT30xxWriteRFRegister(pAd, RF_R30, (UCHAR)RFValue);
	}
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */
	
}

/*
	==========================================================================
	Description:

	Load RF sleep-mode setup
	
	==========================================================================
 */
VOID RT5390LoadRFSleepModeSetup(
	IN PRTMP_ADAPTER 	pAd)
{
	UCHAR RFValue;
	UINT32 MACValue;


	if (IS_RT5390(pAd))
	{
		UCHAR	rfreg;
		
		RT30xxReadRFRegister(pAd, RF_R01, &rfreg);
		rfreg = ((rfreg & ~0x01) | 0x00); /* vco_en */
		RT30xxWriteRFRegister(pAd, RF_R01, rfreg);

		RT30xxReadRFRegister(pAd, RF_R06, &rfreg);
		rfreg = ((rfreg & ~0xC0) | 0x00); /* vco_ic (VCO bias current control, 00: off) */
		RT30xxWriteRFRegister(pAd, RF_R06, rfreg);

		RT30xxReadRFRegister(pAd, RF_R22, &rfreg);
		rfreg = ((rfreg & ~0xE0) | 0x00); /* cp_ic (reference current control, 000: 0.25 mA) */
		RT30xxWriteRFRegister(pAd, RF_R22, rfreg);

		RT30xxReadRFRegister(pAd, RF_R42, &rfreg);
		rfreg = ((rfreg & ~0x40) | 0x00); /* rx_ctb_en */
		RT30xxWriteRFRegister(pAd, RF_R42, rfreg);

		RT30xxReadRFRegister(pAd, RF_R20, &rfreg);
		rfreg = ((rfreg & ~0x77) | 0x77); /* ldo_pll_vc and ldo_rf_vc (111: -0.15) */
		RT30xxWriteRFRegister(pAd, RF_R20, rfreg);
	}
		
	/* Don't touch LDO_CFG0 for 3090F & 3593, possibly the board is single power scheme */

	RTMP_IO_READ32(pAd, LDO_CFG0, &MACValue);
	MACValue |= 0x1D000000;
	RTMP_IO_WRITE32(pAd, LDO_CFG0, MACValue);
	
}

VOID RT5390HaltAction(
	IN PRTMP_ADAPTER 	pAd)
{
	UINT32		TxPinCfg = 0x00050F0F;

	
	/* Turn off LNA_PE or TRSW_POL */
	
	
	if ( IS_RT5390(pAd)
#ifdef RTMP_EFUSE_SUPPORT
		&& (pAd->bUseEfuse)
#endif /* RTMP_EFUSE_SUPPORT */
		)
	{
		TxPinCfg &= 0xFFFBF0F0; /* bit18 off */
	}
	else
	{
		TxPinCfg &= 0xFFFFF0F0;
	}

	RTMP_IO_WRITE32(pAd, TX_PIN_CFG, TxPinCfg);   
	
}


/*
	==========================================================================
	Description:

	Reverse RF sleep-mode setup
	
	==========================================================================
 */
VOID RT5390ReverseRFSleepModeSetup(
	IN PRTMP_ADAPTER 	pAd,
	IN BOOLEAN			FlgIsInitState)
{
	UCHAR RFValue;
	UINT32 MACValue;


#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
		if (IS_RT5390(pAd))
		{
				UCHAR	rfreg;
				
				RT30xxReadRFRegister(pAd, RF_R01, &rfreg);
				if (IS_RT5392(pAd))
				{
					rfreg = ((rfreg & ~0x3F) | 0x3F);
				}
				else
				{
				rfreg = ((rfreg & ~0x0F) | 0x0F); /* Enable rf_block_en, pll_en, rx0_en and tx0_en */
				}
				RT30xxWriteRFRegister(pAd, RF_R01, rfreg);

				RT30xxReadRFRegister(pAd, RF_R06, &rfreg);
				if (IS_RT5390F(pAd) || IS_RT5392C(pAd))
				{
					rfreg = ((rfreg & ~0xC0) | 0xC0); /* vco_ic (VCO bias current control, 11: high) */
				}
				else
				{
				rfreg = ((rfreg & ~0xC0) | 0x80); /* vco_ic (VCO bias current control, 10: mid.) */
				}
				RT30xxWriteRFRegister(pAd, RF_R06, rfreg);
				
				RT30xxReadRFRegister(pAd, RF_R02, &rfreg);
				rfreg = ((rfreg & ~0x80) | 0x80); /* rescal_en (initiate calibration) */
				RT30xxWriteRFRegister(pAd, RF_R02, rfreg);

				RT30xxReadRFRegister(pAd, RF_R22, &rfreg);
				rfreg = ((rfreg & ~0xE0) | 0x20); /* cp_ic (reference current control, 001: 0.33 mA) */
				RT30xxWriteRFRegister(pAd, RF_R22, rfreg);

				RT30xxReadRFRegister(pAd, RF_R42, &rfreg);
				rfreg = ((rfreg & ~0x40) | 0x40); /* rx_ctb_en */
				RT30xxWriteRFRegister(pAd, RF_R42, rfreg);
				RT30xxReadRFRegister(pAd, RF_R20, &rfreg);
				rfreg = ((rfreg & ~0x77) | 0x00); /* ldo_rf_vc and ldo_pll_vc ( 111: +0.15) */
				RT30xxWriteRFRegister(pAd, RF_R20, rfreg);
				RT30xxReadRFRegister(pAd, RF_R03, &rfreg);
				rfreg = ((rfreg & ~0x80) | 0x80); /* vcocal_en (initiate VCO calibration (reset after completion)) */
				RT30xxWriteRFRegister(pAd, RF_R03, rfreg);
				
			
		}
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */
		/* RT3071 version E has fixed this issue */
		if ((pAd->NicConfig2.field.DACTestBit == 1) && ((pAd->MACVersion & 0xffff) < 0x0211))
		{
			// patch tx EVM issue temporarily
			RTMP_IO_READ32(pAd, LDO_CFG0, &MACValue);
			MACValue = ((MACValue & 0xE0FFFFFF) | 0x0D000000);
			RTMP_IO_WRITE32(pAd, LDO_CFG0, MACValue);
		}


}
/* end johnli*/

VOID RT5390_RxSensitivityTuning(
	IN PRTMP_ADAPTER			pAd)
{

/*How to tuning rxsensitivity ?*/	
//	DBGPRINT(RT_DEBUG_TRACE,("turn off R17 tuning, restore to 0x%02x\n", R66));
}


/*
========================================================================
Routine Description:
	Initialize specific MAC registers.

Arguments:
	pAd					- WLAN control block pointer

Return Value:
	None

Note:
========================================================================
*/
VOID NICInitRT5390MacRegisters(
	IN	PRTMP_ADAPTER pAd)
{
	UINT32 IdReg;


	for(IdReg=0; IdReg<RT5390_NUM_MAC_REG_PARMS; IdReg++)
	{
		RTMP_IO_WRITE32(pAd, RT5390_MACRegTable[IdReg].Register,
								RT5390_MACRegTable[IdReg].Value);
	}
}


/*
========================================================================
Routine Description:
	Initialize specific BBP registers.

Arguments:
	pAd					- WLAN control block pointer

Return Value:
	None

Note:
========================================================================
*/
VOID NICInitRT5390BbpRegisters(
	IN	PRTMP_ADAPTER pAd)
{
	UCHAR BbpReg = 0;
	BBP_R105_STRUC BBPR105 = { { 0 } };
/*	BBP_R106_STRUC BBPR106 = { { 0 } }; */
	
	DBGPRINT(RT_DEBUG_TRACE, ("--> %s\n", __FUNCTION__));

	
	/*  The channel estimation updates based on remodulation of L-SIG and HT-SIG symbols. */
	
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R105, &BBPR105.byte);
	
	 /* Apply Maximum Likelihood Detection (MLD) for 2 stream case (reserved field if single RX) */
	
	{
	if (pAd->Antenna.field.RxPath == 1) /* Single RX */
	{
		BBPR105.field.MLDFor2Stream = 0;
	}
	else
	{
		BBPR105.field.MLDFor2Stream = 1;
	}
	}
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R105, BBPR105.byte);

	DBGPRINT(RT_DEBUG_TRACE, ("%s: BBP_R105: BBPR105.field.EnableSIGRemodulation = %d, BBPR105.field.MLDFor2Stream = %d\n", 
		__FUNCTION__, 
		BBPR105.field.EnableSIGRemodulation, 
		BBPR105.field.MLDFor2Stream));

	{
		/*   Avoid data lost and CRC error */		
		
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &BbpReg);
		BbpReg = ((BbpReg & ~0x40) | 0x40); /* MAC interface control (MAC_IF_80M, 1: 80 MHz) */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, BbpReg);

		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R31, 0x08); /* ADC/DAC contro */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R68, 0x0B); /* Rx AGC energy lower bound in log2 */
		
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x0A); /* Rx AGC SQ CCK Xcorr threshold */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x13); /* Rx AGC SQ ACorr threshold */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R75, 0x46); /* Rx high power VGA offset for LNA offset */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R76, 0x28); /* Rx medium power VGA offset for LNA offset */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R77, 0x59); /* Rx high/medium power threshold in log2 */
		
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R82, 0x62); /* Rx AGC LNA select threshold in log2 */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R83, 0x7A); /* Rx AGC LNA MM select threshold in log2 */
		
		if (IS_RT5392(pAd))
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R84, 0x9A); /* Rx AGC VGA/LNA delay */
		else
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R84, 0x19); /* Rx AGC VGA/LNA delay */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R86, 0x38); /* Rx AGC high gain threshold in dB */
		
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R91, 0x04); /* Guard interval delay counter for 20M band */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R92, 0x02); /* Guard interval delay counter for 40M band */

		if (IS_RT5392(pAd))
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R95, 0x9A); /* CCK MRC decode */
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R98, 0x12); /* TX CCK higher gain */ 
		}

		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R103, 0xC0); /* Rx - 11b adaptive equalizer gear down control and signal energy average period */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R104, 0x92); /* SIGN detection threshold/GF CDD control */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R105, 0x3C); /* FEQ control */

		if (IS_RT5392(pAd))
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R106, 0x05); /* GI remover */
		else			
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R106, 0x03); /* GI remover */

		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R128, 0x12); /* R/W remodulation control */
		
		if (IS_RT5392(pAd))
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R134, 0xD0); /* TX CCK higher gain */
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R135, 0xF6); /* TX CCK higher gain */ 
		}
		



	}
	/* KH Notice:Ian codes has the following part, but Zero remove it. Why? */
	{
			if (pAd->NicConfig2.field.AntOpt == 1)
			{
				if (pAd->NicConfig2.field.AntDiversity == 0) // 0: Main antenna
				{
					RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R152, &BbpReg);
					BbpReg = ((BbpReg & ~0x80) | (0x80));
					RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R152, BbpReg);

					DBGPRINT(RT_DEBUG_TRACE, ("%s, switch to main antenna\n", __FUNCTION__));
				}
				else // 1: Aux. antenna
				{
					RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R152, &BbpReg);
					BbpReg = ((BbpReg & ~0x80) | (0x00));
					RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R152, BbpReg);

					DBGPRINT(RT_DEBUG_TRACE, ("%s, switch to aux. antenna\n", __FUNCTION__));
				}
			}
			else if (pAd->NicConfig2.field.AntDiversity == 0)	// Diversity is Off, set to Main Antenna as default
			{
					RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R152, &BbpReg);
					BbpReg = ((BbpReg & ~0x80) | (0x80));
					RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R152, BbpReg);
	
					DBGPRINT(RT_DEBUG_TRACE, ("%s, switch to main antenna as default ...... 3\n", __FUNCTION__));
			}
		}
	if (!IS_RT5392(pAd))
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R154, 0x0); /* Disable HW Antenna Diversity (5390/5370 only) */
	DBGPRINT(RT_DEBUG_TRACE, ("<-- %s\n", __FUNCTION__));
}
#ifdef CONFIG_STA_SUPPORT
/*
	==========================================================================
	Description:
		dynamic tune BBP R66 to find a balance between sensibility and 
		noise isolation

	IRQL = DISPATCH_LEVEL

	==========================================================================
 */
UCHAR RT5390_ChipStaBBPAdjust(
	IN PRTMP_ADAPTER		pAd,
	IN CHAR					Rssi,
	IN UCHAR				R66)
{
	UCHAR	OrigR66Value = 0;/* R66UpperBound = 0x30, R66LowerBound = 0x30; */
	
	/* work as a STA */
	
	if (pAd->Mlme.CntlMachine.CurrState != CNTL_IDLE)  /* no R66 tuning when SCANNING */
		return 0;

	if ((pAd->OpMode == OPMODE_STA) 
		&& (OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED)
			)
		&& !(OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_DOZE))
		)
	{
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R66, &OrigR66Value);
		R66 = OrigR66Value;
		if (pAd->Antenna.field.RxPath > 1)
			Rssi = (pAd->StaCfg.RssiSample.AvgRssi0 + pAd->StaCfg.RssiSample.AvgRssi1) >> 1;
		else
			Rssi = pAd->StaCfg.RssiSample.AvgRssi0;
	
		if (pAd->LatchRfRegs.Channel <= 14)
		{	/* BG band */

			/* RT3070 is a no LNA solution, it should have different control regarding to AGC gain control */
			/* Otherwise, it will have some throughput side effect when low RSSI */
			
			{
				if (Rssi > RSSI_FOR_MID_LOW_SENSIBILITY)
				{
					R66 = 0x1C + 2*GET_LNA_GAIN(pAd) + 0x20;
					if (OrigR66Value != R66)
					{
						if (IS_RT5390(pAd))
						{
							if (IS_RT5392(pAd))
								RT5392WriteBBPR66(pAd, R66);
							else	
								RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
							
							RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R83, 0x4A);
						}				
						else
							RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);		
					}
				}
				else
				{
					R66 = 0x1C + 2*GET_LNA_GAIN(pAd);
					if (OrigR66Value != R66)
					{
						if (IS_RT5390(pAd))
						{
							if (IS_RT5392(pAd))
								RT5392WriteBBPR66(pAd, R66);
							else	
								RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
							
							RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R83, 0x7A);

						}	
						else
							RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
					}
				}
			}
		}

		return R66;
	}
	return 0;
}
#endif // CONFIG_STA_SUPPORT //

VOID RT5390_ChipBBPAdjust(
	IN RTMP_ADAPTER			*pAd)
{
	UINT32 Value;
	UCHAR byteValue = 0;

#ifdef DOT11_N_SUPPORT
	if ((pAd->CommonCfg.HtCapability.HtCapInfo.ChannelWidth  == BW_40) && 
		(pAd->CommonCfg.RegTransmitSetting.field.EXTCHA == EXTCHA_ABOVE)
		/*(pAd->CommonCfg.AddHTInfo.AddHtInfo.ExtChanOffset == EXTCHA_ABOVE)*/
	)
	{
		{
		pAd->CommonCfg.BBPCurrentBW = BW_40;
		pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel + 2;
		}
		/* TX : control channel at lower */ 
		RTMP_IO_READ32(pAd, TX_BAND_CFG, &Value);
		Value &= (~0x1);
		RTMP_IO_WRITE32(pAd, TX_BAND_CFG, Value);

		/*  RX : control channel at lower */ 
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &byteValue);
		byteValue &= (~0x20);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, byteValue);

		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &byteValue);
		byteValue &= (~0x18);
		byteValue |= 0x10;
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, byteValue);

		if (pAd->CommonCfg.Channel > 14)
		{ 	/* request by Gary 20070208 for middle and long range A Band */
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, 0x48);
		}
		else
		{	/* request by Gary 20070208 for middle and long range G Band */
			if (IS_RT5392(pAd))
					RT5392WriteBBPR66(pAd, 0x38);
			else	
					RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, 0x38);
		}	 
		if (pAd->MACVersion == 0x28600100)
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x1A);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x0A);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x16);
		}
		else
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x12);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x0A);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x10);
		}	

		DBGPRINT(RT_DEBUG_TRACE, ("ApStartUp : ExtAbove, ChannelWidth=%d, Channel=%d, ExtChanOffset=%d(%d) \n",
									pAd->CommonCfg.HtCapability.HtCapInfo.ChannelWidth, 
									pAd->CommonCfg.Channel, 
									pAd->CommonCfg.RegTransmitSetting.field.EXTCHA,
									pAd->CommonCfg.AddHTInfo.AddHtInfo.ExtChanOffset));
	}
	else if ((pAd->CommonCfg.Channel > 2) && 
			(pAd->CommonCfg.HtCapability.HtCapInfo.ChannelWidth  == BW_40) && 
			(pAd->CommonCfg.RegTransmitSetting.field.EXTCHA == EXTCHA_BELOW)
			/*(pAd->CommonCfg.AddHTInfo.AddHtInfo.ExtChanOffset == EXTCHA_BELOW)*/)
	{
		pAd->CommonCfg.BBPCurrentBW = BW_40;

		if (pAd->CommonCfg.Channel == 14)
			pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel - 1;
		else
			pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel - 2;
		/*  TX : control channel at upper */ 
		RTMP_IO_READ32(pAd, TX_BAND_CFG, &Value);
		Value |= (0x1);		
		RTMP_IO_WRITE32(pAd, TX_BAND_CFG, Value);

		/*  RX : control channel at upper */
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R3, &byteValue);
		byteValue |= (0x20);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R3, byteValue);

		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &byteValue);
		byteValue &= (~0x18);
		byteValue |= 0x10;
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, byteValue);
		
		if (pAd->CommonCfg.Channel > 14)
		{ 	/* request by Gary 20070208 for middle and long range A Band */
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, 0x48);
		}
		else
		{ 	/* request by Gary 20070208 for middle and long range G band */
			if (IS_RT5392(pAd))
				RT5392WriteBBPR66(pAd, 0x38);
			else	
				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, 0x38);
		}	
			
		if (pAd->MACVersion == 0x28600100)
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x1A);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x0A);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x16);
		}
		else
		{	
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x12);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x0A);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x10);
		}
		DBGPRINT(RT_DEBUG_TRACE, ("ApStartUp : ExtBlow, ChannelWidth=%d, Channel=%d, ExtChanOffset=%d(%d) \n",
									pAd->CommonCfg.HtCapability.HtCapInfo.ChannelWidth, 
									pAd->CommonCfg.Channel, 
									pAd->CommonCfg.RegTransmitSetting.field.EXTCHA,
									pAd->CommonCfg.AddHTInfo.AddHtInfo.ExtChanOffset));
	}
	else
#endif /* DOT11_N_SUPPORT */
	{
		pAd->CommonCfg.BBPCurrentBW = BW_20;
		pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel;
		
		/*  TX : control channel at lower */ 
		RTMP_IO_READ32(pAd, TX_BAND_CFG, &Value);
		Value &= (~0x1);
		RTMP_IO_WRITE32(pAd, TX_BAND_CFG, Value);

		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R4, &byteValue);
		byteValue &= (~0x18);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R4, byteValue);
		
		/* 20 MHz bandwidth */
 		if (pAd->CommonCfg.Channel > 14)
		{	 /* request by Gary 20070208 */
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, 0x40);
		}	
		else
		{	/* request by Gary 20070208 */
			/* RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, 0x30); */
			/* request by Brian 20070306 */
			if (IS_RT5392(pAd))
				RT5392WriteBBPR66(pAd, 0x38);
			else	
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, 0x38);
		}	
				 		if (pAd->MACVersion == 0x28600100)
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x16);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x08);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x11);
		}
		else
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R69, 0x12);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R70, 0x0a);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R73, 0x10);
		}

#ifdef DOT11_N_SUPPORT
		DBGPRINT(RT_DEBUG_TRACE, ("ApStartUp : 20MHz, ChannelWidth=%d, Channel=%d, ExtChanOffset=%d(%d) \n",
										pAd->CommonCfg.HtCapability.HtCapInfo.ChannelWidth, 
										pAd->CommonCfg.Channel, 
										pAd->CommonCfg.RegTransmitSetting.field.EXTCHA,
										pAd->CommonCfg.AddHTInfo.AddHtInfo.ExtChanOffset));
#endif /* DOT11_N_SUPPORT */
	}
	
	if (pAd->CommonCfg.Channel > 14)
	{	/* request by Gary 20070208 for middle and long range A Band */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R62, 0x1D);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R63, 0x1D);
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R64, 0x1D);
		/* RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R86, 0x1D); */
	}
	else
	{ 	/* request by Gary 20070208 for middle and long range G band */
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R62, 0x2D);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R63, 0x2D);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R64, 0x2D);
			/* RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R86, 0x2D); */
	}	

}

VOID RT5390_ChipSwitchChannel(
	IN PRTMP_ADAPTER 			pAd,
	IN UCHAR					Channel,
	IN BOOLEAN					bScan) 
{
	CHAR    TxPwer = 0, TxPwer2 = DEFAULT_RF_TX_POWER; /* Bbp94 = BBPR94_DEFAULT, TxPwer2 = DEFAULT_RF_TX_POWER; */
	UCHAR	index;
	UINT32 	Value = 0; /* BbpReg, Value; */
	UCHAR 	RFValue;
#ifdef DOT11N_SS3_SUPPORT
	CHAR    TxPwer3 = 0;
#endif /* DOT11N_SS3_SUPPORT */

#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
       UCHAR Tx0FinePowerCtrl = 0, Tx1FinePowerCtrl = 0;
       BBP_R109_STRUC BbpR109 = {{0}};
	BBP_R110_STRUC BbpR110 = {{0}};
	UCHAR TxRxh20M = 0;
	UCHAR PreRFValue = 0;
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */



	RFValue = 0;
	/* Search Tx power value */


	/*
		We can't use ChannelList to search channel, since some central channl's txpowr doesn't list 
		in ChannelList, so use TxPower array instead.
	*/
	for (index = 0; index < MAX_NUM_OF_CHANNELS; index++)
	{
		if (Channel == pAd->TxPower[index].Channel)
		{
			TxPwer = pAd->TxPower[index].Power;
			TxPwer2 = pAd->TxPower[index].Power2;
#ifdef DOT11N_SS3_SUPPORT
			if (IS_RT2883(pAd) || IS_RT3593(pAd) || IS_RT3883(pAd))
		    	TxPwer3 = pAd->TxPower[index].Power3;
#endif /* DOT11N_SS3_SUPPORT */


			if (IS_RT5390(pAd))/*&&
				//(pAd->infType == RTMP_DEV_INF_PCI || pAd->infType == RTMP_DEV_INF_PCIE))*/
			{
				Tx0FinePowerCtrl = pAd->TxPower[index].Tx0FinePowerCtrl;
				Tx1FinePowerCtrl = pAd->TxPower[index].Tx1FinePowerCtrl;
			}

			break;
		}
	}


	if (index == MAX_NUM_OF_CHANNELS)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("AsicSwitchChannel: Can't find the Channel#%d \n", Channel));
	}


#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
	if (IS_RT5390(pAd))
	{	
		for (index = 0; index < NUM_OF_3020_CHNL; index++)
		{

			if (Channel == FreqItems3020[index].Channel)
			{
				/* Programming channel parameters */
				RT30xxWriteRFRegister(pAd, RF_R08, FreqItems3020[index].N); /* N */
				RT30xxWriteRFRegister(pAd, RF_R09, (FreqItems3020[index].K & 0x0F)); /* K, N<11:8> is set to zero */

				RT30xxReadRFRegister(pAd, RF_R11, (PUCHAR)&RFValue);
				RFValue = ((RFValue & ~0x03) | (FreqItems3020[index].R & 0x03)); /* R */
				RT30xxWriteRFRegister(pAd, RF_R11, (UCHAR)RFValue);

				RT30xxReadRFRegister(pAd, RF_R49, (PUCHAR)&RFValue);
				RFValue = ((RFValue & ~0x3F) | (TxPwer & 0x3F)); /* tx0_alc */

				if ((RFValue & 0x3F) > 0x27) /* The valid range of the RF R49 (<5:0>tx0_alc<5:0>) is 0x00~0x27 */
				{
					RFValue = ((RFValue & ~0x3F) | 0x27);
				}

				RT30xxWriteRFRegister(pAd, RF_R49, (UCHAR)RFValue);

				if (IS_RT5392(pAd))
				{
					RT30xxReadRFRegister(pAd, RF_R50, &RFValue);
					RFValue = ((RFValue & ~0x3F) | (TxPwer2 & 0x3F)); /* tx0_alc */
					
					if ((RFValue & 0x3F) > 0x27) // The valid range of the RF R49 (<5:0>tx0_alc<5:0>) is 0x00~0x27
					{
						RFValue = ((RFValue & ~0x3F) | 0x27);
					}
					
					RT30xxWriteRFRegister(pAd, RF_R50, RFValue);
				}

				RT30xxReadRFRegister(pAd, RF_R01, (PUCHAR)&RFValue);

				if (IS_RT5392(pAd))
				{
					RFValue = ((RFValue & ~0x3F) | 0x3F);
				}
				else

				{
				RFValue = ((RFValue & ~0x0F) | 0x0F); /* Enable rf_block_en, pll_en, rx0_en and tx0_en */
				}
				RT30xxWriteRFRegister(pAd, RF_R01, (UCHAR)RFValue);
#ifdef CONFIG_STA_SUPPORT
#ifdef RTMP_FREQ_CALIBRATION_SUPPORT
				if (pAd->FreqCalibrationCtrl.bEnableFrequencyCalibration == TRUE)
				{
				        if (INFRA_ON(pAd)) /* Update the frequency offset from the adaptive frequency offset */
					{
						RT30xxReadRFRegister(pAd, RF_R17, (PUCHAR)&RFValue);
/*						pAd->PreRFXCodeValue = (UCHAR)RFValue;*/
						PreRFValue = RFValue;
						RFValue = ((RFValue & ~0x7F) | (pAd->FreqCalibrationCtrl.AdaptiveFreqOffset & 0x7F)); /* xo_code (C1 value control) - Crystal calibration */
/*						RFValue = min((INT)RFValue, 0x5F);
						RFMultiStepXoCode(pAd, RF_R17, (UCHAR)RFValue,pAd->PreRFXCodeValue);*/
						RFValue = min(RFValue, ((UCHAR)0x5F));// warning!! by sw
						if (PreRFValue != RFValue)
						{
							AsicSendCommandToMcu(pAd, 0x74, 0xff, RFValue, PreRFValue);
						}
					}
					else /* Update the frequency offset from EEPROM */
					{
						RT30xxReadRFRegister(pAd, RF_R17, (PUCHAR)&RFValue);
/*						pAd->PreRFXCodeValue = (UCHAR)RFValue;*/
						PreRFValue = RFValue;
						RFValue = ((RFValue & ~0x7F) | (pAd->RfFreqOffset & 0x7F)); /* xo_code (C1 value control) - Crystal calibration */
/*						RFValue = min((INT)RFValue, 0x5F);
						RFMultiStepXoCode(pAd, RF_R17, (UCHAR)RFValue,pAd->PreRFXCodeValue);*/
						RFValue = min(RFValue, ((UCHAR)0x5F));// warning!! by sw
						if (PreRFValue != RFValue)
						{
							AsicSendCommandToMcu(pAd, 0x74, 0xff, RFValue, PreRFValue);
						}
					}
				}
				else
#endif /* RTMP_FREQ_CALIBRATION_SUPPORT */					
#endif /* CONFIG_STA_SUPPORT */					
				{
					RT30xxReadRFRegister(pAd, RF_R17, (PUCHAR)&RFValue);
/*					pAd->PreRFXCodeValue = (UCHAR)RFValue;*/
					PreRFValue = RFValue;
					RFValue = ((RFValue & ~0x7F) | (pAd->RfFreqOffset & 0x7F)); /* xo_code (C1 value control) - Crystal calibration */
/*					RFValue = min((INT)RFValue, 0x5F);
					RFMultiStepXoCode(pAd, RF_R17, (UCHAR)RFValue,pAd->PreRFXCodeValue);*/
					RFValue = min(RFValue, ((UCHAR)0x5F));// warning!! by sw
					if (PreRFValue != RFValue)
					{
						AsicSendCommandToMcu(pAd, 0x74, 0xff, RFValue, PreRFValue);
					}
				}

				if ((!bScan) && (pAd->CommonCfg.BBPCurrentBW == BW_40)) /* BW 40 */
				{
					TxRxh20M = ((pAd->Mlme.CaliBW40RfR24 & 0x20) >> 5); /* Tx/Rx h20M */
				}
				else // BW 20
				{
					TxRxh20M = ((pAd->Mlme.CaliBW20RfR24 & 0x20) >> 5); /* Tx/Rx h20M */
				}
#ifdef RT5370
				if (IS_RT5392(pAd))
				{
					if ((Channel >= 1) && (Channel <= 11)) // chanel 1~10
					{
						RT30xxWriteRFRegister(pAd, RF_R59, 0x0F);
					}
					else if ((Channel >= 12) && (Channel <= 14)) // channel 11~14
					{
						RT30xxWriteRFRegister(pAd, RF_R59, 0x0B);
					}
				}
				else if (IS_RT5390U(pAd))
				{
					if ((Channel >= 1) && (Channel <= 4))
					{
						RT30xxWriteRFRegister(pAd, RF_R55, 0x23); // txvga_cc and pa1_cc_cck
					}
					else if ((Channel >= 5) && (Channel <= 6))
					{
						RT30xxWriteRFRegister(pAd, RF_R55, 0x13); // txvga_cc and pa1_cc_cck
					}
					else if ((Channel >= 7) && (Channel <= 14))
					{
						RT30xxWriteRFRegister(pAd, RF_R55, 0x03); // txvga_cc and pa1_cc_cck
					}			

					if ((Channel >= 1) && (Channel <= 10))
					{
						RT30xxWriteRFRegister(pAd, RF_R59, 0x07); // pa2_cc_ofdm and pa1_cc_ofdm
					}
					else if (Channel == 11)
					{
						RT30xxWriteRFRegister(pAd, RF_R59, 0x06); // pa2_cc_ofdm and pa1_cc_ofdm
					}
					else if (Channel == 12)
					{
						RT30xxWriteRFRegister(pAd, RF_R59, 0x05); // pa2_cc_ofdm and pa1_cc_ofdm
					}
					else if ((Channel >= 13) && (Channel <= 14))
					{
						RT30xxWriteRFRegister(pAd, RF_R59, 0x04); // pa2_cc_ofdm and pa1_cc_ofdm
					}

				}

				/* For RT5370F support */
/*				else if (IS_RT5390F(pAd)) */
				else if (IS_RT5390F(pAd) && !IS_MINI_CARD(pAd))
				{
					if ((Channel >= 1) && (Channel <= 11))
					{
						RT30xxWriteRFRegister(pAd, RF_R55, 0x43); /* txvga_cc and pa1_cc_cck */
					}
					else if ((Channel >= 12) && (Channel <= 14))
					{
						RT30xxWriteRFRegister(pAd, RF_R55, 0x23); /* txvga_cc and pa1_cc_cck */
					}

					if ((Channel >= 1) && (Channel <= 11))
					{
						RT30xxWriteRFRegister(pAd, RF_R59, 0x0F); /* pa2_cc_ofdm and pa1_cc_ofdm */
					}
					else if (Channel == 12)
					{
						RT30xxWriteRFRegister(pAd, RF_R59, 0x0D); /* pa2_cc_ofdm and pa1_cc_ofdm */
					}
					else if ((Channel >= 13) && (Channel <= 14))
					{
						RT30xxWriteRFRegister(pAd, RF_R59, 0x0B); /* pa2_cc_ofdm and pa1_cc_ofdms */
					}
				}
				else
#endif /* RT5370 */
				RFValue=0;
				RT30xxReadRFRegister(pAd, RF_R30, (PUCHAR)&RFValue);
				RFValue = ((RFValue & ~0x06) | (TxRxh20M << 1) | (TxRxh20M << 2)); /* tx_h20M and rx_h20M */
				RT30xxWriteRFRegister(pAd, RF_R30, (UCHAR)RFValue);

				RT30xxReadRFRegister(pAd, RF_R30, (PUCHAR)&RFValue);
				RFValue = ((RFValue & ~0x18) | 0x10); /* rxvcm (Rx BB filter VCM) */
				RT30xxWriteRFRegister(pAd, RF_R30, (UCHAR)RFValue);

				RT30xxReadRFRegister(pAd, RF_R03, (PUCHAR)&RFValue);
				RFValue = ((RFValue & ~0x80) | 0x80); /* vcocal_en (initiate VCO calibration (reset after completion)) - It should be at the end of RF configuration. */
				RT30xxWriteRFRegister(pAd, RF_R03, (UCHAR)RFValue);					
				pAd->LatchRfRegs.Channel = Channel; /* Channel latch */

				DBGPRINT(RT_DEBUG_TRACE, ("%s: 5390: SwitchChannel#%d(RF=%d, Pwr0=%d, Pwr1=%d, %dT), N=0x%02X, K=0x%02X, R=0x%02X\n",
					__FUNCTION__, 
					Channel, 
					pAd->RfIcType, 
					TxPwer, 
					TxPwer2, 
					pAd->Antenna.field.TxPath, 
					FreqItems3020[index].N, 
					FreqItems3020[index].K, 
					FreqItems3020[index].R));

				break;
			}
		}
	}
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */

	/* Change BBP setting during siwtch from a->g, g->a */
	if (Channel <= 14)
	{
		ULONG	TxPinCfg = 0x00050F0A;/* Gary 2007/08/09 0x050A0A */


		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R62, (0x37 - GET_LNA_GAIN(pAd)));
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R63, (0x37 - GET_LNA_GAIN(pAd)));
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R64, (0x37 - GET_LNA_GAIN(pAd)));
/*		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R86, 0);//(0x44 - GET_LNA_GAIN(pAd)));	// According the Rory's suggestion to solve the middle range issue. */

		/* Rx High power VGA offset for LNA select */
//#ifdef RT3593
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
		if (IS_RT5390(pAd))
		{
			//2 TODO: Check with Julian
/*			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R82, 0x62); */

			if (pAd->NicConfig2.field.ExternalLNAForG)
			{
#ifdef RT5370
				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R75, 0x52);
#else
				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R75, 0x46);
#endif /* RT5370 */
			}
			else
			{
				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R75, 0x50);
			}
		}
		else
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */
		{
			if (pAd->NicConfig2.field.ExternalLNAForG)
			{
				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R82, 0x62);
				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R75, 0x46);
			}
			else
			{
				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R82, 0x84);
				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R75, 0x50);
			}
		}

		/* 5G band selection PIN, bit1 and bit2 are complement */
		RTMP_IO_READ32(pAd, TX_BAND_CFG, &Value);
		Value &= (~0x6);
		Value |= (0x04);
		RTMP_IO_WRITE32(pAd, TX_BAND_CFG, Value);

			
		/* Turn off unused PA or LNA when only 1T or 1R */
		if (pAd->Antenna.field.TxPath == 1)
		{
			TxPinCfg &= 0xFFFFFFF3;
		}
		if (pAd->Antenna.field.RxPath == 1)
		{
			TxPinCfg &= 0xFFFFF3FF;
		}
			
		RTMP_IO_WRITE32(pAd, TX_PIN_CFG, TxPinCfg);

#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
		/* PCIe PHY Transmit attenuation adjustment */
		if (IS_RT3090A(pAd) || IS_RT3390(pAd) || IS_RT3593(pAd) || IS_RT5390(pAd))
		{
			TX_ATTENUATION_CTRL_STRUC TxAttenuationCtrl = { { 0 } };

			RTMP_IO_READ32(pAd, PCIE_PHY_TX_ATTENUATION_CTRL, &TxAttenuationCtrl.word);

			if (Channel == 14) /* Channel #14 */
			{
				TxAttenuationCtrl.field.PCIE_PHY_TX_ATTEN_EN = 1; /* Enable PCIe PHY Tx attenuation */
				TxAttenuationCtrl.field.PCIE_PHY_TX_ATTEN_VALUE = 4; /* 9/16 full drive level */
			}
			else /* Channel #1~#13 */
			{
				TxAttenuationCtrl.field.PCIE_PHY_TX_ATTEN_EN = 0; /* Disable PCIe PHY Tx attenuation */
				TxAttenuationCtrl.field.PCIE_PHY_TX_ATTEN_VALUE = 0; /* n/a */
			}

			RTMP_IO_WRITE32(pAd, PCIE_PHY_TX_ATTENUATION_CTRL, TxAttenuationCtrl.word);
		}
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */
	}
	else
	{
		ULONG	TxPinCfg = 0x00050F05;/* Gary 2007/8/9 0x050505 */
		UINT8	bbpValue;
		

		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R62, (0x37 - GET_LNA_GAIN(pAd)));
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R63, (0x37 - GET_LNA_GAIN(pAd)));
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R64, (0x37 - GET_LNA_GAIN(pAd)));
/*			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R86, 0);//(0x44 - GET_LNA_GAIN(pAd)));   // According the Rory's suggestion to solve the middle range issue.     */

		/* Set the BBP_R82 value here */
		bbpValue = 0xF2;
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R82, bbpValue);


		/* Rx High power VGA offset for LNA select */
		if (pAd->NicConfig2.field.ExternalLNAForA)
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R75, 0x46);
		}
		else
		{
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R75, 0x50);
		}

		// 5G band selection PIN, bit1 and bit2 are complement */
		RTMP_IO_READ32(pAd, TX_BAND_CFG, &Value);
		Value &= (~0x6);
		Value |= (0x02);
		RTMP_IO_WRITE32(pAd, TX_BAND_CFG, Value);

		/* Turn off unused PA or LNA when only 1T or 1R */

			if (pAd->Antenna.field.TxPath == 1)
			{
				TxPinCfg &= 0xFFFFFFF3;
			}
			if (pAd->Antenna.field.RxPath == 1)
			{
				TxPinCfg &= 0xFFFFF3FF;
			}
		

		RTMP_IO_WRITE32(pAd, TX_PIN_CFG, TxPinCfg);

	}

	
	/* GPIO control */
	

	/* R66 should be set according to Channel and use 20MHz when scanning */
	/* RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, (0x2E + GET_LNA_GAIN(pAd))); */
	if (bScan)
		RTMPSetAGCInitValue(pAd, BW_20);
	else
		RTMPSetAGCInitValue(pAd, pAd->CommonCfg.BBPCurrentBW);

	/*
	  On 11A, We should delay and wait RF/BBP to be stable
	  and the appropriate time should be 1000 micro seconds 
	  2005/06/05 - On 11G, We also need this delay time. Otherwise it's difficult to pass the WHQL.
	*/
	RTMPusecDelay(1000);  
}




/*
	========================================================================
	
	Routine Description: 5392 R66 writing must select BBP_R27

	Arguments:

	Return Value:

	IRQL = 
	
	Note: This function copy from RT3572WriteBBPR66. The content almost the same.
	
	========================================================================
*/
NTSTATUS	RT5392WriteBBPR66(
	IN	PRTMP_ADAPTER	pAd,
	IN	UCHAR			Value)
{
	NTSTATUS NStatus = STATUS_UNSUCCESSFUL;
	UCHAR	bbpData = 0;

	if (!IS_RT5392(pAd))
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s: Incorrect MAC version, pAd->MACVersion = 0x%X\n", 
			__FUNCTION__, 
			pAd->MACVersion));
		return NStatus;
	}
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R27, &bbpData);

	/* R66 controls the gain of Rx0 */
	bbpData &= ~(0x60);	/* clear bit 5,6 */
#ifdef RTMP_MAC_USB
	if (RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R27, bbpData) == STATUS_SUCCESS)
#endif /* RTMP_MAC_USB */
	{
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, Value);
	}

	/* R66 controls the gain of Rx1 */
	bbpData |= 0x20;		/* set bit 5 */
#ifdef RTMP_MAC_USB
	if (RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R27, bbpData) == STATUS_SUCCESS)
#endif /* RTMP_MAC_USB */
	{
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, Value);
		NStatus = STATUS_SUCCESS;
	}
	return NStatus;
}




#ifdef RTMP_INTERNAL_TX_ALC
#if defined(RT5370) || defined(RT5390)
VOID RT5390_InitDesiredTSSITable(
	IN PRTMP_ADAPTER			pAd)
{
	UCHAR TSSIBase = 0; /* The TSSI over OFDM 54Mbps */
	UCHAR RFValue = 0;
	USHORT TxPower = 0, TxPowerOFDM54 = 0;

	UCHAR index = 0;
	CHAR DesiredTssi = 0;
	USHORT Value = 0;
	UCHAR BbpR47 = 0;
	INT		i=0;
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)

	BOOLEAN bExtendedTssiMode = FALSE;
	EEPROM_TX_PWR_OFFSET_STRUC TxPwrOffset = {{0}};

	UCHAR ch = 0;
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */

	if (pAd->TxPowerCtrl.bInternalTxALC == FALSE)
	{
		return;
	}

	DBGPRINT(RT_DEBUG_TRACE, ("---> %s\n", __FUNCTION__));

#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
	if (IS_RT5390(pAd))
	{
		RT28xx_EEPROM_READ16(pAd, EEPROM_TSSI_OVER_OFDM_54, Value);
#ifdef RT5370
		TSSIBase = (Value & 0x007F); /* range: bit6~bit0 */
#endif /* RT5370 */

		RT28xx_EEPROM_READ16(pAd, (EEPROM_TSSI_STEP_OVER_2DOT4G - 1), Value);
		if (((Value >> 8) & 0x80) == 0x80) /* Enable the extended TSSI mode */
		{
			bExtendedTssiMode = TRUE;
		}
		else
		{
			bExtendedTssiMode = FALSE;
		}

		if (bExtendedTssiMode == TRUE) /* Tx power offset for the extended TSSI mode */
		{
			pAd->TxPowerCtrl.bExtendedTssiMode = TRUE;

			
			/* Get the per-channel Tx power offset */
			
			RT28xx_EEPROM_READ16(pAd, (EEPROM_TX_POWER_OFFSET_OVER_CH_1 - 1), TxPwrOffset.word);
			pAd->TxPowerCtrl.PerChTxPwrOffset[1] = (TxPwrOffset.field.Byte1 & 0x0F); /* Tx power offset over channel 1 */
			pAd->TxPowerCtrl.PerChTxPwrOffset[2] = (((TxPwrOffset.field.Byte1 & 0xF0) >> 4) & 0x0F); /* Tx power offset over channel 2 */

			RT28xx_EEPROM_READ16(pAd, EEPROM_TX_POWER_OFFSET_OVER_CH_3, TxPwrOffset.word);
			pAd->TxPowerCtrl.PerChTxPwrOffset[3] = (TxPwrOffset.field.Byte0 & 0x0F); /* Tx power offset over channel 3 */
			pAd->TxPowerCtrl.PerChTxPwrOffset[4] = (((TxPwrOffset.field.Byte0 & 0xF0) >> 4) & 0x0F); /* Tx power offset over channel 4 */
			pAd->TxPowerCtrl.PerChTxPwrOffset[5] = (TxPwrOffset.field.Byte1 & 0x0F); /* Tx power offset over channel 5 */
			pAd->TxPowerCtrl.PerChTxPwrOffset[6] = (((TxPwrOffset.field.Byte1 & 0xF0) >> 4) & 0x0F); /* Tx power offset over channel 6 */

			RT28xx_EEPROM_READ16(pAd, EEPROM_TX_POWER_OFFSET_OVER_CH_7, TxPwrOffset.word);
			pAd->TxPowerCtrl.PerChTxPwrOffset[7] = (TxPwrOffset.field.Byte0 & 0x0F); /* Tx power offset over channel 7 */
			pAd->TxPowerCtrl.PerChTxPwrOffset[8] = (((TxPwrOffset.field.Byte0 & 0xF0) >> 4) & 0x0F); /* Tx power offset over channel 8 */
			pAd->TxPowerCtrl.PerChTxPwrOffset[9] = (TxPwrOffset.field.Byte1 & 0x0F); /* Tx power offset over channel 9 */
			pAd->TxPowerCtrl.PerChTxPwrOffset[10] = (((TxPwrOffset.field.Byte1 & 0xF0) >> 4) & 0x0F); /* Tx power offset over channel 10 */

			RT28xx_EEPROM_READ16(pAd, EEPROM_TX_POWER_OFFSET_OVER_CH_11, TxPwrOffset.word);
			pAd->TxPowerCtrl.PerChTxPwrOffset[11] = (TxPwrOffset.field.Byte0 & 0x0F); /* Tx power offset over channel 11 */
			pAd->TxPowerCtrl.PerChTxPwrOffset[12] = (((TxPwrOffset.field.Byte0 & 0xF0) >> 4) & 0x0F); /* Tx power offset over channel 12 */
			pAd->TxPowerCtrl.PerChTxPwrOffset[13] = (TxPwrOffset.field.Byte1 & 0x0F); /* Tx power offset over channel 13 */
			pAd->TxPowerCtrl.PerChTxPwrOffset[14] = (((TxPwrOffset.field.Byte1 & 0xF0) >> 4) & 0x0F); /* Tx power offset over channel 14 */

			
			/* 4-bit representation ==> 8-bit representation (2's complement) */
			
			for (i = 1; i <= NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET; i++)
			{
				if ((pAd->TxPowerCtrl.PerChTxPwrOffset[i] & 0x08) == 0x00) /* Positive number */
				{
					pAd->TxPowerCtrl.PerChTxPwrOffset[i] = (pAd->TxPowerCtrl.PerChTxPwrOffset[i] & ~0xF8);
				}
				else /* 0x08: Negative number */
				{
					pAd->TxPowerCtrl.PerChTxPwrOffset[i] = (pAd->TxPowerCtrl.PerChTxPwrOffset[i] | 0xF0);
				}
			}

			DBGPRINT(RT_DEBUG_TRACE, ("%s: TxPwrOffset[1] = %d, TxPwrOffset[2] = %d, TxPwrOffset[3] = %d, TxPwrOffset[4] = %d\n", 
				__FUNCTION__, 
				pAd->TxPowerCtrl.PerChTxPwrOffset[1], 
				pAd->TxPowerCtrl.PerChTxPwrOffset[2], 
				pAd->TxPowerCtrl.PerChTxPwrOffset[3], 
				pAd->TxPowerCtrl.PerChTxPwrOffset[4]));
			DBGPRINT(RT_DEBUG_TRACE, ("%s: TxPwrOffset[5] = %d, TxPwrOffset[6] = %d, TxPwrOffset[7] = %d, TxPwrOffset[8] = %d\n", 
				__FUNCTION__, 
				pAd->TxPowerCtrl.PerChTxPwrOffset[5], 
				pAd->TxPowerCtrl.PerChTxPwrOffset[6], 
				pAd->TxPowerCtrl.PerChTxPwrOffset[7], 
				pAd->TxPowerCtrl.PerChTxPwrOffset[8]));
			DBGPRINT(RT_DEBUG_TRACE, ("%s: TxPwrOffset[9] = %d, TxPwrOffset[10] = %d, TxPwrOffset[11] = %d, TxPwrOffset[12] = %d\n", 
				__FUNCTION__, 
				pAd->TxPowerCtrl.PerChTxPwrOffset[9], 
				pAd->TxPowerCtrl.PerChTxPwrOffset[10], 
				pAd->TxPowerCtrl.PerChTxPwrOffset[11], 
				pAd->TxPowerCtrl.PerChTxPwrOffset[12]));
			DBGPRINT(RT_DEBUG_TRACE, ("%s: TxPwrOffset[13] = %d, TxPwrOffset[14] = %d\n", 
				__FUNCTION__, 
				pAd->TxPowerCtrl.PerChTxPwrOffset[13], 
				pAd->TxPowerCtrl.PerChTxPwrOffset[14]));
		}
		else
		{
			pAd->TxPowerCtrl.bExtendedTssiMode = FALSE;
			RTMPZeroMemory(pAd->TxPowerCtrl.PerChTxPwrOffset, sizeof (pAd->TxPowerCtrl.PerChTxPwrOffset));
		}

		
		RT28xx_EEPROM_READ16(pAd, (EEPROM_OFDM_MCS6_MCS7 - 1), Value);
		TxPowerOFDM54 = (0x000F & (Value >> 8));

		DBGPRINT(RT_DEBUG_TRACE, ("%s: TSSIBase = 0x%X, TxPowerOFDM54 = 0x%X\n", 
			__FUNCTION__, 
			TSSIBase, 
			TxPowerOFDM54));

		/* The desired TSSI over CCK */
		
		RT28xx_EEPROM_READ16(pAd, EEPROM_CCK_MCS0_MCS1, Value);
		TxPower = (Value & 0x000F);
		DBGPRINT(RT_DEBUG_TRACE, ("%s: eFuse 0xDE = 0x%X\n", __FUNCTION__, TxPower));
		for (ch = 1; ch <= NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET; ch++)
		{
			index = GET_TSSI_RATE_TABLE_INDEX(TxPower - TxPowerOFDM54 + 3 + pAd->TxPowerCtrl.PerChTxPwrOffset[ch] + TSSI_RATIO_TABLE_OFFSET);
			DesiredTssi = (SHORT)Rounding(pAd, (TSSIBase * TssiRatioTable[index][0] / TssiRatioTable[index][1]), (TSSIBase * TssiRatioTable[index][0] % TssiRatioTable[index][1]), TssiRatioTable[index][1]);
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, index = %d, DesiredTssi = 0x%02X\n", __FUNCTION__, ch, index, DesiredTssi));

			
			/* Boundary verification: the desired TSSI value */
			
			if (DesiredTssi < 0x00)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));

				DesiredTssi = 0x00;
			}
			else if (DesiredTssi > 0x7C)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));

				DesiredTssi = 0x7C;
			}

			RT5390_desiredTSSIOverCCKExt[ch][MCS_0] = (CHAR)DesiredTssi;
			RT5390_desiredTSSIOverCCKExt[ch][MCS_1] = (CHAR)DesiredTssi;
		}
		RT28xx_EEPROM_READ16(pAd, (EEPROM_CCK_MCS2_MCS3 - 1), Value);
		TxPower = ((Value >> 8) & 0x000F);
		DBGPRINT(RT_DEBUG_TRACE, ("%s: eFuse 0xDF = 0x%X\n", __FUNCTION__, TxPower));

		for (ch = 1; ch <= NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET; ch++)
		{
			index = GET_TSSI_RATE_TABLE_INDEX(TxPower - TxPowerOFDM54 + 3 + pAd->TxPowerCtrl.PerChTxPwrOffset[ch] + TSSI_RATIO_TABLE_OFFSET);
			DesiredTssi = (SHORT)Rounding(pAd, (TSSIBase * TssiRatioTable[index][0] / TssiRatioTable[index][1]), (TSSIBase * TssiRatioTable[index][0] % TssiRatioTable[index][1]), TssiRatioTable[index][1]);
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, index = %d, DesiredTssi = 0x%02X\n", __FUNCTION__, ch, index, DesiredTssi));

			
			/* Boundary verification: the desired TSSI value */
			
			if (DesiredTssi < 0x00)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));
				
				DesiredTssi = 0x00;
			}
			else if (DesiredTssi > 0x7C)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));
				DesiredTssi = 0x7C;

				
			}

			RT5390_desiredTSSIOverCCKExt[ch][MCS_2] = (CHAR)DesiredTssi;
			RT5390_desiredTSSIOverCCKExt[ch][MCS_3] = (CHAR)DesiredTssi;

		}

		for (ch = 1; ch <= NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET; ch++)
		{
			index = GET_TSSI_RATE_TABLE_INDEX(TxPower - TxPowerOFDM54 + 3 + pAd->TxPowerCtrl.PerChTxPwrOffset[ch] + TSSI_RATIO_TABLE_OFFSET);
			DesiredTssi = (SHORT)Rounding(pAd, (TSSIBase * TssiRatioTable[index][0] / TssiRatioTable[index][1]), (TSSIBase * TssiRatioTable[index][0] % TssiRatioTable[index][1]), TssiRatioTable[index][1]);
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, index = %d, DesiredTssi = 0x%02X\n", __FUNCTION__, ch, index, DesiredTssi));

			
			/* Boundary verification: the desired TSSI value */
			
			if (DesiredTssi < 0x00)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));
				
				DesiredTssi = 0x00;
			}
			else if (DesiredTssi > 0x7C)			
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));
				DesiredTssi = 0x7C;

			}

			RT5390_desiredTSSIOverCCKExt[ch][MCS_2] = (CHAR)DesiredTssi;
			RT5390_desiredTSSIOverCCKExt[ch][MCS_3] = (CHAR)DesiredTssi;
		}

		for (ch = 1; ch <= NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET; ch++)
		{
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, RT5390_desiredTSSIOverCCK[%d][0] = %d, RT5390_desiredTSSIOverCCK[%d][1] = %d, RT5390_desiredTSSIOverCCK[%d][2] = %d, RT5390_desiredTSSIOverCCK[%d][3] = %d\n", 
				__FUNCTION__, 
				ch, 
				ch, 
				RT5390_desiredTSSIOverCCKExt[ch][0], 
				ch, 
				RT5390_desiredTSSIOverCCKExt[ch][1], 
				ch, 
				RT5390_desiredTSSIOverCCKExt[ch][2], 
				ch, 
				RT5390_desiredTSSIOverCCKExt[ch][3]));
		}

		
		/* The desired TSSI over OFDM */
		
		RT28xx_EEPROM_READ16(pAd, EEPROM_OFDM_MCS0_MCS1, Value);
		TxPower = (Value & 0x000F);
		DBGPRINT(RT_DEBUG_TRACE, ("%s: eFuse 0xE0 = 0x%X\n", __FUNCTION__, TxPower));

		for (ch = 1; ch <= NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET; ch++)
		{
			index = GET_TSSI_RATE_TABLE_INDEX(TxPower - TxPowerOFDM54 + pAd->TxPowerCtrl.PerChTxPwrOffset[ch] + TSSI_RATIO_TABLE_OFFSET);
			DesiredTssi = (SHORT)Rounding(pAd, (TSSIBase * TssiRatioTable[index][0] / TssiRatioTable[index][1]), (TSSIBase * TssiRatioTable[index][0] % TssiRatioTable[index][1]), TssiRatioTable[index][1]);
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, index = %d, DesiredTssi = 0x%02X\n", __FUNCTION__, ch, index, DesiredTssi));

			
			/* Boundary verification: the desired TSSI value */
			
			if (DesiredTssi < 0x00)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));
				
				DesiredTssi = 0x00;
			}
			else if (DesiredTssi > 0x7C)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));

				DesiredTssi = 0x7C;
			}

			RT5390_desiredTSSIOverOFDMExt[ch][MCS_0] = (CHAR)DesiredTssi;
			RT5390_desiredTSSIOverOFDMExt[ch][MCS_1] = (CHAR)DesiredTssi;
		}

		RT28xx_EEPROM_READ16(pAd, (EEPROM_OFDM_MCS2_MCS3 - 1), Value);
		TxPower = ((Value >> 8) & 0x000F);
		DBGPRINT(RT_DEBUG_TRACE, ("%s: eFuse 0xE1 = 0x%X\n", __FUNCTION__, TxPower));

		for (ch = 1; ch <= NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET; ch++)
		{
			index = GET_TSSI_RATE_TABLE_INDEX(TxPower - TxPowerOFDM54 + pAd->TxPowerCtrl.PerChTxPwrOffset[ch] + TSSI_RATIO_TABLE_OFFSET);
			DesiredTssi = (SHORT)Rounding(pAd, (TSSIBase * TssiRatioTable[index][0] / TssiRatioTable[index][1]), (TSSIBase * TssiRatioTable[index][0] % TssiRatioTable[index][1]), TssiRatioTable[index][1]);
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, index = %d, DesiredTssi = 0x%02X\n", __FUNCTION__, ch, index, DesiredTssi));

			
			/* Boundary verification: the desired TSSI value */
			
			if (DesiredTssi < 0x00)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));
				
				DesiredTssi = 0x00;
			}
			else if (DesiredTssi > 0x7C)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));

				DesiredTssi = 0x7C;
			}

			RT5390_desiredTSSIOverOFDMExt[ch][MCS_2] = (CHAR)DesiredTssi;
			RT5390_desiredTSSIOverOFDMExt[ch][MCS_3] = (CHAR)DesiredTssi;
		}
		
		RT28xx_EEPROM_READ16(pAd, EEPROM_OFDM_MCS4_MCS5, Value);
		TxPower = (Value & 0x000F);
		DBGPRINT(RT_DEBUG_TRACE, ("%s: eFuse 0xE2 = 0x%X\n", __FUNCTION__, TxPower));

		for (ch = 1; ch <= NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET; ch++)
		{
			index = GET_TSSI_RATE_TABLE_INDEX(TxPower - TxPowerOFDM54 + pAd->TxPowerCtrl.PerChTxPwrOffset[ch] + TSSI_RATIO_TABLE_OFFSET);
			DesiredTssi = (SHORT)Rounding(pAd, (TSSIBase * TssiRatioTable[index][0] / TssiRatioTable[index][1]), (TSSIBase * TssiRatioTable[index][0] % TssiRatioTable[index][1]), TssiRatioTable[index][1]);
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, index = %d, DesiredTssi = 0x%02X\n", __FUNCTION__, ch, index, DesiredTssi));

			
			/* Boundary verification: the desired TSSI value */
			
			if (DesiredTssi < 0x00)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));
				
				DesiredTssi = 0x00;
			}
			else if (DesiredTssi > 0x7C)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));

				DesiredTssi = 0x7C;
			}

			RT5390_desiredTSSIOverOFDMExt[ch][MCS_4] = (CHAR)DesiredTssi;
			RT5390_desiredTSSIOverOFDMExt[ch][MCS_5] = (CHAR)DesiredTssi;

			index = GET_TSSI_RATE_TABLE_INDEX(pAd->TxPowerCtrl.PerChTxPwrOffset[ch] + TSSI_RATIO_TABLE_OFFSET);
			DesiredTssi = (SHORT)Rounding(pAd, (TSSIBase * TssiRatioTable[index][0] / TssiRatioTable[index][1]), (TSSIBase * TssiRatioTable[index][0] % TssiRatioTable[index][1]), TssiRatioTable[index][1]);
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, index = %d, DesiredTssi = 0x%02X\n", __FUNCTION__, ch, index, DesiredTssi));

			
			/* Boundary verification: the desired TSSI value */
			
			if (DesiredTssi < 0x00)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));
				
				DesiredTssi = 0x00;
			}
			else if (DesiredTssi > 0x7C)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));

				DesiredTssi = 0x7C;
			}

			RT5390_desiredTSSIOverOFDMExt[ch][MCS_6] = (CHAR)DesiredTssi;
			RT5390_desiredTSSIOverOFDMExt[ch][MCS_7] =(CHAR) DesiredTssi;
		}
		
		for (ch = 1; ch <= NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET; ch++)
		{
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, desiredTSSIOverOFDM[%d][0] = %d, desiredTSSIOverOFDM[%d][1] = %d, desiredTSSIOverOFDM[%d][2] = %d, desiredTSSIOverOFDM[%d][3] = %d\n", 
				__FUNCTION__, 
				ch, 
				ch, 
				RT5390_desiredTSSIOverOFDMExt[ch][0], 
				ch, 
				RT5390_desiredTSSIOverOFDMExt[ch][1], 
				ch, 
				RT5390_desiredTSSIOverOFDMExt[ch][2], 
				ch, 
				RT5390_desiredTSSIOverOFDMExt[ch][3]));
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, desiredTSSIOverOFDM[%d][4] = %d, desiredTSSIOverOFDM[%d][5] = %d, desiredTSSIOverOFDM[[%d]6] = %d, desiredTSSIOverOFDM[%d][7] = %d\n", 
				__FUNCTION__, 
				ch, 
				ch, 
				RT5390_desiredTSSIOverOFDMExt[ch][4], 
				ch, 
				RT5390_desiredTSSIOverOFDMExt[ch][5], 
				ch, 
				RT5390_desiredTSSIOverOFDMExt[ch][6], 
				ch, 
				RT5390_desiredTSSIOverOFDMExt[ch][7]));
		}

		
		/* The desired TSSI over HT */
		
		RT28xx_EEPROM_READ16(pAd, EEPROM_HT_MCS0_MCS1, Value);
		TxPower = (Value & 0x000F);
		DBGPRINT(RT_DEBUG_TRACE, ("%s: eFuse 0xE4 = 0x%X\n", __FUNCTION__, TxPower));

		for (ch = 1; ch <= NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET; ch++)
		{
			index = GET_TSSI_RATE_TABLE_INDEX(TxPower - TxPowerOFDM54 + pAd->TxPowerCtrl.PerChTxPwrOffset[ch] + TSSI_RATIO_TABLE_OFFSET);
			DesiredTssi = (SHORT)Rounding(pAd, (TSSIBase * TssiRatioTable[index][0] / TssiRatioTable[index][1]), (TSSIBase * TssiRatioTable[index][0] % TssiRatioTable[index][1]), TssiRatioTable[index][1]);
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, index = %d, DesiredTssi = 0x%02X\n", __FUNCTION__, ch, index, DesiredTssi));

			
			/* Boundary verification: the desired TSSI value */
		
			if (DesiredTssi < 0x00)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));
				
				DesiredTssi = 0x00;
			}
			else if (DesiredTssi > 0x7F)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));

				DesiredTssi = 0x7F;
			}

			RT5390_desiredTSSIOverHTExt[ch][MCS_0] = (CHAR)DesiredTssi;
			RT5390_desiredTSSIOverHTExt[ch][MCS_1] = (CHAR)DesiredTssi;
		}
		
		RT28xx_EEPROM_READ16(pAd, (EEPROM_HT_MCS2_MCS3 - 1), Value);
		TxPower = ((Value >> 8) & 0x000F);

		for (ch = 1; ch <= NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET; ch++)
		{
			index = GET_TSSI_RATE_TABLE_INDEX(TxPower - TxPowerOFDM54 + pAd->TxPowerCtrl.PerChTxPwrOffset[ch] + TSSI_RATIO_TABLE_OFFSET);
			DesiredTssi = (SHORT)Rounding(pAd, (TSSIBase * TssiRatioTable[index][0] / TssiRatioTable[index][1]), (TSSIBase * TssiRatioTable[index][0] % TssiRatioTable[index][1]), TssiRatioTable[index][1]);
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, index = %d, DesiredTssi = 0x%02X\n", __FUNCTION__, ch, index, DesiredTssi));

			
			/* Boundary verification: the desired TSSI value */
			
			if (DesiredTssi < 0x00)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));
				
				DesiredTssi = 0x00;
			}
			else if (DesiredTssi > 0x7F)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));

				DesiredTssi = 0x7F;
			}

			RT5390_desiredTSSIOverHTExt[ch][MCS_2] = (CHAR)DesiredTssi;
			RT5390_desiredTSSIOverHTExt[ch][MCS_3] = (CHAR)DesiredTssi;
		}

		RT28xx_EEPROM_READ16(pAd, EEPROM_HT_MCS4_MCS5, Value);
		TxPower = (Value & 0x000F);

		for (ch = 1; ch <= NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET; ch++)
		{
			index = GET_TSSI_RATE_TABLE_INDEX(TxPower - TxPowerOFDM54 + pAd->TxPowerCtrl.PerChTxPwrOffset[ch] + TSSI_RATIO_TABLE_OFFSET);
			DesiredTssi = (SHORT)Rounding(pAd, (TSSIBase * TssiRatioTable[index][0] / TssiRatioTable[index][1]), (TSSIBase * TssiRatioTable[index][0] % TssiRatioTable[index][1]), TssiRatioTable[index][1]);
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, index = %d, DesiredTssi = 0x%02X\n", __FUNCTION__, ch, index, DesiredTssi));

			
			/* Boundary verification: the desired TSSI value */
			
			if (DesiredTssi < 0x00)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));
				
				DesiredTssi = 0x00;
			}
			else if (DesiredTssi > 0x7F)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));

				DesiredTssi = 0x7F;
			}

			RT5390_desiredTSSIOverHTExt[ch][MCS_4] = (CHAR)DesiredTssi;
			RT5390_desiredTSSIOverHTExt[ch][MCS_5] = (CHAR)DesiredTssi;
		}

		RT28xx_EEPROM_READ16(pAd, (EEPROM_HT_MCS6_MCS7 - 1), Value);
		TxPower = ((Value >> 8) & 0x000F);

		for (ch = 1; ch <= NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET; ch++)
		{
			index = GET_TSSI_RATE_TABLE_INDEX(TxPower - TxPowerOFDM54 + pAd->TxPowerCtrl.PerChTxPwrOffset[ch] + TSSI_RATIO_TABLE_OFFSET);
			DesiredTssi = (SHORT)Rounding(pAd, (TSSIBase * TssiRatioTable[index][0] / TssiRatioTable[index][1]), (TSSIBase * TssiRatioTable[index][0] % TssiRatioTable[index][1]), TssiRatioTable[index][1]);
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, index = %d, DesiredTssi = 0x%02X\n", __FUNCTION__, ch, index, DesiredTssi));

			
			/* Boundary verification: the desired TSSI value */
			
			if (DesiredTssi < 0x00)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));
				
				DesiredTssi = 0x00;
			}
			else if (DesiredTssi > 0x7F)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));

				DesiredTssi = 0x7F;
			}

			RT5390_desiredTSSIOverHTExt[ch][MCS_6] = (CHAR)DesiredTssi;
			RT5390_desiredTSSIOverHTExt[ch][MCS_7] = (CHAR)DesiredTssi;
		}
		

		for (ch = 1; ch <= NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET; ch++)
		{
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, desiredTSSIOverHT[%d][0] = %d, desiredTSSIOverHT[%d][1] = %d, desiredTSSIOverHT[%d][2] = %d, desiredTSSIOverHT[%d][3] = %d\n", 
				__FUNCTION__, 
				ch, 
				ch, 
				RT5390_desiredTSSIOverHTExt[ch][0], 
				ch, 
				RT5390_desiredTSSIOverHTExt[ch][1], 
				ch, 
				RT5390_desiredTSSIOverHTExt[ch][2], 
				ch, 
				RT5390_desiredTSSIOverHTExt[ch][3]));
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, desiredTSSIOverHT[%d][4] = %d, desiredTSSIOverHT[%d][5] = %d, desiredTSSIOverHT[%d][6] = %d, desiredTSSIOverHT[%d][7] = %d\n", 
				__FUNCTION__, 
				ch, 
				ch, 
				RT5390_desiredTSSIOverHTExt[ch][4], 
				ch, 
				RT5390_desiredTSSIOverHTExt[ch][5], 
				ch, 
				RT5390_desiredTSSIOverHTExt[ch][6], 
				ch, 
				RT5390_desiredTSSIOverHTExt[ch][7]));
		}

		
		/* The desired TSSI over HT using STBC */
		
		RT28xx_EEPROM_READ16(pAd, EEPROM_HT_USING_STBC_MCS0_MCS1, Value);
		TxPower = (Value & 0x000F);

			for (ch = 1; ch <= NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET; ch++)
		{
			index = GET_TSSI_RATE_TABLE_INDEX(TxPower - TxPowerOFDM54 + pAd->TxPowerCtrl.PerChTxPwrOffset[ch] + TSSI_RATIO_TABLE_OFFSET);
			DesiredTssi = (SHORT)Rounding(pAd, (TSSIBase * TssiRatioTable[index][0] / TssiRatioTable[index][1]), (TSSIBase * TssiRatioTable[index][0] % TssiRatioTable[index][1]), TssiRatioTable[index][1]);
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, index = %d, DesiredTssi = 0x%02X\n", __FUNCTION__, ch, index, DesiredTssi));

			
			/* Boundary verification: the desired TSSI value */
			
			if (DesiredTssi < 0x00)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));
				
				DesiredTssi = 0x00;
			}
			else if (DesiredTssi > 0x7F)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));

				DesiredTssi = 0x7F;
			}

			RT5390_desiredTSSIOverHTUsingSTBCExt[ch][MCS_0] = (CHAR)DesiredTssi;
			RT5390_desiredTSSIOverHTUsingSTBCExt[ch][MCS_1] = (CHAR)DesiredTssi;
		}

		RT28xx_EEPROM_READ16(pAd, (EEPROM_HT_USING_STBC_MCS2_MCS3 - 1), Value);
		TxPower = ((Value >> 8) & 0x000F);

		for (ch = 1; ch <= NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET; ch++)
		{
			index = GET_TSSI_RATE_TABLE_INDEX(TxPower - TxPowerOFDM54 + pAd->TxPowerCtrl.PerChTxPwrOffset[ch] + TSSI_RATIO_TABLE_OFFSET);
			DesiredTssi = (SHORT)Rounding(pAd, (TSSIBase * TssiRatioTable[index][0] / TssiRatioTable[index][1]), (TSSIBase * TssiRatioTable[index][0] % TssiRatioTable[index][1]), TssiRatioTable[index][1]);
			DBGPRINT(RT_DEBUG_TRACE, ("%s:ch = %d, index = %d, DesiredTssi = 0x%02X\n", __FUNCTION__, ch, index, DesiredTssi));

			
			/* Boundary verification: the desired TSSI value */
			
			if (DesiredTssi < 0x00)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));
				
				DesiredTssi = 0x00;
			}
			else if (DesiredTssi > 0x7F)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));

				DesiredTssi = 0x7F;
			}

			RT5390_desiredTSSIOverHTUsingSTBCExt[ch][MCS_2] = (CHAR)DesiredTssi;
			RT5390_desiredTSSIOverHTUsingSTBCExt[ch][MCS_3] = (CHAR)DesiredTssi;
		}

		RT28xx_EEPROM_READ16(pAd, EEPROM_HT_USING_STBC_MCS4_MCS5, Value);
		TxPower = (Value & 0x000F);

		for (ch = 1; ch <= NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET; ch++)
		{
			index = GET_TSSI_RATE_TABLE_INDEX(TxPower - TxPowerOFDM54 + pAd->TxPowerCtrl.PerChTxPwrOffset[ch] + TSSI_RATIO_TABLE_OFFSET);
			DesiredTssi = (SHORT)Rounding(pAd, (TSSIBase * TssiRatioTable[index][0] / TssiRatioTable[index][1]), (TSSIBase * TssiRatioTable[index][0] % TssiRatioTable[index][1]), TssiRatioTable[index][1]);
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, index = %d, DesiredTssi = 0x%02X\n", __FUNCTION__, ch, index, DesiredTssi));

			
			/* Boundary verification: the desired TSSI value */
			
			if (DesiredTssi < 0x00)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));
				
				DesiredTssi = 0x00;
			}
			else if (DesiredTssi > 0x7F)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));

				DesiredTssi = 0x7F;
			}

			RT5390_desiredTSSIOverHTUsingSTBCExt[ch][MCS_4] = (CHAR)DesiredTssi;
			RT5390_desiredTSSIOverHTUsingSTBCExt[ch][MCS_5] = (CHAR)DesiredTssi;
		}

		RT28xx_EEPROM_READ16(pAd, (EEPROM_HT_USING_STBC_MCS6_MCS7 - 1), Value);
		TxPower = ((Value >> 8) & 0x000F);

		for (ch = 1; ch <= NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET; ch++)
		{
			index = GET_TSSI_RATE_TABLE_INDEX(TxPower - TxPowerOFDM54 + pAd->TxPowerCtrl.PerChTxPwrOffset[ch] + TSSI_RATIO_TABLE_OFFSET);
			DesiredTssi = (SHORT)Rounding(pAd, (TSSIBase * TssiRatioTable[index][0] / TssiRatioTable[index][1]), (TSSIBase * TssiRatioTable[index][0] % TssiRatioTable[index][1]), TssiRatioTable[index][1]);
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, index = %d, DesiredTssi = 0x%02X\n", __FUNCTION__, ch, index, DesiredTssi));

			
			/* Boundary verification: the desired TSSI value */
			
			if (DesiredTssi < 0x00)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));
				
				DesiredTssi = 0x00;
			}
			else if (DesiredTssi > 0x7F)
			{
				DBGPRINT(RT_DEBUG_TRACE, ("%s: Out of range, DesiredTssi = 0x%02X\n", 
					__FUNCTION__, 
					DesiredTssi));

				DesiredTssi = 0x7F;
			}

			RT5390_desiredTSSIOverHTUsingSTBCExt[ch][MCS_6] = (CHAR)DesiredTssi;
			RT5390_desiredTSSIOverHTUsingSTBCExt[ch][MCS_7] = (CHAR)DesiredTssi;
		}

		
		/* Boundary verification: the desired TSSI value */
		
		for (ch = 1; ch <= NUM_OF_CH_FOR_PER_CH_TX_PWR_OFFSET; ch++)
		{
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, desiredTSSIOverHTUsingSTBC[%d][0] = %d, desiredTSSIOverHTUsingSTBC[%d][1] = %d, desiredTSSIOverHTUsingSTBC[%d][2] = %d, desiredTSSIOverHTUsingSTBC[%d][3] = %d\n", 
				__FUNCTION__, 
				ch, 
				ch, 
				RT5390_desiredTSSIOverHTUsingSTBCExt[ch][0], 
				ch, 
				RT5390_desiredTSSIOverHTUsingSTBCExt[ch][1], 
				ch, 
				RT5390_desiredTSSIOverHTUsingSTBCExt[ch][2], 
				ch, 
				RT5390_desiredTSSIOverHTUsingSTBCExt[ch][3]));
			
			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, desiredTSSIOverHTUsingSTBC[%d][4] = %d, desiredTSSIOverHTUsingSTBC[%d][5] = %d, desiredTSSIOverHTUsingSTBC[%d][6] = %d, desiredTSSIOverHTUsingSTBC[%d][7] = %d\n", 
				__FUNCTION__, 
				ch, 
				ch, 
				RT5390_desiredTSSIOverHTUsingSTBCExt[ch][4], 
				ch, 
				RT5390_desiredTSSIOverHTUsingSTBCExt[ch][5], 
				ch, 
				RT5390_desiredTSSIOverHTUsingSTBCExt[ch][6], 
				ch, 
				RT5390_desiredTSSIOverHTUsingSTBCExt[ch][7]));
		}

		
		/* 5390 RF TSSI configuraiton */
		
		RT30xxReadRFRegister(pAd, RF_R28, (PUCHAR)(&RFValue));
		RFValue = 0;
		RT30xxWriteRFRegister(pAd, RF_R28, RFValue);

		RT30xxReadRFRegister(pAd, RF_R29, (PUCHAR)(&RFValue));
		RFValue = ((RFValue & ~0x03) | 0x00);
		RT30xxWriteRFRegister(pAd, RF_R29, RFValue);

		RT30xxReadRFRegister(pAd, RF_R27, (PUCHAR)(&RFValue));
		RFValue = (RFValue & ~0xFC); /* [7:4] = 0, [3:2] = 0 */
		RFValue = (RFValue | 0x03); /* [1:0] = 0x03 (tssi_gain = 12dB) */
		RT30xxWriteRFRegister(pAd, RF_R27, RFValue);
		
/* Zero's Release without the following codes */
		  RT28xx_EEPROM_READ16(pAd, EEPROM_TSSI_GAIN_AND_ATTENUATION,Value);
		Value = (Value & 0x00FF);
		DBGPRINT(RT_DEBUG_TRACE, ("%s: EEPROM_TSSI_GAIN_AND_ATTENUATION = 0x%X\n", 
			__FUNCTION__, 
			Value));

		if ((Value != 0x00) && (Value != 0xFF))
		{
			RT30xxReadRFRegister(pAd, RF_R27, (PUCHAR)(&RFValue));
			Value = (Value & 0x000F);
			RFValue = ((RFValue & 0xF0) | Value); /* [3:0] = (tssi_gain and tssi_atten) */
			RT30xxWriteRFRegister(pAd, RF_R27, RFValue);
		}
		
		/* 5390 BBP TSSI configuration */
		
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R47, &BbpR47);
		BbpR47 = ((BbpR47 & ~0x80) | 0x80); /* ADC6 on */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R47, BbpR47);
		
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R47, &BbpR47);
		BbpR47 = ((BbpR47 & ~0x18) | 0x10); /* TSSI_MODE (new averaged TSSI mode for 3290/5390) */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R47, BbpR47);

		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R47, &BbpR47);
		BbpR47 = ((BbpR47 & ~0x07) | 0x04); /* TSSI_REPORT_SEL (TSSI INFO 0 - TSSI) and enable TSSI INFO udpate */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R47, BbpR47);

		
	}
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */
	
	DBGPRINT(RT_DEBUG_TRACE, ("<--- %s\n", __FUNCTION__));
}

VOID RT5390_AsicTxAlcGetAutoAgcOffset(
	IN PRTMP_ADAPTER			pAd,
	IN PCHAR					pDeltaPwr,
	IN PCHAR					pTotalDeltaPwr,
	IN PCHAR					pAgcCompensate,
	IN PUCHAR					pBbpR49)
{
	extern TX_POWER_TUNING_ENTRY_STRUCT *TxPowerTuningTable;
	CHAR TotalDeltaPower = 0; 
	BBP_R49_STRUC BbpR49;
	UINT32 desiredTSSI = 0, currentTSSI = 0;
	PTX_POWER_TUNING_ENTRY_STRUCT pTxPowerTuningEntry = NULL;
	UCHAR RFValue = 0;

	/* Locate the internal Tx ALC tuning entry */
	if (pAd->TxPowerCtrl.bInternalTxALC == TRUE  && (IS_RT3390(pAd)))
	{
		if (pAd->Mlme.OneSecPeriodicRound % 4 == 0)
		{
			desiredTSSI = RT5390_GetDesiredTSSI(pAd);

			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R49, &BbpR49.byte);
			currentTSSI = BbpR49.field.TSSI;

			if (desiredTSSI > currentTSSI)
			{
				pAd->TxPowerCtrl.idxTxPowerTable++;
			}

			if (desiredTSSI < currentTSSI)
			{
				pAd->TxPowerCtrl.idxTxPowerTable--;
			}

			if (pAd->TxPowerCtrl.idxTxPowerTable < LOWERBOUND_TX_POWER_TUNING_ENTRY)
			{
				pAd->TxPowerCtrl.idxTxPowerTable = LOWERBOUND_TX_POWER_TUNING_ENTRY;
			}

			if (pAd->TxPowerCtrl.idxTxPowerTable >= UPPERBOUND_TX_POWER_TUNING_ENTRY(pAd))
			{
				pAd->TxPowerCtrl.idxTxPowerTable = UPPERBOUND_TX_POWER_TUNING_ENTRY(pAd);
			}

			
			/* Valide pAd->TxPowerCtrl.idxTxPowerTable: -30 ~ 45 */
			

			pTxPowerTuningEntry = &TxPowerTuningTable[pAd->TxPowerCtrl.idxTxPowerTable + TX_POWER_TUNING_ENTRY_OFFSET]; // zero-based array
			pAd->TxPowerCtrl.RF_R12_Value = pTxPowerTuningEntry->RF_R12_Value;
			pAd->TxPowerCtrl.MAC_PowerDelta = pTxPowerTuningEntry->MAC_PowerDelta;

			
			/* Tx power adjustment over RF */
			
			RT30xxReadRFRegister(pAd, RF_R12, (PUCHAR)(&RFValue));
			RFValue = ((RFValue & 0xE0) | pAd->TxPowerCtrl.RF_R12_Value);
			RT30xxWriteRFRegister(pAd, RF_R12, (UCHAR)(RFValue));

			
			/* Tx power adjustment over MAC */
			
			TotalDeltaPower += pAd->TxPowerCtrl.MAC_PowerDelta;

			DBGPRINT(RT_DEBUG_TRACE, ("%s: desiredTSSI = %d, currentTSSI = %d, idxTxPowerTable = %d, {RF_R12_Value = %d, MAC_PowerDelta = %d}\n", 
				__FUNCTION__, 
				desiredTSSI, 
				currentTSSI, 
				pAd->TxPowerCtrl.idxTxPowerTable, 
				pTxPowerTuningEntry->RF_R12_Value, 
				pTxPowerTuningEntry->MAC_PowerDelta));
		}
		else
		{
			
			/* Tx power adjustment over RF */
			
			RT30xxReadRFRegister(pAd, RF_R12, (PUCHAR)(&RFValue));
				RFValue = ((RFValue & 0xE0) | pAd->TxPowerCtrl.RF_R12_Value);
			RT30xxWriteRFRegister(pAd, RF_R12, (UCHAR)(RFValue));

			
			/* Tx power adjustment over MAC */
			
			TotalDeltaPower += pAd->TxPowerCtrl.MAC_PowerDelta;
		}
	}

}


/*
        ==========================================================================
        Description:
                Get the desired TSSI based on the latest packet

        Arguments:
                pAd
		pBbpR49

        Return Value:
                The desired TSSI
        ==========================================================================
 */
UINT32 RT5390_GetDesiredTSSI(
	IN PRTMP_ADAPTER		pAd)
{
	PHTTRANSMIT_SETTING pLatestTxHTSetting = (PHTTRANSMIT_SETTING)(&pAd->LastTxRate);
	UCHAR desiredTSSI = 0;
	UCHAR MCS = 0;
	UCHAR ch=0;
	UCHAR MaxMCS = 7;
	MCS = (UCHAR)(pLatestTxHTSetting->field.MCS);

	if ((pAd->CommonCfg.CentralChannel >= 1) && (pAd->CommonCfg.CentralChannel <= 14))
			{
				ch = pAd->CommonCfg.CentralChannel;
			}
			else
			{
				ch = 1;
	
				DBGPRINT(RT_DEBUG_ERROR, ("%s: Incorrect channel #%d\n", __FUNCTION__, pAd->CommonCfg.CentralChannel));
			}

		if (pLatestTxHTSetting->field.MODE == MODE_CCK)
	{
		if (MCS > 3) /* boundary verification */
		{
			DBGPRINT(RT_DEBUG_ERROR, ("%s: incorrect MCS: MCS = %d\n", 
				__FUNCTION__, 
				MCS));
			
			MCS = 0;
		}
		desiredTSSI = RT5390_desiredTSSIOverCCKExt[ch][MCS];
	}
	else if (pLatestTxHTSetting->field.MODE == MODE_OFDM)
	{
		if (MCS > 7) /* boundary verification */
		{
			DBGPRINT(RT_DEBUG_ERROR, ("%s: incorrect MCS: MCS = %d\n", 
				__FUNCTION__, 
				MCS));

			MCS = 0;
		}

 		desiredTSSI = RT5390_desiredTSSIOverOFDMExt[ch][MCS];

	}
	else if ((pLatestTxHTSetting->field.MODE == MODE_HTMIX) || (pLatestTxHTSetting->field.MODE == MODE_HTGREENFIELD))
	{
		MaxMCS = pAd->chipCap.TxAlcMaxMCS - 1;

		if (MCS > MaxMCS) /* boundary verification */
		{
			DBGPRINT(RT_DEBUG_ERROR, ("%s: incorrect MCS: MCS = %d\n", 
				__FUNCTION__, 
				MCS));

			MCS = 0;
		}

		if (pLatestTxHTSetting->field.STBC == 1)
		{
			desiredTSSI = RT5390_desiredTSSIOverHTExt[ch][MCS];
		}
		else
		{
			desiredTSSI = RT5390_desiredTSSIOverHTUsingSTBCExt[ch][MCS];
		}
		
		/* For HT BW40 MCS 7 with/without STBC configuration, the desired TSSI value should subtract one from the formula. */
		
		if ((pLatestTxHTSetting->field.BW == BW_40) && (MCS == MCS_7))
		{
			desiredTSSI -= 1;
		}
	}

	DBGPRINT(RT_DEBUG_INFO, ("%s: desiredTSSI = %d, Latest Tx HT setting: MODE = %d, MCS = %d, STBC = %d\n", 
		__FUNCTION__, 
		desiredTSSI, 
		pLatestTxHTSetting->field.MODE, 
		pLatestTxHTSetting->field.MCS, 
		pLatestTxHTSetting->field.STBC));

	DBGPRINT(RT_DEBUG_INFO, ("<--- %s\n", __FUNCTION__));

	return desiredTSSI;
}

/*
   Rounding to integer
   e.g., +16.9 ~= 17 and -16.9 ~= -17

   Parameters
	  pAd: The adapter data structure
	  Integer: Integer part
	  Fraction: Fraction part
	  DenominatorOfTssiRatio: The denominator of the TSSI ratio

    Return Value:
	  Rounding result
*/
LONG Rounding(
	IN PRTMP_ADAPTER pAd, 
	IN LONG Integer, 
	IN LONG Fraction, 
	IN LONG DenominatorOfTssiRatio)
{
	LONG temp = 0;

	DBGPRINT(RT_DEBUG_INFO, ("%s: Integer = %d, Fraction = %d, DenominatorOfTssiRatio = %d\n", 
		__FUNCTION__, 
		(INT)Integer, 
		(INT)Fraction, 
		(INT)DenominatorOfTssiRatio));

	if (Fraction >= 0)
	{
		if (Fraction < (DenominatorOfTssiRatio / 10))
		{
			return Integer; /* e.g., 32.08059 ~= 32 */
		}
	}
	else
	{
		if (-Fraction < (DenominatorOfTssiRatio / 10))
		{
			return Integer; /* e.g., -32.08059 ~= -32 */
		}
	}

	if (Integer >= 0)
	{
		if (Fraction == 0)
		{
			return Integer;
		}
		else
		{
			do {
				if (Fraction == 0)
				{
					break;
				}
				else
				{
					temp = Fraction / 10;
					if (temp == 0)
					{
						break;
					}
					else
					{
						Fraction = temp;
					}
				}
			} while (1);

			DBGPRINT(RT_DEBUG_INFO, ("%s: [+] temp = %d, Fraction = %d\n", __FUNCTION__, (INT)temp, (INT)Fraction));

			if (Fraction >= 5)
			{
				return (Integer + 1);
			}
			else
			{
				return Integer;
			}
		}
	}
	else
	{
		if (Fraction == 0)
		{
			return Integer;
		}
		else
		{
			do {
				if (Fraction == 0)
				{
					break;
				}
				else
				{
					temp = Fraction / 10;
					if (temp == 0)
					{
						break;
					}
					else
					{
						Fraction = temp;
					}
				}
			} while (1);

			DBGPRINT(RT_DEBUG_INFO, ("%s: [-] temp = %d, Fraction = %d\n", __FUNCTION__, (INT)temp, (INT)Fraction));

			if (Fraction <= -5)
			{
				return (Integer - 1);
			}
			else
			{
				return Integer;
			}
		}
	}
}

/*
   Get the desired TSSI based on the latest packet

   Parameters
	  pAd: The adapter data structure
	  pDesiredTssi: The desired TSSI
	  pCurrentTssi: The current TSSI/
	
   Return Value:
	  Success or failure
*/
BOOLEAN GetDesiredTssiAndCurrentTssi(
	IN PRTMP_ADAPTER pAd, 
	IN OUT PCHAR pDesiredTssi, 
	IN OUT PCHAR pCurrentTssi)
{
	UCHAR BbpR47 = 0;
	UCHAR RateInfo = 0;
	CCK_TSSI_INFO cckTssiInfo = {{0}};
	OFDM_TSSI_INFO ofdmTssiInfo = {{0}};
	HT_TSSI_INFO htTssiInfo = {{{0}}};// warning!! by sw

	UCHAR ch=0;

	DBGPRINT(RT_DEBUG_INFO, ("---> %s\n", __FUNCTION__));

	if (IS_RT5390F(pAd))
	{
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R47, &BbpR47);
		if ((BbpR47 & 0x04) == 0x04) /* The TSSI INFO is not ready. */
		{
			DBGPRINT(RT_DEBUG_ERROR, ("%s: BBP TSSI INFO is not ready. (BbpR47 = 0x%X)\n", __FUNCTION__, BbpR47));

			return FALSE;
		}
		
		
		/* Get TSSI */
		
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R49, (PUCHAR)(pCurrentTssi));
#ifdef RT5370
		if ((*pCurrentTssi < 0) || (*pCurrentTssi > 0x7C))
		{
			DBGPRINT(RT_DEBUG_ERROR, ("%s: Out of range, *pCurrentTssi = %d\n", __FUNCTION__, *pCurrentTssi));
			
			*pCurrentTssi = 0;
		}
#endif /* RT5370 */
		DBGPRINT(RT_DEBUG_TRACE, ("%s: *pCurrentTssi = %d\n", __FUNCTION__, *pCurrentTssi));

		
		/* Get packet information */
		
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R47, &BbpR47);
		BbpR47 = ((BbpR47 & ~0x03) | 0x01); /* TSSI_REPORT_SEL (TSSI INFO 1 - Packet infomation) */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R47, BbpR47);

		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R49, (PUCHAR)(&RateInfo));

		if ((*pCurrentTssi < 0) || (*pCurrentTssi > 0x7C))	

		{
			DBGPRINT(RT_DEBUG_ERROR, ("%s: Out of range, *pCurrentTssi = %d\n", __FUNCTION__, *pCurrentTssi));
			
			*pCurrentTssi = 0;
		}
		if ((RateInfo & 0x03) == MODE_CCK) /* CCK */
		{
			cckTssiInfo.value = RateInfo;

			DBGPRINT(RT_DEBUG_TRACE, ("%s: CCK, cckTssiInfo.field.Rate = %d\n", 
				__FUNCTION__, 
				cckTssiInfo.field.Rate));

			DBGPRINT(RT_DEBUG_INFO, ("%s: RateInfo = 0x%X\n", __FUNCTION__, RateInfo));

			if (((cckTssiInfo.field.Rate >= 4) && (cckTssiInfo.field.Rate <= 7)) || 
			      (cckTssiInfo.field.Rate > 11)) /* boundary verification */
			{
				DBGPRINT(RT_DEBUG_ERROR, ("%s: incorrect MCS: cckTssiInfo.field.Rate = %d\n", 
					__FUNCTION__, 
					cckTssiInfo.field.Rate));
				
				return FALSE;
			}

			/* Data rate mapping for short/long preamble over CCK */
			if (cckTssiInfo.field.Rate == 8)
			{
				cckTssiInfo.field.Rate = 0; /* Short preamble CCK 1Mbps => Long preamble CCK 1Mbps */
			}
			else if (cckTssiInfo.field.Rate == 9)
			{
				cckTssiInfo.field.Rate = 1; /* Short preamble CCK 2Mbps => Long preamble CCK 2Mbps */
			}
			else if (cckTssiInfo.field.Rate == 10)
			{
				cckTssiInfo.field.Rate = 2; /* Short preamble CCK 5.5Mbps => Long preamble CCK 5.5Mbps */
			}
			else if (cckTssiInfo.field.Rate == 11)
			{
				cckTssiInfo.field.Rate = 3; /* Short preamble CCK 11Mbps => Long preamble CCK 11Mbps */
			}

			if ((pAd->CommonCfg.CentralChannel >= 1) && (pAd->CommonCfg.CentralChannel <= 14))
			{
				ch = pAd->CommonCfg.CentralChannel;
			}
			else
			{
				ch = 1;

				DBGPRINT(RT_DEBUG_ERROR, ("%s: Incorrect channel #%d\n", __FUNCTION__, pAd->CommonCfg.CentralChannel));
			}
		
			*pDesiredTssi = RT5390_desiredTSSIOverCCKExt[ch][cckTssiInfo.field.Rate];

			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, *pDesiredTssi = %d\n", __FUNCTION__, ch, *pDesiredTssi));

		}
		else if ((RateInfo & 0x03) == MODE_OFDM) // OFDM
		{
			ofdmTssiInfo.value = RateInfo;

			
			/* BBP OFDM rate format ==> MAC OFDM rate format */
			
			switch (ofdmTssiInfo.field.Rate)
			{
				case 0x0B: /* 6 Mbits/s */
				{
					ofdmTssiInfo.field.Rate = MCS_0;
				}
				break;

				case 0x0F: /* 9 Mbits/s */
				{
					ofdmTssiInfo.field.Rate = MCS_1;
				}
				break;

				case 0x0A: /* 12 Mbits/s */
				{
					ofdmTssiInfo.field.Rate = MCS_2;
				}
				break;

				case 0x0E: /* 18  Mbits/s */
				{
					ofdmTssiInfo.field.Rate = MCS_3;
				}
				break;

				case 0x09: /* 24  Mbits/s */
				{
					ofdmTssiInfo.field.Rate = MCS_4;
				}
				break;

				case 0x0D: /* 36  Mbits/s */
				{
					ofdmTssiInfo.field.Rate = MCS_5;
				}
				break;

				case 0x08: /* 48  Mbits/s */
				{
					ofdmTssiInfo.field.Rate = MCS_6;
				}
				break;

				case 0x0C: /* 54  Mbits/s */
				{
					ofdmTssiInfo.field.Rate = MCS_7;
				}
				break;

				default: 
				{
					DBGPRINT(RT_DEBUG_ERROR, ("%s: Incorrect OFDM rate = 0x%X\n", __FUNCTION__, ofdmTssiInfo.field.Rate));
					
					return FALSE;
				}
				break;
			}

			DBGPRINT(RT_DEBUG_TRACE, ("%s: OFDM, ofdmTssiInfo.field.Rate = %d\n", 
				__FUNCTION__, 
				ofdmTssiInfo.field.Rate));

			if ((ofdmTssiInfo.field.Rate < 0) || (ofdmTssiInfo.field.Rate > 7)) /* boundary verification */
			{
				DBGPRINT(RT_DEBUG_ERROR, ("%s: incorrect MCS: ofdmTssiInfo.field.Rate = %d\n", 
					__FUNCTION__, 
					ofdmTssiInfo.field.Rate));

				return FALSE;
			}

			if ((pAd->CommonCfg.CentralChannel >= 1) && (pAd->CommonCfg.CentralChannel <= 14))
			{
				ch = pAd->CommonCfg.CentralChannel;
			}
			else
			{
				ch = 1;

				DBGPRINT(RT_DEBUG_ERROR, ("%s: Incorrect channel #%d\n", __FUNCTION__, pAd->CommonCfg.CentralChannel));
			}

			*pDesiredTssi = RT5390_desiredTSSIOverOFDMExt[ch][ofdmTssiInfo.field.Rate];

			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, *pDesiredTssi = %d\n", __FUNCTION__, ch, *pDesiredTssi));
			DBGPRINT(RT_DEBUG_INFO, ("%s: RateInfo = 0x%X\n", __FUNCTION__, RateInfo));
		}
		else /* Mixed mode or green-field mode */
		{
			htTssiInfo.PartA.value = RateInfo;
			DBGPRINT(RT_DEBUG_INFO, ("%s: RateInfo = 0x%X\n", __FUNCTION__, RateInfo));

			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R47, &BbpR47);
			BbpR47 = ((BbpR47 & ~0x03) | 0x02); /* TSSI_REPORT_SEL (TSSI INFO 2 - Packet infomation) */
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R47, 0x92);
			RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R49, (PUCHAR)(&RateInfo));

			htTssiInfo.PartB.value = RateInfo;
			DBGPRINT(RT_DEBUG_INFO, ("%s: RateInfo = 0x%X\n", __FUNCTION__, RateInfo));

			DBGPRINT(RT_DEBUG_TRACE, ("%s: HT, htTssiInfo.PartA.field.STBC = %d, htTssiInfo.PartB.field.MCS = %d\n", 
				__FUNCTION__, 
				htTssiInfo.PartA.field.STBC, 
				htTssiInfo.PartB.field.MCS));

			if ((htTssiInfo.PartB.field.MCS < 0) || (htTssiInfo.PartB.field.MCS > 7)) /* boundary verification */
			{
				DBGPRINT(RT_DEBUG_ERROR, ("%s: incorrect MCS: htTssiInfo.PartB.field.MCS = %d\n", 
					__FUNCTION__, 
					htTssiInfo.PartB.field.MCS));

				return FALSE;
			}

			if ((pAd->CommonCfg.CentralChannel >= 1) && (pAd->CommonCfg.CentralChannel <= 14))
			{
				ch = pAd->CommonCfg.CentralChannel;
			}
			else
			{
				ch = 1;

				DBGPRINT(RT_DEBUG_ERROR, ("%s: Incorrect channel #%d\n", __FUNCTION__, pAd->CommonCfg.CentralChannel));
			}

			if (htTssiInfo.PartA.field.STBC == 0)
			{
				*pDesiredTssi = RT5390_desiredTSSIOverHTExt[ch][htTssiInfo.PartB.field.MCS];
			}
			else
			{
				*pDesiredTssi = RT5390_desiredTSSIOverHTUsingSTBCExt[ch][htTssiInfo.PartB.field.MCS];
			}

			DBGPRINT(RT_DEBUG_TRACE, ("%s: ch = %d, *pDesiredTssi = %d\n", __FUNCTION__, ch, *pDesiredTssi));			
		}	

		if (*pDesiredTssi < 0x00)
		{
			*pDesiredTssi = 0x00;
		}	
		else if (*pDesiredTssi > 0x7C)
		{
			*pDesiredTssi = 0x7C;
		}

		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R47, &BbpR47);
		BbpR47 = ((BbpR47 & ~0x07) | 0x04); /* TSSI_REPORT_SEL (TSSI INFO 0 - TSSI) and enable TSSI INFO udpate */
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R47, BbpR47);
	}

	DBGPRINT(RT_DEBUG_INFO, ("<--- %s\n", __FUNCTION__));

	return TRUE;
}

#ifdef RALINK_ATE
INT RT5390_ATETssiCalibration(
	IN	PRTMP_ADAPTER		pAd,
	IN	PSTRING				arg)
{    
	UCHAR inputDAC;
	// warning!! by sw
	UINT 		i = 0;
	UCHAR		BbpData, RFValue, OrgBbp47Value, ChannelPower;
	USHORT		EEPData;
	UCHAR 		BSSID_ADDR[MAC_ADDR_LEN] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
	BBP_R47_STRUC	BBPR47;	
	
	inputDAC = simple_strtol(arg, 0, 10);

	if (!IS_RT5390(pAd) || !(pAd->TxPowerCtrl.bInternalTxALC))                          
	{
		DBGPRINT(RT_DEBUG_TRACE, ("Not support TSSI calibration since not 5390 chip or EEPROM not set!!!\n"));
		return FALSE;
	}

	/* Set RF R27[3:0] TSSI gain */		
	RT30xxReadRFRegister(pAd, RF_R27, (PUCHAR)(&RFValue));			
	RFValue = ((RFValue & 0xF0) | pAd->TssiGain); /* [3:0] = (tssi_gain and tssi_atten) */
	RT30xxWriteRFRegister(pAd, RF_R27, RFValue);	

	/* Set RF R28 bit[7:6] = 00 */
	RT30xxReadRFRegister(pAd, RF_R28, &RFValue);
	/* RF28Value = RFValue; */
	RFValue &= (~0xC0); 
	RT30xxWriteRFRegister(pAd, RF_R28, RFValue);

	/* set BBP R47[7] = 1(ADC6 ON), R47[4:3] = 0x2(new average TSSI mode), R47[2] = 1(TSSI_UPDATE_REQ), R49[1:0] = 0(TSSI info 0 - TSSI) */
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R47, &BBPR47.byte);
	OrgBbp47Value = BBPR47.byte;
	BBPR47.field.Adc6On = 1;
	BBPR47.field.TssiMode = 0x02;
	BBPR47.field.TssiUpdateReq = 1;
	BBPR47.field.TssiReportSel = 0;							
	DBGPRINT(RT_DEBUG_TRACE, ("Write BBP R47 = 0x%x\n", BBPR47.byte));
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R47, BBPR47.byte);		

	/* start TX at 54Mbps, we use channel and power value passed from upper layer program */
	NdisZeroMemory(&pAd->ate, sizeof(struct _ATE_INFO));
	pAd->ate.TxCount = 100;
	pAd->ate.TxLength = 1024;
	 pAd->ate.Channel = 1;
	COPY_MAC_ADDR(pAd->ate.Addr1, BROADCAST_ADDR);
	COPY_MAC_ADDR(pAd->ate.Addr2, pAd->PermanentAddress);                                                     
	COPY_MAC_ADDR(pAd->ate.Addr3, BSSID_ADDR);    

	Set_ATE_TX_MODE_Proc(pAd, "1");		/* MODE_OFDM */
	Set_ATE_TX_MCS_Proc(pAd, "7");		/* 54Mbps */
	Set_ATE_TX_BW_Proc(pAd, "0");		/* 20MHz */
			
	/* set power value calibrated DAC */		
	pAd->ate.TxPower0 = inputDAC;
     	DBGPRINT(RT_DEBUG_TRACE, ("(Calibrated) Tx.Power0= 0x%x\n", pAd->ate.TxPower0));
		 
	/* read frequency offset from EEPROM */                       
	RT28xx_EEPROM_READ16(pAd, EEPROM_FREQ_OFFSET, EEPData);
	pAd->ate.RFFreqOffset = (UCHAR) (EEPData & 0xff);
		
	Set_ATE_Proc(pAd, "TXFRAME"); 
	RTMPusecDelay(200000);

	while (i < 500)
	{
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R47, &BbpData);

		if ((BbpData & 0x04) == 0)
			break;

		RTMPusecDelay(2);
		i++;	
	}

	if (i >= 500)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("TSSI status not ready!!! (i=%d)\n", i));
		return FALSE;
	}	

	/* read BBP R49[6:0] and write to EEPROM 0x6E */
	DBGPRINT(RT_DEBUG_TRACE, ("Read  BBP_R49\n")); 
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R49, &BbpData);
	DBGPRINT(RT_DEBUG_TRACE, ("BBP R49 = 0x%x\n", BbpData)); 
	BbpData &= 0x7f;

	/* the upper boundary of 0x6E (TSSI base) is 0x7C */
	if (BbpData > 0x7C)
		BbpData = 0;

	RT28xx_EEPROM_READ16(pAd, EEPROM_TSSI_OVER_OFDM_54, EEPData);
	EEPData &= 0xff00;
	EEPData |= BbpData;
	DBGPRINT(RT_DEBUG_TRACE, ("Write  E2P 0x6e: 0x%x\n", EEPData)); 		
	
#ifdef RTMP_EFUSE_SUPPORT
	if (pAd->bUseEfuse)
	{
		if (pAd->bFroceEEPROMBuffer)
			NdisMoveMemory(&(pAd->EEPROMImage[EEPROM_TSSI_OVER_OFDM_54]), (PUCHAR)(&EEPData) ,2);
		else
			eFuseWrite(pAd, EEPROM_TSSI_OVER_OFDM_54, (PUSHORT)(&EEPData), 2);// warning!! by sw
	}
#endif /* RTMP_EFUSE_SUPPORT */
	else
	{
		RT28xx_EEPROM_WRITE16(pAd, EEPROM_TSSI_OVER_OFDM_54, EEPData);
		RTMPusecDelay(10);
	}    

	/* restore RF R27 and R28, BBP R47 */
	/* RT30xxWriteRFRegister(pAd, RF_R27, RF27Value); */				
	/* RT30xxWriteRFRegister(pAd, RF_R28, RF28Value); */
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R47, OrgBbp47Value);

	Set_ATE_Proc(pAd, "ATESTART");

	return TRUE;
}

/* Vx = V0 + t(V1 - V0) ? f(x), where t = (x-x0) / (x1 - x0) */
CHAR RTATEInsertTssi(UCHAR InChannel, UCHAR Channel0, UCHAR Channel1,CHAR Tssi0, CHAR Tssi1)
{
	CHAR	InTssi, TssiDelta, ChannelDelta, InChannelDelta;
	
	ChannelDelta = Channel1 - Channel0;
	InChannelDelta = InChannel - Channel0;
	TssiDelta = Tssi1 - Tssi0;

	/* channel delta should not be 0 */
	if (ChannelDelta == 0)
		InTssi = Tssi0;

	DBGPRINT(RT_DEBUG_WARN, ("--->RTATEInsertTssi\n")); 	
	
	if ((TssiDelta > 0) && (((InChannelDelta * TssiDelta * 10) / ChannelDelta) % 10 >= 5))
	{
		InTssi = Tssi0 + ((InChannelDelta * TssiDelta) / ChannelDelta);
		InTssi += 1;
	}
	else	if ((TssiDelta < 0) && (((InChannelDelta * TssiDelta * 10) / ChannelDelta) % 10 <= -5))
	{
		InTssi = Tssi0 + ((InChannelDelta * TssiDelta) / ChannelDelta);
		InTssi -= 1;
	}
	else
	{
		InTssi = Tssi0 + ((InChannelDelta * TssiDelta) / ChannelDelta);	
	}	

	DBGPRINT(RT_DEBUG_WARN, ("<---RTATEInsertTssi\n")); 		
	
	return InTssi;
}

UCHAR RTATEGetTssiByChannel(PRTMP_ADAPTER pAd, UCHAR Channel)
{
	UINT	i = 0;
	UCHAR	BbpData =0;
	UCHAR	ChannelPower;
	UCHAR 	BSSID_ADDR[MAC_ADDR_LEN] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
	USHORT	EEPData;
	BBP_R47_STRUC BBPR47;

	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R47, &BBPR47.byte);
	BBPR47.field.Adc6On = 1;
	BBPR47.field.TssiMode = 0x02;
	BBPR47.field.TssiUpdateReq = 1;
	BBPR47.field.TssiReportSel = 0;							
	DBGPRINT(RT_DEBUG_WARN, ("Write BBP R47 = 0x%x\n", BBPR47.byte));
	RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R47, BBPR47.byte);
		
	/* start TX at 54Mbps */
	NdisZeroMemory(&pAd->ate, sizeof(ATE_INFO));
	pAd->ate.TxCount = 100;
	pAd->ate.TxLength = 1024;
	pAd->ate.Channel = Channel;
	COPY_MAC_ADDR(pAd->ate.Addr1, BROADCAST_ADDR);
	COPY_MAC_ADDR(pAd->ate.Addr2, pAd->PermanentAddress);                                                     
	COPY_MAC_ADDR(pAd->ate.Addr3, BSSID_ADDR);    		

	Set_ATE_TX_MODE_Proc(pAd, "1");		/* MODE_OFDM */
	Set_ATE_TX_MCS_Proc(pAd, "7");		/* 54Mbps */
	Set_ATE_TX_BW_Proc(pAd, "0");		/* 20MHz */
		
	/* read calibrated channel power value from EEPROM */
	RT28xx_EEPROM_READ16(pAd, EEPROM_G_TX_PWR_OFFSET+Channel-1, ChannelPower);
	pAd->ate.TxPower0 = (UCHAR)(ChannelPower & 0xff);
	DBGPRINT(RT_DEBUG_TRACE, ("Channel %d, Calibrated Tx.Power0= 0x%x\n", Channel, pAd->ate.TxPower0));
	
	/* read frequency offset from EEPROM */                        
	RT28xx_EEPROM_READ16(pAd, EEPROM_FREQ_OFFSET, EEPData);
	pAd->ate.RFFreqOffset = (UCHAR)(EEPData & 0xff);
		
	Set_ATE_Proc(pAd, "TXFRAME"); 
	RTMPusecDelay(200000);

	while (i < 500)
	{
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R47, &BbpData);

		if ((BbpData & 0x04) == 0)
			break;

		RTMPusecDelay(2);
		i++;	
	}

	if (i >= 500)
		DBGPRINT(RT_DEBUG_WARN, ("TSSI status not ready!!! (i=%d)\n", i));

	/* read BBP R49[6:0] and write to EEPROM 0x6E */
	DBGPRINT(RT_DEBUG_WARN, ("Read  BBP_R49\n")); 
	RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R49, &BbpData);
	DBGPRINT(RT_DEBUG_WARN, ("BBP R49 = 0x%x\n", BbpData)); 
	BbpData &= 0x7f;

	/* the upper boundary of 0x6E (TSSI base) is 0x7C */
	if (BbpData > 0x7C)
		BbpData = 0;

	/* back to ATE IDLE state */
	Set_ATE_Proc(pAd, "ATESTART");

	return BbpData;	
}

/* Get the power delta bound */
#define GET_TSSI_RATE_TABLE_INDEX(x) (((x) > UPPER_POWER_DELTA_INDEX) ? (UPPER_POWER_DELTA_INDEX) : (((x) < LOWER_POWER_DELTA_INDEX) ? (LOWER_POWER_DELTA_INDEX) : ((x))))

CHAR GetPowerDeltaFromTssiRatio(CHAR TssiOfChannel, CHAR TssiBase)
{
	LONG	TssiRatio, TssiDelta, MinTssiDelta;
	CHAR	i, PowerDeltaStatIndex, PowerDeltaEndIndex, MinTssiDeltaIndex;	
	CHAR	PowerDelta;
	extern ULONG TssiRatioTable[][2];

	// TODO: If 0 is a valid value for TSSI base
	if (TssiBase == 0)
		return 0;
	
	TssiRatio = TssiOfChannel * TssiRatioTable[0][1] / TssiBase;

	DBGPRINT(RT_DEBUG_WARN, ("TssiOfChannel = %d, TssiBase = %d, TssiRatio = %ld\n", TssiOfChannel,  TssiBase, TssiRatio));// warning!! by sw

	PowerDeltaStatIndex = 4;
	PowerDeltaEndIndex = 19;

	MinTssiDeltaIndex= PowerDeltaStatIndex;
	MinTssiDelta = TssiRatio - TssiRatioTable[MinTssiDeltaIndex][0];
	
	if (MinTssiDelta < 0)
		MinTssiDelta = -MinTssiDelta;

	for (i = PowerDeltaStatIndex+1; i <= PowerDeltaEndIndex; i++)
	{
		TssiDelta = TssiRatio -TssiRatioTable[i][0];
		
		if (TssiDelta < 0)
		{
			TssiDelta = -TssiDelta;
		}

		if (TssiDelta < MinTssiDelta)
		{
			MinTssiDelta = TssiDelta;
			MinTssiDeltaIndex = i;
		}
	}

	PowerDelta = MinTssiDeltaIndex - TSSI_RATIO_TABLE_OFFSET;
	// warning!! by sw
	DBGPRINT(RT_DEBUG_WARN, ("MinTssiDeltaIndex = %d, MinTssiDelta = %ld, PowerDelta = %d\n", MinTssiDeltaIndex,  MinTssiDelta, PowerDelta));
	
	return (PowerDelta);
}

INT RT5390_ATETssiCalibrationExtend(
	IN	PRTMP_ADAPTER		pAd,
	IN	PSTRING				arg)
{  
	UCHAR inputData;
	
	inputData = simple_strtol(arg, 0, 10);
	
	if (!(IS_RT5390(pAd) && (pAd->TxPowerCtrl.bInternalTxALC) && (pAd->TxPowerCtrl.bExtendedTssiMode)))			
	{
		DBGPRINT(RT_DEBUG_WARN, ("Not support TSSI calibration since not 5390 chip or EEPROM not set!!!\n"));
		return FALSE;
	}			
	else
	{				
		UCHAR	RFValue;
		CHAR	TssiRefPerChannel[14+1], PowerDeltaPerChannel[14+1], TssiBase;
		USHORT	EEPData;
		UCHAR	CurrentChannel;

		/* step 0: set init register values for TSSI calibration */
		/* Set RF R27[3:2] = 00, R27[1:0] = 11 */
		RT30xxReadRFRegister(pAd, RF_R27, &RFValue);
		/* RF27Value = RFValue; */
		/* RFValue &= (~0x0F); */
		/* RFValue |= 0x02; */ 
		RFValue = ((RFValue & 0xF0) | pAd->TssiGain); /* [3:0] = (tssi_gain and tssi_atten) */
		RT30xxWriteRFRegister(pAd, RF_R27, RFValue);

		/* Set RF R28 bit[7:6] = 00 */
		RT30xxReadRFRegister(pAd, RF_R28, &RFValue);
		/* RF28Value = RFValue; */
		RFValue &= (~0xC0); 
		RT30xxWriteRFRegister(pAd, RF_R28, RFValue);

		/* step 1: get channel 7 TSSI as reference value */
		CurrentChannel = 7;
		TssiRefPerChannel[CurrentChannel] = RTATEGetTssiByChannel(pAd, CurrentChannel);
		TssiBase = TssiRefPerChannel[CurrentChannel];
		PowerDeltaPerChannel[CurrentChannel] = GetPowerDeltaFromTssiRatio(TssiRefPerChannel[CurrentChannel], TssiBase);

		/* Save TSSI ref base to EEPROM 0x6E */
		RT28xx_EEPROM_READ16(pAd, EEPROM_TSSI_OVER_OFDM_54, EEPData);
		EEPData &= 0xff00;
		EEPData |= TssiBase;
		DBGPRINT(RT_DEBUG_WARN, ("Write  E2P 0x6E: 0x%x\n", EEPData)); 				
		RT28xx_EEPROM_WRITE16(pAd, EEPROM_TSSI_OVER_OFDM_54, EEPData);
		RTMPusecDelay(10); /* delay for twp(MAX)=10ms */
		
		/* step 2: get channel 1 and 13 TSSI values */
		/* start TX at 54Mbps */
		CurrentChannel = 1;
		TssiRefPerChannel[CurrentChannel] = RTATEGetTssiByChannel(pAd, CurrentChannel);
		PowerDeltaPerChannel[CurrentChannel] = GetPowerDeltaFromTssiRatio(TssiRefPerChannel[CurrentChannel], TssiBase);

		/* start TX at 54Mbps */
		CurrentChannel = 13;
		TssiRefPerChannel[CurrentChannel] = RTATEGetTssiByChannel(pAd, CurrentChannel);
		PowerDeltaPerChannel[CurrentChannel] = GetPowerDeltaFromTssiRatio(TssiRefPerChannel[CurrentChannel], TssiBase);

		/* step 3: insert the power table */
		/* insert channel 2 to 6 TSSI values */
		/*
			for(CurrentChannel = 2; CurrentChannel <7; CurrentChannel++)
				TssiRefPerChannel[CurrentChannel] = RTATEInsertTssi(CurrentChannel, 1, 7, TssiRefPerChannel[1], TssiRefPerChannel[7]);
		*/
		for (CurrentChannel = 2; CurrentChannel < 7; CurrentChannel++)
			PowerDeltaPerChannel[CurrentChannel] = RTATEInsertTssi(CurrentChannel, 1, 7, PowerDeltaPerChannel[1], PowerDeltaPerChannel[7]);

		/* insert channel 8 to 12 TSSI values */
		/*
			for(CurrentChannel = 8; CurrentChannel < 13; CurrentChannel++)
				TssiRefPerChannel[CurrentChannel] = RTATEInsertTssi(CurrentChannel, 7, 13, TssiRefPerChannel[7], TssiRefPerChannel[13]);
		*/
		for (CurrentChannel = 8; CurrentChannel < 13; CurrentChannel++)
			PowerDeltaPerChannel[CurrentChannel] = RTATEInsertTssi(CurrentChannel, 7, 13, PowerDeltaPerChannel[7], PowerDeltaPerChannel[13]);


		/* channel 14 TSSI equals channel 13 TSSI */
		/* TssiRefPerChannel[14] = TssiRefPerChannel[13]; */
		PowerDeltaPerChannel[14] = PowerDeltaPerChannel[13];

		for (CurrentChannel = 1; CurrentChannel <= 14; CurrentChannel++)
		{
			DBGPRINT(RT_DEBUG_WARN, ("Channel %d, PowerDeltaPerChannel= 0x%x\n", CurrentChannel, PowerDeltaPerChannel[CurrentChannel]));
		
			/* PowerDeltaPerChannel[CurrentChannel] = GetPowerDeltaFromTssiRatio(TssiRefPerChannel[CurrentChannel], TssiBase); */

			/* boundary check */
			if (PowerDeltaPerChannel[CurrentChannel] > 7)
				PowerDeltaPerChannel[CurrentChannel] = 7;
			if (PowerDeltaPerChannel[CurrentChannel] < -8)
				PowerDeltaPerChannel[CurrentChannel] = -8;

			/* eeprom only use 4 bit for TSSI delta */
			PowerDeltaPerChannel[CurrentChannel] &= 0x0f;
			DBGPRINT(RT_DEBUG_WARN, ("Channel = %d, PowerDeltaPerChannel=0x%x\n", CurrentChannel, PowerDeltaPerChannel[CurrentChannel]));	
		}
	

		/* step 4: store TSSI delta values to EEPROM 0x6f - 0x75 */
		RT28xx_EEPROM_READ16(pAd, EEPROM_TX_POWER_OFFSET_OVER_CH_1-1, EEPData);
		EEPData &= 0x00ff;
		EEPData |= (PowerDeltaPerChannel[1] << 8) | (PowerDeltaPerChannel[2] << 12);
		RT28xx_EEPROM_WRITE16(pAd, EEPROM_TX_POWER_OFFSET_OVER_CH_1-1, EEPData);
		
		for (CurrentChannel = 3; CurrentChannel <= 14; CurrentChannel += 4)
		{
			/* EEPData = ( TssiDeltaPerChannel[CurrentChannel+2]  << 12) |(  TssiDeltaPerChannel[CurrentChannel+1]  << 8); */
			/* DBGPRINT(RT_DEBUG_TRACE, ("CurrentChannel=%d, TssiDeltaPerChannel[CurrentChannel+2] = 0x%x, EEPData=0x%x\n", CurrentChannel, TssiDeltaPerChannel[CurrentChannel+2], EEPData)); */
			EEPData = (PowerDeltaPerChannel[CurrentChannel + 3] << 12) | (PowerDeltaPerChannel[CurrentChannel + 2] << 8) | 
				(PowerDeltaPerChannel[CurrentChannel + 1] << 4) | PowerDeltaPerChannel[CurrentChannel];
			RT28xx_EEPROM_WRITE16(pAd, (EEPROM_TX_POWER_OFFSET_OVER_CH_3 + ((CurrentChannel - 3) / 2)), EEPData);
			/* DBGPRINT(RT_DEBUG_TRACE, ("offset=0x%x, EEPData = 0x%x\n", (EEPROM_TSSI_DELTA_CH3_CH4 +((CurrentChannel-3)/2)),EEPData));	*/
		}
						
		/* restore RF R27 and R28, BBP R47 */
		/* RT30xxWriteRFRegister(pAd, RF_R27, RF27Value); */				
		/* RT30xxWriteRFRegister(pAd, RF_R28, RF28Value); */

		Set_ATE_Proc(pAd, "ATESTART");
	}

	return TRUE;
}
#endif /* RALINK_ATE */
#endif /* defined(RT5370) || defined(RT5390) */
#endif /* RTMP_INTERNAL_TX_ALC */

#ifdef RTMP_TEMPERATURE_COMPENSATION
INT RT5392_ATEReadExternalTSSI(
	IN	PRTMP_ADAPTER		pAd,
	IN	PSTRING				arg)
{
	UCHAR	inputData, RFValue, BbpData;
	USHORT	EEPData, EEPTemp;

	inputData = simple_strtol(arg, 0, 10);
	
	RT28xx_EEPROM_READ16(pAd, EEPROM_NIC2_OFFSET, EEPData);

	if (!(IS_RT5392(pAd) && ((EEPData & 0x02) != 0)))			
	{
		DBGPRINT(RT_DEBUG_TRACE, ("Not support TSSI calibration since not 5392 chip or EEPROM not set!!!\n"));
		return FALSE;
	}
	else
	{	
		/* BBP R47[7]=1 : ADC 6 on */
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R47, &BbpData);
		BbpData |= 0x80;
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R47, BbpData);

		/* RF R27[7:6]=0x1 : Adc_insel 01:Temp */	
		/* Write RF R27[3:0]=EEPROM 0x76 bit[3:0] */
		RT30xxReadRFRegister(pAd, RF_R27, &RFValue);	
		RFValue &= ~0xC0;
		RFValue |= 0x40;
		RFValue = ((RFValue & 0xF0) | pAd->TssiGain); /* [3:0] = (tssi_gain and tssi_atten) */
		RT30xxWriteRFRegister(pAd, RF_R27, RFValue);			

		/* Wait 1ms. */					
		RTMPusecDelay(1000);

		/* Read BBP R49 reading as return value. */
		DBGPRINT(RT_DEBUG_TRACE, ("Read  BBP_R49\n")); 
		RTMP_BBP_IO_READ8_BY_REG_ID(pAd, BBP_R49, &BbpData);
		DBGPRINT(RT_DEBUG_TRACE, ("BBP R49=0x%x\n", BbpData));

		RT28xx_EEPROM_READ16(pAd, EEPROM_TSSI_STEP_OVER_2DOT4G, EEPData);
		/* RT28xx_EEPROM_READ16(pAd, EEPROM_TSSI_STEP_OVER_2DOT4G, EEPTemp); */
		/* BbpData = (BbpData + (UCHAR) (EEPTemp & 0xff)) / 2; */
		EEPData &= 0xff00;
		EEPData |= BbpData;
			
#ifdef RTMP_EFUSE_SUPPORT
		if (pAd->bUseEfuse)
		{
			if (pAd->bFroceEEPROMBuffer)
				NdisMoveMemory(&(pAd->EEPROMImage[EEPROM_TSSI_STEP_OVER_2DOT4G]), (PUCHAR)(&EEPData) ,2);
			else
			    	eFuseWrite(pAd, EEPROM_TSSI_STEP_OVER_2DOT4G, (PUCHAR)(&EEPData), 2);
		}
		else
#endif /* RTMP_EFUSE_SUPPORT */
		{
			RT28xx_EEPROM_WRITE16(pAd, EEPROM_TSSI_STEP_OVER_2DOT4G, EEPData);
			RTMPusecDelay(10);
		}   

		/* RF R27[7:6]=0x0 */
		RT30xxReadRFRegister(pAd, RF_R27, &RFValue);	
		RFValue &= ~0xC0;
		RT30xxWriteRFRegister(pAd, RF_R27, RFValue);		
	}

	return TRUE;
}
#endif /* RTMP_TEMPERATURE_COMPENSATION */

VOID RT5390_RTMPSetAGCInitValue(
	IN PRTMP_ADAPTER		pAd,
	IN UCHAR				BandWidth)
{
	UCHAR	R66 = 0x30;
	
	if (pAd->LatchRfRegs.Channel <= 14)
	{	/* BG band */

		/* Gary was verified Amazon AP and find that RT307x has BBP_R66 invalid default value */
		if (IS_RT5390(pAd))
		{
			R66 = 0x1C + 2*GET_LNA_GAIN(pAd);
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
			if (IS_RT5390(pAd))
			{
				if (IS_RT5392(pAd))
				{
					RT5392WriteBBPR66(pAd, R66);
				}
				else
				{
					RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
				}	
			}	
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */
		}
		else
		{
			R66 = 0x2E + GET_LNA_GAIN(pAd);
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
		}
#if defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392)
/*20100902 update for Rx Sensitive*/
	if (IS_RT5392(pAd))
			RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R135 ,0xF6);
#endif /* defined(RT5370) || defined(RT5372) || defined(RT5390) || defined(RT5392) */
	}
	else
	{	/*A band */
		{	
			if (BandWidth == BW_20)
			{
				R66 = (UCHAR)(0x32 + (GET_LNA_GAIN(pAd)*5)/3);

				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
			}
#ifdef DOT11_N_SUPPORT
			else
			{
				R66 = (UCHAR)(0x3A + (GET_LNA_GAIN(pAd)*5)/3);

				RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, R66);
			}
#endif /* DOT11_N_SUPPORT */
		}		
	}

}

VOID RT5390_ChipResumeMsduTransmission(
	IN	PRTMP_ADAPTER		pAd)
{    
	if (IS_RT5392(pAd))
		RT5392WriteBBPR66(pAd, pAd->BbpTuning.R66CurrentValue);
	else
		RTMP_BBP_IO_WRITE8_BY_REG_ID(pAd, BBP_R66, pAd->BbpTuning.R66CurrentValue);
}

VOID RT5390_AsicResetBbpAgent(
	IN PRTMP_ADAPTER		pAd)	
{	
	ULONG loop = 0;
	UINT8 BBPValue = 0;
	UINT32 MacValue = 0;
	RTMP_IO_READ32(pAd,0x1004,&MacValue);
	MacValue|=0x2;
	printk("MacValue1=%x\n",MacValue);
	RTMP_IO_WRITE32(pAd,0x1004,MacValue);
	MacValue&=~(0x2);
	printk("MacValue2=%x\n",MacValue);
	RTMP_IO_WRITE32(pAd,0x1004,MacValue);
}
