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
#ifndef __HAL_PHY_H__
#define __HAL_PHY_H__


#if DISABLE_BB_RF
#define	HAL_FW_ENABLE				0
#define	HAL_MAC_ENABLE			0
#define	HAL_BB_ENABLE				0
#define	HAL_RF_ENABLE				0
#else /*  FPGA_PHY and ASIC */
#define		HAL_FW_ENABLE				1
#define	HAL_MAC_ENABLE			1
#define	HAL_BB_ENABLE				1
#define	HAL_RF_ENABLE				1
#endif

#define	RF6052_MAX_TX_PWR			0x3F
#define	RF6052_MAX_REG_88E			0xFF
#define	RF6052_MAX_REG_92C			0x7F

#define	RF6052_MAX_REG	\
		(RF6052_MAX_REG_88E > RF6052_MAX_REG_92C) ? RF6052_MAX_REG_88E: RF6052_MAX_REG_92C

#define GET_RF6052_REAL_MAX_REG(_Adapter)	RF6052_MAX_REG_92C

#define	RF6052_MAX_PATH				2

/*  */
/*  Antenna detection method, i.e., using single tone detection or RSSI reported from each antenna detected. */
/*  Added by Roger, 2013.05.22. */
/*  */
#define ANT_DETECT_BY_SINGLE_TONE	BIT0
#define ANT_DETECT_BY_RSSI				BIT1
#define IS_ANT_DETECT_SUPPORT_SINGLE_TONE(__Adapter)		((GET_HAL_DATA(__Adapter)->AntDetection) & ANT_DETECT_BY_SINGLE_TONE)
#define IS_ANT_DETECT_SUPPORT_RSSI(__Adapter)		((GET_HAL_DATA(__Adapter)->AntDetection) & ANT_DETECT_BY_RSSI)


/*--------------------------Define Parameters-------------------------------*/
enum BAND_TYPE {
	BAND_ON_2_4G = 0,
	BAND_ON_5G,
	BAND_ON_BOTH,
	BANDMAX
};

enum RF_TYPE {
	RF_TYPE_MIN = 0,	/*  0 */
	RF_8225 = 1,		/*  1 11b/g RF for verification only */
	RF_8256 = 2,		/*  2 11b/g/n */
	RF_8258 = 3,		/*  3 11a/b/g/n RF */
	RF_6052 = 4,		/*  4 11b/g/n RF */
	RF_PSEUDO_11N = 5,	/*  5, It is a temporality RF. */
	RF_TYPE_MAX
};

enum RF_PATH {
	RF_PATH_A = 0,
	RF_PATH_B,
	RF_PATH_C,
	RF_PATH_D
};

#define	TX_1S			0
#define	TX_2S			1
#define	TX_3S			2
#define	TX_4S			3

#define	RF_PATH_MAX_92C_88E		2
#define	RF_PATH_MAX_90_8812		4	/* Max RF number 90 support */

enum ANTENNA_PATH {
       ANTENNA_NONE	= 0,
	ANTENNA_D	= 1,
	ANTENNA_C	= 2,
	ANTENNA_CD	= 3,
	ANTENNA_B	= 4,
	ANTENNA_BD	= 5,
	ANTENNA_BC	= 6,
	ANTENNA_BCD	= 7,
	ANTENNA_A	= 8,
	ANTENNA_AD	= 9,
	ANTENNA_AC	= 10,
	ANTENNA_ACD	= 11,
	ANTENNA_AB	= 12,
	ANTENNA_ABD	= 13,
	ANTENNA_ABC	= 14,
	ANTENNA_ABCD	= 15
};

enum RF_CONTENT {
	radioa_txt = 0x1000,
	radiob_txt = 0x1001,
	radioc_txt = 0x1002,
	radiod_txt = 0x1003
};

enum BaseBand_Config_Type {
	BaseBand_Config_PHY_REG = 0,			/* Radio Path A */
	BaseBand_Config_AGC_TAB = 1,			/* Radio Path B */
	BaseBand_Config_AGC_TAB_2G = 2,
	BaseBand_Config_AGC_TAB_5G = 3,
	BaseBand_Config_PHY_REG_PG
};

enum HW_BLOCK {
	HW_BLOCK_MAC = 0,
	HW_BLOCK_PHY0 = 1,
	HW_BLOCK_PHY1 = 2,
	HW_BLOCK_RF = 3,
	HW_BLOCK_MAXIMUM = 4, /*  Never use this */
};

enum WIRELESS_MODE {
	WIRELESS_MODE_UNKNOWN = 0x00,
	WIRELESS_MODE_A = 0x01,
	WIRELESS_MODE_B = 0x02,
	WIRELESS_MODE_G = 0x04,
	WIRELESS_MODE_AUTO = 0x08,
	WIRELESS_MODE_N_24G = 0x10,
	WIRELESS_MODE_N_5G = 0x20,
	WIRELESS_MODE_AC_5G = 0x40,
	WIRELESS_MODE_AC_24G  = 0x80,
	WIRELESS_MODE_AC_ONLY  = 0x100,
};

enum SwChnlCmdID {
	CmdID_End,
	CmdID_SetTxPowerLevel,
	CmdID_BBRegWrite10,
	CmdID_WritePortUlong,
	CmdID_WritePortUshort,
	CmdID_WritePortUchar,
	CmdID_RF_WriteReg,
};

struct SwChnlCmd {
	enum SwChnlCmdID	CmdID;
	u32 			Para1;
	u32 			Para2;
	u32 			msDelay;
};

struct R_ANTENNA_SELECT_OFDM {
#ifdef __LITTLE_ENDIAN
	u32 		r_tx_antenna:4;
	u32 		r_ant_l:4;
	u32 		r_ant_non_ht:4;
	u32 		r_ant_ht1:4;
	u32 		r_ant_ht2:4;
	u32 		r_ant_ht_s1:4;
	u32 		r_ant_non_ht_s1:4;
	u32 		OFDM_TXSC:2;
	u32 		Reserved:2;
#else
	u32 		Reserved:2;
	u32 		OFDM_TXSC:2;
	u32 		r_ant_non_ht_s1:4;
	u32 		r_ant_ht_s1:4;
	u32 		r_ant_ht2:4;
	u32 		r_ant_ht1:4;
	u32 		r_ant_non_ht:4;
	u32 		r_ant_l:4;
	u32 		r_tx_antenna:4;
#endif
};

/*--------------------------Exported Function prototype---------------------*/

#endif /* __HAL_COMMON_H__ */
