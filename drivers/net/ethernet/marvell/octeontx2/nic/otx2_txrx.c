// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Ethernet driver
 *
 * Copyright (C) 2020 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/etherdevice.h>
#include <net/ip.h>

#include "otx2_reg.h"
#include "otx2_common.h"
#include "otx2_struct.h"
#include "otx2_txrx.h"

#define CQE_ADDR(CQ, idx) ((CQ)->cqe_base + ((CQ)->cqe_size * (idx)))

static struct nix_cqe_hdr_s *otx2_get_next_cqe(struct otx2_cq_queue *cq)
{
	struct nix_cqe_hdr_s *cqe_hdr;

	cqe_hdr = (struct nix_cqe_hdr_s *)CQE_ADDR(cq, cq->cq_head);
	if (cqe_hdr->cqe_type == NIX_XQE_TYPE_INVALID)
		return NULL;

	cq->cq_head++;
	cq->cq_head &= (cq->cqe_cnt - 1);

	return cqe_hdr;
}

static unsigned int frag_num(unsigned int i)
{
#ifdef __BIG_ENDIAN
	return (i & ~3) + 3 - (i & 3);
#else
	return i;
#endif
}

static dma_addr_t otx2_dma_map_skb_frag(struct otx2_nic *pfvf,
					struct sk_buff *skb, int seg, int *len)
{
	const skb_frag_t *frag;
	struct page *page;
	int offset;

	/* First segment is always skb->data */
	if (!seg) {
		page = virt_to_page(skb->data);
		offset = offset_in_page(skb->data);
		*len = skb_headlen(skb);
	} else {
		frag = &skb_shinfo(skb)->frags[seg - 1];
		page = skb_frag_page(frag);
		offset = skb_frag_off(frag);
		*len = skb_frag_size(frag);
	}
	return otx2_dma_map_page(pfvf, page, offset, *len, DMA_TO_DEVICE);
}

static void otx2_dma_unmap_skb_frags(struct otx2_nic *pfvf, struct sg_list *sg)
{
	int seg;

	for (seg = 0; seg < sg->num_segs; seg++) {
		otx2_dma_unmap_page(pfvf, sg->dma_addr[seg],
				    sg->size[seg], DMA_TO_DEVICE);
	}
	sg->num_segs = 0;
}

static void otx2_snd_pkt_handler(struct otx2_nic *pfvf,
				 struct otx2_cq_queue *cq,
				 struct otx2_snd_queue *sq,
				 struct nix_cqe_tx_s *cqe,
				 int budget, int *tx_pkts, int *tx_bytes)
{
	struct nix_send_comp_s *snd_comp = &cqe->comp;
	struct sk_buff *skb = NULL;
	struct sg_list *sg;

	if (unlikely(snd_comp->status))
		net_err_ratelimited("%s: TX%d: Error in send CQ status:%x\n",
				    pfvf->netdev->name, cq->cint_idx,
				    snd_comp->status);

	sg = &sq->sg[snd_comp->sqe_id];
	skb = (struct sk_buff *)sg->skb;
	if (unlikely(!skb))
		return;

	*tx_bytes += skb->len;
	(*tx_pkts)++;
	otx2_dma_unmap_skb_frags(pfvf, sg);
	napi_consume_skb(skb, budget);
	sg->skb = (u64)NULL;
}

static void otx2_skb_add_frag(struct otx2_nic *pfvf, struct sk_buff *skb,
			      u64 iova, int len)
{
	struct page *page;
	void *va;

	va = phys_to_virt(otx2_iova_to_phys(pfvf->iommu_domain, iova));
	page = virt_to_page(va);
	skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags, page,
			va - page_address(page), len, pfvf->rbsize);

	otx2_dma_unmap_page(pfvf, iova - OTX2_HEAD_ROOM,
			    pfvf->rbsize, DMA_FROM_DEVICE);
}

static bool otx2_check_rcv_errors(struct otx2_nic *pfvf,
				  struct nix_cqe_rx_s *cqe, int qidx)
{
	struct otx2_drv_stats *stats = &pfvf->hw.drv_stats;
	struct nix_rx_parse_s *parse = &cqe->parse;

	if (parse->errlev == NPC_ERRLVL_RE) {
		switch (parse->errcode) {
		case ERRCODE_FCS:
		case ERRCODE_FCS_RCV:
			atomic_inc(&stats->rx_fcs_errs);
			break;
		case ERRCODE_UNDERSIZE:
			atomic_inc(&stats->rx_undersize_errs);
			break;
		case ERRCODE_OVERSIZE:
			atomic_inc(&stats->rx_oversize_errs);
			break;
		case ERRCODE_OL2_LEN_MISMATCH:
			atomic_inc(&stats->rx_len_errs);
			break;
		default:
			atomic_inc(&stats->rx_other_errs);
			break;
		}
	} else if (parse->errlev == NPC_ERRLVL_NIX) {
		switch (parse->errcode) {
		case ERRCODE_OL3_LEN:
		case ERRCODE_OL4_LEN:
		case ERRCODE_IL3_LEN:
		case ERRCODE_IL4_LEN:
			atomic_inc(&stats->rx_len_errs);
			break;
		case ERRCODE_OL4_CSUM:
		case ERRCODE_IL4_CSUM:
			atomic_inc(&stats->rx_csum_errs);
			break;
		default:
			atomic_inc(&stats->rx_other_errs);
			break;
		}
	} else {
		atomic_inc(&stats->rx_other_errs);
		/* For now ignore all the NPC parser errors and
		 * pass the packets to stack.
		 */
		return false;
	}

	/* If RXALL is enabled pass on packets to stack. */
	if (cqe->sg.segs && (pfvf->netdev->features & NETIF_F_RXALL))
		return false;

	/* Free buffer back to pool */
	if (cqe->sg.segs)
		otx2_aura_freeptr(pfvf, qidx, cqe->sg.seg_addr & ~0x07ULL);
	return true;
}

static void otx2_rcv_pkt_handler(struct otx2_nic *pfvf,
				 struct napi_struct *napi,
				 struct otx2_cq_queue *cq,
				 struct nix_cqe_rx_s *cqe)
{
	struct nix_rx_parse_s *parse = &cqe->parse;
	struct sk_buff *skb = NULL;

	if (unlikely(parse->errlev || parse->errcode)) {
		if (otx2_check_rcv_errors(pfvf, cqe, cq->cq_idx))
			return;
	}

	skb = napi_get_frags(napi);
	if (unlikely(!skb))
		return;

	otx2_skb_add_frag(pfvf, skb, cqe->sg.seg_addr, cqe->sg.seg_size);
	cq->pool_ptrs++;

	skb_record_rx_queue(skb, cq->cq_idx);
	if (pfvf->netdev->features & NETIF_F_RXCSUM)
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	napi_gro_frags(napi);
}

static int otx2_rx_napi_handler(struct otx2_nic *pfvf,
				struct napi_struct *napi,
				struct otx2_cq_queue *cq, int budget)
{
	struct nix_cqe_rx_s *cqe;
	int processed_cqe = 0;
	s64 bufptr;

	while (likely(processed_cqe < budget)) {
		cqe = (struct nix_cqe_rx_s *)CQE_ADDR(cq, cq->cq_head);
		if (cqe->hdr.cqe_type == NIX_XQE_TYPE_INVALID ||
		    !cqe->sg.seg_addr) {
			if (!processed_cqe)
				return 0;
			break;
		}
		cq->cq_head++;
		cq->cq_head &= (cq->cqe_cnt - 1);

		otx2_rcv_pkt_handler(pfvf, napi, cq, cqe);

		cqe->hdr.cqe_type = NIX_XQE_TYPE_INVALID;
		cqe->sg.seg_addr = 0x00;
		processed_cqe++;
	}

	/* Free CQEs to HW */
	otx2_write64(pfvf, NIX_LF_CQ_OP_DOOR,
		     ((u64)cq->cq_idx << 32) | processed_cqe);

	if (unlikely(!cq->pool_ptrs))
		return 0;

	/* Refill pool with new buffers */
	while (cq->pool_ptrs) {
		bufptr = otx2_alloc_rbuf(pfvf, cq->rbpool, GFP_ATOMIC);
		if (unlikely(bufptr <= 0))
			break;
		otx2_aura_freeptr(pfvf, cq->cq_idx, bufptr + OTX2_HEAD_ROOM);
		cq->pool_ptrs--;
	}
	otx2_get_page(cq->rbpool);

	return processed_cqe;
}

static int otx2_tx_napi_handler(struct otx2_nic *pfvf,
				struct otx2_cq_queue *cq, int budget)
{
	int tx_pkts = 0, tx_bytes = 0;
	struct nix_cqe_tx_s *cqe;
	int processed_cqe = 0;

	while (likely(processed_cqe < budget)) {
		cqe = (struct nix_cqe_tx_s *)otx2_get_next_cqe(cq);
		if (unlikely(!cqe)) {
			if (!processed_cqe)
				return 0;
			break;
		}
		otx2_snd_pkt_handler(pfvf, cq, &pfvf->qset.sq[cq->cint_idx],
				     cqe, budget, &tx_pkts, &tx_bytes);

		cqe->hdr.cqe_type = NIX_XQE_TYPE_INVALID;
		processed_cqe++;
	}

	/* Free CQEs to HW */
	otx2_write64(pfvf, NIX_LF_CQ_OP_DOOR,
		     ((u64)cq->cq_idx << 32) | processed_cqe);

	if (likely(tx_pkts)) {
		struct netdev_queue *txq;

		txq = netdev_get_tx_queue(pfvf->netdev, cq->cint_idx);
		netdev_tx_completed_queue(txq, tx_pkts, tx_bytes);
		/* Check if queue was stopped earlier due to ring full */
		smp_mb();
		if (netif_tx_queue_stopped(txq) &&
		    netif_carrier_ok(pfvf->netdev))
			netif_tx_wake_queue(txq);
	}
	return 0;
}

int otx2_napi_handler(struct napi_struct *napi, int budget)
{
	struct otx2_cq_poll *cq_poll;
	int workdone = 0, cq_idx, i;
	struct otx2_cq_queue *cq;
	struct otx2_qset *qset;
	struct otx2_nic *pfvf;

	cq_poll = container_of(napi, struct otx2_cq_poll, napi);
	pfvf = (struct otx2_nic *)cq_poll->dev;
	qset = &pfvf->qset;

	for (i = CQS_PER_CINT - 1; i >= 0; i--) {
		cq_idx = cq_poll->cq_ids[i];
		if (unlikely(cq_idx == CINT_INVALID_CQ))
			continue;
		cq = &qset->cq[cq_idx];
		if (cq->cq_type == CQ_RX) {
			workdone += otx2_rx_napi_handler(pfvf, napi,
							 cq, budget);
		} else {
			workdone += otx2_tx_napi_handler(pfvf, cq, budget);
		}
	}

	/* Clear the IRQ */
	otx2_write64(pfvf, NIX_LF_CINTX_INT(cq_poll->cint_idx), BIT_ULL(0));

	if (workdone < budget && napi_complete_done(napi, workdone)) {
		/* Re-enable interrupts */
		otx2_write64(pfvf, NIX_LF_CINTX_ENA_W1S(cq_poll->cint_idx),
			     BIT_ULL(0));
	}
	return workdone;
}

static void otx2_sqe_flush(struct otx2_snd_queue *sq, int size)
{
	u64 status;

	/* Packet data stores should finish before SQE is flushed to HW */
	dma_wmb();

	do {
		memcpy(sq->lmt_addr, sq->sqe_base, size);
		status = otx2_lmt_flush(sq->io_addr);
	} while (status == 0);

	sq->head++;
	sq->head &= (sq->sqe_cnt - 1);
}

#define MAX_SEGS_PER_SG	3
/* Add SQE scatter/gather subdescriptor structure */
static bool otx2_sqe_add_sg(struct otx2_nic *pfvf, struct otx2_snd_queue *sq,
			    struct sk_buff *skb, int num_segs, int *offset)
{
	struct nix_sqe_sg_s *sg = NULL;
	u64 dma_addr, *iova = NULL;
	u16 *sg_lens = NULL;
	int seg, len;

	sq->sg[sq->head].num_segs = 0;

	for (seg = 0; seg < num_segs; seg++) {
		if ((seg % MAX_SEGS_PER_SG) == 0) {
			sg = (struct nix_sqe_sg_s *)(sq->sqe_base + *offset);
			sg->ld_type = NIX_SEND_LDTYPE_LDD;
			sg->subdc = NIX_SUBDC_SG;
			sg->segs = 0;
			sg_lens = (void *)sg;
			iova = (void *)sg + sizeof(*sg);
			/* Next subdc always starts at a 16byte boundary.
			 * So if sg->segs is whether 2 or 3, offset += 16bytes.
			 */
			if ((num_segs - seg) >= (MAX_SEGS_PER_SG - 1))
				*offset += sizeof(*sg) + (3 * sizeof(u64));
			else
				*offset += sizeof(*sg) + sizeof(u64);
		}
		dma_addr = otx2_dma_map_skb_frag(pfvf, skb, seg, &len);
		if (dma_mapping_error(pfvf->dev, dma_addr))
			return false;

		sg_lens[frag_num(seg % MAX_SEGS_PER_SG)] = len;
		sg->segs++;
		*iova++ = dma_addr;

		/* Save DMA mapping info for later unmapping */
		sq->sg[sq->head].dma_addr[seg] = dma_addr;
		sq->sg[sq->head].size[seg] = len;
		sq->sg[sq->head].num_segs++;
	}

	sq->sg[sq->head].skb = (u64)skb;
	return true;
}

/* Add SQE header subdescriptor structure */
static void otx2_sqe_add_hdr(struct otx2_nic *pfvf, struct otx2_snd_queue *sq,
			     struct nix_sqe_hdr_s *sqe_hdr,
			     struct sk_buff *skb, u16 qidx)
{
	int proto = 0;

	/* Check if SQE was framed before, if yes then no need to
	 * set these constants again and again.
	 */
	if (!sqe_hdr->total) {
		/* Don't free Tx buffers to Aura */
		sqe_hdr->df = 1;
		sqe_hdr->aura = sq->aura_id;
		/* Post a CQE Tx after pkt transmission */
		sqe_hdr->pnc = 1;
		sqe_hdr->sq = qidx;
	}
	sqe_hdr->total = skb->len;
	/* Set SQE identifier which will be used later for freeing SKB */
	sqe_hdr->sqe_id = sq->head;

	/* Offload TCP/UDP checksum to HW */
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		sqe_hdr->ol3ptr = skb_network_offset(skb);
		sqe_hdr->ol4ptr = skb_transport_offset(skb);
		/* get vlan protocol Ethertype */
		if (eth_type_vlan(skb->protocol))
			skb->protocol = vlan_get_protocol(skb);

		if (skb->protocol == htons(ETH_P_IP)) {
			proto = ip_hdr(skb)->protocol;
			/* In case of TSO, HW needs this to be explicitly set.
			 * So set this always, instead of adding a check.
			 */
			sqe_hdr->ol3type = NIX_SENDL3TYPE_IP4_CKSUM;
		} else if (skb->protocol == htons(ETH_P_IPV6)) {
			proto = ipv6_hdr(skb)->nexthdr;
		}

		if (proto == IPPROTO_TCP)
			sqe_hdr->ol4type = NIX_SENDL4TYPE_TCP_CKSUM;
		else if (proto == IPPROTO_UDP)
			sqe_hdr->ol4type = NIX_SENDL4TYPE_UDP_CKSUM;
	}
}

bool otx2_sq_append_skb(struct net_device *netdev, struct otx2_snd_queue *sq,
			struct sk_buff *skb, u16 qidx)
{
	struct netdev_queue *txq = netdev_get_tx_queue(netdev, qidx);
	struct otx2_nic *pfvf = netdev_priv(netdev);
	int offset, num_segs, free_sqe;
	struct nix_sqe_hdr_s *sqe_hdr;

	/* Check if there is room for new SQE.
	 * 'Num of SQBs freed to SQ's pool - SQ's Aura count'
	 * will give free SQE count.
	 */
	free_sqe = (sq->num_sqbs - *sq->aura_fc_addr) * sq->sqe_per_sqb;

	if (!free_sqe || free_sqe < sq->sqe_thresh)
		return false;

	num_segs = skb_shinfo(skb)->nr_frags + 1;

	/* If SKB doesn't fit in a single SQE, linearize it.
	 * TODO: Consider adding JUMP descriptor instead.
	 */
	if (unlikely(num_segs > OTX2_MAX_FRAGS_IN_SQE)) {
		if (__skb_linearize(skb)) {
			dev_kfree_skb_any(skb);
			return true;
		}
		num_segs = skb_shinfo(skb)->nr_frags + 1;
	}

	/* Set SQE's SEND_HDR.
	 * Do not clear the first 64bit as it contains constant info.
	 */
	memset(sq->sqe_base + 8, 0, sq->sqe_size - 8);
	sqe_hdr = (struct nix_sqe_hdr_s *)(sq->sqe_base);
	otx2_sqe_add_hdr(pfvf, sq, sqe_hdr, skb, qidx);
	offset = sizeof(*sqe_hdr);

	/* Add SG subdesc with data frags */
	if (!otx2_sqe_add_sg(pfvf, sq, skb, num_segs, &offset)) {
		otx2_dma_unmap_skb_frags(pfvf, &sq->sg[sq->head]);
		return false;
	}

	sqe_hdr->sizem1 = (offset / 16) - 1;

	netdev_tx_sent_queue(txq, skb->len);

	/* Flush SQE to HW */
	otx2_sqe_flush(sq, offset);

	return true;
}

void otx2_cleanup_rx_cqes(struct otx2_nic *pfvf, struct otx2_cq_queue *cq)
{
	struct nix_cqe_rx_s *cqe;
	int processed_cqe = 0;
	u64 iova, pa;

	while ((cqe = (struct nix_cqe_rx_s *)otx2_get_next_cqe(cq))) {
		if (!cqe->sg.subdc)
			continue;
		iova = cqe->sg.seg_addr - OTX2_HEAD_ROOM;
		pa = otx2_iova_to_phys(pfvf->iommu_domain, iova);
		otx2_dma_unmap_page(pfvf, iova, pfvf->rbsize, DMA_FROM_DEVICE);
		put_page(virt_to_page(phys_to_virt(pa)));
		processed_cqe++;
	}

	/* Free CQEs to HW */
	otx2_write64(pfvf, NIX_LF_CQ_OP_DOOR,
		     ((u64)cq->cq_idx << 32) | processed_cqe);
}

void otx2_cleanup_tx_cqes(struct otx2_nic *pfvf, struct otx2_cq_queue *cq)
{
	struct sk_buff *skb = NULL;
	struct otx2_snd_queue *sq;
	struct nix_cqe_tx_s *cqe;
	int processed_cqe = 0;
	struct sg_list *sg;

	sq = &pfvf->qset.sq[cq->cint_idx];

	while ((cqe = (struct nix_cqe_tx_s *)otx2_get_next_cqe(cq))) {
		sg = &sq->sg[cqe->comp.sqe_id];
		skb = (struct sk_buff *)sg->skb;
		if (skb) {
			otx2_dma_unmap_skb_frags(pfvf, sg);
			dev_kfree_skb_any(skb);
			sg->skb = (u64)NULL;
		}
		processed_cqe++;
	}

	/* Free CQEs to HW */
	otx2_write64(pfvf, NIX_LF_CQ_OP_DOOR,
		     ((u64)cq->cq_idx << 32) | processed_cqe);
}
