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
#ifndef __HAL_COMMON_H__
#define __HAL_COMMON_H__

//CCK
#define RATE_1M						BIT(0)
#define RATE_2M						BIT(1)
#define RATE_5_5M					BIT(2)
#define RATE_11M					BIT(3)
//OFDM 
#define RATE_6M						BIT(4)
#define RATE_9M						BIT(5)
#define RATE_12M					BIT(6)
#define RATE_18M					BIT(7)
#define RATE_24M					BIT(8)
#define RATE_36M					BIT(9)
#define RATE_48M					BIT(10)
#define RATE_54M					BIT(11)
//MCS 1 Spatial Stream
#define RATE_MCS0					BIT(12)
#define RATE_MCS1					BIT(13)
#define RATE_MCS2					BIT(14)
#define RATE_MCS3					BIT(15)
#define RATE_MCS4					BIT(16)
#define RATE_MCS5					BIT(17)
#define RATE_MCS6					BIT(18)
#define RATE_MCS7					BIT(19)
//MCS 2 Spatial Stream
#define RATE_MCS8					BIT(20)
#define RATE_MCS9					BIT(21)
#define RATE_MCS10					BIT(22)
#define RATE_MCS11					BIT(23)
#define RATE_MCS12					BIT(24)
#define RATE_MCS13					BIT(25)
#define RATE_MCS14					BIT(26)
#define RATE_MCS15					BIT(27)

// ALL CCK Rate
#define	RATE_ALL_CCK				RATR_1M|RATR_2M|RATR_55M|RATR_11M 
#define	RATE_ALL_OFDM_AG			RATR_6M|RATR_9M|RATR_12M|RATR_18M|RATR_24M|\
									RATR_36M|RATR_48M|RATR_54M	
#define	RATE_ALL_OFDM_1SS			RATR_MCS0|RATR_MCS1|RATR_MCS2|RATR_MCS3 |\
									RATR_MCS4|RATR_MCS5|RATR_MCS6	|RATR_MCS7	
#define	RATE_ALL_OFDM_2SS			RATR_MCS8|RATR_MCS9	|RATR_MCS10|RATR_MCS11|\
									RATR_MCS12|RATR_MCS13|RATR_MCS14|RATR_MCS15

/*------------------------------ Tx Desc definition Macro ------------------------*/ 
//#pragma mark -- Tx Desc related definition. --
//----------------------------------------------------------------------------
//-----------------------------------------------------------
//	Rate
//-----------------------------------------------------------
// CCK Rates, TxHT = 0
#define DESC_RATE1M				0x00
#define DESC_RATE2M				0x01
#define DESC_RATE5_5M				0x02
#define DESC_RATE11M				0x03

// OFDM Rates, TxHT = 0
#define DESC_RATE6M				0x04
#define DESC_RATE9M				0x05
#define DESC_RATE12M				0x06
#define DESC_RATE18M				0x07
#define DESC_RATE24M				0x08
#define DESC_RATE36M				0x09
#define DESC_RATE48M				0x0a
#define DESC_RATE54M				0x0b

// MCS Rates, TxHT = 1
#define DESC_RATEMCS0				0x0c
#define DESC_RATEMCS1				0x0d
#define DESC_RATEMCS2				0x0e
#define DESC_RATEMCS3				0x0f
#define DESC_RATEMCS4				0x10
#define DESC_RATEMCS5				0x11
#define DESC_RATEMCS6				0x12
#define DESC_RATEMCS7				0x13
#define DESC_RATEMCS8				0x14
#define DESC_RATEMCS9				0x15
#define DESC_RATEMCS10				0x16
#define DESC_RATEMCS11				0x17
#define DESC_RATEMCS12				0x18
#define DESC_RATEMCS13				0x19
#define DESC_RATEMCS14				0x1a
#define DESC_RATEMCS15				0x1b
#define DESC_RATEMCS15_SG			0x1c
#define DESC_RATEMCS32				0x20

//============================================================
// Global var
//============================================================
#define	OFDM_TABLE_SIZE_92C 	37
#define	OFDM_TABLE_SIZE_92D 	43
#define	CCK_TABLE_SIZE		33

extern u32 OFDMSwingTable[OFDM_TABLE_SIZE_92D] ;

extern u8 CCKSwingTable_Ch1_Ch13[CCK_TABLE_SIZE][8];

extern u8 CCKSwingTable_Ch14 [CCK_TABLE_SIZE][8];

#ifdef CONFIG_CHIP_VER_INTEGRATION
void dump_chip_info(HAL_VERSION	ChipVersion);
#endif

u8	//return the final channel plan decision
hal_com_get_channel_plan(
	IN	PADAPTER	padapter,
	IN	u8			hw_channel_plan,	//channel plan from HW (efuse/eeprom)
	IN	u8			sw_channel_plan,	//channel plan from SW (registry/module param)
	IN	u8			def_channel_plan,	//channel plan used when the former two is invalid
	IN	BOOLEAN		AutoLoadFail
	);

void	HalSetBrateCfg(
	IN PADAPTER		Adapter,
	IN u8			*mBratesOS,
	OUT u16			*pBrateCfg);

u8	MRateToHwRate(u8 rate);

void hal_init_macaddr(_adapter *adapter);

void c2h_evt_clear(_adapter *adapter);
s32 c2h_evt_read(_adapter *adapter, u8 *buf);

#endif //__HAL_COMMON_H__

