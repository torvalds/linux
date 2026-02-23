/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef ATH12K_DP_RX_H
#define ATH12K_DP_RX_H

#include <crypto/hash.h>
#include "core.h"
#include "debug.h"

#define DP_MAX_NWIFI_HDR_LEN	30

struct ath12k_reoq_buf {
	void *vaddr;
	dma_addr_t paddr_aligned;
	u32 size;
};

struct ath12k_dp_rx_tid {
	u8 tid;
	u32 ba_win_sz;
	struct ath12k_reoq_buf qbuf;

	/* Info related to rx fragments */
	u32 cur_sn;
	u16 last_frag_no;
	u16 rx_frag_bitmap;

	struct sk_buff_head rx_frags;
	struct hal_reo_dest_ring *dst_ring_desc;

	/* Timer info related to fragments */
	struct timer_list frag_timer;
	struct ath12k_dp *dp;
};

struct ath12k_dp_rx_tid_rxq {
	u8 tid;
	bool active;
	struct ath12k_reoq_buf qbuf;
};

struct ath12k_dp_rx_reo_cache_flush_elem {
	struct list_head list;
	struct ath12k_dp_rx_tid_rxq data;
	unsigned long ts;
};

struct dp_reo_update_rx_queue_elem {
	struct list_head list;
	struct ath12k_dp_rx_tid_rxq rx_tid;
	int peer_id;
	bool is_ml_peer;
	u16 ml_peer_id;
};

struct ath12k_dp_rx_reo_cmd {
	struct list_head list;
	struct ath12k_dp_rx_tid_rxq data;
	int cmd_num;
	void (*handler)(struct ath12k_dp *dp, void *ctx,
			enum hal_reo_cmd_status status);
};

#define ATH12K_DP_RX_FRAGMENT_TIMEOUT_MS (2 * HZ)

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
	default:
		ret = NL80211_RATE_INFO_HE_GI_0_8;
		break;
	}

	return ret;
}

static inline bool ath12k_dp_rx_h_more_frags(struct ath12k_hal *hal,
					     struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr;

	hdr = (struct ieee80211_hdr *)(skb->data + hal->hal_desc_sz);
	return ieee80211_has_morefrags(hdr->frame_control);
}

static inline u16 ath12k_dp_rx_h_frag_no(struct ath12k_hal *hal,
					 struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr;

	hdr = (struct ieee80211_hdr *)(skb->data + hal->hal_desc_sz);
	return le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_FRAG;
}

static inline u8 ath12k_dp_rx_h_l3pad(struct ath12k_base *ab,
				      struct hal_rx_desc *desc)
{
	return ab->hal.ops->rx_desc_get_l3_pad_bytes(desc);
}

static inline void ath12k_dp_rx_desc_end_tlv_copy(struct ath12k_hal *hal,
						  struct hal_rx_desc *fdesc,
						  struct hal_rx_desc *ldesc)
{
	hal->ops->rx_desc_copy_end_tlv(fdesc, ldesc);
}

static inline void ath12k_dp_rxdesc_set_msdu_len(struct ath12k_hal *hal,
						 struct hal_rx_desc *desc,
						 u16 len)
{
	hal->ops->rx_desc_set_msdu_len(desc, len);
}

static inline u32 ath12k_dp_rxdesc_get_ppduid(struct ath12k_base *ab,
					      struct hal_rx_desc *rx_desc)
{
	return ab->hal.ops->rx_desc_get_mpdu_ppdu_id(rx_desc);
}

static inline void ath12k_dp_rx_desc_get_dot11_hdr(struct ath12k_hal *hal,
						   struct hal_rx_desc *desc,
						   struct ieee80211_hdr *hdr)
{
	hal->ops->rx_desc_get_dot11_hdr(desc, hdr);
}

static inline void ath12k_dp_rx_desc_get_crypto_header(struct ath12k_hal *hal,
						       struct hal_rx_desc *desc,
						       u8 *crypto_hdr,
						       enum hal_encrypt_type enctype)
{
	hal->ops->rx_desc_get_crypto_header(desc, crypto_hdr, enctype);
}

static inline u8 ath12k_dp_rx_get_msdu_src_link(struct ath12k_hal *hal,
						struct hal_rx_desc *desc)
{
	return hal->ops->rx_desc_get_msdu_src_link_id(desc);
}

static inline void ath12k_dp_clean_up_skb_list(struct sk_buff_head *skb_list)
{
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(skb_list)))
		dev_kfree_skb_any(skb);
}

static inline
void ath12k_dp_extract_rx_desc_data(struct ath12k_hal *hal,
				    struct hal_rx_desc_data *rx_info,
				    struct hal_rx_desc *rx_desc,
				    struct hal_rx_desc *ldesc)
{
	hal->ops->extract_rx_desc_data(rx_info, rx_desc, ldesc);
}

void ath12k_dp_rx_h_undecap(struct ath12k_pdev_dp *dp_pdev, struct sk_buff *msdu,
			    struct hal_rx_desc *rx_desc,
			    enum hal_encrypt_type enctype,
			    bool decrypted,
			    struct hal_rx_desc_data *rx_info);
void ath12k_dp_rx_deliver_msdu(struct ath12k_pdev_dp *dp_pdev, struct napi_struct *napi,
			       struct sk_buff *msdu,
			       struct hal_rx_desc_data *rx_info);
bool ath12k_dp_rx_check_nwifi_hdr_len_valid(struct ath12k_dp *dp,
					    struct hal_rx_desc *rx_desc,
					    struct sk_buff *msdu,
					    struct hal_rx_desc_data *rx_info);
u64 ath12k_dp_rx_h_get_pn(struct ath12k_dp *dp, struct sk_buff *skb);
void ath12k_dp_rx_h_sort_frags(struct ath12k_hal *hal,
			       struct sk_buff_head *frag_list,
			       struct sk_buff *cur_frag);
void ath12k_dp_rx_h_undecap_frag(struct ath12k_pdev_dp *dp_pdev, struct sk_buff *msdu,
				 enum hal_encrypt_type enctype, u32 flags);
int ath12k_dp_rx_h_michael_mic(struct crypto_shash *tfm, u8 *key,
			       struct ieee80211_hdr *hdr, u8 *data,
			       size_t data_len, u8 *mic);
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
void ath12k_dp_rx_peer_tid_cleanup(struct ath12k *ar, struct ath12k_dp_link_peer *peer);
void ath12k_dp_rx_peer_tid_delete(struct ath12k *ar,
				  struct ath12k_dp_link_peer *peer, u8 tid);
int ath12k_dp_rx_peer_tid_setup(struct ath12k *ar, const u8 *peer_mac, int vdev_id,
				u8 tid, u32 ba_win_sz, u16 ssn,
				enum hal_pn_type pn_type);
int ath12k_dp_rx_pdev_reo_setup(struct ath12k_base *ab);
void ath12k_dp_rx_pdev_reo_cleanup(struct ath12k_base *ab);
int ath12k_dp_rx_htt_setup(struct ath12k_base *ab);
int ath12k_dp_rx_alloc(struct ath12k_base *ab);
void ath12k_dp_rx_free(struct ath12k_base *ab);
int ath12k_dp_rx_pdev_alloc(struct ath12k_base *ab, int pdev_idx);
void ath12k_dp_rx_pdev_free(struct ath12k_base *ab, int pdev_idx);
void ath12k_dp_rx_reo_cmd_list_cleanup(struct ath12k_base *ab);
int ath12k_dp_rx_bufs_replenish(struct ath12k_dp *dp,
				struct dp_rxdma_ring *rx_ring,
				struct list_head *used_list,
				int req_entries);
int ath12k_dp_rx_pdev_mon_attach(struct ath12k *ar);
int ath12k_dp_rx_peer_frag_setup(struct ath12k *ar, const u8 *peer_mac, int vdev_id);

u8 ath12k_dp_rx_h_l3pad(struct ath12k_base *ab,
			struct hal_rx_desc *desc);
struct ath12k_dp_link_peer *
ath12k_dp_rx_h_find_link_peer(struct ath12k_pdev_dp *dp_pdev, struct sk_buff *msdu,
			      struct hal_rx_desc_data *rx_info);
u8 ath12k_dp_rx_h_decap_type(struct ath12k_base *ab,
			     struct hal_rx_desc *desc);
u32 ath12k_dp_rx_h_mpdu_err(struct ath12k_base *ab,
			    struct hal_rx_desc *desc);
int ath12k_dp_rx_crypto_mic_len(struct ath12k_dp *dp, enum hal_encrypt_type enctype);
u32 ath12k_dp_rxdesc_get_ppduid(struct ath12k_base *ab,
				struct hal_rx_desc *rx_desc);
void ath12k_dp_rx_h_ppdu(struct ath12k_pdev_dp *dp_pdev,
			 struct hal_rx_desc_data *rx_info);
struct sk_buff *ath12k_dp_rx_get_msdu_last_buf(struct sk_buff_head *msdu_list,
					       struct sk_buff *first);
void ath12k_dp_reo_cmd_free(struct ath12k_dp *dp, void *ctx,
			    enum hal_reo_cmd_status status);
void ath12k_dp_rx_tid_del_func(struct ath12k_dp *dp, void *ctx,
			       enum hal_reo_cmd_status status);
void ath12k_dp_rx_process_reo_cmd_update_rx_queue_list(struct ath12k_dp *dp);
void ath12k_dp_init_rx_tid_rxq(struct ath12k_dp_rx_tid_rxq *rx_tid_rxq,
			       struct ath12k_dp_rx_tid *rx_tid,
			       bool active);
void ath12k_dp_mark_tid_as_inactive(struct ath12k_dp *dp, int peer_id, u8 tid);
#endif /* ATH12K_DP_RX_H */
