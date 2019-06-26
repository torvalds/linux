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
#define RSI_NEEDED_HEADROOM             84
#define RSI_RCV_BUFFER_LEN              2000

#define RSI_11B_MODE                    0
#define RSI_11G_MODE                    BIT(7)
#define RETRY_COUNT                     8
#define RETRY_LONG                      4
#define RETRY_SHORT                     7
#define WMM_SHORT_SLOT_TIME             9
#define SIFS_DURATION                   16

#define EAPOL4_PACKET_LEN		0x85
#define KEY_TYPE_CLEAR                  0
#define RSI_PAIRWISE_KEY                1
#define RSI_GROUP_KEY                   2

/* EPPROM_READ_ADDRESS */
#define WLAN_MAC_EEPROM_ADDR            40
#define WLAN_MAC_MAGIC_WORD_LEN         0x01
#define WLAN_HOST_MODE_LEN              0x04
#define WLAN_FW_VERSION_LEN             0x08
#define MAGIC_WORD                      0x5A
#define WLAN_EEPROM_RFTYPE_ADDR		424

/*WOWLAN RESUME WAKEUP TYPES*/
#define RSI_UNICAST_MAGIC_PKT		BIT(0)
#define RSI_BROADCAST_MAGICPKT		BIT(1)
#define RSI_EAPOL_PKT			BIT(2)
#define RSI_DISCONNECT_PKT		BIT(3)
#define RSI_HW_BMISS_PKT		BIT(4)
#define RSI_INSERT_SEQ_IN_FW		BIT(2)

#define WOW_MAX_FILTERS_PER_LIST 16
#define WOW_PATTERN_SIZE 256

/* Receive Frame Types */
#define RSI_RX_DESC_MSG_TYPE_OFFSET	2
#define TA_CONFIRM_TYPE                 0x01
#define RX_DOT11_MGMT                   0x02
#define TX_STATUS_IND                   0x04
#define BEACON_EVENT_IND		0x08
#define EAPOL4_CONFIRM                  1
#define PROBEREQ_CONFIRM                2
#define CARD_READY_IND                  0x00
#define SLEEP_NOTIFY_IND                0x06
#define RSI_TX_STATUS_TYPE		15
#define RSI_TX_STATUS			12

#define RSI_DELETE_PEER                 0x0
#define RSI_ADD_PEER                    0x1
#define START_AMPDU_AGGR                0x1
#define STOP_AMPDU_AGGR                 0x0
#define INTERNAL_MGMT_PKT               0x99

#define PUT_BBP_RESET                   0
#define BBP_REG_WRITE                   0
#define RF_RESET_ENABLE                 BIT(3)
#define RATE_INFO_ENABLE                BIT(0)
#define MORE_DATA_PRESENT		BIT(1)
#define RSI_BROADCAST_PKT               BIT(9)
#define RSI_DESC_REQUIRE_CFM_TO_HOST	BIT(2)
#define RSI_ADD_DELTA_TSF_VAP_ID	BIT(3)
#define RSI_FETCH_RETRY_CNT_FRM_HST	BIT(4)
#define RSI_QOS_ENABLE			BIT(12)
#define RSI_REKEY_PURPOSE		BIT(13)
#define RSI_ENCRYPT_PKT			BIT(15)
#define RSI_SET_PS_ENABLE		BIT(12)

#define RSI_CMDDESC_40MHZ		BIT(4)
#define RSI_CMDDESC_UPPER_20_ENABLE	BIT(5)
#define RSI_CMDDESC_LOWER_20_ENABLE	BIT(6)
#define RSI_CMDDESC_FULL_40_ENABLE	(BIT(5) | BIT(6))
#define UPPER_20_ENABLE                 (0x2 << 12)
#define LOWER_20_ENABLE                 (0x4 << 12)
#define FULL40M_ENABLE                  0x6

#define RSI_LMAC_CLOCK_80MHZ            0x1
#define RSI_ENABLE_40MHZ                (0x1 << 3)
#define ENABLE_SHORTGI_RATE		BIT(9)

#define RX_BA_INDICATION                1
#define RSI_TBL_SZ                      40
#define MAX_RETRIES                     8
#define RSI_IFTYPE_STATION		 0

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
#define RSI_RATE_AUTO			0xffff

#define BW_20MHZ                        0
#define BW_40MHZ                        1

#define EP_2GHZ_20MHZ			0
#define EP_2GHZ_40MHZ			1
#define EP_5GHZ_20MHZ			2
#define EP_5GHZ_40MHZ			3

#define SIFS_TX_11N_VALUE		580
#define SIFS_TX_11B_VALUE		346
#define SHORT_SLOT_VALUE		360
#define LONG_SLOT_VALUE			640
#define OFDM_ACK_TOUT_VALUE		2720
#define CCK_ACK_TOUT_VALUE		9440
#define LONG_PREAMBLE			0x0000
#define SHORT_PREAMBLE			0x0001

#define RSI_SUPP_FILTERS	(FIF_ALLMULTI | FIF_PROBE_REQ |\
				 FIF_BCN_PRBRESP_PROMISC)

#define ANTENNA_SEL_INT			0x02 /* RF_OUT_2 / Integerated */
#define ANTENNA_SEL_UFL			0x03 /* RF_OUT_1 / U.FL */
#define ANTENNA_MASK_VALUE		0x00ff
#define ANTENNA_SEL_TYPE		1

/* Rx filter word definitions */
#define PROMISCOUS_MODE			BIT(0)
#define ALLOW_DATA_ASSOC_PEER		BIT(1)
#define ALLOW_MGMT_ASSOC_PEER		BIT(2)
#define ALLOW_CTRL_ASSOC_PEER		BIT(3)
#define DISALLOW_BEACONS		BIT(4)
#define ALLOW_CONN_PEER_MGMT_WHILE_BUF_FULL BIT(5)
#define DISALLOW_BROADCAST_DATA		BIT(6)

#define RSI_MPDU_DENSITY		0x8
#define RSI_CHAN_RADAR			BIT(7)
#define RSI_BEACON_INTERVAL		200
#define RSI_DTIM_COUNT			2

#define RSI_PS_DISABLE_IND		BIT(15)
#define RSI_PS_ENABLE			1
#define RSI_PS_DISABLE			0
#define RSI_DEEP_SLEEP			1
#define RSI_CONNECTED_SLEEP		2
#define RSI_SLEEP_REQUEST		1
#define RSI_WAKEUP_REQUEST		2

#define RSI_IEEE80211_UAPSD_QUEUES \
	(IEEE80211_WMM_IE_STA_QOSINFO_AC_VO | \
	 IEEE80211_WMM_IE_STA_QOSINFO_AC_VI | \
	 IEEE80211_WMM_IE_STA_QOSINFO_AC_BE | \
	 IEEE80211_WMM_IE_STA_QOSINFO_AC_BK)

#define RSI_DESC_VAP_ID_MASK		0xC000u
#define RSI_DESC_VAP_ID_OFST		14
#define RSI_DATA_DESC_MAC_BBP_INFO	BIT(0)
#define RSI_DATA_DESC_NO_ACK_IND	BIT(9)
#define RSI_DATA_DESC_QOS_EN		BIT(12)
#define RSI_DATA_DESC_NORMAL_FRAME	0x00
#define RSI_DATA_DESC_DTIM_BEACON_GATED_FRAME	BIT(10)
#define RSI_DATA_DESC_BEACON_FRAME	BIT(11)
#define RSI_DATA_DESC_DTIM_BEACON	(BIT(10) | BIT(11))
#define RSI_DATA_DESC_INSERT_TSF	BIT(15)
#define RSI_DATA_DESC_INSERT_SEQ_NO	BIT(2)

#ifdef CONFIG_PM
#define RSI_WOW_ANY			BIT(1)
#define RSI_WOW_GTK_REKEY		BIT(3)
#define RSI_WOW_MAGIC_PKT		BIT(4)
#define RSI_WOW_DISCONNECT		BIT(5)
#endif

#define RSI_MAX_TX_AGGR_FRMS		8
#define RSI_MAX_RX_AGGR_FRMS		8

#define RSI_MAX_SCAN_SSIDS		16
#define RSI_MAX_SCAN_IE_LEN		256

enum opmode {
	RSI_OPMODE_UNSUPPORTED = -1,
	RSI_OPMODE_AP = 0,
	RSI_OPMODE_STA,
	RSI_OPMODE_P2P_GO,
	RSI_OPMODE_P2P_CLIENT
};

enum vap_status {
	VAP_ADD = 1,
	VAP_DELETE = 2,
	VAP_UPDATE = 3
};

enum peer_type {
	PEER_TYPE_AP,
	PEER_TYPE_STA,
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
	BLOCK_HW_QUEUE,
	SET_KEY_REQ,
	AUTO_RATE_IND,
	BOOTUP_PARAMS_REQUEST,
	VAP_CAPABILITIES,
	EEPROM_READ,
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
	PER_CMD_PKT,
	ANT_SEL_FRAME = 0x20,
	VAP_DYNAMIC_UPDATE = 0x27,
	COMMON_DEV_CONFIG = 0x28,
	RADIO_PARAMS_UPDATE = 0x29,
	WOWLAN_CONFIG_PARAMS = 0x2B,
	FEATURES_ENABLE = 0x33,
	WOWLAN_WAKEUP_REASON = 0xc5
};

struct rsi_mac_frame {
	__le16 desc_word[8];
} __packed;

#define PWR_SAVE_WAKEUP_IND		BIT(0)
#define TCP_CHECK_SUM_OFFLOAD		BIT(1)
#define CONFIRM_REQUIRED_TO_HOST	BIT(2)
#define ADD_DELTA_TSF			BIT(3)
#define FETCH_RETRY_CNT_FROM_HOST_DESC	BIT(4)
#define EOSP_INDICATION			BIT(5)
#define REQUIRE_TSF_SYNC_CONFIRM	BIT(6)
#define ENCAP_MGMT_PKT			BIT(7)
#define DESC_IMMEDIATE_WAKEUP		BIT(15)

struct rsi_xtended_desc {
	u8 confirm_frame_type;
	u8 retry_cnt;
	u16 reserved;
};

struct rsi_cmd_desc_dword0 {
	__le16 len_qno;
	u8 frame_type;
	u8 misc_flags;
};

struct rsi_cmd_desc_dword1 {
	u8 xtend_desc_size;
	u8 reserved1;
	__le16 reserved2;
};

struct rsi_cmd_desc_dword2 {
	__le32 pkt_info; /* Packet specific data */
};

struct rsi_cmd_desc_dword3 {
	__le16 token;
	u8 qid_tid;
	u8 sta_id;
};

struct rsi_cmd_desc {
	struct rsi_cmd_desc_dword0 desc_dword0;
	struct rsi_cmd_desc_dword1 desc_dword1;
	struct rsi_cmd_desc_dword2 desc_dword2;
	struct rsi_cmd_desc_dword3 desc_dword3;
};

struct rsi_boot_params {
	__le16 desc_word[8];
	struct bootup_params bootup_params;
} __packed;

struct rsi_boot_params_9116 {
	struct rsi_cmd_desc_dword0 desc_dword0;
	struct rsi_cmd_desc_dword1 desc_dword1;
	struct rsi_cmd_desc_dword2 desc_dword2;
	__le16 reserved;
	__le16 umac_clk;
	struct bootup_params_9116 bootup_params;
} __packed;

struct rsi_peer_notify {
	struct rsi_cmd_desc desc;
	u8 mac_addr[6];
	__le16 command;
	__le16 mpdu_density;
	__le16 reserved;
	__le32 sta_flags;
} __packed;

/* Aggregation params flags */
#define RSI_AGGR_PARAMS_TID_MASK	0xf
#define RSI_AGGR_PARAMS_START		BIT(4)
#define RSI_AGGR_PARAMS_RX_AGGR		BIT(5)
struct rsi_aggr_params {
	struct rsi_cmd_desc_dword0 desc_dword0;
	struct rsi_cmd_desc_dword0 desc_dword1;
	__le16 seq_start;
	__le16 baw_size;
	__le16 token;
	u8 aggr_params;
	u8 peer_id;
} __packed;

struct rsi_bb_rf_prog {
	struct rsi_cmd_desc_dword0 desc_dword0;
	__le16 reserved1;
	u8 rf_power_mode;
	u8 reserved2;
	u8 endpoint;
	u8 reserved3;
	__le16 reserved4;
	__le16 reserved5;
	__le16 flags;
} __packed;

struct rsi_chan_config {
	struct rsi_cmd_desc_dword0 desc_dword0;
	struct rsi_cmd_desc_dword1 desc_dword1;
	u8 channel_number;
	u8 antenna_gain_offset_2g;
	u8 antenna_gain_offset_5g;
	u8 channel_width;
	__le16 tx_power;
	u8 region_rftype;
	u8 flags;
} __packed;

struct rsi_vap_caps {
	struct rsi_cmd_desc_dword0 desc_dword0;
	u8 reserved1;
	u8 status;
	__le16 reserved2;
	u8 vif_type;
	u8 channel_bw;
	__le16 antenna_info;
	__le16 token;
	u8 radioid_macid;
	u8 vap_id;
	u8 mac_addr[6];
	__le16 keep_alive_period;
	u8 bssid[6];
	__le16 reserved4;
	__le32 flags;
	__le16 frag_threshold;
	__le16 rts_threshold;
	__le32 default_mgmt_rate;
	__le16 default_ctrl_rate;
	__le16 ctrl_rate_flags;
	__le32 default_data_rate;
	__le16 beacon_interval;
	__le16 dtim_period;
	__le16 beacon_miss_threshold;
} __packed;

struct rsi_ant_sel_frame {
	struct rsi_cmd_desc_dword0 desc_dword0;
	u8 reserved;
	u8 sub_frame_type;
	__le16 ant_value;
	__le32 reserved1;
	__le32 reserved2;
} __packed;

struct rsi_dynamic_s {
	struct rsi_cmd_desc_dword0 desc_dword0;
	struct rsi_cmd_desc_dword1 desc_dword1;
	struct rsi_cmd_desc_dword2 desc_dword2;
	struct rsi_cmd_desc_dword3 desc_dword3;
	struct framebody {
		__le16 data_rate;
		__le16 mgmt_rate;
		__le16 keep_alive_period;
	} frame_body;
} __packed;

/* Key descriptor flags */
#define RSI_KEY_TYPE_BROADCAST	BIT(1)
#define RSI_WEP_KEY		BIT(2)
#define RSI_WEP_KEY_104		BIT(3)
#define RSI_CIPHER_WPA		BIT(4)
#define RSI_CIPHER_TKIP		BIT(5)
#define RSI_KEY_MODE_AP		BIT(7)
#define RSI_PROTECT_DATA_FRAMES	BIT(13)
#define RSI_KEY_ID_MASK		0xC0
#define RSI_KEY_ID_OFFSET	14
struct rsi_set_key {
	struct rsi_cmd_desc_dword0 desc_dword0;
	struct rsi_cmd_desc_dword1 desc_dword1;
	__le16 key_desc;
	__le32 bpn;
	u8 sta_id;
	u8 vap_id;
	u8 key[4][32];
	u8 tx_mic_key[8];
	u8 rx_mic_key[8];
} __packed;

struct rsi_auto_rate {
	struct rsi_cmd_desc desc;
	__le16 failure_limit;
	__le16 initial_boundary;
	__le16 max_threshold_limt;
	__le16 num_supported_rates;
	__le16 aarf_rssi;
	__le16 moderate_rate_inx;
	__le16 collision_tolerance;
	__le16 supported_rates[40];
} __packed;

#define QUIET_INFO_VALID	BIT(0)
#define QUIET_ENABLE		BIT(1)
struct rsi_block_unblock_data {
	struct rsi_cmd_desc_dword0 desc_dword0;
	u8 xtend_desc_size;
	u8 host_quiet_info;
	__le16 reserved;
	__le16 block_q_bitmap;
	__le16 unblock_q_bitmap;
	__le16 token;
	__le16 flush_q_bitmap;
} __packed;

struct qos_params {
	__le16 cont_win_min_q;
	__le16 cont_win_max_q;
	__le16 aifsn_val_q;
	__le16 txop_q;
} __packed;

struct rsi_radio_caps {
	struct rsi_cmd_desc_dword0 desc_dword0;
	struct rsi_cmd_desc_dword0 desc_dword1;
	u8 channel_num;
	u8 rf_model;
	__le16 ppe_ack_rate;
	__le16 mode_11j;
	u8 radio_cfg_info;
	u8 radio_info;
	struct qos_params qos_params[MAX_HW_QUEUES];
	u8 num_11n_rates;
	u8 num_11ac_rates;
	__le16 gcpd_per_rate[20];
	__le16 sifs_tx_11n;
	__le16 sifs_tx_11b;
	__le16 slot_rx_11n;
	__le16 ofdm_ack_tout;
	__le16 cck_ack_tout;
	__le16 preamble_type;
} __packed;

/* ULP GPIO flags */
#define RSI_GPIO_MOTION_SENSOR_ULP_WAKEUP	BIT(0)
#define RSI_GPIO_SLEEP_IND_FROM_DEVICE		BIT(1)
#define RSI_GPIO_2_ULP				BIT(2)
#define RSI_GPIO_PUSH_BUTTON_ULP_WAKEUP		BIT(3)

/* SOC GPIO flags */
#define RSI_GPIO_0_PSPI_CSN_0			BIT(0)
#define RSI_GPIO_1_PSPI_CSN_1			BIT(1)
#define RSI_GPIO_2_HOST_WAKEUP_INTR		BIT(2)
#define RSI_GPIO_3_PSPI_DATA_0			BIT(3)
#define RSI_GPIO_4_PSPI_DATA_1			BIT(4)
#define RSI_GPIO_5_PSPI_DATA_2			BIT(5)
#define RSI_GPIO_6_PSPI_DATA_3			BIT(6)
#define RSI_GPIO_7_I2C_SCL			BIT(7)
#define RSI_GPIO_8_I2C_SDA			BIT(8)
#define RSI_GPIO_9_UART1_RX			BIT(9)
#define RSI_GPIO_10_UART1_TX			BIT(10)
#define RSI_GPIO_11_UART1_RTS_I2S_CLK		BIT(11)
#define RSI_GPIO_12_UART1_CTS_I2S_WS		BIT(12)
#define RSI_GPIO_13_DBG_UART_RX_I2S_DIN		BIT(13)
#define RSI_GPIO_14_DBG_UART_RX_I2S_DOUT	BIT(14)
#define RSI_GPIO_15_LP_WAKEUP_BOOT_BYPASS	BIT(15)
#define RSI_GPIO_16_LED_0			BIT(16)
#define RSI_GPIO_17_BTCOEX_WLAN_ACT_EXT_ANT_SEL	BIT(17)
#define RSI_GPIO_18_BTCOEX_BT_PRIO_EXT_ANT_SEL	BIT(18)
#define RSI_GPIO_19_BTCOEX_BT_ACT_EXT_ON_OFF	BIT(19)
#define RSI_GPIO_20_RF_RESET			BIT(20)
#define RSI_GPIO_21_SLEEP_IND_FROM_DEVICE	BIT(21)

#define RSI_UNUSED_SOC_GPIO_BITMAP (RSI_GPIO_9_UART1_RX | \
				    RSI_GPIO_10_UART1_TX | \
				    RSI_GPIO_11_UART1_RTS_I2S_CLK | \
				    RSI_GPIO_12_UART1_CTS_I2S_WS | \
				    RSI_GPIO_13_DBG_UART_RX_I2S_DIN | \
				    RSI_GPIO_14_DBG_UART_RX_I2S_DOUT | \
				    RSI_GPIO_15_LP_WAKEUP_BOOT_BYPASS | \
				    RSI_GPIO_17_BTCOEX_WLAN_ACT_EXT_ANT_SEL | \
				    RSI_GPIO_18_BTCOEX_BT_PRIO_EXT_ANT_SEL | \
				    RSI_GPIO_19_BTCOEX_BT_ACT_EXT_ON_OFF | \
				    RSI_GPIO_21_SLEEP_IND_FROM_DEVICE)

#define RSI_UNUSED_ULP_GPIO_BITMAP (RSI_GPIO_MOTION_SENSOR_ULP_WAKEUP | \
				    RSI_GPIO_SLEEP_IND_FROM_DEVICE | \
				    RSI_GPIO_2_ULP | \
				    RSI_GPIO_PUSH_BUTTON_ULP_WAKEUP);
struct rsi_config_vals {
	__le16 len_qno;
	u8 pkt_type;
	u8 misc_flags;
	__le16 reserved1[6];
	u8 lp_ps_handshake;
	u8 ulp_ps_handshake;
	u8 sleep_config_params; /* 0 for no handshake,
				 * 1 for GPIO based handshake,
				 * 2 packet handshake
				 */
	u8 unused_ulp_gpio;
	__le32 unused_soc_gpio_bitmap;
	u8 ext_pa_or_bt_coex_en;
	u8 opermode;
	u8 wlan_rf_pwr_mode;
	u8 bt_rf_pwr_mode;
	u8 zigbee_rf_pwr_mode;
	u8 driver_mode;
	u8 region_code;
	u8 antenna_sel_val;
	u8 reserved2[16];
} __packed;

/* Packet info flags */
#define RSI_EEPROM_HDR_SIZE_OFFSET		8
#define RSI_EEPROM_HDR_SIZE_MASK		0x300
#define RSI_EEPROM_LEN_OFFSET			20
#define RSI_EEPROM_LEN_MASK			0xFFF00000

struct rsi_eeprom_read_frame {
	__le16 len_qno;
	u8 pkt_type;
	u8 misc_flags;
	__le32 pkt_info;
	__le32 eeprom_offset;
	__le16 delay_ms;
	__le16 reserved3;
} __packed;

struct rsi_request_ps {
	struct rsi_cmd_desc desc;
	struct ps_sleep_params ps_sleep;
	u8 ps_mimic_support;
	u8 ps_uapsd_acs;
	u8 ps_uapsd_wakeup_period;
	u8 reserved;
	__le32 ps_listen_interval;
	__le32 ps_dtim_interval_duration;
	__le16 ps_num_dtim_intervals;
} __packed;

struct rsi_wowlan_req {
	struct rsi_cmd_desc desc;
	u8 sourceid[ETH_ALEN];
	u16 wow_flags;
	u16 host_sleep_status;
} __packed;

#define RSI_START_BGSCAN		1
#define RSI_STOP_BGSCAN			0
#define HOST_BG_SCAN_TRIG		BIT(4)
struct rsi_bgscan_config {
	struct rsi_cmd_desc_dword0 desc_dword0;
	__le64 reserved;
	__le32 reserved1;
	__le16 bgscan_threshold;
	__le16 roam_threshold;
	__le16 bgscan_periodicity;
	u8 num_bgscan_channels;
	u8 two_probe;
	__le16 active_scan_duration;
	__le16 passive_scan_duration;
	__le16 channels2scan[MAX_BGSCAN_CHANNELS_DUAL_BAND];
} __packed;

struct rsi_bgscan_probe {
	struct rsi_cmd_desc_dword0 desc_dword0;
	__le64 reserved;
	__le32 reserved1;
	__le16 mgmt_rate;
	__le16 flags;
	__le16 def_chan;
	__le16 channel_scan_time;
	__le16 probe_req_length;
} __packed;

#define RSI_DUTY_CYCLING	BIT(0)
#define RSI_END_OF_FRAME	BIT(1)
#define RSI_SIFS_TX_ENABLE	BIT(2)
#define RSI_DPD			BIT(3)
struct rsi_wlan_9116_features {
	struct rsi_cmd_desc desc;
	u8 pll_mode;
	u8 rf_type;
	u8 wireless_mode;
	u8 enable_ppe;
	u8 afe_type;
	u8 reserved1;
	__le16 reserved2;
	__le32 feature_enable;
};

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

static inline void rsi_set_len_qno(__le16 *addr, u16 len, u8 qno)
{
	*addr = cpu_to_le16(len | ((qno & 7) << 12));
}

int rsi_handle_card_ready(struct rsi_common *common, u8 *msg);
int rsi_mgmt_pkt_recv(struct rsi_common *common, u8 *msg);
int rsi_set_vap_capabilities(struct rsi_common *common, enum opmode mode,
			     u8 *mac_addr, u8 vap_id, u8 vap_status);
int rsi_send_aggregation_params_frame(struct rsi_common *common, u16 tid,
				      u16 ssn, u8 buf_size, u8 event,
				      u8 sta_id);
int rsi_hal_load_key(struct rsi_common *common, u8 *data, u16 key_len,
		     u8 key_type, u8 key_id, u32 cipher, s16 sta_id,
		     struct ieee80211_vif *vif);
int rsi_set_channel(struct rsi_common *common,
		    struct ieee80211_channel *channel);
int rsi_send_vap_dynamic_update(struct rsi_common *common);
int rsi_send_block_unblock_frame(struct rsi_common *common, bool event);
int rsi_hal_send_sta_notify_frame(struct rsi_common *common, enum opmode opmode,
				  u8 notify_event, const unsigned char *bssid,
				  u8 qos_enable, u16 aid, u16 sta_id,
				  struct ieee80211_vif *vif);
void rsi_inform_bss_status(struct rsi_common *common, enum opmode opmode,
			   u8 status, const u8 *addr, u8 qos_enable, u16 aid,
			   struct ieee80211_sta *sta, u16 sta_id,
			   u16 assoc_cap, struct ieee80211_vif *vif);
void rsi_indicate_pkt_to_os(struct rsi_common *common, struct sk_buff *skb);
int rsi_mac80211_attach(struct rsi_common *common);
void rsi_indicate_tx_status(struct rsi_hw *common, struct sk_buff *skb,
			    int status);
bool rsi_is_cipher_wep(struct rsi_common *common);
void rsi_core_qos_processor(struct rsi_common *common);
void rsi_core_xmit(struct rsi_common *common, struct sk_buff *skb);
int rsi_send_mgmt_pkt(struct rsi_common *common, struct sk_buff *skb);
int rsi_send_data_pkt(struct rsi_common *common, struct sk_buff *skb);
int rsi_band_check(struct rsi_common *common, struct ieee80211_channel *chan);
int rsi_send_rx_filter_frame(struct rsi_common *common, u16 rx_filter_word);
int rsi_send_radio_params_update(struct rsi_common *common);
int rsi_set_antenna(struct rsi_common *common, u8 antenna);
#ifdef CONFIG_PM
int rsi_send_wowlan_request(struct rsi_common *common, u16 flags,
			    u16 sleep_status);
#endif
int rsi_send_ps_request(struct rsi_hw *adapter, bool enable,
			struct ieee80211_vif *vif);
void init_bgscan_params(struct rsi_common *common);
int rsi_send_bgscan_params(struct rsi_common *common, int enable);
int rsi_send_bgscan_probe_req(struct rsi_common *common,
			      struct ieee80211_vif *vif);
#endif
