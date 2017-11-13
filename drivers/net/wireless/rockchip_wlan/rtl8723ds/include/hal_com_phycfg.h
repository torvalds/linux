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
	IN	RATE_SECTION	RateSection
);

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
phy_set_tx_power_index_by_rate_section(
	IN	PADAPTER		pAdapter,
	IN	enum rf_path		RFPath,
	IN	u8				Channel,
	IN	u8				RateSection
);

s8
_PHY_GetTxPowerByRate(
	IN	PADAPTER	pAdapter,
	IN	u8			Band,
	IN	enum rf_path	RFPath,
	IN	u8			RateIndex
);

s8
PHY_GetTxPowerByRate(
	IN	PADAPTER	pAdapter,
	IN	u8			Band,
	IN	enum rf_path	RFPath,
	IN	u8			RateIndex
);

VOID
PHY_SetTxPowerByRate(
	IN	PADAPTER	pAdapter,
	IN	u8			Band,
	IN	enum rf_path	RFPath,
	IN	u8			Rate,
	IN	s8			Value
);

VOID
phy_set_tx_power_level_by_path(
	IN	PADAPTER	Adapter,
	IN	u8			channel,
	IN	u8			path
);

VOID
PHY_SetTxPowerIndexByRateArray(
	IN	PADAPTER		pAdapter,
	IN	enum rf_path		RFPath,
	IN	enum channel_width BandWidth,
	IN	u8				Channel,
	IN	u8				*Rates,
	IN	u8				RateArraySize
);

VOID
PHY_InitTxPowerByRate(
	IN	PADAPTER	pAdapter
);

VOID
phy_store_tx_power_by_rate(
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
	IN	enum rf_path		RFPath,
	IN	u8				Rate,
	u8 ntx_idx,
	IN	enum channel_width	BandWidth,
	IN	u8				Channel,
	OUT PBOOLEAN		bIn24G
);

#ifdef CONFIG_TXPWR_LIMIT
s8 phy_get_txpwr_lmt_abs(_adapter *adapter
	, const char *regd_name
	, BAND_TYPE band, enum channel_width bw
	, u8 tlrs, u8 ntx_idx, u8 cch, u8 lock
);

s8 phy_get_txpwr_lmt(_adapter *adapter
	, const char *regd_name
	, BAND_TYPE band, enum channel_width bw
	, u8 rfpath, u8 rs, u8 ntx_idx, u8 cch, u8 lock
);

s8 PHY_GetTxPowerLimit(_adapter *adapter
	, const char *regd_name
	, BAND_TYPE band, enum channel_width bw
	, u8 rfpath, u8 rate, u8 ntx_idx, u8 cch
);
#else
#define phy_get_txpwr_lmt_abs(adapter, regd_name, band, bw, tlrs, ntx_idx, cch, lock) MAX_POWER_INDEX
#define phy_get_txpwr_lmt(adapter, regd_name, band, bw, rfpath, rs, ntx_idx, cch, lock) MAX_POWER_INDEX
#define PHY_GetTxPowerLimit(adapter, regd_name, band, bw, rfpath, rate, ntx_idx, cch) MAX_POWER_INDEX
#endif /* CONFIG_TXPWR_LIMIT */

s8
PHY_GetTxPowerTrackingOffset(
	PADAPTER	pAdapter,
	enum rf_path	RFPath,
	u8			Rate
);

struct txpwr_idx_comp {
	u8 ntx_idx;
	u8 base;
	s8 by_rate;
	s8 limit;
	s8 tpt;
	s8 ebias;
};

u8
phy_get_tx_power_index(
	IN	PADAPTER			pAdapter,
	IN	enum rf_path			RFPath,
	IN	u8					Rate,
	IN	enum channel_width	BandWidth,
	IN	u8					Channel
);

VOID
PHY_SetTxPowerIndex(
	IN	PADAPTER		pAdapter,
	IN	u32				PowerIndex,
	IN	enum rf_path		RFPath,
	IN	u8				Rate
);

void dump_tx_power_idx_title(void *sel, _adapter *adapter);
void dump_tx_power_idx_by_path_rs(void *sel, _adapter *adapter, u8 rfpath, u8 rs);
void dump_tx_power_idx(void *sel, _adapter *adapter);

bool phy_is_tx_power_limit_needed(_adapter *adapter);
bool phy_is_tx_power_by_rate_needed(_adapter *adapter);
int phy_load_tx_power_by_rate(_adapter *adapter, u8 chk_file);
#ifdef CONFIG_TXPWR_LIMIT
int phy_load_tx_power_limit(_adapter *adapter, u8 chk_file);
#endif
void phy_load_tx_power_ext_info(_adapter *adapter, u8 chk_file);
void phy_reload_tx_power_ext_info(_adapter *adapter);
void phy_reload_default_tx_power_ext_info(_adapter *adapter);

const struct map_t *hal_pg_txpwr_def_info(_adapter *adapter);

void dump_hal_txpwr_info_2g(void *sel, _adapter *adapter, u8 rfpath_num, u8 max_tx_cnt);
void dump_hal_txpwr_info_5g(void *sel, _adapter *adapter, u8 rfpath_num, u8 max_tx_cnt);

void hal_load_txpwr_info(
	_adapter *adapter,
	TxPowerInfo24G *pwr_info_2g,
	TxPowerInfo5G *pwr_info_5g,
	u8 *pg_data
);

void dump_tx_power_ext_info(void *sel, _adapter *adapter);
void dump_target_tx_power(void *sel, _adapter *adapter);
void dump_tx_power_by_rate(void *sel, _adapter *adapter);

int rtw_get_phy_file_path(_adapter *adapter, const char *file_name);

#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
#define MAC_FILE_FW_NIC			"FW_NIC.bin"
#define MAC_FILE_FW_WW_IMG		"FW_WoWLAN.bin"
#define PHY_FILE_MAC_REG		"MAC_REG.txt"

#define PHY_FILE_AGC_TAB		"AGC_TAB.txt"
#define PHY_FILE_PHY_REG		"PHY_REG.txt"
#define PHY_FILE_PHY_REG_MP		"PHY_REG_MP.txt"
#define PHY_FILE_PHY_REG_PG		"PHY_REG_PG.txt"

#define PHY_FILE_RADIO_A		"RadioA.txt"
#define PHY_FILE_RADIO_B		"RadioB.txt"
#define PHY_FILE_RADIO_C		"RadioC.txt"
#define PHY_FILE_RADIO_D		"RadioD.txt"
#define PHY_FILE_TXPWR_TRACK	"TxPowerTrack.txt"
#define PHY_FILE_TXPWR_LMT		"TXPWR_LMT.txt"

#define PHY_FILE_WIFI_ANT_ISOLATION	"wifi_ant_isolation.txt"

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
int PHY_ConfigRFWithParaFile(IN	PADAPTER	Adapter, IN char	*pFileName, IN enum rf_path	eRFPath);
int PHY_ConfigRFWithTxPwrTrackParaFile(IN PADAPTER	Adapter, IN char	*pFileName);
#ifdef CONFIG_TXPWR_LIMIT
int PHY_ConfigRFWithPowerLimitTableParaFile(IN PADAPTER	Adapter, IN const char *pFileName);
#endif
void phy_free_filebuf_mask(_adapter *padapter, u8 mask);
void phy_free_filebuf(_adapter *padapter);
#endif /* CONFIG_LOAD_PHY_PARA_FROM_FILE */

#endif /* __HAL_COMMON_H__ */
