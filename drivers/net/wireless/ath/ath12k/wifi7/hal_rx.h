/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_HAL_RX_H
#define ATH12K_HAL_RX_H

#include "hal_desc.h"

struct hal_reo_status;

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
	__le32 peer_metadata;
};

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

#define HAL_TLV_STATUS_PPDU_NOT_DONE            0
#define HAL_TLV_STATUS_PPDU_DONE                1
#define HAL_TLV_STATUS_BUF_DONE                 2
#define HAL_TLV_STATUS_PPDU_NON_STD_DONE        3

enum hal_rx_mon_status {
	HAL_RX_MON_STATUS_PPDU_NOT_DONE,
	HAL_RX_MON_STATUS_PPDU_DONE,
	HAL_RX_MON_STATUS_BUF_DONE,
	HAL_RX_MON_STATUS_BUF_ADDR,
	HAL_RX_MON_STATUS_MPDU_START,
	HAL_RX_MON_STATUS_MPDU_END,
	HAL_RX_MON_STATUS_MSDU_END,
};

#define HAL_RX_PPDU_START_INFO0_PPDU_ID			GENMASK(15, 0)
#define HAL_RX_PPDU_START_INFO1_CHAN_NUM		GENMASK(15, 0)
#define HAL_RX_PPDU_START_INFO1_CHAN_FREQ		GENMASK(31, 16)

struct hal_rx_ppdu_start {
	__le32 info0;
	__le32 info1;
	__le32 ppdu_start_ts_31_0;
	__le32 ppdu_start_ts_63_32;
	__le32 rsvd[2];
} __packed;

#define HAL_RX_PPDU_END_USER_STATS_INFO0_PEER_ID		GENMASK(13, 0)
#define HAL_RX_PPDU_END_USER_STATS_INFO0_DEVICE_ID		GENMASK(15, 14)
#define HAL_RX_PPDU_END_USER_STATS_INFO0_MPDU_CNT_FCS_ERR	GENMASK(26, 16)

#define HAL_RX_PPDU_END_USER_STATS_INFO1_MPDU_CNT_FCS_OK	GENMASK(10, 0)
#define HAL_RX_PPDU_END_USER_STATS_INFO1_FC_VALID		BIT(11)
#define HAL_RX_PPDU_END_USER_STATS_INFO1_QOS_CTRL_VALID		BIT(12)
#define HAL_RX_PPDU_END_USER_STATS_INFO1_HT_CTRL_VALID		BIT(13)
#define HAL_RX_PPDU_END_USER_STATS_INFO1_PKT_TYPE              GENMASK(24, 21)

#define HAL_RX_PPDU_END_USER_STATS_INFO2_AST_INDEX		GENMASK(15, 0)
#define HAL_RX_PPDU_END_USER_STATS_INFO2_FRAME_CTRL		GENMASK(31, 16)

#define HAL_RX_PPDU_END_USER_STATS_INFO3_QOS_CTRL		GENMASK(31, 16)

#define HAL_RX_PPDU_END_USER_STATS_INFO4_UDP_MSDU_CNT		GENMASK(15, 0)
#define HAL_RX_PPDU_END_USER_STATS_INFO4_TCP_MSDU_CNT		GENMASK(31, 16)

#define HAL_RX_PPDU_END_USER_STATS_INFO5_OTHER_MSDU_CNT		GENMASK(15, 0)
#define HAL_RX_PPDU_END_USER_STATS_INFO5_TCP_ACK_MSDU_CNT	GENMASK(31, 16)

#define HAL_RX_PPDU_END_USER_STATS_INFO6_TID_BITMAP		GENMASK(15, 0)
#define HAL_RX_PPDU_END_USER_STATS_INFO6_TID_EOSP_BITMAP	GENMASK(31, 16)

#define HAL_RX_PPDU_END_USER_STATS_INFO7_MPDU_OK_BYTE_COUNT    GENMASK(24, 0)
#define HAL_RX_PPDU_END_USER_STATS_INFO8_MPDU_ERR_BYTE_COUNT   GENMASK(24, 0)

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
	__le32 info7;
	__le32 rsvd4;
	__le32 info8;
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
	__le32 rsvd;
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
#define HAL_RX_HE_SIG_A_MU_DL_INFO1_NUM_LTF_SYMB	GENMASK(10, 8)
#define HAL_RX_HE_SIG_A_MU_DL_INFO1_LDPC_EXTRA		BIT(11)
#define HAL_RX_HE_SIG_A_MU_DL_INFO1_STBC		BIT(12)
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
#define HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_TXBF	BIT(14)
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

#define HAL_RX_RSSI_LEGACY_INFO_INFO0_RECEPTION		GENMASK(3, 0)
#define HAL_RX_RSSI_LEGACY_INFO_INFO0_RX_BW		GENMASK(7, 5)
#define HAL_RX_RSSI_LEGACY_INFO_INFO1_RSSI_COMB		GENMASK(15, 8)
#define HAL_RX_RSSI_LEGACY_INFO_INFO2_RSSI_COMB_PPDU	GENMASK(7, 0)

struct hal_rx_phyrx_rssi_legacy_info {
	__le32 info0;
	__le32 rsvd0[39];
	__le32 info1;
	__le32 info2;
} __packed;

#define HAL_RX_MPDU_START_INFO0_PPDU_ID			GENMASK(31, 16)
#define HAL_RX_MPDU_START_INFO1_PEERID			GENMASK(29, 16)
#define HAL_RX_MPDU_START_INFO1_DEVICE_ID		GENMASK(31, 30)
#define HAL_RX_MPDU_START_INFO2_MPDU_LEN		GENMASK(13, 0)
struct hal_rx_mpdu_start {
	__le32 rsvd0[9];
	__le32 info0;
	__le32 info1;
	__le32 rsvd1[2];
	__le32 info2;
	__le32 rsvd2[16];
} __packed;

struct hal_rx_msdu_end {
	__le32 info0;
	__le32 rsvd0[9];
	__le16 info00;
	__le16 info01;
	__le32 rsvd00[8];
	__le32 info1;
	__le32 rsvd1[10];
	__le32 info2;
	__le32 rsvd2;
} __packed;

#define HAL_RX_PPDU_END_DURATION	GENMASK(23, 0)
struct hal_rx_ppdu_end_duration {
	__le32 rsvd0[9];
	__le32 info0;
	__le32 rsvd1[18];
} __packed;

struct hal_rx_rxpcu_classification_overview {
	u32 rsvd0;
} __packed;

#define HAL_RX_NUM_MSDU_DESC 6
struct hal_rx_msdu_list {
	struct hal_rx_msdu_desc_info msdu_info[HAL_RX_NUM_MSDU_DESC];
	u64 paddr[HAL_RX_NUM_MSDU_DESC];
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

#define HAL_RX_CMN_USR_INFO0_CP_SETTING			GENMASK(17, 16)
#define HAL_RX_CMN_USR_INFO0_LTF_SIZE			GENMASK(19, 18)

struct hal_phyrx_common_user_info {
	__le32 rsvd[2];
	__le32 info0;
	__le32 rsvd1;
} __packed;

#define HAL_RX_EHT_SIG_NDP_CMN_INFO0_SPATIAL_REUSE	GENMASK(3, 0)
#define HAL_RX_EHT_SIG_NDP_CMN_INFO0_GI_LTF		GENMASK(5, 4)
#define HAL_RX_EHT_SIG_NDP_CMN_INFO0_NUM_LTF_SYM	GENMASK(8, 6)
#define HAL_RX_EHT_SIG_NDP_CMN_INFO0_NSS		GENMASK(10, 7)
#define HAL_RX_EHT_SIG_NDP_CMN_INFO0_BEAMFORMED		BIT(11)
#define HAL_RX_EHT_SIG_NDP_CMN_INFO0_DISREGARD		GENMASK(13, 12)
#define HAL_RX_EHT_SIG_NDP_CMN_INFO0_CRC		GENMASK(17, 14)

struct hal_eht_sig_ndp_cmn_eb {
	__le32 info0;
} __packed;

#define HAL_RX_EHT_SIG_OVERFLOW_INFO0_SPATIAL_REUSE		GENMASK(3, 0)
#define HAL_RX_EHT_SIG_OVERFLOW_INFO0_GI_LTF			GENMASK(5, 4)
#define HAL_RX_EHT_SIG_OVERFLOW_INFO0_NUM_LTF_SYM		GENMASK(8, 6)
#define HAL_RX_EHT_SIG_OVERFLOW_INFO0_LDPC_EXTA_SYM		BIT(9)
#define HAL_RX_EHT_SIG_OVERFLOW_INFO0_PRE_FEC_PAD_FACTOR	GENMASK(11, 10)
#define HAL_RX_EHT_SIG_OVERFLOW_INFO0_DISAMBIGUITY		BIT(12)
#define HAL_RX_EHT_SIG_OVERFLOW_INFO0_DISREGARD			GENMASK(16, 13)

struct hal_eht_sig_usig_overflow {
	__le32 info0;
} __packed;

#define HAL_RX_EHT_SIG_NON_MUMIMO_USER_INFO0_STA_ID	GENMASK(10, 0)
#define HAL_RX_EHT_SIG_NON_MUMIMO_USER_INFO0_MCS	GENMASK(14, 11)
#define HAL_RX_EHT_SIG_NON_MUMIMO_USER_INFO0_VALIDATE	BIT(15)
#define HAL_RX_EHT_SIG_NON_MUMIMO_USER_INFO0_NSS	GENMASK(19, 16)
#define HAL_RX_EHT_SIG_NON_MUMIMO_USER_INFO0_BEAMFORMED	BIT(20)
#define HAL_RX_EHT_SIG_NON_MUMIMO_USER_INFO0_CODING	BIT(21)
#define HAL_RX_EHT_SIG_NON_MUMIMO_USER_INFO0_CRC	GENMASK(25, 22)

struct hal_eht_sig_non_mu_mimo {
	__le32 info0;
} __packed;

#define HAL_RX_EHT_SIG_MUMIMO_USER_INFO0_STA_ID		GENMASK(10, 0)
#define HAL_RX_EHT_SIG_MUMIMO_USER_INFO0_MCS		GENMASK(14, 11)
#define HAL_RX_EHT_SIG_MUMIMO_USER_INFO0_CODING		BIT(15)
#define HAL_RX_EHT_SIG_MUMIMO_USER_INFO0_SPATIAL_CODING	GENMASK(22, 16)
#define HAL_RX_EHT_SIG_MUMIMO_USER_INFO0_CRC		GENMASK(26, 23)

struct hal_eht_sig_mu_mimo {
	__le32 info0;
} __packed;

union hal_eht_sig_user_field {
	struct hal_eht_sig_mu_mimo mu_mimo;
	struct hal_eht_sig_non_mu_mimo n_mu_mimo;
};

#define HAL_RX_EHT_SIG_NON_OFDMA_INFO0_SPATIAL_REUSE		GENMASK(3, 0)
#define HAL_RX_EHT_SIG_NON_OFDMA_INFO0_GI_LTF			GENMASK(5, 4)
#define HAL_RX_EHT_SIG_NON_OFDMA_INFO0_NUM_LTF_SYM		GENMASK(8, 6)
#define HAL_RX_EHT_SIG_NON_OFDMA_INFO0_LDPC_EXTA_SYM		BIT(9)
#define HAL_RX_EHT_SIG_NON_OFDMA_INFO0_PRE_FEC_PAD_FACTOR	GENMASK(11, 10)
#define HAL_RX_EHT_SIG_NON_OFDMA_INFO0_DISAMBIGUITY		BIT(12)
#define HAL_RX_EHT_SIG_NON_OFDMA_INFO0_DISREGARD		GENMASK(16, 13)
#define HAL_RX_EHT_SIG_NON_OFDMA_INFO0_NUM_USERS		GENMASK(19, 17)

struct hal_eht_sig_non_ofdma_cmn_eb {
	__le32 info0;
	union hal_eht_sig_user_field user_field;
} __packed;

#define HAL_RX_EHT_SIG_OFDMA_EB1_SPATIAL_REUSE		GENMASK_ULL(3, 0)
#define HAL_RX_EHT_SIG_OFDMA_EB1_GI_LTF			GENMASK_ULL(5, 4)
#define HAL_RX_EHT_SIG_OFDMA_EB1_NUM_LFT_SYM		GENMASK_ULL(8, 6)
#define HAL_RX_EHT_SIG_OFDMA_EB1_LDPC_EXTRA_SYM		BIT(9)
#define HAL_RX_EHT_SIG_OFDMA_EB1_PRE_FEC_PAD_FACTOR	GENMASK_ULL(11, 10)
#define HAL_RX_EHT_SIG_OFDMA_EB1_PRE_DISAMBIGUITY	BIT(12)
#define HAL_RX_EHT_SIG_OFDMA_EB1_DISREGARD		GENMASK_ULL(16, 13)
#define HAL_RX_EHT_SIG_OFDMA_EB1_RU_ALLOC_1_1		GENMASK_ULL(25, 17)
#define HAL_RX_EHT_SIG_OFDMA_EB1_RU_ALLOC_1_2		GENMASK_ULL(34, 26)
#define HAL_RX_EHT_SIG_OFDMA_EB1_CRC			GENMASK_ULL(30, 27)

struct hal_eht_sig_ofdma_cmn_eb1 {
	__le64 info0;
} __packed;

#define HAL_RX_EHT_SIG_OFDMA_EB2_RU_ALLOC_2_1		GENMASK_ULL(8, 0)
#define HAL_RX_EHT_SIG_OFDMA_EB2_RU_ALLOC_2_2		GENMASK_ULL(17, 9)
#define HAL_RX_EHT_SIG_OFDMA_EB2_RU_ALLOC_2_3		GENMASK_ULL(26, 18)
#define HAL_RX_EHT_SIG_OFDMA_EB2_RU_ALLOC_2_4		GENMASK_ULL(35, 27)
#define HAL_RX_EHT_SIG_OFDMA_EB2_RU_ALLOC_2_5		GENMASK_ULL(44, 36)
#define HAL_RX_EHT_SIG_OFDMA_EB2_RU_ALLOC_2_6		GENMASK_ULL(53, 45)
#define HAL_RX_EHT_SIG_OFDMA_EB2_MCS			GNEMASK_ULL(57, 54)

struct hal_eht_sig_ofdma_cmn_eb2 {
	__le64 info0;
} __packed;

struct hal_eht_sig_ofdma_cmn_eb {
	struct hal_eht_sig_ofdma_cmn_eb1 eb1;
	struct hal_eht_sig_ofdma_cmn_eb2 eb2;
	union hal_eht_sig_user_field user_field;
} __packed;

enum hal_eht_bw {
	HAL_EHT_BW_20,
	HAL_EHT_BW_40,
	HAL_EHT_BW_80,
	HAL_EHT_BW_160,
	HAL_EHT_BW_320_1,
	HAL_EHT_BW_320_2,
};

#define HAL_RX_USIG_CMN_INFO0_PHY_VERSION	GENMASK(2, 0)
#define HAL_RX_USIG_CMN_INFO0_BW		GENMASK(5, 3)
#define HAL_RX_USIG_CMN_INFO0_UL_DL		BIT(6)
#define HAL_RX_USIG_CMN_INFO0_BSS_COLOR		GENMASK(12, 7)
#define HAL_RX_USIG_CMN_INFO0_TXOP		GENMASK(19, 13)
#define HAL_RX_USIG_CMN_INFO0_DISREGARD		GENMASK(25, 20)
#define HAL_RX_USIG_CMN_INFO0_VALIDATE		BIT(26)

struct hal_mon_usig_cmn {
	__le32 info0;
} __packed;

#define HAL_RX_USIG_TB_INFO0_PPDU_TYPE_COMP_MODE	GENMASK(1, 0)
#define HAL_RX_USIG_TB_INFO0_VALIDATE			BIT(2)
#define HAL_RX_USIG_TB_INFO0_SPATIAL_REUSE_1		GENMASK(6, 3)
#define HAL_RX_USIG_TB_INFO0_SPATIAL_REUSE_2		GENMASK(10, 7)
#define HAL_RX_USIG_TB_INFO0_DISREGARD_1		GENMASK(15, 11)
#define HAL_RX_USIG_TB_INFO0_CRC			GENMASK(19, 16)
#define HAL_RX_USIG_TB_INFO0_TAIL			GENMASK(25, 20)
#define HAL_RX_USIG_TB_INFO0_RX_INTEG_CHECK_PASS	BIT(31)

struct hal_mon_usig_tb {
	__le32 info0;
} __packed;

#define HAL_RX_USIG_MU_INFO0_PPDU_TYPE_COMP_MODE	GENMASK(1, 0)
#define HAL_RX_USIG_MU_INFO0_VALIDATE_1			BIT(2)
#define HAL_RX_USIG_MU_INFO0_PUNC_CH_INFO		GENMASK(7, 3)
#define HAL_RX_USIG_MU_INFO0_VALIDATE_2			BIT(8)
#define HAL_RX_USIG_MU_INFO0_EHT_SIG_MCS		GENMASK(10, 9)
#define HAL_RX_USIG_MU_INFO0_NUM_EHT_SIG_SYM		GENMASK(15, 11)
#define HAL_RX_USIG_MU_INFO0_CRC			GENMASK(20, 16)
#define HAL_RX_USIG_MU_INFO0_TAIL			GENMASK(26, 21)
#define HAL_RX_USIG_MU_INFO0_RX_INTEG_CHECK_PASS	BIT(31)

struct hal_mon_usig_mu {
	__le32 info0;
} __packed;

union hal_mon_usig_non_cmn {
	struct hal_mon_usig_tb tb;
	struct hal_mon_usig_mu mu;
};

struct hal_mon_usig_hdr {
	struct hal_mon_usig_cmn cmn;
	union hal_mon_usig_non_cmn non_cmn;
} __packed;

#define HAL_RX_USR_INFO0_PHY_PPDU_ID		GENMASK(15, 0)
#define HAL_RX_USR_INFO0_USR_RSSI		GENMASK(23, 16)
#define HAL_RX_USR_INFO0_PKT_TYPE		GENMASK(27, 24)
#define HAL_RX_USR_INFO0_STBC			BIT(28)
#define HAL_RX_USR_INFO0_RECEPTION_TYPE		GENMASK(31, 29)

#define HAL_RX_USR_INFO1_MCS			GENMASK(3, 0)
#define HAL_RX_USR_INFO1_SGI			GENMASK(5, 4)
#define HAL_RX_USR_INFO1_HE_RANGING_NDP		BIT(6)
#define HAL_RX_USR_INFO1_MIMO_SS_BITMAP		GENMASK(15, 8)
#define HAL_RX_USR_INFO1_RX_BW			GENMASK(18, 16)
#define HAL_RX_USR_INFO1_DL_OFMDA_USR_IDX	GENMASK(31, 24)

#define HAL_RX_USR_INFO2_DL_OFDMA_CONTENT_CHAN	BIT(0)
#define HAL_RX_USR_INFO2_NSS			GENMASK(10, 8)
#define HAL_RX_USR_INFO2_STREAM_OFFSET		GENMASK(13, 11)
#define HAL_RX_USR_INFO2_STA_DCM		BIT(14)
#define HAL_RX_USR_INFO2_LDPC			BIT(15)
#define HAL_RX_USR_INFO2_RU_TYPE_80_0		GENMASK(19, 16)
#define HAL_RX_USR_INFO2_RU_TYPE_80_1		GENMASK(23, 20)
#define HAL_RX_USR_INFO2_RU_TYPE_80_2		GENMASK(27, 24)
#define HAL_RX_USR_INFO2_RU_TYPE_80_3		GENMASK(31, 28)

#define HAL_RX_USR_INFO3_RU_START_IDX_80_0	GENMASK(5, 0)
#define HAL_RX_USR_INFO3_RU_START_IDX_80_1	GENMASK(13, 8)
#define HAL_RX_USR_INFO3_RU_START_IDX_80_2	GENMASK(21, 16)
#define HAL_RX_USR_INFO3_RU_START_IDX_80_3	GENMASK(29, 24)

struct hal_receive_user_info {
	__le32 info0;
	__le32 info1;
	__le32 info2;
	__le32 info3;
	__le32 user_fd_rssi_seg0;
	__le32 user_fd_rssi_seg1;
	__le32 user_fd_rssi_seg2;
	__le32 user_fd_rssi_seg3;
} __packed;

enum hal_mon_reception_type {
	HAL_RECEPTION_TYPE_SU,
	HAL_RECEPTION_TYPE_DL_MU_MIMO,
	HAL_RECEPTION_TYPE_DL_MU_OFMA,
	HAL_RECEPTION_TYPE_DL_MU_OFDMA_MIMO,
	HAL_RECEPTION_TYPE_UL_MU_MIMO,
	HAL_RECEPTION_TYPE_UL_MU_OFDMA,
	HAL_RECEPTION_TYPE_UL_MU_OFDMA_MIMO,
};

/* Different allowed RU in 11BE */
#define HAL_EHT_RU_26		0ULL
#define HAL_EHT_RU_52		1ULL
#define HAL_EHT_RU_78		2ULL
#define HAL_EHT_RU_106		3ULL
#define HAL_EHT_RU_132		4ULL
#define HAL_EHT_RU_242		5ULL
#define HAL_EHT_RU_484		6ULL
#define HAL_EHT_RU_726		7ULL
#define HAL_EHT_RU_996		8ULL
#define HAL_EHT_RU_996x2	9ULL
#define HAL_EHT_RU_996x3	10ULL
#define HAL_EHT_RU_996x4	11ULL
#define HAL_EHT_RU_NONE		15ULL
#define HAL_EHT_RU_INVALID	31ULL
/* MRUs spanning above 80Mhz
 * HAL_EHT_RU_996_484 = HAL_EHT_RU_484 + HAL_EHT_RU_996 + 4 (reserved)
 */
#define HAL_EHT_RU_996_484	18ULL
#define HAL_EHT_RU_996x2_484	28ULL
#define HAL_EHT_RU_996x3_484	40ULL
#define HAL_EHT_RU_996_484_242	23ULL

#define NUM_RU_BITS_PER80	16
#define NUM_RU_BITS_PER20	4

/* Different per_80Mhz band in 320Mhz bandwidth */
#define HAL_80_0	0
#define HAL_80_1	1
#define HAL_80_2	2
#define HAL_80_3	3

#define HAL_RU_80MHZ(num_band)		((num_band) * NUM_RU_BITS_PER80)
#define HAL_RU_20MHZ(idx_per_80)	((idx_per_80) * NUM_RU_BITS_PER20)

#define HAL_RU_SHIFT(num_band, idx_per_80)	\
		(HAL_RU_80MHZ(num_band) + HAL_RU_20MHZ(idx_per_80))

#define HAL_RU(ru, num_band, idx_per_80)	\
		((u64)(ru) << HAL_RU_SHIFT(num_band, idx_per_80))

/* MRU-996+484 */
#define HAL_EHT_RU_996_484_0	(HAL_RU(HAL_EHT_RU_484, HAL_80_0, 1) |	\
				 HAL_RU(HAL_EHT_RU_996, HAL_80_1, 0))
#define HAL_EHT_RU_996_484_1	(HAL_RU(HAL_EHT_RU_484, HAL_80_0, 0) |	\
				 HAL_RU(HAL_EHT_RU_996, HAL_80_1, 0))
#define HAL_EHT_RU_996_484_2	(HAL_RU(HAL_EHT_RU_996, HAL_80_0, 0) |	\
				 HAL_RU(HAL_EHT_RU_484, HAL_80_1, 1))
#define HAL_EHT_RU_996_484_3	(HAL_RU(HAL_EHT_RU_996, HAL_80_0, 0) |	\
				 HAL_RU(HAL_EHT_RU_484, HAL_80_1, 0))
#define HAL_EHT_RU_996_484_4	(HAL_RU(HAL_EHT_RU_484, HAL_80_2, 1) |	\
				 HAL_RU(HAL_EHT_RU_996, HAL_80_3, 0))
#define HAL_EHT_RU_996_484_5	(HAL_RU(HAL_EHT_RU_484, HAL_80_2, 0) |	\
				 HAL_RU(HAL_EHT_RU_996, HAL_80_3, 0))
#define HAL_EHT_RU_996_484_6	(HAL_RU(HAL_EHT_RU_996, HAL_80_2, 0) |	\
				 HAL_RU(HAL_EHT_RU_484, HAL_80_3, 1))
#define HAL_EHT_RU_996_484_7	(HAL_RU(HAL_EHT_RU_996, HAL_80_2, 0) |	\
				 HAL_RU(HAL_EHT_RU_484, HAL_80_3, 0))

/* MRU-996x2+484 */
#define HAL_EHT_RU_996x2_484_0	(HAL_RU(HAL_EHT_RU_484, HAL_80_0, 1) |	\
				 HAL_RU(HAL_EHT_RU_996x2, HAL_80_1, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x2, HAL_80_2, 0))
#define HAL_EHT_RU_996x2_484_1	(HAL_RU(HAL_EHT_RU_484, HAL_80_0, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x2, HAL_80_1, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x2, HAL_80_2, 0))
#define HAL_EHT_RU_996x2_484_2	(HAL_RU(HAL_EHT_RU_996x2, HAL_80_0, 0) |	\
				 HAL_RU(HAL_EHT_RU_484, HAL_80_1, 1) |	\
				 HAL_RU(HAL_EHT_RU_996x2, HAL_80_2, 0))
#define HAL_EHT_RU_996x2_484_3	(HAL_RU(HAL_EHT_RU_996x2, HAL_80_0, 0) |	\
				 HAL_RU(HAL_EHT_RU_484, HAL_80_1, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x2, HAL_80_2, 0))
#define HAL_EHT_RU_996x2_484_4	(HAL_RU(HAL_EHT_RU_996x2, HAL_80_0, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x2, HAL_80_1, 0) |	\
				 HAL_RU(HAL_EHT_RU_484, HAL_80_2, 1))
#define HAL_EHT_RU_996x2_484_5	(HAL_RU(HAL_EHT_RU_996x2, HAL_80_0, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x2, HAL_80_1, 0) |	\
				 HAL_RU(HAL_EHT_RU_484, HAL_80_2, 0))
#define HAL_EHT_RU_996x2_484_6	(HAL_RU(HAL_EHT_RU_484, HAL_80_1, 1) |	\
				 HAL_RU(HAL_EHT_RU_996x2, HAL_80_2, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x2, HAL_80_3, 0))
#define HAL_EHT_RU_996x2_484_7	(HAL_RU(HAL_EHT_RU_484, HAL_80_1, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x2, HAL_80_2, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x2, HAL_80_3, 0))
#define HAL_EHT_RU_996x2_484_8	(HAL_RU(HAL_EHT_RU_996x2, HAL_80_1, 0) |	\
				 HAL_RU(HAL_EHT_RU_484, HAL_80_2, 1) |	\
				 HAL_RU(HAL_EHT_RU_996x2, HAL_80_3, 0))
#define HAL_EHT_RU_996x2_484_9	(HAL_RU(HAL_EHT_RU_996x2, HAL_80_1, 0) |	\
				 HAL_RU(HAL_EHT_RU_484, HAL_80_2, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x2, HAL_80_3, 0))
#define HAL_EHT_RU_996x2_484_10	(HAL_RU(HAL_EHT_RU_996x2, HAL_80_1, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x2, HAL_80_2, 0) |	\
				 HAL_RU(HAL_EHT_RU_484, HAL_80_3, 1))
#define HAL_EHT_RU_996x2_484_11	(HAL_RU(HAL_EHT_RU_996x2, HAL_80_1, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x2, HAL_80_2, 0) |	\
				 HAL_RU(HAL_EHT_RU_484, HAL_80_3, 0))

/* MRU-996x3+484 */
#define HAL_EHT_RU_996x3_484_0	(HAL_RU(HAL_EHT_RU_484, HAL_80_0, 1) |	\
				 HAL_RU(HAL_EHT_RU_996x3, HAL_80_1, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x3, HAL_80_2, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x3, HAL_80_3, 0))
#define HAL_EHT_RU_996x3_484_1	(HAL_RU(HAL_EHT_RU_484, HAL_80_0, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x3, HAL_80_1, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x3, HAL_80_2, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x3, HAL_80_3, 0))
#define HAL_EHT_RU_996x3_484_2	(HAL_RU(HAL_EHT_RU_996x3, HAL_80_0, 0) |	\
				 HAL_RU(HAL_EHT_RU_484, HAL_80_1, 1) |	\
				 HAL_RU(HAL_EHT_RU_996x3, HAL_80_2, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x3, HAL_80_3, 0))
#define HAL_EHT_RU_996x3_484_3	(HAL_RU(HAL_EHT_RU_996x3, HAL_80_0, 0) |	\
				 HAL_RU(HAL_EHT_RU_484, HAL_80_1, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x3, HAL_80_2, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x3, HAL_80_3, 0))
#define HAL_EHT_RU_996x3_484_4	(HAL_RU(HAL_EHT_RU_996x3, HAL_80_0, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x3, HAL_80_1, 0) |	\
				 HAL_RU(HAL_EHT_RU_484, HAL_80_2, 1) |	\
				 HAL_RU(HAL_EHT_RU_996x3, HAL_80_3, 0))
#define HAL_EHT_RU_996x3_484_5	(HAL_RU(HAL_EHT_RU_996x3, HAL_80_0, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x3, HAL_80_1, 0) |	\
				 HAL_RU(HAL_EHT_RU_484, HAL_80_2, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x3, HAL_80_3, 0))
#define HAL_EHT_RU_996x3_484_6	(HAL_RU(HAL_EHT_RU_996x3, HAL_80_0, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x3, HAL_80_1, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x3, HAL_80_2, 0) |	\
				 HAL_RU(HAL_EHT_RU_484, HAL_80_3, 1))
#define HAL_EHT_RU_996x3_484_7	(HAL_RU(HAL_EHT_RU_996x3, HAL_80_0, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x3, HAL_80_1, 0) |	\
				 HAL_RU(HAL_EHT_RU_996x3, HAL_80_2, 0) |	\
				 HAL_RU(HAL_EHT_RU_484, HAL_80_3, 0))

#define HAL_RU_PER80(ru_per80, num_80mhz, ru_idx_per80mhz) \
			(HAL_RU(ru_per80, num_80mhz, ru_idx_per80mhz))

void ath12k_wifi7_hal_reo_status_queue_stats(struct ath12k_base *ab,
					     struct hal_reo_get_queue_stats_status *desc,
					     struct hal_reo_status *status);
void ath12k_wifi7_hal_reo_flush_queue_status(struct ath12k_base *ab,
					     struct hal_reo_flush_queue_status *desc,
					     struct hal_reo_status *status);
void ath12k_wifi7_hal_reo_flush_cache_status(struct ath12k_base *ab,
					     struct hal_reo_flush_cache_status *desc,
					     struct hal_reo_status *status);
void ath12k_wifi7_hal_reo_unblk_cache_status(struct ath12k_base *ab,
					     struct hal_reo_unblock_cache_status *desc,
					     struct hal_reo_status *status);
void
ath12k_wifi7_hal_reo_flush_timeout_list_status(struct ath12k_base *ab,
					       struct hal_reo_flush_timeout_list_status *desc,
					       struct hal_reo_status *status);
void
ath12k_wifi7_hal_reo_desc_thresh_reached_status(struct ath12k_base *ab,
						struct hal_reo_desc_thresh_reached_status *desc,
						struct hal_reo_status *status);
void ath12k_wifi7_hal_reo_update_rx_reo_queue_status(struct ath12k_base *ab,
						     struct hal_reo_status_hdr *desc,
						     struct hal_reo_status *status);
void ath12k_wifi7_hal_rx_msdu_link_info_get(struct hal_rx_msdu_link *link, u32 *num_msdus,
					    u32 *msdu_cookies,
					    enum hal_rx_buf_return_buf_manager *rbm);
void ath12k_wifi7_hal_rx_msdu_link_desc_set(struct ath12k_base *ab,
					    struct hal_wbm_release_ring *desc,
					    struct ath12k_buffer_addr *buf_addr_info,
					    enum hal_wbm_rel_bm_act action);
void ath12k_wifi7_hal_rx_buf_addr_info_set(struct ath12k_buffer_addr *binfo,
					   dma_addr_t paddr, u32 cookie, u8 manager);
void ath12k_wifi7_hal_rx_buf_addr_info_get(struct ath12k_buffer_addr *binfo,
					   dma_addr_t *paddr,
					   u32 *cookie, u8 *rbm);
int ath12k_wifi7_hal_desc_reo_parse_err(struct ath12k_dp *dp,
					struct hal_reo_dest_ring *desc,
					dma_addr_t *paddr, u32 *desc_bank);
int ath12k_wifi7_hal_wbm_desc_parse_err(struct ath12k_dp *dp, void *desc,
					struct hal_rx_wbm_rel_info *rel_info);
void ath12k_wifi7_hal_rx_reo_ent_paddr_get(struct ath12k_buffer_addr *buff_addr,
					   dma_addr_t *paddr, u32 *cookie);
void ath12k_wifi7_hal_rx_reo_ent_buf_paddr_get(void *rx_desc, dma_addr_t *paddr,
					       u32 *sw_cookie,
					       struct ath12k_buffer_addr **pp_buf_addr,
					       u8 *rbm, u32 *msdu_cnt);
void ath12k_wifi7_hal_rx_msdu_list_get(struct ath12k *ar,
				       void *link_desc,
				       void *msdu_list_opaque,
				       u16 *num_msdus);
void ath12k_wifi7_hal_reo_init_cmd_ring_tlv64(struct ath12k_base *ab,
					      struct hal_srng *srng);
void ath12k_wifi7_hal_reo_init_cmd_ring_tlv32(struct ath12k_base *ab,
					      struct hal_srng *srng);
void ath12k_wifi7_hal_reo_shared_qaddr_cache_clear(struct ath12k_base *ab);
void ath12k_wifi7_hal_reo_hw_setup(struct ath12k_base *ab, u32 ring_hash_map);
void ath12k_wifi7_hal_reo_qdesc_setup(struct hal_rx_reo_queue *qdesc,
				      int tid, u32 ba_window_size,
				      u32 start_seq, enum hal_pn_type type);

#endif
