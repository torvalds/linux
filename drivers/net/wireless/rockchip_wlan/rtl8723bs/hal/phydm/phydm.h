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


#ifndef	__HALDMOUTSRC_H__
#define __HALDMOUTSRC_H__

/*============================================================*/
/*include files*/
/*============================================================*/
/*PHYDM header*/
#include "phydm_pre_define.h"
#include "phydm_dig.h"
#include "phydm_pathdiv.h"
#include "phydm_antdiv.h"
#include "phydm_soml.h"
#include "phydm_smt_ant.h"
#include "phydm_antdect.h"
#include "phydm_rainfo.h"
#include "phydm_dynamictxpower.h"
#include "phydm_cfotracking.h"
#include "phydm_acs.h"
#include "phydm_adaptivity.h"
#include "phydm_dfs.h"
#include "phydm_ccx.h"
#include "txbf/phydm_hal_txbf_api.h"
#include "phydm_adc_sampling.h"
#include "phydm_dynamic_rx_path.h"
#include "phydm_psd.h"
#include "phydm_primary_cca.h"
#include "phydm_cck_pd.h"
#include "phydm_rssi_monitor.h"
#include "phydm_auto_dbg.h"
#include "phydm_math_lib.h"
#include "phydm_noisemonitor.h"
#include "phydm_api.h"
#include "phydm_pow_train.h"
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	#include "phydm_beamforming.h"
#endif

/*HALRF header*/
#include "halrf/halrf_iqk.h"
#include "halrf/halrf.h"
#include "halrf/halrf_powertracking.h"
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	#include "halrf/halphyrf_ap.h"
#elif(DM_ODM_SUPPORT_TYPE & (ODM_CE))
	#include "halrf/halphyrf_ce.h"
#elif (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	#include "halrf/halphyrf_win.h"
#endif

extern const u16 phy_rate_table[28];

/*============================================================*/
/*Definition */
/*============================================================*/

/* Traffic load decision */
#define	TRAFFIC_ULTRA_LOW	1
#define	TRAFFIC_LOW			2
#define	TRAFFIC_MID			3
#define	TRAFFIC_HIGH			4

#define	NONE			0

#define MAX_2(_x_, _y_)	(((_x_)>(_y_))? (_x_) : (_y_))
#define MIN_2(_x_, _y_)	(((_x_)<(_y_))? (_x_) : (_y_))

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	#define PHYDM_WATCH_DOG_PERIOD	1 /*second*/
#else
	#define PHYDM_WATCH_DOG_PERIOD	2 /*second*/
#endif

/*============================================================*/
/*structure and define*/
/*============================================================*/

#define		dm_type_by_fw			0
#define		dm_type_by_driver		1

struct phydm_phystatus_statistic {
	
	/*[CCK]*/
	u32		rssi_cck_sum;
	u32		rssi_cck_cnt;
	/*[OFDM]*/	
	u32		rssi_ofdm_sum;
	u32		rssi_ofdm_cnt;
	u32		evm_ofdm_sum;
	u32		snr_ofdm_sum;
	/*[1SS]*/
	u32		rssi_1ss_cnt;
	u32		rssi_1ss_sum;
	u32		evm_1ss_sum;
	u32		snr_1ss_sum;
	/*[2SS]*/
	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	u32		rssi_2ss_cnt;
	u32		rssi_2ss_sum[2];
	u32		evm_2ss_sum[2];
	u32		snr_2ss_sum[2];
	#endif
	/*[3SS]*/
	#if (defined(PHYDM_COMPILE_ABOVE_3SS))
	u32		rssi_3ss_cnt;
	u32		rssi_3ss_sum[3];
	u32		evm_3ss_sum[3];
	u32		snr_3ss_sum[3];
	#endif
	/*[4SS]*/
	#if (defined(PHYDM_COMPILE_ABOVE_4SS))
	u32		rssi_4ss_cnt;
	u32		rssi_4ss_sum[4];
	u32		evm_4ss_sum[4];	
	u32		snr_4ss_sum[4];
	#endif
};

struct phydm_phystatus_avg {
	
	/*[CCK]*/
	u8		rssi_cck_avg;
	/*[OFDM]*/
	u8		rssi_ofdm_avg;
	u8		evm_ofdm_avg;
	u8		snr_ofdm_avg;
	/*[1SS]*/
	u8		rssi_1ss_avg;
	u8		evm_1ss_avg;
	u8		snr_1ss_avg;
	/*[2SS]*/
	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	u8		rssi_2ss_avg[2];
	u8		evm_2ss_avg[2];
	u8		snr_2ss_avg[2];
	#endif
	/*[3SS]*/
	#if (defined(PHYDM_COMPILE_ABOVE_3SS))
	u8		rssi_3ss_avg[3];
	u8		evm_3ss_avg[3];
	u8		snr_3ss_avg[3];
	#endif
	/*[4SS]*/
	#if (defined(PHYDM_COMPILE_ABOVE_4SS))
	u8		rssi_4ss_avg[4];
	u8		evm_4ss_avg[4];	
	u8		snr_4ss_avg[4];
	#endif
};

struct _odm_phy_dbg_info_ {
	/*ODM Write,debug info*/
	s8		rx_snr_db[4];
	u32		num_qry_phy_status;
	u32		num_qry_phy_status_cck;
	u32		num_qry_phy_status_ofdm;
#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT == 1)
	u32		num_qry_mu_pkt;
	u32		num_qry_bf_pkt;
	u32		num_qry_mu_vht_pkt[40];
	boolean	is_ldpc_pkt;
	boolean	is_stbc_pkt;
	u8		num_of_ppdu[4];
	u8		gid_num[4];
#endif
	u8		num_qry_beacon_pkt;
	/* Others */
	s32		rx_evm[4];

	u16		num_qry_legacy_pkt[LEGACY_RATE_NUM];
	u16		num_qry_ht_pkt[HT_RATE_NUM];
	u8		ht_pkt_not_zero;
	#if	ODM_IC_11AC_SERIES_SUPPORT
	u16		num_qry_vht_pkt[VHT_RATE_NUM];
	u8		vht_pkt_not_zero;
	#endif
	struct phydm_phystatus_statistic	phystatus_statistic_info;
	struct phydm_phystatus_avg	phystatus_statistic_avg;
};

enum odm_cmninfo_e {
	/*Fixed value*/
	/*-----------HOOK BEFORE REG INIT-----------*/
	ODM_CMNINFO_PLATFORM = 0,
	ODM_CMNINFO_ABILITY,
	ODM_CMNINFO_INTERFACE,
	ODM_CMNINFO_MP_TEST_CHIP,
	ODM_CMNINFO_IC_TYPE,
	ODM_CMNINFO_CUT_VER,
	ODM_CMNINFO_FAB_VER,
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
	ODM_CMNINFO_EXT_TRSW,
	ODM_CMNINFO_EXT_LNA_GAIN,
	ODM_CMNINFO_PATCH_ID,
	ODM_CMNINFO_BINHCT_TEST,
	ODM_CMNINFO_BWIFI_TEST,
	ODM_CMNINFO_SMART_CONCURRENT,
	ODM_CMNINFO_CONFIG_BB_RF,
	ODM_CMNINFO_DOMAIN_CODE_2G,
	ODM_CMNINFO_DOMAIN_CODE_5G,
	ODM_CMNINFO_IQKPAOFF,
	ODM_CMNINFO_HUBUSBMODE,
	ODM_CMNINFO_FWDWRSVDPAGEINPROGRESS,
	ODM_CMNINFO_TX_TP,
	ODM_CMNINFO_RX_TP,
	ODM_CMNINFO_SOUNDING_SEQ,
	ODM_CMNINFO_REGRFKFREEENABLE,
	ODM_CMNINFO_RFKFREEENABLE,
	ODM_CMNINFO_NORMAL_RX_PATH_CHANGE,
	ODM_CMNINFO_EFUSE0X3D8,
	ODM_CMNINFO_EFUSE0X3D7,
	ODM_CMNINFO_SOFT_AP_SPECIAL_SETTING,
	ODM_CMNINFO_ADVANCE_OTA,
	ODM_CMNINFO_HP_HWID,
	/*-----------HOOK BEFORE REG INIT-----------*/

	/*Dynamic value:*/

	/*--------- POINTER REFERENCE-----------*/
	ODM_CMNINFO_TX_UNI,
	ODM_CMNINFO_RX_UNI,
	ODM_CMNINFO_BAND,
	ODM_CMNINFO_SEC_CHNL_OFFSET,
	ODM_CMNINFO_SEC_MODE,
	ODM_CMNINFO_BW,
	ODM_CMNINFO_CHNL,
	ODM_CMNINFO_FORCED_RATE,
	ODM_CMNINFO_ANT_DIV,
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
	/*--------- POINTER REFERENCE-----------*/

	/*------------CALL BY VALUE-------------*/
	ODM_CMNINFO_WIFI_DIRECT,
	ODM_CMNINFO_WIFI_DISPLAY,
	ODM_CMNINFO_LINK_IN_PROGRESS,
	ODM_CMNINFO_LINK,
	ODM_CMNINFO_CMW500LINK,
	ODM_CMNINFO_STATION_STATE,
	ODM_CMNINFO_RSSI_MIN,
	ODM_CMNINFO_RSSI_MIN_BY_PATH,
	ODM_CMNINFO_DBG_COMP,
	ODM_CMNINFO_DBG_LEVEL,
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
	/*------------CALL BY VALUE-------------*/

	/*Dynamic ptr array hook itms.*/
	ODM_CMNINFO_STA_STATUS,
	ODM_CMNINFO_MAX,

};

enum phydm_rfe_bb_source_sel {
	PAPE_2G	= 0,
	PAPE_5G	= 1,
	LNA0N_2G	= 2,
	LNAON_5G	= 3,
	TRSW		= 4,
	TRSW_B		= 5,
	GNT_BT		= 6,
	ZERO		= 7,
	ANTSEL_0	= 8,
	ANTSEL_1	= 9,
	ANTSEL_2	= 0xa,
	ANTSEL_3	= 0xb,
	ANTSEL_4	= 0xc,
	ANTSEL_5	= 0xd,
	ANTSEL_6	= 0xe,
	ANTSEL_7	= 0xf
};

enum phydm_info_query_e {
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
};

enum phydm_api_e {

	PHYDM_API_NBI			= 1,
	PHYDM_API_CSI_MASK,

};

enum phydm_func_idx_e { /*F_XXX = PHYDM XXX function*/

	F00_DIG			= 0,
	F01_RA_MASK		= 1,
	F02_DYN_TXPWR		= 2,
	F03_FA_CNT			= 3,
	F04_RSSI_MNTR		= 4,
	F05_CCK_PD			= 5,
	F06_ANT_DIV		= 6,
	F07_SMT_ANT		= 7,
	F08_PWR_TRAIN		= 8,
	F09_RA				= 9,
	F10_PATH_DIV		= 10,
	F11_DFS			= 11,
	F12_DYN_ARFR		= 12,
	F13_ADPTVTY		= 13,
	F14_CFO_TRK		= 14,
	F15_ENV_MNTR		= 15,
	F16_PRI_CCA		= 16,
	F17_ADPTV_SOML	= 17,
	F18_LNA_SAT_CHK = 18,
	/*BIT18*/
	/*BIT19*/
	F20_DYN_RX_PATH	= 20
};

/*=[PHYDM supportability]==========================================*/
enum odm_ability_e {

	ODM_BB_DIG				= BIT(F00_DIG),
	ODM_BB_RA_MASK			= BIT(F01_RA_MASK),
	ODM_BB_DYNAMIC_TXPWR	= BIT(F02_DYN_TXPWR),
	ODM_BB_FA_CNT				= BIT(F03_FA_CNT),
	ODM_BB_RSSI_MONITOR		= BIT(F04_RSSI_MNTR),
	ODM_BB_CCK_PD				= BIT(F05_CCK_PD),
	ODM_BB_ANT_DIV			= BIT(F06_ANT_DIV),
	ODM_BB_SMT_ANT			= BIT(F07_SMT_ANT),
	ODM_BB_PWR_TRAIN			= BIT(F08_PWR_TRAIN),
	ODM_BB_RATE_ADAPTIVE		= BIT(F09_RA),
	ODM_BB_PATH_DIV			= BIT(F10_PATH_DIV),
	ODM_BB_DFS				= BIT(F11_DFS),
	ODM_BB_DYNAMIC_ARFR		= BIT(F12_DYN_ARFR),
	ODM_BB_ADAPTIVITY			= BIT(F13_ADPTVTY),
	ODM_BB_CFO_TRACKING		= BIT(F14_CFO_TRK),
	ODM_BB_ENV_MONITOR		= BIT(F15_ENV_MNTR),
	ODM_BB_PRIMARY_CCA		= BIT(F16_PRI_CCA),
	ODM_BB_ADAPTIVE_SOML	= BIT(F17_ADPTV_SOML),
	ODM_BB_LNA_SAT_CHK		= BIT(F18_LNA_SAT_CHK),
	/*BIT19*/
	ODM_BB_DYNAMIC_RX_PATH	= BIT(F20_DYN_RX_PATH)
};

/*=[PHYDM Debug Component]=====================================*/
enum phydm_dbg_comp {
	/*BB Driver Functions*/
	DBG_DIG			= BIT(F00_DIG),
	DBG_RA_MASK		= BIT(F01_RA_MASK),
	DBG_DYN_TXPWR	= BIT(F02_DYN_TXPWR),
	DBG_FA_CNT		= BIT(F03_FA_CNT),
	DBG_RSSI_MNTR		= BIT(F04_RSSI_MNTR),
	DBG_CCKPD			= BIT(F05_CCK_PD),
	DBG_ANT_DIV		= BIT(F06_ANT_DIV),
	DBG_SMT_ANT		= BIT(F07_SMT_ANT),
	DBG_PWR_TRAIN		= BIT(F08_PWR_TRAIN),
	DBG_RA				= BIT(F09_RA),
	DBG_PATH_DIV		= BIT(F10_PATH_DIV),
	DBG_DFS			= BIT(F11_DFS),
	DBG_DYN_ARFR		= BIT(F12_DYN_ARFR),
	DBG_ADPTVTY		= BIT(F13_ADPTVTY),
	DBG_CFO_TRK		= BIT(F14_CFO_TRK), 
	DBG_ENV_MNTR		= BIT(F15_ENV_MNTR),
	DBG_PRI_CCA		= BIT(F16_PRI_CCA),
	DBG_ADPTV_SOML	= BIT(F17_ADPTV_SOML),
	DBG_LNA_SAT_CHK = BIT(F18_LNA_SAT_CHK),
	/*BIT19*/
	DBG_DYN_RX_PATH	= BIT(F20_DYN_RX_PATH),
	/*Neet to re-arrange*/
	DBG_TMP			= BIT(21),
	DBG_FW_TRACE		= BIT(22),
	DBG_TXBF			= BIT(23),
	DBG_COMMON_FLOW	= BIT(24),
	ODM_COMP_TX_PWR_TRACK	= BIT(25),
	ODM_COMP_CALIBRATION		= BIT(26),
	ODM_COMP_MP				= BIT(27),
	ODM_PHY_CONFIG			= BIT(28),
	ODM_COMP_INIT				= BIT(29),
	ODM_COMP_COMMON			= BIT(30),
	ODM_COMP_API				= BIT(31)
};

/*=========================================================*/

/*ODM_CMNINFO_ONE_PATH_CCA*/
enum odm_cca_path_e {
	ODM_CCA_2R		= 0,
	ODM_CCA_1R_A		= 1,
	ODM_CCA_1R_B		= 2,
};

enum phy_reg_pg_type {
	PHY_REG_PG_RELATIVE_VALUE	= 0,
	PHY_REG_PG_EXACT_VALUE		= 1
};

enum phydm_offload_ability {
	PHYDM_PHY_PARAM_OFFLOAD = BIT(0),
	PHYDM_RF_IQK_OFFLOAD = BIT(1),
};

struct phydm_pause_lv {
	s8	lv_dig;
	s8	lv_cckpd;
	s8	lv_antdiv;
	s8	lv_adapt;
};

struct phydm_func_poiner {
	void	(*pause_phydm_handler)(void	 *p_dm_void, u32 *val_buf, u8 val_len);
};

struct pkt_process_info {
	u8	phystatus_smp_mode_en; /*send phystatus every sampling time*/
	u8	pre_ppdu_cnt;
	u8	lna_idx;
	u8	vga_idx;
};

#ifdef ODM_CONFIG_BT_COEXIST
struct	phydm_bt_info {
	boolean		is_bt_enabled;			/*BT is enabled*/
	boolean		is_bt_connect_process;	/*BT HS is under connection progress.*/
	u8			bt_hs_rssi;				/*BT HS mode wifi rssi value.*/
	boolean		is_bt_hs_operation;		/*BT HS mode is under progress*/
	boolean		is_bt_limited_dig;		/*BT is busy.*/
};
#endif

struct	phydm_iot_center {
	boolean		is_linked_cmw500;
	u8			win_patch_id;		/*Customer ID*/
	u32			phydm_patch_id;

};

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	#if (RT_PLATFORM != PLATFORM_LINUX)
		typedef
	#endif

	struct PHY_DM_STRUCT
#else/*for AP, CE Team*/
	struct PHY_DM_STRUCT
#endif
{
	/*Add for different team use temporarily*/
	struct _ADAPTER		*adapter;		/*For CE/NIC team*/
	struct rtl8192cd_priv	*priv;			/*For AP team*/
	/*WHen you use adapter or priv pointer, you must make sure the pointer is ready.*/
	boolean		odm_ready;
	enum phy_reg_pg_type	phy_reg_pg_value_type;
	u8			phy_reg_pg_version;
	u64			support_ability;	/*PHYDM function Supportability*/
	u64			pause_ability;	/*PHYDM function pause Supportability*/
	u64			debug_components;
	u32			fw_debug_components;
	u32			debug_level;
	u32			num_qry_phy_status_all;		/*CCK + OFDM*/
	u32			last_num_qry_phy_status_all;
	u32			rx_pwdb_ave;
	boolean		is_init_hw_info_by_rfe;

	/*------ ODM HANDLE, DRIVER NEEDS NOT TO HOOK------*/
	boolean		is_cck_high_power;
	u8			rf_path_rx_enable;
	/*------ ODM HANDLE, DRIVER NEEDS NOT TO HOOK------*/

	/* COMMON INFORMATION */

	/*Init value*/
	/*-----------HOOK BEFORE REG INIT-----------*/

	u8			support_platform;/*PHYDM Platform info WIN/AP/CE = 1/2/3 */
	u8			normal_rx_path;
	u8			support_interface;/*PHYDM PCIE/USB/SDIO = 1/2/3*/
	u32			support_ic_type;	/*PHYDM supported IC*/
	u8			cut_version;		/*cut version TestChip/A-cut/B-cut... = 0/1/2/3/...*/
	u8			fab_version;		/*Fab version TSMC/UMC = 0/1*/
	u8			rf_type;			/*RF type 4T4R/3T3R/2T2R/1T2R/1T1R/...*/
	u8			rfe_type;
	u8			board_type;
	u8			package_type;
	u16			type_glna;
	u16			type_gpa;
	u16			type_alna;
	u16			type_apa;
	u8			ext_lna;			/*with 2G external LNA  NO/Yes = 0/1*/
	u8			ext_lna_5g;		/*with 5G external LNA  NO/Yes = 0/1*/
	u8			ext_pa;			/*with 2G external PNA  NO/Yes = 0/1*/
	u8			ext_pa_5g;		/*with 5G external PNA  NO/Yes = 0/1*/
	u8 			efuse0x3d7;		/*with Efuse number*/
	u8 			efuse0x3d8;
	u8			ext_trsw;		/*with external TRSW  NO/Yes = 0/1*/
	u8			ext_lna_gain;	/*gain of external lna*/
	boolean		is_in_hct_test;
	u8			wifi_test;
	boolean		is_dual_mac_smart_concurrent;
	u32			bk_support_ability; /*SD4 only*/
	u8			with_extenal_ant_switch;
	/*cck agc relative*/
	boolean		cck_new_agc;
	s8			cck_lna_gain_table[8];
	/*-------------------------------------*/
	u8			phydm_period;
	u32			phydm_sys_up_time;
	u8			num_rf_path; /*ex: 8821C=1, 8192E=2, 8814B=4*/
	u32			soft_ap_special_setting;
	s8			s8_dummy;
	u8			u8_dummy;
	u16			u16_dummy;
	u32			u32_dummy;
	u8			rfe_hwsetting_band;
	u8			p_advance_ota;
	boolean		hp_hw_id;
	boolean		BOOLEAN_temp;
	boolean		is_dfs_band;
	u8			is_receiver_blocking_en;
	u16			fw_offload_ability;
/*-----------HOOK BEFORE REG INIT-----------*/
/*===========================================================*/	
/*====[ CALL BY Reference ]=========================================*/
/*===========================================================*/	

	u64			*p_num_tx_bytes_unicast;	/*TX Unicast byte count*/
	u64			*p_num_rx_bytes_unicast;	/*RX Unicast byte count*/
	u8			*p_band_type;				/*Frequence band 2.4G/5G = 0/1*/
	u8			*p_sec_ch_offset;			/*Secondary channel offset don't_care/below/above = 0/1/2*/
	u8			*p_security;					/*security mode Open/WEP/AES/TKIP = 0/1/2/3*/
	u8			*p_band_width;				/*BW info 20M/40M/80M = 0/1/2*/
	u8			*p_channel;					/*central channel number*/
	boolean		*p_is_scan_in_process;		/*Common info for status*/
	boolean		*p_is_power_saving;
	u8			*p_one_path_cca;			/*CCA path 2-path/path-A/path-B = 0/1/2; using enum odm_cca_path_e.*/
	u8			*p_antenna_test;
	boolean		*p_is_net_closed;
	boolean		*p_is_fcs_mode_enable;
	/*--------- For 8723B IQK-------------------------------------*/
	boolean		*p_is_1_antenna;
	u8			*p_rf_default_path;	/* 0:S1, 1:S0 */
	/*-----------------------------------------------------------*/

	u16			*p_forced_data_rate;
	u8			*p_enable_antdiv;
	u8			*p_enable_adaptivity;
	u8			*hub_usb_mode;		/*1: USB 2.0, 2: USB 3.0*/
	boolean		*p_is_fw_dw_rsvd_page_in_progress;
	u32			*p_current_tx_tp;
	u32			*p_current_rx_tp;
	u8			*p_sounding_seq;
	u32			*p_soft_ap_mode;
	u8			*p_mp_mode;
	u32			*p_interrupt_mask;
	u8			*p_bb_op_mode;
/*===========================================================*/	
/*====[ CALL BY VALUE ]===========================================*/
/*===========================================================*/	

	u8			disable_phydm_watchdog;
	boolean		is_link_in_process;
	boolean		is_wifi_direct;
	boolean		is_wifi_display;
	boolean		is_linked;
	boolean		bsta_state;
	u8			rssi_min;
	u8			pre_rssi_min;
	u8			rssi_max;
	u8			rssi_min_by_path;
	boolean		is_mp_chip;
	boolean		is_one_entry_only;
	u32			one_entry_macid;
	u32			one_entry_tp;
	u32			pre_one_entry_tp;
	u8			pre_number_linked_client;
	u8			number_linked_client;
	u8			pre_number_active_client;
	u8			number_active_client;
	boolean		is_disable_phy_api;
	u8			RSSI_A;
	u8			RSSI_B;
	u8			RSSI_C;
	u8			RSSI_D;
	u64			RSSI_TRSW;
	u64			RSSI_TRSW_H;
	u64			RSSI_TRSW_L;
	u64			RSSI_TRSW_iso;
	u8			tx_ant_status;
	u8			rx_ant_status;
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
	boolean		is_txagc_offset_positive_a;
	u32			txagc_offset_value_b;
	boolean		is_txagc_offset_positive_b;
	/*[traffic]*/
	u8			traffic_load;
	u8			pre_traffic_load;
	u32			tx_tp;	/*Mbps*/
	u32			rx_tp;	/*Mbps*/
	u32			total_tp;/*Mbps*/
	u8			txrx_state_all;	/*0: tx, 1:rx, 2:bi-direction*/
	u64			cur_tx_ok_cnt;
	u64			cur_rx_ok_cnt;
	u64			last_tx_ok_cnt;
	u64			last_rx_ok_cnt;
	u16			consecutive_idlel_time;	/*unit: second*/
	/*---------------------------*/
	boolean		is_bb_swing_offset_positive_a;
	boolean		is_bb_swing_offset_positive_b;

	/*[DIG]*/
	boolean		MPDIG_2G;				/*off MPDIG*/
	u8			times_2g;	/*for MP DIG*/

	/*[TDMA-DIG]*/
	u8			tdma_dig_timer_ms;
	u8			tdma_dig_state_number;
	u8			tdma_dig_low_upper_bond;
	u8			fix_expire_to_zero;
	boolean		original_dig_restore;
	/*---------------------------*/

	/*[AntDiv]*/
	u8			ant_div_type;
	u8			antdiv_rssi;
	u8			fat_comb_a;
	u8			fat_comb_b;
	u8			antdiv_intvl;
	u8			ant_type;
	u8			pre_ant_type;
	u8			antdiv_period;
	u8			evm_antdiv_period;
	u8			antdiv_select;
	u8			antdiv_train_num;/*training time for each antenna in EVM method*/
	u8			stop_antdiv_rssi_th;
	u16			stop_antdiv_tp_diff_th;
	u16			stop_antdiv_tp_th;
	u8			antdiv_tp_period;
	u16			tp_active_th;
	u8			tp_active_occur;
	u8			path_select;
	u8			antdiv_evm_en;
	u8			bdc_holdstate;
	/*---------------------------*/
	
	u8			ndpa_period;
	boolean		h2c_rarpt_connect;
	boolean		cck_agc_report_type;
	u8			print_agc;
	u8			la_mode;
	/*---8821C Antenna and RF Set BTG/WLG/WLA Select---------------*/
	u8			current_rf_set_8821c;
	u8			default_rf_set_8821c;
	u8			current_ant_num_8821c;
	u8			default_ant_num_8821c;
	u8			rfe_type_21c;
	/*-----------------------------------------------------------*/
	/*---For Adaptivtiy---------------------------------------------*/
	s8			TH_L2H_default;
	s8			th_edcca_hl_diff_default;
	s8			th_l2h_ini;
	s8			th_edcca_hl_diff;
	s8			th_l2h_ini_mode2;
	s8			th_edcca_hl_diff_mode2;
	boolean		carrier_sense_enable;
	boolean		adaptivity_flag;	/*Limit IGI upper bound for Adaptivity*/
	u8			dc_backoff;
	boolean		adaptivity_enable;
	u8			ap_total_num;
	boolean		edcca_enable;
	u8			odm_regulation_2_4g;
	u8			odm_regulation_5g;
	/*-----------------------------------------------------------*/
	
	u8			pre_dbg_priority;
	u8			nbi_set_result;
	u8			c2h_cmd_start;
	u8			fw_debug_trace[60];
	u8			pre_c2h_seq;
	boolean		fw_buff_is_enpty;
	u32			data_frame_num;

	/*--- for noise detection ---------------------------------------*/
	boolean		is_noisy_state;
	boolean		noisy_decision; /*b_noisy*/
	boolean		pre_b_noisy;
	u32			noisy_decision_smooth;
	u8			lna_sat_chk_cnt;
	u8			lna_sat_chk_duty_cycle;
	u32			lna_sat_chk_period_ms;
	boolean		is_disable_lna_sat_chk;
	boolean		is_disable_gain_table_switch;
	/*-----------------------------------------------------------*/
	
	boolean		is_disable_dym_ecs;
	boolean		is_disable_dym_ant_weighting;
	struct sta_info	*p_odm_sta_info[ODM_ASSOCIATE_ENTRY_NUM];/*_ODM_STA_INFO, 2012/01/12 MH For MP, we need to reduce one array pointer for default port.??*/
	struct cmn_sta_info	*p_phydm_sta_info[ODM_ASSOCIATE_ENTRY_NUM];
	u8			phydm_macid_table[ODM_ASSOCIATE_ENTRY_NUM];

#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT == 1)
	s32			accumulate_pwdb[ODM_ASSOCIATE_ENTRY_NUM];
#endif

#if (RATE_ADAPTIVE_SUPPORT == 1)
	u16			currmin_rpt_time;
	struct _odm_ra_info_   ra_info[ODM_ASSOCIATE_ENTRY_NUM];
	/*Use mac_id as array index. STA mac_id=0, VWiFi Client mac_id={1, ODM_ASSOCIATE_ENTRY_NUM-1} //YJ,add,120119*/
#endif
	boolean		ra_support88e;	/*2012/02/14 MH Add to share 88E ra with other SW team.We need to colelct all support abilit to a proper area.*/
	boolean		*p_is_driver_stopped;
	boolean		*p_is_driver_is_going_to_pnp_set_power_sleep;
	boolean		*pinit_adpt_in_progress;
	boolean		is_user_assign_level;
	u8			RSSI_BT;			/*come from BT*/

	/*---PSD Relative ---------------------------------------------*/
	boolean		is_psd_in_process;
	boolean		is_psd_active;
	/*-----------------------------------------------------------*/
	
	boolean		bsomlenabled;		/* for dynamic SoML control */
	boolean		bhtstfdisabled;		/* for dynamic HTSTF gain control	*/
	boolean		disrxhpsoml;			/* for dynamic RxHP control with SoML on/off */
	u32			n_iqk_cnt;
	u32			n_iqk_ok_cnt;
	u32			n_iqk_fail_cnt;

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	boolean		config_bbrf;
#endif
	boolean		is_disable_power_training;
	u8			dynamic_tx_high_power_lvl;
	u8			last_dtp_lvl;
	u32			tx_agc_ofdm_18_6;
	u8			rx_pkt_type;

#ifdef CONFIG_PHYDM_DFS_MASTER
	u8			dfs_region_domain;
	u8			*dfs_master_enabled;
	/*---phydm_radar_detect_with_dbg_parm start --------------------*/
	u8			radar_detect_dbg_parm_en;
	u32			radar_detect_reg_918;
	u32			radar_detect_reg_91c;
	u32			radar_detect_reg_920;
	u32			radar_detect_reg_924;
	/*-----------------------------------------------------------*/
#endif

/*=== PHYDM Timer ========================================== (start)*/

	struct timer_list	mpt_dig_timer;	/*MPT DIG timer*/
	struct timer_list	path_div_switch_timer;
	struct timer_list	cck_path_diversity_timer;	/*2011.09.27 add for path Diversity*/
	struct timer_list	fast_ant_training_timer;
#ifdef ODM_EVM_ENHANCE_ANTDIV
	struct timer_list	evm_fast_ant_training_timer;
#endif
	struct timer_list	sbdcnt_timer;


/*=== PHYDM Workitem ======================================= (start)*/

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#if USE_WORKITEM
	RT_WORK_ITEM	path_div_switch_workitem;
	RT_WORK_ITEM	cck_path_diversity_workitem;
	RT_WORK_ITEM	fast_ant_training_workitem;
	RT_WORK_ITEM	ra_rpt_workitem;
	RT_WORK_ITEM	sbdcnt_workitem;
#endif
#endif


/*=== PHYDM Structure ======================================== (start)*/
	struct	phydm_func_poiner			phydm_func_handler;
	struct	phydm_iot_center				iot_table;

#ifdef ODM_CONFIG_BT_COEXIST
	struct	phydm_bt_info				bt_info_table;
#endif

	struct	pkt_process_info				pkt_proc_struct;
	struct phydm_adaptivity_struct			adaptivity;
	struct _DFS_STATISTICS		dfs;

	struct _ODM_NOISE_MONITOR_			noise_level;

	struct _odm_phy_dbg_info_				phy_dbg_info;

#ifdef CONFIG_ADAPTIVE_SOML
	struct adaptive_soml					dm_soml_table;
#endif

#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct _BF_DIV_COEX_					dm_bdc_table;
	#endif

	#if (defined(CONFIG_HL_SMART_ANTENNA))
	struct smt_ant_honbo					dm_sat_table;
	#endif
#endif

#if (defined(CONFIG_SMART_ANTENNA))
	struct smt_ant						smtant_table;
#endif

	struct phydm_fat_struct				dm_fat_table;
	struct phydm_dig_struct				dm_dig_table;
	struct phydm_lna_sat_info_struct	dm_lna_sat_info;

#ifdef PHYDM_SUPPORT_CCKPD
	struct phydm_cckpd_struct				dm_cckpd_table;
#endif
	
#ifdef PHYDM_PRIMARY_CCA
	struct phydm_pricca_struct				dm_pri_cca;
#endif

	struct _rate_adaptive_table_			dm_ra_table;
	struct phydm_fa_struct					false_alm_cnt;
#ifdef PHYDM_TDMA_DIG_SUPPORT
	struct phydm_fa_acc_struct				false_alm_cnt_acc;
#endif
	struct _sw_antenna_switch_				dm_swat_table;
	struct phydm_cfo_track_struct			dm_cfo_track;
	struct _ACS_							dm_acs;
	struct _CCX_INFO						dm_ccx_info;
	struct _hal_rf_						rf_table; 		/*for HALRF function*/
	struct odm_rf_calibration_structure		rf_calibrate_info;
	struct odm_power_trim_data			power_trim_data;	
#if (RTL8822B_SUPPORT == 1)
	struct drp_rtl8822b_struct			phydm_rtl8822b;
#endif

#ifdef CONFIG_PSD_TOOL
	struct _PHYDM_PSD_					dm_psd_table;
#endif

#if (PHYDM_LA_MODE_SUPPORT == 1)
	struct _RT_ADCSMP					adcsmp;
#endif

#ifdef CONFIG_DYNAMIC_RX_PATH
	struct _DYNAMIC_RX_PATH_			dm_drp_table;
#endif

	struct _IQK_INFORMATION				IQK_info;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _path_div_parameter_define_		path_iqk;
#endif

#if (defined(CONFIG_PATH_DIVERSITY))
	struct _ODM_PATH_DIVERSITY_			dm_path_div;
#endif

#if (defined(CONFIG_ANT_DETECTION))
	struct _ANT_DETECTED_INFO			ant_detected_info;	/* Antenna detected information for RSSI tool*/
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (BEAMFORMING_SUPPORT == 1)
	struct _RT_BEAMFORMING_INFO 		beamforming_info;
#endif
#endif
#ifdef PHYDM_AUTO_DEGBUG
	struct	phydm_auto_dbg_struc			auto_dbg_table;
#endif

	struct	phydm_pause_lv				pause_lv_table;	
	struct	phydm_api_stuc 				api_table;
#ifdef PHYDM_POWER_TRAINING_SUPPORT
	struct	phydm_pow_train_stuc			pow_train_table;
#endif
/*==========================================================*/

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)

#if (RT_PLATFORM != PLATFORM_LINUX)
}PHY_DM_STRUCT;		/*DM_Dynamic_Mechanism_Structure*/
#else
};
#endif

#else	/*for AP,CE Team*/
};
#endif

enum phydm_adv_ota {
	PHYDM_PATHB_1RCCA = BIT(0),
	PHYDM_HP_OTA_SETTING_A = BIT(1),
	PHYDM_HP_OTA_SETTING_B = BIT(2),
	PHYDM_ASUS_OTA_SETTING = BIT(3),
	PHYDM_ASUS_OTA_SETTING_CCK_PATH = BIT(4),
	PHYDM_HP_OTA_SETTING_CCK_PATH = BIT(5),

};

enum phydm_bb_op_mode {
	PHYDM_PERFORMANCE_MODE = 0,	/*Service one device*/
	PHYDM_BALANCE_MODE = 1,		/*Service more than one device*/
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
};

enum odm_rf_config_type {
	CONFIG_RF_RADIO,
	CONFIG_RF_TXPWR_LMT,
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
#endif	/*end of enum rt_status definition*/


/*===========================================================*/
/*AGC RX High Power mode*/
/*===========================================================*/
#define	lna_low_gain_1		0x64
#define	lna_low_gain_2		0x5A
#define	lna_low_gain_3		0x58

/*Add for cmn sta info*/

#define is_sta_active(p_sta)	((p_sta) && (p_sta->dm_ctrl & STA_DM_CTRL_ACTIVE))

void
phydm_watchdog_lps(
	struct PHY_DM_STRUCT		*p_dm
);

void
phydm_watchdog_lps_32k(
	struct PHY_DM_STRUCT		*p_dm
);

void
phydm_txcurrentcalibration(
	struct PHY_DM_STRUCT	*p_dm
);	

void
phydm_dm_early_init(
	struct PHY_DM_STRUCT	*p_dm
);

void
odm_dm_init(
	struct PHY_DM_STRUCT	*p_dm
);

void
odm_dm_reset(
	struct PHY_DM_STRUCT	*p_dm
);

void
phydm_fwoffload_ability_init(
	struct PHY_DM_STRUCT		*p_dm,
	enum phydm_offload_ability	offload_ability
);

void
phydm_fwoffload_ability_clear(
	struct PHY_DM_STRUCT		*p_dm,
	enum phydm_offload_ability	offload_ability
);


void
phydm_support_ability_debug(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char		*output,
	u32		*_out_len
);

void
phydm_pause_dm_watchdog(
	void					*p_dm_void,
	enum phydm_pause_type		pause_type
);

void
phydm_watchdog(
	struct PHY_DM_STRUCT	*p_dm
);

void
phydm_watchdog_mp(
	struct PHY_DM_STRUCT	*p_dm
);

u8
phydm_pause_func(
	void					*p_dm_void,
	enum phydm_func_idx_e	pause_func,	
	enum phydm_pause_type	pause_type,
	enum phydm_pause_level	pause_lv,
	u8						val_lehgth,
	u32						*val_buf
	
);

void
phydm_pause_func_console(
	void		*p_dm_void,
	char		input[][16],
	u32		*_used,
	char		*output,
	u32		*_out_len,
	u32		input_num
);

void
odm_cmn_info_init(
	struct PHY_DM_STRUCT	*p_dm,
	enum odm_cmninfo_e		cmn_info,
	u64						value
);

void
odm_cmn_info_hook(
	struct PHY_DM_STRUCT	*p_dm,
	enum odm_cmninfo_e		cmn_info,
	void						*p_value
);

void
odm_cmn_info_update(
	struct PHY_DM_STRUCT	*p_dm,
	u32						cmn_info,
	u64						value
);

u32
phydm_cmn_info_query(
	struct PHY_DM_STRUCT	*p_dm,
	enum phydm_info_query_e	info_type
);

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
void
odm_init_all_threads(
	struct PHY_DM_STRUCT	*p_dm
);

void
odm_stop_all_threads(
	struct PHY_DM_STRUCT	*p_dm
);
#endif

void
odm_init_all_timers(
	struct PHY_DM_STRUCT	*p_dm
);

void
odm_cancel_all_timers(
	struct PHY_DM_STRUCT	*p_dm
);

void
odm_release_all_timers(
	struct PHY_DM_STRUCT	*p_dm
);


#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void odm_init_all_work_items(struct PHY_DM_STRUCT	*p_dm);
void odm_free_all_work_items(struct PHY_DM_STRUCT	*p_dm);

/*2012/01/12 MH Check afapter status. Temp fix BSOD.*/

#define	HAL_ADAPTER_STS_CHK(p_dm) do {\
		if (p_dm->adapter == NULL) { \
			\
			return;\
		} \
	} while (0)

#endif	/*#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)*/

void *
phydm_get_structure(
	struct PHY_DM_STRUCT		*p_dm,
	u8			structure_type
);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN) || (DM_ODM_SUPPORT_TYPE == ODM_CE)
	/*===========================================================*/
	/* The following is for compile only*/
	/*===========================================================*/

	#if (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
		#define IS_HARDWARE_TYPE_8188E(_adapter)		false
		#define IS_HARDWARE_TYPE_8188F(_adapter)		false
		#define IS_HARDWARE_TYPE_8703B(_adapter)		false
		#define IS_HARDWARE_TYPE_8723D(_adapter)		false
		#define IS_HARDWARE_TYPE_8821C(_adapter)		false
		#define IS_HARDWARE_TYPE_8812AU(_adapter)	false
		#define IS_HARDWARE_TYPE_8814A(_adapter)		false
		#define IS_HARDWARE_TYPE_8814AU(_adapter)	false
		#define IS_HARDWARE_TYPE_8814AE(_adapter)	false
		#define IS_HARDWARE_TYPE_8814AS(_adapter)	false
		#define IS_HARDWARE_TYPE_8723BU(_adapter)	false
		#define IS_HARDWARE_TYPE_8822BU(_adapter)	false
		#define IS_HARDWARE_TYPE_8822BS(_adapter)		false
		#define IS_HARDWARE_TYPE_JAGUAR(_Adapter)		\
			(IS_HARDWARE_TYPE_8812(_Adapter) || IS_HARDWARE_TYPE_8821(_Adapter))
	#else
		#define	IS_HARDWARE_TYPE_8723A(_adapter)	false
	#endif
	#define	IS_HARDWARE_TYPE_8723AE(_adapter)		false
	#define	IS_HARDWARE_TYPE_8192C(_adapter)			false
	#define	IS_HARDWARE_TYPE_8192D(_adapter)		false
	#define	RF_T_METER_92D	0x42


	#define	GET_RX_STATUS_DESC_RX_MCS(__prx_status_desc)	LE_BITS_TO_1BYTE(__prx_status_desc+12, 0, 6)

	#define	REG_CONFIG_RAM64X16		0xb2c


	/* *********************************************************** */
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	void odm_dtc(struct PHY_DM_STRUCT *p_dm);
#endif

void
phydm_dc_cancellation(
	struct	PHY_DM_STRUCT	*p_dm
);

void
phydm_receiver_blocking(
	void *p_dm_void
);
#endif
