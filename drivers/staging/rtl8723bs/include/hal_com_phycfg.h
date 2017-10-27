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
#ifndef __HAL_COM_PHYCFG_H__
#define __HAL_COM_PHYCFG_H__

#define		PathA		0x0	/*  Useless */
#define		PathB		0x1
#define		PathC		0x2
#define		PathD		0x3

enum RATE_SECTION {
	CCK = 0,
	OFDM,
	HT_MCS0_MCS7,
	HT_MCS8_MCS15,
	HT_MCS16_MCS23,
	HT_MCS24_MCS31,
	VHT_1SSMCS0_1SSMCS9,
	VHT_2SSMCS0_2SSMCS9,
	VHT_3SSMCS0_3SSMCS9,
	VHT_4SSMCS0_4SSMCS9,
};

enum RF_TX_NUM {
	RF_1TX = 0,
	RF_2TX,
	RF_3TX,
	RF_4TX,
	RF_MAX_TX_NUM,
	RF_TX_NUM_NONIMPLEMENT,
};

#define MAX_POWER_INDEX			0x3F

enum _REGULATION_TXPWR_LMT {
	TXPWR_LMT_FCC = 0,
	TXPWR_LMT_MKK,
	TXPWR_LMT_ETSI,
	TXPWR_LMT_WW,
	TXPWR_LMT_MAX_REGULATION_NUM,
};

/*------------------------------Define structure----------------------------*/
struct bb_register_def {
	u32 rfintfs;			/*  set software control: */
					/* 	0x870~0x877[8 bytes] */

	u32 rfintfo;			/*  output data: */
					/* 	0x860~0x86f [16 bytes] */

	u32 rfintfe;			/*  output enable: */
					/* 	0x860~0x86f [16 bytes] */

	u32 rf3wireOffset;		/*  LSSI data: */
					/* 	0x840~0x84f [16 bytes] */

	u32 rfHSSIPara2;		/*  wire parameter control2 : */
					/* 	0x824~0x827, 0x82c~0x82f,
					 *	0x834~0x837, 0x83c~0x83f
					 */
	u32 rfLSSIReadBack;		/* LSSI RF readback data SI mode */
					/* 	0x8a0~0x8af [16 bytes] */

	u32 rfLSSIReadBackPi;		/* LSSI RF readback data PI mode
					 *	0x8b8-8bc for Path A and B */

};

u8
PHY_GetTxPowerByRateBase(
struct adapter *	Adapter,
u8 		Band,
u8 		RfPath,
u8 		TxNum,
enum RATE_SECTION	RateSection
	);

u8
PHY_GetRateSectionIndexOfTxPowerByRate(
struct adapter *padapter,
u32 		RegAddr,
u32 		BitMask
	);

void
PHY_GetRateValuesOfTxPowerByRate(
struct adapter *padapter,
u32 		RegAddr,
u32 		BitMask,
u32 		Value,
	u8*		RateIndex,
	s8*		PwrByRateVal,
	u8*		RateNum
	);

u8
PHY_GetRateIndexOfTxPowerByRate(
u8 Rate
	);

void
PHY_SetTxPowerIndexByRateSection(
struct adapter *	padapter,
u8 		RFPath,
u8 		Channel,
u8 		RateSection
	);

s8
PHY_GetTxPowerByRate(
struct adapter *padapter,
u8 	Band,
u8 	RFPath,
u8 	TxNum,
u8 	RateIndex
	);

void
PHY_SetTxPowerByRate(
struct adapter *padapter,
u8 	Band,
u8 	RFPath,
u8 	TxNum,
u8 	Rate,
s8			Value
	);

void
PHY_SetTxPowerLevelByPath(
struct adapter *Adapter,
u8 	channel,
u8 	path
	);

void
PHY_SetTxPowerIndexByRateArray(
struct adapter *	padapter,
u8 		RFPath,
enum CHANNEL_WIDTH	BandWidth,
u8 		Channel,
u8*			Rates,
u8 		RateArraySize
	);

void
PHY_InitTxPowerByRate(
struct adapter *padapter
	);

void
PHY_StoreTxPowerByRate(
struct adapter *padapter,
u32 		Band,
u32 		RfPath,
u32 		TxNum,
u32 		RegAddr,
u32 		BitMask,
u32 		Data
	);

void
PHY_TxPowerByRateConfiguration(
	struct adapter *		padapter
	);

u8
PHY_GetTxPowerIndexBase(
struct adapter *	padapter,
u8 		RFPath,
u8 		Rate,
enum CHANNEL_WIDTH	BandWidth,
u8 		Channel,
	bool		*bIn24G
	);

s8 PHY_GetTxPowerLimit (struct adapter *adapter, u32 RegPwrTblSel,
			enum BAND_TYPE Band, enum CHANNEL_WIDTH Bandwidth,
u8 		RfPath,
u8 		DataRate,
u8 		Channel
	);

void
PHY_SetTxPowerLimit(
struct adapter *		Adapter,
u8 			*Regulation,
u8 			*Band,
u8 			*Bandwidth,
u8 			*RateSection,
u8 			*RfPath,
u8 			*Channel,
u8 			*PowerLimit
	);

void
PHY_ConvertTxPowerLimitToPowerIndex(
struct adapter *		Adapter
	);

void
PHY_InitTxPowerLimit(
struct adapter *		Adapter
	);

s8
PHY_GetTxPowerTrackingOffset(
	struct adapter *padapter,
	u8 	Rate,
	u8 	RFPath
	);

u8
PHY_GetTxPowerIndex(
struct adapter *		padapter,
u8 			RFPath,
u8 			Rate,
enum CHANNEL_WIDTH		BandWidth,
u8 			Channel
	);

void
PHY_SetTxPowerIndex(
struct adapter *	padapter,
u32 			PowerIndex,
u8 		RFPath,
u8 		Rate
	);

void
Hal_ChannelPlanToRegulation(
struct adapter *	Adapter,
u16 			ChannelPlan
	);

#define MAX_PARA_FILE_BUF_LEN	25600

#define LOAD_MAC_PARA_FILE				BIT0
#define LOAD_BB_PARA_FILE					BIT1
#define LOAD_BB_PG_PARA_FILE				BIT2
#define LOAD_BB_MP_PARA_FILE				BIT3
#define LOAD_RF_PARA_FILE					BIT4
#define LOAD_RF_TXPWR_TRACK_PARA_FILE	BIT5
#define LOAD_RF_TXPWR_LMT_PARA_FILE		BIT6

int phy_ConfigMACWithParaFile(struct adapter *Adapter, char*pFileName);

int phy_ConfigBBWithParaFile(struct adapter *Adapter, char*pFileName, u32 ConfigType);

int phy_ConfigBBWithPgParaFile(struct adapter *Adapter, char*pFileName);

int phy_ConfigBBWithMpParaFile(struct adapter *Adapter, char*pFileName);

int PHY_ConfigRFWithParaFile(struct adapter *Adapter, char*pFileName, u8 eRFPath);

int PHY_ConfigRFWithTxPwrTrackParaFile(struct adapter *Adapter, char*pFileName);

int PHY_ConfigRFWithPowerLimitTableParaFile(struct adapter *Adapter, char*pFileName);

void phy_free_filebuf(struct adapter *padapter);

#endif /* __HAL_COMMON_H__ */
