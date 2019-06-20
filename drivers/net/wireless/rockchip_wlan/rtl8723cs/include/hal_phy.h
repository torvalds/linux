/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef __HAL_PHY_H__
#define __HAL_PHY_H__


#if DISABLE_BB_RF
	#define	HAL_FW_ENABLE				0
	#define	HAL_MAC_ENABLE			0
	#define	HAL_BB_ENABLE				0
	#define	HAL_RF_ENABLE				0
#else /* FPGA_PHY and ASIC */
	#define	HAL_FW_ENABLE				1
	#define	HAL_MAC_ENABLE			1
	#define	HAL_BB_ENABLE				1
	#define	HAL_RF_ENABLE				1
#endif

#define	RF6052_MAX_TX_PWR			0x3F
#define	RF6052_MAX_REG_88E			0xFF
#define	RF6052_MAX_REG_92C			0x7F

#define	RF6052_MAX_REG	\
	((RF6052_MAX_REG_88E > RF6052_MAX_REG_92C) ? RF6052_MAX_REG_88E : RF6052_MAX_REG_92C)

#define GET_RF6052_REAL_MAX_REG(_Adapter)	\
	(IS_HARDWARE_TYPE_8188E(_Adapter) ? RF6052_MAX_REG_88E : RF6052_MAX_REG_92C)

#define	RF6052_MAX_PATH				2

/*
 * Antenna detection method, i.e., using single tone detection or RSSI reported from each antenna detected.
 * Added by Roger, 2013.05.22.
 *   */
#define ANT_DETECT_BY_SINGLE_TONE	BIT0
#define ANT_DETECT_BY_RSSI				BIT1
#define IS_ANT_DETECT_SUPPORT_SINGLE_TONE(__Adapter)		((GET_HAL_DATA(__Adapter)->AntDetection) & ANT_DETECT_BY_SINGLE_TONE)
#define IS_ANT_DETECT_SUPPORT_RSSI(__Adapter)		((GET_HAL_DATA(__Adapter)->AntDetection) & ANT_DETECT_BY_RSSI)


/*--------------------------Define Parameters-------------------------------*/
typedef	enum _RF_CHIP {
	RF_CHIP_MIN = 0,	/* 0 */
	RF_8225 = 1,			/* 1 11b/g RF for verification only */
	RF_8256 = 2,			/* 2 11b/g/n */
	RF_8258 = 3,			/* 3 11a/b/g/n RF */
	RF_6052 = 4,			/* 4 11b/g/n RF */
	RF_PSEUDO_11N = 5,	/* 5, It is a temporality RF. */
	RF_CHIP_MAX
} RF_CHIP_E, *PRF_CHIP_E;

typedef enum _ANTENNA_PATH {
	ANTENNA_NONE	= 0,
	ANTENNA_D		= 1,
	ANTENNA_C		= 2,
	ANTENNA_CD	= 3,
	ANTENNA_B		= 4,
	ANTENNA_BD	= 5,
	ANTENNA_BC	= 6,
	ANTENNA_BCD	= 7,
	ANTENNA_A		= 8,
	ANTENNA_AD	= 9,
	ANTENNA_AC	= 10,
	ANTENNA_ACD	= 11,
	ANTENNA_AB	= 12,
	ANTENNA_ABD	= 13,
	ANTENNA_ABC	= 14,
	ANTENNA_ABCD	= 15
} ANTENNA_PATH;

typedef enum _RF_CONTENT {
	radioa_txt = 0x1000,
	radiob_txt = 0x1001,
	radioc_txt = 0x1002,
	radiod_txt = 0x1003
} RF_CONTENT;

typedef enum _BaseBand_Config_Type {
	BaseBand_Config_PHY_REG = 0,			/* Radio Path A */
	BaseBand_Config_AGC_TAB = 1,			/* Radio Path B */
	BaseBand_Config_AGC_TAB_2G = 2,
	BaseBand_Config_AGC_TAB_5G = 3,
	BaseBand_Config_PHY_REG_PG
} BaseBand_Config_Type, *PBaseBand_Config_Type;

typedef enum _HW_BLOCK {
	HW_BLOCK_MAC = 0,
	HW_BLOCK_PHY0 = 1,
	HW_BLOCK_PHY1 = 2,
	HW_BLOCK_RF = 3,
	HW_BLOCK_MAXIMUM = 4, /* Never use this */
} HW_BLOCK_E, *PHW_BLOCK_E;

typedef enum _WIRELESS_MODE {
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
} WIRELESS_MODE;

typedef enum _SwChnlCmdID {
	CmdID_End,
	CmdID_SetTxPowerLevel,
	CmdID_BBRegWrite10,
	CmdID_WritePortUlong,
	CmdID_WritePortUshort,
	CmdID_WritePortUchar,
	CmdID_RF_WriteReg,
} SwChnlCmdID;

typedef struct _SwChnlCmd {
	SwChnlCmdID	CmdID;
	u32				Para1;
	u32				Para2;
	u32				msDelay;
} SwChnlCmd;

typedef struct _R_ANTENNA_SELECT_OFDM {
	u32			r_tx_antenna:4;
	u32			r_ant_l:4;
	u32			r_ant_non_ht:4;
	u32			r_ant_ht1:4;
	u32			r_ant_ht2:4;
	u32			r_ant_ht_s1:4;
	u32			r_ant_non_ht_s1:4;
	u32			OFDM_TXSC:2;
	u32			Reserved:2;
} R_ANTENNA_SELECT_OFDM;

typedef struct _R_ANTENNA_SELECT_CCK {
	u8			r_cckrx_enable_2:2;
	u8			r_cckrx_enable:2;
	u8			r_ccktx_enable:4;
} R_ANTENNA_SELECT_CCK;


/*--------------------------Exported Function prototype---------------------*/
u32
PHY_CalculateBitShift(
	u32 BitMask
);

#ifdef CONFIG_RF_SHADOW_RW
typedef struct RF_Shadow_Compare_Map {
	/* Shadow register value */
	u32		Value;
	/* Compare or not flag */
	u8		Compare;
	/* Record If it had ever modified unpredicted */
	u8		ErrorOrNot;
	/* Recorver Flag */
	u8		Recorver;
	/*  */
	u8		Driver_Write;
} RF_SHADOW_T;

u32
PHY_RFShadowRead(
		PADAPTER		Adapter,
		enum rf_path		eRFPath,
		u32				Offset);

void
PHY_RFShadowWrite(
		PADAPTER		Adapter,
		enum rf_path		eRFPath,
		u32				Offset,
		u32				Data);

BOOLEAN
PHY_RFShadowCompare(
		PADAPTER		Adapter,
		enum rf_path		eRFPath,
		u32				Offset);

void
PHY_RFShadowRecorver(
		PADAPTER		Adapter,
		enum rf_path		eRFPath,
		u32				Offset);

void
PHY_RFShadowCompareAll(
		PADAPTER		Adapter);

void
PHY_RFShadowRecorverAll(
		PADAPTER		Adapter);

void
PHY_RFShadowCompareFlagSet(
		PADAPTER		Adapter,
		enum rf_path		eRFPath,
		u32				Offset,
		u8				Type);

void
PHY_RFShadowRecorverFlagSet(
		PADAPTER		Adapter,
		enum rf_path		eRFPath,
		u32				Offset,
		u8				Type);

void
PHY_RFShadowCompareFlagSetAll(
		PADAPTER		Adapter);

void
PHY_RFShadowRecorverFlagSetAll(
		PADAPTER		Adapter);

void
PHY_RFShadowRefresh(
		PADAPTER		Adapter);
#endif /*#CONFIG_RF_SHADOW_RW*/
#endif /* __HAL_COMMON_H__ */
