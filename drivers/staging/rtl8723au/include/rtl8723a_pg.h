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
 ******************************************************************************/
#ifndef __RTL8723A_PG_H__
#define __RTL8723A_PG_H__

/*			EEPROM/Efuse PG Offset for 8723E/8723U/8723S */
#define EEPROM_CCK_TX_PWR_INX_8723A			0x10
#define EEPROM_HT40_1S_TX_PWR_INX_8723A		0x16
#define EEPROM_HT20_TX_PWR_INX_DIFF_8723A	0x1C
#define EEPROM_OFDM_TX_PWR_INX_DIFF_8723A	0x1F
#define EEPROM_HT40_MAX_PWR_OFFSET_8723A	0x22
#define EEPROM_HT20_MAX_PWR_OFFSET_8723A	0x25

#define EEPROM_ChannelPlan_8723A			0x28
#define EEPROM_TSSI_A_8723A					0x29
#define EEPROM_THERMAL_METER_8723A			0x2A
#define RF_OPTION1_8723A					0x2B
#define RF_OPTION2_8723A					0x2C
#define RF_OPTION3_8723A					0x2D
#define RF_OPTION4_8723A					0x2E
#define EEPROM_VERSION_8723A				0x30
#define EEPROM_CustomID_8723A				0x31
#define EEPROM_SubCustomID_8723A			0x32
#define EEPROM_XTAL_K_8723A					0x33
#define EEPROM_Chipset_8723A				0x34

/*  RTL8723AE */
#define EEPROM_VID_8723AE					0x49
#define EEPROM_DID_8723AE					0x4B
#define EEPROM_SVID_8723AE					0x4D
#define EEPROM_SMID_8723AE					0x4F
#define EEPROM_MAC_ADDR_8723AE				0x67

/*  RTL8723AU */
#define EEPROM_MAC_ADDR_8723AU				0xC6
#define EEPROM_VID_8723AU					0xB7
#define EEPROM_PID_8723AU					0xB9

/*  RTL8723AS */
#define EEPROM_MAC_ADDR_8723AS				0xAA

/*			EEPROM/Efuse Value Type */
#define EETYPE_TX_PWR						0x0

/*			EEPROM/Efuse Default Value */
#define EEPROM_Default_CrystalCap_8723A		0x20


/*        EEPROM/EFUSE data structure definition. */
#define	MAX_CHNL_GROUP		3+9

struct txpowerinfo {
	u8 CCKIndex[RF_PATH_MAX][MAX_CHNL_GROUP];
	u8 HT40_1SIndex[RF_PATH_MAX][MAX_CHNL_GROUP];
	u8 HT40_2SIndexDiff[RF_PATH_MAX][MAX_CHNL_GROUP];
	u8 HT20IndexDiff[RF_PATH_MAX][MAX_CHNL_GROUP];
	u8 OFDMIndexDiff[RF_PATH_MAX][MAX_CHNL_GROUP];
	u8 HT40MaxOffset[RF_PATH_MAX][MAX_CHNL_GROUP];
	u8 HT20MaxOffset[RF_PATH_MAX][MAX_CHNL_GROUP];
	u8 TSSI_A[3];
	u8 TSSI_B[3];
	u8 TSSI_A_5G[3];		/* 5GL/5GM/5GH */
	u8 TSSI_B_5G[3];
};

enum bt_ant_num {
	Ant_x2	= 0,
	Ant_x1	= 1
};

enum bt_cotype {
	BT_2Wire		= 0,
	BT_ISSC_3Wire		= 1,
	BT_Accel		= 2,
	BT_CSR_BC4		= 3,
	BT_CSR_BC8		= 4,
	BT_RTL8756		= 5,
	BT_RTL8723A		= 6
};

enum bt_radioshared {
	BT_Radio_Shared		= 0,
	BT_Radio_Individual	= 1,
};

#endif
