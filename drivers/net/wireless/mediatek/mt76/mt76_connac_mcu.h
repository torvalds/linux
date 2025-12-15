/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (C) 2020 MediaTek Inc. */

#ifndef __MT76_CONNAC_MCU_H
#define __MT76_CONNAC_MCU_H

#include "mt76_connac.h"

#define FW_FEATURE_SET_ENCRYPT		BIT(0)
#define FW_FEATURE_SET_KEY_IDX		GENMASK(2, 1)
#define FW_FEATURE_ENCRY_MODE		BIT(4)
#define FW_FEATURE_OVERRIDE_ADDR	BIT(5)
#define FW_FEATURE_NON_DL		BIT(6)

#define DL_MODE_ENCRYPT			BIT(0)
#define DL_MODE_KEY_IDX			GENMASK(2, 1)
#define DL_MODE_RESET_SEC_IV		BIT(3)
#define DL_MODE_WORKING_PDA_CR4		BIT(4)
#define DL_MODE_VALID_RAM_ENTRY         BIT(5)
#define DL_CONFIG_ENCRY_MODE_SEL	BIT(6)
#define DL_MODE_NEED_RSP		BIT(31)

#define FW_START_OVERRIDE		BIT(0)
#define FW_START_WORKING_PDA_CR4	BIT(2)
#define FW_START_WORKING_PDA_DSP	BIT(3)

#define PATCH_SEC_NOT_SUPPORT		GENMASK(31, 0)
#define PATCH_SEC_TYPE_MASK		GENMASK(15, 0)
#define PATCH_SEC_TYPE_INFO		0x2

#define PATCH_SEC_ENC_TYPE_MASK			GENMASK(31, 24)
#define PATCH_SEC_ENC_TYPE_PLAIN		0x00
#define PATCH_SEC_ENC_TYPE_AES			0x01
#define PATCH_SEC_ENC_TYPE_SCRAMBLE		0x02
#define PATCH_SEC_ENC_SCRAMBLE_INFO_MASK	GENMASK(15, 0)
#define PATCH_SEC_ENC_AES_KEY_MASK		GENMASK(7, 0)

enum {
	FW_TYPE_DEFAULT = 0,
	FW_TYPE_CLC = 2,
	FW_TYPE_MAX_NUM = 255
};

#define MCU_PQ_ID(p, q)		(((p) << 15) | ((q) << 10))
#define MCU_PKT_ID		0xa0

struct mt76_connac2_mcu_txd {
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

	u32 rsv[5];
} __packed __aligned(4);

/**
 * struct mt76_connac2_mcu_uni_txd - mcu command descriptor for connac2 and connac3
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
struct mt76_connac2_mcu_uni_txd {
	__le32 txd[8];

	/* DW1 */
	__le16 len;
	__le16 cid;

	/* DW2 */
	u8 rsv;
	u8 pkt_type;
	u8 frag_n;
	u8 seq;

	/* DW3 */
	__le16 checksum;
	u8 s2d_index;
	u8 option;

	/* DW4 */
	u8 rsv1[4];
} __packed __aligned(4);

struct mt76_connac2_mcu_rxd {
	/* New members MUST be added within the struct_group() macro below. */
	struct_group_tagged(mt76_connac2_mcu_rxd_hdr, hdr,
		__le32 rxd[6];

		__le16 len;
		__le16 pkt_type_id;

		u8 eid;
		u8 seq;
		u8 option;
		u8 rsv;
		u8 ext_eid;
		u8 rsv1[2];
		u8 s2d_index;
	);

	u8 tlv[];
};
static_assert(offsetof(struct mt76_connac2_mcu_rxd, tlv) == sizeof(struct mt76_connac2_mcu_rxd_hdr),
	      "struct member likely outside of struct_group_tagged()");

struct mt76_connac2_patch_hdr {
	char build_date[16];
	char platform[4];
	__be32 hw_sw_ver;
	__be32 patch_ver;
	__be16 checksum;
	u16 rsv;
	struct {
		__be32 patch_ver;
		__be32 subsys;
		__be32 feature;
		__be32 n_region;
		__be32 crc;
		u32 rsv[11];
	} desc;
} __packed;

struct mt76_connac2_patch_sec {
	__be32 type;
	__be32 offs;
	__be32 size;
	union {
		__be32 spec[13];
		struct {
			__be32 addr;
			__be32 len;
			__be32 sec_key_idx;
			__be32 align_len;
			u32 rsv[9];
		} info;
	};
} __packed;

struct mt76_connac2_fw_trailer {
	u8 chip_id;
	u8 eco_code;
	u8 n_region;
	u8 format_ver;
	u8 format_flag;
	u8 rsv[2];
	char fw_ver[10];
	char build_date[15];
	__le32 crc;
} __packed;

struct mt76_connac2_fw_region {
	__le32 decomp_crc;
	__le32 decomp_len;
	__le32 decomp_blk_sz;
	u8 rsv[4];
	__le32 addr;
	__le32 len;
	u8 feature_set;
	u8 type;
	u8 rsv1[14];
} __packed;

struct tlv {
	__le16 tag;
	__le16 len;
	u8 data[];
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

/* sta_rec */

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
	/* mt7915 - mt7921 */
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

struct sta_rec_he_v2 {
	__le16 tag;
	__le16 len;
	u8 he_mac_cap[6];
	u8 he_phy_cap[11];
	u8 pkt_ext;
	/* 0: BW80, 1: BW160, 2: BW8080 */
	__le16 max_nss_mcs[CMD_HE_MCS_BW_NUM];
} __packed;

struct sta_rec_amsdu {
	__le16 tag;
	__le16 len;
	u8 max_amsdu_num;
	u8 max_mpdu_size;
	u8 amsdu_en;
	u8 rsv;
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

#define RA_LEGACY_OFDM GENMASK(13, 6)
#define RA_LEGACY_CCK  GENMASK(3, 0)
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
	u8 max_ampdu_len; /* connac3 */
	u8 rsv[1];
} __packed;

struct sta_rec_he_6g_capa {
	__le16 tag;
	__le16 len;
	__le16 capa;
	u8 rsv[2];
} __packed;

struct sta_rec_pn_info {
	__le16 tag;
	__le16 len;
	u8 pn[6];
	u8 tsc_type;
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
	u8 nrow_gt_bw80;
	u8 ncol_gt_bw80;
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
		u8 rx_ctrl_frame_to_mbss;
		u8 rsv[2];
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

struct sta_rec_remove {
	__le16 tag;
	__le16 len;
	u8 action;
	u8 pad[3];
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

	u8 spe_idx;
	u8 short_preamble;
	u8 is_5g;
	u8 mmps_mode;
} __packed;

struct sta_rec_tx_proc {
	__le16 tag;
	__le16 len;
	__le32 flag;
} __packed;

/* wtbl_rec */

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
	u8 no_rx_trans;
	u8 rsv;
} __packed;

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

/* mt7615 only */

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

#define MT76_CONNAC_WTBL_UPDATE_MAX_SIZE (sizeof(struct wtbl_req_hdr) +	\
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

#define MT76_CONNAC_STA_UPDATE_MAX_SIZE	(sizeof(struct sta_req_hdr) +	\
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
					 sizeof(struct sta_rec_ra) +	\
					 sizeof(struct sta_rec_sec) +	\
					 sizeof(struct sta_rec_ra_fixed) + \
					 sizeof(struct sta_rec_he_6g_capa) + \
					 sizeof(struct sta_rec_pn_info) + \
					 sizeof(struct sta_rec_tx_proc) + \
					 sizeof(struct tlv) +		\
					 MT76_CONNAC_WTBL_UPDATE_MAX_SIZE)

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
	STA_REC_HE_6G = 0x17,
	STA_REC_HE_V2 = 0x19,
	STA_REC_MLD = 0x20,
	STA_REC_EHT_MLD = 0x21,
	STA_REC_EHT = 0x22,
	STA_REC_MLD_OFF = 0x23,
	STA_REC_REMOVE = 0x25,
	STA_REC_PN_INFO = 0x26,
	STA_REC_KEY_V3 = 0x27,
	STA_REC_HDRT = 0x28,
	STA_REC_HDR_TRANS = 0x2B,
	STA_REC_MAX_NUM
};

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

#define STA_TYPE_STA			BIT(0)
#define STA_TYPE_AP			BIT(1)
#define STA_TYPE_ADHOC			BIT(2)
#define STA_TYPE_WDS			BIT(4)
#define STA_TYPE_BC			BIT(5)

#define NETWORK_INFRA			BIT(16)
#define NETWORK_P2P			BIT(17)
#define NETWORK_IBSS			BIT(18)
#define NETWORK_WDS			BIT(21)

#define SCAN_FUNC_RANDOM_MAC		BIT(0)
#define SCAN_FUNC_RNR_SCAN		BIT(3)
#define SCAN_FUNC_SPLIT_SCAN		BIT(5)

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

#define PHY_MODE_A				BIT(0)
#define PHY_MODE_B				BIT(1)
#define PHY_MODE_G				BIT(2)
#define PHY_MODE_GN				BIT(3)
#define PHY_MODE_AN				BIT(4)
#define PHY_MODE_AC				BIT(5)
#define PHY_MODE_AX_24G				BIT(6)
#define PHY_MODE_AX_5G				BIT(7)

#define PHY_MODE_AX_6G				BIT(0) /* phymode_ext */
#define PHY_MODE_BE_24G				BIT(1)
#define PHY_MODE_BE_5G				BIT(2)
#define PHY_MODE_BE_6G				BIT(3)

#define MODE_CCK				BIT(0)
#define MODE_OFDM				BIT(1)
#define MODE_HT					BIT(2)
#define MODE_VHT				BIT(3)
#define MODE_HE					BIT(4)
#define MODE_EHT				BIT(5)

#define STA_CAP_WMM				BIT(0)
#define STA_CAP_SGI_20				BIT(4)
#define STA_CAP_SGI_40				BIT(5)
#define STA_CAP_TX_STBC				BIT(6)
#define STA_CAP_RX_STBC				BIT(7)
#define STA_CAP_VHT_SGI_80			BIT(16)
#define STA_CAP_VHT_SGI_160			BIT(17)
#define STA_CAP_VHT_TX_STBC			BIT(18)
#define STA_CAP_VHT_RX_STBC			BIT(19)
#define STA_CAP_VHT_LDPC			BIT(23)
#define STA_CAP_LDPC				BIT(24)
#define STA_CAP_HT				BIT(26)
#define STA_CAP_VHT				BIT(27)
#define STA_CAP_HE				BIT(28)

enum {
	PHY_TYPE_HR_DSSS_INDEX = 0,
	PHY_TYPE_ERP_INDEX,
	PHY_TYPE_ERP_P2P_INDEX,
	PHY_TYPE_OFDM_INDEX,
	PHY_TYPE_HT_INDEX,
	PHY_TYPE_VHT_INDEX,
	PHY_TYPE_HE_INDEX,
	PHY_TYPE_BE_INDEX,
	PHY_TYPE_INDEX_NUM
};

#define HR_DSSS_ERP_BASIC_RATE			GENMASK(3, 0)
#define OFDM_BASIC_RATE				(BIT(6) | BIT(8) | BIT(10))

#define PHY_TYPE_BIT_HR_DSSS			BIT(PHY_TYPE_HR_DSSS_INDEX)
#define PHY_TYPE_BIT_ERP			BIT(PHY_TYPE_ERP_INDEX)
#define PHY_TYPE_BIT_OFDM			BIT(PHY_TYPE_OFDM_INDEX)
#define PHY_TYPE_BIT_HT				BIT(PHY_TYPE_HT_INDEX)
#define PHY_TYPE_BIT_VHT			BIT(PHY_TYPE_VHT_INDEX)
#define PHY_TYPE_BIT_HE				BIT(PHY_TYPE_HE_INDEX)
#define PHY_TYPE_BIT_BE				BIT(PHY_TYPE_BE_INDEX)

#define MT_WTBL_RATE_TX_MODE			GENMASK(9, 6)
#define MT_WTBL_RATE_MCS			GENMASK(5, 0)
#define MT_WTBL_RATE_NSS			GENMASK(12, 10)
#define MT_WTBL_RATE_HE_GI			GENMASK(7, 4)
#define MT_WTBL_RATE_GI				GENMASK(3, 0)

#define MT_WTBL_W5_CHANGE_BW_RATE		GENMASK(7, 5)
#define MT_WTBL_W5_SHORT_GI_20			BIT(8)
#define MT_WTBL_W5_SHORT_GI_40			BIT(9)
#define MT_WTBL_W5_SHORT_GI_80			BIT(10)
#define MT_WTBL_W5_SHORT_GI_160			BIT(11)
#define MT_WTBL_W5_BW_CAP			GENMASK(13, 12)
#define MT_WTBL_W5_MPDU_FAIL_COUNT		GENMASK(25, 23)
#define MT_WTBL_W5_MPDU_OK_COUNT		GENMASK(28, 26)
#define MT_WTBL_W5_RATE_IDX			GENMASK(31, 29)

enum {
	WTBL_RESET_AND_SET = 1,
	WTBL_SET,
	WTBL_QUERY,
	WTBL_RESET_ALL
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

enum {
	DEV_INFO_ACTIVE,
	DEV_INFO_MAX_NUM
};

/* event table */
enum {
	MCU_EVENT_TARGET_ADDRESS_LEN = 0x01,
	MCU_EVENT_FW_START = 0x01,
	MCU_EVENT_GENERIC = 0x01,
	MCU_EVENT_ACCESS_REG = 0x02,
	MCU_EVENT_MT_PATCH_SEM = 0x04,
	MCU_EVENT_REG_ACCESS = 0x05,
	MCU_EVENT_LP_INFO = 0x07,
	MCU_EVENT_SCAN_DONE = 0x0d,
	MCU_EVENT_TX_DONE = 0x0f,
	MCU_EVENT_ROC = 0x10,
	MCU_EVENT_BSS_ABSENCE  = 0x11,
	MCU_EVENT_BSS_BEACON_LOSS = 0x13,
	MCU_EVENT_CH_PRIVILEGE = 0x18,
	MCU_EVENT_SCHED_SCAN_DONE = 0x23,
	MCU_EVENT_DBG_MSG = 0x27,
	MCU_EVENT_RSSI_NOTIFY = 0x96,
	MCU_EVENT_TXPWR = 0xd0,
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
	MCU_EXT_EVENT_WA_TX_STAT = 0x74,
	MCU_EXT_EVENT_BCC_NOTIFY = 0x75,
	MCU_EXT_EVENT_WF_RF_PIN_CTRL = 0x9a,
	MCU_EXT_EVENT_MURU_CTRL = 0x9f,
};

/* unified event table */
enum {
	MCU_UNI_EVENT_RESULT = 0x01,
	MCU_UNI_EVENT_HIF_CTRL = 0x03,
	MCU_UNI_EVENT_FW_LOG_2_HOST = 0x04,
	MCU_UNI_EVENT_ACCESS_REG = 0x6,
	MCU_UNI_EVENT_IE_COUNTDOWN = 0x09,
	MCU_UNI_EVENT_COREDUMP = 0x0a,
	MCU_UNI_EVENT_BSS_BEACON_LOSS = 0x0c,
	MCU_UNI_EVENT_SCAN_DONE = 0x0e,
	MCU_UNI_EVENT_RDD_REPORT = 0x11,
	MCU_UNI_EVENT_ROC = 0x27,
	MCU_UNI_EVENT_TX_DONE = 0x2d,
	MCU_UNI_EVENT_THERMAL = 0x35,
	MCU_UNI_EVENT_RSSI_MONITOR = 0x41,
	MCU_UNI_EVENT_NIC_CAPAB = 0x43,
	MCU_UNI_EVENT_WED_RRO = 0x57,
	MCU_UNI_EVENT_PER_STA_INFO = 0x6d,
	MCU_UNI_EVENT_ALL_STA_INFO = 0x6e,
	MCU_UNI_EVENT_SDO = 0x83,
};

#define MCU_UNI_CMD_EVENT			BIT(1)
#define MCU_UNI_CMD_UNSOLICITED_EVENT		BIT(2)

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
	FW_STATE_RDY = 7
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
	MCU_CIPHER_BIP_CMAC_256,
	MCU_CIPHER_BCN_PROT_CMAC_128,
	MCU_CIPHER_BCN_PROT_CMAC_256,
	MCU_CIPHER_BCN_PROT_GMAC_128,
	MCU_CIPHER_BCN_PROT_GMAC_256,
	MCU_CIPHER_BIP_GMAC_128,
	MCU_CIPHER_BIP_GMAC_256,
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

#define MCU_CMD_ACK				BIT(0)
#define MCU_CMD_UNI				BIT(1)
#define MCU_CMD_SET				BIT(2)

#define MCU_CMD_UNI_EXT_ACK			(MCU_CMD_ACK | MCU_CMD_UNI | \
						 MCU_CMD_SET)
#define MCU_CMD_UNI_QUERY_ACK			(MCU_CMD_ACK | MCU_CMD_UNI)

#define __MCU_CMD_FIELD_ID			GENMASK(7, 0)
#define __MCU_CMD_FIELD_EXT_ID			GENMASK(15, 8)
#define __MCU_CMD_FIELD_QUERY			BIT(16)
#define __MCU_CMD_FIELD_UNI			BIT(17)
#define __MCU_CMD_FIELD_CE			BIT(18)
#define __MCU_CMD_FIELD_WA			BIT(19)
#define __MCU_CMD_FIELD_WM			BIT(20)

#define MCU_CMD(_t)				FIELD_PREP(__MCU_CMD_FIELD_ID,		\
							   MCU_CMD_##_t)
#define MCU_EXT_CMD(_t)				(MCU_CMD(EXT_CID) | \
						 FIELD_PREP(__MCU_CMD_FIELD_EXT_ID,	\
							    MCU_EXT_CMD_##_t))
#define MCU_EXT_QUERY(_t)			(MCU_EXT_CMD(_t) | __MCU_CMD_FIELD_QUERY)
#define MCU_UNI_CMD(_t)				(__MCU_CMD_FIELD_UNI |			\
						 FIELD_PREP(__MCU_CMD_FIELD_ID,		\
							    MCU_UNI_CMD_##_t))

#define MCU_UNI_QUERY(_t)			(__MCU_CMD_FIELD_UNI | __MCU_CMD_FIELD_QUERY | \
						 FIELD_PREP(__MCU_CMD_FIELD_ID,		\
							    MCU_UNI_CMD_##_t))

#define MCU_CE_CMD(_t)				(__MCU_CMD_FIELD_CE |			\
						 FIELD_PREP(__MCU_CMD_FIELD_ID,		\
							   MCU_CE_CMD_##_t))
#define MCU_CE_QUERY(_t)			(MCU_CE_CMD(_t) | __MCU_CMD_FIELD_QUERY)

#define MCU_WA_CMD(_t)				(MCU_CMD(_t) | __MCU_CMD_FIELD_WA)
#define MCU_WA_EXT_CMD(_t)			(MCU_EXT_CMD(_t) | __MCU_CMD_FIELD_WA)
#define MCU_WA_PARAM_CMD(_t)			(MCU_WA_CMD(WA_PARAM) | \
						 FIELD_PREP(__MCU_CMD_FIELD_EXT_ID, \
							    MCU_WA_PARAM_CMD_##_t))

#define MCU_WM_UNI_CMD(_t)			(MCU_UNI_CMD(_t) |		\
						 __MCU_CMD_FIELD_WM)
#define MCU_WM_UNI_CMD_QUERY(_t)		(MCU_UNI_CMD(_t) |		\
						 __MCU_CMD_FIELD_QUERY |	\
						 __MCU_CMD_FIELD_WM)
#define MCU_WA_UNI_CMD(_t)			(MCU_UNI_CMD(_t) |		\
						 __MCU_CMD_FIELD_WA)
#define MCU_WMWA_UNI_CMD(_t)			(MCU_WM_UNI_CMD(_t) |		\
						 __MCU_CMD_FIELD_WA)

enum {
	MCU_EXT_CMD_EFUSE_ACCESS = 0x01,
	MCU_EXT_CMD_RF_REG_ACCESS = 0x02,
	MCU_EXT_CMD_RF_TEST = 0x04,
	MCU_EXT_CMD_ID_RADIO_ON_OFF_CTRL = 0x05,
	MCU_EXT_CMD_PM_STATE_CTRL = 0x07,
	MCU_EXT_CMD_CHANNEL_SWITCH = 0x08,
	MCU_EXT_CMD_SET_TX_POWER_CTRL = 0x11,
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
	MCU_EXT_CMD_DBDC_CTRL = 0x45,
	MCU_EXT_CMD_MAC_INIT_CTRL = 0x46,
	MCU_EXT_CMD_RX_HDR_TRANS = 0x47,
	MCU_EXT_CMD_MUAR_UPDATE = 0x48,
	MCU_EXT_CMD_BCN_OFFLOAD = 0x49,
	MCU_EXT_CMD_RX_AIRTIME_CTRL = 0x4a,
	MCU_EXT_CMD_SET_RX_PATH = 0x4e,
	MCU_EXT_CMD_EFUSE_FREE_BLOCK = 0x4f,
	MCU_EXT_CMD_TX_POWER_FEATURE_CTRL = 0x58,
	MCU_EXT_CMD_RXDCOC_CAL = 0x59,
	MCU_EXT_CMD_GET_MIB_INFO = 0x5a,
	MCU_EXT_CMD_TXDPD_CAL = 0x60,
	MCU_EXT_CMD_CAL_CACHE = 0x67,
	MCU_EXT_CMD_RED_ENABLE = 0x68,
	MCU_EXT_CMD_CP_SUPPORT = 0x75,
	MCU_EXT_CMD_SET_RADAR_TH = 0x7c,
	MCU_EXT_CMD_SET_RDD_PATTERN = 0x7d,
	MCU_EXT_CMD_MWDS_SUPPORT = 0x80,
	MCU_EXT_CMD_SET_SER_TRIGGER = 0x81,
	MCU_EXT_CMD_TWT_AGRT_UPDATE = 0x94,
	MCU_EXT_CMD_FW_DBG_CTRL = 0x95,
	MCU_EXT_CMD_OFFCH_SCAN_CTRL = 0x9a,
	MCU_EXT_CMD_SET_RDD_TH = 0x9d,
	MCU_EXT_CMD_MURU_CTRL = 0x9f,
	MCU_EXT_CMD_SET_SPR = 0xa8,
	MCU_EXT_CMD_GROUP_PRE_CAL_INFO = 0xab,
	MCU_EXT_CMD_DPD_PRE_CAL_INFO = 0xac,
	MCU_EXT_CMD_PHY_STAT_INFO = 0xad,
	MCU_EXT_CMD_WF_RF_PIN_CTRL = 0xbd,
};

enum {
	MCU_UNI_CMD_DEV_INFO_UPDATE = 0x01,
	MCU_UNI_CMD_BSS_INFO_UPDATE = 0x02,
	MCU_UNI_CMD_STA_REC_UPDATE = 0x03,
	MCU_UNI_CMD_EDCA_UPDATE = 0x04,
	MCU_UNI_CMD_SUSPEND = 0x05,
	MCU_UNI_CMD_OFFLOAD = 0x06,
	MCU_UNI_CMD_HIF_CTRL = 0x07,
	MCU_UNI_CMD_BAND_CONFIG = 0x08,
	MCU_UNI_CMD_REPT_MUAR = 0x09,
	MCU_UNI_CMD_WSYS_CONFIG = 0x0b,
	MCU_UNI_CMD_REG_ACCESS = 0x0d,
	MCU_UNI_CMD_CHIP_CONFIG = 0x0e,
	MCU_UNI_CMD_POWER_CTRL = 0x0f,
	MCU_UNI_CMD_RX_HDR_TRANS = 0x12,
	MCU_UNI_CMD_SER = 0x13,
	MCU_UNI_CMD_TWT = 0x14,
	MCU_UNI_CMD_SET_DOMAIN_INFO = 0x15,
	MCU_UNI_CMD_SCAN_REQ = 0x16,
	MCU_UNI_CMD_RDD_CTRL = 0x19,
	MCU_UNI_CMD_GET_MIB_INFO = 0x22,
	MCU_UNI_CMD_GET_STAT_INFO = 0x23,
	MCU_UNI_CMD_SNIFFER = 0x24,
	MCU_UNI_CMD_SR = 0x25,
	MCU_UNI_CMD_ROC = 0x27,
	MCU_UNI_CMD_SET_DBDC_PARMS = 0x28,
	MCU_UNI_CMD_TXPOWER = 0x2b,
	MCU_UNI_CMD_SET_POWER_LIMIT = 0x2c,
	MCU_UNI_CMD_EFUSE_CTRL = 0x2d,
	MCU_UNI_CMD_RA = 0x2f,
	MCU_UNI_CMD_MURU = 0x31,
	MCU_UNI_CMD_TESTMODE_RX_STAT = 0x32,
	MCU_UNI_CMD_BF = 0x33,
	MCU_UNI_CMD_CHANNEL_SWITCH = 0x34,
	MCU_UNI_CMD_THERMAL = 0x35,
	MCU_UNI_CMD_VOW = 0x37,
	MCU_UNI_CMD_FIXED_RATE_TABLE = 0x40,
	MCU_UNI_CMD_RSSI_MONITOR = 0x41,
	MCU_UNI_CMD_TESTMODE_CTRL = 0x46,
	MCU_UNI_CMD_RRO = 0x57,
	MCU_UNI_CMD_OFFCH_SCAN_CTRL = 0x58,
	MCU_UNI_CMD_PER_STA_INFO = 0x6d,
	MCU_UNI_CMD_ALL_STA_INFO = 0x6e,
	MCU_UNI_CMD_ASSERT_DUMP = 0x6f,
	MCU_UNI_CMD_RADIO_STATUS = 0x80,
	MCU_UNI_CMD_SDO = 0x88,
};

enum {
	MCU_CMD_TARGET_ADDRESS_LEN_REQ = 0x01,
	MCU_CMD_FW_START_REQ = 0x02,
	MCU_CMD_INIT_ACCESS_REG = 0x3,
	MCU_CMD_NIC_POWER_CTRL = 0x4,
	MCU_CMD_PATCH_START_REQ = 0x05,
	MCU_CMD_PATCH_FINISH_REQ = 0x07,
	MCU_CMD_PATCH_SEM_CONTROL = 0x10,
	MCU_CMD_WA_PARAM = 0xc4,
	MCU_CMD_EXT_CID = 0xed,
	MCU_CMD_FW_SCATTER = 0xee,
	MCU_CMD_RESTART_DL_REQ = 0xef,
};

/* offload mcu commands */
enum {
	MCU_CE_CMD_TEST_CTRL = 0x01,
	MCU_CE_CMD_START_HW_SCAN = 0x03,
	MCU_CE_CMD_SET_PS_PROFILE = 0x05,
	MCU_CE_CMD_SET_RX_FILTER = 0x0a,
	MCU_CE_CMD_SET_CHAN_DOMAIN = 0x0f,
	MCU_CE_CMD_SET_BSS_CONNECTED = 0x16,
	MCU_CE_CMD_SET_BSS_ABORT = 0x17,
	MCU_CE_CMD_CANCEL_HW_SCAN = 0x1b,
	MCU_CE_CMD_SET_ROC = 0x1c,
	MCU_CE_CMD_SET_EDCA_PARMS = 0x1d,
	MCU_CE_CMD_SET_P2P_OPPPS = 0x33,
	MCU_CE_CMD_SET_CLC = 0x5c,
	MCU_CE_CMD_SET_RATE_TX_POWER = 0x5d,
	MCU_CE_CMD_SCHED_SCAN_ENABLE = 0x61,
	MCU_CE_CMD_SCHED_SCAN_REQ = 0x62,
	MCU_CE_CMD_GET_NIC_CAPAB = 0x8a,
	MCU_CE_CMD_RSSI_MONITOR = 0xa1,
	MCU_CE_CMD_SET_MU_EDCA_PARMS = 0xb0,
	MCU_CE_CMD_REG_WRITE = 0xc0,
	MCU_CE_CMD_REG_READ = 0xc0,
	MCU_CE_CMD_CHIP_CONFIG = 0xca,
	MCU_CE_CMD_FWLOG_2_HOST = 0xc5,
	MCU_CE_CMD_GET_WTBL = 0xcd,
	MCU_CE_CMD_GET_TXPWR = 0xd0,
};

enum {
	PATCH_SEM_RELEASE,
	PATCH_SEM_GET
};

enum {
	UNI_BSS_INFO_BASIC = 0,
	UNI_BSS_INFO_RA = 1,
	UNI_BSS_INFO_RLM = 2,
	UNI_BSS_INFO_BSS_COLOR = 4,
	UNI_BSS_INFO_HE_BASIC = 5,
	UNI_BSS_INFO_11V_MBSSID = 6,
	UNI_BSS_INFO_BCN_CONTENT = 7,
	UNI_BSS_INFO_BCN_CSA = 8,
	UNI_BSS_INFO_BCN_BCC = 9,
	UNI_BSS_INFO_BCN_MBSSID = 10,
	UNI_BSS_INFO_RATE = 11,
	UNI_BSS_INFO_QBSS = 15,
	UNI_BSS_INFO_SEC = 16,
	UNI_BSS_INFO_BCN_PROT = 17,
	UNI_BSS_INFO_TXCMD = 18,
	UNI_BSS_INFO_UAPSD = 19,
	UNI_BSS_INFO_PS = 21,
	UNI_BSS_INFO_BCNFT = 22,
	UNI_BSS_INFO_IFS_TIME = 23,
	UNI_BSS_INFO_OFFLOAD = 25,
	UNI_BSS_INFO_MLD = 26,
	UNI_BSS_INFO_PM_DISABLE = 27,
	UNI_BSS_INFO_EHT = 30,
};

enum {
	UNI_OFFLOAD_OFFLOAD_ARP,
	UNI_OFFLOAD_OFFLOAD_ND,
	UNI_OFFLOAD_OFFLOAD_GTK_REKEY,
	UNI_OFFLOAD_OFFLOAD_BMC_RPY_DETECT,
};

enum UNI_ALL_STA_INFO_TAG {
	UNI_ALL_STA_TXRX_RATE,
	UNI_ALL_STA_TX_STAT,
	UNI_ALL_STA_TXRX_ADM_STAT,
	UNI_ALL_STA_TXRX_AIR_TIME,
	UNI_ALL_STA_DATA_TX_RETRY_COUNT,
	UNI_ALL_STA_GI_MODE,
	UNI_ALL_STA_TXRX_MSDU_COUNT,
	UNI_ALL_STA_MAX_NUM
};

enum {
	MT_NIC_CAP_TX_RESOURCE,
	MT_NIC_CAP_TX_EFUSE_ADDR,
	MT_NIC_CAP_COEX,
	MT_NIC_CAP_SINGLE_SKU,
	MT_NIC_CAP_CSUM_OFFLOAD,
	MT_NIC_CAP_HW_VER,
	MT_NIC_CAP_SW_VER,
	MT_NIC_CAP_MAC_ADDR,
	MT_NIC_CAP_PHY,
	MT_NIC_CAP_MAC,
	MT_NIC_CAP_FRAME_BUF,
	MT_NIC_CAP_BEAM_FORM,
	MT_NIC_CAP_LOCATION,
	MT_NIC_CAP_MUMIMO,
	MT_NIC_CAP_BUFFER_MODE_INFO,
	MT_NIC_CAP_HW_ADIE_VERSION = 0x14,
	MT_NIC_CAP_ANTSWP = 0x16,
	MT_NIC_CAP_WFDMA_REALLOC,
	MT_NIC_CAP_6G,
	MT_NIC_CAP_CHIP_CAP = 0x20,
	MT_NIC_CAP_EML_CAP = 0x22,
};

#define UNI_WOW_DETECT_TYPE_MAGIC		BIT(0)
#define UNI_WOW_DETECT_TYPE_ANY			BIT(1)
#define UNI_WOW_DETECT_TYPE_DISCONNECT		BIT(2)
#define UNI_WOW_DETECT_TYPE_GTK_REKEY_FAIL	BIT(3)
#define UNI_WOW_DETECT_TYPE_BCN_LOST		BIT(4)
#define UNI_WOW_DETECT_TYPE_SCH_SCAN_HIT	BIT(5)
#define UNI_WOW_DETECT_TYPE_BITMAP		BIT(6)

enum {
	UNI_SUSPEND_MODE_SETTING,
	UNI_SUSPEND_WOW_CTRL,
	UNI_SUSPEND_WOW_GPIO_PARAM,
	UNI_SUSPEND_WOW_WAKEUP_PORT,
	UNI_SUSPEND_WOW_PATTERN,
};

enum {
	WOW_USB = 1,
	WOW_PCIE = 2,
	WOW_GPIO = 3,
};

struct mt76_connac_bss_basic_tlv {
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
		     * bit(6): AX2
		     * bit(7): AX5
		     * bit(8): AX6
		     */
	__le16 sta_idx;
	__le16 nonht_basic_phy;
	u8 phymode_ext; /* bit(0) AX_6G */
	u8 link_idx;
} __packed;

struct mt76_connac_bss_qos_tlv {
	__le16 tag;
	__le16 len;
	u8 qos;
	u8 pad[3];
} __packed;

struct mt76_connac_beacon_loss_event {
	u8 bss_idx;
	u8 reason;
	u8 pad[2];
} __packed;

struct mt76_connac_rssi_notify_event {
	__le32 rssi[4];
} __packed;

struct mt76_connac_mcu_bss_event {
	u8 bss_idx;
	u8 is_absent;
	u8 free_quota;
	u8 pad;
} __packed;

struct mt76_connac_mcu_scan_ssid {
	__le32 ssid_len;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
} __packed;

struct mt76_connac_mcu_scan_channel {
	u8 band; /* 1: 2.4GHz
		  * 2: 5.0GHz
		  * Others: Reserved
		  */
	u8 channel_num;
} __packed;

struct mt76_connac_mcu_scan_match {
	__le32 rssi_th;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 ssid_len;
	u8 rsv[3];
} __packed;

struct mt76_connac_hw_scan_req {
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
	struct mt76_connac_mcu_scan_ssid ssids[4];
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
	struct mt76_connac_mcu_scan_channel channels[32];
	__le16 ies_len;
	u8 ies[MT76_CONNAC_SCAN_IE_LEN];
	/* following fields are valid if version > 0 */
	u8 ext_channels_num;
	u8 ext_ssids_num;
	__le16 channel_min_dwell_time;
	struct mt76_connac_mcu_scan_channel ext_channels[32];
	struct mt76_connac_mcu_scan_ssid ext_ssids[6];
	u8 bssid[ETH_ALEN];
	u8 random_mac[ETH_ALEN]; /* valid when BIT(1) in scan_func is set. */
	u8 pad[63];
	u8 ssid_type_ext;
} __packed;

#define MT76_CONNAC_SCAN_DONE_EVENT_MAX_CHANNEL_NUM		64

struct mt76_connac_hw_scan_done {
	u8 seq_num;
	u8 sparse_channel_num;
	struct mt76_connac_mcu_scan_channel sparse_channel;
	u8 complete_channel_num;
	u8 current_state;
	u8 version;
	u8 pad;
	__le32 beacon_scan_num;
	u8 pno_enabled;
	u8 pad2[3];
	u8 sparse_channel_valid_num;
	u8 pad3[3];
	u8 channel_num[MT76_CONNAC_SCAN_DONE_EVENT_MAX_CHANNEL_NUM];
	/* idle format for channel_idle_time
	 * 0: first bytes: idle time(ms) 2nd byte: dwell time(ms)
	 * 1: first bytes: idle time(8ms) 2nd byte: dwell time(8ms)
	 * 2: dwell time (16us)
	 */
	__le16 channel_idle_time[MT76_CONNAC_SCAN_DONE_EVENT_MAX_CHANNEL_NUM];
	/* beacon and probe response count */
	u8 beacon_probe_num[MT76_CONNAC_SCAN_DONE_EVENT_MAX_CHANNEL_NUM];
	u8 mdrdy_count[MT76_CONNAC_SCAN_DONE_EVENT_MAX_CHANNEL_NUM];
	__le32 beacon_2g_num;
	__le32 beacon_5g_num;
} __packed;

struct mt76_connac_sched_scan_req {
	u8 version;
	u8 seq_num;
	u8 stop_on_match;
	u8 ssids_num;
	u8 match_num;
	u8 pad;
	__le16 ie_len;
	struct mt76_connac_mcu_scan_ssid ssids[MT76_CONNAC_MAX_SCHED_SCAN_SSID];
	struct mt76_connac_mcu_scan_match match[MT76_CONNAC_MAX_SCAN_MATCH];
	u8 channel_type;
	u8 channels_num;
	u8 intervals_num;
	u8 scan_func; /* MT7663: BIT(0) eable random mac address */
	struct mt76_connac_mcu_scan_channel channels[64];
	__le16 intervals[MT76_CONNAC_MAX_NUM_SCHED_SCAN_INTERVAL];
	union {
		struct {
			u8 random_mac[ETH_ALEN];
			u8 pad2[58];
		} mt7663;
		struct {
			u8 bss_idx;
			u8 pad1[3];
			__le32 delay;
			u8 pad2[12];
			u8 random_mac[ETH_ALEN];
			u8 pad3[38];
		} mt7921;
	};
} __packed;

struct mt76_connac_sched_scan_done {
	u8 seq_num;
	u8 status; /* 0: ssid found */
	__le16 pad;
} __packed;

struct bss_info_uni_bss_color {
	__le16 tag;
	__le16 len;
	u8 enable;
	u8 bss_color;
	u8 rsv[2];
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

struct bss_info_uni_mbssid {
	__le16 tag;
	__le16 len;
	u8 max_indicator;
	u8 mbss_idx;
	u8 tx_bss_omac_idx;
	u8 rsv;
} __packed;

struct mt76_connac_gtk_rekey_tlv {
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
	u8 option; /* 1: rekey data update without enabling offload */
	u8 pad[1];
	__le32 proto; /* WPA-RSN-WAPI-OPSN */
	__le32 pairwise_cipher;
	__le32 group_cipher;
	__le32 key_mgmt; /* NONE-PSK-IEEE802.1X */
	__le32 mgmt_group_cipher;
	u8 reserverd[4];
} __packed;

#define MT76_CONNAC_WOW_MASK_MAX_LEN			16
#define MT76_CONNAC_WOW_PATTEN_MAX_LEN			128

struct mt76_connac_wow_pattern_tlv {
	__le16 tag;
	__le16 len;
	u8 index; /* pattern index */
	u8 enable; /* 0: disable
		    * 1: enable
		    */
	u8 data_len; /* pattern length */
	u8 pad;
	u8 mask[MT76_CONNAC_WOW_MASK_MAX_LEN];
	u8 pattern[MT76_CONNAC_WOW_PATTEN_MAX_LEN];
	u8 rsv[4];
} __packed;

struct mt76_connac_wow_ctrl_tlv {
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

struct mt76_connac_wow_gpio_param_tlv {
	__le16 tag;
	__le16 len;
	u8 gpio_pin;
	u8 trigger_lvl;
	u8 pad[2];
	__le32 gpio_interval;
	u8 rsv[4];
} __packed;

struct mt76_connac_arpns_tlv {
	__le16 tag;
	__le16 len;
	u8 mode;
	u8 ips_num;
	u8 option;
	u8 pad[1];
} __packed;

struct mt76_connac_suspend_tlv {
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

enum mt76_sta_info_state {
	MT76_STA_INFO_STATE_NONE,
	MT76_STA_INFO_STATE_AUTH,
	MT76_STA_INFO_STATE_ASSOC
};

struct mt76_sta_cmd_info {
	union {
		struct ieee80211_sta *sta;
		struct ieee80211_link_sta *link_sta;
	};
	struct mt76_wcid *wcid;

	struct ieee80211_vif *vif;
	struct ieee80211_bss_conf *link_conf;

	bool offload_fw;
	bool enable;
	bool newly;
	int cmd;
	u8 rcpi;
	u8 state;
};

#define MT_SKU_POWER_LIMIT	161

struct mt76_connac_sku_tlv {
	u8 channel;
	s8 pwr_limit[MT_SKU_POWER_LIMIT];
} __packed;

struct mt76_connac_tx_power_limit_tlv {
	/* DW0 - common info*/
	u8 ver;
	u8 pad0;
	__le16 len;
	/* DW1 - cmd hint */
	u8 n_chan; /* # channel */
	u8 band; /* 2.4GHz - 5GHz - 6GHz */
	u8 last_msg;
	u8 pad1;
	/* DW3 */
	u8 alpha2[4]; /* regulatory_request.alpha2 */
	u8 pad2[32];
} __packed;

struct mt76_connac_config {
	__le16 id;
	u8 type;
	u8 resp_type;
	__le16 data_size;
	__le16 resv;
	u8 data[320];
} __packed;

struct mt76_connac_mcu_uni_event {
	u8 cid;
	u8 pad[3];
	__le32 status; /* 0: success, others: fail */
} __packed;

struct mt76_connac_mcu_reg_event {
	__le32 reg;
	__le32 val;
} __packed;

static inline enum mcu_cipher_type
mt76_connac_mcu_get_cipher(int cipher)
{
	switch (cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		return MCU_CIPHER_WEP40;
	case WLAN_CIPHER_SUITE_WEP104:
		return MCU_CIPHER_WEP104;
	case WLAN_CIPHER_SUITE_TKIP:
		return MCU_CIPHER_TKIP;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		return MCU_CIPHER_BIP_CMAC_128;
	case WLAN_CIPHER_SUITE_CCMP:
		return MCU_CIPHER_AES_CCMP;
	case WLAN_CIPHER_SUITE_CCMP_256:
		return MCU_CIPHER_CCMP_256;
	case WLAN_CIPHER_SUITE_GCMP:
		return MCU_CIPHER_GCMP;
	case WLAN_CIPHER_SUITE_GCMP_256:
		return MCU_CIPHER_GCMP_256;
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
		return MCU_CIPHER_BIP_GMAC_128;
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		return MCU_CIPHER_BIP_GMAC_256;
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		return MCU_CIPHER_BIP_CMAC_256;
	case WLAN_CIPHER_SUITE_SMS4:
		return MCU_CIPHER_WAPI;
	default:
		return MCU_CIPHER_NONE;
	}
}

static inline u32
mt76_connac_mcu_gen_dl_mode(struct mt76_dev *dev, u8 feature_set, bool is_wa)
{
	u32 ret = 0;

	ret |= feature_set & FW_FEATURE_SET_ENCRYPT ?
	       DL_MODE_ENCRYPT | DL_MODE_RESET_SEC_IV : 0;
	if (is_mt7921(dev) || is_mt7925(dev))
		ret |= feature_set & FW_FEATURE_ENCRY_MODE ?
		       DL_CONFIG_ENCRY_MODE_SEL : 0;
	ret |= FIELD_PREP(DL_MODE_KEY_IDX,
			  FIELD_GET(FW_FEATURE_SET_KEY_IDX, feature_set));
	ret |= DL_MODE_NEED_RSP;
	ret |= is_wa ? DL_MODE_WORKING_PDA_CR4 : 0;

	return ret;
}

#define to_wcid_lo(id)		FIELD_GET(GENMASK(7, 0), (u16)id)
#define to_wcid_hi(id)		FIELD_GET(GENMASK(10, 8), (u16)id)

static inline void
mt76_connac_mcu_get_wlan_idx(struct mt76_dev *dev, struct mt76_wcid *wcid,
			     u8 *wlan_idx_lo, u8 *wlan_idx_hi)
{
	*wlan_idx_hi = 0;

	if (!is_connac_v1(dev)) {
		*wlan_idx_lo = wcid ? to_wcid_lo(wcid->idx) : 0;
		*wlan_idx_hi = wcid ? to_wcid_hi(wcid->idx) : 0;
	} else {
		*wlan_idx_lo = wcid ? wcid->idx : 0;
	}
}

struct sk_buff *
__mt76_connac_mcu_alloc_sta_req(struct mt76_dev *dev, struct mt76_vif_link *mvif,
				struct mt76_wcid *wcid, int len);
static inline struct sk_buff *
mt76_connac_mcu_alloc_sta_req(struct mt76_dev *dev, struct mt76_vif_link *mvif,
			      struct mt76_wcid *wcid)
{
	return __mt76_connac_mcu_alloc_sta_req(dev, mvif, wcid,
					       MT76_CONNAC_STA_UPDATE_MAX_SIZE);
}

struct wtbl_req_hdr *
mt76_connac_mcu_alloc_wtbl_req(struct mt76_dev *dev, struct mt76_wcid *wcid,
			       int cmd, void *sta_wtbl, struct sk_buff **skb);
struct tlv *mt76_connac_mcu_add_nested_tlv(struct sk_buff *skb, int tag,
					   int len, void *sta_ntlv,
					   void *sta_wtbl);
static inline struct tlv *
mt76_connac_mcu_add_tlv(struct sk_buff *skb, int tag, int len)
{
	return mt76_connac_mcu_add_nested_tlv(skb, tag, len, skb->data, NULL);
}

int mt76_connac_mcu_set_channel_domain(struct mt76_phy *phy);
int mt76_connac_mcu_set_vif_ps(struct mt76_dev *dev, struct ieee80211_vif *vif);
void mt76_connac_mcu_sta_basic_tlv(struct mt76_dev *dev, struct sk_buff *skb,
				   struct ieee80211_bss_conf *link_conf,
				   struct ieee80211_link_sta *link_sta,
				   int state, bool newly);
void mt76_connac_mcu_wtbl_generic_tlv(struct mt76_dev *dev, struct sk_buff *skb,
				      struct ieee80211_vif *vif,
				      struct ieee80211_sta *sta, void *sta_wtbl,
				      void *wtbl_tlv);
void mt76_connac_mcu_wtbl_hdr_trans_tlv(struct sk_buff *skb,
					struct ieee80211_vif *vif,
					struct mt76_wcid *wcid,
					void *sta_wtbl, void *wtbl_tlv);
int mt76_connac_mcu_sta_update_hdr_trans(struct mt76_dev *dev,
					 struct ieee80211_vif *vif,
					 struct mt76_wcid *wcid, int cmd);
void mt76_connac_mcu_sta_he_tlv_v2(struct sk_buff *skb, struct ieee80211_sta *sta);
u8 mt76_connac_get_phy_mode_v2(struct mt76_phy *mphy, struct ieee80211_vif *vif,
			       enum nl80211_band band,
			       struct ieee80211_link_sta *link_sta);
int mt76_connac_mcu_wtbl_update_hdr_trans(struct mt76_dev *dev,
					  struct ieee80211_vif *vif,
					  struct ieee80211_sta *sta);
void mt76_connac_mcu_sta_tlv(struct mt76_phy *mphy, struct sk_buff *skb,
			     struct ieee80211_sta *sta,
			     struct ieee80211_vif *vif,
			     u8 rcpi, u8 state);
void mt76_connac_mcu_wtbl_ht_tlv(struct mt76_dev *dev, struct sk_buff *skb,
				 struct ieee80211_sta *sta, void *sta_wtbl,
				 void *wtbl_tlv, bool ht_ldpc, bool vht_ldpc);
void mt76_connac_mcu_wtbl_ba_tlv(struct mt76_dev *dev, struct sk_buff *skb,
				 struct ieee80211_ampdu_params *params,
				 bool enable, bool tx, void *sta_wtbl,
				 void *wtbl_tlv);
void mt76_connac_mcu_sta_ba_tlv(struct sk_buff *skb,
				struct ieee80211_ampdu_params *params,
				bool enable, bool tx);
int mt76_connac_mcu_uni_add_dev(struct mt76_phy *phy,
				struct ieee80211_bss_conf *bss_conf,
				struct mt76_vif_link *mvif,
				struct mt76_wcid *wcid,
				bool enable);
int mt76_connac_mcu_sta_ba(struct mt76_dev *dev, struct mt76_vif_link *mvif,
			   struct ieee80211_ampdu_params *params,
			   int cmd, bool enable, bool tx);
int mt76_connac_mcu_uni_set_chctx(struct mt76_phy *phy,
				  struct mt76_vif_link *vif,
				  struct ieee80211_chanctx_conf *ctx);
int mt76_connac_mcu_uni_add_bss(struct mt76_phy *phy,
				struct ieee80211_vif *vif,
				struct mt76_wcid *wcid,
				bool enable,
				struct ieee80211_chanctx_conf *ctx);
int mt76_connac_mcu_sta_cmd(struct mt76_phy *phy,
			    struct mt76_sta_cmd_info *info);
void mt76_connac_mcu_beacon_loss_iter(void *priv, u8 *mac,
				      struct ieee80211_vif *vif);
int mt76_connac_mcu_set_rts_thresh(struct mt76_dev *dev, u32 val, u8 band);
int mt76_connac_mcu_set_mac_enable(struct mt76_dev *dev, int band, bool enable,
				   bool hdr_trans);
int mt76_connac_mcu_init_download(struct mt76_dev *dev, u32 addr, u32 len,
				  u32 mode);
int mt76_connac_mcu_start_patch(struct mt76_dev *dev);
int mt76_connac_mcu_patch_sem_ctrl(struct mt76_dev *dev, bool get);
int mt76_connac_mcu_start_firmware(struct mt76_dev *dev, u32 addr, u32 option);

void mt76_connac_mcu_build_rnr_scan_param(struct mt76_dev *mdev,
					  struct cfg80211_scan_request *sreq);
int mt76_connac_mcu_hw_scan(struct mt76_phy *phy, struct ieee80211_vif *vif,
			    struct ieee80211_scan_request *scan_req);
int mt76_connac_mcu_cancel_hw_scan(struct mt76_phy *phy,
				   struct ieee80211_vif *vif);
int mt76_connac_mcu_sched_scan_req(struct mt76_phy *phy,
				   struct ieee80211_vif *vif,
				   struct cfg80211_sched_scan_request *sreq);
int mt76_connac_mcu_sched_scan_enable(struct mt76_phy *phy,
				      struct ieee80211_vif *vif,
				      bool enable);
int mt76_connac_mcu_update_arp_filter(struct mt76_dev *dev,
				      struct mt76_vif_link *vif,
				      struct ieee80211_bss_conf *info);
int mt76_connac_mcu_set_gtk_rekey(struct mt76_dev *dev, struct ieee80211_vif *vif,
				  bool suspend);
int mt76_connac_mcu_set_wow_ctrl(struct mt76_phy *phy, struct ieee80211_vif *vif,
				 bool suspend, struct cfg80211_wowlan *wowlan);
int mt76_connac_mcu_update_gtk_rekey(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct cfg80211_gtk_rekey_data *key);
int mt76_connac_mcu_set_suspend_mode(struct mt76_dev *dev,
				     struct ieee80211_vif *vif,
				     bool enable, u8 mdtim,
				     bool wow_suspend);
int mt76_connac_mcu_set_hif_suspend(struct mt76_dev *dev, bool suspend, bool wait_resp);
void mt76_connac_mcu_set_suspend_iter(void *priv, u8 *mac,
				      struct ieee80211_vif *vif);
int mt76_connac_sta_state_dp(struct mt76_dev *dev,
			     enum ieee80211_sta_state old_state,
			     enum ieee80211_sta_state new_state);
int mt76_connac_mcu_chip_config(struct mt76_dev *dev);
int mt76_connac_mcu_set_deep_sleep(struct mt76_dev *dev, bool enable);
void mt76_connac_mcu_coredump_event(struct mt76_dev *dev, struct sk_buff *skb,
				    struct mt76_connac_coredump *coredump);
s8 mt76_connac_get_ch_power(struct mt76_phy *phy,
			    struct ieee80211_channel *chan,
			    s8 target_power);
int mt76_connac_mcu_set_rate_txpower(struct mt76_phy *phy);
int mt76_connac_mcu_set_p2p_oppps(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif);
u32 mt76_connac_mcu_reg_rr(struct mt76_dev *dev, u32 offset);
void mt76_connac_mcu_reg_wr(struct mt76_dev *dev, u32 offset, u32 val);

const struct ieee80211_sta_he_cap *
mt76_connac_get_he_phy_cap(struct mt76_phy *phy, struct ieee80211_vif *vif);
const struct ieee80211_sta_eht_cap *
mt76_connac_get_eht_phy_cap(struct mt76_phy *phy, struct ieee80211_vif *vif);
u8 mt76_connac_get_phy_mode(struct mt76_phy *phy, struct ieee80211_vif *vif,
			    enum nl80211_band band,
			    struct ieee80211_link_sta *sta);
u8 mt76_connac_get_phy_mode_ext(struct mt76_phy *phy, struct ieee80211_bss_conf *conf,
				enum nl80211_band band);

int mt76_connac_mcu_add_key(struct mt76_dev *dev, struct ieee80211_vif *vif,
			    struct mt76_connac_sta_key_conf *sta_key_conf,
			    struct ieee80211_key_conf *key, int mcu_cmd,
			    struct mt76_wcid *wcid, enum set_key_cmd cmd);

void mt76_connac_mcu_bss_ext_tlv(struct sk_buff *skb, struct mt76_vif_link *mvif);
void mt76_connac_mcu_bss_omac_tlv(struct sk_buff *skb,
				  struct ieee80211_vif *vif);
int mt76_connac_mcu_bss_basic_tlv(struct sk_buff *skb,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta,
				  struct mt76_phy *phy, u16 wlan_idx,
				  bool enable);
void mt76_connac_mcu_sta_uapsd(struct sk_buff *skb, struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta);
void mt76_connac_mcu_wtbl_smps_tlv(struct sk_buff *skb,
				   struct ieee80211_sta *sta,
				   void *sta_wtbl, void *wtbl_tlv);
int mt76_connac_mcu_set_pm(struct mt76_dev *dev, int band, int enter);
int mt76_connac_mcu_restart(struct mt76_dev *dev);
int mt76_connac_mcu_del_wtbl_all(struct mt76_dev *dev);
int mt76_connac_mcu_rdd_cmd(struct mt76_dev *dev, int cmd, u8 index,
			    u8 rx_sel, u8 val);
int mt76_connac_mcu_sta_wed_update(struct mt76_dev *dev, struct sk_buff *skb);
int mt76_connac2_load_ram(struct mt76_dev *dev, const char *fw_wm,
			  const char *fw_wa);
int mt76_connac2_load_patch(struct mt76_dev *dev, const char *fw_name);
int mt76_connac2_mcu_fill_message(struct mt76_dev *mdev, struct sk_buff *skb,
				  int cmd, int *wait_seq);
#endif /* __MT76_CONNAC_MCU_H */
