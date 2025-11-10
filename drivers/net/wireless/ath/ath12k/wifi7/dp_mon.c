// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2019-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "hal_desc.h"
#include "../dp_mon.h"
#include "dp_mon.h"
#include "../debug.h"
#include "hal_qcn9274.h"
#include "dp_rx.h"
#include "../peer.h"

static void
ath12k_wifi7_dp_mon_rx_memset_ppdu_info(struct hal_rx_mon_ppdu_info *ppdu_info)
{
	memset(ppdu_info, 0, sizeof(*ppdu_info));
	ppdu_info->peer_id = HAL_INVALID_PEERID;
}

static u32
ath12k_wifi7_dp_rx_mon_mpdu_pop(struct ath12k *ar, int mac_id,
				void *ring_entry, struct sk_buff **head_msdu,
				struct sk_buff **tail_msdu,
				struct list_head *used_list,
				u32 *npackets, u32 *ppdu_id)
{
	struct ath12k_mon_data *pmon = (struct ath12k_mon_data *)&ar->dp.mon_data;
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_buffer_addr *p_buf_addr_info, *p_last_buf_addr_info;
	u32 msdu_ppdu_id = 0, msdu_cnt = 0, total_len = 0, frag_len = 0;
	u32 rx_buf_size, rx_pkt_offset, sw_cookie;
	bool is_frag, is_first_msdu, drop_mpdu = false;
	struct hal_reo_entrance_ring *ent_desc =
		(struct hal_reo_entrance_ring *)ring_entry;
	u32 rx_bufs_used = 0, i = 0, desc_bank = 0;
	struct hal_rx_desc *rx_desc, *tail_rx_desc;
	struct hal_rx_msdu_link *msdu_link_desc;
	struct sk_buff *msdu = NULL, *last = NULL;
	struct ath12k_rx_desc_info *desc_info;
	struct ath12k_buffer_addr buf_info;
	struct hal_rx_msdu_list msdu_list;
	struct ath12k_skb_rxcb *rxcb;
	u16 num_msdus = 0;
	dma_addr_t paddr;
	u8 rbm;

	ath12k_wifi7_hal_rx_reo_ent_buf_paddr_get(ring_entry, &paddr,
						  &sw_cookie,
						  &p_last_buf_addr_info,
						  &rbm,
						  &msdu_cnt);

	spin_lock_bh(&pmon->mon_lock);

	if (le32_get_bits(ent_desc->info1,
			  HAL_REO_ENTR_RING_INFO1_RXDMA_PUSH_REASON) ==
			  HAL_REO_DEST_RING_PUSH_REASON_ERR_DETECTED) {
		u8 rxdma_err = le32_get_bits(ent_desc->info1,
					     HAL_REO_ENTR_RING_INFO1_RXDMA_ERROR_CODE);
		if (rxdma_err == HAL_REO_ENTR_RING_RXDMA_ECODE_FLUSH_REQUEST_ERR ||
		    rxdma_err == HAL_REO_ENTR_RING_RXDMA_ECODE_MPDU_LEN_ERR ||
		    rxdma_err == HAL_REO_ENTR_RING_RXDMA_ECODE_OVERFLOW_ERR) {
			drop_mpdu = true;
			pmon->rx_mon_stats.dest_mpdu_drop++;
		}
	}

	is_frag = false;
	is_first_msdu = true;
	rx_pkt_offset = sizeof(struct hal_rx_desc);

	do {
		if (pmon->mon_last_linkdesc_paddr == paddr) {
			pmon->rx_mon_stats.dup_mon_linkdesc_cnt++;
			spin_unlock_bh(&pmon->mon_lock);
			return rx_bufs_used;
		}

		desc_bank = u32_get_bits(sw_cookie, DP_LINK_DESC_BANK_MASK);
		msdu_link_desc =
			dp->link_desc_banks[desc_bank].vaddr +
			(paddr - dp->link_desc_banks[desc_bank].paddr);

		ath12k_wifi7_hal_rx_msdu_list_get(ar, msdu_link_desc, &msdu_list,
						  &num_msdus);
		desc_info = ath12k_dp_get_rx_desc(ar->ab->dp,
						  msdu_list.sw_cookie[num_msdus - 1]);
		tail_rx_desc = (struct hal_rx_desc *)(desc_info->skb)->data;

		for (i = 0; i < num_msdus; i++) {
			u32 l2_hdr_offset;

			if (pmon->mon_last_buf_cookie == msdu_list.sw_cookie[i]) {
				ath12k_dbg(ar->ab, ATH12K_DBG_DATA,
					   "i %d last_cookie %d is same\n",
					   i, pmon->mon_last_buf_cookie);
				drop_mpdu = true;
				pmon->rx_mon_stats.dup_mon_buf_cnt++;
				continue;
			}

			desc_info =
				ath12k_dp_get_rx_desc(ar->ab->dp, msdu_list.sw_cookie[i]);
			msdu = desc_info->skb;

			if (!msdu) {
				ath12k_dbg(ar->ab, ATH12K_DBG_DATA,
					   "msdu_pop: invalid msdu (%d/%d)\n",
					   i + 1, num_msdus);
				goto next_msdu;
			}
			rxcb = ATH12K_SKB_RXCB(msdu);
			if (rxcb->paddr != msdu_list.paddr[i]) {
				ath12k_dbg(ar->ab, ATH12K_DBG_DATA,
					   "i %d paddr %lx != %lx\n",
					   i, (unsigned long)rxcb->paddr,
					   (unsigned long)msdu_list.paddr[i]);
				drop_mpdu = true;
				continue;
			}
			if (!rxcb->unmapped) {
				dma_unmap_single(ar->ab->dev, rxcb->paddr,
						 msdu->len +
						 skb_tailroom(msdu),
						 DMA_FROM_DEVICE);
				rxcb->unmapped = 1;
			}
			if (drop_mpdu) {
				ath12k_dbg(ar->ab, ATH12K_DBG_DATA,
					   "i %d drop msdu %p *ppdu_id %x\n",
					   i, msdu, *ppdu_id);
				dev_kfree_skb_any(msdu);
				msdu = NULL;
				goto next_msdu;
			}

			rx_desc = (struct hal_rx_desc *)msdu->data;
			l2_hdr_offset = ath12k_dp_rx_h_l3pad(ar->ab, tail_rx_desc);
			if (is_first_msdu) {
				if (!ath12k_wifi7_dp_rxdesc_mpdu_valid(ar->ab,
								       rx_desc)) {
					drop_mpdu = true;
					dev_kfree_skb_any(msdu);
					msdu = NULL;
					pmon->mon_last_linkdesc_paddr = paddr;
					goto next_msdu;
				}
				msdu_ppdu_id =
					ath12k_dp_rxdesc_get_ppduid(ar->ab, rx_desc);

				if (ath12k_dp_mon_comp_ppduid(msdu_ppdu_id,
							      ppdu_id)) {
					spin_unlock_bh(&pmon->mon_lock);
					return rx_bufs_used;
				}
				pmon->mon_last_linkdesc_paddr = paddr;
				is_first_msdu = false;
			}
			ath12k_dp_mon_get_buf_len(&msdu_list.msdu_info[i],
						  &is_frag, &total_len,
						  &frag_len, &msdu_cnt);
			rx_buf_size = rx_pkt_offset + l2_hdr_offset + frag_len;

			if (ath12k_dp_pkt_set_pktlen(msdu, rx_buf_size)) {
				dev_kfree_skb_any(msdu);
				goto next_msdu;
			}

			if (!(*head_msdu))
				*head_msdu = msdu;
			else if (last)
				last->next = msdu;

			last = msdu;
next_msdu:
			pmon->mon_last_buf_cookie = msdu_list.sw_cookie[i];
			rx_bufs_used++;
			desc_info->skb = NULL;
			list_add_tail(&desc_info->list, used_list);
		}

		ath12k_wifi7_hal_rx_buf_addr_info_set(&buf_info, paddr,
						      sw_cookie, rbm);

		ath12k_dp_mon_next_link_desc_get(ab, msdu_link_desc, &paddr,
						 &sw_cookie, &rbm,
						 &p_buf_addr_info);

		ath12k_dp_arch_rx_link_desc_return(ar->ab->dp, &buf_info,
						   HAL_WBM_REL_BM_ACT_PUT_IN_IDLE);

		p_last_buf_addr_info = p_buf_addr_info;

	} while (paddr && msdu_cnt);

	spin_unlock_bh(&pmon->mon_lock);

	if (last)
		last->next = NULL;

	*tail_msdu = msdu;

	if (msdu_cnt == 0)
		*npackets = 1;

	return rx_bufs_used;
}

/* The destination ring processing is stuck if the destination is not
 * moving while status ring moves 16 PPDU. The destination ring processing
 * skips this destination ring PPDU as a workaround.
 */
#define MON_DEST_RING_STUCK_MAX_CNT 16

static void
ath12k_wifi7_dp_rx_mon_dest_process(struct ath12k *ar, int mac_id,
				    u32 quota, struct napi_struct *napi)
{
	struct ath12k_mon_data *pmon = (struct ath12k_mon_data *)&ar->dp.mon_data;
	struct ath12k_pdev_mon_stats *rx_mon_stats;
	u32 ppdu_id, rx_bufs_used = 0, ring_id;
	u32 mpdu_rx_bufs_used, npackets = 0;
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	void *ring_entry, *mon_dst_srng;
	struct dp_mon_mpdu *tmp_mpdu;
	LIST_HEAD(rx_desc_used_list);
	struct hal_srng *srng;

	ring_id = dp->rxdma_err_dst_ring[mac_id].ring_id;
	srng = &ab->hal.srng_list[ring_id];

	mon_dst_srng = &ab->hal.srng_list[ring_id];

	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, mon_dst_srng);

	ppdu_id = pmon->mon_ppdu_info.ppdu_id;
	rx_mon_stats = &pmon->rx_mon_stats;

	while ((ring_entry = ath12k_hal_srng_dst_peek(ar->ab, mon_dst_srng))) {
		struct sk_buff *head_msdu, *tail_msdu;

		head_msdu = NULL;
		tail_msdu = NULL;

		mpdu_rx_bufs_used = ath12k_wifi7_dp_rx_mon_mpdu_pop(ar, mac_id,
								    ring_entry,
								    &head_msdu,
								    &tail_msdu,
								    &rx_desc_used_list,
								    &npackets,
								    &ppdu_id);

		rx_bufs_used += mpdu_rx_bufs_used;

		if (mpdu_rx_bufs_used) {
			dp->mon_dest_ring_stuck_cnt = 0;
		} else {
			dp->mon_dest_ring_stuck_cnt++;
			rx_mon_stats->dest_mon_not_reaped++;
		}

		if (dp->mon_dest_ring_stuck_cnt > MON_DEST_RING_STUCK_MAX_CNT) {
			rx_mon_stats->dest_mon_stuck++;
			ath12k_dbg(ar->ab, ATH12K_DBG_DATA,
				   "status ring ppdu_id=%d dest ring ppdu_id=%d mon_dest_ring_stuck_cnt=%d dest_mon_not_reaped=%u dest_mon_stuck=%u\n",
				   pmon->mon_ppdu_info.ppdu_id, ppdu_id,
				   dp->mon_dest_ring_stuck_cnt,
				   rx_mon_stats->dest_mon_not_reaped,
				   rx_mon_stats->dest_mon_stuck);
			spin_lock_bh(&pmon->mon_lock);
			pmon->mon_ppdu_info.ppdu_id = ppdu_id;
			spin_unlock_bh(&pmon->mon_lock);
			continue;
		}

		if (ppdu_id != pmon->mon_ppdu_info.ppdu_id) {
			spin_lock_bh(&pmon->mon_lock);
			pmon->mon_ppdu_status = DP_PPDU_STATUS_START;
			spin_unlock_bh(&pmon->mon_lock);
			ath12k_dbg(ar->ab, ATH12K_DBG_DATA,
				   "dest_rx: new ppdu_id %x != status ppdu_id %x dest_mon_not_reaped = %u dest_mon_stuck = %u\n",
				   ppdu_id, pmon->mon_ppdu_info.ppdu_id,
				   rx_mon_stats->dest_mon_not_reaped,
				   rx_mon_stats->dest_mon_stuck);
			break;
		}

		if (head_msdu && tail_msdu) {
			tmp_mpdu = kzalloc(sizeof(*tmp_mpdu), GFP_ATOMIC);
			if (!tmp_mpdu)
				break;

			tmp_mpdu->head = head_msdu;
			tmp_mpdu->tail = tail_msdu;
			tmp_mpdu->err_bitmap = pmon->err_bitmap;
			tmp_mpdu->decap_format = pmon->decap_format;
			ath12k_dp_mon_rx_deliver(&ar->dp, tmp_mpdu,
						 &pmon->mon_ppdu_info, napi);
			rx_mon_stats->dest_mpdu_done++;
			kfree(tmp_mpdu);
		}

		ring_entry = ath12k_hal_srng_dst_get_next_entry(ar->ab,
								mon_dst_srng);
	}
	ath12k_hal_srng_access_end(ar->ab, mon_dst_srng);

	spin_unlock_bh(&srng->lock);

	if (rx_bufs_used) {
		rx_mon_stats->dest_ppdu_done++;
		ath12k_dp_rx_bufs_replenish(ar->ab->dp,
					    &dp->rx_refill_buf_ring,
					    &rx_desc_used_list,
					    rx_bufs_used);
	}
}

static enum dp_mon_status_buf_state
ath12k_wifi7_dp_rx_mon_buf_done(struct ath12k_base *ab, struct hal_srng *srng,
				struct dp_rxdma_mon_ring *rx_ring)
{
	struct ath12k_skb_rxcb *rxcb;
	struct hal_tlv_64_hdr *tlv;
	struct sk_buff *skb;
	void *status_desc;
	dma_addr_t paddr;
	u32 cookie;
	int buf_id;
	u8 rbm;

	status_desc = ath12k_hal_srng_src_next_peek(ab, srng);
	if (!status_desc)
		return DP_MON_STATUS_NO_DMA;

	ath12k_wifi7_hal_rx_buf_addr_info_get(status_desc, &paddr, &cookie, &rbm);

	buf_id = u32_get_bits(cookie, DP_RXDMA_BUF_COOKIE_BUF_ID);

	spin_lock_bh(&rx_ring->idr_lock);
	skb = idr_find(&rx_ring->bufs_idr, buf_id);
	spin_unlock_bh(&rx_ring->idr_lock);

	if (!skb)
		return DP_MON_STATUS_NO_DMA;

	rxcb = ATH12K_SKB_RXCB(skb);
	dma_sync_single_for_cpu(ab->dev, rxcb->paddr,
				skb->len + skb_tailroom(skb),
				DMA_FROM_DEVICE);

	tlv = (struct hal_tlv_64_hdr *)skb->data;
	if (le64_get_bits(tlv->tl, HAL_TLV_HDR_TAG) != HAL_RX_STATUS_BUFFER_DONE)
		return DP_MON_STATUS_NO_DMA;

	return DP_MON_STATUS_REPLINISH;
}

static enum hal_rx_mon_status
ath12k_wifi7_dp_mon_parse_rx_dest(struct ath12k_pdev_dp *dp_pdev,
				  struct ath12k_mon_data *pmon,
				  struct sk_buff *skb)
{
	struct ath12k *ar = ath12k_pdev_dp_to_ar(dp_pdev);
	struct hal_tlv_64_hdr *tlv;
	struct ath12k_skb_rxcb *rxcb;
	enum hal_rx_mon_status hal_status;
	u16 tlv_tag, tlv_len;
	u8 *ptr = skb->data;

	do {
		tlv = (struct hal_tlv_64_hdr *)ptr;
		tlv_tag = le64_get_bits(tlv->tl, HAL_TLV_64_HDR_TAG);

		/* The actual length of PPDU_END is the combined length of many PHY
		 * TLVs that follow. Skip the TLV header and
		 * rx_rxpcu_classification_overview that follows the header to get to
		 * next TLV.
		 */

		if (tlv_tag == HAL_RX_PPDU_END)
			tlv_len = sizeof(struct hal_rx_rxpcu_classification_overview);
		else
			tlv_len = le64_get_bits(tlv->tl, HAL_TLV_64_HDR_LEN);

		hal_status = ath12k_dp_mon_rx_parse_status_tlv(dp_pdev, pmon, tlv);

		if (ar->monitor_started && ar->ab->hw_params->rxdma1_enable &&
		    ath12k_dp_mon_parse_rx_dest_tlv(dp_pdev, pmon, hal_status,
						    tlv->value))
			return HAL_RX_MON_STATUS_PPDU_DONE;

		ptr += sizeof(*tlv) + tlv_len;
		ptr = PTR_ALIGN(ptr, HAL_TLV_64_ALIGN);

		if ((ptr - skb->data) > skb->len)
			break;

	} while ((hal_status == HAL_RX_MON_STATUS_PPDU_NOT_DONE) ||
		 (hal_status == HAL_RX_MON_STATUS_BUF_ADDR) ||
		 (hal_status == HAL_RX_MON_STATUS_MPDU_START) ||
		 (hal_status == HAL_RX_MON_STATUS_MPDU_END) ||
		 (hal_status == HAL_RX_MON_STATUS_MSDU_END));

	rxcb = ATH12K_SKB_RXCB(skb);
	if (rxcb->is_end_of_ppdu)
		hal_status = HAL_RX_MON_STATUS_PPDU_DONE;

	return hal_status;
}

static enum hal_rx_mon_status
ath12k_wifi7_dp_mon_rx_parse_mon_status(struct ath12k_pdev_dp *dp_pdev,
					struct ath12k_mon_data *pmon,
					struct sk_buff *skb,
					struct napi_struct *napi)
{
	struct hal_rx_mon_ppdu_info *ppdu_info = &pmon->mon_ppdu_info;
	struct dp_mon_mpdu *tmp;
	struct dp_mon_mpdu *mon_mpdu = pmon->mon_mpdu;
	enum hal_rx_mon_status hal_status;

	hal_status = ath12k_wifi7_dp_mon_parse_rx_dest(dp_pdev, pmon, skb);
	if (hal_status != HAL_RX_MON_STATUS_PPDU_DONE)
		return hal_status;

	list_for_each_entry_safe(mon_mpdu, tmp, &pmon->dp_rx_mon_mpdu_list, list) {
		list_del(&mon_mpdu->list);

		if (mon_mpdu->head && mon_mpdu->tail)
			ath12k_dp_mon_rx_deliver(dp_pdev, mon_mpdu, ppdu_info, napi);

		kfree(mon_mpdu);
	}

	return hal_status;
}

static int
ath12k_wifi7_dp_rx_reap_mon_status_ring(struct ath12k_base *ab, int mac_id,
					int *budget, struct sk_buff_head *skb_list)
{
	const struct ath12k_hw_hal_params *hal_params;
	int buf_id, srng_id, num_buffs_reaped = 0;
	enum dp_mon_status_buf_state reap_status;
	struct dp_rxdma_mon_ring *rx_ring;
	struct ath12k_mon_data *pmon;
	struct ath12k_skb_rxcb *rxcb;
	struct hal_tlv_64_hdr *tlv;
	void *rx_mon_status_desc;
	struct hal_srng *srng;
	struct ath12k_dp *dp;
	struct sk_buff *skb;
	struct ath12k *ar;
	dma_addr_t paddr;
	u32 cookie;
	u8 rbm;

	ar = ab->pdevs[ath12k_hw_mac_id_to_pdev_id(ab->hw_params, mac_id)].ar;
	dp = ath12k_ab_to_dp(ab);
	pmon = &ar->dp.mon_data;
	srng_id = ath12k_hw_mac_id_to_srng_id(ab->hw_params, mac_id);
	rx_ring = &dp->rx_mon_status_refill_ring[srng_id];

	srng = &ab->hal.srng_list[rx_ring->refill_buf_ring.ring_id];

	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);

	while (*budget) {
		*budget -= 1;
		rx_mon_status_desc = ath12k_hal_srng_src_peek(ab, srng);
		if (!rx_mon_status_desc) {
			pmon->buf_state = DP_MON_STATUS_REPLINISH;
			break;
		}
		ath12k_wifi7_hal_rx_buf_addr_info_get(rx_mon_status_desc, &paddr,
						      &cookie, &rbm);
		if (paddr) {
			buf_id = u32_get_bits(cookie, DP_RXDMA_BUF_COOKIE_BUF_ID);

			spin_lock_bh(&rx_ring->idr_lock);
			skb = idr_find(&rx_ring->bufs_idr, buf_id);
			spin_unlock_bh(&rx_ring->idr_lock);

			if (!skb) {
				ath12k_warn(ab, "rx monitor status with invalid buf_id %d\n",
					    buf_id);
				pmon->buf_state = DP_MON_STATUS_REPLINISH;
				goto move_next;
			}

			rxcb = ATH12K_SKB_RXCB(skb);

			dma_sync_single_for_cpu(ab->dev, rxcb->paddr,
						skb->len + skb_tailroom(skb),
						DMA_FROM_DEVICE);

			tlv = (struct hal_tlv_64_hdr *)skb->data;
			if (le64_get_bits(tlv->tl, HAL_TLV_HDR_TAG) !=
					HAL_RX_STATUS_BUFFER_DONE) {
				pmon->buf_state = DP_MON_STATUS_NO_DMA;
				ath12k_warn(ab,
					    "mon status DONE not set %llx, buf_id %d\n",
					    le64_get_bits(tlv->tl, HAL_TLV_HDR_TAG),
					    buf_id);
				/* RxDMA status done bit might not be set even
				 * though tp is moved by HW.
				 */

				/* If done status is missing:
				 * 1. As per MAC team's suggestion,
				 *    when HP + 1 entry is peeked and if DMA
				 *    is not done and if HP + 2 entry's DMA done
				 *    is set. skip HP + 1 entry and
				 *    start processing in next interrupt.
				 * 2. If HP + 2 entry's DMA done is not set,
				 *    poll onto HP + 1 entry DMA done to be set.
				 *    Check status for same buffer for next time
				 *    dp_rx_mon_status_srng_process
				 */
				reap_status = ath12k_wifi7_dp_rx_mon_buf_done(ab, srng,
									      rx_ring);
				if (reap_status == DP_MON_STATUS_NO_DMA)
					continue;

				spin_lock_bh(&rx_ring->idr_lock);
				idr_remove(&rx_ring->bufs_idr, buf_id);
				spin_unlock_bh(&rx_ring->idr_lock);

				dma_unmap_single(ab->dev, rxcb->paddr,
						 skb->len + skb_tailroom(skb),
						 DMA_FROM_DEVICE);

				dev_kfree_skb_any(skb);
				pmon->buf_state = DP_MON_STATUS_REPLINISH;
				goto move_next;
			}

			spin_lock_bh(&rx_ring->idr_lock);
			idr_remove(&rx_ring->bufs_idr, buf_id);
			spin_unlock_bh(&rx_ring->idr_lock);

			dma_unmap_single(ab->dev, rxcb->paddr,
					 skb->len + skb_tailroom(skb),
					 DMA_FROM_DEVICE);

			if (ath12k_dp_pkt_set_pktlen(skb, RX_MON_STATUS_BUF_SIZE)) {
				dev_kfree_skb_any(skb);
				goto move_next;
			}
			__skb_queue_tail(skb_list, skb);
		} else {
			pmon->buf_state = DP_MON_STATUS_REPLINISH;
		}
move_next:
		skb = ath12k_dp_rx_alloc_mon_status_buf(ab, rx_ring,
							&buf_id);
		hal_params = ab->hal.hal_params;

		if (!skb) {
			ath12k_warn(ab, "failed to alloc buffer for status ring\n");
			ath12k_wifi7_hal_rx_buf_addr_info_set(rx_mon_status_desc,
							      0, 0,
							      hal_params->rx_buf_rbm);
			num_buffs_reaped++;
			break;
		}
		rxcb = ATH12K_SKB_RXCB(skb);

		cookie = u32_encode_bits(mac_id, DP_RXDMA_BUF_COOKIE_PDEV_ID) |
			 u32_encode_bits(buf_id, DP_RXDMA_BUF_COOKIE_BUF_ID);

		ath12k_wifi7_hal_rx_buf_addr_info_set(rx_mon_status_desc, rxcb->paddr,
						      cookie, hal_params->rx_buf_rbm);
		ath12k_hal_srng_src_get_next_entry(ab, srng);
		num_buffs_reaped++;
	}
	ath12k_hal_srng_access_end(ab, srng);
	spin_unlock_bh(&srng->lock);

	return num_buffs_reaped;
}

static int
__ath12k_wifi7_dp_mon_process_ring(struct ath12k *ar, int mac_id,
				   struct napi_struct *napi, int *budget)
{
	struct ath12k_mon_data *pmon = (struct ath12k_mon_data *)&ar->dp.mon_data;
	struct ath12k_pdev_mon_stats *rx_mon_stats = &pmon->rx_mon_stats;
	struct hal_rx_mon_ppdu_info *ppdu_info = &pmon->mon_ppdu_info;
	enum hal_rx_mon_status hal_status;
	struct sk_buff_head skb_list;
	int num_buffs_reaped;
	struct sk_buff *skb;

	__skb_queue_head_init(&skb_list);

	num_buffs_reaped = ath12k_wifi7_dp_rx_reap_mon_status_ring(ar->ab, mac_id,
								   budget, &skb_list);
	if (!num_buffs_reaped)
		goto exit;

	while ((skb = __skb_dequeue(&skb_list))) {
		memset(ppdu_info, 0, sizeof(*ppdu_info));
		ppdu_info->peer_id = HAL_INVALID_PEERID;

		hal_status = ath12k_wifi7_dp_mon_parse_rx_dest(&ar->dp, pmon, skb);

		if (ar->monitor_started &&
		    pmon->mon_ppdu_status == DP_PPDU_STATUS_START &&
		    hal_status == HAL_TLV_STATUS_PPDU_DONE) {
			rx_mon_stats->status_ppdu_done++;
			pmon->mon_ppdu_status = DP_PPDU_STATUS_DONE;
			ath12k_wifi7_dp_rx_mon_dest_process(ar, mac_id, *budget, napi);
			pmon->mon_ppdu_status = DP_PPDU_STATUS_START;
		}

		dev_kfree_skb_any(skb);
	}

exit:
	return num_buffs_reaped;
}

static int
ath12k_wifi7_dp_mon_srng_process(struct ath12k_pdev_dp *pdev_dp, int *budget,
				 struct napi_struct *napi)
{
	struct ath12k_dp *dp = pdev_dp->dp;
	struct ath12k_base *ab = dp->ab;
	struct ath12k_mon_data *pmon = (struct ath12k_mon_data *)&pdev_dp->mon_data;
	struct hal_rx_mon_ppdu_info *ppdu_info = &pmon->mon_ppdu_info;
	struct hal_mon_dest_desc *mon_dst_desc;
	struct sk_buff *skb;
	struct ath12k_skb_rxcb *rxcb;
	struct dp_srng *mon_dst_ring;
	struct hal_srng *srng;
	struct dp_rxdma_mon_ring *buf_ring;
	struct ath12k_dp_link_peer *peer;
	struct sk_buff_head skb_list;
	u64 cookie;
	int num_buffs_reaped = 0, srng_id, buf_id;
	u32 hal_status, end_offset, info0, end_reason;
	u8 pdev_idx = ath12k_hw_mac_id_to_pdev_id(ab->hw_params, pdev_dp->mac_id);

	__skb_queue_head_init(&skb_list);
	srng_id = ath12k_hw_mac_id_to_srng_id(ab->hw_params, pdev_idx);
	mon_dst_ring = &pdev_dp->rxdma_mon_dst_ring[srng_id];
	buf_ring = &dp->rxdma_mon_buf_ring;

	srng = &ab->hal.srng_list[mon_dst_ring->ring_id];
	spin_lock_bh(&srng->lock);
	ath12k_hal_srng_access_begin(ab, srng);

	while (likely(*budget)) {
		mon_dst_desc = ath12k_hal_srng_dst_peek(ab, srng);
		if (unlikely(!mon_dst_desc))
			break;

		/* In case of empty descriptor, the cookie in the ring descriptor
		 * is invalid. Therefore, this entry is skipped, and ring processing
		 * continues.
		 */
		info0 = le32_to_cpu(mon_dst_desc->info0);
		if (u32_get_bits(info0, HAL_MON_DEST_INFO0_EMPTY_DESC))
			goto move_next;

		cookie = le32_to_cpu(mon_dst_desc->cookie);
		buf_id = u32_get_bits(cookie, DP_RXDMA_BUF_COOKIE_BUF_ID);

		spin_lock_bh(&buf_ring->idr_lock);
		skb = idr_remove(&buf_ring->bufs_idr, buf_id);
		spin_unlock_bh(&buf_ring->idr_lock);

		if (unlikely(!skb)) {
			ath12k_warn(ab, "monitor destination with invalid buf_id %d\n",
				    buf_id);
			goto move_next;
		}

		rxcb = ATH12K_SKB_RXCB(skb);
		dma_unmap_single(ab->dev, rxcb->paddr,
				 skb->len + skb_tailroom(skb),
				 DMA_FROM_DEVICE);

		end_reason = u32_get_bits(info0, HAL_MON_DEST_INFO0_END_REASON);

		/* HAL_MON_FLUSH_DETECTED implies that an rx flush received at the end of
		 * rx PPDU and HAL_MON_PPDU_TRUNCATED implies that the PPDU got
		 * truncated due to a system level error. In both the cases, buffer data
		 * can be discarded
		 */
		if ((end_reason == HAL_MON_FLUSH_DETECTED) ||
		    (end_reason == HAL_MON_PPDU_TRUNCATED)) {
			ath12k_dbg(ab, ATH12K_DBG_DATA,
				   "Monitor dest descriptor end reason %d", end_reason);
			dev_kfree_skb_any(skb);
			goto move_next;
		}

		/* Calculate the budget when the ring descriptor with the
		 * HAL_MON_END_OF_PPDU to ensure that one PPDU worth of data is always
		 * reaped. This helps to efficiently utilize the NAPI budget.
		 */
		if (end_reason == HAL_MON_END_OF_PPDU) {
			*budget -= 1;
			rxcb->is_end_of_ppdu = true;
		}

		end_offset = u32_get_bits(info0, HAL_MON_DEST_INFO0_END_OFFSET);
		if (likely(end_offset <= DP_RX_BUFFER_SIZE)) {
			skb_put(skb, end_offset);
		} else {
			ath12k_warn(ab,
				    "invalid offset on mon stats destination %u\n",
				    end_offset);
			skb_put(skb, DP_RX_BUFFER_SIZE);
		}

		__skb_queue_tail(&skb_list, skb);

move_next:
		ath12k_dp_mon_buf_replenish(ab, buf_ring, 1);
		ath12k_hal_srng_dst_get_next_entry(ab, srng);
		num_buffs_reaped++;
	}

	ath12k_hal_srng_access_end(ab, srng);
	spin_unlock_bh(&srng->lock);

	if (!num_buffs_reaped)
		return 0;

	/* In some cases, one PPDU worth of data can be spread across multiple NAPI
	 * schedules, To avoid losing existing parsed ppdu_info information, skip
	 * the memset of the ppdu_info structure and continue processing it.
	 */
	if (!ppdu_info->ppdu_continuation)
		ath12k_wifi7_dp_mon_rx_memset_ppdu_info(ppdu_info);

	while ((skb = __skb_dequeue(&skb_list))) {
		hal_status = ath12k_wifi7_dp_mon_rx_parse_mon_status(pdev_dp, pmon,
								     skb, napi);
		if (hal_status != HAL_RX_MON_STATUS_PPDU_DONE) {
			ppdu_info->ppdu_continuation = true;
			dev_kfree_skb_any(skb);
			continue;
		}

		if (ppdu_info->peer_id == HAL_INVALID_PEERID)
			goto free_skb;

		rcu_read_lock();
		peer = ath12k_dp_link_peer_find_by_peerid(pdev_dp, ppdu_info->peer_id);
		if (!peer || !peer->sta) {
			ath12k_dbg(ab, ATH12K_DBG_DATA,
				   "failed to find the peer with monitor peer_id %d\n",
				   ppdu_info->peer_id);
			goto next_skb;
		}

		if (ppdu_info->reception_type == HAL_RX_RECEPTION_TYPE_SU) {
			ath12k_dp_mon_rx_update_peer_su_stats(peer, ppdu_info);
		} else if ((ppdu_info->fc_valid) &&
			   (ppdu_info->ast_index != HAL_AST_IDX_INVALID)) {
			ath12k_dp_mon_rx_process_ulofdma(ppdu_info);
			ath12k_dp_mon_rx_update_peer_mu_stats(ab, ppdu_info);
		}

next_skb:
		rcu_read_unlock();
free_skb:
		dev_kfree_skb_any(skb);
		ath12k_wifi7_dp_mon_rx_memset_ppdu_info(ppdu_info);
	}

	return num_buffs_reaped;
}

int ath12k_wifi7_dp_mon_process_ring(struct ath12k_dp *dp, int mac_id,
				     struct napi_struct *napi, int budget,
				     enum dp_monitor_mode monitor_mode)
{
	u8 pdev_idx = ath12k_hw_mac_id_to_pdev_id(dp->hw_params, mac_id);
	struct ath12k_pdev_dp *dp_pdev;
	struct ath12k *ar;
	int num_buffs_reaped = 0;

	rcu_read_lock();

	dp_pdev = ath12k_dp_to_pdev_dp(dp, pdev_idx);
	if (!dp_pdev) {
		rcu_read_unlock();
		return 0;
	}

	if (dp->hw_params->rxdma1_enable) {
		if (monitor_mode == ATH12K_DP_RX_MONITOR_MODE)
			num_buffs_reaped = ath12k_wifi7_dp_mon_srng_process(dp_pdev,
									    &budget,
									    napi);
	} else {
		ar = ath12k_pdev_dp_to_ar(dp_pdev);

		if (ar->monitor_started)
			num_buffs_reaped =
				__ath12k_wifi7_dp_mon_process_ring(ar, mac_id, napi,
								   &budget);
	}

	rcu_read_unlock();

	return num_buffs_reaped;
}
