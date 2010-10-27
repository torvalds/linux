/*
 * Intel Wireless Multicomm 3200 WiFi driver
 *
 * Copyright (C) 2009 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Intel Corporation <ilw@linux.intel.com>
 * Samuel Ortiz <samuel.ortiz@intel.com>
 * Zhu Yi <yi.zhu@intel.com>
 *
 */

#ifndef __IWM_LMAC_H__
#define __IWM_LMAC_H__

struct iwm_lmac_hdr {
	u8 id;
	u8 flags;
	__le16 seq_num;
} __packed;

/* LMAC commands */
#define CALIB_CFG_FLAG_SEND_COMPLETE_NTFY_AFTER_MSK  0x1

struct iwm_lmac_cal_cfg_elt {
	__le32 enable; /* 1 means LMAC needs to do something */
	__le32 start;  /* 1 to start calibration, 0 to stop */
	__le32 send_res; /* 1 for sending back results */
	__le32 apply_res; /* 1 for applying calibration results to HW */
	__le32 reserved;
} __packed;

struct iwm_lmac_cal_cfg_status {
	struct iwm_lmac_cal_cfg_elt init;
	struct iwm_lmac_cal_cfg_elt periodic;
	__le32 flags; /* CALIB_CFG_FLAG_SEND_COMPLETE_NTFY_AFTER_MSK */
} __packed;

struct iwm_lmac_cal_cfg_cmd {
	struct iwm_lmac_cal_cfg_status ucode_cfg;
	struct iwm_lmac_cal_cfg_status driver_cfg;
	__le32 reserved;
} __packed;

struct iwm_lmac_cal_cfg_resp {
	__le32 status;
} __packed;

#define IWM_CARD_STATE_SW_HW_ENABLED	0x00
#define IWM_CARD_STATE_HW_DISABLED	0x01
#define IWM_CARD_STATE_SW_DISABLED	0x02
#define IWM_CARD_STATE_CTKILL_DISABLED	0x04
#define IWM_CARD_STATE_IS_RXON		0x10

struct iwm_lmac_card_state {
	__le32 flags;
} __packed;

/**
 * COEX_PRIORITY_TABLE_CMD
 *
 * Priority entry for each state
 * Will keep two tables, for STA and WIPAN
 */
enum {
	/* UN-ASSOCIATION PART */
	COEX_UNASSOC_IDLE = 0,
	COEX_UNASSOC_MANUAL_SCAN,
	COEX_UNASSOC_AUTO_SCAN,

	/* CALIBRATION */
	COEX_CALIBRATION,
	COEX_PERIODIC_CALIBRATION,

	/* CONNECTION */
	COEX_CONNECTION_ESTAB,

	/* ASSOCIATION PART */
	COEX_ASSOCIATED_IDLE,
	COEX_ASSOC_MANUAL_SCAN,
	COEX_ASSOC_AUTO_SCAN,
	COEX_ASSOC_ACTIVE_LEVEL,

	/* RF ON/OFF */
	COEX_RF_ON,
	COEX_RF_OFF,
	COEX_STAND_ALONE_DEBUG,

	/* IPNN */
	COEX_IPAN_ASSOC_LEVEL,

	/* RESERVED */
	COEX_RSRVD1,
	COEX_RSRVD2,

	COEX_EVENTS_NUM
};

#define COEX_EVT_FLAG_MEDIUM_FREE_NTFY_MSK	0x1
#define COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_MSK	0x2
#define COEX_EVT_FLAG_DELAY_MEDIUM_FREE_NTFY_MSK	0x4

struct coex_event {
	u8 req_prio;
	u8 win_med_prio;
	u8 reserved;
	u8 flags;
} __packed;

#define COEX_FLAGS_STA_TABLE_VALID_MSK		0x1
#define COEX_FLAGS_UNASSOC_WAKEUP_UMASK_MSK	0x4
#define COEX_FLAGS_ASSOC_WAKEUP_UMASK_MSK	0x8
#define COEX_FLAGS_COEX_ENABLE_MSK		0x80

struct iwm_coex_prio_table_cmd {
	u8 flags;
	u8 reserved[3];
	struct coex_event sta_prio[COEX_EVENTS_NUM];
} __packed;

/* Coexistence definitions
 *
 * Constants to fill in the Priorities' Tables
 * RP - Requested Priority
 * WP - Win Medium Priority: priority assigned when the contention has been won
 * FLAGS - Combination of COEX_EVT_FLAG_MEDIUM_FREE_NTFY_MSK and
 * 	   COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_MSK
 */

#define COEX_UNASSOC_IDLE_FLAGS		0
#define COEX_UNASSOC_MANUAL_SCAN_FLAGS	(COEX_EVT_FLAG_MEDIUM_FREE_NTFY_MSK | \
					 COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_MSK)
#define COEX_UNASSOC_AUTO_SCAN_FLAGS	(COEX_EVT_FLAG_MEDIUM_FREE_NTFY_MSK | \
					 COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_MSK)
#define COEX_CALIBRATION_FLAGS		(COEX_EVT_FLAG_MEDIUM_FREE_NTFY_MSK | \
					 COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_MSK)
#define COEX_PERIODIC_CALIBRATION_FLAGS	0
/* COEX_CONNECTION_ESTAB: we need DELAY_MEDIUM_FREE_NTFY to let WiMAX
 * disconnect from network. */
#define COEX_CONNECTION_ESTAB_FLAGS (COEX_EVT_FLAG_MEDIUM_FREE_NTFY_MSK | \
				     COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_MSK | \
				     COEX_EVT_FLAG_DELAY_MEDIUM_FREE_NTFY_MSK)
#define COEX_ASSOCIATED_IDLE_FLAGS	0
#define COEX_ASSOC_MANUAL_SCAN_FLAGS	(COEX_EVT_FLAG_MEDIUM_FREE_NTFY_MSK | \
					 COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_MSK)
#define COEX_ASSOC_AUTO_SCAN_FLAGS	(COEX_EVT_FLAG_MEDIUM_FREE_NTFY_MSK | \
					 COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_MSK)
#define COEX_ASSOC_ACTIVE_LEVEL_FLAGS	0
#define COEX_RF_ON_FLAGS		0
#define COEX_RF_OFF_FLAGS		0
#define COEX_STAND_ALONE_DEBUG_FLAGS	(COEX_EVT_FLAG_MEDIUM_FREE_NTFY_MSK | \
					 COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_MSK)
#define COEX_IPAN_ASSOC_LEVEL_FLAGS (COEX_EVT_FLAG_MEDIUM_FREE_NTFY_MSK | \
				     COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_MSK | \
				     COEX_EVT_FLAG_DELAY_MEDIUM_FREE_NTFY_MSK)
#define COEX_RSRVD1_FLAGS		0
#define COEX_RSRVD2_FLAGS		0
/* XOR_RF_ON is the event wrapping all radio ownership. We need
 * DELAY_MEDIUM_FREE_NTFY to let WiMAX disconnect from network. */
#define COEX_XOR_RF_ON_FLAGS	    (COEX_EVT_FLAG_MEDIUM_FREE_NTFY_MSK | \
				     COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_MSK | \
				     COEX_EVT_FLAG_DELAY_MEDIUM_FREE_NTFY_MSK)

/* CT kill config command */
struct iwm_ct_kill_cfg_cmd {
	u32 exit_threshold;
	u32 reserved;
	u32 entry_threshold;
} __packed;


/* LMAC OP CODES */
#define REPLY_PAD			0x0
#define REPLY_ALIVE			0x1
#define REPLY_ERROR			0x2
#define REPLY_ECHO			0x3
#define REPLY_HALT			0x6

/* RXON state commands */
#define REPLY_RX_ON			0x10
#define REPLY_RX_ON_ASSOC		0x11
#define REPLY_RX_OFF			0x12
#define REPLY_QOS_PARAM			0x13
#define REPLY_RX_ON_TIMING		0x14
#define REPLY_INTERNAL_QOS_PARAM	0x15
#define REPLY_RX_INT_TIMEOUT_CNFG	0x16
#define REPLY_NULL			0x17

/* Multi-Station support */
#define REPLY_ADD_STA			0x18
#define REPLY_REMOVE_STA		0x19
#define REPLY_RESET_ALL_STA		0x1a

/* RX, TX */
#define REPLY_ALM_RX			0x1b
#define REPLY_TX			0x1c
#define REPLY_TXFIFO_FLUSH		0x1e

/* MISC commands */
#define REPLY_MGMT_MCAST_KEY		0x1f
#define REPLY_WEPKEY			0x20
#define REPLY_INIT_IV			0x21
#define REPLY_WRITE_MIB			0x22
#define REPLY_READ_MIB			0x23
#define REPLY_RADIO_FE			0x24
#define REPLY_TXFIFO_CFG		0x25
#define REPLY_WRITE_READ		0x26
#define REPLY_INSTALL_SEC_KEY		0x27


#define REPLY_RATE_SCALE		0x47
#define REPLY_LEDS_CMD			0x48
#define REPLY_TX_LINK_QUALITY_CMD	0x4e
#define REPLY_ANA_MIB_OVERRIDE_CMD	0x4f
#define REPLY_WRITE2REG_CMD		0x50

/* winfi-wifi coexistence */
#define COEX_PRIORITY_TABLE_CMD		0x5a
#define COEX_MEDIUM_NOTIFICATION	0x5b
#define COEX_EVENT_CMD			0x5c

/* more Protocol and Protocol-test commands */
#define REPLY_MAX_SLEEP_TIME_CMD	0x61
#define CALIBRATION_CFG_CMD		0x65
#define CALIBRATION_RES_NOTIFICATION	0x66
#define CALIBRATION_COMPLETE_NOTIFICATION	0x67

/* Measurements */
#define REPLY_QUIET_CMD			0x71
#define REPLY_CHANNEL_SWITCH		0x72
#define CHANNEL_SWITCH_NOTIFICATION	0x73

#define REPLY_SPECTRUM_MEASUREMENT_CMD	0x74
#define SPECTRUM_MEASURE_NOTIFICATION	0x75
#define REPLY_MEASUREMENT_ABORT_CMD	0x76

/* Power Management */
#define POWER_TABLE_CMD			0x77
#define SAVE_RESTORE_ADDRESS_CMD		0x78
#define REPLY_WATERMARK_CMD		0x79
#define PM_DEBUG_STATISTIC_NOTIFIC	0x7B
#define PD_FLUSH_N_NOTIFICATION		0x7C

/* Scan commands and notifications */
#define REPLY_SCAN_REQUEST_CMD		0x80
#define REPLY_SCAN_ABORT_CMD		0x81
#define SCAN_START_NOTIFICATION		0x82
#define SCAN_RESULTS_NOTIFICATION	0x83
#define SCAN_COMPLETE_NOTIFICATION	0x84

/* Continuous TX commands */
#define REPLY_CONT_TX_CMD		0x85
#define END_OF_CONT_TX_NOTIFICATION	0x86

/* Timer/Eeprom commands */
#define TIMER_CMD			0x87
#define EEPROM_WRITE_CMD		0x88

/* PAPD commands */
#define FEEDBACK_REQUEST_NOTIFICATION	0x8b
#define REPLY_CW_CMD			0x8c

/* IBSS/AP commands Continue */
#define BEACON_NOTIFICATION		0x90
#define REPLY_TX_BEACON			0x91
#define REPLY_REQUEST_ATIM		0x93
#define WHO_IS_AWAKE_NOTIFICATION	0x94
#define TX_PWR_DBM_LIMIT_CMD		0x95
#define QUIET_NOTIFICATION		0x96
#define TX_PWR_TABLE_CMD		0x97
#define TX_ANT_CONFIGURATION_CMD	0x98
#define MEASURE_ABORT_NOTIFICATION	0x99
#define REPLY_CALIBRATION_TUNE		0x9a

/* bt config command */
#define REPLY_BT_CONFIG			0x9b
#define REPLY_STATISTICS_CMD		0x9c
#define STATISTICS_NOTIFICATION		0x9d

/* RF-KILL commands and notifications */
#define REPLY_CARD_STATE_CMD		0xa0
#define CARD_STATE_NOTIFICATION		0xa1

/* Missed beacons notification */
#define MISSED_BEACONS_NOTIFICATION	0xa2
#define MISSED_BEACONS_NOTIFICATION_TH_CMD	0xa3

#define REPLY_CT_KILL_CONFIG_CMD	0xa4

/* HD commands and notifications */
#define REPLY_HD_PARAMS_CMD		0xa6
#define HD_PARAMS_NOTIFICATION		0xa7
#define SENSITIVITY_CMD			0xa8
#define U_APSD_PARAMS_CMD		0xa9
#define NOISY_PLATFORM_CMD		0xaa
#define ILLEGAL_CMD			0xac
#define REPLY_PHY_CALIBRATION_CMD	0xb0
#define REPLAY_RX_GAIN_CALIB_CMD	0xb1

/* WiPAN commands */
#define REPLY_WIPAN_PARAMS_CMD		0xb2
#define REPLY_WIPAN_RX_ON_CMD		0xb3
#define REPLY_WIPAN_RX_ON_TIMING	0xb4
#define REPLY_WIPAN_TX_PWR_TABLE_CMD	0xb5
#define REPLY_WIPAN_RXON_ASSOC_CMD	0xb6
#define REPLY_WIPAN_QOS_PARAM		0xb7
#define WIPAN_REPLY_WEPKEY		0xb8

/* BeamForming commands */
#define BEAMFORMER_CFG_CMD		0xba
#define BEAMFORMEE_NOTIFICATION		0xbb

/* TGn new Commands */
#define REPLY_RX_PHY_CMD		0xc0
#define REPLY_RX_MPDU_CMD		0xc1
#define REPLY_MULTICAST_HASH		0xc2
#define REPLY_KDR_RX			0xc3
#define REPLY_RX_DSP_EXT_INFO		0xc4
#define REPLY_COMPRESSED_BA		0xc5

/* PNC commands */
#define PNC_CONFIG_CMD			0xc8
#define PNC_UPDATE_TABLE_CMD		0xc9
#define XVT_GENERAL_CTRL_CMD		0xca
#define REPLY_LEGACY_RADIO_FE		0xdd

/* WoWLAN commands */
#define WOWLAN_PATTERNS			0xe0
#define WOWLAN_WAKEUP_FILTER		0xe1
#define WOWLAN_TSC_RSC_PARAM		0xe2
#define WOWLAN_TKIP_PARAM		0xe3
#define WOWLAN_KEK_KCK_MATERIAL		0xe4
#define WOWLAN_GET_STATUSES		0xe5
#define WOWLAN_TX_POWER_PER_DB		0xe6
#define REPLY_WOWLAN_GET_STATUSES       WOWLAN_GET_STATUSES

#define REPLY_DEBUG_CMD			0xf0
#define REPLY_DSP_DEBUG_CMD		0xf1
#define REPLY_DEBUG_MONITOR_CMD		0xf2
#define REPLY_DEBUG_XVT_CMD		0xf3
#define REPLY_DEBUG_DC_CALIB		0xf4
#define REPLY_DYNAMIC_BP		0xf5

/* General purpose Commands */
#define REPLY_GP1_CMD			0xfa
#define REPLY_GP2_CMD			0xfb
#define REPLY_GP3_CMD			0xfc
#define REPLY_GP4_CMD			0xfd
#define REPLY_REPLAY_WRAPPER		0xfe
#define REPLY_FRAME_DURATION_CALC_CMD	0xff

#define LMAC_COMMAND_ID_MAX		0xff
#define LMAC_COMMAND_ID_NUM		(LMAC_COMMAND_ID_MAX + 1)


/* Calibration */

enum {
	PHY_CALIBRATE_DC_CMD			= 0,
	PHY_CALIBRATE_LO_CMD			= 1,
	PHY_CALIBRATE_RX_BB_CMD			= 2,
	PHY_CALIBRATE_TX_IQ_CMD			= 3,
	PHY_CALIBRATE_RX_IQ_CMD			= 4,
	PHY_CALIBRATION_NOISE_CMD		= 5,
	PHY_CALIBRATE_AGC_TABLE_CMD		= 6,
	PHY_CALIBRATE_CRYSTAL_FRQ_CMD		= 7,
	PHY_CALIBRATE_OPCODES_NUM,
	SHILOH_PHY_CALIBRATE_DC_CMD		= 8,
	SHILOH_PHY_CALIBRATE_LO_CMD		= 9,
	SHILOH_PHY_CALIBRATE_RX_BB_CMD		= 10,
	SHILOH_PHY_CALIBRATE_TX_IQ_CMD		= 11,
	SHILOH_PHY_CALIBRATE_RX_IQ_CMD		= 12,
	SHILOH_PHY_CALIBRATION_NOISE_CMD	= 13,
	SHILOH_PHY_CALIBRATE_AGC_TABLE_CMD	= 14,
	SHILOH_PHY_CALIBRATE_CRYSTAL_FRQ_CMD	= 15,
	SHILOH_PHY_CALIBRATE_BASE_BAND_CMD	= 16,
	SHILOH_PHY_CALIBRATE_TXIQ_PERIODIC_CMD	= 17,
	CALIBRATION_CMD_NUM,
};

enum {
	CALIB_CFG_RX_BB_IDX       = 0,
	CALIB_CFG_DC_IDX          = 1,
	CALIB_CFG_LO_IDX          = 2,
	CALIB_CFG_TX_IQ_IDX       = 3,
	CALIB_CFG_RX_IQ_IDX       = 4,
	CALIB_CFG_NOISE_IDX       = 5,
	CALIB_CFG_CRYSTAL_IDX     = 6,
	CALIB_CFG_TEMPERATURE_IDX = 7,
	CALIB_CFG_PAPD_IDX        = 8,
	CALIB_CFG_LAST_IDX        = CALIB_CFG_PAPD_IDX,
	CALIB_CFG_MODULE_NUM,
};

#define IWM_CALIB_MAP_INIT_MSK		0xFFFF
#define IWM_CALIB_MAP_PER_LMAC(m)	((m & 0xFF0000) >> 16)
#define IWM_CALIB_MAP_PER_UMAC(m)	((m & 0xFF000000) >> 24)
#define IWM_CALIB_OPCODE_TO_INDEX(op)   (op - PHY_CALIBRATE_OPCODES_NUM)

struct iwm_lmac_calib_hdr {
	u8 opcode;
	u8 first_grp;
	u8 grp_num;
	u8 all_data_valid;
} __packed;

#define IWM_LMAC_CALIB_FREQ_GROUPS_NR	7
#define IWM_CALIB_FREQ_GROUPS_NR	5
#define IWM_CALIB_DC_MODES_NR		12

struct iwm_calib_rxiq_entry {
	u16 ptam_postdist_ars;
	u16 ptam_postdist_arc;
} __packed;

struct iwm_calib_rxiq_group {
	struct iwm_calib_rxiq_entry mode[IWM_CALIB_DC_MODES_NR];
} __packed;

struct iwm_lmac_calib_rxiq {
	struct iwm_calib_rxiq_group group[IWM_LMAC_CALIB_FREQ_GROUPS_NR];
} __packed;

struct iwm_calib_rxiq {
	struct iwm_lmac_calib_hdr hdr;
	struct iwm_calib_rxiq_group group[IWM_CALIB_FREQ_GROUPS_NR];
} __packed;

#define LMAC_STA_ID_SEED	0x0f
#define LMAC_STA_ID_POS		0

#define LMAC_STA_COLOR_SEED	0x7
#define LMAC_STA_COLOR_POS	4

struct iwm_lmac_power_report {
	u8 pa_status;
	u8 pa_integ_res_A[3];
	u8 pa_integ_res_B[3];
	u8 pa_integ_res_C[3];
} __packed;

struct iwm_lmac_tx_resp {
	u8 frame_cnt; /* 1-no aggregation, greater then 1 - aggregation */
	u8 bt_kill_cnt;
	__le16 retry_cnt;
	__le32 initial_tx_rate;
	__le16 wireless_media_time;
	struct iwm_lmac_power_report power_report;
	__le32 tfd_info;
	__le16 seq_ctl;
	__le16 byte_cnt;
	u8 tlc_rate_info;
	u8 ra_tid;
	__le16 frame_ctl;
	__le32 status;
} __packed;

#endif
