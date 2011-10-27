/*
 * Marvell Wireless LAN device driver: ioctl data structures & APIs
 *
 * Copyright (C) 2011, Marvell International Ltd.
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

#include <net/mac80211.h>

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
	struct mwifiex_802_11_ssid ssid;
	u8 bssid[ETH_ALEN];
};

enum {
	BAND_B = 1,
	BAND_G = 2,
	BAND_A = 4,
	BAND_GN = 8,
	BAND_AN = 16,
};

#define NO_SEC_CHANNEL               0
#define SEC_CHANNEL_ABOVE            1
#define SEC_CHANNEL_BELOW            3

struct mwifiex_ds_band_cfg {
	u32 config_bands;
	u32 adhoc_start_band;
	u32 adhoc_channel;
	u32 sec_chan_offset;
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

#define BCN_RSSI_AVG_MASK               0x00000002
#define BCN_NF_AVG_MASK                 0x00000200
#define ALL_RSSI_INFO_MASK              0x00000fff

struct mwifiex_ds_get_signal {
	/*
	 * Bit0:  Last Beacon RSSI,  Bit1:  Average Beacon RSSI,
	 * Bit2:  Last Data RSSI,    Bit3:  Average Data RSSI,
	 * Bit4:  Last Beacon SNR,   Bit5:  Average Beacon SNR,
	 * Bit6:  Last Data SNR,     Bit7:  Average Data SNR,
	 * Bit8:  Last Beacon NF,    Bit9:  Average Beacon NF,
	 * Bit10: Last Data NF,      Bit11: Average Data NF
	 */
	u16 selector;
	s16 bcn_rssi_last;
	s16 bcn_rssi_avg;
	s16 data_rssi_last;
	s16 data_rssi_avg;
	s16 bcn_snr_last;
	s16 bcn_snr_avg;
	s16 data_snr_last;
	s16 data_snr_avg;
	s16 bcn_nf_last;
	s16 bcn_nf_avg;
	s16 data_nf_last;
	s16 data_nf_avg;
};

#define MWIFIEX_MAX_VER_STR_LEN    128

struct mwifiex_ver_ext {
	u32 version_str_sel;
	char version_str[MWIFIEX_MAX_VER_STR_LEN];
};

struct mwifiex_bss_info {
	u32 bss_mode;
	struct mwifiex_802_11_ssid ssid;
	u32 scan_table_idx;
	u32 bss_chan;
	u32 region_code;
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
};

#define DBG_CMD_NUM	5

struct mwifiex_debug_info {
	u32 int_counter;
	u32 packets_out[MAX_NUM_TID];
	u32 max_tx_buf_size;
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
	u32 num_cmd_timeout;
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
#define WAPI_RXPN_LEN			16

struct mwifiex_ds_encrypt_key {
	u32 key_disable;
	u32 key_index;
	u32 key_len;
	u8 key_material[WLAN_MAX_KEY_LEN];
	u8 mac_addr[ETH_ALEN];
	u32 is_wapi_key;
	u8 wapi_rxpn[WAPI_RXPN_LEN];
};

struct mwifiex_rate_cfg {
	u32 action;
	u32 is_rate_auto;
	u32 rate;
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

struct mwifiex_ds_11n_tx_cfg {
	u16 tx_htcap;
	u16 tx_htinfo;
};

struct mwifiex_ds_11n_amsdu_aggr_ctrl {
	u16 enable;
	u16 curr_buf_size;
};

#define MWIFIEX_NUM_OF_CMD_BUFFER	20
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

struct mwifiex_ds_misc_gen_ie {
	u32 type;
	u32 len;
	u8 ie_data[IW_CUSTOM_MAX];
};

struct mwifiex_ds_misc_cmd {
	u32 len;
	u8 cmd[MWIFIEX_SIZE_OF_CMD_BUFFER];
};

#define MWIFIEX_MAX_VSIE_LEN       (256)
#define MWIFIEX_MAX_VSIE_NUM       (8)
#define MWIFIEX_VSIE_MASK_SCAN     0x01
#define MWIFIEX_VSIE_MASK_ASSOC    0x02
#define MWIFIEX_VSIE_MASK_ADHOC    0x04

enum {
	MWIFIEX_FUNC_INIT = 1,
	MWIFIEX_FUNC_SHUTDOWN,
};

#endif /* !_MWIFIEX_IOCTL_H_ */
