/**
 * Copyright (c) 2014 Redpine Signals Inc.
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

#ifndef __RSI_MGMT_H__
#define __RSI_MGMT_H__

#include <linux/sort.h>
#include "rsi_boot_params.h"
#include "rsi_main.h"

#define MAX_MGMT_PKT_SIZE               512
#define RSI_NEEDED_HEADROOM             80
#define RSI_RCV_BUFFER_LEN              2000

#define RSI_11B_MODE                    0
#define RSI_11G_MODE                    BIT(7)
#define RETRY_COUNT                     8
#define RETRY_LONG                      4
#define RETRY_SHORT                     7
#define WMM_SHORT_SLOT_TIME             9
#define SIFS_DURATION                   16

#define KEY_TYPE_CLEAR                  0
#define RSI_PAIRWISE_KEY                1
#define RSI_GROUP_KEY                   2

/* EPPROM_READ_ADDRESS */
#define WLAN_MAC_EEPROM_ADDR            40
#define WLAN_MAC_MAGIC_WORD_LEN         0x01
#define WLAN_HOST_MODE_LEN              0x04
#define WLAN_FW_VERSION_LEN             0x08
#define MAGIC_WORD                      0x5A

/* Receive Frame Types */
#define TA_CONFIRM_TYPE                 0x01
#define RX_DOT11_MGMT                   0x02
#define TX_STATUS_IND                   0x04
#define PROBEREQ_CONFIRM                2
#define CARD_READY_IND                  0x00

#define RSI_DELETE_PEER                 0x0
#define RSI_ADD_PEER                    0x1
#define START_AMPDU_AGGR                0x1
#define STOP_AMPDU_AGGR                 0x0
#define INTERNAL_MGMT_PKT               0x99

#define PUT_BBP_RESET                   0
#define BBP_REG_WRITE                   0
#define RF_RESET_ENABLE                 BIT(3)
#define RATE_INFO_ENABLE                BIT(0)
#define RSI_BROADCAST_PKT               BIT(9)

#define UPPER_20_ENABLE                 (0x2 << 12)
#define LOWER_20_ENABLE                 (0x4 << 12)
#define FULL40M_ENABLE                  0x6

#define RSI_LMAC_CLOCK_80MHZ            0x1
#define RSI_ENABLE_40MHZ                (0x1 << 3)

#define RX_BA_INDICATION                1
#define RSI_TBL_SZ                      40
#define MAX_RETRIES                     8

#define STD_RATE_MCS7                   0x07
#define STD_RATE_MCS6                   0x06
#define STD_RATE_MCS5                   0x05
#define STD_RATE_MCS4                   0x04
#define STD_RATE_MCS3                   0x03
#define STD_RATE_MCS2                   0x02
#define STD_RATE_MCS1                   0x01
#define STD_RATE_MCS0                   0x00
#define STD_RATE_54                     0x6c
#define STD_RATE_48                     0x60
#define STD_RATE_36                     0x48
#define STD_RATE_24                     0x30
#define STD_RATE_18                     0x24
#define STD_RATE_12                     0x18
#define STD_RATE_11                     0x16
#define STD_RATE_09                     0x12
#define STD_RATE_06                     0x0C
#define STD_RATE_5_5                    0x0B
#define STD_RATE_02                     0x04
#define STD_RATE_01                     0x02

#define RSI_RF_TYPE                     1
#define RSI_RATE_00                     0x00
#define RSI_RATE_1                      0x0
#define RSI_RATE_2                      0x2
#define RSI_RATE_5_5                    0x4
#define RSI_RATE_11                     0x6
#define RSI_RATE_6                      0x8b
#define RSI_RATE_9                      0x8f
#define RSI_RATE_12                     0x8a
#define RSI_RATE_18                     0x8e
#define RSI_RATE_24                     0x89
#define RSI_RATE_36                     0x8d
#define RSI_RATE_48                     0x88
#define RSI_RATE_54                     0x8c
#define RSI_RATE_MCS0                   0x100
#define RSI_RATE_MCS1                   0x101
#define RSI_RATE_MCS2                   0x102
#define RSI_RATE_MCS3                   0x103
#define RSI_RATE_MCS4                   0x104
#define RSI_RATE_MCS5                   0x105
#define RSI_RATE_MCS6                   0x106
#define RSI_RATE_MCS7                   0x107
#define RSI_RATE_MCS7_SG                0x307

#define BW_20MHZ                        0
#define BW_40MHZ                        1

#define RSI_SUPP_FILTERS	(FIF_ALLMULTI | FIF_PROBE_REQ |\
				 FIF_BCN_PRBRESP_PROMISC)
enum opmode {
	STA_OPMODE = 1,
	AP_OPMODE = 2
};

extern struct ieee80211_rate rsi_rates[12];
extern const u16 rsi_mcsrates[8];

enum sta_notify_events {
	STA_CONNECTED = 0,
	STA_DISCONNECTED,
	STA_TX_ADDBA_DONE,
	STA_TX_DELBA,
	STA_RX_ADDBA_DONE,
	STA_RX_DELBA
};

/* Send Frames Types */
enum cmd_frame_type {
	TX_DOT11_MGMT,
	RESET_MAC_REQ,
	RADIO_CAPABILITIES,
	BB_PROG_VALUES_REQUEST,
	RF_PROG_VALUES_REQUEST,
	WAKEUP_SLEEP_REQUEST,
	SCAN_REQUEST,
	TSF_UPDATE,
	PEER_NOTIFY,
	BLOCK_UNBLOCK,
	SET_KEY_REQ,
	AUTO_RATE_IND,
	BOOTUP_PARAMS_REQUEST,
	VAP_CAPABILITIES,
	EEPROM_READ_TYPE ,
	EEPROM_WRITE,
	GPIO_PIN_CONFIG ,
	SET_RX_FILTER,
	AMPDU_IND,
	STATS_REQUEST_FRAME,
	BB_BUF_PROG_VALUES_REQ,
	BBP_PROG_IN_TA,
	BG_SCAN_PARAMS,
	BG_SCAN_PROBE_REQ,
	CW_MODE_REQ,
	PER_CMD_PKT
};

struct rsi_mac_frame {
	__le16 desc_word[8];
} __packed;

struct rsi_boot_params {
	__le16 desc_word[8];
	struct bootup_params bootup_params;
} __packed;

struct rsi_peer_notify {
	__le16 desc_word[8];
	u8 mac_addr[6];
	__le16 command;
	__le16 mpdu_density;
	__le16 reserved;
	__le32 sta_flags;
} __packed;

struct rsi_vap_caps {
	__le16 desc_word[8];
	u8 mac_addr[6];
	__le16 keep_alive_period;
	u8 bssid[6];
	__le16 reserved;
	__le32 flags;
	__le16 frag_threshold;
	__le16 rts_threshold;
	__le32 default_mgmt_rate;
	__le32 default_ctrl_rate;
	__le32 default_data_rate;
	__le16 beacon_interval;
	__le16 dtim_period;
} __packed;

struct rsi_set_key {
	__le16 desc_word[8];
	u8 key[4][32];
	u8 tx_mic_key[8];
	u8 rx_mic_key[8];
} __packed;

struct rsi_auto_rate {
	__le16 desc_word[8];
	__le16 failure_limit;
	__le16 initial_boundary;
	__le16 max_threshold_limt;
	__le16 num_supported_rates;
	__le16 aarf_rssi;
	__le16 moderate_rate_inx;
	__le16 collision_tolerance;
	__le16 supported_rates[40];
} __packed;

struct qos_params {
	__le16 cont_win_min_q;
	__le16 cont_win_max_q;
	__le16 aifsn_val_q;
	__le16 txop_q;
} __packed;

struct rsi_radio_caps {
	__le16 desc_word[8];
	struct qos_params qos_params[MAX_HW_QUEUES];
	u8 num_11n_rates;
	u8 num_11ac_rates;
	__le16 gcpd_per_rate[20];
} __packed;

static inline u32 rsi_get_queueno(u8 *addr, u16 offset)
{
	return (le16_to_cpu(*(__le16 *)&addr[offset]) & 0x7000) >> 12;
}

static inline u32 rsi_get_length(u8 *addr, u16 offset)
{
	return (le16_to_cpu(*(__le16 *)&addr[offset])) & 0x0fff;
}

static inline u8 rsi_get_extended_desc(u8 *addr, u16 offset)
{
	return le16_to_cpu(*((__le16 *)&addr[offset + 4])) & 0x00ff;
}

static inline u8 rsi_get_rssi(u8 *addr)
{
	return *(u8 *)(addr + FRAME_DESC_SZ);
}

static inline u8 rsi_get_channel(u8 *addr)
{
	return *(char *)(addr + 15);
}

int rsi_mgmt_pkt_recv(struct rsi_common *common, u8 *msg);
int rsi_set_vap_capabilities(struct rsi_common *common, enum opmode mode);
int rsi_send_aggregation_params_frame(struct rsi_common *common, u16 tid,
				      u16 ssn, u8 buf_size, u8 event);
int rsi_hal_load_key(struct rsi_common *common, u8 *data, u16 key_len,
		     u8 key_type, u8 key_id, u32 cipher);
int rsi_set_channel(struct rsi_common *common, u16 chno);
void rsi_inform_bss_status(struct rsi_common *common, u8 status,
			   const u8 *bssid, u8 qos_enable, u16 aid);
void rsi_indicate_pkt_to_os(struct rsi_common *common, struct sk_buff *skb);
int rsi_mac80211_attach(struct rsi_common *common);
void rsi_indicate_tx_status(struct rsi_hw *common, struct sk_buff *skb,
			    int status);
bool rsi_is_cipher_wep(struct rsi_common *common);
void rsi_core_qos_processor(struct rsi_common *common);
void rsi_core_xmit(struct rsi_common *common, struct sk_buff *skb);
int rsi_send_mgmt_pkt(struct rsi_common *common, struct sk_buff *skb);
int rsi_send_data_pkt(struct rsi_common *common, struct sk_buff *skb);
#endif
