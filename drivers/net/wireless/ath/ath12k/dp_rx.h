/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef ATH12K_DP_RX_H
#define ATH12K_DP_RX_H

#include <crypto/hash.h>
#include "core.h"
#include "wifi7/hal_rx_desc.h"
#include "debug.h"

#define DP_MAX_NWIFI_HDR_LEN	30

struct ath12k_dp_rx_tid {
	u8 tid;
	u32 ba_win_sz;
	bool active;
	struct ath12k_reoq_buf qbuf;

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

struct ath12k_dp_rx_info {
	struct ieee80211_rx_status *rx_status;
	u32 phy_meta_data;
	u16 peer_id;
	u8 decap_type;
	u8 pkt_type;
	u8 sgi;
	u8 rate_mcs;
	u8 bw;
	u8 nss;
	u8 addr2[ETH_ALEN];
	u8 tid;
	bool ip_csum_fail;
	bool l4_csum_fail;
	bool is_mcbc;
	bool addr2_present;
};

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

static inline enum hal_encrypt_type ath12k_dp_rx_h_enctype(struct ath12k_base *ab,
							   struct hal_rx_desc *desc)
{
	if (!ab->hal_rx_ops->rx_desc_encrypt_valid(desc))
		return HAL_ENCRYPT_TYPE_OPEN;

	return ab->hal_rx_ops->rx_desc_get_encrypt_type(desc);
}

static inline u8 ath12k_dp_rx_h_decap_type(struct ath12k_base *ab,
					   struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_decap_type(desc);
}

static inline u8 ath12k_dp_rx_h_mesh_ctl_present(struct ath12k_base *ab,
						 struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_mesh_ctl(desc);
}

static inline bool ath12k_dp_rx_h_seq_ctrl_valid(struct ath12k_base *ab,
						 struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_mpdu_seq_ctl_vld(desc);
}

static inline bool ath12k_dp_rx_h_fc_valid(struct ath12k_base *ab,
					   struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_mpdu_fc_valid(desc);
}

static inline bool ath12k_dp_rx_h_more_frags(struct ath12k_base *ab,
					     struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr;

	hdr = (struct ieee80211_hdr *)(skb->data + ab->hal.hal_desc_sz);
	return ieee80211_has_morefrags(hdr->frame_control);
}

static inline u16 ath12k_dp_rx_h_frag_no(struct ath12k_base *ab,
					 struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr;

	hdr = (struct ieee80211_hdr *)(skb->data + ab->hal.hal_desc_sz);
	return le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_FRAG;
}

static inline u16 ath12k_dp_rx_h_seq_no(struct ath12k_base *ab,
					struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_mpdu_start_seq_no(desc);
}

static inline bool ath12k_dp_rx_h_msdu_done(struct ath12k_base *ab,
					    struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->dp_rx_h_msdu_done(desc);
}

static inline bool ath12k_dp_rx_h_l4_cksum_fail(struct ath12k_base *ab,
						struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->dp_rx_h_l4_cksum_fail(desc);
}

static inline bool ath12k_dp_rx_h_ip_cksum_fail(struct ath12k_base *ab,
						struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->dp_rx_h_ip_cksum_fail(desc);
}

static inline bool ath12k_dp_rx_h_is_decrypted(struct ath12k_base *ab,
					       struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->dp_rx_h_is_decrypted(desc);
}

static inline u32 ath12k_dp_rx_h_mpdu_err(struct ath12k_base *ab,
					  struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->dp_rx_h_mpdu_err(desc);
}

static inline u16 ath12k_dp_rx_h_msdu_len(struct ath12k_base *ab,
					  struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_msdu_len(desc);
}

static inline u8 ath12k_dp_rx_h_sgi(struct ath12k_base *ab,
				    struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_msdu_sgi(desc);
}

static inline u8 ath12k_dp_rx_h_rate_mcs(struct ath12k_base *ab,
					 struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_msdu_rate_mcs(desc);
}

static inline u8 ath12k_dp_rx_h_rx_bw(struct ath12k_base *ab,
				      struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_msdu_rx_bw(desc);
}

static inline u32 ath12k_dp_rx_h_freq(struct ath12k_base *ab,
				      struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_msdu_freq(desc);
}

static inline u8 ath12k_dp_rx_h_pkt_type(struct ath12k_base *ab,
					 struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_msdu_pkt_type(desc);
}

static inline u8 ath12k_dp_rx_h_nss(struct ath12k_base *ab,
				    struct hal_rx_desc *desc)
{
	return hweight8(ab->hal_rx_ops->rx_desc_get_msdu_nss(desc));
}

static inline u8 ath12k_dp_rx_h_tid(struct ath12k_base *ab,
				    struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_mpdu_tid(desc);
}

static inline u16 ath12k_dp_rx_h_peer_id(struct ath12k_base *ab,
					 struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_mpdu_peer_id(desc);
}

static inline u8 ath12k_dp_rx_h_l3pad(struct ath12k_base *ab,
				      struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_l3_pad_bytes(desc);
}

static inline bool ath12k_dp_rx_h_first_msdu(struct ath12k_base *ab,
					     struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_first_msdu(desc);
}

static inline bool ath12k_dp_rx_h_last_msdu(struct ath12k_base *ab,
					    struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_last_msdu(desc);
}

static inline void ath12k_dp_rx_desc_end_tlv_copy(struct ath12k_base *ab,
						  struct hal_rx_desc *fdesc,
						  struct hal_rx_desc *ldesc)
{
	ab->hw_params->hal_ops->rx_desc_copy_end_tlv(fdesc, ldesc);
}

static inline void ath12k_dp_rxdesc_set_msdu_len(struct ath12k_base *ab,
						 struct hal_rx_desc *desc,
						 u16 len)
{
	ab->hw_params->hal_ops->rx_desc_set_msdu_len(desc, len);
}

static inline u32 ath12k_dp_rxdesc_get_ppduid(struct ath12k_base *ab,
					      struct hal_rx_desc *rx_desc)
{
	return ab->hal_rx_ops->rx_desc_get_mpdu_ppdu_id(rx_desc);
}

static inline bool ath12k_dp_rxdesc_mpdu_valid(struct ath12k_base *ab,
					       struct hal_rx_desc *rx_desc)
{
	u32 tlv_tag;

	tlv_tag = ab->hal_rx_ops->rx_desc_get_mpdu_start_tag(rx_desc);

	return tlv_tag == HAL_RX_MPDU_START;
}

static inline bool ath12k_dp_rx_h_is_da_mcbc(struct ath12k_base *ab,
					     struct hal_rx_desc *desc)
{
	return (ath12k_dp_rx_h_first_msdu(ab, desc) &&
		ab->hal_rx_ops->rx_desc_is_da_mcbc(desc));
}

static inline bool ath12k_dp_rxdesc_mac_addr2_valid(struct ath12k_base *ab,
						    struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_mac_addr2_valid(desc);
}

static inline u8 *ath12k_dp_rxdesc_get_mpdu_start_addr2(struct ath12k_base *ab,
							struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_mpdu_start_addr2(desc);
}

static inline void ath12k_dp_rx_desc_get_dot11_hdr(struct ath12k_base *ab,
						   struct hal_rx_desc *desc,
						   struct ieee80211_hdr *hdr)
{
	ab->hw_params->hal_ops->rx_desc_get_dot11_hdr(desc, hdr);
}

static inline void ath12k_dp_rx_desc_get_crypto_header(struct ath12k_base *ab,
						       struct hal_rx_desc *desc,
						       u8 *crypto_hdr,
						       enum hal_encrypt_type enctype)
{
	ab->hw_params->hal_ops->rx_desc_get_crypto_header(desc, crypto_hdr, enctype);
}

static inline u8 ath12k_dp_rx_get_msdu_src_link(struct ath12k_base *ab,
						struct hal_rx_desc *desc)
{
	return ab->hw_params->hal_ops->rx_desc_get_msdu_src_link_id(desc);
}

static inline void ath12k_dp_clean_up_skb_list(struct sk_buff_head *skb_list)
{
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(skb_list)))
		dev_kfree_skb_any(skb);
}

void ath12k_dp_rx_h_undecap(struct ath12k *ar, struct sk_buff *msdu,
			    struct hal_rx_desc *rx_desc,
			    enum hal_encrypt_type enctype,
			    struct ieee80211_rx_status *status,
			    bool decrypted);
void ath12k_dp_rx_deliver_msdu(struct ath12k *ar, struct napi_struct *napi,
			       struct sk_buff *msdu,
			       struct ath12k_dp_rx_info *rx_info);
bool ath12k_dp_rx_check_nwifi_hdr_len_valid(struct ath12k_base *ab,
					    struct hal_rx_desc *rx_desc,
					    struct sk_buff *msdu);
u64 ath12k_dp_rx_h_get_pn(struct ath12k *ar, struct sk_buff *skb);
void ath12k_dp_rx_h_sort_frags(struct ath12k_base *ab,
			       struct sk_buff_head *frag_list,
			       struct sk_buff *cur_frag);
void ath12k_dp_rx_h_undecap_frag(struct ath12k *ar, struct sk_buff *msdu,
				 enum hal_encrypt_type enctype, u32 flags);
void ath12k_dp_rx_frags_cleanup(struct ath12k_dp_rx_tid *rx_tid,
				bool rel_link_desc);
int ath12k_dp_rx_h_michael_mic(struct crypto_shash *tfm, u8 *key,
			       struct ieee80211_hdr *hdr, u8 *data,
			       size_t data_len, u8 *mic);
u16 ath12k_dp_rx_get_peer_id(struct ath12k_base *ab,
			     enum ath12k_peer_metadata_version ver,
			     __le32 peer_metadata);
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
int ath12k_dp_rx_pdev_reo_setup(struct ath12k_base *ab);
void ath12k_dp_rx_pdev_reo_cleanup(struct ath12k_base *ab);
int ath12k_dp_rx_htt_setup(struct ath12k_base *ab);
int ath12k_dp_rx_alloc(struct ath12k_base *ab);
void ath12k_dp_rx_free(struct ath12k_base *ab);
int ath12k_dp_rx_pdev_alloc(struct ath12k_base *ab, int pdev_idx);
void ath12k_dp_rx_pdev_free(struct ath12k_base *ab, int pdev_idx);
void ath12k_dp_rx_reo_cmd_list_cleanup(struct ath12k_base *ab);
int ath12k_dp_rx_bufs_replenish(struct ath12k_base *ab,
				struct dp_rxdma_ring *rx_ring,
				struct list_head *used_list,
				int req_entries);
int ath12k_dp_rx_pdev_mon_attach(struct ath12k *ar);
int ath12k_dp_rx_peer_frag_setup(struct ath12k *ar, const u8 *peer_mac, int vdev_id);

u8 ath12k_dp_rx_h_l3pad(struct ath12k_base *ab,
			struct hal_rx_desc *desc);
struct ath12k_peer *
ath12k_dp_rx_h_find_peer(struct ath12k_base *ab, struct sk_buff *msdu,
			 struct ath12k_dp_rx_info *rx_info);
u8 ath12k_dp_rx_h_decap_type(struct ath12k_base *ab,
			     struct hal_rx_desc *desc);
u32 ath12k_dp_rx_h_mpdu_err(struct ath12k_base *ab,
			    struct hal_rx_desc *desc);
void ath12k_dp_rx_h_fetch_info(struct ath12k_base *ab,  struct hal_rx_desc *rx_desc,
			       struct ath12k_dp_rx_info *rx_info);

int ath12k_dp_rx_crypto_mic_len(struct ath12k *ar, enum hal_encrypt_type enctype);
u32 ath12k_dp_rxdesc_get_ppduid(struct ath12k_base *ab,
				struct hal_rx_desc *rx_desc);
bool ath12k_dp_rxdesc_mpdu_valid(struct ath12k_base *ab,
				 struct hal_rx_desc *rx_desc);
bool ath12k_dp_rxdesc_mpdu_valid(struct ath12k_base *ab,
				 struct hal_rx_desc *rx_desc);
void ath12k_dp_rx_h_ppdu(struct ath12k *ar, struct ath12k_dp_rx_info *rx_info);
struct sk_buff *ath12k_dp_rx_get_msdu_last_buf(struct sk_buff_head *msdu_list,
					       struct sk_buff *first);
void ath12k_dp_reo_cmd_free(struct ath12k_dp *dp, void *ctx,
			    enum hal_reo_cmd_status status);
void ath12k_dp_rx_tid_del_func(struct ath12k_dp *dp, void *ctx,
			       enum hal_reo_cmd_status status);
#endif /* ATH12K_DP_RX_H */
