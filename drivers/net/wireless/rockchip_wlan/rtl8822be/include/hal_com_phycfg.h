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
#ifndef __HAL_COM_PHYCFG_H__
#define __HAL_COM_PHYCFG_H__

#define		PathA                     			0x0	/* Useless */
#define		PathB			0x1
#define		PathC			0x2
#define		PathD			0x3

typedef enum _RF_TX_NUM {
	RF_1TX = 0,
	RF_2TX,
	RF_3TX,
	RF_4TX,
	RF_MAX_TX_NUM,
	RF_TX_NUM_NONIMPLEMENT,
} RF_TX_NUM;

#define MAX_POWER_INDEX		0x3F

typedef enum _REGULATION_TXPWR_LMT {
	TXPWR_LMT_FCC = 0,
	TXPWR_LMT_MKK = 1,
	TXPWR_LMT_ETSI = 2,
	TXPWR_LMT_WW = 3,

	TXPWR_LMT_MAX_REGULATION_NUM = 4
} REGULATION_TXPWR_LMT;

#define TX_PWR_LMT_REF_VHT_FROM_HT	BIT0
#define TX_PWR_LMT_REF_HT_FROM_VHT	BIT1

/*------------------------------Define structure----------------------------*/
typedef struct _BB_REGISTER_DEFINITION {
	u32 rfintfs;			/* set software control: */
	/*		0x870~0x877[8 bytes] */

	u32 rfintfo; 			/* output data: */
	/*		0x860~0x86f [16 bytes] */

	u32 rfintfe; 			/* output enable: */
	/*		0x860~0x86f [16 bytes] */

	u32 rf3wireOffset;	/* LSSI data: */
	/*		0x840~0x84f [16 bytes] */

	u32 rfHSSIPara2;	/* wire parameter control2 :  */
	/*		0x824~0x827,0x82c~0x82f, 0x834~0x837, 0x83c~0x83f [16 bytes] */

	u32 rfLSSIReadBack;	/* LSSI RF readback data SI mode */
	/*		0x8a0~0x8af [16 bytes] */

	u32 rfLSSIReadBackPi;	/* LSSI RF readback data PI mode 0x8b8-8bc for Path A and B */

} BB_REGISTER_DEFINITION_T, *PBB_REGISTER_DEFINITION_T;


/* ---------------------------------------------------------------------- */
u8
PHY_GetTxPowerByRateBase(
	IN	PADAPTER		Adapter,
	IN	u8				Band,
	IN	u8				RfPath,
	IN	u8				TxNum,
	IN	RATE_SECTION	RateSection
);

#ifdef TX_POWER_BY_RATE_OLD
u8
PHY_GetRateSectionIndexOfTxPowerByRate(
	IN	PADAPTER	pAdapter,
	IN	u32			RegAddr,
	IN	u32			BitMask
);
#endif /* TX_POWER_BY_RATE_OLD */

VOID
PHY_GetRateValuesOfTxPowerByRate(
	IN	PADAPTER pAdapter,
	IN	u32 RegAddr,
	IN	u32 BitMask,
	IN	u32 Value,
	OUT	u8 *Rate,
	OUT	s8 *PwrByRateVal,
	OUT	u8 *RateNum
);

u8
PHY_GetRateIndexOfTxPowerByRate(
	IN	u8	Rate
);

VOID
PHY_SetTxPowerIndexByRateSection(
	IN	PADAPTER		pAdapter,
	IN	u8				RFPath,
	IN	u8				Channel,
	IN	u8				RateSection
);

s8
_PHY_GetTxPowerByRate(
	IN	PADAPTER	pAdapter,
	IN	u8			Band,
	IN	u8			RFPath,
	IN	u8			TxNum,
	IN	u8			RateIndex
);

s8
PHY_GetTxPowerByRate(
	IN	PADAPTER	pAdapter,
	IN	u8			Band,
	IN	u8			RFPath,
	IN	u8			TxNum,
	IN	u8			RateIndex
);

#ifdef CONFIG_PHYDM_POWERTRACK_BY_TSSI
s8
PHY_GetTxPowerByRateOriginal(
	IN	PADAPTER	pAdapter,
	IN	u8			Band,
	IN	u8			RFPath,
	IN	u8			TxNum,
	IN	u8			Rate
);
#endif

VOID
PHY_SetTxPowerByRate(
	IN	PADAPTER	pAdapter,
	IN	u8			Band,
	IN	u8			RFPath,
	IN	u8			TxNum,
	IN	u8			Rate,
	IN	s8			Value
);

VOID
PHY_SetTxPowerLevelByPath(
	IN	PADAPTER	Adapter,
	IN	u8			channel,
	IN	u8			path
);

VOID
PHY_SetTxPowerIndexByRateArray(
	IN	PADAPTER		pAdapter,
	IN	u8				RFPath,
	IN	CHANNEL_WIDTH	BandWidth,
	IN	u8				Channel,
	IN	u8				*Rates,
	IN	u8				RateArraySize
);

VOID
PHY_InitTxPowerByRate(
	IN	PADAPTER	pAdapter
);

VOID
PHY_StoreTxPowerByRate(
	IN	PADAPTER	pAdapter,
	IN	u32			Band,
	IN	u32			RfPath,
	IN	u32			TxNum,
	IN	u32			RegAddr,
	IN	u32			BitMask,
	IN	u32			Data
);

VOID
PHY_TxPowerByRateConfiguration(
	IN  PADAPTER			pAdapter
);

u8
PHY_GetTxPowerIndexBase(
	IN	PADAPTER		pAdapter,
	IN	u8				RFPath,
	IN	u8				Rate,
	IN	CHANNEL_WIDTH	BandWidth,
	IN	u8				Channel,
	OUT PBOOLEAN		bIn24G
);

s8
PHY_GetTxPowerLimit(
	IN	PADAPTER		Adapter,
	IN	u32				RegPwrTblSel,
	IN	BAND_TYPE		Band,
	IN	CHANNEL_WIDTH	Bandwidth,
	IN	u8				RfPath,
	IN	u8				DataRate,
	IN	u8				Channel
);

#ifdef CONFIG_PHYDM_POWERTRACK_BY_TSSI
s8
PHY_GetTxPowerLimitOriginal(
	IN	PADAPTER		Adapter,
	IN	u32				RegPwrTblSel,
	IN	BAND_TYPE		Band,
	IN	CHANNEL_WIDTH	Bandwidth,
	IN	u8				RfPath,
	IN	u8				DataRate,
	IN	u8				Channel
);
#endif

VOID
PHY_ConvertTxPowerLimitToPowerIndex(
	IN	PADAPTER			Adapter
);

VOID
PHY_InitTxPowerLimit(
	IN	PADAPTER			Adapter
);

s8
PHY_GetTxPowerTrackingOffset(
	PADAPTER	pAdapter,
	u8			Rate,
	u8			RFPath
);

u8
PHY_GetTxPowerIndex(
	IN	PADAPTER			pAdapter,
	IN	u8					RFPath,
	IN	u8					Rate,
	IN	CHANNEL_WIDTH		BandWidth,
	IN	u8					Channel
);

VOID
PHY_SetTxPowerIndex(
	IN	PADAPTER		pAdapter,
	IN	u32				PowerIndex,
	IN	u8				RFPath,
	IN	u8				Rate
);

bool phy_is_tx_power_limit_needed(_adapter *adapter);
bool phy_is_tx_power_by_rate_needed(_adapter *adapter);
int phy_load_tx_power_by_rate(_adapter *adapter, const char *hal_file_name, u8 force);
int phy_load_tx_power_limit(_adapter *adapter, const char *hal_file_name, u8 force);
void phy_load_tx_power_ext_info(_adapter *adapter, u8 chk_file, u8 force);
void phy_reload_tx_power_ext_info(_adapter *adapter);
void phy_reload_default_tx_power_ext_info(_adapter *adapter);

void dump_tx_power_ext_info(void *sel, _adapter *adapter);
void dump_target_tx_power(void *sel, _adapter *adapter);
void dump_tx_power_by_rate(void *sel, _adapter *adapter);
void dump_tx_power_limit(void *sel, _adapter *adapter);

int rtw_is_phy_file_readable(const char *hal_file_name);

#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	#define MAX_PARA_FILE_BUF_LEN	25600

	#define LOAD_MAC_PARA_FILE				BIT0
	#define LOAD_BB_PARA_FILE					BIT1
	#define LOAD_BB_PG_PARA_FILE				BIT2
	#define LOAD_BB_MP_PARA_FILE				BIT3
	#define LOAD_RF_PARA_FILE					BIT4
	#define LOAD_RF_TXPWR_TRACK_PARA_FILE	BIT5
	#define LOAD_RF_TXPWR_LMT_PARA_FILE		BIT6

	int phy_ConfigMACWithParaFile(IN PADAPTER	Adapter, IN char	*pFileName);

	int phy_ConfigBBWithParaFile(IN PADAPTER	Adapter, IN char	*pFileName, IN u32	ConfigType);

	int phy_ConfigBBWithPgParaFile(IN PADAPTER	Adapter, IN const char *pFileName);

	int phy_ConfigBBWithMpParaFile(IN PADAPTER	Adapter, IN char	*pFileName);

	int PHY_ConfigRFWithParaFile(IN	PADAPTER	Adapter, IN char	*pFileName, IN u8	eRFPath);

	int PHY_ConfigRFWithTxPwrTrackParaFile(IN PADAPTER	Adapter, IN char	*pFileName);

	int PHY_ConfigRFWithPowerLimitTableParaFile(IN PADAPTER	Adapter, IN const char *pFileName);

	void phy_free_filebuf_mask(_adapter *padapter, u8 mask);
	void phy_free_filebuf(_adapter *padapter);
#endif /* CONFIG_LOAD_PHY_PARA_FROM_FILE */

#endif /* __HAL_COMMON_H__ */
