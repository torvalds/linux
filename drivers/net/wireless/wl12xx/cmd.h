/*
 * This file is part of wl1271
 *
 * Copyright (C) 1998-2009 Texas Instruments. All rights reserved.
 * Copyright (C) 2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __CMD_H__
#define __CMD_H__

#include "wl12xx.h"

struct acx_header;

int wl1271_cmd_send(struct wl1271 *wl, u16 id, void *buf, size_t len,
		    size_t res_len);
int wl1271_cmd_general_parms(struct wl1271 *wl);
int wl1271_cmd_radio_parms(struct wl1271 *wl);
int wl1271_cmd_ext_radio_parms(struct wl1271 *wl);
int wl1271_cmd_join(struct wl1271 *wl, u8 bss_type);
int wl1271_cmd_test(struct wl1271 *wl, void *buf, size_t buf_len, u8 answer);
int wl1271_cmd_interrogate(struct wl1271 *wl, u16 id, void *buf, size_t len);
int wl1271_cmd_configure(struct wl1271 *wl, u16 id, void *buf, size_t len);
int wl1271_cmd_data_path(struct wl1271 *wl, bool enable);
int wl1271_cmd_ps_mode(struct wl1271 *wl, u8 ps_mode, u32 rates, bool send);
int wl1271_cmd_read_memory(struct wl1271 *wl, u32 addr, void *answer,
			   size_t len);
int wl1271_cmd_template_set(struct wl1271 *wl, u16 template_id,
			    void *buf, size_t buf_len, int index, u32 rates);
int wl1271_cmd_build_null_data(struct wl1271 *wl);
int wl1271_cmd_build_ps_poll(struct wl1271 *wl, u16 aid);
int wl1271_cmd_build_probe_req(struct wl1271 *wl,
			       const u8 *ssid, size_t ssid_len,
			       const u8 *ie, size_t ie_len, u8 band);
struct sk_buff *wl1271_cmd_build_ap_probe_req(struct wl1271 *wl,
					      struct sk_buff *skb);
int wl1271_build_qos_null_data(struct wl1271 *wl);
int wl1271_cmd_build_klv_null_data(struct wl1271 *wl);
int wl1271_cmd_set_default_wep_key(struct wl1271 *wl, u8 id);
int wl1271_cmd_set_key(struct wl1271 *wl, u16 action, u8 id, u8 key_type,
		       u8 key_size, const u8 *key, const u8 *addr,
		       u32 tx_seq_32, u16 tx_seq_16);
int wl1271_cmd_disconnect(struct wl1271 *wl);
int wl1271_cmd_set_sta_state(struct wl1271 *wl);

enum wl1271_commands {
	CMD_INTERROGATE     = 1,    /*use this to read information elements*/
	CMD_CONFIGURE       = 2,    /*use this to write information elements*/
	CMD_ENABLE_RX       = 3,
	CMD_ENABLE_TX       = 4,
	CMD_DISABLE_RX      = 5,
	CMD_DISABLE_TX      = 6,
	CMD_SCAN            = 8,
	CMD_STOP_SCAN       = 9,
	CMD_START_JOIN      = 11,
	CMD_SET_KEYS        = 12,
	CMD_READ_MEMORY     = 13,
	CMD_WRITE_MEMORY    = 14,
	CMD_SET_TEMPLATE    = 19,
	CMD_TEST            = 23,
	CMD_NOISE_HIST      = 28,
	CMD_LNA_CONTROL     = 32,
	CMD_SET_BCN_MODE    = 33,
	CMD_MEASUREMENT      = 34,
	CMD_STOP_MEASUREMENT = 35,
	CMD_DISCONNECT       = 36,
	CMD_SET_PS_MODE      = 37,
	CMD_CHANNEL_SWITCH   = 38,
	CMD_STOP_CHANNEL_SWICTH = 39,
	CMD_AP_DISCOVERY     = 40,
	CMD_STOP_AP_DISCOVERY = 41,
	CMD_SPS_SCAN = 42,
	CMD_STOP_SPS_SCAN = 43,
	CMD_HEALTH_CHECK     = 45,
	CMD_DEBUG            = 46,
	CMD_TRIGGER_SCAN_TO  = 47,
	CMD_CONNECTION_SCAN_CFG      = 48,
	CMD_CONNECTION_SCAN_SSID_CFG = 49,
	CMD_START_PERIODIC_SCAN      = 50,
	CMD_STOP_PERIODIC_SCAN       = 51,
	CMD_SET_STA_STATE            = 52,

	NUM_COMMANDS,
	MAX_COMMAND_ID = 0xFFFF,
};

#define MAX_CMD_PARAMS 572

enum {
	CMD_TEMPL_KLV_IDX_NULL_DATA = 0,
	CMD_TEMPL_KLV_IDX_MAX = 4
};

enum cmd_templ {
	CMD_TEMPL_NULL_DATA = 0,
	CMD_TEMPL_BEACON,
	CMD_TEMPL_CFG_PROBE_REQ_2_4,
	CMD_TEMPL_CFG_PROBE_REQ_5,
	CMD_TEMPL_PROBE_RESPONSE,
	CMD_TEMPL_QOS_NULL_DATA,
	CMD_TEMPL_PS_POLL,
	CMD_TEMPL_KLV,
	CMD_TEMPL_DISCONNECT,
	CMD_TEMPL_PROBE_REQ_2_4, /* for firmware internal use only */
	CMD_TEMPL_PROBE_REQ_5,   /* for firmware internal use only */
	CMD_TEMPL_BAR,           /* for firmware internal use only */
	CMD_TEMPL_CTS,           /*
				  * For CTS-to-self (FastCTS) mechanism
				  * for BT/WLAN coexistence (SoftGemini). */
	CMD_TEMPL_MAX = 0xff
};

/* unit ms */
#define WL1271_COMMAND_TIMEOUT     2000
#define WL1271_CMD_TEMPL_MAX_SIZE  252
#define WL1271_EVENT_TIMEOUT       750

struct wl1271_cmd_header {
	__le16 id;
	__le16 status;
	/* payload */
	u8 data[0];
} __packed;

#define WL1271_CMD_MAX_PARAMS 572

struct wl1271_command {
	struct wl1271_cmd_header header;
	u8  parameters[WL1271_CMD_MAX_PARAMS];
} __packed;

enum {
	CMD_MAILBOX_IDLE		=  0,
	CMD_STATUS_SUCCESS		=  1,
	CMD_STATUS_UNKNOWN_CMD		=  2,
	CMD_STATUS_UNKNOWN_IE		=  3,
	CMD_STATUS_REJECT_MEAS_SG_ACTIVE	= 11,
	CMD_STATUS_RX_BUSY		= 13,
	CMD_STATUS_INVALID_PARAM		= 14,
	CMD_STATUS_TEMPLATE_TOO_LARGE		= 15,
	CMD_STATUS_OUT_OF_MEMORY		= 16,
	CMD_STATUS_STA_TABLE_FULL		= 17,
	CMD_STATUS_RADIO_ERROR		= 18,
	CMD_STATUS_WRONG_NESTING		= 19,
	CMD_STATUS_TIMEOUT		= 21, /* Driver internal use.*/
	CMD_STATUS_FW_RESET		= 22, /* Driver internal use.*/
	MAX_COMMAND_STATUS		= 0xff
};

#define CMDMBOX_HEADER_LEN 4
#define CMDMBOX_INFO_ELEM_HEADER_LEN 4

enum {
	BSS_TYPE_IBSS = 0,
	BSS_TYPE_STA_BSS = 2,
	BSS_TYPE_AP_BSS = 3,
	MAX_BSS_TYPE = 0xFF
};

#define WL1271_JOIN_CMD_CTRL_TX_FLUSH     0x80 /* Firmware flushes all Tx */
#define WL1271_JOIN_CMD_TX_SESSION_OFFSET 1
#define WL1271_JOIN_CMD_BSS_TYPE_5GHZ 0x10

struct wl1271_cmd_join {
	struct wl1271_cmd_header header;

	__le32 bssid_lsb;
	__le16 bssid_msb;
	__le16 beacon_interval; /* in TBTTs */
	__le32 rx_config_options;
	__le32 rx_filter_options;

	/*
	 * The target uses this field to determine the rate at
	 * which to transmit control frame responses (such as
	 * ACK or CTS frames).
	 */
	__le32 basic_rate_set;
	u8 dtim_interval;
	/*
	 * bits 0-2: This bitwise field specifies the type
	 * of BSS to start or join (BSS_TYPE_*).
	 * bit 4: Band - The radio band in which to join
	 * or start.
	 *  0 - 2.4GHz band
	 *  1 - 5GHz band
	 * bits 3, 5-7: Reserved
	 */
	u8 bss_type;
	u8 channel;
	u8 ssid_len;
	u8 ssid[IW_ESSID_MAX_SIZE];
	u8 ctrl; /* JOIN_CMD_CTRL_* */
	u8 reserved[3];
} __packed;

struct cmd_enabledisable_path {
	struct wl1271_cmd_header header;

	u8 channel;
	u8 padding[3];
} __packed;

#define WL1271_RATE_AUTOMATIC  0

struct wl1271_cmd_template_set {
	struct wl1271_cmd_header header;

	__le16 len;
	u8 template_type;
	u8 index;  /* relevant only for KLV_TEMPLATE type */
	__le32 enabled_rates;
	u8 short_retry_limit;
	u8 long_retry_limit;
	u8 aflags;
	u8 reserved;
	u8 template_data[WL1271_CMD_TEMPL_MAX_SIZE];
} __packed;

#define TIM_ELE_ID    5
#define PARTIAL_VBM_MAX    251

struct wl1271_tim {
	u8 identity;
	u8 length;
	u8 dtim_count;
	u8 dtim_period;
	u8 bitmap_ctrl;
	u8 pvb_field[PARTIAL_VBM_MAX]; /* Partial Virtual Bitmap */
} __packed;

enum wl1271_cmd_ps_mode {
	STATION_ACTIVE_MODE,
	STATION_POWER_SAVE_MODE
};

struct wl1271_cmd_ps_params {
	struct wl1271_cmd_header header;

	u8 ps_mode; /* STATION_* */
	u8 send_null_data; /* Do we have to send NULL data packet ? */
	u8 retries; /* Number of retires for the initial NULL data packet */

	 /*
	  * TUs during which the target stays awake after switching
	  * to power save mode.
	  */
	u8 hang_over_period;
	__le32 null_data_rate;
} __packed;

/* HW encryption keys */
#define NUM_ACCESS_CATEGORIES_COPY 4
#define MAX_KEY_SIZE 32

enum wl1271_cmd_key_action {
	KEY_ADD_OR_REPLACE = 1,
	KEY_REMOVE         = 2,
	KEY_SET_ID         = 3,
	MAX_KEY_ACTION     = 0xffff,
};

enum wl1271_cmd_key_type {
	KEY_NONE = 0,
	KEY_WEP  = 1,
	KEY_TKIP = 2,
	KEY_AES  = 3,
	KEY_GEM  = 4,
};

/* FIXME: Add description for key-types */

struct wl1271_cmd_set_keys {
	struct wl1271_cmd_header header;

	/* Ignored for default WEP key */
	u8 addr[ETH_ALEN];

	/* key_action_e */
	__le16 key_action;

	__le16 reserved_1;

	/* key size in bytes */
	u8 key_size;

	/* key_type_e */
	u8 key_type;
	u8 ssid_profile;

	/*
	 * TKIP, AES: frame's key id field.
	 * For WEP default key: key id;
	 */
	u8 id;
	u8 reserved_2[6];
	u8 key[MAX_KEY_SIZE];
	__le16 ac_seq_num16[NUM_ACCESS_CATEGORIES_COPY];
	__le32 ac_seq_num32[NUM_ACCESS_CATEGORIES_COPY];
} __packed;

struct wl1271_cmd_test_header {
	u8 id;
	u8 padding[3];
} __packed;

enum wl1271_channel_tune_bands {
	WL1271_CHANNEL_TUNE_BAND_2_4,
	WL1271_CHANNEL_TUNE_BAND_5,
	WL1271_CHANNEL_TUNE_BAND_4_9
};

#define WL1271_PD_REFERENCE_POINT_BAND_B_G  0

#define TEST_CMD_INI_FILE_RADIO_PARAM       0x19
#define TEST_CMD_INI_FILE_GENERAL_PARAM     0x1E
#define TEST_CMD_INI_FILE_RF_EXTENDED_PARAM 0x26

struct wl1271_general_parms_cmd {
	struct wl1271_cmd_header header;

	struct wl1271_cmd_test_header test;

	struct wl1271_ini_general_params general_params;

	u8 sr_debug_table[WL1271_INI_MAX_SMART_REFLEX_PARAM];
	u8 sr_sen_n_p;
	u8 sr_sen_n_p_gain;
	u8 sr_sen_nrn;
	u8 sr_sen_prn;
	u8 padding[3];
} __packed;

struct wl1271_radio_parms_cmd {
	struct wl1271_cmd_header header;

	struct wl1271_cmd_test_header test;

	/* Static radio parameters */
	struct wl1271_ini_band_params_2 static_params_2;
	struct wl1271_ini_band_params_5 static_params_5;

	/* Dynamic radio parameters */
	struct wl1271_ini_fem_params_2 dyn_params_2;
	u8 padding2;
	struct wl1271_ini_fem_params_5 dyn_params_5;
	u8 padding3[2];
} __packed;

struct wl1271_ext_radio_parms_cmd {
	struct wl1271_cmd_header header;

	struct wl1271_cmd_test_header test;

	u8 tx_per_channel_power_compensation_2[CONF_TX_PWR_COMPENSATION_LEN_2];
	u8 tx_per_channel_power_compensation_5[CONF_TX_PWR_COMPENSATION_LEN_5];
	u8 padding[3];
} __packed;

/*
 * There are three types of disconnections:
 *
 * DISCONNECT_IMMEDIATE: the fw doesn't send any frames
 * DISCONNECT_DEAUTH:    the fw generates a DEAUTH request with the reason
 *                       we have passed
 * DISCONNECT_DISASSOC:  the fw generates a DESASSOC request with the reason
 *                       we have passed
 */
enum wl1271_disconnect_type {
	DISCONNECT_IMMEDIATE,
	DISCONNECT_DEAUTH,
	DISCONNECT_DISASSOC
};

struct wl1271_cmd_disconnect {
	struct wl1271_cmd_header header;

	__le32 rx_config_options;
	__le32 rx_filter_options;

	__le16 reason;
	u8  type;

	u8  padding;
} __packed;

#define WL1271_CMD_STA_STATE_CONNECTED  1

struct wl1271_cmd_set_sta_state {
	struct wl1271_cmd_header header;

	u8 state;
	u8 padding[3];
} __packed;

#endif /* __WL1271_CMD_H__ */
