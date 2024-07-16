// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Physical Function ethernet driver
 *
 * Copyright (C) 2023 Marvell.
 *
 */

#include <linux/netdevice.h>
#include <net/tso.h>

#include "cn10k.h"
#include "otx2_reg.h"
#include "otx2_common.h"
#include "otx2_txrx.h"
#include "otx2_struct.h"

#define OTX2_QOS_MAX_LEAF_NODES 16

static void otx2_qos_aura_pool_free(struct otx2_nic *pfvf, int pool_id)
{
	struct otx2_pool *pool;

	if (!pfvf->qset.pool)
		return;

	pool = &pfvf->qset.pool[pool_id];
	qmem_free(pfvf->dev, pool->stack);
	qmem_free(pfvf->dev, pool->fc_addr);
	pool->stack = NULL;
	pool->fc_addr = NULL;
}

static int otx2_qos_sq_aura_pool_init(struct otx2_nic *pfvf, int qidx)
{
	struct otx2_qset *qset = &pfvf->qset;
	int pool_id, stack_pages, num_sqbs;
	struct otx2_hw *hw = &pfvf->hw;
	struct otx2_snd_queue *sq;
	struct otx2_pool *pool;
	dma_addr_t bufptr;
	int err, ptr;
	u64 iova, pa;

	/* Calculate number of SQBs needed.
	 *
	 * For a 128byte SQE, and 4K size SQB, 31 SQEs will fit in one SQB.
	 * Last SQE is used for pointing to next SQB.
	 */
	num_sqbs = (hw->sqb_size / 128) - 1;
	num_sqbs = (qset->sqe_cnt + num_sqbs) / num_sqbs;

	/* Get no of stack pages needed */
	stack_pages =
		(num_sqbs + hw->stack_pg_ptrs - 1) / hw->stack_pg_ptrs;

	pool_id = otx2_get_pool_idx(pfvf, AURA_NIX_SQ, qidx);
	pool = &pfvf->qset.pool[pool_id];

	/* Initialize aura context */
	err = otx2_aura_init(pfvf, pool_id, pool_id, num_sqbs);
	if (err)
		return err;

	/* Initialize pool context */
	err = otx2_pool_init(pfvf, pool_id, stack_pages,
			     num_sqbs, hw->sqb_size);
	if (err)
		goto aura_free;

	/* Flush accumulated messages */
	err = otx2_sync_mbox_msg(&pfvf->mbox);
	if (err)
		goto pool_free;

	/* Allocate pointers and free them to aura/pool */
	sq = &qset->sq[qidx];
	sq->sqb_count = 0;
	sq->sqb_ptrs = kcalloc(num_sqbs, sizeof(*sq->sqb_ptrs), GFP_KERNEL);
	if (!sq->sqb_ptrs) {
		err = -ENOMEM;
		goto pool_free;
	}

	for (ptr = 0; ptr < num_sqbs; ptr++) {
		err = otx2_alloc_rbuf(pfvf, pool, &bufptr);
		if (err)
			goto sqb_free;
		pfvf->hw_ops->aura_freeptr(pfvf, pool_id, bufptr);
		sq->sqb_ptrs[sq->sqb_count++] = (u64)bufptr;
	}

	return 0;

sqb_free:
	while (ptr--) {
		if (!sq->sqb_ptrs[ptr])
			continue;
		iova = sq->sqb_ptrs[ptr];
		pa = otx2_iova_to_phys(pfvf->iommu_domain, iova);
		dma_unmap_page_attrs(pfvf->dev, iova, hw->sqb_size,
				     DMA_FROM_DEVICE,
				     DMA_ATTR_SKIP_CPU_SYNC);
		put_page(virt_to_page(phys_to_virt(pa)));
		otx2_aura_allocptr(pfvf, pool_id);
	}
	sq->sqb_count = 0;
	kfree(sq->sqb_ptrs);
pool_free:
	qmem_free(pfvf->dev, pool->stack);
aura_free:
	qmem_free(pfvf->dev, pool->fc_addr);
	otx2_mbox_reset(&pfvf->mbox.mbox, 0);
	return err;
}

static void otx2_qos_sq_free_sqbs(struct otx2_nic *pfvf, int qidx)
{
	struct otx2_qset *qset = &pfvf->qset;
	struct otx2_hw *hw = &pfvf->hw;
	struct otx2_snd_queue *sq;
	u64 iova, pa;
	int sqb;

	sq = &qset->sq[qidx];
	if (!sq->sqb_ptrs)
		return;
	for (sqb = 0; sqb < sq->sqb_count; sqb++) {
		if (!sq->sqb_ptrs[sqb])
			continue;
		iova = sq->sqb_ptrs[sqb];
		pa = otx2_iova_to_phys(pfvf->iommu_domain, iova);
		dma_unmap_page_attrs(pfvf->dev, iova, hw->sqb_size,
				     DMA_FROM_DEVICE,
				     DMA_ATTR_SKIP_CPU_SYNC);
		put_page(virt_to_page(phys_to_virt(pa)));
	}

	sq->sqb_count = 0;

	sq = &qset->sq[qidx];
	qmem_free(pfvf->dev, sq->sqe);
	qmem_free(pfvf->dev, sq->tso_hdrs);
	kfree(sq->sg);
	kfree(sq->sqb_ptrs);
	qmem_free(pfvf->dev, sq->timestamps);

	memset((void *)sq, 0, sizeof(*sq));
}

/* send queue id */
static void otx2_qos_sqb_flush(struct otx2_nic *pfvf, int qidx)
{
	int sqe_tail, sqe_head;
	u64 incr, *ptr, val;

	ptr = (__force u64 *)otx2_get_regaddr(pfvf, NIX_LF_SQ_OP_STATUS);
	incr = (u64)qidx << 32;
	val = otx2_atomic64_add(incr, ptr);
	sqe_head = (val >> 20) & 0x3F;
	sqe_tail = (val >> 28) & 0x3F;
	if (sqe_head != sqe_tail)
		usleep_range(50, 60);
}

static int otx2_qos_ctx_disable(struct otx2_nic *pfvf, u16 qidx, int aura_id)
{
	struct nix_cn10k_aq_enq_req *cn10k_sq_aq;
	struct npa_aq_enq_req *aura_aq;
	struct npa_aq_enq_req *pool_aq;
	struct nix_aq_enq_req *sq_aq;

	if (test_bit(CN10K_LMTST, &pfvf->hw.cap_flag)) {
		cn10k_sq_aq = otx2_mbox_alloc_msg_nix_cn10k_aq_enq(&pfvf->mbox);
		if (!cn10k_sq_aq)
			return -ENOMEM;
		cn10k_sq_aq->qidx = qidx;
		cn10k_sq_aq->sq.ena = 0;
		cn10k_sq_aq->sq_mask.ena = 1;
		cn10k_sq_aq->ctype = NIX_AQ_CTYPE_SQ;
		cn10k_sq_aq->op = NIX_AQ_INSTOP_WRITE;
	} else {
		sq_aq = otx2_mbox_alloc_msg_nix_aq_enq(&pfvf->mbox);
		if (!sq_aq)
			return -ENOMEM;
		sq_aq->qidx = qidx;
		sq_aq->sq.ena = 0;
		sq_aq->sq_mask.ena = 1;
		sq_aq->ctype = NIX_AQ_CTYPE_SQ;
		sq_aq->op = NIX_AQ_INSTOP_WRITE;
	}

	aura_aq = otx2_mbox_alloc_msg_npa_aq_enq(&pfvf->mbox);
	if (!aura_aq) {
		otx2_mbox_reset(&pfvf->mbox.mbox, 0);
		return -ENOMEM;
	}

	aura_aq->aura_id = aura_id;
	aura_aq->aura.ena = 0;
	aura_aq->aura_mask.ena = 1;
	aura_aq->ctype = NPA_AQ_CTYPE_AURA;
	aura_aq->op = NPA_AQ_INSTOP_WRITE;

	pool_aq = otx2_mbox_alloc_msg_npa_aq_enq(&pfvf->mbox);
	if (!pool_aq) {
		otx2_mbox_reset(&pfvf->mbox.mbox, 0);
		return -ENOMEM;
	}

	pool_aq->aura_id = aura_id;
	pool_aq->pool.ena = 0;
	pool_aq->pool_mask.ena = 1;

	pool_aq->ctype = NPA_AQ_CTYPE_POOL;
	pool_aq->op = NPA_AQ_INSTOP_WRITE;

	return otx2_sync_mbox_msg(&pfvf->mbox);
}

int otx2_qos_enable_sq(struct otx2_nic *pfvf, int qidx, u16 smq)
{
	struct otx2_hw *hw = &pfvf->hw;
	int pool_id, sq_idx, err;

	if (pfvf->flags & OTX2_FLAG_INTF_DOWN)
		return -EPERM;

	sq_idx = hw->non_qos_queues + qidx;

	mutex_lock(&pfvf->mbox.lock);
	err = otx2_qos_sq_aura_pool_init(pfvf, sq_idx);
	if (err)
		goto out;

	pool_id = otx2_get_pool_idx(pfvf, AURA_NIX_SQ, sq_idx);
	pfvf->qos.qid_to_sqmap[qidx] = smq;
	err = otx2_sq_init(pfvf, sq_idx, pool_id);
	if (err)
		goto out;
out:
	mutex_unlock(&pfvf->mbox.lock);
	return err;
}

void otx2_qos_disable_sq(struct otx2_nic *pfvf, int qidx, u16 mdq)
{
	struct otx2_qset *qset = &pfvf->qset;
	struct otx2_hw *hw = &pfvf->hw;
	struct otx2_snd_queue *sq;
	struct otx2_cq_queue *cq;
	int pool_id, sq_idx;

	sq_idx = hw->non_qos_queues + qidx;

	/* If the DOWN flag is set SQs are already freed */
	if (pfvf->flags & OTX2_FLAG_INTF_DOWN)
		return;

	sq = &pfvf->qset.sq[sq_idx];
	if (!sq->sqb_ptrs)
		return;

	if (sq_idx < hw->non_qos_queues ||
	    sq_idx >= otx2_get_total_tx_queues(pfvf)) {
		netdev_err(pfvf->netdev, "Send Queue is not a QoS queue\n");
		return;
	}

	cq = &qset->cq[pfvf->hw.rx_queues + sq_idx];
	pool_id = otx2_get_pool_idx(pfvf, AURA_NIX_SQ, sq_idx);

	otx2_qos_sqb_flush(pfvf, sq_idx);
	otx2_smq_flush(pfvf, otx2_get_smq_idx(pfvf, sq_idx));
	otx2_cleanup_tx_cqes(pfvf, cq);

	mutex_lock(&pfvf->mbox.lock);
	otx2_qos_ctx_disable(pfvf, sq_idx, pool_id);
	mutex_unlock(&pfvf->mbox.lock);

	otx2_qos_sq_free_sqbs(pfvf, sq_idx);
	otx2_qos_aura_pool_free(pfvf, pool_id);
}
