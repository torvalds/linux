/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#ifndef	__R8192UDM_H__
#define __R8192UDM_H__


/*--------------------------Define Parameters-------------------------------*/
#define			OFDM_Table_Length	19
#define		CCK_Table_length	12

#define		DM_DIG_THRESH_HIGH					40
#define		DM_DIG_THRESH_LOW					35

#define		DM_DIG_HIGH_PWR_THRESH_HIGH		75
#define		DM_DIG_HIGH_PWR_THRESH_LOW		70

#define		BW_AUTO_SWITCH_HIGH_LOW			25
#define		BW_AUTO_SWITCH_LOW_HIGH			30

#define		DM_DIG_BACKOFF				12
#define		DM_DIG_MAX					0x36
#define		DM_DIG_MIN					0x1c
#define		DM_DIG_MIN_Netcore			0x12

#define		RxPathSelection_SS_TH_low		30
#define		RxPathSelection_diff_TH			18

#define		RateAdaptiveTH_High			50
#define		RateAdaptiveTH_Low_20M		30
#define		RateAdaptiveTH_Low_40M		10
#define		VeryLowRSSI					15

#define		WAIotTHVal						25

#define		E_FOR_TX_POWER_TRACK	       300
#define		TX_POWER_NEAR_FIELD_THRESH_HIGH		68
#define		TX_POWER_NEAR_FIELD_THRESH_LOW		62
#define	 TX_POWER_ATHEROAP_THRESH_HIGH	   78
#define		TX_POWER_ATHEROAP_THRESH_LOW		72

#define			Current_Tx_Rate_Reg	 0x1e0
#define			Initial_Tx_Rate_Reg	 0x1e1
#define			Tx_Retry_Count_Reg	 0x1ac
#define		RegC38_TH				 20

#define		DM_Type_ByDriver		1

/*--------------------------Define Parameters-------------------------------*/


/*------------------------------Define structure----------------------------*/
struct dig_t {
	u8		dig_enable_flag;
	u8		dig_algorithm;
	u8		dig_algorithm_switch;

	long		rssi_low_thresh;
	long		rssi_high_thresh;

	long		rssi_high_power_lowthresh;
	long		rssi_high_power_highthresh;

	u8		dig_state;
	u8		dig_highpwr_state;
	u8		CurSTAConnectState;
	u8		PreSTAConnectState;

	u8		curpd_thstate;
	u8		prepd_thstate;
	u8		curcs_ratio_state;
	u8		precs_ratio_state;

	u32		pre_ig_value;
	u32		cur_ig_value;

	u8		backoff_val;
	u8		rx_gain_range_max;
	u8		rx_gain_range_min;

	long		rssi_val;
};

enum dm_dig_sta {
	DM_STA_DIG_OFF = 0,
	DM_STA_DIG_ON,
	DM_STA_DIG_MAX
};


enum dm_ratr_sta {
	DM_RATR_STA_HIGH = 0,
	DM_RATR_STA_MIDDLE = 1,
	DM_RATR_STA_LOW = 2,
	DM_RATR_STA_MAX
};

enum dm_dig_alg {
	DIG_ALGO_BY_FALSE_ALARM = 0,
	DIG_ALGO_BY_RSSI	= 1,
	DIG_ALGO_BEFORE_CONNECT_BY_RSSI_AND_ALARM = 2,
	DIG_ALGO_BY_TOW_PORT = 3,
	DIG_ALGO_MAX
};

enum dm_dig_connect {
	DIG_STA_DISCONNECT = 0,
	DIG_STA_CONNECT = 1,
	DIG_STA_BEFORE_CONNECT = 2,
	DIG_AP_DISCONNECT = 3,
	DIG_AP_CONNECT = 4,
	DIG_AP_ADD_STATION = 5,
	DIG_CONNECT_MAX
};

enum dm_dig_pd_th {
	DIG_PD_AT_LOW_POWER = 0,
	DIG_PD_AT_NORMAL_POWER = 1,
	DIG_PD_AT_HIGH_POWER = 2,
	DIG_PD_MAX
};

enum dm_dig_cs_ratio {
	DIG_CS_RATIO_LOWER = 0,
	DIG_CS_RATIO_HIGHER = 1,
	DIG_CS_MAX
};

struct drx_path_sel {
	u8		Enable;
	u8		cck_method;
	u8		cck_Rx_path;

	u8		SS_TH_low;
	u8		diff_TH;
	u8		disabledRF;
	u8		reserved;

	u8		rf_rssi[4];
	u8		rf_enable_rssi_th[4];
	long		cck_pwdb_sta[4];
};

enum dm_cck_rx_path_method {
	CCK_Rx_Version_1 = 0,
	CCK_Rx_Version_2 = 1,
	CCK_Rx_Version_MAX
};


struct dcmd_txcmd {
	u32	Op;
	u32	Length;
	u32	Value;
};
/*------------------------------Define structure----------------------------*/


/*------------------------Export global variable----------------------------*/
extern	struct dig_t dm_digtable;
extern struct drx_path_sel DM_RxPathSelTable;

/* Pre-calculated gain tables */
extern const u32 dm_tx_bb_gain[TxBBGainTableLength];
extern const u8 dm_cck_tx_bb_gain[CCKTxBBGainTableLength][8];
extern const u8 dm_cck_tx_bb_gain_ch14[CCKTxBBGainTableLength][8];
/* Maps table index to iq amplify gain (dB, 12 to -24dB) */
#define dm_tx_bb_gain_idx_to_amplify(idx) (-idx + 12)

/*------------------------Export global variable----------------------------*/


/*--------------------------Exported Function prototype---------------------*/
/*--------------------------Exported Function prototype---------------------*/

void rtl92e_dm_init(struct net_device *dev);
void rtl92e_dm_deinit(struct net_device *dev);

void rtl92e_dm_watchdog(struct net_device *dev);


void    rtl92e_init_adaptive_rate(struct net_device *dev);
void    rtl92e_dm_txpower_tracking_wq(void *data);

void rtl92e_dm_cck_txpower_adjust(struct net_device *dev, bool binch14);

void    rtl92e_dm_restore_state(struct net_device *dev);
void    rtl92e_dm_backup_state(struct net_device *dev);
void    rtl92e_dm_init_edca_turbo(struct net_device *dev);
void    rtl92e_dm_rf_pathcheck_wq(void *data);
void rtl92e_dm_init_txpower_tracking(struct net_device *dev);
#endif	/*__R8192UDM_H__ */
