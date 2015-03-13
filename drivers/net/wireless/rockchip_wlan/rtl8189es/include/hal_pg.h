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

#ifndef __HAL_PG_H__
#define __HAL_PG_H__

//====================================================
//			EEPROM/Efuse PG Offset for 8192 CE/CU
//====================================================
#define EEPROM_VID_92C							0x0A
#define EEPROM_PID_92C							0x0C
#define EEPROM_DID_92C							0x0C 
#define EEPROM_SVID_92C						0x0E
#define EEPROM_SMID_92C						0x10 
#define EEPROM_MAC_ADDR_92C					0x16

#define EEPROM_MAC_ADDR						0x16
#define EEPROM_TV_OPTION						0x50
#define EEPROM_SUBCUSTOMER_ID_92C			0x59
#define EEPROM_CCK_TX_PWR_INX					0x5A
#define EEPROM_HT40_1S_TX_PWR_INX			0x60
#define EEPROM_HT40_2S_TX_PWR_INX_DIFF		0x66
#define EEPROM_HT20_TX_PWR_INX_DIFF			0x69
#define EEPROM_OFDM_TX_PWR_INX_DIFF			0x6C
#define EEPROM_HT40_MAX_PWR_OFFSET			0x6F
#define EEPROM_HT20_MAX_PWR_OFFSET			0x72
#define EEPROM_CHANNEL_PLAN_92C 				0x75
#define EEPROM_TSSI_A							0x76
#define EEPROM_TSSI_B							0x77
#define EEPROM_THERMAL_METER_92C				0x78
#define EEPROM_RF_OPT1_92C					0x79
#define EEPROM_RF_OPT2_92C					0x7A
#define EEPROM_RF_OPT3_92C					0x7B
#define EEPROM_RF_OPT4_92C					0x7C
#define EEPROM_VERSION_92C						0x7E
#define EEPROM_CUSTOMER_ID_92C				0x7F

#define EEPROM_NORMAL_CHANNEL_PLAN			0x75
#define EEPROM_NORMAL_BoardType_92C			EEPROM_RF_OPT1_92C
#define BOARD_TYPE_NORMAL_MASK				0xE0
#define BOARD_TYPE_TEST_MASK					0xF
#define EEPROM_TYPE_ID							0x7E

// PCIe related
#define	EEPROM_PCIE_DEV_CAP_01				0xE0 // Express device capability in PCIe configuration space, i.e., map to offset 0x74
#define	EEPROM_PCIE_DEV_CAP_02				0xE1 // Express device capability in PCIe configuration space, i.e., map to offset 0x75

// EEPROM address for Test chip
#define EEPROM_TEST_USB_OPT					0x0E

#define EEPROM_EASY_REPLACEMENT				0x50//BIT0 1 for build-in module, 0 for external dongle

//====================================================
//			EEPROM/Efuse PG Offset for 8723AE/8723AU/8723AS
//====================================================
#define EEPROM_CCK_TX_PWR_INX_8723A			0x10
#define EEPROM_HT40_1S_TX_PWR_INX_8723A		0x16
#define EEPROM_HT20_TX_PWR_INX_DIFF_8723A	0x1C
#define EEPROM_OFDM_TX_PWR_INX_DIFF_8723A	0x1F
#define EEPROM_HT40_MAX_PWR_OFFSET_8723A	0x22 
#define EEPROM_HT20_MAX_PWR_OFFSET_8723A	0x25 

#define EEPROM_ChannelPlan_8723A				0x28
#define EEPROM_TSSI_A_8723A					0x29
#define EEPROM_THERMAL_METER_8723A			0x2A
#define RF_OPTION1_8723A						0x2B
#define RF_OPTION2_8723A						0x2C
#define RF_OPTION3_8723A						0x2D
#define RF_OPTION4_8723A						0x2E
#define EEPROM_VERSION_8723A					0x30
#define EEPROM_CustomID_8723A					0x31
#define EEPROM_SubCustomID_8723A				0x32
#define EEPROM_XTAL_K_8723A					0x33
#define EEPROM_Chipset_8723A					0x34


// RTL8723AE
#define EEPROM_VID_8723AE						0x49
#define EEPROM_DID_8723AE						0x4B
#define EEPROM_SVID_8723AE						0x4D
#define EEPROM_SMID_8723AE					0x4F
#define EEPROM_MAC_ADDR_8723AE				0x67

//RTL8723AU
#define EEPROM_MAC_ADDR_8723AU				0xC6
#define EEPROM_VID_8723AU						0xB7
#define EEPROM_PID_8723AU						0xB9

// RTL8723AS
#define EEPROM_MAC_ADDR_8723AS				0xAA

//====================================================
//			EEPROM/Efuse PG Offset for 8192 DE/DU
//====================================================
// pcie
#define RTL8190_EEPROM_ID						0x8129	// 0-1
#define EEPROM_HPON							0x02 // LDO settings.2-5
#define EEPROM_CLK								0x06 // Clock settings.6-7
#define EEPROM_MAC_FUNCTION					0x08 // SE Test mode.8

#define EEPROM_MAC_ADDR_MAC0_92DE			0x55
#define EEPROM_MAC_ADDR_MAC1_92DE			0x5B

//usb
#define EEPROM_ENDPOINT_SETTING				0x10
#define EEPROM_CHIRP_K							0x12	// Changed
#define EEPROM_USB_PHY							0x13	// Changed
#define EEPROM_STRING							0x1F
#define EEPROM_SUBCUSTOMER_ID_92D			0x59

#define EEPROM_MAC_ADDR_MAC0_92DU			0x19
#define EEPROM_MAC_ADDR_MAC1_92DU			0x5B
//----------------------------------------------------------------
// 2.4G band Tx power index setting
#define EEPROM_CCK_TX_PWR_INX_2G_92D					0x61
#define EEPROM_HT40_1S_TX_PWR_INX_2G_92D				0x67
#define EEPROM_HT40_2S_TX_PWR_INX_DIFF_2G_92D			0x6D
#define EEPROM_HT20_TX_PWR_INX_DIFF_2G_92D				0x70
#define EEPROM_OFDM_TX_PWR_INX_DIFF_2G_92D				0x73
#define EEPROM_HT40_MAX_PWR_OFFSET_2G_92D				0x76
#define EEPROM_HT20_MAX_PWR_OFFSET_2G_92D				0x79

//5GL channel 32-64
#define EEPROM_HT40_1S_TX_PWR_INX_5GL_92D				0x7C
#define EEPROM_HT40_2S_TX_PWR_INX_DIFF_5GL_92D			0x82
#define EEPROM_HT20_TX_PWR_INX_DIFF_5GL_92D				0x85
#define EEPROM_OFDM_TX_PWR_INX_DIFF_5GL_92D			0x88
#define EEPROM_HT40_MAX_PWR_OFFSET_5GL_92D				0x8B
#define EEPROM_HT20_MAX_PWR_OFFSET_5GL_92D				0x8E

//5GM channel 100-140
#define EEPROM_HT40_1S_TX_PWR_INX_5GM_92D				0x91
#define EEPROM_HT40_2S_TX_PWR_INX_DIFF_5GM_92D			0x97
#define EEPROM_HT20_TX_PWR_INX_DIFF_5GM_92D			0x9A
#define EEPROM_OFDM_TX_PWR_INX_DIFF_5GM_92D			0x9D
#define EEPROM_HT40_MAX_PWR_OFFSET_5GM_92D			0xA0
#define EEPROM_HT20_MAX_PWR_OFFSET_5GM_92D			0xA3

//5GH channel 149-165
#define EEPROM_HT40_1S_TX_PWR_INX_5GH_92D				0xA6
#define EEPROM_HT40_2S_TX_PWR_INX_DIFF_5GH_92D			0xAC
#define EEPROM_HT20_TX_PWR_INX_DIFF_5GH_92D			0xAF
#define EEPROM_OFDM_TX_PWR_INX_DIFF_5GH_92D			0xB2
#define EEPROM_HT40_MAX_PWR_OFFSET_5GH_92D				0xB5
#define EEPROM_HT20_MAX_PWR_OFFSET_5GH_92D				0xB8


#define EEPROM_CHANNEL_PLAN_92D							0xBB // Map of supported channels.	
#define EEPROM_TEST_CHANNEL_PLAN_92D					0xBB
#define EEPROM_THERMAL_METER_92D							0xC3	//[4:0]
#define EEPROM_IQK_DELTA_92D								0xBC
#define EEPROM_LCK_DELTA_92D								0xBC
#define EEPROM_XTAL_K_92D									0xBD	//[7:5]
#define EEPROM_TSSI_A_5G_92D								0xBE
#define EEPROM_TSSI_B_5G_92D								0xBF
#define EEPROM_TSSI_AB_5G_92D								0xC0

#define EEPROM_RF_OPT1_92D						0xC4
#define EEPROM_RF_OPT2_92D						0xC5
#define EEPROM_RF_OPT3_92D						0xC6
#define EEPROM_RF_OPT4_92D						0xC7
#define EEPROM_RF_OPT5_92D						0xC8
#define EEPROM_RF_OPT6_92D						0xC9
#define EEPROM_RF_OPT7_92D						0xCC

#define EEPROM_NORMAL_BoardType_92D				EEPROM_RF_OPT1_92D	//[7:5]

#define EEPROM_WIDIPAIRING_ADDR				0xF0
#define EEPROM_WIDIPAIRING_KEY				0xF6

#define EEPROM_DEF_PART_NO					0x3FD    //Byte
#define EEPROME_CHIP_VERSION_L				0x3FF
#define EEPROME_CHIP_VERSION_H				0x3FE

//----------------------------------------------------------------

#define EEPROM_VID_92DE						0x28
#define EEPROM_PID_92DE						0x2A
#define EEPROM_SVID_92DE						0x2C
#define EEPROM_SMID_92DE						0x2E
#define EEPROM_PATHDIV_92D					0xC4

#define EEPROM_BOARD_OPTIONS_92D				0xC4
#define EEPROM_5G_LNA_GAIN_92D				0xC6
#define EEPROM_FEATURE_OPTIONS_92D			0xC7
#define EEPROM_BT_SETTING_92D					0xC8

#define EEPROM_VERSION_92D					0xCA
#define EEPROM_CUSTOMER_ID_92D				0xCB

#define EEPROM_VID_92DU						0xC
#define EEPROM_PID_92DU						0xE

//====================================================
//			EEPROM/Efuse PG Offset for 88EE/88EU/88ES
//====================================================
#define EEPROM_TX_PWR_INX_88E					0x10

#define EEPROM_ChannelPlan_88E					0xB8
#define EEPROM_XTAL_88E						0xB9
#define EEPROM_THERMAL_METER_88E				0xBA
#define EEPROM_IQK_LCK_88E						0xBB

#define EEPROM_RF_BOARD_OPTION_88E			0xC1
#define EEPROM_RF_FEATURE_OPTION_88E			0xC2
#define EEPROM_RF_BT_SETTING_88E				0xC3
#define EEPROM_VERSION_88E						0xC4
#define EEPROM_CustomID_88E					0xC5
#define EEPROM_RF_ANTENNA_OPT_88E			0xC9

// RTL88EE
#define EEPROM_MAC_ADDR_88EE					0xD0
#define EEPROM_VID_88EE						0xD6
#define EEPROM_DID_88EE						0xD8
#define EEPROM_SVID_88EE						0xDA
#define EEPROM_SMID_88EE						0xDC

//RTL88EU
#define EEPROM_MAC_ADDR_88EU					0xD7
#define EEPROM_VID_88EU						0xD0
#define EEPROM_PID_88EU						0xD2
#define EEPROM_USB_OPTIONAL_FUNCTION0		0xD4 //8192EU, 8812AU is the same
#define EEPROM_USB_OPTIONAL_FUNCTION0_8811AU 0x104

// RTL88ES
#define EEPROM_MAC_ADDR_88ES					0x11A
//====================================================
//			EEPROM/Efuse PG Offset for 8192EE/8192EU/8192ES
//====================================================
// 0x10 ~ 0x63 = TX power area.
#define	EEPROM_TX_PWR_INX_8192E				0x10

#define	EEPROM_ChannelPlan_8192E				0xB8
#define	EEPROM_XTAL_8192E						0xB9
#define	EEPROM_THERMAL_METER_8192E			0xBA
#define	EEPROM_IQK_LCK_8192E					0xBB
#define	EEPROM_2G_5G_PA_TYPE_8192E			0xBC
#define	EEPROM_2G_LNA_TYPE_GAIN_SEL_8192E	0xBD
#define	EEPROM_5G_LNA_TYPE_GAIN_SEL_8192E	0xBF

#define	EEPROM_RF_BOARD_OPTION_8192E		0xC1
#define	EEPROM_RF_FEATURE_OPTION_8192E		0xC2
#define	EEPROM_RF_BT_SETTING_8192E			0xC3
#define	EEPROM_VERSION_8192E					0xC4
#define	EEPROM_CustomID_8192E				0xC5
#define	EEPROM_TX_BBSWING_2G_8192E			0xC6
#define	EEPROM_TX_BBSWING_5G_8192E			0xC7
#define	EEPROM_TX_PWR_CALIBRATE_RATE_8192E	0xC8
#define	EEPROM_RF_ANTENNA_OPT_8192E			0xC9

// RTL8192EE
#define	EEPROM_MAC_ADDR_8192EE				0xD0
#define	EEPROM_VID_8192EE						0xD6
#define	EEPROM_DID_8192EE						0xD8
#define	EEPROM_SVID_8192EE					0xDA
#define	EEPROM_SMID_8192EE					0xDC

//RTL8192EU
#define	EEPROM_MAC_ADDR_8192EU				0xD7
#define	EEPROM_VID_8192EU						0xD0
#define	EEPROM_PID_8192EU						0xD2
#define 	EEPROM_PA_TYPE_8192EU               		0xBC
#define 	EEPROM_LNA_TYPE_2G_8192EU           	0xBD
#define 	EEPROM_LNA_TYPE_5G_8192EU           	0xBF

// RTL8192ES
#define	EEPROM_MAC_ADDR_8192ES				0x11B
//====================================================
//			EEPROM/Efuse PG Offset for 8812AE/8812AU/8812AS
//====================================================
// 0x10 ~ 0x63 = TX power area.
#define EEPROM_USB_MODE_8812					0x08
#define EEPROM_TX_PWR_INX_8812				0x10

#define EEPROM_ChannelPlan_8812				0xB8
#define EEPROM_XTAL_8812						0xB9
#define EEPROM_THERMAL_METER_8812			0xBA
#define EEPROM_IQK_LCK_8812					0xBB
#define EEPROM_2G_5G_PA_TYPE_8812			0xBC
#define EEPROM_2G_LNA_TYPE_GAIN_SEL_8812	0xBD
#define EEPROM_5G_LNA_TYPE_GAIN_SEL_8812	0xBF

#define EEPROM_RF_BOARD_OPTION_8812			0xC1
#define EEPROM_RF_FEATURE_OPTION_8812		0xC2
#define EEPROM_RF_BT_SETTING_8812				0xC3
#define EEPROM_VERSION_8812					0xC4
#define EEPROM_CustomID_8812					0xC5
#define EEPROM_TX_BBSWING_2G_8812			0xC6
#define EEPROM_TX_BBSWING_5G_8812			0xC7
#define EEPROM_TX_PWR_CALIBRATE_RATE_8812	0xC8
#define EEPROM_RF_ANTENNA_OPT_8812			0xC9
#define EEPROM_RFE_OPTION_8812				0xCA

// RTL8812AE
#define EEPROM_MAC_ADDR_8812AE				0xD0
#define EEPROM_VID_8812AE						0xD6
#define EEPROM_DID_8812AE						0xD8
#define EEPROM_SVID_8812AE						0xDA
#define EEPROM_SMID_8812AE					0xDC

//RTL8812AU
#define EEPROM_MAC_ADDR_8812AU				0xD7
#define EEPROM_VID_8812AU						0xD0
#define EEPROM_PID_8812AU						0xD2
#define EEPROM_PA_TYPE_8812AU					0xBC
#define EEPROM_LNA_TYPE_2G_8812AU			0xBD
#define EEPROM_LNA_TYPE_5G_8812AU			0xBF

//====================================================
//			EEPROM/Efuse PG Offset for 8821AE/8821AU/8821AS
//====================================================
#define EEPROM_TX_PWR_INX_8821				0x10

#define EEPROM_ChannelPlan_8821				0xB8
#define EEPROM_XTAL_8821						0xB9
#define EEPROM_THERMAL_METER_8821			0xBA
#define EEPROM_IQK_LCK_8821					0xBB


#define EEPROM_RF_BOARD_OPTION_8821			0xC1
#define EEPROM_RF_FEATURE_OPTION_8821		0xC2
#define EEPROM_RF_BT_SETTING_8821				0xC3
#define EEPROM_VERSION_8821					0xC4
#define EEPROM_CustomID_8821					0xC5
#define EEPROM_RF_ANTENNA_OPT_8821			0xC9

// RTL8821AE
#define EEPROM_MAC_ADDR_8821AE				0xD0
#define EEPROM_VID_8821AE						0xD6
#define EEPROM_DID_8821AE						0xD8
#define EEPROM_SVID_8821AE						0xDA
#define EEPROM_SMID_8821AE					0xDC

//RTL8821AU
#define EEPROM_PA_TYPE_8821AU					0xBC
#define EEPROM_LNA_TYPE_8821AU				0xBF

// RTL8821AS
#define EEPROM_MAC_ADDR_8821AS				0x11A

//RTL8821AU
#define EEPROM_MAC_ADDR_8821AU				0x107
#define EEPROM_VID_8821AU						0x100
#define EEPROM_PID_8821AU						0x102


//====================================================
//			EEPROM/Efuse PG Offset for 8192 SE/SU
//====================================================
#define EEPROM_VID_92SE						0x0A
#define EEPROM_DID_92SE						0x0C
#define EEPROM_SVID_92SE						0x0E
#define EEPROM_SMID_92SE						0x10

#define EEPROM_MAC_ADDR_92S					0x12

#define EEPROM_TSSI_A_92SE						0x74
#define EEPROM_TSSI_B_92SE						0x75

#define EEPROM_Version_92SE					0x7C


#define EEPROM_VID_92SU						0x08
#define EEPROM_PID_92SU						0x0A 

#define EEPROM_Version_92SU					0x50
#define EEPROM_TSSI_A_92SU						0x6b 
#define EEPROM_TSSI_B_92SU						0x6c 

//====================================================
//			EEPROM/Efuse PG Offset for 8723BE/8723BU/8723BS
//====================================================
// 0x10 ~ 0x63 = TX power area.
#define	EEPROM_TX_PWR_INX_8723B				0x10

#define	EEPROM_ChannelPlan_8723B				0xB8
#define	EEPROM_XTAL_8723B						0xB9
#define	EEPROM_THERMAL_METER_8723B			0xBA
#define	EEPROM_IQK_LCK_8723B					0xBB
#define	EEPROM_2G_5G_PA_TYPE_8723B			0xBC
#define	EEPROM_2G_LNA_TYPE_GAIN_SEL_8723B	0xBD
#define	EEPROM_5G_LNA_TYPE_GAIN_SEL_8723B	0xBF

#define	EEPROM_RF_BOARD_OPTION_8723B		0xC1
#define	EEPROM_FEATURE_OPTION_8723B			0xC2
#define	EEPROM_RF_BT_SETTING_8723B			0xC3
#define	EEPROM_VERSION_8723B					0xC4
#define	EEPROM_CustomID_8723B				0xC5
#define	EEPROM_TX_BBSWING_2G_8723B			0xC6
#define	EEPROM_TX_PWR_CALIBRATE_RATE_8723B	0xC8
#define	EEPROM_RF_ANTENNA_OPT_8723B		0xC9
#define	EEPROM_RFE_OPTION_8723B				0xCA

// RTL8723BE
#define EEPROM_MAC_ADDR_8723BE				0xD0
#define EEPROM_VID_8723BE						0xD6
#define EEPROM_DID_8723BE						0xD8
#define EEPROM_SVID_8723BE						0xDA
#define EEPROM_SMID_8723BE						0xDC

//RTL8723BU
#define EEPROM_MAC_ADDR_8723BU				0x107
#define EEPROM_VID_8723BU						0x100
#define EEPROM_PID_8723BU						0x102
#define EEPROM_PA_TYPE_8723BU					0xBC
#define EEPROM_LNA_TYPE_2G_8723BU				0xBD

//RTL8723BS
#define	EEPROM_MAC_ADDR_8723BS				0x11A
#define EEPROM_Voltage_ADDR_8723B			0x8


//====================================================
//			EEPROM/Efuse Value Type
//====================================================
#define EETYPE_TX_PWR							0x0
//====================================================
//			EEPROM/Efuse Default Value
//====================================================
#define EEPROM_CID_DEFAULT					0x0
#define EEPROM_CID_DEFAULT_EXT				0xFF // Reserved for Realtek
#define EEPROM_CID_TOSHIBA						0x4
#define EEPROM_CID_CCX							0x10
#define EEPROM_CID_QMI							0x0D
#define EEPROM_CID_WHQL 						0xFE

#define EEPROM_CHANNEL_PLAN_FCC				0x0
#define EEPROM_CHANNEL_PLAN_IC				0x1
#define EEPROM_CHANNEL_PLAN_ETSI				0x2
#define EEPROM_CHANNEL_PLAN_SPAIN			0x3
#define EEPROM_CHANNEL_PLAN_FRANCE			0x4
#define EEPROM_CHANNEL_PLAN_MKK				0x5
#define EEPROM_CHANNEL_PLAN_MKK1				0x6
#define EEPROM_CHANNEL_PLAN_ISRAEL			0x7
#define EEPROM_CHANNEL_PLAN_TELEC			0x8
#define EEPROM_CHANNEL_PLAN_GLOBAL_DOMAIN	0x9
#define EEPROM_CHANNEL_PLAN_WORLD_WIDE_13	0xA
#define EEPROM_CHANNEL_PLAN_NCC_TAIWAN		0xB
#define EEPROM_CHANNEL_PLAN_CHIAN			0XC
#define EEPROM_CHANNEL_PLAN_SINGAPORE_INDIA_MEXICO  0XD
#define EEPROM_CHANNEL_PLAN_KOREA			0xE
#define EEPROM_CHANNEL_PLAN_TURKEY              	0xF
#define EEPROM_CHANNEL_PLAN_JAPAN                 	0x10
#define EEPROM_CHANNEL_PLAN_FCC_NO_DFS		0x11
#define EEPROM_CHANNEL_PLAN_JAPAN_NO_DFS	0x12
#define EEPROM_CHANNEL_PLAN_WORLD_WIDE_5G	0x13
#define EEPROM_CHANNEL_PLAN_TAIWAN_NO_DFS 	0x14

#define EEPROM_USB_OPTIONAL1					0xE
#define EEPROM_CHANNEL_PLAN_BY_HW_MASK		0x80

#define RTL_EEPROM_ID							0x8129
#define EEPROM_Default_TSSI						0x0
#define EEPROM_Default_BoardType				0x02
#define EEPROM_Default_ThermalMeter			0x12
#define EEPROM_Default_ThermalMeter_92SU		0x7
#define EEPROM_Default_ThermalMeter_88E		0x18
#define EEPROM_Default_ThermalMeter_8812		0x18
#define	EEPROM_Default_ThermalMeter_8192E			0x1A
#define	EEPROM_Default_ThermalMeter_8723B		0x18


#define EEPROM_Default_CrystalCap				0x0
#define EEPROM_Default_CrystalCap_8723A		0x20
#define EEPROM_Default_CrystalCap_88E 			0x20
#define EEPROM_Default_CrystalCap_8812			0x20
#define EEPROM_Default_CrystalCap_8192E			0x20
#define EEPROM_Default_CrystalCap_8723B			0x20
#define EEPROM_Default_CrystalFreq				0x0
#define EEPROM_Default_TxPowerLevel_92C		0x22
#define EEPROM_Default_TxPowerLevel_2G			0x2C
#define EEPROM_Default_TxPowerLevel_5G			0x22
#define EEPROM_Default_TxPowerLevel			0x22
#define EEPROM_Default_HT40_2SDiff				0x0
#define EEPROM_Default_HT20_Diff				2
#define EEPROM_Default_LegacyHTTxPowerDiff		0x3
#define EEPROM_Default_LegacyHTTxPowerDiff_92C	0x3
#define EEPROM_Default_LegacyHTTxPowerDiff_92D	0x4	
#define EEPROM_Default_HT40_PwrMaxOffset		0
#define EEPROM_Default_HT20_PwrMaxOffset		0

#define EEPROM_Default_PID						0x1234
#define EEPROM_Default_VID						0x5678
#define EEPROM_Default_CustomerID				0xAB
#define EEPROM_Default_CustomerID_8188E		0x00
#define EEPROM_Default_SubCustomerID			0xCD
#define EEPROM_Default_Version					0

#define EEPROM_Default_externalPA_C9		0x00
#define EEPROM_Default_externalPA_CC		0xFF
#define EEPROM_Default_internalPA_SP3T_C9	0xAA
#define EEPROM_Default_internalPA_SP3T_CC	0xAF
#define EEPROM_Default_internalPA_SPDT_C9	0xAA
#ifdef CONFIG_PCI_HCI
#define EEPROM_Default_internalPA_SPDT_CC	0xA0
#else
#define EEPROM_Default_internalPA_SPDT_CC	0xFA
#endif
#define EEPROM_Default_PAType						0
#define EEPROM_Default_LNAType						0

//New EFUSE deafult value
#define EEPROM_DEFAULT_24G_INDEX			0x2D
#define EEPROM_DEFAULT_24G_HT20_DIFF		0X02
#define EEPROM_DEFAULT_24G_OFDM_DIFF	0X04

#define EEPROM_DEFAULT_5G_INDEX			0X2A
#define EEPROM_DEFAULT_5G_HT20_DIFF		0X00
#define EEPROM_DEFAULT_5G_OFDM_DIFF		0X04

#define EEPROM_DEFAULT_DIFF				0XFE
#define EEPROM_DEFAULT_CHANNEL_PLAN		0x7F
#define EEPROM_DEFAULT_BOARD_OPTION		0x00
#define EEPROM_DEFAULT_RFE_OPTION		0x04
#define EEPROM_DEFAULT_FEATURE_OPTION	0x00
#define EEPROM_DEFAULT_BT_OPTION			0x10


#define EEPROM_DEFAULT_TX_CALIBRATE_RATE	0x00

//
// For VHT series TX power by rate table.
// VHT TX power by rate off setArray = 
// Band:-2G&5G = 0 / 1
// RF: at most 4*4 = ABCD=0/1/2/3
// CCK=0 OFDM=1/2 HT-MCS 0-15=3/4/56 VHT=7/8/9/10/11			
//
#define TX_PWR_BY_RATE_NUM_BAND			2
#define TX_PWR_BY_RATE_NUM_RF			4
#define TX_PWR_BY_RATE_NUM_RATE			84

#define TXPWR_LMT_MAX_RF				4

//----------------------------------------------------------------------------
//       EEPROM/EFUSE data structure definition.
//----------------------------------------------------------------------------
#define MAX_RF_PATH_NUM	2
#define MAX_CHNL_GROUP		3+9
typedef struct _TxPowerInfo{
	u8 CCKIndex[MAX_RF_PATH_NUM][MAX_CHNL_GROUP];
	u8 HT40_1SIndex[MAX_RF_PATH_NUM][MAX_CHNL_GROUP];
	u8 HT40_2SIndexDiff[MAX_RF_PATH_NUM][MAX_CHNL_GROUP];
	s8 HT20IndexDiff[MAX_RF_PATH_NUM][MAX_CHNL_GROUP];
	u8 OFDMIndexDiff[MAX_RF_PATH_NUM][MAX_CHNL_GROUP];
	u8 HT40MaxOffset[MAX_RF_PATH_NUM][MAX_CHNL_GROUP];
	u8 HT20MaxOffset[MAX_RF_PATH_NUM][MAX_CHNL_GROUP];
	u8 TSSI_A[3];
	u8 TSSI_B[3];
	u8 TSSI_A_5G[3];		//5GL/5GM/5GH
	u8 TSSI_B_5G[3];
}TxPowerInfo, *PTxPowerInfo;


//For 88E new structure

/*
2.4G: 
{
{1,2},
{3,4,5},
{6,7,8},
{9,10,11},
{12,13},
{14}
}

5G:
{
{36,38,40},
{44,46,48},
{52,54,56},
{60,62,64},
{100,102,104},
{108,110,112},
{116,118,120},
{124,126,128},
{132,134,136},
{140,142,144},
{149,151,153},
{157,159,161},
{173,175,177},
}
*/
#define	MAX_RF_PATH				4
#define 	RF_PATH_MAX				MAX_RF_PATH	
#define	MAX_CHNL_GROUP_24G		6 
#define	MAX_CHNL_GROUP_5G		14 

//It must always set to 4, otherwise read efuse table secquence will be wrong.
#define 	MAX_TX_COUNT				4

typedef struct _TxPowerInfo24G{
	u8 IndexCCK_Base[MAX_RF_PATH][MAX_CHNL_GROUP_24G];
	u8 IndexBW40_Base[MAX_RF_PATH][MAX_CHNL_GROUP_24G];
	//If only one tx, only BW20 and OFDM are used.
	s8 CCK_Diff[MAX_RF_PATH][MAX_TX_COUNT];	
	s8 OFDM_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 BW20_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 BW40_Diff[MAX_RF_PATH][MAX_TX_COUNT];
}TxPowerInfo24G, *PTxPowerInfo24G;

typedef struct _TxPowerInfo5G{
	u8 IndexBW40_Base[MAX_RF_PATH][MAX_CHNL_GROUP_5G];
	//If only one tx, only BW20, OFDM, BW80 and BW160 are used.
	s8 OFDM_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 BW20_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 BW40_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 BW80_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8 BW160_Diff[MAX_RF_PATH][MAX_TX_COUNT];
}TxPowerInfo5G, *PTxPowerInfo5G;


typedef	enum _BT_Ant_NUM{
	Ant_x2	= 0,		
	Ant_x1	= 1
} BT_Ant_NUM, *PBT_Ant_NUM;

typedef	enum _BT_CoType{
	BT_2WIRE		= 0,		
	BT_ISSC_3WIRE	= 1,
	BT_ACCEL		= 2,
	BT_CSR_BC4		= 3,
	BT_CSR_BC8		= 4,
	BT_RTL8756		= 5,
	BT_RTL8723A		= 6,
	BT_RTL8821		= 7,
	BT_RTL8723B		= 8,
	BT_RTL8192E		= 9,
	BT_RTL8813A		= 10,
	BT_RTL8812A		= 11
} BT_CoType, *PBT_CoType;

typedef	enum _BT_RadioShared{
	BT_Radio_Shared 	= 0,	
	BT_Radio_Individual	= 1,
} BT_RadioShared, *PBT_RadioShared;


#endif
