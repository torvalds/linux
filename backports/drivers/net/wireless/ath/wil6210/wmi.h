/*
 * Copyright (c) 2012-2014 Qualcomm Atheros, Inc.
 * Copyright (c) 2006-2012 Wilocity .
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This file contains the definitions of the WMI protocol specified in the
 * Wireless Module Interface (WMI) for the Wilocity
 * MARLON 60 Gigabit wireless solution.
 * It includes definitions of all the commands and events.
 * Commands are messages from the host to the WM.
 * Events are messages from the WM to the host.
 */

#ifndef __WILOCITY_WMI_H__
#define __WILOCITY_WMI_H__

/* General */
#define WILOCITY_MAX_ASSOC_STA (8)
#define WMI_MAC_LEN		(6)
#define WMI_PROX_RANGE_NUM	(3)

/* List of Commands */
enum wmi_command_id {
	WMI_CONNECT_CMDID		= 0x0001,
	WMI_DISCONNECT_CMDID		= 0x0003,
	WMI_DISCONNECT_STA_CMDID	= 0x0004,
	WMI_START_SCAN_CMDID		= 0x0007,
	WMI_SET_BSS_FILTER_CMDID	= 0x0009,
	WMI_SET_PROBED_SSID_CMDID	= 0x000a,
	WMI_SET_LISTEN_INT_CMDID	= 0x000b,
	WMI_BCON_CTRL_CMDID		= 0x000f,
	WMI_ADD_CIPHER_KEY_CMDID	= 0x0016,
	WMI_DELETE_CIPHER_KEY_CMDID	= 0x0017,
	WMI_SET_APPIE_CMDID		= 0x003f,
	WMI_SET_WSC_STATUS_CMDID	= 0x0041,
	WMI_PXMT_RANGE_CFG_CMDID	= 0x0042,
	WMI_PXMT_SNR2_RANGE_CFG_CMDID	= 0x0043,
	WMI_FAST_MEM_ACC_MODE_CMDID	= 0x0300,
	WMI_MEM_READ_CMDID		= 0x0800,
	WMI_MEM_WR_CMDID		= 0x0801,
	WMI_ECHO_CMDID			= 0x0803,
	WMI_DEEP_ECHO_CMDID		= 0x0804,
	WMI_CONFIG_MAC_CMDID		= 0x0805,
	WMI_CONFIG_PHY_DEBUG_CMDID	= 0x0806,
	WMI_ADD_DEBUG_TX_PCKT_CMDID	= 0x0808,
	WMI_PHY_GET_STATISTICS_CMDID	= 0x0809,
	WMI_FS_TUNE_CMDID		= 0x080a,
	WMI_CORR_MEASURE_CMDID		= 0x080b,
	WMI_READ_RSSI_CMDID		= 0x080c,
	WMI_TEMP_SENSE_CMDID		= 0x080e,
	WMI_DC_CALIB_CMDID		= 0x080f,
	WMI_SEND_TONE_CMDID		= 0x0810,
	WMI_IQ_TX_CALIB_CMDID		= 0x0811,
	WMI_IQ_RX_CALIB_CMDID		= 0x0812,
	WMI_SET_UCODE_IDLE_CMDID	= 0x0813,
	WMI_SET_WORK_MODE_CMDID		= 0x0815,
	WMI_LO_LEAKAGE_CALIB_CMDID	= 0x0816,
	WMI_MARLON_R_ACTIVATE_CMDID	= 0x0817,
	WMI_MARLON_R_READ_CMDID		= 0x0818,
	WMI_MARLON_R_WRITE_CMDID	= 0x0819,
	WMI_MARLON_R_TXRX_SEL_CMDID	= 0x081a,
	MAC_IO_STATIC_PARAMS_CMDID	= 0x081b,
	MAC_IO_DYNAMIC_PARAMS_CMDID	= 0x081c,
	WMI_SILENT_RSSI_CALIB_CMDID	= 0x081d,
	WMI_RF_RX_TEST_CMDID		= 0x081e,
	WMI_CFG_RX_CHAIN_CMDID		= 0x0820,
	WMI_VRING_CFG_CMDID		= 0x0821,
	WMI_VRING_BA_EN_CMDID		= 0x0823,
	WMI_VRING_BA_DIS_CMDID		= 0x0824,
	WMI_RCP_ADDBA_RESP_CMDID	= 0x0825,
	WMI_RCP_DELBA_CMDID		= 0x0826,
	WMI_SET_SSID_CMDID		= 0x0827,
	WMI_GET_SSID_CMDID		= 0x0828,
	WMI_SET_PCP_CHANNEL_CMDID	= 0x0829,
	WMI_GET_PCP_CHANNEL_CMDID	= 0x082a,
	WMI_SW_TX_REQ_CMDID		= 0x082b,
	WMI_READ_MAC_RXQ_CMDID		= 0x0830,
	WMI_READ_MAC_TXQ_CMDID		= 0x0831,
	WMI_WRITE_MAC_RXQ_CMDID		= 0x0832,
	WMI_WRITE_MAC_TXQ_CMDID		= 0x0833,
	WMI_WRITE_MAC_XQ_FIELD_CMDID	= 0x0834,
	WMI_MLME_PUSH_CMDID		= 0x0835,
	WMI_BEAMFORMING_MGMT_CMDID	= 0x0836,
	WMI_BF_TXSS_MGMT_CMDID		= 0x0837,
	WMI_BF_SM_MGMT_CMDID		= 0x0838,
	WMI_BF_RXSS_MGMT_CMDID		= 0x0839,
	WMI_SET_SECTORS_CMDID		= 0x0849,
	WMI_MAINTAIN_PAUSE_CMDID	= 0x0850,
	WMI_MAINTAIN_RESUME_CMDID	= 0x0851,
	WMI_RS_MGMT_CMDID		= 0x0852,
	WMI_RF_MGMT_CMDID		= 0x0853,
	/* Performance monitoring commands */
	WMI_BF_CTRL_CMDID		= 0x0862,
	WMI_NOTIFY_REQ_CMDID		= 0x0863,
	WMI_GET_STATUS_CMDID		= 0x0864,
	WMI_UNIT_TEST_CMDID		= 0x0900,
	WMI_HICCUP_CMDID		= 0x0901,
	WMI_FLASH_READ_CMDID		= 0x0902,
	WMI_FLASH_WRITE_CMDID		= 0x0903,
	WMI_SECURITY_UNIT_TEST_CMDID	= 0x0904,
	/*P2P*/
	WMI_P2P_CFG_CMDID		= 0x0910,
	WMI_PORT_ALLOCATE_CMDID		= 0x0911,
	WMI_PORT_DELETE_CMDID		= 0x0912,
	WMI_POWER_MGMT_CFG_CMDID	= 0x0913,
	WMI_START_LISTEN_CMDID		= 0x0914,
	WMI_START_SEARCH_CMDID		= 0x0915,
	WMI_DISCOVERY_START_CMDID	= 0x0916,
	WMI_DISCOVERY_STOP_CMDID	= 0x0917,
	WMI_PCP_START_CMDID		= 0x0918,
	WMI_PCP_STOP_CMDID		= 0x0919,
	WMI_GET_PCP_FACTOR_CMDID	= 0x091b,

	WMI_SET_MAC_ADDRESS_CMDID	= 0xf003,
	WMI_ABORT_SCAN_CMDID		= 0xf007,
	WMI_SET_PMK_CMDID		= 0xf028,

	WMI_SET_PROMISCUOUS_MODE_CMDID	= 0xf041,
	WMI_GET_PMK_CMDID		= 0xf048,
	WMI_SET_PASSPHRASE_CMDID	= 0xf049,
	WMI_SEND_ASSOC_RES_CMDID	= 0xf04a,
	WMI_SET_ASSOC_REQ_RELAY_CMDID	= 0xf04b,
	WMI_EAPOL_TX_CMDID		= 0xf04c,
	WMI_MAC_ADDR_REQ_CMDID		= 0xf04d,
	WMI_FW_VER_CMDID		= 0xf04e,
};

/*
 * Commands data structures
 */

/*
 * WMI_CONNECT_CMDID
 */
enum wmi_network_type {
	WMI_NETTYPE_INFRA		= 0x01,
	WMI_NETTYPE_ADHOC		= 0x02,
	WMI_NETTYPE_ADHOC_CREATOR	= 0x04,
	WMI_NETTYPE_AP			= 0x10,
	WMI_NETTYPE_P2P			= 0x20,
	WMI_NETTYPE_WBE			= 0x40, /* PCIE over 60g */
};

enum wmi_dot11_auth_mode {
	WMI_AUTH11_OPEN			= 0x01,
	WMI_AUTH11_SHARED		= 0x02,
	WMI_AUTH11_LEAP			= 0x04,
	WMI_AUTH11_WSC			= 0x08,
};

enum wmi_auth_mode {
	WMI_AUTH_NONE			= 0x01,
	WMI_AUTH_WPA			= 0x02,
	WMI_AUTH_WPA2			= 0x04,
	WMI_AUTH_WPA_PSK		= 0x08,
	WMI_AUTH_WPA2_PSK		= 0x10,
	WMI_AUTH_WPA_CCKM		= 0x20,
	WMI_AUTH_WPA2_CCKM		= 0x40,
};

enum wmi_crypto_type {
	WMI_CRYPT_NONE			= 0x01,
	WMI_CRYPT_WEP			= 0x02,
	WMI_CRYPT_TKIP			= 0x04,
	WMI_CRYPT_AES			= 0x08,
	WMI_CRYPT_AES_GCMP		= 0x20,
};

enum wmi_connect_ctrl_flag_bits {
	WMI_CONNECT_ASSOC_POLICY_USER		= 0x0001,
	WMI_CONNECT_SEND_REASSOC		= 0x0002,
	WMI_CONNECT_IGNORE_WPA_GROUP_CIPHER	= 0x0004,
	WMI_CONNECT_PROFILE_MATCH_DONE		= 0x0008,
	WMI_CONNECT_IGNORE_AAC_BEACON		= 0x0010,
	WMI_CONNECT_CSA_FOLLOW_BSS		= 0x0020,
	WMI_CONNECT_DO_WPA_OFFLOAD		= 0x0040,
	WMI_CONNECT_DO_NOT_DEAUTH		= 0x0080,
};

#define WMI_MAX_SSID_LEN    (32)

struct wmi_connect_cmd {
	u8 network_type;
	u8 dot11_auth_mode;
	u8 auth_mode;
	u8 pairwise_crypto_type;
	u8 pairwise_crypto_len;
	u8 group_crypto_type;
	u8 group_crypto_len;
	u8 ssid_len;
	u8 ssid[WMI_MAX_SSID_LEN];
	u8 channel;
	u8 reserved0;
	u8 bssid[WMI_MAC_LEN];
	__le32 ctrl_flags;
	u8 dst_mac[WMI_MAC_LEN];
	u8 reserved1[2];
} __packed;

/*
 * WMI_DISCONNECT_STA_CMDID
 */
struct wmi_disconnect_sta_cmd {
	u8 dst_mac[WMI_MAC_LEN];
	__le16 disconnect_reason;
} __packed;

/*
 * WMI_SET_PMK_CMDID
 */

#define WMI_MIN_KEY_INDEX	(0)
#define WMI_MAX_KEY_INDEX	(3)
#define WMI_MAX_KEY_LEN		(32)
#define WMI_PASSPHRASE_LEN	(64)
#define WMI_PMK_LEN		(32)

struct  wmi_set_pmk_cmd {
	u8 pmk[WMI_PMK_LEN];
} __packed;

/*
 * WMI_SET_PASSPHRASE_CMDID
 */
struct wmi_set_passphrase_cmd {
	u8 ssid[WMI_MAX_SSID_LEN];
	u8 passphrase[WMI_PASSPHRASE_LEN];
	u8 ssid_len;
	u8 passphrase_len;
} __packed;

/*
 * WMI_ADD_CIPHER_KEY_CMDID
 */
enum wmi_key_usage {
	WMI_KEY_USE_PAIRWISE	= 0,
	WMI_KEY_USE_GROUP	= 1,
	WMI_KEY_USE_TX		= 2,  /* default Tx Key - Static WEP only */
};

struct wmi_add_cipher_key_cmd {
	u8 key_index;
	u8 key_type;
	u8 key_usage;		/* enum wmi_key_usage */
	u8 key_len;
	u8 key_rsc[8];		/* key replay sequence counter */
	u8 key[WMI_MAX_KEY_LEN];
	u8 key_op_ctrl;		/* Additional Key Control information */
	u8 mac[WMI_MAC_LEN];
} __packed;

/*
 * WMI_DELETE_CIPHER_KEY_CMDID
 */
struct wmi_delete_cipher_key_cmd {
	u8 key_index;
	u8 mac[WMI_MAC_LEN];
} __packed;

/*
 * WMI_START_SCAN_CMDID
 *
 * Start L1 scan operation
 *
 * Returned events:
 * - WMI_RX_MGMT_PACKET_EVENTID - for every probe resp.
 * - WMI_SCAN_COMPLETE_EVENTID
 */
enum wmi_scan_type {
	WMI_LONG_SCAN		= 0,
	WMI_SHORT_SCAN		= 1,
	WMI_PBC_SCAN		= 2,
	WMI_ACTIVE_SCAN		= 3,
	WMI_DIRECT_SCAN		= 4,
};

struct wmi_start_scan_cmd {
	u8 direct_scan_mac_addr[6];
	u8 reserved[2];
	__le32 home_dwell_time;	/* Max duration in the home channel(ms) */
	__le32 force_scan_interval;	/* Time interval between scans (ms)*/
	u8 scan_type;		/* wmi_scan_type */
	u8 num_channels;		/* how many channels follow */
	struct {
		u8 channel;
		u8 reserved;
	} channel_list[0];	/* channels ID's */
				/* 0 - 58320 MHz */
				/* 1 - 60480 MHz */
				/* 2 - 62640 MHz */
} __packed;

/*
 * WMI_SET_PROBED_SSID_CMDID
 */
#define MAX_PROBED_SSID_INDEX	(3)

enum wmi_ssid_flag {
	WMI_SSID_FLAG_DISABLE	= 0,	/* disables entry */
	WMI_SSID_FLAG_SPECIFIC	= 1,	/* probes specified ssid */
	WMI_SSID_FLAG_ANY	= 2,	/* probes for any ssid */
};

struct wmi_probed_ssid_cmd {
	u8 entry_index;			/* 0 to MAX_PROBED_SSID_INDEX */
	u8 flag;			/* enum wmi_ssid_flag */
	u8 ssid_len;
	u8 ssid[WMI_MAX_SSID_LEN];
} __packed;

/*
 * WMI_SET_APPIE_CMDID
 * Add Application specified IE to a management frame
 */
#define WMI_MAX_IE_LEN		(1024)

/*
 * Frame Types
 */
enum wmi_mgmt_frame_type {
	WMI_FRAME_BEACON	= 0,
	WMI_FRAME_PROBE_REQ	= 1,
	WMI_FRAME_PROBE_RESP	= 2,
	WMI_FRAME_ASSOC_REQ	= 3,
	WMI_FRAME_ASSOC_RESP	= 4,
	WMI_NUM_MGMT_FRAME,
};

struct wmi_set_appie_cmd {
	u8 mgmt_frm_type;	/* enum wmi_mgmt_frame_type */
	u8 reserved;
	__le16 ie_len;	/* Length of the IE to be added to MGMT frame */
	u8 ie_info[0];
} __packed;

/*
 * WMI_PXMT_RANGE_CFG_CMDID
 */
struct wmi_pxmt_range_cfg_cmd {
	u8 dst_mac[WMI_MAC_LEN];
	__le16 range;
} __packed;

/*
 * WMI_PXMT_SNR2_RANGE_CFG_CMDID
 */
struct wmi_pxmt_snr2_range_cfg_cmd {
	s8 snr2range_arr[WMI_PROX_RANGE_NUM-1];
} __packed;

/*
 * WMI_RF_MGMT_CMDID
 */
enum wmi_rf_mgmt_type {
	WMI_RF_MGMT_W_DISABLE	= 0,
	WMI_RF_MGMT_W_ENABLE	= 1,
	WMI_RF_MGMT_GET_STATUS	= 2,
};

struct wmi_rf_mgmt_cmd {
	__le32 rf_mgmt_type;
} __packed;

/*
 * WMI_RF_RX_TEST_CMDID
 */
struct wmi_rf_rx_test_cmd {
	__le32 sector;
} __packed;

/*
 * WMI_CORR_MEASURE_CMDID
 */
struct wmi_corr_measure_cmd {
	s32 freq_mhz;
	__le32 length_samples;
	__le32 iterations;
} __packed;

/*
 * WMI_SET_SSID_CMDID
 */
struct wmi_set_ssid_cmd {
	__le32 ssid_len;
	u8 ssid[WMI_MAX_SSID_LEN];
} __packed;

/*
 * WMI_SET_PCP_CHANNEL_CMDID
 */
struct wmi_set_pcp_channel_cmd {
	u8 channel;
	u8 reserved[3];
} __packed;

/*
 * WMI_BCON_CTRL_CMDID
 */
struct wmi_bcon_ctrl_cmd {
	__le16 bcon_interval;
	__le16 frag_num;
	__le64 ss_mask;
	u8 network_type;
	u8 pcp_max_assoc_sta;
	u8 disable_sec_offload;
	u8 disable_sec;
} __packed;

/******* P2P ***********/

/*
 * WMI_PORT_ALLOCATE_CMDID
 */
enum wmi_port_role {
	WMI_PORT_STA		= 0,
	WMI_PORT_PCP		= 1,
	WMI_PORT_AP		= 2,
	WMI_PORT_P2P_DEV	= 3,
	WMI_PORT_P2P_CLIENT	= 4,
	WMI_PORT_P2P_GO		= 5,
};

struct wmi_port_allocate_cmd {
	u8 mac[WMI_MAC_LEN];
	u8 port_role;
	u8 mid;
} __packed;

/*
 * WMI_PORT_DELETE_CMDID
 */
struct wmi_delete_port_cmd {
	u8 mid;
	u8 reserved[3];
} __packed;

/*
 * WMI_P2P_CFG_CMDID
 */
enum wmi_discovery_mode {
	WMI_DISCOVERY_MODE_NON_OFFLOAD	= 0,
	WMI_DISCOVERY_MODE_OFFLOAD	= 1,
	WMI_DISCOVERY_MODE_PEER2PEER	= 2,
};

struct wmi_p2p_cfg_cmd {
	u8 discovery_mode;	/* wmi_discovery_mode */
	u8 channel;
	__le16 bcon_interval; /* base to listen/search duration calculation */
} __packed;

/*
 * WMI_POWER_MGMT_CFG_CMDID
 */
enum wmi_power_source_type {
	WMI_POWER_SOURCE_BATTERY	= 0,
	WMI_POWER_SOURCE_OTHER		= 1,
};

struct wmi_power_mgmt_cfg_cmd {
	u8 power_source;	/* wmi_power_source_type */
	u8 reserved[3];
} __packed;

/*
 * WMI_PCP_START_CMDID
 */
struct wmi_pcp_start_cmd {
	__le16 bcon_interval;
	u8 pcp_max_assoc_sta;
	u8 reserved0[9];
	u8 network_type;
	u8 channel;
	u8 disable_sec_offload;
	u8 disable_sec;
} __packed;

/*
 * WMI_SW_TX_REQ_CMDID
 */
struct wmi_sw_tx_req_cmd {
	u8 dst_mac[WMI_MAC_LEN];
	__le16 len;
	u8 payload[0];
} __packed;

/*
 * WMI_VRING_CFG_CMDID
 */

struct wmi_sw_ring_cfg {
	__le64 ring_mem_base;
	__le16 ring_size;
	__le16 max_mpdu_size;
} __packed;

struct wmi_vring_cfg_schd {
	__le16 priority;
	__le16 timeslot_us;
} __packed;

enum wmi_vring_cfg_encap_trans_type {
	WMI_VRING_ENC_TYPE_802_3		= 0,
	WMI_VRING_ENC_TYPE_NATIVE_WIFI		= 1,
};

enum wmi_vring_cfg_ds_cfg {
	WMI_VRING_DS_PBSS			= 0,
	WMI_VRING_DS_STATION			= 1,
	WMI_VRING_DS_AP				= 2,
	WMI_VRING_DS_ADDR4			= 3,
};

enum wmi_vring_cfg_nwifi_ds_trans_type {
	WMI_NWIFI_TX_TRANS_MODE_NO		= 0,
	WMI_NWIFI_TX_TRANS_MODE_AP2PBSS		= 1,
	WMI_NWIFI_TX_TRANS_MODE_STA2PBSS	= 2,
};

enum wmi_vring_cfg_schd_params_priority {
	WMI_SCH_PRIO_REGULAR			= 0,
	WMI_SCH_PRIO_HIGH			= 1,
};

#define CIDXTID_CID_POS (0)
#define CIDXTID_CID_LEN (4)
#define CIDXTID_CID_MSK (0xF)
#define CIDXTID_TID_POS (4)
#define CIDXTID_TID_LEN (4)
#define CIDXTID_TID_MSK (0xF0)

struct wmi_vring_cfg {
	struct wmi_sw_ring_cfg tx_sw_ring;
	u8 ringid;				/* 0-23 vrings */

	u8 cidxtid;

	u8 encap_trans_type;
	u8 ds_cfg;				/* 802.3 DS cfg */
	u8 nwifi_ds_trans_type;

	#define VRING_CFG_MAC_CTRL_LIFETIME_EN_POS (0)
	#define VRING_CFG_MAC_CTRL_LIFETIME_EN_LEN (1)
	#define VRING_CFG_MAC_CTRL_LIFETIME_EN_MSK (0x1)
	#define VRING_CFG_MAC_CTRL_AGGR_EN_POS (1)
	#define VRING_CFG_MAC_CTRL_AGGR_EN_LEN (1)
	#define VRING_CFG_MAC_CTRL_AGGR_EN_MSK (0x2)
	u8 mac_ctrl;

	#define VRING_CFG_TO_RESOLUTION_VALUE_POS (0)
	#define VRING_CFG_TO_RESOLUTION_VALUE_LEN (6)
	#define VRING_CFG_TO_RESOLUTION_VALUE_MSK (0x3F)
	u8 to_resolution;
	u8 agg_max_wsize;
	struct wmi_vring_cfg_schd schd_params;
} __packed;

enum wmi_vring_cfg_cmd_action {
	WMI_VRING_CMD_ADD			= 0,
	WMI_VRING_CMD_MODIFY			= 1,
	WMI_VRING_CMD_DELETE			= 2,
};

struct wmi_vring_cfg_cmd {
	__le32 action;
	struct wmi_vring_cfg vring_cfg;
} __packed;

/*
 * WMI_VRING_BA_EN_CMDID
 */
struct wmi_vring_ba_en_cmd {
	u8 ringid;
	u8 agg_max_wsize;
	__le16 ba_timeout;
	u8 amsdu;
} __packed;

/*
 * WMI_VRING_BA_DIS_CMDID
 */
struct wmi_vring_ba_dis_cmd {
	u8 ringid;
	u8 reserved;
	__le16 reason;
} __packed;

/*
 * WMI_NOTIFY_REQ_CMDID
 */
struct wmi_notify_req_cmd {
	u8 cid;
	u8 year;
	u8 month;
	u8 day;
	__le32 interval_usec;
	u8 hour;
	u8 minute;
	u8 second;
	u8 miliseconds;
} __packed;

/*
 * WMI_CFG_RX_CHAIN_CMDID
 */
enum wmi_sniffer_cfg_mode {
	WMI_SNIFFER_OFF				= 0,
	WMI_SNIFFER_ON				= 1,
};

enum wmi_sniffer_cfg_phy_info_mode {
	WMI_SNIFFER_PHY_INFO_DISABLED		= 0,
	WMI_SNIFFER_PHY_INFO_ENABLED		= 1,
};

enum wmi_sniffer_cfg_phy_support {
	WMI_SNIFFER_CP				= 0,
	WMI_SNIFFER_DP				= 1,
	WMI_SNIFFER_BOTH_PHYS			= 2,
};

struct wmi_sniffer_cfg {
	__le32 mode;		/* enum wmi_sniffer_cfg_mode */
	__le32 phy_info_mode;	/* enum wmi_sniffer_cfg_phy_info_mode */
	__le32 phy_support;	/* enum wmi_sniffer_cfg_phy_support */
	u8 channel;
	u8 reserved[3];
} __packed;

enum wmi_cfg_rx_chain_cmd_action {
	WMI_RX_CHAIN_ADD			= 0,
	WMI_RX_CHAIN_DEL			= 1,
};

enum wmi_cfg_rx_chain_cmd_decap_trans_type {
	WMI_DECAP_TYPE_802_3			= 0,
	WMI_DECAP_TYPE_NATIVE_WIFI		= 1,
};

enum wmi_cfg_rx_chain_cmd_nwifi_ds_trans_type {
	WMI_NWIFI_RX_TRANS_MODE_NO		= 0,
	WMI_NWIFI_RX_TRANS_MODE_PBSS2AP		= 1,
	WMI_NWIFI_RX_TRANS_MODE_PBSS2STA	= 2,
};

enum wmi_cfg_rx_chain_cmd_reorder_type {
	WMI_RX_HW_REORDER = 0,
	WMI_RX_SW_REORDER = 1,
};

struct wmi_cfg_rx_chain_cmd {
	__le32 action;
	struct wmi_sw_ring_cfg rx_sw_ring;
	u8 mid;
	u8 decap_trans_type;

	#define L2_802_3_OFFLOAD_CTRL_VLAN_TAG_INSERTION_POS (0)
	#define L2_802_3_OFFLOAD_CTRL_VLAN_TAG_INSERTION_LEN (1)
	#define L2_802_3_OFFLOAD_CTRL_VLAN_TAG_INSERTION_MSK (0x1)
	u8 l2_802_3_offload_ctrl;

	#define L2_NWIFI_OFFLOAD_CTRL_REMOVE_QOS_POS (0)
	#define L2_NWIFI_OFFLOAD_CTRL_REMOVE_QOS_LEN (1)
	#define L2_NWIFI_OFFLOAD_CTRL_REMOVE_QOS_MSK (0x1)
	#define L2_NWIFI_OFFLOAD_CTRL_REMOVE_PN_POS (1)
	#define L2_NWIFI_OFFLOAD_CTRL_REMOVE_PN_LEN (1)
	#define L2_NWIFI_OFFLOAD_CTRL_REMOVE_PN_MSK (0x2)
	u8 l2_nwifi_offload_ctrl;

	u8 vlan_id;
	u8 nwifi_ds_trans_type;

	#define L3_L4_CTRL_IPV4_CHECKSUM_EN_POS (0)
	#define L3_L4_CTRL_IPV4_CHECKSUM_EN_LEN (1)
	#define L3_L4_CTRL_IPV4_CHECKSUM_EN_MSK (0x1)
	#define L3_L4_CTRL_TCPIP_CHECKSUM_EN_POS (1)
	#define L3_L4_CTRL_TCPIP_CHECKSUM_EN_LEN (1)
	#define L3_L4_CTRL_TCPIP_CHECKSUM_EN_MSK (0x2)
	u8 l3_l4_ctrl;

	#define RING_CTRL_OVERRIDE_PREFETCH_THRSH_POS (0)
	#define RING_CTRL_OVERRIDE_PREFETCH_THRSH_LEN (1)
	#define RING_CTRL_OVERRIDE_PREFETCH_THRSH_MSK (0x1)
	#define RING_CTRL_OVERRIDE_WB_THRSH_POS (1)
	#define RING_CTRL_OVERRIDE_WB_THRSH_LEN (1)
	#define RING_CTRL_OVERRIDE_WB_THRSH_MSK (0x2)
	#define RING_CTRL_OVERRIDE_ITR_THRSH_POS (2)
	#define RING_CTRL_OVERRIDE_ITR_THRSH_LEN (1)
	#define RING_CTRL_OVERRIDE_ITR_THRSH_MSK (0x4)
	#define RING_CTRL_OVERRIDE_HOST_THRSH_POS (3)
	#define RING_CTRL_OVERRIDE_HOST_THRSH_LEN (1)
	#define RING_CTRL_OVERRIDE_HOST_THRSH_MSK (0x8)
	u8 ring_ctrl;

	__le16 prefetch_thrsh;
	__le16 wb_thrsh;
	__le32 itr_value;
	__le16 host_thrsh;
	u8 reorder_type;
	u8 reserved;
	struct wmi_sniffer_cfg sniffer_cfg;
} __packed;

/*
 * WMI_RCP_ADDBA_RESP_CMDID
 */
struct wmi_rcp_addba_resp_cmd {
	u8 cidxtid;
	u8 dialog_token;
	__le16 status_code;
	__le16 ba_param_set;	/* ieee80211_ba_parameterset field to send */
	__le16 ba_timeout;
} __packed;

/*
 * WMI_RCP_DELBA_CMDID
 */
struct wmi_rcp_delba_cmd {
	u8 cidxtid;
	u8 reserved;
	__le16 reason;
} __packed;

/*
 * WMI_RCP_ADDBA_REQ_CMDID
 */
struct wmi_rcp_addba_req_cmd {
	u8 cidxtid;
	u8 dialog_token;
	/* ieee80211_ba_parameterset field as it received */
	__le16 ba_param_set;
	__le16 ba_timeout;
	/* ieee80211_ba_seqstrl field as it received */
	__le16 ba_seq_ctrl;
} __packed;

/*
 * WMI_SET_MAC_ADDRESS_CMDID
 */
struct wmi_set_mac_address_cmd {
	u8 mac[WMI_MAC_LEN];
	u8 reserved[2];
} __packed;

/*
* WMI_EAPOL_TX_CMDID
*/
struct wmi_eapol_tx_cmd {
	u8 dst_mac[WMI_MAC_LEN];
	__le16 eapol_len;
	u8 eapol[0];
} __packed;

/*
 * WMI_ECHO_CMDID
 *
 * Check FW is alive
 *
 * WMI_DEEP_ECHO_CMDID
 *
 * Check FW and ucode are alive
 *
 * Returned event: WMI_ECHO_RSP_EVENTID
 * same event for both commands
 */
struct wmi_echo_cmd {
	__le32 value;
} __packed;

/*
 * WMI_TEMP_SENSE_CMDID
 *
 * Measure MAC and radio temperatures
 */
struct wmi_temp_sense_cmd {
	__le32 measure_marlon_m_en;
	__le32 measure_marlon_r_en;
} __packed;

/*
 * WMI Events
 */

/*
 * List of Events (target to host)
 */
enum wmi_event_id {
	WMI_READY_EVENTID			= 0x1001,
	WMI_CONNECT_EVENTID			= 0x1002,
	WMI_DISCONNECT_EVENTID			= 0x1003,
	WMI_SCAN_COMPLETE_EVENTID		= 0x100a,
	WMI_REPORT_STATISTICS_EVENTID		= 0x100b,
	WMI_RD_MEM_RSP_EVENTID			= 0x1800,
	WMI_FW_READY_EVENTID			= 0x1801,
	WMI_EXIT_FAST_MEM_ACC_MODE_EVENTID	= 0x0200,
	WMI_ECHO_RSP_EVENTID			= 0x1803,
	WMI_FS_TUNE_DONE_EVENTID		= 0x180a,
	WMI_CORR_MEASURE_EVENTID		= 0x180b,
	WMI_READ_RSSI_EVENTID			= 0x180c,
	WMI_TEMP_SENSE_DONE_EVENTID		= 0x180e,
	WMI_DC_CALIB_DONE_EVENTID		= 0x180f,
	WMI_IQ_TX_CALIB_DONE_EVENTID		= 0x1811,
	WMI_IQ_RX_CALIB_DONE_EVENTID		= 0x1812,
	WMI_SET_WORK_MODE_DONE_EVENTID		= 0x1815,
	WMI_LO_LEAKAGE_CALIB_DONE_EVENTID	= 0x1816,
	WMI_MARLON_R_ACTIVATE_DONE_EVENTID	= 0x1817,
	WMI_MARLON_R_READ_DONE_EVENTID		= 0x1818,
	WMI_MARLON_R_WRITE_DONE_EVENTID		= 0x1819,
	WMI_MARLON_R_TXRX_SEL_DONE_EVENTID	= 0x181a,
	WMI_SILENT_RSSI_CALIB_DONE_EVENTID	= 0x181d,
	WMI_RF_RX_TEST_DONE_EVENTID		= 0x181e,
	WMI_CFG_RX_CHAIN_DONE_EVENTID		= 0x1820,
	WMI_VRING_CFG_DONE_EVENTID		= 0x1821,
	WMI_BA_STATUS_EVENTID			= 0x1823,
	WMI_RCP_ADDBA_REQ_EVENTID		= 0x1824,
	WMI_ADDBA_RESP_SENT_EVENTID		= 0x1825,
	WMI_DELBA_EVENTID			= 0x1826,
	WMI_GET_SSID_EVENTID			= 0x1828,
	WMI_GET_PCP_CHANNEL_EVENTID		= 0x182a,
	WMI_SW_TX_COMPLETE_EVENTID		= 0x182b,

	WMI_READ_MAC_RXQ_EVENTID		= 0x1830,
	WMI_READ_MAC_TXQ_EVENTID		= 0x1831,
	WMI_WRITE_MAC_RXQ_EVENTID		= 0x1832,
	WMI_WRITE_MAC_TXQ_EVENTID		= 0x1833,
	WMI_WRITE_MAC_XQ_FIELD_EVENTID		= 0x1834,

	WMI_BEAFORMING_MGMT_DONE_EVENTID	= 0x1836,
	WMI_BF_TXSS_MGMT_DONE_EVENTID		= 0x1837,
	WMI_BF_RXSS_MGMT_DONE_EVENTID		= 0x1839,
	WMI_RS_MGMT_DONE_EVENTID		= 0x1852,
	WMI_RF_MGMT_STATUS_EVENTID		= 0x1853,
	WMI_BF_SM_MGMT_DONE_EVENTID		= 0x1838,
	WMI_RX_MGMT_PACKET_EVENTID		= 0x1840,
	WMI_TX_MGMT_PACKET_EVENTID		= 0x1841,

	/* Performance monitoring events */
	WMI_DATA_PORT_OPEN_EVENTID		= 0x1860,
	WMI_WBE_LINKDOWN_EVENTID		= 0x1861,

	WMI_BF_CTRL_DONE_EVENTID		= 0x1862,
	WMI_NOTIFY_REQ_DONE_EVENTID		= 0x1863,
	WMI_GET_STATUS_DONE_EVENTID		= 0x1864,

	WMI_UNIT_TEST_EVENTID			= 0x1900,
	WMI_FLASH_READ_DONE_EVENTID		= 0x1902,
	WMI_FLASH_WRITE_DONE_EVENTID		= 0x1903,
	/*P2P*/
	WMI_PORT_ALLOCATED_EVENTID		= 0x1911,
	WMI_PORT_DELETED_EVENTID		= 0x1912,
	WMI_LISTEN_STARTED_EVENTID		= 0x1914,
	WMI_SEARCH_STARTED_EVENTID		= 0x1915,
	WMI_DISCOVERY_STARTED_EVENTID		= 0x1916,
	WMI_DISCOVERY_STOPPED_EVENTID		= 0x1917,
	WMI_PCP_STARTED_EVENTID			= 0x1918,
	WMI_PCP_STOPPED_EVENTID			= 0x1919,
	WMI_PCP_FACTOR_EVENTID			= 0x191a,
	WMI_SET_CHANNEL_EVENTID			= 0x9000,
	WMI_ASSOC_REQ_EVENTID			= 0x9001,
	WMI_EAPOL_RX_EVENTID			= 0x9002,
	WMI_MAC_ADDR_RESP_EVENTID		= 0x9003,
	WMI_FW_VER_EVENTID			= 0x9004,
};

/*
 * Events data structures
 */

enum wmi_fw_status {
	WMI_FW_STATUS_SUCCESS,
	WMI_FW_STATUS_FAILURE,
};

/*
 * WMI_RF_MGMT_STATUS_EVENTID
 */
enum wmi_rf_status {
	WMI_RF_ENABLED			= 0,
	WMI_RF_DISABLED_HW		= 1,
	WMI_RF_DISABLED_SW		= 2,
	WMI_RF_DISABLED_HW_SW		= 3,
};

struct wmi_rf_mgmt_status_event {
	__le32 rf_status;
} __packed;

/*
 * WMI_GET_STATUS_DONE_EVENTID
 */
struct wmi_get_status_done_event {
	__le32 is_associated;
	u8 cid;
	u8 reserved0[3];
	u8 bssid[WMI_MAC_LEN];
	u8 channel;
	u8 reserved1;
	u8 network_type;
	u8 reserved2[3];
	__le32 ssid_len;
	u8 ssid[WMI_MAX_SSID_LEN];
	__le32 rf_status;
	__le32 is_secured;
} __packed;

/*
 * WMI_FW_VER_EVENTID
 */
struct wmi_fw_ver_event {
	u8 major;
	u8 minor;
	__le16 subminor;
	__le16 build;
} __packed;

/*
* WMI_MAC_ADDR_RESP_EVENTID
*/
struct wmi_mac_addr_resp_event {
	u8 mac[WMI_MAC_LEN];
	u8 auth_mode;
	u8 crypt_mode;
	__le32 offload_mode;
} __packed;

/*
* WMI_EAPOL_RX_EVENTID
*/
struct wmi_eapol_rx_event {
	u8 src_mac[WMI_MAC_LEN];
	__le16 eapol_len;
	u8 eapol[0];
} __packed;

/*
* WMI_READY_EVENTID
*/
enum wmi_phy_capability {
	WMI_11A_CAPABILITY		= 1,
	WMI_11G_CAPABILITY		= 2,
	WMI_11AG_CAPABILITY		= 3,
	WMI_11NA_CAPABILITY		= 4,
	WMI_11NG_CAPABILITY		= 5,
	WMI_11NAG_CAPABILITY		= 6,
	WMI_11AD_CAPABILITY		= 7,
	WMI_11N_CAPABILITY_OFFSET = WMI_11NA_CAPABILITY - WMI_11A_CAPABILITY,
};

struct wmi_ready_event {
	__le32 sw_version;
	__le32 abi_version;
	u8 mac[WMI_MAC_LEN];
	u8 phy_capability;		/* enum wmi_phy_capability */
	u8 numof_additional_mids;
} __packed;

/*
 * WMI_NOTIFY_REQ_DONE_EVENTID
 */
struct wmi_notify_req_done_event {
	__le32 status; /* beamforming status, 0: fail; 1: OK; 2: retrying */
	__le64 tsf;
	__le32 snr_val;
	__le32 tx_tpt;
	__le32 tx_goodput;
	__le32 rx_goodput;
	__le16 bf_mcs;
	__le16 my_rx_sector;
	__le16 my_tx_sector;
	__le16 other_rx_sector;
	__le16 other_tx_sector;
	__le16 range;
	u8 sqi;
	u8 reserved[3];
} __packed;

/*
 * WMI_CONNECT_EVENTID
 */
struct wmi_connect_event {
	u8 channel;
	u8 reserved0;
	u8 bssid[WMI_MAC_LEN];
	__le16 listen_interval;
	__le16 beacon_interval;
	u8 network_type;
	u8 reserved1[3];
	u8 beacon_ie_len;
	u8 assoc_req_len;
	u8 assoc_resp_len;
	u8 cid;
	u8 reserved2[3];
	u8 assoc_info[0];
} __packed;

/*
 * WMI_DISCONNECT_EVENTID
 */
enum wmi_disconnect_reason {
	WMI_DIS_REASON_NO_NETWORK_AVAIL		= 1,
	WMI_DIS_REASON_LOST_LINK		= 2, /* bmiss */
	WMI_DIS_REASON_DISCONNECT_CMD		= 3,
	WMI_DIS_REASON_BSS_DISCONNECTED		= 4,
	WMI_DIS_REASON_AUTH_FAILED		= 5,
	WMI_DIS_REASON_ASSOC_FAILED		= 6,
	WMI_DIS_REASON_NO_RESOURCES_AVAIL	= 7,
	WMI_DIS_REASON_CSERV_DISCONNECT		= 8,
	WMI_DIS_REASON_INVALID_PROFILE		= 10,
	WMI_DIS_REASON_DOT11H_CHANNEL_SWITCH	= 11,
	WMI_DIS_REASON_PROFILE_MISMATCH		= 12,
	WMI_DIS_REASON_CONNECTION_EVICTED	= 13,
	WMI_DIS_REASON_IBSS_MERGE		= 14,
};

struct wmi_disconnect_event {
	__le16 protocol_reason_status;	/* reason code, see 802.11 spec. */
	u8 bssid[WMI_MAC_LEN];		/* set if known */
	u8 disconnect_reason;		/* see wmi_disconnect_reason */
	u8 assoc_resp_len;	/* not used */
	u8 assoc_info[0];	/* not used */
} __packed;

/*
 * WMI_SCAN_COMPLETE_EVENTID
 */
enum scan_status {
	WMI_SCAN_SUCCESS	= 0,
	WMI_SCAN_FAILED		= 1,
	WMI_SCAN_ABORTED	= 2,
	WMI_SCAN_REJECTED	= 3,
};

struct wmi_scan_complete_event {
	__le32 status;	/* scan_status */
} __packed;

/*
 * WMI_BA_STATUS_EVENTID
 */
enum wmi_vring_ba_status {
	WMI_BA_AGREED			= 0,
	WMI_BA_NON_AGREED		= 1,
	/* BA_EN in middle of teardown flow */
	WMI_BA_TD_WIP			= 2,
	/* BA_DIS or BA_EN in middle of BA SETUP flow */
	WMI_BA_SETUP_WIP		= 3,
	/* BA_EN when the BA session is already active */
	WMI_BA_SESSION_ACTIVE		= 4,
	/* BA_DIS when the BA session is not active */
	WMI_BA_SESSION_NOT_ACTIVE	= 5,
};

struct wmi_vring_ba_status_event {
	__le16 status; /* enum wmi_vring_ba_status */
	u8 reserved[2];
	u8 ringid;
	u8 agg_wsize;
	__le16 ba_timeout;
	u8 amsdu;
} __packed;

/*
 * WMI_DELBA_EVENTID
 */
struct wmi_delba_event {
	u8 cidxtid;
	u8 from_initiator;
	__le16 reason;
} __packed;

/*
 * WMI_VRING_CFG_DONE_EVENTID
 */
struct wmi_vring_cfg_done_event {
	u8 ringid;
	u8 status;
	u8 reserved[2];
	__le32 tx_vring_tail_ptr;
} __packed;

/*
 * WMI_ADDBA_RESP_SENT_EVENTID
 */
struct wmi_rcp_addba_resp_sent_event {
	u8 cidxtid;
	u8 reserved;
	__le16 status;
} __packed;

/*
 * WMI_RCP_ADDBA_REQ_EVENTID
 */
struct wmi_rcp_addba_req_event {
	u8 cidxtid;
	u8 dialog_token;
	__le16 ba_param_set;	/* ieee80211_ba_parameterset as it received */
	__le16 ba_timeout;
	__le16 ba_seq_ctrl;	/* ieee80211_ba_seqstrl field as it received */
} __packed;

/*
 * WMI_CFG_RX_CHAIN_DONE_EVENTID
 */
enum wmi_cfg_rx_chain_done_event_status {
	WMI_CFG_RX_CHAIN_SUCCESS	= 1,
};

struct wmi_cfg_rx_chain_done_event {
	__le32 rx_ring_tail_ptr;	/* Rx V-Ring Tail pointer */
	__le32 status;
} __packed;

/*
 * WMI_WBE_LINKDOWN_EVENTID
 */
enum wmi_wbe_link_down_event_reason {
	WMI_WBE_REASON_USER_REQUEST	= 0,
	WMI_WBE_REASON_RX_DISASSOC	= 1,
	WMI_WBE_REASON_BAD_PHY_LINK	= 2,
};

struct wmi_wbe_link_down_event {
	u8 cid;
	u8 reserved[3];
	__le32 reason;
} __packed;

/*
 * WMI_DATA_PORT_OPEN_EVENTID
 */
struct wmi_data_port_open_event {
	u8 cid;
	u8 reserved[3];
} __packed;

/*
 * WMI_GET_PCP_CHANNEL_EVENTID
 */
struct wmi_get_pcp_channel_event {
	u8 channel;
	u8 reserved[3];
} __packed;

/*
* WMI_PORT_ALLOCATED_EVENTID
*/
struct wmi_port_allocated_event {
	u8 status;	/* wmi_fw_status */
	u8 reserved[3];
} __packed;

/*
* WMI_PORT_DELETED_EVENTID
*/
struct wmi_port_deleted_event {
	u8 status;	/* wmi_fw_status */
	u8 reserved[3];
} __packed;

/*
 * WMI_LISTEN_STARTED_EVENTID
 */
struct wmi_listen_started_event {
	u8 status;	/* wmi_fw_status */
	u8 reserved[3];
} __packed;

/*
 * WMI_SEARCH_STARTED_EVENTID
 */
struct wmi_search_started_event {
	u8 status;	/* wmi_fw_status */
	u8 reserved[3];
} __packed;

/*
 * WMI_PCP_STARTED_EVENTID
 */
struct wmi_pcp_started_event {
	u8 status;	/* wmi_fw_status */
	u8 reserved[3];
} __packed;

/*
 * WMI_PCP_FACTOR_EVENTID
 */
struct wmi_pcp_factor_event {
	__le32 pcp_factor;
} __packed;

/*
 * WMI_SW_TX_COMPLETE_EVENTID
 */
enum wmi_sw_tx_status {
	WMI_TX_SW_STATUS_SUCCESS		= 0,
	WMI_TX_SW_STATUS_FAILED_NO_RESOURCES	= 1,
	WMI_TX_SW_STATUS_FAILED_TX		= 2,
};

struct wmi_sw_tx_complete_event {
	u8 status;	/* enum wmi_sw_tx_status */
	u8 reserved[3];
} __packed;

/*
 * WMI_CORR_MEASURE_EVENTID
 */
struct wmi_corr_measure_event {
	s32 i;
	s32 q;
	s32 image_i;
	s32 image_q;
} __packed;

/*
 * WMI_READ_RSSI_EVENTID
 */
struct wmi_read_rssi_event {
	__le32 ina_rssi_adc_dbm;
} __packed;

/*
 * WMI_GET_SSID_EVENTID
 */
struct wmi_get_ssid_event {
	__le32 ssid_len;
	u8 ssid[WMI_MAX_SSID_LEN];
} __packed;

/*
 * WMI_RX_MGMT_PACKET_EVENTID
 */
struct wmi_rx_mgmt_info {
	u8 mcs;
	s8 snr;
	u8 range;
	u8 sqi;
	__le16 stype;
	__le16 status;
	__le32 len;
	u8 qid;
	u8 mid;
	u8 cid;
	u8 channel;	/* From Radio MNGR */
} __packed;

/*
 * WMI_TX_MGMT_PACKET_EVENTID
 */
struct wmi_tx_mgmt_packet_event {
	u8 payload[0];
} __packed;

struct wmi_rx_mgmt_packet_event {
	struct wmi_rx_mgmt_info info;
	u8 payload[0];
} __packed;

/*
 * WMI_ECHO_RSP_EVENTID
 */
struct wmi_echo_event {
	__le32 echoed_value;
} __packed;

/*
 * WMI_TEMP_SENSE_DONE_EVENTID
 *
 * Measure MAC and radio temperatures
 */
struct wmi_temp_sense_done_event {
	__le32 marlon_m_t1000;
	__le32 marlon_r_t1000;
} __packed;

#endif /* __WILOCITY_WMI_H__ */
