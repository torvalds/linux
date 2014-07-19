/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "core.h"
#include "htc.h"
#include "htt.h"
#include "txrx.h"
#include "debug.h"
#include "trace.h"

#include <linux/log2.h>

/* slightly larger than one large A-MPDU */
#define HTT_RX_RING_SIZE_MIN 128

/* roughly 20 ms @ 1 Gbps of 1500B MSDUs */
#define HTT_RX_RING_SIZE_MAX 2048

#define HTT_RX_AVG_FRM_BYTES 1000

/* ms, very conservative */
#define HTT_RX_HOST_LATENCY_MAX_MS 20

/* ms, conservative */
#define HTT_RX_HOST_LATENCY_WORST_LIKELY_MS 10

/* when under memory pressure rx ring refill may fail and needs a retry */
#define HTT_RX_RING_REFILL_RETRY_MS 50


static int ath10k_htt_rx_get_csum_state(struct sk_buff *skb);
static void ath10k_htt_txrx_compl_task(unsigned long ptr);

static int ath10k_htt_rx_ring_size(struct ath10k_htt *htt)
{
	int size;

	/*
	 * It is expected that the host CPU will typically be able to
	 * service the rx indication from one A-MPDU before the rx
	 * indication from the subsequent A-MPDU happens, roughly 1-2 ms
	 * later. However, the rx ring should be sized very conservatively,
	 * to accomodate the worst reasonable delay before the host CPU
	 * services a rx indication interrupt.
	 *
	 * The rx ring need not be kept full of empty buffers. In theory,
	 * the htt host SW can dynamically track the low-water mark in the
	 * rx ring, and dynamically adjust the level to which the rx ring
	 * is filled with empty buffers, to dynamically meet the desired
	 * low-water mark.
	 *
	 * In contrast, it's difficult to resize the rx ring itself, once
	 * it's in use. Thus, the ring itself should be sized very
	 * conservatively, while the degree to which the ring is filled
	 * with empty buffers should be sized moderately conservatively.
	 */

	/* 1e6 bps/mbps / 1e3 ms per sec = 1000 */
	size =
	    htt->max_throughput_mbps +
	    1000  /
	    (8 * HTT_RX_AVG_FRM_BYTES) * HTT_RX_HOST_LATENCY_MAX_MS;

	if (size < HTT_RX_RING_SIZE_MIN)
		size = HTT_RX_RING_SIZE_MIN;

	if (size > HTT_RX_RING_SIZE_MAX)
		size = HTT_RX_RING_SIZE_MAX;

	size = roundup_pow_of_two(size);

	return size;
}

static int ath10k_htt_rx_ring_fill_level(struct ath10k_htt *htt)
{
	int size;

	/* 1e6 bps/mbps / 1e3 ms per sec = 1000 */
	size =
	    htt->max_throughput_mbps *
	    1000  /
	    (8 * HTT_RX_AVG_FRM_BYTES) * HTT_RX_HOST_LATENCY_WORST_LIKELY_MS;

	/*
	 * Make sure the fill level is at least 1 less than the ring size.
	 * Leaving 1 element empty allows the SW to easily distinguish
	 * between a full ring vs. an empty ring.
	 */
	if (size >= htt->rx_ring.size)
		size = htt->rx_ring.size - 1;

	return size;
}

static void ath10k_htt_rx_ring_free(struct ath10k_htt *htt)
{
	struct sk_buff *skb;
	struct ath10k_skb_cb *cb;
	int i;

	for (i = 0; i < htt->rx_ring.fill_cnt; i++) {
		skb = htt->rx_ring.netbufs_ring[i];
		cb = ATH10K_SKB_CB(skb);
		dma_unmap_single(htt->ar->dev, cb->paddr,
				 skb->len + skb_tailroom(skb),
				 DMA_FROM_DEVICE);
		dev_kfree_skb_any(skb);
	}

	htt->rx_ring.fill_cnt = 0;
}

static int __ath10k_htt_rx_ring_fill_n(struct ath10k_htt *htt, int num)
{
	struct htt_rx_desc *rx_desc;
	struct sk_buff *skb;
	dma_addr_t paddr;
	int ret = 0, idx;

	idx = __le32_to_cpu(*(htt->rx_ring.alloc_idx.vaddr));
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
		rx_desc = (struct htt_rx_desc *)skb->data;
		rx_desc->attention.flags = __cpu_to_le32(0);

		paddr = dma_map_single(htt->ar->dev, skb->data,
				       skb->len + skb_tailroom(skb),
				       DMA_FROM_DEVICE);

		if (unlikely(dma_mapping_error(htt->ar->dev, paddr))) {
			dev_kfree_skb_any(skb);
			ret = -ENOMEM;
			goto fail;
		}

		ATH10K_SKB_CB(skb)->paddr = paddr;
		htt->rx_ring.netbufs_ring[idx] = skb;
		htt->rx_ring.paddrs_ring[idx] = __cpu_to_le32(paddr);
		htt->rx_ring.fill_cnt++;

		num--;
		idx++;
		idx &= htt->rx_ring.size_mask;
	}

fail:
	*(htt->rx_ring.alloc_idx.vaddr) = __cpu_to_le32(idx);
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
	 * improves the avarage and stability. */
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
		tasklet_schedule(&htt->rx_replenish_task);
	}
	spin_unlock_bh(&htt->rx_ring.lock);
}

static void ath10k_htt_rx_ring_refill_retry(unsigned long arg)
{
	struct ath10k_htt *htt = (struct ath10k_htt *)arg;
	ath10k_htt_rx_msdu_buff_replenish(htt);
}

static void ath10k_htt_rx_ring_clean_up(struct ath10k_htt *htt)
{
	struct sk_buff *skb;
	int i;

	for (i = 0; i < htt->rx_ring.size; i++) {
		skb = htt->rx_ring.netbufs_ring[i];
		if (!skb)
			continue;

		dma_unmap_single(htt->ar->dev, ATH10K_SKB_CB(skb)->paddr,
				 skb->len + skb_tailroom(skb),
				 DMA_FROM_DEVICE);
		dev_kfree_skb_any(skb);
		htt->rx_ring.netbufs_ring[i] = NULL;
	}
}

void ath10k_htt_rx_free(struct ath10k_htt *htt)
{
	del_timer_sync(&htt->rx_ring.refill_retry_timer);
	tasklet_kill(&htt->rx_replenish_task);
	tasklet_kill(&htt->txrx_compl_task);

	skb_queue_purge(&htt->tx_compl_q);
	skb_queue_purge(&htt->rx_compl_q);

	ath10k_htt_rx_ring_clean_up(htt);

	dma_free_coherent(htt->ar->dev,
			  (htt->rx_ring.size *
			   sizeof(htt->rx_ring.paddrs_ring)),
			  htt->rx_ring.paddrs_ring,
			  htt->rx_ring.base_paddr);

	dma_free_coherent(htt->ar->dev,
			  sizeof(*htt->rx_ring.alloc_idx.vaddr),
			  htt->rx_ring.alloc_idx.vaddr,
			  htt->rx_ring.alloc_idx.paddr);

	kfree(htt->rx_ring.netbufs_ring);
}

static inline struct sk_buff *ath10k_htt_rx_netbuf_pop(struct ath10k_htt *htt)
{
	int idx;
	struct sk_buff *msdu;

	lockdep_assert_held(&htt->rx_ring.lock);

	if (htt->rx_ring.fill_cnt == 0) {
		ath10k_warn("tried to pop sk_buff from an empty rx ring\n");
		return NULL;
	}

	idx = htt->rx_ring.sw_rd_idx.msdu_payld;
	msdu = htt->rx_ring.netbufs_ring[idx];
	htt->rx_ring.netbufs_ring[idx] = NULL;

	idx++;
	idx &= htt->rx_ring.size_mask;
	htt->rx_ring.sw_rd_idx.msdu_payld = idx;
	htt->rx_ring.fill_cnt--;

	return msdu;
}

static void ath10k_htt_rx_free_msdu_chain(struct sk_buff *skb)
{
	struct sk_buff *next;

	while (skb) {
		next = skb->next;
		dev_kfree_skb_any(skb);
		skb = next;
	}
}

/* return: < 0 fatal error, 0 - non chained msdu, 1 chained msdu */
static int ath10k_htt_rx_amsdu_pop(struct ath10k_htt *htt,
				   u8 **fw_desc, int *fw_desc_len,
				   struct sk_buff **head_msdu,
				   struct sk_buff **tail_msdu)
{
	int msdu_len, msdu_chaining = 0;
	struct sk_buff *msdu;
	struct htt_rx_desc *rx_desc;
	bool corrupted = false;

	lockdep_assert_held(&htt->rx_ring.lock);

	if (htt->rx_confused) {
		ath10k_warn("htt is confused. refusing rx\n");
		return -1;
	}

	msdu = *head_msdu = ath10k_htt_rx_netbuf_pop(htt);
	while (msdu) {
		int last_msdu, msdu_len_invalid, msdu_chained;

		dma_unmap_single(htt->ar->dev,
				 ATH10K_SKB_CB(msdu)->paddr,
				 msdu->len + skb_tailroom(msdu),
				 DMA_FROM_DEVICE);

		ath10k_dbg_dump(ATH10K_DBG_HTT_DUMP, NULL, "htt rx pop: ",
				msdu->data, msdu->len + skb_tailroom(msdu));

		rx_desc = (struct htt_rx_desc *)msdu->data;

		/* FIXME: we must report msdu payload since this is what caller
		 *        expects now */
		skb_put(msdu, offsetof(struct htt_rx_desc, msdu_payload));
		skb_pull(msdu, offsetof(struct htt_rx_desc, msdu_payload));

		/*
		 * Sanity check - confirm the HW is finished filling in the
		 * rx data.
		 * If the HW and SW are working correctly, then it's guaranteed
		 * that the HW's MAC DMA is done before this point in the SW.
		 * To prevent the case that we handle a stale Rx descriptor,
		 * just assert for now until we have a way to recover.
		 */
		if (!(__le32_to_cpu(rx_desc->attention.flags)
				& RX_ATTENTION_FLAGS_MSDU_DONE)) {
			ath10k_htt_rx_free_msdu_chain(*head_msdu);
			*head_msdu = NULL;
			msdu = NULL;
			ath10k_err("htt rx stopped. cannot recover\n");
			htt->rx_confused = true;
			break;
		}

		/*
		 * Copy the FW rx descriptor for this MSDU from the rx
		 * indication message into the MSDU's netbuf. HL uses the
		 * same rx indication message definition as LL, and simply
		 * appends new info (fields from the HW rx desc, and the
		 * MSDU payload itself). So, the offset into the rx
		 * indication message only has to account for the standard
		 * offset of the per-MSDU FW rx desc info within the
		 * message, and how many bytes of the per-MSDU FW rx desc
		 * info have already been consumed. (And the endianness of
		 * the host, since for a big-endian host, the rx ind
		 * message contents, including the per-MSDU rx desc bytes,
		 * were byteswapped during upload.)
		 */
		if (*fw_desc_len > 0) {
			rx_desc->fw_desc.info0 = **fw_desc;
			/*
			 * The target is expected to only provide the basic
			 * per-MSDU rx descriptors. Just to be sure, verify
			 * that the target has not attached extension data
			 * (e.g. LRO flow ID).
			 */

			/* or more, if there's extension data */
			(*fw_desc)++;
			(*fw_desc_len)--;
		} else {
			/*
			 * When an oversized AMSDU happened, FW will lost
			 * some of MSDU status - in this case, the FW
			 * descriptors provided will be less than the
			 * actual MSDUs inside this MPDU. Mark the FW
			 * descriptors so that it will still deliver to
			 * upper stack, if no CRC error for this MPDU.
			 *
			 * FIX THIS - the FW descriptors are actually for
			 * MSDUs in the end of this A-MSDU instead of the
			 * beginning.
			 */
			rx_desc->fw_desc.info0 = 0;
		}

		msdu_len_invalid = !!(__le32_to_cpu(rx_desc->attention.flags)
					& (RX_ATTENTION_FLAGS_MPDU_LENGTH_ERR |
					   RX_ATTENTION_FLAGS_MSDU_LENGTH_ERR));
		msdu_len = MS(__le32_to_cpu(rx_desc->msdu_start.info0),
			      RX_MSDU_START_INFO0_MSDU_LENGTH);
		msdu_chained = rx_desc->frag_info.ring2_more_count;

		if (msdu_len_invalid)
			msdu_len = 0;

		skb_trim(msdu, 0);
		skb_put(msdu, min(msdu_len, HTT_RX_MSDU_SIZE));
		msdu_len -= msdu->len;

		/* FIXME: Do chained buffers include htt_rx_desc or not? */
		while (msdu_chained--) {
			struct sk_buff *next = ath10k_htt_rx_netbuf_pop(htt);

			dma_unmap_single(htt->ar->dev,
					 ATH10K_SKB_CB(next)->paddr,
					 next->len + skb_tailroom(next),
					 DMA_FROM_DEVICE);

			ath10k_dbg_dump(ATH10K_DBG_HTT_DUMP, NULL,
					"htt rx chained: ", next->data,
					next->len + skb_tailroom(next));

			skb_trim(next, 0);
			skb_put(next, min(msdu_len, HTT_RX_BUF_SIZE));
			msdu_len -= next->len;

			msdu->next = next;
			msdu = next;
			msdu_chaining = 1;
		}

		last_msdu = __le32_to_cpu(rx_desc->msdu_end.info0) &
				RX_MSDU_END_INFO0_LAST_MSDU;

		if (msdu_chaining && !last_msdu)
			corrupted = true;

		if (last_msdu) {
			msdu->next = NULL;
			break;
		} else {
			struct sk_buff *next = ath10k_htt_rx_netbuf_pop(htt);
			msdu->next = next;
			msdu = next;
		}
	}
	*tail_msdu = msdu;

	if (*head_msdu == NULL)
		msdu_chaining = -1;

	/*
	 * Apparently FW sometimes reports weird chained MSDU sequences with
	 * more than one rx descriptor. This seems like a bug but needs more
	 * analyzing. For the time being fix it by dropping such sequences to
	 * avoid blowing up the host system.
	 */
	if (corrupted) {
		ath10k_warn("failed to pop chained msdus, dropping\n");
		ath10k_htt_rx_free_msdu_chain(*head_msdu);
		*head_msdu = NULL;
		*tail_msdu = NULL;
		msdu_chaining = -EINVAL;
	}

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

static void ath10k_htt_rx_replenish_task(unsigned long ptr)
{
	struct ath10k_htt *htt = (struct ath10k_htt *)ptr;
	ath10k_htt_rx_msdu_buff_replenish(htt);
}

int ath10k_htt_rx_alloc(struct ath10k_htt *htt)
{
	dma_addr_t paddr;
	void *vaddr;
	struct timer_list *timer = &htt->rx_ring.refill_retry_timer;

	htt->rx_ring.size = ath10k_htt_rx_ring_size(htt);
	if (!is_power_of_2(htt->rx_ring.size)) {
		ath10k_warn("htt rx ring size is not power of 2\n");
		return -EINVAL;
	}

	htt->rx_ring.size_mask = htt->rx_ring.size - 1;

	/*
	 * Set the initial value for the level to which the rx ring
	 * should be filled, based on the max throughput and the
	 * worst likely latency for the host to fill the rx ring
	 * with new buffers. In theory, this fill level can be
	 * dynamically adjusted from the initial value set here, to
	 * reflect the actual host latency rather than a
	 * conservative assumption about the host latency.
	 */
	htt->rx_ring.fill_level = ath10k_htt_rx_ring_fill_level(htt);

	htt->rx_ring.netbufs_ring =
		kzalloc(htt->rx_ring.size * sizeof(struct sk_buff *),
			GFP_KERNEL);
	if (!htt->rx_ring.netbufs_ring)
		goto err_netbuf;

	vaddr = dma_alloc_coherent(htt->ar->dev,
		   (htt->rx_ring.size * sizeof(htt->rx_ring.paddrs_ring)),
		   &paddr, GFP_DMA);
	if (!vaddr)
		goto err_dma_ring;

	htt->rx_ring.paddrs_ring = vaddr;
	htt->rx_ring.base_paddr = paddr;

	vaddr = dma_alloc_coherent(htt->ar->dev,
				   sizeof(*htt->rx_ring.alloc_idx.vaddr),
				   &paddr, GFP_DMA);
	if (!vaddr)
		goto err_dma_idx;

	htt->rx_ring.alloc_idx.vaddr = vaddr;
	htt->rx_ring.alloc_idx.paddr = paddr;
	htt->rx_ring.sw_rd_idx.msdu_payld = 0;
	*htt->rx_ring.alloc_idx.vaddr = 0;

	/* Initialize the Rx refill retry timer */
	setup_timer(timer, ath10k_htt_rx_ring_refill_retry, (unsigned long)htt);

	spin_lock_init(&htt->rx_ring.lock);

	htt->rx_ring.fill_cnt = 0;
	if (__ath10k_htt_rx_ring_fill_n(htt, htt->rx_ring.fill_level))
		goto err_fill_ring;

	tasklet_init(&htt->rx_replenish_task, ath10k_htt_rx_replenish_task,
		     (unsigned long)htt);

	skb_queue_head_init(&htt->tx_compl_q);
	skb_queue_head_init(&htt->rx_compl_q);

	tasklet_init(&htt->txrx_compl_task, ath10k_htt_txrx_compl_task,
		     (unsigned long)htt);

	ath10k_dbg(ATH10K_DBG_BOOT, "htt rx ring size %d fill_level %d\n",
		   htt->rx_ring.size, htt->rx_ring.fill_level);
	return 0;

err_fill_ring:
	ath10k_htt_rx_ring_free(htt);
	dma_free_coherent(htt->ar->dev,
			  sizeof(*htt->rx_ring.alloc_idx.vaddr),
			  htt->rx_ring.alloc_idx.vaddr,
			  htt->rx_ring.alloc_idx.paddr);
err_dma_idx:
	dma_free_coherent(htt->ar->dev,
			  (htt->rx_ring.size *
			   sizeof(htt->rx_ring.paddrs_ring)),
			  htt->rx_ring.paddrs_ring,
			  htt->rx_ring.base_paddr);
err_dma_ring:
	kfree(htt->rx_ring.netbufs_ring);
err_netbuf:
	return -ENOMEM;
}

static int ath10k_htt_rx_crypto_param_len(enum htt_rx_mpdu_encrypt_type type)
{
	switch (type) {
	case HTT_RX_MPDU_ENCRYPT_WEP40:
	case HTT_RX_MPDU_ENCRYPT_WEP104:
		return 4;
	case HTT_RX_MPDU_ENCRYPT_TKIP_WITHOUT_MIC:
	case HTT_RX_MPDU_ENCRYPT_WEP128: /* not tested */
	case HTT_RX_MPDU_ENCRYPT_TKIP_WPA:
	case HTT_RX_MPDU_ENCRYPT_WAPI: /* not tested */
	case HTT_RX_MPDU_ENCRYPT_AES_CCM_WPA2:
		return 8;
	case HTT_RX_MPDU_ENCRYPT_NONE:
		return 0;
	}

	ath10k_warn("unknown encryption type %d\n", type);
	return 0;
}

static int ath10k_htt_rx_crypto_tail_len(enum htt_rx_mpdu_encrypt_type type)
{
	switch (type) {
	case HTT_RX_MPDU_ENCRYPT_NONE:
	case HTT_RX_MPDU_ENCRYPT_WEP40:
	case HTT_RX_MPDU_ENCRYPT_WEP104:
	case HTT_RX_MPDU_ENCRYPT_WEP128:
	case HTT_RX_MPDU_ENCRYPT_WAPI:
		return 0;
	case HTT_RX_MPDU_ENCRYPT_TKIP_WITHOUT_MIC:
	case HTT_RX_MPDU_ENCRYPT_TKIP_WPA:
		return 4;
	case HTT_RX_MPDU_ENCRYPT_AES_CCM_WPA2:
		return 8;
	}

	ath10k_warn("unknown encryption type %d\n", type);
	return 0;
}

/* Applies for first msdu in chain, before altering it. */
static struct ieee80211_hdr *ath10k_htt_rx_skb_get_hdr(struct sk_buff *skb)
{
	struct htt_rx_desc *rxd;
	enum rx_msdu_decap_format fmt;

	rxd = (void *)skb->data - sizeof(*rxd);
	fmt = MS(__le32_to_cpu(rxd->msdu_start.info1),
			RX_MSDU_START_INFO1_DECAP_FORMAT);

	if (fmt == RX_MSDU_DECAP_RAW)
		return (void *)skb->data;
	else
		return (void *)skb->data - RX_HTT_HDR_STATUS_LEN;
}

/* This function only applies for first msdu in an msdu chain */
static bool ath10k_htt_rx_hdr_is_amsdu(struct ieee80211_hdr *hdr)
{
	if (ieee80211_is_data_qos(hdr->frame_control)) {
		u8 *qc = ieee80211_get_qos_ctl(hdr);
		if (qc[0] & 0x80)
			return true;
	}
	return false;
}

struct rfc1042_hdr {
	u8 llc_dsap;
	u8 llc_ssap;
	u8 llc_ctrl;
	u8 snap_oui[3];
	__be16 snap_type;
} __packed;

struct amsdu_subframe_hdr {
	u8 dst[ETH_ALEN];
	u8 src[ETH_ALEN];
	__be16 len;
} __packed;

static const u8 rx_legacy_rate_idx[] = {
	3,	/* 0x00  - 11Mbps  */
	2,	/* 0x01  - 5.5Mbps */
	1,	/* 0x02  - 2Mbps   */
	0,	/* 0x03  - 1Mbps   */
	3,	/* 0x04  - 11Mbps  */
	2,	/* 0x05  - 5.5Mbps */
	1,	/* 0x06  - 2Mbps   */
	0,	/* 0x07  - 1Mbps   */
	10,	/* 0x08  - 48Mbps  */
	8,	/* 0x09  - 24Mbps  */
	6,	/* 0x0A  - 12Mbps  */
	4,	/* 0x0B  - 6Mbps   */
	11,	/* 0x0C  - 54Mbps  */
	9,	/* 0x0D  - 36Mbps  */
	7,	/* 0x0E  - 18Mbps  */
	5,	/* 0x0F  - 9Mbps   */
};

static void ath10k_htt_rx_h_rates(struct ath10k *ar,
				  enum ieee80211_band band,
				  u8 info0, u32 info1, u32 info2,
				  struct ieee80211_rx_status *status)
{
	u8 cck, rate, rate_idx, bw, sgi, mcs, nss;
	u8 preamble = 0;

	/* Check if valid fields */
	if (!(info0 & HTT_RX_INDICATION_INFO0_START_VALID))
		return;

	preamble = MS(info1, HTT_RX_INDICATION_INFO1_PREAMBLE_TYPE);

	switch (preamble) {
	case HTT_RX_LEGACY:
		cck = info0 & HTT_RX_INDICATION_INFO0_LEGACY_RATE_CCK;
		rate = MS(info0, HTT_RX_INDICATION_INFO0_LEGACY_RATE);
		rate_idx = 0;

		if (rate < 0x08 || rate > 0x0F)
			break;

		switch (band) {
		case IEEE80211_BAND_2GHZ:
			if (cck)
				rate &= ~BIT(3);
			rate_idx = rx_legacy_rate_idx[rate];
			break;
		case IEEE80211_BAND_5GHZ:
			rate_idx = rx_legacy_rate_idx[rate];
			/* We are using same rate table registering
			   HW - ath10k_rates[]. In case of 5GHz skip
			   CCK rates, so -4 here */
			rate_idx -= 4;
			break;
		default:
			break;
		}

		status->rate_idx = rate_idx;
		break;
	case HTT_RX_HT:
	case HTT_RX_HT_WITH_TXBF:
		/* HT-SIG - Table 20-11 in info1 and info2 */
		mcs = info1 & 0x1F;
		nss = mcs >> 3;
		bw = (info1 >> 7) & 1;
		sgi = (info2 >> 7) & 1;

		status->rate_idx = mcs;
		status->flag |= RX_FLAG_HT;
		if (sgi)
			status->flag |= RX_FLAG_SHORT_GI;
		if (bw)
			status->flag |= RX_FLAG_40MHZ;
		break;
	case HTT_RX_VHT:
	case HTT_RX_VHT_WITH_TXBF:
		/* VHT-SIG-A1 in info 1, VHT-SIG-A2 in info2
		   TODO check this */
		mcs = (info2 >> 4) & 0x0F;
		nss = ((info1 >> 10) & 0x07) + 1;
		bw = info1 & 3;
		sgi = info2 & 1;

		status->rate_idx = mcs;
		status->vht_nss = nss;

		if (sgi)
			status->flag |= RX_FLAG_SHORT_GI;

		switch (bw) {
		/* 20MHZ */
		case 0:
			break;
		/* 40MHZ */
		case 1:
			status->flag |= RX_FLAG_40MHZ;
			break;
		/* 80MHZ */
		case 2:
			status->vht_flag |= RX_VHT_FLAG_80MHZ;
		}

		status->flag |= RX_FLAG_VHT;
		break;
	default:
		break;
	}
}

static void ath10k_htt_rx_h_protected(struct ath10k_htt *htt,
				      struct ieee80211_rx_status *rx_status,
				      struct sk_buff *skb,
				      enum htt_rx_mpdu_encrypt_type enctype,
				      enum rx_msdu_decap_format fmt,
				      bool dot11frag)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	rx_status->flag &= ~(RX_FLAG_DECRYPTED |
			     RX_FLAG_IV_STRIPPED |
			     RX_FLAG_MMIC_STRIPPED);

	if (enctype == HTT_RX_MPDU_ENCRYPT_NONE)
		return;

	/*
	 * There's no explicit rx descriptor flag to indicate whether a given
	 * frame has been decrypted or not. We're forced to use the decap
	 * format as an implicit indication. However fragmentation rx is always
	 * raw and it probably never reports undecrypted raws.
	 *
	 * This makes sure sniffed frames are reported as-is without stripping
	 * the protected flag.
	 */
	if (fmt == RX_MSDU_DECAP_RAW && !dot11frag)
		return;

	rx_status->flag |= RX_FLAG_DECRYPTED |
			   RX_FLAG_IV_STRIPPED |
			   RX_FLAG_MMIC_STRIPPED;
	hdr->frame_control = __cpu_to_le16(__le16_to_cpu(hdr->frame_control) &
					   ~IEEE80211_FCTL_PROTECTED);
}

static bool ath10k_htt_rx_h_channel(struct ath10k *ar,
				    struct ieee80211_rx_status *status)
{
	struct ieee80211_channel *ch;

	spin_lock_bh(&ar->data_lock);
	ch = ar->scan_channel;
	if (!ch)
		ch = ar->rx_channel;
	spin_unlock_bh(&ar->data_lock);

	if (!ch)
		return false;

	status->band = ch->band;
	status->freq = ch->center_freq;

	return true;
}

static void ath10k_process_rx(struct ath10k *ar,
			      struct ieee80211_rx_status *rx_status,
			      struct sk_buff *skb)
{
	struct ieee80211_rx_status *status;

	status = IEEE80211_SKB_RXCB(skb);
	*status = *rx_status;

	ath10k_dbg(ATH10K_DBG_DATA,
		   "rx skb %p len %u %s%s%s%s%s %srate_idx %u vht_nss %u freq %u band %u flag 0x%x fcs-err %imic-err %i\n",
		   skb,
		   skb->len,
		   status->flag == 0 ? "legacy" : "",
		   status->flag & RX_FLAG_HT ? "ht" : "",
		   status->flag & RX_FLAG_VHT ? "vht" : "",
		   status->flag & RX_FLAG_40MHZ ? "40" : "",
		   status->vht_flag & RX_VHT_FLAG_80MHZ ? "80" : "",
		   status->flag & RX_FLAG_SHORT_GI ? "sgi " : "",
		   status->rate_idx,
		   status->vht_nss,
		   status->freq,
		   status->band, status->flag,
		   !!(status->flag & RX_FLAG_FAILED_FCS_CRC),
		   !!(status->flag & RX_FLAG_MMIC_ERROR));
	ath10k_dbg_dump(ATH10K_DBG_HTT_DUMP, NULL, "rx skb: ",
			skb->data, skb->len);

	ieee80211_rx(ar->hw, skb);
}

static int ath10k_htt_rx_nwifi_hdrlen(struct ieee80211_hdr *hdr)
{
	/* nwifi header is padded to 4 bytes. this fixes 4addr rx */
	return round_up(ieee80211_hdrlen(hdr->frame_control), 4);
}

static void ath10k_htt_rx_amsdu(struct ath10k_htt *htt,
				struct ieee80211_rx_status *rx_status,
				struct sk_buff *skb_in)
{
	struct htt_rx_desc *rxd;
	struct sk_buff *skb = skb_in;
	struct sk_buff *first;
	enum rx_msdu_decap_format fmt;
	enum htt_rx_mpdu_encrypt_type enctype;
	struct ieee80211_hdr *hdr;
	u8 hdr_buf[64], addr[ETH_ALEN], *qos;
	unsigned int hdr_len;

	rxd = (void *)skb->data - sizeof(*rxd);
	enctype = MS(__le32_to_cpu(rxd->mpdu_start.info0),
			RX_MPDU_START_INFO0_ENCRYPT_TYPE);

	hdr = (struct ieee80211_hdr *)rxd->rx_hdr_status;
	hdr_len = ieee80211_hdrlen(hdr->frame_control);
	memcpy(hdr_buf, hdr, hdr_len);
	hdr = (struct ieee80211_hdr *)hdr_buf;

	first = skb;
	while (skb) {
		void *decap_hdr;
		int len;

		rxd = (void *)skb->data - sizeof(*rxd);
		fmt = MS(__le32_to_cpu(rxd->msdu_start.info1),
			 RX_MSDU_START_INFO1_DECAP_FORMAT);
		decap_hdr = (void *)rxd->rx_hdr_status;

		skb->ip_summed = ath10k_htt_rx_get_csum_state(skb);

		/* First frame in an A-MSDU chain has more decapped data. */
		if (skb == first) {
			len = round_up(ieee80211_hdrlen(hdr->frame_control), 4);
			len += round_up(ath10k_htt_rx_crypto_param_len(enctype),
					4);
			decap_hdr += len;
		}

		switch (fmt) {
		case RX_MSDU_DECAP_RAW:
			/* remove trailing FCS */
			skb_trim(skb, skb->len - FCS_LEN);
			break;
		case RX_MSDU_DECAP_NATIVE_WIFI:
			/* pull decapped header and copy DA */
			hdr = (struct ieee80211_hdr *)skb->data;
			hdr_len = ath10k_htt_rx_nwifi_hdrlen(hdr);
			memcpy(addr, ieee80211_get_DA(hdr), ETH_ALEN);
			skb_pull(skb, hdr_len);

			/* push original 802.11 header */
			hdr = (struct ieee80211_hdr *)hdr_buf;
			hdr_len = ieee80211_hdrlen(hdr->frame_control);
			memcpy(skb_push(skb, hdr_len), hdr, hdr_len);

			/* original A-MSDU header has the bit set but we're
			 * not including A-MSDU subframe header */
			hdr = (struct ieee80211_hdr *)skb->data;
			qos = ieee80211_get_qos_ctl(hdr);
			qos[0] &= ~IEEE80211_QOS_CTL_A_MSDU_PRESENT;

			/* original 802.11 header has a different DA */
			memcpy(ieee80211_get_DA(hdr), addr, ETH_ALEN);
			break;
		case RX_MSDU_DECAP_ETHERNET2_DIX:
			/* strip ethernet header and insert decapped 802.11
			 * header, amsdu subframe header and rfc1042 header */

			len = 0;
			len += sizeof(struct rfc1042_hdr);
			len += sizeof(struct amsdu_subframe_hdr);

			skb_pull(skb, sizeof(struct ethhdr));
			memcpy(skb_push(skb, len), decap_hdr, len);
			memcpy(skb_push(skb, hdr_len), hdr, hdr_len);
			break;
		case RX_MSDU_DECAP_8023_SNAP_LLC:
			/* insert decapped 802.11 header making a singly
			 * A-MSDU */
			memcpy(skb_push(skb, hdr_len), hdr, hdr_len);
			break;
		}

		skb_in = skb;
		ath10k_htt_rx_h_protected(htt, rx_status, skb_in, enctype, fmt,
					  false);
		skb = skb->next;
		skb_in->next = NULL;

		if (skb)
			rx_status->flag |= RX_FLAG_AMSDU_MORE;
		else
			rx_status->flag &= ~RX_FLAG_AMSDU_MORE;

		ath10k_process_rx(htt->ar, rx_status, skb_in);
	}

	/* FIXME: It might be nice to re-assemble the A-MSDU when there's a
	 * monitor interface active for sniffing purposes. */
}

static void ath10k_htt_rx_msdu(struct ath10k_htt *htt,
			       struct ieee80211_rx_status *rx_status,
			       struct sk_buff *skb)
{
	struct htt_rx_desc *rxd;
	struct ieee80211_hdr *hdr;
	enum rx_msdu_decap_format fmt;
	enum htt_rx_mpdu_encrypt_type enctype;
	int hdr_len;
	void *rfc1042;

	/* This shouldn't happen. If it does than it may be a FW bug. */
	if (skb->next) {
		ath10k_warn("htt rx received chained non A-MSDU frame\n");
		ath10k_htt_rx_free_msdu_chain(skb->next);
		skb->next = NULL;
	}

	rxd = (void *)skb->data - sizeof(*rxd);
	fmt = MS(__le32_to_cpu(rxd->msdu_start.info1),
			RX_MSDU_START_INFO1_DECAP_FORMAT);
	enctype = MS(__le32_to_cpu(rxd->mpdu_start.info0),
			RX_MPDU_START_INFO0_ENCRYPT_TYPE);
	hdr = (struct ieee80211_hdr *)rxd->rx_hdr_status;
	hdr_len = ieee80211_hdrlen(hdr->frame_control);

	skb->ip_summed = ath10k_htt_rx_get_csum_state(skb);

	switch (fmt) {
	case RX_MSDU_DECAP_RAW:
		/* remove trailing FCS */
		skb_trim(skb, skb->len - FCS_LEN);
		break;
	case RX_MSDU_DECAP_NATIVE_WIFI:
		/* Pull decapped header */
		hdr = (struct ieee80211_hdr *)skb->data;
		hdr_len = ath10k_htt_rx_nwifi_hdrlen(hdr);
		skb_pull(skb, hdr_len);

		/* Push original header */
		hdr = (struct ieee80211_hdr *)rxd->rx_hdr_status;
		hdr_len = ieee80211_hdrlen(hdr->frame_control);
		memcpy(skb_push(skb, hdr_len), hdr, hdr_len);
		break;
	case RX_MSDU_DECAP_ETHERNET2_DIX:
		/* strip ethernet header and insert decapped 802.11 header and
		 * rfc1042 header */

		rfc1042 = hdr;
		rfc1042 += roundup(hdr_len, 4);
		rfc1042 += roundup(ath10k_htt_rx_crypto_param_len(enctype), 4);

		skb_pull(skb, sizeof(struct ethhdr));
		memcpy(skb_push(skb, sizeof(struct rfc1042_hdr)),
		       rfc1042, sizeof(struct rfc1042_hdr));
		memcpy(skb_push(skb, hdr_len), hdr, hdr_len);
		break;
	case RX_MSDU_DECAP_8023_SNAP_LLC:
		/* remove A-MSDU subframe header and insert
		 * decapped 802.11 header. rfc1042 header is already there */

		skb_pull(skb, sizeof(struct amsdu_subframe_hdr));
		memcpy(skb_push(skb, hdr_len), hdr, hdr_len);
		break;
	}

	ath10k_htt_rx_h_protected(htt, rx_status, skb, enctype, fmt, false);

	ath10k_process_rx(htt->ar, rx_status, skb);
}

static int ath10k_htt_rx_get_csum_state(struct sk_buff *skb)
{
	struct htt_rx_desc *rxd;
	u32 flags, info;
	bool is_ip4, is_ip6;
	bool is_tcp, is_udp;
	bool ip_csum_ok, tcpudp_csum_ok;

	rxd = (void *)skb->data - sizeof(*rxd);
	flags = __le32_to_cpu(rxd->attention.flags);
	info = __le32_to_cpu(rxd->msdu_start.info1);

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

static int ath10k_unchain_msdu(struct sk_buff *msdu_head)
{
	struct sk_buff *next = msdu_head->next;
	struct sk_buff *to_free = next;
	int space;
	int total_len = 0;

	/* TODO:  Might could optimize this by using
	 * skb_try_coalesce or similar method to
	 * decrease copying, or maybe get mac80211 to
	 * provide a way to just receive a list of
	 * skb?
	 */

	msdu_head->next = NULL;

	/* Allocate total length all at once. */
	while (next) {
		total_len += next->len;
		next = next->next;
	}

	space = total_len - skb_tailroom(msdu_head);
	if ((space > 0) &&
	    (pskb_expand_head(msdu_head, 0, space, GFP_ATOMIC) < 0)) {
		/* TODO:  bump some rx-oom error stat */
		/* put it back together so we can free the
		 * whole list at once.
		 */
		msdu_head->next = to_free;
		return -1;
	}

	/* Walk list again, copying contents into
	 * msdu_head
	 */
	next = to_free;
	while (next) {
		skb_copy_from_linear_data(next, skb_put(msdu_head, next->len),
					  next->len);
		next = next->next;
	}

	/* If here, we have consolidated skb.  Free the
	 * fragments and pass the main skb on up the
	 * stack.
	 */
	ath10k_htt_rx_free_msdu_chain(to_free);
	return 0;
}

static bool ath10k_htt_rx_amsdu_allowed(struct ath10k_htt *htt,
					struct sk_buff *head,
					enum htt_rx_mpdu_status status,
					bool channel_set,
					u32 attention)
{
	if (head->len == 0) {
		ath10k_dbg(ATH10K_DBG_HTT,
			   "htt rx dropping due to zero-len\n");
		return false;
	}

	if (attention & RX_ATTENTION_FLAGS_DECRYPT_ERR) {
		ath10k_dbg(ATH10K_DBG_HTT,
			   "htt rx dropping due to decrypt-err\n");
		return false;
	}

	if (!channel_set) {
		ath10k_warn("no channel configured; ignoring frame!\n");
		return false;
	}

	/* Skip mgmt frames while we handle this in WMI */
	if (status == HTT_RX_IND_MPDU_STATUS_MGMT_CTRL ||
	    attention & RX_ATTENTION_FLAGS_MGMT_TYPE) {
		ath10k_dbg(ATH10K_DBG_HTT, "htt rx mgmt ctrl\n");
		return false;
	}

	if (status != HTT_RX_IND_MPDU_STATUS_OK &&
	    status != HTT_RX_IND_MPDU_STATUS_TKIP_MIC_ERR &&
	    status != HTT_RX_IND_MPDU_STATUS_ERR_INV_PEER &&
	    !htt->ar->monitor_started) {
		ath10k_dbg(ATH10K_DBG_HTT,
			   "htt rx ignoring frame w/ status %d\n",
			   status);
		return false;
	}

	if (test_bit(ATH10K_CAC_RUNNING, &htt->ar->dev_flags)) {
		ath10k_dbg(ATH10K_DBG_HTT,
			   "htt rx CAC running\n");
		return false;
	}

	return true;
}

static void ath10k_htt_rx_handler(struct ath10k_htt *htt,
				  struct htt_rx_indication *rx)
{
	struct ieee80211_rx_status *rx_status = &htt->rx_status;
	struct htt_rx_indication_mpdu_range *mpdu_ranges;
	struct htt_rx_desc *rxd;
	enum htt_rx_mpdu_status status;
	struct ieee80211_hdr *hdr;
	int num_mpdu_ranges;
	u32 attention;
	int fw_desc_len;
	u8 *fw_desc;
	bool channel_set;
	int i, j;
	int ret;

	lockdep_assert_held(&htt->rx_ring.lock);

	fw_desc_len = __le16_to_cpu(rx->prefix.fw_rx_desc_bytes);
	fw_desc = (u8 *)&rx->fw_desc;

	num_mpdu_ranges = MS(__le32_to_cpu(rx->hdr.info1),
			     HTT_RX_INDICATION_INFO1_NUM_MPDU_RANGES);
	mpdu_ranges = htt_rx_ind_get_mpdu_ranges(rx);

	/* Fill this once, while this is per-ppdu */
	if (rx->ppdu.info0 & HTT_RX_INDICATION_INFO0_START_VALID) {
		memset(rx_status, 0, sizeof(*rx_status));
		rx_status->signal  = ATH10K_DEFAULT_NOISE_FLOOR +
				     rx->ppdu.combined_rssi;
	}

	if (rx->ppdu.info0 & HTT_RX_INDICATION_INFO0_END_VALID) {
		/* TSF available only in 32-bit */
		rx_status->mactime = __le32_to_cpu(rx->ppdu.tsf) & 0xffffffff;
		rx_status->flag |= RX_FLAG_MACTIME_END;
	}

	channel_set = ath10k_htt_rx_h_channel(htt->ar, rx_status);

	if (channel_set) {
		ath10k_htt_rx_h_rates(htt->ar, rx_status->band,
				      rx->ppdu.info0,
				      __le32_to_cpu(rx->ppdu.info1),
				      __le32_to_cpu(rx->ppdu.info2),
				      rx_status);
	}

	ath10k_dbg_dump(ATH10K_DBG_HTT_DUMP, NULL, "htt rx ind: ",
			rx, sizeof(*rx) +
			(sizeof(struct htt_rx_indication_mpdu_range) *
				num_mpdu_ranges));

	for (i = 0; i < num_mpdu_ranges; i++) {
		status = mpdu_ranges[i].mpdu_range_status;

		for (j = 0; j < mpdu_ranges[i].mpdu_count; j++) {
			struct sk_buff *msdu_head, *msdu_tail;

			msdu_head = NULL;
			msdu_tail = NULL;
			ret = ath10k_htt_rx_amsdu_pop(htt,
						      &fw_desc,
						      &fw_desc_len,
						      &msdu_head,
						      &msdu_tail);

			if (ret < 0) {
				ath10k_warn("failed to pop amsdu from htt rx ring %d\n",
					    ret);
				ath10k_htt_rx_free_msdu_chain(msdu_head);
				continue;
			}

			rxd = container_of((void *)msdu_head->data,
					   struct htt_rx_desc,
					   msdu_payload);
			attention = __le32_to_cpu(rxd->attention.flags);

			if (!ath10k_htt_rx_amsdu_allowed(htt, msdu_head,
							 status,
							 channel_set,
							 attention)) {
				ath10k_htt_rx_free_msdu_chain(msdu_head);
				continue;
			}

			if (ret > 0 &&
			    ath10k_unchain_msdu(msdu_head) < 0) {
				ath10k_htt_rx_free_msdu_chain(msdu_head);
				continue;
			}

			if (attention & RX_ATTENTION_FLAGS_FCS_ERR)
				rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;
			else
				rx_status->flag &= ~RX_FLAG_FAILED_FCS_CRC;

			if (attention & RX_ATTENTION_FLAGS_TKIP_MIC_ERR)
				rx_status->flag |= RX_FLAG_MMIC_ERROR;
			else
				rx_status->flag &= ~RX_FLAG_MMIC_ERROR;

			hdr = ath10k_htt_rx_skb_get_hdr(msdu_head);

			if (ath10k_htt_rx_hdr_is_amsdu(hdr))
				ath10k_htt_rx_amsdu(htt, rx_status, msdu_head);
			else
				ath10k_htt_rx_msdu(htt, rx_status, msdu_head);
		}
	}

	tasklet_schedule(&htt->rx_replenish_task);
}

static void ath10k_htt_rx_frag_handler(struct ath10k_htt *htt,
				struct htt_rx_fragment_indication *frag)
{
	struct sk_buff *msdu_head, *msdu_tail;
	enum htt_rx_mpdu_encrypt_type enctype;
	struct htt_rx_desc *rxd;
	enum rx_msdu_decap_format fmt;
	struct ieee80211_rx_status *rx_status = &htt->rx_status;
	struct ieee80211_hdr *hdr;
	int ret;
	bool tkip_mic_err;
	bool decrypt_err;
	u8 *fw_desc;
	int fw_desc_len, hdrlen, paramlen;
	int trim;

	fw_desc_len = __le16_to_cpu(frag->fw_rx_desc_bytes);
	fw_desc = (u8 *)frag->fw_msdu_rx_desc;

	msdu_head = NULL;
	msdu_tail = NULL;

	spin_lock_bh(&htt->rx_ring.lock);
	ret = ath10k_htt_rx_amsdu_pop(htt, &fw_desc, &fw_desc_len,
				      &msdu_head, &msdu_tail);
	spin_unlock_bh(&htt->rx_ring.lock);

	ath10k_dbg(ATH10K_DBG_HTT_DUMP, "htt rx frag ahead\n");

	if (ret) {
		ath10k_warn("failed to pop amsdu from httr rx ring for fragmented rx %d\n",
			    ret);
		ath10k_htt_rx_free_msdu_chain(msdu_head);
		return;
	}

	/* FIXME: implement signal strength */
	rx_status->flag |= RX_FLAG_NO_SIGNAL_VAL;

	hdr = (struct ieee80211_hdr *)msdu_head->data;
	rxd = (void *)msdu_head->data - sizeof(*rxd);
	tkip_mic_err = !!(__le32_to_cpu(rxd->attention.flags) &
				RX_ATTENTION_FLAGS_TKIP_MIC_ERR);
	decrypt_err = !!(__le32_to_cpu(rxd->attention.flags) &
				RX_ATTENTION_FLAGS_DECRYPT_ERR);
	fmt = MS(__le32_to_cpu(rxd->msdu_start.info1),
			RX_MSDU_START_INFO1_DECAP_FORMAT);

	if (fmt != RX_MSDU_DECAP_RAW) {
		ath10k_warn("we dont support non-raw fragmented rx yet\n");
		dev_kfree_skb_any(msdu_head);
		goto end;
	}

	enctype = MS(__le32_to_cpu(rxd->mpdu_start.info0),
		     RX_MPDU_START_INFO0_ENCRYPT_TYPE);
	ath10k_htt_rx_h_protected(htt, rx_status, msdu_head, enctype, fmt,
				  true);
	msdu_head->ip_summed = ath10k_htt_rx_get_csum_state(msdu_head);

	if (tkip_mic_err)
		ath10k_warn("tkip mic error\n");

	if (decrypt_err) {
		ath10k_warn("decryption err in fragmented rx\n");
		dev_kfree_skb_any(msdu_head);
		goto end;
	}

	if (enctype != HTT_RX_MPDU_ENCRYPT_NONE) {
		hdrlen = ieee80211_hdrlen(hdr->frame_control);
		paramlen = ath10k_htt_rx_crypto_param_len(enctype);

		/* It is more efficient to move the header than the payload */
		memmove((void *)msdu_head->data + paramlen,
			(void *)msdu_head->data,
			hdrlen);
		skb_pull(msdu_head, paramlen);
		hdr = (struct ieee80211_hdr *)msdu_head->data;
	}

	/* remove trailing FCS */
	trim  = 4;

	/* remove crypto trailer */
	trim += ath10k_htt_rx_crypto_tail_len(enctype);

	/* last fragment of TKIP frags has MIC */
	if (!ieee80211_has_morefrags(hdr->frame_control) &&
	    enctype == HTT_RX_MPDU_ENCRYPT_TKIP_WPA)
		trim += 8;

	if (trim > msdu_head->len) {
		ath10k_warn("htt rx fragment: trailer longer than the frame itself? drop\n");
		dev_kfree_skb_any(msdu_head);
		goto end;
	}

	skb_trim(msdu_head, msdu_head->len - trim);

	ath10k_dbg_dump(ATH10K_DBG_HTT_DUMP, NULL, "htt rx frag mpdu: ",
			msdu_head->data, msdu_head->len);
	ath10k_process_rx(htt->ar, rx_status, msdu_head);

end:
	if (fw_desc_len > 0) {
		ath10k_dbg(ATH10K_DBG_HTT,
			   "expecting more fragmented rx in one indication %d\n",
			   fw_desc_len);
	}
}

static void ath10k_htt_rx_frm_tx_compl(struct ath10k *ar,
				       struct sk_buff *skb)
{
	struct ath10k_htt *htt = &ar->htt;
	struct htt_resp *resp = (struct htt_resp *)skb->data;
	struct htt_tx_done tx_done = {};
	int status = MS(resp->data_tx_completion.flags, HTT_DATA_TX_STATUS);
	__le16 msdu_id;
	int i;

	lockdep_assert_held(&htt->tx_lock);

	switch (status) {
	case HTT_DATA_TX_STATUS_NO_ACK:
		tx_done.no_ack = true;
		break;
	case HTT_DATA_TX_STATUS_OK:
		break;
	case HTT_DATA_TX_STATUS_DISCARD:
	case HTT_DATA_TX_STATUS_POSTPONE:
	case HTT_DATA_TX_STATUS_DOWNLOAD_FAIL:
		tx_done.discard = true;
		break;
	default:
		ath10k_warn("unhandled tx completion status %d\n", status);
		tx_done.discard = true;
		break;
	}

	ath10k_dbg(ATH10K_DBG_HTT, "htt tx completion num_msdus %d\n",
		   resp->data_tx_completion.num_msdus);

	for (i = 0; i < resp->data_tx_completion.num_msdus; i++) {
		msdu_id = resp->data_tx_completion.msdus[i];
		tx_done.msdu_id = __le16_to_cpu(msdu_id);
		ath10k_txrx_tx_unref(htt, &tx_done);
	}
}

void ath10k_htt_t2h_msg_handler(struct ath10k *ar, struct sk_buff *skb)
{
	struct ath10k_htt *htt = &ar->htt;
	struct htt_resp *resp = (struct htt_resp *)skb->data;

	/* confirm alignment */
	if (!IS_ALIGNED((unsigned long)skb->data, 4))
		ath10k_warn("unaligned htt message, expect trouble\n");

	ath10k_dbg(ATH10K_DBG_HTT, "htt rx, msg_type: 0x%0X\n",
		   resp->hdr.msg_type);
	switch (resp->hdr.msg_type) {
	case HTT_T2H_MSG_TYPE_VERSION_CONF: {
		htt->target_version_major = resp->ver_resp.major;
		htt->target_version_minor = resp->ver_resp.minor;
		complete(&htt->target_version_received);
		break;
	}
	case HTT_T2H_MSG_TYPE_RX_IND:
		spin_lock_bh(&htt->rx_ring.lock);
		__skb_queue_tail(&htt->rx_compl_q, skb);
		spin_unlock_bh(&htt->rx_ring.lock);
		tasklet_schedule(&htt->txrx_compl_task);
		return;
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
		int status = __le32_to_cpu(resp->mgmt_tx_completion.status);

		tx_done.msdu_id =
			__le32_to_cpu(resp->mgmt_tx_completion.desc_id);

		switch (status) {
		case HTT_MGMT_TX_STATUS_OK:
			break;
		case HTT_MGMT_TX_STATUS_RETRY:
			tx_done.no_ack = true;
			break;
		case HTT_MGMT_TX_STATUS_DROP:
			tx_done.discard = true;
			break;
		}

		spin_lock_bh(&htt->tx_lock);
		ath10k_txrx_tx_unref(htt, &tx_done);
		spin_unlock_bh(&htt->tx_lock);
		break;
	}
	case HTT_T2H_MSG_TYPE_TX_COMPL_IND:
		spin_lock_bh(&htt->tx_lock);
		__skb_queue_tail(&htt->tx_compl_q, skb);
		spin_unlock_bh(&htt->tx_lock);
		tasklet_schedule(&htt->txrx_compl_task);
		return;
	case HTT_T2H_MSG_TYPE_SEC_IND: {
		struct ath10k *ar = htt->ar;
		struct htt_security_indication *ev = &resp->security_indication;

		ath10k_dbg(ATH10K_DBG_HTT,
			   "sec ind peer_id %d unicast %d type %d\n",
			  __le16_to_cpu(ev->peer_id),
			  !!(ev->flags & HTT_SECURITY_IS_UNICAST),
			  MS(ev->flags, HTT_SECURITY_TYPE));
		complete(&ar->install_key_done);
		break;
	}
	case HTT_T2H_MSG_TYPE_RX_FRAG_IND: {
		ath10k_dbg_dump(ATH10K_DBG_HTT_DUMP, NULL, "htt event: ",
				skb->data, skb->len);
		ath10k_htt_rx_frag_handler(htt, &resp->rx_frag_ind);
		break;
	}
	case HTT_T2H_MSG_TYPE_TEST:
		/* FIX THIS */
		break;
	case HTT_T2H_MSG_TYPE_STATS_CONF:
		trace_ath10k_htt_stats(skb->data, skb->len);
		break;
	case HTT_T2H_MSG_TYPE_TX_INSPECT_IND:
	case HTT_T2H_MSG_TYPE_RX_ADDBA:
	case HTT_T2H_MSG_TYPE_RX_DELBA:
	case HTT_T2H_MSG_TYPE_RX_FLUSH:
	default:
		ath10k_dbg(ATH10K_DBG_HTT, "htt event (%d) not handled\n",
			   resp->hdr.msg_type);
		ath10k_dbg_dump(ATH10K_DBG_HTT_DUMP, NULL, "htt event: ",
				skb->data, skb->len);
		break;
	};

	/* Free the indication buffer */
	dev_kfree_skb_any(skb);
}

static void ath10k_htt_txrx_compl_task(unsigned long ptr)
{
	struct ath10k_htt *htt = (struct ath10k_htt *)ptr;
	struct htt_resp *resp;
	struct sk_buff *skb;

	spin_lock_bh(&htt->tx_lock);
	while ((skb = __skb_dequeue(&htt->tx_compl_q))) {
		ath10k_htt_rx_frm_tx_compl(htt->ar, skb);
		dev_kfree_skb_any(skb);
	}
	spin_unlock_bh(&htt->tx_lock);

	spin_lock_bh(&htt->rx_ring.lock);
	while ((skb = __skb_dequeue(&htt->rx_compl_q))) {
		resp = (struct htt_resp *)skb->data;
		ath10k_htt_rx_handler(htt, &resp->rx_ind);
		dev_kfree_skb_any(skb);
	}
	spin_unlock_bh(&htt->rx_ring.lock);
}
