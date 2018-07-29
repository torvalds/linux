/* SPDX-License-Identifier: GPL-2.0 */
/*****************************************************************************
 *	Copyright(c) 2007,  RealTEK Technology Inc. All Right Reserved.
 *
 * Module:		Hal819xUsbDM.h	(RTL8192  Header H File)
 *
 *
 * Note:		For dynamic control definition constant structure.
 *
 *
 * Export:
 *
 * Abbrev:
 *
 * History:
 *	Data		Who		Remark
 *	10/04/2007  MHC		Create initial version.
 *
 *****************************************************************************/
 /* Check to see if the file has been included already.  */
#ifndef	__R8192UDM_H__
#define __R8192UDM_H__


/*--------------------------Define Parameters-------------------------------*/
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
#define		CTSToSelfTHVal					30

/* defined by vivi, for tx power track */
#define		E_FOR_TX_POWER_TRACK               300
/* Dynamic Tx Power Control Threshold */
#define		TX_POWER_NEAR_FIELD_THRESH_HIGH		68
#define		TX_POWER_NEAR_FIELD_THRESH_LOW		62
/* added by amy for atheros AP */
#define         TX_POWER_ATHEROAP_THRESH_HIGH           78
#define		TX_POWER_ATHEROAP_THRESH_LOW		72

/* defined by vivi, for showing on UI */
#define			Current_Tx_Rate_Reg         0x1b8
#define			Initial_Tx_Rate_Reg		  0x1b9
#define			Tx_Retry_Count_Reg         0x1ac
#define		RegC38_TH				 20
/*--------------------------Define Parameters-------------------------------*/


/*------------------------------Define structure----------------------------*/

enum dig_algorithm {
	DIG_ALGO_BY_FALSE_ALARM = 0,
	DIG_ALGO_BY_RSSI	= 1,
};

enum dynamic_init_gain_state {
	DM_STA_DIG_OFF = 0,
	DM_STA_DIG_ON,
	DM_STA_DIG_MAX
};

enum dig_connect {
	DIG_DISCONNECT = 0,
	DIG_CONNECT = 1,
};

enum dig_pkt_detection_threshold {
	DIG_PD_AT_LOW_POWER = 0,
	DIG_PD_AT_NORMAL_POWER = 1,
	DIG_PD_AT_HIGH_POWER = 2,
};

enum dig_cck_cs_ratio_state {
	DIG_CS_RATIO_LOWER = 0,
	DIG_CS_RATIO_HIGHER = 1,
};

/* 2007/10/04 MH Define upper and lower threshold of DIG enable or disable. */
struct dig {
	u8		dig_enable_flag;
	enum dig_algorithm		dig_algorithm;
	u8		dbg_mode;
	u8		dig_algorithm_switch;

	long		rssi_low_thresh;
	long		rssi_high_thresh;

	long		rssi_high_power_lowthresh;
	long		rssi_high_power_highthresh;

	enum dynamic_init_gain_state		dig_state;
	enum dynamic_init_gain_state		dig_highpwr_state;
	enum dig_connect		cur_connect_state;
	enum dig_connect		pre_connect_state;

	enum dig_pkt_detection_threshold		curpd_thstate;
	enum dig_pkt_detection_threshold		prepd_thstate;
	enum dig_cck_cs_ratio_state		curcs_ratio_state;
	enum dig_cck_cs_ratio_state		precs_ratio_state;

	u32		pre_ig_value;
	u32		cur_ig_value;

	u8		backoff_val;
	u8		rx_gain_range_max;
	u8		rx_gain_range_min;
	bool		initialgain_lowerbound_state;

	long		rssi_val;
};

enum cck_rx_path_method {
	CCK_Rx_Version_1 = 0,
	CCK_Rx_Version_2 = 1,
};

struct dynamic_rx_path_sel {
	u8		Enable;
	u8		DbgMode;
	enum cck_rx_path_method		cck_method;
	u8		cck_Rx_path;

	u8		SS_TH_low;
	u8		diff_TH;
	u8		disabledRF;
	u8		reserved;

	u8		rf_rssi[4];
	u8		rf_enable_rssi_th[4];
	long		cck_pwdb_sta[4];
};

typedef enum tag_DM_DbgMode_Definition {
	DM_DBG_OFF = 0,
	DM_DBG_ON = 1,
	DM_DBG_MAX
} DM_DBG_E;

typedef struct tag_Tx_Config_Cmd_Format {
	u32	Op;			/* Command packet type. */
	u32	Length;			/* Command packet length. */
	u32	Value;
} DCMD_TXCMD_T, *PDCMD_TXCMD_T;
/*------------------------------Define structure----------------------------*/


/*------------------------Export global variable----------------------------*/
extern struct dig dm_digtable;
extern u8 dm_shadow[16][256];
extern struct dynamic_rx_path_sel DM_RxPathSelTable;
/*------------------------Export global variable----------------------------*/


/*------------------------Export Marco Definition---------------------------*/

/*------------------------Export Marco Definition---------------------------*/


/*--------------------------Exported Function prototype---------------------*/
void init_hal_dm(struct net_device *dev);
void deinit_hal_dm(struct net_device *dev);
void hal_dm_watchdog(struct net_device *dev);
void init_rate_adaptive(struct net_device *dev);
void dm_txpower_trackingcallback(struct work_struct *work);
void dm_restore_dynamic_mechanism_state(struct net_device *dev);
void dm_backup_dynamic_mechanism_state(struct net_device *dev);
void dm_force_tx_fw_info(struct net_device *dev,
			 u32 force_type, u32 force_value);
void dm_init_edca_turbo(struct net_device *dev);
void dm_rf_operation_test_callback(unsigned long data);
void dm_rf_pathcheck_workitemcallback(struct work_struct *work);
void dm_fsync_timer_callback(struct timer_list *t);
void dm_cck_txpower_adjust(struct net_device *dev, bool  binch14);
void dm_shadow_init(struct net_device *dev);
void dm_initialize_txpower_tracking(struct net_device *dev);
/*--------------------------Exported Function prototype---------------------*/


#endif	/*__R8192UDM_H__ */


/* End of r8192U_dm.h */
