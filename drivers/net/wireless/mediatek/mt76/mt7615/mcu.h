/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2019 MediaTek Inc. */

#ifndef __MT7615_MCU_H
#define __MT7615_MCU_H

#include "../mt76_connac_mcu.h"

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
	MCU_EVENT_COREDUMP = 0xf0,
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

enum {
	MCU_ATE_SET_FREQ_OFFSET = 0xa,
	MCU_ATE_SET_TX_POWER_CONTROL = 0x15,
};

struct mt7615_mcu_uni_event {
	u8 cid;
	u8 pad[3];
	__le32 status; /* 0: success, others: fail */
} __packed;

struct mt7615_mcu_reg_event {
	__le32 reg;
	__le32 val;
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
