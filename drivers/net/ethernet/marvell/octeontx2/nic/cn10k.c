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

int cn10k_free_all_ipolicers(struct otx2_nic *pfvf)
{
	struct nix_bandprof_free_req *req;
	int rc;

	if (is_dev_otx2(pfvf->pdev))
		return 0;

	mutex_lock(&pfvf->mbox.lock);

	req = otx2_mbox_alloc_msg_nix_bandprof_free(&pfvf->mbox);
	if (!req) {
		rc =  -ENOMEM;
		goto out;
	}

	/* Free all bandwidth profiles allocated */
	req->free_all = true;

	rc = otx2_sync_mbox_msg(&pfvf->mbox);
out:
	mutex_unlock(&pfvf->mbox.lock);
	return rc;
}

int cn10k_alloc_leaf_profile(struct otx2_nic *pfvf, u16 *leaf)
{
	struct nix_bandprof_alloc_req *req;
	struct nix_bandprof_alloc_rsp *rsp;
	int rc;

	req = otx2_mbox_alloc_msg_nix_bandprof_alloc(&pfvf->mbox);
	if (!req)
		return  -ENOMEM;

	req->prof_count[BAND_PROF_LEAF_LAYER] = 1;

	rc = otx2_sync_mbox_msg(&pfvf->mbox);
	if (rc)
		goto out;

	rsp = (struct  nix_bandprof_alloc_rsp *)
	       otx2_mbox_get_rsp(&pfvf->mbox.mbox, 0, &req->hdr);
	if (!rsp->prof_count[BAND_PROF_LEAF_LAYER]) {
		rc = -EIO;
		goto out;
	}

	*leaf = rsp->prof_idx[BAND_PROF_LEAF_LAYER][0];
out:
	if (rc) {
		dev_warn(pfvf->dev,
			 "Failed to allocate ingress bandwidth policer\n");
	}

	return rc;
}

int cn10k_alloc_matchall_ipolicer(struct otx2_nic *pfvf)
{
	struct otx2_hw *hw = &pfvf->hw;
	int ret;

	mutex_lock(&pfvf->mbox.lock);

	ret = cn10k_alloc_leaf_profile(pfvf, &hw->matchall_ipolicer);

	mutex_unlock(&pfvf->mbox.lock);

	return ret;
}

#define POLICER_TIMESTAMP	  1  /* 1 second */
#define MAX_RATE_EXP		  22 /* Valid rate exponent range: 0 - 22 */

static void cn10k_get_ingress_burst_cfg(u32 burst, u32 *burst_exp,
					u32 *burst_mantissa)
{
	int tmp;

	/* Burst is calculated as
	 * (1+[BURST_MANTISSA]/256)*2^[BURST_EXPONENT]
	 * This is the upper limit on number tokens (bytes) that
	 * can be accumulated in the bucket.
	 */
	*burst_exp = ilog2(burst);
	if (burst < 256) {
		/* No float: can't express mantissa in this case */
		*burst_mantissa = 0;
		return;
	}

	if (*burst_exp > MAX_RATE_EXP)
		*burst_exp = MAX_RATE_EXP;

	/* Calculate mantissa
	 * Find remaining bytes 'burst - 2^burst_exp'
	 * mantissa = (remaining bytes) / 2^ (burst_exp - 8)
	 */
	tmp = burst - rounddown_pow_of_two(burst);
	*burst_mantissa = tmp / (1UL << (*burst_exp - 8));
}

static void cn10k_get_ingress_rate_cfg(u64 rate, u32 *rate_exp,
				       u32 *rate_mantissa, u32 *rdiv)
{
	u32 div = 0;
	u32 exp = 0;
	u64 tmp;

	/* Figure out mantissa, exponent and divider from given max pkt rate
	 *
	 * To achieve desired rate HW adds
	 * (1+[RATE_MANTISSA]/256)*2^[RATE_EXPONENT] tokens (bytes) at every
	 * policer timeunit * 2^rdiv ie 2 * 2^rdiv usecs, to the token bucket.
	 * Here policer timeunit is 2 usecs and rate is in bits per sec.
	 * Since floating point cannot be used below algorithm uses 1000000
	 * scale factor to support rates upto 100Gbps.
	 */
	tmp = rate * 32 * 2;
	if (tmp < 256000000) {
		while (tmp < 256000000) {
			tmp = tmp * 2;
			div++;
		}
	} else {
		for (exp = 0; tmp >= 512000000 && exp <= MAX_RATE_EXP; exp++)
			tmp = tmp / 2;

		if (exp > MAX_RATE_EXP)
			exp = MAX_RATE_EXP;
	}

	*rate_mantissa = (tmp - 256000000) / 1000000;
	*rate_exp = exp;
	*rdiv = div;
}

int cn10k_map_unmap_rq_policer(struct otx2_nic *pfvf, int rq_idx,
			       u16 policer, bool map)
{
	struct nix_cn10k_aq_enq_req *aq;

	aq = otx2_mbox_alloc_msg_nix_cn10k_aq_enq(&pfvf->mbox);
	if (!aq)
		return -ENOMEM;

	/* Enable policing and set the bandwidth profile (policer) index */
	if (map)
		aq->rq.policer_ena = 1;
	else
		aq->rq.policer_ena = 0;
	aq->rq_mask.policer_ena = 1;

	aq->rq.band_prof_id = policer;
	aq->rq_mask.band_prof_id = GENMASK(9, 0);

	/* Fill AQ info */
	aq->qidx = rq_idx;
	aq->ctype = NIX_AQ_CTYPE_RQ;
	aq->op = NIX_AQ_INSTOP_WRITE;

	return otx2_sync_mbox_msg(&pfvf->mbox);
}

int cn10k_free_leaf_profile(struct otx2_nic *pfvf, u16 leaf)
{
	struct nix_bandprof_free_req *req;

	req = otx2_mbox_alloc_msg_nix_bandprof_free(&pfvf->mbox);
	if (!req)
		return -ENOMEM;

	req->prof_count[BAND_PROF_LEAF_LAYER] = 1;
	req->prof_idx[BAND_PROF_LEAF_LAYER][0] = leaf;

	return otx2_sync_mbox_msg(&pfvf->mbox);
}

int cn10k_free_matchall_ipolicer(struct otx2_nic *pfvf)
{
	struct otx2_hw *hw = &pfvf->hw;
	int qidx, rc;

	mutex_lock(&pfvf->mbox.lock);

	/* Remove RQ's policer mapping */
	for (qidx = 0; qidx < hw->rx_queues; qidx++)
		cn10k_map_unmap_rq_policer(pfvf, qidx,
					   hw->matchall_ipolicer, false);

	rc = cn10k_free_leaf_profile(pfvf, hw->matchall_ipolicer);

	mutex_unlock(&pfvf->mbox.lock);
	return rc;
}

int cn10k_set_ipolicer_rate(struct otx2_nic *pfvf, u16 profile,
			    u32 burst, u64 rate, bool pps)
{
	struct nix_cn10k_aq_enq_req *aq;
	u32 burst_exp, burst_mantissa;
	u32 rate_exp, rate_mantissa;
	u32 rdiv;

	/* Get exponent and mantissa values for the desired rate */
	cn10k_get_ingress_burst_cfg(burst, &burst_exp, &burst_mantissa);
	cn10k_get_ingress_rate_cfg(rate, &rate_exp, &rate_mantissa, &rdiv);

	/* Init bandwidth profile */
	aq = otx2_mbox_alloc_msg_nix_cn10k_aq_enq(&pfvf->mbox);
	if (!aq)
		return -ENOMEM;

	/* Set initial color mode to blind */
	aq->prof.icolor = 0x03;
	aq->prof_mask.icolor = 0x03;

	/* Set rate and burst values */
	aq->prof.cir_exponent = rate_exp;
	aq->prof_mask.cir_exponent = 0x1F;

	aq->prof.cir_mantissa = rate_mantissa;
	aq->prof_mask.cir_mantissa = 0xFF;

	aq->prof.cbs_exponent = burst_exp;
	aq->prof_mask.cbs_exponent = 0x1F;

	aq->prof.cbs_mantissa = burst_mantissa;
	aq->prof_mask.cbs_mantissa = 0xFF;

	aq->prof.rdiv = rdiv;
	aq->prof_mask.rdiv = 0xF;

	if (pps) {
		/* The amount of decremented tokens is calculated according to
		 * the following equation:
		 * max([ LMODE ? 0 : (packet_length - LXPTR)] +
		 *	     ([ADJUST_MANTISSA]/256 - 1) * 2^[ADJUST_EXPONENT],
		 *	1/256)
		 * if LMODE is 1 then rate limiting will be based on
		 * PPS otherwise bps.
		 * The aim of the ADJUST value is to specify a token cost per
		 * packet in contrary to the packet length that specifies a
		 * cost per byte. To rate limit based on PPS adjust mantissa
		 * is set as 384 and exponent as 1 so that number of tokens
		 * decremented becomes 1 i.e, 1 token per packeet.
		 */
		aq->prof.adjust_exponent = 1;
		aq->prof_mask.adjust_exponent = 0x1F;

		aq->prof.adjust_mantissa = 384;
		aq->prof_mask.adjust_mantissa = 0x1FF;

		aq->prof.lmode = 0x1;
		aq->prof_mask.lmode = 0x1;
	}

	/* Two rate three color marker
	 * With PEIR/EIR set to zero, color will be either green or red
	 */
	aq->prof.meter_algo = 2;
	aq->prof_mask.meter_algo = 0x3;

	aq->prof.rc_action = NIX_RX_BAND_PROF_ACTIONRESULT_DROP;
	aq->prof_mask.rc_action = 0x3;

	aq->prof.yc_action = NIX_RX_BAND_PROF_ACTIONRESULT_PASS;
	aq->prof_mask.yc_action = 0x3;

	aq->prof.gc_action = NIX_RX_BAND_PROF_ACTIONRESULT_PASS;
	aq->prof_mask.gc_action = 0x3;

	/* Setting exponent value as 24 and mantissa as 0 configures
	 * the bucket with zero values making bucket unused. Peak
	 * information rate and Excess information rate buckets are
	 * unused here.
	 */
	aq->prof.peir_exponent = 24;
	aq->prof_mask.peir_exponent = 0x1F;

	aq->prof.peir_mantissa = 0;
	aq->prof_mask.peir_mantissa = 0xFF;

	aq->prof.pebs_exponent = 24;
	aq->prof_mask.pebs_exponent = 0x1F;

	aq->prof.pebs_mantissa = 0;
	aq->prof_mask.pebs_mantissa = 0xFF;

	/* Fill AQ info */
	aq->qidx = profile;
	aq->ctype = NIX_AQ_CTYPE_BANDPROF;
	aq->op = NIX_AQ_INSTOP_WRITE;

	return otx2_sync_mbox_msg(&pfvf->mbox);
}

int cn10k_set_matchall_ipolicer_rate(struct otx2_nic *pfvf,
				     u32 burst, u64 rate)
{
	struct otx2_hw *hw = &pfvf->hw;
	int qidx, rc;

	mutex_lock(&pfvf->mbox.lock);

	rc = cn10k_set_ipolicer_rate(pfvf, hw->matchall_ipolicer, burst,
				     rate, false);
	if (rc)
		goto out;

	for (qidx = 0; qidx < hw->rx_queues; qidx++) {
		rc = cn10k_map_unmap_rq_policer(pfvf, qidx,
						hw->matchall_ipolicer, true);
		if (rc)
			break;
	}

out:
	mutex_unlock(&pfvf->mbox.lock);
	return rc;
}
