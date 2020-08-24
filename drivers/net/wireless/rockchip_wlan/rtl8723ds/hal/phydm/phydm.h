/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __HALDMOUTSRC_H__
#define __HALDMOUTSRC_H__

/*@============================================================*/
/*@include files*/
/*@============================================================*/
/*PHYDM header*/
#include "phydm_pre_define.h"
#include "phydm_features.h"
#include "phydm_dig.h"
#ifdef CONFIG_PATH_DIVERSITY
#include "phydm_pathdiv.h"
#endif
#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
#include "phydm_antdiv.h"
#endif

#include "phydm_soml.h"

#ifdef CONFIG_SMART_ANTENNA
#include "phydm_smt_ant.h"
#endif
#ifdef CONFIG_ANT_DETECTION
#include "phydm_antdect.h"
#endif
#include "phydm_rainfo.h"
#ifdef CONFIG_DYNAMIC_TX_TWR
#include "phydm_dynamictxpower.h"
#endif
#include "phydm_cfotracking.h"
#include "phydm_adaptivity.h"
#include "phydm_dfs.h"
#include "phydm_ccx.h"
#include "txbf/phydm_hal_txbf_api.h"
#if (PHYDM_LA_MODE_SUPPORT)
#include "phydm_adc_sampling.h"
#endif
#ifdef CONFIG_PSD_TOOL
#include "phydm_psd.h"
#endif
#ifdef PHYDM_PRIMARY_CCA
#include "phydm_primary_cca.h"
#endif
#include "phydm_cck_pd.h"
#include "phydm_rssi_monitor.h"
#ifdef PHYDM_AUTO_DEGBUG
#include "phydm_auto_dbg.h"
#endif
#include "phydm_math_lib.h"
#include "phydm_noisemonitor.h"
#include "phydm_api.h"
#ifdef PHYDM_POWER_TRAINING_SUPPORT
#include "phydm_pow_train.h"
#endif
#ifdef PHYDM_LNA_SAT_CHK_SUPPORT
#include "phydm_lna_sat.h"
#endif
#ifdef PHYDM_PMAC_TX_SETTING_SUPPORT
#include "phydm_pmac_tx_setting.h"
#endif
#ifdef PHYDM_MP_SUPPORT
#include "phydm_mp.h"
#endif

#ifdef PHYDM_CCK_RX_PATHDIV_SUPPORT
#include "phydm_cck_rx_pathdiv.h"
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	#include "phydm_beamforming.h"
#endif

#ifdef CONFIG_DIRECTIONAL_BF
#include "phydm_direct_bf.h"
#endif

#include "phydm_regtable.h"

/*@HALRF header*/
#include "halrf/halrf_iqk.h"
#include "halrf/halrf_dpk.h"
#include "halrf/halrf.h"
#include "halrf/halrf_powertracking.h"
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	#include "halrf/halphyrf_ap.h"
#elif(DM_ODM_SUPPORT_TYPE & (ODM_CE))
	#include "halrf/halphyrf_ce.h"
#elif (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	#include "halrf/halphyrf_win.h"
#elif(DM_ODM_SUPPORT_TYPE & (ODM_IOT))
	#include "halrf/halphyrf_iot.h"
#endif

extern const u16	phy_rate_table[84];

/*@============================================================*/
/*@Definition */
/*@============================================================*/

/* Traffic load decision */
#define TRAFFIC_NO_TP			0
#define	TRAFFIC_ULTRA_LOW		1
#define	TRAFFIC_LOW			2
#define	TRAFFIC_MID			3
#define	TRAFFIC_HIGH			4

#define	NONE				0

#if defined(DM_ODM_CE_MAC80211)
#define MAX_2(x, y)					\
	__max2(typeof(x), typeof(y),			\
	      x, y)
#define __max2(t1, t2, x, y) ({		\
	t1 m80211_max1 = (x);					\
	t2 m80211_max2 = (y);					\
	m80211_max1 > m80211_max2 ? m80211_max1 : m80211_max2; })

#define MIN_2(x, y)					\
	__min2(typeof(x), typeof(y),			\
	      x, y)
#define __min2(t1, t2, x, y) ({		\
	t1 m80211_min1 = (x);					\
	t2 m80211_min2 = (y);					\
	m80211_min1 < m80211_min2 ? m80211_min1 : m80211_min2; })

#define DIFF_2(x, y)					\
	__diff2(typeof(x), typeof(y),			\
	      x, y)
#define __diff2(t1, t2, x, y) ({		\
	t1 __d1 = (x);					\
	t2 __d2 = (y);					\
	(__d1 >= __d2) ? (__d1 - __d2) : (__d2 - __d1); })
#else
#define MAX_2(_x_, _y_)	(((_x_) > (_y_)) ? (_x_) : (_y_))
#define MIN_2(_x_, _y_)	(((_x_) < (_y_)) ? (_x_) : (_y_))
#define DIFF_2(_x_, _y_)	((_x_ >= _y_) ? (_x_ - _y_) : (_y_ - _x_))
#endif

#define IS_GREATER(_x_, _y_)	(((_x_) >= (_y_)) ? true : false)
#define IS_LESS(_x_, _y_)	(((_x_) < (_y_)) ? true : false)

#if defined(DM_ODM_CE_MAC80211)
#define BYTE_DUPLICATE_2_DWORD(B0) ({	\
	u32 __b_dup = (B0);\
	(((__b_dup) << 24) | ((__b_dup) << 16) | ((__b_dup) << 8) | (__b_dup));\
	})
#else
#define BYTE_DUPLICATE_2_DWORD(B0)	\
	(((B0) << 24) | ((B0) << 16) | ((B0) << 8) | (B0))
#endif
#define BYTE_2_DWORD(B3, B2, B1, B0)	\
	(((B3) << 24) | ((B2) << 16) | ((B1) << 8) | (B0))
#define BIT_2_BYTE(B3, B2, B1, B0)	\
	(((B3) << 3) | ((B2) << 2) | ((B1) << 1) | (B0))

/*@For cmn sta info*/
#if defined(DM_ODM_CE_MAC80211)
#define is_sta_active(sta) ({	\
	struct cmn_sta_info *__sta = (sta);	\
	((__sta) && (__sta->dm_ctrl & STA_DM_CTRL_ACTIVE));	\
	})

#define IS_FUNC_EN(name) ({	\
	u8 *__is_func_name = (name);	\
	(__is_func_name) && (*__is_func_name);	\
	})
#else
#define is_sta_active(sta)	((sta) && (sta->dm_ctrl & STA_DM_CTRL_ACTIVE))

#define IS_FUNC_EN(name)	((name) && (*name))
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	#define PHYDM_WATCH_DOG_PERIOD	1 /*second*/
#else
	#define PHYDM_WATCH_DOG_PERIOD	2 /*second*/
#endif

#define PHY_HIST_SIZE		12
#define PHY_HIST_TH_SIZE	(PHY_HIST_SIZE - 1)

/*@============================================================*/
/*structure and define*/
/*@============================================================*/

#define		dm_type_by_fw		0
#define		dm_type_by_driver	1

#ifdef BB_RAM_SUPPORT

struct phydm_bb_ram_per_sta {
	/* @Reg0x1E84 for RAM I/O*/
	boolean			hw_igi_en;
	boolean			tx_pwr_offset0_en;
	boolean			tx_pwr_offset1_en;
	/* @ macid from 0 to 63, above 63 => mapping to 63*/
	u8			macid_addr;
	/* @hw_igi value for paths after packet Tx in a period of time*/
	u8			hw_igi;
	/* @tx_pwr_offset0 offset for Tx power index*/
	s8			tx_pwr_offset0;
	s8			tx_pwr_offset1;

};

struct phydm_bb_ram_ctrl {
	/*@ For 98F/14B/22C/12F, each tx_pwr_ofst step will be 1dB*/
	struct phydm_bb_ram_per_sta pram_sta_ctrl[64];
	/*------------ For table2 do not set power offset by macid --------*/
	/* For type == 2'b10, 0x1e70[22:16] = tx_pwr_offset_reg0, 0x1e70[23] = enable */
	boolean			tx_pwr_ofst_reg0_en;
	u8			tx_pwr_ofst_reg0;
	/* For type == 2'b11, 0x1e70[30:24] = tx_pwr_offset_reg1, 0x1e70[31] = enable */
	boolean			tx_pwr_ofst_reg1_en;
	u8			tx_pwr_ofst_reg1;
	boolean			hwigi_watchdog_en;
};

#endif

struct phydm_phystatus_statistic {
	/*@[CCK]*/
	u32			rssi_cck_sum;
	u32			rssi_cck_cnt;
	u32			rssi_beacon_sum[RF_PATH_MEM_SIZE];
	u32			rssi_beacon_cnt;
	#ifdef PHYSTS_3RD_TYPE_SUPPORT
	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	u32			rssi_cck_sum_abv_2ss[RF_PATH_MEM_SIZE - 1];
	#endif
	#endif
	/*@[OFDM]*/
	u32			rssi_ofdm_sum[RF_PATH_MEM_SIZE];
	u32			rssi_ofdm_cnt;
	u32			evm_ofdm_sum;
	u32			snr_ofdm_sum[RF_PATH_MEM_SIZE];
	u16			evm_ofdm_hist[PHY_HIST_SIZE];
	u16			snr_ofdm_hist[PHY_HIST_SIZE];
	/*@[1SS]*/
	u32			rssi_1ss_cnt;
	u32			rssi_1ss_sum[RF_PATH_MEM_SIZE];
	u32			evm_1ss_sum;
	u32			snr_1ss_sum[RF_PATH_MEM_SIZE];
	u16			evm_1ss_hist[PHY_HIST_SIZE];
	u16			snr_1ss_hist[PHY_HIST_SIZE];
	/*@[2SS]*/
	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	u32			rssi_2ss_cnt;
	u32			rssi_2ss_sum[RF_PATH_MEM_SIZE];
	u32			evm_2ss_sum[2];
	u32			snr_2ss_sum[RF_PATH_MEM_SIZE];
	u16			evm_2ss_hist[2][PHY_HIST_SIZE];
	u16			snr_2ss_hist[2][PHY_HIST_SIZE];
	#endif
	/*@[3SS]*/
	#if (defined(PHYDM_COMPILE_ABOVE_3SS))
	u32			rssi_3ss_cnt;
	u32			rssi_3ss_sum[RF_PATH_MEM_SIZE];
	u32			evm_3ss_sum[3];
	u32			snr_3ss_sum[RF_PATH_MEM_SIZE];
	u16			evm_3ss_hist[3][PHY_HIST_SIZE];
	u16			snr_3ss_hist[3][PHY_HIST_SIZE];
	#endif
	/*@[4SS]*/
	#if (defined(PHYDM_COMPILE_ABOVE_4SS))
	u32			rssi_4ss_cnt;
	u32			rssi_4ss_sum[RF_PATH_MEM_SIZE];
	u32			evm_4ss_sum[4];
	u32			snr_4ss_sum[RF_PATH_MEM_SIZE];
	u16			evm_4ss_hist[4][PHY_HIST_SIZE];
	u16			snr_4ss_hist[4][PHY_HIST_SIZE];
	#endif
#ifdef PHYDM_PHYSTAUS_AUTO_SWITCH
	u16			p4_cnt[RF_PATH_MEM_SIZE]; /*phy-sts page4 cnt*/
	u16			cn_sum[RF_PATH_MEM_SIZE]; /*condition number*/
	u16			cn_hist[RF_PATH_MEM_SIZE][PHY_HIST_SIZE];
#endif
};

struct phydm_phystatus_avg {
	/*@[CCK]*/
	u8			rssi_cck_avg;
	u8			rssi_beacon_avg[RF_PATH_MEM_SIZE];
	#ifdef PHYSTS_3RD_TYPE_SUPPORT
	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	u8			rssi_cck_avg_abv_2ss[RF_PATH_MEM_SIZE - 1];
	#endif
	#endif
	/*@[OFDM]*/
	u8			rssi_ofdm_avg[RF_PATH_MEM_SIZE];
	u8			evm_ofdm_avg;
	u8			snr_ofdm_avg[RF_PATH_MEM_SIZE];
	/*@[1SS]*/
	u8			rssi_1ss_avg[RF_PATH_MEM_SIZE];
	u8			evm_1ss_avg;
	u8			snr_1ss_avg[RF_PATH_MEM_SIZE];
	/*@[2SS]*/
	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	u8			rssi_2ss_avg[RF_PATH_MEM_SIZE];
	u8			evm_2ss_avg[2];
	u8			snr_2ss_avg[RF_PATH_MEM_SIZE];
	#endif
	/*@[3SS]*/
	#if (defined(PHYDM_COMPILE_ABOVE_3SS))
	u8			rssi_3ss_avg[RF_PATH_MEM_SIZE];
	u8			evm_3ss_avg[3];
	u8			snr_3ss_avg[RF_PATH_MEM_SIZE];
	#endif
	/*@[4SS]*/
	#if (defined(PHYDM_COMPILE_ABOVE_4SS))
	u8			rssi_4ss_avg[RF_PATH_MEM_SIZE];
	u8			evm_4ss_avg[4];
	u8			snr_4ss_avg[RF_PATH_MEM_SIZE];
	#endif
};

struct odm_phy_dbg_info {
	/*@ODM Write,debug info*/
	u32			num_qry_phy_status_cck;
	u32			num_qry_phy_status_ofdm;
#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT) || (defined(PHYSTS_3RD_TYPE_SUPPORT))
	u32			num_qry_mu_pkt;
	u32			num_qry_bf_pkt;
	u16			num_mu_vht_pkt[VHT_RATE_NUM];
	boolean			is_ldpc_pkt;
	boolean			is_stbc_pkt;
	u8			num_of_ppdu[4];
	u8			gid_num[4];
#endif
	u32			condi_num; /*@condition number U(18,4)*/
	u8			condi_num_cdf[CN_CNT_MAX];
	u8			num_qry_beacon_pkt;
	u8			beacon_cnt_in_period; /*@beacon cnt within watchdog period*/
	u8			beacon_phy_rate;
	u8			show_phy_sts_all_pkt;	/*@Show phy status witch not match BSSID*/
	u16			show_phy_sts_max_cnt;	/*@show number of phy-status row data per PHYDM watchdog*/
	u16			show_phy_sts_cnt;
	u16			num_qry_legacy_pkt[LEGACY_RATE_NUM];
	u16			num_qry_ht_pkt[HT_RATE_NUM];
	u16			num_qry_pkt_sc_20m[LOW_BW_RATE_NUM]; /*@20M SC*/
	boolean			ht_pkt_not_zero;
	boolean			low_bw_20_occur;
	#if ODM_IC_11AC_SERIES_SUPPORT || defined(PHYDM_IC_JGR3_SERIES_SUPPORT)
	u16			num_qry_vht_pkt[VHT_RATE_NUM];
	u16			num_qry_pkt_sc_40m[LOW_BW_RATE_NUM]; /*@40M SC*/
	boolean			vht_pkt_not_zero;
	boolean			low_bw_40_occur;
	#endif
	u16			snr_hist_th[PHY_HIST_TH_SIZE];
	u16			evm_hist_th[PHY_HIST_TH_SIZE];
	#ifdef PHYSTS_3RD_TYPE_SUPPORT
	u16			cn_hist_th[PHY_HIST_TH_SIZE]; /*U(16,1)*/
	u8			condition_num_seg0;
	u8			eigen_val[4];
	s16			cfo_tail[4]; /*per-path's cfo_tail */
	#endif
	struct phydm_phystatus_statistic	physts_statistic_info;
	struct phydm_phystatus_avg		phystatus_statistic_avg;
};

enum odm_cmninfo {
	/*@Fixed value*/
	/*@-----------HOOK BEFORE REG INIT-----------*/
	ODM_CMNINFO_PLATFORM = 0,
	ODM_CMNINFO_ABILITY,
	ODM_CMNINFO_INTERFACE,
	ODM_CMNINFO_MP_TEST_CHIP,
	ODM_CMNINFO_IC_TYPE,
	ODM_CMNINFO_CUT_VER,
	ODM_CMNINFO_FAB_VER,
	ODM_CMNINFO_FW_VER,
	ODM_CMNINFO_FW_SUB_VER,
	ODM_CMNINFO_RF_TYPE,
	ODM_CMNINFO_RFE_TYPE,
	ODM_CMNINFO_DPK_EN,
	ODM_CMNINFO_BOARD_TYPE,
	ODM_CMNINFO_PACKAGE_TYPE,
	ODM_CMNINFO_EXT_LNA,
	ODM_CMNINFO_5G_EXT_LNA,
	ODM_CMNINFO_EXT_PA,
	ODM_CMNINFO_5G_EXT_PA,
	ODM_CMNINFO_GPA,
	ODM_CMNINFO_APA,
	ODM_CMNINFO_GLNA,
	ODM_CMNINFO_ALNA,
	ODM_CMNINFO_TDMA,
	ODM_CMNINFO_EXT_TRSW,
	ODM_CMNINFO_EXT_LNA_GAIN,
	ODM_CMNINFO_PATCH_ID,
	ODM_CMNINFO_BINHCT_TEST,
	ODM_CMNINFO_BWIFI_TEST,
	ODM_CMNINFO_SMART_CONCURRENT,
	ODM_CMNINFO_CONFIG_BB_RF,
	ODM_CMNINFO_IQKPAOFF,
	ODM_CMNINFO_HUBUSBMODE,
	ODM_CMNINFO_FWDWRSVDPAGEINPROGRESS,
	ODM_CMNINFO_TX_TP,
	ODM_CMNINFO_RX_TP,
	ODM_CMNINFO_SOUNDING_SEQ,
	ODM_CMNINFO_REGRFKFREEENABLE,
	ODM_CMNINFO_RFKFREEENABLE,
	ODM_CMNINFO_NORMAL_RX_PATH_CHANGE,
	ODM_CMNINFO_VALID_PATH_SET,
	ODM_CMNINFO_EFUSE0X3D8,
	ODM_CMNINFO_EFUSE0X3D7,
	ODM_CMNINFO_SOFT_AP_SPECIAL_SETTING,
	ODM_CMNINFO_X_CAP_SETTING,
	ODM_CMNINFO_ADVANCE_OTA,
	ODM_CMNINFO_HP_HWID,
	ODM_CMNINFO_TSSI_ENABLE,
	ODM_CMNINFO_DIS_DPD,
	ODM_CMNINFO_POWER_VOLTAGE,
	ODM_CMNINFO_ANTDIV_GPIO,
	ODM_CMNINFO_EN_AUTO_BW_TH,
	ODM_CMNINFO_PEAK_DETECT_MODE,
	/*@-----------HOOK BEFORE REG INIT-----------*/

	/*@Dynamic value:*/

	/*@--------- POINTER REFERENCE-----------*/
	ODM_CMNINFO_TX_UNI,
	ODM_CMNINFO_RX_UNI,
	ODM_CMNINFO_BAND,
	ODM_CMNINFO_SEC_CHNL_OFFSET,
	ODM_CMNINFO_SEC_MODE,
	ODM_CMNINFO_BW,
	ODM_CMNINFO_CHNL,
	ODM_CMNINFO_FORCED_RATE,
	ODM_CMNINFO_ANT_DIV,
	ODM_CMNINFO_PATH_DIV,
	ODM_CMNINFO_ADAPTIVE_SOML,
	ODM_CMNINFO_ADAPTIVITY,
	ODM_CMNINFO_SCAN,
	ODM_CMNINFO_POWER_SAVING,
	ODM_CMNINFO_ONE_PATH_CCA,
	ODM_CMNINFO_DRV_STOP,
	ODM_CMNINFO_PNP_IN,
	ODM_CMNINFO_INIT_ON,
	ODM_CMNINFO_ANT_TEST,
	ODM_CMNINFO_NET_CLOSED,
	ODM_CMNINFO_P2P_LINK,
	ODM_CMNINFO_FCS_MODE,
	ODM_CMNINFO_IS1ANTENNA,
	ODM_CMNINFO_RFDEFAULTPATH,
	ODM_CMNINFO_DFS_MASTER_ENABLE,
	ODM_CMNINFO_FORCE_TX_ANT_BY_TXDESC,
	ODM_CMNINFO_SET_S0S1_DEFAULT_ANTENNA,
	ODM_CMNINFO_SOFT_AP_MODE,
	ODM_CMNINFO_MP_MODE,
	ODM_CMNINFO_INTERRUPT_MASK,
	ODM_CMNINFO_BB_OPERATION_MODE,
	ODM_CMNINFO_BF_ANTDIV_DECISION,
	ODM_CMNINFO_MANUAL_SUPPORTABILITY,
	ODM_CMNINFO_EN_DYM_BW_INDICATION,
	/*@--------- POINTER REFERENCE-----------*/

	/*@------------CALL BY VALUE-------------*/
	ODM_CMNINFO_WIFI_DIRECT,
	ODM_CMNINFO_WIFI_DISPLAY,
	ODM_CMNINFO_LINK_IN_PROGRESS,
	ODM_CMNINFO_LINK,
	ODM_CMNINFO_CMW500LINK,
	ODM_CMNINFO_STATION_STATE,
	ODM_CMNINFO_RSSI_MIN,
	ODM_CMNINFO_RSSI_MIN_BY_PATH,
	ODM_CMNINFO_DBG_COMP,
	ODM_CMNINFO_RA_THRESHOLD_HIGH,	/*to be removed*/
	ODM_CMNINFO_RA_THRESHOLD_LOW,	/*to be removed*/
	ODM_CMNINFO_RF_ANTENNA_TYPE,
	ODM_CMNINFO_WITH_EXT_ANTENNA_SWITCH,
	ODM_CMNINFO_BE_FIX_TX_ANT,
	ODM_CMNINFO_BT_ENABLED,
	ODM_CMNINFO_BT_HS_CONNECT_PROCESS,
	ODM_CMNINFO_BT_HS_RSSI,
	ODM_CMNINFO_BT_OPERATION,
	ODM_CMNINFO_BT_LIMITED_DIG,
	ODM_CMNINFO_AP_TOTAL_NUM,
	ODM_CMNINFO_POWER_TRAINING,
	ODM_CMNINFO_DFS_REGION_DOMAIN,
	ODM_CMNINFO_BT_CONTINUOUS_TURN,
	ODM_CMNINFO_IS_DOWNLOAD_FW,
	ODM_CMNINFO_PHYDM_PATCH_ID,
	ODM_CMNINFO_RRSR_VAL,
	ODM_CMNINFO_LINKED_BF_SUPPORT,
	ODM_CMNINFO_FLATNESS_TYPE,
	/*@------------CALL BY VALUE-------------*/

	/*@Dynamic ptr array hook itms.*/
	ODM_CMNINFO_STA_STATUS,
	ODM_CMNINFO_MAX,

};

enum phydm_rfe_bb_source_sel {
	PAPE_2G			= 0,
	PAPE_5G			= 1,
	LNA0N_2G		= 2,
	LNAON_5G		= 3,
	TRSW			= 4,
	TRSW_B			= 5,
	GNT_BT			= 6,
	ZERO			= 7,
	ANTSEL_0		= 8,
	ANTSEL_1		= 9,
	ANTSEL_2		= 0xa,
	ANTSEL_3		= 0xb,
	ANTSEL_4		= 0xc,
	ANTSEL_5		= 0xd,
	ANTSEL_6		= 0xe,
	ANTSEL_7		= 0xf
};

enum phydm_info_query {
	PHYDM_INFO_FA_OFDM,
	PHYDM_INFO_FA_CCK,
	PHYDM_INFO_FA_TOTAL,
	PHYDM_INFO_CCA_OFDM,
	PHYDM_INFO_CCA_CCK,
	PHYDM_INFO_CCA_ALL,
	PHYDM_INFO_CRC32_OK_VHT,
	PHYDM_INFO_CRC32_OK_HT,
	PHYDM_INFO_CRC32_OK_LEGACY,
	PHYDM_INFO_CRC32_OK_CCK,
	PHYDM_INFO_CRC32_ERROR_VHT,
	PHYDM_INFO_CRC32_ERROR_HT,
	PHYDM_INFO_CRC32_ERROR_LEGACY,
	PHYDM_INFO_CRC32_ERROR_CCK,
	PHYDM_INFO_EDCCA_FLAG,
	PHYDM_INFO_OFDM_ENABLE,
	PHYDM_INFO_CCK_ENABLE,
	PHYDM_INFO_CRC32_OK_HT_AGG,
	PHYDM_INFO_CRC32_ERROR_HT_AGG,
	PHYDM_INFO_DBG_PORT_0,
	PHYDM_INFO_CURR_IGI,
	PHYDM_INFO_RSSI_MIN,
	PHYDM_INFO_RSSI_MAX,
	PHYDM_INFO_CLM_RATIO,
	PHYDM_INFO_NHM_RATIO,
	PHYDM_INFO_NHM_NOISE_PWR,
};

enum phydm_api {
	PHYDM_API_NBI		= 1,
	PHYDM_API_CSI_MASK	= 2,
};

enum phydm_func_idx { /*@F_XXX = PHYDM XXX function*/

	F00_DIG			= 0,
	F01_RA_MASK		= 1,
	F02_DYN_TXPWR		= 2,
	F03_FA_CNT		= 3,
	F04_RSSI_MNTR		= 4,
	F05_CCK_PD		= 5,
	F06_ANT_DIV		= 6,
	F07_SMT_ANT		= 7,
	F08_PWR_TRAIN		= 8,
	F09_RA			= 9,
	F10_PATH_DIV		= 10,
	F11_DFS			= 11,
	F12_DYN_ARFR		= 12,
	F13_ADPTVTY		= 13,
	F14_CFO_TRK		= 14,
	F15_ENV_MNTR		= 15,
	F16_PRI_CCA		= 16,
	F17_ADPTV_SOML		= 17,
	F18_LNA_SAT_CHK		= 18,
};

/*@=[PHYDM supportability]==========================================*/
enum odm_ability {
	ODM_BB_DIG		= BIT(F00_DIG),
	ODM_BB_RA_MASK		= BIT(F01_RA_MASK),
	ODM_BB_DYNAMIC_TXPWR	= BIT(F02_DYN_TXPWR),
	ODM_BB_FA_CNT		= BIT(F03_FA_CNT),
	ODM_BB_RSSI_MONITOR	= BIT(F04_RSSI_MNTR),
	ODM_BB_CCK_PD		= BIT(F05_CCK_PD),
	ODM_BB_ANT_DIV		= BIT(F06_ANT_DIV),
	ODM_BB_SMT_ANT		= BIT(F07_SMT_ANT),
	ODM_BB_PWR_TRAIN	= BIT(F08_PWR_TRAIN),
	ODM_BB_RATE_ADAPTIVE	= BIT(F09_RA),
	ODM_BB_PATH_DIV		= BIT(F10_PATH_DIV),
	ODM_BB_DFS		= BIT(F11_DFS),
	ODM_BB_DYNAMIC_ARFR	= BIT(F12_DYN_ARFR),
	ODM_BB_ADAPTIVITY	= BIT(F13_ADPTVTY),
	ODM_BB_CFO_TRACKING	= BIT(F14_CFO_TRK),
	ODM_BB_ENV_MONITOR	= BIT(F15_ENV_MNTR),
	ODM_BB_PRIMARY_CCA	= BIT(F16_PRI_CCA),
	ODM_BB_ADAPTIVE_SOML	= BIT(F17_ADPTV_SOML),
	ODM_BB_LNA_SAT_CHK	= BIT(F18_LNA_SAT_CHK),
};

/*@=[PHYDM Debug Component]=====================================*/
enum phydm_dbg_comp {
	/*@BB Driver Functions*/
	DBG_DIG			= BIT(F00_DIG),
	DBG_RA_MASK		= BIT(F01_RA_MASK),
	DBG_DYN_TXPWR		= BIT(F02_DYN_TXPWR),
	DBG_FA_CNT		= BIT(F03_FA_CNT),
	DBG_RSSI_MNTR		= BIT(F04_RSSI_MNTR),
	DBG_CCKPD		= BIT(F05_CCK_PD),
	DBG_ANT_DIV		= BIT(F06_ANT_DIV),
	DBG_SMT_ANT		= BIT(F07_SMT_ANT),
	DBG_PWR_TRAIN		= BIT(F08_PWR_TRAIN),
	DBG_RA			= BIT(F09_RA),
	DBG_PATH_DIV		= BIT(F10_PATH_DIV),
	DBG_DFS			= BIT(F11_DFS),
	DBG_DYN_ARFR		= BIT(F12_DYN_ARFR),
	DBG_ADPTVTY		= BIT(F13_ADPTVTY),
	DBG_CFO_TRK		= BIT(F14_CFO_TRK),
	DBG_ENV_MNTR		= BIT(F15_ENV_MNTR),
	DBG_PRI_CCA		= BIT(F16_PRI_CCA),
	DBG_ADPTV_SOML		= BIT(F17_ADPTV_SOML),
	DBG_LNA_SAT_CHK		= BIT(F18_LNA_SAT_CHK),
	/*Neet to re-arrange*/
	DBG_PHY_STATUS		= BIT(20),
	DBG_TMP			= BIT(21),
	DBG_FW_TRACE		= BIT(22),
	DBG_TXBF		= BIT(23),
	DBG_COMMON_FLOW		= BIT(24),
	DBG_COMP_MCC		= BIT(25),
	DBG_FW_DM		= BIT(26),
	DBG_DM_SUMMARY		= BIT(27),
	ODM_PHY_CONFIG		= BIT(28),
	ODM_COMP_INIT		= BIT(29),
	DBG_CMN			= BIT(30),/*@common*/
	ODM_COMP_API		= BIT(31)
};

/*@=========================================================*/

/*@ODM_CMNINFO_ONE_PATH_CCA*/
enum odm_cca_path {
	ODM_CCA_2R		= 0,
	ODM_CCA_1R_A		= 1,
	ODM_CCA_1R_B		= 2,
};

enum phy_reg_pg_type {
	PHY_REG_PG_RELATIVE_VALUE = 0,
	PHY_REG_PG_EXACT_VALUE	= 1
};

enum phydm_offload_ability {
	PHYDM_PHY_PARAM_OFFLOAD = BIT(0),
	PHYDM_RF_IQK_OFFLOAD	= BIT(1),
	PHYDM_RF_DPK_OFFLOAD	= BIT(2),
};

enum phydm_init_result {
	PHYDM_INIT_SUCCESS = 0,
	PHYDM_INIT_FAIL_BBRF_REG_INVALID = 1
};

struct phydm_pause_lv {
	s8			lv_dig;
	s8			lv_cckpd;
	s8			lv_antdiv;
	s8			lv_adapt;
	s8			lv_adsl;
};

struct phydm_func_poiner {
	void (*pause_phydm_handler)(void *dm_void, u32 *val_buf, u8 val_len);
};

struct pkt_process_info {
	#ifdef PHYDM_PHYSTAUS_AUTO_SWITCH
	/*@send phystatus in each sampling time*/
	boolean			physts_auto_swch_en;
	u8			mac_ppdu_cnt;
	u8			phy_ppdu_cnt; /*change with phy cca cnt*/
	u8			page_bitmap_target;
	u8			page_bitmap_record;
	u8			ppdu_phy_rate;
	u8			ppdu_macid;
	boolean			is_1st_mpdu;
	#endif
	u8			lna_idx;
	u8			vga_idx;
};

#ifdef ODM_CONFIG_BT_COEXIST
struct	phydm_bt_info {
	boolean			is_bt_enabled;		/*@BT is enabled*/
	boolean			is_bt_connect_process;	/*@BT HS is under connection progress.*/
	u8			bt_hs_rssi;		/*@BT HS mode wifi rssi value.*/
	boolean			is_bt_hs_operation;	/*@BT HS mode is under progress*/
	boolean			is_bt_limited_dig;	/*@BT is busy.*/
};
#endif

struct	phydm_iot_center {
	boolean			is_linked_cmw500;
	u8			win_patch_id;		/*Customer ID*/
	boolean			patch_id_100f0401;
	boolean			patch_id_10120200;
	boolean			patch_id_40010700;
	boolean			patch_id_021f0800;
	u32			phydm_patch_id;		/*temp for CCX IOT */
};

#if (RTL8822B_SUPPORT)
struct drp_rtl8822b_struct {
	enum bb_path path_judge;
	u16 path_a_cck_fa;
	u16 path_b_cck_fa;
};
#endif

#ifdef CONFIG_MCC_DM
#define MCC_DM_REG_NUM	32
struct _phydm_mcc_dm_ {
	u8		mcc_pre_status;
	u8		mcc_reg_id[MCC_DM_REG_NUM];
	u16		mcc_dm_reg[MCC_DM_REG_NUM];
	u8		mcc_dm_val[MCC_DM_REG_NUM][2];
	/*mcc DIG*/
	u8		mcc_rssi[2];
	/*u8		mcc_igi[2];*/

	/* need to be config by driver*/
	u8		mcc_status;
	u8		sta_macid[2][NUM_STA];
	u16		mcc_rf_ch[2];

};
#endif

#if (RTL8822C_SUPPORT || RTL8812F_SUPPORT || RTL8197G_SUPPORT)
struct phydm_physts {
	u8			cck_gi_u_bnd;
	u8			cck_gi_l_bnd;
};
#endif

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	#if (RT_PLATFORM != PLATFORM_LINUX)
		typedef
	#endif

struct dm_struct {
#else/*for AP, CE Team*/
struct dm_struct {
#endif
	/*@Add for different team use temporarily*/
	void			*adapter;		/*@For CE/NIC team*/
	struct rtl8192cd_priv	*priv;			/*@For AP team*/
	boolean			odm_ready;
	enum phy_reg_pg_type	phy_reg_pg_value_type;
	u8			phy_reg_pg_version;
	u64			support_ability;	/*@PHYDM function Supportability*/
	u64			pause_ability;		/*@PHYDM function pause Supportability*/
	u64			debug_components;
	u8			cmn_dbg_msg_period;
	u8			cmn_dbg_msg_cnt;
	u32			fw_debug_components;
	u32			num_qry_phy_status_all;	/*@CCK + OFDM*/
	u32			last_num_qry_phy_status_all;
	u32			rx_pwdb_ave;
	boolean		is_init_hw_info_by_rfe;

	//TSSI
	u8			en_tssi_mode;

	/*@------ ODM HANDLE, DRIVER NEEDS NOT TO HOOK------*/
	boolean			is_cck_high_power;
	u8			rf_path_rx_enable;
	/*@------ ODM HANDLE, DRIVER NEEDS NOT TO HOOK------*/

	/* @COMMON INFORMATION */

	/*@Init value*/
	/*@-----------HOOK BEFORE REG INIT-----------*/

	u8			support_platform;	/*@PHYDM Platform info WIN/AP/CE = 1/2/3 */
	u8			normal_rx_path;
	u8			valid_path_set;	/*@use for single rx path only*/
	boolean			brxagcswitch;		/* @for rx AGC table switch in Microsoft case */
	u8			support_interface;	/*@PHYDM PCIE/USB/SDIO = 1/2/3*/
	u32			support_ic_type;	/*@PHYDM supported IC*/
	enum phydm_api_host	run_in_drv_fw;		/*@PHYDM API is using in FW or Driver*/
	u8			ic_ip_series;		/*N/AC/JGR3*/
	enum phydm_phy_sts_type	ic_phy_sts_type;	/*@Type1/type2/type3*/
	u8			cut_version;		/*@cut version TestChip/A-cut/B-cut... = 0/1/2/3/...*/
	u8			fab_version;		/*@Fab version TSMC/UMC = 0/1*/
	u8			fw_version;
	u8			fw_sub_version;
	u8			rf_type;		/*@RF type 4T4R/3T3R/2T2R/1T2R/1T1R/...*/
	u8			rfe_type;
	u8			board_type;
	u8			package_type;
	u16			type_glna;
	u16			type_gpa;
	u16			type_alna;
	u16			type_apa;
	u8			ext_lna;		/*@with 2G external LNA  NO/Yes = 0/1*/
	u8			ext_lna_5g;		/*@with 5G external LNA  NO/Yes = 0/1*/
	u8			ext_pa;			/*@with 2G external PNA  NO/Yes = 0/1*/
	u8			ext_pa_5g;		/*@with 5G external PNA  NO/Yes = 0/1*/
	u8			efuse0x3d7;		/*@with Efuse number*/
	u8			efuse0x3d8;
	u8			ext_trsw;		/*@with external TRSW  NO/Yes = 0/1*/
	u8			ext_lna_gain;		/*@gain of external lna*/
	boolean			is_in_hct_test;
	u8			wifi_test;
	boolean			is_dual_mac_smart_concurrent;
	u32			bk_support_ability;	/*SD4 only*/
	u8			with_extenal_ant_switch;
	/*@cck agc relative*/
	boolean			cck_new_agc;
	s8			cck_lna_gain_table[8];
	u8			cck_sat_cnt_th_init;
	/*@-------------------------------------*/
	u32			phydm_sys_up_time;
	u8			num_rf_path;		/*@ex: 8821C=1, 8192E=2, 8814B=4*/
	u32			soft_ap_special_setting;
	boolean			boolean_dummy;
	s8			s8_dummy;
	u8			u8_dummy;
	u16			u16_dummy;
	u32			u32_dummy;
	u8			rfe_hwsetting_band;
	u8			p_advance_ota;
	boolean			hp_hw_id;
	boolean			BOOLEAN_temp;
	boolean			is_dfs_band;
	u8			is_rx_blocking_en;
	u16			fw_offload_ability;
	boolean			is_download_fw;
	boolean			en_dis_dpd;
	u16			dis_dpd_rate;
	u8			en_auto_bw_th;
	#if (RTL8822C_SUPPORT || RTL8814B_SUPPORT || RTL8197G_SUPPORT)
	u8			txagc_buff[RF_PATH_MEM_SIZE][PHY_NUM_RATE_IDX];
	u32			bp_0x9b0;
	#endif
	#if (RTL8822C_SUPPORT)
	u8			ofdm_rxagc_l_bnd[16];
	boolean			l_bnd_detect[16];
	#endif
	boolean			rf_write_no_protection;
/*@-----------HOOK BEFORE REG INIT-----------*/
/*@===========================================================*/
/*@====[ CALL BY Reference ]=========================================*/
/*@===========================================================*/

	u64			*num_tx_bytes_unicast;	/*@TX Unicast byte cnt*/
	u64			*num_rx_bytes_unicast;	/*@RX Unicast byte cnt*/
	u8			*band_type;		/*@2.4G/5G = 0/1*/
	u8			*sec_ch_offset;		/*@Secondary channel offset don't_care/below/above = 0/1/2*/
	u8			*security;		/*@security mode Open/WEP/AES/TKIP = 0/1/2/3*/
	u8			*band_width;		/*@20M/40M/80M = 0/1/2*/
	u8			*channel;		/*@central CH number*/
	boolean			*is_scan_in_process;
	boolean			*is_power_saving;
	boolean			*is_tdma;
	u8			*one_path_cca;		/*@CCA path 2-path/path-A/path-B = 0/1/2; using enum odm_cca_path.*/
	u8			*antenna_test;
	boolean			*is_net_closed;
	boolean			*is_fcs_mode_enable;	/*@fast channel switch (= MCC mode)*/
	/*@--------- For 8723B IQK-------------------------------------*/
	boolean			*is_1_antenna;
	u8			*rf_default_path;	/* @0:S1, 1:S0 */
	/*@-----------------------------------------------------------*/

	u16			*forced_data_rate;
	u8			*enable_antdiv;
	u8			*enable_pathdiv;
	u8			*en_adap_soml;
	u8			*edcca_mode;
	u8			*hub_usb_mode;		/*@1:USB2.0, 2:USB3.0*/
	boolean			*is_fw_dw_rsvd_page_in_progress;
	u32			*current_tx_tp;
	u32			*current_rx_tp;
	u8			*sounding_seq;
	u32			*soft_ap_mode;
	u8			*mp_mode;
	u32			*interrupt_mask;
	u8			*bb_op_mode;
	u32			*manual_supportability;
	u8			*dis_dym_bw_indication;
/*@===========================================================*/
/*@====[ CALL BY VALUE ]===========================================*/
/*@===========================================================*/

	u8			disable_phydm_watchdog;
	boolean			is_link_in_process;
	boolean			is_wifi_direct;
	boolean			is_wifi_display;
	boolean			is_linked;
	boolean			pre_is_linked;
	boolean			first_connect;
	boolean			first_disconnect;
	boolean			bsta_state;
	u8			rssi_min;
	u8			rssi_min_macid;
	u8			pre_rssi_min;
	u8			rssi_max;
	u8			rssi_max_macid;
	u8			rssi_min_by_path;
	boolean			is_mp_chip;
	boolean			is_one_entry_only;
	u32			one_entry_macid;
	u32			one_entry_tp;
	u32			pre_one_entry_tp;
	u8			pre_number_linked_client;
	u8			number_linked_client;
	u8			pre_number_active_client;
	u8			number_active_client;
	boolean			is_disable_phy_api;
	u8			rssi_a;
	u8			rssi_b;
	u8			rssi_c;
	u8			rssi_d;
	s8			rxsc_80;
	s8			rxsc_40;
	s8			rxsc_20;
	s8			rxsc_l;
	u64			rssi_trsw;
	u64			rssi_trsw_h;
	u64			rssi_trsw_l;
	u64			rssi_trsw_iso;
	u8			tx_ant_status; /*TX path enable*/
	u8			rx_ant_status; /*RX path enable*/
	#ifdef PHYDM_COMPILE_ABOVE_4SS
	enum bb_path		tx_4ss_status; /*@Use N-X for 4STS rate*/
	#endif
	#ifdef PHYDM_COMPILE_ABOVE_3SS
	enum bb_path		tx_3ss_status; /*@Use N-X for 3STS rate*/
	#endif
	#ifdef PHYDM_COMPILE_ABOVE_2SS
	enum bb_path		tx_2ss_status; /*@Use N-X for 2STS rate*/
	#endif
	enum bb_path		tx_1ss_status; /*@Use N-X for 1STS rate*/
	u8			cck_lna_idx;
	u8			cck_vga_idx;
	u8			curr_station_id;
	u8			ofdm_agc_idx[4];
	u8			rx_rate;
	u8			rate_ss;
	u8			tx_rate;
	u8			linked_interval;
	u8			pre_channel;
	u32			txagc_offset_value_a;
	boolean			is_txagc_offset_positive_a;
	u32			txagc_offset_value_b;
	boolean			is_txagc_offset_positive_b;
	u8			ap_total_num;
	boolean			flatness_type;
	/*@[traffic]*/
	u8			traffic_load;
	u8			pre_traffic_load;
	u32			tx_tp;			/*@Mbps*/
	u32			rx_tp;			/*@Mbps*/
	u32			total_tp;		/*@Mbps*/
	u8			txrx_state_all;		/*@0:tx, 1:rx, 2:bi-dir*/
	u64			cur_tx_ok_cnt;
	u64			cur_rx_ok_cnt;
	u64			last_tx_ok_cnt;
	u64			last_rx_ok_cnt;
	u16			consecutive_idlel_time;	/*@unit: second*/
	/*@---------------------------*/
	boolean			is_bb_swing_offset_positive_a;
	boolean			is_bb_swing_offset_positive_b;

	/*@[DIG]*/
	boolean			MPDIG_2G;		/*off MPDIG*/
	u8			times_2g;		/*@for MP DIG*/
	u8			force_igi;		/*@for debug*/

	/*@[TDMA-DIG]*/
	u8			tdma_dig_timer_ms;
	u8			tdma_dig_state_number;
	u8			tdma_dig_low_upper_bond;
	u8			force_tdma_low_igi;
	u8			force_tdma_high_igi;
	u8			fix_expire_to_zero;
	boolean			original_dig_restore;
	/*@---------------------------*/

	/*@[AntDiv]*/
	u8			ant_div_type;
	u8			antdiv_rssi;
	u8			fat_comb_a;
	u8			fat_comb_b;
	u8			antdiv_intvl;
	u8			antdiv_delay;
	u8			ant_type;
	u8			ant_type2;
	u8			pre_ant_type;
	u8			pre_ant_type2;
	u8			antdiv_period;
	u8			evm_antdiv_period;
	u8			antdiv_select;
	u8			antdiv_train_num; /*@training time for each antenna in EVM method*/
	u8			stop_antdiv_rssi_th;
	u16			stop_antdiv_tp_diff_th;
	u16			stop_antdiv_tp_th;
	u8			antdiv_tp_period;
	u16			tp_active_th;
	u8			tp_active_occur;
	u8			path_select;
	u8			antdiv_evm_en;
	u8			bdc_holdstate;
	u8			antdiv_counter;
	/*@---------------------------*/

	u8			ndpa_period;
	boolean			h2c_rarpt_connect;
	boolean			cck_agc_report_type; /*@1:4bit LNA, 0:3bit LNA */
	u8			print_agc;
	u8			la_mode;
	/*@---8821C Antenna and RF Set BTG/WLG/WLA Select---------------*/
	u8			current_rf_set_8821c;
	u8			default_rf_set_8821c;
	u8			current_ant_num_8821c;
	u8			default_ant_num_8821c;
	u8			rfe_type_expand;
	/*@-----------------------------------------------------------*/
	/*@---For Adaptivtiy---------------------------------------------*/
	s8			TH_L2H_default;
	s8			th_edcca_hl_diff_default;
	s8			th_l2h_ini;
	s8			th_edcca_hl_diff;
	boolean			carrier_sense_enable;
	/*@-----------------------------------------------------------*/
	u8			pre_dbg_priority;
	u8			nbi_set_result;
	u8			c2h_cmd_start;
	u8			fw_debug_trace[60];
	u8			pre_c2h_seq;
	boolean			fw_buff_is_enpty;
	u32			data_frame_num;
	/*@--- for spur detection ---------------------------------------*/
	boolean			en_reg_mntr_bb;
	boolean			en_reg_mntr_rf;
	boolean			en_reg_mntr_mac;
	boolean			en_reg_mntr_byte;
	/*@--------------------------------------------------------------*/
#if (RTL8814B_SUPPORT || RTL8812F_SUPPORT)
	/*@--- for spur detection ---------------------------------------*/
	u8			dsde_sel;
	u8			nbi_path_sel;
	u8			csi_wgt;
	/*@------------------------------------------*/
#endif
	/*@--- for noise detection ---------------------------------------*/
	boolean			is_noisy_state;
	boolean			noisy_decision; /*@b_noisy*/
	boolean			pre_b_noisy;
	u32			noisy_decision_smooth;
	/*@-----------------------------------------------------------*/

	/*@--- for MCC ant weighting ------------------------------------*/
	boolean			is_stop_dym_ant_weighting;
	/*@-----------------------------------------------------------*/

	boolean			is_disable_dym_ecs;
	boolean			is_disable_dym_ant_weighting;
	struct cmn_sta_info	*phydm_sta_info[ODM_ASSOCIATE_ENTRY_NUM];
	u8			phydm_macid_table[ODM_ASSOCIATE_ENTRY_NUM];/*@sta_idx = phydm_macid_table[HW_macid]*/

#if (RATE_ADAPTIVE_SUPPORT)
	u16			currmin_rpt_time;
	struct _phydm_txstatistic_ hw_stats;
	struct _odm_ra_info_	ra_info[ODM_ASSOCIATE_ENTRY_NUM];
/*Use mac_id as array index. STA mac_id=0*/
/*VWiFi Client mac_id={1, ODM_ASSOCIATE_ENTRY_NUM-1} //YJ,add,120119*/
#endif
	/*@2012/02/14 MH Add to share 88E ra with other SW team*/
	/*We need to colelct all support abilit to a proper area.*/
	boolean			ra_support88e;
	boolean			*is_driver_stopped;
	boolean			*is_driver_is_going_to_pnp_set_power_sleep;
	boolean			*pinit_adpt_in_progress;
	boolean			is_user_assign_level;
	u8			RSSI_BT;		/*@come from BT*/

	/*@---PSD Relative ---------------------------------------------*/
	boolean			is_psd_in_process;
	boolean			is_psd_active;
	/*@-----------------------------------------------------------*/

	boolean			bsomlenabled;	/* @D-SoML control */
	u8			no_ndp_cnts;
	u8			ndp_cnt_pre;
	boolean			is_beamformed;
	u8			linked_bf_support;
	boolean			bhtstfdisabled;	/* @dynamic HTSTF gain control*/
	u32			n_iqk_cnt;
	u32			n_iqk_ok_cnt;
	u32			n_iqk_fail_cnt;

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	boolean			config_bbrf;
#endif
	boolean			is_disable_power_training;
	boolean			is_bt_continuous_turn;
	u8			enhance_pwr_th[3];
	u8			set_pwr_th[3];
	/*@----------Dyn Tx Pwr ---------------------------------------*/
#ifdef BB_RAM_SUPPORT
	struct phydm_bb_ram_ctrl p_bb_ram_ctrl;
#endif
	u8			dynamic_tx_high_power_lvl;
	void	(*fill_desc_dyntxpwr)(void *dm, u8 *desc, u8 dyn_tx_power);
	u8			last_dtp_lvl;
	u8			min_power_index;
	u32			tx_agc_ofdm_18_6;
	/*-------------------------------------------------------------*/
	u8			rx_pkt_type;

#ifdef CONFIG_PHYDM_DFS_MASTER
	u8			dfs_region_domain;
	u8			*dfs_master_enabled;
	/*@---phydm_radar_detect_with_dbg_parm start --------------------*/
	u8			radar_detect_dbg_parm_en;
	u32			radar_detect_reg_918;
	u32			radar_detect_reg_91c;
	u32			radar_detect_reg_920;
	u32			radar_detect_reg_924;

	u32			radar_detect_reg_a40;
	u32			radar_detect_reg_a44;
	u32			radar_detect_reg_a48;
	u32			radar_detect_reg_a4c;
	u32			radar_detect_reg_a50;
	u32			radar_detect_reg_a54;

	u32			radar_detect_reg_f54;
	u32			radar_detect_reg_f58;
	u32			radar_detect_reg_f5c;
	u32			radar_detect_reg_f70;
	u32			radar_detect_reg_f74;
	/*@---For zero-wait DFS---------------------------------------*/
	boolean			seg1_dfs_flag;
	/*@-----------------------------------------------------------*/
/*@-----------------------------------------------------------*/
#endif

/*@=== RTL8721D ===*/
#if (RTL8721D_SUPPORT)
	boolean			cbw20_adc80;
	boolean			invalid_mode;
	u8			power_voltage;
	u8			cca_cbw20_lev;
	u8			cca_cbw40_lev;
	u8			antdiv_gpio;
	u8			peak_detect_mode;
#endif

/*@=== PHYDM Timer ========================================== (start)*/

	struct phydm_timer_list	mpt_dig_timer;
	struct phydm_timer_list	fast_ant_training_timer;
#ifdef ODM_EVM_ENHANCE_ANTDIV
	struct phydm_timer_list	evm_fast_ant_training_timer;
#endif
#ifdef PHYDM_TDMA_DIG_SUPPORT
	struct phydm_timer_list tdma_dig_timer;
#endif
	struct phydm_timer_list	sbdcnt_timer;

/*@=== PHYDM Workitem ======================================= (start)*/

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#if USE_WORKITEM
	RT_WORK_ITEM		fast_ant_training_workitem;
	RT_WORK_ITEM		ra_rpt_workitem;
	RT_WORK_ITEM		sbdcnt_workitem;
	RT_WORK_ITEM		phydm_evm_antdiv_workitem;
#ifdef PHYDM_TDMA_DIG_SUPPORT
	RT_WORK_ITEM		phydm_tdma_dig_workitem;
#endif
#endif
#endif

/*@=== PHYDM Structure ======================================== (start)*/
	struct	phydm_func_poiner	phydm_func_handler;
	struct	phydm_iot_center	iot_table;

#ifdef ODM_CONFIG_BT_COEXIST
	struct	phydm_bt_info		bt_info_table;
#endif

	struct	pkt_process_info	pkt_proc_struct;
	struct phydm_adaptivity_struct	adaptivity;
#ifdef CONFIG_PHYDM_DFS_MASTER
	struct _DFS_STATISTICS		dfs;
#endif
	struct odm_noise_monitor	noise_level;
	struct odm_phy_dbg_info		phy_dbg_info;
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct odm_phy_dbg_info		phy_dbg_info_win_bkp;
#endif
#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	struct phydm_bf_rate_info_jgr3 bf_rate_info_jgr3;
#endif

#ifdef CONFIG_ADAPTIVE_SOML
	struct adaptive_soml		dm_soml_table;
#endif

#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct _BF_DIV_COEX_		dm_bdc_table;
	#endif

	#if (defined(CONFIG_HL_SMART_ANTENNA))
	struct smt_ant_honbo		dm_sat_table;
	#endif
#endif

#if (defined(CONFIG_SMART_ANTENNA))
	struct smt_ant			smtant_table;
#endif

	struct _hal_rf_			rf_table;	/*@for HALRF function*/
	struct dm_rf_calibration_struct	rf_calibrate_info;
	struct dm_iqk_info		IQK_info;
	struct dm_dpk_info		dpk_info;
	struct dm_dack_info		dack_info;
#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	struct phydm_fat_struct		dm_fat_table;
	struct sw_antenna_switch	dm_swat_table;
#endif
	struct phydm_dig_struct		dm_dig_table;

#ifdef PHYDM_SUPPORT_CCKPD
	struct phydm_cckpd_struct	dm_cckpd_table;

	#ifdef PHYDM_DCC_ENHANCE
	struct phydm_dcc_struct		dm_dcc_info; /*dig cckpd coex*/
	#endif
#endif

#ifdef PHYDM_LNA_SAT_CHK_SUPPORT
	struct phydm_lna_sat_t		dm_lna_sat_info;
#endif

#ifdef CONFIG_MCC_DM
	struct _phydm_mcc_dm_ mcc_dm;
#endif

#ifdef PHYDM_PRIMARY_CCA
	struct phydm_pricca_struct	dm_pri_cca;
#endif

	struct ra_table			dm_ra_table;
	struct phydm_fa_struct		false_alm_cnt;
#ifdef PHYDM_TDMA_DIG_SUPPORT
	struct phydm_fa_acc_struct	false_alm_cnt_acc;
#ifdef IS_USE_NEW_TDMA
	struct phydm_fa_acc_struct	false_alm_cnt_acc_low;
#endif
#endif
	struct phydm_cfo_track_struct	dm_cfo_track;
	struct ccx_info			dm_ccx_info;

	struct odm_power_trim_data	power_trim_data;
#if (RTL8822B_SUPPORT)
	struct drp_rtl8822b_struct	phydm_rtl8822b;
#endif

#ifdef CONFIG_PSD_TOOL
	struct psd_info			dm_psd_table;
#endif

#if (PHYDM_LA_MODE_SUPPORT)
	struct rt_adcsmp		adcsmp;
#endif

#if (defined(CONFIG_PATH_DIVERSITY))
	struct _ODM_PATH_DIVERSITY_	dm_path_div;
#endif

#if (defined(CONFIG_ANT_DETECTION))
	struct _ANT_DETECTED_INFO	ant_detected_info;
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#ifdef PHYDM_BEAMFORMING_SUPPORT
	struct _RT_BEAMFORMING_INFO 	beamforming_info;
#endif
#endif
#ifdef PHYDM_AUTO_DEGBUG
	struct	phydm_auto_dbg_struct	auto_dbg_table;
#endif

	struct	phydm_pause_lv		pause_lv_table;
	struct	phydm_api_stuc		api_table;
#ifdef PHYDM_POWER_TRAINING_SUPPORT
	struct	phydm_pow_train_stuc	pow_train_table;
#endif

#ifdef PHYDM_PMAC_TX_SETTING_SUPPORT
	struct phydm_pmac_tx dm_pmac_tx_table;
#endif

#ifdef PHYDM_MP_SUPPORT
	struct phydm_mp dm_mp_table;
#endif

#ifdef PHYDM_CCK_RX_PATHDIV_SUPPORT
	struct phydm_cck_rx_pathdiv dm_cck_rx_pathdiv_table;
#endif
/*@==========================================================*/

#if (RTL8822C_SUPPORT || RTL8812F_SUPPORT || RTL8197G_SUPPORT)
	/*@-------------------phydm_phystatus report --------------------*/
	struct phydm_physts dm_physts_table;
#endif

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)

#if (RT_PLATFORM != PLATFORM_LINUX)
} dm_struct;	/*@DM_Dynamic_Mechanism_Structure*/
#else
};
#endif

#else	/*@for AP,CE Team*/
};
#endif

enum phydm_adv_ota {
	PHYDM_PATHB_1RCCA		= BIT(0),
	PHYDM_HP_OTA_SETTING_A		= BIT(1),
	PHYDM_HP_OTA_SETTING_B		= BIT(2),
	PHYDM_ASUS_OTA_SETTING		= BIT(3),
	PHYDM_ASUS_OTA_SETTING_CCK_PATH = BIT(4),
	PHYDM_HP_OTA_SETTING_CCK_PATH	= BIT(5),
	PHYDM_LENOVO_OTA_SETTING_NBI_CSI = BIT(6),

};

enum phydm_bb_op_mode {
	PHYDM_PERFORMANCE_MODE	= 0,		/*Service one device*/
	PHYDM_BALANCE_MODE	= 1,		/*@Service more than one device*/
};

enum phydm_structure_type {
	PHYDM_FALSEALMCNT,
	PHYDM_CFOTRACK,
	PHYDM_ADAPTIVITY,
	PHYDM_DFS,
	PHYDM_ROMINFO,

};

enum odm_bb_config_type {
	CONFIG_BB_PHY_REG,
	CONFIG_BB_AGC_TAB,
	CONFIG_BB_AGC_TAB_2G,
	CONFIG_BB_AGC_TAB_5G,
	CONFIG_BB_PHY_REG_PG,
	CONFIG_BB_PHY_REG_MP,
	CONFIG_BB_AGC_TAB_DIFF,
	CONFIG_BB_RF_CAL_INIT,
};

enum odm_rf_config_type {
	CONFIG_RF_RADIO,
	CONFIG_RF_TXPWR_LMT,
	CONFIG_RF_SYN_RADIO,
};

enum odm_fw_config_type {
	CONFIG_FW_NIC,
	CONFIG_FW_NIC_2,
	CONFIG_FW_AP,
	CONFIG_FW_AP_2,
	CONFIG_FW_MP,
	CONFIG_FW_WOWLAN,
	CONFIG_FW_WOWLAN_2,
	CONFIG_FW_AP_WOWLAN,
	CONFIG_FW_BT,
};

/*status code*/
#if (DM_ODM_SUPPORT_TYPE != ODM_WIN)
enum rt_status {
	RT_STATUS_SUCCESS,
	RT_STATUS_FAILURE,
	RT_STATUS_PENDING,
	RT_STATUS_RESOURCE,
	RT_STATUS_INVALID_CONTEXT,
	RT_STATUS_INVALID_PARAMETER,
	RT_STATUS_NOT_SUPPORT,
	RT_STATUS_OS_API_FAILED,
};
#endif	/*@end of enum rt_status definition*/

void
phydm_watchdog_lps(struct dm_struct *dm);

void
phydm_watchdog_lps_32k(struct dm_struct *dm);

void
phydm_txcurrentcalibration(struct dm_struct *dm);

void
phydm_dm_early_init(struct dm_struct *dm);

enum phydm_init_result
odm_dm_init(struct dm_struct *dm);

void
odm_dm_reset(struct dm_struct *dm);

void
phydm_fwoffload_ability_init(struct dm_struct *dm,
			     enum phydm_offload_ability offload_ability);

void
phydm_fwoffload_ability_clear(struct dm_struct *dm,
			      enum phydm_offload_ability offload_ability);

void
phydm_supportability_en(void *dm_void, char input[][16], u32 *_used,
			char *output, u32 *_out_len);

void
phydm_pause_dm_watchdog(void *dm_void, enum phydm_pause_type pause_type);

void
phydm_watchdog(struct dm_struct *dm);

void
phydm_watchdog_mp(struct dm_struct *dm);

void
phydm_pause_func_init(void *dm_void);

u8
phydm_pause_func(void *dm_void, enum phydm_func_idx pause_func,
		 enum phydm_pause_type pause_type,
		 enum phydm_pause_level pause_lv, u8 val_lehgth, u32 *val_buf);

void
phydm_pause_func_console(void *dm_void, char input[][16], u32 *_used,
			 char *output, u32 *_out_len);

void phydm_pause_dm_by_asso_pkt(struct dm_struct *dm,
				enum phydm_pause_type pause_type, u8 rssi);

void phydm_fw_dm_ctrl_en(void *dm_void, enum phydm_func_idx fun_idx,
			 boolean enable);

void
odm_cmn_info_init(struct dm_struct *dm, enum odm_cmninfo cmn_info, u64 value);

void
odm_cmn_info_hook(struct dm_struct *dm, enum odm_cmninfo cmn_info, void *value);

void
odm_cmn_info_update(struct dm_struct *dm, u32 cmn_info, u64 value);

u32
phydm_cmn_info_query(struct dm_struct *dm, enum phydm_info_query info_type);

void
odm_init_all_timers(struct dm_struct *dm);

void
odm_cancel_all_timers(struct dm_struct *dm);

void
odm_release_all_timers(struct dm_struct *dm);

void *
phydm_get_structure(struct dm_struct *dm, u8 structure_type);

void
phydm_dc_cancellation(struct dm_struct *dm);

void
phydm_receiver_blocking(void *dm_void);

void
phydm_dyn_bw_indication(void *dm_void);

void
phydm_iot_patch_id_update(void *dm_void, u32 iot_idx, boolean en);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void
odm_init_all_work_items(
	struct dm_struct	*dm
);
void
odm_free_all_work_items(
	struct dm_struct	*dm
);
#endif	/*@#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)*/

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
void
odm_dtc(struct dm_struct *dm);
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
void
odm_init_all_threads(
	struct dm_struct	*dm
);

void
odm_stop_all_threads(
	struct dm_struct	*dm
);
#endif

#endif
