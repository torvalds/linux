/*
 * Marvell Wireless LAN device driver: ioctl data structures & APIs
 *
 * Copyright (C) 2011-2014, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#ifndef _MWIFIEX_IOCTL_H_
#define _MWIFIEX_IOCTL_H_

#include <net/lib80211.h>

enum {
	MWIFIEX_SCAN_TYPE_UNCHANGED = 0,
	MWIFIEX_SCAN_TYPE_ACTIVE,
	MWIFIEX_SCAN_TYPE_PASSIVE
};

struct mwifiex_user_scan {
	u32 scan_cfg_len;
	u8 scan_cfg_buf[1];
};

#define MWIFIEX_PROMISC_MODE            1
#define MWIFIEX_MULTICAST_MODE		2
#define	MWIFIEX_ALL_MULTI_MODE		4
#define MWIFIEX_MAX_MULTICAST_LIST_SIZE	32

struct mwifiex_multicast_list {
	u32 mode;
	u32 num_multicast_addr;
	u8 mac_list[MWIFIEX_MAX_MULTICAST_LIST_SIZE][ETH_ALEN];
};

struct mwifiex_chan_freq {
	u32 channel;
	u32 freq;
};

struct mwifiex_ssid_bssid {
	struct cfg80211_ssid ssid;
	u8 bssid[ETH_ALEN];
};

enum {
	BAND_B = 1,
	BAND_G = 2,
	BAND_A = 4,
	BAND_GN = 8,
	BAND_AN = 16,
	BAND_AAC = 32,
};

#define MWIFIEX_WPA_PASSHPHRASE_LEN 64
struct wpa_param {
	u8 pairwise_cipher_wpa;
	u8 pairwise_cipher_wpa2;
	u8 group_cipher;
	u32 length;
	u8 passphrase[MWIFIEX_WPA_PASSHPHRASE_LEN];
};

struct wep_key {
	u8 key_index;
	u8 is_default;
	u16 length;
	u8 key[WLAN_KEY_LEN_WEP104];
};

#define KEY_MGMT_ON_HOST        0x03
#define MWIFIEX_AUTH_MODE_AUTO  0xFF
#define BAND_CONFIG_BG          0x00
#define BAND_CONFIG_A           0x01
#define MWIFIEX_SUPPORTED_RATES                 14
#define MWIFIEX_SUPPORTED_RATES_EXT             32
#define MWIFIEX_TDLS_SUPPORTED_RATES		8
#define MWIFIEX_TDLS_DEF_QOS_CAPAB		0xf
#define MWIFIEX_PRIO_BK				2
#define MWIFIEX_PRIO_VI				5

struct mwifiex_uap_bss_param {
	u8 channel;
	u8 band_cfg;
	u16 rts_threshold;
	u16 frag_threshold;
	u8 retry_limit;
	struct mwifiex_802_11_ssid ssid;
	u8 bcast_ssid_ctl;
	u8 radio_ctl;
	u8 dtim_period;
	u16 beacon_period;
	u16 auth_mode;
	u16 protocol;
	u16 key_mgmt;
	u16 key_mgmt_operation;
	struct wpa_param wpa_cfg;
	struct wep_key wep_cfg[NUM_WEP_KEYS];
	struct ieee80211_ht_cap ht_cap;
	struct ieee80211_vht_cap vht_cap;
	u8 rates[MWIFIEX_SUPPORTED_RATES];
	u32 sta_ao_timer;
	u32 ps_sta_ao_timer;
	u8 qos_info;
	struct mwifiex_types_wmm_info wmm_info;
};

enum {
	ADHOC_IDLE,
	ADHOC_STARTED,
	ADHOC_JOINED,
	ADHOC_COALESCED
};

struct mwifiex_ds_get_stats {
	u32 mcast_tx_frame;
	u32 failed;
	u32 retry;
	u32 multi_retry;
	u32 frame_dup;
	u32 rts_success;
	u32 rts_failure;
	u32 ack_failure;
	u32 rx_frag;
	u32 mcast_rx_frame;
	u32 fcs_error;
	u32 tx_frame;
	u32 wep_icv_error[4];
};

#define MWIFIEX_MAX_VER_STR_LEN    128

struct mwifiex_ver_ext {
	u32 version_str_sel;
	char version_str[MWIFIEX_MAX_VER_STR_LEN];
};

struct mwifiex_bss_info {
	u32 bss_mode;
	struct cfg80211_ssid ssid;
	u32 bss_chan;
	u8 country_code[3];
	u32 media_connected;
	u32 max_power_level;
	u32 min_power_level;
	u32 adhoc_state;
	signed int bcn_nf_last;
	u32 wep_status;
	u32 is_hs_configured;
	u32 is_deep_sleep;
	u8 bssid[ETH_ALEN];
};

#define MAX_NUM_TID     8

#define MAX_RX_WINSIZE  64

struct mwifiex_ds_rx_reorder_tbl {
	u16 tid;
	u8 ta[ETH_ALEN];
	u32 start_win;
	u32 win_size;
	u32 buffer[MAX_RX_WINSIZE];
};

struct mwifiex_ds_tx_ba_stream_tbl {
	u16 tid;
	u8 ra[ETH_ALEN];
	u8 amsdu;
};

#define DBG_CMD_NUM	5

struct mwifiex_debug_info {
	u32 int_counter;
	u32 packets_out[MAX_NUM_TID];
	u32 tx_buf_size;
	u32 curr_tx_buf_size;
	u32 tx_tbl_num;
	struct mwifiex_ds_tx_ba_stream_tbl
		tx_tbl[MWIFIEX_MAX_TX_BASTREAM_SUPPORTED];
	u32 rx_tbl_num;
	struct mwifiex_ds_rx_reorder_tbl rx_tbl
		[MWIFIEX_MAX_RX_BASTREAM_SUPPORTED];
	u16 ps_mode;
	u32 ps_state;
	u8 is_deep_sleep;
	u8 pm_wakeup_card_req;
	u32 pm_wakeup_fw_try;
	u8 is_hs_configured;
	u8 hs_activated;
	u32 num_cmd_host_to_card_failure;
	u32 num_cmd_sleep_cfm_host_to_card_failure;
	u32 num_tx_host_to_card_failure;
	u32 num_event_deauth;
	u32 num_event_disassoc;
	u32 num_event_link_lost;
	u32 num_cmd_deauth;
	u32 num_cmd_assoc_success;
	u32 num_cmd_assoc_failure;
	u32 num_tx_timeout;
	u8 is_cmd_timedout;
	u16 timeout_cmd_id;
	u16 timeout_cmd_act;
	u16 last_cmd_id[DBG_CMD_NUM];
	u16 last_cmd_act[DBG_CMD_NUM];
	u16 last_cmd_index;
	u16 last_cmd_resp_id[DBG_CMD_NUM];
	u16 last_cmd_resp_index;
	u16 last_event[DBG_CMD_NUM];
	u16 last_event_index;
	u8 data_sent;
	u8 cmd_sent;
	u8 cmd_resp_received;
	u8 event_received;
};

#define MWIFIEX_KEY_INDEX_UNICAST	0x40000000
#define PN_LEN				16

struct mwifiex_ds_encrypt_key {
	u32 key_disable;
	u32 key_index;
	u32 key_len;
	u8 key_material[WLAN_MAX_KEY_LEN];
	u8 mac_addr[ETH_ALEN];
	u32 is_wapi_key;
	u8 pn[PN_LEN];		/* packet number */
	u8 pn_len;
	u8 is_igtk_key;
	u8 is_current_wep_key;
	u8 is_rx_seq_valid;
};

struct mwifiex_power_cfg {
	u32 is_power_auto;
	u32 power_level;
};

struct mwifiex_ds_hs_cfg {
	u32 is_invoke_hostcmd;
	/*  Bit0: non-unicast data
	 *  Bit1: unicast data
	 *  Bit2: mac events
	 *  Bit3: magic packet
	 */
	u32 conditions;
	u32 gpio;
	u32 gap;
};

#define DEEP_SLEEP_ON  1
#define DEEP_SLEEP_OFF 0
#define DEEP_SLEEP_IDLE_TIME	100
#define PS_MODE_AUTO		1

struct mwifiex_ds_auto_ds {
	u16 auto_ds;
	u16 idle_time;
};

struct mwifiex_ds_pm_cfg {
	union {
		u32 ps_mode;
		struct mwifiex_ds_hs_cfg hs_cfg;
		struct mwifiex_ds_auto_ds auto_deep_sleep;
		u32 sleep_period;
	} param;
};

struct mwifiex_11ac_vht_cfg {
	u8 band_config;
	u8 misc_config;
	u32 cap_info;
	u32 mcs_tx_set;
	u32 mcs_rx_set;
};

struct mwifiex_ds_11n_tx_cfg {
	u16 tx_htcap;
	u16 tx_htinfo;
	u16 misc_config; /* Needed for 802.11AC cards only */
};

struct mwifiex_ds_11n_amsdu_aggr_ctrl {
	u16 enable;
	u16 curr_buf_size;
};

struct mwifiex_ds_ant_cfg {
	u32 tx_ant;
	u32 rx_ant;
};

#define MWIFIEX_NUM_OF_CMD_BUFFER	50
#define MWIFIEX_SIZE_OF_CMD_BUFFER	2048

enum {
	MWIFIEX_IE_TYPE_GEN_IE = 0,
	MWIFIEX_IE_TYPE_ARP_FILTER,
};

enum {
	MWIFIEX_REG_MAC = 1,
	MWIFIEX_REG_BBP,
	MWIFIEX_REG_RF,
	MWIFIEX_REG_PMIC,
	MWIFIEX_REG_CAU,
};

struct mwifiex_ds_reg_rw {
	__le32 type;
	__le32 offset;
	__le32 value;
};

#define MAX_EEPROM_DATA 256

struct mwifiex_ds_read_eeprom {
	__le16 offset;
	__le16 byte_count;
	u8 value[MAX_EEPROM_DATA];
};

#define IEEE_MAX_IE_SIZE		256

#define MWIFIEX_IE_HDR_SIZE	(sizeof(struct mwifiex_ie) - IEEE_MAX_IE_SIZE)

struct mwifiex_ds_misc_gen_ie {
	u32 type;
	u32 len;
	u8 ie_data[IEEE_MAX_IE_SIZE];
};

struct mwifiex_ds_misc_cmd {
	u32 len;
	u8 cmd[MWIFIEX_SIZE_OF_CMD_BUFFER];
};

#define BITMASK_BCN_RSSI_LOW	BIT(0)
#define BITMASK_BCN_RSSI_HIGH	BIT(4)

enum subsc_evt_rssi_state {
	EVENT_HANDLED,
	RSSI_LOW_RECVD,
	RSSI_HIGH_RECVD
};

struct subsc_evt_cfg {
	u8 abs_value;
	u8 evt_freq;
};

struct mwifiex_ds_misc_subsc_evt {
	u16 action;
	u16 events;
	struct subsc_evt_cfg bcn_l_rssi_cfg;
	struct subsc_evt_cfg bcn_h_rssi_cfg;
};

#define MWIFIEX_MEF_MAX_BYTESEQ		6	/* non-adjustable */
#define MWIFIEX_MEF_MAX_FILTERS		10

struct mwifiex_mef_filter {
	u16 repeat;
	u16 offset;
	s8 byte_seq[MWIFIEX_MEF_MAX_BYTESEQ + 1];
	u8 filt_type;
	u8 filt_action;
};

struct mwifiex_mef_entry {
	u8 mode;
	u8 action;
	struct mwifiex_mef_filter filter[MWIFIEX_MEF_MAX_FILTERS];
};

struct mwifiex_ds_mef_cfg {
	u32 criteria;
	u16 num_entries;
	struct mwifiex_mef_entry *mef_entry;
};

#define MWIFIEX_MAX_VSIE_LEN       (256)
#define MWIFIEX_MAX_VSIE_NUM       (8)
#define MWIFIEX_VSIE_MASK_CLEAR    0x00
#define MWIFIEX_VSIE_MASK_SCAN     0x01
#define MWIFIEX_VSIE_MASK_ASSOC    0x02
#define MWIFIEX_VSIE_MASK_ADHOC    0x04

enum {
	MWIFIEX_FUNC_INIT = 1,
	MWIFIEX_FUNC_SHUTDOWN,
};

enum COALESCE_OPERATION {
	RECV_FILTER_MATCH_TYPE_EQ = 0x80,
	RECV_FILTER_MATCH_TYPE_NE,
};

enum COALESCE_PACKET_TYPE {
	PACKET_TYPE_UNICAST = 1,
	PACKET_TYPE_MULTICAST = 2,
	PACKET_TYPE_BROADCAST = 3
};

#define MWIFIEX_COALESCE_MAX_RULES	8
#define MWIFIEX_COALESCE_MAX_BYTESEQ	4	/* non-adjustable */
#define MWIFIEX_COALESCE_MAX_FILTERS	4
#define MWIFIEX_MAX_COALESCING_DELAY	100     /* in msecs */

struct filt_field_param {
	u8 operation;
	u8 operand_len;
	u16 offset;
	u8 operand_byte_stream[MWIFIEX_COALESCE_MAX_BYTESEQ];
};

struct mwifiex_coalesce_rule {
	u16 max_coalescing_delay;
	u8 num_of_fields;
	u8 pkt_type;
	struct filt_field_param params[MWIFIEX_COALESCE_MAX_FILTERS];
};

struct mwifiex_ds_coalesce_cfg {
	u16 num_of_rules;
	struct mwifiex_coalesce_rule rule[MWIFIEX_COALESCE_MAX_RULES];
};

struct mwifiex_ds_tdls_oper {
	u16 tdls_action;
	u8 peer_mac[ETH_ALEN];
	u16 capability;
	u8 qos_info;
	u8 *ext_capab;
	u8 ext_capab_len;
	u8 *supp_rates;
	u8 supp_rates_len;
	u8 *ht_capab;
};

#endif /* !_MWIFIEX_IOCTL_H_ */
