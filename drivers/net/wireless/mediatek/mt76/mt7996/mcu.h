/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#ifndef __MT7996_MCU_H
#define __MT7996_MCU_H

#include "../mt76_connac_mcu.h"

struct mt7996_mcu_rxd {
	__le32 rxd[8];

	__le16 len;
	__le16 pkt_type_id;

	u8 eid;
	u8 seq;
	u8 option;
	u8 __rsv;

	u8 ext_eid;
	u8 __rsv1[2];
	u8 s2d_index;
};

struct mt7996_mcu_uni_event {
	u8 cid;
	u8 __rsv[3];
	__le32 status; /* 0: success, others: fail */
} __packed;

struct mt7996_mcu_csa_notify {
	struct mt7996_mcu_rxd rxd;

	u8 omac_idx;
	u8 csa_count;
	u8 band_idx;
	u8 rsv;
} __packed;

struct mt7996_mcu_rdd_report {
	struct mt7996_mcu_rxd rxd;

	u8 __rsv1[4];

	__le16 tag;
	__le16 len;

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

	u8 __rsv2;

	__le32 out_pri_const;
	__le32 out_pri_stg[3];
	__le32 out_pri_stg_dmin;

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

struct mt7996_mcu_background_chain_ctrl {
	u8 _rsv[4];

	__le16 tag;
	__le16 len;

	u8 chan;		/* primary channel */
	u8 central_chan;	/* central channel */
	u8 bw;
	u8 tx_stream;
	u8 rx_stream;

	u8 monitor_chan;	/* monitor channel */
	u8 monitor_central_chan;/* monitor central channel */
	u8 monitor_bw;
	u8 monitor_tx_stream;
	u8 monitor_rx_stream;

	u8 scan_mode;		/* 0: ScanStop
				 * 1: ScanStart
				 * 2: ScanRunning
				 */
	u8 band_idx;		/* DBDC */
	u8 monitor_scan_type;
	u8 band;		/* 0: 2.4GHz, 1: 5GHz */
	u8 rsv[2];
} __packed;

struct mt7996_mcu_eeprom {
	u8 _rsv[4];

	__le16 tag;
	__le16 len;
	u8 buffer_mode;
	u8 format;
	__le16 buf_len;
} __packed;

struct mt7996_mcu_phy_rx_info {
	u8 category;
	u8 rate;
	u8 mode;
	u8 nsts;
	u8 gi;
	u8 coding;
	u8 stbc;
	u8 bw;
};

struct mt7996_mcu_mib {
	__le16 tag;
	__le16 len;
	__le32 offs;
	__le64 data;
} __packed;

enum mt7996_chan_mib_offs {
	UNI_MIB_OBSS_AIRTIME = 26,
	UNI_MIB_NON_WIFI_TIME = 27,
	UNI_MIB_TX_TIME = 28,
	UNI_MIB_RX_TIME = 29
};

struct edca {
	__le16 tag;
	__le16 len;

	u8 queue;
	u8 set;
	u8 cw_min;
	u8 cw_max;
	__le16 txop;
	u8 aifs;
	u8 __rsv;
};

#define MCU_PQ_ID(p, q)			(((p) << 15) | ((q) << 10))
#define MCU_PKT_ID			0xa0

enum {
	MCU_FW_LOG_WM,
	MCU_FW_LOG_WA,
	MCU_FW_LOG_TO_HOST,
	MCU_FW_LOG_RELAY = 16
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
	MCU_WA_PARAM_HW_PATH_HIF_VER = 0x2f,
};

enum mcu_mmps_mode {
	MCU_MMPS_STATIC,
	MCU_MMPS_DYNAMIC,
	MCU_MMPS_RSV,
	MCU_MMPS_DISABLE,
};

struct bss_rate_tlv {
	__le16 tag;
	__le16 len;
	u8 __rsv1[4];
	__le16 bc_trans;
	__le16 mc_trans;
	u8 short_preamble;
	u8 bc_fixed_rate;
	u8 mc_fixed_rate;
	u8 __rsv2[1];
} __packed;

struct bss_ra_tlv {
	__le16 tag;
	__le16 len;
	u8 short_preamble;
	u8 force_sgi;
	u8 force_gf;
	u8 ht_mode;
	u8 se_off;
	u8 antenna_idx;
	__le16 max_phyrate;
	u8 force_tx_streams;
	u8 __rsv[3];
} __packed;

struct bss_rlm_tlv {
	__le16 tag;
	__le16 len;
	u8 control_channel;
	u8 center_chan;
	u8 center_chan2;
	u8 bw;
	u8 tx_streams;
	u8 rx_streams;
	u8 ht_op_info;
	u8 sco;
	u8 band;
	u8 __rsv[3];
} __packed;

struct bss_color_tlv {
	__le16 tag;
	__le16 len;
	u8 enable;
	u8 color;
	u8 rsv[2];
} __packed;

struct bss_inband_discovery_tlv {
	__le16 tag;
	__le16 len;
	u8 tx_type;
	u8 tx_mode;
	u8 tx_interval;
	u8 enable;
	__le16 wcid;
	__le16 prob_rsp_len;
#define MAX_INBAND_FRAME_SIZE 512
	u8 pkt[MAX_INBAND_FRAME_SIZE];
} __packed;

struct bss_bcn_content_tlv {
	__le16 tag;
	__le16 len;
	__le16 tim_ie_pos;
	__le16 csa_ie_pos;
	__le16 bcc_ie_pos;
	u8 enable;
	u8 type;
	__le16 pkt_len;
#define MAX_BEACON_SIZE 512
	u8 pkt[MAX_BEACON_SIZE];
} __packed;

struct bss_bcn_cntdwn_tlv {
	__le16 tag;
	__le16 len;
	u8 cnt;
	u8 rsv[3];
} __packed;

struct bss_bcn_mbss_tlv {
	__le16 tag;
	__le16 len;
	__le32 bitmap;
#define MAX_BEACON_NUM	32
	__le16 offset[MAX_BEACON_NUM];
} __packed __aligned(4);

struct bss_txcmd_tlv {
	__le16 tag;
	__le16 len;
	u8 txcmd_mode;
	u8 __rsv[3];
} __packed;

struct bss_sec_tlv {
	__le16 tag;
	__le16 len;
	u8 __rsv1[2];
	u8 cipher;
	u8 __rsv2[1];
} __packed;

struct bss_power_save {
	__le16 tag;
	__le16 len;
	u8 profile;
	u8 _rsv[3];
} __packed;

struct bss_mld_tlv {
	__le16 tag;
	__le16 len;
	u8 group_mld_id;
	u8 own_mld_id;
	u8 mac_addr[ETH_ALEN];
	u8 remap_idx;
	u8 __rsv[3];
} __packed;

struct sta_rec_ba_uni {
	__le16 tag;
	__le16 len;
	u8 tid;
	u8 ba_type;
	u8 amsdu;
	u8 ba_en;
	__le16 ssn;
	__le16 winsize;
	u8 ba_rdd_rro;
	u8 __rsv[3];
} __packed;

struct sec_key_uni {
	__le16 wlan_idx;
	u8 mgmt_prot;
	u8 cipher_id;
	u8 cipher_len;
	u8 key_id;
	u8 key_len;
	u8 need_resp;
	u8 key[32];
} __packed;

struct sta_rec_sec_uni {
	__le16 tag;
	__le16 len;
	u8 add;
	u8 n_cipher;
	u8 rsv[2];

	struct sec_key_uni key[2];
} __packed;

struct sta_rec_hdrt {
	__le16 tag;
	__le16 len;
	u8 hdrt_mode;
	u8 rsv[3];
} __packed;

struct sta_rec_hdr_trans {
	__le16 tag;
	__le16 len;
	u8 from_ds;
	u8 to_ds;
	u8 dis_rx_hdr_tran;
	u8 rsv;
} __packed;

struct hdr_trans_en {
	__le16 tag;
	__le16 len;
	u8 enable;
	u8 check_bssid;
	u8 mode;
	u8 __rsv;
} __packed;

struct hdr_trans_vlan {
	__le16 tag;
	__le16 len;
	u8 insert_vlan;
	u8 remove_vlan;
	u8 tid;
	u8 __rsv;
} __packed;

struct hdr_trans_blacklist {
	__le16 tag;
	__le16 len;
	u8 idx;
	u8 enable;
	__le16 type;
} __packed;

struct uni_header {
	u8 __rsv[4];
} __packed;

struct vow_rx_airtime {
	__le16 tag;
	__le16 len;

	u8 enable;
	u8 band;
	u8 __rsv[2];
} __packed;

struct bf_sounding_on {
	__le16 tag;
	__le16 len;

	u8 snd_mode;
	u8 sta_num;
	u8 __rsv[2];
	__le16 wlan_id[4];
	__le32 snd_period;
} __packed;

struct bf_hw_en_status_update {
	__le16 tag;
	__le16 len;

	bool ebf;
	bool ibf;
	u8 __rsv[2];
} __packed;

struct bf_mod_en_ctrl {
	__le16 tag;
	__le16 len;

	u8 bf_num;
	u8 bf_bitmap;
	u8 bf_sel[8];
	u8 __rsv[2];
} __packed;

union bf_tag_tlv {
	struct bf_sounding_on bf_snd;
	struct bf_hw_en_status_update bf_hw_en;
	struct bf_mod_en_ctrl bf_mod_en;
};

struct ra_rate {
	__le16 wlan_idx;
	u8 mode;
	u8 stbc;
	__le16 gi;
	u8 bw;
	u8 ldpc;
	u8 mcs;
	u8 nss;
	__le16 ltf;
	u8 spe;
	u8 preamble;
	u8 __rsv[2];
} __packed;

struct ra_fixed_rate {
	__le16 tag;
	__le16 len;

	__le16 version;
	struct ra_rate rate;
} __packed;

enum {
	UNI_RA_FIXED_RATE = 0xf,
};

#define MT7996_HDR_TRANS_MAX_SIZE	(sizeof(struct hdr_trans_en) +	 \
					 sizeof(struct hdr_trans_vlan) + \
					 sizeof(struct hdr_trans_blacklist))

enum {
	UNI_HDR_TRANS_EN,
	UNI_HDR_TRANS_VLAN,
	UNI_HDR_TRANS_BLACKLIST,
};

enum {
	RATE_PARAM_FIXED = 3,
	RATE_PARAM_MMPS_UPDATE = 5,
	RATE_PARAM_FIXED_HE_LTF = 7,
	RATE_PARAM_FIXED_MCS,
	RATE_PARAM_FIXED_GI = 11,
	RATE_PARAM_AUTO = 20,
};

enum {
	BF_SOUNDING_ON = 1,
	BF_HW_EN_UPDATE = 17,
	BF_MOD_EN_CTRL = 20,
};

enum {
	CMD_BAND_NONE,
	CMD_BAND_24G,
	CMD_BAND_5G,
	CMD_BAND_6G,
};

struct bss_req_hdr {
	u8 bss_idx;
	u8 __rsv[3];
} __packed;

enum {
	UNI_CHANNEL_SWITCH,
	UNI_CHANNEL_RX_PATH,
};

#define MT7996_BSS_UPDATE_MAX_SIZE	(sizeof(struct bss_req_hdr) +		\
					 sizeof(struct mt76_connac_bss_basic_tlv) +	\
					 sizeof(struct bss_rlm_tlv) +		\
					 sizeof(struct bss_ra_tlv) +		\
					 sizeof(struct bss_info_uni_he) +	\
					 sizeof(struct bss_rate_tlv) +		\
					 sizeof(struct bss_txcmd_tlv) +		\
					 sizeof(struct bss_power_save) +	\
					 sizeof(struct bss_sec_tlv) +		\
					 sizeof(struct bss_mld_tlv))

#define MT7996_STA_UPDATE_MAX_SIZE	(sizeof(struct sta_req_hdr) +		\
					 sizeof(struct sta_rec_basic) +		\
					 sizeof(struct sta_rec_bf) +		\
					 sizeof(struct sta_rec_ht) +		\
					 sizeof(struct sta_rec_he_v2) +		\
					 sizeof(struct sta_rec_ba_uni) +	\
					 sizeof(struct sta_rec_vht) +		\
					 sizeof(struct sta_rec_uapsd) + 	\
					 sizeof(struct sta_rec_amsdu) +		\
					 sizeof(struct sta_rec_bfee) +		\
					 sizeof(struct sta_rec_phy) +		\
					 sizeof(struct sta_rec_ra) +		\
					 sizeof(struct sta_rec_sec) +		\
					 sizeof(struct sta_rec_ra_fixed) +	\
					 sizeof(struct sta_rec_he_6g_capa) +	\
					 sizeof(struct sta_rec_hdrt) +		\
					 sizeof(struct sta_rec_hdr_trans) +	\
					 sizeof(struct tlv))

#define MT7996_BEACON_UPDATE_SIZE	(sizeof(struct bss_req_hdr) +		\
					 sizeof(struct bss_bcn_content_tlv) +	\
					 sizeof(struct bss_bcn_cntdwn_tlv) +	\
					 sizeof(struct bss_bcn_mbss_tlv))

#define MT7996_INBAND_FRAME_SIZE	(sizeof(struct bss_req_hdr) +		\
					 sizeof(struct bss_inband_discovery_tlv))

enum {
	UNI_BAND_CONFIG_RADIO_ENABLE,
	UNI_BAND_CONFIG_RTS_THRESHOLD = 0x08,
};

enum {
	UNI_WSYS_CONFIG_FW_LOG_CTRL,
	UNI_WSYS_CONFIG_FW_DBG_CTRL,
};

enum {
	UNI_RDD_CTRL_PARM,
	UNI_RDD_CTRL_SET_TH = 0x3,
};

enum {
	UNI_EFUSE_ACCESS = 1,
	UNI_EFUSE_BUFFER_MODE,
	UNI_EFUSE_FREE_BLOCK,
	UNI_EFUSE_BUFFER_RD,
};

enum {
	UNI_VOW_DRR_CTRL,
	UNI_VOW_RX_AT_AIRTIME_EN = 0x0b,
	UNI_VOW_RX_AT_AIRTIME_CLR_EN = 0x0e,
};

enum {
	UNI_CMD_MIB_DATA,
};

enum {
	UNI_POWER_OFF,
};

enum {
	UNI_CMD_TWT_ARGT_UPDATE = 0x0,
	UNI_CMD_TWT_MGMT_OFFLOAD,
};

enum {
	UNI_RRO_DEL_ENTRY = 0x1,
	UNI_RRO_SET_PLATFORM_TYPE,
	UNI_RRO_GET_BA_SESSION_TABLE,
	UNI_RRO_SET_BYPASS_MODE,
	UNI_RRO_SET_TXFREE_PATH,
};

enum{
	UNI_CMD_SR_ENABLE = 0x1,
	UNI_CMD_SR_ENABLE_SD,
	UNI_CMD_SR_ENABLE_MODE,
	UNI_CMD_SR_ENABLE_DPD = 0x12,
	UNI_CMD_SR_ENABLE_TX,
	UNI_CMD_SR_SET_SRG_BITMAP = 0x80,
	UNI_CMD_SR_SET_PARAM = 0xc1,
	UNI_CMD_SR_SET_SIGA = 0xd0,
};

enum {
	UNI_CMD_ACCESS_REG_BASIC = 0x0,
	UNI_CMD_ACCESS_RF_REG_BASIC,
};

enum {
	UNI_CMD_SER_QUERY = 0x0,
	UNI_CMD_SER_SET = 0x2,
	UNI_CMD_SER_TRIGGER = 0x3,
};

enum {
	SER_QUERY,
	/* recovery */
	SER_SET_RECOVER_L1,
	SER_SET_RECOVER_L2,
	SER_SET_RECOVER_L3_RX_ABORT,
	SER_SET_RECOVER_L3_TX_ABORT,
	SER_SET_RECOVER_L3_TX_DISABLE,
	SER_SET_RECOVER_L3_BF,
	/* action */
	SER_ENABLE = 2,
	SER_RECOVER
};

enum {
	MT7996_SEC_MODE_PLAIN,
	MT7996_SEC_MODE_AES,
	MT7996_SEC_MODE_SCRAMBLE,
	MT7996_SEC_MODE_MAX,
};

#define MT7996_PATCH_SEC		GENMASK(31, 24)
#define MT7996_PATCH_SCRAMBLE_KEY	GENMASK(15, 8)
#define MT7996_PATCH_AES_KEY		GENMASK(7, 0)

#define MT7996_SEC_ENCRYPT		BIT(0)
#define MT7996_SEC_KEY_IDX		GENMASK(2, 1)
#define MT7996_SEC_IV			BIT(3)

#endif
