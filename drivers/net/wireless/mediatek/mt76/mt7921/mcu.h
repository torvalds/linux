/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2020 MediaTek Inc. */

#ifndef __MT7921_MCU_H
#define __MT7921_MCU_H

struct mt7921_mcu_txd {
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
 * struct mt7921_uni_txd - mcu command descriptor for firmware v3
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
struct mt7921_uni_txd {
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
	MCU_EVENT_REG_ACCESS = 0x05,
	MCU_EVENT_SCAN_DONE = 0x0d,
	MCU_EVENT_BSS_ABSENCE  = 0x11,
	MCU_EVENT_BSS_BEACON_LOSS = 0x13,
	MCU_EVENT_CH_PRIVILEGE = 0x18,
	MCU_EVENT_SCHED_SCAN_DONE = 0x23,
	MCU_EVENT_DBG_MSG = 0x27,
};

/* ext event table */
enum {
	MCU_EXT_EVENT_RATE_REPORT = 0x87,
};

struct mt7921_mcu_rxd {
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

struct mt7921_mcu_eeprom_info {
	__le32 addr;
	__le32 valid;
	u8 data[16];
} __packed;

#define MT_RA_RATE_NSS			GENMASK(8, 6)
#define MT_RA_RATE_MCS			GENMASK(3, 0)
#define MT_RA_RATE_TX_MODE		GENMASK(12, 9)
#define MT_RA_RATE_DCM_EN		BIT(4)
#define MT_RA_RATE_BW			GENMASK(14, 13)

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
	MCU_CMD_NIC_POWER_CTRL = MCU_FW_PREFIX | 0x4,
	MCU_CMD_PATCH_START_REQ = MCU_FW_PREFIX | 0x05,
	MCU_CMD_PATCH_FINISH_REQ = MCU_FW_PREFIX | 0x07,
	MCU_CMD_PATCH_SEM_CONTROL = MCU_FW_PREFIX | 0x10,
	MCU_CMD_EXT_CID = 0xED,
	MCU_CMD_FW_SCATTER = MCU_FW_PREFIX | 0xEE,
};

enum {
	MCU_EXT_CMD_EFUSE_ACCESS = 0x01,
	MCU_EXT_CMD_CHANNEL_SWITCH = 0x08,
	MCU_EXT_CMD_EFUSE_BUFFER_MODE = 0x21,
	MCU_EXT_CMD_EDCA_UPDATE = 0x27,
	MCU_EXT_CMD_THERMAL_CTRL = 0x2c,
	MCU_EXT_CMD_WTBL_UPDATE = 0x32,
	MCU_EXT_CMD_PROTECT_CTRL = 0x3e,
	MCU_EXT_CMD_MAC_INIT_CTRL = 0x46,
	MCU_EXT_CMD_RX_HDR_TRANS = 0x47,
	MCU_EXT_CMD_SET_RX_PATH = 0x4e,
};

enum {
	MCU_UNI_CMD_DEV_INFO_UPDATE = MCU_UNI_PREFIX | 0x01,
	MCU_UNI_CMD_BSS_INFO_UPDATE = MCU_UNI_PREFIX | 0x02,
	MCU_UNI_CMD_STA_REC_UPDATE = MCU_UNI_PREFIX | 0x03,
	MCU_UNI_CMD_SUSPEND = MCU_UNI_PREFIX | 0x05,
	MCU_UNI_CMD_OFFLOAD = MCU_UNI_PREFIX | 0x06,
	MCU_UNI_CMD_HIF_CTRL = MCU_UNI_PREFIX | 0x07,
};

struct mt7921_mcu_uni_event {
	u8 cid;
	u8 pad[3];
	__le32 status; /* 0: success, others: fail */
} __packed;

/* offload mcu commands */
enum {
	MCU_CMD_START_HW_SCAN = MCU_CE_PREFIX | 0x03,
	MCU_CMD_SET_CHAN_DOMAIN = MCU_CE_PREFIX | 0x0f,
	MCU_CMD_SET_BSS_CONNECTED = MCU_CE_PREFIX | 0x16,
	MCU_CMD_SET_BSS_ABORT = MCU_CE_PREFIX | 0x17,
	MCU_CMD_CANCEL_HW_SCAN = MCU_CE_PREFIX | 0x1b,
	MCU_CMD_SCHED_SCAN_ENABLE = MCU_CE_PREFIX | 0x61,
	MCU_CMD_SCHED_SCAN_REQ = MCU_CE_PREFIX | 0x62,
	MCU_CMD_REG_WRITE = MCU_CE_PREFIX | 0xc0,
	MCU_CMD_REG_READ = MCU_CE_PREFIX | MCU_QUERY_MASK | 0xc0,
	MCU_CMD_FWLOG_2_HOST = MCU_CE_PREFIX | 0xc5,
	MCU_CMD_GET_WTBL = MCU_CE_PREFIX | 0xcd,
};

#define MCU_CMD_ACK		BIT(0)
#define MCU_CMD_UNI		BIT(1)
#define MCU_CMD_QUERY		BIT(2)

#define MCU_CMD_UNI_EXT_ACK	(MCU_CMD_ACK | MCU_CMD_UNI | MCU_CMD_QUERY)

enum {
	UNI_BSS_INFO_BASIC = 0,
	UNI_BSS_INFO_RLM = 2,
	UNI_BSS_INFO_HE_BASIC = 5,
	UNI_BSS_INFO_BCN_CONTENT = 7,
	UNI_BSS_INFO_QBSS = 15,
	UNI_BSS_INFO_UAPSD = 19,
	UNI_BSS_INFO_PS = 21,
	UNI_BSS_INFO_BCNFT = 22,
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

struct bss_info_uni_he {
	__le16 tag;
	__le16 len;
	__le16 he_rts_thres;
	u8 he_pe_duration;
	u8 su_disable;
	__le16 max_nss_mcs[CMD_HE_MCS_BW_NUM];
	u8 rsv[2];
} __packed;

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

struct sta_rec_state {
	__le16 tag;
	__le16 len;
	__le32 flags;
	u8 state;
	u8 vht_opmode;
	u8 action;
	u8 rsv[1];
} __packed;

#define HT_MCS_MASK_NUM 10

struct sta_rec_ra_info {
	__le16 tag;
	__le16 len;
	__le16 legacy;
	u8 rx_mcs_bitmask[HT_MCS_MASK_NUM];
} __packed;

struct sta_rec_phy {
	__le16 tag;
	__le16 len;
	__le16 basic_rate;
	u8 phy_type;
	u8 ampdu;
	u8 rts_policy;
	u8 rcpi;
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
	STA_REC_STATE,
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
	STA_REC_PHY = 0x15,
	STA_REC_MAX_NUM
};

enum mt7921_cipher_type {
	MT_CIPHER_NONE,
	MT_CIPHER_WEP40,
	MT_CIPHER_WEP104,
	MT_CIPHER_WEP128,
	MT_CIPHER_TKIP,
	MT_CIPHER_AES_CCMP,
	MT_CIPHER_CCMP_256,
	MT_CIPHER_GCMP,
	MT_CIPHER_GCMP_256,
	MT_CIPHER_WAPI,
	MT_CIPHER_BIP_CMAC_128,
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
	MT_EBF = BIT(0),	/* explicit beamforming */
	MT_IBF = BIT(1)		/* implicit beamforming */
};

#define MT7921_WTBL_UPDATE_MAX_SIZE	(sizeof(struct wtbl_req_hdr) +	\
					 sizeof(struct wtbl_generic) +	\
					 sizeof(struct wtbl_rx) +	\
					 sizeof(struct wtbl_ht) +	\
					 sizeof(struct wtbl_vht) +	\
					 sizeof(struct wtbl_hdr_trans) +\
					 sizeof(struct wtbl_ba) +	\
					 sizeof(struct wtbl_smps))

#define MT7921_STA_UPDATE_MAX_SIZE	(sizeof(struct sta_req_hdr) +	\
					 sizeof(struct sta_rec_basic) +	\
					 sizeof(struct sta_rec_ht) +	\
					 sizeof(struct sta_rec_he) +	\
					 sizeof(struct sta_rec_ba) +	\
					 sizeof(struct sta_rec_vht) +	\
					 sizeof(struct sta_rec_uapsd) + \
					 sizeof(struct sta_rec_amsdu) +	\
					 sizeof(struct tlv) +		\
					 MT7921_WTBL_UPDATE_MAX_SIZE)

#define MT7921_WTBL_UPDATE_BA_SIZE	(sizeof(struct wtbl_req_hdr) +	\
					 sizeof(struct wtbl_ba))

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

struct mt7921_mcu_reg_event {
	__le32 reg;
	__le32 val;
} __packed;

struct mt7921_bss_basic_tlv {
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

struct mt7921_bss_qos_tlv {
	__le16 tag;
	__le16 len;
	u8 qos;
	u8 pad[3];
} __packed;

struct mt7921_beacon_loss_event {
	u8 bss_idx;
	u8 reason;
	u8 pad[2];
} __packed;

struct mt7921_mcu_scan_ssid {
	__le32 ssid_len;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
} __packed;

struct mt7921_mcu_scan_channel {
	u8 band; /* 1: 2.4GHz
		  * 2: 5.0GHz
		  * Others: Reserved
		  */
	u8 channel_num;
} __packed;

struct mt7921_mcu_scan_match {
	__le32 rssi_th;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 ssid_len;
	u8 rsv[3];
} __packed;

struct mt7921_hw_scan_req {
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
	struct mt7921_mcu_scan_ssid ssids[4];
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
	struct mt7921_mcu_scan_channel channels[32];
	__le16 ies_len;
	u8 ies[MT7921_SCAN_IE_LEN];
	/* following fields are valid if version > 0 */
	u8 ext_channels_num;
	u8 ext_ssids_num;
	__le16 channel_min_dwell_time;
	struct mt7921_mcu_scan_channel ext_channels[32];
	struct mt7921_mcu_scan_ssid ext_ssids[6];
	u8 bssid[ETH_ALEN];
	u8 random_mac[ETH_ALEN]; /* valid when BIT(1) in scan_func is set. */
	u8 pad[63];
	u8 ssid_type_ext;
} __packed;

#define SCAN_DONE_EVENT_MAX_CHANNEL_NUM	64
struct mt7921_hw_scan_done {
	u8 seq_num;
	u8 sparse_channel_num;
	struct mt7921_mcu_scan_channel sparse_channel;
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

struct mt7921_sched_scan_req {
	u8 version;
	u8 seq_num;
	u8 stop_on_match;
	u8 ssids_num;
	u8 match_num;
	u8 pad;
	__le16 ie_len;
	struct mt7921_mcu_scan_ssid ssids[MT7921_MAX_SCHED_SCAN_SSID];
	struct mt7921_mcu_scan_match match[MT7921_MAX_SCAN_MATCH];
	u8 channel_type;
	u8 channels_num;
	u8 intervals_num;
	u8 scan_func;
	struct mt7921_mcu_scan_channel channels[64];
	__le16 intervals[MT7921_MAX_SCHED_SCAN_INTERVAL];
	u8 bss_idx;
	u8 pad2[64];
} __packed;

struct mt7921_mcu_bss_event {
	u8 bss_idx;
	u8 is_absent;
	u8 free_quota;
	u8 pad;
} __packed;

enum {
	PHY_TYPE_HR_DSSS_INDEX = 0,
	PHY_TYPE_ERP_INDEX,
	PHY_TYPE_ERP_P2P_INDEX,
	PHY_TYPE_OFDM_INDEX,
	PHY_TYPE_HT_INDEX,
	PHY_TYPE_VHT_INDEX,
	PHY_TYPE_HE_INDEX,
	PHY_TYPE_INDEX_NUM
};

#define PHY_TYPE_BIT_HR_DSSS    BIT(PHY_TYPE_HR_DSSS_INDEX)
#define PHY_TYPE_BIT_ERP        BIT(PHY_TYPE_ERP_INDEX)
#define PHY_TYPE_BIT_OFDM       BIT(PHY_TYPE_OFDM_INDEX)
#define PHY_TYPE_BIT_HT         BIT(PHY_TYPE_HT_INDEX)
#define PHY_TYPE_BIT_VHT        BIT(PHY_TYPE_VHT_INDEX)
#define PHY_TYPE_BIT_HE         BIT(PHY_TYPE_HE_INDEX)

#define MT_WTBL_RATE_TX_MODE	GENMASK(9, 6)
#define MT_WTBL_RATE_MCS	GENMASK(5, 0)
#define MT_WTBL_RATE_NSS	GENMASK(12, 10)
#define MT_WTBL_RATE_HE_GI	GENMASK(7, 4)
#define MT_WTBL_RATE_GI		GENMASK(3, 0)

struct mt7921_mcu_tx_config {
	u8 peer_addr[ETH_ALEN];
	u8 sw;
	u8 dis_rx_hdr_tran;

	u8 aad_om;
	u8 pfmu_idx;
	__le16 partial_aid;

	u8 ibf;
	u8 ebf;
	u8 is_ht;
	u8 is_vht;

	u8 mesh;
	u8 baf_en;
	u8 cf_ack;
	u8 rdg_ba;

	u8 rdg;
	u8 pm;
	u8 rts;
	u8 smps;

	u8 txop_ps;
	u8 not_update_ipsm;
	u8 skip_tx;
	u8 ldpc;

	u8 qos;
	u8 from_ds;
	u8 to_ds;
	u8 dyn_bw;

	u8 amdsu_cross_lg;
	u8 check_per;
	u8 gid_63;
	u8 he;

	u8 vht_ibf;
	u8 vht_ebf;
	u8 vht_ldpc;
	u8 he_ldpc;
} __packed;

struct mt7921_mcu_sec_config {
	u8 wpi_flag;
	u8 rv;
	u8 ikv;
	u8 rkv;

	u8 rcid;
	u8 rca1;
	u8 rca2;
	u8 even_pn;

	u8 key_id;
	u8 muar_idx;
	u8 cipher_suit;
	u8 rsv[1];
} __packed;

struct mt7921_mcu_key_config {
	u8 key[32];
} __packed;

struct mt7921_mcu_rate_info {
	u8 mpdu_fail;
	u8 mpdu_tx;
	u8 rate_idx;
	u8 rsv[1];
	__le16 rate[8];
} __packed;

struct mt7921_mcu_ba_config {
	u8 ba_en;
	u8 rsv[3];
	__le32 ba_winsize;
} __packed;

struct mt7921_mcu_ant_id_config {
	u8 ant_id[4];
} __packed;

struct mt7921_mcu_peer_cap {
	struct mt7921_mcu_ant_id_config ant_id_config;

	u8 power_offset;
	u8 bw_selector;
	u8 change_bw_rate_n;
	u8 bw;
	u8 spe_idx;

	u8 g2;
	u8 g4;
	u8 g8;
	u8 g16;

	u8 mmss;
	u8 ampdu_factor;
	u8 rsv[1];
} __packed;

struct mt7921_mcu_rx_cnt {
	u8 rx_rcpi[4];
	u8 rx_cc[4];
	u8 rx_cc_sel;
	u8 ce_rmsd;
	u8 rsv[2];
} __packed;

struct mt7921_mcu_tx_cnt {
	__le16 rate1_cnt;
	__le16 rate1_fail_cnt;
	__le16 rate2_cnt;
	__le16 rate3_cnt;
	__le16 cur_bw_tx_cnt;
	__le16 cur_bw_tx_fail_cnt;
	__le16 other_bw_tx_cnt;
	__le16 other_bw_tx_fail_cnt;
} __packed;

struct mt7921_mcu_wlan_info_event {
	struct mt7921_mcu_tx_config tx_config;
	struct mt7921_mcu_sec_config sec_config;
	struct mt7921_mcu_key_config key_config;
	struct mt7921_mcu_rate_info rate_info;
	struct mt7921_mcu_ba_config ba_config;
	struct mt7921_mcu_peer_cap peer_cap;
	struct mt7921_mcu_rx_cnt rx_cnt;
	struct mt7921_mcu_tx_cnt tx_cnt;
} __packed;

struct mt7921_mcu_wlan_info {
	__le32 wlan_idx;
	struct mt7921_mcu_wlan_info_event event;
} __packed;
#endif
