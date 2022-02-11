// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include "core.h"
#include "htc.h"
#include "htt.h"
#include "txrx.h"
#include "debug.h"
#include "trace.h"
#include "mac.h"

#include <linux/log2.h>
#include <linux/bitfield.h>

/* when under memory pressure rx ring refill may fail and needs a retry */
#define HTT_RX_RING_REFILL_RETRY_MS 50

#define HTT_RX_RING_REFILL_RESCHED_MS 5

/* shortcut to interpret a raw memory buffer as a rx descriptor */
#define HTT_RX_BUF_TO_RX_DESC(hw, buf) ath10k_htt_rx_desc_from_raw_buffer(hw, buf)

static int ath10k_htt_rx_get_csum_state(struct ath10k_hw_params *hw, struct sk_buff *skb);

static struct sk_buff *
ath10k_htt_rx_find_skb_paddr(struct ath10k *ar, u64 paddr)
{
	struct ath10k_skb_rxcb *rxcb;

	hash_for_each_possible(ar->htt.rx_ring.skb_table, rxcb, hlist, paddr)
		if (rxcb->paddr == paddr)
			return ATH10K_RXCB_SKB(rxcb);

	WARN_ON_ONCE(1);
	return NULL;
}

static void ath10k_htt_rx_ring_free(struct ath10k_htt *htt)
{
	struct sk_buff *skb;
	struct ath10k_skb_rxcb *rxcb;
	struct hlist_node *n;
	int i;

	if (htt->rx_ring.in_ord_rx) {
		hash_for_each_safe(htt->rx_ring.skb_table, i, n, rxcb, hlist) {
			skb = ATH10K_RXCB_SKB(rxcb);
			dma_unmap_single(htt->ar->dev, rxcb->paddr,
					 skb->len + skb_tailroom(skb),
					 DMA_FROM_DEVICE);
			hash_del(&rxcb->hlist);
			dev_kfree_skb_any(skb);
		}
	} else {
		for (i = 0; i < htt->rx_ring.size; i++) {
			skb = htt->rx_ring.netbufs_ring[i];
			if (!skb)
				continue;

			rxcb = ATH10K_SKB_RXCB(skb);
			dma_unmap_single(htt->ar->dev, rxcb->paddr,
					 skb->len + skb_tailroom(skb),
					 DMA_FROM_DEVICE);
			dev_kfree_skb_any(skb);
		}
	}

	htt->rx_ring.fill_cnt = 0;
	hash_init(htt->rx_ring.skb_table);
	memset(htt->rx_ring.netbufs_ring, 0,
	       htt->rx_ring.size * sizeof(htt->rx_ring.netbufs_ring[0]));
}

static size_t ath10k_htt_get_rx_ring_size_32(struct ath10k_htt *htt)
{
	return htt->rx_ring.size * sizeof(htt->rx_ring.paddrs_ring_32);
}

static size_t ath10k_htt_get_rx_ring_size_64(struct ath10k_htt *htt)
{
	return htt->rx_ring.size * sizeof(htt->rx_ring.paddrs_ring_64);
}

static void ath10k_htt_config_paddrs_ring_32(struct ath10k_htt *htt,
					     void *vaddr)
{
	htt->rx_ring.paddrs_ring_32 = vaddr;
}

static void ath10k_htt_config_paddrs_ring_64(struct ath10k_htt *htt,
					     void *vaddr)
{
	htt->rx_ring.paddrs_ring_64 = vaddr;
}

static void ath10k_htt_set_paddrs_ring_32(struct ath10k_htt *htt,
					  dma_addr_t paddr, int idx)
{
	htt->rx_ring.paddrs_ring_32[idx] = __cpu_to_le32(paddr);
}

static void ath10k_htt_set_paddrs_ring_64(struct ath10k_htt *htt,
					  dma_addr_t paddr, int idx)
{
	htt->rx_ring.paddrs_ring_64[idx] = __cpu_to_le64(paddr);
}

static void ath10k_htt_reset_paddrs_ring_32(struct ath10k_htt *htt, int idx)
{
	htt->rx_ring.paddrs_ring_32[idx] = 0;
}

static void ath10k_htt_reset_paddrs_ring_64(struct ath10k_htt *htt, int idx)
{
	htt->rx_ring.paddrs_ring_64[idx] = 0;
}

static void *ath10k_htt_get_vaddr_ring_32(struct ath10k_htt *htt)
{
	return (void *)htt->rx_ring.paddrs_ring_32;
}

static void *ath10k_htt_get_vaddr_ring_64(struct ath10k_htt *htt)
{
	return (void *)htt->rx_ring.paddrs_ring_64;
}

static int __ath10k_htt_rx_ring_fill_n(struct ath10k_htt *htt, int num)
{
	struct ath10k_hw_params *hw = &htt->ar->hw_params;
	struct htt_rx_desc *rx_desc;
	struct ath10k_skb_rxcb *rxcb;
	struct sk_buff *skb;
	dma_addr_t paddr;
	int ret = 0, idx;

	/* The Full Rx Reorder firmware has no way of telling the host
	 * implicitly when it copied HTT Rx Ring buffers to MAC Rx Ring.
	 * To keep things simple make sure ring is always half empty. This
	 * guarantees there'll be no replenishment overruns possible.
	 */
	BUILD_BUG_ON(HTT_RX_RING_FILL_LEVEL >= HTT_RX_RING_SIZE / 2);

	idx = __le32_to_cpu(*htt->rx_ring.alloc_idx.vaddr);

	if (idx < 0 || idx >= htt->rx_ring.size) {
		ath10k_err(htt->ar, "rx ring index is not valid, firmware malfunctioning?\n");
		idx &= htt->rx_ring.size_mask;
		ret = -ENOMEM;
		goto fail;
	}

	while (num > 0) {
		skb = dev_alloc_skb(HTT_RX_BUF_SIZE + HTT_RX_DESC_ALIGN);
		if (!skb) {
			ret = -ENOMEM;
			goto fail;
		}

		if (!IS_ALIGNED((unsigned long)skb->data, HTT_RX_DESC_ALIGN))
			skb_pull(skb,
				 PTR_ALIGN(skb->data, HTT_RX_DESC_ALIGN) -
				 skb->data);

		/* Clear rx_desc attention word before posting to Rx ring */
		rx_desc = HTT_RX_BUF_TO_RX_DESC(hw, skb->data);
		ath10k_htt_rx_desc_get_attention(hw, rx_desc)->flags = __cpu_to_le32(0);

		paddr = dma_map_single(htt->ar->dev, skb->data,
				       skb->len + skb_tailroom(skb),
				       DMA_FROM_DEVICE);

		if (unlikely(dma_mapping_error(htt->ar->dev, paddr))) {
			dev_kfree_skb_any(skb);
			ret = -ENOMEM;
			goto fail;
		}

		rxcb = ATH10K_SKB_RXCB(skb);
		rxcb->paddr = paddr;
		htt->rx_ring.netbufs_ring[idx] = skb;
		ath10k_htt_set_paddrs_ring(htt, paddr, idx);
		htt->rx_ring.fill_cnt++;

		if (htt->rx_ring.in_ord_rx) {
			hash_add(htt->rx_ring.skb_table,
				 &ATH10K_SKB_RXCB(skb)->hlist,
				 paddr);
		}

		num--;
		idx++;
		idx &= htt->rx_ring.size_mask;
	}

fail:
	/*
	 * Make sure the rx buffer is updated before available buffer
	 * index to avoid any potential rx ring corruption.
	 */
	mb();
	*htt->rx_ring.alloc_idx.vaddr = __cpu_to_le32(idx);
	return ret;
}

static int ath10k_htt_rx_ring_fill_n(struct ath10k_htt *htt, int num)
{
	lockdep_assert_held(&htt->rx_ring.lock);
	return __ath10k_htt_rx_ring_fill_n(htt, num);
}

static void ath10k_htt_rx_msdu_buff_replenish(struct ath10k_htt *htt)
{
	int ret, num_deficit, num_to_fill;

	/* Refilling the whole RX ring buffer proves to be a bad idea. The
	 * reason is RX may take up significant amount of CPU cycles and starve
	 * other tasks, e.g. TX on an ethernet device while acting as a bridge
	 * with ath10k wlan interface. This ended up with very poor performance
	 * once CPU the host system was overwhelmed with RX on ath10k.
	 *
	 * By limiting the number of refills the replenishing occurs
	 * progressively. This in turns makes use of the fact tasklets are
	 * processed in FIFO order. This means actual RX processing can starve
	 * out refilling. If there's not enough buffers on RX ring FW will not
	 * report RX until it is refilled with enough buffers. This
	 * automatically balances load wrt to CPU power.
	 *
	 * This probably comes at a cost of lower maximum throughput but
	 * improves the average and stability.
	 */
	spin_lock_bh(&htt->rx_ring.lock);
	num_deficit = htt->rx_ring.fill_level - htt->rx_ring.fill_cnt;
	num_to_fill = min(ATH10K_HTT_MAX_NUM_REFILL, num_deficit);
	num_deficit -= num_to_fill;
	ret = ath10k_htt_rx_ring_fill_n(htt, num_to_fill);
	if (ret == -ENOMEM) {
		/*
		 * Failed to fill it to the desired level -
		 * we'll start a timer and try again next time.
		 * As long as enough buffers are left in the ring for
		 * another A-MPDU rx, no special recovery is needed.
		 */
		mod_timer(&htt->rx_ring.refill_retry_timer, jiffies +
			  msecs_to_jiffies(HTT_RX_RING_REFILL_RETRY_MS));
	} else if (num_deficit > 0) {
		mod_timer(&htt->rx_ring.refill_retry_timer, jiffies +
			  msecs_to_jiffies(HTT_RX_RING_REFILL_RESCHED_MS));
	}
	spin_unlock_bh(&htt->rx_ring.lock);
}

static void ath10k_htt_rx_ring_refill_retry(struct timer_list *t)
{
	struct ath10k_htt *htt = from_timer(htt, t, rx_ring.refill_retry_timer);

	ath10k_htt_rx_msdu_buff_replenish(htt);
}

int ath10k_htt_rx_ring_refill(struct ath10k *ar)
{
	struct ath10k_htt *htt = &ar->htt;
	int ret;

	if (ar->bus_param.dev_type == ATH10K_DEV_TYPE_HL)
		return 0;

	spin_lock_bh(&htt->rx_ring.lock);
	ret = ath10k_htt_rx_ring_fill_n(htt, (htt->rx_ring.fill_level -
					      htt->rx_ring.fill_cnt));

	if (ret)
		ath10k_htt_rx_ring_free(htt);

	spin_unlock_bh(&htt->rx_ring.lock);

	return ret;
}

void ath10k_htt_rx_free(struct ath10k_htt *htt)
{
	if (htt->ar->bus_param.dev_type == ATH10K_DEV_TYPE_HL)
		return;

	del_timer_sync(&htt->rx_ring.refill_retry_timer);

	skb_queue_purge(&htt->rx_msdus_q);
	skb_queue_purge(&htt->rx_in_ord_compl_q);
	skb_queue_purge(&htt->tx_fetch_ind_q);

	spin_lock_bh(&htt->rx_ring.lock);
	ath10k_htt_rx_ring_free(htt);
	spin_unlock_bh(&htt->rx_ring.lock);

	dma_free_coherent(htt->ar->dev,
			  ath10k_htt_get_rx_ring_size(htt),
			  ath10k_htt_get_vaddr_ring(htt),
			  htt->rx_ring.base_paddr);

	dma_free_coherent(htt->ar->dev,
			  sizeof(*htt->rx_ring.alloc_idx.vaddr),
			  htt->rx_ring.alloc_idx.vaddr,
			  htt->rx_ring.alloc_idx.paddr);

	kfree(htt->rx_ring.netbufs_ring);
}

static inline struct sk_buff *ath10k_htt_rx_netbuf_pop(struct ath10k_htt *htt)
{
	struct ath10k *ar = htt->ar;
	int idx;
	struct sk_buff *msdu;

	lockdep_assert_held(&htt->rx_ring.lock);

	if (htt->rx_ring.fill_cnt == 0) {
		ath10k_warn(ar, "tried to pop sk_buff from an empty rx ring\n");
		return NULL;
	}

	idx = htt->rx_ring.sw_rd_idx.msdu_payld;
	msdu = htt->rx_ring.netbufs_ring[idx];
	htt->rx_ring.netbufs_ring[idx] = NULL;
	ath10k_htt_reset_paddrs_ring(htt, idx);

	idx++;
	idx &= htt->rx_ring.size_mask;
	htt->rx_ring.sw_rd_idx.msdu_payld = idx;
	htt->rx_ring.fill_cnt--;

	dma_unmap_single(htt->ar->dev,
			 ATH10K_SKB_RXCB(msdu)->paddr,
			 msdu->len + skb_tailroom(msdu),
			 DMA_FROM_DEVICE);
	ath10k_dbg_dump(ar, ATH10K_DBG_HTT_DUMP, NULL, "htt rx netbuf pop: ",
			msdu->data, msdu->len + skb_tailroom(msdu));

	return msdu;
}

/* return: < 0 fatal error, 0 - non chained msdu, 1 chained msdu */
static int ath10k_htt_rx_amsdu_pop(struct ath10k_htt *htt,
				   struct sk_buff_head *amsdu)
{
	struct ath10k *ar = htt->ar;
	struct ath10k_hw_params *hw = &ar->hw_params;
	int msdu_len, msdu_chaining = 0;
	struct sk_buff *msdu;
	struct htt_rx_desc *rx_desc;
	struct rx_attention *rx_desc_attention;
	struct rx_frag_info_common *rx_desc_frag_info_common;
	struct rx_msdu_start_common *rx_desc_msdu_start_common;
	struct rx_msdu_end_common *rx_desc_msdu_end_common;

	lockdep_assert_held(&htt->rx_ring.lock);

	for (;;) {
		int last_msdu, msdu_len_invalid, msdu_chained;

		msdu = ath10k_htt_rx_netbuf_pop(htt);
		if (!msdu) {
			__skb_queue_purge(amsdu);
			return -ENOENT;
		}

		__skb_queue_tail(amsdu, msdu);

		rx_desc = HTT_RX_BUF_TO_RX_DESC(hw, msdu->data);
		rx_desc_attention = ath10k_htt_rx_desc_get_attention(hw, rx_desc);
		rx_desc_msdu_start_common = ath10k_htt_rx_desc_get_msdu_start(hw,
									      rx_desc);
		rx_desc_msdu_end_common = ath10k_htt_rx_desc_get_msdu_end(hw, rx_desc);
		rx_desc_frag_info_common = ath10k_htt_rx_desc_get_frag_info(hw, rx_desc);

		/* FIXME: we must report msdu payload since this is what caller
		 * expects now
		 */
		skb_put(msdu, hw->rx_desc_ops->rx_desc_msdu_payload_offset);
		skb_pull(msdu, hw->rx_desc_ops->rx_desc_msdu_payload_offset);

		/*
		 * Sanity check - confirm the HW is finished filling in the
		 * rx data.
		 * If the HW and SW are working correctly, then it's guaranteed
		 * that the HW's MAC DMA is done before this point in the SW.
		 * To prevent the case that we handle a stale Rx descriptor,
		 * just assert for now until we have a way to recover.
		 */
		if (!(__le32_to_cpu(rx_desc_attention->flags)
				& RX_ATTENTION_FLAGS_MSDU_DONE)) {
			__skb_queue_purge(amsdu);
			return -EIO;
		}

		msdu_len_invalid = !!(__le32_to_cpu(rx_desc_attention->flags)
					& (RX_ATTENTION_FLAGS_MPDU_LENGTH_ERR |
					   RX_ATTENTION_FLAGS_MSDU_LENGTH_ERR));
		msdu_len = MS(__le32_to_cpu(rx_desc_msdu_start_common->info0),
			      RX_MSDU_START_INFO0_MSDU_LENGTH);
		msdu_chained = rx_desc_frag_info_common->ring2_more_count;

		if (msdu_len_invalid)
			msdu_len = 0;

		skb_trim(msdu, 0);
		skb_put(msdu, min(msdu_len, ath10k_htt_rx_msdu_size(hw)));
		msdu_len -= msdu->len;

		/* Note: Chained buffers do not contain rx descriptor */
		while (msdu_chained--) {
			msdu = ath10k_htt_rx_netbuf_pop(htt);
			if (!msdu) {
				__skb_queue_purge(amsdu);
				return -ENOENT;
			}

			__skb_queue_tail(amsdu, msdu);
			skb_trim(msdu, 0);
			skb_put(msdu, min(msdu_len, HTT_RX_BUF_SIZE));
			msdu_len -= msdu->len;
			msdu_chaining = 1;
		}

		last_msdu = __le32_to_cpu(rx_desc_msdu_end_common->info0) &
				RX_MSDU_END_INFO0_LAST_MSDU;

		/* FIXME: why are we skipping the first part of the rx_desc? */
		trace_ath10k_htt_rx_desc(ar, rx_desc + sizeof(u32),
					 hw->rx_desc_ops->rx_desc_size - sizeof(u32));

		if (last_msdu)
			break;
	}

	if (skb_queue_empty(amsdu))
		msdu_chaining = -1;

	/*
	 * Don't refill the ring yet.
	 *
	 * First, the elements popped here are still in use - it is not
	 * safe to overwrite them until the matching call to
	 * mpdu_desc_list_next. Second, for efficiency it is preferable to
	 * refill the rx ring with 1 PPDU's worth of rx buffers (something
	 * like 32 x 3 buffers), rather than one MPDU's worth of rx buffers
	 * (something like 3 buffers). Consequently, we'll rely on the txrx
	 * SW to tell us when it is done pulling all the PPDU's rx buffers
	 * out of the rx ring, and then refill it just once.
	 */

	return msdu_chaining;
}

static struct sk_buff *ath10k_htt_rx_pop_paddr(struct ath10k_htt *htt,
					       u64 paddr)
{
	struct ath10k *ar = htt->ar;
	struct ath10k_skb_rxcb *rxcb;
	struct sk_buff *msdu;

	lockdep_assert_held(&htt->rx_ring.lock);

	msdu = ath10k_htt_rx_find_skb_paddr(ar, paddr);
	if (!msdu)
		return NULL;

	rxcb = ATH10K_SKB_RXCB(msdu);
	hash_del(&rxcb->hlist);
	htt->rx_ring.fill_cnt--;

	dma_unmap_single(htt->ar->dev, rxcb->paddr,
			 msdu->len + skb_tailroom(msdu),
			 DMA_FROM_DEVICE);
	ath10k_dbg_dump(ar, ATH10K_DBG_HTT_DUMP, NULL, "htt rx netbuf pop: ",
			msdu->data, msdu->len + skb_tailroom(msdu));

	return msdu;
}

static inline void ath10k_htt_append_frag_list(struct sk_buff *skb_head,
					       struct sk_buff *frag_list,
					       unsigned int frag_len)
{
	skb_shinfo(skb_head)->frag_list = frag_list;
	skb_head->data_len = frag_len;
	skb_head->len += skb_head->data_len;
}

static int ath10k_htt_rx_handle_amsdu_mon_32(struct ath10k_htt *htt,
					     struct sk_buff *msdu,
					     struct htt_rx_in_ord_msdu_desc **msdu_desc)
{
	struct ath10k *ar = htt->ar;
	struct ath10k_hw_params *hw = &ar->hw_params;
	u32 paddr;
	struct sk_buff *frag_buf;
	struct sk_buff *prev_frag_buf;
	u8 last_frag;
	struct htt_rx_in_ord_msdu_desc *ind_desc = *msdu_desc;
	struct htt_rx_desc *rxd;
	int amsdu_len = __le16_to_cpu(ind_desc->msdu_len);

	rxd = HTT_RX_BUF_TO_RX_DESC(hw, msdu->data);
	trace_ath10k_htt_rx_desc(ar, rxd, hw->rx_desc_ops->rx_desc_size);

	skb_put(msdu, hw->rx_desc_ops->rx_desc_size);
	skb_pull(msdu, hw->rx_desc_ops->rx_desc_size);
	skb_put(msdu, min(amsdu_len, ath10k_htt_rx_msdu_size(hw)));
	amsdu_len -= msdu->len;

	last_frag = ind_desc->reserved;
	if (last_frag) {
		if (amsdu_len) {
			ath10k_warn(ar, "invalid amsdu len %u, left %d",
				    __le16_to_cpu(ind_desc->msdu_len),
				    amsdu_len);
		}
		return 0;
	}

	ind_desc++;
	paddr = __le32_to_cpu(ind_desc->msdu_paddr);
	frag_buf = ath10k_htt_rx_pop_paddr(htt, paddr);
	if (!frag_buf) {
		ath10k_warn(ar, "failed to pop frag-1 paddr: 0x%x", paddr);
		return -ENOENT;
	}

	skb_put(frag_buf, min(amsdu_len, HTT_RX_BUF_SIZE));
	ath10k_htt_append_frag_list(msdu, frag_buf, amsdu_len);

	amsdu_len -= frag_buf->len;
	prev_frag_buf = frag_buf;
	last_frag = ind_desc->reserved;
	while (!last_frag) {
		ind_desc++;
		paddr = __le32_to_cpu(ind_desc->msdu_paddr);
		frag_buf = ath10k_htt_rx_pop_paddr(htt, paddr);
		if (!frag_buf) {
			ath10k_warn(ar, "failed to pop frag-n paddr: 0x%x",
				    paddr);
			prev_frag_buf->next = NULL;
			return -ENOENT;
		}

		skb_put(frag_buf, min(amsdu_len, HTT_RX_BUF_SIZE));
		last_frag = ind_desc->reserved;
		amsdu_len -= frag_buf->len;

		prev_frag_buf->next = frag_buf;
		prev_frag_buf = frag_buf;
	}

	if (amsdu_len) {
		ath10k_warn(ar, "invalid amsdu len %u, left %d",
			    __le16_to_cpu(ind_desc->msdu_len), amsdu_len);
	}

	*msdu_desc = ind_desc;

	prev_frag_buf->next = NULL;
	return 0;
}

static int
ath10k_htt_rx_handle_amsdu_mon_64(struct ath10k_htt *htt,
				  struct sk_buff *msdu,
				  struct htt_rx_in_ord_msdu_desc_ext **msdu_desc)
{
	struct ath10k *ar = htt->ar;
	struct ath10k_hw_params *hw = &ar->hw_params;
	u64 paddr;
	struct sk_buff *frag_buf;
	struct sk_buff *prev_frag_buf;
	u8 last_frag;
	struct htt_rx_in_ord_msdu_desc_ext *ind_desc = *msdu_desc;
	struct htt_rx_desc *rxd;
	int amsdu_len = __le16_to_cpu(ind_desc->msdu_len);

	rxd = HTT_RX_BUF_TO_RX_DESC(hw, msdu->data);
	trace_ath10k_htt_rx_desc(ar, rxd, hw->rx_desc_ops->rx_desc_size);

	skb_put(msdu, hw->rx_desc_ops->rx_desc_size);
	skb_pull(msdu, hw->rx_desc_ops->rx_desc_size);
	skb_put(msdu, min(amsdu_len, ath10k_htt_rx_msdu_size(hw)));
	amsdu_len -= msdu->len;

	last_frag = ind_desc->reserved;
	if (last_frag) {
		if (amsdu_len) {
			ath10k_warn(ar, "invalid amsdu len %u, left %d",
				    __le16_to_cpu(ind_desc->msdu_len),
				    amsdu_len);
		}
		return 0;
	}

	ind_desc++;
	paddr = __le64_to_cpu(ind_desc->msdu_paddr);
	frag_buf = ath10k_htt_rx_pop_paddr(htt, paddr);
	if (!frag_buf) {
		ath10k_warn(ar, "failed to pop frag-1 paddr: 0x%llx", paddr);
		return -ENOENT;
	}

	skb_put(frag_buf, min(amsdu_len, HTT_RX_BUF_SIZE));
	ath10k_htt_append_frag_list(msdu, frag_buf, amsdu_len);

	amsdu_len -= frag_buf->len;
	prev_frag_buf = frag_buf;
	last_frag = ind_desc->reserved;
	while (!last_frag) {
		ind_desc++;
		paddr = __le64_to_cpu(ind_desc->msdu_paddr);
		frag_buf = ath10k_htt_rx_pop_paddr(htt, paddr);
		if (!frag_buf) {
			ath10k_warn(ar, "failed to pop frag-n paddr: 0x%llx",
				    paddr);
			prev_frag_buf->next = NULL;
			return -ENOENT;
		}

		skb_put(frag_buf, min(amsdu_len, HTT_RX_BUF_SIZE));
		last_frag = ind_desc->reserved;
		amsdu_len -= frag_buf->len;

		prev_frag_buf->next = frag_buf;
		prev_frag_buf = frag_buf;
	}

	if (amsdu_len) {
		ath10k_warn(ar, "invalid amsdu len %u, left %d",
			    __le16_to_cpu(ind_desc->msdu_len), amsdu_len);
	}

	*msdu_desc = ind_desc;

	prev_frag_buf->next = NULL;
	return 0;
}

static int ath10k_htt_rx_pop_paddr32_list(struct ath10k_htt *htt,
					  struct htt_rx_in_ord_ind *ev,
					  struct sk_buff_head *list)
{
	struct ath10k *ar = htt->ar;
	struct ath10k_hw_params *hw = &ar->hw_params;
	struct htt_rx_in_ord_msdu_desc *msdu_desc = ev->msdu_descs32;
	struct htt_rx_desc *rxd;
	struct rx_attention *rxd_attention;
	struct sk_buff *msdu;
	int msdu_count, ret;
	bool is_offload;
	u32 paddr;

	lockdep_assert_held(&htt->rx_ring.lock);

	msdu_count = __le16_to_cpu(ev->msdu_count);
	is_offload = !!(ev->info & HTT_RX_IN_ORD_IND_INFO_OFFLOAD_MASK);

	while (msdu_count--) {
		paddr = __le32_to_cpu(msdu_desc->msdu_paddr);

		msdu = ath10k_htt_rx_pop_paddr(htt, paddr);
		if (!msdu) {
			__skb_queue_purge(list);
			return -ENOENT;
		}

		if (!is_offload && ar->monitor_arvif) {
			ret = ath10k_htt_rx_handle_amsdu_mon_32(htt, msdu,
								&msdu_desc);
			if (ret) {
				__skb_queue_purge(list);
				return ret;
			}
			__skb_queue_tail(list, msdu);
			msdu_desc++;
			continue;
		}

		__skb_queue_tail(list, msdu);

		if (!is_offload) {
			rxd = HTT_RX_BUF_TO_RX_DESC(hw, msdu->data);
			rxd_attention = ath10k_htt_rx_desc_get_attention(hw, rxd);

			trace_ath10k_htt_rx_desc(ar, rxd, hw->rx_desc_ops->rx_desc_size);

			skb_put(msdu, hw->rx_desc_ops->rx_desc_size);
			skb_pull(msdu, hw->rx_desc_ops->rx_desc_size);
			skb_put(msdu, __le16_to_cpu(msdu_desc->msdu_len));

			if (!(__le32_to_cpu(rxd_attention->flags) &
			      RX_ATTENTION_FLAGS_MSDU_DONE)) {
				ath10k_warn(htt->ar, "tried to pop an incomplete frame, oops!\n");
				return -EIO;
			}
		}

		msdu_desc++;
	}

	return 0;
}

static int ath10k_htt_rx_pop_paddr64_list(struct ath10k_htt *htt,
					  struct htt_rx_in_ord_ind *ev,
					  struct sk_buff_head *list)
{
	struct ath10k *ar = htt->ar;
	struct ath10k_hw_params *hw = &ar->hw_params;
	struct htt_rx_in_ord_msdu_desc_ext *msdu_desc = ev->msdu_descs64;
	struct htt_rx_desc *rxd;
	struct rx_attention *rxd_attention;
	struct sk_buff *msdu;
	int msdu_count, ret;
	bool is_offload;
	u64 paddr;

	lockdep_assert_held(&htt->rx_ring.lock);

	msdu_count = __le16_to_cpu(ev->msdu_count);
	is_offload = !!(ev->info & HTT_RX_IN_ORD_IND_INFO_OFFLOAD_MASK);

	while (msdu_count--) {
		paddr = __le64_to_cpu(msdu_desc->msdu_paddr);
		msdu = ath10k_htt_rx_pop_paddr(htt, paddr);
		if (!msdu) {
			__skb_queue_purge(list);
			return -ENOENT;
		}

		if (!is_offload && ar->monitor_arvif) {
			ret = ath10k_htt_rx_handle_amsdu_mon_64(htt, msdu,
								&msdu_desc);
			if (ret) {
				__skb_queue_purge(list);
				return ret;
			}
			__skb_queue_tail(list, msdu);
			msdu_desc++;
			continue;
		}

		__skb_queue_tail(list, msdu);

		if (!is_offload) {
			rxd = HTT_RX_BUF_TO_RX_DESC(hw, msdu->data);
			rxd_attention = ath10k_htt_rx_desc_get_attention(hw, rxd);

			trace_ath10k_htt_rx_desc(ar, rxd, hw->rx_desc_ops->rx_desc_size);

			skb_put(msdu, hw->rx_desc_ops->rx_desc_size);
			skb_pull(msdu, hw->rx_desc_ops->rx_desc_size);
			skb_put(msdu, __le16_to_cpu(msdu_desc->msdu_len));

			if (!(__le32_to_cpu(rxd_attention->flags) &
			      RX_ATTENTION_FLAGS_MSDU_DONE)) {
				ath10k_warn(htt->ar, "tried to pop an incomplete frame, oops!\n");
				return -EIO;
			}
		}

		msdu_desc++;
	}

	return 0;
}

int ath10k_htt_rx_alloc(struct ath10k_htt *htt)
{
	struct ath10k *ar = htt->ar;
	dma_addr_t paddr;
	void *vaddr, *vaddr_ring;
	size_t size;
	struct timer_list *timer = &htt->rx_ring.refill_retry_timer;

	if (ar->bus_param.dev_type == ATH10K_DEV_TYPE_HL)
		return 0;

	htt->rx_confused = false;

	/* XXX: The fill level could be changed during runtime in response to
	 * the host processing latency. Is this really worth it?
	 */
	htt->rx_ring.size = HTT_RX_RING_SIZE;
	htt->rx_ring.size_mask = htt->rx_ring.size - 1;
	htt->rx_ring.fill_level = ar->hw_params.rx_ring_fill_level;

	if (!is_power_of_2(htt->rx_ring.size)) {
		ath10k_warn(ar, "htt rx ring size is not power of 2\n");
		return -EINVAL;
	}

	htt->rx_ring.netbufs_ring =
		kcalloc(htt->rx_ring.size, sizeof(struct sk_buff *),
			GFP_KERNEL);
	if (!htt->rx_ring.netbufs_ring)
		goto err_netbuf;

	size = ath10k_htt_get_rx_ring_size(htt);

	vaddr_ring = dma_alloc_coherent(htt->ar->dev, size, &paddr, GFP_KERNEL);
	if (!vaddr_ring)
		goto err_dma_ring;

	ath10k_htt_config_paddrs_ring(htt, vaddr_ring);
	htt->rx_ring.base_paddr = paddr;

	vaddr = dma_alloc_coherent(htt->ar->dev,
				   sizeof(*htt->rx_ring.alloc_idx.vaddr),
				   &paddr, GFP_KERNEL);
	if (!vaddr)
		goto err_dma_idx;

	htt->rx_ring.alloc_idx.vaddr = vaddr;
	htt->rx_ring.alloc_idx.paddr = paddr;
	htt->rx_ring.sw_rd_idx.msdu_payld = htt->rx_ring.size_mask;
	*htt->rx_ring.alloc_idx.vaddr = 0;

	/* Initialize the Rx refill retry timer */
	timer_setup(timer, ath10k_htt_rx_ring_refill_retry, 0);

	spin_lock_init(&htt->rx_ring.lock);

	htt->rx_ring.fill_cnt = 0;
	htt->rx_ring.sw_rd_idx.msdu_payld = 0;
	hash_init(htt->rx_ring.skb_table);

	skb_queue_head_init(&htt->rx_msdus_q);
	skb_queue_head_init(&htt->rx_in_ord_compl_q);
	skb_queue_head_init(&htt->tx_fetch_ind_q);
	atomic_set(&htt->num_mpdus_ready, 0);

	ath10k_dbg(ar, ATH10K_DBG_BOOT, "htt rx ring size %d fill_level %d\n",
		   htt->rx_ring.size, htt->rx_ring.fill_level);
	return 0;

err_dma_idx:
	dma_free_coherent(htt->ar->dev,
			  ath10k_htt_get_rx_ring_size(htt),
			  vaddr_ring,
			  htt->rx_ring.base_paddr);
err_dma_ring:
	kfree(htt->rx_ring.netbufs_ring);
err_netbuf:
	return -ENOMEM;
}

static int ath10k_htt_rx_crypto_param_len(struct ath10k *ar,
					  enum htt_rx_mpdu_encrypt_type type)
{
	switch (type) {
	case HTT_RX_MPDU_ENCRYPT_NONE:
		return 0;
	case HTT_RX_MPDU_ENCRYPT_WEP40:
	case HTT_RX_MPDU_ENCRYPT_WEP104:
		return IEEE80211_WEP_IV_LEN;
	case HTT_RX_MPDU_ENCRYPT_TKIP_WITHOUT_MIC:
	case HTT_RX_MPDU_ENCRYPT_TKIP_WPA:
		return IEEE80211_TKIP_IV_LEN;
	case HTT_RX_MPDU_ENCRYPT_AES_CCM_WPA2:
		return IEEE80211_CCMP_HDR_LEN;
	case HTT_RX_MPDU_ENCRYPT_AES_CCM256_WPA2:
		return IEEE80211_CCMP_256_HDR_LEN;
	case HTT_RX_MPDU_ENCRYPT_AES_GCMP_WPA2:
	case HTT_RX_MPDU_ENCRYPT_AES_GCMP256_WPA2:
		return IEEE80211_GCMP_HDR_LEN;
	case HTT_RX_MPDU_ENCRYPT_WEP128:
	case HTT_RX_MPDU_ENCRYPT_WAPI:
		break;
	}

	ath10k_warn(ar, "unsupported encryption type %d\n", type);
	return 0;
}

#define MICHAEL_MIC_LEN 8

static int ath10k_htt_rx_crypto_mic_len(struct ath10k *ar,
					enum htt_rx_mpdu_encrypt_type type)
{
	switch (type) {
	case HTT_RX_MPDU_ENCRYPT_NONE:
	case HTT_RX_MPDU_ENCRYPT_WEP40:
	case HTT_RX_MPDU_ENCRYPT_WEP104:
	case HTT_RX_MPDU_ENCRYPT_TKIP_WITHOUT_MIC:
	case HTT_RX_MPDU_ENCRYPT_TKIP_WPA:
		return 0;
	case HTT_RX_MPDU_ENCRYPT_AES_CCM_WPA2:
		return IEEE80211_CCMP_MIC_LEN;
	case HTT_RX_MPDU_ENCRYPT_AES_CCM256_WPA2:
		return IEEE80211_CCMP_256_MIC_LEN;
	case HTT_RX_MPDU_ENCRYPT_AES_GCMP_WPA2:
	case HTT_RX_MPDU_ENCRYPT_AES_GCMP256_WPA2:
		return IEEE80211_GCMP_MIC_LEN;
	case HTT_RX_MPDU_ENCRYPT_WEP128:
	case HTT_RX_MPDU_ENCRYPT_WAPI:
		break;
	}

	ath10k_warn(ar, "unsupported encryption type %d\n", type);
	return 0;
}

static int ath10k_htt_rx_crypto_icv_len(struct ath10k *ar,
					enum htt_rx_mpdu_encrypt_type type)
{
	switch (type) {
	case HTT_RX_MPDU_ENCRYPT_NONE:
	case HTT_RX_MPDU_ENCRYPT_AES_CCM_WPA2:
	case HTT_RX_MPDU_ENCRYPT_AES_CCM256_WPA2:
	case HTT_RX_MPDU_ENCRYPT_AES_GCMP_WPA2:
	case HTT_RX_MPDU_ENCRYPT_AES_GCMP256_WPA2:
		return 0;
	case HTT_RX_MPDU_ENCRYPT_WEP40:
	case HTT_RX_MPDU_ENCRYPT_WEP104:
		return IEEE80211_WEP_ICV_LEN;
	case HTT_RX_MPDU_ENCRYPT_TKIP_WITHOUT_MIC:
	case HTT_RX_MPDU_ENCRYPT_TKIP_WPA:
		return IEEE80211_TKIP_ICV_LEN;
	case HTT_RX_MPDU_ENCRYPT_WEP128:
	case HTT_RX_MPDU_ENCRYPT_WAPI:
		break;
	}

	ath10k_warn(ar, "unsupported encryption type %d\n", type);
	return 0;
}

struct amsdu_subframe_hdr {
	u8 dst[ETH_ALEN];
	u8 src[ETH_ALEN];
	__be16 len;
} __packed;

#define GROUP_ID_IS_SU_MIMO(x) ((x) == 0 || (x) == 63)

static inline u8 ath10k_bw_to_mac80211_bw(u8 bw)
{
	u8 ret = 0;

	switch (bw) {
	case 0:
		ret = RATE_INFO_BW_20;
		break;
	case 1:
		ret = RATE_INFO_BW_40;
		break;
	case 2:
		ret = RATE_INFO_BW_80;
		break;
	case 3:
		ret = RATE_INFO_BW_160;
		break;
	}

	return ret;
}

static void ath10k_htt_rx_h_rates(struct ath10k *ar,
				  struct ieee80211_rx_status *status,
				  struct htt_rx_desc *rxd)
{
	struct ath10k_hw_params *hw = &ar->hw_params;
	struct rx_attention *rxd_attention;
	struct rx_mpdu_start *rxd_mpdu_start;
	struct rx_mpdu_end *rxd_mpdu_end;
	struct rx_msdu_start_common *rxd_msdu_start_common;
	struct rx_msdu_end_common *rxd_msdu_end_common;
	struct rx_ppdu_start *rxd_ppdu_start;
	struct ieee80211_supported_band *sband;
	u8 cck, rate, bw, sgi, mcs, nss;
	u8 *rxd_msdu_payload;
	u8 preamble = 0;
	u8 group_id;
	u32 info1, info2, info3;
	u32 stbc, nsts_su;

	rxd_attention = ath10k_htt_rx_desc_get_attention(hw, rxd);
	rxd_mpdu_start = ath10k_htt_rx_desc_get_mpdu_start(hw, rxd);
	rxd_mpdu_end = ath10k_htt_rx_desc_get_mpdu_end(hw, rxd);
	rxd_msdu_start_common = ath10k_htt_rx_desc_get_msdu_start(hw, rxd);
	rxd_msdu_end_common = ath10k_htt_rx_desc_get_msdu_end(hw, rxd);
	rxd_ppdu_start = ath10k_htt_rx_desc_get_ppdu_start(hw, rxd);
	rxd_msdu_payload = ath10k_htt_rx_desc_get_msdu_payload(hw, rxd);

	info1 = __le32_to_cpu(rxd_ppdu_start->info1);
	info2 = __le32_to_cpu(rxd_ppdu_start->info2);
	info3 = __le32_to_cpu(rxd_ppdu_start->info3);

	preamble = MS(info1, RX_PPDU_START_INFO1_PREAMBLE_TYPE);

	switch (preamble) {
	case HTT_RX_LEGACY:
		/* To get legacy rate index band is required. Since band can't
		 * be undefined check if freq is non-zero.
		 */
		if (!status->freq)
			return;

		cck = info1 & RX_PPDU_START_INFO1_L_SIG_RATE_SELECT;
		rate = MS(info1, RX_PPDU_START_INFO1_L_SIG_RATE);
		rate &= ~RX_PPDU_START_RATE_FLAG;

		sband = &ar->mac.sbands[status->band];
		status->rate_idx = ath10k_mac_hw_rate_to_idx(sband, rate, cck);
		break;
	case HTT_RX_HT:
	case HTT_RX_HT_WITH_TXBF:
		/* HT-SIG - Table 20-11 in info2 and info3 */
		mcs = info2 & 0x1F;
		nss = mcs >> 3;
		bw = (info2 >> 7) & 1;
		sgi = (info3 >> 7) & 1;

		status->rate_idx = mcs;
		status->encoding = RX_ENC_HT;
		if (sgi)
			status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		if (bw)
			status->bw = RATE_INFO_BW_40;
		break;
	case HTT_RX_VHT:
	case HTT_RX_VHT_WITH_TXBF:
		/* VHT-SIG-A1 in info2, VHT-SIG-A2 in info3
		 * TODO check this
		 */
		bw = info2 & 3;
		sgi = info3 & 1;
		stbc = (info2 >> 3) & 1;
		group_id = (info2 >> 4) & 0x3F;

		if (GROUP_ID_IS_SU_MIMO(group_id)) {
			mcs = (info3 >> 4) & 0x0F;
			nsts_su = ((info2 >> 10) & 0x07);
			if (stbc)
				nss = (nsts_su >> 2) + 1;
			else
				nss = (nsts_su + 1);
		} else {
			/* Hardware doesn't decode VHT-SIG-B into Rx descriptor
			 * so it's impossible to decode MCS. Also since
			 * firmware consumes Group Id Management frames host
			 * has no knowledge regarding group/user position
			 * mapping so it's impossible to pick the correct Nsts
			 * from VHT-SIG-A1.
			 *
			 * Bandwidth and SGI are valid so report the rateinfo
			 * on best-effort basis.
			 */
			mcs = 0;
			nss = 1;
		}

		if (mcs > 0x09) {
			ath10k_warn(ar, "invalid MCS received %u\n", mcs);
			ath10k_warn(ar, "rxd %08x mpdu start %08x %08x msdu start %08x %08x ppdu start %08x %08x %08x %08x %08x\n",
				    __le32_to_cpu(rxd_attention->flags),
				    __le32_to_cpu(rxd_mpdu_start->info0),
				    __le32_to_cpu(rxd_mpdu_start->info1),
				    __le32_to_cpu(rxd_msdu_start_common->info0),
				    __le32_to_cpu(rxd_msdu_start_common->info1),
				    rxd_ppdu_start->info0,
				    __le32_to_cpu(rxd_ppdu_start->info1),
				    __le32_to_cpu(rxd_ppdu_start->info2),
				    __le32_to_cpu(rxd_ppdu_start->info3),
				    __le32_to_cpu(rxd_ppdu_start->info4));

			ath10k_warn(ar, "msdu end %08x mpdu end %08x\n",
				    __le32_to_cpu(rxd_msdu_end_common->info0),
				    __le32_to_cpu(rxd_mpdu_end->info0));

			ath10k_dbg_dump(ar, ATH10K_DBG_HTT_DUMP, NULL,
					"rx desc msdu payload: ",
					rxd_msdu_payload, 50);
		}

		status->rate_idx = mcs;
		status->nss = nss;

		if (sgi)
			status->enc_flags |= RX_ENC_FLAG_SHORT_GI;

		status->bw = ath10k_bw_to_mac80211_bw(bw);
		status->encoding = RX_ENC_VHT;
		break;
	default:
		break;
	}
}

static struct ieee80211_channel *
ath10k_htt_rx_h_peer_channel(struct ath10k *ar, struct htt_rx_desc *rxd)
{
	struct ath10k_hw_params *hw = &ar->hw_params;
	struct rx_attention *rxd_attention;
	struct rx_msdu_end_common *rxd_msdu_end_common;
	struct rx_mpdu_start *rxd_mpdu_start;
	struct ath10k_peer *peer;
	struct ath10k_vif *arvif;
	struct cfg80211_chan_def def;
	u16 peer_id;

	lockdep_assert_held(&ar->data_lock);

	if (!rxd)
		return NULL;

	rxd_attention = ath10k_htt_rx_desc_get_attention(hw, rxd);
	rxd_msdu_end_common = ath10k_htt_rx_desc_get_msdu_end(hw, rxd);
	rxd_mpdu_start = ath10k_htt_rx_desc_get_mpdu_start(hw, rxd);

	if (rxd_attention->flags &
	    __cpu_to_le32(RX_ATTENTION_FLAGS_PEER_IDX_INVALID))
		return NULL;

	if (!(rxd_msdu_end_common->info0 &
	      __cpu_to_le32(RX_MSDU_END_INFO0_FIRST_MSDU)))
		return NULL;

	peer_id = MS(__le32_to_cpu(rxd_mpdu_start->info0),
		     RX_MPDU_START_INFO0_PEER_IDX);

	peer = ath10k_peer_find_by_id(ar, peer_id);
	if (!peer)
		return NULL;

	arvif = ath10k_get_arvif(ar, peer->vdev_id);
	if (WARN_ON_ONCE(!arvif))
		return NULL;

	if (ath10k_mac_vif_chan(arvif->vif, &def))
		return NULL;

	return def.chan;
}

static struct ieee80211_channel *
ath10k_htt_rx_h_vdev_channel(struct ath10k *ar, u32 vdev_id)
{
	struct ath10k_vif *arvif;
	struct cfg80211_chan_def def;

	lockdep_assert_held(&ar->data_lock);

	list_for_each_entry(arvif, &ar->arvifs, list) {
		if (arvif->vdev_id == vdev_id &&
		    ath10k_mac_vif_chan(arvif->vif, &def) == 0)
			return def.chan;
	}

	return NULL;
}

static void
ath10k_htt_rx_h_any_chan_iter(struct ieee80211_hw *hw,
			      struct ieee80211_chanctx_conf *conf,
			      void *data)
{
	struct cfg80211_chan_def *def = data;

	*def = conf->def;
}

static struct ieee80211_channel *
ath10k_htt_rx_h_any_channel(struct ath10k *ar)
{
	struct cfg80211_chan_def def = {};

	ieee80211_iter_chan_contexts_atomic(ar->hw,
					    ath10k_htt_rx_h_any_chan_iter,
					    &def);

	return def.chan;
}

static bool ath10k_htt_rx_h_channel(struct ath10k *ar,
				    struct ieee80211_rx_status *status,
				    struct htt_rx_desc *rxd,
				    u32 vdev_id)
{
	struct ieee80211_channel *ch;

	spin_lock_bh(&ar->data_lock);
	ch = ar->scan_channel;
	if (!ch)
		ch = ar->rx_channel;
	if (!ch)
		ch = ath10k_htt_rx_h_peer_channel(ar, rxd);
	if (!ch)
		ch = ath10k_htt_rx_h_vdev_channel(ar, vdev_id);
	if (!ch)
		ch = ath10k_htt_rx_h_any_channel(ar);
	if (!ch)
		ch = ar->tgt_oper_chan;
	spin_unlock_bh(&ar->data_lock);

	if (!ch)
		return false;

	status->band = ch->band;
	status->freq = ch->center_freq;

	return true;
}

static void ath10k_htt_rx_h_signal(struct ath10k *ar,
				   struct ieee80211_rx_status *status,
				   struct htt_rx_desc *rxd)
{
	struct ath10k_hw_params *hw = &ar->hw_params;
	struct rx_ppdu_start *rxd_ppdu_start = ath10k_htt_rx_desc_get_ppdu_start(hw, rxd);
	int i;

	for (i = 0; i < IEEE80211_MAX_CHAINS ; i++) {
		status->chains &= ~BIT(i);

		if (rxd_ppdu_start->rssi_chains[i].pri20_mhz != 0x80) {
			status->chain_signal[i] = ATH10K_DEFAULT_NOISE_FLOOR +
				rxd_ppdu_start->rssi_chains[i].pri20_mhz;

			status->chains |= BIT(i);
		}
	}

	/* FIXME: Get real NF */
	status->signal = ATH10K_DEFAULT_NOISE_FLOOR +
			 rxd_ppdu_start->rssi_comb;
	status->flag &= ~RX_FLAG_NO_SIGNAL_VAL;
}

static void ath10k_htt_rx_h_mactime(struct ath10k *ar,
				    struct ieee80211_rx_status *status,
				    struct htt_rx_desc *rxd)
{
	struct ath10k_hw_params *hw = &ar->hw_params;
	struct rx_ppdu_end_common *rxd_ppdu_end_common;

	rxd_ppdu_end_common = ath10k_htt_rx_desc_get_ppdu_end(hw, rxd);

	/* FIXME: TSF is known only at the end of PPDU, in the last MPDU. This
	 * means all prior MSDUs in a PPDU are reported to mac80211 without the
	 * TSF. Is it worth holding frames until end of PPDU is known?
	 *
	 * FIXME: Can we get/compute 64bit TSF?
	 */
	status->mactime = __le32_to_cpu(rxd_ppdu_end_common->tsf_timestamp);
	status->flag |= RX_FLAG_MACTIME_END;
}

static void ath10k_htt_rx_h_ppdu(struct ath10k *ar,
				 struct sk_buff_head *amsdu,
				 struct ieee80211_rx_status *status,
				 u32 vdev_id)
{
	struct sk_buff *first;
	struct ath10k_hw_params *hw = &ar->hw_params;
	struct htt_rx_desc *rxd;
	struct rx_attention *rxd_attention;
	bool is_first_ppdu;
	bool is_last_ppdu;

	if (skb_queue_empty(amsdu))
		return;

	first = skb_peek(amsdu);
	rxd = HTT_RX_BUF_TO_RX_DESC(hw,
				    (void *)first->data - hw->rx_desc_ops->rx_desc_size);

	rxd_attention = ath10k_htt_rx_desc_get_attention(hw, rxd);

	is_first_ppdu = !!(rxd_attention->flags &
			   __cpu_to_le32(RX_ATTENTION_FLAGS_FIRST_MPDU));
	is_last_ppdu = !!(rxd_attention->flags &
			  __cpu_to_le32(RX_ATTENTION_FLAGS_LAST_MPDU));

	if (is_first_ppdu) {
		/* New PPDU starts so clear out the old per-PPDU status. */
		status->freq = 0;
		status->rate_idx = 0;
		status->nss = 0;
		status->encoding = RX_ENC_LEGACY;
		status->bw = RATE_INFO_BW_20;

		status->flag &= ~RX_FLAG_MACTIME_END;
		status->flag |= RX_FLAG_NO_SIGNAL_VAL;

		status->flag &= ~(RX_FLAG_AMPDU_IS_LAST);
		status->flag |= RX_FLAG_AMPDU_DETAILS | RX_FLAG_AMPDU_LAST_KNOWN;
		status->ampdu_reference = ar->ampdu_reference;

		ath10k_htt_rx_h_signal(ar, status, rxd);
		ath10k_htt_rx_h_channel(ar, status, rxd, vdev_id);
		ath10k_htt_rx_h_rates(ar, status, rxd);
	}

	if (is_last_ppdu) {
		ath10k_htt_rx_h_mactime(ar, status, rxd);

		/* set ampdu last segment flag */
		status->flag |= RX_FLAG_AMPDU_IS_LAST;
		ar->ampdu_reference++;
	}
}

static const char * const tid_to_ac[] = {
	"BE",
	"BK",
	"BK",
	"BE",
	"VI",
	"VI",
	"VO",
	"VO",
};

static char *ath10k_get_tid(struct ieee80211_hdr *hdr, char *out, size_t size)
{
	u8 *qc;
	int tid;

	if (!ieee80211_is_data_qos(hdr->frame_control))
		return "";

	qc = ieee80211_get_qos_ctl(hdr);
	tid = *qc & IEEE80211_QOS_CTL_TID_MASK;
	if (tid < 8)
		snprintf(out, size, "tid %d (%s)", tid, tid_to_ac[tid]);
	else
		snprintf(out, size, "tid %d", tid);

	return out;
}

static void ath10k_htt_rx_h_queue_msdu(struct ath10k *ar,
				       struct ieee80211_rx_status *rx_status,
				       struct sk_buff *skb)
{
	struct ieee80211_rx_status *status;

	status = IEEE80211_SKB_RXCB(skb);
	*status = *rx_status;

	skb_queue_tail(&ar->htt.rx_msdus_q, skb);
}

static void ath10k_process_rx(struct ath10k *ar, struct sk_buff *skb)
{
	struct ieee80211_rx_status *status;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	char tid[32];

	status = IEEE80211_SKB_RXCB(skb);

	if (!(ar->filter_flags & FIF_FCSFAIL) &&
	    status->flag & RX_FLAG_FAILED_FCS_CRC) {
		ar->stats.rx_crc_err_drop++;
		dev_kfree_skb_any(skb);
		return;
	}

	ath10k_dbg(ar, ATH10K_DBG_DATA,
		   "rx skb %pK len %u peer %pM %s %s sn %u %s%s%s%s%s%s %srate_idx %u vht_nss %u freq %u band %u flag 0x%x fcs-err %i mic-err %i amsdu-more %i\n",
		   skb,
		   skb->len,
		   ieee80211_get_SA(hdr),
		   ath10k_get_tid(hdr, tid, sizeof(tid)),
		   is_multicast_ether_addr(ieee80211_get_DA(hdr)) ?
							"mcast" : "ucast",
		   (__le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4,
		   (status->encoding == RX_ENC_LEGACY) ? "legacy" : "",
		   (status->encoding == RX_ENC_HT) ? "ht" : "",
		   (status->encoding == RX_ENC_VHT) ? "vht" : "",
		   (status->bw == RATE_INFO_BW_40) ? "40" : "",
		   (status->bw == RATE_INFO_BW_80) ? "80" : "",
		   (status->bw == RATE_INFO_BW_160) ? "160" : "",
		   status->enc_flags & RX_ENC_FLAG_SHORT_GI ? "sgi " : "",
		   status->rate_idx,
		   status->nss,
		   status->freq,
		   status->band, status->flag,
		   !!(status->flag & RX_FLAG_FAILED_FCS_CRC),
		   !!(status->flag & RX_FLAG_MMIC_ERROR),
		   !!(status->flag & RX_FLAG_AMSDU_MORE));
	ath10k_dbg_dump(ar, ATH10K_DBG_HTT_DUMP, NULL, "rx skb: ",
			skb->data, skb->len);
	trace_ath10k_rx_hdr(ar, skb->data, skb->len);
	trace_ath10k_rx_payload(ar, skb->data, skb->len);

	ieee80211_rx_napi(ar->hw, NULL, skb, &ar->napi);
}

static int ath10k_htt_rx_nwifi_hdrlen(struct ath10k *ar,
				      struct ieee80211_hdr *hdr)
{
	int len = ieee80211_hdrlen(hdr->frame_control);

	if (!test_bit(ATH10K_FW_FEATURE_NO_NWIFI_DECAP_4ADDR_PADDING,
		      ar->running_fw->fw_file.fw_features))
		len = round_up(len, 4);

	return len;
}

static void ath10k_htt_rx_h_undecap_raw(struct ath10k *ar,
					struct sk_buff *msdu,
					struct ieee80211_rx_status *status,
					enum htt_rx_mpdu_encrypt_type enctype,
					bool is_decrypted,
					const u8 first_hdr[64])
{
	struct ieee80211_hdr *hdr;
	struct ath10k_hw_params *hw = &ar->hw_params;
	struct htt_rx_desc *rxd;
	struct rx_msdu_end_common *rxd_msdu_end_common;
	size_t hdr_len;
	size_t crypto_len;
	bool is_first;
	bool is_last;
	bool msdu_limit_err;
	int bytes_aligned = ar->hw_params.decap_align_bytes;
	u8 *qos;

	rxd = HTT_RX_BUF_TO_RX_DESC(hw,
				    (void *)msdu->data - hw->rx_desc_ops->rx_desc_size);

	rxd_msdu_end_common = ath10k_htt_rx_desc_get_msdu_end(hw, rxd);
	is_first = !!(rxd_msdu_end_common->info0 &
		      __cpu_to_le32(RX_MSDU_END_INFO0_FIRST_MSDU));
	is_last = !!(rxd_msdu_end_common->info0 &
		     __cpu_to_le32(RX_MSDU_END_INFO0_LAST_MSDU));

	/* Delivered decapped frame:
	 * [802.11 header]
	 * [crypto param] <-- can be trimmed if !fcs_err &&
	 *                    !decrypt_err && !peer_idx_invalid
	 * [amsdu header] <-- only if A-MSDU
	 * [rfc1042/llc]
	 * [payload]
	 * [FCS] <-- at end, needs to be trimmed
	 */

	/* Some hardwares(QCA99x0 variants) limit number of msdus in a-msdu when
	 * deaggregate, so that unwanted MSDU-deaggregation is avoided for
	 * error packets. If limit exceeds, hw sends all remaining MSDUs as
	 * a single last MSDU with this msdu limit error set.
	 */
	msdu_limit_err = ath10k_htt_rx_desc_msdu_limit_error(hw, rxd);

	/* If MSDU limit error happens, then don't warn on, the partial raw MSDU
	 * without first MSDU is expected in that case, and handled later here.
	 */
	/* This probably shouldn't happen but warn just in case */
	if (WARN_ON_ONCE(!is_first && !msdu_limit_err))
		return;

	/* This probably shouldn't happen but warn just in case */
	if (WARN_ON_ONCE(!(is_first && is_last) && !msdu_limit_err))
		return;

	skb_trim(msdu, msdu->len - FCS_LEN);

	/* Push original 80211 header */
	if (unlikely(msdu_limit_err)) {
		hdr = (struct ieee80211_hdr *)first_hdr;
		hdr_len = ieee80211_hdrlen(hdr->frame_control);
		crypto_len = ath10k_htt_rx_crypto_param_len(ar, enctype);

		if (ieee80211_is_data_qos(hdr->frame_control)) {
			qos = ieee80211_get_qos_ctl(hdr);
			qos[0] |= IEEE80211_QOS_CTL_A_MSDU_PRESENT;
		}

		if (crypto_len)
			memcpy(skb_push(msdu, crypto_len),
			       (void *)hdr + round_up(hdr_len, bytes_aligned),
			       crypto_len);

		memcpy(skb_push(msdu, hdr_len), hdr, hdr_len);
	}

	/* In most cases this will be true for sniffed frames. It makes sense
	 * to deliver them as-is without stripping the crypto param. This is
	 * necessary for software based decryption.
	 *
	 * If there's no error then the frame is decrypted. At least that is
	 * the case for frames that come in via fragmented rx indication.
	 */
	if (!is_decrypted)
		return;

	/* The payload is decrypted so strip crypto params. Start from tail
	 * since hdr is used to compute some stuff.
	 */

	hdr = (void *)msdu->data;

	/* Tail */
	if (status->flag & RX_FLAG_IV_STRIPPED) {
		skb_trim(msdu, msdu->len -
			 ath10k_htt_rx_crypto_mic_len(ar, enctype));

		skb_trim(msdu, msdu->len -
			 ath10k_htt_rx_crypto_icv_len(ar, enctype));
	} else {
		/* MIC */
		if (status->flag & RX_FLAG_MIC_STRIPPED)
			skb_trim(msdu, msdu->len -
				 ath10k_htt_rx_crypto_mic_len(ar, enctype));

		/* ICV */
		if (status->flag & RX_FLAG_ICV_STRIPPED)
			skb_trim(msdu, msdu->len -
				 ath10k_htt_rx_crypto_icv_len(ar, enctype));
	}

	/* MMIC */
	if ((status->flag & RX_FLAG_MMIC_STRIPPED) &&
	    !ieee80211_has_morefrags(hdr->frame_control) &&
	    enctype == HTT_RX_MPDU_ENCRYPT_TKIP_WPA)
		skb_trim(msdu, msdu->len - MICHAEL_MIC_LEN);

	/* Head */
	if (status->flag & RX_FLAG_IV_STRIPPED) {
		hdr_len = ieee80211_hdrlen(hdr->frame_control);
		crypto_len = ath10k_htt_rx_crypto_param_len(ar, enctype);

		memmove((void *)msdu->data + crypto_len,
			(void *)msdu->data, hdr_len);
		skb_pull(msdu, crypto_len);
	}
}

static void ath10k_htt_rx_h_undecap_nwifi(struct ath10k *ar,
					  struct sk_buff *msdu,
					  struct ieee80211_rx_status *status,
					  const u8 first_hdr[64],
					  enum htt_rx_mpdu_encrypt_type enctype)
{
	struct ath10k_hw_params *hw = &ar->hw_params;
	struct ieee80211_hdr *hdr;
	struct htt_rx_desc *rxd;
	size_t hdr_len;
	u8 da[ETH_ALEN];
	u8 sa[ETH_ALEN];
	int l3_pad_bytes;
	int bytes_aligned = ar->hw_params.decap_align_bytes;

	/* Delivered decapped frame:
	 * [nwifi 802.11 header] <-- replaced with 802.11 hdr
	 * [rfc1042/llc]
	 *
	 * Note: The nwifi header doesn't have QoS Control and is
	 * (always?) a 3addr frame.
	 *
	 * Note2: There's no A-MSDU subframe header. Even if it's part
	 * of an A-MSDU.
	 */

	/* pull decapped header and copy SA & DA */
	rxd = HTT_RX_BUF_TO_RX_DESC(hw, (void *)msdu->data -
				    hw->rx_desc_ops->rx_desc_size);

	l3_pad_bytes = ath10k_htt_rx_desc_get_l3_pad_bytes(&ar->hw_params, rxd);
	skb_put(msdu, l3_pad_bytes);

	hdr = (struct ieee80211_hdr *)(msdu->data + l3_pad_bytes);

	hdr_len = ath10k_htt_rx_nwifi_hdrlen(ar, hdr);
	ether_addr_copy(da, ieee80211_get_DA(hdr));
	ether_addr_copy(sa, ieee80211_get_SA(hdr));
	skb_pull(msdu, hdr_len);

	/* push original 802.11 header */
	hdr = (struct ieee80211_hdr *)first_hdr;
	hdr_len = ieee80211_hdrlen(hdr->frame_control);

	if (!(status->flag & RX_FLAG_IV_STRIPPED)) {
		memcpy(skb_push(msdu,
				ath10k_htt_rx_crypto_param_len(ar, enctype)),
		       (void *)hdr + round_up(hdr_len, bytes_aligned),
			ath10k_htt_rx_crypto_param_len(ar, enctype));
	}

	memcpy(skb_push(msdu, hdr_len), hdr, hdr_len);

	/* original 802.11 header has a different DA and in
	 * case of 4addr it may also have different SA
	 */
	hdr = (struct ieee80211_hdr *)msdu->data;
	ether_addr_copy(ieee80211_get_DA(hdr), da);
	ether_addr_copy(ieee80211_get_SA(hdr), sa);
}

static void *ath10k_htt_rx_h_find_rfc1042(struct ath10k *ar,
					  struct sk_buff *msdu,
					  enum htt_rx_mpdu_encrypt_type enctype)
{
	struct ieee80211_hdr *hdr;
	struct ath10k_hw_params *hw = &ar->hw_params;
	struct htt_rx_desc *rxd;
	struct rx_msdu_end_common *rxd_msdu_end_common;
	u8 *rxd_rx_hdr_status;
	size_t hdr_len, crypto_len;
	void *rfc1042;
	bool is_first, is_last, is_amsdu;
	int bytes_aligned = ar->hw_params.decap_align_bytes;

	rxd = HTT_RX_BUF_TO_RX_DESC(hw,
				    (void *)msdu->data - hw->rx_desc_ops->rx_desc_size);

	rxd_msdu_end_common = ath10k_htt_rx_desc_get_msdu_end(hw, rxd);
	rxd_rx_hdr_status = ath10k_htt_rx_desc_get_rx_hdr_status(hw, rxd);
	hdr = (void *)rxd_rx_hdr_status;

	is_first = !!(rxd_msdu_end_common->info0 &
		      __cpu_to_le32(RX_MSDU_END_INFO0_FIRST_MSDU));
	is_last = !!(rxd_msdu_end_common->info0 &
		     __cpu_to_le32(RX_MSDU_END_INFO0_LAST_MSDU));
	is_amsdu = !(is_first && is_last);

	rfc1042 = hdr;

	if (is_first) {
		hdr_len = ieee80211_hdrlen(hdr->frame_control);
		crypto_len = ath10k_htt_rx_crypto_param_len(ar, enctype);

		rfc1042 += round_up(hdr_len, bytes_aligned) +
			   round_up(crypto_len, bytes_aligned);
	}

	if (is_amsdu)
		rfc1042 += sizeof(struct amsdu_subframe_hdr);

	return rfc1042;
}

static void ath10k_htt_rx_h_undecap_eth(struct ath10k *ar,
					struct sk_buff *msdu,
					struct ieee80211_rx_status *status,
					const u8 first_hdr[64],
					enum htt_rx_mpdu_encrypt_type enctype)
{
	struct ath10k_hw_params *hw = &ar->hw_params;
	struct ieee80211_hdr *hdr;
	struct ethhdr *eth;
	size_t hdr_len;
	void *rfc1042;
	u8 da[ETH_ALEN];
	u8 sa[ETH_ALEN];
	int l3_pad_bytes;
	struct htt_rx_desc *rxd;
	int bytes_aligned = ar->hw_params.decap_align_bytes;

	/* Delivered decapped frame:
	 * [eth header] <-- replaced with 802.11 hdr & rfc1042/llc
	 * [payload]
	 */

	rfc1042 = ath10k_htt_rx_h_find_rfc1042(ar, msdu, enctype);
	if (WARN_ON_ONCE(!rfc1042))
		return;

	rxd = HTT_RX_BUF_TO_RX_DESC(hw,
				    (void *)msdu->data - hw->rx_desc_ops->rx_desc_size);

	l3_pad_bytes = ath10k_htt_rx_desc_get_l3_pad_bytes(&ar->hw_params, rxd);
	skb_put(msdu, l3_pad_bytes);
	skb_pull(msdu, l3_pad_bytes);

	/* pull decapped header and copy SA & DA */
	eth = (struct ethhdr *)msdu->data;
	ether_addr_copy(da, eth->h_dest);
	ether_addr_copy(sa, eth->h_source);
	skb_pull(msdu, sizeof(struct ethhdr));

	/* push rfc1042/llc/snap */
	memcpy(skb_push(msdu, sizeof(struct rfc1042_hdr)), rfc1042,
	       sizeof(struct rfc1042_hdr));

	/* push original 802.11 header */
	hdr = (struct ieee80211_hdr *)first_hdr;
	hdr_len = ieee80211_hdrlen(hdr->frame_control);

	if (!(status->flag & RX_FLAG_IV_STRIPPED)) {
		memcpy(skb_push(msdu,
				ath10k_htt_rx_crypto_param_len(ar, enctype)),
		       (void *)hdr + round_up(hdr_len, bytes_aligned),
			ath10k_htt_rx_crypto_param_len(ar, enctype));
	}

	memcpy(skb_push(msdu, hdr_len), hdr, hdr_len);

	/* original 802.11 header has a different DA and in
	 * case of 4addr it may also have different SA
	 */
	hdr = (struct ieee80211_hdr *)msdu->data;
	ether_addr_copy(ieee80211_get_DA(hdr), da);
	ether_addr_copy(ieee80211_get_SA(hdr), sa);
}

static void ath10k_htt_rx_h_undecap_snap(struct ath10k *ar,
					 struct sk_buff *msdu,
					 struct ieee80211_rx_status *status,
					 const u8 first_hdr[64],
					 enum htt_rx_mpdu_encrypt_type enctype)
{
	struct ath10k_hw_params *hw = &ar->hw_params;
	struct ieee80211_hdr *hdr;
	size_t hdr_len;
	int l3_pad_bytes;
	struct htt_rx_desc *rxd;
	int bytes_aligned = ar->hw_params.decap_align_bytes;

	/* Delivered decapped frame:
	 * [amsdu header] <-- replaced with 802.11 hdr
	 * [rfc1042/llc]
	 * [payload]
	 */

	rxd = HTT_RX_BUF_TO_RX_DESC(hw,
				    (void *)msdu->data - hw->rx_desc_ops->rx_desc_size);

	l3_pad_bytes = ath10k_htt_rx_desc_get_l3_pad_bytes(&ar->hw_params, rxd);

	skb_put(msdu, l3_pad_bytes);
	skb_pull(msdu, sizeof(struct amsdu_subframe_hdr) + l3_pad_bytes);

	hdr = (struct ieee80211_hdr *)first_hdr;
	hdr_len = ieee80211_hdrlen(hdr->frame_control);

	if (!(status->flag & RX_FLAG_IV_STRIPPED)) {
		memcpy(skb_push(msdu,
				ath10k_htt_rx_crypto_param_len(ar, enctype)),
		       (void *)hdr + round_up(hdr_len, bytes_aligned),
			ath10k_htt_rx_crypto_param_len(ar, enctype));
	}

	memcpy(skb_push(msdu, hdr_len), hdr, hdr_len);
}

static void ath10k_htt_rx_h_undecap(struct ath10k *ar,
				    struct sk_buff *msdu,
				    struct ieee80211_rx_status *status,
				    u8 first_hdr[64],
				    enum htt_rx_mpdu_encrypt_type enctype,
				    bool is_decrypted)
{
	struct ath10k_hw_params *hw = &ar->hw_params;
	struct htt_rx_desc *rxd;
	struct rx_msdu_start_common *rxd_msdu_start_common;
	enum rx_msdu_decap_format decap;

	/* First msdu's decapped header:
	 * [802.11 header] <-- padded to 4 bytes long
	 * [crypto param] <-- padded to 4 bytes long
	 * [amsdu header] <-- only if A-MSDU
	 * [rfc1042/llc]
	 *
	 * Other (2nd, 3rd, ..) msdu's decapped header:
	 * [amsdu header] <-- only if A-MSDU
	 * [rfc1042/llc]
	 */

	rxd = HTT_RX_BUF_TO_RX_DESC(hw,
				    (void *)msdu->data - hw->rx_desc_ops->rx_desc_size);

	rxd_msdu_start_common = ath10k_htt_rx_desc_get_msdu_start(hw, rxd);
	decap = MS(__le32_to_cpu(rxd_msdu_start_common->info1),
		   RX_MSDU_START_INFO1_DECAP_FORMAT);

	switch (decap) {
	case RX_MSDU_DECAP_RAW:
		ath10k_htt_rx_h_undecap_raw(ar, msdu, status, enctype,
					    is_decrypted, first_hdr);
		break;
	case RX_MSDU_DECAP_NATIVE_WIFI:
		ath10k_htt_rx_h_undecap_nwifi(ar, msdu, status, first_hdr,
					      enctype);
		break;
	case RX_MSDU_DECAP_ETHERNET2_DIX:
		ath10k_htt_rx_h_undecap_eth(ar, msdu, status, first_hdr, enctype);
		break;
	case RX_MSDU_DECAP_8023_SNAP_LLC:
		ath10k_htt_rx_h_undecap_snap(ar, msdu, status, first_hdr,
					     enctype);
		break;
	}
}

static int ath10k_htt_rx_get_csum_state(struct ath10k_hw_params *hw, struct sk_buff *skb)
{
	struct htt_rx_desc *rxd;
	struct rx_attention *rxd_attention;
	struct rx_msdu_start_common *rxd_msdu_start_common;
	u32 flags, info;
	bool is_ip4, is_ip6;
	bool is_tcp, is_udp;
	bool ip_csum_ok, tcpudp_csum_ok;

	rxd = HTT_RX_BUF_TO_RX_DESC(hw,
				    (void *)skb->data - hw->rx_desc_ops->rx_desc_size);

	rxd_attention = ath10k_htt_rx_desc_get_attention(hw, rxd);
	rxd_msdu_start_common = ath10k_htt_rx_desc_get_msdu_start(hw, rxd);
	flags = __le32_to_cpu(rxd_attention->flags);
	info = __le32_to_cpu(rxd_msdu_start_common->info1);

	is_ip4 = !!(info & RX_MSDU_START_INFO1_IPV4_PROTO);
	is_ip6 = !!(info & RX_MSDU_START_INFO1_IPV6_PROTO);
	is_tcp = !!(info & RX_MSDU_START_INFO1_TCP_PROTO);
	is_udp = !!(info & RX_MSDU_START_INFO1_UDP_PROTO);
	ip_csum_ok = !(flags & RX_ATTENTION_FLAGS_IP_CHKSUM_FAIL);
	tcpudp_csum_ok = !(flags & RX_ATTENTION_FLAGS_TCP_UDP_CHKSUM_FAIL);

	if (!is_ip4 && !is_ip6)
		return CHECKSUM_NONE;
	if (!is_tcp && !is_udp)
		return CHECKSUM_NONE;
	if (!ip_csum_ok)
		return CHECKSUM_NONE;
	if (!tcpudp_csum_ok)
		return CHECKSUM_NONE;

	return CHECKSUM_UNNECESSARY;
}

static void ath10k_htt_rx_h_csum_offload(struct ath10k_hw_params *hw,
					 struct sk_buff *msdu)
{
	msdu->ip_summed = ath10k_htt_rx_get_csum_state(hw, msdu);
}

static u64 ath10k_htt_rx_h_get_pn(struct ath10k *ar, struct sk_buff *skb,
				  u16 offset,
				  enum htt_rx_mpdu_encrypt_type enctype)
{
	struct ieee80211_hdr *hdr;
	u64 pn = 0;
	u8 *ehdr;

	hdr = (struct ieee80211_hdr *)(skb->data + offset);
	ehdr = skb->data + offset + ieee80211_hdrlen(hdr->frame_control);

	if (enctype == HTT_RX_MPDU_ENCRYPT_AES_CCM_WPA2) {
		pn = ehdr[0];
		pn |= (u64)ehdr[1] << 8;
		pn |= (u64)ehdr[4] << 16;
		pn |= (u64)ehdr[5] << 24;
		pn |= (u64)ehdr[6] << 32;
		pn |= (u64)ehdr[7] << 40;
	}
	return pn;
}

static bool ath10k_htt_rx_h_frag_multicast_check(struct ath10k *ar,
						 struct sk_buff *skb,
						 u16 offset)
{
	struct ieee80211_hdr *hdr;

	hdr = (struct ieee80211_hdr *)(skb->data + offset);
	return !is_multicast_ether_addr(hdr->addr1);
}

static bool ath10k_htt_rx_h_frag_pn_check(struct ath10k *ar,
					  struct sk_buff *skb,
					  u16 peer_id,
					  u16 offset,
					  enum htt_rx_mpdu_encrypt_type enctype)
{
	struct ath10k_peer *peer;
	union htt_rx_pn_t *last_pn, new_pn = {0};
	struct ieee80211_hdr *hdr;
	u8 tid, frag_number;
	u32 seq;

	peer = ath10k_peer_find_by_id(ar, peer_id);
	if (!peer) {
		ath10k_dbg(ar, ATH10K_DBG_HTT, "invalid peer for frag pn check\n");
		return false;
	}

	hdr = (struct ieee80211_hdr *)(skb->data + offset);
	if (ieee80211_is_data_qos(hdr->frame_control))
		tid = ieee80211_get_tid(hdr);
	else
		tid = ATH10K_TXRX_NON_QOS_TID;

	last_pn = &peer->frag_tids_last_pn[tid];
	new_pn.pn48 = ath10k_htt_rx_h_get_pn(ar, skb, offset, enctype);
	frag_number = le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_FRAG;
	seq = (__le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4;

	if (frag_number == 0) {
		last_pn->pn48 = new_pn.pn48;
		peer->frag_tids_seq[tid] = seq;
	} else {
		if (seq != peer->frag_tids_seq[tid])
			return false;

		if (new_pn.pn48 != last_pn->pn48 + 1)
			return false;

		last_pn->pn48 = new_pn.pn48;
	}

	return true;
}

static void ath10k_htt_rx_h_mpdu(struct ath10k *ar,
				 struct sk_buff_head *amsdu,
				 struct ieee80211_rx_status *status,
				 bool fill_crypt_header,
				 u8 *rx_hdr,
				 enum ath10k_pkt_rx_err *err,
				 u16 peer_id,
				 bool frag)
{
	struct sk_buff *first;
	struct sk_buff *last;
	struct sk_buff *msdu, *temp;
	struct ath10k_hw_params *hw = &ar->hw_params;
	struct htt_rx_desc *rxd;
	struct rx_attention *rxd_attention;
	struct rx_mpdu_start *rxd_mpdu_start;

	struct ieee80211_hdr *hdr;
	enum htt_rx_mpdu_encrypt_type enctype;
	u8 first_hdr[64];
	u8 *qos;
	bool has_fcs_err;
	bool has_crypto_err;
	bool has_tkip_err;
	bool has_peer_idx_invalid;
	bool is_decrypted;
	bool is_mgmt;
	u32 attention;
	bool frag_pn_check = true, multicast_check = true;

	if (skb_queue_empty(amsdu))
		return;

	first = skb_peek(amsdu);
	rxd = HTT_RX_BUF_TO_RX_DESC(hw,
				    (void *)first->data - hw->rx_desc_ops->rx_desc_size);

	rxd_attention = ath10k_htt_rx_desc_get_attention(hw, rxd);
	rxd_mpdu_start = ath10k_htt_rx_desc_get_mpdu_start(hw, rxd);

	is_mgmt = !!(rxd_attention->flags &
		     __cpu_to_le32(RX_ATTENTION_FLAGS_MGMT_TYPE));

	enctype = MS(__le32_to_cpu(rxd_mpdu_start->info0),
		     RX_MPDU_START_INFO0_ENCRYPT_TYPE);

	/* First MSDU's Rx descriptor in an A-MSDU contains full 802.11
	 * decapped header. It'll be used for undecapping of each MSDU.
	 */
	hdr = (void *)ath10k_htt_rx_desc_get_rx_hdr_status(hw, rxd);
	memcpy(first_hdr, hdr, RX_HTT_HDR_STATUS_LEN);

	if (rx_hdr)
		memcpy(rx_hdr, hdr, RX_HTT_HDR_STATUS_LEN);

	/* Each A-MSDU subframe will use the original header as the base and be
	 * reported as a separate MSDU so strip the A-MSDU bit from QoS Ctl.
	 */
	hdr = (void *)first_hdr;

	if (ieee80211_is_data_qos(hdr->frame_control)) {
		qos = ieee80211_get_qos_ctl(hdr);
		qos[0] &= ~IEEE80211_QOS_CTL_A_MSDU_PRESENT;
	}

	/* Some attention flags are valid only in the last MSDU. */
	last = skb_peek_tail(amsdu);
	rxd = HTT_RX_BUF_TO_RX_DESC(hw,
				    (void *)last->data - hw->rx_desc_ops->rx_desc_size);

	rxd_attention = ath10k_htt_rx_desc_get_attention(hw, rxd);
	attention = __le32_to_cpu(rxd_attention->flags);

	has_fcs_err = !!(attention & RX_ATTENTION_FLAGS_FCS_ERR);
	has_crypto_err = !!(attention & RX_ATTENTION_FLAGS_DECRYPT_ERR);
	has_tkip_err = !!(attention & RX_ATTENTION_FLAGS_TKIP_MIC_ERR);
	has_peer_idx_invalid = !!(attention & RX_ATTENTION_FLAGS_PEER_IDX_INVALID);

	/* Note: If hardware captures an encrypted frame that it can't decrypt,
	 * e.g. due to fcs error, missing peer or invalid key data it will
	 * report the frame as raw.
	 */
	is_decrypted = (enctype != HTT_RX_MPDU_ENCRYPT_NONE &&
			!has_fcs_err &&
			!has_crypto_err &&
			!has_peer_idx_invalid);

	/* Clear per-MPDU flags while leaving per-PPDU flags intact. */
	status->flag &= ~(RX_FLAG_FAILED_FCS_CRC |
			  RX_FLAG_MMIC_ERROR |
			  RX_FLAG_DECRYPTED |
			  RX_FLAG_IV_STRIPPED |
			  RX_FLAG_ONLY_MONITOR |
			  RX_FLAG_MMIC_STRIPPED);

	if (has_fcs_err)
		status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (has_tkip_err)
		status->flag |= RX_FLAG_MMIC_ERROR;

	if (err) {
		if (has_fcs_err)
			*err = ATH10K_PKT_RX_ERR_FCS;
		else if (has_tkip_err)
			*err = ATH10K_PKT_RX_ERR_TKIP;
		else if (has_crypto_err)
			*err = ATH10K_PKT_RX_ERR_CRYPT;
		else if (has_peer_idx_invalid)
			*err = ATH10K_PKT_RX_ERR_PEER_IDX_INVAL;
	}

	/* Firmware reports all necessary management frames via WMI already.
	 * They are not reported to monitor interfaces at all so pass the ones
	 * coming via HTT to monitor interfaces instead. This simplifies
	 * matters a lot.
	 */
	if (is_mgmt)
		status->flag |= RX_FLAG_ONLY_MONITOR;

	if (is_decrypted) {
		status->flag |= RX_FLAG_DECRYPTED;

		if (likely(!is_mgmt))
			status->flag |= RX_FLAG_MMIC_STRIPPED;

		if (fill_crypt_header)
			status->flag |= RX_FLAG_MIC_STRIPPED |
					RX_FLAG_ICV_STRIPPED;
		else
			status->flag |= RX_FLAG_IV_STRIPPED;
	}

	skb_queue_walk(amsdu, msdu) {
		if (frag && !fill_crypt_header && is_decrypted &&
		    enctype == HTT_RX_MPDU_ENCRYPT_AES_CCM_WPA2)
			frag_pn_check = ath10k_htt_rx_h_frag_pn_check(ar,
								      msdu,
								      peer_id,
								      0,
								      enctype);

		if (frag)
			multicast_check = ath10k_htt_rx_h_frag_multicast_check(ar,
									       msdu,
									       0);

		if (!frag_pn_check || !multicast_check) {
			/* Discard the fragment with invalid PN or multicast DA
			 */
			temp = msdu->prev;
			__skb_unlink(msdu, amsdu);
			dev_kfree_skb_any(msdu);
			msdu = temp;
			frag_pn_check = true;
			multicast_check = true;
			continue;
		}

		ath10k_htt_rx_h_csum_offload(&ar->hw_params, msdu);

		if (frag && !fill_crypt_header &&
		    enctype == HTT_RX_MPDU_ENCRYPT_TKIP_WPA)
			status->flag &= ~RX_FLAG_MMIC_STRIPPED;

		ath10k_htt_rx_h_undecap(ar, msdu, status, first_hdr, enctype,
					is_decrypted);

		/* Undecapping involves copying the original 802.11 header back
		 * to sk_buff. If frame is protected and hardware has decrypted
		 * it then remove the protected bit.
		 */
		if (!is_decrypted)
			continue;
		if (is_mgmt)
			continue;

		if (fill_crypt_header)
			continue;

		hdr = (void *)msdu->data;
		hdr->frame_control &= ~__cpu_to_le16(IEEE80211_FCTL_PROTECTED);

		if (frag && !fill_crypt_header &&
		    enctype == HTT_RX_MPDU_ENCRYPT_TKIP_WPA)
			status->flag &= ~RX_FLAG_IV_STRIPPED &
					~RX_FLAG_MMIC_STRIPPED;
	}
}

static void ath10k_htt_rx_h_enqueue(struct ath10k *ar,
				    struct sk_buff_head *amsdu,
				    struct ieee80211_rx_status *status)
{
	struct sk_buff *msdu;
	struct sk_buff *first_subframe;

	first_subframe = skb_peek(amsdu);

	while ((msdu = __skb_dequeue(amsdu))) {
		/* Setup per-MSDU flags */
		if (skb_queue_empty(amsdu))
			status->flag &= ~RX_FLAG_AMSDU_MORE;
		else
			status->flag |= RX_FLAG_AMSDU_MORE;

		if (msdu == first_subframe) {
			first_subframe = NULL;
			status->flag &= ~RX_FLAG_ALLOW_SAME_PN;
		} else {
			status->flag |= RX_FLAG_ALLOW_SAME_PN;
		}

		ath10k_htt_rx_h_queue_msdu(ar, status, msdu);
	}
}

static int ath10k_unchain_msdu(struct sk_buff_head *amsdu,
			       unsigned long *unchain_cnt)
{
	struct sk_buff *skb, *first;
	int space;
	int total_len = 0;
	int amsdu_len = skb_queue_len(amsdu);

	/* TODO:  Might could optimize this by using
	 * skb_try_coalesce or similar method to
	 * decrease copying, or maybe get mac80211 to
	 * provide a way to just receive a list of
	 * skb?
	 */

	first = __skb_dequeue(amsdu);

	/* Allocate total length all at once. */
	skb_queue_walk(amsdu, skb)
		total_len += skb->len;

	space = total_len - skb_tailroom(first);
	if ((space > 0) &&
	    (pskb_expand_head(first, 0, space, GFP_ATOMIC) < 0)) {
		/* TODO:  bump some rx-oom error stat */
		/* put it back together so we can free the
		 * whole list at once.
		 */
		__skb_queue_head(amsdu, first);
		return -1;
	}

	/* Walk list again, copying contents into
	 * msdu_head
	 */
	while ((skb = __skb_dequeue(amsdu))) {
		skb_copy_from_linear_data(skb, skb_put(first, skb->len),
					  skb->len);
		dev_kfree_skb_any(skb);
	}

	__skb_queue_head(amsdu, first);

	*unchain_cnt += amsdu_len - 1;

	return 0;
}

static void ath10k_htt_rx_h_unchain(struct ath10k *ar,
				    struct sk_buff_head *amsdu,
				    unsigned long *drop_cnt,
				    unsigned long *unchain_cnt)
{
	struct sk_buff *first;
	struct ath10k_hw_params *hw = &ar->hw_params;
	struct htt_rx_desc *rxd;
	struct rx_msdu_start_common *rxd_msdu_start_common;
	struct rx_frag_info_common *rxd_frag_info;
	enum rx_msdu_decap_format decap;

	first = skb_peek(amsdu);
	rxd = HTT_RX_BUF_TO_RX_DESC(hw,
				    (void *)first->data - hw->rx_desc_ops->rx_desc_size);

	rxd_msdu_start_common = ath10k_htt_rx_desc_get_msdu_start(hw, rxd);
	rxd_frag_info = ath10k_htt_rx_desc_get_frag_info(hw, rxd);
	decap = MS(__le32_to_cpu(rxd_msdu_start_common->info1),
		   RX_MSDU_START_INFO1_DECAP_FORMAT);

	/* FIXME: Current unchaining logic can only handle simple case of raw
	 * msdu chaining. If decapping is other than raw the chaining may be
	 * more complex and this isn't handled by the current code. Don't even
	 * try re-constructing such frames - it'll be pretty much garbage.
	 */
	if (decap != RX_MSDU_DECAP_RAW ||
	    skb_queue_len(amsdu) != 1 + rxd_frag_info->ring2_more_count) {
		*drop_cnt += skb_queue_len(amsdu);
		__skb_queue_purge(amsdu);
		return;
	}

	ath10k_unchain_msdu(amsdu, unchain_cnt);
}

static bool ath10k_htt_rx_validate_amsdu(struct ath10k *ar,
					 struct sk_buff_head *amsdu)
{
	u8 *subframe_hdr;
	struct sk_buff *first;
	bool is_first, is_last;
	struct ath10k_hw_params *hw = &ar->hw_params;
	struct htt_rx_desc *rxd;
	struct rx_msdu_end_common *rxd_msdu_end_common;
	struct rx_mpdu_start *rxd_mpdu_start;
	struct ieee80211_hdr *hdr;
	size_t hdr_len, crypto_len;
	enum htt_rx_mpdu_encrypt_type enctype;
	int bytes_aligned = ar->hw_params.decap_align_bytes;

	first = skb_peek(amsdu);

	rxd = HTT_RX_BUF_TO_RX_DESC(hw,
				    (void *)first->data - hw->rx_desc_ops->rx_desc_size);

	rxd_msdu_end_common = ath10k_htt_rx_desc_get_msdu_end(hw, rxd);
	rxd_mpdu_start = ath10k_htt_rx_desc_get_mpdu_start(hw, rxd);
	hdr = (void *)ath10k_htt_rx_desc_get_rx_hdr_status(hw, rxd);

	is_first = !!(rxd_msdu_end_common->info0 &
		      __cpu_to_le32(RX_MSDU_END_INFO0_FIRST_MSDU));
	is_last = !!(rxd_msdu_end_common->info0 &
		     __cpu_to_le32(RX_MSDU_END_INFO0_LAST_MSDU));

	/* Return in case of non-aggregated msdu */
	if (is_first && is_last)
		return true;

	/* First msdu flag is not set for the first msdu of the list */
	if (!is_first)
		return false;

	enctype = MS(__le32_to_cpu(rxd_mpdu_start->info0),
		     RX_MPDU_START_INFO0_ENCRYPT_TYPE);

	hdr_len = ieee80211_hdrlen(hdr->frame_control);
	crypto_len = ath10k_htt_rx_crypto_param_len(ar, enctype);

	subframe_hdr = (u8 *)hdr + round_up(hdr_len, bytes_aligned) +
		       crypto_len;

	/* Validate if the amsdu has a proper first subframe.
	 * There are chances a single msdu can be received as amsdu when
	 * the unauthenticated amsdu flag of a QoS header
	 * gets flipped in non-SPP AMSDU's, in such cases the first
	 * subframe has llc/snap header in place of a valid da.
	 * return false if the da matches rfc1042 pattern
	 */
	if (ether_addr_equal(subframe_hdr, rfc1042_header))
		return false;

	return true;
}

static bool ath10k_htt_rx_amsdu_allowed(struct ath10k *ar,
					struct sk_buff_head *amsdu,
					struct ieee80211_rx_status *rx_status)
{
	if (!rx_status->freq) {
		ath10k_dbg(ar, ATH10K_DBG_HTT, "no channel configured; ignoring frame(s)!\n");
		return false;
	}

	if (test_bit(ATH10K_CAC_RUNNING, &ar->dev_flags)) {
		ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx cac running\n");
		return false;
	}

	if (!ath10k_htt_rx_validate_amsdu(ar, amsdu)) {
		ath10k_dbg(ar, ATH10K_DBG_HTT, "invalid amsdu received\n");
		return false;
	}

	return true;
}

static void ath10k_htt_rx_h_filter(struct ath10k *ar,
				   struct sk_buff_head *amsdu,
				   struct ieee80211_rx_status *rx_status,
				   unsigned long *drop_cnt)
{
	if (skb_queue_empty(amsdu))
		return;

	if (ath10k_htt_rx_amsdu_allowed(ar, amsdu, rx_status))
		return;

	if (drop_cnt)
		*drop_cnt += skb_queue_len(amsdu);

	__skb_queue_purge(amsdu);
}

static int ath10k_htt_rx_handle_amsdu(struct ath10k_htt *htt)
{
	struct ath10k *ar = htt->ar;
	struct ieee80211_rx_status *rx_status = &htt->rx_status;
	struct sk_buff_head amsdu;
	int ret;
	unsigned long drop_cnt = 0;
	unsigned long unchain_cnt = 0;
	unsigned long drop_cnt_filter = 0;
	unsigned long msdus_to_queue, num_msdus;
	enum ath10k_pkt_rx_err err = ATH10K_PKT_RX_ERR_MAX;
	u8 first_hdr[RX_HTT_HDR_STATUS_LEN];

	__skb_queue_head_init(&amsdu);

	spin_lock_bh(&htt->rx_ring.lock);
	if (htt->rx_confused) {
		spin_unlock_bh(&htt->rx_ring.lock);
		return -EIO;
	}
	ret = ath10k_htt_rx_amsdu_pop(htt, &amsdu);
	spin_unlock_bh(&htt->rx_ring.lock);

	if (ret < 0) {
		ath10k_warn(ar, "rx ring became corrupted: %d\n", ret);
		__skb_queue_purge(&amsdu);
		/* FIXME: It's probably a good idea to reboot the
		 * device instead of leaving it inoperable.
		 */
		htt->rx_confused = true;
		return ret;
	}

	num_msdus = skb_queue_len(&amsdu);

	ath10k_htt_rx_h_ppdu(ar, &amsdu, rx_status, 0xffff);

	/* only for ret = 1 indicates chained msdus */
	if (ret > 0)
		ath10k_htt_rx_h_unchain(ar, &amsdu, &drop_cnt, &unchain_cnt);

	ath10k_htt_rx_h_filter(ar, &amsdu, rx_status, &drop_cnt_filter);
	ath10k_htt_rx_h_mpdu(ar, &amsdu, rx_status, true, first_hdr, &err, 0,
			     false);
	msdus_to_queue = skb_queue_len(&amsdu);
	ath10k_htt_rx_h_enqueue(ar, &amsdu, rx_status);

	ath10k_sta_update_rx_tid_stats(ar, first_hdr, num_msdus, err,
				       unchain_cnt, drop_cnt, drop_cnt_filter,
				       msdus_to_queue);

	return 0;
}

static void ath10k_htt_rx_mpdu_desc_pn_hl(struct htt_hl_rx_desc *rx_desc,
					  union htt_rx_pn_t *pn,
					  int pn_len_bits)
{
	switch (pn_len_bits) {
	case 48:
		pn->pn48 = __le32_to_cpu(rx_desc->pn_31_0) +
			   ((u64)(__le32_to_cpu(rx_desc->u0.pn_63_32) & 0xFFFF) << 32);
		break;
	case 24:
		pn->pn24 = __le32_to_cpu(rx_desc->pn_31_0);
		break;
	}
}

static bool ath10k_htt_rx_pn_cmp48(union htt_rx_pn_t *new_pn,
				   union htt_rx_pn_t *old_pn)
{
	return ((new_pn->pn48 & 0xffffffffffffULL) <=
		(old_pn->pn48 & 0xffffffffffffULL));
}

static bool ath10k_htt_rx_pn_check_replay_hl(struct ath10k *ar,
					     struct ath10k_peer *peer,
					     struct htt_rx_indication_hl *rx)
{
	bool last_pn_valid, pn_invalid = false;
	enum htt_txrx_sec_cast_type sec_index;
	enum htt_security_types sec_type;
	union htt_rx_pn_t new_pn = {0};
	struct htt_hl_rx_desc *rx_desc;
	union htt_rx_pn_t *last_pn;
	u32 rx_desc_info, tid;
	int num_mpdu_ranges;

	lockdep_assert_held(&ar->data_lock);

	if (!peer)
		return false;

	if (!(rx->fw_desc.flags & FW_RX_DESC_FLAGS_FIRST_MSDU))
		return false;

	num_mpdu_ranges = MS(__le32_to_cpu(rx->hdr.info1),
			     HTT_RX_INDICATION_INFO1_NUM_MPDU_RANGES);

	rx_desc = (struct htt_hl_rx_desc *)&rx->mpdu_ranges[num_mpdu_ranges];
	rx_desc_info = __le32_to_cpu(rx_desc->info);

	if (!MS(rx_desc_info, HTT_RX_DESC_HL_INFO_ENCRYPTED))
		return false;

	tid = MS(rx->hdr.info0, HTT_RX_INDICATION_INFO0_EXT_TID);
	last_pn_valid = peer->tids_last_pn_valid[tid];
	last_pn = &peer->tids_last_pn[tid];

	if (MS(rx_desc_info, HTT_RX_DESC_HL_INFO_MCAST_BCAST))
		sec_index = HTT_TXRX_SEC_MCAST;
	else
		sec_index = HTT_TXRX_SEC_UCAST;

	sec_type = peer->rx_pn[sec_index].sec_type;
	ath10k_htt_rx_mpdu_desc_pn_hl(rx_desc, &new_pn, peer->rx_pn[sec_index].pn_len);

	if (sec_type != HTT_SECURITY_AES_CCMP &&
	    sec_type != HTT_SECURITY_TKIP &&
	    sec_type != HTT_SECURITY_TKIP_NOMIC)
		return false;

	if (last_pn_valid)
		pn_invalid = ath10k_htt_rx_pn_cmp48(&new_pn, last_pn);
	else
		peer->tids_last_pn_valid[tid] = true;

	if (!pn_invalid)
		last_pn->pn48 = new_pn.pn48;

	return pn_invalid;
}

static bool ath10k_htt_rx_proc_rx_ind_hl(struct ath10k_htt *htt,
					 struct htt_rx_indication_hl *rx,
					 struct sk_buff *skb,
					 enum htt_rx_pn_check_type check_pn_type,
					 enum htt_rx_tkip_demic_type tkip_mic_type)
{
	struct ath10k *ar = htt->ar;
	struct ath10k_peer *peer;
	struct htt_rx_indication_mpdu_range *mpdu_ranges;
	struct fw_rx_desc_hl *fw_desc;
	enum htt_txrx_sec_cast_type sec_index;
	enum htt_security_types sec_type;
	union htt_rx_pn_t new_pn = {0};
	struct htt_hl_rx_desc *rx_desc;
	struct ieee80211_hdr *hdr;
	struct ieee80211_rx_status *rx_status;
	u16 peer_id;
	u8 rx_desc_len;
	int num_mpdu_ranges;
	size_t tot_hdr_len;
	struct ieee80211_channel *ch;
	bool pn_invalid, qos, first_msdu;
	u32 tid, rx_desc_info;

	peer_id = __le16_to_cpu(rx->hdr.peer_id);
	tid = MS(rx->hdr.info0, HTT_RX_INDICATION_INFO0_EXT_TID);

	spin_lock_bh(&ar->data_lock);
	peer = ath10k_peer_find_by_id(ar, peer_id);
	spin_unlock_bh(&ar->data_lock);
	if (!peer && peer_id != HTT_INVALID_PEERID)
		ath10k_warn(ar, "Got RX ind from invalid peer: %u\n", peer_id);

	if (!peer)
		return true;

	num_mpdu_ranges = MS(__le32_to_cpu(rx->hdr.info1),
			     HTT_RX_INDICATION_INFO1_NUM_MPDU_RANGES);
	mpdu_ranges = htt_rx_ind_get_mpdu_ranges_hl(rx);
	fw_desc = &rx->fw_desc;
	rx_desc_len = fw_desc->len;

	if (fw_desc->u.bits.discard) {
		ath10k_dbg(ar, ATH10K_DBG_HTT, "htt discard mpdu\n");
		goto err;
	}

	/* I have not yet seen any case where num_mpdu_ranges > 1.
	 * qcacld does not seem handle that case either, so we introduce the
	 * same limitiation here as well.
	 */
	if (num_mpdu_ranges > 1)
		ath10k_warn(ar,
			    "Unsupported number of MPDU ranges: %d, ignoring all but the first\n",
			    num_mpdu_ranges);

	if (mpdu_ranges->mpdu_range_status !=
	    HTT_RX_IND_MPDU_STATUS_OK &&
	    mpdu_ranges->mpdu_range_status !=
	    HTT_RX_IND_MPDU_STATUS_TKIP_MIC_ERR) {
		ath10k_dbg(ar, ATH10K_DBG_HTT, "htt mpdu_range_status %d\n",
			   mpdu_ranges->mpdu_range_status);
		goto err;
	}

	rx_desc = (struct htt_hl_rx_desc *)&rx->mpdu_ranges[num_mpdu_ranges];
	rx_desc_info = __le32_to_cpu(rx_desc->info);

	if (MS(rx_desc_info, HTT_RX_DESC_HL_INFO_MCAST_BCAST))
		sec_index = HTT_TXRX_SEC_MCAST;
	else
		sec_index = HTT_TXRX_SEC_UCAST;

	sec_type = peer->rx_pn[sec_index].sec_type;
	first_msdu = rx->fw_desc.flags & FW_RX_DESC_FLAGS_FIRST_MSDU;

	ath10k_htt_rx_mpdu_desc_pn_hl(rx_desc, &new_pn, peer->rx_pn[sec_index].pn_len);

	if (check_pn_type == HTT_RX_PN_CHECK && tid >= IEEE80211_NUM_TIDS) {
		spin_lock_bh(&ar->data_lock);
		pn_invalid = ath10k_htt_rx_pn_check_replay_hl(ar, peer, rx);
		spin_unlock_bh(&ar->data_lock);

		if (pn_invalid)
			goto err;
	}

	/* Strip off all headers before the MAC header before delivery to
	 * mac80211
	 */
	tot_hdr_len = sizeof(struct htt_resp_hdr) + sizeof(rx->hdr) +
		      sizeof(rx->ppdu) + sizeof(rx->prefix) +
		      sizeof(rx->fw_desc) +
		      sizeof(*mpdu_ranges) * num_mpdu_ranges + rx_desc_len;

	skb_pull(skb, tot_hdr_len);

	hdr = (struct ieee80211_hdr *)skb->data;
	qos = ieee80211_is_data_qos(hdr->frame_control);

	rx_status = IEEE80211_SKB_RXCB(skb);
	memset(rx_status, 0, sizeof(*rx_status));

	if (rx->ppdu.combined_rssi == 0) {
		/* SDIO firmware does not provide signal */
		rx_status->signal = 0;
		rx_status->flag |= RX_FLAG_NO_SIGNAL_VAL;
	} else {
		rx_status->signal = ATH10K_DEFAULT_NOISE_FLOOR +
			rx->ppdu.combined_rssi;
		rx_status->flag &= ~RX_FLAG_NO_SIGNAL_VAL;
	}

	spin_lock_bh(&ar->data_lock);
	ch = ar->scan_channel;
	if (!ch)
		ch = ar->rx_channel;
	if (!ch)
		ch = ath10k_htt_rx_h_any_channel(ar);
	if (!ch)
		ch = ar->tgt_oper_chan;
	spin_unlock_bh(&ar->data_lock);

	if (ch) {
		rx_status->band = ch->band;
		rx_status->freq = ch->center_freq;
	}
	if (rx->fw_desc.flags & FW_RX_DESC_FLAGS_LAST_MSDU)
		rx_status->flag &= ~RX_FLAG_AMSDU_MORE;
	else
		rx_status->flag |= RX_FLAG_AMSDU_MORE;

	/* Not entirely sure about this, but all frames from the chipset has
	 * the protected flag set even though they have already been decrypted.
	 * Unmasking this flag is necessary in order for mac80211 not to drop
	 * the frame.
	 * TODO: Verify this is always the case or find out a way to check
	 * if there has been hw decryption.
	 */
	if (ieee80211_has_protected(hdr->frame_control)) {
		hdr->frame_control &= ~__cpu_to_le16(IEEE80211_FCTL_PROTECTED);
		rx_status->flag |= RX_FLAG_DECRYPTED |
				   RX_FLAG_IV_STRIPPED |
				   RX_FLAG_MMIC_STRIPPED;

		if (tid < IEEE80211_NUM_TIDS &&
		    first_msdu &&
		    check_pn_type == HTT_RX_PN_CHECK &&
		   (sec_type == HTT_SECURITY_AES_CCMP ||
		    sec_type == HTT_SECURITY_TKIP ||
		    sec_type == HTT_SECURITY_TKIP_NOMIC)) {
			u8 offset, *ivp, i;
			s8 keyidx = 0;
			__le64 pn48 = cpu_to_le64(new_pn.pn48);

			hdr = (struct ieee80211_hdr *)skb->data;
			offset = ieee80211_hdrlen(hdr->frame_control);
			hdr->frame_control |= __cpu_to_le16(IEEE80211_FCTL_PROTECTED);
			rx_status->flag &= ~RX_FLAG_IV_STRIPPED;

			memmove(skb->data - IEEE80211_CCMP_HDR_LEN,
				skb->data, offset);
			skb_push(skb, IEEE80211_CCMP_HDR_LEN);
			ivp = skb->data + offset;
			memset(skb->data + offset, 0, IEEE80211_CCMP_HDR_LEN);
			/* Ext IV */
			ivp[IEEE80211_WEP_IV_LEN - 1] |= ATH10K_IEEE80211_EXTIV;

			for (i = 0; i < ARRAY_SIZE(peer->keys); i++) {
				if (peer->keys[i] &&
				    peer->keys[i]->flags & IEEE80211_KEY_FLAG_PAIRWISE)
					keyidx = peer->keys[i]->keyidx;
			}

			/* Key ID */
			ivp[IEEE80211_WEP_IV_LEN - 1] |= keyidx << 6;

			if (sec_type == HTT_SECURITY_AES_CCMP) {
				rx_status->flag |= RX_FLAG_MIC_STRIPPED;
				/* pn 0, pn 1 */
				memcpy(skb->data + offset, &pn48, 2);
				/* pn 1, pn 3 , pn 34 , pn 5 */
				memcpy(skb->data + offset + 4, ((u8 *)&pn48) + 2, 4);
			} else {
				rx_status->flag |= RX_FLAG_ICV_STRIPPED;
				/* TSC 0 */
				memcpy(skb->data + offset + 2, &pn48, 1);
				/* TSC 1 */
				memcpy(skb->data + offset, ((u8 *)&pn48) + 1, 1);
				/* TSC 2 , TSC 3 , TSC 4 , TSC 5*/
				memcpy(skb->data + offset + 4, ((u8 *)&pn48) + 2, 4);
			}
		}
	}

	if (tkip_mic_type == HTT_RX_TKIP_MIC)
		rx_status->flag &= ~RX_FLAG_IV_STRIPPED &
				   ~RX_FLAG_MMIC_STRIPPED;

	if (mpdu_ranges->mpdu_range_status == HTT_RX_IND_MPDU_STATUS_TKIP_MIC_ERR)
		rx_status->flag |= RX_FLAG_MMIC_ERROR;

	if (!qos && tid < IEEE80211_NUM_TIDS) {
		u8 offset;
		__le16 qos_ctrl = 0;

		hdr = (struct ieee80211_hdr *)skb->data;
		offset = ieee80211_hdrlen(hdr->frame_control);

		hdr->frame_control |= cpu_to_le16(IEEE80211_STYPE_QOS_DATA);
		memmove(skb->data - IEEE80211_QOS_CTL_LEN, skb->data, offset);
		skb_push(skb, IEEE80211_QOS_CTL_LEN);
		qos_ctrl = cpu_to_le16(tid);
		memcpy(skb->data + offset, &qos_ctrl, IEEE80211_QOS_CTL_LEN);
	}

	if (ar->napi.dev)
		ieee80211_rx_napi(ar->hw, NULL, skb, &ar->napi);
	else
		ieee80211_rx_ni(ar->hw, skb);

	/* We have delivered the skb to the upper layers (mac80211) so we
	 * must not free it.
	 */
	return false;
err:
	/* Tell the caller that it must free the skb since we have not
	 * consumed it
	 */
	return true;
}

static int ath10k_htt_rx_frag_tkip_decap_nomic(struct sk_buff *skb,
					       u16 head_len,
					       u16 hdr_len)
{
	u8 *ivp, *orig_hdr;

	orig_hdr = skb->data;
	ivp = orig_hdr + hdr_len + head_len;

	/* the ExtIV bit is always set to 1 for TKIP */
	if (!(ivp[IEEE80211_WEP_IV_LEN - 1] & ATH10K_IEEE80211_EXTIV))
		return -EINVAL;

	memmove(orig_hdr + IEEE80211_TKIP_IV_LEN, orig_hdr, head_len + hdr_len);
	skb_pull(skb, IEEE80211_TKIP_IV_LEN);
	skb_trim(skb, skb->len - ATH10K_IEEE80211_TKIP_MICLEN);
	return 0;
}

static int ath10k_htt_rx_frag_tkip_decap_withmic(struct sk_buff *skb,
						 u16 head_len,
						 u16 hdr_len)
{
	u8 *ivp, *orig_hdr;

	orig_hdr = skb->data;
	ivp = orig_hdr + hdr_len + head_len;

	/* the ExtIV bit is always set to 1 for TKIP */
	if (!(ivp[IEEE80211_WEP_IV_LEN - 1] & ATH10K_IEEE80211_EXTIV))
		return -EINVAL;

	memmove(orig_hdr + IEEE80211_TKIP_IV_LEN, orig_hdr, head_len + hdr_len);
	skb_pull(skb, IEEE80211_TKIP_IV_LEN);
	skb_trim(skb, skb->len - IEEE80211_TKIP_ICV_LEN);
	return 0;
}

static int ath10k_htt_rx_frag_ccmp_decap(struct sk_buff *skb,
					 u16 head_len,
					 u16 hdr_len)
{
	u8 *ivp, *orig_hdr;

	orig_hdr = skb->data;
	ivp = orig_hdr + hdr_len + head_len;

	/* the ExtIV bit is always set to 1 for CCMP */
	if (!(ivp[IEEE80211_WEP_IV_LEN - 1] & ATH10K_IEEE80211_EXTIV))
		return -EINVAL;

	skb_trim(skb, skb->len - IEEE80211_CCMP_MIC_LEN);
	memmove(orig_hdr + IEEE80211_CCMP_HDR_LEN, orig_hdr, head_len + hdr_len);
	skb_pull(skb, IEEE80211_CCMP_HDR_LEN);
	return 0;
}

static int ath10k_htt_rx_frag_wep_decap(struct sk_buff *skb,
					u16 head_len,
					u16 hdr_len)
{
	u8 *orig_hdr;

	orig_hdr = skb->data;

	memmove(orig_hdr + IEEE80211_WEP_IV_LEN,
		orig_hdr, head_len + hdr_len);
	skb_pull(skb, IEEE80211_WEP_IV_LEN);
	skb_trim(skb, skb->len - IEEE80211_WEP_ICV_LEN);
	return 0;
}

static bool ath10k_htt_rx_proc_rx_frag_ind_hl(struct ath10k_htt *htt,
					      struct htt_rx_fragment_indication *rx,
					      struct sk_buff *skb)
{
	struct ath10k *ar = htt->ar;
	enum htt_rx_tkip_demic_type tkip_mic = HTT_RX_NON_TKIP_MIC;
	enum htt_txrx_sec_cast_type sec_index;
	struct htt_rx_indication_hl *rx_hl;
	enum htt_security_types sec_type;
	u32 tid, frag, seq, rx_desc_info;
	union htt_rx_pn_t new_pn = {0};
	struct htt_hl_rx_desc *rx_desc;
	u16 peer_id, sc, hdr_space;
	union htt_rx_pn_t *last_pn;
	struct ieee80211_hdr *hdr;
	int ret, num_mpdu_ranges;
	struct ath10k_peer *peer;
	struct htt_resp *resp;
	size_t tot_hdr_len;

	resp = (struct htt_resp *)(skb->data + HTT_RX_FRAG_IND_INFO0_HEADER_LEN);
	skb_pull(skb, HTT_RX_FRAG_IND_INFO0_HEADER_LEN);
	skb_trim(skb, skb->len - FCS_LEN);

	peer_id = __le16_to_cpu(rx->peer_id);
	rx_hl = (struct htt_rx_indication_hl *)(&resp->rx_ind_hl);

	spin_lock_bh(&ar->data_lock);
	peer = ath10k_peer_find_by_id(ar, peer_id);
	if (!peer) {
		ath10k_dbg(ar, ATH10K_DBG_HTT, "invalid peer: %u\n", peer_id);
		goto err;
	}

	num_mpdu_ranges = MS(__le32_to_cpu(rx_hl->hdr.info1),
			     HTT_RX_INDICATION_INFO1_NUM_MPDU_RANGES);

	tot_hdr_len = sizeof(struct htt_resp_hdr) +
		      sizeof(rx_hl->hdr) +
		      sizeof(rx_hl->ppdu) +
		      sizeof(rx_hl->prefix) +
		      sizeof(rx_hl->fw_desc) +
		      sizeof(struct htt_rx_indication_mpdu_range) * num_mpdu_ranges;

	tid =  MS(rx_hl->hdr.info0, HTT_RX_INDICATION_INFO0_EXT_TID);
	rx_desc = (struct htt_hl_rx_desc *)(skb->data + tot_hdr_len);
	rx_desc_info = __le32_to_cpu(rx_desc->info);

	hdr = (struct ieee80211_hdr *)((u8 *)rx_desc + rx_hl->fw_desc.len);

	if (is_multicast_ether_addr(hdr->addr1)) {
		/* Discard the fragment with multicast DA */
		goto err;
	}

	if (!MS(rx_desc_info, HTT_RX_DESC_HL_INFO_ENCRYPTED)) {
		spin_unlock_bh(&ar->data_lock);
		return ath10k_htt_rx_proc_rx_ind_hl(htt, &resp->rx_ind_hl, skb,
						    HTT_RX_NON_PN_CHECK,
						    HTT_RX_NON_TKIP_MIC);
	}

	if (ieee80211_has_retry(hdr->frame_control))
		goto err;

	hdr_space = ieee80211_hdrlen(hdr->frame_control);
	sc = __le16_to_cpu(hdr->seq_ctrl);
	seq = (sc & IEEE80211_SCTL_SEQ) >> 4;
	frag = sc & IEEE80211_SCTL_FRAG;

	sec_index = MS(rx_desc_info, HTT_RX_DESC_HL_INFO_MCAST_BCAST) ?
		    HTT_TXRX_SEC_MCAST : HTT_TXRX_SEC_UCAST;
	sec_type = peer->rx_pn[sec_index].sec_type;
	ath10k_htt_rx_mpdu_desc_pn_hl(rx_desc, &new_pn, peer->rx_pn[sec_index].pn_len);

	switch (sec_type) {
	case HTT_SECURITY_TKIP:
		tkip_mic = HTT_RX_TKIP_MIC;
		ret = ath10k_htt_rx_frag_tkip_decap_withmic(skb,
							    tot_hdr_len +
							    rx_hl->fw_desc.len,
							    hdr_space);
		if (ret)
			goto err;
		break;
	case HTT_SECURITY_TKIP_NOMIC:
		ret = ath10k_htt_rx_frag_tkip_decap_nomic(skb,
							  tot_hdr_len +
							  rx_hl->fw_desc.len,
							  hdr_space);
		if (ret)
			goto err;
		break;
	case HTT_SECURITY_AES_CCMP:
		ret = ath10k_htt_rx_frag_ccmp_decap(skb,
						    tot_hdr_len + rx_hl->fw_desc.len,
						    hdr_space);
		if (ret)
			goto err;
		break;
	case HTT_SECURITY_WEP128:
	case HTT_SECURITY_WEP104:
	case HTT_SECURITY_WEP40:
		ret = ath10k_htt_rx_frag_wep_decap(skb,
						   tot_hdr_len + rx_hl->fw_desc.len,
						   hdr_space);
		if (ret)
			goto err;
		break;
	default:
		break;
	}

	resp = (struct htt_resp *)(skb->data);

	if (sec_type != HTT_SECURITY_AES_CCMP &&
	    sec_type != HTT_SECURITY_TKIP &&
	    sec_type != HTT_SECURITY_TKIP_NOMIC) {
		spin_unlock_bh(&ar->data_lock);
		return ath10k_htt_rx_proc_rx_ind_hl(htt, &resp->rx_ind_hl, skb,
						    HTT_RX_NON_PN_CHECK,
						    HTT_RX_NON_TKIP_MIC);
	}

	last_pn = &peer->frag_tids_last_pn[tid];

	if (frag == 0) {
		if (ath10k_htt_rx_pn_check_replay_hl(ar, peer, &resp->rx_ind_hl))
			goto err;

		last_pn->pn48 = new_pn.pn48;
		peer->frag_tids_seq[tid] = seq;
	} else if (sec_type == HTT_SECURITY_AES_CCMP) {
		if (seq != peer->frag_tids_seq[tid])
			goto err;

		if (new_pn.pn48 != last_pn->pn48 + 1)
			goto err;

		last_pn->pn48 = new_pn.pn48;
		last_pn = &peer->tids_last_pn[tid];
		last_pn->pn48 = new_pn.pn48;
	}

	spin_unlock_bh(&ar->data_lock);

	return ath10k_htt_rx_proc_rx_ind_hl(htt, &resp->rx_ind_hl, skb,
					    HTT_RX_NON_PN_CHECK, tkip_mic);

err:
	spin_unlock_bh(&ar->data_lock);

	/* Tell the caller that it must free the skb since we have not
	 * consumed it
	 */
	return true;
}

static void ath10k_htt_rx_proc_rx_ind_ll(struct ath10k_htt *htt,
					 struct htt_rx_indication *rx)
{
	struct ath10k *ar = htt->ar;
	struct htt_rx_indication_mpdu_range *mpdu_ranges;
	int num_mpdu_ranges;
	int i, mpdu_count = 0;
	u16 peer_id;
	u8 tid;

	num_mpdu_ranges = MS(__le32_to_cpu(rx->hdr.info1),
			     HTT_RX_INDICATION_INFO1_NUM_MPDU_RANGES);
	peer_id = __le16_to_cpu(rx->hdr.peer_id);
	tid =  MS(rx->hdr.info0, HTT_RX_INDICATION_INFO0_EXT_TID);

	mpdu_ranges = htt_rx_ind_get_mpdu_ranges(rx);

	ath10k_dbg_dump(ar, ATH10K_DBG_HTT_DUMP, NULL, "htt rx ind: ",
			rx, struct_size(rx, mpdu_ranges, num_mpdu_ranges));

	for (i = 0; i < num_mpdu_ranges; i++)
		mpdu_count += mpdu_ranges[i].mpdu_count;

	atomic_add(mpdu_count, &htt->num_mpdus_ready);

	ath10k_sta_update_rx_tid_stats_ampdu(ar, peer_id, tid, mpdu_ranges,
					     num_mpdu_ranges);
}

static void ath10k_htt_rx_tx_compl_ind(struct ath10k *ar,
				       struct sk_buff *skb)
{
	struct ath10k_htt *htt = &ar->htt;
	struct htt_resp *resp = (struct htt_resp *)skb->data;
	struct htt_tx_done tx_done = {};
	int status = MS(resp->data_tx_completion.flags, HTT_DATA_TX_STATUS);
	__le16 msdu_id, *msdus;
	bool rssi_enabled = false;
	u8 msdu_count = 0, num_airtime_records, tid;
	int i, htt_pad = 0;
	struct htt_data_tx_compl_ppdu_dur *ppdu_info;
	struct ath10k_peer *peer;
	u16 ppdu_info_offset = 0, peer_id;
	u32 tx_duration;

	switch (status) {
	case HTT_DATA_TX_STATUS_NO_ACK:
		tx_done.status = HTT_TX_COMPL_STATE_NOACK;
		break;
	case HTT_DATA_TX_STATUS_OK:
		tx_done.status = HTT_TX_COMPL_STATE_ACK;
		break;
	case HTT_DATA_TX_STATUS_DISCARD:
	case HTT_DATA_TX_STATUS_POSTPONE:
	case HTT_DATA_TX_STATUS_DOWNLOAD_FAIL:
		tx_done.status = HTT_TX_COMPL_STATE_DISCARD;
		break;
	default:
		ath10k_warn(ar, "unhandled tx completion status %d\n", status);
		tx_done.status = HTT_TX_COMPL_STATE_DISCARD;
		break;
	}

	ath10k_dbg(ar, ATH10K_DBG_HTT, "htt tx completion num_msdus %d\n",
		   resp->data_tx_completion.num_msdus);

	msdu_count = resp->data_tx_completion.num_msdus;
	msdus = resp->data_tx_completion.msdus;
	rssi_enabled = ath10k_is_rssi_enable(&ar->hw_params, resp);

	if (rssi_enabled)
		htt_pad = ath10k_tx_data_rssi_get_pad_bytes(&ar->hw_params,
							    resp);

	for (i = 0; i < msdu_count; i++) {
		msdu_id = msdus[i];
		tx_done.msdu_id = __le16_to_cpu(msdu_id);

		if (rssi_enabled) {
			/* Total no of MSDUs should be even,
			 * if odd MSDUs are sent firmware fills
			 * last msdu id with 0xffff
			 */
			if (msdu_count & 0x01) {
				msdu_id = msdus[msdu_count +  i + 1 + htt_pad];
				tx_done.ack_rssi = __le16_to_cpu(msdu_id);
			} else {
				msdu_id = msdus[msdu_count +  i + htt_pad];
				tx_done.ack_rssi = __le16_to_cpu(msdu_id);
			}
		}

		/* kfifo_put: In practice firmware shouldn't fire off per-CE
		 * interrupt and main interrupt (MSI/-X range case) for the same
		 * HTC service so it should be safe to use kfifo_put w/o lock.
		 *
		 * From kfifo_put() documentation:
		 *  Note that with only one concurrent reader and one concurrent
		 *  writer, you don't need extra locking to use these macro.
		 */
		if (ar->bus_param.dev_type == ATH10K_DEV_TYPE_HL) {
			ath10k_txrx_tx_unref(htt, &tx_done);
		} else if (!kfifo_put(&htt->txdone_fifo, tx_done)) {
			ath10k_warn(ar, "txdone fifo overrun, msdu_id %d status %d\n",
				    tx_done.msdu_id, tx_done.status);
			ath10k_txrx_tx_unref(htt, &tx_done);
		}
	}

	if (!(resp->data_tx_completion.flags2 & HTT_TX_CMPL_FLAG_PPDU_DURATION_PRESENT))
		return;

	ppdu_info_offset = (msdu_count & 0x01) ? msdu_count + 1 : msdu_count;

	if (rssi_enabled)
		ppdu_info_offset += ppdu_info_offset;

	if (resp->data_tx_completion.flags2 &
	    (HTT_TX_CMPL_FLAG_PPID_PRESENT | HTT_TX_CMPL_FLAG_PA_PRESENT))
		ppdu_info_offset += 2;

	ppdu_info = (struct htt_data_tx_compl_ppdu_dur *)&msdus[ppdu_info_offset];
	num_airtime_records = FIELD_GET(HTT_TX_COMPL_PPDU_DUR_INFO0_NUM_ENTRIES_MASK,
					__le32_to_cpu(ppdu_info->info0));

	for (i = 0; i < num_airtime_records; i++) {
		struct htt_data_tx_ppdu_dur *ppdu_dur;
		u32 info0;

		ppdu_dur = &ppdu_info->ppdu_dur[i];
		info0 = __le32_to_cpu(ppdu_dur->info0);

		peer_id = FIELD_GET(HTT_TX_PPDU_DUR_INFO0_PEER_ID_MASK,
				    info0);
		rcu_read_lock();
		spin_lock_bh(&ar->data_lock);

		peer = ath10k_peer_find_by_id(ar, peer_id);
		if (!peer || !peer->sta) {
			spin_unlock_bh(&ar->data_lock);
			rcu_read_unlock();
			continue;
		}

		tid = FIELD_GET(HTT_TX_PPDU_DUR_INFO0_TID_MASK, info0) &
						IEEE80211_QOS_CTL_TID_MASK;
		tx_duration = __le32_to_cpu(ppdu_dur->tx_duration);

		ieee80211_sta_register_airtime(peer->sta, tid, tx_duration, 0);

		spin_unlock_bh(&ar->data_lock);
		rcu_read_unlock();
	}
}

static void ath10k_htt_rx_addba(struct ath10k *ar, struct htt_resp *resp)
{
	struct htt_rx_addba *ev = &resp->rx_addba;
	struct ath10k_peer *peer;
	struct ath10k_vif *arvif;
	u16 info0, tid, peer_id;

	info0 = __le16_to_cpu(ev->info0);
	tid = MS(info0, HTT_RX_BA_INFO0_TID);
	peer_id = MS(info0, HTT_RX_BA_INFO0_PEER_ID);

	ath10k_dbg(ar, ATH10K_DBG_HTT,
		   "htt rx addba tid %u peer_id %u size %u\n",
		   tid, peer_id, ev->window_size);

	spin_lock_bh(&ar->data_lock);
	peer = ath10k_peer_find_by_id(ar, peer_id);
	if (!peer) {
		ath10k_warn(ar, "received addba event for invalid peer_id: %u\n",
			    peer_id);
		spin_unlock_bh(&ar->data_lock);
		return;
	}

	arvif = ath10k_get_arvif(ar, peer->vdev_id);
	if (!arvif) {
		ath10k_warn(ar, "received addba event for invalid vdev_id: %u\n",
			    peer->vdev_id);
		spin_unlock_bh(&ar->data_lock);
		return;
	}

	ath10k_dbg(ar, ATH10K_DBG_HTT,
		   "htt rx start rx ba session sta %pM tid %u size %u\n",
		   peer->addr, tid, ev->window_size);

	ieee80211_start_rx_ba_session_offl(arvif->vif, peer->addr, tid);
	spin_unlock_bh(&ar->data_lock);
}

static void ath10k_htt_rx_delba(struct ath10k *ar, struct htt_resp *resp)
{
	struct htt_rx_delba *ev = &resp->rx_delba;
	struct ath10k_peer *peer;
	struct ath10k_vif *arvif;
	u16 info0, tid, peer_id;

	info0 = __le16_to_cpu(ev->info0);
	tid = MS(info0, HTT_RX_BA_INFO0_TID);
	peer_id = MS(info0, HTT_RX_BA_INFO0_PEER_ID);

	ath10k_dbg(ar, ATH10K_DBG_HTT,
		   "htt rx delba tid %u peer_id %u\n",
		   tid, peer_id);

	spin_lock_bh(&ar->data_lock);
	peer = ath10k_peer_find_by_id(ar, peer_id);
	if (!peer) {
		ath10k_warn(ar, "received addba event for invalid peer_id: %u\n",
			    peer_id);
		spin_unlock_bh(&ar->data_lock);
		return;
	}

	arvif = ath10k_get_arvif(ar, peer->vdev_id);
	if (!arvif) {
		ath10k_warn(ar, "received addba event for invalid vdev_id: %u\n",
			    peer->vdev_id);
		spin_unlock_bh(&ar->data_lock);
		return;
	}

	ath10k_dbg(ar, ATH10K_DBG_HTT,
		   "htt rx stop rx ba session sta %pM tid %u\n",
		   peer->addr, tid);

	ieee80211_stop_rx_ba_session_offl(arvif->vif, peer->addr, tid);
	spin_unlock_bh(&ar->data_lock);
}

static int ath10k_htt_rx_extract_amsdu(struct ath10k_hw_params *hw,
				       struct sk_buff_head *list,
				       struct sk_buff_head *amsdu)
{
	struct sk_buff *msdu;
	struct htt_rx_desc *rxd;
	struct rx_msdu_end_common *rxd_msdu_end_common;

	if (skb_queue_empty(list))
		return -ENOBUFS;

	if (WARN_ON(!skb_queue_empty(amsdu)))
		return -EINVAL;

	while ((msdu = __skb_dequeue(list))) {
		__skb_queue_tail(amsdu, msdu);

		rxd = HTT_RX_BUF_TO_RX_DESC(hw,
					    (void *)msdu->data -
					    hw->rx_desc_ops->rx_desc_size);

		rxd_msdu_end_common = ath10k_htt_rx_desc_get_msdu_end(hw, rxd);
		if (rxd_msdu_end_common->info0 &
		    __cpu_to_le32(RX_MSDU_END_INFO0_LAST_MSDU))
			break;
	}

	msdu = skb_peek_tail(amsdu);
	rxd = HTT_RX_BUF_TO_RX_DESC(hw,
				    (void *)msdu->data - hw->rx_desc_ops->rx_desc_size);

	rxd_msdu_end_common = ath10k_htt_rx_desc_get_msdu_end(hw, rxd);
	if (!(rxd_msdu_end_common->info0 &
	      __cpu_to_le32(RX_MSDU_END_INFO0_LAST_MSDU))) {
		skb_queue_splice_init(amsdu, list);
		return -EAGAIN;
	}

	return 0;
}

static void ath10k_htt_rx_h_rx_offload_prot(struct ieee80211_rx_status *status,
					    struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	if (!ieee80211_has_protected(hdr->frame_control))
		return;

	/* Offloaded frames are already decrypted but firmware insists they are
	 * protected in the 802.11 header. Strip the flag.  Otherwise mac80211
	 * will drop the frame.
	 */

	hdr->frame_control &= ~__cpu_to_le16(IEEE80211_FCTL_PROTECTED);
	status->flag |= RX_FLAG_DECRYPTED |
			RX_FLAG_IV_STRIPPED |
			RX_FLAG_MMIC_STRIPPED;
}

static void ath10k_htt_rx_h_rx_offload(struct ath10k *ar,
				       struct sk_buff_head *list)
{
	struct ath10k_htt *htt = &ar->htt;
	struct ieee80211_rx_status *status = &htt->rx_status;
	struct htt_rx_offload_msdu *rx;
	struct sk_buff *msdu;
	size_t offset;

	while ((msdu = __skb_dequeue(list))) {
		/* Offloaded frames don't have Rx descriptor. Instead they have
		 * a short meta information header.
		 */

		rx = (void *)msdu->data;

		skb_put(msdu, sizeof(*rx));
		skb_pull(msdu, sizeof(*rx));

		if (skb_tailroom(msdu) < __le16_to_cpu(rx->msdu_len)) {
			ath10k_warn(ar, "dropping frame: offloaded rx msdu is too long!\n");
			dev_kfree_skb_any(msdu);
			continue;
		}

		skb_put(msdu, __le16_to_cpu(rx->msdu_len));

		/* Offloaded rx header length isn't multiple of 2 nor 4 so the
		 * actual payload is unaligned. Align the frame.  Otherwise
		 * mac80211 complains.  This shouldn't reduce performance much
		 * because these offloaded frames are rare.
		 */
		offset = 4 - ((unsigned long)msdu->data & 3);
		skb_put(msdu, offset);
		memmove(msdu->data + offset, msdu->data, msdu->len);
		skb_pull(msdu, offset);

		/* FIXME: The frame is NWifi. Re-construct QoS Control
		 * if possible later.
		 */

		memset(status, 0, sizeof(*status));
		status->flag |= RX_FLAG_NO_SIGNAL_VAL;

		ath10k_htt_rx_h_rx_offload_prot(status, msdu);
		ath10k_htt_rx_h_channel(ar, status, NULL, rx->vdev_id);
		ath10k_htt_rx_h_queue_msdu(ar, status, msdu);
	}
}

static int ath10k_htt_rx_in_ord_ind(struct ath10k *ar, struct sk_buff *skb)
{
	struct ath10k_htt *htt = &ar->htt;
	struct htt_resp *resp = (void *)skb->data;
	struct ieee80211_rx_status *status = &htt->rx_status;
	struct sk_buff_head list;
	struct sk_buff_head amsdu;
	u16 peer_id;
	u16 msdu_count;
	u8 vdev_id;
	u8 tid;
	bool offload;
	bool frag;
	int ret;

	lockdep_assert_held(&htt->rx_ring.lock);

	if (htt->rx_confused)
		return -EIO;

	skb_pull(skb, sizeof(resp->hdr));
	skb_pull(skb, sizeof(resp->rx_in_ord_ind));

	peer_id = __le16_to_cpu(resp->rx_in_ord_ind.peer_id);
	msdu_count = __le16_to_cpu(resp->rx_in_ord_ind.msdu_count);
	vdev_id = resp->rx_in_ord_ind.vdev_id;
	tid = SM(resp->rx_in_ord_ind.info, HTT_RX_IN_ORD_IND_INFO_TID);
	offload = !!(resp->rx_in_ord_ind.info &
			HTT_RX_IN_ORD_IND_INFO_OFFLOAD_MASK);
	frag = !!(resp->rx_in_ord_ind.info & HTT_RX_IN_ORD_IND_INFO_FRAG_MASK);

	ath10k_dbg(ar, ATH10K_DBG_HTT,
		   "htt rx in ord vdev %i peer %i tid %i offload %i frag %i msdu count %i\n",
		   vdev_id, peer_id, tid, offload, frag, msdu_count);

	if (skb->len < msdu_count * sizeof(*resp->rx_in_ord_ind.msdu_descs32)) {
		ath10k_warn(ar, "dropping invalid in order rx indication\n");
		return -EINVAL;
	}

	/* The event can deliver more than 1 A-MSDU. Each A-MSDU is later
	 * extracted and processed.
	 */
	__skb_queue_head_init(&list);
	if (ar->hw_params.target_64bit)
		ret = ath10k_htt_rx_pop_paddr64_list(htt, &resp->rx_in_ord_ind,
						     &list);
	else
		ret = ath10k_htt_rx_pop_paddr32_list(htt, &resp->rx_in_ord_ind,
						     &list);

	if (ret < 0) {
		ath10k_warn(ar, "failed to pop paddr list: %d\n", ret);
		htt->rx_confused = true;
		return -EIO;
	}

	/* Offloaded frames are very different and need to be handled
	 * separately.
	 */
	if (offload)
		ath10k_htt_rx_h_rx_offload(ar, &list);

	while (!skb_queue_empty(&list)) {
		__skb_queue_head_init(&amsdu);
		ret = ath10k_htt_rx_extract_amsdu(&ar->hw_params, &list, &amsdu);
		switch (ret) {
		case 0:
			/* Note: The in-order indication may report interleaved
			 * frames from different PPDUs meaning reported rx rate
			 * to mac80211 isn't accurate/reliable. It's still
			 * better to report something than nothing though. This
			 * should still give an idea about rx rate to the user.
			 */
			ath10k_htt_rx_h_ppdu(ar, &amsdu, status, vdev_id);
			ath10k_htt_rx_h_filter(ar, &amsdu, status, NULL);
			ath10k_htt_rx_h_mpdu(ar, &amsdu, status, false, NULL,
					     NULL, peer_id, frag);
			ath10k_htt_rx_h_enqueue(ar, &amsdu, status);
			break;
		case -EAGAIN:
			fallthrough;
		default:
			/* Should not happen. */
			ath10k_warn(ar, "failed to extract amsdu: %d\n", ret);
			htt->rx_confused = true;
			__skb_queue_purge(&list);
			return -EIO;
		}
	}
	return ret;
}

static void ath10k_htt_rx_tx_fetch_resp_id_confirm(struct ath10k *ar,
						   const __le32 *resp_ids,
						   int num_resp_ids)
{
	int i;
	u32 resp_id;

	ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx tx fetch confirm num_resp_ids %d\n",
		   num_resp_ids);

	for (i = 0; i < num_resp_ids; i++) {
		resp_id = le32_to_cpu(resp_ids[i]);

		ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx tx fetch confirm resp_id %u\n",
			   resp_id);

		/* TODO: free resp_id */
	}
}

static void ath10k_htt_rx_tx_fetch_ind(struct ath10k *ar, struct sk_buff *skb)
{
	struct ieee80211_hw *hw = ar->hw;
	struct ieee80211_txq *txq;
	struct htt_resp *resp = (struct htt_resp *)skb->data;
	struct htt_tx_fetch_record *record;
	size_t len;
	size_t max_num_bytes;
	size_t max_num_msdus;
	size_t num_bytes;
	size_t num_msdus;
	const __le32 *resp_ids;
	u16 num_records;
	u16 num_resp_ids;
	u16 peer_id;
	u8 tid;
	int ret;
	int i;
	bool may_tx;

	ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx tx fetch ind\n");

	len = sizeof(resp->hdr) + sizeof(resp->tx_fetch_ind);
	if (unlikely(skb->len < len)) {
		ath10k_warn(ar, "received corrupted tx_fetch_ind event: buffer too short\n");
		return;
	}

	num_records = le16_to_cpu(resp->tx_fetch_ind.num_records);
	num_resp_ids = le16_to_cpu(resp->tx_fetch_ind.num_resp_ids);

	len += sizeof(resp->tx_fetch_ind.records[0]) * num_records;
	len += sizeof(resp->tx_fetch_ind.resp_ids[0]) * num_resp_ids;

	if (unlikely(skb->len < len)) {
		ath10k_warn(ar, "received corrupted tx_fetch_ind event: too many records/resp_ids\n");
		return;
	}

	ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx tx fetch ind num records %u num resps %u seq %u\n",
		   num_records, num_resp_ids,
		   le16_to_cpu(resp->tx_fetch_ind.fetch_seq_num));

	if (!ar->htt.tx_q_state.enabled) {
		ath10k_warn(ar, "received unexpected tx_fetch_ind event: not enabled\n");
		return;
	}

	if (ar->htt.tx_q_state.mode == HTT_TX_MODE_SWITCH_PUSH) {
		ath10k_warn(ar, "received unexpected tx_fetch_ind event: in push mode\n");
		return;
	}

	rcu_read_lock();

	for (i = 0; i < num_records; i++) {
		record = &resp->tx_fetch_ind.records[i];
		peer_id = MS(le16_to_cpu(record->info),
			     HTT_TX_FETCH_RECORD_INFO_PEER_ID);
		tid = MS(le16_to_cpu(record->info),
			 HTT_TX_FETCH_RECORD_INFO_TID);
		max_num_msdus = le16_to_cpu(record->num_msdus);
		max_num_bytes = le32_to_cpu(record->num_bytes);

		ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx tx fetch record %i peer_id %u tid %u msdus %zu bytes %zu\n",
			   i, peer_id, tid, max_num_msdus, max_num_bytes);

		if (unlikely(peer_id >= ar->htt.tx_q_state.num_peers) ||
		    unlikely(tid >= ar->htt.tx_q_state.num_tids)) {
			ath10k_warn(ar, "received out of range peer_id %u tid %u\n",
				    peer_id, tid);
			continue;
		}

		spin_lock_bh(&ar->data_lock);
		txq = ath10k_mac_txq_lookup(ar, peer_id, tid);
		spin_unlock_bh(&ar->data_lock);

		/* It is okay to release the lock and use txq because RCU read
		 * lock is held.
		 */

		if (unlikely(!txq)) {
			ath10k_warn(ar, "failed to lookup txq for peer_id %u tid %u\n",
				    peer_id, tid);
			continue;
		}

		num_msdus = 0;
		num_bytes = 0;

		ieee80211_txq_schedule_start(hw, txq->ac);
		may_tx = ieee80211_txq_may_transmit(hw, txq);
		while (num_msdus < max_num_msdus &&
		       num_bytes < max_num_bytes) {
			if (!may_tx)
				break;

			ret = ath10k_mac_tx_push_txq(hw, txq);
			if (ret < 0)
				break;

			num_msdus++;
			num_bytes += ret;
		}
		ieee80211_return_txq(hw, txq, false);
		ieee80211_txq_schedule_end(hw, txq->ac);

		record->num_msdus = cpu_to_le16(num_msdus);
		record->num_bytes = cpu_to_le32(num_bytes);

		ath10k_htt_tx_txq_recalc(hw, txq);
	}

	rcu_read_unlock();

	resp_ids = ath10k_htt_get_tx_fetch_ind_resp_ids(&resp->tx_fetch_ind);
	ath10k_htt_rx_tx_fetch_resp_id_confirm(ar, resp_ids, num_resp_ids);

	ret = ath10k_htt_tx_fetch_resp(ar,
				       resp->tx_fetch_ind.token,
				       resp->tx_fetch_ind.fetch_seq_num,
				       resp->tx_fetch_ind.records,
				       num_records);
	if (unlikely(ret)) {
		ath10k_warn(ar, "failed to submit tx fetch resp for token 0x%08x: %d\n",
			    le32_to_cpu(resp->tx_fetch_ind.token), ret);
		/* FIXME: request fw restart */
	}

	ath10k_htt_tx_txq_sync(ar);
}

static void ath10k_htt_rx_tx_fetch_confirm(struct ath10k *ar,
					   struct sk_buff *skb)
{
	const struct htt_resp *resp = (void *)skb->data;
	size_t len;
	int num_resp_ids;

	ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx tx fetch confirm\n");

	len = sizeof(resp->hdr) + sizeof(resp->tx_fetch_confirm);
	if (unlikely(skb->len < len)) {
		ath10k_warn(ar, "received corrupted tx_fetch_confirm event: buffer too short\n");
		return;
	}

	num_resp_ids = le16_to_cpu(resp->tx_fetch_confirm.num_resp_ids);
	len += sizeof(resp->tx_fetch_confirm.resp_ids[0]) * num_resp_ids;

	if (unlikely(skb->len < len)) {
		ath10k_warn(ar, "received corrupted tx_fetch_confirm event: resp_ids buffer overflow\n");
		return;
	}

	ath10k_htt_rx_tx_fetch_resp_id_confirm(ar,
					       resp->tx_fetch_confirm.resp_ids,
					       num_resp_ids);
}

static void ath10k_htt_rx_tx_mode_switch_ind(struct ath10k *ar,
					     struct sk_buff *skb)
{
	const struct htt_resp *resp = (void *)skb->data;
	const struct htt_tx_mode_switch_record *record;
	struct ieee80211_txq *txq;
	struct ath10k_txq *artxq;
	size_t len;
	size_t num_records;
	enum htt_tx_mode_switch_mode mode;
	bool enable;
	u16 info0;
	u16 info1;
	u16 threshold;
	u16 peer_id;
	u8 tid;
	int i;

	ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx tx mode switch ind\n");

	len = sizeof(resp->hdr) + sizeof(resp->tx_mode_switch_ind);
	if (unlikely(skb->len < len)) {
		ath10k_warn(ar, "received corrupted tx_mode_switch_ind event: buffer too short\n");
		return;
	}

	info0 = le16_to_cpu(resp->tx_mode_switch_ind.info0);
	info1 = le16_to_cpu(resp->tx_mode_switch_ind.info1);

	enable = !!(info0 & HTT_TX_MODE_SWITCH_IND_INFO0_ENABLE);
	num_records = MS(info0, HTT_TX_MODE_SWITCH_IND_INFO1_THRESHOLD);
	mode = MS(info1, HTT_TX_MODE_SWITCH_IND_INFO1_MODE);
	threshold = MS(info1, HTT_TX_MODE_SWITCH_IND_INFO1_THRESHOLD);

	ath10k_dbg(ar, ATH10K_DBG_HTT,
		   "htt rx tx mode switch ind info0 0x%04hx info1 0x%04x enable %d num records %zd mode %d threshold %u\n",
		   info0, info1, enable, num_records, mode, threshold);

	len += sizeof(resp->tx_mode_switch_ind.records[0]) * num_records;

	if (unlikely(skb->len < len)) {
		ath10k_warn(ar, "received corrupted tx_mode_switch_mode_ind event: too many records\n");
		return;
	}

	switch (mode) {
	case HTT_TX_MODE_SWITCH_PUSH:
	case HTT_TX_MODE_SWITCH_PUSH_PULL:
		break;
	default:
		ath10k_warn(ar, "received invalid tx_mode_switch_mode_ind mode %d, ignoring\n",
			    mode);
		return;
	}

	if (!enable)
		return;

	ar->htt.tx_q_state.enabled = enable;
	ar->htt.tx_q_state.mode = mode;
	ar->htt.tx_q_state.num_push_allowed = threshold;

	rcu_read_lock();

	for (i = 0; i < num_records; i++) {
		record = &resp->tx_mode_switch_ind.records[i];
		info0 = le16_to_cpu(record->info0);
		peer_id = MS(info0, HTT_TX_MODE_SWITCH_RECORD_INFO0_PEER_ID);
		tid = MS(info0, HTT_TX_MODE_SWITCH_RECORD_INFO0_TID);

		if (unlikely(peer_id >= ar->htt.tx_q_state.num_peers) ||
		    unlikely(tid >= ar->htt.tx_q_state.num_tids)) {
			ath10k_warn(ar, "received out of range peer_id %u tid %u\n",
				    peer_id, tid);
			continue;
		}

		spin_lock_bh(&ar->data_lock);
		txq = ath10k_mac_txq_lookup(ar, peer_id, tid);
		spin_unlock_bh(&ar->data_lock);

		/* It is okay to release the lock and use txq because RCU read
		 * lock is held.
		 */

		if (unlikely(!txq)) {
			ath10k_warn(ar, "failed to lookup txq for peer_id %u tid %u\n",
				    peer_id, tid);
			continue;
		}

		spin_lock_bh(&ar->htt.tx_lock);
		artxq = (void *)txq->drv_priv;
		artxq->num_push_allowed = le16_to_cpu(record->num_max_msdus);
		spin_unlock_bh(&ar->htt.tx_lock);
	}

	rcu_read_unlock();

	ath10k_mac_tx_push_pending(ar);
}

void ath10k_htt_htc_t2h_msg_handler(struct ath10k *ar, struct sk_buff *skb)
{
	bool release;

	release = ath10k_htt_t2h_msg_handler(ar, skb);

	/* Free the indication buffer */
	if (release)
		dev_kfree_skb_any(skb);
}

static inline s8 ath10k_get_legacy_rate_idx(struct ath10k *ar, u8 rate)
{
	static const u8 legacy_rates[] = {1, 2, 5, 11, 6, 9, 12,
					  18, 24, 36, 48, 54};
	int i;

	for (i = 0; i < ARRAY_SIZE(legacy_rates); i++) {
		if (rate == legacy_rates[i])
			return i;
	}

	ath10k_warn(ar, "Invalid legacy rate %d peer stats", rate);
	return -EINVAL;
}

static void
ath10k_accumulate_per_peer_tx_stats(struct ath10k *ar,
				    struct ath10k_sta *arsta,
				    struct ath10k_per_peer_tx_stats *pstats,
				    s8 legacy_rate_idx)
{
	struct rate_info *txrate = &arsta->txrate;
	struct ath10k_htt_tx_stats *tx_stats;
	int idx, ht_idx, gi, mcs, bw, nss;
	unsigned long flags;

	if (!arsta->tx_stats)
		return;

	tx_stats = arsta->tx_stats;
	flags = txrate->flags;
	gi = test_bit(ATH10K_RATE_INFO_FLAGS_SGI_BIT, &flags);
	mcs = ATH10K_HW_MCS_RATE(pstats->ratecode);
	bw = txrate->bw;
	nss = txrate->nss;
	ht_idx = mcs + (nss - 1) * 8;
	idx = mcs * 8 + 8 * 10 * (nss - 1);
	idx += bw * 2 + gi;

#define STATS_OP_FMT(name) tx_stats->stats[ATH10K_STATS_TYPE_##name]

	if (txrate->flags & RATE_INFO_FLAGS_VHT_MCS) {
		STATS_OP_FMT(SUCC).vht[0][mcs] += pstats->succ_bytes;
		STATS_OP_FMT(SUCC).vht[1][mcs] += pstats->succ_pkts;
		STATS_OP_FMT(FAIL).vht[0][mcs] += pstats->failed_bytes;
		STATS_OP_FMT(FAIL).vht[1][mcs] += pstats->failed_pkts;
		STATS_OP_FMT(RETRY).vht[0][mcs] += pstats->retry_bytes;
		STATS_OP_FMT(RETRY).vht[1][mcs] += pstats->retry_pkts;
	} else if (txrate->flags & RATE_INFO_FLAGS_MCS) {
		STATS_OP_FMT(SUCC).ht[0][ht_idx] += pstats->succ_bytes;
		STATS_OP_FMT(SUCC).ht[1][ht_idx] += pstats->succ_pkts;
		STATS_OP_FMT(FAIL).ht[0][ht_idx] += pstats->failed_bytes;
		STATS_OP_FMT(FAIL).ht[1][ht_idx] += pstats->failed_pkts;
		STATS_OP_FMT(RETRY).ht[0][ht_idx] += pstats->retry_bytes;
		STATS_OP_FMT(RETRY).ht[1][ht_idx] += pstats->retry_pkts;
	} else {
		mcs = legacy_rate_idx;

		STATS_OP_FMT(SUCC).legacy[0][mcs] += pstats->succ_bytes;
		STATS_OP_FMT(SUCC).legacy[1][mcs] += pstats->succ_pkts;
		STATS_OP_FMT(FAIL).legacy[0][mcs] += pstats->failed_bytes;
		STATS_OP_FMT(FAIL).legacy[1][mcs] += pstats->failed_pkts;
		STATS_OP_FMT(RETRY).legacy[0][mcs] += pstats->retry_bytes;
		STATS_OP_FMT(RETRY).legacy[1][mcs] += pstats->retry_pkts;
	}

	if (ATH10K_HW_AMPDU(pstats->flags)) {
		tx_stats->ba_fails += ATH10K_HW_BA_FAIL(pstats->flags);

		if (txrate->flags & RATE_INFO_FLAGS_MCS) {
			STATS_OP_FMT(AMPDU).ht[0][ht_idx] +=
				pstats->succ_bytes + pstats->retry_bytes;
			STATS_OP_FMT(AMPDU).ht[1][ht_idx] +=
				pstats->succ_pkts + pstats->retry_pkts;
		} else {
			STATS_OP_FMT(AMPDU).vht[0][mcs] +=
				pstats->succ_bytes + pstats->retry_bytes;
			STATS_OP_FMT(AMPDU).vht[1][mcs] +=
				pstats->succ_pkts + pstats->retry_pkts;
		}
		STATS_OP_FMT(AMPDU).bw[0][bw] +=
			pstats->succ_bytes + pstats->retry_bytes;
		STATS_OP_FMT(AMPDU).nss[0][nss - 1] +=
			pstats->succ_bytes + pstats->retry_bytes;
		STATS_OP_FMT(AMPDU).gi[0][gi] +=
			pstats->succ_bytes + pstats->retry_bytes;
		STATS_OP_FMT(AMPDU).rate_table[0][idx] +=
			pstats->succ_bytes + pstats->retry_bytes;
		STATS_OP_FMT(AMPDU).bw[1][bw] +=
			pstats->succ_pkts + pstats->retry_pkts;
		STATS_OP_FMT(AMPDU).nss[1][nss - 1] +=
			pstats->succ_pkts + pstats->retry_pkts;
		STATS_OP_FMT(AMPDU).gi[1][gi] +=
			pstats->succ_pkts + pstats->retry_pkts;
		STATS_OP_FMT(AMPDU).rate_table[1][idx] +=
			pstats->succ_pkts + pstats->retry_pkts;
	} else {
		tx_stats->ack_fails +=
				ATH10K_HW_BA_FAIL(pstats->flags);
	}

	STATS_OP_FMT(SUCC).bw[0][bw] += pstats->succ_bytes;
	STATS_OP_FMT(SUCC).nss[0][nss - 1] += pstats->succ_bytes;
	STATS_OP_FMT(SUCC).gi[0][gi] += pstats->succ_bytes;

	STATS_OP_FMT(SUCC).bw[1][bw] += pstats->succ_pkts;
	STATS_OP_FMT(SUCC).nss[1][nss - 1] += pstats->succ_pkts;
	STATS_OP_FMT(SUCC).gi[1][gi] += pstats->succ_pkts;

	STATS_OP_FMT(FAIL).bw[0][bw] += pstats->failed_bytes;
	STATS_OP_FMT(FAIL).nss[0][nss - 1] += pstats->failed_bytes;
	STATS_OP_FMT(FAIL).gi[0][gi] += pstats->failed_bytes;

	STATS_OP_FMT(FAIL).bw[1][bw] += pstats->failed_pkts;
	STATS_OP_FMT(FAIL).nss[1][nss - 1] += pstats->failed_pkts;
	STATS_OP_FMT(FAIL).gi[1][gi] += pstats->failed_pkts;

	STATS_OP_FMT(RETRY).bw[0][bw] += pstats->retry_bytes;
	STATS_OP_FMT(RETRY).nss[0][nss - 1] += pstats->retry_bytes;
	STATS_OP_FMT(RETRY).gi[0][gi] += pstats->retry_bytes;

	STATS_OP_FMT(RETRY).bw[1][bw] += pstats->retry_pkts;
	STATS_OP_FMT(RETRY).nss[1][nss - 1] += pstats->retry_pkts;
	STATS_OP_FMT(RETRY).gi[1][gi] += pstats->retry_pkts;

	if (txrate->flags >= RATE_INFO_FLAGS_MCS) {
		STATS_OP_FMT(SUCC).rate_table[0][idx] += pstats->succ_bytes;
		STATS_OP_FMT(SUCC).rate_table[1][idx] += pstats->succ_pkts;
		STATS_OP_FMT(FAIL).rate_table[0][idx] += pstats->failed_bytes;
		STATS_OP_FMT(FAIL).rate_table[1][idx] += pstats->failed_pkts;
		STATS_OP_FMT(RETRY).rate_table[0][idx] += pstats->retry_bytes;
		STATS_OP_FMT(RETRY).rate_table[1][idx] += pstats->retry_pkts;
	}

	tx_stats->tx_duration += pstats->duration;
}

static void
ath10k_update_per_peer_tx_stats(struct ath10k *ar,
				struct ieee80211_sta *sta,
				struct ath10k_per_peer_tx_stats *peer_stats)
{
	struct ath10k_sta *arsta = (struct ath10k_sta *)sta->drv_priv;
	struct ieee80211_chanctx_conf *conf = NULL;
	u8 rate = 0, sgi;
	s8 rate_idx = 0;
	bool skip_auto_rate;
	struct rate_info txrate;

	lockdep_assert_held(&ar->data_lock);

	txrate.flags = ATH10K_HW_PREAMBLE(peer_stats->ratecode);
	txrate.bw = ATH10K_HW_BW(peer_stats->flags);
	txrate.nss = ATH10K_HW_NSS(peer_stats->ratecode);
	txrate.mcs = ATH10K_HW_MCS_RATE(peer_stats->ratecode);
	sgi = ATH10K_HW_GI(peer_stats->flags);
	skip_auto_rate = ATH10K_FW_SKIPPED_RATE_CTRL(peer_stats->flags);

	/* Firmware's rate control skips broadcast/management frames,
	 * if host has configure fixed rates and in some other special cases.
	 */
	if (skip_auto_rate)
		return;

	if (txrate.flags == WMI_RATE_PREAMBLE_VHT && txrate.mcs > 9) {
		ath10k_warn(ar, "Invalid VHT mcs %d peer stats",  txrate.mcs);
		return;
	}

	if (txrate.flags == WMI_RATE_PREAMBLE_HT &&
	    (txrate.mcs > 7 || txrate.nss < 1)) {
		ath10k_warn(ar, "Invalid HT mcs %d nss %d peer stats",
			    txrate.mcs, txrate.nss);
		return;
	}

	memset(&arsta->txrate, 0, sizeof(arsta->txrate));
	memset(&arsta->tx_info.status, 0, sizeof(arsta->tx_info.status));
	if (txrate.flags == WMI_RATE_PREAMBLE_CCK ||
	    txrate.flags == WMI_RATE_PREAMBLE_OFDM) {
		rate = ATH10K_HW_LEGACY_RATE(peer_stats->ratecode);
		/* This is hacky, FW sends CCK rate 5.5Mbps as 6 */
		if (rate == 6 && txrate.flags == WMI_RATE_PREAMBLE_CCK)
			rate = 5;
		rate_idx = ath10k_get_legacy_rate_idx(ar, rate);
		if (rate_idx < 0)
			return;
		arsta->txrate.legacy = rate;
	} else if (txrate.flags == WMI_RATE_PREAMBLE_HT) {
		arsta->txrate.flags = RATE_INFO_FLAGS_MCS;
		arsta->txrate.mcs = txrate.mcs + 8 * (txrate.nss - 1);
	} else {
		arsta->txrate.flags = RATE_INFO_FLAGS_VHT_MCS;
		arsta->txrate.mcs = txrate.mcs;
	}

	switch (txrate.flags) {
	case WMI_RATE_PREAMBLE_OFDM:
		if (arsta->arvif && arsta->arvif->vif)
			conf = rcu_dereference(arsta->arvif->vif->chanctx_conf);
		if (conf && conf->def.chan->band == NL80211_BAND_5GHZ)
			arsta->tx_info.status.rates[0].idx = rate_idx - 4;
		break;
	case WMI_RATE_PREAMBLE_CCK:
		arsta->tx_info.status.rates[0].idx = rate_idx;
		if (sgi)
			arsta->tx_info.status.rates[0].flags |=
				(IEEE80211_TX_RC_USE_SHORT_PREAMBLE |
				 IEEE80211_TX_RC_SHORT_GI);
		break;
	case WMI_RATE_PREAMBLE_HT:
		arsta->tx_info.status.rates[0].idx =
				txrate.mcs + ((txrate.nss - 1) * 8);
		if (sgi)
			arsta->tx_info.status.rates[0].flags |=
					IEEE80211_TX_RC_SHORT_GI;
		arsta->tx_info.status.rates[0].flags |= IEEE80211_TX_RC_MCS;
		break;
	case WMI_RATE_PREAMBLE_VHT:
		ieee80211_rate_set_vht(&arsta->tx_info.status.rates[0],
				       txrate.mcs, txrate.nss);
		if (sgi)
			arsta->tx_info.status.rates[0].flags |=
						IEEE80211_TX_RC_SHORT_GI;
		arsta->tx_info.status.rates[0].flags |= IEEE80211_TX_RC_VHT_MCS;
		break;
	}

	arsta->txrate.nss = txrate.nss;
	arsta->txrate.bw = ath10k_bw_to_mac80211_bw(txrate.bw);
	arsta->last_tx_bitrate = cfg80211_calculate_bitrate(&arsta->txrate);
	if (sgi)
		arsta->txrate.flags |= RATE_INFO_FLAGS_SHORT_GI;

	switch (arsta->txrate.bw) {
	case RATE_INFO_BW_40:
		arsta->tx_info.status.rates[0].flags |=
				IEEE80211_TX_RC_40_MHZ_WIDTH;
		break;
	case RATE_INFO_BW_80:
		arsta->tx_info.status.rates[0].flags |=
				IEEE80211_TX_RC_80_MHZ_WIDTH;
		break;
	}

	if (peer_stats->succ_pkts) {
		arsta->tx_info.flags = IEEE80211_TX_STAT_ACK;
		arsta->tx_info.status.rates[0].count = 1;
		ieee80211_tx_rate_update(ar->hw, sta, &arsta->tx_info);
	}

	if (ar->htt.disable_tx_comp) {
		arsta->tx_failed += peer_stats->failed_pkts;
		ath10k_dbg(ar, ATH10K_DBG_HTT, "tx failed %d\n",
			   arsta->tx_failed);
	}

	arsta->tx_retries += peer_stats->retry_pkts;
	ath10k_dbg(ar, ATH10K_DBG_HTT, "htt tx retries %d", arsta->tx_retries);

	if (ath10k_debug_is_extd_tx_stats_enabled(ar))
		ath10k_accumulate_per_peer_tx_stats(ar, arsta, peer_stats,
						    rate_idx);
}

static void ath10k_htt_fetch_peer_stats(struct ath10k *ar,
					struct sk_buff *skb)
{
	struct htt_resp *resp = (struct htt_resp *)skb->data;
	struct ath10k_per_peer_tx_stats *p_tx_stats = &ar->peer_tx_stats;
	struct htt_per_peer_tx_stats_ind *tx_stats;
	struct ieee80211_sta *sta;
	struct ath10k_peer *peer;
	int peer_id, i;
	u8 ppdu_len, num_ppdu;

	num_ppdu = resp->peer_tx_stats.num_ppdu;
	ppdu_len = resp->peer_tx_stats.ppdu_len * sizeof(__le32);

	if (skb->len < sizeof(struct htt_resp_hdr) + num_ppdu * ppdu_len) {
		ath10k_warn(ar, "Invalid peer stats buf length %d\n", skb->len);
		return;
	}

	tx_stats = (struct htt_per_peer_tx_stats_ind *)
			(resp->peer_tx_stats.payload);
	peer_id = __le16_to_cpu(tx_stats->peer_id);

	rcu_read_lock();
	spin_lock_bh(&ar->data_lock);
	peer = ath10k_peer_find_by_id(ar, peer_id);
	if (!peer || !peer->sta) {
		ath10k_warn(ar, "Invalid peer id %d peer stats buffer\n",
			    peer_id);
		goto out;
	}

	sta = peer->sta;
	for (i = 0; i < num_ppdu; i++) {
		tx_stats = (struct htt_per_peer_tx_stats_ind *)
			   (resp->peer_tx_stats.payload + i * ppdu_len);

		p_tx_stats->succ_bytes = __le32_to_cpu(tx_stats->succ_bytes);
		p_tx_stats->retry_bytes = __le32_to_cpu(tx_stats->retry_bytes);
		p_tx_stats->failed_bytes =
				__le32_to_cpu(tx_stats->failed_bytes);
		p_tx_stats->ratecode = tx_stats->ratecode;
		p_tx_stats->flags = tx_stats->flags;
		p_tx_stats->succ_pkts = __le16_to_cpu(tx_stats->succ_pkts);
		p_tx_stats->retry_pkts = __le16_to_cpu(tx_stats->retry_pkts);
		p_tx_stats->failed_pkts = __le16_to_cpu(tx_stats->failed_pkts);
		p_tx_stats->duration = __le16_to_cpu(tx_stats->tx_duration);

		ath10k_update_per_peer_tx_stats(ar, sta, p_tx_stats);
	}

out:
	spin_unlock_bh(&ar->data_lock);
	rcu_read_unlock();
}

static void ath10k_fetch_10_2_tx_stats(struct ath10k *ar, u8 *data)
{
	struct ath10k_pktlog_hdr *hdr = (struct ath10k_pktlog_hdr *)data;
	struct ath10k_per_peer_tx_stats *p_tx_stats = &ar->peer_tx_stats;
	struct ath10k_10_2_peer_tx_stats *tx_stats;
	struct ieee80211_sta *sta;
	struct ath10k_peer *peer;
	u16 log_type = __le16_to_cpu(hdr->log_type);
	u32 peer_id = 0, i;

	if (log_type != ATH_PKTLOG_TYPE_TX_STAT)
		return;

	tx_stats = (struct ath10k_10_2_peer_tx_stats *)((hdr->payload) +
		    ATH10K_10_2_TX_STATS_OFFSET);

	if (!tx_stats->tx_ppdu_cnt)
		return;

	peer_id = tx_stats->peer_id;

	rcu_read_lock();
	spin_lock_bh(&ar->data_lock);
	peer = ath10k_peer_find_by_id(ar, peer_id);
	if (!peer || !peer->sta) {
		ath10k_warn(ar, "Invalid peer id %d in peer stats buffer\n",
			    peer_id);
		goto out;
	}

	sta = peer->sta;
	for (i = 0; i < tx_stats->tx_ppdu_cnt; i++) {
		p_tx_stats->succ_bytes =
			__le16_to_cpu(tx_stats->success_bytes[i]);
		p_tx_stats->retry_bytes =
			__le16_to_cpu(tx_stats->retry_bytes[i]);
		p_tx_stats->failed_bytes =
			__le16_to_cpu(tx_stats->failed_bytes[i]);
		p_tx_stats->ratecode = tx_stats->ratecode[i];
		p_tx_stats->flags = tx_stats->flags[i];
		p_tx_stats->succ_pkts = tx_stats->success_pkts[i];
		p_tx_stats->retry_pkts = tx_stats->retry_pkts[i];
		p_tx_stats->failed_pkts = tx_stats->failed_pkts[i];

		ath10k_update_per_peer_tx_stats(ar, sta, p_tx_stats);
	}
	spin_unlock_bh(&ar->data_lock);
	rcu_read_unlock();

	return;

out:
	spin_unlock_bh(&ar->data_lock);
	rcu_read_unlock();
}

static int ath10k_htt_rx_pn_len(enum htt_security_types sec_type)
{
	switch (sec_type) {
	case HTT_SECURITY_TKIP:
	case HTT_SECURITY_TKIP_NOMIC:
	case HTT_SECURITY_AES_CCMP:
		return 48;
	default:
		return 0;
	}
}

static void ath10k_htt_rx_sec_ind_handler(struct ath10k *ar,
					  struct htt_security_indication *ev)
{
	enum htt_txrx_sec_cast_type sec_index;
	enum htt_security_types sec_type;
	struct ath10k_peer *peer;

	spin_lock_bh(&ar->data_lock);

	peer = ath10k_peer_find_by_id(ar, __le16_to_cpu(ev->peer_id));
	if (!peer) {
		ath10k_warn(ar, "failed to find peer id %d for security indication",
			    __le16_to_cpu(ev->peer_id));
		goto out;
	}

	sec_type = MS(ev->flags, HTT_SECURITY_TYPE);

	if (ev->flags & HTT_SECURITY_IS_UNICAST)
		sec_index = HTT_TXRX_SEC_UCAST;
	else
		sec_index = HTT_TXRX_SEC_MCAST;

	peer->rx_pn[sec_index].sec_type = sec_type;
	peer->rx_pn[sec_index].pn_len = ath10k_htt_rx_pn_len(sec_type);

	memset(peer->tids_last_pn_valid, 0, sizeof(peer->tids_last_pn_valid));
	memset(peer->tids_last_pn, 0, sizeof(peer->tids_last_pn));

out:
	spin_unlock_bh(&ar->data_lock);
}

bool ath10k_htt_t2h_msg_handler(struct ath10k *ar, struct sk_buff *skb)
{
	struct ath10k_htt *htt = &ar->htt;
	struct htt_resp *resp = (struct htt_resp *)skb->data;
	enum htt_t2h_msg_type type;

	/* confirm alignment */
	if (!IS_ALIGNED((unsigned long)skb->data, 4))
		ath10k_warn(ar, "unaligned htt message, expect trouble\n");

	ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx, msg_type: 0x%0X\n",
		   resp->hdr.msg_type);

	if (resp->hdr.msg_type >= ar->htt.t2h_msg_types_max) {
		ath10k_dbg(ar, ATH10K_DBG_HTT, "htt rx, unsupported msg_type: 0x%0X\n max: 0x%0X",
			   resp->hdr.msg_type, ar->htt.t2h_msg_types_max);
		return true;
	}
	type = ar->htt.t2h_msg_types[resp->hdr.msg_type];

	switch (type) {
	case HTT_T2H_MSG_TYPE_VERSION_CONF: {
		htt->target_version_major = resp->ver_resp.major;
		htt->target_version_minor = resp->ver_resp.minor;
		complete(&htt->target_version_received);
		break;
	}
	case HTT_T2H_MSG_TYPE_RX_IND:
		if (ar->bus_param.dev_type != ATH10K_DEV_TYPE_HL) {
			ath10k_htt_rx_proc_rx_ind_ll(htt, &resp->rx_ind);
		} else {
			skb_queue_tail(&htt->rx_indication_head, skb);
			return false;
		}
		break;
	case HTT_T2H_MSG_TYPE_PEER_MAP: {
		struct htt_peer_map_event ev = {
			.vdev_id = resp->peer_map.vdev_id,
			.peer_id = __le16_to_cpu(resp->peer_map.peer_id),
		};
		memcpy(ev.addr, resp->peer_map.addr, sizeof(ev.addr));
		ath10k_peer_map_event(htt, &ev);
		break;
	}
	case HTT_T2H_MSG_TYPE_PEER_UNMAP: {
		struct htt_peer_unmap_event ev = {
			.peer_id = __le16_to_cpu(resp->peer_unmap.peer_id),
		};
		ath10k_peer_unmap_event(htt, &ev);
		break;
	}
	case HTT_T2H_MSG_TYPE_MGMT_TX_COMPLETION: {
		struct htt_tx_done tx_done = {};
		struct ath10k_htt *htt = &ar->htt;
		struct ath10k_htc *htc = &ar->htc;
		struct ath10k_htc_ep *ep = &ar->htc.endpoint[htt->eid];
		int status = __le32_to_cpu(resp->mgmt_tx_completion.status);
		int info = __le32_to_cpu(resp->mgmt_tx_completion.info);

		tx_done.msdu_id = __le32_to_cpu(resp->mgmt_tx_completion.desc_id);

		switch (status) {
		case HTT_MGMT_TX_STATUS_OK:
			tx_done.status = HTT_TX_COMPL_STATE_ACK;
			if (test_bit(WMI_SERVICE_HTT_MGMT_TX_COMP_VALID_FLAGS,
				     ar->wmi.svc_map) &&
			    (resp->mgmt_tx_completion.flags &
			     HTT_MGMT_TX_CMPL_FLAG_ACK_RSSI)) {
				tx_done.ack_rssi =
				FIELD_GET(HTT_MGMT_TX_CMPL_INFO_ACK_RSSI_MASK,
					  info);
			}
			break;
		case HTT_MGMT_TX_STATUS_RETRY:
			tx_done.status = HTT_TX_COMPL_STATE_NOACK;
			break;
		case HTT_MGMT_TX_STATUS_DROP:
			tx_done.status = HTT_TX_COMPL_STATE_DISCARD;
			break;
		}

		if (htt->disable_tx_comp) {
			spin_lock_bh(&htc->tx_lock);
			ep->tx_credits++;
			spin_unlock_bh(&htc->tx_lock);
		}

		status = ath10k_txrx_tx_unref(htt, &tx_done);
		if (!status) {
			spin_lock_bh(&htt->tx_lock);
			ath10k_htt_tx_mgmt_dec_pending(htt);
			spin_unlock_bh(&htt->tx_lock);
		}
		break;
	}
	case HTT_T2H_MSG_TYPE_TX_COMPL_IND:
		ath10k_htt_rx_tx_compl_ind(htt->ar, skb);
		break;
	case HTT_T2H_MSG_TYPE_SEC_IND: {
		struct ath10k *ar = htt->ar;
		struct htt_security_indication *ev = &resp->security_indication;

		ath10k_htt_rx_sec_ind_handler(ar, ev);
		ath10k_dbg(ar, ATH10K_DBG_HTT,
			   "sec ind peer_id %d unicast %d type %d\n",
			  __le16_to_cpu(ev->peer_id),
			  !!(ev->flags & HTT_SECURITY_IS_UNICAST),
			  MS(ev->flags, HTT_SECURITY_TYPE));
		complete(&ar->install_key_done);
		break;
	}
	case HTT_T2H_MSG_TYPE_RX_FRAG_IND: {
		ath10k_dbg_dump(ar, ATH10K_DBG_HTT_DUMP, NULL, "htt event: ",
				skb->data, skb->len);
		atomic_inc(&htt->num_mpdus_ready);

		return ath10k_htt_rx_proc_rx_frag_ind(htt,
						      &resp->rx_frag_ind,
						      skb);
	}
	case HTT_T2H_MSG_TYPE_TEST:
		break;
	case HTT_T2H_MSG_TYPE_STATS_CONF:
		trace_ath10k_htt_stats(ar, skb->data, skb->len);
		break;
	case HTT_T2H_MSG_TYPE_TX_INSPECT_IND:
		/* Firmware can return tx frames if it's unable to fully
		 * process them and suspects host may be able to fix it. ath10k
		 * sends all tx frames as already inspected so this shouldn't
		 * happen unless fw has a bug.
		 */
		ath10k_warn(ar, "received an unexpected htt tx inspect event\n");
		break;
	case HTT_T2H_MSG_TYPE_RX_ADDBA:
		ath10k_htt_rx_addba(ar, resp);
		break;
	case HTT_T2H_MSG_TYPE_RX_DELBA:
		ath10k_htt_rx_delba(ar, resp);
		break;
	case HTT_T2H_MSG_TYPE_PKTLOG: {
		trace_ath10k_htt_pktlog(ar, resp->pktlog_msg.payload,
					skb->len -
					offsetof(struct htt_resp,
						 pktlog_msg.payload));

		if (ath10k_peer_stats_enabled(ar))
			ath10k_fetch_10_2_tx_stats(ar,
						   resp->pktlog_msg.payload);
		break;
	}
	case HTT_T2H_MSG_TYPE_RX_FLUSH: {
		/* Ignore this event because mac80211 takes care of Rx
		 * aggregation reordering.
		 */
		break;
	}
	case HTT_T2H_MSG_TYPE_RX_IN_ORD_PADDR_IND: {
		skb_queue_tail(&htt->rx_in_ord_compl_q, skb);
		return false;
	}
	case HTT_T2H_MSG_TYPE_TX_CREDIT_UPDATE_IND: {
		struct ath10k_htt *htt = &ar->htt;
		struct ath10k_htc *htc = &ar->htc;
		struct ath10k_htc_ep *ep = &ar->htc.endpoint[htt->eid];
		u32 msg_word = __le32_to_cpu(*(__le32 *)resp);
		int htt_credit_delta;

		htt_credit_delta = HTT_TX_CREDIT_DELTA_ABS_GET(msg_word);
		if (HTT_TX_CREDIT_SIGN_BIT_GET(msg_word))
			htt_credit_delta = -htt_credit_delta;

		ath10k_dbg(ar, ATH10K_DBG_HTT,
			   "htt credit update delta %d\n",
			   htt_credit_delta);

		if (htt->disable_tx_comp) {
			spin_lock_bh(&htc->tx_lock);
			ep->tx_credits += htt_credit_delta;
			spin_unlock_bh(&htc->tx_lock);
			ath10k_dbg(ar, ATH10K_DBG_HTT,
				   "htt credit total %d\n",
				   ep->tx_credits);
			ep->ep_ops.ep_tx_credits(htc->ar);
		}
		break;
	}
	case HTT_T2H_MSG_TYPE_CHAN_CHANGE: {
		u32 phymode = __le32_to_cpu(resp->chan_change.phymode);
		u32 freq = __le32_to_cpu(resp->chan_change.freq);

		ar->tgt_oper_chan = ieee80211_get_channel(ar->hw->wiphy, freq);
		ath10k_dbg(ar, ATH10K_DBG_HTT,
			   "htt chan change freq %u phymode %s\n",
			   freq, ath10k_wmi_phymode_str(phymode));
		break;
	}
	case HTT_T2H_MSG_TYPE_AGGR_CONF:
		break;
	case HTT_T2H_MSG_TYPE_TX_FETCH_IND: {
		struct sk_buff *tx_fetch_ind = skb_copy(skb, GFP_ATOMIC);

		if (!tx_fetch_ind) {
			ath10k_warn(ar, "failed to copy htt tx fetch ind\n");
			break;
		}
		skb_queue_tail(&htt->tx_fetch_ind_q, tx_fetch_ind);
		break;
	}
	case HTT_T2H_MSG_TYPE_TX_FETCH_CONFIRM:
		ath10k_htt_rx_tx_fetch_confirm(ar, skb);
		break;
	case HTT_T2H_MSG_TYPE_TX_MODE_SWITCH_IND:
		ath10k_htt_rx_tx_mode_switch_ind(ar, skb);
		break;
	case HTT_T2H_MSG_TYPE_PEER_STATS:
		ath10k_htt_fetch_peer_stats(ar, skb);
		break;
	case HTT_T2H_MSG_TYPE_EN_STATS:
	default:
		ath10k_warn(ar, "htt event (%d) not handled\n",
			    resp->hdr.msg_type);
		ath10k_dbg_dump(ar, ATH10K_DBG_HTT_DUMP, NULL, "htt event: ",
				skb->data, skb->len);
		break;
	}
	return true;
}
EXPORT_SYMBOL(ath10k_htt_t2h_msg_handler);

void ath10k_htt_rx_pktlog_completion_handler(struct ath10k *ar,
					     struct sk_buff *skb)
{
	trace_ath10k_htt_pktlog(ar, skb->data, skb->len);
	dev_kfree_skb_any(skb);
}
EXPORT_SYMBOL(ath10k_htt_rx_pktlog_completion_handler);

static int ath10k_htt_rx_deliver_msdu(struct ath10k *ar, int quota, int budget)
{
	struct sk_buff *skb;

	while (quota < budget) {
		if (skb_queue_empty(&ar->htt.rx_msdus_q))
			break;

		skb = skb_dequeue(&ar->htt.rx_msdus_q);
		if (!skb)
			break;
		ath10k_process_rx(ar, skb);
		quota++;
	}

	return quota;
}

int ath10k_htt_rx_hl_indication(struct ath10k *ar, int budget)
{
	struct htt_resp *resp;
	struct ath10k_htt *htt = &ar->htt;
	struct sk_buff *skb;
	bool release;
	int quota;

	for (quota = 0; quota < budget; quota++) {
		skb = skb_dequeue(&htt->rx_indication_head);
		if (!skb)
			break;

		resp = (struct htt_resp *)skb->data;

		release = ath10k_htt_rx_proc_rx_ind_hl(htt,
						       &resp->rx_ind_hl,
						       skb,
						       HTT_RX_PN_CHECK,
						       HTT_RX_NON_TKIP_MIC);

		if (release)
			dev_kfree_skb_any(skb);

		ath10k_dbg(ar, ATH10K_DBG_HTT, "rx indication poll pending count:%d\n",
			   skb_queue_len(&htt->rx_indication_head));
	}
	return quota;
}
EXPORT_SYMBOL(ath10k_htt_rx_hl_indication);

int ath10k_htt_txrx_compl_task(struct ath10k *ar, int budget)
{
	struct ath10k_htt *htt = &ar->htt;
	struct htt_tx_done tx_done = {};
	struct sk_buff_head tx_ind_q;
	struct sk_buff *skb;
	unsigned long flags;
	int quota = 0, done, ret;
	bool resched_napi = false;

	__skb_queue_head_init(&tx_ind_q);

	/* Process pending frames before dequeuing more data
	 * from hardware.
	 */
	quota = ath10k_htt_rx_deliver_msdu(ar, quota, budget);
	if (quota == budget) {
		resched_napi = true;
		goto exit;
	}

	while ((skb = skb_dequeue(&htt->rx_in_ord_compl_q))) {
		spin_lock_bh(&htt->rx_ring.lock);
		ret = ath10k_htt_rx_in_ord_ind(ar, skb);
		spin_unlock_bh(&htt->rx_ring.lock);

		dev_kfree_skb_any(skb);
		if (ret == -EIO) {
			resched_napi = true;
			goto exit;
		}
	}

	while (atomic_read(&htt->num_mpdus_ready)) {
		ret = ath10k_htt_rx_handle_amsdu(htt);
		if (ret == -EIO) {
			resched_napi = true;
			goto exit;
		}
		atomic_dec(&htt->num_mpdus_ready);
	}

	/* Deliver received data after processing data from hardware */
	quota = ath10k_htt_rx_deliver_msdu(ar, quota, budget);

	/* From NAPI documentation:
	 *  The napi poll() function may also process TX completions, in which
	 *  case if it processes the entire TX ring then it should count that
	 *  work as the rest of the budget.
	 */
	if ((quota < budget) && !kfifo_is_empty(&htt->txdone_fifo))
		quota = budget;

	/* kfifo_get: called only within txrx_tasklet so it's neatly serialized.
	 * From kfifo_get() documentation:
	 *  Note that with only one concurrent reader and one concurrent writer,
	 *  you don't need extra locking to use these macro.
	 */
	while (kfifo_get(&htt->txdone_fifo, &tx_done))
		ath10k_txrx_tx_unref(htt, &tx_done);

	ath10k_mac_tx_push_pending(ar);

	spin_lock_irqsave(&htt->tx_fetch_ind_q.lock, flags);
	skb_queue_splice_init(&htt->tx_fetch_ind_q, &tx_ind_q);
	spin_unlock_irqrestore(&htt->tx_fetch_ind_q.lock, flags);

	while ((skb = __skb_dequeue(&tx_ind_q))) {
		ath10k_htt_rx_tx_fetch_ind(ar, skb);
		dev_kfree_skb_any(skb);
	}

exit:
	ath10k_htt_rx_msdu_buff_replenish(htt);
	/* In case of rx failure or more data to read, report budget
	 * to reschedule NAPI poll
	 */
	done = resched_napi ? budget : quota;

	return done;
}
EXPORT_SYMBOL(ath10k_htt_txrx_compl_task);

static const struct ath10k_htt_rx_ops htt_rx_ops_32 = {
	.htt_get_rx_ring_size = ath10k_htt_get_rx_ring_size_32,
	.htt_config_paddrs_ring = ath10k_htt_config_paddrs_ring_32,
	.htt_set_paddrs_ring = ath10k_htt_set_paddrs_ring_32,
	.htt_get_vaddr_ring = ath10k_htt_get_vaddr_ring_32,
	.htt_reset_paddrs_ring = ath10k_htt_reset_paddrs_ring_32,
};

static const struct ath10k_htt_rx_ops htt_rx_ops_64 = {
	.htt_get_rx_ring_size = ath10k_htt_get_rx_ring_size_64,
	.htt_config_paddrs_ring = ath10k_htt_config_paddrs_ring_64,
	.htt_set_paddrs_ring = ath10k_htt_set_paddrs_ring_64,
	.htt_get_vaddr_ring = ath10k_htt_get_vaddr_ring_64,
	.htt_reset_paddrs_ring = ath10k_htt_reset_paddrs_ring_64,
};

static const struct ath10k_htt_rx_ops htt_rx_ops_hl = {
	.htt_rx_proc_rx_frag_ind = ath10k_htt_rx_proc_rx_frag_ind_hl,
};

void ath10k_htt_set_rx_ops(struct ath10k_htt *htt)
{
	struct ath10k *ar = htt->ar;

	if (ar->bus_param.dev_type == ATH10K_DEV_TYPE_HL)
		htt->rx_ops = &htt_rx_ops_hl;
	else if (ar->hw_params.target_64bit)
		htt->rx_ops = &htt_rx_ops_64;
	else
		htt->rx_ops = &htt_rx_ops_32;
}
