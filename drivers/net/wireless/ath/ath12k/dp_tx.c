// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "core.h"
#include "dp_tx.h"
#include "debug.h"
#include "debugfs.h"
#include "hw.h"
#include "peer.h"
#include "mac.h"

enum hal_tcl_encap_type
ath12k_dp_tx_get_encap_type(struct ath12k_base *ab, struct sk_buff *skb)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);

	if (test_bit(ATH12K_FLAG_RAW_MODE, &ab->dev_flags))
		return HAL_TCL_ENCAP_TYPE_RAW;

	if (tx_info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP)
		return HAL_TCL_ENCAP_TYPE_ETHERNET;

	return HAL_TCL_ENCAP_TYPE_NATIVE_WIFI;
}
EXPORT_SYMBOL(ath12k_dp_tx_get_encap_type);

void ath12k_dp_tx_encap_nwifi(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	u8 *qos_ctl;

	if (!ieee80211_is_data_qos(hdr->frame_control))
		return;

	qos_ctl = ieee80211_get_qos_ctl(hdr);
	memmove(skb->data + IEEE80211_QOS_CTL_LEN,
		skb->data, (void *)qos_ctl - (void *)skb->data);
	skb_pull(skb, IEEE80211_QOS_CTL_LEN);

	hdr = (void *)skb->data;
	hdr->frame_control &= ~__cpu_to_le16(IEEE80211_STYPE_QOS_DATA);
}
EXPORT_SYMBOL(ath12k_dp_tx_encap_nwifi);

u8 ath12k_dp_tx_get_tid(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	struct ath12k_skb_cb *cb = ATH12K_SKB_CB(skb);

	if (cb->flags & ATH12K_SKB_HW_80211_ENCAP)
		return skb->priority & IEEE80211_QOS_CTL_TID_MASK;
	else if (!ieee80211_is_data_qos(hdr->frame_control))
		return HAL_DESC_REO_NON_QOS_TID;
	else
		return skb->priority & IEEE80211_QOS_CTL_TID_MASK;
}
EXPORT_SYMBOL(ath12k_dp_tx_get_tid);

enum hal_encrypt_type ath12k_dp_tx_get_encrypt_type(u32 cipher)
{
	switch (cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		return HAL_ENCRYPT_TYPE_WEP_40;
	case WLAN_CIPHER_SUITE_WEP104:
		return HAL_ENCRYPT_TYPE_WEP_104;
	case WLAN_CIPHER_SUITE_TKIP:
		return HAL_ENCRYPT_TYPE_TKIP_MIC;
	case WLAN_CIPHER_SUITE_CCMP:
		return HAL_ENCRYPT_TYPE_CCMP_128;
	case WLAN_CIPHER_SUITE_CCMP_256:
		return HAL_ENCRYPT_TYPE_CCMP_256;
	case WLAN_CIPHER_SUITE_GCMP:
		return HAL_ENCRYPT_TYPE_GCMP_128;
	case WLAN_CIPHER_SUITE_GCMP_256:
		return HAL_ENCRYPT_TYPE_AES_GCMP_256;
	default:
		return HAL_ENCRYPT_TYPE_OPEN;
	}
}
EXPORT_SYMBOL(ath12k_dp_tx_get_encrypt_type);

void ath12k_dp_tx_release_txbuf(struct ath12k_dp *dp,
				struct ath12k_tx_desc_info *tx_desc,
				u8 pool_id)
{
	spin_lock_bh(&dp->tx_desc_lock[pool_id]);
	tx_desc->skb_ext_desc = NULL;
	list_move_tail(&tx_desc->list, &dp->tx_desc_free_list[pool_id]);
	spin_unlock_bh(&dp->tx_desc_lock[pool_id]);
}
EXPORT_SYMBOL(ath12k_dp_tx_release_txbuf);

struct ath12k_tx_desc_info *ath12k_dp_tx_assign_buffer(struct ath12k_dp *dp,
						       u8 pool_id)
{
	struct ath12k_tx_desc_info *desc;

	spin_lock_bh(&dp->tx_desc_lock[pool_id]);
	desc = list_first_entry_or_null(&dp->tx_desc_free_list[pool_id],
					struct ath12k_tx_desc_info,
					list);
	if (!desc) {
		spin_unlock_bh(&dp->tx_desc_lock[pool_id]);
		ath12k_warn(dp->ab, "failed to allocate data Tx buffer\n");
		return NULL;
	}

	list_move_tail(&desc->list, &dp->tx_desc_used_list[pool_id]);
	spin_unlock_bh(&dp->tx_desc_lock[pool_id]);

	return desc;
}
EXPORT_SYMBOL(ath12k_dp_tx_assign_buffer);

void *ath12k_dp_metadata_align_skb(struct sk_buff *skb, u8 tail_len)
{
	struct sk_buff *tail;
	void *metadata;

	if (unlikely(skb_cow_data(skb, tail_len, &tail) < 0))
		return NULL;

	metadata = pskb_put(skb, tail, tail_len);
	memset(metadata, 0, tail_len);
	return metadata;
}
EXPORT_SYMBOL(ath12k_dp_metadata_align_skb);

static void ath12k_dp_tx_move_payload(struct sk_buff *skb,
				      unsigned long delta,
				      bool head)
{
	unsigned long len = skb->len;

	if (head) {
		skb_push(skb, delta);
		memmove(skb->data, skb->data + delta, len);
		skb_trim(skb, len);
	} else {
		skb_put(skb, delta);
		memmove(skb->data + delta, skb->data, len);
		skb_pull(skb, delta);
	}
}

int ath12k_dp_tx_align_payload(struct ath12k_dp *dp, struct sk_buff **pskb)
{
	u32 iova_mask = dp->hw_params->iova_mask;
	unsigned long offset, delta1, delta2;
	struct sk_buff *skb2, *skb = *pskb;
	unsigned int headroom = skb_headroom(skb);
	int tailroom = skb_tailroom(skb);
	int ret = 0;

	offset = (unsigned long)skb->data & iova_mask;
	delta1 = offset;
	delta2 = iova_mask - offset + 1;

	if (headroom >= delta1) {
		ath12k_dp_tx_move_payload(skb, delta1, true);
	} else if (tailroom >= delta2) {
		ath12k_dp_tx_move_payload(skb, delta2, false);
	} else {
		skb2 = skb_realloc_headroom(skb, iova_mask);
		if (!skb2) {
			ret = -ENOMEM;
			goto out;
		}

		dev_kfree_skb_any(skb);

		offset = (unsigned long)skb2->data & iova_mask;
		if (offset)
			ath12k_dp_tx_move_payload(skb2, offset, true);
		*pskb = skb2;
	}

out:
	return ret;
}
EXPORT_SYMBOL(ath12k_dp_tx_align_payload);

void ath12k_dp_tx_free_txbuf(struct ath12k_dp *dp,
			     struct dp_tx_ring *tx_ring,
			     struct ath12k_tx_desc_params *desc_params)
{
	struct ath12k_pdev_dp *dp_pdev;
	struct sk_buff *msdu = desc_params->skb;
	struct ath12k_skb_cb *skb_cb;
	u8 pdev_idx = ath12k_hw_mac_id_to_pdev_id(dp->hw_params, desc_params->mac_id);

	skb_cb = ATH12K_SKB_CB(msdu);

	dma_unmap_single(dp->dev, skb_cb->paddr, msdu->len, DMA_TO_DEVICE);
	if (skb_cb->paddr_ext_desc) {
		dma_unmap_single(dp->dev, skb_cb->paddr_ext_desc,
				 desc_params->skb_ext_desc->len, DMA_TO_DEVICE);
		dev_kfree_skb_any(desc_params->skb_ext_desc);
	}

	guard(rcu)();

	dp_pdev = ath12k_dp_to_pdev_dp(dp, pdev_idx);

	ieee80211_free_txskb(ath12k_pdev_dp_to_hw(dp_pdev), msdu);

	if (atomic_dec_and_test(&dp_pdev->num_tx_pending))
		wake_up(&dp_pdev->tx_empty_waitq);
}
EXPORT_SYMBOL(ath12k_dp_tx_free_txbuf);
