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

#ifndef __IWM_COMMANDS_H__
#define __IWM_COMMANDS_H__

#include <linux/ieee80211.h>

#define IWM_BARKER_REBOOT_NOTIFICATION	0xF
#define IWM_ACK_BARKER_NOTIFICATION	0x10

/* UMAC commands */
#define UMAC_RST_CTRL_FLG_LARC_CLK_EN	0x0001
#define UMAC_RST_CTRL_FLG_LARC_RESET	0x0002
#define UMAC_RST_CTRL_FLG_FUNC_RESET	0x0004
#define UMAC_RST_CTRL_FLG_DEV_RESET	0x0008
#define UMAC_RST_CTRL_FLG_WIFI_CORE_EN	0x0010
#define UMAC_RST_CTRL_FLG_WIFI_LINK_EN	0x0040
#define UMAC_RST_CTRL_FLG_WIFI_MLME_EN	0x0080
#define UMAC_RST_CTRL_FLG_NVM_RELOAD	0x0100

struct iwm_umac_cmd_reset {
	__le32 flags;
} __attribute__ ((packed));

#define UMAC_PARAM_TBL_ORD_FIX    0x0
#define UMAC_PARAM_TBL_ORD_VAR    0x1
#define UMAC_PARAM_TBL_CFG_FIX    0x2
#define UMAC_PARAM_TBL_CFG_VAR    0x3
#define UMAC_PARAM_TBL_BSS_TRK    0x4
#define UMAC_PARAM_TBL_FA_CFG_FIX 0x5
#define UMAC_PARAM_TBL_STA        0x6
#define UMAC_PARAM_TBL_CHN        0x7
#define UMAC_PARAM_TBL_STATISTICS 0x8

/* fast access table */
enum {
	CFG_FRAG_THRESHOLD = 0,
	CFG_FRAME_RETRY_LIMIT,
	CFG_OS_QUEUE_UTIL_TH,
	CFG_RX_FILTER,
	/* <-- LAST --> */
	FAST_ACCESS_CFG_TBL_FIX_LAST
};

/* fixed size table */
enum {
	CFG_POWER_INDEX = 0,
	CFG_PM_LEGACY_RX_TIMEOUT,
	CFG_PM_LEGACY_TX_TIMEOUT,
	CFG_PM_CTRL_FLAGS,
	CFG_PM_KEEP_ALIVE_IN_BEACONS,
	CFG_BT_ON_THRESHOLD,
	CFG_RTS_THRESHOLD,
	CFG_CTS_TO_SELF,
	CFG_COEX_MODE,
	CFG_WIRELESS_MODE,
	CFG_ASSOCIATION_TIMEOUT,
	CFG_ROAM_TIMEOUT,
	CFG_CAPABILITY_SUPPORTED_RATES,
	CFG_SCAN_ALLOWED_UNASSOC_FLAGS,
	CFG_SCAN_ALLOWED_MAIN_ASSOC_FLAGS,
	CFG_SCAN_ALLOWED_PAN_ASSOC_FLAGS,
	CFG_SCAN_INTERNAL_PERIODIC_ENABLED,
	CFG_SCAN_IMM_INTERNAL_PERIODIC_SCAN_ON_INIT,
	CFG_SCAN_DEFAULT_PERIODIC_FREQ_SEC,
	CFG_SCAN_NUM_PASSIVE_CHAN_PER_PARTIAL_SCAN,
	CFG_TLC_SUPPORTED_TX_HT_RATES,
	CFG_TLC_SUPPORTED_TX_RATES,
	CFG_TLC_VALID_ANTENNA,
	CFG_TLC_SPATIAL_STREAM_SUPPORTED,
	CFG_TLC_RETRY_PER_RATE,
	CFG_TLC_RETRY_PER_HT_RATE,
	CFG_TLC_FIXED_RATE,
	CFG_TLC_FIXED_RATE_FLAGS,
	CFG_TLC_CONTROL_FLAGS,
	CFG_TLC_SR_MIN_FAIL,
	CFG_TLC_SR_MIN_PASS,
	CFG_TLC_HT_STAY_IN_COL_PASS_THRESH,
	CFG_TLC_HT_STAY_IN_COL_FAIL_THRESH,
	CFG_TLC_LEGACY_STAY_IN_COL_PASS_THRESH,
	CFG_TLC_LEGACY_STAY_IN_COL_FAIL_THRESH,
	CFG_TLC_HT_FLUSH_STATS_PACKETS,
	CFG_TLC_LEGACY_FLUSH_STATS_PACKETS,
	CFG_TLC_LEGACY_FLUSH_STATS_MS,
	CFG_TLC_HT_FLUSH_STATS_MS,
	CFG_TLC_STAY_IN_COL_TIME_OUT,
	CFG_TLC_AGG_SHORT_LIM,
	CFG_TLC_AGG_LONG_LIM,
	CFG_TLC_HT_SR_NO_DECREASE,
	CFG_TLC_LEGACY_SR_NO_DECREASE,
	CFG_TLC_SR_FORCE_DECREASE,
	CFG_TLC_SR_ALLOW_INCREASE,
	CFG_TLC_AGG_SET_LONG,
	CFG_TLC_AUTO_AGGREGATION,
	CFG_TLC_AGG_THRESHOLD,
	CFG_TLC_TID_LOAD_THRESHOLD,
	CFG_TLC_BLOCK_ACK_TIMEOUT,
	CFG_TLC_NO_BA_COUNTED_AS_ONE,
	CFG_TLC_NUM_BA_STREAMS_ALLOWED,
	CFG_TLC_NUM_BA_STREAMS_PRESENT,
	CFG_TLC_RENEW_ADDBA_DELAY,
	CFG_TLC_NUM_OF_MULTISEC_TO_COUN_LOAD,
	CFG_TLC_IS_STABLE_IN_HT,
	CFG_RLC_CHAIN_CTRL,
	CFG_TRK_TABLE_OP_MODE,
	CFG_TRK_TABLE_RSSI_THRESHOLD,
	CFG_TX_PWR_TARGET, /* Used By xVT */
	CFG_TX_PWR_LIMIT_USR,
	CFG_TX_PWR_LIMIT_BSS, /* 11d limit */
	CFG_TX_PWR_LIMIT_BSS_CONSTRAINT, /* 11h constraint */
	CFG_TX_PWR_MODE,
	CFG_MLME_DBG_NOTIF_BLOCK,
	CFG_BT_OFF_BECONS_INTERVALS,
	CFG_BT_FRAG_DURATION,

	/* <-- LAST --> */
	CFG_TBL_FIX_LAST
};

/* variable size table */
enum {
	CFG_NET_ADDR = 0,
	CFG_PROFILE,
	/* <-- LAST --> */
	CFG_TBL_VAR_LAST
};

struct iwm_umac_cmd_set_param_fix {
	__le16 tbl;
	__le16 key;
	__le32 value;
} __attribute__ ((packed));

struct iwm_umac_cmd_set_param_var {
	__le16 tbl;
	__le16 key;
	__le16 len;
	__le16 reserved;
} __attribute__ ((packed));

struct iwm_umac_cmd_get_param {
	__le16 tbl;
	__le16 key;
} __attribute__ ((packed));

struct iwm_umac_cmd_get_param_resp {
	__le16 tbl;
	__le16 key;
	__le16 len;
	__le16 reserved;
} __attribute__ ((packed));

struct iwm_umac_cmd_eeprom_proxy_hdr {
	__le32 type;
	__le32 offset;
	__le32 len;
} __attribute__ ((packed));

struct iwm_umac_cmd_eeprom_proxy {
	struct iwm_umac_cmd_eeprom_proxy_hdr hdr;
	u8 buf[0];
} __attribute__ ((packed));

#define IWM_UMAC_CMD_EEPROM_TYPE_READ       0x1
#define IWM_UMAC_CMD_EEPROM_TYPE_WRITE      0x2

#define UMAC_CHANNEL_FLAG_VALID		BIT(0)
#define UMAC_CHANNEL_FLAG_IBSS		BIT(1)
#define UMAC_CHANNEL_FLAG_ACTIVE	BIT(3)
#define UMAC_CHANNEL_FLAG_RADAR		BIT(4)
#define UMAC_CHANNEL_FLAG_DFS		BIT(7)

struct iwm_umac_channel_info {
	u8 band;
	u8 type;
	u8 reserved;
	u8 flags;
	__le32 channels_mask;
} __attribute__ ((packed));

struct iwm_umac_cmd_get_channel_list {
	__le16 count;
	__le16 reserved;
	struct iwm_umac_channel_info ch[0];
} __attribute__ ((packed));


/* UMAC WiFi interface commands */

/* Coexistence mode */
#define COEX_MODE_SA  0x1
#define COEX_MODE_XOR 0x2
#define COEX_MODE_CM  0x3
#define COEX_MODE_MAX 0x4

/* Wireless mode */
#define WIRELESS_MODE_11A  0x1
#define WIRELESS_MODE_11G  0x2

#define UMAC_PROFILE_EX_IE_REQUIRED	0x1
#define UMAC_PROFILE_QOS_ALLOWED	0x2

/* Scanning */
#define UMAC_WIFI_IF_PROBE_OPTION_MAX        10

#define UMAC_WIFI_IF_SCAN_TYPE_USER          0x0
#define UMAC_WIFI_IF_SCAN_TYPE_UMAC_RESERVED 0x1
#define UMAC_WIFI_IF_SCAN_TYPE_HOST_PERIODIC 0x2
#define UMAC_WIFI_IF_SCAN_TYPE_MAX           0x3

struct iwm_umac_ssid {
	u8 ssid_len;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 reserved[3];
} __attribute__ ((packed));

struct iwm_umac_cmd_scan_request {
	struct iwm_umac_wifi_if hdr;
	__le32 type; /* UMAC_WIFI_IF_SCAN_TYPE_* */
	u8 ssid_num;
	u8 seq_num;
	u8 timeout; /* In seconds */
	u8 reserved;
	struct iwm_umac_ssid ssids[UMAC_WIFI_IF_PROBE_OPTION_MAX];
} __attribute__ ((packed));

#define UMAC_CIPHER_TYPE_NONE		0xFF
#define UMAC_CIPHER_TYPE_USE_GROUPCAST	0x00
#define UMAC_CIPHER_TYPE_WEP_40		0x01
#define UMAC_CIPHER_TYPE_WEP_104	0x02
#define UMAC_CIPHER_TYPE_TKIP		0x04
#define UMAC_CIPHER_TYPE_CCMP		0x08

/* Supported authentication types - bitmap */
#define UMAC_AUTH_TYPE_OPEN		0x00
#define UMAC_AUTH_TYPE_LEGACY_PSK	0x01
#define UMAC_AUTH_TYPE_8021X		0x02
#define UMAC_AUTH_TYPE_RSNA_PSK		0x04

/* iwm_umac_security.flag is WPA supported -- bits[0:0] */
#define UMAC_SEC_FLG_WPA_ON_POS		0
#define UMAC_SEC_FLG_WPA_ON_SEED	1
#define UMAC_SEC_FLG_WPA_ON_MSK (UMAC_SEC_FLG_WPA_ON_SEED << \
				 UMAC_SEC_FLG_WPA_ON_POS)

/* iwm_umac_security.flag is WPA2 supported -- bits [1:1] */
#define UMAC_SEC_FLG_RSNA_ON_POS	1
#define UMAC_SEC_FLG_RSNA_ON_SEED	1
#define UMAC_SEC_FLG_RSNA_ON_MSK        (UMAC_SEC_FLG_RSNA_ON_SEED << \
					 UMAC_SEC_FLG_RSNA_ON_POS)

/* iwm_umac_security.flag is WSC mode on -- bits [2:2] */
#define UMAC_SEC_FLG_WSC_ON_POS		2
#define UMAC_SEC_FLG_WSC_ON_SEED	1

/* Legacy profile can use only WEP40 and WEP104 for encryption and
 * OPEN or PSK for authentication */
#define UMAC_SEC_FLG_LEGACY_PROFILE	0

struct iwm_umac_security {
	u8 auth_type;
	u8 ucast_cipher;
	u8 mcast_cipher;
	u8 flags;
} __attribute__ ((packed));

struct iwm_umac_ibss {
	u8 beacon_interval;	/* in millisecond */
	u8 atim;		/* in millisecond */
	s8 join_only;
	u8 band;
	u8 channel;
	u8 reserved[3];
} __attribute__ ((packed));

#define UMAC_MODE_BSS	0
#define UMAC_MODE_IBSS	1

#define UMAC_BSSID_MAX	4

struct iwm_umac_profile {
	struct iwm_umac_wifi_if hdr;
	__le32 mode;
	struct iwm_umac_ssid ssid;
	u8 bssid[UMAC_BSSID_MAX][ETH_ALEN];
	struct iwm_umac_security sec;
	struct iwm_umac_ibss ibss;
	__le32 channel_2ghz;
	__le32 channel_5ghz;
	__le16 flags;
	u8 wireless_mode;
	u8 bss_num;
} __attribute__ ((packed));

struct iwm_umac_invalidate_profile {
	struct iwm_umac_wifi_if hdr;
	u8 reason;
	u8 reserved[3];
} __attribute__ ((packed));

/* Encryption key commands */
struct iwm_umac_key_wep40 {
	struct iwm_umac_wifi_if hdr;
	struct iwm_umac_key_hdr key_hdr;
	u8 key[WLAN_KEY_LEN_WEP40];
	u8 static_key;
	u8 reserved[2];
} __attribute__ ((packed));

struct iwm_umac_key_wep104 {
	struct iwm_umac_wifi_if hdr;
	struct iwm_umac_key_hdr key_hdr;
	u8 key[WLAN_KEY_LEN_WEP104];
	u8 static_key;
	u8 reserved[2];
} __attribute__ ((packed));

#define IWM_TKIP_KEY_SIZE 16
#define IWM_TKIP_MIC_SIZE 8
struct iwm_umac_key_tkip {
	struct iwm_umac_wifi_if hdr;
	struct iwm_umac_key_hdr key_hdr;
	u8 iv_count[6];
	u8 reserved[2];
	u8 tkip_key[IWM_TKIP_KEY_SIZE];
	u8 mic_rx_key[IWM_TKIP_MIC_SIZE];
	u8 mic_tx_key[IWM_TKIP_MIC_SIZE];
} __attribute__ ((packed));

struct iwm_umac_key_ccmp {
	struct iwm_umac_wifi_if hdr;
	struct iwm_umac_key_hdr key_hdr;
	u8 iv_count[6];
	u8 reserved[2];
	u8 key[WLAN_KEY_LEN_CCMP];
} __attribute__ ((packed));

struct iwm_umac_key_remove {
	struct iwm_umac_wifi_if hdr;
	struct iwm_umac_key_hdr key_hdr;
} __attribute__ ((packed));

struct iwm_umac_tx_key_id {
	struct iwm_umac_wifi_if hdr;
	u8 key_idx;
	u8 reserved[3];
} __attribute__ ((packed));

struct iwm_umac_cmd_stats_req {
	__le32 flags;
} __attribute__ ((packed));

/* LMAC commands */
int iwm_read_mac(struct iwm_priv *iwm, u8 *mac);
int iwm_send_prio_table(struct iwm_priv *iwm);
int iwm_send_init_calib_cfg(struct iwm_priv *iwm, u8 calib_requested);
int iwm_send_periodic_calib_cfg(struct iwm_priv *iwm, u8 calib_requested);
int iwm_send_calib_results(struct iwm_priv *iwm);
int iwm_store_rxiq_calib_result(struct iwm_priv *iwm);

/* UMAC commands */
int iwm_send_wifi_if_cmd(struct iwm_priv *iwm, void *payload, u16 payload_size,
			 bool resp);
int iwm_send_umac_reset(struct iwm_priv *iwm, __le32 reset_flags, bool resp);
int iwm_umac_set_config_fix(struct iwm_priv *iwm, u16 tbl, u16 key, u32 value);
int iwm_umac_set_config_var(struct iwm_priv *iwm, u16 key,
			    void *payload, u16 payload_size);
int iwm_send_umac_config(struct iwm_priv *iwm, __le32 reset_flags);
int iwm_send_mlme_profile(struct iwm_priv *iwm);
int iwm_invalidate_mlme_profile(struct iwm_priv *iwm);
int iwm_send_packet(struct iwm_priv *iwm, struct sk_buff *skb, int pool_id);
int iwm_set_tx_key(struct iwm_priv *iwm, u8 key_idx);
int iwm_set_key(struct iwm_priv *iwm, bool remove, bool set_tx_key,
		struct iwm_key *key);
int iwm_send_umac_stats_req(struct iwm_priv *iwm, u32 flags);
int iwm_send_umac_channel_list(struct iwm_priv *iwm);
int iwm_scan_ssids(struct iwm_priv *iwm, struct cfg80211_ssid *ssids,
		   int ssid_num);
int iwm_scan_one_ssid(struct iwm_priv *iwm, u8 *ssid, int ssid_len);

/* UDMA commands */
int iwm_target_reset(struct iwm_priv *iwm);
#endif
