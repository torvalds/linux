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
	 /* Nothing to do, for now */
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
