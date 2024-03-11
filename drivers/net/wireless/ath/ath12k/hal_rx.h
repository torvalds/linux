/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH12K_HAL_RX_H
#define ATH12K_HAL_RX_H

struct hal_rx_wbm_rel_info {
	u32 cookie;
	enum hal_wbm_rel_src_module err_rel_src;
	enum hal_reo_dest_ring_push_reason push_reason;
	u32 err_code;
	bool first_msdu;
	bool last_msdu;
	bool continuation;
	void *rx_desc;
	bool hw_cc_done;
};

#define HAL_INVALID_PEERID 0xffff
#define VHT_SIG_SU_NSS_MASK 0x7

#define HAL_RX_MAX_MCS 12
#define HAL_RX_MAX_NSS 8

#define HAL_RX_MPDU_INFO_PN_GET_BYTE1(__val) \
	le32_get_bits((__val), GENMASK(7, 0))

#define HAL_RX_MPDU_INFO_PN_GET_BYTE2(__val) \
	le32_get_bits((__val), GENMASK(15, 8))

#define HAL_RX_MPDU_INFO_PN_GET_BYTE3(__val) \
	le32_get_bits((__val), GENMASK(23, 16))

#define HAL_RX_MPDU_INFO_PN_GET_BYTE4(__val) \
	le32_get_bits((__val), GENMASK(31, 24))

struct hal_rx_mon_status_tlv_hdr {
	u32 hdr;
	u8 value[];
};

enum hal_rx_su_mu_coding {
	HAL_RX_SU_MU_CODING_BCC,
	HAL_RX_SU_MU_CODING_LDPC,
	HAL_RX_SU_MU_CODING_MAX,
};

enum hal_rx_gi {
	HAL_RX_GI_0_8_US,
	HAL_RX_GI_0_4_US,
	HAL_RX_GI_1_6_US,
	HAL_RX_GI_3_2_US,
	HAL_RX_GI_MAX,
};

enum hal_rx_bw {
	HAL_RX_BW_20MHZ,
	HAL_RX_BW_40MHZ,
	HAL_RX_BW_80MHZ,
	HAL_RX_BW_160MHZ,
	HAL_RX_BW_320MHZ,
	HAL_RX_BW_MAX,
};

enum hal_rx_preamble {
	HAL_RX_PREAMBLE_11A,
	HAL_RX_PREAMBLE_11B,
	HAL_RX_PREAMBLE_11N,
	HAL_RX_PREAMBLE_11AC,
	HAL_RX_PREAMBLE_11AX,
	HAL_RX_PREAMBLE_MAX,
};

enum hal_rx_reception_type {
	HAL_RX_RECEPTION_TYPE_SU,
	HAL_RX_RECEPTION_TYPE_MU_MIMO,
	HAL_RX_RECEPTION_TYPE_MU_OFDMA,
	HAL_RX_RECEPTION_TYPE_MU_OFDMA_MIMO,
	HAL_RX_RECEPTION_TYPE_MAX,
};

enum hal_rx_legacy_rate {
	HAL_RX_LEGACY_RATE_1_MBPS,
	HAL_RX_LEGACY_RATE_2_MBPS,
	HAL_RX_LEGACY_RATE_5_5_MBPS,
	HAL_RX_LEGACY_RATE_6_MBPS,
	HAL_RX_LEGACY_RATE_9_MBPS,
	HAL_RX_LEGACY_RATE_11_MBPS,
	HAL_RX_LEGACY_RATE_12_MBPS,
	HAL_RX_LEGACY_RATE_18_MBPS,
	HAL_RX_LEGACY_RATE_24_MBPS,
	HAL_RX_LEGACY_RATE_36_MBPS,
	HAL_RX_LEGACY_RATE_48_MBPS,
	HAL_RX_LEGACY_RATE_54_MBPS,
	HAL_RX_LEGACY_RATE_INVALID,
};

#define HAL_TLV_STATUS_PPDU_NOT_DONE            0
#define HAL_TLV_STATUS_PPDU_DONE                1
#define HAL_TLV_STATUS_BUF_DONE                 2
#define HAL_TLV_STATUS_PPDU_NON_STD_DONE        3
#define HAL_RX_FCS_LEN                          4

enum hal_rx_mon_status {
	HAL_RX_MON_STATUS_PPDU_NOT_DONE,
	HAL_RX_MON_STATUS_PPDU_DONE,
	HAL_RX_MON_STATUS_BUF_DONE,
};

#define HAL_RX_MAX_MPDU		256
#define HAL_RX_NUM_WORDS_PER_PPDU_BITMAP	(HAL_RX_MAX_MPDU >> 5)

struct hal_rx_user_status {
	u32 mcs:4,
	nss:3,
	ofdma_info_valid:1,
	ul_ofdma_ru_start_index:7,
	ul_ofdma_ru_width:7,
	ul_ofdma_ru_size:8;
	u32 ul_ofdma_user_v0_word0;
	u32 ul_ofdma_user_v0_word1;
	u32 ast_index;
	u32 tid;
	u16 tcp_msdu_count;
	u16 tcp_ack_msdu_count;
	u16 udp_msdu_count;
	u16 other_msdu_count;
	u16 frame_control;
	u8 frame_control_info_valid;
	u8 data_sequence_control_info_valid;
	u16 first_data_seq_ctrl;
	u32 preamble_type;
	u16 ht_flags;
	u16 vht_flags;
	u16 he_flags;
	u8 rs_flags;
	u8 ldpc;
	u32 mpdu_cnt_fcs_ok;
	u32 mpdu_cnt_fcs_err;
	u32 mpdu_fcs_ok_bitmap[HAL_RX_NUM_WORDS_PER_PPDU_BITMAP];
	u32 mpdu_ok_byte_count;
	u32 mpdu_err_byte_count;
};

#define HAL_MAX_UL_MU_USERS	37

struct hal_rx_mon_ppdu_info {
	u32 ppdu_id;
	u32 last_ppdu_id;
	u64 ppdu_ts;
	u32 num_mpdu_fcs_ok;
	u32 num_mpdu_fcs_err;
	u32 preamble_type;
	u32 mpdu_len;
	u16 chan_num;
	u16 tcp_msdu_count;
	u16 tcp_ack_msdu_count;
	u16 udp_msdu_count;
	u16 other_msdu_count;
	u16 peer_id;
	u8 rate;
	u8 mcs;
	u8 nss;
	u8 bw;
	u8 vht_flag_values1;
	u8 vht_flag_values2;
	u8 vht_flag_values3[4];
	u8 vht_flag_values4;
	u8 vht_flag_values5;
	u16 vht_flag_values6;
	u8 is_stbc;
	u8 gi;
	u8 sgi;
	u8 ldpc;
	u8 beamformed;
	u8 rssi_comb;
	u16 tid;
	u8 fc_valid;
	u16 ht_flags;
	u16 vht_flags;
	u16 he_flags;
	u16 he_mu_flags;
	u8 dcm;
	u8 ru_alloc;
	u8 reception_type;
	u64 tsft;
	u64 rx_duration;
	u16 frame_control;
	u32 ast_index;
	u8 rs_fcs_err;
	u8 rs_flags;
	u8 cck_flag;
	u8 ofdm_flag;
	u8 ulofdma_flag;
	u8 frame_control_info_valid;
	u16 he_per_user_1;
	u16 he_per_user_2;
	u8 he_per_user_position;
	u8 he_per_user_known;
	u16 he_flags1;
	u16 he_flags2;
	u8 he_RU[4];
	u16 he_data1;
	u16 he_data2;
	u16 he_data3;
	u16 he_data4;
	u16 he_data5;
	u16 he_data6;
	u32 ppdu_len;
	u32 prev_ppdu_id;
	u32 device_id;
	u16 first_data_seq_ctrl;
	u8 monitor_direct_used;
	u8 data_sequence_control_info_valid;
	u8 ltf_size;
	u8 rxpcu_filter_pass;
	s8 rssi_chain[8][8];
	u32 num_users;
	u32 mpdu_fcs_ok_bitmap[HAL_RX_NUM_WORDS_PER_PPDU_BITMAP];
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	u8 addr4[ETH_ALEN];
	struct hal_rx_user_status userstats[HAL_MAX_UL_MU_USERS];
	u8 userid;
	u16 ampdu_id[HAL_MAX_UL_MU_USERS];
	bool first_msdu_in_mpdu;
	bool is_ampdu;
	u8 medium_prot_type;
};

#define HAL_RX_PPDU_START_INFO0_PPDU_ID		GENMASK(15, 0)

struct hal_rx_ppdu_start {
	__le32 info0;
	__le32 chan_num;
	__le32 ppdu_start_ts;
} __packed;

#define HAL_RX_PPDU_END_USER_STATS_INFO0_MPDU_CNT_FCS_ERR	GENMASK(25, 16)

#define HAL_RX_PPDU_END_USER_STATS_INFO1_MPDU_CNT_FCS_OK	GENMASK(8, 0)
#define HAL_RX_PPDU_END_USER_STATS_INFO1_FC_VALID		BIT(9)
#define HAL_RX_PPDU_END_USER_STATS_INFO1_QOS_CTRL_VALID		BIT(10)
#define HAL_RX_PPDU_END_USER_STATS_INFO1_HT_CTRL_VALID		BIT(11)
#define HAL_RX_PPDU_END_USER_STATS_INFO1_PKT_TYPE		GENMASK(23, 20)

#define HAL_RX_PPDU_END_USER_STATS_INFO2_AST_INDEX		GENMASK(15, 0)
#define HAL_RX_PPDU_END_USER_STATS_INFO2_FRAME_CTRL		GENMASK(31, 16)

#define HAL_RX_PPDU_END_USER_STATS_INFO3_QOS_CTRL		GENMASK(31, 16)

#define HAL_RX_PPDU_END_USER_STATS_INFO4_UDP_MSDU_CNT		GENMASK(15, 0)
#define HAL_RX_PPDU_END_USER_STATS_INFO4_TCP_MSDU_CNT		GENMASK(31, 16)

#define HAL_RX_PPDU_END_USER_STATS_INFO5_OTHER_MSDU_CNT		GENMASK(15, 0)
#define HAL_RX_PPDU_END_USER_STATS_INFO5_TCP_ACK_MSDU_CNT	GENMASK(31, 16)

#define HAL_RX_PPDU_END_USER_STATS_INFO6_TID_BITMAP		GENMASK(15, 0)
#define HAL_RX_PPDU_END_USER_STATS_INFO6_TID_EOSP_BITMAP	GENMASK(31, 16)

#define HAL_RX_PPDU_END_USER_STATS_MPDU_DELIM_OK_BYTE_COUNT	GENMASK(24, 0)
#define HAL_RX_PPDU_END_USER_STATS_MPDU_DELIM_ERR_BYTE_COUNT	GENMASK(24, 0)

struct hal_rx_ppdu_end_user_stats {
	__le32 rsvd0[2];
	__le32 info0;
	__le32 info1;
	__le32 info2;
	__le32 info3;
	__le32 ht_ctrl;
	__le32 rsvd1[2];
	__le32 info4;
	__le32 info5;
	__le32 usr_resp_ref;
	__le32 info6;
	__le32 rsvd3[4];
	__le32 mpdu_ok_cnt;
	__le32 rsvd4;
	__le32 mpdu_err_cnt;
	__le32 rsvd5[2];
	__le32 usr_resp_ref_ext;
	__le32 rsvd6;
} __packed;

struct hal_rx_ppdu_end_user_stats_ext {
	__le32 info0;
	__le32 info1;
	__le32 info2;
	__le32 info3;
	__le32 info4;
	__le32 info5;
	__le32 info6;
} __packed;

#define HAL_RX_HT_SIG_INFO_INFO0_MCS		GENMASK(6, 0)
#define HAL_RX_HT_SIG_INFO_INFO0_BW		BIT(7)

#define HAL_RX_HT_SIG_INFO_INFO1_STBC		GENMASK(5, 4)
#define HAL_RX_HT_SIG_INFO_INFO1_FEC_CODING	BIT(6)
#define HAL_RX_HT_SIG_INFO_INFO1_GI		BIT(7)

struct hal_rx_ht_sig_info {
	__le32 info0;
	__le32 info1;
} __packed;

#define HAL_RX_LSIG_B_INFO_INFO0_RATE	GENMASK(3, 0)
#define HAL_RX_LSIG_B_INFO_INFO0_LEN	GENMASK(15, 4)

struct hal_rx_lsig_b_info {
	__le32 info0;
} __packed;

#define HAL_RX_LSIG_A_INFO_INFO0_RATE		GENMASK(3, 0)
#define HAL_RX_LSIG_A_INFO_INFO0_LEN		GENMASK(16, 5)
#define HAL_RX_LSIG_A_INFO_INFO0_PKT_TYPE	GENMASK(27, 24)

struct hal_rx_lsig_a_info {
	__le32 info0;
} __packed;

#define HAL_RX_VHT_SIG_A_INFO_INFO0_BW		GENMASK(1, 0)
#define HAL_RX_VHT_SIG_A_INFO_INFO0_STBC	BIT(3)
#define HAL_RX_VHT_SIG_A_INFO_INFO0_GROUP_ID	GENMASK(9, 4)
#define HAL_RX_VHT_SIG_A_INFO_INFO0_NSTS	GENMASK(21, 10)

#define HAL_RX_VHT_SIG_A_INFO_INFO1_GI_SETTING		GENMASK(1, 0)
#define HAL_RX_VHT_SIG_A_INFO_INFO1_SU_MU_CODING	BIT(2)
#define HAL_RX_VHT_SIG_A_INFO_INFO1_MCS			GENMASK(7, 4)
#define HAL_RX_VHT_SIG_A_INFO_INFO1_BEAMFORMED		BIT(8)

struct hal_rx_vht_sig_a_info {
	__le32 info0;
	__le32 info1;
} __packed;

enum hal_rx_vht_sig_a_gi_setting {
	HAL_RX_VHT_SIG_A_NORMAL_GI = 0,
	HAL_RX_VHT_SIG_A_SHORT_GI = 1,
	HAL_RX_VHT_SIG_A_SHORT_GI_AMBIGUITY = 3,
};

#define HE_GI_0_8 0
#define HE_GI_0_4 1
#define HE_GI_1_6 2
#define HE_GI_3_2 3

#define HE_LTF_1_X 0
#define HE_LTF_2_X 1
#define HE_LTF_4_X 2

#define HAL_RX_HE_SIG_A_SU_INFO_INFO0_TRANSMIT_MCS	GENMASK(6, 3)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO0_DCM		BIT(7)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO0_TRANSMIT_BW	GENMASK(20, 19)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO0_CP_LTF_SIZE	GENMASK(22, 21)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO0_NSTS		GENMASK(25, 23)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO0_BSS_COLOR		GENMASK(13, 8)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO0_SPATIAL_REUSE	GENMASK(18, 15)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO0_FORMAT_IND	BIT(0)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO0_BEAM_CHANGE	BIT(1)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO0_DL_UL_FLAG	BIT(2)

#define HAL_RX_HE_SIG_A_SU_INFO_INFO1_TXOP_DURATION	GENMASK(6, 0)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO1_CODING		BIT(7)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO1_LDPC_EXTRA	BIT(8)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO1_STBC		BIT(9)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO1_TXBF		BIT(10)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO1_PKT_EXT_FACTOR	GENMASK(12, 11)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO1_PKT_EXT_PE_DISAM	BIT(13)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO1_DOPPLER_IND	BIT(15)

struct hal_rx_he_sig_a_su_info {
	__le32 info0;
	__le32 info1;
} __packed;

#define HAL_RX_HE_SIG_A_MU_DL_INFO0_UL_FLAG		BIT(1)
#define HAL_RX_HE_SIG_A_MU_DL_INFO0_MCS_OF_SIGB		GENMASK(3, 1)
#define HAL_RX_HE_SIG_A_MU_DL_INFO0_DCM_OF_SIGB		BIT(4)
#define HAL_RX_HE_SIG_A_MU_DL_INFO0_BSS_COLOR		GENMASK(10, 5)
#define HAL_RX_HE_SIG_A_MU_DL_INFO0_SPATIAL_REUSE	GENMASK(14, 11)
#define HAL_RX_HE_SIG_A_MU_DL_INFO0_TRANSMIT_BW		GENMASK(17, 15)
#define HAL_RX_HE_SIG_A_MU_DL_INFO0_NUM_SIGB_SYMB	GENMASK(21, 18)
#define HAL_RX_HE_SIG_A_MU_DL_INFO0_COMP_MODE_SIGB	BIT(22)
#define HAL_RX_HE_SIG_A_MU_DL_INFO0_CP_LTF_SIZE		GENMASK(24, 23)
#define HAL_RX_HE_SIG_A_MU_DL_INFO0_DOPPLER_INDICATION	BIT(25)

#define HAL_RX_HE_SIG_A_MU_DL_INFO1_TXOP_DURATION	GENMASK(6, 0)
#define HAL_RX_HE_SIG_A_MU_DL_INFO1_CODING		BIT(7)
#define HAL_RX_HE_SIG_A_MU_DL_INFO1_NUM_LTF_SYMB	GENMASK(10, 8)
#define HAL_RX_HE_SIG_A_MU_DL_INFO1_LDPC_EXTRA		BIT(11)
#define HAL_RX_HE_SIG_A_MU_DL_INFO1_STBC		BIT(12)
#define HAL_RX_HE_SIG_A_MU_DL_INFO1_TXBF		BIT(10)
#define HAL_RX_HE_SIG_A_MU_DL_INFO1_PKT_EXT_FACTOR	GENMASK(14, 13)
#define HAL_RX_HE_SIG_A_MU_DL_INFO1_PKT_EXT_PE_DISAM	BIT(15)

struct hal_rx_he_sig_a_mu_dl_info {
	__le32 info0;
	__le32 info1;
} __packed;

#define HAL_RX_HE_SIG_B1_MU_INFO_INFO0_RU_ALLOCATION	GENMASK(7, 0)

struct hal_rx_he_sig_b1_mu_info {
	__le32 info0;
} __packed;

#define HAL_RX_HE_SIG_B2_MU_INFO_INFO0_STA_ID           GENMASK(10, 0)
#define HAL_RX_HE_SIG_B2_MU_INFO_INFO0_STA_MCS		GENMASK(18, 15)
#define HAL_RX_HE_SIG_B2_MU_INFO_INFO0_STA_CODING	BIT(20)
#define HAL_RX_HE_SIG_B2_MU_INFO_INFO0_STA_NSTS		GENMASK(31, 29)

struct hal_rx_he_sig_b2_mu_info {
	__le32 info0;
} __packed;

#define HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_ID	GENMASK(10, 0)
#define HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_NSTS	GENMASK(13, 11)
#define HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_TXBF	BIT(19)
#define HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_MCS	GENMASK(18, 15)
#define HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_DCM	BIT(19)
#define HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_CODING	BIT(20)

struct hal_rx_he_sig_b2_ofdma_info {
	__le32 info0;
} __packed;

enum hal_rx_ul_reception_type {
	HAL_RECEPTION_TYPE_ULOFMDA,
	HAL_RECEPTION_TYPE_ULMIMO,
	HAL_RECEPTION_TYPE_OTHER,
	HAL_RECEPTION_TYPE_FRAMELESS
};

#define HAL_RX_PHYRX_RSSI_LEGACY_INFO_INFO0_RSSI_COMB	GENMASK(15, 8)
#define HAL_RX_PHYRX_RSSI_LEGACY_INFO_RSVD1_RECEPTION   GENMASK(3, 0)

struct hal_rx_phyrx_rssi_legacy_info {
	__le32 rsvd[35];
	__le32 info0;
} __packed;

#define HAL_RX_MPDU_START_INFO0_PPDU_ID	GENMASK(31, 16)
#define HAL_RX_MPDU_START_INFO1_PEERID	GENMASK(31, 16)
#define HAL_RX_MPDU_START_INFO2_MPDU_LEN GENMASK(13, 0)
struct hal_rx_mpdu_start {
	__le32 info0;
	__le32 info1;
	__le32 rsvd1[11];
	__le32 info2;
	__le32 rsvd2[9];
} __packed;

#define HAL_RX_PPDU_END_DURATION	GENMASK(23, 0)
struct hal_rx_ppdu_end_duration {
	__le32 rsvd0[9];
	__le32 info0;
	__le32 rsvd1[4];
} __packed;

struct hal_rx_rxpcu_classification_overview {
	u32 rsvd0;
} __packed;

struct hal_rx_msdu_desc_info {
	u32 msdu_flags;
	u16 msdu_len; /* 14 bits for length */
};

#define HAL_RX_NUM_MSDU_DESC 6
struct hal_rx_msdu_list {
	struct hal_rx_msdu_desc_info msdu_info[HAL_RX_NUM_MSDU_DESC];
	u32 sw_cookie[HAL_RX_NUM_MSDU_DESC];
	u8 rbm[HAL_RX_NUM_MSDU_DESC];
};

#define HAL_RX_FBM_ACK_INFO0_ADDR1_31_0		GENMASK(31, 0)
#define HAL_RX_FBM_ACK_INFO1_ADDR1_47_32	GENMASK(15, 0)
#define HAL_RX_FBM_ACK_INFO1_ADDR2_15_0		GENMASK(31, 16)
#define HAL_RX_FBM_ACK_INFO2_ADDR2_47_16	GENMASK(31, 0)

struct hal_rx_frame_bitmap_ack {
	__le32 reserved;
	__le32 info0;
	__le32 info1;
	__le32 info2;
	__le32 reserved1[10];
} __packed;

#define HAL_RX_RESP_REQ_INFO0_PPDU_ID		GENMASK(15, 0)
#define HAL_RX_RESP_REQ_INFO0_RECEPTION_TYPE	BIT(16)
#define HAL_RX_RESP_REQ_INFO1_DURATION		GENMASK(15, 0)
#define HAL_RX_RESP_REQ_INFO1_RATE_MCS		GENMASK(24, 21)
#define HAL_RX_RESP_REQ_INFO1_SGI		GENMASK(26, 25)
#define HAL_RX_RESP_REQ_INFO1_STBC		BIT(27)
#define HAL_RX_RESP_REQ_INFO1_LDPC		BIT(28)
#define HAL_RX_RESP_REQ_INFO1_IS_AMPDU		BIT(29)
#define HAL_RX_RESP_REQ_INFO2_NUM_USER		GENMASK(6, 0)
#define HAL_RX_RESP_REQ_INFO3_ADDR1_31_0	GENMASK(31, 0)
#define HAL_RX_RESP_REQ_INFO4_ADDR1_47_32	GENMASK(15, 0)
#define HAL_RX_RESP_REQ_INFO4_ADDR1_15_0	GENMASK(31, 16)
#define HAL_RX_RESP_REQ_INFO5_ADDR1_47_16	GENMASK(31, 0)

struct hal_rx_resp_req_info {
	__le32 info0;
	__le32 reserved[1];
	__le32 info1;
	__le32 info2;
	__le32 reserved1[2];
	__le32 info3;
	__le32 info4;
	__le32 info5;
	__le32 reserved2[5];
} __packed;

#define REO_QUEUE_DESC_MAGIC_DEBUG_PATTERN_0 0xDDBEEF
#define REO_QUEUE_DESC_MAGIC_DEBUG_PATTERN_1 0xADBEEF
#define REO_QUEUE_DESC_MAGIC_DEBUG_PATTERN_2 0xBDBEEF
#define REO_QUEUE_DESC_MAGIC_DEBUG_PATTERN_3 0xCDBEEF

#define HAL_RX_UL_OFDMA_USER_INFO_V0_W0_VALID		BIT(30)
#define HAL_RX_UL_OFDMA_USER_INFO_V0_W0_VER		BIT(31)
#define HAL_RX_UL_OFDMA_USER_INFO_V0_W1_NSS		GENMASK(2, 0)
#define HAL_RX_UL_OFDMA_USER_INFO_V0_W1_MCS		GENMASK(6, 3)
#define HAL_RX_UL_OFDMA_USER_INFO_V0_W1_LDPC		BIT(7)
#define HAL_RX_UL_OFDMA_USER_INFO_V0_W1_DCM		BIT(8)
#define HAL_RX_UL_OFDMA_USER_INFO_V0_W1_RU_START	GENMASK(15, 9)
#define HAL_RX_UL_OFDMA_USER_INFO_V0_W1_RU_SIZE		GENMASK(18, 16)

/* HE Radiotap data1 Mask */
#define HE_SU_FORMAT_TYPE 0x0000
#define HE_EXT_SU_FORMAT_TYPE 0x0001
#define HE_MU_FORMAT_TYPE  0x0002
#define HE_TRIG_FORMAT_TYPE  0x0003
#define HE_BEAM_CHANGE_KNOWN 0x0008
#define HE_DL_UL_KNOWN 0x0010
#define HE_MCS_KNOWN 0x0020
#define HE_DCM_KNOWN 0x0040
#define HE_CODING_KNOWN 0x0080
#define HE_LDPC_EXTRA_SYMBOL_KNOWN 0x0100
#define HE_STBC_KNOWN 0x0200
#define HE_DATA_BW_RU_KNOWN 0x4000
#define HE_DOPPLER_KNOWN 0x8000
#define HE_BSS_COLOR_KNOWN 0x0004

/* HE Radiotap data2 Mask */
#define HE_GI_KNOWN 0x0002
#define HE_TXBF_KNOWN 0x0010
#define HE_PE_DISAMBIGUITY_KNOWN 0x0020
#define HE_TXOP_KNOWN 0x0040
#define HE_LTF_SYMBOLS_KNOWN 0x0004
#define HE_PRE_FEC_PADDING_KNOWN 0x0008
#define HE_MIDABLE_PERIODICITY_KNOWN 0x0080

/* HE radiotap data3 shift values */
#define HE_BEAM_CHANGE_SHIFT 6
#define HE_DL_UL_SHIFT 7
#define HE_TRANSMIT_MCS_SHIFT 8
#define HE_DCM_SHIFT 12
#define HE_CODING_SHIFT 13
#define HE_LDPC_EXTRA_SYMBOL_SHIFT 14
#define HE_STBC_SHIFT 15

/* HE radiotap data4 shift values */
#define HE_STA_ID_SHIFT 4

/* HE radiotap data5 */
#define HE_GI_SHIFT 4
#define HE_LTF_SIZE_SHIFT 6
#define HE_LTF_SYM_SHIFT 8
#define HE_TXBF_SHIFT 14
#define HE_PE_DISAMBIGUITY_SHIFT 15
#define HE_PRE_FEC_PAD_SHIFT 12

/* HE radiotap data6 */
#define HE_DOPPLER_SHIFT 4
#define HE_TXOP_SHIFT 8

/* HE radiotap HE-MU flags1 */
#define HE_SIG_B_MCS_KNOWN 0x0010
#define HE_SIG_B_DCM_KNOWN 0x0040
#define HE_SIG_B_SYM_NUM_KNOWN 0x8000
#define HE_RU_0_KNOWN 0x0100
#define HE_RU_1_KNOWN 0x0200
#define HE_RU_2_KNOWN 0x0400
#define HE_RU_3_KNOWN 0x0800
#define HE_DCM_FLAG_1_SHIFT 5
#define HE_SPATIAL_REUSE_MU_KNOWN 0x0100
#define HE_SIG_B_COMPRESSION_FLAG_1_KNOWN 0x4000

/* HE radiotap HE-MU flags2 */
#define HE_SIG_B_COMPRESSION_FLAG_2_SHIFT 3
#define HE_BW_KNOWN 0x0004
#define HE_NUM_SIG_B_SYMBOLS_SHIFT 4
#define HE_SIG_B_COMPRESSION_FLAG_2_KNOWN 0x0100
#define HE_NUM_SIG_B_FLAG_2_SHIFT 9
#define HE_LTF_FLAG_2_SYMBOLS_SHIFT 12
#define HE_LTF_KNOWN 0x8000

/* HE radiotap per_user_1 */
#define HE_STA_SPATIAL_SHIFT 11
#define HE_TXBF_SHIFT 14
#define HE_RESERVED_SET_TO_1_SHIFT 19
#define HE_STA_CODING_SHIFT 20

/* HE radiotap per_user_2 */
#define HE_STA_MCS_SHIFT 4
#define HE_STA_DCM_SHIFT 5

/* HE radiotap per user known */
#define HE_USER_FIELD_POSITION_KNOWN 0x01
#define HE_STA_ID_PER_USER_KNOWN 0x02
#define HE_STA_NSTS_KNOWN 0x04
#define HE_STA_TX_BF_KNOWN 0x08
#define HE_STA_SPATIAL_CONFIG_KNOWN 0x10
#define HE_STA_MCS_KNOWN 0x20
#define HE_STA_DCM_KNOWN 0x40
#define HE_STA_CODING_KNOWN 0x80

#define HAL_RX_MPDU_ERR_FCS			BIT(0)
#define HAL_RX_MPDU_ERR_DECRYPT			BIT(1)
#define HAL_RX_MPDU_ERR_TKIP_MIC		BIT(2)
#define HAL_RX_MPDU_ERR_AMSDU_ERR		BIT(3)
#define HAL_RX_MPDU_ERR_OVERFLOW		BIT(4)
#define HAL_RX_MPDU_ERR_MSDU_LEN		BIT(5)
#define HAL_RX_MPDU_ERR_MPDU_LEN		BIT(6)
#define HAL_RX_MPDU_ERR_UNENCRYPTED_FRAME	BIT(7)

static inline
enum nl80211_he_ru_alloc ath12k_he_ru_tones_to_nl80211_he_ru_alloc(u16 ru_tones)
{
	enum nl80211_he_ru_alloc ret;

	switch (ru_tones) {
	case RU_52:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_52;
		break;
	case RU_106:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_106;
		break;
	case RU_242:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_242;
		break;
	case RU_484:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_484;
		break;
	case RU_996:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_996;
		break;
	case RU_26:
		fallthrough;
	default:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_26;
		break;
	}
	return ret;
}

void ath12k_hal_reo_status_queue_stats(struct ath12k_base *ab,
				       struct hal_tlv_64_hdr *tlv,
				       struct hal_reo_status *status);
void ath12k_hal_reo_flush_queue_status(struct ath12k_base *ab,
				       struct hal_tlv_64_hdr *tlv,
				       struct hal_reo_status *status);
void ath12k_hal_reo_flush_cache_status(struct ath12k_base *ab,
				       struct hal_tlv_64_hdr *tlv,
				       struct hal_reo_status *status);
void ath12k_hal_reo_unblk_cache_status(struct ath12k_base *ab,
				       struct hal_tlv_64_hdr *tlv,
				       struct hal_reo_status *status);
void ath12k_hal_reo_flush_timeout_list_status(struct ath12k_base *ab,
					      struct hal_tlv_64_hdr *tlv,
					      struct hal_reo_status *status);
void ath12k_hal_reo_desc_thresh_reached_status(struct ath12k_base *ab,
					       struct hal_tlv_64_hdr *tlv,
					       struct hal_reo_status *status);
void ath12k_hal_reo_update_rx_reo_queue_status(struct ath12k_base *ab,
					       struct hal_tlv_64_hdr *tlv,
					       struct hal_reo_status *status);
void ath12k_hal_rx_msdu_link_info_get(struct hal_rx_msdu_link *link, u32 *num_msdus,
				      u32 *msdu_cookies,
				      enum hal_rx_buf_return_buf_manager *rbm);
void ath12k_hal_rx_msdu_link_desc_set(struct ath12k_base *ab,
				      struct hal_wbm_release_ring *dst_desc,
				      struct hal_wbm_release_ring *src_desc,
				      enum hal_wbm_rel_bm_act action);
void ath12k_hal_rx_buf_addr_info_set(struct ath12k_buffer_addr *binfo,
				     dma_addr_t paddr, u32 cookie, u8 manager);
void ath12k_hal_rx_buf_addr_info_get(struct ath12k_buffer_addr *binfo,
				     dma_addr_t *paddr,
				     u32 *cookie, u8 *rbm);
int ath12k_hal_desc_reo_parse_err(struct ath12k_base *ab,
				  struct hal_reo_dest_ring *desc,
				  dma_addr_t *paddr, u32 *desc_bank);
int ath12k_hal_wbm_desc_parse_err(struct ath12k_base *ab, void *desc,
				  struct hal_rx_wbm_rel_info *rel_info);
void ath12k_hal_rx_reo_ent_paddr_get(struct ath12k_base *ab,
				     struct ath12k_buffer_addr *buff_addr,
				     dma_addr_t *paddr, u32 *cookie);

#endif
