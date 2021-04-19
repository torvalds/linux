// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Physcial Function ethernet driver
 *
 * Copyright (C) 2020 Marvell.
 */

#include "cn10k.h"
#include "otx2_reg.h"
#include "otx2_struct.h"

static struct dev_hw_ops	otx2_hw_ops = {
	.sq_aq_init = otx2_sq_aq_init,
	.sqe_flush = otx2_sqe_flush,
	.aura_freeptr = otx2_aura_freeptr,
	.refill_pool_ptrs = otx2_refill_pool_ptrs,
};

static struct dev_hw_ops cn10k_hw_ops = {
	.sq_aq_init = cn10k_sq_aq_init,
	.sqe_flush = cn10k_sqe_flush,
	.aura_freeptr = cn10k_aura_freeptr,
	.refill_pool_ptrs = cn10k_refill_pool_ptrs,
};

int cn10k_pf_lmtst_init(struct otx2_nic *pf)
{
	int size, num_lines;
	u64 base;

	if (!test_bit(CN10K_LMTST, &pf->hw.cap_flag)) {
		pf->hw_ops = &otx2_hw_ops;
		return 0;
	}

	pf->hw_ops = &cn10k_hw_ops;
	base = pci_resource_start(pf->pdev, PCI_MBOX_BAR_NUM) +
		       (MBOX_SIZE * (pf->total_vfs + 1));

	size = pci_resource_len(pf->pdev, PCI_MBOX_BAR_NUM) -
	       (MBOX_SIZE * (pf->total_vfs + 1));

	pf->hw.lmt_base = ioremap(base, size);

	if (!pf->hw.lmt_base) {
		dev_err(pf->dev, "Unable to map PF LMTST region\n");
		return -ENOMEM;
	}

	/* FIXME: Get the num of LMTST lines from LMT table */
	pf->tot_lmt_lines = size / LMT_LINE_SIZE;
	num_lines = (pf->tot_lmt_lines - NIX_LMTID_BASE) /
			    pf->hw.tx_queues;
	/* Number of LMT lines per SQ queues */
	pf->nix_lmt_lines = num_lines > 32 ? 32 : num_lines;

	pf->nix_lmt_size = pf->nix_lmt_lines * LMT_LINE_SIZE;
	return 0;
}

int cn10k_vf_lmtst_init(struct otx2_nic *vf)
{
	int size, num_lines;

	if (!test_bit(CN10K_LMTST, &vf->hw.cap_flag)) {
		vf->hw_ops = &otx2_hw_ops;
		return 0;
	}

	vf->hw_ops = &cn10k_hw_ops;
	size = pci_resource_len(vf->pdev, PCI_MBOX_BAR_NUM);
	vf->hw.lmt_base = ioremap_wc(pci_resource_start(vf->pdev,
							PCI_MBOX_BAR_NUM),
				     size);
	if (!vf->hw.lmt_base) {
		dev_err(vf->dev, "Unable to map VF LMTST region\n");
		return -ENOMEM;
	}

	vf->tot_lmt_lines = size / LMT_LINE_SIZE;
	/* LMTST lines per SQ */
	num_lines = (vf->tot_lmt_lines - NIX_LMTID_BASE) /
			    vf->hw.tx_queues;
	vf->nix_lmt_lines = num_lines > 32 ? 32 : num_lines;
	vf->nix_lmt_size = vf->nix_lmt_lines * LMT_LINE_SIZE;
	return 0;
}
EXPORT_SYMBOL(cn10k_vf_lmtst_init);

int cn10k_sq_aq_init(void *dev, u16 qidx, u16 sqb_aura)
{
	struct nix_cn10k_aq_enq_req *aq;
	struct otx2_nic *pfvf = dev;
	struct otx2_snd_queue *sq;

	sq = &pfvf->qset.sq[qidx];
	sq->lmt_addr = (__force u64 *)((u64)pfvf->hw.nix_lmt_base +
			       (qidx * pfvf->nix_lmt_size));

	/* Get memory to put this msg */
	aq = otx2_mbox_alloc_msg_nix_cn10k_aq_enq(&pfvf->mbox);
	if (!aq)
		return -ENOMEM;

	aq->sq.cq = pfvf->hw.rx_queues + qidx;
	aq->sq.max_sqe_size = NIX_MAXSQESZ_W16; /* 128 byte */
	aq->sq.cq_ena = 1;
	aq->sq.ena = 1;
	/* Only one SMQ is allocated, map all SQ's to that SMQ  */
	aq->sq.smq = pfvf->hw.txschq_list[NIX_TXSCH_LVL_SMQ][0];
	/* FIXME: set based on NIX_AF_DWRR_RPM_MTU*/
	aq->sq.smq_rr_weight = pfvf->netdev->mtu;
	aq->sq.default_chan = pfvf->hw.tx_chan_base;
	aq->sq.sqe_stype = NIX_STYPE_STF; /* Cache SQB */
	aq->sq.sqb_aura = sqb_aura;
	aq->sq.sq_int_ena = NIX_SQINT_BITS;
	aq->sq.qint_idx = 0;
	/* Due pipelining impact minimum 2000 unused SQ CQE's
	 * need to maintain to avoid CQ overflow.
	 */
	aq->sq.cq_limit = ((SEND_CQ_SKID * 256) / (pfvf->qset.sqe_cnt));

	/* Fill AQ info */
	aq->qidx = qidx;
	aq->ctype = NIX_AQ_CTYPE_SQ;
	aq->op = NIX_AQ_INSTOP_INIT;

	return otx2_sync_mbox_msg(&pfvf->mbox);
}

#define NPA_MAX_BURST 16
void cn10k_refill_pool_ptrs(void *dev, struct otx2_cq_queue *cq)
{
	struct otx2_nic *pfvf = dev;
	u64 ptrs[NPA_MAX_BURST];
	int num_ptrs = 1;
	dma_addr_t bufptr;

	/* Refill pool with new buffers */
	while (cq->pool_ptrs) {
		if (otx2_alloc_buffer(pfvf, cq, &bufptr)) {
			if (num_ptrs--)
				__cn10k_aura_freeptr(pfvf, cq->cq_idx, ptrs,
						     num_ptrs,
						     cq->rbpool->lmt_addr);
			break;
		}
		cq->pool_ptrs--;
		ptrs[num_ptrs] = (u64)bufptr + OTX2_HEAD_ROOM;
		num_ptrs++;
		if (num_ptrs == NPA_MAX_BURST || cq->pool_ptrs == 0) {
			__cn10k_aura_freeptr(pfvf, cq->cq_idx, ptrs,
					     num_ptrs,
					     cq->rbpool->lmt_addr);
			num_ptrs = 1;
		}
	}
}

void cn10k_sqe_flush(void *dev, struct otx2_snd_queue *sq, int size, int qidx)
{
	struct otx2_nic *pfvf = dev;
	int lmt_id = NIX_LMTID_BASE + (qidx * pfvf->nix_lmt_lines);
	u64 val = 0, tar_addr = 0;

	/* FIXME: val[0:10] LMT_ID.
	 * [12:15] no of LMTST - 1 in the burst.
	 * [19:63] data size of each LMTST in the burst except first.
	 */
	val = (lmt_id & 0x7FF);
	/* Target address for LMTST flush tells HW how many 128bit
	 * words are present.
	 * tar_addr[6:4] size of first LMTST - 1 in units of 128b.
	 */
	tar_addr |= sq->io_addr | (((size / 16) - 1) & 0x7) << 4;
	dma_wmb();
	memcpy(sq->lmt_addr, sq->sqe_base, size);
	cn10k_lmt_flush(val, tar_addr);

	sq->head++;
	sq->head &= (sq->sqe_cnt - 1);
}
