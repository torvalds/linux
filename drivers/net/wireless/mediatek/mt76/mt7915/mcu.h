/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2020 MediaTek Inc. */

#ifndef __MT7915_MCU_H
#define __MT7915_MCU_H

struct mt7915_mcu_txd {
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

/* event table */
enum {
	MCU_EVENT_TARGET_ADDRESS_LEN = 0x01,
	MCU_EVENT_FW_START = 0x01,
	MCU_EVENT_GENERIC = 0x01,
	MCU_EVENT_ACCESS_REG = 0x02,
	MCU_EVENT_MT_PATCH_SEM = 0x04,
	MCU_EVENT_CH_PRIVILEGE = 0x18,
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
	MCU_EXT_EVENT_BCC_NOTIFY = 0x75,
};

enum {
	MCU_ATE_SET_TRX = 0x1,
	MCU_ATE_SET_FREQ_OFFSET = 0xa,
	MCU_ATE_SET_SLOT_TIME = 0x13,
	MCU_ATE_CLEAN_TXQUEUE = 0x1c,
};

struct mt7915_mcu_rxd {
	__le32 rxd[6];

	__le16 len;
	__le16 pkt_type_id;

	u8 eid;
	u8 seq;
	__le16 __rsv;

	u8 ext_eid;
	u8 __rsv1[2];
	u8 s2d_index;
};

struct mt7915_mcu_thermal_ctrl {
	u8 ctrl_id;
	u8 band_idx;
	union {
		struct {
			u8 protect_type; /* 1: duty admit, 2: radio off */
			u8 trigger_type; /* 0: low, 1: high */
		} __packed type;
		struct {
			u8 duty_level;	/* level 0~3 */
			u8 duty_cycle;
		} __packed duty;
	};
} __packed;

struct mt7915_mcu_thermal_notify {
	struct mt7915_mcu_rxd rxd;

	struct mt7915_mcu_thermal_ctrl ctrl;
	__le32 temperature;
	u8 rsv[8];
} __packed;

struct mt7915_mcu_csa_notify {
	struct mt7915_mcu_rxd rxd;

	u8 omac_idx;
	u8 csa_count;
	u8 band_idx;
	u8 rsv;
} __packed;

struct mt7915_mcu_rdd_report {
	struct mt7915_mcu_rxd rxd;

	u8 band_idx;
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

	u8 rsv;

	__le32 out_pri_const;
	__le32 out_pri_stg[3];

	struct {
		__le32 start;
		__le16 pulse_width;
		__le16 pulse_power;
		u8 mdrdy_flag;
		u8 rsv[3];
	} long_pulse[32];

	struct {
		__le32 start;
		__le16 pulse_width;
		__le16 pulse_power;
		u8 mdrdy_flag;
		u8 rsv[3];
	} periodic_pulse[32];

	struct {
		__le32 start;
		__le16 pulse_width;
		__le16 pulse_power;
		u8 sc_pass;
		u8 sw_reset;
		u8 mdrdy_flag;
		u8 tx_active;
	} hw_pulse[32];
} __packed;

struct mt7915_mcu_eeprom {
	u8 buffer_mode;
	u8 format;
	__le16 len;
} __packed;

struct mt7915_mcu_eeprom_info {
	__le32 addr;
	__le32 valid;
	u8 data[16];
} __packed;

struct mt7915_mcu_phy_rx_info {
	u8 category;
	u8 rate;
	u8 mode;
	u8 nsts;
	u8 gi;
	u8 coding;
	u8 stbc;
	u8 bw;
};

struct mt7915_mcu_mib {
	__le32 band;
	__le32 offs;
	__le64 data;
} __packed;

enum mt7915_chan_mib_offs {
	MIB_BUSY_TIME = 14,
	MIB_TX_TIME = 81,
	MIB_RX_TIME,
	MIB_OBSS_AIRTIME = 86
};

struct edca {
	u8 queue;
	u8 set;
	u8 aifs;
	u8 cw_min;
	__le16 cw_max;
	__le16 txop;
};

struct mt7915_mcu_tx {
	u8 total;
	u8 action;
	u8 valid;
	u8 mode;

	struct edca edca[IEEE80211_NUM_ACS];
} __packed;

#define WMM_AIFS_SET		BIT(0)
#define WMM_CW_MIN_SET		BIT(1)
#define WMM_CW_MAX_SET		BIT(2)
#define WMM_TXOP_SET		BIT(3)
#define WMM_PARAM_SET		GENMASK(3, 0)

#define MCU_PQ_ID(p, q)			(((p) << 15) | ((q) << 10))
#define MCU_PKT_ID			0xa0

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

enum {
	MCU_FW_LOG_WM,
	MCU_FW_LOG_WA,
	MCU_FW_LOG_TO_HOST,
};

#define __MCU_CMD_FIELD_ID	GENMASK(7, 0)
#define __MCU_CMD_FIELD_EXT_ID	GENMASK(15, 8)
#define __MCU_CMD_FIELD_QUERY	BIT(16)
#define __MCU_CMD_FIELD_WA	BIT(17)

enum {
	MCU_CMD_TARGET_ADDRESS_LEN_REQ = 0x01,
	MCU_CMD_FW_START_REQ = 0x02,
	MCU_CMD_INIT_ACCESS_REG = 0x3,
	MCU_CMD_NIC_POWER_CTRL = 0x4,
	MCU_CMD_PATCH_START_REQ = 0x05,
	MCU_CMD_PATCH_FINISH_REQ = 0x07,
	MCU_CMD_PATCH_SEM_CONTROL = 0x10,
	MCU_CMD_WA_PARAM = 0xC4,
	MCU_CMD_EXT_CID = 0xED,
	MCU_CMD_FW_SCATTER = 0xEE,
	MCU_CMD_RESTART_DL_REQ = 0xEF,
};

enum {
	MCU_EXT_CMD_EFUSE_ACCESS = 0x01,
	MCU_EXT_CMD_RF_TEST = 0x04,
	MCU_EXT_CMD_PM_STATE_CTRL = 0x07,
	MCU_EXT_CMD_CHANNEL_SWITCH = 0x08,
	MCU_EXT_CMD_FW_LOG_2_HOST = 0x13,
	MCU_EXT_CMD_TXBF_ACTION = 0x1e,
	MCU_EXT_CMD_EFUSE_BUFFER_MODE = 0x21,
	MCU_EXT_CMD_THERMAL_PROT = 0x23,
	MCU_EXT_CMD_STA_REC_UPDATE = 0x25,
	MCU_EXT_CMD_BSS_INFO_UPDATE = 0x26,
	MCU_EXT_CMD_EDCA_UPDATE = 0x27,
	MCU_EXT_CMD_DEV_INFO_UPDATE = 0x2A,
	MCU_EXT_CMD_THERMAL_CTRL = 0x2c,
	MCU_EXT_CMD_WTBL_UPDATE = 0x32,
	MCU_EXT_CMD_SET_DRR_CTRL = 0x36,
	MCU_EXT_CMD_SET_RDD_CTRL = 0x3a,
	MCU_EXT_CMD_ATE_CTRL = 0x3d,
	MCU_EXT_CMD_PROTECT_CTRL = 0x3e,
	MCU_EXT_CMD_MAC_INIT_CTRL = 0x46,
	MCU_EXT_CMD_RX_HDR_TRANS = 0x47,
	MCU_EXT_CMD_MUAR_UPDATE = 0x48,
	MCU_EXT_CMD_RX_AIRTIME_CTRL = 0x4a,
	MCU_EXT_CMD_SET_RX_PATH = 0x4e,
	MCU_EXT_CMD_TX_POWER_FEATURE_CTRL = 0x58,
	MCU_EXT_CMD_GET_MIB_INFO = 0x5a,
	MCU_EXT_CMD_MWDS_SUPPORT = 0x80,
	MCU_EXT_CMD_SET_SER_TRIGGER = 0x81,
	MCU_EXT_CMD_SCS_CTRL = 0x82,
	MCU_EXT_CMD_TWT_AGRT_UPDATE = 0x94,
	MCU_EXT_CMD_FW_DBG_CTRL = 0x95,
	MCU_EXT_CMD_SET_RDD_TH = 0x9d,
	MCU_EXT_CMD_MURU_CTRL = 0x9f,
	MCU_EXT_CMD_SET_SPR = 0xa8,
	MCU_EXT_CMD_GROUP_PRE_CAL_INFO = 0xab,
	MCU_EXT_CMD_DPD_PRE_CAL_INFO = 0xac,
	MCU_EXT_CMD_PHY_STAT_INFO = 0xad,
};

enum {
	MCU_TWT_AGRT_ADD,
	MCU_TWT_AGRT_MODIFY,
	MCU_TWT_AGRT_DELETE,
	MCU_TWT_AGRT_TEARDOWN,
	MCU_TWT_AGRT_GET_TSF,
};

enum {
	MCU_WA_PARAM_CMD_QUERY,
	MCU_WA_PARAM_CMD_SET,
	MCU_WA_PARAM_CMD_CAPABILITY,
	MCU_WA_PARAM_CMD_DEBUG,
};

enum {
	MCU_WA_PARAM_PDMA_RX = 0x04,
	MCU_WA_PARAM_CPU_UTIL = 0x0b,
	MCU_WA_PARAM_RED = 0x0e,
};

#define MCU_CMD(_t)		FIELD_PREP(__MCU_CMD_FIELD_ID, MCU_CMD_##_t)
#define MCU_EXT_CMD(_t)		(MCU_CMD(EXT_CID) | \
				 FIELD_PREP(__MCU_CMD_FIELD_EXT_ID, \
					    MCU_EXT_CMD_##_t))
#define MCU_EXT_QUERY(_t)	(MCU_EXT_CMD(_t) | __MCU_CMD_FIELD_QUERY)

#define MCU_WA_CMD(_t)		(MCU_CMD(_t) | __MCU_CMD_FIELD_WA)
#define MCU_WA_EXT_CMD(_t)	(MCU_EXT_CMD(_t) | __MCU_CMD_FIELD_WA)
#define MCU_WA_PARAM_CMD(_t)	(MCU_WA_CMD(WA_PARAM) | \
				 FIELD_PREP(__MCU_CMD_FIELD_EXT_ID, \
					    MCU_WA_PARAM_CMD_##_t))

enum {
	PATCH_SEM_RELEASE,
	PATCH_SEM_GET
};

enum {
	PATCH_NOT_DL_SEM_FAIL,
	PATCH_IS_DL,
	PATCH_NOT_DL_SEM_SUCCESS,
	PATCH_REL_SEM_SUCCESS
};

enum {
	FW_STATE_INITIAL,
	FW_STATE_FW_DOWNLOAD,
	FW_STATE_NORMAL_OPERATION,
	FW_STATE_NORMAL_TRX,
	FW_STATE_WACPU_RDY        = 7
};

enum {
	EE_MODE_EFUSE,
	EE_MODE_BUFFER,
};

enum {
	EE_FORMAT_BIN,
	EE_FORMAT_WHOLE,
	EE_FORMAT_MULTIPLE,
};

enum {
	MCU_PHY_STATE_TX_RATE,
	MCU_PHY_STATE_RX_RATE,
	MCU_PHY_STATE_RSSI,
	MCU_PHY_STATE_CONTENTION_RX_RATE,
	MCU_PHY_STATE_OFDMLQ_CNINFO,
};

#define STA_TYPE_STA			BIT(0)
#define STA_TYPE_AP			BIT(1)
#define STA_TYPE_ADHOC			BIT(2)
#define STA_TYPE_WDS			BIT(4)
#define STA_TYPE_BC			BIT(5)

#define NETWORK_INFRA			BIT(16)
#define NETWORK_P2P			BIT(17)
#define NETWORK_IBSS			BIT(18)
#define NETWORK_WDS			BIT(21)

#define CONNECTION_INFRA_STA		(STA_TYPE_STA | NETWORK_INFRA)
#define CONNECTION_INFRA_AP		(STA_TYPE_AP | NETWORK_INFRA)
#define CONNECTION_P2P_GC		(STA_TYPE_STA | NETWORK_P2P)
#define CONNECTION_P2P_GO		(STA_TYPE_AP | NETWORK_P2P)
#define CONNECTION_IBSS_ADHOC		(STA_TYPE_ADHOC | NETWORK_IBSS)
#define CONNECTION_WDS			(STA_TYPE_WDS | NETWORK_WDS)
#define CONNECTION_INFRA_BC		(STA_TYPE_BC | NETWORK_INFRA)

#define CONN_STATE_DISCONNECT		0
#define CONN_STATE_CONNECT		1
#define CONN_STATE_PORT_SECURE		2

enum {
	DEV_INFO_ACTIVE,
	DEV_INFO_MAX_NUM
};

enum {
	SCS_SEND_DATA,
	SCS_SET_MANUAL_PD_TH,
	SCS_CONFIG,
	SCS_ENABLE,
	SCS_SHOW_INFO,
	SCS_GET_GLO_ADDR,
	SCS_GET_GLO_ADDR_EVENT,
};

enum {
	CMD_CBW_20MHZ = IEEE80211_STA_RX_BW_20,
	CMD_CBW_40MHZ = IEEE80211_STA_RX_BW_40,
	CMD_CBW_80MHZ = IEEE80211_STA_RX_BW_80,
	CMD_CBW_160MHZ = IEEE80211_STA_RX_BW_160,
	CMD_CBW_10MHZ,
	CMD_CBW_5MHZ,
	CMD_CBW_8080MHZ,

	CMD_HE_MCS_BW80 = 0,
	CMD_HE_MCS_BW160,
	CMD_HE_MCS_BW8080,
	CMD_HE_MCS_BW_NUM
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
	u8 bmc_wcid_lo;
	u8 cipher;
	u8 phy_mode;
	u8 max_bssid;	/* max BSSID. range: 1 ~ 8, 0: MBSSID disabled */
	u8 non_tx_bssid;/* non-transmitted BSSID, 0: transmitted BSSID */
	u8 bmc_wcid_hi;	/* high Byte and version */
	u8 rsv[2];
} __packed;

struct bss_info_rf_ch {
	__le16 tag;
	__le16 len;
	u8 pri_ch;
	u8 center_ch0;
	u8 center_ch1;
	u8 bw;
	u8 he_ru26_block;	/* 1: don't send HETB in RU26, 0: allow */
	u8 he_all_disable;	/* 1: disallow all HETB, 0: allow */
	u8 rsv[2];
} __packed;

struct bss_info_ext_bss {
	__le16 tag;
	__le16 len;
	__le32 mbss_tsf_offset; /* in unit of us */
	u8 rsv[8];
} __packed;

struct bss_info_bmc_rate {
	__le16 tag;
	__le16 len;
	__le16 bc_trans;
	__le16 mc_trans;
	u8 short_preamble;
	u8 rsv[7];
} __packed;

struct bss_info_ra {
	__le16 tag;
	__le16 len;
	u8 op_mode;
	u8 adhoc_en;
	u8 short_preamble;
	u8 tx_streams;
	u8 rx_streams;
	u8 algo;
	u8 force_sgi;
	u8 force_gf;
	u8 ht_mode;
	u8 has_20_sta;		/* Check if any sta support GF. */
	u8 bss_width_trigger_events;
	u8 vht_nss_cap;
	u8 vht_bw_signal;	/* not use */
	u8 vht_force_sgi;	/* not use */
	u8 se_off;
	u8 antenna_idx;
	u8 train_up_rule;
	u8 rsv[3];
	unsigned short train_up_high_thres;
	short train_up_rule_rssi;
	unsigned short low_traffic_thres;
	__le16 max_phyrate;
	__le32 phy_cap;
	__le32 interval;
	__le32 fast_interval;
} __packed;

struct bss_info_hw_amsdu {
	__le16 tag;
	__le16 len;
	__le32 cmp_bitmap_0;
	__le32 cmp_bitmap_1;
	__le16 trig_thres;
	u8 enable;
	u8 rsv;
} __packed;

struct bss_info_color {
	__le16 tag;
	__le16 len;
	u8 disable;
	u8 color;
	u8 rsv[2];
} __packed;

struct bss_info_he {
	__le16 tag;
	__le16 len;
	u8 he_pe_duration;
	u8 vht_op_info_present;
	__le16 he_rts_thres;
	__le16 max_nss_mcs[CMD_HE_MCS_BW_NUM];
	u8 rsv[6];
} __packed;

struct bss_info_bcn {
	__le16 tag;
	__le16 len;
	u8 ver;
	u8 enable;
	__le16 sub_ntlv;
} __packed __aligned(4);

struct bss_info_bcn_cntdwn {
	__le16 tag;
	__le16 len;
	u8 cnt;
	u8 rsv[3];
} __packed __aligned(4);

struct bss_info_bcn_mbss {
#define MAX_BEACON_NUM	32
	__le16 tag;
	__le16 len;
	__le32 bitmap;
	__le16 offset[MAX_BEACON_NUM];
	u8 rsv[8];
} __packed __aligned(4);

struct bss_info_bcn_cont {
	__le16 tag;
	__le16 len;
	__le16 tim_ofs;
	__le16 csa_ofs;
	__le16 bcc_ofs;
	__le16 pkt_len;
} __packed __aligned(4);

enum {
	BSS_INFO_BCN_CSA,
	BSS_INFO_BCN_BCC,
	BSS_INFO_BCN_MBSSID,
	BSS_INFO_BCN_CONTENT,
	BSS_INFO_BCN_MAX
};

enum {
	BSS_INFO_OMAC,
	BSS_INFO_BASIC,
	BSS_INFO_RF_CH,		/* optional, for BT/LTE coex */
	BSS_INFO_PM,		/* sta only */
	BSS_INFO_UAPSD,		/* sta only */
	BSS_INFO_ROAM_DETECT,	/* obsoleted */
	BSS_INFO_LQ_RM,		/* obsoleted */
	BSS_INFO_EXT_BSS,
	BSS_INFO_BMC_RATE,	/* for bmc rate control in CR4 */
	BSS_INFO_SYNC_MODE,	/* obsoleted */
	BSS_INFO_RA,
	BSS_INFO_HW_AMSDU,
	BSS_INFO_BSS_COLOR,
	BSS_INFO_HE_BASIC,
	BSS_INFO_PROTECT_INFO,
	BSS_INFO_OFFLOAD,
	BSS_INFO_11V_MBSSID,
	BSS_INFO_MAX_NUM
};

enum {
	WTBL_RESET_AND_SET = 1,
	WTBL_SET,
	WTBL_QUERY,
	WTBL_RESET_ALL
};

struct wtbl_req_hdr {
	u8 wlan_idx_lo;
	u8 operation;
	__le16 tlv_num;
	u8 wlan_idx_hi;
	u8 rsv[3];
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

struct wtbl_hdr_trans {
	__le16 tag;
	__le16 len;
	u8 to_ds;
	u8 from_ds;
	u8 no_rx_trans;
	u8 _rsv;
};

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
	/* originator & recipient */
	__le16 ba_winsize;
	/* recipient only */
	u8 peer_addr[ETH_ALEN];
	u8 rst_ba_tid;
	u8 rst_ba_sel;
	u8 rst_ba_sb;
	u8 band_idx;
	u8 rsv1[4];
} __packed;

struct wtbl_smps {
	__le16 tag;
	__le16 len;
	u8 smps;
	u8 rsv[3];
} __packed;

enum {
	WTBL_GENERIC,
	WTBL_RX,
	WTBL_HT,
	WTBL_VHT,
	WTBL_PEER_PS,		/* not used */
	WTBL_TX_PS,
	WTBL_HDR_TRANS,
	WTBL_SEC_KEY,
	WTBL_BA,
	WTBL_RDG,		/* obsoleted */
	WTBL_PROTECT,		/* not used */
	WTBL_CLEAR,		/* not used */
	WTBL_BF,
	WTBL_SMPS,
	WTBL_RAW_DATA,		/* debug only */
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
	u8 wlan_idx_lo;
	__le16 tlv_num;
	u8 is_tlv_append;
	u8 muar_idx;
	u8 wlan_idx_hi;
	u8 rsv;
} __packed;

struct sta_rec_basic {
	__le16 tag;
	__le16 len;
	__le32 conn_type;
	u8 conn_state;
	u8 qos;
	__le16 aid;
	u8 peer_addr[ETH_ALEN];
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
	u8 rts_bw_sig;
	u8 rsv[3];
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

struct sta_rec_muru {
	__le16 tag;
	__le16 len;

	struct {
		bool ofdma_dl_en;
		bool ofdma_ul_en;
		bool mimo_dl_en;
		bool mimo_ul_en;
		u8 rsv[4];
	} cfg;

	struct {
		u8 punc_pream_rx;
		bool he_20m_in_40m_2g;
		bool he_20m_in_160m;
		bool he_80m_in_160m;
		bool lt16_sigb;
		bool rx_su_comp_sigb;
		bool rx_su_non_comp_sigb;
		u8 rsv;
	} ofdma_dl;

	struct {
		u8 t_frame_dur;
		u8 mu_cascading;
		u8 uo_ra;
		u8 he_2x996_tone;
		u8 rx_t_frame_11ac;
		u8 rsv[3];
	} ofdma_ul;

	struct {
		bool vht_mu_bfee;
		bool partial_bw_dl_mimo;
		u8 rsv[2];
	} mimo_dl;

	struct {
		bool full_ul_mimo;
		bool partial_ul_mimo;
		u8 rsv[2];
	} mimo_ul;
} __packed;

struct sta_rec_he {
	__le16 tag;
	__le16 len;

	__le32 he_cap;

	u8 t_frame_dur;
	u8 max_ampdu_exp;
	u8 bw_set;
	u8 device_class;
	u8 dcm_tx_mode;
	u8 dcm_tx_max_nss;
	u8 dcm_rx_mode;
	u8 dcm_rx_max_nss;
	u8 dcm_max_ru;
	u8 punc_pream_rx;
	u8 pkt_ext;
	u8 rsv1;

	__le16 max_nss_mcs[CMD_HE_MCS_BW_NUM];

	u8 rsv2[2];
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

struct sta_rec_amsdu {
	__le16 tag;
	__le16 len;
	u8 max_amsdu_num;
	u8 max_mpdu_size;
	u8 amsdu_en;
	u8 rsv;
} __packed;

struct sec_key {
	u8 cipher_id;
	u8 cipher_len;
	u8 key_id;
	u8 key_len;
	u8 key[32];
} __packed;

struct sta_rec_sec {
	__le16 tag;
	__le16 len;
	u8 add;
	u8 n_cipher;
	u8 rsv[2];

	struct sec_key key[2];
} __packed;

struct sta_phy {
	u8 type;
	u8 flag;
	u8 stbc;
	u8 sgi;
	u8 bw;
	u8 ldpc;
	u8 mcs;
	u8 nss;
	u8 he_ltf;
};

struct sta_rec_ra {
	__le16 tag;
	__le16 len;

	u8 valid;
	u8 auto_rate;
	u8 phy_mode;
	u8 channel;
	u8 bw;
	u8 disable_cck;
	u8 ht_mcs32;
	u8 ht_gf;
	u8 ht_mcs[4];
	u8 mmps_mode;
	u8 gband_256;
	u8 af;
	u8 auth_wapi_mode;
	u8 rate_len;

	u8 supp_mode;
	u8 supp_cck_rate;
	u8 supp_ofdm_rate;
	__le32 supp_ht_mcs;
	__le16 supp_vht_mcs[4];

	u8 op_mode;
	u8 op_vht_chan_width;
	u8 op_vht_rx_nss;
	u8 op_vht_rx_nss_type;

	__le32 sta_cap;

	struct sta_phy phy;
} __packed;

struct sta_rec_ra_fixed {
	__le16 tag;
	__le16 len;

	__le32 field;
	u8 op_mode;
	u8 op_vht_chan_width;
	u8 op_vht_rx_nss;
	u8 op_vht_rx_nss_type;

	struct sta_phy phy;

	u8 spe_en;
	u8 short_preamble;
	u8 is_5g;
	u8 mmps_mode;
} __packed;

enum {
	RATE_PARAM_FIXED = 3,
	RATE_PARAM_FIXED_HE_LTF = 7,
	RATE_PARAM_FIXED_MCS,
	RATE_PARAM_FIXED_GI = 11,
	RATE_PARAM_AUTO = 20,
};

#define RATE_CFG_MCS			GENMASK(3, 0)
#define RATE_CFG_NSS			GENMASK(7, 4)
#define RATE_CFG_GI			GENMASK(11, 8)
#define RATE_CFG_BW			GENMASK(15, 12)
#define RATE_CFG_STBC			GENMASK(19, 16)
#define RATE_CFG_LDPC			GENMASK(23, 20)
#define RATE_CFG_PHY_TYPE		GENMASK(27, 24)
#define RATE_CFG_HE_LTF			GENMASK(31, 28)

struct sta_rec_bf {
	__le16 tag;
	__le16 len;

	__le16 pfmu;		/* 0xffff: no access right for PFMU */
	bool su_mu;		/* 0: SU, 1: MU */
	u8 bf_cap;		/* 0: iBF, 1: eBF */
	u8 sounding_phy;	/* 0: legacy, 1: OFDM, 2: HT, 4: VHT */
	u8 ndpa_rate;
	u8 ndp_rate;
	u8 rept_poll_rate;
	u8 tx_mode;		/* 0: legacy, 1: OFDM, 2: HT, 4: VHT ... */
	u8 ncol;
	u8 nrow;
	u8 bw;			/* 0: 20M, 1: 40M, 2: 80M, 3: 160M */

	u8 mem_total;
	u8 mem_20m;
	struct {
		u8 row;
		u8 col: 6, row_msb: 2;
	} mem[4];

	__le16 smart_ant;
	u8 se_idx;
	u8 auto_sounding;	/* b7: low traffic indicator
				 * b6: Stop sounding for this entry
				 * b5 ~ b0: postpone sounding
				 */
	u8 ibf_timeout;
	u8 ibf_dbw;
	u8 ibf_ncol;
	u8 ibf_nrow;
	u8 nrow_bw160;
	u8 ncol_bw160;
	u8 ru_start_idx;
	u8 ru_end_idx;

	bool trigger_su;
	bool trigger_mu;
	bool ng16_su;
	bool ng16_mu;
	bool codebook42_su;
	bool codebook75_mu;

	u8 he_ltf;
	u8 rsv[3];
} __packed;

struct sta_rec_bfee {
	__le16 tag;
	__le16 len;
	bool fb_identity_matrix;	/* 1: feedback identity matrix */
	bool ignore_feedback;		/* 1: ignore */
	u8 rsv[2];
} __packed;

enum {
	STA_REC_BASIC,
	STA_REC_RA,
	STA_REC_RA_CMM_INFO,
	STA_REC_RA_UPDATE,
	STA_REC_BF,
	STA_REC_AMSDU,
	STA_REC_BA,
	STA_REC_RED,		/* not used */
	STA_REC_TX_PROC,	/* for hdr trans and CSO in CR4 */
	STA_REC_HT,
	STA_REC_VHT,
	STA_REC_APPS,
	STA_REC_KEY,
	STA_REC_WTBL,
	STA_REC_HE,
	STA_REC_HW_AMSDU,
	STA_REC_WTBL_AADOM,
	STA_REC_KEY_V2,
	STA_REC_MURU,
	STA_REC_MUEDCA,
	STA_REC_BFEE,
	STA_REC_MAX_NUM
};

enum mcu_cipher_type {
	MCU_CIPHER_NONE = 0,
	MCU_CIPHER_WEP40,
	MCU_CIPHER_WEP104,
	MCU_CIPHER_WEP128,
	MCU_CIPHER_TKIP,
	MCU_CIPHER_AES_CCMP,
	MCU_CIPHER_CCMP_256,
	MCU_CIPHER_GCMP,
	MCU_CIPHER_GCMP_256,
	MCU_CIPHER_WAPI,
	MCU_CIPHER_BIP_CMAC_128,
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

enum {
	THERMAL_SENSOR_TEMP_QUERY,
	THERMAL_SENSOR_MANUAL_CTRL,
	THERMAL_SENSOR_INFO_QUERY,
	THERMAL_SENSOR_TASK_CTRL,
};

enum {
	THERMAL_PROTECT_PARAMETER_CTRL,
	THERMAL_PROTECT_BASIC_INFO,
	THERMAL_PROTECT_ENABLE,
	THERMAL_PROTECT_DISABLE,
	THERMAL_PROTECT_DUTY_CONFIG,
	THERMAL_PROTECT_MECH_INFO,
	THERMAL_PROTECT_DUTY_INFO,
	THERMAL_PROTECT_STATE_ACT,
};

enum {
	MT_BF_SOUNDING_ON = 1,
	MT_BF_TYPE_UPDATE = 20,
	MT_BF_MODULE_UPDATE = 25
};

enum {
	MURU_SET_ARB_OP_MODE = 14,
	MURU_SET_PLATFORM_TYPE = 25,
};

enum {
	MURU_PLATFORM_TYPE_PERF_LEVEL_1 = 1,
	MURU_PLATFORM_TYPE_PERF_LEVEL_2,
};

#define MT7915_WTBL_UPDATE_MAX_SIZE	(sizeof(struct wtbl_req_hdr) +	\
					 sizeof(struct wtbl_generic) +	\
					 sizeof(struct wtbl_rx) +	\
					 sizeof(struct wtbl_ht) +	\
					 sizeof(struct wtbl_vht) +	\
					 sizeof(struct wtbl_hdr_trans) +\
					 sizeof(struct wtbl_ba) +	\
					 sizeof(struct wtbl_smps))

#define MT7915_STA_UPDATE_MAX_SIZE	(sizeof(struct sta_req_hdr) +	\
					 sizeof(struct sta_rec_basic) +	\
					 sizeof(struct sta_rec_bf) +	\
					 sizeof(struct sta_rec_ht) +	\
					 sizeof(struct sta_rec_he) +	\
					 sizeof(struct sta_rec_ba) +	\
					 sizeof(struct sta_rec_vht) +	\
					 sizeof(struct sta_rec_uapsd) + \
					 sizeof(struct sta_rec_amsdu) +	\
					 sizeof(struct sta_rec_muru) +	\
					 sizeof(struct sta_rec_bfee) +	\
					 sizeof(struct tlv) +		\
					 MT7915_WTBL_UPDATE_MAX_SIZE)

#define MT7915_BSS_UPDATE_MAX_SIZE	(sizeof(struct sta_req_hdr) +	\
					 sizeof(struct bss_info_omac) +	\
					 sizeof(struct bss_info_basic) +\
					 sizeof(struct bss_info_rf_ch) +\
					 sizeof(struct bss_info_ra) +	\
					 sizeof(struct bss_info_hw_amsdu) +\
					 sizeof(struct bss_info_he) +	\
					 sizeof(struct bss_info_bmc_rate) +\
					 sizeof(struct bss_info_ext_bss))

#define MT7915_BEACON_UPDATE_SIZE	(sizeof(struct sta_req_hdr) +	\
					 sizeof(struct bss_info_bcn_cntdwn) + \
					 sizeof(struct bss_info_bcn_mbss) + \
					 sizeof(struct bss_info_bcn_cont))

#define PHY_MODE_A			BIT(0)
#define PHY_MODE_B			BIT(1)
#define PHY_MODE_G			BIT(2)
#define PHY_MODE_GN			BIT(3)
#define PHY_MODE_AN			BIT(4)
#define PHY_MODE_AC			BIT(5)
#define PHY_MODE_AX_24G			BIT(6)
#define PHY_MODE_AX_5G			BIT(7)
#define PHY_MODE_AX_6G			BIT(8)

#define MODE_CCK			BIT(0)
#define MODE_OFDM			BIT(1)
#define MODE_HT				BIT(2)
#define MODE_VHT			BIT(3)
#define MODE_HE				BIT(4)

#define STA_CAP_WMM			BIT(0)
#define STA_CAP_SGI_20			BIT(4)
#define STA_CAP_SGI_40			BIT(5)
#define STA_CAP_TX_STBC			BIT(6)
#define STA_CAP_RX_STBC			BIT(7)
#define STA_CAP_VHT_SGI_80		BIT(16)
#define STA_CAP_VHT_SGI_160		BIT(17)
#define STA_CAP_VHT_TX_STBC		BIT(18)
#define STA_CAP_VHT_RX_STBC		BIT(19)
#define STA_CAP_VHT_LDPC		BIT(23)
#define STA_CAP_LDPC			BIT(24)
#define STA_CAP_HT			BIT(26)
#define STA_CAP_VHT			BIT(27)
#define STA_CAP_HE			BIT(28)

/* HE MAC */
#define STA_REC_HE_CAP_HTC			BIT(0)
#define STA_REC_HE_CAP_BQR			BIT(1)
#define STA_REC_HE_CAP_BSR			BIT(2)
#define STA_REC_HE_CAP_OM			BIT(3)
#define STA_REC_HE_CAP_AMSDU_IN_AMPDU		BIT(4)
/* HE PHY */
#define STA_REC_HE_CAP_DUAL_BAND		BIT(5)
#define STA_REC_HE_CAP_LDPC			BIT(6)
#define STA_REC_HE_CAP_TRIG_CQI_FK		BIT(7)
#define STA_REC_HE_CAP_PARTIAL_BW_EXT_RANGE	BIT(8)
/* STBC */
#define STA_REC_HE_CAP_LE_EQ_80M_TX_STBC	BIT(9)
#define STA_REC_HE_CAP_LE_EQ_80M_RX_STBC	BIT(10)
#define STA_REC_HE_CAP_GT_80M_TX_STBC		BIT(11)
#define STA_REC_HE_CAP_GT_80M_RX_STBC		BIT(12)
/* GI */
#define STA_REC_HE_CAP_SU_PPDU_1LTF_8US_GI	BIT(13)
#define STA_REC_HE_CAP_SU_MU_PPDU_4LTF_8US_GI	BIT(14)
#define STA_REC_HE_CAP_ER_SU_PPDU_1LTF_8US_GI	BIT(15)
#define STA_REC_HE_CAP_ER_SU_PPDU_4LTF_8US_GI	BIT(16)
#define STA_REC_HE_CAP_NDP_4LTF_3DOT2MS_GI	BIT(17)
/* 242 TONE */
#define STA_REC_HE_CAP_BW20_RU242_SUPPORT	BIT(18)
#define STA_REC_HE_CAP_TX_1024QAM_UNDER_RU242	BIT(19)
#define STA_REC_HE_CAP_RX_1024QAM_UNDER_RU242	BIT(20)

#endif
