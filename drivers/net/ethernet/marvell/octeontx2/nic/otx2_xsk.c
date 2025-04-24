// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Ethernet driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#include <linux/bpf_trace.h>
#include <linux/stringify.h>
#include <net/xdp_sock_drv.h>
#include <net/xdp.h>

#include "otx2_common.h"
#include "otx2_xsk.h"

int otx2_xsk_pool_alloc_buf(struct otx2_nic *pfvf, struct otx2_pool *pool,
			    dma_addr_t *dma, int idx)
{
	struct xdp_buff *xdp;
	int delta;

	xdp = xsk_buff_alloc(pool->xsk_pool);
	if (!xdp)
		return -ENOMEM;

	pool->xdp[pool->xdp_top++] = xdp;
	*dma = OTX2_DATA_ALIGN(xsk_buff_xdp_get_dma(xdp));
	/* Adjust xdp->data for unaligned addresses */
	delta = *dma - xsk_buff_xdp_get_dma(xdp);
	xdp->data += delta;

	return 0;
}

static int otx2_xsk_ctx_disable(struct otx2_nic *pfvf, u16 qidx, int aura_id)
{
	struct nix_cn10k_aq_enq_req *cn10k_rq_aq;
	struct npa_aq_enq_req *aura_aq;
	struct npa_aq_enq_req *pool_aq;
	struct nix_aq_enq_req *rq_aq;

	if (test_bit(CN10K_LMTST, &pfvf->hw.cap_flag)) {
		cn10k_rq_aq = otx2_mbox_alloc_msg_nix_cn10k_aq_enq(&pfvf->mbox);
		if (!cn10k_rq_aq)
			return -ENOMEM;
		cn10k_rq_aq->qidx = qidx;
		cn10k_rq_aq->rq.ena = 0;
		cn10k_rq_aq->rq_mask.ena = 1;
		cn10k_rq_aq->ctype = NIX_AQ_CTYPE_RQ;
		cn10k_rq_aq->op = NIX_AQ_INSTOP_WRITE;
	} else {
		rq_aq = otx2_mbox_alloc_msg_nix_aq_enq(&pfvf->mbox);
		if (!rq_aq)
			return -ENOMEM;
		rq_aq->qidx = qidx;
		rq_aq->sq.ena = 0;
		rq_aq->sq_mask.ena = 1;
		rq_aq->ctype = NIX_AQ_CTYPE_RQ;
		rq_aq->op = NIX_AQ_INSTOP_WRITE;
	}

	aura_aq = otx2_mbox_alloc_msg_npa_aq_enq(&pfvf->mbox);
	if (!aura_aq)
		goto fail;

	aura_aq->aura_id = aura_id;
	aura_aq->aura.ena = 0;
	aura_aq->aura_mask.ena = 1;
	aura_aq->ctype = NPA_AQ_CTYPE_AURA;
	aura_aq->op = NPA_AQ_INSTOP_WRITE;

	pool_aq = otx2_mbox_alloc_msg_npa_aq_enq(&pfvf->mbox);
	if (!pool_aq)
		goto fail;

	pool_aq->aura_id = aura_id;
	pool_aq->pool.ena = 0;
	pool_aq->pool_mask.ena = 1;

	pool_aq->ctype = NPA_AQ_CTYPE_POOL;
	pool_aq->op = NPA_AQ_INSTOP_WRITE;

	return otx2_sync_mbox_msg(&pfvf->mbox);

fail:
	otx2_mbox_reset(&pfvf->mbox.mbox, 0);
	return -ENOMEM;
}

static void otx2_clean_up_rq(struct otx2_nic *pfvf, int qidx)
{
	struct otx2_qset *qset = &pfvf->qset;
	struct otx2_cq_queue *cq;
	struct otx2_pool *pool;
	u64 iova;

	/* If the DOWN flag is set SQs are already freed */
	if (pfvf->flags & OTX2_FLAG_INTF_DOWN)
		return;

	cq = &qset->cq[qidx];
	if (cq)
		otx2_cleanup_rx_cqes(pfvf, cq, qidx);

	pool = &pfvf->qset.pool[qidx];
	iova = otx2_aura_allocptr(pfvf, qidx);
	while (iova) {
		iova -= OTX2_HEAD_ROOM;
		otx2_free_bufs(pfvf, pool, iova, pfvf->rbsize);
		iova = otx2_aura_allocptr(pfvf, qidx);
	}

	mutex_lock(&pfvf->mbox.lock);
	otx2_xsk_ctx_disable(pfvf, qidx, qidx);
	mutex_unlock(&pfvf->mbox.lock);
}

int otx2_xsk_pool_enable(struct otx2_nic *pf, struct xsk_buff_pool *pool, u16 qidx)
{
	u16 rx_queues = pf->hw.rx_queues;
	u16 tx_queues = pf->hw.tx_queues;
	int err;

	if (qidx >= rx_queues || qidx >= tx_queues)
		return -EINVAL;

	err = xsk_pool_dma_map(pool, pf->dev, DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING);
	if (err)
		return err;

	set_bit(qidx, pf->af_xdp_zc_qidx);
	otx2_clean_up_rq(pf, qidx);
	/* Reconfigure RSS table as 'qidx' cannot be part of RSS now */
	otx2_set_rss_table(pf, DEFAULT_RSS_CONTEXT_GROUP);
	/* Kick start the NAPI context so that receiving will start */
	return otx2_xsk_wakeup(pf->netdev, qidx, XDP_WAKEUP_RX);
}

int otx2_xsk_pool_disable(struct otx2_nic *pf, u16 qidx)
{
	struct net_device *netdev = pf->netdev;
	struct xsk_buff_pool *pool;
	struct otx2_snd_queue *sq;

	pool = xsk_get_pool_from_qid(netdev, qidx);
	if (!pool)
		return -EINVAL;

	sq = &pf->qset.sq[qidx + pf->hw.tx_queues];
	sq->xsk_pool = NULL;
	otx2_clean_up_rq(pf, qidx);
	clear_bit(qidx, pf->af_xdp_zc_qidx);
	xsk_pool_dma_unmap(pool, DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING);
	/* Reconfigure RSS table as 'qidx' now need to be part of RSS now */
	otx2_set_rss_table(pf, DEFAULT_RSS_CONTEXT_GROUP);

	return 0;
}

int otx2_xsk_pool_setup(struct otx2_nic *pf, struct xsk_buff_pool *pool, u16 qidx)
{
	if (pool)
		return otx2_xsk_pool_enable(pf, pool, qidx);

	return otx2_xsk_pool_disable(pf, qidx);
}

int otx2_xsk_wakeup(struct net_device *dev, u32 queue_id, u32 flags)
{
	struct otx2_nic *pf = netdev_priv(dev);
	struct otx2_cq_poll *cq_poll = NULL;
	struct otx2_qset *qset = &pf->qset;

	if (pf->flags & OTX2_FLAG_INTF_DOWN)
		return -ENETDOWN;

	if (queue_id >= pf->hw.rx_queues || queue_id >= pf->hw.tx_queues)
		return -EINVAL;

	cq_poll = &qset->napi[queue_id];
	if (!cq_poll)
		return -EINVAL;

	/* Trigger interrupt */
	if (!napi_if_scheduled_mark_missed(&cq_poll->napi)) {
		otx2_write64(pf, NIX_LF_CINTX_ENA_W1S(cq_poll->cint_idx), BIT_ULL(0));
		otx2_write64(pf, NIX_LF_CINTX_INT_W1S(cq_poll->cint_idx), BIT_ULL(0));
	}

	return 0;
}

void otx2_attach_xsk_buff(struct otx2_nic *pfvf, struct otx2_snd_queue *sq, int qidx)
{
	if (test_bit(qidx, pfvf->af_xdp_zc_qidx))
		sq->xsk_pool = xsk_get_pool_from_qid(pfvf->netdev, qidx);
}

void otx2_zc_napi_handler(struct otx2_nic *pfvf, struct xsk_buff_pool *pool,
			  int queue, int budget)
{
	struct xdp_desc *xdp_desc = pool->tx_descs;
	int err, i, work_done = 0, batch;

	budget = min(budget, otx2_read_free_sqe(pfvf, queue));
	batch = xsk_tx_peek_release_desc_batch(pool, budget);
	if (!batch)
		return;

	for (i = 0; i < batch; i++) {
		dma_addr_t dma_addr;

		dma_addr = xsk_buff_raw_get_dma(pool, xdp_desc[i].addr);
		err = otx2_xdp_sq_append_pkt(pfvf, NULL, dma_addr, xdp_desc[i].len,
					     queue, OTX2_AF_XDP_FRAME);
		if (!err) {
			netdev_err(pfvf->netdev, "AF_XDP: Unable to transfer packet err%d\n", err);
			break;
		}
		work_done++;
	}

	if (work_done)
		xsk_tx_release(pool);
}
