/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef ATH12K_DP_RX_H
#define ATH12K_DP_RX_H

#include "core.h"
#include "rx_desc.h"
#include "debug.h"

#define DP_MAX_NWIFI_HDR_LEN	30

struct ath12k_dp_rx_tid {
	u8 tid;
	u32 *vaddr;
	dma_addr_t paddr;
	u32 size;
	u32 ba_win_sz;
	bool active;

	/* Info related to rx fragments */
	u32 cur_sn;
	u16 last_frag_no;
	u16 rx_frag_bitmap;

	struct sk_buff_head rx_frags;
	struct hal_reo_dest_ring *dst_ring_desc;

	/* Timer info related to fragments */
	struct timer_list frag_timer;
	struct ath12k_base *ab;
};

struct ath12k_dp_rx_reo_cache_flush_elem {
	struct list_head list;
	struct ath12k_dp_rx_tid data;
	unsigned long ts;
};

struct ath12k_dp_rx_reo_cmd {
	struct list_head list;
	struct ath12k_dp_rx_tid data;
	int cmd_num;
	void (*handler)(struct ath12k_dp *dp, void *ctx,
			enum hal_reo_cmd_status status);
};

#define ATH12K_DP_RX_REO_DESC_FREE_THRES  64
#define ATH12K_DP_RX_REO_DESC_FREE_TIMEOUT_MS 1000

enum ath12k_dp_rx_decap_type {
	DP_RX_DECAP_TYPE_RAW,
	DP_RX_DECAP_TYPE_NATIVE_WIFI,
	DP_RX_DECAP_TYPE_ETHERNET2_DIX,
	DP_RX_DECAP_TYPE_8023,
};

struct ath12k_dp_rx_rfc1042_hdr {
	u8 llc_dsap;
	u8 llc_ssap;
	u8 llc_ctrl;
	u8 snap_oui[3];
	__be16 snap_type;
} __packed;

static inline u32 ath12k_he_gi_to_nl80211_he_gi(u8 sgi)
{
	u32 ret = 0;

	switch (sgi) {
	case RX_MSDU_START_SGI_0_8_US:
		ret = NL80211_RATE_INFO_HE_GI_0_8;
		break;
	case RX_MSDU_START_SGI_1_6_US:
		ret = NL80211_RATE_INFO_HE_GI_1_6;
		break;
	case RX_MSDU_START_SGI_3_2_US:
		ret = NL80211_RATE_INFO_HE_GI_3_2;
		break;
	}

	return ret;
}

int ath12k_dp_rx_ampdu_start(struct ath12k *ar,
			     struct ieee80211_ampdu_params *params,
			     u8 link_id);
int ath12k_dp_rx_ampdu_stop(struct ath12k *ar,
			    struct ieee80211_ampdu_params *params,
			    u8 link_id);
int ath12k_dp_rx_peer_pn_replay_config(struct ath12k_link_vif *arvif,
				       const u8 *peer_addr,
				       enum set_key_cmd key_cmd,
				       struct ieee80211_key_conf *key);
void ath12k_dp_rx_peer_tid_cleanup(struct ath12k *ar, struct ath12k_peer *peer);
void ath12k_dp_rx_peer_tid_delete(struct ath12k *ar,
				  struct ath12k_peer *peer, u8 tid);
int ath12k_dp_rx_peer_tid_setup(struct ath12k *ar, const u8 *peer_mac, int vdev_id,
				u8 tid, u32 ba_win_sz, u16 ssn,
				enum hal_pn_type pn_type);
void ath12k_dp_htt_htc_t2h_msg_handler(struct ath12k_base *ab,
				       struct sk_buff *skb);
int ath12k_dp_rx_pdev_reo_setup(struct ath12k_base *ab);
void ath12k_dp_rx_pdev_reo_cleanup(struct ath12k_base *ab);
int ath12k_dp_rx_htt_setup(struct ath12k_base *ab);
int ath12k_dp_rx_alloc(struct ath12k_base *ab);
void ath12k_dp_rx_free(struct ath12k_base *ab);
int ath12k_dp_rx_pdev_alloc(struct ath12k_base *ab, int pdev_idx);
void ath12k_dp_rx_pdev_free(struct ath12k_base *ab, int pdev_idx);
void ath12k_dp_rx_reo_cmd_list_cleanup(struct ath12k_base *ab);
void ath12k_dp_rx_process_reo_status(struct ath12k_base *ab);
int ath12k_dp_rx_process_wbm_err(struct ath12k_base *ab,
				 struct napi_struct *napi, int budget);
int ath12k_dp_rx_process_err(struct ath12k_base *ab, struct napi_struct *napi,
			     int budget);
int ath12k_dp_rx_process(struct ath12k_base *ab, int mac_id,
			 struct napi_struct *napi,
			 int budget);
int ath12k_dp_rx_bufs_replenish(struct ath12k_base *ab,
				struct dp_rxdma_ring *rx_ring,
				struct list_head *used_list,
				int req_entries);
int ath12k_dp_rx_pdev_mon_attach(struct ath12k *ar);
int ath12k_dp_rx_peer_frag_setup(struct ath12k *ar, const u8 *peer_mac, int vdev_id);

u8 ath12k_dp_rx_h_l3pad(struct ath12k_base *ab,
			struct hal_rx_desc *desc);
struct ath12k_peer *
ath12k_dp_rx_h_find_peer(struct ath12k_base *ab, struct sk_buff *msdu);
u8 ath12k_dp_rx_h_decap_type(struct ath12k_base *ab,
			     struct hal_rx_desc *desc);
u32 ath12k_dp_rx_h_mpdu_err(struct ath12k_base *ab,
			    struct hal_rx_desc *desc);
void ath12k_dp_rx_h_ppdu(struct ath12k *ar, struct hal_rx_desc *rx_desc,
			 struct ieee80211_rx_status *rx_status);
struct ath12k_peer *
ath12k_dp_rx_h_find_peer(struct ath12k_base *ab, struct sk_buff *msdu);

int ath12k_dp_rxdma_ring_sel_config_qcn9274(struct ath12k_base *ab);
int ath12k_dp_rxdma_ring_sel_config_wcn7850(struct ath12k_base *ab);

int ath12k_dp_htt_tlv_iter(struct ath12k_base *ab, const void *ptr, size_t len,
			   int (*iter)(struct ath12k_base *ar, u16 tag, u16 len,
				       const void *ptr, void *data),
			   void *data);
#endif /* ATH12K_DP_RX_H */
