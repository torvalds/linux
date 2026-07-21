/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2026 Morse Micro
 */
#ifndef _MM81X_COMMAND_DEFS_H_
#define _MM81X_COMMAND_DEFS_H_

#include <linux/types.h>

#define __sle16 __le16
#define __sle32 __le32
#define __sle64 __le64

#define HOST_CMD_SEMVER_MAJOR 56
#define HOST_CMD_SEMVER_MINOR 17
#define HOST_CMD_SEMVER_PATCH 0

#define HOST_CMD_TYPE_REQ BIT(0)
#define HOST_CMD_TYPE_RESP BIT(1)
#define HOST_CMD_TYPE_EVT BIT(2)

#define HOST_CMD_SSID_MAX_LEN 32
#define HOST_CMD_MAC_ADDR_LEN 6

enum host_cmd_id {
	HOST_CMD_ID_SET_CHANNEL = 0x0001,
	HOST_CMD_ID_GET_CHANNEL = 0x001D,
	HOST_CMD_ID_GET_CHANNEL_FULL = 0x0013,
	HOST_CMD_ID_GET_CHANNEL_DTIM = 0x001C,
	HOST_CMD_ID_GET_VERSION = 0x0002,
	HOST_CMD_ID_SET_TXPOWER = 0x0003,
	HOST_CMD_ID_GET_MAX_TXPOWER = 0x0024,
	HOST_CMD_ID_ADD_INTERFACE = 0x0004,
	HOST_CMD_ID_REMOVE_INTERFACE = 0x0005,
	HOST_CMD_ID_BSS_CONFIG = 0x0006,
	HOST_CMD_ID_SCAN_CONFIG = 0x0010,
	HOST_CMD_ID_SET_QOS_PARAMS = 0x0011,
	HOST_CMD_ID_GET_QOS_PARAMS = 0x0012,
	HOST_CMD_ID_SET_STA_STATE = 0x0014,
	HOST_CMD_ID_SET_BSS_COLOR = 0x0015,
	HOST_CMD_ID_CONFIG_PS = 0x0016,
	HOST_CMD_ID_HEALTH_CHECK = 0x0019,
	HOST_CMD_ID_CTS_SELF_PS = 0x001A,
	HOST_CMD_ID_DTIM_CHANNEL_ENABLE = 0x001B,
	HOST_CMD_ID_ARP_OFFLOAD = 0x0020,
	HOST_CMD_ID_SET_LONG_SLEEP_CONFIG = 0x0021,
	HOST_CMD_ID_SET_DUTY_CYCLE = 0x0022,
	HOST_CMD_ID_GET_DUTY_CYCLE = 0x0023,
	HOST_CMD_ID_GET_CAPABILITIES = 0x0025,
	HOST_CMD_ID_TWT_AGREEMENT_INSTALL = 0x0026,
	HOST_CMD_ID_TWT_AGREEMENT_VALIDATE = 0x0036,
	HOST_CMD_ID_TWT_AGREEMENT_REMOVE = 0x0027,
	HOST_CMD_ID_GET_TSF = 0x0028,
	HOST_CMD_ID_MAC_ADDR = 0x0029,
	HOST_CMD_ID_MPSW_CONFIG = 0x0030,
	HOST_CMD_ID_INSTALL_KEY = 0x000A,
	HOST_CMD_ID_DISABLE_KEY = 0x000B,
	HOST_CMD_ID_DHCP_OFFLOAD = 0x0032,
	HOST_CMD_ID_SET_KEEP_ALIVE_OFFLOAD = 0x0033,
	HOST_CMD_ID_UPDATE_OUI_FILTER = 0x0034,
	HOST_CMD_ID_IBSS_CONFIG = 0x0035,
	HOST_CMD_ID_OCS = 0x0038,
	HOST_CMD_ID_MESH_CONFIG = 0x0039,
	HOST_CMD_ID_SET_OFFSET_TSF = 0x003A,
	HOST_CMD_ID_GET_CHANNEL_USAGE = 0x003B,
	HOST_CMD_ID_MCAST_FILTER = 0x003C,
	HOST_CMD_ID_BSS_BEACON_CONFIG = 0x003D,
	HOST_CMD_ID_UAPSD_CONFIG = 0x0040,
	HOST_CMD_ID_PAGE_SLICING_CONFIG = 0x0043,
	HOST_CMD_ID_HW_SCAN = 0x0044,
	HOST_CMD_ID_SET_WHITELIST = 0x0045,
	HOST_CMD_ID_ARP_PERIODIC_REFRESH = 0x0046,
	HOST_CMD_ID_SET_TCP_KEEPALIVE = 0x0047,
	HOST_CMD_ID_FORCE_POWER_MODE = 0x0048,
	HOST_CMD_ID_LI_SLEEP = 0x0049,
	HOST_CMD_ID_GET_DISABLED_CHANNELS = 0x004A,
	HOST_CMD_ID_SET_CQM_RSSI = 0x004F,
	HOST_CMD_ID_GET_APF_CAPABILITIES = 0x0050,
	HOST_CMD_ID_READ_WRITE_APF = 0x0051,
	HOST_CMD_ID_BSSID_SET = 0x0052,
	HOST_CMD_ID_BEACON_OFFLOAD = 0x0053,
	HOST_CMD_ID_PROBE_RESPONSE_OFFLOAD = 0x0054,
	HOST_CMD_ID_HOST_STATS_LOG = 0x2007,
	HOST_CMD_ID_HOST_STATS_RESET = 0x2008,
	HOST_CMD_ID_MAC_STATS_LOG = 0x200C,
	HOST_CMD_ID_MAC_STATS_RESET = 0x200D,
	HOST_CMD_ID_UPHY_STATS_LOG = 0x200E,
	HOST_CMD_ID_UPHY_STATS_RESET = 0x200F,
	HOST_CMD_ID_SET_STA_TYPE = 0xA000,
	HOST_CMD_ID_SET_ENC_MODE = 0xA001,
	HOST_CMD_ID_TEST_BA = 0xA002,
	HOST_CMD_ID_SET_LISTEN_INTERVAL = 0xA003,
	HOST_CMD_ID_SET_AMPDU = 0xA004,
	HOST_CMD_ID_COREDUMP = 0xA006,
	HOST_CMD_ID_SET_S1G_OP_CLASS = 0xA007,
	HOST_CMD_ID_SEND_WAKE_ACTION_FRAME = 0xA008,
	HOST_CMD_ID_VENDOR_IE_CONFIG = 0xA009,
	HOST_CMD_ID_SET_TWT_CONF = 0xA010,
	HOST_CMD_ID_GET_AVAILABLE_CHANNELS = 0xA011,
	HOST_CMD_ID_SET_ECSA_S1G_INFO = 0xA012,
	HOST_CMD_ID_GET_HW_VERSION = 0xA013,
	HOST_CMD_ID_CAC = 0xA014,
	HOST_CMD_ID_DRIVER_SET_DUTY_CYCLE = 0xA015,
	HOST_CMD_ID_OCS_DRIVER = 0xA017,
	HOST_CMD_ID_MBSSID = 0xA016,
	HOST_CMD_ID_SET_MESH_CONFIG = 0xA018,
	HOST_CMD_ID_SET_MCBA_CONF = 0xA019,
	HOST_CMD_ID_DYNAMIC_PEERING_CONFIG = 0xA020,
	HOST_CMD_ID_CONFIG_RAW = 0xA021,
	HOST_CMD_ID_CONFIG_BSS_STATS = 0xA022,
	HOST_CMD_ID_GET_RSSI = 0x1002,
	HOST_CMD_ID_SET_IFS = 0x1003,
	HOST_CMD_ID_SET_FEM_SETTINGS = 0x1005,
	HOST_CMD_ID_SET_TXOP = 0x1008,
	HOST_CMD_ID_SET_CONTROL_RESPONSE = 0x1009,
	HOST_CMD_ID_SET_PERIODIC_CAL = 0x100A,
	HOST_CMD_ID_SET_BCN_RSSI_THRESHOLD = 0x100B,
	HOST_CMD_ID_SET_TX_PKT_LIFETIME_USECS = 0x100C,
	HOST_CMD_ID_SET_PHYSM_WATCHDOG = 0x100D,
	HOST_CMD_ID_TX_POLAR = 0x100E,
	HOST_CMD_ID_EVT_STA_STATE = 0x4001,
	HOST_CMD_ID_EVT_BEACON_LOSS = 0x4002,
	HOST_CMD_ID_EVT_SIG_FIELD_ERROR = 0x4003,
	HOST_CMD_ID_EVT_UMAC_TRAFFIC_CONTROL = 0x4004,
	HOST_CMD_ID_EVT_DHCP_LEASE_UPDATE = 0x4005,
	HOST_CMD_ID_EVT_OCS_DONE = 0x4006,
	HOST_CMD_ID_EVT_HW_SCAN_DONE = 0x4011,
	HOST_CMD_ID_EVT_CHANNEL_USAGE = 0x4012,
	HOST_CMD_ID_EVT_CONNECTION_LOSS = 0x4013,
	HOST_CMD_ID_EVT_SCHED_SCAN_RESULTS = 0x4014,
	HOST_CMD_ID_EVT_CQM_RSSI_NOTIFY = 0x4015,
	HOST_CMD_ID_EVT_SCAN_DONE = 0x4007,
	HOST_CMD_ID_EVT_SCAN_RESULT = 0x4008,
	HOST_CMD_ID_EVT_CONNECTED = 0x4009,
	HOST_CMD_ID_EVT_DISCONNECTED = 0x4010,
	HOST_CMD_ID_EVT_BEACON_FILTER_MATCH = 0x4016,
	HOST_CMD_ID_SET_CAPABILITIES = 0x8118,
	HOST_CMD_ID_SET_TRANSMISSION_RATE = 0x8009,
	HOST_CMD_ID_FORCE_ASSERT = 0x800E,
	HOST_CMD_ID_GET_SET_GENERIC_PARAM = 0x003E,
};

struct host_cmd_mac_addr {
	u8 octet[HOST_CMD_MAC_ADDR_LEN];
};

enum host_cmd_ocs_subcmd {
	HOST_CMD_OCS_SUBCMD_CONFIG = 1,
	HOST_CMD_OCS_SUBCMD_STATUS = 2,
};

enum host_cmd_headless_cfg_option {
	HOST_CMD_HEADLESS_CFG_OPTION_KEEP_IFACES = BIT(0),
	HOST_CMD_HEADLESS_CFG_OPTION_BUFFER_RX = BIT(1),
	HOST_CMD_HEADLESS_CFG_OPTION_NOTIFY_ON_ANY_RX = BIT(2),
};

struct host_cmd_header {
	__le16 flags;
	__le16 message_id;
	__le16 len;
	__le16 host_id;
	__le16 vif_id;
	__le16 pad;
};

#define HOST_CMD_CHANNEL_BW_NOT_SET 0xFF
#define HOST_CMD_CHANNEL_IDX_NOT_SET 0xFF
#define HOST_CMD_CHANNEL_FREQ_NOT_SET 0xFFFFFFFF

enum host_cmd_dot11_proto_mode {
	HOST_CMD_DOT11_PROTO_MODE_AH = 0,
};

struct host_cmd_req_set_channel {
	struct host_cmd_header hdr;
	__le32 op_chan_freq_hz;
	u8 op_bw_mhz;
	u8 pri_bw_mhz;
	u8 pri_1mhz_chan_idx;
	u8 dot11_mode;
	u8 __deprecated_reg_tx_power_set;
	u8 is_off_channel;
} __packed;

struct host_cmd_resp_set_channel {
	struct host_cmd_header hdr;
	__le32 status;
	__sle32 power_qdbm;
} __packed;

struct host_cmd_req_get_channel {
	struct host_cmd_header hdr;
} __packed;

struct host_cmd_resp_get_channel {
	struct host_cmd_header hdr;
	__le32 status;
	__le32 op_chan_freq_hz;
	u8 op_chan_bw_mhz;
	u8 pri_chan_bw_mhz;
	u8 pri_1mhz_chan_idx;
} __packed;

#define HOST_CMD_MAX_VERSION_LEN 128

struct host_cmd_req_get_version {
	struct host_cmd_header hdr;
} __packed;

struct host_cmd_resp_get_version {
	struct host_cmd_header hdr;
	__le32 status;
	__sle32 length;
	u8 version[];
} __packed;

struct host_cmd_req_set_txpower {
	struct host_cmd_header hdr;
	__sle32 power_qdbm;
} __packed;

struct host_cmd_resp_set_txpower {
	struct host_cmd_header hdr;
	__le32 status;
	__sle32 power_qdbm;
} __packed;

struct host_cmd_req_get_max_txpower {
	struct host_cmd_header hdr;
} __packed;

struct host_cmd_resp_get_max_txpower {
	struct host_cmd_header hdr;
	__le32 status;
	__sle32 power_qdbm;
} __packed;

enum host_cmd_interface_type {
	HOST_CMD_INTERFACE_TYPE_INVALID = 0,
	HOST_CMD_INTERFACE_TYPE_STA = 1,
	HOST_CMD_INTERFACE_TYPE_AP = 2,
	HOST_CMD_INTERFACE_TYPE_MON = 3,
	HOST_CMD_INTERFACE_TYPE_ADHOC = 4,
	HOST_CMD_INTERFACE_TYPE_MESH = 5,
	HOST_CMD_INTERFACE_TYPE_LAST = HOST_CMD_INTERFACE_TYPE_MESH,
};

struct host_cmd_req_add_interface {
	struct host_cmd_header hdr;
	struct host_cmd_mac_addr addr;
	__le32 interface_type;
} __packed;

struct host_cmd_resp_add_interface {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_remove_interface {
	struct host_cmd_header hdr;
} __packed;

struct host_cmd_resp_remove_interface {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_bss_config {
	struct host_cmd_header hdr;
	__le16 beacon_interval_tu;
	__le16 dtim_period;
	u8 __padding[2];
	__le32 cssid;
} __packed;

struct host_cmd_resp_bss_config {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_scan_config {
	struct host_cmd_header hdr;
	u8 enabled;
	u8 is_survey;
} __packed;

struct host_cmd_resp_scan_config {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_set_qos_params {
	struct host_cmd_header hdr;
	u8 uapsd;
	u8 queue_idx;
	u8 aifs_slot_count;
	__le16 contention_window_min;
	__le16 contention_window_max;
	__le32 max_txop_usec;
} __packed;

struct host_cmd_resp_set_qos_params {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_get_qos_params {
	struct host_cmd_header hdr;
	u8 queue_idx;
} __packed;

struct host_cmd_resp_get_qos_params {
	struct host_cmd_header hdr;
	__le32 status;
	u8 aifs_slot_count;
	__le16 contention_window_min;
	__le16 contention_window_max;
	__le32 max_txop_usec;
} __packed;

struct host_cmd_req_set_sta_state {
	struct host_cmd_header hdr;
	u8 sta_addr[HOST_CMD_MAC_ADDR_LEN];
	__le16 aid;
	__le16 state;
	u8 uapsd_queues;
	__le32 flags;
} __packed;

struct host_cmd_resp_set_sta_state {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_set_bss_color {
	struct host_cmd_header hdr;
	u8 bss_color;
} __packed;

struct host_cmd_resp_set_bss_color {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_config_ps {
	struct host_cmd_header hdr;
	u8 enabled;
	u8 dynamic_ps_offload;
} __packed;

struct host_cmd_resp_config_ps {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_health_check {
	struct host_cmd_header hdr;
} __packed;

struct host_cmd_resp_health_check {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_cts_self_ps {
	struct host_cmd_header hdr;
	u8 enable;
} __packed;

struct host_cmd_resp_cts_self_ps {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_dtim_channel_enable {
	struct host_cmd_header hdr;
	u8 enable;
} __packed;

struct host_cmd_resp_dtim_channel_enable {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

#define HOST_CMD_ARP_OFFLOAD_MAX_IP_ADDRESSES 4

struct host_cmd_req_arp_offload {
	struct host_cmd_header hdr;
	__be32 ip_table[HOST_CMD_ARP_OFFLOAD_MAX_IP_ADDRESSES];
} __packed;

struct host_cmd_resp_arp_offload {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_set_long_sleep_config {
	struct host_cmd_header hdr;
	u8 enabled;
} __packed;

struct host_cmd_resp_set_long_sleep_config {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

#define HOST_CMD_DUTY_CYCLE_SET_CFG_DUTY_CYCLE BIT(0)
#define HOST_CMD_DUTY_CYCLE_SET_CFG_OMIT_CONTROL_RESP BIT(1)
#define HOST_CMD_DUTY_CYCLE_SET_CFG_EXT BIT(2)
#define HOST_CMD_DUTY_CYCLE_SET_CFG_BURST_RECORD_UNIT BIT(3)

enum host_cmd_duty_cycle_mode {
	HOST_CMD_DUTY_CYCLE_MODE_SPREAD = 0,
	HOST_CMD_DUTY_CYCLE_MODE_BURST = 1,
	HOST_CMD_DUTY_CYCLE_MODE_LAST = HOST_CMD_DUTY_CYCLE_MODE_BURST,
};

struct host_cmd_duty_cycle_configuration {
	u8 omit_control_responses;
	__le32 duty_cycle;
} __packed;

struct host_cmd_duty_cycle_set_configuration_ext {
	__le32 burst_record_unit_us;
	u8 mode;
} __packed;

struct host_cmd_duty_cycle_configuration_ext {
	__le32 airtime_remaining_us;
	__le32 burst_window_duration_us;
	struct host_cmd_duty_cycle_set_configuration_ext set;
} __packed;

struct host_cmd_req_set_duty_cycle {
	struct host_cmd_header hdr;
	struct host_cmd_duty_cycle_configuration config;
	u8 set_cfgs;
	struct host_cmd_duty_cycle_set_configuration_ext config_ext;
} __packed;

struct host_cmd_resp_get_duty_cycle {
	struct host_cmd_header hdr;
	__le32 status;
	struct host_cmd_duty_cycle_configuration config;
	struct host_cmd_duty_cycle_configuration_ext config_ext;
} __packed;

#define HOST_CMD_SET_S1G_CAP_FLAGS BIT(0)
#define HOST_CMD_SET_S1G_CAP_AMPDU_MSS BIT(1)
#define HOST_CMD_SET_S1G_CAP_BEAM_STS BIT(2)
#define HOST_CMD_SET_S1G_CAP_NUM_SOUND_DIMS BIT(3)
#define HOST_CMD_SET_S1G_CAP_MAX_AMPDU_LEXP BIT(4)
#define HOST_CMD_SET_MORSE_CAP_MMSS_OFFSET BIT(5)
#define HOST_CMD_S1G_CAPABILITY_FLAGS_WIDTH 4

struct host_cmd_mm_capabilities {
	__le32 flags[HOST_CMD_S1G_CAPABILITY_FLAGS_WIDTH];
	u8 ampdu_mss;
	u8 beamformee_sts_capability;
	u8 number_sounding_dimensions;
	u8 maximum_ampdu_length_exponent;
} __packed;

struct host_cmd_req_get_capabilities {
	struct host_cmd_header hdr;
} __packed;

struct host_cmd_resp_get_capabilities {
	struct host_cmd_header hdr;
	__le32 status;
	struct host_cmd_mm_capabilities capabilities;
	u8 morse_mmss_offset;
} __packed;

#define HOST_CMD_DOT11_TWT_AGREEMENT_MAX_LEN 20

struct host_cmd_req_twt_agreement_install {
	struct host_cmd_header hdr;
	u8 flow_id;
	u8 agreement_len;
	u8 agreement[HOST_CMD_DOT11_TWT_AGREEMENT_MAX_LEN];
} __packed;

struct host_cmd_resp_twt_agreement_install {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_twt_agreement_validate {
	struct host_cmd_header hdr;
	u8 flow_id;
	u8 agreement_len;
	u8 agreement[HOST_CMD_DOT11_TWT_AGREEMENT_MAX_LEN];
} __packed;

struct host_cmd_resp_twt_agreement_validate {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_twt_agreement_remove {
	struct host_cmd_header hdr;
	u8 flow_id;
} __packed;

struct host_cmd_req_get_tsf {
	struct host_cmd_header hdr;
} __packed;

struct host_cmd_resp_get_tsf {
	struct host_cmd_header hdr;
	__le32 status;
	__le64 now_tsf;
	__le64 now_chip_ts;
} __packed;

struct host_cmd_req_mac_addr {
	struct host_cmd_header hdr;
	u8 write;
	u8 octet[HOST_CMD_MAC_ADDR_LEN];
} __packed;

struct host_cmd_resp_mac_addr {
	struct host_cmd_header hdr;
	__le32 status;
	u8 octet[HOST_CMD_MAC_ADDR_LEN];
} __packed;

#define HOST_CMD_SET_MPSW_CFG_AIRTIME_BOUNDS BIT(0)
#define HOST_CMD_SET_MPSW_CFG_PKT_SPC_WIN_LEN BIT(1)
#define HOST_CMD_SET_MPSW_CFG_ENABLED BIT(2)

struct host_cmd_mpsw_configuration {
	__le32 airtime_max_us;
	__le32 airtime_min_us;
	__le32 packet_space_window_length_us;
	u8 enable;
} __packed;

struct host_cmd_req_mpsw_config {
	struct host_cmd_header hdr;
	struct host_cmd_mpsw_configuration config;
	u8 set_cfgs;
} __packed;

struct host_cmd_resp_mpsw_config {
	struct host_cmd_header hdr;
	__le32 status;
	struct host_cmd_mpsw_configuration config;
} __packed;

#define HOST_CMD_MAX_KEY_LEN 32

enum host_cmd_key_cipher {
	HOST_CMD_KEY_CIPHER_INVALID = 0,
	HOST_CMD_KEY_CIPHER_AES_CCM = 1,
	HOST_CMD_KEY_CIPHER_AES_GCM = 2,
	HOST_CMD_KEY_CIPHER_AES_CMAC = 3,
	HOST_CMD_KEY_CIPHER_AES_GMAC = 4,
	HOST_CMD_KEY_CIPHER_LAST = HOST_CMD_KEY_CIPHER_AES_GMAC,
};

enum host_cmd_aes_key_len {
	HOST_CMD_AES_KEY_LEN_INVALID = 0,
	HOST_CMD_AES_KEY_LEN_LENGTH_128 = 1,
	HOST_CMD_AES_KEY_LEN_LENGTH_256 = 2,
	HOST_CMD_AES_KEY_LEN_LENGTH_LAST = HOST_CMD_AES_KEY_LEN_LENGTH_256,
};

enum host_cmd_temporal_key_type {
	HOST_CMD_TEMPORAL_KEY_TYPE_INVALID = 0,
	HOST_CMD_TEMPORAL_KEY_TYPE_GTK = 1,
	HOST_CMD_TEMPORAL_KEY_TYPE_PTK = 2,
	HOST_CMD_TEMPORAL_KEY_TYPE_IGTK = 3,
	HOST_CMD_TEMPORAL_KEY_TYPE_LAST = HOST_CMD_TEMPORAL_KEY_TYPE_IGTK,
};

struct host_cmd_req_install_key {
	struct host_cmd_header hdr;
	__le64 pn;
	__le32 aid;
	u8 key_idx;
	u8 cipher;
	u8 key_length;
	u8 key_type;
	u8 __padding[2];
	u8 key[HOST_CMD_MAX_KEY_LEN];
} __packed;

struct host_cmd_resp_install_key {
	struct host_cmd_header hdr;
	__le32 status;
	u8 key_idx;
} __packed;

struct host_cmd_req_disable_key {
	struct host_cmd_header hdr;
	__le32 key_type;
	__le32 aid;
	u8 key_idx;
} __packed;

struct host_cmd_resp_disable_key {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

enum host_cmd_dhcp_opcode {
	HOST_CMD_DHCP_OPCODE_ENABLE = 0,
	HOST_CMD_DHCP_OPCODE_DO_DISCOVERY = 1,
	HOST_CMD_DHCP_OPCODE_GET_LEASE = 2,
	HOST_CMD_DHCP_OPCODE_CLEAR_LEASE = 3,
	HOST_CMD_DHCP_OPCODE_RENEW_LEASE = 4,
	HOST_CMD_DHCP_OPCODE_REBIND_LEASE = 5,
	HOST_CMD_DHCP_OPCODE_SEND_LEASE_UPDATE = 6,
};

enum host_cmd_dhcp_retcode {
	HOST_CMD_DHCP_RETCODE_SUCCESS = 0,
	HOST_CMD_DHCP_RETCODE_NOT_ENABLED = 1,
	HOST_CMD_DHCP_RETCODE_ALREADY_ENABLED = 2,
	HOST_CMD_DHCP_RETCODE_NO_LEASE = 3,
	HOST_CMD_DHCP_RETCODE_HAVE_LEASE = 4,
	HOST_CMD_DHCP_RETCODE_BUSY = 5,
	HOST_CMD_DHCP_RETCODE_BAD_VIF = 6,
};

struct host_cmd_req_dhcp_offload {
	struct host_cmd_header hdr;
	__le32 opcode;
} __packed;

struct host_cmd_resp_dhcp_offload {
	struct host_cmd_header hdr;
	__le32 status;
	__le32 retcode;
	__le32 my_ip;
	__le32 netmask;
	__le32 router;
	__le32 dns;
} __packed;

struct host_cmd_req_set_keep_alive_offload {
	struct host_cmd_header hdr;
	__le16 bss_max_idle_period;
	u8 interpret_as_11ah;
} __packed;

#define HOST_CMD_MAX_OUI_FILTERS 5
#define HOST_CMD_OUI_SIZE 3
#define HOST_CMD_MAX_OUI_FILTER_ARRAY_SIZE 15

struct host_cmd_req_update_oui_filter {
	struct host_cmd_header hdr;
	u8 n_ouis;
	u8 ouis[HOST_CMD_MAX_OUI_FILTERS][HOST_CMD_OUI_SIZE];
} __packed;

enum host_cmd_ibss_config_opcode {
	HOST_CMD_IBSS_CONFIG_OPCODE_CREATE = 0,
	HOST_CMD_IBSS_CONFIG_OPCODE_JOIN = 1,
	HOST_CMD_IBSS_CONFIG_OPCODE_STOP = 2,
};

struct host_cmd_req_ibss_config {
	struct host_cmd_header hdr;
	u8 ibss_bssid[HOST_CMD_MAC_ADDR_LEN];
	u8 ibss_cfg_opcode;
	u8 ibss_probe_filtering;
} __packed;

enum host_cmd_ocs_type {
	HOST_CMD_OCS_TYPE_QNULL = 0,
	HOST_CMD_OCS_TYPE_RAW = 1,
};

struct host_cmd_ocs_config_req {
	__le32 op_channel_freq_hz;
	u8 op_channel_bw_mhz;
	u8 pri_channel_bw_mhz;
	u8 pri_1mhz_channel_index;
	__le16 aid;
	u8 type;
} __packed;

struct host_cmd_ocs_status_resp {
	u8 running;
} __packed;

struct host_cmd_req_ocs {
	struct host_cmd_header hdr;
	__le32 subcmd;
	union {
		u8 opaque[0];
		struct host_cmd_ocs_config_req config;
	};
} __packed;

struct host_cmd_resp_ocs {
	struct host_cmd_header hdr;
	__le32 status;
	__le32 subcmd;
	union {
		u8 opaque[0];
		struct host_cmd_ocs_status_resp ocs_status;
	};
} __packed;

enum host_cmd_mesh_config_opcode {
	HOST_CMD_MESH_CONFIG_OPCODE_START = 0,
	HOST_CMD_MESH_CONFIG_OPCODE_STOP = 1,
};

struct host_cmd_req_mesh_config {
	struct host_cmd_header hdr;
	u8 mesh_cfg_opcode;
	u8 enable_beaconing;
	u8 mbca_config;
	u8 min_beacon_gap_ms;
	__le16 mbss_start_scan_duration_ms;
	__le16 tbtt_adj_timer_interval_ms;
} __packed;

struct host_cmd_req_set_offset_tsf {
	struct host_cmd_header hdr;
	__sle64 offset_tsf;
} __packed;

struct host_cmd_req_get_channel_usage {
	struct host_cmd_header hdr;
} __packed;

struct host_cmd_resp_get_channel_usage {
	struct host_cmd_header hdr;
	__le32 status;
	__le64 time_listen;
	__le64 busy_time;
	__le32 freq_hz;
	s8 noise;
	u8 bw_mhz;
} __packed;

#define HOST_CMD_MAX_MCAST_FILTERS 12

struct host_cmd_req_mcast_filter {
	struct host_cmd_header hdr;
	u8 count;
	__le32 hw_addr[];
} __packed;

struct host_cmd_req_bss_beacon_config {
	struct host_cmd_header hdr;
	u8 enable;
} __packed;

struct host_cmd_resp_bss_beacon_config {
	struct host_cmd_header hdr;
	__le32 status;
	__le16 interface_id;
} __packed;

struct host_cmd_req_uapsd_config {
	struct host_cmd_header hdr;
	u8 auto_trigger_enabled;
	__le32 auto_trigger_timeout;
} __packed;

struct host_cmd_resp_uapsd_config {
	struct host_cmd_header hdr;
	__le32 status;
	u8 auto_trigger_enabled;
} __packed;

struct host_cmd_req_page_slicing_config {
	struct host_cmd_header hdr;
	u8 enable;
} __packed;

#define HOST_CMD_HW_SCAN_FLAGS_START BIT(0)
#define HOST_CMD_HW_SCAN_FLAGS_ABORT BIT(1)
#define HOST_CMD_HW_SCAN_FLAGS_SURVEY BIT(2)
#define HOST_CMD_HW_SCAN_FLAGS_STORE BIT(3)
#define HOST_CMD_HW_SCAN_FLAGS_1MHZ_PROBES BIT(4)
#define HOST_CMD_HW_SCAN_FLAGS_SCHED_START BIT(5)
#define HOST_CMD_HW_SCAN_FLAGS_SCHED_STOP BIT(6)
#define HOST_CMD_HW_SCAN_FLAGS_PROBE_ON_DOZE_BEACON BIT(7)

enum host_cmd_hw_scan_tlv_tag {
	HOST_CMD_HW_SCAN_TLV_TAG_PAD = 0,
	HOST_CMD_HW_SCAN_TLV_TAG_PROBE_REQ = 1,
	HOST_CMD_HW_SCAN_TLV_TAG_CHAN_LIST = 2,
	HOST_CMD_HW_SCAN_TLV_TAG_POWER_LIST = 3,
	HOST_CMD_HW_SCAN_TLV_TAG_DWELL_ON_HOME = 4,
	HOST_CMD_HW_SCAN_TLV_TAG_SCHED = 5,
	HOST_CMD_HW_SCAN_TLV_TAG_FILTER = 6,
	HOST_CMD_HW_SCAN_TLV_TAG_SCHED_PARAMS = 7,
};

struct host_cmd_hw_scan_tlv {
	__le16 tag;
	__le16 len;
	u8 value[];
} __packed;

struct host_cmd_req_hw_scan {
	struct host_cmd_header hdr;
	__le32 flags;
	__le32 dwell_time_ms;
	u8 variable[];
} __packed;

#define HOST_CMD_WHITELIST_FLAGS_CLEAR BIT(0)

struct host_cmd_req_set_whitelist {
	struct host_cmd_header hdr;
	u8 flags;
	u8 ip_protocol;
	__be16 llc_protocol;
	__be32 src_ip;
	__be32 dest_ip;
	__be32 netmask;
	__be16 src_port;
	__be16 dest_port;
} __packed;

struct host_cmd_arp_periodic_params {
	__le32 refresh_period_s;
	__le32 destination_ip;
	u8 send_as_garp;
} __packed;

struct host_cmd_req_arp_periodic_refresh {
	struct host_cmd_header hdr;
	struct host_cmd_arp_periodic_params config;
} __packed;

#define HOST_CMD_TCP_KEEPALIVE_SET_CFG_PERIOD BIT(0)
#define HOST_CMD_TCP_KEEPALIVE_SET_CFG_RETRY_COUNT BIT(1)
#define HOST_CMD_TCP_KEEPALIVE_SET_CFG_RETRY_INTERVAL BIT(2)
#define HOST_CMD_TCP_KEEPALIVE_SET_CFG_SRC_IP_ADDR BIT(3)
#define HOST_CMD_TCP_KEEPALIVE_SET_CFG_DEST_IP_ADDR BIT(4)
#define HOST_CMD_TCP_KEEPALIVE_SET_CFG_SRC_PORT BIT(5)
#define HOST_CMD_TCP_KEEPALIVE_SET_CFG_DEST_PORT BIT(6)

struct host_cmd_req_set_tcp_keepalive {
	struct host_cmd_header hdr;
	u8 enabled;
	u8 retry_count;
	u8 retry_interval_s;
	u8 set_cfgs;
	__be32 src_ip;
	__be32 dest_ip;
	__be16 src_port;
	__be16 dest_port;
	__le16 period_s;
} __packed;

enum host_cmd_power_mode {
	HOST_CMD_POWER_MODE_SNOOZE = 0,
	HOST_CMD_POWER_MODE_DEEP_SLEEP = 1,
	HOST_CMD_POWER_MODE_HIBERNATE = 2,
};

struct host_cmd_req_force_power_mode {
	struct host_cmd_header hdr;
	__le32 mode;
} __packed;

struct host_cmd_req_li_sleep {
	struct host_cmd_header hdr;
	__le32 listen_interval;
} __packed;

struct host_cmd_disabled_channel_entry {
	__le16 freq_100khz;
	u8 bw_mhz;
} __packed;

struct host_cmd_resp_get_disabled_channels {
	struct host_cmd_header hdr;
	__le32 status;
	__le32 n_channels;
	struct host_cmd_disabled_channel_entry channels[];
} __packed;

struct host_cmd_req_set_cqm_rssi {
	struct host_cmd_header hdr;
	__sle32 threshold;
	__le32 hysteresis;
} __packed;

struct host_cmd_req_get_apf_capabilities {
	struct host_cmd_header hdr;
} __packed;

struct host_cmd_resp_get_apf_capabilities {
	struct host_cmd_header hdr;
	__le32 status;
	__le32 max_length;
	u8 version;
} __packed;

struct host_cmd_req_read_write_apf {
	struct host_cmd_header hdr;
	__le32 offset;
	__le16 program_length;
	u8 write;
	u8 program[];
} __packed;

struct host_cmd_resp_read_write_apf {
	struct host_cmd_header hdr;
	__le32 status;
	__le16 program_length;
	u8 program[];
} __packed;

struct host_cmd_req_bssid_set {
	struct host_cmd_header hdr;
	struct host_cmd_mac_addr bssid;
} __packed;

#define HOST_CMD_BEACON_OFFLOAD_FLAGS_START BIT(0)
#define HOST_CMD_BEACON_OFFLOAD_FLAGS_STOP BIT(1)
#define HOST_CMD_BEACON_OFFLOAD_CSSID_LEN 4

enum host_cmd_beacon_offload_tlv_tag {
	HOST_CMD_BEACON_OFFLOAD_TLV_TAG_DTIM_CNT = 0,
	HOST_CMD_BEACON_OFFLOAD_TLV_TAG_FRAME_CTRL = 1,
	HOST_CMD_BEACON_OFFLOAD_TLV_TAG_CHANGE_SEQ = 2,
	HOST_CMD_BEACON_OFFLOAD_TLV_TAG_CSSID = 3,
	HOST_CMD_BEACON_OFFLOAD_TLV_TAG_IES = 4,
	HOST_CMD_BEACON_OFFLOAD_TLV_TAG_TX_INFO = 5,
};

struct host_cmd_beacon_offload_tlv_hdr {
	__le16 tag;
	__le16 len;
} __packed;

struct host_cmd_beacon_offload_tlv_generic {
	struct host_cmd_beacon_offload_tlv_hdr hdr;
	u8 value[];
} __packed;

struct host_cmd_beacon_offload_tlv_dtim_cnt {
	struct host_cmd_beacon_offload_tlv_hdr hdr;
	__le16 dtim_cnt;
} __packed;

struct host_cmd_beacon_offload_tlv_frame_ctrl {
	struct host_cmd_beacon_offload_tlv_hdr hdr;
	u8 frame_ctrl[2];
} __packed;

struct host_cmd_beacon_offload_tlv_change_seq {
	struct host_cmd_beacon_offload_tlv_hdr hdr;
	__le16 change_seq;
} __packed;

struct host_cmd_beacon_offload_tlv_tx_info {
	struct host_cmd_beacon_offload_tlv_hdr hdr;
	u8 bw_mhz;
} __packed;

struct host_cmd_beacon_offload_tlv_cssid {
	struct host_cmd_beacon_offload_tlv_hdr hdr;
	u8 cssid[HOST_CMD_BEACON_OFFLOAD_CSSID_LEN];
} __packed;

struct host_cmd_beacon_offload_tlv_ies {
	struct host_cmd_beacon_offload_tlv_hdr hdr;
	u8 buf[];
} __packed;

struct host_cmd_req_beacon_offload {
	struct host_cmd_header hdr;
	__le32 flags;
	u8 variable[];
} __packed;

struct host_cmd_resp_beacon_offload {
	struct host_cmd_header hdr;
	__le32 status;
	__le16 dtim_count;
} __packed;

struct host_cmd_req_probe_response_offload {
	struct host_cmd_header hdr;
	u8 enable;
	__le16 probe_resp_len;
	u8 probe_resp_buf[];
} __packed;

struct host_cmd_resp_probe_response_offload {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_set_sta_type {
	struct host_cmd_header hdr;
	u8 sta_type;
} __packed;

struct host_cmd_req_set_enc_mode {
	struct host_cmd_header hdr;
	u8 enc_mode;
} __packed;

struct host_cmd_req_test_ba {
	struct host_cmd_header hdr;
	u8 addr[HOST_CMD_MAC_ADDR_LEN];
	u8 start;
	u8 tx;
	__le32 tid;
} __packed;

struct host_cmd_req_set_listen_interval {
	struct host_cmd_header hdr;
	__le16 listen_interval;
} __packed;

struct host_cmd_req_set_ampdu {
	struct host_cmd_header hdr;
	u8 ampdu_enabled;
} __packed;

struct host_cmd_req_set_s1g_op_class {
	struct host_cmd_header hdr;
	u8 opclass;
	u8 prim_opclass;
} __packed;

struct host_cmd_req_send_wake_action_frame {
	struct host_cmd_header hdr;
	u8 dest_addr[HOST_CMD_MAC_ADDR_LEN];
	__le32 payload_size;
	u8 payload[];
} __packed;

#define HOST_CMD_MAX_VENDOR_IE_LENGTH 255
#define HOST_CMD_VENDOR_IE_TYPE_FLAG_BEACON BIT(0)
#define HOST_CMD_VENDOR_IE_TYPE_FLAG_PROBE_REQ BIT(1)
#define HOST_CMD_VENDOR_IE_TYPE_FLAG_PROBE_RESP BIT(2)
#define HOST_CMD_VENDOR_IE_TYPE_FLAG_ASSOC_REQ BIT(3)
#define HOST_CMD_VENDOR_IE_TYPE_FLAG_ASSOC_RESP BIT(4)

enum host_cmd_vendor_ie_op {
	HOST_CMD_VENDOR_IE_OP_ADD_ELEMENT = 0,
	HOST_CMD_VENDOR_IE_OP_CLEAR_ELEMENTS = 1,
	HOST_CMD_VENDOR_IE_OP_ADD_FILTER = 2,
	HOST_CMD_VENDOR_IE_OP_CLEAR_FILTERS = 3,
	HOST_CMD_VENDOR_IE_OP_INVALID = U16_MAX,
};

struct host_cmd_req_vendor_ie_config {
	struct host_cmd_header hdr;
	__le16 opcode;
	__le16 mgmt_type_mask;
	u8 data[HOST_CMD_MAX_VENDOR_IE_LENGTH];
} __packed;

struct host_cmd_resp_vendor_ie_config {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

enum host_cmd_twt_conf_op {
	HOST_CMD_TWT_CONF_OP_CONFIGURE = 0,
	HOST_CMD_TWT_CONF_OP_FORCE_INSTALL_AGREEMENT = 1,
	HOST_CMD_TWT_CONF_OP_REMOVE_AGREEMENT = 2,
	HOST_CMD_TWT_CONF_OP_CONFIGURE_EXPLICIT = 3,
};

struct host_cmd_explicit_twt_wake_interval {
	__le16 wake_interval_mantissa;
	u8 wake_interval_exponent;
	u8 __padding[5];
} __packed;

union host_cmd_wake_interval {
	__le64 wake_interval_us;
	struct host_cmd_explicit_twt_wake_interval explicit_twt;
} __packed;

struct host_cmd_req_set_twt_conf {
	struct host_cmd_header hdr;
	u8 opcode;
	u8 flow_id;
	__le64 target_wake_time;
	union host_cmd_wake_interval wake_interval;
	__le32 wake_duration_us;
	u8 twt_setup_command;
	u8 __padding[3];
} __packed;

#define HOST_CMD_MAX_AVAILABLE_CHANNELS 255

struct host_cmd_channel_info {
	__le32 frequency_khz;
	u8 channel_5g;
	u8 channel_s1g;
	u8 bandwidth_mhz;
} __packed;

struct host_cmd_resp_get_available_channels {
	struct host_cmd_header hdr;
	__le32 status;
	__le32 num_channels;
	struct host_cmd_channel_info channels[HOST_CMD_MAX_AVAILABLE_CHANNELS];
} __packed;

#define HOST_CMD_S1G_CAP0_S1G_LONG BIT(0)
#define HOST_CMD_S1G_CAP0_SGI_1MHZ BIT(1)
#define HOST_CMD_S1G_CAP0_SGI_2MHZ BIT(2)
#define HOST_CMD_S1G_CAP0_SGI_4MHZ BIT(3)
#define HOST_CMD_S1G_CAP0_SGI_8MHZ BIT(4)
#define HOST_CMD_S1G_CAP0_SGI_16MHZ BIT(5)

struct host_cmd_req_set_ecsa_s1g_info {
	struct host_cmd_header hdr;
	__le32 operating_channel_freq_hz;
	u8 opclass;
	u8 primary_channel_bw_mhz;
	u8 prim_1mhz_ch_idx;
	u8 operating_channel_bw_mhz;
	u8 prim_opclass;
	u8 s1g_cap0;
	u8 s1g_cap1;
	u8 s1g_cap2;
	u8 s1g_cap3;
} __packed;

struct host_cmd_resp_get_hw_version {
	struct host_cmd_header hdr;
	__le32 status;
	u8 hw_version[64];
} __packed;

#define HOST_CMD_CAC_CFG_CHANGE_RULE_MAX 8
#define HOST_CMD_CAC_CFG_ARFS_MAX 99
#define HOST_CMD_CAC_CFG_CHANGE_MAX 99
#define HOST_CMD_CAC_CFG_CHANGE_STEP 5

enum host_cmd_cac_op {
	HOST_CMD_CAC_OP_DISABLE = 0,
	HOST_CMD_CAC_OP_ENABLE = 1,
	HOST_CMD_CAC_OP_CFG_GET = 2,
	HOST_CMD_CAC_OP_CFG_SET = 3,
};

struct host_cmd_cac_change_rule {
	__le16 arfs;
	__sle16 threshold_change;
} __packed;

struct host_cmd_req_cac {
	struct host_cmd_header hdr;
	u8 opcode;
	u8 rule_tot;
	struct host_cmd_cac_change_rule rule[HOST_CMD_CAC_CFG_CHANGE_RULE_MAX];
} __packed;

struct host_cmd_resp_cac {
	struct host_cmd_header hdr;
	__le32 status;
	u8 rule_tot;
	struct host_cmd_cac_change_rule rule[HOST_CMD_CAC_CFG_CHANGE_RULE_MAX];
} __packed;

struct host_cmd_ocs_driver_req {
	__le32 op_channel_freq_hz;
	u8 op_channel_bw_mhz;
	u8 pri_channel_bw_mhz;
	u8 pri_1mhz_channel_index;
} __packed;

struct host_cmd_ocs_driver_resp {
	u8 running;
} __packed;

struct host_cmd_req_ocs_driver {
	struct host_cmd_header hdr;
	__le32 subcmd;
	union {
		u8 opaque[0];
		struct host_cmd_ocs_driver_req config;
	};
} __packed;

struct host_cmd_resp_ocs_driver {
	struct host_cmd_header hdr;
	__le32 status;
	__le32 subcmd;
	union {
		u8 opaque[0];
		struct host_cmd_ocs_driver_resp ocs_status;
	};
} __packed;

#define HOST_CMD_IFNAMSIZ 16

struct host_cmd_req_mbssid {
	struct host_cmd_header hdr;
	u8 max_bssid_indicator;
	s8 transmitter_iface[HOST_CMD_IFNAMSIZ];
} __packed;

#define HOST_CMD_MESH_ID_LEN_MAX 32
#define HOST_CMD_MESH_BEACONLESS_MODE_DISABLE 0
#define HOST_CMD_MESH_BEACONLESS_MODE_ENABLE 1
#define HOST_CMD_MESH_PEER_LINKS_MIN 0
#define HOST_CMD_MESH_PEER_LINKS_MAX 10

struct host_cmd_req_set_mesh_config {
	struct host_cmd_header hdr;
	u8 mesh_id_len;
	u8 mesh_id[HOST_CMD_MESH_ID_LEN_MAX];
	u8 mesh_beaconless_mode;
	u8 max_plinks;
} __packed;

struct host_cmd_req_set_mcba_conf {
	struct host_cmd_header hdr;
	u8 mbca_config;
	u8 beacon_timing_report_interval;
	u8 min_beacon_gap_ms;
	__le16 mbss_start_scan_duration_ms;
	__le16 tbtt_adj_interval_ms;
} __packed;

struct host_cmd_req_dynamic_peering_config {
	struct host_cmd_header hdr;
	u8 enabled;
	u8 rssi_margin;
	__le32 blacklist_timeout;
} __packed;

#define HOST_CMD_CFG_RAW_FLAG_ENABLE BIT(0)
#define HOST_CMD_CFG_RAW_FLAG_DELETE BIT(1)
#define HOST_CMD_CFG_RAW_FLAG_UPDATE BIT(2)
#define HOST_CMD_CFG_RAW_FLAG_DYNAMIC BIT(3)
#define HOST_CMD_RAW_RESERVED_AID_DCS 2008
#define HOST_CMD_RAW_RESERVED_AID_DOWNLINK 2009

enum host_cmd_raw_tlv_tag {
	HOST_CMD_RAW_TLV_TAG_SLOT_DEF = 0,
	HOST_CMD_RAW_TLV_TAG_GROUP = 1,
	HOST_CMD_RAW_TLV_TAG_START_TIME = 2,
	HOST_CMD_RAW_TLV_TAG_PRAW = 3,
	HOST_CMD_RAW_TLV_TAG_BCN_SPREAD = 4,
	HOST_CMD_RAW_TLV_TAG_DYN_GLOBAL = 5,
	HOST_CMD_RAW_TLV_TAG_DYN_CONFIG = 6,
	HOST_CMD_RAW_TLV_TAG_LAST = 7,
};

struct host_cmd_raw_tlv_slot_def {
	u8 tag;
	__le32 raw_duration_us;
	u8 num_slots;
	u8 cross_slot_bleed;
} __packed;

struct host_cmd_raw_tlv_group {
	u8 tag;
	__le16 aid_start;
	__le16 aid_end;
} __packed;

struct host_cmd_raw_tlv_start_time {
	u8 tag;
	__le32 start_time_us;
} __packed;

struct host_cmd_raw_tlv_praw {
	u8 tag;
	u8 periodicity;
	u8 validity;
	u8 start_offset;
	u8 refresh_on_expiry;
} __packed;

struct host_cmd_raw_tlv_bcn_spread {
	u8 tag;
	__le16 max_spread;
	__le16 nominal_sta_per_bcn;
} __packed;

struct host_cmd_raw_tlv_dyn_global {
	u8 tag;
	__le16 num_configs;
	__le16 num_bcn_indexes;
} __packed;

struct host_cmd_raw_tlv_dyn_config {
	u8 tag;
	__le16 id;
	__le16 index;
	__le16 len;
	u8 variable[];
} __packed;

union host_cmd_raw_tlvs {
	u8 tag;
	struct host_cmd_raw_tlv_slot_def slot_def;
	struct host_cmd_raw_tlv_group group;
	struct host_cmd_raw_tlv_start_time start_time;
	struct host_cmd_raw_tlv_praw praw;
	struct host_cmd_raw_tlv_bcn_spread bcn_spread;
	struct host_cmd_raw_tlv_dyn_global dyn_global;
	struct host_cmd_raw_tlv_dyn_config dyn_config;
} __packed;

struct host_cmd_req_config_raw {
	struct host_cmd_header hdr;
	__le32 flags;
	__le16 id;
	u8 variable[];
} __packed;

struct host_cmd_req_config_bss_stats {
	struct host_cmd_header hdr;
	u8 enable;
	__le32 monitor_window_ms;
} __packed;

struct host_cmd_req_get_rssi {
	struct host_cmd_header hdr;
} __packed;

struct host_cmd_resp_get_rssi {
	struct host_cmd_header hdr;
	__le32 status;
	__sle32 rssi0;
	__sle32 rssi1;
	__sle32 rssi2;
	__sle32 rssi3;
	__sle32 rssi4;
	__sle32 rssi5;
	__sle32 rssi6;
	__sle32 rssi7;
} __packed;

#define HOST_CMD_SET_IFS_MIN_USECS 160

struct host_cmd_req_set_ifs {
	struct host_cmd_header hdr;
	__le32 period_usecs;
} __packed;

struct host_cmd_resp_set_ifs {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_set_fem_settings {
	struct host_cmd_header hdr;
	__le32 tx_antenna;
	__le32 rx_antenna;
	__le32 lna_enabled;
	__le32 pa_enabled;
} __packed;

struct host_cmd_resp_set_fem_settings {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_set_txop {
	struct host_cmd_header hdr;
	u8 min_packet_count;
} __packed;

struct host_cmd_resp_set_txop {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_set_control_response {
	struct host_cmd_header hdr;
	u8 direction;
	u8 control_response_1mhz_en;
} __packed;

struct host_cmd_resp_set_control_response {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_set_periodic_cal {
	struct host_cmd_header hdr;
	__le32 periodic_cal_en_mask;
} __packed;

struct host_cmd_resp_set_periodic_cal {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_set_bcn_rssi_threshold {
	struct host_cmd_header hdr;
	u8 threshold_db;
} __packed;

struct host_cmd_resp_set_bcn_rssi_threshold {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_set_tx_pkt_lifetime_usecs {
	struct host_cmd_header hdr;
	__le32 lifetime_usecs;
} __packed;

struct host_cmd_resp_set_tx_pkt_lifetime_usecs {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_set_physm_watchdog {
	struct host_cmd_header hdr;
	u8 physm_watchdog_en;
} __packed;

struct host_cmd_req_tx_polar {
	struct host_cmd_header hdr;
	u8 enable;
} __packed;

struct host_cmd_evt_sta_state {
	struct host_cmd_header hdr;
	u8 sta_addr[HOST_CMD_MAC_ADDR_LEN];
	__le16 aid;
	__le16 state;
} __packed;

struct host_cmd_evt_beacon_loss {
	struct host_cmd_header hdr;
	__le32 num_bcns;
} __packed;

struct host_cmd_evt_sig_field_error {
	struct host_cmd_header hdr;
	__le64 start_timestamp;
	__le64 end_timestamp;
} __packed;

#define HOST_CMD_UMAC_TRAFFIC_CONTROL_SOURCE_TWT BIT(0)
#define HOST_CMD_UMAC_TRAFFIC_CONTROL_SOURCE_DUTY_CYCLE BIT(1)

struct host_cmd_evt_umac_traffic_control {
	struct host_cmd_header hdr;
	u8 pause_data_traffic;
	__le32 sources;
} __packed;

struct host_cmd_evt_dhcp_lease_update {
	struct host_cmd_header hdr;
	__le32 my_ip;
	__le32 netmask;
	__le32 router;
	__le32 dns;
} __packed;

struct host_cmd_evt_ocs_done {
	struct host_cmd_header hdr;
	__le64 time_listen;
	__le64 time_rx;
	s8 noise;
	u8 metric;
} __packed;

struct host_cmd_evt_hw_scan_done {
	struct host_cmd_header hdr;
	u8 aborted;
} __packed;

struct host_cmd_evt_channel_usage {
	struct host_cmd_header hdr;
	__le64 time_listen;
	__le64 busy_time;
	__le32 freq_hz;
	u8 noise;
	u8 bw_mhz;
} __packed;

enum host_cmd_connection_loss_reason {
	HOST_CMD_CONNECTION_LOSS_REASON_TSF_RESET = 0,
};

struct host_cmd_evt_connection_loss {
	struct host_cmd_header hdr;
	__le32 reason;
} __packed;

struct host_cmd_evt_sched_scan_results {
	struct host_cmd_header hdr;
} __packed;

enum host_cmd_cqm_rssi_threshold_event {
	HOST_CMD_CQM_RSSI_THRESHOLD_EVENT_LOW = 0,
	HOST_CMD_CQM_RSSI_THRESHOLD_EVENT_HIGH = 1,
};

struct host_cmd_evt_cqm_rssi_notify {
	struct host_cmd_header hdr;
	__sle16 rssi;
	__le16 event;
} __packed;

struct host_cmd_evt_scan_done {
	struct host_cmd_header hdr;
	u8 aborted;
} __packed;

enum host_cmd_scan_result_frame {
	HOST_CMD_SCAN_RESULT_FRAME_UNKNOWN = 0,
	HOST_CMD_SCAN_RESULT_FRAME_BEACON = 1,
	HOST_CMD_SCAN_RESULT_FRAME_PROBE_RESPONSE = 2,
};

struct host_cmd_evt_scan_result {
	struct host_cmd_header hdr;
	__le32 channel_freq_hz;
	u8 bw_mhz;
	u8 frame_type;
	__sle16 rssi;
	u8 bssid[HOST_CMD_MAC_ADDR_LEN];
	__le16 beacon_interval;
	__le16 capability_info;
	__le64 tsf;
	__le16 ies_len;
	u8 ies[];
} __packed;

struct host_cmd_evt_connected {
	struct host_cmd_header hdr;
	u8 bssid[HOST_CMD_MAC_ADDR_LEN];
	__sle16 rssi;
	u8 padding_0[8];
	__le16 assoc_resp_ies_len;
	u8 assoc_resp_ies[];
} __packed;

struct host_cmd_evt_beacon_filter_match {
	struct host_cmd_header hdr;
	u8 padding_0[4];
	__le32 ies_len;
	u8 ies[];
} __packed;

struct host_cmd_req_set_capabilities {
	struct host_cmd_header hdr;
	struct host_cmd_mm_capabilities capabilities;
	u8 set_caps;
	u8 morse_mmss_offset;
} __packed;

struct host_cmd_resp_set_capabilities {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

struct host_cmd_req_set_transmission_rate {
	struct host_cmd_header hdr;
	__sle32 mcs_index;
	__sle32 bandwidth_mhz;
	__sle32 tx_80211ah_format;
	s8 use_traveling_pilots;
	s8 use_sgi;
	u8 enabled;
	s8 nss_idx;
	s8 use_ldpc;
	s8 use_stbc;
} __packed;

struct host_cmd_resp_set_transmission_rate {
	struct host_cmd_header hdr;
	__le32 status;
} __packed;

enum host_cmd_hart_id {
	HOST_CMD_HART_ID_HOST = 0,
	HOST_CMD_HART_ID_MAC = 1,
	HOST_CMD_HART_ID_UPHY = 2,
	HOST_CMD_HART_ID_LPHY = 3,
};

struct host_cmd_req_force_assert {
	struct host_cmd_header hdr;
	__le32 hart_id;
} __packed;

#define HOST_CMD_HOST_BLOCK_TX_FRAMES BIT(0)
#define HOST_CMD_HOST_BLOCK_TX_CMD BIT(1)

enum host_cmd_param_action {
	HOST_CMD_PARAM_ACTION_SET = 0,
	HOST_CMD_PARAM_ACTION_GET = 1,
	HOST_CMD_PARAM_ACTION_LAST = 2,
};

enum host_cmd_slow_clock_mode {
	HOST_CMD_SLOW_CLOCK_MODE_AUTO = 0,
	HOST_CMD_SLOW_CLOCK_MODE_INTERNAL = 1,
};

enum host_cmd_param_id {
	HOST_CMD_PARAM_ID_MAX_TRAFFIC_DELIVERY_WAIT_US = 0,
	HOST_CMD_PARAM_ID_EXTRA_ACK_TIMEOUT_ADJUST_US = 1,
	HOST_CMD_PARAM_ID_TX_STATUS_FLUSH_WATERMARK = 2,
	HOST_CMD_PARAM_ID_TX_STATUS_FLUSH_MIN_AMPDU_SIZE = 3,
	HOST_CMD_PARAM_ID_POWERSAVE_TYPE = 4,
	HOST_CMD_PARAM_ID_SNOOZE_DURATION_ADJUST_US = 5,
	HOST_CMD_PARAM_ID_TX_BLOCK = 6,
	HOST_CMD_PARAM_ID_FORCED_SNOOZE_PERIOD_US = 7,
	HOST_CMD_PARAM_ID_WAKE_ACTION_GPIO = 8,
	HOST_CMD_PARAM_ID_WAKE_ACTION_GPIO_PULSE_MS = 9,
	HOST_CMD_PARAM_ID_CONNECTION_MONITOR_GPIO = 10,
	HOST_CMD_PARAM_ID_INPUT_TRIGGER_GPIO = 11,
	HOST_CMD_PARAM_ID_INPUT_TRIGGER_MODE = 12,
	HOST_CMD_PARAM_ID_COUNTRY = 13,
	HOST_CMD_PARAM_ID_RTS_THRESHOLD = 14,
	HOST_CMD_PARAM_ID_HOST_TX_BLOCK = 15,
	HOST_CMD_PARAM_ID_MEM_RETENTION_CODE = 16,
	HOST_CMD_PARAM_ID_NON_TIM_MODE = 17,
	HOST_CMD_PARAM_ID_DYNAMIC_PS_TIMEOUT_MS = 18,
	HOST_CMD_PARAM_ID_HOME_CHANNEL_DWELL_MS = 19,
	HOST_CMD_PARAM_ID_SLOW_CLOCK_MODE = 20,
	HOST_CMD_PARAM_ID_FRAGMENT_THRESHOLD = 21,
	HOST_CMD_PARAM_ID_BEACON_LOSS_COUNT = 22,
	HOST_CMD_PARAM_ID_AP_POWER_SAVE = 23,
	HOST_CMD_PARAM_ID_BEACON_OFFLOAD = 24,
	HOST_CMD_PARAM_ID_PROBE_RESP_OFFLOAD = 25,
	HOST_CMD_PARAM_ID_BSS_MAX_AWAY_DURATION = 26,
	HOST_CMD_PARAM_ID_DEFAULT_ACTIVE_SCAN_DWELL_MS = 27,
	HOST_CMD_PARAM_ID_CTS_TO_SELF = 28,
	HOST_CMD_PARAM_ID_CHANNELIZATION = 29,
	HOST_CMD_PARAM_ID_LAST = 30,
};

struct host_cmd_req_get_set_generic_param {
	struct host_cmd_header hdr;
	__le32 param_id;
	__le32 action;
	__le32 flags;
	__le32 value;
} __packed;

struct host_cmd_resp_get_set_generic_param {
	struct host_cmd_header hdr;
	__le32 status;
	__le32 flags;
	__le32 value;
} __packed;

#endif
