/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#ifndef __HAL_PG_H__
#define __HAL_PG_H__

#define	MAX_RF_PATH				4
/* MAX_TX_COUNT must always be set to 4, otherwise the read efuse table
 * sequence will be wrong.
 */
#define MAX_TX_COUNT				4

/*  For VHT series TX power by rate table. */
/*  VHT TX power by rate off setArray = */
/*  Band:-2G&5G = 0 / 1 */
/*  RF: at most 4*4 = ABCD = 0/1/2/3 */
/*  CCK = 0 OFDM = 1/2 HT-MCS 0-15 =3/4/56 VHT =7/8/9/10/11 */
#define TX_PWR_BY_RATE_NUM_BAND			2
#define TX_PWR_BY_RATE_NUM_RF			4
#define TX_PWR_BY_RATE_NUM_RATE			84
#define MAX_RF_PATH_NUM				2
#define	MAX_CHNL_GROUP_24G			6
#define EEPROM_DEFAULT_BOARD_OPTION		0x00

/* EEPROM/Efuse PG Offset for 8723BE/8723BU/8723BS */
/*  0x10 ~ 0x63 = TX power area. */
#define	EEPROM_TX_PWR_INX_8723B			0x10
/* New EFUSE default value */
#define EEPROM_DEFAULT_24G_INDEX		0x2D
#define EEPROM_DEFAULT_24G_HT20_DIFF		0X02
#define EEPROM_DEFAULT_24G_OFDM_DIFF		0X04
#define	EEPROM_Default_ThermalMeter_8723B	0x18
#define EEPROM_Default_CrystalCap_8723B		0x20

#define	EEPROM_ChannelPlan_8723B		0xB8
#define	EEPROM_XTAL_8723B			0xB9
#define	EEPROM_THERMAL_METER_8723B		0xBA

#define	EEPROM_RF_BOARD_OPTION_8723B		0xC1
#define	EEPROM_RF_BT_SETTING_8723B		0xC3
#define	EEPROM_VERSION_8723B			0xC4
#define	EEPROM_CustomID_8723B			0xC5
#define EEPROM_DEFAULT_DIFF			0XFE

/* RTL8723BS */
#define	EEPROM_MAC_ADDR_8723BS			0x11A
#define EEPROM_Voltage_ADDR_8723B		0x8
#define RTL_EEPROM_ID				0x8129

struct TxPowerInfo24G {
	u8 IndexCCK_Base[MAX_RF_PATH][MAX_CHNL_GROUP_24G];
	u8 IndexBW40_Base[MAX_RF_PATH][MAX_CHNL_GROUP_24G];
	/* If only one tx, only BW20 and OFDM are used. */
	s8 CCK_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 OFDM_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 BW20_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 BW40_Diff[MAX_RF_PATH][MAX_TX_COUNT];
};

enum {
	Ant_x2	= 0,
	Ant_x1	= 1
};

enum {
	BT_RTL8723B = 8,
};

#endif
