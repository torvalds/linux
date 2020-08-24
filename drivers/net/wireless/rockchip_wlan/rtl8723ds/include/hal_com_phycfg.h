/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef DBG_TX_POWER_IDX
#define DBG_TX_POWER_IDX 0
#endif

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

enum txpwr_pg_mode {
	TXPWR_PG_WITH_PWR_IDX,
	TXPWR_PG_WITH_TSSI_OFFSET,
	TXPWR_PG_UNKNOWN, /* keep last */
};

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

extern const char *const _txpwr_pg_mode_str[];
#define txpwr_pg_mode_str(_mode) (((_mode) >= TXPWR_PG_UNKNOWN) ? _txpwr_pg_mode_str[TXPWR_PG_UNKNOWN] : _txpwr_pg_mode_str[(_mode)])

u8 phy_get_target_txpwr(
		PADAPTER		Adapter,
		u8				Band,
		u8				RfPath,
		RATE_SECTION	RateSection
);

void
PHY_GetRateValuesOfTxPowerByRate(
		PADAPTER pAdapter,
		u32 RegAddr,
		u32 BitMask,
		u32 Value,
		u8 *Rate,
		s8 *PwrByRateVal,
		u8 *RateNum
);

u8
PHY_GetRateIndexOfTxPowerByRate(
		u8	Rate
);

void
phy_set_tx_power_index_by_rate_section(
		PADAPTER		pAdapter,
		enum rf_path		RFPath,
		u8				Channel,
		u8				RateSection
);

s8
_PHY_GetTxPowerByRate(
		PADAPTER	pAdapter,
		u8			Band,
		enum rf_path	RFPath,
		u8			RateIndex
);

s8
PHY_GetTxPowerByRate(
		PADAPTER	pAdapter,
		u8			Band,
		enum rf_path	RFPath,
		RATE_SECTION rs,
		enum MGN_RATE rate
);

void
PHY_SetTxPowerByRate(
		PADAPTER	pAdapter,
		u8			Band,
		enum rf_path	RFPath,
		u8			Rate,
		s8			Value
);

void
phy_set_tx_power_level_by_path(
		PADAPTER	Adapter,
		u8			channel,
		u8			path
);

void
PHY_InitTxPowerByRate(
		PADAPTER	pAdapter
);

void
phy_store_tx_power_by_rate(
		PADAPTER	pAdapter,
		u32			Band,
		u32			RfPath,
		u32			TxNum,
		u32			RegAddr,
		u32			BitMask,
		u32			Data
);

void
PHY_TxPowerByRateConfiguration(
	  PADAPTER			pAdapter
);

bool phy_chk_ch_setting_consistency(_adapter *adapter, u8 ch);

#ifdef CONFIG_TXPWR_PG_WITH_PWR_IDX
u8 phy_get_pg_txpwr_idx(_adapter *pAdapter
	, enum rf_path RFPath, RATE_SECTION rs, u8 ntx_idx
	, enum channel_width BandWidth, u8 band, u8 Channel);
#endif

#if CONFIG_TXPWR_LIMIT
s8 phy_get_txpwr_lmt(_adapter *adapter
	, const char *regd_name
	, BAND_TYPE band, enum channel_width bw
	, u8 tlrs, u8 ntx_idx, u8 cch, u8 lock
);

s8 phy_get_txpwr_lmt_diff(_adapter *adapter
	, const char *regd_name
	, BAND_TYPE band, enum channel_width bw
	, u8 rfpath, u8 rs, u8 tlrs, u8 ntx_idx, u8 cch, u8 lock
);

s8 phy_get_txpwr_lmt_sub_chs(_adapter *adapter
	, const char *regd_name
	, BAND_TYPE band, enum channel_width bw
	, u8 rfpath, u8 rate, u8 ntx_idx, u8 cch, u8 opch
);
#else
#define phy_get_txpwr_lmt(adapter, regd_name, band, bw, tlrs, ntx_idx, cch, lock) (GET_HAL_SPEC(adapter)->txgi_max)
#define phy_get_txpwr_lmt_diff(adapter, regd_name, band, bw, rfpath, rs, tlrs, ntx_idx, cch, lock) (GET_HAL_SPEC(adapter)->txgi_max)
#define phy_get_txpwr_lmt_sub_chs(adapter, regd_name, band, bw, rfpath, rate, ntx_idx, cch, opch) (GET_HAL_SPEC(adapter)->txgi_max)
#endif /* CONFIG_TXPWR_LIMIT */

s8 phy_get_txpwr_target(_adapter *adapter, u8 rfpath, RATE_SECTION rs, u8 rate, u8 ntx_idx
	, enum channel_width bw, BAND_TYPE band, u8 cch, u8 opch, struct txpwr_idx_comp *tic);
s8 phy_get_txpwr_amends(_adapter *adapter, u8 rfpath, RATE_SECTION rs, u8 rate, u8 ntx_idx
	, enum channel_width bw, BAND_TYPE band, u8 cch, struct txpwr_idx_comp *tic);
#ifdef CONFIG_TXPWR_PG_WITH_TSSI_OFFSET
s8 phy_get_tssi_txpwr_by_rate_ref(_adapter *adapter, enum rf_path path
	, enum channel_width bw, u8 cch, u8 opch);
#endif
u8 hal_com_get_txpwr_idx(_adapter *adapter, enum rf_path rfpath
	, RATE_SECTION rs, enum MGN_RATE rate, enum channel_width bw, BAND_TYPE band, u8 cch, u8 opch
	, struct txpwr_idx_comp *tic);

s16 phy_get_txpwr_single_mbm(_adapter *adapter, u8 rfpath, RATE_SECTION rs, u8 rate
	, enum channel_width bw, u8 cch, u8 opch, struct txpwr_idx_comp *tic);
s16 phy_get_txpwr_total_mbm(_adapter *adapter, RATE_SECTION rs, u8 rate
	, enum channel_width bw, u8 cch, u8 opch, struct txpwr_idx_comp *tic);

s16 phy_get_txpwr_single_max_mbm(_adapter *adapter, u8 rfpath
	, enum channel_width bw, u8 cch, u8 opch, u16 bmp_cck_ofdm, u32 bmp_ht, u64 bmp_vht);
s16 phy_get_txpwr_total_max_mbm(_adapter *adapter
	, enum channel_width bw, u8 cch, u8 opch, u16 bmp_cck_ofdm, u32 bmp_ht, u64 bmp_vht);

s8
phy_get_tx_power_final_absolute_value(_adapter *adapter, u8 rfpath, u8 rate,
				      enum channel_width bw, u8 channel);

s8
PHY_GetTxPowerTrackingOffset(
	PADAPTER	pAdapter,
	enum rf_path	RFPath,
	u8			Rate
);

struct txpwr_idx_comp {
	u8 ntx_idx;
	s8 target;
	s8 base;

	/* for target */
	s8 by_rate;
	s8 btc;
	s8 extra;
	s8 utarget;
	s8 limit;
	s8 ulimit;

	/* for amends */
	s8 tpt;
	s8 dpd;
};

u8 phy_get_tx_power_index_ex(_adapter *adapter
	, enum rf_path rfpath, RATE_SECTION rs, enum MGN_RATE rate
	, enum channel_width bw, BAND_TYPE band, u8 cch, u8 opch);

u8
phy_get_tx_power_index(
		PADAPTER			pAdapter,
		enum rf_path			RFPath,
		u8					Rate,
		enum channel_width	BandWidth,
		u8					Channel
);

void
PHY_SetTxPowerIndex(
		PADAPTER		pAdapter,
		u32				PowerIndex,
		enum rf_path		RFPath,
		u8				Rate
);

bool phy_is_txpwr_user_mbm_valid(_adapter *adapter, s16 mbm);
bool phy_is_txpwr_user_target_specified(_adapter *adapter);

void dump_tx_power_index_inline(void *sel, _adapter *adapter, u8 rfpath
	, enum channel_width bw, u8 cch, enum MGN_RATE rate, u8 pwr_idx, struct txpwr_idx_comp *tic);
void dump_tx_power_idx_title(void *sel, _adapter *adapter
	, enum channel_width bw, u8 cch, u8 opch);
void dump_tx_power_idx_by_path_rs(void *sel, _adapter *adapter, u8 rfpath
	, RATE_SECTION rs, enum channel_width bw, u8 cch, u8 opch);
void dump_tx_power_idx(void *sel, _adapter *adapter
	, enum channel_width bw, u8 cch, u8 opch);
void dump_txpwr_total_dbm_title(void *sel, _adapter *adapter
	, enum channel_width bw, u8 cch, u8 opch);
void dump_txpwr_total_dbm_by_rs(void *sel, _adapter *adapter, u8 rs
	, enum channel_width bw, u8 cch, u8 opch);
void dump_txpwr_total_dbm(void *sel, _adapter *adapter
	, enum channel_width bw, u8 cch, u8 opch);

bool phy_is_tx_power_limit_needed(_adapter *adapter);
bool phy_is_tx_power_by_rate_needed(_adapter *adapter);
int phy_load_tx_power_by_rate(_adapter *adapter, u8 chk_file);
#if CONFIG_TXPWR_LIMIT
int phy_load_tx_power_limit(_adapter *adapter, u8 chk_file);
#endif
void phy_load_tx_power_ext_info(_adapter *adapter, u8 chk_file);
void phy_reload_tx_power_ext_info(_adapter *adapter);
void phy_reload_default_tx_power_ext_info(_adapter *adapter);

const struct map_t *hal_pg_txpwr_def_info(_adapter *adapter);

#ifdef CONFIG_EFUSE_CONFIG_FILE
int check_phy_efuse_tx_power_info_valid(_adapter *adapter);
#endif

#ifdef CONFIG_TXPWR_PG_WITH_PWR_IDX
void dump_hal_txpwr_info_2g(void *sel, _adapter *adapter, u8 rfpath_num, u8 max_tx_cnt);
void dump_hal_txpwr_info_5g(void *sel, _adapter *adapter, u8 rfpath_num, u8 max_tx_cnt);

void hal_load_txpwr_info(_adapter *adapter);
#endif

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

#define MAX_PARA_FILE_BUF_LEN	32768 /* 32k */

#define LOAD_MAC_PARA_FILE				BIT0
#define LOAD_BB_PARA_FILE					BIT1
#define LOAD_BB_PG_PARA_FILE				BIT2
#define LOAD_BB_MP_PARA_FILE				BIT3
#define LOAD_RF_PARA_FILE					BIT4
#define LOAD_RF_TXPWR_TRACK_PARA_FILE	BIT5
#define LOAD_RF_TXPWR_LMT_PARA_FILE		BIT6

int phy_ConfigMACWithParaFile(PADAPTER	Adapter, char	*pFileName);
int phy_ConfigBBWithParaFile(PADAPTER	Adapter, char	*pFileName, u32	ConfigType);
int phy_ConfigBBWithPgParaFile(PADAPTER	Adapter, const char *pFileName);
int phy_ConfigBBWithMpParaFile(PADAPTER	Adapter, char	*pFileName);
int PHY_ConfigRFWithParaFile(PADAPTER	Adapter, char	*pFileName, enum rf_path	eRFPath);
int PHY_ConfigRFWithTxPwrTrackParaFile(PADAPTER	Adapter, char	*pFileName);
#if CONFIG_TXPWR_LIMIT
int PHY_ConfigRFWithPowerLimitTableParaFile(PADAPTER	Adapter, const char *pFileName);
#endif
void phy_free_filebuf_mask(_adapter *padapter, u8 mask);
void phy_free_filebuf(_adapter *padapter);
#endif /* CONFIG_LOAD_PHY_PARA_FROM_FILE */
u8 phy_check_under_survey_ch(_adapter *adapter);
#endif /* __HAL_COMMON_H__ */
