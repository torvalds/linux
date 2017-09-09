/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
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

/*============================================================*/
/*include files*/
/*============================================================*/
#include "phydm_pre_define.h"
#include "phydm_dig.h"
#include "phydm_edcaturbocheck.h"
#include "phydm_antdiv.h"
#include "phydm_dynamicbbpowersaving.h"
#include "phydm_rainfo.h"
#include "phydm_dynamictxpower.h"
#include "phydm_cfotracking.h"
#include "phydm_acs.h"
#include "phydm_adaptivity.h"
#include "phydm_iqk.h"
#include "phydm_dfs.h"
#include "phydm_ccx.h"
#include "txbf/phydm_hal_txbf_api.h"

#include "phydm_adc_sampling.h"
#include "phydm_dynamic_rx_path.h"
#include "phydm_psd.h"

#include "phydm_beamforming.h"

#include "phydm_noisemonitor.h"
#include "halphyrf_ce.h"

/*============================================================*/
/*Definition */
/*============================================================*/

/* Traffic load decision */
#define TRAFFIC_ULTRA_LOW 1
#define TRAFFIC_LOW 2
#define TRAFFIC_MID 3
#define TRAFFIC_HIGH 4

#define NONE 0

/*NBI API------------------------------------*/
#define NBI_ENABLE 1
#define NBI_DISABLE 2

#define NBI_TABLE_SIZE_128 27
#define NBI_TABLE_SIZE_256 59

#define NUM_START_CH_80M 7
#define NUM_START_CH_40M 14

#define CH_OFFSET_40M 2
#define CH_OFFSET_80M 6

/*CSI MASK API------------------------------------*/
#define CSI_MASK_ENABLE 1
#define CSI_MASK_DISABLE 2

/*------------------------------------------------*/

#define FFT_128_TYPE 1
#define FFT_256_TYPE 2

#define SET_SUCCESS 1
#define SET_ERROR 2
#define SET_NO_NEED 3

#define FREQ_POSITIVE 1
#define FREQ_NEGATIVE 2

#define PHYDM_WATCH_DOG_PERIOD 2

/*============================================================*/
/*structure and define*/
/*============================================================*/

/*2011/09/20 MH Add for AP/ADSLpseudo DM structuer requirement.*/
/*We need to remove to other position???*/

struct rtl8192cd_priv {
	u8 temp;
};

struct dyn_primary_cca {
	u8 pri_cca_flag;
	u8 intf_flag;
	u8 intf_type;
	u8 dup_rts_flag;
	u8 monitor_flag;
	u8 ch_offset;
	u8 mf_state;
};

#define dm_type_by_fw 0
#define dm_type_by_driver 1

/*Declare for common info*/

#define IQK_THRESHOLD 8
#define DPK_THRESHOLD 4

struct dm_phy_status_info {
	/*  */
	/* Be care, if you want to add any element please insert between */
	/* rx_pwdb_all & signal_strength. */
	/*  */
	u8 rx_pwdb_all;
	u8 signal_quality; /* in 0-100 index. */
	s8 rx_mimo_signal_quality[4]; /* per-path's EVM translate to 0~100% */
	u8 rx_mimo_evm_dbm[4]; /* per-path's original EVM (dbm) */
	u8 rx_mimo_signal_strength[4]; /* in 0~100 index */
	s16 cfo_short[4]; /* per-path's cfo_short */
	s16 cfo_tail[4]; /* per-path's cfo_tail */
	s8 rx_power; /* in dBm Translate from PWdB */
	s8 recv_signal_power; /* Real power in dBm for this packet,
			       * no beautification and aggregation.
			       * Keep this raw info to be used for the other
			       * procedures.
			       */
	u8 bt_rx_rssi_percentage;
	u8 signal_strength; /* in 0-100 index. */
	s8 rx_pwr[4]; /* per-path's pwdb */
	s8 rx_snr[4]; /* per-path's SNR	*/
	/* s8      BB_Backup[13];                   backup reg. */
	u8 rx_count : 2; /* RX path counter---*/
	u8 band_width : 2;
	u8 rxsc : 4; /* sub-channel---*/
	u8 bt_coex_pwr_adjust;
	u8 channel; /* channel number---*/
	bool is_mu_packet; /* is MU packet or not---*/
	bool is_beamformed; /* BF packet---*/
};

struct dm_per_pkt_info {
	u8 data_rate;
	u8 station_id;
	bool is_packet_match_bssid;
	bool is_packet_to_self;
	bool is_packet_beacon;
	bool is_to_self;
	u8 ppdu_cnt;
};

struct odm_phy_dbg_info {
	/*ODM Write,debug info*/
	s8 rx_snr_db[4];
	u32 num_qry_phy_status;
	u32 num_qry_phy_status_cck;
	u32 num_qry_phy_status_ofdm;
	u32 num_qry_mu_pkt;
	u32 num_qry_bf_pkt;
	u32 num_qry_mu_vht_pkt[40];
	u32 num_qry_vht_pkt[40];
	bool is_ldpc_pkt;
	bool is_stbc_pkt;
	u8 num_of_ppdu[4];
	u8 gid_num[4];
	u8 num_qry_beacon_pkt;
	/* Others */
	s32 rx_evm[4];
};

/*2011/20/20 MH For MP driver RT_WLAN_STA =  struct rtl_sta_info*/
/*Please declare below ODM relative info in your STA info structure.*/

struct odm_sta_info {
	/*Driver Write*/
	bool is_used; /*record the sta status link or not?*/
	u8 iot_peer; /*Enum value.	HT_IOT_PEER_E*/

	/*ODM Write*/
	/*PHY_STATUS_INFO*/
	u8 rssi_path[4];
	u8 rssi_ave;
	u8 RXEVM[4];
	u8 RXSNR[4];
};

enum odm_cmninfo {
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
	ODM_CMNINFO_DPK_EN,
	ODM_CMNINFO_EXT_LNA_GAIN,
	ODM_CMNINFO_PATCH_ID,
	ODM_CMNINFO_BINHCT_TEST,
	ODM_CMNINFO_BWIFI_TEST,
	ODM_CMNINFO_SMART_CONCURRENT,
	ODM_CMNINFO_CONFIG_BB_RF,
	ODM_CMNINFO_DOMAIN_CODE_2G,
	ODM_CMNINFO_DOMAIN_CODE_5G,
	ODM_CMNINFO_IQKFWOFFLOAD,
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
	/*-----------HOOK BEFORE REG INIT-----------*/

	/*Dynamic value:*/

	/*--------- POINTER REFERENCE-----------*/
	ODM_CMNINFO_MAC_PHY_MODE,
	ODM_CMNINFO_TX_UNI,
	ODM_CMNINFO_RX_UNI,
	ODM_CMNINFO_WM_MODE,
	ODM_CMNINFO_BAND,
	ODM_CMNINFO_SEC_CHNL_OFFSET,
	ODM_CMNINFO_SEC_MODE,
	ODM_CMNINFO_BW,
	ODM_CMNINFO_CHNL,
	ODM_CMNINFO_FORCED_RATE,
	ODM_CMNINFO_ANT_DIV,
	ODM_CMNINFO_ADAPTIVITY,
	ODM_CMNINFO_DMSP_GET_VALUE,
	ODM_CMNINFO_BUDDY_ADAPTOR,
	ODM_CMNINFO_DMSP_IS_MASTER,
	ODM_CMNINFO_SCAN,
	ODM_CMNINFO_POWER_SAVING,
	ODM_CMNINFO_ONE_PATH_CCA,
	ODM_CMNINFO_DRV_STOP,
	ODM_CMNINFO_PNP_IN,
	ODM_CMNINFO_INIT_ON,
	ODM_CMNINFO_ANT_TEST,
	ODM_CMNINFO_NET_CLOSED,
	ODM_CMNINFO_FORCED_IGI_LB,
	ODM_CMNINFO_P2P_LINK,
	ODM_CMNINFO_FCS_MODE,
	ODM_CMNINFO_IS1ANTENNA,
	ODM_CMNINFO_RFDEFAULTPATH,
	ODM_CMNINFO_DFS_MASTER_ENABLE,
	ODM_CMNINFO_FORCE_TX_ANT_BY_TXDESC,
	ODM_CMNINFO_SET_S0S1_DEFAULT_ANTENNA,
	/*--------- POINTER REFERENCE-----------*/

	/*------------CALL BY VALUE-------------*/
	ODM_CMNINFO_WIFI_DIRECT,
	ODM_CMNINFO_WIFI_DISPLAY,
	ODM_CMNINFO_LINK_IN_PROGRESS,
	ODM_CMNINFO_LINK,
	ODM_CMNINFO_CMW500LINK,
	ODM_CMNINFO_LPSPG,
	ODM_CMNINFO_STATION_STATE,
	ODM_CMNINFO_RSSI_MIN,
	ODM_CMNINFO_DBG_COMP,
	ODM_CMNINFO_DBG_LEVEL,
	ODM_CMNINFO_RA_THRESHOLD_HIGH,
	ODM_CMNINFO_RA_THRESHOLD_LOW,
	ODM_CMNINFO_RF_ANTENNA_TYPE,
	ODM_CMNINFO_WITH_EXT_ANTENNA_SWITCH,
	ODM_CMNINFO_BE_FIX_TX_ANT,
	ODM_CMNINFO_BT_ENABLED,
	ODM_CMNINFO_BT_HS_CONNECT_PROCESS,
	ODM_CMNINFO_BT_HS_RSSI,
	ODM_CMNINFO_BT_OPERATION,
	ODM_CMNINFO_BT_LIMITED_DIG,
	ODM_CMNINFO_BT_DIG,
	ODM_CMNINFO_BT_BUSY,
	ODM_CMNINFO_BT_DISABLE_EDCA,
	ODM_CMNINFO_AP_TOTAL_NUM,
	ODM_CMNINFO_POWER_TRAINING,
	ODM_CMNINFO_DFS_REGION_DOMAIN,
	/*------------CALL BY VALUE-------------*/

	/*Dynamic ptr array hook itms.*/
	ODM_CMNINFO_STA_STATUS,
	ODM_CMNINFO_MAX,

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
	PHYDM_INFO_DBG_PORT_0
};

enum phydm_api {
	PHYDM_API_NBI = 1,
	PHYDM_API_CSI_MASK,

};

/*2011/10/20 MH Define ODM support ability.  ODM_CMNINFO_ABILITY*/
enum odm_ability {
	/*BB ODM section BIT 0-19*/
	ODM_BB_DIG = BIT(0),
	ODM_BB_RA_MASK = BIT(1),
	ODM_BB_DYNAMIC_TXPWR = BIT(2),
	ODM_BB_FA_CNT = BIT(3),
	ODM_BB_RSSI_MONITOR = BIT(4),
	ODM_BB_CCK_PD = BIT(5),
	ODM_BB_ANT_DIV = BIT(6),
	ODM_BB_PWR_TRAIN = BIT(8),
	ODM_BB_RATE_ADAPTIVE = BIT(9),
	ODM_BB_PATH_DIV = BIT(10),
	ODM_BB_ADAPTIVITY = BIT(13),
	ODM_BB_CFO_TRACKING = BIT(14),
	ODM_BB_NHM_CNT = BIT(15),
	ODM_BB_PRIMARY_CCA = BIT(16),
	ODM_BB_TXBF = BIT(17),
	ODM_BB_DYNAMIC_ARFR = BIT(18),

	ODM_MAC_EDCA_TURBO = BIT(20),
	ODM_BB_DYNAMIC_RX_PATH = BIT(21),

	/*RF ODM section BIT 24-31*/
	ODM_RF_TX_PWR_TRACK = BIT(24),
	ODM_RF_RX_GAIN_TRACK = BIT(25),
	ODM_RF_CALIBRATION = BIT(26),

};

/*ODM_CMNINFO_ONE_PATH_CCA*/
enum odm_cca_path {
	ODM_CCA_2R = 0,
	ODM_CCA_1R_A = 1,
	ODM_CCA_1R_B = 2,
};

enum cca_pathdiv_en {
	CCA_PATHDIV_DISABLE = 0,
	CCA_PATHDIV_ENABLE = 1,

};

enum phy_reg_pg_type {
	PHY_REG_PG_RELATIVE_VALUE = 0,
	PHY_REG_PG_EXACT_VALUE = 1
};

/*2011/09/22 MH Copy from SD4 defined structure.
 *We use to support PHY DM integration.
 */

struct phy_dm_struct {
	/*Add for different team use temporarily*/
	void *adapter; /*For CE/NIC team*/
	struct rtl8192cd_priv *priv; /*For AP/ADSL team*/
	/*When you use adapter or priv pointer,
	 *you must make sure the pointer is ready.
	 */
	bool odm_ready;

	struct rtl8192cd_priv fake_priv;

	enum phy_reg_pg_type phy_reg_pg_value_type;
	u8 phy_reg_pg_version;

	u32 debug_components;
	u32 fw_debug_components;
	u32 debug_level;

	u32 num_qry_phy_status_all; /*CCK + OFDM*/
	u32 last_num_qry_phy_status_all;
	u32 rx_pwdb_ave;
	bool MPDIG_2G; /*off MPDIG*/
	u8 times_2g;
	bool is_init_hw_info_by_rfe;

	/*------ ODM HANDLE, DRIVER NEEDS NOT TO HOOK------*/
	bool is_cck_high_power;
	u8 rf_path_rx_enable;
	u8 control_channel;
	/*------ ODM HANDLE, DRIVER NEEDS NOT TO HOOK------*/

	/* 1  COMMON INFORMATION */

	/*Init value*/
	/*-----------HOOK BEFORE REG INIT-----------*/
	/*ODM Platform info AP/ADSL/CE/MP = 1/2/3/4*/
	u8 support_platform;
	/* ODM Platform info WIN/AP/CE = 1/2/3 */
	u8 normal_rx_path;
	/*ODM Support Ability DIG/RATR/TX_PWR_TRACK/ ... = 1/2/3/...*/
	u32 support_ability;
	/*ODM PCIE/USB/SDIO = 1/2/3*/
	u8 support_interface;
	/*ODM composite or independent. Bit oriented/ 92C+92D+ .... or
	 *any other type = 1/2/3/...
	 */
	u32 support_ic_type;
	/*cut version TestChip/A-cut/B-cut... = 0/1/2/3/...*/
	u8 cut_version;
	/*Fab version TSMC/UMC = 0/1*/
	u8 fab_version;
	/*RF type 4T4R/3T3R/2T2R/1T2R/1T1R/...*/
	u8 rf_type;
	u8 rfe_type;
	/*Board type Normal/HighPower/MiniCard/SLIM/Combo/... = 0/1/2/3/4/...*/
	/*Enable Function DPK OFF/ON = 0/1*/
	u8 dpk_en;
	u8 board_type;
	u8 package_type;
	u16 type_glna;
	u16 type_gpa;
	u16 type_alna;
	u16 type_apa;
	/*with external LNA  NO/Yes = 0/1*/
	u8 ext_lna; /*2G*/
	u8 ext_lna_5g; /*5G*/
	/*with external PA  NO/Yes = 0/1*/
	u8 ext_pa; /*2G*/
	u8 ext_pa_5g; /*5G*/
	/*with Efuse number*/
	u8 efuse0x3d7;
	u8 efuse0x3d8;
	/*with external TRSW  NO/Yes = 0/1*/
	u8 ext_trsw;
	u8 ext_lna_gain; /*2G*/
	u8 patch_id; /*Customer ID*/
	bool is_in_hct_test;
	u8 wifi_test;

	bool is_dual_mac_smart_concurrent;
	u32 bk_support_ability;
	u8 ant_div_type;
	u8 with_extenal_ant_switch;
	bool config_bbrf;
	u8 odm_regulation_2_4g;
	u8 odm_regulation_5g;
	u8 iqk_fw_offload;
	bool cck_new_agc;
	u8 phydm_period;
	u32 phydm_sys_up_time;
	u8 num_rf_path;
	/*-----------HOOK BEFORE REG INIT-----------*/

	/*Dynamic value*/

	/*--------- POINTER REFERENCE-----------*/

	u8 u1_byte_temp;
	bool BOOLEAN_temp;
	void *PADAPTER_temp;

	/*MAC PHY mode SMSP/DMSP/DMDP = 0/1/2*/
	u8 *mac_phy_mode;
	/*TX Unicast byte count*/
	u64 *num_tx_bytes_unicast;
	/*RX Unicast byte count*/
	u64 *num_rx_bytes_unicast;
	/*Wireless mode B/G/A/N = BIT0/BIT1/BIT2/BIT3*/
	u8 *wireless_mode;
	/*Frequence band 2.4G/5G = 0/1*/
	u8 *band_type;
	/*Secondary channel offset don't_care/below/above = 0/1/2*/
	u8 *sec_ch_offset;
	/*security mode Open/WEP/AES/TKIP = 0/1/2/3*/
	u8 *security;
	/*BW info 20M/40M/80M = 0/1/2*/
	u8 *band_width;
	/*Central channel location Ch1/Ch2/....*/
	u8 *channel; /*central channel number*/
	bool dpk_done;
	/*Common info for 92D DMSP*/

	bool *is_get_value_from_other_mac;
	void **buddy_adapter;
	bool *is_master_of_dmsp; /* MAC0: master, MAC1: slave */
	/*Common info for status*/
	bool *is_scan_in_process;
	bool *is_power_saving;
	/*CCA path 2-path/path-A/path-B = 0/1/2; using enum odm_cca_path.*/
	u8 *one_path_cca;
	u8 *antenna_test;
	bool *is_net_closed;
	u8 *pu1_forced_igi_lb;
	bool *is_fcs_mode_enable;
	/*--------- For 8723B IQK-----------*/
	bool *is_1_antenna;
	u8 *rf_default_path;
	/* 0:S1, 1:S0 */

	/*--------- POINTER REFERENCE-----------*/
	u16 *forced_data_rate;
	u8 *enable_antdiv;
	u8 *enable_adaptivity;
	u8 *hub_usb_mode;
	bool *is_fw_dw_rsvd_page_in_progress;
	u32 *current_tx_tp;
	u32 *current_rx_tp;
	u8 *sounding_seq;
	/*------------CALL BY VALUE-------------*/
	bool is_link_in_process;
	bool is_wifi_direct;
	bool is_wifi_display;
	bool is_linked;
	bool is_linkedcmw500;
	bool is_in_lps_pg;
	bool bsta_state;
	u8 rssi_min;
	u8 interface_index; /*Add for 92D  dual MAC: 0--Mac0 1--Mac1*/
	bool is_mp_chip;
	bool is_one_entry_only;
	bool mp_mode;
	u32 one_entry_macid;
	u8 pre_number_linked_client;
	u8 number_linked_client;
	u8 pre_number_active_client;
	u8 number_active_client;
	/*Common info for BTDM*/
	bool is_bt_enabled; /*BT is enabled*/
	bool is_bt_connect_process; /*BT HS is under connection progress.*/
	u8 bt_hs_rssi; /*BT HS mode wifi rssi value.*/
	bool is_bt_hs_operation; /*BT HS mode is under progress*/
	u8 bt_hs_dig_val; /*use BT rssi to decide the DIG value*/
	bool is_bt_disable_edca_turbo; /*Under some condition, don't enable*/
	bool is_bt_busy; /*BT is busy.*/
	bool is_bt_limited_dig; /*BT is busy.*/
	bool is_disable_phy_api;
	/*------------CALL BY VALUE-------------*/
	u8 rssi_a;
	u8 rssi_b;
	u8 rssi_c;
	u8 rssi_d;
	u64 rssi_trsw;
	u64 rssi_trsw_h;
	u64 rssi_trsw_l;
	u64 rssi_trsw_iso;
	u8 tx_ant_status;
	u8 rx_ant_status;
	u8 cck_lna_idx;
	u8 cck_vga_idx;
	u8 curr_station_id;
	u8 ofdm_agc_idx[4];

	u8 rx_rate;
	bool is_noisy_state;
	u8 tx_rate;
	u8 linked_interval;
	u8 pre_channel;
	u32 txagc_offset_value_a;
	bool is_txagc_offset_positive_a;
	u32 txagc_offset_value_b;
	bool is_txagc_offset_positive_b;
	u32 tx_tp;
	u32 rx_tp;
	u32 total_tp;
	u64 cur_tx_ok_cnt;
	u64 cur_rx_ok_cnt;
	u64 last_tx_ok_cnt;
	u64 last_rx_ok_cnt;
	u32 bb_swing_offset_a;
	bool is_bb_swing_offset_positive_a;
	u32 bb_swing_offset_b;
	bool is_bb_swing_offset_positive_b;
	u8 igi_lower_bound;
	u8 igi_upper_bound;
	u8 antdiv_rssi;
	u8 fat_comb_a;
	u8 fat_comb_b;
	u8 antdiv_intvl;
	u8 ant_type;
	u8 pre_ant_type;
	u8 antdiv_period;
	u8 evm_antdiv_period;
	u8 antdiv_select;
	u8 path_select;
	u8 antdiv_evm_en;
	u8 bdc_holdstate;
	u8 ndpa_period;
	bool h2c_rarpt_connect;
	bool cck_agc_report_type;

	u8 dm_dig_max_TH;
	u8 dm_dig_min_TH;
	u8 print_agc;
	u8 traffic_load;
	u8 pre_traffic_load;
	/*8821C Antenna BTG/WLG/WLA Select*/
	u8 current_rf_set_8821c;
	u8 default_rf_set_8821c;
	/*For Adaptivtiy*/
	u16 nhm_cnt_0;
	u16 nhm_cnt_1;
	s8 TH_L2H_default;
	s8 th_edcca_hl_diff_default;
	s8 th_l2h_ini;
	s8 th_edcca_hl_diff;
	s8 th_l2h_ini_mode2;
	s8 th_edcca_hl_diff_mode2;
	bool carrier_sense_enable;
	u8 adaptivity_igi_upper;
	bool adaptivity_flag;
	u8 dc_backoff;
	bool adaptivity_enable;
	u8 ap_total_num;
	bool edcca_enable;
	u8 pre_dbg_priority;
	struct adaptivity_statistics adaptivity;
	/*For Adaptivtiy*/
	u8 last_usb_hub;
	u8 tx_bf_data_rate;

	u8 nbi_set_result;

	u8 c2h_cmd_start;
	u8 fw_debug_trace[60];
	u8 pre_c2h_seq;
	bool fw_buff_is_enpty;
	u32 data_frame_num;

	/*for noise detection*/
	bool noisy_decision; /*b_noisy*/
	bool pre_b_noisy;
	u32 noisy_decision_smooth;
	bool is_disable_dym_ecs;

	struct odm_noise_monitor noise_level;
	/*Define STA info.*/
	/*odm_sta_info*/
	/*2012/01/12 MH For MP,
	 *we need to reduce one array pointer for default port.??
	 */
	struct rtl_sta_info *odm_sta_info[ODM_ASSOCIATE_ENTRY_NUM];
	u16 platform2phydm_macid_table[ODM_ASSOCIATE_ENTRY_NUM];
	/* platform_macid_table[platform_macid] = phydm_macid */
	s32 accumulate_pwdb[ODM_ASSOCIATE_ENTRY_NUM];

	/*2012/02/14 MH Add to share 88E ra with other SW team.*/
	/*We need to colelct all support abilit to a proper area.*/

	bool ra_support88e;

	struct odm_phy_dbg_info phy_dbg_info;

	/*ODM Structure*/
	struct fast_antenna_training dm_fat_table;
	struct dig_thres dm_dig_table;
	struct dyn_pwr_saving dm_ps_table;
	struct dyn_primary_cca dm_pri_cca;
	struct ra_table dm_ra_table;
	struct false_alarm_stat false_alm_cnt;
	struct false_alarm_stat flase_alm_cnt_buddy_adapter;
	struct sw_antenna_switch dm_swat_table;
	struct cfo_tracking dm_cfo_track;
	struct acs_info dm_acs;
	struct ccx_info dm_ccx_info;
	struct psd_info dm_psd_table;

	struct rt_adcsmp adcsmp;

	struct dm_iqk_info IQK_info;

	struct edca_turbo dm_edca_table;
	u32 WMMEDCA_BE;

	bool *is_driver_stopped;
	bool *is_driver_is_going_to_pnp_set_power_sleep;
	bool *pinit_adpt_in_progress;

	/*PSD*/
	bool is_user_assign_level;
	u8 RSSI_BT; /*come from BT*/
	bool is_psd_in_process;
	bool is_psd_active;
	bool is_dm_initial_gain_enable;

	/*MPT DIG*/
	struct timer_list mpt_dig_timer;

	/*for rate adaptive, in fact,  88c/92c fw will handle this*/
	u8 is_use_ra_mask;

	/* for dynamic SoML control */
	bool bsomlenabled;

	struct odm_rate_adaptive rate_adaptive;
	struct dm_rf_calibration_struct rf_calibrate_info;
	u32 n_iqk_cnt;
	u32 n_iqk_ok_cnt;
	u32 n_iqk_fail_cnt;

	/*Power Training*/
	u8 force_power_training_state;
	bool is_change_state;
	u32 PT_score;
	u64 ofdm_rx_cnt;
	u64 cck_rx_cnt;
	bool is_disable_power_training;
	u8 dynamic_tx_high_power_lvl;
	u8 last_dtp_lvl;
	u32 tx_agc_ofdm_18_6;
	u8 rx_pkt_type;

	/*ODM relative time.*/
	struct timer_list path_div_switch_timer;
	/*2011.09.27 add for path Diversity*/
	struct timer_list cck_path_diversity_timer;
	struct timer_list fast_ant_training_timer;
	struct timer_list sbdcnt_timer;

	/*ODM relative workitem.*/
};

enum phydm_structure_type {
	PHYDM_FALSEALMCNT,
	PHYDM_CFOTRACK,
	PHYDM_ADAPTIVITY,
	PHYDM_ROMINFO,

};

enum odm_rf_content {
	odm_radioa_txt = 0x1000,
	odm_radiob_txt = 0x1001,
	odm_radioc_txt = 0x1002,
	odm_radiod_txt = 0x1003
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

/*===========================================================*/
/*AGC RX High Power mode*/
/*===========================================================*/
#define lna_low_gain_1 0x64
#define lna_low_gain_2 0x5A
#define lna_low_gain_3 0x58

#define FA_RXHP_TH1 5000
#define FA_RXHP_TH2 1500
#define FA_RXHP_TH3 800
#define FA_RXHP_TH4 600
#define FA_RXHP_TH5 500

enum dm_1r_cca {
	CCA_1R = 0,
	CCA_2R = 1,
	CCA_MAX = 2,
};

enum dm_rf {
	rf_save = 0,
	rf_normal = 1,
	RF_MAX = 2,
};

/*check Sta pointer valid or not*/

#define IS_STA_VALID(sta) (sta)

u32 odm_convert_to_db(u32 value);

u32 odm_convert_to_linear(u32 value);

s32 odm_pwdb_conversion(s32 X, u32 total_bit, u32 decimal_bit);

s32 odm_sign_conversion(s32 value, u32 total_bit);

void odm_init_mp_driver_status(struct phy_dm_struct *dm);

void phydm_txcurrentcalibration(struct phy_dm_struct *dm);

void phydm_seq_sorting(void *dm_void, u32 *value, u32 *rank_idx, u32 *idx_out,
		       u8 seq_length);

void odm_dm_init(struct phy_dm_struct *dm);

void odm_dm_reset(struct phy_dm_struct *dm);

void phydm_support_ability_debug(void *dm_void, u32 *const dm_value, u32 *_used,
				 char *output, u32 *_out_len);

void phydm_config_ofdm_rx_path(struct phy_dm_struct *dm, u32 path);

void phydm_config_trx_path(void *dm_void, u32 *const dm_value, u32 *_used,
			   char *output, u32 *_out_len);

void odm_dm_watchdog(struct phy_dm_struct *dm);

void phydm_watchdog_mp(struct phy_dm_struct *dm);

void odm_cmn_info_init(struct phy_dm_struct *dm, enum odm_cmninfo cmn_info,
		       u32 value);

void odm_cmn_info_hook(struct phy_dm_struct *dm, enum odm_cmninfo cmn_info,
		       void *value);

void odm_cmn_info_ptr_array_hook(struct phy_dm_struct *dm,
				 enum odm_cmninfo cmn_info, u16 index,
				 void *value);

void odm_cmn_info_update(struct phy_dm_struct *dm, u32 cmn_info, u64 value);

u32 phydm_cmn_info_query(struct phy_dm_struct *dm,
			 enum phydm_info_query info_type);

void odm_init_all_timers(struct phy_dm_struct *dm);

void odm_cancel_all_timers(struct phy_dm_struct *dm);

void odm_release_all_timers(struct phy_dm_struct *dm);

void odm_asoc_entry_init(struct phy_dm_struct *dm);

void *phydm_get_structure(struct phy_dm_struct *dm, u8 structure_type);

/*===========================================================*/
/* The following is for compile only*/
/*===========================================================*/

#define IS_HARDWARE_TYPE_8188E(_adapter) false
#define IS_HARDWARE_TYPE_8188F(_adapter) false
#define IS_HARDWARE_TYPE_8703B(_adapter) false
#define IS_HARDWARE_TYPE_8723D(_adapter) false
#define IS_HARDWARE_TYPE_8821C(_adapter) false
#define IS_HARDWARE_TYPE_8812AU(_adapter) false
#define IS_HARDWARE_TYPE_8814A(_adapter) false
#define IS_HARDWARE_TYPE_8814AU(_adapter) false
#define IS_HARDWARE_TYPE_8814AE(_adapter) false
#define IS_HARDWARE_TYPE_8814AS(_adapter) false
#define IS_HARDWARE_TYPE_8723BU(_adapter) false
#define IS_HARDWARE_TYPE_8822BU(_adapter) false
#define IS_HARDWARE_TYPE_8822BS(_adapter) false
#define IS_HARDWARE_TYPE_JAGUAR(_adapter)                                      \
	(IS_HARDWARE_TYPE_8812(_adapter) || IS_HARDWARE_TYPE_8821(_adapter))
#define IS_HARDWARE_TYPE_8723AE(_adapter) false
#define IS_HARDWARE_TYPE_8192C(_adapter) false
#define IS_HARDWARE_TYPE_8192D(_adapter) false
#define RF_T_METER_92D 0x42

#define GET_RX_STATUS_DESC_RX_MCS(__prx_status_desc)                           \
	LE_BITS_TO_1BYTE(__prx_status_desc + 12, 0, 6)

#define REG_CONFIG_RAM64X16 0xb2c

#define TARGET_CHNL_NUM_2G_5G 59

/* *********************************************************** */

void odm_dtc(struct phy_dm_struct *dm);

void phydm_noisy_detection(struct phy_dm_struct *dm);

void phydm_set_ext_switch(void *dm_void, u32 *const dm_value, u32 *_used,
			  char *output, u32 *_out_len);

void phydm_api_debug(void *dm_void, u32 function_map, u32 *const dm_value,
		     u32 *_used, char *output, u32 *_out_len);

u8 phydm_nbi_setting(void *dm_void, u32 enable, u32 channel, u32 bw,
		     u32 f_interference, u32 second_ch);
#endif /* __HALDMOUTSRC_H__ */
