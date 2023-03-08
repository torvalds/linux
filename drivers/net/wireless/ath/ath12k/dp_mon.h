/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2019-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH12K_DP_MON_H
#define ATH12K_DP_MON_H

#include "core.h"

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

enum hal_rx_mon_status
ath12k_dp_mon_rx_parse_mon_status(struct ath12k *ar,
				  struct ath12k_mon_data *pmon,
				  int mac_id, struct sk_buff *skb,
				  struct napi_struct *napi);
int ath12k_dp_mon_buf_replenish(struct ath12k_base *ab,
				struct dp_rxdma_ring *buf_ring,
				int req_entries);
int ath12k_dp_mon_srng_process(struct ath12k *ar, int mac_id,
			       int *budget, enum dp_monitor_mode monitor_mode,
			       struct napi_struct *napi);
int ath12k_dp_mon_process_ring(struct ath12k_base *ab, int mac_id,
			       struct napi_struct *napi, int budget,
			       enum dp_monitor_mode monitor_mode);
struct sk_buff *ath12k_dp_mon_tx_alloc_skb(void);
enum dp_mon_tx_tlv_status
ath12k_dp_mon_tx_status_get_num_user(u16 tlv_tag,
				     struct hal_tlv_hdr *tx_tlv,
				     u8 *num_users);
enum hal_rx_mon_status
ath12k_dp_mon_tx_parse_mon_status(struct ath12k *ar,
				  struct ath12k_mon_data *pmon,
				  int mac_id,
				  struct sk_buff *skb,
				  struct napi_struct *napi,
				  u32 ppdu_id);
void ath12k_dp_mon_rx_process_ulofdma(struct hal_rx_mon_ppdu_info *ppdu_info);
int ath12k_dp_mon_rx_process_stats(struct ath12k *ar, int mac_id,
				   struct napi_struct *napi, int *budget);
#endif
