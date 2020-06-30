/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#ifndef ATH11K_HAL_RX_H
#define ATH11K_HAL_RX_H

struct hal_rx_wbm_rel_info {
	u32 cookie;
	enum hal_wbm_rel_src_module err_rel_src;
	enum hal_reo_dest_ring_push_reason push_reason;
	u32 err_code;
	bool first_msdu;
	bool last_msdu;
};

#define HAL_INVALID_PEERID 0xffff
#define VHT_SIG_SU_NSS_MASK 0x7

#define HAL_RX_MAX_MCS 12
#define HAL_RX_MAX_NSS 8

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

struct hal_rx_mon_ppdu_info {
	u32 ppdu_id;
	u32 ppdu_ts;
	u32 num_mpdu_fcs_ok;
	u32 num_mpdu_fcs_err;
	u32 preamble_type;
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
	u8 is_stbc;
	u8 gi;
	u8 ldpc;
	u8 beamformed;
	u8 rssi_comb;
	u8 tid;
	u8 dcm;
	u8 ru_alloc;
	u8 reception_type;
	u64 rx_duration;
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
	__le32 info6;
	__le32 rsvd2[11];
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

#define HAL_RX_HE_SIG_A_SU_INFO_INFO0_TRANSMIT_MCS	GENMASK(6, 3)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO0_DCM		BIT(7)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO0_TRANSMIT_BW	GENMASK(20, 19)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO0_CP_LTF_SIZE	GENMASK(22, 21)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO0_NSTS		GENMASK(25, 23)

#define HAL_RX_HE_SIG_A_SU_INFO_INFO1_CODING		BIT(7)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO1_STBC		BIT(9)
#define HAL_RX_HE_SIG_A_SU_INFO_INFO1_TXBF		BIT(10)

struct hal_rx_he_sig_a_su_info {
	__le32 info0;
	__le32 info1;
} __packed;

#define HAL_RX_HE_SIG_A_MU_DL_INFO_INFO0_TRANSMIT_BW	GENMASK(17, 15)
#define HAL_RX_HE_SIG_A_MU_DL_INFO_INFO0_CP_LTF_SIZE	GENMASK(24, 23)

#define HAL_RX_HE_SIG_A_MU_DL_INFO_INFO1_STBC		BIT(12)

struct hal_rx_he_sig_a_mu_dl_info {
	__le32 info0;
	__le32 info1;
} __packed;

#define HAL_RX_HE_SIG_B1_MU_INFO_INFO0_RU_ALLOCATION	GENMASK(7, 0)

struct hal_rx_he_sig_b1_mu_info {
	__le32 info0;
} __packed;

#define HAL_RX_HE_SIG_B2_MU_INFO_INFO0_STA_MCS		GENMASK(18, 15)
#define HAL_RX_HE_SIG_B2_MU_INFO_INFO0_STA_CODING	BIT(20)
#define HAL_RX_HE_SIG_B2_MU_INFO_INFO0_STA_NSTS		GENMASK(31, 29)

struct hal_rx_he_sig_b2_mu_info {
	__le32 info0;
} __packed;

#define HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_NSTS	GENMASK(13, 11)
#define HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_TXBF	BIT(19)
#define HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_MCS	GENMASK(18, 15)
#define HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_DCM	BIT(19)
#define HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_CODING	BIT(20)

struct hal_rx_he_sig_b2_ofdma_info {
	__le32 info0;
} __packed;

#define HAL_RX_PHYRX_RSSI_LEGACY_INFO_INFO1_RSSI_COMB	GENMASK(15, 8)

struct hal_rx_phyrx_rssi_legacy_info {
	__le32 rsvd[35];
	__le32 info0;
} __packed;

#define HAL_RX_MPDU_INFO_INFO0_PEERID	GENMASK(31, 16)
struct hal_rx_mpdu_info {
	__le32 rsvd0;
	__le32 info0;
	__le32 rsvd1[21];
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

void ath11k_hal_reo_status_queue_stats(struct ath11k_base *ab, u32 *reo_desc,
				       struct hal_reo_status *status);
void ath11k_hal_reo_flush_queue_status(struct ath11k_base *ab, u32 *reo_desc,
				       struct hal_reo_status *status);
void ath11k_hal_reo_flush_cache_status(struct ath11k_base *ab, u32 *reo_desc,
				       struct hal_reo_status *status);
void ath11k_hal_reo_flush_cache_status(struct ath11k_base *ab, u32 *reo_desc,
				       struct hal_reo_status *status);
void ath11k_hal_reo_unblk_cache_status(struct ath11k_base *ab, u32 *reo_desc,
				       struct hal_reo_status *status);
void ath11k_hal_reo_flush_timeout_list_status(struct ath11k_base *ab,
					      u32 *reo_desc,
					      struct hal_reo_status *status);
void ath11k_hal_reo_desc_thresh_reached_status(struct ath11k_base *ab,
					       u32 *reo_desc,
					       struct hal_reo_status *status);
void ath11k_hal_reo_update_rx_reo_queue_status(struct ath11k_base *ab,
					       u32 *reo_desc,
					       struct hal_reo_status *status);
int ath11k_hal_reo_process_status(u8 *reo_desc, u8 *status);
void ath11k_hal_rx_msdu_link_info_get(void *link_desc, u32 *num_msdus,
				      u32 *msdu_cookies,
				      enum hal_rx_buf_return_buf_manager *rbm);
void ath11k_hal_rx_msdu_link_desc_set(struct ath11k_base *ab, void *desc,
				      void *link_desc,
				      enum hal_wbm_rel_bm_act action);
void ath11k_hal_rx_buf_addr_info_set(void *desc, dma_addr_t paddr,
				     u32 cookie, u8 manager);
void ath11k_hal_rx_buf_addr_info_get(void *desc, dma_addr_t *paddr,
				     u32 *cookie, u8 *rbm);
int ath11k_hal_desc_reo_parse_err(struct ath11k_base *ab, u32 *rx_desc,
				  dma_addr_t *paddr, u32 *desc_bank);
int ath11k_hal_wbm_desc_parse_err(struct ath11k_base *ab, void *desc,
				  struct hal_rx_wbm_rel_info *rel_info);
void ath11k_hal_rx_reo_ent_paddr_get(struct ath11k_base *ab, void *desc,
				     dma_addr_t *paddr, u32 *desc_bank);
void ath11k_hal_rx_reo_ent_buf_paddr_get(void *rx_desc,
					 dma_addr_t *paddr, u32 *sw_cookie,
					 void **pp_buf_addr_info,
					 u32 *msdu_cnt);
enum hal_rx_mon_status
ath11k_hal_rx_parse_mon_status(struct ath11k_base *ab,
			       struct hal_rx_mon_ppdu_info *ppdu_info,
			       struct sk_buff *skb);

static inline u32 ath11k_he_ru_tones_to_nl80211_he_ru_alloc(u16 ru_tones)
{
	u32 ret = 0;

	switch (ru_tones) {
	case RU_26:
		ret = NL80211_RATE_INFO_HE_RU_ALLOC_26;
		break;
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
	}
	return ret;
}

#define REO_QUEUE_DESC_MAGIC_DEBUG_PATTERN_0 0xDDBEEF
#define REO_QUEUE_DESC_MAGIC_DEBUG_PATTERN_1 0xADBEEF
#define REO_QUEUE_DESC_MAGIC_DEBUG_PATTERN_2 0xBDBEEF
#define REO_QUEUE_DESC_MAGIC_DEBUG_PATTERN_3 0xCDBEEF
#endif
