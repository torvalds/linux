/*
 * Copyright (c) 2012-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2006-2012 Wilocity
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
 * Wireless Module Interface (WMI) for the Qualcomm
 * 60 GHz wireless solution.
 * It includes definitions of all the commands and events.
 * Commands are messages from the host to the WM.
 * Events are messages from the WM to the host.
 *
 * This is an automatically generated file.
 */

#ifndef __WILOCITY_WMI_H__
#define __WILOCITY_WMI_H__

/* General */
#define WMI_MAX_ASSOC_STA		(8)
#define WMI_DEFAULT_ASSOC_STA		(1)
#define WMI_MAC_LEN			(6)
#define WMI_PROX_RANGE_NUM		(3)
#define WMI_MAX_LOSS_DMG_BEACONS	(20)
#define MAX_NUM_OF_SECTORS		(128)

/* Mailbox interface
 * used for commands and events
 */
enum wmi_mid {
	MID_DEFAULT		= 0x00,
	FIRST_DBG_MID_ID	= 0x10,
	LAST_DBG_MID_ID		= 0xFE,
	MID_BROADCAST		= 0xFF,
};

/* FW capability IDs
 * Each ID maps to a bit in a 32-bit bitmask value provided by the FW to
 * the host
 */
enum wmi_fw_capability {
	WMI_FW_CAPABILITY_FTM			= 0,
	WMI_FW_CAPABILITY_PS_CONFIG		= 1,
	WMI_FW_CAPABILITY_RF_SECTORS		= 2,
	WMI_FW_CAPABILITY_MGMT_RETRY_LIMIT	= 3,
	WMI_FW_CAPABILITY_DISABLE_AP_SME	= 4,
	WMI_FW_CAPABILITY_WMI_ONLY		= 5,
	WMI_FW_CAPABILITY_THERMAL_THROTTLING	= 7,
	WMI_FW_CAPABILITY_D3_SUSPEND		= 8,
	WMI_FW_CAPABILITY_MAX,
};

/* WMI_CMD_HDR */
struct wmi_cmd_hdr {
	u8 mid;
	u8 reserved;
	__le16 command_id;
	__le32 fw_timestamp;
} __packed;

/* List of Commands */
enum wmi_command_id {
	WMI_CONNECT_CMDID				= 0x01,
	WMI_DISCONNECT_CMDID				= 0x03,
	WMI_DISCONNECT_STA_CMDID			= 0x04,
	WMI_START_SCAN_CMDID				= 0x07,
	WMI_SET_BSS_FILTER_CMDID			= 0x09,
	WMI_SET_PROBED_SSID_CMDID			= 0x0A,
	WMI_SET_LISTEN_INT_CMDID			= 0x0B,
	WMI_BCON_CTRL_CMDID				= 0x0F,
	WMI_ADD_CIPHER_KEY_CMDID			= 0x16,
	WMI_DELETE_CIPHER_KEY_CMDID			= 0x17,
	WMI_PCP_CONF_CMDID				= 0x18,
	WMI_SET_APPIE_CMDID				= 0x3F,
	WMI_SET_WSC_STATUS_CMDID			= 0x41,
	WMI_PXMT_RANGE_CFG_CMDID			= 0x42,
	WMI_PXMT_SNR2_RANGE_CFG_CMDID			= 0x43,
	WMI_MEM_READ_CMDID				= 0x800,
	WMI_MEM_WR_CMDID				= 0x801,
	WMI_ECHO_CMDID					= 0x803,
	WMI_DEEP_ECHO_CMDID				= 0x804,
	WMI_CONFIG_MAC_CMDID				= 0x805,
	WMI_CONFIG_PHY_DEBUG_CMDID			= 0x806,
	WMI_ADD_DEBUG_TX_PCKT_CMDID			= 0x808,
	WMI_PHY_GET_STATISTICS_CMDID			= 0x809,
	WMI_FS_TUNE_CMDID				= 0x80A,
	WMI_CORR_MEASURE_CMDID				= 0x80B,
	WMI_READ_RSSI_CMDID				= 0x80C,
	WMI_TEMP_SENSE_CMDID				= 0x80E,
	WMI_DC_CALIB_CMDID				= 0x80F,
	WMI_SEND_TONE_CMDID				= 0x810,
	WMI_IQ_TX_CALIB_CMDID				= 0x811,
	WMI_IQ_RX_CALIB_CMDID				= 0x812,
	WMI_SET_UCODE_IDLE_CMDID			= 0x813,
	WMI_SET_WORK_MODE_CMDID				= 0x815,
	WMI_LO_LEAKAGE_CALIB_CMDID			= 0x816,
	WMI_MARLON_R_READ_CMDID				= 0x818,
	WMI_MARLON_R_WRITE_CMDID			= 0x819,
	WMI_MARLON_R_TXRX_SEL_CMDID			= 0x81A,
	MAC_IO_STATIC_PARAMS_CMDID			= 0x81B,
	MAC_IO_DYNAMIC_PARAMS_CMDID			= 0x81C,
	WMI_SILENT_RSSI_CALIB_CMDID			= 0x81D,
	WMI_RF_RX_TEST_CMDID				= 0x81E,
	WMI_CFG_RX_CHAIN_CMDID				= 0x820,
	WMI_VRING_CFG_CMDID				= 0x821,
	WMI_BCAST_VRING_CFG_CMDID			= 0x822,
	WMI_VRING_BA_EN_CMDID				= 0x823,
	WMI_VRING_BA_DIS_CMDID				= 0x824,
	WMI_RCP_ADDBA_RESP_CMDID			= 0x825,
	WMI_RCP_DELBA_CMDID				= 0x826,
	WMI_SET_SSID_CMDID				= 0x827,
	WMI_GET_SSID_CMDID				= 0x828,
	WMI_SET_PCP_CHANNEL_CMDID			= 0x829,
	WMI_GET_PCP_CHANNEL_CMDID			= 0x82A,
	WMI_SW_TX_REQ_CMDID				= 0x82B,
	WMI_READ_MAC_RXQ_CMDID				= 0x830,
	WMI_READ_MAC_TXQ_CMDID				= 0x831,
	WMI_WRITE_MAC_RXQ_CMDID				= 0x832,
	WMI_WRITE_MAC_TXQ_CMDID				= 0x833,
	WMI_WRITE_MAC_XQ_FIELD_CMDID			= 0x834,
	WMI_MLME_PUSH_CMDID				= 0x835,
	WMI_BEAMFORMING_MGMT_CMDID			= 0x836,
	WMI_BF_TXSS_MGMT_CMDID				= 0x837,
	WMI_BF_SM_MGMT_CMDID				= 0x838,
	WMI_BF_RXSS_MGMT_CMDID				= 0x839,
	WMI_BF_TRIG_CMDID				= 0x83A,
	WMI_LINK_MAINTAIN_CFG_WRITE_CMDID		= 0x842,
	WMI_LINK_MAINTAIN_CFG_READ_CMDID		= 0x843,
	WMI_SET_SECTORS_CMDID				= 0x849,
	WMI_MAINTAIN_PAUSE_CMDID			= 0x850,
	WMI_MAINTAIN_RESUME_CMDID			= 0x851,
	WMI_RS_MGMT_CMDID				= 0x852,
	WMI_RF_MGMT_CMDID				= 0x853,
	WMI_OTP_READ_CMDID				= 0x856,
	WMI_OTP_WRITE_CMDID				= 0x857,
	WMI_LED_CFG_CMDID				= 0x858,
	/* Performance monitoring commands */
	WMI_BF_CTRL_CMDID				= 0x862,
	WMI_NOTIFY_REQ_CMDID				= 0x863,
	WMI_GET_STATUS_CMDID				= 0x864,
	WMI_GET_RF_STATUS_CMDID				= 0x866,
	WMI_GET_BASEBAND_TYPE_CMDID			= 0x867,
	WMI_UNIT_TEST_CMDID				= 0x900,
	WMI_HICCUP_CMDID				= 0x901,
	WMI_FLASH_READ_CMDID				= 0x902,
	WMI_FLASH_WRITE_CMDID				= 0x903,
	/* Power management */
	WMI_TRAFFIC_SUSPEND_CMDID			= 0x904,
	WMI_TRAFFIC_RESUME_CMDID			= 0x905,
	/* P2P */
	WMI_P2P_CFG_CMDID				= 0x910,
	WMI_PORT_ALLOCATE_CMDID				= 0x911,
	WMI_PORT_DELETE_CMDID				= 0x912,
	WMI_POWER_MGMT_CFG_CMDID			= 0x913,
	WMI_START_LISTEN_CMDID				= 0x914,
	WMI_START_SEARCH_CMDID				= 0x915,
	WMI_DISCOVERY_START_CMDID			= 0x916,
	WMI_DISCOVERY_STOP_CMDID			= 0x917,
	WMI_PCP_START_CMDID				= 0x918,
	WMI_PCP_STOP_CMDID				= 0x919,
	WMI_GET_PCP_FACTOR_CMDID			= 0x91B,
	/* Power Save Configuration Commands */
	WMI_PS_DEV_PROFILE_CFG_CMDID			= 0x91C,
	/* Not supported yet */
	WMI_PS_DEV_CFG_CMDID				= 0x91D,
	/* Not supported yet */
	WMI_PS_DEV_CFG_READ_CMDID			= 0x91E,
	/* Per MAC Power Save Configuration commands
	 * Not supported yet
	 */
	WMI_PS_MID_CFG_CMDID				= 0x91F,
	/* Not supported yet */
	WMI_PS_MID_CFG_READ_CMDID			= 0x920,
	WMI_RS_CFG_CMDID				= 0x921,
	WMI_GET_DETAILED_RS_RES_CMDID			= 0x922,
	WMI_AOA_MEAS_CMDID				= 0x923,
	WMI_BRP_SET_ANT_LIMIT_CMDID			= 0x924,
	WMI_SET_MGMT_RETRY_LIMIT_CMDID			= 0x930,
	WMI_GET_MGMT_RETRY_LIMIT_CMDID			= 0x931,
	WMI_NEW_STA_CMDID				= 0x935,
	WMI_DEL_STA_CMDID				= 0x936,
	WMI_SET_THERMAL_THROTTLING_CFG_CMDID		= 0x940,
	WMI_GET_THERMAL_THROTTLING_CFG_CMDID		= 0x941,
	WMI_TOF_SESSION_START_CMDID			= 0x991,
	WMI_TOF_GET_CAPABILITIES_CMDID			= 0x992,
	WMI_TOF_SET_LCR_CMDID				= 0x993,
	WMI_TOF_SET_LCI_CMDID				= 0x994,
	WMI_TOF_CHANNEL_INFO_CMDID			= 0x995,
	WMI_TOF_SET_TX_RX_OFFSET_CMDID			= 0x997,
	WMI_TOF_GET_TX_RX_OFFSET_CMDID			= 0x998,
	WMI_GET_RF_SECTOR_PARAMS_CMDID			= 0x9A0,
	WMI_SET_RF_SECTOR_PARAMS_CMDID			= 0x9A1,
	WMI_GET_SELECTED_RF_SECTOR_INDEX_CMDID		= 0x9A2,
	WMI_SET_SELECTED_RF_SECTOR_INDEX_CMDID		= 0x9A3,
	WMI_SET_RF_SECTOR_ON_CMDID			= 0x9A4,
	WMI_PRIO_TX_SECTORS_ORDER_CMDID			= 0x9A5,
	WMI_PRIO_TX_SECTORS_NUMBER_CMDID		= 0x9A6,
	WMI_PRIO_TX_SECTORS_SET_DEFAULT_CFG_CMDID	= 0x9A7,
	WMI_SET_MAC_ADDRESS_CMDID			= 0xF003,
	WMI_ABORT_SCAN_CMDID				= 0xF007,
	WMI_SET_PROMISCUOUS_MODE_CMDID			= 0xF041,
	WMI_GET_PMK_CMDID				= 0xF048,
	WMI_SET_PASSPHRASE_CMDID			= 0xF049,
	WMI_SEND_ASSOC_RES_CMDID			= 0xF04A,
	WMI_SET_ASSOC_REQ_RELAY_CMDID			= 0xF04B,
	WMI_MAC_ADDR_REQ_CMDID				= 0xF04D,
	WMI_FW_VER_CMDID				= 0xF04E,
	WMI_PMC_CMDID					= 0xF04F,
};

/* WMI_CONNECT_CMDID */
enum wmi_network_type {
	WMI_NETTYPE_INFRA		= 0x01,
	WMI_NETTYPE_ADHOC		= 0x02,
	WMI_NETTYPE_ADHOC_CREATOR	= 0x04,
	WMI_NETTYPE_AP			= 0x10,
	WMI_NETTYPE_P2P			= 0x20,
	/* PCIE over 60g */
	WMI_NETTYPE_WBE			= 0x40,
};

enum wmi_dot11_auth_mode {
	WMI_AUTH11_OPEN		= 0x01,
	WMI_AUTH11_SHARED	= 0x02,
	WMI_AUTH11_LEAP		= 0x04,
	WMI_AUTH11_WSC		= 0x08,
};

enum wmi_auth_mode {
	WMI_AUTH_NONE		= 0x01,
	WMI_AUTH_WPA		= 0x02,
	WMI_AUTH_WPA2		= 0x04,
	WMI_AUTH_WPA_PSK	= 0x08,
	WMI_AUTH_WPA2_PSK	= 0x10,
	WMI_AUTH_WPA_CCKM	= 0x20,
	WMI_AUTH_WPA2_CCKM	= 0x40,
};

enum wmi_crypto_type {
	WMI_CRYPT_NONE		= 0x01,
	WMI_CRYPT_AES_GCMP	= 0x20,
};

enum wmi_connect_ctrl_flag_bits {
	WMI_CONNECT_ASSOC_POLICY_USER		= 0x01,
	WMI_CONNECT_SEND_REASSOC		= 0x02,
	WMI_CONNECT_IGNORE_WPA_GROUP_CIPHER	= 0x04,
	WMI_CONNECT_PROFILE_MATCH_DONE		= 0x08,
	WMI_CONNECT_IGNORE_AAC_BEACON		= 0x10,
	WMI_CONNECT_CSA_FOLLOW_BSS		= 0x20,
	WMI_CONNECT_DO_WPA_OFFLOAD		= 0x40,
	WMI_CONNECT_DO_NOT_DEAUTH		= 0x80,
};

#define WMI_MAX_SSID_LEN	(32)

/* WMI_CONNECT_CMDID */
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

/* WMI_DISCONNECT_STA_CMDID */
struct wmi_disconnect_sta_cmd {
	u8 dst_mac[WMI_MAC_LEN];
	__le16 disconnect_reason;
} __packed;

#define WMI_MAX_KEY_INDEX	(3)
#define WMI_MAX_KEY_LEN		(32)
#define WMI_PASSPHRASE_LEN	(64)

/* WMI_SET_PASSPHRASE_CMDID */
struct wmi_set_passphrase_cmd {
	u8 ssid[WMI_MAX_SSID_LEN];
	u8 passphrase[WMI_PASSPHRASE_LEN];
	u8 ssid_len;
	u8 passphrase_len;
} __packed;

/* WMI_ADD_CIPHER_KEY_CMDID */
enum wmi_key_usage {
	WMI_KEY_USE_PAIRWISE	= 0x00,
	WMI_KEY_USE_RX_GROUP	= 0x01,
	WMI_KEY_USE_TX_GROUP	= 0x02,
};

struct wmi_add_cipher_key_cmd {
	u8 key_index;
	u8 key_type;
	/* enum wmi_key_usage */
	u8 key_usage;
	u8 key_len;
	/* key replay sequence counter */
	u8 key_rsc[8];
	u8 key[WMI_MAX_KEY_LEN];
	/* Additional Key Control information */
	u8 key_op_ctrl;
	u8 mac[WMI_MAC_LEN];
} __packed;

/* WMI_DELETE_CIPHER_KEY_CMDID */
struct wmi_delete_cipher_key_cmd {
	u8 key_index;
	u8 mac[WMI_MAC_LEN];
} __packed;

/* WMI_START_SCAN_CMDID
 *
 * Start L1 scan operation
 *
 * Returned events:
 * - WMI_RX_MGMT_PACKET_EVENTID - for every probe resp.
 * - WMI_SCAN_COMPLETE_EVENTID
 */
enum wmi_scan_type {
	WMI_ACTIVE_SCAN		= 0x00,
	WMI_SHORT_SCAN		= 0x01,
	WMI_PASSIVE_SCAN	= 0x02,
	WMI_DIRECT_SCAN		= 0x03,
	WMI_LONG_SCAN		= 0x04,
};

/* WMI_START_SCAN_CMDID */
struct wmi_start_scan_cmd {
	u8 direct_scan_mac_addr[WMI_MAC_LEN];
	/* run scan with discovery beacon. Relevant for ACTIVE scan only. */
	u8 discovery_mode;
	u8 reserved;
	/* Max duration in the home channel(ms) */
	__le32 dwell_time;
	/* Time interval between scans (ms) */
	__le32 force_scan_interval;
	/* enum wmi_scan_type */
	u8 scan_type;
	/* how many channels follow */
	u8 num_channels;
	/* channels ID's:
	 * 0 - 58320 MHz
	 * 1 - 60480 MHz
	 * 2 - 62640 MHz
	 */
	struct {
		u8 channel;
		u8 reserved;
	} channel_list[0];
} __packed;

/* WMI_SET_PROBED_SSID_CMDID */
#define MAX_PROBED_SSID_INDEX	(3)

enum wmi_ssid_flag {
	/* disables entry */
	WMI_SSID_FLAG_DISABLE	= 0x00,
	/* probes specified ssid */
	WMI_SSID_FLAG_SPECIFIC	= 0x01,
	/* probes for any ssid */
	WMI_SSID_FLAG_ANY	= 0x02,
};

struct wmi_probed_ssid_cmd {
	/* 0 to MAX_PROBED_SSID_INDEX */
	u8 entry_index;
	/* enum wmi_ssid_flag */
	u8 flag;
	u8 ssid_len;
	u8 ssid[WMI_MAX_SSID_LEN];
} __packed;

/* WMI_SET_APPIE_CMDID
 * Add Application specified IE to a management frame
 */
#define WMI_MAX_IE_LEN	(1024)

/* Frame Types */
enum wmi_mgmt_frame_type {
	WMI_FRAME_BEACON	= 0x00,
	WMI_FRAME_PROBE_REQ	= 0x01,
	WMI_FRAME_PROBE_RESP	= 0x02,
	WMI_FRAME_ASSOC_REQ	= 0x03,
	WMI_FRAME_ASSOC_RESP	= 0x04,
	WMI_NUM_MGMT_FRAME	= 0x05,
};

struct wmi_set_appie_cmd {
	/* enum wmi_mgmt_frame_type */
	u8 mgmt_frm_type;
	u8 reserved;
	/* Length of the IE to be added to MGMT frame */
	__le16 ie_len;
	u8 ie_info[0];
} __packed;

/* WMI_PXMT_RANGE_CFG_CMDID */
struct wmi_pxmt_range_cfg_cmd {
	u8 dst_mac[WMI_MAC_LEN];
	__le16 range;
} __packed;

/* WMI_PXMT_SNR2_RANGE_CFG_CMDID */
struct wmi_pxmt_snr2_range_cfg_cmd {
	s8 snr2range_arr[2];
} __packed;

/* WMI_RF_MGMT_CMDID */
enum wmi_rf_mgmt_type {
	WMI_RF_MGMT_W_DISABLE	= 0x00,
	WMI_RF_MGMT_W_ENABLE	= 0x01,
	WMI_RF_MGMT_GET_STATUS	= 0x02,
};

/* WMI_RF_MGMT_CMDID */
struct wmi_rf_mgmt_cmd {
	__le32 rf_mgmt_type;
} __packed;

/* WMI_RF_RX_TEST_CMDID */
struct wmi_rf_rx_test_cmd {
	__le32 sector;
} __packed;

/* WMI_CORR_MEASURE_CMDID */
struct wmi_corr_measure_cmd {
	__le32 freq_mhz;
	__le32 length_samples;
	__le32 iterations;
} __packed;

/* WMI_SET_SSID_CMDID */
struct wmi_set_ssid_cmd {
	__le32 ssid_len;
	u8 ssid[WMI_MAX_SSID_LEN];
} __packed;

/* WMI_SET_PCP_CHANNEL_CMDID */
struct wmi_set_pcp_channel_cmd {
	u8 channel;
	u8 reserved[3];
} __packed;

/* WMI_BCON_CTRL_CMDID */
struct wmi_bcon_ctrl_cmd {
	__le16 bcon_interval;
	__le16 frag_num;
	__le64 ss_mask;
	u8 network_type;
	u8 pcp_max_assoc_sta;
	u8 disable_sec_offload;
	u8 disable_sec;
	u8 hidden_ssid;
	u8 is_go;
	u8 reserved[2];
} __packed;

/* WMI_PORT_ALLOCATE_CMDID */
enum wmi_port_role {
	WMI_PORT_STA		= 0x00,
	WMI_PORT_PCP		= 0x01,
	WMI_PORT_AP		= 0x02,
	WMI_PORT_P2P_DEV	= 0x03,
	WMI_PORT_P2P_CLIENT	= 0x04,
	WMI_PORT_P2P_GO		= 0x05,
};

/* WMI_PORT_ALLOCATE_CMDID */
struct wmi_port_allocate_cmd {
	u8 mac[WMI_MAC_LEN];
	u8 port_role;
	u8 mid;
} __packed;

/* WMI_PORT_DELETE_CMDID */
struct wmi_port_delete_cmd {
	u8 mid;
	u8 reserved[3];
} __packed;

/* WMI_TRAFFIC_SUSPEND_CMD wakeup trigger bit mask values */
enum wmi_wakeup_trigger {
	WMI_WAKEUP_TRIGGER_UCAST	= 0x01,
	WMI_WAKEUP_TRIGGER_BCAST	= 0x02,
};

/* WMI_TRAFFIC_SUSPEND_CMDID */
struct wmi_traffic_suspend_cmd {
	/* Bit vector: bit[0] - wake on Unicast, bit[1] - wake on Broadcast */
	u8 wakeup_trigger;
} __packed;

/* WMI_P2P_CFG_CMDID */
enum wmi_discovery_mode {
	WMI_DISCOVERY_MODE_NON_OFFLOAD	= 0x00,
	WMI_DISCOVERY_MODE_OFFLOAD	= 0x01,
	WMI_DISCOVERY_MODE_PEER2PEER	= 0x02,
};

struct wmi_p2p_cfg_cmd {
	/* enum wmi_discovery_mode */
	u8 discovery_mode;
	u8 channel;
	/* base to listen/search duration calculation */
	__le16 bcon_interval;
} __packed;

/* WMI_POWER_MGMT_CFG_CMDID */
enum wmi_power_source_type {
	WMI_POWER_SOURCE_BATTERY	= 0x00,
	WMI_POWER_SOURCE_OTHER		= 0x01,
};

struct wmi_power_mgmt_cfg_cmd {
	/* enum wmi_power_source_type */
	u8 power_source;
	u8 reserved[3];
} __packed;

/* WMI_PCP_START_CMDID */
struct wmi_pcp_start_cmd {
	__le16 bcon_interval;
	u8 pcp_max_assoc_sta;
	u8 hidden_ssid;
	u8 is_go;
	u8 reserved0[5];
	/* A-BFT length override if non-0 */
	u8 abft_len;
	u8 disable_ap_sme;
	u8 network_type;
	u8 channel;
	u8 disable_sec_offload;
	u8 disable_sec;
} __packed;

/* WMI_SW_TX_REQ_CMDID */
struct wmi_sw_tx_req_cmd {
	u8 dst_mac[WMI_MAC_LEN];
	__le16 len;
	u8 payload[0];
} __packed;

struct wmi_sw_ring_cfg {
	__le64 ring_mem_base;
	__le16 ring_size;
	__le16 max_mpdu_size;
} __packed;

/* wmi_vring_cfg_schd */
struct wmi_vring_cfg_schd {
	__le16 priority;
	__le16 timeslot_us;
} __packed;

enum wmi_vring_cfg_encap_trans_type {
	WMI_VRING_ENC_TYPE_802_3	= 0x00,
	WMI_VRING_ENC_TYPE_NATIVE_WIFI	= 0x01,
};

enum wmi_vring_cfg_ds_cfg {
	WMI_VRING_DS_PBSS	= 0x00,
	WMI_VRING_DS_STATION	= 0x01,
	WMI_VRING_DS_AP		= 0x02,
	WMI_VRING_DS_ADDR4	= 0x03,
};

enum wmi_vring_cfg_nwifi_ds_trans_type {
	WMI_NWIFI_TX_TRANS_MODE_NO		= 0x00,
	WMI_NWIFI_TX_TRANS_MODE_AP2PBSS		= 0x01,
	WMI_NWIFI_TX_TRANS_MODE_STA2PBSS	= 0x02,
};

enum wmi_vring_cfg_schd_params_priority {
	WMI_SCH_PRIO_REGULAR	= 0x00,
	WMI_SCH_PRIO_HIGH	= 0x01,
};

#define CIDXTID_CID_POS				(0)
#define CIDXTID_CID_LEN				(4)
#define CIDXTID_CID_MSK				(0xF)
#define CIDXTID_TID_POS				(4)
#define CIDXTID_TID_LEN				(4)
#define CIDXTID_TID_MSK				(0xF0)
#define VRING_CFG_MAC_CTRL_LIFETIME_EN_POS	(0)
#define VRING_CFG_MAC_CTRL_LIFETIME_EN_LEN	(1)
#define VRING_CFG_MAC_CTRL_LIFETIME_EN_MSK	(0x1)
#define VRING_CFG_MAC_CTRL_AGGR_EN_POS		(1)
#define VRING_CFG_MAC_CTRL_AGGR_EN_LEN		(1)
#define VRING_CFG_MAC_CTRL_AGGR_EN_MSK		(0x2)
#define VRING_CFG_TO_RESOLUTION_VALUE_POS	(0)
#define VRING_CFG_TO_RESOLUTION_VALUE_LEN	(6)
#define VRING_CFG_TO_RESOLUTION_VALUE_MSK	(0x3F)

struct wmi_vring_cfg {
	struct wmi_sw_ring_cfg tx_sw_ring;
	/* 0-23 vrings */
	u8 ringid;
	u8 cidxtid;
	u8 encap_trans_type;
	/* 802.3 DS cfg */
	u8 ds_cfg;
	u8 nwifi_ds_trans_type;
	u8 mac_ctrl;
	u8 to_resolution;
	u8 agg_max_wsize;
	struct wmi_vring_cfg_schd schd_params;
} __packed;

enum wmi_vring_cfg_cmd_action {
	WMI_VRING_CMD_ADD	= 0x00,
	WMI_VRING_CMD_MODIFY	= 0x01,
	WMI_VRING_CMD_DELETE	= 0x02,
};

/* WMI_VRING_CFG_CMDID */
struct wmi_vring_cfg_cmd {
	__le32 action;
	struct wmi_vring_cfg vring_cfg;
} __packed;

struct wmi_bcast_vring_cfg {
	struct wmi_sw_ring_cfg tx_sw_ring;
	/* 0-23 vrings */
	u8 ringid;
	u8 encap_trans_type;
	/* 802.3 DS cfg */
	u8 ds_cfg;
	u8 nwifi_ds_trans_type;
} __packed;

/* WMI_BCAST_VRING_CFG_CMDID */
struct wmi_bcast_vring_cfg_cmd {
	__le32 action;
	struct wmi_bcast_vring_cfg vring_cfg;
} __packed;

/* WMI_VRING_BA_EN_CMDID */
struct wmi_vring_ba_en_cmd {
	u8 ringid;
	u8 agg_max_wsize;
	__le16 ba_timeout;
	u8 amsdu;
	u8 reserved[3];
} __packed;

/* WMI_VRING_BA_DIS_CMDID */
struct wmi_vring_ba_dis_cmd {
	u8 ringid;
	u8 reserved;
	__le16 reason;
} __packed;

/* WMI_NOTIFY_REQ_CMDID */
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

/* WMI_CFG_RX_CHAIN_CMDID */
enum wmi_sniffer_cfg_mode {
	WMI_SNIFFER_OFF	= 0x00,
	WMI_SNIFFER_ON	= 0x01,
};

enum wmi_sniffer_cfg_phy_info_mode {
	WMI_SNIFFER_PHY_INFO_DISABLED	= 0x00,
	WMI_SNIFFER_PHY_INFO_ENABLED	= 0x01,
};

enum wmi_sniffer_cfg_phy_support {
	WMI_SNIFFER_CP		= 0x00,
	WMI_SNIFFER_DP		= 0x01,
	WMI_SNIFFER_BOTH_PHYS	= 0x02,
};

/* wmi_sniffer_cfg */
struct wmi_sniffer_cfg {
	/* enum wmi_sniffer_cfg_mode */
	__le32 mode;
	/* enum wmi_sniffer_cfg_phy_info_mode */
	__le32 phy_info_mode;
	/* enum wmi_sniffer_cfg_phy_support */
	__le32 phy_support;
	u8 channel;
	u8 reserved[3];
} __packed;

enum wmi_cfg_rx_chain_cmd_action {
	WMI_RX_CHAIN_ADD	= 0x00,
	WMI_RX_CHAIN_DEL	= 0x01,
};

enum wmi_cfg_rx_chain_cmd_decap_trans_type {
	WMI_DECAP_TYPE_802_3		= 0x00,
	WMI_DECAP_TYPE_NATIVE_WIFI	= 0x01,
	WMI_DECAP_TYPE_NONE		= 0x02,
};

enum wmi_cfg_rx_chain_cmd_nwifi_ds_trans_type {
	WMI_NWIFI_RX_TRANS_MODE_NO		= 0x00,
	WMI_NWIFI_RX_TRANS_MODE_PBSS2AP		= 0x01,
	WMI_NWIFI_RX_TRANS_MODE_PBSS2STA	= 0x02,
};

enum wmi_cfg_rx_chain_cmd_reorder_type {
	WMI_RX_HW_REORDER	= 0x00,
	WMI_RX_SW_REORDER	= 0x01,
};

#define L2_802_3_OFFLOAD_CTRL_VLAN_TAG_INSERTION_POS	(0)
#define L2_802_3_OFFLOAD_CTRL_VLAN_TAG_INSERTION_LEN	(1)
#define L2_802_3_OFFLOAD_CTRL_VLAN_TAG_INSERTION_MSK	(0x1)
#define L2_802_3_OFFLOAD_CTRL_SNAP_KEEP_POS		(1)
#define L2_802_3_OFFLOAD_CTRL_SNAP_KEEP_LEN		(1)
#define L2_802_3_OFFLOAD_CTRL_SNAP_KEEP_MSK		(0x2)
#define L2_NWIFI_OFFLOAD_CTRL_REMOVE_QOS_POS		(0)
#define L2_NWIFI_OFFLOAD_CTRL_REMOVE_QOS_LEN		(1)
#define L2_NWIFI_OFFLOAD_CTRL_REMOVE_QOS_MSK		(0x1)
#define L2_NWIFI_OFFLOAD_CTRL_REMOVE_PN_POS		(1)
#define L2_NWIFI_OFFLOAD_CTRL_REMOVE_PN_LEN		(1)
#define L2_NWIFI_OFFLOAD_CTRL_REMOVE_PN_MSK		(0x2)
#define L3_L4_CTRL_IPV4_CHECKSUM_EN_POS			(0)
#define L3_L4_CTRL_IPV4_CHECKSUM_EN_LEN			(1)
#define L3_L4_CTRL_IPV4_CHECKSUM_EN_MSK			(0x1)
#define L3_L4_CTRL_TCPIP_CHECKSUM_EN_POS		(1)
#define L3_L4_CTRL_TCPIP_CHECKSUM_EN_LEN		(1)
#define L3_L4_CTRL_TCPIP_CHECKSUM_EN_MSK		(0x2)
#define RING_CTRL_OVERRIDE_PREFETCH_THRSH_POS		(0)
#define RING_CTRL_OVERRIDE_PREFETCH_THRSH_LEN		(1)
#define RING_CTRL_OVERRIDE_PREFETCH_THRSH_MSK		(0x1)
#define RING_CTRL_OVERRIDE_WB_THRSH_POS			(1)
#define RING_CTRL_OVERRIDE_WB_THRSH_LEN			(1)
#define RING_CTRL_OVERRIDE_WB_THRSH_MSK			(0x2)
#define RING_CTRL_OVERRIDE_ITR_THRSH_POS		(2)
#define RING_CTRL_OVERRIDE_ITR_THRSH_LEN		(1)
#define RING_CTRL_OVERRIDE_ITR_THRSH_MSK		(0x4)
#define RING_CTRL_OVERRIDE_HOST_THRSH_POS		(3)
#define RING_CTRL_OVERRIDE_HOST_THRSH_LEN		(1)
#define RING_CTRL_OVERRIDE_HOST_THRSH_MSK		(0x8)

/* WMI_CFG_RX_CHAIN_CMDID */
struct wmi_cfg_rx_chain_cmd {
	__le32 action;
	struct wmi_sw_ring_cfg rx_sw_ring;
	u8 mid;
	u8 decap_trans_type;
	u8 l2_802_3_offload_ctrl;
	u8 l2_nwifi_offload_ctrl;
	u8 vlan_id;
	u8 nwifi_ds_trans_type;
	u8 l3_l4_ctrl;
	u8 ring_ctrl;
	__le16 prefetch_thrsh;
	__le16 wb_thrsh;
	__le32 itr_value;
	__le16 host_thrsh;
	u8 reorder_type;
	u8 reserved;
	struct wmi_sniffer_cfg sniffer_cfg;
	__le16 max_rx_pl_per_desc;
} __packed;

/* WMI_RCP_ADDBA_RESP_CMDID */
struct wmi_rcp_addba_resp_cmd {
	u8 cidxtid;
	u8 dialog_token;
	__le16 status_code;
	/* ieee80211_ba_parameterset field to send */
	__le16 ba_param_set;
	__le16 ba_timeout;
} __packed;

/* WMI_RCP_DELBA_CMDID */
struct wmi_rcp_delba_cmd {
	u8 cidxtid;
	u8 reserved;
	__le16 reason;
} __packed;

/* WMI_RCP_ADDBA_REQ_CMDID */
struct wmi_rcp_addba_req_cmd {
	u8 cidxtid;
	u8 dialog_token;
	/* ieee80211_ba_parameterset field as it received */
	__le16 ba_param_set;
	__le16 ba_timeout;
	/* ieee80211_ba_seqstrl field as it received */
	__le16 ba_seq_ctrl;
} __packed;

/* WMI_SET_MAC_ADDRESS_CMDID */
struct wmi_set_mac_address_cmd {
	u8 mac[WMI_MAC_LEN];
	u8 reserved[2];
} __packed;

/* WMI_ECHO_CMDID
 * Check FW is alive
 * WMI_DEEP_ECHO_CMDID
 * Check FW and ucode are alive
 * Returned event: WMI_ECHO_RSP_EVENTID
 * same event for both commands
 */
struct wmi_echo_cmd {
	__le32 value;
} __packed;

/* WMI_OTP_READ_CMDID */
struct wmi_otp_read_cmd {
	__le32 addr;
	__le32 size;
	__le32 values;
} __packed;

/* WMI_OTP_WRITE_CMDID */
struct wmi_otp_write_cmd {
	__le32 addr;
	__le32 size;
	__le32 values;
} __packed;

/* WMI_TEMP_SENSE_CMDID
 *
 * Measure MAC and radio temperatures
 *
 * Possible modes for temperature measurement
 */
enum wmi_temperature_measure_mode {
	TEMPERATURE_USE_OLD_VALUE	= 0x01,
	TEMPERATURE_MEASURE_NOW		= 0x02,
};

/* WMI_TEMP_SENSE_CMDID */
struct wmi_temp_sense_cmd {
	__le32 measure_baseband_en;
	__le32 measure_rf_en;
	__le32 measure_mode;
} __packed;

enum wmi_pmc_op {
	WMI_PMC_ALLOCATE	= 0x00,
	WMI_PMC_RELEASE		= 0x01,
};

/* WMI_PMC_CMDID */
struct wmi_pmc_cmd {
	/* enum wmi_pmc_cmd_op_type */
	u8 op;
	u8 reserved;
	__le16 ring_size;
	__le64 mem_base;
} __packed;

enum wmi_aoa_meas_type {
	WMI_AOA_PHASE_MEAS	= 0x00,
	WMI_AOA_PHASE_AMP_MEAS	= 0x01,
};

/* WMI_AOA_MEAS_CMDID */
struct wmi_aoa_meas_cmd {
	u8 mac_addr[WMI_MAC_LEN];
	/* channels IDs:
	 * 0 - 58320 MHz
	 * 1 - 60480 MHz
	 * 2 - 62640 MHz
	 */
	u8 channel;
	/* enum wmi_aoa_meas_type */
	u8 aoa_meas_type;
	__le32 meas_rf_mask;
} __packed;

/* WMI_SET_MGMT_RETRY_LIMIT_CMDID */
struct wmi_set_mgmt_retry_limit_cmd {
	/* MAC retransmit limit for mgmt frames */
	u8 mgmt_retry_limit;
	/* alignment to 32b */
	u8 reserved[3];
} __packed;

/* Zones: HIGH, MAX, CRITICAL */
#define WMI_NUM_OF_TT_ZONES	(3)

struct wmi_tt_zone_limits {
	/* Above this temperature this zone is active */
	u8 temperature_high;
	/* Below this temperature the adjacent lower zone is active */
	u8 temperature_low;
	u8 reserved[2];
} __packed;

/* Struct used for both configuration and status commands of thermal
 * throttling
 */
struct wmi_tt_data {
	/* Enable/Disable TT algorithm for baseband */
	u8 bb_enabled;
	u8 reserved0[3];
	/* Define zones for baseband */
	struct wmi_tt_zone_limits bb_zones[WMI_NUM_OF_TT_ZONES];
	/* Enable/Disable TT algorithm for radio */
	u8 rf_enabled;
	u8 reserved1[3];
	/* Define zones for all radio chips */
	struct wmi_tt_zone_limits rf_zones[WMI_NUM_OF_TT_ZONES];
} __packed;

/* WMI_SET_THERMAL_THROTTLING_CFG_CMDID */
struct wmi_set_thermal_throttling_cfg_cmd {
	/* Command data */
	struct wmi_tt_data tt_data;
} __packed;

/* WMI_NEW_STA_CMDID */
struct wmi_new_sta_cmd {
	u8 dst_mac[WMI_MAC_LEN];
	u8 aid;
} __packed;

/* WMI_DEL_STA_CMDID */
struct wmi_del_sta_cmd {
	u8 dst_mac[WMI_MAC_LEN];
	__le16 disconnect_reason;
} __packed;

enum wmi_tof_burst_duration {
	WMI_TOF_BURST_DURATION_250_USEC		= 2,
	WMI_TOF_BURST_DURATION_500_USEC		= 3,
	WMI_TOF_BURST_DURATION_1_MSEC		= 4,
	WMI_TOF_BURST_DURATION_2_MSEC		= 5,
	WMI_TOF_BURST_DURATION_4_MSEC		= 6,
	WMI_TOF_BURST_DURATION_8_MSEC		= 7,
	WMI_TOF_BURST_DURATION_16_MSEC		= 8,
	WMI_TOF_BURST_DURATION_32_MSEC		= 9,
	WMI_TOF_BURST_DURATION_64_MSEC		= 10,
	WMI_TOF_BURST_DURATION_128_MSEC		= 11,
	WMI_TOF_BURST_DURATION_NO_PREFERENCES	= 15,
};

enum wmi_tof_session_start_flags {
	WMI_TOF_SESSION_START_FLAG_SECURED	= 0x1,
	WMI_TOF_SESSION_START_FLAG_ASAP		= 0x2,
	WMI_TOF_SESSION_START_FLAG_LCI_REQ	= 0x4,
	WMI_TOF_SESSION_START_FLAG_LCR_REQ	= 0x8,
};

/* WMI_TOF_SESSION_START_CMDID */
struct wmi_ftm_dest_info {
	u8 channel;
	/* wmi_tof_session_start_flags_e */
	u8 flags;
	u8 initial_token;
	u8 num_of_ftm_per_burst;
	u8 num_of_bursts_exp;
	/* wmi_tof_burst_duration_e */
	u8 burst_duration;
	/* Burst Period indicate interval between two consecutive burst
	 * instances, in units of 100 ms
	 */
	__le16 burst_period;
	u8 dst_mac[WMI_MAC_LEN];
	__le16 reserved;
} __packed;

/* WMI_TOF_SESSION_START_CMDID */
struct wmi_tof_session_start_cmd {
	__le32 session_id;
	u8 num_of_aoa_measures;
	u8 aoa_type;
	__le16 num_of_dest;
	u8 reserved[4];
	struct wmi_ftm_dest_info ftm_dest_info[0];
} __packed;

enum wmi_tof_channel_info_report_type {
	WMI_TOF_CHANNEL_INFO_TYPE_CIR			= 0x1,
	WMI_TOF_CHANNEL_INFO_TYPE_RSSI			= 0x2,
	WMI_TOF_CHANNEL_INFO_TYPE_SNR			= 0x4,
	WMI_TOF_CHANNEL_INFO_TYPE_DEBUG_DATA		= 0x8,
	WMI_TOF_CHANNEL_INFO_TYPE_VENDOR_SPECIFIC	= 0x10,
};

/* WMI_TOF_CHANNEL_INFO_CMDID */
struct wmi_tof_channel_info_cmd {
	/* wmi_tof_channel_info_report_type_e */
	__le32 channel_info_report_request;
} __packed;

/* WMI_TOF_SET_TX_RX_OFFSET_CMDID */
struct wmi_tof_set_tx_rx_offset_cmd {
	/* TX delay offset */
	__le32 tx_offset;
	/* RX delay offset */
	__le32 rx_offset;
	__le32 reserved[2];
} __packed;

/* WMI Events
 * List of Events (target to host)
 */
enum wmi_event_id {
	WMI_READY_EVENTID				= 0x1001,
	WMI_CONNECT_EVENTID				= 0x1002,
	WMI_DISCONNECT_EVENTID				= 0x1003,
	WMI_SCAN_COMPLETE_EVENTID			= 0x100A,
	WMI_REPORT_STATISTICS_EVENTID			= 0x100B,
	WMI_RD_MEM_RSP_EVENTID				= 0x1800,
	WMI_FW_READY_EVENTID				= 0x1801,
	WMI_EXIT_FAST_MEM_ACC_MODE_EVENTID		= 0x200,
	WMI_ECHO_RSP_EVENTID				= 0x1803,
	WMI_FS_TUNE_DONE_EVENTID			= 0x180A,
	WMI_CORR_MEASURE_EVENTID			= 0x180B,
	WMI_READ_RSSI_EVENTID				= 0x180C,
	WMI_TEMP_SENSE_DONE_EVENTID			= 0x180E,
	WMI_DC_CALIB_DONE_EVENTID			= 0x180F,
	WMI_IQ_TX_CALIB_DONE_EVENTID			= 0x1811,
	WMI_IQ_RX_CALIB_DONE_EVENTID			= 0x1812,
	WMI_SET_WORK_MODE_DONE_EVENTID			= 0x1815,
	WMI_LO_LEAKAGE_CALIB_DONE_EVENTID		= 0x1816,
	WMI_MARLON_R_READ_DONE_EVENTID			= 0x1818,
	WMI_MARLON_R_WRITE_DONE_EVENTID			= 0x1819,
	WMI_MARLON_R_TXRX_SEL_DONE_EVENTID		= 0x181A,
	WMI_SILENT_RSSI_CALIB_DONE_EVENTID		= 0x181D,
	WMI_RF_RX_TEST_DONE_EVENTID			= 0x181E,
	WMI_CFG_RX_CHAIN_DONE_EVENTID			= 0x1820,
	WMI_VRING_CFG_DONE_EVENTID			= 0x1821,
	WMI_BA_STATUS_EVENTID				= 0x1823,
	WMI_RCP_ADDBA_REQ_EVENTID			= 0x1824,
	WMI_RCP_ADDBA_RESP_SENT_EVENTID			= 0x1825,
	WMI_DELBA_EVENTID				= 0x1826,
	WMI_GET_SSID_EVENTID				= 0x1828,
	WMI_GET_PCP_CHANNEL_EVENTID			= 0x182A,
	WMI_SW_TX_COMPLETE_EVENTID			= 0x182B,
	WMI_READ_MAC_RXQ_EVENTID			= 0x1830,
	WMI_READ_MAC_TXQ_EVENTID			= 0x1831,
	WMI_WRITE_MAC_RXQ_EVENTID			= 0x1832,
	WMI_WRITE_MAC_TXQ_EVENTID			= 0x1833,
	WMI_WRITE_MAC_XQ_FIELD_EVENTID			= 0x1834,
	WMI_BEAMFORMING_MGMT_DONE_EVENTID		= 0x1836,
	WMI_BF_TXSS_MGMT_DONE_EVENTID			= 0x1837,
	WMI_BF_RXSS_MGMT_DONE_EVENTID			= 0x1839,
	WMI_RS_MGMT_DONE_EVENTID			= 0x1852,
	WMI_RF_MGMT_STATUS_EVENTID			= 0x1853,
	WMI_BF_SM_MGMT_DONE_EVENTID			= 0x1838,
	WMI_RX_MGMT_PACKET_EVENTID			= 0x1840,
	WMI_TX_MGMT_PACKET_EVENTID			= 0x1841,
	WMI_LINK_MAINTAIN_CFG_WRITE_DONE_EVENTID	= 0x1842,
	WMI_LINK_MAINTAIN_CFG_READ_DONE_EVENTID		= 0x1843,
	WMI_OTP_READ_RESULT_EVENTID			= 0x1856,
	WMI_LED_CFG_DONE_EVENTID			= 0x1858,
	/* Performance monitoring events */
	WMI_DATA_PORT_OPEN_EVENTID			= 0x1860,
	WMI_WBE_LINK_DOWN_EVENTID			= 0x1861,
	WMI_BF_CTRL_DONE_EVENTID			= 0x1862,
	WMI_NOTIFY_REQ_DONE_EVENTID			= 0x1863,
	WMI_GET_STATUS_DONE_EVENTID			= 0x1864,
	WMI_VRING_EN_EVENTID				= 0x1865,
	WMI_GET_RF_STATUS_EVENTID			= 0x1866,
	WMI_GET_BASEBAND_TYPE_EVENTID			= 0x1867,
	WMI_UNIT_TEST_EVENTID				= 0x1900,
	WMI_FLASH_READ_DONE_EVENTID			= 0x1902,
	WMI_FLASH_WRITE_DONE_EVENTID			= 0x1903,
	/* Power management */
	WMI_TRAFFIC_SUSPEND_EVENTID			= 0x1904,
	WMI_TRAFFIC_RESUME_EVENTID			= 0x1905,
	/* P2P */
	WMI_P2P_CFG_DONE_EVENTID			= 0x1910,
	WMI_PORT_ALLOCATED_EVENTID			= 0x1911,
	WMI_PORT_DELETED_EVENTID			= 0x1912,
	WMI_LISTEN_STARTED_EVENTID			= 0x1914,
	WMI_SEARCH_STARTED_EVENTID			= 0x1915,
	WMI_DISCOVERY_STARTED_EVENTID			= 0x1916,
	WMI_DISCOVERY_STOPPED_EVENTID			= 0x1917,
	WMI_PCP_STARTED_EVENTID				= 0x1918,
	WMI_PCP_STOPPED_EVENTID				= 0x1919,
	WMI_PCP_FACTOR_EVENTID				= 0x191A,
	/* Power Save Configuration Events */
	WMI_PS_DEV_PROFILE_CFG_EVENTID			= 0x191C,
	/* Not supported yet */
	WMI_PS_DEV_CFG_EVENTID				= 0x191D,
	/* Not supported yet */
	WMI_PS_DEV_CFG_READ_EVENTID			= 0x191E,
	/* Not supported yet */
	WMI_PS_MID_CFG_EVENTID				= 0x191F,
	/* Not supported yet */
	WMI_PS_MID_CFG_READ_EVENTID			= 0x1920,
	WMI_RS_CFG_DONE_EVENTID				= 0x1921,
	WMI_GET_DETAILED_RS_RES_EVENTID			= 0x1922,
	WMI_AOA_MEAS_EVENTID				= 0x1923,
	WMI_BRP_SET_ANT_LIMIT_EVENTID			= 0x1924,
	WMI_SET_MGMT_RETRY_LIMIT_EVENTID		= 0x1930,
	WMI_GET_MGMT_RETRY_LIMIT_EVENTID		= 0x1931,
	WMI_SET_THERMAL_THROTTLING_CFG_EVENTID		= 0x1940,
	WMI_GET_THERMAL_THROTTLING_CFG_EVENTID		= 0x1941,
	WMI_TOF_SESSION_END_EVENTID			= 0x1991,
	WMI_TOF_GET_CAPABILITIES_EVENTID		= 0x1992,
	WMI_TOF_SET_LCR_EVENTID				= 0x1993,
	WMI_TOF_SET_LCI_EVENTID				= 0x1994,
	WMI_TOF_FTM_PER_DEST_RES_EVENTID		= 0x1995,
	WMI_TOF_CHANNEL_INFO_EVENTID			= 0x1996,
	WMI_TOF_SET_TX_RX_OFFSET_EVENTID		= 0x1997,
	WMI_TOF_GET_TX_RX_OFFSET_EVENTID		= 0x1998,
	WMI_GET_RF_SECTOR_PARAMS_DONE_EVENTID		= 0x19A0,
	WMI_SET_RF_SECTOR_PARAMS_DONE_EVENTID		= 0x19A1,
	WMI_GET_SELECTED_RF_SECTOR_INDEX_DONE_EVENTID	= 0x19A2,
	WMI_SET_SELECTED_RF_SECTOR_INDEX_DONE_EVENTID	= 0x19A3,
	WMI_SET_RF_SECTOR_ON_DONE_EVENTID		= 0x19A4,
	WMI_PRIO_TX_SECTORS_ORDER_EVENTID		= 0x19A5,
	WMI_PRIO_TX_SECTORS_NUMBER_EVENTID		= 0x19A6,
	WMI_PRIO_TX_SECTORS_SET_DEFAULT_CFG_EVENTID	= 0x19A7,
	WMI_SET_CHANNEL_EVENTID				= 0x9000,
	WMI_ASSOC_REQ_EVENTID				= 0x9001,
	WMI_EAPOL_RX_EVENTID				= 0x9002,
	WMI_MAC_ADDR_RESP_EVENTID			= 0x9003,
	WMI_FW_VER_EVENTID				= 0x9004,
	WMI_ACS_PASSIVE_SCAN_COMPLETE_EVENTID		= 0x9005,
};

/* Events data structures */
enum wmi_fw_status {
	WMI_FW_STATUS_SUCCESS	= 0x00,
	WMI_FW_STATUS_FAILURE	= 0x01,
};

/* WMI_RF_MGMT_STATUS_EVENTID */
enum wmi_rf_status {
	WMI_RF_ENABLED		= 0x00,
	WMI_RF_DISABLED_HW	= 0x01,
	WMI_RF_DISABLED_SW	= 0x02,
	WMI_RF_DISABLED_HW_SW	= 0x03,
};

/* WMI_RF_MGMT_STATUS_EVENTID */
struct wmi_rf_mgmt_status_event {
	__le32 rf_status;
} __packed;

/* WMI_GET_STATUS_DONE_EVENTID */
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

/* WMI_FW_VER_EVENTID */
struct wmi_fw_ver_event {
	/* FW image version */
	__le32 fw_major;
	__le32 fw_minor;
	__le32 fw_subminor;
	__le32 fw_build;
	/* FW image build time stamp */
	__le32 hour;
	__le32 minute;
	__le32 second;
	__le32 day;
	__le32 month;
	__le32 year;
	/* Boot Loader image version */
	__le32 bl_major;
	__le32 bl_minor;
	__le32 bl_subminor;
	__le32 bl_build;
	/* The number of entries in the FW capabilies array */
	u8 fw_capabilities_len;
	u8 reserved[3];
	/* FW capabilities info
	 * Must be the last member of the struct
	 */
	__le32 fw_capabilities[0];
} __packed;

/* WMI_GET_RF_STATUS_EVENTID */
enum rf_type {
	RF_UNKNOWN	= 0x00,
	RF_MARLON	= 0x01,
	RF_SPARROW	= 0x02,
};

/* WMI_GET_RF_STATUS_EVENTID */
enum board_file_rf_type {
	BF_RF_MARLON	= 0x00,
	BF_RF_SPARROW	= 0x01,
};

/* WMI_GET_RF_STATUS_EVENTID */
enum rf_status {
	RF_OK			= 0x00,
	RF_NO_COMM		= 0x01,
	RF_WRONG_BOARD_FILE	= 0x02,
};

/* WMI_GET_RF_STATUS_EVENTID */
struct wmi_get_rf_status_event {
	/* enum rf_type */
	__le32 rf_type;
	/* attached RFs bit vector */
	__le32 attached_rf_vector;
	/* enabled RFs bit vector */
	__le32 enabled_rf_vector;
	/* enum rf_status, refers to enabled RFs */
	u8 rf_status[32];
	/* enum board file RF type */
	__le32 board_file_rf_type;
	/* board file platform type */
	__le32 board_file_platform_type;
	/* board file version */
	__le32 board_file_version;
	__le32 reserved[2];
} __packed;

/* WMI_GET_BASEBAND_TYPE_EVENTID */
enum baseband_type {
	BASEBAND_UNKNOWN	= 0x00,
	BASEBAND_SPARROW_M_A0	= 0x03,
	BASEBAND_SPARROW_M_A1	= 0x04,
	BASEBAND_SPARROW_M_B0	= 0x05,
	BASEBAND_SPARROW_M_C0	= 0x06,
	BASEBAND_SPARROW_M_D0	= 0x07,
	BASEBAND_TALYN_M_A0	= 0x08,
};

/* WMI_GET_BASEBAND_TYPE_EVENTID */
struct wmi_get_baseband_type_event {
	/* enum baseband_type */
	__le32 baseband_type;
} __packed;

/* WMI_MAC_ADDR_RESP_EVENTID */
struct wmi_mac_addr_resp_event {
	u8 mac[WMI_MAC_LEN];
	u8 auth_mode;
	u8 crypt_mode;
	__le32 offload_mode;
} __packed;

/* WMI_EAPOL_RX_EVENTID */
struct wmi_eapol_rx_event {
	u8 src_mac[WMI_MAC_LEN];
	__le16 eapol_len;
	u8 eapol[0];
} __packed;

/* WMI_READY_EVENTID */
enum wmi_phy_capability {
	WMI_11A_CAPABILITY		= 0x01,
	WMI_11G_CAPABILITY		= 0x02,
	WMI_11AG_CAPABILITY		= 0x03,
	WMI_11NA_CAPABILITY		= 0x04,
	WMI_11NG_CAPABILITY		= 0x05,
	WMI_11NAG_CAPABILITY		= 0x06,
	WMI_11AD_CAPABILITY		= 0x07,
	WMI_11N_CAPABILITY_OFFSET	= 0x03,
};

struct wmi_ready_event {
	__le32 sw_version;
	__le32 abi_version;
	u8 mac[WMI_MAC_LEN];
	/* enum wmi_phy_capability */
	u8 phy_capability;
	u8 numof_additional_mids;
} __packed;

/* WMI_NOTIFY_REQ_DONE_EVENTID */
struct wmi_notify_req_done_event {
	/* beamforming status, 0: fail; 1: OK; 2: retrying */
	__le32 status;
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

/* WMI_CONNECT_EVENTID */
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
	u8 aid;
	u8 reserved2[2];
	/* not in use */
	u8 assoc_info[0];
} __packed;

/* disconnect_reason */
enum wmi_disconnect_reason {
	WMI_DIS_REASON_NO_NETWORK_AVAIL		= 0x01,
	/* bmiss */
	WMI_DIS_REASON_LOST_LINK		= 0x02,
	WMI_DIS_REASON_DISCONNECT_CMD		= 0x03,
	WMI_DIS_REASON_BSS_DISCONNECTED		= 0x04,
	WMI_DIS_REASON_AUTH_FAILED		= 0x05,
	WMI_DIS_REASON_ASSOC_FAILED		= 0x06,
	WMI_DIS_REASON_NO_RESOURCES_AVAIL	= 0x07,
	WMI_DIS_REASON_CSERV_DISCONNECT		= 0x08,
	WMI_DIS_REASON_INVALID_PROFILE		= 0x0A,
	WMI_DIS_REASON_DOT11H_CHANNEL_SWITCH	= 0x0B,
	WMI_DIS_REASON_PROFILE_MISMATCH		= 0x0C,
	WMI_DIS_REASON_CONNECTION_EVICTED	= 0x0D,
	WMI_DIS_REASON_IBSS_MERGE		= 0x0E,
};

/* WMI_DISCONNECT_EVENTID */
struct wmi_disconnect_event {
	/* reason code, see 802.11 spec. */
	__le16 protocol_reason_status;
	/* set if known */
	u8 bssid[WMI_MAC_LEN];
	/* see enum wmi_disconnect_reason */
	u8 disconnect_reason;
	/* last assoc req may passed to host - not in used */
	u8 assoc_resp_len;
	/* last assoc req may passed to host - not in used */
	u8 assoc_info[0];
} __packed;

/* WMI_SCAN_COMPLETE_EVENTID */
enum scan_status {
	WMI_SCAN_SUCCESS	= 0x00,
	WMI_SCAN_FAILED		= 0x01,
	WMI_SCAN_ABORTED	= 0x02,
	WMI_SCAN_REJECTED	= 0x03,
	WMI_SCAN_ABORT_REJECTED	= 0x04,
};

struct wmi_scan_complete_event {
	/* enum scan_status */
	__le32 status;
} __packed;

/* WMI_ACS_PASSIVE_SCAN_COMPLETE_EVENT */
enum wmi_acs_info_bitmask {
	WMI_ACS_INFO_BITMASK_BEACON_FOUND	= 0x01,
	WMI_ACS_INFO_BITMASK_BUSY_TIME		= 0x02,
	WMI_ACS_INFO_BITMASK_TX_TIME		= 0x04,
	WMI_ACS_INFO_BITMASK_RX_TIME		= 0x08,
	WMI_ACS_INFO_BITMASK_NOISE		= 0x10,
};

struct scan_acs_info {
	u8 channel;
	u8 beacon_found;
	/* msec */
	__le16 busy_time;
	__le16 tx_time;
	__le16 rx_time;
	u8 noise;
	u8 reserved[3];
} __packed;

struct wmi_acs_passive_scan_complete_event {
	__le32 dwell_time;
	/* valid fields within channel info according to
	 * their appearance in struct order
	 */
	__le16 filled;
	u8 num_scanned_channels;
	u8 reserved;
	struct scan_acs_info scan_info_list[0];
} __packed;

/* WMI_BA_STATUS_EVENTID */
enum wmi_vring_ba_status {
	WMI_BA_AGREED			= 0x00,
	WMI_BA_NON_AGREED		= 0x01,
	/* BA_EN in middle of teardown flow */
	WMI_BA_TD_WIP			= 0x02,
	/* BA_DIS or BA_EN in middle of BA SETUP flow */
	WMI_BA_SETUP_WIP		= 0x03,
	/* BA_EN when the BA session is already active */
	WMI_BA_SESSION_ACTIVE		= 0x04,
	/* BA_DIS when the BA session is not active */
	WMI_BA_SESSION_NOT_ACTIVE	= 0x05,
};

struct wmi_ba_status_event {
	/* enum wmi_vring_ba_status */
	__le16 status;
	u8 reserved[2];
	u8 ringid;
	u8 agg_wsize;
	__le16 ba_timeout;
	u8 amsdu;
} __packed;

/* WMI_DELBA_EVENTID */
struct wmi_delba_event {
	u8 cidxtid;
	u8 from_initiator;
	__le16 reason;
} __packed;

/* WMI_VRING_CFG_DONE_EVENTID */
struct wmi_vring_cfg_done_event {
	u8 ringid;
	u8 status;
	u8 reserved[2];
	__le32 tx_vring_tail_ptr;
} __packed;

/* WMI_RCP_ADDBA_RESP_SENT_EVENTID */
struct wmi_rcp_addba_resp_sent_event {
	u8 cidxtid;
	u8 reserved;
	__le16 status;
} __packed;

/* WMI_RCP_ADDBA_REQ_EVENTID */
struct wmi_rcp_addba_req_event {
	u8 cidxtid;
	u8 dialog_token;
	/* ieee80211_ba_parameterset as it received */
	__le16 ba_param_set;
	__le16 ba_timeout;
	/* ieee80211_ba_seqstrl field as it received */
	__le16 ba_seq_ctrl;
} __packed;

/* WMI_CFG_RX_CHAIN_DONE_EVENTID */
enum wmi_cfg_rx_chain_done_event_status {
	WMI_CFG_RX_CHAIN_SUCCESS	= 0x01,
};

struct wmi_cfg_rx_chain_done_event {
	/* V-Ring Tail pointer */
	__le32 rx_ring_tail_ptr;
	__le32 status;
} __packed;

/* WMI_WBE_LINK_DOWN_EVENTID */
enum wmi_wbe_link_down_event_reason {
	WMI_WBE_REASON_USER_REQUEST	= 0x00,
	WMI_WBE_REASON_RX_DISASSOC	= 0x01,
	WMI_WBE_REASON_BAD_PHY_LINK	= 0x02,
};

/* WMI_WBE_LINK_DOWN_EVENTID */
struct wmi_wbe_link_down_event {
	u8 cid;
	u8 reserved[3];
	__le32 reason;
} __packed;

/* WMI_DATA_PORT_OPEN_EVENTID */
struct wmi_data_port_open_event {
	u8 cid;
	u8 reserved[3];
} __packed;

/* WMI_VRING_EN_EVENTID */
struct wmi_vring_en_event {
	u8 vring_index;
	u8 reserved[3];
} __packed;

/* WMI_GET_PCP_CHANNEL_EVENTID */
struct wmi_get_pcp_channel_event {
	u8 channel;
	u8 reserved[3];
} __packed;

/* WMI_P2P_CFG_DONE_EVENTID */
struct wmi_p2p_cfg_done_event {
	/* wmi_fw_status */
	u8 status;
	u8 reserved[3];
} __packed;

/* WMI_PORT_ALLOCATED_EVENTID */
struct wmi_port_allocated_event {
	/* wmi_fw_status */
	u8 status;
	u8 reserved[3];
} __packed;

/* WMI_PORT_DELETED_EVENTID */
struct wmi_port_deleted_event {
	/* wmi_fw_status */
	u8 status;
	u8 reserved[3];
} __packed;

/* WMI_LISTEN_STARTED_EVENTID */
struct wmi_listen_started_event {
	/* wmi_fw_status */
	u8 status;
	u8 reserved[3];
} __packed;

/* WMI_SEARCH_STARTED_EVENTID */
struct wmi_search_started_event {
	/* wmi_fw_status */
	u8 status;
	u8 reserved[3];
} __packed;

/* WMI_PCP_STARTED_EVENTID */
struct wmi_pcp_started_event {
	/* wmi_fw_status */
	u8 status;
	u8 reserved[3];
} __packed;

/* WMI_PCP_FACTOR_EVENTID */
struct wmi_pcp_factor_event {
	__le32 pcp_factor;
} __packed;

enum wmi_sw_tx_status {
	WMI_TX_SW_STATUS_SUCCESS		= 0x00,
	WMI_TX_SW_STATUS_FAILED_NO_RESOURCES	= 0x01,
	WMI_TX_SW_STATUS_FAILED_TX		= 0x02,
};

/* WMI_SW_TX_COMPLETE_EVENTID */
struct wmi_sw_tx_complete_event {
	/* enum wmi_sw_tx_status */
	u8 status;
	u8 reserved[3];
} __packed;

/* WMI_CORR_MEASURE_EVENTID */
struct wmi_corr_measure_event {
	/* signed */
	__le32 i;
	/* signed */
	__le32 q;
	/* signed */
	__le32 image_i;
	/* signed */
	__le32 image_q;
} __packed;

/* WMI_READ_RSSI_EVENTID */
struct wmi_read_rssi_event {
	__le32 ina_rssi_adc_dbm;
} __packed;

/* WMI_GET_SSID_EVENTID */
struct wmi_get_ssid_event {
	__le32 ssid_len;
	u8 ssid[WMI_MAX_SSID_LEN];
} __packed;

/* wmi_rx_mgmt_info */
struct wmi_rx_mgmt_info {
	u8 mcs;
	s8 snr;
	u8 range;
	u8 sqi;
	__le16 stype;
	__le16 status;
	__le32 len;
	/* Not resolved when == 0xFFFFFFFF  ==> Broadcast to all MIDS */
	u8 qid;
	/* Not resolved when == 0xFFFFFFFF  ==> Broadcast to all MIDS */
	u8 mid;
	u8 cid;
	/* From Radio MNGR */
	u8 channel;
} __packed;

/* wmi_otp_read_write_cmd */
struct wmi_otp_read_write_cmd {
	__le32 addr;
	__le32 size;
	u8 values[0];
} __packed;

/* WMI_OTP_READ_RESULT_EVENTID */
struct wmi_otp_read_result_event {
	u8 payload[0];
} __packed;

/* WMI_TX_MGMT_PACKET_EVENTID */
struct wmi_tx_mgmt_packet_event {
	u8 payload[0];
} __packed;

/* WMI_RX_MGMT_PACKET_EVENTID */
struct wmi_rx_mgmt_packet_event {
	struct wmi_rx_mgmt_info info;
	u8 payload[0];
} __packed;

/* WMI_ECHO_RSP_EVENTID */
struct wmi_echo_rsp_event {
	__le32 echoed_value;
} __packed;

/* WMI_TEMP_SENSE_DONE_EVENTID
 *
 * Measure MAC and radio temperatures
 */
struct wmi_temp_sense_done_event {
	/* Temperature times 1000 (actual temperature will be achieved by
	 * dividing the value by 1000)
	 */
	__le32 baseband_t1000;
	/* Temperature times 1000 (actual temperature will be achieved by
	 * dividing the value by 1000)
	 */
	__le32 rf_t1000;
} __packed;

#define WMI_SCAN_DWELL_TIME_MS	(100)
#define WMI_SURVEY_TIMEOUT_MS	(10000)

enum wmi_hidden_ssid {
	WMI_HIDDEN_SSID_DISABLED	= 0x00,
	WMI_HIDDEN_SSID_SEND_EMPTY	= 0x10,
	WMI_HIDDEN_SSID_CLEAR		= 0xFE,
};

/* WMI_LED_CFG_CMDID
 *
 * Configure LED On\Off\Blinking operation
 *
 * Returned events:
 * - WMI_LED_CFG_DONE_EVENTID
 */
enum led_mode {
	LED_DISABLE	= 0x00,
	LED_ENABLE	= 0x01,
};

/* The names of the led as
 * described on HW schemes.
 */
enum wmi_led_id {
	WMI_LED_WLAN	= 0x00,
	WMI_LED_WPAN	= 0x01,
	WMI_LED_WWAN	= 0x02,
};

/* Led polarity mode. */
enum wmi_led_polarity {
	LED_POLARITY_HIGH_ACTIVE	= 0x00,
	LED_POLARITY_LOW_ACTIVE		= 0x01,
};

/* Combination of on and off
 * creates the blinking period
 */
struct wmi_led_blink_mode {
	__le32 blink_on;
	__le32 blink_off;
} __packed;

/* WMI_LED_CFG_CMDID */
struct wmi_led_cfg_cmd {
	/* enum led_mode_e */
	u8 led_mode;
	/* enum wmi_led_id_e */
	u8 id;
	/* slow speed blinking combination */
	struct wmi_led_blink_mode slow_blink_cfg;
	/* medium speed blinking combination */
	struct wmi_led_blink_mode medium_blink_cfg;
	/* high speed blinking combination */
	struct wmi_led_blink_mode fast_blink_cfg;
	/* polarity of the led */
	u8 led_polarity;
	/* reserved */
	u8 reserved;
} __packed;

/* WMI_LED_CFG_DONE_EVENTID */
struct wmi_led_cfg_done_event {
	/* led config status */
	__le32 status;
} __packed;

#define WMI_NUM_MCS	(13)

/* Rate search parameters configuration per connection */
struct wmi_rs_cfg {
	/* The maximal allowed PER for each MCS
	 * MCS will be considered as failed if PER during RS is higher
	 */
	u8 per_threshold[WMI_NUM_MCS];
	/* Number of MPDUs for each MCS
	 * this is the minimal statistic required to make an educated
	 * decision
	 */
	u8 min_frame_cnt[WMI_NUM_MCS];
	/* stop threshold [0-100] */
	u8 stop_th;
	/* MCS1 stop threshold [0-100] */
	u8 mcs1_fail_th;
	u8 max_back_failure_th;
	/* Debug feature for disabling internal RS trigger (which is
	 * currently triggered by BF Done)
	 */
	u8 dbg_disable_internal_trigger;
	__le32 back_failure_mask;
	__le32 mcs_en_vec;
} __packed;

/* WMI_RS_CFG_CMDID */
struct wmi_rs_cfg_cmd {
	/* connection id */
	u8 cid;
	/* enable or disable rate search */
	u8 rs_enable;
	/* rate search configuration */
	struct wmi_rs_cfg rs_cfg;
} __packed;

/* WMI_RS_CFG_DONE_EVENTID */
struct wmi_rs_cfg_done_event {
	u8 cid;
	/* enum wmi_fw_status */
	u8 status;
	u8 reserved[2];
} __packed;

/* WMI_GET_DETAILED_RS_RES_CMDID */
struct wmi_get_detailed_rs_res_cmd {
	/* connection id */
	u8 cid;
	u8 reserved[3];
} __packed;

/* RS results status */
enum wmi_rs_results_status {
	WMI_RS_RES_VALID	= 0x00,
	WMI_RS_RES_INVALID	= 0x01,
};

/* Rate search results */
struct wmi_rs_results {
	/* number of sent MPDUs */
	u8 num_of_tx_pkt[WMI_NUM_MCS];
	/* number of non-acked MPDUs */
	u8 num_of_non_acked_pkt[WMI_NUM_MCS];
	/* RS timestamp */
	__le32 tsf;
	/* RS selected MCS */
	u8 mcs;
} __packed;

/* WMI_GET_DETAILED_RS_RES_EVENTID */
struct wmi_get_detailed_rs_res_event {
	u8 cid;
	/* enum wmi_rs_results_status */
	u8 status;
	/* detailed rs results */
	struct wmi_rs_results rs_results;
	u8 reserved[3];
} __packed;

/* BRP antenna limit mode */
enum wmi_brp_ant_limit_mode {
	/* Disable BRP force antenna limit */
	WMI_BRP_ANT_LIMIT_MODE_DISABLE		= 0x00,
	/* Define maximal antennas limit. Only effective antennas will be
	 * actually used
	 */
	WMI_BRP_ANT_LIMIT_MODE_EFFECTIVE	= 0x01,
	/* Force a specific number of antennas */
	WMI_BRP_ANT_LIMIT_MODE_FORCE		= 0x02,
	/* number of BRP antenna limit modes */
	WMI_BRP_ANT_LIMIT_MODES_NUM		= 0x03,
};

/* WMI_BRP_SET_ANT_LIMIT_CMDID */
struct wmi_brp_set_ant_limit_cmd {
	/* connection id */
	u8 cid;
	/* enum wmi_brp_ant_limit_mode */
	u8 limit_mode;
	/* antenna limit count, 1-27
	 * disable_mode - ignored
	 * effective_mode - upper limit to number of antennas to be used
	 * force_mode - exact number of antennas to be used
	 */
	u8 ant_limit;
	u8 reserved;
} __packed;

/* WMI_BRP_SET_ANT_LIMIT_EVENTID */
struct wmi_brp_set_ant_limit_event {
	/* wmi_fw_status */
	u8 status;
	u8 reserved[3];
} __packed;

/* broadcast connection ID */
#define WMI_LINK_MAINTAIN_CFG_CID_BROADCAST	(0xFFFFFFFF)

/* Types wmi_link_maintain_cfg presets for WMI_LINK_MAINTAIN_CFG_WRITE_CMD */
enum wmi_link_maintain_cfg_type {
	/* AP/PCP default normal (non-FST) configuration settings */
	WMI_LINK_MAINTAIN_CFG_TYPE_DEFAULT_NORMAL_AP	= 0x00,
	/* AP/PCP  default FST configuration settings */
	WMI_LINK_MAINTAIN_CFG_TYPE_DEFAULT_FST_AP	= 0x01,
	/* STA default normal (non-FST) configuration settings */
	WMI_LINK_MAINTAIN_CFG_TYPE_DEFAULT_NORMAL_STA	= 0x02,
	/* STA default FST configuration settings */
	WMI_LINK_MAINTAIN_CFG_TYPE_DEFAULT_FST_STA	= 0x03,
	/* custom configuration settings */
	WMI_LINK_MAINTAIN_CFG_TYPE_CUSTOM		= 0x04,
	/* number of defined configuration types */
	WMI_LINK_MAINTAIN_CFG_TYPES_NUM			= 0x05,
};

/* Response status codes for WMI_LINK_MAINTAIN_CFG_WRITE/READ commands */
enum wmi_link_maintain_cfg_response_status {
	/* WMI_LINK_MAINTAIN_CFG_WRITE/READ command successfully accomplished
	 */
	WMI_LINK_MAINTAIN_CFG_RESPONSE_STATUS_OK		= 0x00,
	/* ERROR due to bad argument in WMI_LINK_MAINTAIN_CFG_WRITE/READ
	 * command request
	 */
	WMI_LINK_MAINTAIN_CFG_RESPONSE_STATUS_BAD_ARGUMENT	= 0x01,
};

/* Link Loss and Keep Alive configuration */
struct wmi_link_maintain_cfg {
	/* link_loss_enable_detectors_vec */
	__le32 link_loss_enable_detectors_vec;
	/* detectors check period usec */
	__le32 check_link_loss_period_usec;
	/* max allowed tx ageing */
	__le32 tx_ageing_threshold_usec;
	/* keep alive period for high SNR */
	__le32 keep_alive_period_usec_high_snr;
	/* keep alive period for low SNR */
	__le32 keep_alive_period_usec_low_snr;
	/* lower snr limit for keep alive period update */
	__le32 keep_alive_snr_threshold_low_db;
	/* upper snr limit for keep alive period update */
	__le32 keep_alive_snr_threshold_high_db;
	/* num of successive bad bcons causing link-loss */
	__le32 bad_beacons_num_threshold;
	/* SNR limit for bad_beacons_detector */
	__le32 bad_beacons_snr_threshold_db;
} __packed;

/* WMI_LINK_MAINTAIN_CFG_WRITE_CMDID */
struct wmi_link_maintain_cfg_write_cmd {
	/* enum wmi_link_maintain_cfg_type_e - type of requested default
	 * configuration to be applied
	 */
	__le32 cfg_type;
	/* requested connection ID or WMI_LINK_MAINTAIN_CFG_CID_BROADCAST */
	__le32 cid;
	/* custom configuration settings to be applied (relevant only if
	 * cfg_type==WMI_LINK_MAINTAIN_CFG_TYPE_CUSTOM)
	 */
	struct wmi_link_maintain_cfg lm_cfg;
} __packed;

/* WMI_LINK_MAINTAIN_CFG_READ_CMDID */
struct wmi_link_maintain_cfg_read_cmd {
	/* connection ID which configuration settings are requested */
	__le32 cid;
} __packed;

/* WMI_LINK_MAINTAIN_CFG_WRITE_DONE_EVENTID */
struct wmi_link_maintain_cfg_write_done_event {
	/* requested connection ID */
	__le32 cid;
	/* wmi_link_maintain_cfg_response_status_e - write status */
	__le32 status;
} __packed;

/* \WMI_LINK_MAINTAIN_CFG_READ_DONE_EVENT */
struct wmi_link_maintain_cfg_read_done_event {
	/* requested connection ID */
	__le32 cid;
	/* wmi_link_maintain_cfg_response_status_e - read status */
	__le32 status;
	/* Retrieved configuration settings */
	struct wmi_link_maintain_cfg lm_cfg;
} __packed;

enum wmi_traffic_suspend_status {
	WMI_TRAFFIC_SUSPEND_APPROVED	= 0x0,
	WMI_TRAFFIC_SUSPEND_REJECTED	= 0x1,
};

/* WMI_TRAFFIC_SUSPEND_EVENTID */
struct wmi_traffic_suspend_event {
	/* enum wmi_traffic_suspend_status_e */
	u8 status;
} __packed;

enum wmi_traffic_resume_status {
	WMI_TRAFFIC_RESUME_SUCCESS	= 0x0,
	WMI_TRAFFIC_RESUME_FAILED	= 0x1,
};

/* WMI_TRAFFIC_RESUME_EVENTID */
struct wmi_traffic_resume_event {
	/* enum wmi_traffic_resume_status_e */
	u8 status;
} __packed;

/* Power Save command completion status codes */
enum wmi_ps_cfg_cmd_status {
	WMI_PS_CFG_CMD_STATUS_SUCCESS	= 0x00,
	WMI_PS_CFG_CMD_STATUS_BAD_PARAM	= 0x01,
	/* other error */
	WMI_PS_CFG_CMD_STATUS_ERROR	= 0x02,
};

/* Device Power Save Profiles */
enum wmi_ps_profile_type {
	WMI_PS_PROFILE_TYPE_DEFAULT		= 0x00,
	WMI_PS_PROFILE_TYPE_PS_DISABLED		= 0x01,
	WMI_PS_PROFILE_TYPE_MAX_PS		= 0x02,
	WMI_PS_PROFILE_TYPE_LOW_LATENCY_PS	= 0x03,
};

/* WMI_PS_DEV_PROFILE_CFG_CMDID
 *
 * Power save profile to be used by the device
 *
 * Returned event:
 * - WMI_PS_DEV_PROFILE_CFG_EVENTID
 */
struct wmi_ps_dev_profile_cfg_cmd {
	/* wmi_ps_profile_type_e */
	u8 ps_profile;
	u8 reserved[3];
} __packed;

/* WMI_PS_DEV_PROFILE_CFG_EVENTID */
struct wmi_ps_dev_profile_cfg_event {
	/* wmi_ps_cfg_cmd_status_e */
	__le32 status;
} __packed;

enum wmi_ps_level {
	WMI_PS_LEVEL_DEEP_SLEEP		= 0x00,
	WMI_PS_LEVEL_SHALLOW_SLEEP	= 0x01,
	/* awake = all PS mechanisms are disabled */
	WMI_PS_LEVEL_AWAKE		= 0x02,
};

enum wmi_ps_deep_sleep_clk_level {
	/* 33k */
	WMI_PS_DEEP_SLEEP_CLK_LEVEL_RTC		= 0x00,
	/* 10k */
	WMI_PS_DEEP_SLEEP_CLK_LEVEL_OSC		= 0x01,
	/* @RTC Low latency */
	WMI_PS_DEEP_SLEEP_CLK_LEVEL_RTC_LT	= 0x02,
	WMI_PS_DEEP_SLEEP_CLK_LEVEL_XTAL	= 0x03,
	WMI_PS_DEEP_SLEEP_CLK_LEVEL_SYSCLK	= 0x04,
	/* Not Applicable */
	WMI_PS_DEEP_SLEEP_CLK_LEVEL_N_A		= 0xFF,
};

/* Response by the FW to a D3 entry request */
enum wmi_ps_d3_resp_policy {
	WMI_PS_D3_RESP_POLICY_DEFAULT	= 0x00,
	/* debug -D3 req is always denied */
	WMI_PS_D3_RESP_POLICY_DENIED	= 0x01,
	/* debug -D3 req is always approved */
	WMI_PS_D3_RESP_POLICY_APPROVED	= 0x02,
};

/* Device common power save configurations */
struct wmi_ps_dev_cfg {
	/* lowest level of PS allowed while unassociated, enum wmi_ps_level_e
	 */
	u8 ps_unassoc_min_level;
	/* lowest deep sleep clock level while nonassoc, enum
	 * wmi_ps_deep_sleep_clk_level_e
	 */
	u8 ps_unassoc_deep_sleep_min_level;
	/* lowest level of PS allowed while associated, enum wmi_ps_level_e */
	u8 ps_assoc_min_level;
	/* lowest deep sleep clock level while assoc, enum
	 * wmi_ps_deep_sleep_clk_level_e
	 */
	u8 ps_assoc_deep_sleep_min_level;
	/* enum wmi_ps_deep_sleep_clk_level_e */
	u8 ps_assoc_low_latency_ds_min_level;
	/* enum wmi_ps_d3_resp_policy_e */
	u8 ps_D3_response_policy;
	/* BOOL */
	u8 ps_D3_pm_pme_enabled;
	/* BOOL */
	u8 ps_halp_enable;
	u8 ps_deep_sleep_enter_thresh_msec;
	/* BOOL */
	u8 ps_voltage_scaling_en;
} __packed;

/* WMI_PS_DEV_CFG_CMDID
 *
 * Configure common Power Save parameters of the device and all MIDs.
 *
 * Returned event:
 * - WMI_PS_DEV_CFG_EVENTID
 */
struct wmi_ps_dev_cfg_cmd {
	/* Device Power Save configuration to be applied */
	struct wmi_ps_dev_cfg ps_dev_cfg;
	/* alignment to 32b */
	u8 reserved[2];
} __packed;

/* WMI_PS_DEV_CFG_EVENTID */
struct wmi_ps_dev_cfg_event {
	/* wmi_ps_cfg_cmd_status_e */
	__le32 status;
} __packed;

/* WMI_PS_DEV_CFG_READ_CMDID
 *
 * request to retrieve  device Power Save configuration
 * (WMI_PS_DEV_CFG_CMD params)
 *
 * Returned event:
 * - WMI_PS_DEV_CFG_READ_EVENTID
 */
struct wmi_ps_dev_cfg_read_cmd {
	__le32 reserved;
} __packed;

/* WMI_PS_DEV_CFG_READ_EVENTID */
struct wmi_ps_dev_cfg_read_event {
	/* wmi_ps_cfg_cmd_status_e */
	__le32 status;
	/* Retrieved device Power Save configuration (WMI_PS_DEV_CFG_CMD
	 * params)
	 */
	struct wmi_ps_dev_cfg dev_ps_cfg;
	/* alignment to 32b */
	u8 reserved[2];
} __packed;

/* Per Mac Power Save configurations */
struct wmi_ps_mid_cfg {
	/* Low power RX in BTI is enabled, BOOL */
	u8 beacon_lprx_enable;
	/* Sync to sector ID enabled, BOOL */
	u8 beacon_sync_to_sectorId_enable;
	/* Low power RX in DTI is enabled, BOOL */
	u8 frame_exchange_lprx_enable;
	/* Sleep Cycle while in scheduled PS, 1-31 */
	u8 scheduled_sleep_cycle_pow2;
	/* Stay Awake for k BIs every (sleep_cycle - k) BIs, 1-31 */
	u8 scheduled_num_of_awake_bis;
	u8 am_to_traffic_load_thresh_mbp;
	u8 traffic_to_am_load_thresh_mbps;
	u8 traffic_to_am_num_of_no_traffic_bis;
	/* BOOL */
	u8 continuous_traffic_psm;
	__le16 no_traffic_to_min_usec;
	__le16 no_traffic_to_max_usec;
	__le16 snoozing_sleep_interval_milisec;
	u8 max_no_data_awake_events;
	/* Trigger WEB after k failed beacons */
	u8 num_of_failed_beacons_rx_to_trigger_web;
	/* Trigger BF after k failed beacons */
	u8 num_of_failed_beacons_rx_to_trigger_bf;
	/* Trigger SOB after k successful beacons */
	u8 num_of_successful_beacons_rx_to_trigger_sob;
} __packed;

/* WMI_PS_MID_CFG_CMDID
 *
 * Configure Power Save parameters of a specific MID.
 * These parameters are relevant for the specific BSS this MID belongs to.
 *
 * Returned event:
 * - WMI_PS_MID_CFG_EVENTID
 */
struct wmi_ps_mid_cfg_cmd {
	/* MAC ID */
	u8 mid;
	/* mid PS configuration to be applied */
	struct wmi_ps_mid_cfg ps_mid_cfg;
} __packed;

/* WMI_PS_MID_CFG_EVENTID */
struct wmi_ps_mid_cfg_event {
	/* MAC ID */
	u8 mid;
	/* alignment to 32b */
	u8 reserved[3];
	/* wmi_ps_cfg_cmd_status_e */
	__le32 status;
} __packed;

/* WMI_PS_MID_CFG_READ_CMDID
 *
 * request to retrieve Power Save configuration of mid
 * (WMI_PS_MID_CFG_CMD params)
 *
 * Returned event:
 * - WMI_PS_MID_CFG_READ_EVENTID
 */
struct wmi_ps_mid_cfg_read_cmd {
	/* MAC ID */
	u8 mid;
	/* alignment to 32b */
	u8 reserved[3];
} __packed;

/* WMI_PS_MID_CFG_READ_EVENTID */
struct wmi_ps_mid_cfg_read_event {
	/* MAC ID */
	u8 mid;
	/* Retrieved MID Power Save configuration(WMI_PS_MID_CFG_CMD params) */
	struct wmi_ps_mid_cfg mid_ps_cfg;
	/* wmi_ps_cfg_cmd_status_e */
	__le32 status;
} __packed;

#define WMI_AOA_MAX_DATA_SIZE	(128)

enum wmi_aoa_meas_status {
	WMI_AOA_MEAS_SUCCESS		= 0x00,
	WMI_AOA_MEAS_PEER_INCAPABLE	= 0x01,
	WMI_AOA_MEAS_FAILURE		= 0x02,
};

/* WMI_AOA_MEAS_EVENTID */
struct wmi_aoa_meas_event {
	u8 mac_addr[WMI_MAC_LEN];
	/* channels IDs:
	 * 0 - 58320 MHz
	 * 1 - 60480 MHz
	 * 2 - 62640 MHz
	 */
	u8 channel;
	/* enum wmi_aoa_meas_type */
	u8 aoa_meas_type;
	/* Measurments are from RFs, defined by the mask */
	__le32 meas_rf_mask;
	/* enum wmi_aoa_meas_status */
	u8 meas_status;
	u8 reserved;
	/* Length of meas_data in bytes */
	__le16 length;
	u8 meas_data[WMI_AOA_MAX_DATA_SIZE];
} __packed;

/* WMI_SET_MGMT_RETRY_LIMIT_EVENTID */
struct wmi_set_mgmt_retry_limit_event {
	/* enum wmi_fw_status */
	u8 status;
	/* alignment to 32b */
	u8 reserved[3];
} __packed;

/* WMI_GET_MGMT_RETRY_LIMIT_EVENTID */
struct wmi_get_mgmt_retry_limit_event {
	/* MAC retransmit limit for mgmt frames */
	u8 mgmt_retry_limit;
	/* alignment to 32b */
	u8 reserved[3];
} __packed;

/* WMI_TOF_GET_CAPABILITIES_EVENTID */
struct wmi_tof_get_capabilities_event {
	u8 ftm_capability;
	/* maximum supported number of destination to start TOF */
	u8 max_num_of_dest;
	/* maximum supported number of measurements per burst */
	u8 max_num_of_meas_per_burst;
	u8 reserved;
	/* maximum supported multi bursts */
	__le16 max_multi_bursts_sessions;
	/* maximum supported FTM burst duration , wmi_tof_burst_duration_e */
	__le16 max_ftm_burst_duration;
	/* AOA supported types */
	__le32 aoa_supported_types;
} __packed;

/* WMI_SET_THERMAL_THROTTLING_CFG_EVENTID */
struct wmi_set_thermal_throttling_cfg_event {
	/* wmi_fw_status */
	u8 status;
	u8 reserved[3];
} __packed;

/* WMI_GET_THERMAL_THROTTLING_CFG_EVENTID */
struct wmi_get_thermal_throttling_cfg_event {
	/* Status data */
	struct wmi_tt_data tt_data;
} __packed;

enum wmi_tof_session_end_status {
	WMI_TOF_SESSION_END_NO_ERROR		= 0x00,
	WMI_TOF_SESSION_END_FAIL		= 0x01,
	WMI_TOF_SESSION_END_PARAMS_ERROR	= 0x02,
	WMI_TOF_SESSION_END_ABORTED		= 0x03,
};

/* WMI_TOF_SESSION_END_EVENTID */
struct wmi_tof_session_end_event {
	/* FTM session ID */
	__le32 session_id;
	/* wmi_tof_session_end_status_e */
	u8 status;
	u8 reserved[3];
} __packed;

/* Responder FTM Results */
struct wmi_responder_ftm_res {
	u8 t1[6];
	u8 t2[6];
	u8 t3[6];
	u8 t4[6];
	__le16 tod_err;
	__le16 toa_err;
	__le16 tod_err_initiator;
	__le16 toa_err_initiator;
} __packed;

enum wmi_tof_ftm_per_dest_res_status {
	WMI_PER_DEST_RES_NO_ERROR		= 0x00,
	WMI_PER_DEST_RES_TX_RX_FAIL		= 0x01,
	WMI_PER_DEST_RES_PARAM_DONT_MATCH	= 0x02,
};

enum wmi_tof_ftm_per_dest_res_flags {
	WMI_PER_DEST_RES_REQ_START		= 0x01,
	WMI_PER_DEST_RES_BURST_REPORT_END	= 0x02,
	WMI_PER_DEST_RES_REQ_END		= 0x04,
	WMI_PER_DEST_RES_PARAM_UPDATE		= 0x08,
};

/* WMI_TOF_FTM_PER_DEST_RES_EVENTID */
struct wmi_tof_ftm_per_dest_res_event {
	/* FTM session ID */
	__le32 session_id;
	/* destination MAC address */
	u8 dst_mac[WMI_MAC_LEN];
	/* wmi_tof_ftm_per_dest_res_flags_e */
	u8 flags;
	/* wmi_tof_ftm_per_dest_res_status_e */
	u8 status;
	/* responder ASAP */
	u8 responder_asap;
	/* responder number of FTM per burst */
	u8 responder_num_ftm_per_burst;
	/* responder number of FTM burst exponent */
	u8 responder_num_ftm_bursts_exp;
	/* responder burst duration ,wmi_tof_burst_duration_e */
	u8 responder_burst_duration;
	/* responder burst period, indicate interval between two consecutive
	 * burst instances, in units of 100 ms
	 */
	__le16 responder_burst_period;
	/* receive burst counter */
	__le16 bursts_cnt;
	/* tsf of responder start burst */
	__le32 tsf_sync;
	/* actual received ftm per burst */
	u8 actual_ftm_per_burst;
	u8 reserved0[7];
	struct wmi_responder_ftm_res responder_ftm_res[0];
} __packed;

enum wmi_tof_channel_info_type {
	WMI_TOF_CHANNEL_INFO_AOA		= 0x00,
	WMI_TOF_CHANNEL_INFO_LCI		= 0x01,
	WMI_TOF_CHANNEL_INFO_LCR		= 0x02,
	WMI_TOF_CHANNEL_INFO_VENDOR_SPECIFIC	= 0x03,
	WMI_TOF_CHANNEL_INFO_CIR		= 0x04,
	WMI_TOF_CHANNEL_INFO_RSSI		= 0x05,
	WMI_TOF_CHANNEL_INFO_SNR		= 0x06,
	WMI_TOF_CHANNEL_INFO_DEBUG		= 0x07,
};

/* WMI_TOF_CHANNEL_INFO_EVENTID */
struct wmi_tof_channel_info_event {
	/* FTM session ID */
	__le32 session_id;
	/* destination MAC address */
	u8 dst_mac[WMI_MAC_LEN];
	/* wmi_tof_channel_info_type_e */
	u8 type;
	/* data report length */
	u8 len;
	/* data report payload */
	u8 report[0];
} __packed;

/* WMI_TOF_SET_TX_RX_OFFSET_EVENTID */
struct wmi_tof_set_tx_rx_offset_event {
	/* enum wmi_fw_status */
	u8 status;
	u8 reserved[3];
} __packed;

/* WMI_TOF_GET_TX_RX_OFFSET_EVENTID */
struct wmi_tof_get_tx_rx_offset_event {
	/* enum wmi_fw_status */
	u8 status;
	u8 reserved1[3];
	/* TX delay offset */
	__le32 tx_offset;
	/* RX delay offset */
	__le32 rx_offset;
	__le32 reserved2[2];
} __packed;

/* Result status codes for WMI commands */
enum wmi_rf_sector_status {
	WMI_RF_SECTOR_STATUS_SUCCESS			= 0x00,
	WMI_RF_SECTOR_STATUS_BAD_PARAMETERS_ERROR	= 0x01,
	WMI_RF_SECTOR_STATUS_BUSY_ERROR			= 0x02,
	WMI_RF_SECTOR_STATUS_NOT_SUPPORTED_ERROR	= 0x03,
};

/* Types of the RF sector (TX,RX) */
enum wmi_rf_sector_type {
	WMI_RF_SECTOR_TYPE_RX	= 0x00,
	WMI_RF_SECTOR_TYPE_TX	= 0x01,
};

/* Content of RF Sector (six 32-bits registers) */
struct wmi_rf_sector_info {
	/* Phase values for RF Chains[15-0] (2bits per RF chain) */
	__le32 psh_hi;
	/* Phase values for RF Chains[31-16] (2bits per RF chain) */
	__le32 psh_lo;
	/* ETYPE Bit0 for all RF chains[31-0] - bit0 of Edge amplifier gain
	 * index
	 */
	__le32 etype0;
	/* ETYPE Bit1 for all RF chains[31-0] - bit1 of Edge amplifier gain
	 * index
	 */
	__le32 etype1;
	/* ETYPE Bit2 for all RF chains[31-0] - bit2 of Edge amplifier gain
	 * index
	 */
	__le32 etype2;
	/* D-Type values (3bits each) for 8 Distribution amplifiers + X16
	 * switch bits
	 */
	__le32 dtype_swch_off;
} __packed;

#define WMI_INVALID_RF_SECTOR_INDEX	(0xFFFF)
#define WMI_MAX_RF_MODULES_NUM		(8)

/* WMI_GET_RF_SECTOR_PARAMS_CMD */
struct wmi_get_rf_sector_params_cmd {
	/* Sector number to be retrieved */
	__le16 sector_idx;
	/* enum wmi_rf_sector_type - type of requested RF sector */
	u8 sector_type;
	/* bitmask vector specifying destination RF modules */
	u8 rf_modules_vec;
} __packed;

/* \WMI_GET_RF_SECTOR_PARAMS_DONE_EVENT */
struct wmi_get_rf_sector_params_done_event {
	/* result status of WMI_GET_RF_SECTOR_PARAMS_CMD (enum
	 * wmi_rf_sector_status)
	 */
	u8 status;
	/* align next field to U64 boundary */
	u8 reserved[7];
	/* TSF timestamp when RF sectors where retrieved */
	__le64 tsf;
	/* Content of RF sector retrieved from each RF module */
	struct wmi_rf_sector_info sectors_info[WMI_MAX_RF_MODULES_NUM];
} __packed;

/* WMI_SET_RF_SECTOR_PARAMS_CMD */
struct wmi_set_rf_sector_params_cmd {
	/* Sector number to be retrieved */
	__le16 sector_idx;
	/* enum wmi_rf_sector_type - type of requested RF sector */
	u8 sector_type;
	/* bitmask vector specifying destination RF modules */
	u8 rf_modules_vec;
	/* Content of RF sector to be written to each RF module */
	struct wmi_rf_sector_info sectors_info[WMI_MAX_RF_MODULES_NUM];
} __packed;

/* \WMI_SET_RF_SECTOR_PARAMS_DONE_EVENT */
struct wmi_set_rf_sector_params_done_event {
	/* result status of WMI_SET_RF_SECTOR_PARAMS_CMD (enum
	 * wmi_rf_sector_status)
	 */
	u8 status;
} __packed;

/* WMI_GET_SELECTED_RF_SECTOR_INDEX_CMD - Get RF sector index selected by
 * TXSS/BRP for communication with specified CID
 */
struct wmi_get_selected_rf_sector_index_cmd {
	/* Connection/Station ID in [0:7] range */
	u8 cid;
	/* type of requested RF sector (enum wmi_rf_sector_type) */
	u8 sector_type;
	/* align to U32 boundary */
	u8 reserved[2];
} __packed;

/* \WMI_GET_SELECTED_RF_SECTOR_INDEX_DONE_EVENT - Returns retrieved RF sector
 * index selected by TXSS/BRP for communication with specified CID
 */
struct wmi_get_selected_rf_sector_index_done_event {
	/* Retrieved sector index selected in TXSS (for TX sector request) or
	 * BRP (for RX sector request)
	 */
	__le16 sector_idx;
	/* result status of WMI_GET_SELECTED_RF_SECTOR_INDEX_CMD (enum
	 * wmi_rf_sector_status)
	 */
	u8 status;
	/* align next field to U64 boundary */
	u8 reserved[5];
	/* TSF timestamp when result was retrieved */
	__le64 tsf;
} __packed;

/* WMI_SET_SELECTED_RF_SECTOR_INDEX_CMD - Force RF sector index for
 * communication with specified CID. Assumes that TXSS/BRP is disabled by
 * other command
 */
struct wmi_set_selected_rf_sector_index_cmd {
	/* Connection/Station ID in [0:7] range */
	u8 cid;
	/* type of requested RF sector (enum wmi_rf_sector_type) */
	u8 sector_type;
	/* Forced sector index */
	__le16 sector_idx;
} __packed;

/* \WMI_SET_SELECTED_RF_SECTOR_INDEX_DONE_EVENT - Success/Fail status for
 * WMI_SET_SELECTED_RF_SECTOR_INDEX_CMD
 */
struct wmi_set_selected_rf_sector_index_done_event {
	/* result status of WMI_SET_SELECTED_RF_SECTOR_INDEX_CMD (enum
	 * wmi_rf_sector_status)
	 */
	u8 status;
	/* align to U32 boundary */
	u8 reserved[3];
} __packed;

/* WMI_SET_RF_SECTOR_ON_CMD - Activates specified sector for specified rf
 * modules
 */
struct wmi_set_rf_sector_on_cmd {
	/* Sector index to be activated */
	__le16 sector_idx;
	/* type of requested RF sector (enum wmi_rf_sector_type) */
	u8 sector_type;
	/* bitmask vector specifying destination RF modules */
	u8 rf_modules_vec;
} __packed;

/* \WMI_SET_RF_SECTOR_ON_DONE_EVENT - Success/Fail status for
 * WMI_SET_RF_SECTOR_ON_CMD
 */
struct wmi_set_rf_sector_on_done_event {
	/* result status of WMI_SET_RF_SECTOR_ON_CMD (enum
	 * wmi_rf_sector_status)
	 */
	u8 status;
	/* align to U32 boundary */
	u8 reserved[3];
} __packed;

enum wmi_sector_sweep_type {
	WMI_SECTOR_SWEEP_TYPE_TXSS		= 0x00,
	WMI_SECTOR_SWEEP_TYPE_BCON		= 0x01,
	WMI_SECTOR_SWEEP_TYPE_TXSS_AND_BCON	= 0x02,
	WMI_SECTOR_SWEEP_TYPE_NUM		= 0x03,
};

/* WMI_PRIO_TX_SECTORS_ORDER_CMDID
 *
 * Set the order of TX sectors in TXSS and/or Beacon(AP).
 *
 * Returned event:
 * - WMI_PRIO_TX_SECTORS_ORDER_EVENTID
 */
struct wmi_prio_tx_sectors_order_cmd {
	/* tx sectors order to be applied, 0xFF for end of array */
	u8 tx_sectors_priority_array[MAX_NUM_OF_SECTORS];
	/* enum wmi_sector_sweep_type, TXSS and/or Beacon */
	u8 sector_sweep_type;
	/* needed only for TXSS configuration */
	u8 cid;
	/* alignment to 32b */
	u8 reserved[2];
} __packed;

/* completion status codes */
enum wmi_prio_tx_sectors_cmd_status {
	WMI_PRIO_TX_SECT_CMD_STATUS_SUCCESS	= 0x00,
	WMI_PRIO_TX_SECT_CMD_STATUS_BAD_PARAM	= 0x01,
	/* other error */
	WMI_PRIO_TX_SECT_CMD_STATUS_ERROR	= 0x02,
};

/* WMI_PRIO_TX_SECTORS_ORDER_EVENTID */
struct wmi_prio_tx_sectors_order_event {
	/* enum wmi_prio_tx_sectors_cmd_status */
	u8 status;
	/* alignment to 32b */
	u8 reserved[3];
} __packed;

struct wmi_prio_tx_sectors_num_cmd {
	/* [0-128], 0 = No changes */
	u8 beacon_number_of_sectors;
	/* [0-128], 0 = No changes */
	u8 txss_number_of_sectors;
	/* [0-8] needed only for TXSS configuration */
	u8 cid;
} __packed;

/* WMI_PRIO_TX_SECTORS_NUMBER_CMDID
 *
 * Set the number of active sectors in TXSS and/or Beacon.
 *
 * Returned event:
 * - WMI_PRIO_TX_SECTORS_NUMBER_EVENTID
 */
struct wmi_prio_tx_sectors_number_cmd {
	struct wmi_prio_tx_sectors_num_cmd active_sectors_num;
	/* alignment to 32b */
	u8 reserved;
} __packed;

/* WMI_PRIO_TX_SECTORS_NUMBER_EVENTID */
struct wmi_prio_tx_sectors_number_event {
	/* enum wmi_prio_tx_sectors_cmd_status */
	u8 status;
	/* alignment to 32b */
	u8 reserved[3];
} __packed;

/* WMI_PRIO_TX_SECTORS_SET_DEFAULT_CFG_CMDID
 *
 * Set default sectors order and number (hard coded in board file)
 * in TXSS and/or Beacon.
 *
 * Returned event:
 * - WMI_PRIO_TX_SECTORS_SET_DEFAULT_CFG_EVENTID
 */
struct wmi_prio_tx_sectors_set_default_cfg_cmd {
	/* enum wmi_sector_sweep_type, TXSS and/or Beacon */
	u8 sector_sweep_type;
	/* needed only for TXSS configuration */
	u8 cid;
	/* alignment to 32b */
	u8 reserved[2];
} __packed;

/* WMI_PRIO_TX_SECTORS_SET_DEFAULT_CFG_EVENTID */
struct wmi_prio_tx_sectors_set_default_cfg_event {
	/* enum wmi_prio_tx_sectors_cmd_status */
	u8 status;
	/* alignment to 32b */
	u8 reserved[3];
} __packed;

#endif /* __WILOCITY_WMI_H__ */
