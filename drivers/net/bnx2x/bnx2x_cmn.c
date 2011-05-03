/* bnx2x_cmn.c: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2010 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Eliezer Tamir
 * Based on code from Michael Chan's bnx2 driver
 * UDP CSUM errata workaround by Arik Gendelman
 * Slowpath and fastpath rework by Vladislav Zolotarov
 * Statistics and Link management by Yitchak Gertner
 *
 */

#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <net/ipv6.h>
#include <net/ip6_checksum.h>
#include <linux/firmware.h>
#include "bnx2x_cmn.h"

#include "bnx2x_init.h"

static int bnx2x_setup_irqs(struct bnx2x *bp);

/* free skb in the packet ring at pos idx
 * return idx of last bd freed
 */
static u16 bnx2x_free_tx_pkt(struct bnx2x *bp, struct bnx2x_fastpath *fp,
			     u16 idx)
{
	struct sw_tx_bd *tx_buf = &fp->tx_buf_ring[idx];
	struct eth_tx_start_bd *tx_start_bd;
	struct eth_tx_bd *tx_data_bd;
	struct sk_buff *skb = tx_buf->skb;
	u16 bd_idx = TX_BD(tx_buf->first_bd), new_cons;
	int nbd;

	/* prefetch skb end pointer to speedup dev_kfree_skb() */
	prefetch(&skb->end);

	DP(BNX2X_MSG_OFF, "pkt_idx %d  buff @(%p)->skb %p\n",
	   idx, tx_buf, skb);

	/* unmap first bd */
	DP(BNX2X_MSG_OFF, "free bd_idx %d\n", bd_idx);
	tx_start_bd = &fp->tx_desc_ring[bd_idx].start_bd;
	dma_unmap_single(&bp->pdev->dev, BD_UNMAP_ADDR(tx_start_bd),
			 BD_UNMAP_LEN(tx_start_bd), DMA_TO_DEVICE);

	nbd = le16_to_cpu(tx_start_bd->nbd) - 1;
#ifdef BNX2X_STOP_ON_ERROR
	if ((nbd - 1) > (MAX_SKB_FRAGS + 2)) {
		BNX2X_ERR("BAD nbd!\n");
		bnx2x_panic();
	}
#endif
	new_cons = nbd + tx_buf->first_bd;

	/* Get the next bd */
	bd_idx = TX_BD(NEXT_TX_IDX(bd_idx));

	/* Skip a parse bd... */
	--nbd;
	bd_idx = TX_BD(NEXT_TX_IDX(bd_idx));

	/* ...and the TSO split header bd since they have no mapping */
	if (tx_buf->flags & BNX2X_TSO_SPLIT_BD) {
		--nbd;
		bd_idx = TX_BD(NEXT_TX_IDX(bd_idx));
	}

	/* now free frags */
	while (nbd > 0) {

		DP(BNX2X_MSG_OFF, "free frag bd_idx %d\n", bd_idx);
		tx_data_bd = &fp->tx_desc_ring[bd_idx].reg_bd;
		dma_unmap_page(&bp->pdev->dev, BD_UNMAP_ADDR(tx_data_bd),
			       BD_UNMAP_LEN(tx_data_bd), DMA_TO_DEVICE);
		if (--nbd)
			bd_idx = TX_BD(NEXT_TX_IDX(bd_idx));
	}

	/* release skb */
	WARN_ON(!skb);
	dev_kfree_skb(skb);
	tx_buf->first_bd = 0;
	tx_buf->skb = NULL;

	return new_cons;
}

int bnx2x_tx_int(struct bnx2x_fastpath *fp)
{
	struct bnx2x *bp = fp->bp;
	struct netdev_queue *txq;
	u16 hw_cons, sw_cons, bd_cons = fp->tx_bd_cons;

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return -1;
#endif

	txq = netdev_get_tx_queue(bp->dev, fp->index);
	hw_cons = le16_to_cpu(*fp->tx_cons_sb);
	sw_cons = fp->tx_pkt_cons;

	while (sw_cons != hw_cons) {
		u16 pkt_cons;

		pkt_cons = TX_BD(sw_cons);

		DP(NETIF_MSG_TX_DONE, "queue[%d]: hw_cons %u  sw_cons %u "
				      " pkt_cons %u\n",
		   fp->index, hw_cons, sw_cons, pkt_cons);

		bd_cons = bnx2x_free_tx_pkt(bp, fp, pkt_cons);
		sw_cons++;
	}

	fp->tx_pkt_cons = sw_cons;
	fp->tx_bd_cons = bd_cons;

	/* Need to make the tx_bd_cons update visible to start_xmit()
	 * before checking for netif_tx_queue_stopped().  Without the
	 * memory barrier, there is a small possibility that
	 * start_xmit() will miss it and cause the queue to be stopped
	 * forever.
	 */
	smp_mb();

	if (unlikely(netif_tx_queue_stopped(txq))) {
		/* Taking tx_lock() is needed to prevent reenabling the queue
		 * while it's empty. This could have happen if rx_action() gets
		 * suspended in bnx2x_tx_int() after the condition before
		 * netif_tx_wake_queue(), while tx_action (bnx2x_start_xmit()):
		 *
		 * stops the queue->sees fresh tx_bd_cons->releases the queue->
		 * sends some packets consuming the whole queue again->
		 * stops the queue
		 */

		__netif_tx_lock(txq, smp_processor_id());

		if ((netif_tx_queue_stopped(txq)) &&
		    (bp->state == BNX2X_STATE_OPEN) &&
		    (bnx2x_tx_avail(fp) >= MAX_SKB_FRAGS + 3))
			netif_tx_wake_queue(txq);

		__netif_tx_unlock(txq);
	}
	return 0;
}

static inline void bnx2x_update_last_max_sge(struct bnx2x_fastpath *fp,
					     u16 idx)
{
	u16 last_max = fp->last_max_sge;

	if (SUB_S16(idx, last_max) > 0)
		fp->last_max_sge = idx;
}

static void bnx2x_update_sge_prod(struct bnx2x_fastpath *fp,
				  struct eth_fast_path_rx_cqe *fp_cqe)
{
	struct bnx2x *bp = fp->bp;
	u16 sge_len = SGE_PAGE_ALIGN(le16_to_cpu(fp_cqe->pkt_len) -
				     le16_to_cpu(fp_cqe->len_on_bd)) >>
		      SGE_PAGE_SHIFT;
	u16 last_max, last_elem, first_elem;
	u16 delta = 0;
	u16 i;

	if (!sge_len)
		return;

	/* First mark all used pages */
	for (i = 0; i < sge_len; i++)
		SGE_MASK_CLEAR_BIT(fp,
			RX_SGE(le16_to_cpu(fp_cqe->sgl_or_raw_data.sgl[i])));

	DP(NETIF_MSG_RX_STATUS, "fp_cqe->sgl[%d] = %d\n",
	   sge_len - 1, le16_to_cpu(fp_cqe->sgl_or_raw_data.sgl[sge_len - 1]));

	/* Here we assume that the last SGE index is the biggest */
	prefetch((void *)(fp->sge_mask));
	bnx2x_update_last_max_sge(fp,
		le16_to_cpu(fp_cqe->sgl_or_raw_data.sgl[sge_len - 1]));

	last_max = RX_SGE(fp->last_max_sge);
	last_elem = last_max >> RX_SGE_MASK_ELEM_SHIFT;
	first_elem = RX_SGE(fp->rx_sge_prod) >> RX_SGE_MASK_ELEM_SHIFT;

	/* If ring is not full */
	if (last_elem + 1 != first_elem)
		last_elem++;

	/* Now update the prod */
	for (i = first_elem; i != last_elem; i = NEXT_SGE_MASK_ELEM(i)) {
		if (likely(fp->sge_mask[i]))
			break;

		fp->sge_mask[i] = RX_SGE_MASK_ELEM_ONE_MASK;
		delta += RX_SGE_MASK_ELEM_SZ;
	}

	if (delta > 0) {
		fp->rx_sge_prod += delta;
		/* clear page-end entries */
		bnx2x_clear_sge_mask_next_elems(fp);
	}

	DP(NETIF_MSG_RX_STATUS,
	   "fp->last_max_sge = %d  fp->rx_sge_prod = %d\n",
	   fp->last_max_sge, fp->rx_sge_prod);
}

static void bnx2x_tpa_start(struct bnx2x_fastpath *fp, u16 queue,
			    struct sk_buff *skb, u16 cons, u16 prod)
{
	struct bnx2x *bp = fp->bp;
	struct sw_rx_bd *cons_rx_buf = &fp->rx_buf_ring[cons];
	struct sw_rx_bd *prod_rx_buf = &fp->rx_buf_ring[prod];
	struct eth_rx_bd *prod_bd = &fp->rx_desc_ring[prod];
	dma_addr_t mapping;

	/* move empty skb from pool to prod and map it */
	prod_rx_buf->skb = fp->tpa_pool[queue].skb;
	mapping = dma_map_single(&bp->pdev->dev, fp->tpa_pool[queue].skb->data,
				 fp->rx_buf_size, DMA_FROM_DEVICE);
	dma_unmap_addr_set(prod_rx_buf, mapping, mapping);

	/* move partial skb from cons to pool (don't unmap yet) */
	fp->tpa_pool[queue] = *cons_rx_buf;

	/* mark bin state as start - print error if current state != stop */
	if (fp->tpa_state[queue] != BNX2X_TPA_STOP)
		BNX2X_ERR("start of bin not in stop [%d]\n", queue);

	fp->tpa_state[queue] = BNX2X_TPA_START;

	/* point prod_bd to new skb */
	prod_bd->addr_hi = cpu_to_le32(U64_HI(mapping));
	prod_bd->addr_lo = cpu_to_le32(U64_LO(mapping));

#ifdef BNX2X_STOP_ON_ERROR
	fp->tpa_queue_used |= (1 << queue);
#ifdef _ASM_GENERIC_INT_L64_H
	DP(NETIF_MSG_RX_STATUS, "fp->tpa_queue_used = 0x%lx\n",
#else
	DP(NETIF_MSG_RX_STATUS, "fp->tpa_queue_used = 0x%llx\n",
#endif
	   fp->tpa_queue_used);
#endif
}

/* Timestamp option length allowed for TPA aggregation:
 *
 *		nop nop kind length echo val
 */
#define TPA_TSTAMP_OPT_LEN	12
/**
 * Calculate the approximate value of the MSS for this
 * aggregation using the first packet of it.
 *
 * @param bp
 * @param parsing_flags Parsing flags from the START CQE
 * @param len_on_bd Total length of the first packet for the
 *		     aggregation.
 */
static inline u16 bnx2x_set_lro_mss(struct bnx2x *bp, u16 parsing_flags,
				    u16 len_on_bd)
{
	/* TPA arrgregation won't have an IP options and TCP options
	 * other than timestamp.
	 */
	u16 hdrs_len = ETH_HLEN + sizeof(struct iphdr) + sizeof(struct tcphdr);


	/* Check if there was a TCP timestamp, if there is it's will
	 * always be 12 bytes length: nop nop kind length echo val.
	 *
	 * Otherwise FW would close the aggregation.
	 */
	if (parsing_flags & PARSING_FLAGS_TIME_STAMP_EXIST_FLAG)
		hdrs_len += TPA_TSTAMP_OPT_LEN;

	return len_on_bd - hdrs_len;
}

static int bnx2x_fill_frag_skb(struct bnx2x *bp, struct bnx2x_fastpath *fp,
			       struct sk_buff *skb,
			       struct eth_fast_path_rx_cqe *fp_cqe,
			       u16 cqe_idx, u16 parsing_flags)
{
	struct sw_rx_page *rx_pg, old_rx_pg;
	u16 len_on_bd = le16_to_cpu(fp_cqe->len_on_bd);
	u32 i, frag_len, frag_size, pages;
	int err;
	int j;

	frag_size = le16_to_cpu(fp_cqe->pkt_len) - len_on_bd;
	pages = SGE_PAGE_ALIGN(frag_size) >> SGE_PAGE_SHIFT;

	/* This is needed in order to enable forwarding support */
	if (frag_size)
		skb_shinfo(skb)->gso_size = bnx2x_set_lro_mss(bp, parsing_flags,
							      len_on_bd);

#ifdef BNX2X_STOP_ON_ERROR
	if (pages > min_t(u32, 8, MAX_SKB_FRAGS)*SGE_PAGE_SIZE*PAGES_PER_SGE) {
		BNX2X_ERR("SGL length is too long: %d. CQE index is %d\n",
			  pages, cqe_idx);
		BNX2X_ERR("fp_cqe->pkt_len = %d  fp_cqe->len_on_bd = %d\n",
			  fp_cqe->pkt_len, len_on_bd);
		bnx2x_panic();
		return -EINVAL;
	}
#endif

	/* Run through the SGL and compose the fragmented skb */
	for (i = 0, j = 0; i < pages; i += PAGES_PER_SGE, j++) {
		u16 sge_idx =
			RX_SGE(le16_to_cpu(fp_cqe->sgl_or_raw_data.sgl[j]));

		/* FW gives the indices of the SGE as if the ring is an array
		   (meaning that "next" element will consume 2 indices) */
		frag_len = min(frag_size, (u32)(SGE_PAGE_SIZE*PAGES_PER_SGE));
		rx_pg = &fp->rx_page_ring[sge_idx];
		old_rx_pg = *rx_pg;

		/* If we fail to allocate a substitute page, we simply stop
		   where we are and drop the whole packet */
		err = bnx2x_alloc_rx_sge(bp, fp, sge_idx);
		if (unlikely(err)) {
			fp->eth_q_stats.rx_skb_alloc_failed++;
			return err;
		}

		/* Unmap the page as we r going to pass it to the stack */
		dma_unmap_page(&bp->pdev->dev,
			       dma_unmap_addr(&old_rx_pg, mapping),
			       SGE_PAGE_SIZE*PAGES_PER_SGE, DMA_FROM_DEVICE);

		/* Add one frag and update the appropriate fields in the skb */
		skb_fill_page_desc(skb, j, old_rx_pg.page, 0, frag_len);

		skb->data_len += frag_len;
		skb->truesize += frag_len;
		skb->len += frag_len;

		frag_size -= frag_len;
	}

	return 0;
}

static void bnx2x_tpa_stop(struct bnx2x *bp, struct bnx2x_fastpath *fp,
			   u16 queue, int pad, int len, union eth_rx_cqe *cqe,
			   u16 cqe_idx)
{
	struct sw_rx_bd *rx_buf = &fp->tpa_pool[queue];
	struct sk_buff *skb = rx_buf->skb;
	/* alloc new skb */
	struct sk_buff *new_skb = netdev_alloc_skb(bp->dev, fp->rx_buf_size);

	/* Unmap skb in the pool anyway, as we are going to change
	   pool entry status to BNX2X_TPA_STOP even if new skb allocation
	   fails. */
	dma_unmap_single(&bp->pdev->dev, dma_unmap_addr(rx_buf, mapping),
			 fp->rx_buf_size, DMA_FROM_DEVICE);

	if (likely(new_skb)) {
		/* fix ip xsum and give it to the stack */
		/* (no need to map the new skb) */
		u16 parsing_flags =
			le16_to_cpu(cqe->fast_path_cqe.pars_flags.flags);

		prefetch(skb);
		prefetch(((char *)(skb)) + L1_CACHE_BYTES);

#ifdef BNX2X_STOP_ON_ERROR
		if (pad + len > fp->rx_buf_size) {
			BNX2X_ERR("skb_put is about to fail...  "
				  "pad %d  len %d  rx_buf_size %d\n",
				  pad, len, fp->rx_buf_size);
			bnx2x_panic();
			return;
		}
#endif

		skb_reserve(skb, pad);
		skb_put(skb, len);

		skb->protocol = eth_type_trans(skb, bp->dev);
		skb->ip_summed = CHECKSUM_UNNECESSARY;

		{
			struct iphdr *iph;

			iph = (struct iphdr *)skb->data;
			iph->check = 0;
			iph->check = ip_fast_csum((u8 *)iph, iph->ihl);
		}

		if (!bnx2x_fill_frag_skb(bp, fp, skb,
					 &cqe->fast_path_cqe, cqe_idx,
					 parsing_flags)) {
			if (parsing_flags & PARSING_FLAGS_VLAN)
				__vlan_hwaccel_put_tag(skb,
						 le16_to_cpu(cqe->fast_path_cqe.
							     vlan_tag));
			napi_gro_receive(&fp->napi, skb);
		} else {
			DP(NETIF_MSG_RX_STATUS, "Failed to allocate new pages"
			   " - dropping packet!\n");
			dev_kfree_skb(skb);
		}


		/* put new skb in bin */
		fp->tpa_pool[queue].skb = new_skb;

	} else {
		/* else drop the packet and keep the buffer in the bin */
		DP(NETIF_MSG_RX_STATUS,
		   "Failed to allocate new skb - dropping packet!\n");
		fp->eth_q_stats.rx_skb_alloc_failed++;
	}

	fp->tpa_state[queue] = BNX2X_TPA_STOP;
}

/* Set Toeplitz hash value in the skb using the value from the
 * CQE (calculated by HW).
 */
static inline void bnx2x_set_skb_rxhash(struct bnx2x *bp, union eth_rx_cqe *cqe,
					struct sk_buff *skb)
{
	/* Set Toeplitz hash from CQE */
	if ((bp->dev->features & NETIF_F_RXHASH) &&
	    (cqe->fast_path_cqe.status_flags &
	     ETH_FAST_PATH_RX_CQE_RSS_HASH_FLG))
		skb->rxhash =
		le32_to_cpu(cqe->fast_path_cqe.rss_hash_result);
}

int bnx2x_rx_int(struct bnx2x_fastpath *fp, int budget)
{
	struct bnx2x *bp = fp->bp;
	u16 bd_cons, bd_prod, bd_prod_fw, comp_ring_cons;
	u16 hw_comp_cons, sw_comp_cons, sw_comp_prod;
	int rx_pkt = 0;

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return 0;
#endif

	/* CQ "next element" is of the size of the regular element,
	   that's why it's ok here */
	hw_comp_cons = le16_to_cpu(*fp->rx_cons_sb);
	if ((hw_comp_cons & MAX_RCQ_DESC_CNT) == MAX_RCQ_DESC_CNT)
		hw_comp_cons++;

	bd_cons = fp->rx_bd_cons;
	bd_prod = fp->rx_bd_prod;
	bd_prod_fw = bd_prod;
	sw_comp_cons = fp->rx_comp_cons;
	sw_comp_prod = fp->rx_comp_prod;

	/* Memory barrier necessary as speculative reads of the rx
	 * buffer can be ahead of the index in the status block
	 */
	rmb();

	DP(NETIF_MSG_RX_STATUS,
	   "queue[%d]:  hw_comp_cons %u  sw_comp_cons %u\n",
	   fp->index, hw_comp_cons, sw_comp_cons);

	while (sw_comp_cons != hw_comp_cons) {
		struct sw_rx_bd *rx_buf = NULL;
		struct sk_buff *skb;
		union eth_rx_cqe *cqe;
		u8 cqe_fp_flags;
		u16 len, pad;

		comp_ring_cons = RCQ_BD(sw_comp_cons);
		bd_prod = RX_BD(bd_prod);
		bd_cons = RX_BD(bd_cons);

		/* Prefetch the page containing the BD descriptor
		   at producer's index. It will be needed when new skb is
		   allocated */
		prefetch((void *)(PAGE_ALIGN((unsigned long)
					     (&fp->rx_desc_ring[bd_prod])) -
				  PAGE_SIZE + 1));

		cqe = &fp->rx_comp_ring[comp_ring_cons];
		cqe_fp_flags = cqe->fast_path_cqe.type_error_flags;

		DP(NETIF_MSG_RX_STATUS, "CQE type %x  err %x  status %x"
		   "  queue %x  vlan %x  len %u\n", CQE_TYPE(cqe_fp_flags),
		   cqe_fp_flags, cqe->fast_path_cqe.status_flags,
		   le32_to_cpu(cqe->fast_path_cqe.rss_hash_result),
		   le16_to_cpu(cqe->fast_path_cqe.vlan_tag),
		   le16_to_cpu(cqe->fast_path_cqe.pkt_len));

		/* is this a slowpath msg? */
		if (unlikely(CQE_TYPE(cqe_fp_flags))) {
			bnx2x_sp_event(fp, cqe);
			goto next_cqe;

		/* this is an rx packet */
		} else {
			rx_buf = &fp->rx_buf_ring[bd_cons];
			skb = rx_buf->skb;
			prefetch(skb);
			len = le16_to_cpu(cqe->fast_path_cqe.pkt_len);
			pad = cqe->fast_path_cqe.placement_offset;

			/* - If CQE is marked both TPA_START and TPA_END it is
			 *   a non-TPA CQE.
			 * - FP CQE will always have either TPA_START or/and
			 *   TPA_STOP flags set.
			 */
			if ((!fp->disable_tpa) &&
			    (TPA_TYPE(cqe_fp_flags) !=
					(TPA_TYPE_START | TPA_TYPE_END))) {
				u16 queue = cqe->fast_path_cqe.queue_index;

				if (TPA_TYPE(cqe_fp_flags) == TPA_TYPE_START) {
					DP(NETIF_MSG_RX_STATUS,
					   "calling tpa_start on queue %d\n",
					   queue);

					bnx2x_tpa_start(fp, queue, skb,
							bd_cons, bd_prod);

					/* Set Toeplitz hash for an LRO skb */
					bnx2x_set_skb_rxhash(bp, cqe, skb);

					goto next_rx;
				} else { /* TPA_STOP */
					DP(NETIF_MSG_RX_STATUS,
					   "calling tpa_stop on queue %d\n",
					   queue);

					if (!BNX2X_RX_SUM_FIX(cqe))
						BNX2X_ERR("STOP on none TCP "
							  "data\n");

					/* This is a size of the linear data
					   on this skb */
					len = le16_to_cpu(cqe->fast_path_cqe.
								len_on_bd);
					bnx2x_tpa_stop(bp, fp, queue, pad,
						    len, cqe, comp_ring_cons);
#ifdef BNX2X_STOP_ON_ERROR
					if (bp->panic)
						return 0;
#endif

					bnx2x_update_sge_prod(fp,
							&cqe->fast_path_cqe);
					goto next_cqe;
				}
			}

			dma_sync_single_for_device(&bp->pdev->dev,
					dma_unmap_addr(rx_buf, mapping),
						   pad + RX_COPY_THRESH,
						   DMA_FROM_DEVICE);
			prefetch(((char *)(skb)) + L1_CACHE_BYTES);

			/* is this an error packet? */
			if (unlikely(cqe_fp_flags & ETH_RX_ERROR_FALGS)) {
				DP(NETIF_MSG_RX_ERR,
				   "ERROR  flags %x  rx packet %u\n",
				   cqe_fp_flags, sw_comp_cons);
				fp->eth_q_stats.rx_err_discard_pkt++;
				goto reuse_rx;
			}

			/* Since we don't have a jumbo ring
			 * copy small packets if mtu > 1500
			 */
			if ((bp->dev->mtu > ETH_MAX_PACKET_SIZE) &&
			    (len <= RX_COPY_THRESH)) {
				struct sk_buff *new_skb;

				new_skb = netdev_alloc_skb(bp->dev,
							   len + pad);
				if (new_skb == NULL) {
					DP(NETIF_MSG_RX_ERR,
					   "ERROR  packet dropped "
					   "because of alloc failure\n");
					fp->eth_q_stats.rx_skb_alloc_failed++;
					goto reuse_rx;
				}

				/* aligned copy */
				skb_copy_from_linear_data_offset(skb, pad,
						    new_skb->data + pad, len);
				skb_reserve(new_skb, pad);
				skb_put(new_skb, len);

				bnx2x_reuse_rx_skb(fp, bd_cons, bd_prod);

				skb = new_skb;

			} else
			if (likely(bnx2x_alloc_rx_skb(bp, fp, bd_prod) == 0)) {
				dma_unmap_single(&bp->pdev->dev,
					dma_unmap_addr(rx_buf, mapping),
						 fp->rx_buf_size,
						 DMA_FROM_DEVICE);
				skb_reserve(skb, pad);
				skb_put(skb, len);

			} else {
				DP(NETIF_MSG_RX_ERR,
				   "ERROR  packet dropped because "
				   "of alloc failure\n");
				fp->eth_q_stats.rx_skb_alloc_failed++;
reuse_rx:
				bnx2x_reuse_rx_skb(fp, bd_cons, bd_prod);
				goto next_rx;
			}

			skb->protocol = eth_type_trans(skb, bp->dev);

			/* Set Toeplitz hash for a none-LRO skb */
			bnx2x_set_skb_rxhash(bp, cqe, skb);

			skb_checksum_none_assert(skb);

			if (bp->rx_csum) {
				if (likely(BNX2X_RX_CSUM_OK(cqe)))
					skb->ip_summed = CHECKSUM_UNNECESSARY;
				else
					fp->eth_q_stats.hw_csum_err++;
			}
		}

		skb_record_rx_queue(skb, fp->index);

		if (le16_to_cpu(cqe->fast_path_cqe.pars_flags.flags) &
		     PARSING_FLAGS_VLAN)
			__vlan_hwaccel_put_tag(skb,
				le16_to_cpu(cqe->fast_path_cqe.vlan_tag));
		napi_gro_receive(&fp->napi, skb);


next_rx:
		rx_buf->skb = NULL;

		bd_cons = NEXT_RX_IDX(bd_cons);
		bd_prod = NEXT_RX_IDX(bd_prod);
		bd_prod_fw = NEXT_RX_IDX(bd_prod_fw);
		rx_pkt++;
next_cqe:
		sw_comp_prod = NEXT_RCQ_IDX(sw_comp_prod);
		sw_comp_cons = NEXT_RCQ_IDX(sw_comp_cons);

		if (rx_pkt == budget)
			break;
	} /* while */

	fp->rx_bd_cons = bd_cons;
	fp->rx_bd_prod = bd_prod_fw;
	fp->rx_comp_cons = sw_comp_cons;
	fp->rx_comp_prod = sw_comp_prod;

	/* Update producers */
	bnx2x_update_rx_prod(bp, fp, bd_prod_fw, sw_comp_prod,
			     fp->rx_sge_prod);

	fp->rx_pkt += rx_pkt;
	fp->rx_calls++;

	return rx_pkt;
}

static irqreturn_t bnx2x_msix_fp_int(int irq, void *fp_cookie)
{
	struct bnx2x_fastpath *fp = fp_cookie;
	struct bnx2x *bp = fp->bp;

	/* Return here if interrupt is disabled */
	if (unlikely(atomic_read(&bp->intr_sem) != 0)) {
		DP(NETIF_MSG_INTR, "called but intr_sem not 0, returning\n");
		return IRQ_HANDLED;
	}

	DP(BNX2X_MSG_FP, "got an MSI-X interrupt on IDX:SB "
			 "[fp %d fw_sd %d igusb %d]\n",
	   fp->index, fp->fw_sb_id, fp->igu_sb_id);
	bnx2x_ack_sb(bp, fp->igu_sb_id, USTORM_ID, 0, IGU_INT_DISABLE, 0);

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return IRQ_HANDLED;
#endif

	/* Handle Rx and Tx according to MSI-X vector */
	prefetch(fp->rx_cons_sb);
	prefetch(fp->tx_cons_sb);
	prefetch(&fp->sb_running_index[SM_RX_ID]);
	napi_schedule(&bnx2x_fp(bp, fp->index, napi));

	return IRQ_HANDLED;
}

/* HW Lock for shared dual port PHYs */
void bnx2x_acquire_phy_lock(struct bnx2x *bp)
{
	mutex_lock(&bp->port.phy_mutex);

	if (bp->port.need_hw_lock)
		bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_MDIO);
}

void bnx2x_release_phy_lock(struct bnx2x *bp)
{
	if (bp->port.need_hw_lock)
		bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_MDIO);

	mutex_unlock(&bp->port.phy_mutex);
}

/* calculates MF speed according to current linespeed and MF configuration */
u16 bnx2x_get_mf_speed(struct bnx2x *bp)
{
	u16 line_speed = bp->link_vars.line_speed;
	if (IS_MF(bp)) {
		u16 maxCfg = bnx2x_extract_max_cfg(bp,
						   bp->mf_config[BP_VN(bp)]);

		/* Calculate the current MAX line speed limit for the MF
		 * devices
		 */
		if (IS_MF_SI(bp))
			line_speed = (line_speed * maxCfg) / 100;
		else { /* SD mode */
			u16 vn_max_rate = maxCfg * 100;

			if (vn_max_rate < line_speed)
				line_speed = vn_max_rate;
		}
	}

	return line_speed;
}

void bnx2x_link_report(struct bnx2x *bp)
{
	if (bp->flags & MF_FUNC_DIS) {
		netif_carrier_off(bp->dev);
		netdev_err(bp->dev, "NIC Link is Down\n");
		return;
	}

	if (bp->link_vars.link_up) {
		u16 line_speed;

		if (bp->state == BNX2X_STATE_OPEN)
			netif_carrier_on(bp->dev);
		netdev_info(bp->dev, "NIC Link is Up, ");

		line_speed = bnx2x_get_mf_speed(bp);

		pr_cont("%d Mbps ", line_speed);

		if (bp->link_vars.duplex == DUPLEX_FULL)
			pr_cont("full duplex");
		else
			pr_cont("half duplex");

		if (bp->link_vars.flow_ctrl != BNX2X_FLOW_CTRL_NONE) {
			if (bp->link_vars.flow_ctrl & BNX2X_FLOW_CTRL_RX) {
				pr_cont(", receive ");
				if (bp->link_vars.flow_ctrl &
				    BNX2X_FLOW_CTRL_TX)
					pr_cont("& transmit ");
			} else {
				pr_cont(", transmit ");
			}
			pr_cont("flow control ON");
		}
		pr_cont("\n");

	} else { /* link_down */
		netif_carrier_off(bp->dev);
		netdev_err(bp->dev, "NIC Link is Down\n");
	}
}

/* Returns the number of actually allocated BDs */
static inline int bnx2x_alloc_rx_bds(struct bnx2x_fastpath *fp,
				      int rx_ring_size)
{
	struct bnx2x *bp = fp->bp;
	u16 ring_prod, cqe_ring_prod;
	int i;

	fp->rx_comp_cons = 0;
	cqe_ring_prod = ring_prod = 0;
	for (i = 0; i < rx_ring_size; i++) {
		if (bnx2x_alloc_rx_skb(bp, fp, ring_prod) < 0) {
			BNX2X_ERR("was only able to allocate "
				  "%d rx skbs on queue[%d]\n", i, fp->index);
			fp->eth_q_stats.rx_skb_alloc_failed++;
			break;
		}
		ring_prod = NEXT_RX_IDX(ring_prod);
		cqe_ring_prod = NEXT_RCQ_IDX(cqe_ring_prod);
		WARN_ON(ring_prod <= i);
	}

	fp->rx_bd_prod = ring_prod;
	/* Limit the CQE producer by the CQE ring size */
	fp->rx_comp_prod = min_t(u16, NUM_RCQ_RINGS*RCQ_DESC_CNT,
			       cqe_ring_prod);
	fp->rx_pkt = fp->rx_calls = 0;

	return i;
}

static inline void bnx2x_alloc_rx_bd_ring(struct bnx2x_fastpath *fp)
{
	struct bnx2x *bp = fp->bp;
	int rx_ring_size = bp->rx_ring_size ? bp->rx_ring_size :
					      MAX_RX_AVAIL/bp->num_queues;

	rx_ring_size = max_t(int, MIN_RX_AVAIL, rx_ring_size);

	bnx2x_alloc_rx_bds(fp, rx_ring_size);

	/* Warning!
	 * this will generate an interrupt (to the TSTORM)
	 * must only be done after chip is initialized
	 */
	bnx2x_update_rx_prod(bp, fp, fp->rx_bd_prod, fp->rx_comp_prod,
			     fp->rx_sge_prod);
}

void bnx2x_init_rx_rings(struct bnx2x *bp)
{
	int func = BP_FUNC(bp);
	int max_agg_queues = CHIP_IS_E1(bp) ? ETH_MAX_AGGREGATION_QUEUES_E1 :
					      ETH_MAX_AGGREGATION_QUEUES_E1H;
	u16 ring_prod;
	int i, j;

	for_each_rx_queue(bp, j) {
		struct bnx2x_fastpath *fp = &bp->fp[j];

		DP(NETIF_MSG_IFUP,
		   "mtu %d  rx_buf_size %d\n", bp->dev->mtu, fp->rx_buf_size);

		if (!fp->disable_tpa) {
			for (i = 0; i < max_agg_queues; i++) {
				fp->tpa_pool[i].skb =
				   netdev_alloc_skb(bp->dev, fp->rx_buf_size);
				if (!fp->tpa_pool[i].skb) {
					BNX2X_ERR("Failed to allocate TPA "
						  "skb pool for queue[%d] - "
						  "disabling TPA on this "
						  "queue!\n", j);
					bnx2x_free_tpa_pool(bp, fp, i);
					fp->disable_tpa = 1;
					break;
				}
				dma_unmap_addr_set((struct sw_rx_bd *)
							&bp->fp->tpa_pool[i],
						   mapping, 0);
				fp->tpa_state[i] = BNX2X_TPA_STOP;
			}

			/* "next page" elements initialization */
			bnx2x_set_next_page_sgl(fp);

			/* set SGEs bit mask */
			bnx2x_init_sge_ring_bit_mask(fp);

			/* Allocate SGEs and initialize the ring elements */
			for (i = 0, ring_prod = 0;
			     i < MAX_RX_SGE_CNT*NUM_RX_SGE_PAGES; i++) {

				if (bnx2x_alloc_rx_sge(bp, fp, ring_prod) < 0) {
					BNX2X_ERR("was only able to allocate "
						  "%d rx sges\n", i);
					BNX2X_ERR("disabling TPA for"
						  " queue[%d]\n", j);
					/* Cleanup already allocated elements */
					bnx2x_free_rx_sge_range(bp,
								fp, ring_prod);
					bnx2x_free_tpa_pool(bp,
							    fp, max_agg_queues);
					fp->disable_tpa = 1;
					ring_prod = 0;
					break;
				}
				ring_prod = NEXT_SGE_IDX(ring_prod);
			}

			fp->rx_sge_prod = ring_prod;
		}
	}

	for_each_rx_queue(bp, j) {
		struct bnx2x_fastpath *fp = &bp->fp[j];

		fp->rx_bd_cons = 0;

		bnx2x_set_next_page_rx_bd(fp);

		/* CQ ring */
		bnx2x_set_next_page_rx_cq(fp);

		/* Allocate BDs and initialize BD ring */
		bnx2x_alloc_rx_bd_ring(fp);

		if (j != 0)
			continue;

		if (!CHIP_IS_E2(bp)) {
			REG_WR(bp, BAR_USTRORM_INTMEM +
			       USTORM_MEM_WORKAROUND_ADDRESS_OFFSET(func),
			       U64_LO(fp->rx_comp_mapping));
			REG_WR(bp, BAR_USTRORM_INTMEM +
			       USTORM_MEM_WORKAROUND_ADDRESS_OFFSET(func) + 4,
			       U64_HI(fp->rx_comp_mapping));
		}
	}
}

static void bnx2x_free_tx_skbs(struct bnx2x *bp)
{
	int i;

	for_each_tx_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		u16 bd_cons = fp->tx_bd_cons;
		u16 sw_prod = fp->tx_pkt_prod;
		u16 sw_cons = fp->tx_pkt_cons;

		while (sw_cons != sw_prod) {
			bd_cons = bnx2x_free_tx_pkt(bp, fp, TX_BD(sw_cons));
			sw_cons++;
		}
	}
}

static void bnx2x_free_rx_skbs(struct bnx2x *bp)
{
	int i, j;

	for_each_rx_queue(bp, j) {
		struct bnx2x_fastpath *fp = &bp->fp[j];

		for (i = 0; i < NUM_RX_BD; i++) {
			struct sw_rx_bd *rx_buf = &fp->rx_buf_ring[i];
			struct sk_buff *skb = rx_buf->skb;

			if (skb == NULL)
				continue;

			dma_unmap_single(&bp->pdev->dev,
					 dma_unmap_addr(rx_buf, mapping),
					 fp->rx_buf_size, DMA_FROM_DEVICE);

			rx_buf->skb = NULL;
			dev_kfree_skb(skb);
		}
		if (!fp->disable_tpa)
			bnx2x_free_tpa_pool(bp, fp, CHIP_IS_E1(bp) ?
					    ETH_MAX_AGGREGATION_QUEUES_E1 :
					    ETH_MAX_AGGREGATION_QUEUES_E1H);
	}
}

void bnx2x_free_skbs(struct bnx2x *bp)
{
	bnx2x_free_tx_skbs(bp);
	bnx2x_free_rx_skbs(bp);
}

void bnx2x_update_max_mf_config(struct bnx2x *bp, u32 value)
{
	/* load old values */
	u32 mf_cfg = bp->mf_config[BP_VN(bp)];

	if (value != bnx2x_extract_max_cfg(bp, mf_cfg)) {
		/* leave all but MAX value */
		mf_cfg &= ~FUNC_MF_CFG_MAX_BW_MASK;

		/* set new MAX value */
		mf_cfg |= (value << FUNC_MF_CFG_MAX_BW_SHIFT)
				& FUNC_MF_CFG_MAX_BW_MASK;

		bnx2x_fw_command(bp, DRV_MSG_CODE_SET_MF_BW, mf_cfg);
	}
}

static void bnx2x_free_msix_irqs(struct bnx2x *bp)
{
	int i, offset = 1;

	free_irq(bp->msix_table[0].vector, bp->dev);
	DP(NETIF_MSG_IFDOWN, "released sp irq (%d)\n",
	   bp->msix_table[0].vector);

#ifdef BCM_CNIC
	offset++;
#endif
	for_each_eth_queue(bp, i) {
		DP(NETIF_MSG_IFDOWN, "about to release fp #%d->%d irq  "
		   "state %x\n", i, bp->msix_table[i + offset].vector,
		   bnx2x_fp(bp, i, state));

		free_irq(bp->msix_table[i + offset].vector, &bp->fp[i]);
	}
}

void bnx2x_free_irq(struct bnx2x *bp)
{
	if (bp->flags & USING_MSIX_FLAG)
		bnx2x_free_msix_irqs(bp);
	else if (bp->flags & USING_MSI_FLAG)
		free_irq(bp->pdev->irq, bp->dev);
	else
		free_irq(bp->pdev->irq, bp->dev);
}

int bnx2x_enable_msix(struct bnx2x *bp)
{
	int msix_vec = 0, i, rc, req_cnt;

	bp->msix_table[msix_vec].entry = msix_vec;
	DP(NETIF_MSG_IFUP, "msix_table[0].entry = %d (slowpath)\n",
	   bp->msix_table[0].entry);
	msix_vec++;

#ifdef BCM_CNIC
	bp->msix_table[msix_vec].entry = msix_vec;
	DP(NETIF_MSG_IFUP, "msix_table[%d].entry = %d (CNIC)\n",
	   bp->msix_table[msix_vec].entry, bp->msix_table[msix_vec].entry);
	msix_vec++;
#endif
	for_each_eth_queue(bp, i) {
		bp->msix_table[msix_vec].entry = msix_vec;
		DP(NETIF_MSG_IFUP, "msix_table[%d].entry = %d "
		   "(fastpath #%u)\n", msix_vec, msix_vec, i);
		msix_vec++;
	}

	req_cnt = BNX2X_NUM_ETH_QUEUES(bp) + CNIC_CONTEXT_USE + 1;

	rc = pci_enable_msix(bp->pdev, &bp->msix_table[0], req_cnt);

	/*
	 * reconfigure number of tx/rx queues according to available
	 * MSI-X vectors
	 */
	if (rc >= BNX2X_MIN_MSIX_VEC_CNT) {
		/* how less vectors we will have? */
		int diff = req_cnt - rc;

		DP(NETIF_MSG_IFUP,
		   "Trying to use less MSI-X vectors: %d\n", rc);

		rc = pci_enable_msix(bp->pdev, &bp->msix_table[0], rc);

		if (rc) {
			DP(NETIF_MSG_IFUP,
			   "MSI-X is not attainable  rc %d\n", rc);
			return rc;
		}
		/*
		 * decrease number of queues by number of unallocated entries
		 */
		bp->num_queues -= diff;

		DP(NETIF_MSG_IFUP, "New queue configuration set: %d\n",
				  bp->num_queues);
	} else if (rc) {
		/* fall to INTx if not enough memory */
		if (rc == -ENOMEM)
			bp->flags |= DISABLE_MSI_FLAG;
		DP(NETIF_MSG_IFUP, "MSI-X is not attainable  rc %d\n", rc);
		return rc;
	}

	bp->flags |= USING_MSIX_FLAG;

	return 0;
}

static int bnx2x_req_msix_irqs(struct bnx2x *bp)
{
	int i, rc, offset = 1;

	rc = request_irq(bp->msix_table[0].vector, bnx2x_msix_sp_int, 0,
			 bp->dev->name, bp->dev);
	if (rc) {
		BNX2X_ERR("request sp irq failed\n");
		return -EBUSY;
	}

#ifdef BCM_CNIC
	offset++;
#endif
	for_each_eth_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];
		snprintf(fp->name, sizeof(fp->name), "%s-fp-%d",
			 bp->dev->name, i);

		rc = request_irq(bp->msix_table[offset].vector,
				 bnx2x_msix_fp_int, 0, fp->name, fp);
		if (rc) {
			BNX2X_ERR("request fp #%d irq failed  rc %d\n", i, rc);
			bnx2x_free_msix_irqs(bp);
			return -EBUSY;
		}

		offset++;
		fp->state = BNX2X_FP_STATE_IRQ;
	}

	i = BNX2X_NUM_ETH_QUEUES(bp);
	offset = 1 + CNIC_CONTEXT_USE;
	netdev_info(bp->dev, "using MSI-X  IRQs: sp %d  fp[%d] %d"
	       " ... fp[%d] %d\n",
	       bp->msix_table[0].vector,
	       0, bp->msix_table[offset].vector,
	       i - 1, bp->msix_table[offset + i - 1].vector);

	return 0;
}

int bnx2x_enable_msi(struct bnx2x *bp)
{
	int rc;

	rc = pci_enable_msi(bp->pdev);
	if (rc) {
		DP(NETIF_MSG_IFUP, "MSI is not attainable\n");
		return -1;
	}
	bp->flags |= USING_MSI_FLAG;

	return 0;
}

static int bnx2x_req_irq(struct bnx2x *bp)
{
	unsigned long flags;
	int rc;

	if (bp->flags & USING_MSI_FLAG)
		flags = 0;
	else
		flags = IRQF_SHARED;

	rc = request_irq(bp->pdev->irq, bnx2x_interrupt, flags,
			 bp->dev->name, bp->dev);
	if (!rc)
		bnx2x_fp(bp, 0, state) = BNX2X_FP_STATE_IRQ;

	return rc;
}

static void bnx2x_napi_enable(struct bnx2x *bp)
{
	int i;

	for_each_napi_queue(bp, i)
		napi_enable(&bnx2x_fp(bp, i, napi));
}

static void bnx2x_napi_disable(struct bnx2x *bp)
{
	int i;

	for_each_napi_queue(bp, i)
		napi_disable(&bnx2x_fp(bp, i, napi));
}

void bnx2x_netif_start(struct bnx2x *bp)
{
	int intr_sem;

	intr_sem = atomic_dec_and_test(&bp->intr_sem);
	smp_wmb(); /* Ensure that bp->intr_sem update is SMP-safe */

	if (intr_sem) {
		if (netif_running(bp->dev)) {
			bnx2x_napi_enable(bp);
			bnx2x_int_enable(bp);
			if (bp->state == BNX2X_STATE_OPEN)
				netif_tx_wake_all_queues(bp->dev);
		}
	}
}

void bnx2x_netif_stop(struct bnx2x *bp, int disable_hw)
{
	bnx2x_int_disable_sync(bp, disable_hw);
	bnx2x_napi_disable(bp);
	netif_tx_disable(bp->dev);
}

u16 bnx2x_select_queue(struct net_device *dev, struct sk_buff *skb)
{
#ifdef BCM_CNIC
	struct bnx2x *bp = netdev_priv(dev);
	if (NO_FCOE(bp))
		return skb_tx_hash(dev, skb);
	else {
		struct ethhdr *hdr = (struct ethhdr *)skb->data;
		u16 ether_type = ntohs(hdr->h_proto);

		/* Skip VLAN tag if present */
		if (ether_type == ETH_P_8021Q) {
			struct vlan_ethhdr *vhdr =
				(struct vlan_ethhdr *)skb->data;

			ether_type = ntohs(vhdr->h_vlan_encapsulated_proto);
		}

		/* If ethertype is FCoE or FIP - use FCoE ring */
		if ((ether_type == ETH_P_FCOE) || (ether_type == ETH_P_FIP))
			return bnx2x_fcoe(bp, index);
	}
#endif
	/* Select a none-FCoE queue:  if FCoE is enabled, exclude FCoE L2 ring
	 */
	return __skb_tx_hash(dev, skb,
			dev->real_num_tx_queues - FCOE_CONTEXT_USE);
}

void bnx2x_set_num_queues(struct bnx2x *bp)
{
	switch (bp->multi_mode) {
	case ETH_RSS_MODE_DISABLED:
		bp->num_queues = 1;
		break;
	case ETH_RSS_MODE_REGULAR:
		bp->num_queues = bnx2x_calc_num_queues(bp);
		break;

	default:
		bp->num_queues = 1;
		break;
	}

	/* Add special queues */
	bp->num_queues += NONE_ETH_CONTEXT_USE;
}

#ifdef BCM_CNIC
static inline void bnx2x_set_fcoe_eth_macs(struct bnx2x *bp)
{
	if (!NO_FCOE(bp)) {
		if (!IS_MF_SD(bp))
			bnx2x_set_fip_eth_mac_addr(bp, 1);
		bnx2x_set_all_enode_macs(bp, 1);
		bp->flags |= FCOE_MACS_SET;
	}
}
#endif

static void bnx2x_release_firmware(struct bnx2x *bp)
{
	kfree(bp->init_ops_offsets);
	kfree(bp->init_ops);
	kfree(bp->init_data);
	release_firmware(bp->firmware);
}

static inline int bnx2x_set_real_num_queues(struct bnx2x *bp)
{
	int rc, num = bp->num_queues;

#ifdef BCM_CNIC
	if (NO_FCOE(bp))
		num -= FCOE_CONTEXT_USE;

#endif
	netif_set_real_num_tx_queues(bp->dev, num);
	rc = netif_set_real_num_rx_queues(bp->dev, num);
	return rc;
}

static inline void bnx2x_set_rx_buf_size(struct bnx2x *bp)
{
	int i;

	for_each_queue(bp, i) {
		struct bnx2x_fastpath *fp = &bp->fp[i];

		/* Always use a mini-jumbo MTU for the FCoE L2 ring */
		if (IS_FCOE_IDX(i))
			/*
			 * Although there are no IP frames expected to arrive to
			 * this ring we still want to add an
			 * IP_HEADER_ALIGNMENT_PADDING to prevent a buffer
			 * overrun attack.
			 */
			fp->rx_buf_size =
				BNX2X_FCOE_MINI_JUMBO_MTU + ETH_OVREHEAD +
				BNX2X_RX_ALIGN + IP_HEADER_ALIGNMENT_PADDING;
		else
			fp->rx_buf_size =
				bp->dev->mtu + ETH_OVREHEAD + BNX2X_RX_ALIGN +
				IP_HEADER_ALIGNMENT_PADDING;
	}
}

/* must be called with rtnl_lock */
int bnx2x_nic_load(struct bnx2x *bp, int load_mode)
{
	u32 load_code;
	int i, rc;

	/* Set init arrays */
	rc = bnx2x_init_firmware(bp);
	if (rc) {
		BNX2X_ERR("Error loading firmware\n");
		return rc;
	}

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return -EPERM;
#endif

	bp->state = BNX2X_STATE_OPENING_WAIT4_LOAD;

	/* must be called before memory allocation and HW init */
	bnx2x_ilt_set_info(bp);

	/* Set the receive queues buffer size */
	bnx2x_set_rx_buf_size(bp);

	if (bnx2x_alloc_mem(bp))
		return -ENOMEM;

	rc = bnx2x_set_real_num_queues(bp);
	if (rc) {
		BNX2X_ERR("Unable to set real_num_queues\n");
		goto load_error0;
	}

	for_each_queue(bp, i)
		bnx2x_fp(bp, i, disable_tpa) =
					((bp->flags & TPA_ENABLE_FLAG) == 0);

#ifdef BCM_CNIC
	/* We don't want TPA on FCoE L2 ring */
	bnx2x_fcoe(bp, disable_tpa) = 1;
#endif
	bnx2x_napi_enable(bp);

	/* Send LOAD_REQUEST command to MCP
	   Returns the type of LOAD command:
	   if it is the first port to be initialized
	   common blocks should be initialized, otherwise - not
	*/
	if (!BP_NOMCP(bp)) {
		load_code = bnx2x_fw_command(bp, DRV_MSG_CODE_LOAD_REQ, 0);
		if (!load_code) {
			BNX2X_ERR("MCP response failure, aborting\n");
			rc = -EBUSY;
			goto load_error1;
		}
		if (load_code == FW_MSG_CODE_DRV_LOAD_REFUSED) {
			rc = -EBUSY; /* other port in diagnostic mode */
			goto load_error1;
		}

	} else {
		int path = BP_PATH(bp);
		int port = BP_PORT(bp);

		DP(NETIF_MSG_IFUP, "NO MCP - load counts[%d]      %d, %d, %d\n",
		   path, load_count[path][0], load_count[path][1],
		   load_count[path][2]);
		load_count[path][0]++;
		load_count[path][1 + port]++;
		DP(NETIF_MSG_IFUP, "NO MCP - new load counts[%d]  %d, %d, %d\n",
		   path, load_count[path][0], load_count[path][1],
		   load_count[path][2]);
		if (load_count[path][0] == 1)
			load_code = FW_MSG_CODE_DRV_LOAD_COMMON;
		else if (load_count[path][1 + port] == 1)
			load_code = FW_MSG_CODE_DRV_LOAD_PORT;
		else
			load_code = FW_MSG_CODE_DRV_LOAD_FUNCTION;
	}

	if ((load_code == FW_MSG_CODE_DRV_LOAD_COMMON) ||
	    (load_code == FW_MSG_CODE_DRV_LOAD_COMMON_CHIP) ||
	    (load_code == FW_MSG_CODE_DRV_LOAD_PORT))
		bp->port.pmf = 1;
	else
		bp->port.pmf = 0;
	DP(NETIF_MSG_LINK, "pmf %d\n", bp->port.pmf);

	/* Initialize HW */
	rc = bnx2x_init_hw(bp, load_code);
	if (rc) {
		BNX2X_ERR("HW init failed, aborting\n");
		bnx2x_fw_command(bp, DRV_MSG_CODE_LOAD_DONE, 0);
		goto load_error2;
	}

	/* Connect to IRQs */
	rc = bnx2x_setup_irqs(bp);
	if (rc) {
		bnx2x_fw_command(bp, DRV_MSG_CODE_LOAD_DONE, 0);
		goto load_error2;
	}

	/* Setup NIC internals and enable interrupts */
	bnx2x_nic_init(bp, load_code);

	if (((load_code == FW_MSG_CODE_DRV_LOAD_COMMON) ||
	    (load_code == FW_MSG_CODE_DRV_LOAD_COMMON_CHIP)) &&
	    (bp->common.shmem2_base))
		SHMEM2_WR(bp, dcc_support,
			  (SHMEM_DCC_SUPPORT_DISABLE_ENABLE_PF_TLV |
			   SHMEM_DCC_SUPPORT_BANDWIDTH_ALLOCATION_TLV));

	/* Send LOAD_DONE command to MCP */
	if (!BP_NOMCP(bp)) {
		load_code = bnx2x_fw_command(bp, DRV_MSG_CODE_LOAD_DONE, 0);
		if (!load_code) {
			BNX2X_ERR("MCP response failure, aborting\n");
			rc = -EBUSY;
			goto load_error3;
		}
	}

	bnx2x_dcbx_init(bp);

	bp->state = BNX2X_STATE_OPENING_WAIT4_PORT;

	rc = bnx2x_func_start(bp);
	if (rc) {
		BNX2X_ERR("Function start failed!\n");
#ifndef BNX2X_STOP_ON_ERROR
		goto load_error3;
#else
		bp->panic = 1;
		return -EBUSY;
#endif
	}

	rc = bnx2x_setup_client(bp, &bp->fp[0], 1 /* Leading */);
	if (rc) {
		BNX2X_ERR("Setup leading failed!\n");
#ifndef BNX2X_STOP_ON_ERROR
		goto load_error3;
#else
		bp->panic = 1;
		return -EBUSY;
#endif
	}

	if (!CHIP_IS_E1(bp) &&
	    (bp->mf_config[BP_VN(bp)] & FUNC_MF_CFG_FUNC_DISABLED)) {
		DP(NETIF_MSG_IFUP, "mf_cfg function disabled\n");
		bp->flags |= MF_FUNC_DIS;
	}

#ifdef BCM_CNIC
	/* Enable Timer scan */
	REG_WR(bp, TM_REG_EN_LINEAR0_TIMER + BP_PORT(bp)*4, 1);
#endif

	for_each_nondefault_queue(bp, i) {
		rc = bnx2x_setup_client(bp, &bp->fp[i], 0);
		if (rc)
#ifdef BCM_CNIC
			goto load_error4;
#else
			goto load_error3;
#endif
	}

	/* Now when Clients are configured we are ready to work */
	bp->state = BNX2X_STATE_OPEN;

#ifdef BCM_CNIC
	bnx2x_set_fcoe_eth_macs(bp);
#endif

	bnx2x_set_eth_mac(bp, 1);

	/* Clear MC configuration */
	if (CHIP_IS_E1(bp))
		bnx2x_invalidate_e1_mc_list(bp);
	else
		bnx2x_invalidate_e1h_mc_list(bp);

	/* Clear UC lists configuration */
	bnx2x_invalidate_uc_list(bp);

	if (bp->pending_max) {
		bnx2x_update_max_mf_config(bp, bp->pending_max);
		bp->pending_max = 0;
	}

	if (bp->port.pmf)
		bnx2x_initial_phy_init(bp, load_mode);

	/* Initialize Rx filtering */
	bnx2x_set_rx_mode(bp->dev);

	/* Start fast path */
	switch (load_mode) {
	case LOAD_NORMAL:
		/* Tx queue should be only reenabled */
		netif_tx_wake_all_queues(bp->dev);
		/* Initialize the receive filter. */
		break;

	case LOAD_OPEN:
		netif_tx_start_all_queues(bp->dev);
		smp_mb__after_clear_bit();
		break;

	case LOAD_DIAG:
		bp->state = BNX2X_STATE_DIAG;
		break;

	default:
		break;
	}

	if (!bp->port.pmf)
		bnx2x__link_status_update(bp);

	/* start the timer */
	mod_timer(&bp->timer, jiffies + bp->current_interval);

#ifdef BCM_CNIC
	bnx2x_setup_cnic_irq_info(bp);
	if (bp->state == BNX2X_STATE_OPEN)
		bnx2x_cnic_notify(bp, CNIC_CTL_START_CMD);
#endif
	bnx2x_inc_load_cnt(bp);

	bnx2x_release_firmware(bp);

	return 0;

#ifdef BCM_CNIC
load_error4:
	/* Disable Timer scan */
	REG_WR(bp, TM_REG_EN_LINEAR0_TIMER + BP_PORT(bp)*4, 0);
#endif
load_error3:
	bnx2x_int_disable_sync(bp, 1);

	/* Free SKBs, SGEs, TPA pool and driver internals */
	bnx2x_free_skbs(bp);
	for_each_rx_queue(bp, i)
		bnx2x_free_rx_sge_range(bp, bp->fp + i, NUM_RX_SGE);

	/* Release IRQs */
	bnx2x_free_irq(bp);
load_error2:
	if (!BP_NOMCP(bp)) {
		bnx2x_fw_command(bp, DRV_MSG_CODE_UNLOAD_REQ_WOL_MCP, 0);
		bnx2x_fw_command(bp, DRV_MSG_CODE_UNLOAD_DONE, 0);
	}

	bp->port.pmf = 0;
load_error1:
	bnx2x_napi_disable(bp);
load_error0:
	bnx2x_free_mem(bp);

	bnx2x_release_firmware(bp);

	return rc;
}

/* must be called with rtnl_lock */
int bnx2x_nic_unload(struct bnx2x *bp, int unload_mode)
{
	int i;

	if (bp->state == BNX2X_STATE_CLOSED) {
		/* Interface has been removed - nothing to recover */
		bp->recovery_state = BNX2X_RECOVERY_DONE;
		bp->is_leader = 0;
		bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_RESERVED_08);
		smp_wmb();

		return -EINVAL;
	}

#ifdef BCM_CNIC
	bnx2x_cnic_notify(bp, CNIC_CTL_STOP_CMD);
#endif
	bp->state = BNX2X_STATE_CLOSING_WAIT4_HALT;

	/* Set "drop all" */
	bp->rx_mode = BNX2X_RX_MODE_NONE;
	bnx2x_set_storm_rx_mode(bp);

	/* Stop Tx */
	bnx2x_tx_disable(bp);

	del_timer_sync(&bp->timer);

	SHMEM_WR(bp, func_mb[BP_FW_MB_IDX(bp)].drv_pulse_mb,
		 (DRV_PULSE_ALWAYS_ALIVE | bp->fw_drv_pulse_wr_seq));

	bnx2x_stats_handle(bp, STATS_EVENT_STOP);

	/* Cleanup the chip if needed */
	if (unload_mode != UNLOAD_RECOVERY)
		bnx2x_chip_cleanup(bp, unload_mode);
	else {
		/* Disable HW interrupts, NAPI and Tx */
		bnx2x_netif_stop(bp, 1);

		/* Release IRQs */
		bnx2x_free_irq(bp);
	}

	bp->port.pmf = 0;

	/* Free SKBs, SGEs, TPA pool and driver internals */
	bnx2x_free_skbs(bp);
	for_each_rx_queue(bp, i)
		bnx2x_free_rx_sge_range(bp, bp->fp + i, NUM_RX_SGE);

	bnx2x_free_mem(bp);

	bp->state = BNX2X_STATE_CLOSED;

	/* The last driver must disable a "close the gate" if there is no
	 * parity attention or "process kill" pending.
	 */
	if ((!bnx2x_dec_load_cnt(bp)) && (!bnx2x_chk_parity_attn(bp)) &&
	    bnx2x_reset_is_done(bp))
		bnx2x_disable_close_the_gate(bp);

	/* Reset MCP mail box sequence if there is on going recovery */
	if (unload_mode == UNLOAD_RECOVERY)
		bp->fw_seq = 0;

	return 0;
}

int bnx2x_set_power_state(struct bnx2x *bp, pci_power_t state)
{
	u16 pmcsr;

	/* If there is no power capability, silently succeed */
	if (!bp->pm_cap) {
		DP(NETIF_MSG_HW, "No power capability. Breaking.\n");
		return 0;
	}

	pci_read_config_word(bp->pdev, bp->pm_cap + PCI_PM_CTRL, &pmcsr);

	switch (state) {
	case PCI_D0:
		pci_write_config_word(bp->pdev, bp->pm_cap + PCI_PM_CTRL,
				      ((pmcsr & ~PCI_PM_CTRL_STATE_MASK) |
				       PCI_PM_CTRL_PME_STATUS));

		if (pmcsr & PCI_PM_CTRL_STATE_MASK)
			/* delay required during transition out of D3hot */
			msleep(20);
		break;

	case PCI_D3hot:
		/* If there are other clients above don't
		   shut down the power */
		if (atomic_read(&bp->pdev->enable_cnt) != 1)
			return 0;
		/* Don't shut down the power for emulation and FPGA */
		if (CHIP_REV_IS_SLOW(bp))
			return 0;

		pmcsr &= ~PCI_PM_CTRL_STATE_MASK;
		pmcsr |= 3;

		if (bp->wol)
			pmcsr |= PCI_PM_CTRL_PME_ENABLE;

		pci_write_config_word(bp->pdev, bp->pm_cap + PCI_PM_CTRL,
				      pmcsr);

		/* No more memory access after this point until
		* device is brought back to D0.
		*/
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * net_device service functions
 */
int bnx2x_poll(struct napi_struct *napi, int budget)
{
	int work_done = 0;
	struct bnx2x_fastpath *fp = container_of(napi, struct bnx2x_fastpath,
						 napi);
	struct bnx2x *bp = fp->bp;

	while (1) {
#ifdef BNX2X_STOP_ON_ERROR
		if (unlikely(bp->panic)) {
			napi_complete(napi);
			return 0;
		}
#endif

		if (bnx2x_has_tx_work(fp))
			bnx2x_tx_int(fp);

		if (bnx2x_has_rx_work(fp)) {
			work_done += bnx2x_rx_int(fp, budget - work_done);

			/* must not complete if we consumed full budget */
			if (work_done >= budget)
				break;
		}

		/* Fall out from the NAPI loop if needed */
		if (!(bnx2x_has_rx_work(fp) || bnx2x_has_tx_work(fp))) {
#ifdef BCM_CNIC
			/* No need to update SB for FCoE L2 ring as long as
			 * it's connected to the default SB and the SB
			 * has been updated when NAPI was scheduled.
			 */
			if (IS_FCOE_FP(fp)) {
				napi_complete(napi);
				break;
			}
#endif

			bnx2x_update_fpsb_idx(fp);
			/* bnx2x_has_rx_work() reads the status block,
			 * thus we need to ensure that status block indices
			 * have been actually read (bnx2x_update_fpsb_idx)
			 * prior to this check (bnx2x_has_rx_work) so that
			 * we won't write the "newer" value of the status block
			 * to IGU (if there was a DMA right after
			 * bnx2x_has_rx_work and if there is no rmb, the memory
			 * reading (bnx2x_update_fpsb_idx) may be postponed
			 * to right before bnx2x_ack_sb). In this case there
			 * will never be another interrupt until there is
			 * another update of the status block, while there
			 * is still unhandled work.
			 */
			rmb();

			if (!(bnx2x_has_rx_work(fp) || bnx2x_has_tx_work(fp))) {
				napi_complete(napi);
				/* Re-enable interrupts */
				DP(NETIF_MSG_HW,
				   "Update index to %d\n", fp->fp_hc_idx);
				bnx2x_ack_sb(bp, fp->igu_sb_id, USTORM_ID,
					     le16_to_cpu(fp->fp_hc_idx),
					     IGU_INT_ENABLE, 1);
				break;
			}
		}
	}

	return work_done;
}

/* we split the first BD into headers and data BDs
 * to ease the pain of our fellow microcode engineers
 * we use one mapping for both BDs
 * So far this has only been observed to happen
 * in Other Operating Systems(TM)
 */
static noinline u16 bnx2x_tx_split(struct bnx2x *bp,
				   struct bnx2x_fastpath *fp,
				   struct sw_tx_bd *tx_buf,
				   struct eth_tx_start_bd **tx_bd, u16 hlen,
				   u16 bd_prod, int nbd)
{
	struct eth_tx_start_bd *h_tx_bd = *tx_bd;
	struct eth_tx_bd *d_tx_bd;
	dma_addr_t mapping;
	int old_len = le16_to_cpu(h_tx_bd->nbytes);

	/* first fix first BD */
	h_tx_bd->nbd = cpu_to_le16(nbd);
	h_tx_bd->nbytes = cpu_to_le16(hlen);

	DP(NETIF_MSG_TX_QUEUED,	"TSO split header size is %d "
	   "(%x:%x) nbd %d\n", h_tx_bd->nbytes, h_tx_bd->addr_hi,
	   h_tx_bd->addr_lo, h_tx_bd->nbd);

	/* now get a new data BD
	 * (after the pbd) and fill it */
	bd_prod = TX_BD(NEXT_TX_IDX(bd_prod));
	d_tx_bd = &fp->tx_desc_ring[bd_prod].reg_bd;

	mapping = HILO_U64(le32_to_cpu(h_tx_bd->addr_hi),
			   le32_to_cpu(h_tx_bd->addr_lo)) + hlen;

	d_tx_bd->addr_hi = cpu_to_le32(U64_HI(mapping));
	d_tx_bd->addr_lo = cpu_to_le32(U64_LO(mapping));
	d_tx_bd->nbytes = cpu_to_le16(old_len - hlen);

	/* this marks the BD as one that has no individual mapping */
	tx_buf->flags |= BNX2X_TSO_SPLIT_BD;

	DP(NETIF_MSG_TX_QUEUED,
	   "TSO split data size is %d (%x:%x)\n",
	   d_tx_bd->nbytes, d_tx_bd->addr_hi, d_tx_bd->addr_lo);

	/* update tx_bd */
	*tx_bd = (struct eth_tx_start_bd *)d_tx_bd;

	return bd_prod;
}

static inline u16 bnx2x_csum_fix(unsigned char *t_header, u16 csum, s8 fix)
{
	if (fix > 0)
		csum = (u16) ~csum_fold(csum_sub(csum,
				csum_partial(t_header - fix, fix, 0)));

	else if (fix < 0)
		csum = (u16) ~csum_fold(csum_add(csum,
				csum_partial(t_header, -fix, 0)));

	return swab16(csum);
}

static inline u32 bnx2x_xmit_type(struct bnx2x *bp, struct sk_buff *skb)
{
	u32 rc;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		rc = XMIT_PLAIN;

	else {
		if (vlan_get_protocol(skb) == htons(ETH_P_IPV6)) {
			rc = XMIT_CSUM_V6;
			if (ipv6_hdr(skb)->nexthdr == IPPROTO_TCP)
				rc |= XMIT_CSUM_TCP;

		} else {
			rc = XMIT_CSUM_V4;
			if (ip_hdr(skb)->protocol == IPPROTO_TCP)
				rc |= XMIT_CSUM_TCP;
		}
	}

	if (skb_is_gso_v6(skb))
		rc |= XMIT_GSO_V6 | XMIT_CSUM_TCP | XMIT_CSUM_V6;
	else if (skb_is_gso(skb))
		rc |= XMIT_GSO_V4 | XMIT_CSUM_V4 | XMIT_CSUM_TCP;

	return rc;
}

#if (MAX_SKB_FRAGS >= MAX_FETCH_BD - 3)
/* check if packet requires linearization (packet is too fragmented)
   no need to check fragmentation if page size > 8K (there will be no
   violation to FW restrictions) */
static int bnx2x_pkt_req_lin(struct bnx2x *bp, struct sk_buff *skb,
			     u32 xmit_type)
{
	int to_copy = 0;
	int hlen = 0;
	int first_bd_sz = 0;

	/* 3 = 1 (for linear data BD) + 2 (for PBD and last BD) */
	if (skb_shinfo(skb)->nr_frags >= (MAX_FETCH_BD - 3)) {

		if (xmit_type & XMIT_GSO) {
			unsigned short lso_mss = skb_shinfo(skb)->gso_size;
			/* Check if LSO packet needs to be copied:
			   3 = 1 (for headers BD) + 2 (for PBD and last BD) */
			int wnd_size = MAX_FETCH_BD - 3;
			/* Number of windows to check */
			int num_wnds = skb_shinfo(skb)->nr_frags - wnd_size;
			int wnd_idx = 0;
			int frag_idx = 0;
			u32 wnd_sum = 0;

			/* Headers length */
			hlen = (int)(skb_transport_header(skb) - skb->data) +
				tcp_hdrlen(skb);

			/* Amount of data (w/o headers) on linear part of SKB*/
			first_bd_sz = skb_headlen(skb) - hlen;

			wnd_sum  = first_bd_sz;

			/* Calculate the first sum - it's special */
			for (frag_idx = 0; frag_idx < wnd_size - 1; frag_idx++)
				wnd_sum +=
					skb_shinfo(skb)->frags[frag_idx].size;

			/* If there was data on linear skb data - check it */
			if (first_bd_sz > 0) {
				if (unlikely(wnd_sum < lso_mss)) {
					to_copy = 1;
					goto exit_lbl;
				}

				wnd_sum -= first_bd_sz;
			}

			/* Others are easier: run through the frag list and
			   check all windows */
			for (wnd_idx = 0; wnd_idx <= num_wnds; wnd_idx++) {
				wnd_sum +=
			  skb_shinfo(skb)->frags[wnd_idx + wnd_size - 1].size;

				if (unlikely(wnd_sum < lso_mss)) {
					to_copy = 1;
					break;
				}
				wnd_sum -=
					skb_shinfo(skb)->frags[wnd_idx].size;
			}
		} else {
			/* in non-LSO too fragmented packet should always
			   be linearized */
			to_copy = 1;
		}
	}

exit_lbl:
	if (unlikely(to_copy))
		DP(NETIF_MSG_TX_QUEUED,
		   "Linearization IS REQUIRED for %s packet. "
		   "num_frags %d  hlen %d  first_bd_sz %d\n",
		   (xmit_type & XMIT_GSO) ? "LSO" : "non-LSO",
		   skb_shinfo(skb)->nr_frags, hlen, first_bd_sz);

	return to_copy;
}
#endif

static inline void bnx2x_set_pbd_gso_e2(struct sk_buff *skb, u32 *parsing_data,
					u32 xmit_type)
{
	*parsing_data |= (skb_shinfo(skb)->gso_size <<
			      ETH_TX_PARSE_BD_E2_LSO_MSS_SHIFT) &
			      ETH_TX_PARSE_BD_E2_LSO_MSS;
	if ((xmit_type & XMIT_GSO_V6) &&
	    (ipv6_hdr(skb)->nexthdr == NEXTHDR_IPV6))
		*parsing_data |= ETH_TX_PARSE_BD_E2_IPV6_WITH_EXT_HDR;
}

/**
 * Update PBD in GSO case.
 *
 * @param skb
 * @param tx_start_bd
 * @param pbd
 * @param xmit_type
 */
static inline void bnx2x_set_pbd_gso(struct sk_buff *skb,
				     struct eth_tx_parse_bd_e1x *pbd,
				     u32 xmit_type)
{
	pbd->lso_mss = cpu_to_le16(skb_shinfo(skb)->gso_size);
	pbd->tcp_send_seq = swab32(tcp_hdr(skb)->seq);
	pbd->tcp_flags = pbd_tcp_flags(skb);

	if (xmit_type & XMIT_GSO_V4) {
		pbd->ip_id = swab16(ip_hdr(skb)->id);
		pbd->tcp_pseudo_csum =
			swab16(~csum_tcpudp_magic(ip_hdr(skb)->saddr,
						  ip_hdr(skb)->daddr,
						  0, IPPROTO_TCP, 0));

	} else
		pbd->tcp_pseudo_csum =
			swab16(~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
						&ipv6_hdr(skb)->daddr,
						0, IPPROTO_TCP, 0));

	pbd->global_data |= ETH_TX_PARSE_BD_E1X_PSEUDO_CS_WITHOUT_LEN;
}

/**
 *
 * @param skb
 * @param tx_start_bd
 * @param pbd_e2
 * @param xmit_type
 *
 * @return header len
 */
static inline  u8 bnx2x_set_pbd_csum_e2(struct bnx2x *bp, struct sk_buff *skb,
	u32 *parsing_data, u32 xmit_type)
{
	*parsing_data |=
			((((u8 *)skb_transport_header(skb) - skb->data) >> 1) <<
			ETH_TX_PARSE_BD_E2_TCP_HDR_START_OFFSET_W_SHIFT) &
			ETH_TX_PARSE_BD_E2_TCP_HDR_START_OFFSET_W;

	if (xmit_type & XMIT_CSUM_TCP) {
		*parsing_data |= ((tcp_hdrlen(skb) / 4) <<
			ETH_TX_PARSE_BD_E2_TCP_HDR_LENGTH_DW_SHIFT) &
			ETH_TX_PARSE_BD_E2_TCP_HDR_LENGTH_DW;

		return skb_transport_header(skb) + tcp_hdrlen(skb) - skb->data;
	} else
		/* We support checksum offload for TCP and UDP only.
		 * No need to pass the UDP header length - it's a constant.
		 */
		return skb_transport_header(skb) +
				sizeof(struct udphdr) - skb->data;
}

/**
 *
 * @param skb
 * @param tx_start_bd
 * @param pbd
 * @param xmit_type
 *
 * @return Header length
 */
static inline u8 bnx2x_set_pbd_csum(struct bnx2x *bp, struct sk_buff *skb,
	struct eth_tx_parse_bd_e1x *pbd,
	u32 xmit_type)
{
	u8 hlen = (skb_network_header(skb) - skb->data) >> 1;

	/* for now NS flag is not used in Linux */
	pbd->global_data =
		(hlen | ((skb->protocol == cpu_to_be16(ETH_P_8021Q)) <<
			 ETH_TX_PARSE_BD_E1X_LLC_SNAP_EN_SHIFT));

	pbd->ip_hlen_w = (skb_transport_header(skb) -
			skb_network_header(skb)) >> 1;

	hlen += pbd->ip_hlen_w;

	/* We support checksum offload for TCP and UDP only */
	if (xmit_type & XMIT_CSUM_TCP)
		hlen += tcp_hdrlen(skb) / 2;
	else
		hlen += sizeof(struct udphdr) / 2;

	pbd->total_hlen_w = cpu_to_le16(hlen);
	hlen = hlen*2;

	if (xmit_type & XMIT_CSUM_TCP) {
		pbd->tcp_pseudo_csum = swab16(tcp_hdr(skb)->check);

	} else {
		s8 fix = SKB_CS_OFF(skb); /* signed! */

		DP(NETIF_MSG_TX_QUEUED,
		   "hlen %d  fix %d  csum before fix %x\n",
		   le16_to_cpu(pbd->total_hlen_w), fix, SKB_CS(skb));

		/* HW bug: fixup the CSUM */
		pbd->tcp_pseudo_csum =
			bnx2x_csum_fix(skb_transport_header(skb),
				       SKB_CS(skb), fix);

		DP(NETIF_MSG_TX_QUEUED, "csum after fix %x\n",
		   pbd->tcp_pseudo_csum);
	}

	return hlen;
}

/* called with netif_tx_lock
 * bnx2x_tx_int() runs without netif_tx_lock unless it needs to call
 * netif_wake_queue()
 */
netdev_tx_t bnx2x_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);
	struct bnx2x_fastpath *fp;
	struct netdev_queue *txq;
	struct sw_tx_bd *tx_buf;
	struct eth_tx_start_bd *tx_start_bd;
	struct eth_tx_bd *tx_data_bd, *total_pkt_bd = NULL;
	struct eth_tx_parse_bd_e1x *pbd_e1x = NULL;
	struct eth_tx_parse_bd_e2 *pbd_e2 = NULL;
	u32 pbd_e2_parsing_data = 0;
	u16 pkt_prod, bd_prod;
	int nbd, fp_index;
	dma_addr_t mapping;
	u32 xmit_type = bnx2x_xmit_type(bp, skb);
	int i;
	u8 hlen = 0;
	__le16 pkt_size = 0;
	struct ethhdr *eth;
	u8 mac_type = UNICAST_ADDRESS;

#ifdef BNX2X_STOP_ON_ERROR
	if (unlikely(bp->panic))
		return NETDEV_TX_BUSY;
#endif

	fp_index = skb_get_queue_mapping(skb);
	txq = netdev_get_tx_queue(dev, fp_index);

	fp = &bp->fp[fp_index];

	if (unlikely(bnx2x_tx_avail(fp) < (skb_shinfo(skb)->nr_frags + 3))) {
		fp->eth_q_stats.driver_xoff++;
		netif_tx_stop_queue(txq);
		BNX2X_ERR("BUG! Tx ring full when queue awake!\n");
		return NETDEV_TX_BUSY;
	}

	DP(NETIF_MSG_TX_QUEUED, "queue[%d]: SKB: summed %x  protocol %x  "
				"protocol(%x,%x) gso type %x  xmit_type %x\n",
	   fp_index, skb->ip_summed, skb->protocol, ipv6_hdr(skb)->nexthdr,
	   ip_hdr(skb)->protocol, skb_shinfo(skb)->gso_type, xmit_type);

	eth = (struct ethhdr *)skb->data;

	/* set flag according to packet type (UNICAST_ADDRESS is default)*/
	if (unlikely(is_multicast_ether_addr(eth->h_dest))) {
		if (is_broadcast_ether_addr(eth->h_dest))
			mac_type = BROADCAST_ADDRESS;
		else
			mac_type = MULTICAST_ADDRESS;
	}

#if (MAX_SKB_FRAGS >= MAX_FETCH_BD - 3)
	/* First, check if we need to linearize the skb (due to FW
	   restrictions). No need to check fragmentation if page size > 8K
	   (there will be no violation to FW restrictions) */
	if (bnx2x_pkt_req_lin(bp, skb, xmit_type)) {
		/* Statistics of linearization */
		bp->lin_cnt++;
		if (skb_linearize(skb) != 0) {
			DP(NETIF_MSG_TX_QUEUED, "SKB linearization failed - "
			   "silently dropping this SKB\n");
			dev_kfree_skb_any(skb);
			return NETDEV_TX_OK;
		}
	}
#endif

	/*
	Please read carefully. First we use one BD which we mark as start,
	then we have a parsing info BD (used for TSO or xsum),
	and only then we have the rest of the TSO BDs.
	(don't forget to mark the last one as last,
	and to unmap only AFTER you write to the BD ...)
	And above all, all pdb sizes are in words - NOT DWORDS!
	*/

	pkt_prod = fp->tx_pkt_prod++;
	bd_prod = TX_BD(fp->tx_bd_prod);

	/* get a tx_buf and first BD */
	tx_buf = &fp->tx_buf_ring[TX_BD(pkt_prod)];
	tx_start_bd = &fp->tx_desc_ring[bd_prod].start_bd;

	tx_start_bd->bd_flags.as_bitfield = ETH_TX_BD_FLAGS_START_BD;
	SET_FLAG(tx_start_bd->general_data, ETH_TX_START_BD_ETH_ADDR_TYPE,
		 mac_type);

	/* header nbd */
	SET_FLAG(tx_start_bd->general_data, ETH_TX_START_BD_HDR_NBDS, 1);

	/* remember the first BD of the packet */
	tx_buf->first_bd = fp->tx_bd_prod;
	tx_buf->skb = skb;
	tx_buf->flags = 0;

	DP(NETIF_MSG_TX_QUEUED,
	   "sending pkt %u @%p  next_idx %u  bd %u @%p\n",
	   pkt_prod, tx_buf, fp->tx_pkt_prod, bd_prod, tx_start_bd);

	if (vlan_tx_tag_present(skb)) {
		tx_start_bd->vlan_or_ethertype =
		    cpu_to_le16(vlan_tx_tag_get(skb));
		tx_start_bd->bd_flags.as_bitfield |=
		    (X_ETH_OUTBAND_VLAN << ETH_TX_BD_FLAGS_VLAN_MODE_SHIFT);
	} else
		tx_start_bd->vlan_or_ethertype = cpu_to_le16(pkt_prod);

	/* turn on parsing and get a BD */
	bd_prod = TX_BD(NEXT_TX_IDX(bd_prod));

	if (xmit_type & XMIT_CSUM) {
		tx_start_bd->bd_flags.as_bitfield |= ETH_TX_BD_FLAGS_L4_CSUM;

		if (xmit_type & XMIT_CSUM_V4)
			tx_start_bd->bd_flags.as_bitfield |=
						ETH_TX_BD_FLAGS_IP_CSUM;
		else
			tx_start_bd->bd_flags.as_bitfield |=
						ETH_TX_BD_FLAGS_IPV6;

		if (!(xmit_type & XMIT_CSUM_TCP))
			tx_start_bd->bd_flags.as_bitfield |=
						ETH_TX_BD_FLAGS_IS_UDP;
	}

	if (CHIP_IS_E2(bp)) {
		pbd_e2 = &fp->tx_desc_ring[bd_prod].parse_bd_e2;
		memset(pbd_e2, 0, sizeof(struct eth_tx_parse_bd_e2));
		/* Set PBD in checksum offload case */
		if (xmit_type & XMIT_CSUM)
			hlen = bnx2x_set_pbd_csum_e2(bp, skb,
						     &pbd_e2_parsing_data,
						     xmit_type);
	} else {
		pbd_e1x = &fp->tx_desc_ring[bd_prod].parse_bd_e1x;
		memset(pbd_e1x, 0, sizeof(struct eth_tx_parse_bd_e1x));
		/* Set PBD in checksum offload case */
		if (xmit_type & XMIT_CSUM)
			hlen = bnx2x_set_pbd_csum(bp, skb, pbd_e1x, xmit_type);

	}

	/* Map skb linear data for DMA */
	mapping = dma_map_single(&bp->pdev->dev, skb->data,
				 skb_headlen(skb), DMA_TO_DEVICE);

	/* Setup the data pointer of the first BD of the packet */
	tx_start_bd->addr_hi = cpu_to_le32(U64_HI(mapping));
	tx_start_bd->addr_lo = cpu_to_le32(U64_LO(mapping));
	nbd = skb_shinfo(skb)->nr_frags + 2; /* start_bd + pbd + frags */
	tx_start_bd->nbd = cpu_to_le16(nbd);
	tx_start_bd->nbytes = cpu_to_le16(skb_headlen(skb));
	pkt_size = tx_start_bd->nbytes;

	DP(NETIF_MSG_TX_QUEUED, "first bd @%p  addr (%x:%x)  nbd %d"
	   "  nbytes %d  flags %x  vlan %x\n",
	   tx_start_bd, tx_start_bd->addr_hi, tx_start_bd->addr_lo,
	   le16_to_cpu(tx_start_bd->nbd), le16_to_cpu(tx_start_bd->nbytes),
	   tx_start_bd->bd_flags.as_bitfield,
	   le16_to_cpu(tx_start_bd->vlan_or_ethertype));

	if (xmit_type & XMIT_GSO) {

		DP(NETIF_MSG_TX_QUEUED,
		   "TSO packet len %d  hlen %d  total len %d  tso size %d\n",
		   skb->len, hlen, skb_headlen(skb),
		   skb_shinfo(skb)->gso_size);

		tx_start_bd->bd_flags.as_bitfield |= ETH_TX_BD_FLAGS_SW_LSO;

		if (unlikely(skb_headlen(skb) > hlen))
			bd_prod = bnx2x_tx_split(bp, fp, tx_buf, &tx_start_bd,
						 hlen, bd_prod, ++nbd);
		if (CHIP_IS_E2(bp))
			bnx2x_set_pbd_gso_e2(skb, &pbd_e2_parsing_data,
					     xmit_type);
		else
			bnx2x_set_pbd_gso(skb, pbd_e1x, xmit_type);
	}

	/* Set the PBD's parsing_data field if not zero
	 * (for the chips newer than 57711).
	 */
	if (pbd_e2_parsing_data)
		pbd_e2->parsing_data = cpu_to_le32(pbd_e2_parsing_data);

	tx_data_bd = (struct eth_tx_bd *)tx_start_bd;

	/* Handle fragmented skb */
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		bd_prod = TX_BD(NEXT_TX_IDX(bd_prod));
		tx_data_bd = &fp->tx_desc_ring[bd_prod].reg_bd;
		if (total_pkt_bd == NULL)
			total_pkt_bd = &fp->tx_desc_ring[bd_prod].reg_bd;

		mapping = dma_map_page(&bp->pdev->dev, frag->page,
				       frag->page_offset,
				       frag->size, DMA_TO_DEVICE);

		tx_data_bd->addr_hi = cpu_to_le32(U64_HI(mapping));
		tx_data_bd->addr_lo = cpu_to_le32(U64_LO(mapping));
		tx_data_bd->nbytes = cpu_to_le16(frag->size);
		le16_add_cpu(&pkt_size, frag->size);

		DP(NETIF_MSG_TX_QUEUED,
		   "frag %d  bd @%p  addr (%x:%x)  nbytes %d\n",
		   i, tx_data_bd, tx_data_bd->addr_hi, tx_data_bd->addr_lo,
		   le16_to_cpu(tx_data_bd->nbytes));
	}

	DP(NETIF_MSG_TX_QUEUED, "last bd @%p\n", tx_data_bd);

	bd_prod = TX_BD(NEXT_TX_IDX(bd_prod));

	/* now send a tx doorbell, counting the next BD
	 * if the packet contains or ends with it
	 */
	if (TX_BD_POFF(bd_prod) < nbd)
		nbd++;

	if (total_pkt_bd != NULL)
		total_pkt_bd->total_pkt_bytes = pkt_size;

	if (pbd_e1x)
		DP(NETIF_MSG_TX_QUEUED,
		   "PBD (E1X) @%p  ip_data %x  ip_hlen %u  ip_id %u  lso_mss %u"
		   "  tcp_flags %x  xsum %x  seq %u  hlen %u\n",
		   pbd_e1x, pbd_e1x->global_data, pbd_e1x->ip_hlen_w,
		   pbd_e1x->ip_id, pbd_e1x->lso_mss, pbd_e1x->tcp_flags,
		   pbd_e1x->tcp_pseudo_csum, pbd_e1x->tcp_send_seq,
		    le16_to_cpu(pbd_e1x->total_hlen_w));
	if (pbd_e2)
		DP(NETIF_MSG_TX_QUEUED,
		   "PBD (E2) @%p  dst %x %x %x src %x %x %x parsing_data %x\n",
		   pbd_e2, pbd_e2->dst_mac_addr_hi, pbd_e2->dst_mac_addr_mid,
		   pbd_e2->dst_mac_addr_lo, pbd_e2->src_mac_addr_hi,
		   pbd_e2->src_mac_addr_mid, pbd_e2->src_mac_addr_lo,
		   pbd_e2->parsing_data);
	DP(NETIF_MSG_TX_QUEUED, "doorbell: nbd %d  bd %u\n", nbd, bd_prod);

	/*
	 * Make sure that the BD data is updated before updating the producer
	 * since FW might read the BD right after the producer is updated.
	 * This is only applicable for weak-ordered memory model archs such
	 * as IA-64. The following barrier is also mandatory since FW will
	 * assumes packets must have BDs.
	 */
	wmb();

	fp->tx_db.data.prod += nbd;
	barrier();

	DOORBELL(bp, fp->cid, fp->tx_db.raw);

	mmiowb();

	fp->tx_bd_prod += nbd;

	if (unlikely(bnx2x_tx_avail(fp) < MAX_SKB_FRAGS + 3)) {
		netif_tx_stop_queue(txq);

		/* paired memory barrier is in bnx2x_tx_int(), we have to keep
		 * ordering of set_bit() in netif_tx_stop_queue() and read of
		 * fp->bd_tx_cons */
		smp_mb();

		fp->eth_q_stats.driver_xoff++;
		if (bnx2x_tx_avail(fp) >= MAX_SKB_FRAGS + 3)
			netif_tx_wake_queue(txq);
	}
	fp->tx_pkt++;

	return NETDEV_TX_OK;
}

/* called with rtnl_lock */
int bnx2x_change_mac_addr(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	struct bnx2x *bp = netdev_priv(dev);

	if (!is_valid_ether_addr((u8 *)(addr->sa_data)))
		return -EINVAL;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	if (netif_running(dev))
		bnx2x_set_eth_mac(bp, 1);

	return 0;
}


static int bnx2x_setup_irqs(struct bnx2x *bp)
{
	int rc = 0;
	if (bp->flags & USING_MSIX_FLAG) {
		rc = bnx2x_req_msix_irqs(bp);
		if (rc)
			return rc;
	} else {
		bnx2x_ack_int(bp);
		rc = bnx2x_req_irq(bp);
		if (rc) {
			BNX2X_ERR("IRQ request failed  rc %d, aborting\n", rc);
			return rc;
		}
		if (bp->flags & USING_MSI_FLAG) {
			bp->dev->irq = bp->pdev->irq;
			netdev_info(bp->dev, "using MSI  IRQ %d\n",
			       bp->pdev->irq);
		}
	}

	return 0;
}

void bnx2x_free_mem_bp(struct bnx2x *bp)
{
	kfree(bp->fp);
	kfree(bp->msix_table);
	kfree(bp->ilt);
}

int __devinit bnx2x_alloc_mem_bp(struct bnx2x *bp)
{
	struct bnx2x_fastpath *fp;
	struct msix_entry *tbl;
	struct bnx2x_ilt *ilt;

	/* fp array */
	fp = kzalloc(L2_FP_COUNT(bp->l2_cid_count)*sizeof(*fp), GFP_KERNEL);
	if (!fp)
		goto alloc_err;
	bp->fp = fp;

	/* msix table */
	tbl = kzalloc((FP_SB_COUNT(bp->l2_cid_count) + 1) * sizeof(*tbl),
				  GFP_KERNEL);
	if (!tbl)
		goto alloc_err;
	bp->msix_table = tbl;

	/* ilt */
	ilt = kzalloc(sizeof(*ilt), GFP_KERNEL);
	if (!ilt)
		goto alloc_err;
	bp->ilt = ilt;

	return 0;
alloc_err:
	bnx2x_free_mem_bp(bp);
	return -ENOMEM;

}

/* called with rtnl_lock */
int bnx2x_change_mtu(struct net_device *dev, int new_mtu)
{
	struct bnx2x *bp = netdev_priv(dev);
	int rc = 0;

	if (bp->recovery_state != BNX2X_RECOVERY_DONE) {
		printk(KERN_ERR "Handling parity error recovery. Try again later\n");
		return -EAGAIN;
	}

	if ((new_mtu > ETH_MAX_JUMBO_PACKET_SIZE) ||
	    ((new_mtu + ETH_HLEN) < ETH_MIN_PACKET_SIZE))
		return -EINVAL;

	/* This does not race with packet allocation
	 * because the actual alloc size is
	 * only updated as part of load
	 */
	dev->mtu = new_mtu;

	if (netif_running(dev)) {
		bnx2x_nic_unload(bp, UNLOAD_NORMAL);
		rc = bnx2x_nic_load(bp, LOAD_NORMAL);
	}

	return rc;
}

void bnx2x_tx_timeout(struct net_device *dev)
{
	struct bnx2x *bp = netdev_priv(dev);

#ifdef BNX2X_STOP_ON_ERROR
	if (!bp->panic)
		bnx2x_panic();
#endif
	/* This allows the netif to be shutdown gracefully before resetting */
	schedule_delayed_work(&bp->reset_task, 0);
}

int bnx2x_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2x *bp;

	if (!dev) {
		dev_err(&pdev->dev, "BAD net device from bnx2x_init_one\n");
		return -ENODEV;
	}
	bp = netdev_priv(dev);

	rtnl_lock();

	pci_save_state(pdev);

	if (!netif_running(dev)) {
		rtnl_unlock();
		return 0;
	}

	netif_device_detach(dev);

	bnx2x_nic_unload(bp, UNLOAD_CLOSE);

	bnx2x_set_power_state(bp, pci_choose_state(pdev, state));

	rtnl_unlock();

	return 0;
}

int bnx2x_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2x *bp;
	int rc;

	if (!dev) {
		dev_err(&pdev->dev, "BAD net device from bnx2x_init_one\n");
		return -ENODEV;
	}
	bp = netdev_priv(dev);

	if (bp->recovery_state != BNX2X_RECOVERY_DONE) {
		printk(KERN_ERR "Handling parity error recovery. Try again later\n");
		return -EAGAIN;
	}

	rtnl_lock();

	pci_restore_state(pdev);

	if (!netif_running(dev)) {
		rtnl_unlock();
		return 0;
	}

	bnx2x_set_power_state(bp, PCI_D0);
	netif_device_attach(dev);

	/* Since the chip was reset, clear the FW sequence number */
	bp->fw_seq = 0;
	rc = bnx2x_nic_load(bp, LOAD_OPEN);

	rtnl_unlock();

	return rc;
}
