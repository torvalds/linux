/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2019-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_DP_MON_H
#define ATH12K_DP_MON_H

#include "core.h"
#include "wifi7/hal_desc.h"
#include "wifi7/hal_rx.h"

#define ATH12K_MON_RX_DOT11_OFFSET	5
#define ATH12K_MON_RX_PKT_OFFSET	8

#define ATH12K_LE32_DEC_ENC(value, dec_bits, enc_bits)	\
		u32_encode_bits(le32_get_bits(value, dec_bits), enc_bits)

#define ATH12K_LE64_DEC_ENC(value, dec_bits, enc_bits) \
		u32_encode_bits(le64_get_bits(value, dec_bits), enc_bits)

enum dp_monitor_mode {
	ATH12K_DP_TX_MONITOR_MODE,
	ATH12K_DP_RX_MONITOR_MODE
};

enum dp_mon_tx_ppdu_info_type {
	DP_MON_TX_PROT_PPDU_INFO,
	DP_MON_TX_DATA_PPDU_INFO
};

enum dp_mon_tx_tlv_status {
	DP_MON_TX_FES_SETUP,
	DP_MON_TX_FES_STATUS_END,
	DP_MON_RX_RESPONSE_REQUIRED_INFO,
	DP_MON_RESPONSE_END_STATUS_INFO,
	DP_MON_TX_MPDU_START,
	DP_MON_TX_MSDU_START,
	DP_MON_TX_BUFFER_ADDR,
	DP_MON_TX_DATA,
	DP_MON_TX_STATUS_PPDU_NOT_DONE,
};

enum dp_mon_tx_medium_protection_type {
	DP_MON_TX_MEDIUM_NO_PROTECTION,
	DP_MON_TX_MEDIUM_RTS_LEGACY,
	DP_MON_TX_MEDIUM_RTS_11AC_STATIC_BW,
	DP_MON_TX_MEDIUM_RTS_11AC_DYNAMIC_BW,
	DP_MON_TX_MEDIUM_CTS2SELF,
	DP_MON_TX_MEDIUM_QOS_NULL_NO_ACK_3ADDR,
	DP_MON_TX_MEDIUM_QOS_NULL_NO_ACK_4ADDR
};

struct dp_mon_qosframe_addr4 {
	__le16 frame_control;
	__le16 duration;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctrl;
	u8 addr4[ETH_ALEN];
	__le16 qos_ctrl;
} __packed;

struct dp_mon_frame_min_one {
	__le16 frame_control;
	__le16 duration;
	u8 addr1[ETH_ALEN];
} __packed;

struct dp_mon_packet_info {
	u64 cookie;
	u16 dma_length;
	bool msdu_continuation;
	bool truncated;
};

struct dp_mon_tx_ppdu_info {
	u32 ppdu_id;
	u8  num_users;
	bool is_used;
	struct hal_rx_mon_ppdu_info rx_status;
	struct list_head dp_tx_mon_mpdu_list;
	struct dp_mon_mpdu *tx_mon_mpdu;
};

int ath12k_dp_mon_buf_replenish(struct ath12k_base *ab,
				struct dp_rxdma_mon_ring *buf_ring,
				int req_entries);
int ath12k_dp_mon_status_bufs_replenish(struct ath12k_base *ab,
					struct dp_rxdma_mon_ring *rx_ring,
					int req_entries);
void ath12k_dp_mon_rx_process_ulofdma(struct hal_rx_mon_ppdu_info *ppdu_info);
void
ath12k_dp_mon_rx_update_peer_mu_stats(struct ath12k_base *ab,
				      struct hal_rx_mon_ppdu_info *ppdu_info);
void ath12k_dp_mon_rx_update_peer_su_stats(struct ath12k_dp_link_peer *peer,
					   struct hal_rx_mon_ppdu_info *ppdu_info);
int ath12k_dp_pkt_set_pktlen(struct sk_buff *skb, u32 len);
int ath12k_dp_mon_rx_deliver(struct ath12k_pdev_dp *dp_pdev,
			     struct dp_mon_mpdu *mon_mpdu,
			     struct hal_rx_mon_ppdu_info *ppduinfo,
			     struct napi_struct *napi);
struct sk_buff
*ath12k_dp_rx_alloc_mon_status_buf(struct ath12k_base *ab,
				   struct dp_rxdma_mon_ring *rx_ring,
				   int *buf_id);
void
ath12k_dp_mon_get_buf_len(struct hal_rx_msdu_desc_info *info,
			  bool *is_frag, u32 *total_len,
			  u32 *frag_len, u32 *msdu_cnt);
void ath12k_dp_mon_next_link_desc_get(struct ath12k_base *ab,
				      struct hal_rx_msdu_link *msdu_link,
				      dma_addr_t *paddr, u32 *sw_cookie, u8 *rbm,
				      struct ath12k_buffer_addr **pp_buf_addr_info);
u32 ath12k_dp_mon_comp_ppduid(u32 msdu_ppdu_id, u32 *ppdu_id);
int
ath12k_dp_mon_parse_status_buf(struct ath12k_pdev_dp *dp_pdev,
			       struct ath12k_mon_data *pmon,
			       const struct dp_mon_packet_info *packet_info);
void
ath12k_dp_mon_rx_handle_ofdma_info(const struct hal_rx_ppdu_end_user_stats *ppdu_end_user,
				   struct hal_rx_user_status *rx_user_status);
void
ath12k_dp_mon_rx_populate_mu_user_info(const struct hal_rx_ppdu_end_user_stats *rx_tlv,
				       struct hal_rx_mon_ppdu_info *ppdu_info,
				       struct hal_rx_user_status *rx_user_status);
void ath12k_dp_mon_parse_l_sig_b(const struct hal_rx_lsig_b_info *lsigb,
				 struct hal_rx_mon_ppdu_info *ppdu_info);
void ath12k_dp_mon_parse_l_sig_a(const struct hal_rx_lsig_a_info *lsiga,
				 struct hal_rx_mon_ppdu_info *ppdu_info);
void
ath12k_dp_mon_hal_rx_parse_user_info(const struct hal_receive_user_info *rx_usr_info,
				     u16 user_id,
				     struct hal_rx_mon_ppdu_info *ppdu_info);
void
ath12k_dp_mon_parse_status_msdu_end(struct ath12k_mon_data *pmon,
				    const struct hal_rx_msdu_end *msdu_end);
void
ath12k_dp_mon_hal_rx_parse_u_sig_hdr(const struct hal_mon_usig_hdr *usig,
				     struct hal_rx_mon_ppdu_info *ppdu_info);
#endif
