/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2019 MediaTek Inc. */

#ifndef __MT7615_MCU_H
#define __MT7615_MCU_H

struct mt7615_mcu_txd {
	__le32 txd[8];

	__le16 len;
	__le16 pq_id;

	u8 cid;
	u8 pkt_type;
	u8 set_query; /* FW don't care */
	u8 seq;

	u8 uc_d2b0_rev;
	u8 ext_cid;
	u8 s2d_index;
	u8 ext_cid_ack;

	u32 reserved[5];
} __packed __aligned(4);

/**
 * struct mt7615_uni_txd - mcu command descriptor for firmware v3
 * @txd: hardware descriptor
 * @len: total length not including txd
 * @cid: command identifier
 * @pkt_type: must be 0xa0 (cmd packet by long format)
 * @frag_n: fragment number
 * @seq: sequence number
 * @checksum: 0 mean there is no checksum
 * @s2d_index: index for command source and destination
 *  Definition              | value | note
 *  CMD_S2D_IDX_H2N         | 0x00  | command from HOST to WM
 *  CMD_S2D_IDX_C2N         | 0x01  | command from WA to WM
 *  CMD_S2D_IDX_H2C         | 0x02  | command from HOST to WA
 *  CMD_S2D_IDX_H2N_AND_H2C | 0x03  | command from HOST to WA and WM
 *
 * @option: command option
 *  BIT[0]: UNI_CMD_OPT_BIT_ACK
 *          set to 1 to request a fw reply
 *          if UNI_CMD_OPT_BIT_0_ACK is set and UNI_CMD_OPT_BIT_2_SET_QUERY
 *          is set, mcu firmware will send response event EID = 0x01
 *          (UNI_EVENT_ID_CMD_RESULT) to the host.
 *  BIT[1]: UNI_CMD_OPT_BIT_UNI_CMD
 *          0: original command
 *          1: unified command
 *  BIT[2]: UNI_CMD_OPT_BIT_SET_QUERY
 *          0: QUERY command
 *          1: SET command
 */
struct mt7615_uni_txd {
	__le32 txd[8];

	/* DW1 */
	__le16 len;
	__le16 cid;

	/* DW2 */
	u8 reserved;
	u8 pkt_type;
	u8 frag_n;
	u8 seq;

	/* DW3 */
	__le16 checksum;
	u8 s2d_index;
	u8 option;

	/* DW4 */
	u8 reserved2[4];
} __packed __aligned(4);

/* event table */
enum {
	MCU_EVENT_TARGET_ADDRESS_LEN = 0x01,
	MCU_EVENT_FW_START = 0x01,
	MCU_EVENT_GENERIC = 0x01,
	MCU_EVENT_ACCESS_REG = 0x02,
	MCU_EVENT_MT_PATCH_SEM = 0x04,
	MCU_EVENT_REG_ACCESS = 0x05,
	MCU_EVENT_SCAN_DONE = 0x0d,
	MCU_EVENT_ROC = 0x10,
	MCU_EVENT_BSS_ABSENCE  = 0x11,
	MCU_EVENT_BSS_BEACON_LOSS = 0x13,
	MCU_EVENT_CH_PRIVILEGE = 0x18,
	MCU_EVENT_SCHED_SCAN_DONE = 0x23,
	MCU_EVENT_EXT = 0xed,
	MCU_EVENT_RESTART_DL = 0xef,
};

/* ext event table */
enum {
	MCU_EXT_EVENT_PS_SYNC = 0x5,
	MCU_EXT_EVENT_FW_LOG_2_HOST = 0x13,
	MCU_EXT_EVENT_THERMAL_PROTECT = 0x22,
	MCU_EXT_EVENT_ASSERT_DUMP = 0x23,
	MCU_EXT_EVENT_RDD_REPORT = 0x3a,
	MCU_EXT_EVENT_CSA_NOTIFY = 0x4f,
};

enum {
    MT_SKU_CCK_1_2 = 0,
    MT_SKU_CCK_55_11,
    MT_SKU_OFDM_6_9,
    MT_SKU_OFDM_12_18,
    MT_SKU_OFDM_24_36,
    MT_SKU_OFDM_48,
    MT_SKU_OFDM_54,
    MT_SKU_HT20_0_8,
    MT_SKU_HT20_32,
    MT_SKU_HT20_1_2_9_10,
    MT_SKU_HT20_3_4_11_12,
    MT_SKU_HT20_5_13,
    MT_SKU_HT20_6_14,
    MT_SKU_HT20_7_15,
    MT_SKU_HT40_0_8,
    MT_SKU_HT40_32,
    MT_SKU_HT40_1_2_9_10,
    MT_SKU_HT40_3_4_11_12,
    MT_SKU_HT40_5_13,
    MT_SKU_HT40_6_14,
    MT_SKU_HT40_7_15,
    MT_SKU_VHT20_0,
    MT_SKU_VHT20_1_2,
    MT_SKU_VHT20_3_4,
    MT_SKU_VHT20_5_6,
    MT_SKU_VHT20_7,
    MT_SKU_VHT20_8,
    MT_SKU_VHT20_9,
    MT_SKU_VHT40_0,
    MT_SKU_VHT40_1_2,
    MT_SKU_VHT40_3_4,
    MT_SKU_VHT40_5_6,
    MT_SKU_VHT40_7,
    MT_SKU_VHT40_8,
    MT_SKU_VHT40_9,
    MT_SKU_VHT80_0,
    MT_SKU_VHT80_1_2,
    MT_SKU_VHT80_3_4,
    MT_SKU_VHT80_5_6,
    MT_SKU_VHT80_7,
    MT_SKU_VHT80_8,
    MT_SKU_VHT80_9,
    MT_SKU_VHT160_0,
    MT_SKU_VHT160_1_2,
    MT_SKU_VHT160_3_4,
    MT_SKU_VHT160_5_6,
    MT_SKU_VHT160_7,
    MT_SKU_VHT160_8,
    MT_SKU_VHT160_9,
    MT_SKU_1SS_DELTA,
    MT_SKU_2SS_DELTA,
    MT_SKU_3SS_DELTA,
    MT_SKU_4SS_DELTA,
};

struct mt7615_mcu_rxd {
	__le32 rxd[4];

	__le16 len;
	__le16 pkt_type_id;

	u8 eid;
	u8 seq;
	__le16 __rsv;

	u8 ext_eid;
	u8 __rsv1[2];
	u8 s2d_index;
};

struct mt7615_mcu_rdd_report {
	struct mt7615_mcu_rxd rxd;

	u8 idx;
	u8 long_detected;
	u8 constant_prf_detected;
	u8 staggered_prf_detected;
	u8 radar_type_idx;
	u8 periodic_pulse_num;
	u8 long_pulse_num;
	u8 hw_pulse_num;

	u8 out_lpn;
	u8 out_spn;
	u8 out_crpn;
	u8 out_crpw;
	u8 out_crbn;
	u8 out_stgpn;
	u8 out_stgpw;

	u8 _rsv[2];

	__le32 out_pri_const;
	__le32 out_pri_stg[3];

	struct {
		__le32 start;
		__le16 pulse_width;
		__le16 pulse_power;
	} long_pulse[32];

	struct {
		__le32 start;
		__le16 pulse_width;
		__le16 pulse_power;
	} periodic_pulse[32];

	struct {
		__le32 start;
		__le16 pulse_width;
		__le16 pulse_power;
		u8 sc_pass;
		u8 sw_reset;
	} hw_pulse[32];
};

#define MCU_PQ_ID(p, q)		(((p) << 15) | ((q) << 10))
#define MCU_PKT_ID		0xa0

enum {
	MCU_Q_QUERY,
	MCU_Q_SET,
	MCU_Q_RESERVED,
	MCU_Q_NA
};

enum {
	MCU_S2D_H2N,
	MCU_S2D_C2N,
	MCU_S2D_H2C,
	MCU_S2D_H2CN
};

#define MCU_FW_PREFIX		BIT(31)
#define MCU_UNI_PREFIX		BIT(30)
#define MCU_CE_PREFIX		BIT(29)
#define MCU_QUERY_PREFIX	BIT(28)
#define MCU_CMD_MASK		~(MCU_FW_PREFIX | MCU_UNI_PREFIX |	\
				  MCU_CE_PREFIX | MCU_QUERY_PREFIX)

#define MCU_QUERY_MASK		BIT(16)

enum {
	MCU_CMD_TARGET_ADDRESS_LEN_REQ = MCU_FW_PREFIX | 0x01,
	MCU_CMD_FW_START_REQ = MCU_FW_PREFIX | 0x02,
	MCU_CMD_INIT_ACCESS_REG = 0x3,
	MCU_CMD_PATCH_START_REQ = 0x05,
	MCU_CMD_PATCH_FINISH_REQ = MCU_FW_PREFIX | 0x07,
	MCU_CMD_PATCH_SEM_CONTROL = MCU_FW_PREFIX | 0x10,
	MCU_CMD_EXT_CID = 0xED,
	MCU_CMD_FW_SCATTER = MCU_FW_PREFIX | 0xEE,
	MCU_CMD_RESTART_DL_REQ = MCU_FW_PREFIX | 0xEF,
};

enum {
	MCU_EXT_CMD_RF_REG_ACCESS = 0x02,
	MCU_EXT_CMD_PM_STATE_CTRL = 0x07,
	MCU_EXT_CMD_CHANNEL_SWITCH = 0x08,
	MCU_EXT_CMD_SET_TX_POWER_CTRL = 0x11,
	MCU_EXT_CMD_FW_LOG_2_HOST = 0x13,
	MCU_EXT_CMD_EFUSE_BUFFER_MODE = 0x21,
	MCU_EXT_CMD_STA_REC_UPDATE = 0x25,
	MCU_EXT_CMD_BSS_INFO_UPDATE = 0x26,
	MCU_EXT_CMD_EDCA_UPDATE = 0x27,
	MCU_EXT_CMD_DEV_INFO_UPDATE = 0x2A,
	MCU_EXT_CMD_GET_TEMP = 0x2c,
	MCU_EXT_CMD_WTBL_UPDATE = 0x32,
	MCU_EXT_CMD_SET_RDD_CTRL = 0x3a,
	MCU_EXT_CMD_ATE_CTRL = 0x3d,
	MCU_EXT_CMD_PROTECT_CTRL = 0x3e,
	MCU_EXT_CMD_DBDC_CTRL = 0x45,
	MCU_EXT_CMD_MAC_INIT_CTRL = 0x46,
	MCU_EXT_CMD_MUAR_UPDATE = 0x48,
	MCU_EXT_CMD_BCN_OFFLOAD = 0x49,
	MCU_EXT_CMD_SET_RX_PATH = 0x4e,
	MCU_EXT_CMD_TX_POWER_FEATURE_CTRL = 0x58,
	MCU_EXT_CMD_RXDCOC_CAL = 0x59,
	MCU_EXT_CMD_TXDPD_CAL = 0x60,
	MCU_EXT_CMD_SET_RDD_TH = 0x7c,
	MCU_EXT_CMD_SET_RDD_PATTERN = 0x7d,
};

enum {
	MCU_UNI_CMD_DEV_INFO_UPDATE = MCU_UNI_PREFIX | 0x01,
	MCU_UNI_CMD_BSS_INFO_UPDATE = MCU_UNI_PREFIX | 0x02,
	MCU_UNI_CMD_STA_REC_UPDATE = MCU_UNI_PREFIX | 0x03,
	MCU_UNI_CMD_SUSPEND = MCU_UNI_PREFIX | 0x05,
	MCU_UNI_CMD_OFFLOAD = MCU_UNI_PREFIX | 0x06,
	MCU_UNI_CMD_HIF_CTRL = MCU_UNI_PREFIX | 0x07,
};

enum {
	MCU_ATE_SET_FREQ_OFFSET = 0xa,
	MCU_ATE_SET_TX_POWER_CONTROL = 0x15,
};

struct mt7615_mcu_uni_event {
	u8 cid;
	u8 pad[3];
	__le32 status; /* 0: success, others: fail */
} __packed;

struct mt7615_beacon_loss_event {
	u8 bss_idx;
	u8 reason;
	u8 pad[2];
} __packed;

struct mt7615_mcu_scan_ssid {
	__le32 ssid_len;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
} __packed;

struct mt7615_mcu_scan_channel {
	u8 band; /* 1: 2.4GHz
		  * 2: 5.0GHz
		  * Others: Reserved
		  */
	u8 channel_num;
} __packed;

struct mt7615_mcu_scan_match {
	__le32 rssi_th;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 ssid_len;
	u8 rsv[3];
} __packed;

struct mt7615_hw_scan_req {
	u8 seq_num;
	u8 bss_idx;
	u8 scan_type; /* 0: PASSIVE SCAN
		       * 1: ACTIVE SCAN
		       */
	u8 ssid_type; /* BIT(0) wildcard SSID
		       * BIT(1) P2P wildcard SSID
		       * BIT(2) specified SSID + wildcard SSID
		       * BIT(2) + ssid_type_ext BIT(0) specified SSID only
		       */
	u8 ssids_num;
	u8 probe_req_num; /* Number of probe request for each SSID */
	u8 scan_func; /* BIT(0) Enable random MAC scan
		       * BIT(1) Disable DBDC scan type 1~3.
		       * BIT(2) Use DBDC scan type 3 (dedicated one RF to scan).
		       */
	u8 version; /* 0: Not support fields after ies.
		     * 1: Support fields after ies.
		     */
	struct mt7615_mcu_scan_ssid ssids[4];
	__le16 probe_delay_time;
	__le16 channel_dwell_time; /* channel Dwell interval */
	__le16 timeout_value;
	u8 channel_type; /* 0: Full channels
			  * 1: Only 2.4GHz channels
			  * 2: Only 5GHz channels
			  * 3: P2P social channel only (channel #1, #6 and #11)
			  * 4: Specified channels
			  * Others: Reserved
			  */
	u8 channels_num; /* valid when channel_type is 4 */
	/* valid when channels_num is set */
	struct mt7615_mcu_scan_channel channels[32];
	__le16 ies_len;
	u8 ies[MT7615_SCAN_IE_LEN];
	/* following fields are valid if version > 0 */
	u8 ext_channels_num;
	u8 ext_ssids_num;
	__le16 channel_min_dwell_time;
	struct mt7615_mcu_scan_channel ext_channels[32];
	struct mt7615_mcu_scan_ssid ext_ssids[6];
	u8 bssid[ETH_ALEN];
	u8 random_mac[ETH_ALEN]; /* valid when BIT(1) in scan_func is set. */
	u8 pad[63];
	u8 ssid_type_ext;
} __packed;

#define SCAN_DONE_EVENT_MAX_CHANNEL_NUM	64
struct mt7615_hw_scan_done {
	u8 seq_num;
	u8 sparse_channel_num;
	struct mt7615_mcu_scan_channel sparse_channel;
	u8 complete_channel_num;
	u8 current_state;
	u8 version;
	u8 pad;
	__le32 beacon_scan_num;
	u8 pno_enabled;
	u8 pad2[3];
	u8 sparse_channel_valid_num;
	u8 pad3[3];
	u8 channel_num[SCAN_DONE_EVENT_MAX_CHANNEL_NUM];
	/* idle format for channel_idle_time
	 * 0: first bytes: idle time(ms) 2nd byte: dwell time(ms)
	 * 1: first bytes: idle time(8ms) 2nd byte: dwell time(8ms)
	 * 2: dwell time (16us)
	 */
	__le16 channel_idle_time[SCAN_DONE_EVENT_MAX_CHANNEL_NUM];
	/* beacon and probe response count */
	u8 beacon_probe_num[SCAN_DONE_EVENT_MAX_CHANNEL_NUM];
	u8 mdrdy_count[SCAN_DONE_EVENT_MAX_CHANNEL_NUM];
	__le32 beacon_2g_num;
	__le32 beacon_5g_num;
} __packed;

struct mt7615_sched_scan_req {
	u8 version;
	u8 seq_num;
	u8 stop_on_match;
	u8 ssids_num;
	u8 match_num;
	u8 pad;
	__le16 ie_len;
	struct mt7615_mcu_scan_ssid ssids[MT7615_MAX_SCHED_SCAN_SSID];
	struct mt7615_mcu_scan_match match[MT7615_MAX_SCAN_MATCH];
	u8 channel_type;
	u8 channels_num;
	u8 intervals_num;
	u8 scan_func; /* BIT(0) eable random mac address */
	struct mt7615_mcu_scan_channel channels[64];
	__le16 intervals[MT7615_MAX_SCHED_SCAN_INTERVAL];
	u8 random_mac[ETH_ALEN]; /* valid when BIT(0) in scan_func is set */
	u8 pad2[58];
} __packed;

struct nt7615_sched_scan_done {
	u8 seq_num;
	u8 status; /* 0: ssid found */
	__le16 pad;
} __packed;

struct mt7615_mcu_reg_event {
	__le32 reg;
	__le32 val;
} __packed;

struct mt7615_mcu_bss_event {
	u8 bss_idx;
	u8 is_absent;
	u8 free_quota;
	u8 pad;
} __packed;

struct mt7615_bss_basic_tlv {
	__le16 tag;
	__le16 len;
	u8 active;
	u8 omac_idx;
	u8 hw_bss_idx;
	u8 band_idx;
	__le32 conn_type;
	u8 conn_state;
	u8 wmm_idx;
	u8 bssid[ETH_ALEN];
	__le16 bmc_tx_wlan_idx;
	__le16 bcn_interval;
	u8 dtim_period;
	u8 phymode; /* bit(0): A
		     * bit(1): B
		     * bit(2): G
		     * bit(3): GN
		     * bit(4): AN
		     * bit(5): AC
		     */
	__le16 sta_idx;
	u8 nonht_basic_phy;
	u8 pad[3];
} __packed;

struct mt7615_bss_qos_tlv {
	__le16 tag;
	__le16 len;
	u8 qos;
	u8 pad[3];
} __packed;

enum {
	WOW_USB = 1,
	WOW_PCIE = 2,
	WOW_GPIO = 3,
};

struct mt7615_wow_ctrl_tlv {
	__le16 tag;
	__le16 len;
	u8 cmd; /* 0x1: PM_WOWLAN_REQ_START
		 * 0x2: PM_WOWLAN_REQ_STOP
		 * 0x3: PM_WOWLAN_PARAM_CLEAR
		 */
	u8 trigger; /* 0: NONE
		     * BIT(0): NL80211_WOWLAN_TRIG_MAGIC_PKT
		     * BIT(1): NL80211_WOWLAN_TRIG_ANY
		     * BIT(2): NL80211_WOWLAN_TRIG_DISCONNECT
		     * BIT(3): NL80211_WOWLAN_TRIG_GTK_REKEY_FAILURE
		     * BIT(4): BEACON_LOST
		     * BIT(5): NL80211_WOWLAN_TRIG_NET_DETECT
		     */
	u8 wakeup_hif; /* 0x0: HIF_SDIO
			* 0x1: HIF_USB
			* 0x2: HIF_PCIE
			* 0x3: HIF_GPIO
			*/
	u8 pad;
	u8 rsv[4];
} __packed;

struct mt7615_wow_gpio_param_tlv {
	__le16 tag;
	__le16 len;
	u8 gpio_pin;
	u8 trigger_lvl;
	u8 pad[2];
	__le32 gpio_interval;
	u8 rsv[4];
} __packed;

#define MT7615_WOW_MASK_MAX_LEN		16
#define MT7615_WOW_PATTEN_MAX_LEN	128
struct mt7615_wow_pattern_tlv {
	__le16 tag;
	__le16 len;
	u8 index; /* pattern index */
	u8 enable; /* 0: disable
		    * 1: enable
		    */
	u8 data_len; /* pattern length */
	u8 pad;
	u8 mask[MT7615_WOW_MASK_MAX_LEN];
	u8 pattern[MT7615_WOW_PATTEN_MAX_LEN];
	u8 rsv[4];
} __packed;

struct mt7615_suspend_tlv {
	__le16 tag;
	__le16 len;
	u8 enable; /* 0: suspend mode disabled
		    * 1: suspend mode enabled
		    */
	u8 mdtim; /* LP parameter */
	u8 wow_suspend; /* 0: update by origin policy
			 * 1: update by wow dtim
			 */
	u8 pad[5];
} __packed;

struct mt7615_gtk_rekey_tlv {
	__le16 tag;
	__le16 len;
	u8 kek[NL80211_KEK_LEN];
	u8 kck[NL80211_KCK_LEN];
	u8 replay_ctr[NL80211_REPLAY_CTR_LEN];
	u8 rekey_mode; /* 0: rekey offload enable
			* 1: rekey offload disable
			* 2: rekey update
			*/
	u8 keyid;
	u8 pad[2];
	__le32 proto; /* WPA-RSN-WAPI-OPSN */
	__le32 pairwise_cipher;
	__le32 group_cipher;
	__le32 key_mgmt; /* NONE-PSK-IEEE802.1X */
	__le32 mgmt_group_cipher;
	u8 option; /* 1: rekey data update without enabling offload */
	u8 reserverd[3];
} __packed;

struct mt7615_roc_tlv {
	u8 bss_idx;
	u8 token;
	u8 active;
	u8 primary_chan;
	u8 sco;
	u8 band;
	u8 width;	/* To support 80/160MHz bandwidth */
	u8 freq_seg1;	/* To support 80/160MHz bandwidth */
	u8 freq_seg2;	/* To support 80/160MHz bandwidth */
	u8 req_type;
	u8 dbdc_band;
	u8 rsv0;
	__le32 max_interval;	/* ms */
	u8 rsv1[8];
} __packed;

struct mt7615_arpns_tlv {
	__le16 tag;
	__le16 len;
	u8 mode;
	u8 ips_num;
	u8 option;
	u8 pad[1];
} __packed;

/* offload mcu commands */
enum {
	MCU_CMD_START_HW_SCAN = MCU_CE_PREFIX | 0x03,
	MCU_CMD_SET_PS_PROFILE = MCU_CE_PREFIX | 0x05,
	MCU_CMD_SET_CHAN_DOMAIN = MCU_CE_PREFIX | 0x0f,
	MCU_CMD_SET_BSS_CONNECTED = MCU_CE_PREFIX | 0x16,
	MCU_CMD_SET_BSS_ABORT = MCU_CE_PREFIX | 0x17,
	MCU_CMD_CANCEL_HW_SCAN = MCU_CE_PREFIX | 0x1b,
	MCU_CMD_SET_ROC = MCU_CE_PREFIX | 0x1c,
	MCU_CMD_SET_P2P_OPPPS = MCU_CE_PREFIX | 0x33,
	MCU_CMD_SCHED_SCAN_ENABLE = MCU_CE_PREFIX | 0x61,
	MCU_CMD_SCHED_SCAN_REQ = MCU_CE_PREFIX | 0x62,
	MCU_CMD_REG_WRITE = MCU_CE_PREFIX | 0xc0,
	MCU_CMD_REG_READ = MCU_CE_PREFIX | MCU_QUERY_MASK | 0xc0,
};

#define MCU_CMD_ACK		BIT(0)
#define MCU_CMD_UNI		BIT(1)
#define MCU_CMD_QUERY		BIT(2)

#define MCU_CMD_UNI_EXT_ACK	(MCU_CMD_ACK | MCU_CMD_UNI | MCU_CMD_QUERY)

enum {
	UNI_BSS_INFO_BASIC = 0,
	UNI_BSS_INFO_RLM = 2,
	UNI_BSS_INFO_BCN_CONTENT = 7,
	UNI_BSS_INFO_QBSS = 15,
	UNI_BSS_INFO_UAPSD = 19,
};

enum {
	UNI_SUSPEND_MODE_SETTING,
	UNI_SUSPEND_WOW_CTRL,
	UNI_SUSPEND_WOW_GPIO_PARAM,
	UNI_SUSPEND_WOW_WAKEUP_PORT,
	UNI_SUSPEND_WOW_PATTERN,
};

enum {
	UNI_OFFLOAD_OFFLOAD_ARP,
	UNI_OFFLOAD_OFFLOAD_ND,
	UNI_OFFLOAD_OFFLOAD_GTK_REKEY,
	UNI_OFFLOAD_OFFLOAD_BMC_RPY_DETECT,
};

enum {
	PATCH_SEM_RELEASE = 0x0,
	PATCH_SEM_GET	  = 0x1
};

enum {
	PATCH_NOT_DL_SEM_FAIL	 = 0x0,
	PATCH_IS_DL		 = 0x1,
	PATCH_NOT_DL_SEM_SUCCESS = 0x2,
	PATCH_REL_SEM_SUCCESS	 = 0x3
};

enum {
	FW_STATE_INITIAL          = 0,
	FW_STATE_FW_DOWNLOAD      = 1,
	FW_STATE_NORMAL_OPERATION = 2,
	FW_STATE_NORMAL_TRX       = 3,
	FW_STATE_CR4_RDY          = 7
};

enum {
	FW_STATE_PWR_ON = 1,
	FW_STATE_N9_RDY = 2,
};

#define STA_TYPE_STA		BIT(0)
#define STA_TYPE_AP		BIT(1)
#define STA_TYPE_ADHOC		BIT(2)
#define STA_TYPE_WDS		BIT(4)
#define STA_TYPE_BC		BIT(5)

#define NETWORK_INFRA		BIT(16)
#define NETWORK_P2P		BIT(17)
#define NETWORK_IBSS		BIT(18)
#define NETWORK_WDS		BIT(21)

#define CONNECTION_INFRA_STA	(STA_TYPE_STA | NETWORK_INFRA)
#define CONNECTION_INFRA_AP	(STA_TYPE_AP | NETWORK_INFRA)
#define CONNECTION_P2P_GC	(STA_TYPE_STA | NETWORK_P2P)
#define CONNECTION_P2P_GO	(STA_TYPE_AP | NETWORK_P2P)
#define CONNECTION_IBSS_ADHOC	(STA_TYPE_ADHOC | NETWORK_IBSS)
#define CONNECTION_WDS		(STA_TYPE_WDS | NETWORK_WDS)
#define CONNECTION_INFRA_BC	(STA_TYPE_BC | NETWORK_INFRA)

#define CONN_STATE_DISCONNECT	0
#define CONN_STATE_CONNECT	1
#define CONN_STATE_PORT_SECURE	2

enum {
	DEV_INFO_ACTIVE,
	DEV_INFO_MAX_NUM
};

enum {
	DBDC_TYPE_WMM,
	DBDC_TYPE_MGMT,
	DBDC_TYPE_BSS,
	DBDC_TYPE_MBSS,
	DBDC_TYPE_REPEATER,
	DBDC_TYPE_MU,
	DBDC_TYPE_BF,
	DBDC_TYPE_PTA,
	__DBDC_TYPE_MAX,
};

struct tlv {
	__le16 tag;
	__le16 len;
} __packed;

struct bss_info_omac {
	__le16 tag;
	__le16 len;
	u8 hw_bss_idx;
	u8 omac_idx;
	u8 band_idx;
	u8 rsv0;
	__le32 conn_type;
	u32 rsv1;
} __packed;

struct bss_info_basic {
	__le16 tag;
	__le16 len;
	__le32 network_type;
	u8 active;
	u8 rsv0;
	__le16 bcn_interval;
	u8 bssid[ETH_ALEN];
	u8 wmm_idx;
	u8 dtim_period;
	u8 bmc_tx_wlan_idx;
	u8 cipher; /* not used */
	u8 phymode; /* not used */
	u8 rsv1[5];
} __packed;

struct bss_info_rf_ch {
	__le16 tag;
	__le16 len;
	u8 pri_ch;
	u8 central_ch0;
	u8 central_ch1;
	u8 bw;
} __packed;

struct bss_info_ext_bss {
	__le16 tag;
	__le16 len;
	__le32 mbss_tsf_offset; /* in unit of us */
	u8 rsv[8];
} __packed;

enum {
	BSS_INFO_OMAC,
	BSS_INFO_BASIC,
	BSS_INFO_RF_CH, /* optional, for BT/LTE coex */
	BSS_INFO_PM, /* sta only */
	BSS_INFO_UAPSD, /* sta only */
	BSS_INFO_ROAM_DETECTION, /* obsoleted */
	BSS_INFO_LQ_RM, /* obsoleted */
	BSS_INFO_EXT_BSS,
	BSS_INFO_BMC_INFO, /* for bmc rate control in CR4 */
	BSS_INFO_SYNC_MODE, /* obsoleted */
	BSS_INFO_RA,
	BSS_INFO_MAX_NUM
};

enum {
	WTBL_RESET_AND_SET = 1,
	WTBL_SET,
	WTBL_QUERY,
	WTBL_RESET_ALL
};

struct wtbl_req_hdr {
	u8 wlan_idx;
	u8 operation;
	__le16 tlv_num;
	u8 rsv[4];
} __packed;

struct wtbl_generic {
	__le16 tag;
	__le16 len;
	u8 peer_addr[ETH_ALEN];
	u8 muar_idx;
	u8 skip_tx;
	u8 cf_ack;
	u8 qos;
	u8 mesh;
	u8 adm;
	__le16 partial_aid;
	u8 baf_en;
	u8 aad_om;
} __packed;

struct wtbl_rx {
	__le16 tag;
	__le16 len;
	u8 rcid;
	u8 rca1;
	u8 rca2;
	u8 rv;
	u8 rsv[4];
} __packed;

struct wtbl_ht {
	__le16 tag;
	__le16 len;
	u8 ht;
	u8 ldpc;
	u8 af;
	u8 mm;
	u8 rsv[4];
} __packed;

struct wtbl_vht {
	__le16 tag;
	__le16 len;
	u8 ldpc;
	u8 dyn_bw;
	u8 vht;
	u8 txop_ps;
	u8 rsv[4];
} __packed;

struct wtbl_tx_ps {
	__le16 tag;
	__le16 len;
	u8 txps;
	u8 rsv[3];
} __packed;

struct wtbl_hdr_trans {
	__le16 tag;
	__le16 len;
	u8 to_ds;
	u8 from_ds;
	u8 disable_rx_trans;
	u8 rsv;
} __packed;

enum {
	MT_BA_TYPE_INVALID,
	MT_BA_TYPE_ORIGINATOR,
	MT_BA_TYPE_RECIPIENT
};

enum {
	RST_BA_MAC_TID_MATCH,
	RST_BA_MAC_MATCH,
	RST_BA_NO_MATCH
};

struct wtbl_ba {
	__le16 tag;
	__le16 len;
	/* common */
	u8 tid;
	u8 ba_type;
	u8 rsv0[2];
	/* originator only */
	__le16 sn;
	u8 ba_en;
	u8 ba_winsize_idx;
	__le16 ba_winsize;
	/* recipient only */
	u8 peer_addr[ETH_ALEN];
	u8 rst_ba_tid;
	u8 rst_ba_sel;
	u8 rst_ba_sb;
	u8 band_idx;
	u8 rsv1[4];
} __packed;

struct wtbl_bf {
	__le16 tag;
	__le16 len;
	u8 ibf;
	u8 ebf;
	u8 ibf_vht;
	u8 ebf_vht;
	u8 gid;
	u8 pfmu_idx;
	u8 rsv[2];
} __packed;

struct wtbl_smps {
	__le16 tag;
	__le16 len;
	u8 smps;
	u8 rsv[3];
} __packed;

struct wtbl_pn {
	__le16 tag;
	__le16 len;
	u8 pn[6];
	u8 rsv[2];
} __packed;

struct wtbl_spe {
	__le16 tag;
	__le16 len;
	u8 spe_idx;
	u8 rsv[3];
} __packed;

struct wtbl_raw {
	__le16 tag;
	__le16 len;
	u8 wtbl_idx;
	u8 dw;
	u8 rsv[2];
	__le32 msk;
	__le32 val;
} __packed;

#define MT7615_WTBL_UPDATE_MAX_SIZE	(sizeof(struct wtbl_req_hdr) +	\
					 sizeof(struct wtbl_generic) +	\
					 sizeof(struct wtbl_rx) +	\
					 sizeof(struct wtbl_ht) +	\
					 sizeof(struct wtbl_vht) +	\
					 sizeof(struct wtbl_tx_ps) +	\
					 sizeof(struct wtbl_hdr_trans) +\
					 sizeof(struct wtbl_ba) +	\
					 sizeof(struct wtbl_bf) +	\
					 sizeof(struct wtbl_smps) +	\
					 sizeof(struct wtbl_pn) +	\
					 sizeof(struct wtbl_spe))

#define MT7615_STA_UPDATE_MAX_SIZE	(sizeof(struct sta_req_hdr) +	\
					 sizeof(struct sta_rec_basic) +	\
					 sizeof(struct sta_rec_ht) +	\
					 sizeof(struct sta_rec_vht) +	\
					 sizeof(struct sta_rec_uapsd) + \
					 sizeof(struct tlv) +	\
					 MT7615_WTBL_UPDATE_MAX_SIZE)

#define MT7615_WTBL_UPDATE_BA_SIZE	(sizeof(struct wtbl_req_hdr) +	\
					 sizeof(struct wtbl_ba))

enum {
	WTBL_GENERIC,
	WTBL_RX,
	WTBL_HT,
	WTBL_VHT,
	WTBL_PEER_PS, /* not used */
	WTBL_TX_PS,
	WTBL_HDR_TRANS,
	WTBL_SEC_KEY,
	WTBL_BA,
	WTBL_RDG, /* obsoleted */
	WTBL_PROTECT, /* not used */
	WTBL_CLEAR, /* not used */
	WTBL_BF,
	WTBL_SMPS,
	WTBL_RAW_DATA, /* debug only */
	WTBL_PN,
	WTBL_SPE,
	WTBL_MAX_NUM
};

struct sta_ntlv_hdr {
	u8 rsv[2];
	__le16 tlv_num;
} __packed;

struct sta_req_hdr {
	u8 bss_idx;
	u8 wlan_idx;
	__le16 tlv_num;
	u8 is_tlv_append;
	u8 muar_idx;
	u8 rsv[2];
} __packed;

struct sta_rec_state {
	__le16 tag;
	__le16 len;
	u8 state;
	__le32 flags;
	u8 vhtop;
	u8 pad[2];
} __packed;

struct sta_rec_basic {
	__le16 tag;
	__le16 len;
	__le32 conn_type;
	u8 conn_state;
	u8 qos;
	__le16 aid;
	u8 peer_addr[ETH_ALEN];
#define EXTRA_INFO_VER	BIT(0)
#define EXTRA_INFO_NEW	BIT(1)
	__le16 extra_info;
} __packed;

struct sta_rec_ht {
	__le16 tag;
	__le16 len;
	__le16 ht_cap;
	u16 rsv;
} __packed;

struct sta_rec_vht {
	__le16 tag;
	__le16 len;
	__le32 vht_cap;
	__le16 vht_rx_mcs_map;
	__le16 vht_tx_mcs_map;
} __packed;

struct sta_rec_ba {
	__le16 tag;
	__le16 len;
	u8 tid;
	u8 ba_type;
	u8 amsdu;
	u8 ba_en;
	__le16 ssn;
	__le16 winsize;
} __packed;

struct sta_rec_uapsd {
	__le16 tag;
	__le16 len;
	u8 dac_map;
	u8 tac_map;
	u8 max_sp;
	u8 rsv0;
	__le16 listen_interval;
	u8 rsv1[2];
} __packed;

enum {
	STA_REC_BASIC,
	STA_REC_RA,
	STA_REC_RA_CMM_INFO,
	STA_REC_RA_UPDATE,
	STA_REC_BF,
	STA_REC_AMSDU, /* for CR4 */
	STA_REC_BA,
	STA_REC_STATE,
	STA_REC_TX_PROC, /* for hdr trans and CSO in CR4 */
	STA_REC_HT,
	STA_REC_VHT,
	STA_REC_APPS,
	STA_REC_WTBL = 13,
	STA_REC_MAX_NUM
};

enum {
	CMD_CBW_20MHZ,
	CMD_CBW_40MHZ,
	CMD_CBW_80MHZ,
	CMD_CBW_160MHZ,
	CMD_CBW_10MHZ,
	CMD_CBW_5MHZ,
	CMD_CBW_8080MHZ
};

enum {
	CH_SWITCH_NORMAL = 0,
	CH_SWITCH_SCAN = 3,
	CH_SWITCH_MCC = 4,
	CH_SWITCH_DFS = 5,
	CH_SWITCH_BACKGROUND_SCAN_START = 6,
	CH_SWITCH_BACKGROUND_SCAN_RUNNING = 7,
	CH_SWITCH_BACKGROUND_SCAN_STOP = 8,
	CH_SWITCH_SCAN_BYPASS_DPD = 9
};

#endif
