/*
 * cxgb4_uld.c:Chelsio Upper Layer Driver Interface for T4/T5/T6 SGE management
 *
 * Copyright (c) 2016 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *  Written by: Atul Gupta (atul.gupta@chelsio.com)
 *  Written by: Hariprasad Shenai (hariprasad@chelsio.com)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/debugfs.h>
#include <linux/export.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/pci.h>

#include "cxgb4.h"
#include "cxgb4_uld.h"
#include "t4_regs.h"
#include "t4fw_api.h"
#include "t4_msg.h"

#define for_each_uldrxq(m, i) for (i = 0; i < ((m)->nrxq + (m)->nciq); i++)

static int get_msix_idx_from_bmap(struct adapter *adap)
{
	struct uld_msix_bmap *bmap = &adap->msix_bmap_ulds;
	unsigned long flags;
	unsigned int msix_idx;

	spin_lock_irqsave(&bmap->lock, flags);
	msix_idx = find_first_zero_bit(bmap->msix_bmap, bmap->mapsize);
	if (msix_idx < bmap->mapsize) {
		__set_bit(msix_idx, bmap->msix_bmap);
	} else {
		spin_unlock_irqrestore(&bmap->lock, flags);
		return -ENOSPC;
	}

	spin_unlock_irqrestore(&bmap->lock, flags);
	return msix_idx;
}

static void free_msix_idx_in_bmap(struct adapter *adap, unsigned int msix_idx)
{
	struct uld_msix_bmap *bmap = &adap->msix_bmap_ulds;
	unsigned long flags;

	spin_lock_irqsave(&bmap->lock, flags);
	 __clear_bit(msix_idx, bmap->msix_bmap);
	spin_unlock_irqrestore(&bmap->lock, flags);
}

/* Flush the aggregated lro sessions */
static void uldrx_flush_handler(struct sge_rspq *q)
{
	struct adapter *adap = q->adap;

	if (adap->uld[q->uld].lro_flush)
		adap->uld[q->uld].lro_flush(&q->lro_mgr);
}

/**
 *	uldrx_handler - response queue handler for ULD queues
 *	@q: the response queue that received the packet
 *	@rsp: the response queue descriptor holding the offload message
 *	@gl: the gather list of packet fragments
 *
 *	Deliver an ingress offload packet to a ULD.  All processing is done by
 *	the ULD, we just maintain statistics.
 */
static int uldrx_handler(struct sge_rspq *q, const __be64 *rsp,
			 const struct pkt_gl *gl)
{
	struct adapter *adap = q->adap;
	struct sge_ofld_rxq *rxq = container_of(q, struct sge_ofld_rxq, rspq);
	int ret;

	/* FW can send CPLs encapsulated in a CPL_FW4_MSG */
	if (((const struct rss_header *)rsp)->opcode == CPL_FW4_MSG &&
	    ((const struct cpl_fw4_msg *)(rsp + 1))->type == FW_TYPE_RSSCPL)
		rsp += 2;

	if (q->flush_handler)
		ret = adap->uld[q->uld].lro_rx_handler(adap->uld[q->uld].handle,
				rsp, gl, &q->lro_mgr,
				&q->napi);
	else
		ret = adap->uld[q->uld].rx_handler(adap->uld[q->uld].handle,
				rsp, gl);

	if (ret) {
		rxq->stats.nomem++;
		return -1;
	}

	if (!gl)
		rxq->stats.imm++;
	else if (gl == CXGB4_MSG_AN)
		rxq->stats.an++;
	else
		rxq->stats.pkts++;
	return 0;
}

static int alloc_uld_rxqs(struct adapter *adap,
			  struct sge_uld_rxq_info *rxq_info, bool lro)
{
	unsigned int nq = rxq_info->nrxq + rxq_info->nciq;
	int i, err, msi_idx, que_idx = 0, bmap_idx = 0;
	struct sge_ofld_rxq *q = rxq_info->uldrxq;
	unsigned short *ids = rxq_info->rspq_id;
	struct sge *s = &adap->sge;
	unsigned int per_chan;

	per_chan = rxq_info->nrxq / adap->params.nports;

	if (adap->flags & USING_MSIX)
		msi_idx = 1;
	else
		msi_idx = -((int)s->intrq.abs_id + 1);

	for (i = 0; i < nq; i++, q++) {
		if (i == rxq_info->nrxq) {
			/* start allocation of concentrator queues */
			per_chan = rxq_info->nciq / adap->params.nports;
			que_idx = 0;
		}

		if (msi_idx >= 0) {
			bmap_idx = get_msix_idx_from_bmap(adap);
			if (bmap_idx < 0) {
				err = -ENOSPC;
				goto freeout;
			}
			msi_idx = adap->msix_info_ulds[bmap_idx].idx;
		}
		err = t4_sge_alloc_rxq(adap, &q->rspq, false,
				       adap->port[que_idx++ / per_chan],
				       msi_idx,
				       q->fl.size ? &q->fl : NULL,
				       uldrx_handler,
				       lro ? uldrx_flush_handler : NULL,
				       0);
		if (err)
			goto freeout;
		if (msi_idx >= 0)
			rxq_info->msix_tbl[i] = bmap_idx;
		memset(&q->stats, 0, sizeof(q->stats));
		if (ids)
			ids[i] = q->rspq.abs_id;
	}
	return 0;
freeout:
	q = rxq_info->uldrxq;
	for ( ; i; i--, q++) {
		if (q->rspq.desc)
			free_rspq_fl(adap, &q->rspq,
				     q->fl.size ? &q->fl : NULL);
	}
	return err;
}

static int
setup_sge_queues_uld(struct adapter *adap, unsigned int uld_type, bool lro)
{
	struct sge_uld_rxq_info *rxq_info = adap->sge.uld_rxq_info[uld_type];
	int i, ret = 0;

	if (adap->flags & USING_MSIX) {
		rxq_info->msix_tbl = kcalloc((rxq_info->nrxq + rxq_info->nciq),
					     sizeof(unsigned short),
					     GFP_KERNEL);
		if (!rxq_info->msix_tbl)
			return -ENOMEM;
	}

	ret = !(!alloc_uld_rxqs(adap, rxq_info, lro));

	/* Tell uP to route control queue completions to rdma rspq */
	if (adap->flags & FULL_INIT_DONE &&
	    !ret && uld_type == CXGB4_ULD_RDMA) {
		struct sge *s = &adap->sge;
		unsigned int cmplqid;
		u32 param, cmdop;

		cmdop = FW_PARAMS_PARAM_DMAQ_EQ_CMPLIQID_CTRL;
		for_each_port(adap, i) {
			cmplqid = rxq_info->uldrxq[i].rspq.cntxt_id;
			param = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DMAQ) |
				 FW_PARAMS_PARAM_X_V(cmdop) |
				 FW_PARAMS_PARAM_YZ_V(s->ctrlq[i].q.cntxt_id));
			ret = t4_set_params(adap, adap->mbox, adap->pf,
					    0, 1, &param, &cmplqid);
		}
	}
	return ret;
}

static void t4_free_uld_rxqs(struct adapter *adap, int n,
			     struct sge_ofld_rxq *q)
{
	for ( ; n; n--, q++) {
		if (q->rspq.desc)
			free_rspq_fl(adap, &q->rspq,
				     q->fl.size ? &q->fl : NULL);
	}
}

static void free_sge_queues_uld(struct adapter *adap, unsigned int uld_type)
{
	struct sge_uld_rxq_info *rxq_info = adap->sge.uld_rxq_info[uld_type];

	if (adap->flags & FULL_INIT_DONE && uld_type == CXGB4_ULD_RDMA) {
		struct sge *s = &adap->sge;
		u32 param, cmdop, cmplqid = 0;
		int i;

		cmdop = FW_PARAMS_PARAM_DMAQ_EQ_CMPLIQID_CTRL;
		for_each_port(adap, i) {
			param = (FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DMAQ) |
				 FW_PARAMS_PARAM_X_V(cmdop) |
				 FW_PARAMS_PARAM_YZ_V(s->ctrlq[i].q.cntxt_id));
			t4_set_params(adap, adap->mbox, adap->pf,
				      0, 1, &param, &cmplqid);
		}
	}

	if (rxq_info->nciq)
		t4_free_uld_rxqs(adap, rxq_info->nciq,
				 rxq_info->uldrxq + rxq_info->nrxq);
	t4_free_uld_rxqs(adap, rxq_info->nrxq, rxq_info->uldrxq);
	if (adap->flags & USING_MSIX)
		kfree(rxq_info->msix_tbl);
}

static int cfg_queues_uld(struct adapter *adap, unsigned int uld_type,
			  const struct cxgb4_uld_info *uld_info)
{
	struct sge *s = &adap->sge;
	struct sge_uld_rxq_info *rxq_info;
	int i, nrxq, ciq_size;

	rxq_info = kzalloc(sizeof(*rxq_info), GFP_KERNEL);
	if (!rxq_info)
		return -ENOMEM;

	if (adap->flags & USING_MSIX && uld_info->nrxq > s->nqs_per_uld) {
		i = s->nqs_per_uld;
		rxq_info->nrxq = roundup(i, adap->params.nports);
	} else {
		i = min_t(int, uld_info->nrxq,
			  num_online_cpus());
		rxq_info->nrxq = roundup(i, adap->params.nports);
	}
	if (!uld_info->ciq) {
		rxq_info->nciq = 0;
	} else  {
		if (adap->flags & USING_MSIX)
			rxq_info->nciq = min_t(int, s->nqs_per_uld,
					       num_online_cpus());
		else
			rxq_info->nciq = min_t(int, MAX_OFLD_QSETS,
					       num_online_cpus());
		rxq_info->nciq = ((rxq_info->nciq / adap->params.nports) *
				  adap->params.nports);
		rxq_info->nciq = max_t(int, rxq_info->nciq,
				       adap->params.nports);
	}

	nrxq = rxq_info->nrxq + rxq_info->nciq; /* total rxq's */
	rxq_info->uldrxq = kcalloc(nrxq, sizeof(struct sge_ofld_rxq),
				   GFP_KERNEL);
	if (!rxq_info->uldrxq) {
		kfree(rxq_info);
		return -ENOMEM;
	}

	rxq_info->rspq_id = kcalloc(nrxq, sizeof(unsigned short), GFP_KERNEL);
	if (!rxq_info->rspq_id) {
		kfree(rxq_info->uldrxq);
		kfree(rxq_info);
		return -ENOMEM;
	}

	for (i = 0; i < rxq_info->nrxq; i++) {
		struct sge_ofld_rxq *r = &rxq_info->uldrxq[i];

		init_rspq(adap, &r->rspq, 5, 1, uld_info->rxq_size, 64);
		r->rspq.uld = uld_type;
		r->fl.size = 72;
	}

	ciq_size = 64 + adap->vres.cq.size + adap->tids.nftids;
	if (ciq_size > SGE_MAX_IQ_SIZE) {
		dev_warn(adap->pdev_dev, "CIQ size too small for available IQs\n");
		ciq_size = SGE_MAX_IQ_SIZE;
	}

	for (i = rxq_info->nrxq; i < nrxq; i++) {
		struct sge_ofld_rxq *r = &rxq_info->uldrxq[i];

		init_rspq(adap, &r->rspq, 5, 1, ciq_size, 64);
		r->rspq.uld = uld_type;
	}

	memcpy(rxq_info->name, uld_info->name, IFNAMSIZ);
	adap->sge.uld_rxq_info[uld_type] = rxq_info;

	return 0;
}

static void free_queues_uld(struct adapter *adap, unsigned int uld_type)
{
	struct sge_uld_rxq_info *rxq_info = adap->sge.uld_rxq_info[uld_type];

	adap->sge.uld_rxq_info[uld_type] = NULL;
	kfree(rxq_info->rspq_id);
	kfree(rxq_info->uldrxq);
	kfree(rxq_info);
}

static int
request_msix_queue_irqs_uld(struct adapter *adap, unsigned int uld_type)
{
	struct sge_uld_rxq_info *rxq_info = adap->sge.uld_rxq_info[uld_type];
	int err = 0;
	unsigned int idx, bmap_idx;

	for_each_uldrxq(rxq_info, idx) {
		bmap_idx = rxq_info->msix_tbl[idx];
		err = request_irq(adap->msix_info_ulds[bmap_idx].vec,
				  t4_sge_intr_msix, 0,
				  adap->msix_info_ulds[bmap_idx].desc,
				  &rxq_info->uldrxq[idx].rspq);
		if (err)
			goto unwind;
	}
	return 0;
unwind:
	while (idx-- > 0) {
		bmap_idx = rxq_info->msix_tbl[idx];
		free_msix_idx_in_bmap(adap, bmap_idx);
		free_irq(adap->msix_info_ulds[bmap_idx].vec,
			 &rxq_info->uldrxq[idx].rspq);
	}
	return err;
}

static void
free_msix_queue_irqs_uld(struct adapter *adap, unsigned int uld_type)
{
	struct sge_uld_rxq_info *rxq_info = adap->sge.uld_rxq_info[uld_type];
	unsigned int idx, bmap_idx;

	for_each_uldrxq(rxq_info, idx) {
		bmap_idx = rxq_info->msix_tbl[idx];

		free_msix_idx_in_bmap(adap, bmap_idx);
		free_irq(adap->msix_info_ulds[bmap_idx].vec,
			 &rxq_info->uldrxq[idx].rspq);
	}
}

static void name_msix_vecs_uld(struct adapter *adap, unsigned int uld_type)
{
	struct sge_uld_rxq_info *rxq_info = adap->sge.uld_rxq_info[uld_type];
	int n = sizeof(adap->msix_info_ulds[0].desc);
	unsigned int idx, bmap_idx;

	for_each_uldrxq(rxq_info, idx) {
		bmap_idx = rxq_info->msix_tbl[idx];

		snprintf(adap->msix_info_ulds[bmap_idx].desc, n, "%s-%s%d",
			 adap->port[0]->name, rxq_info->name, idx);
	}
}

static void enable_rx(struct adapter *adap, struct sge_rspq *q)
{
	if (!q)
		return;

	if (q->handler)
		napi_enable(&q->napi);

	/* 0-increment GTS to start the timer and enable interrupts */
	t4_write_reg(adap, MYPF_REG(SGE_PF_GTS_A),
		     SEINTARM_V(q->intr_params) |
		     INGRESSQID_V(q->cntxt_id));
}

static void quiesce_rx(struct adapter *adap, struct sge_rspq *q)
{
	if (q && q->handler)
		napi_disable(&q->napi);
}

static void enable_rx_uld(struct adapter *adap, unsigned int uld_type)
{
	struct sge_uld_rxq_info *rxq_info = adap->sge.uld_rxq_info[uld_type];
	int idx;

	for_each_uldrxq(rxq_info, idx)
		enable_rx(adap, &rxq_info->uldrxq[idx].rspq);
}

static void quiesce_rx_uld(struct adapter *adap, unsigned int uld_type)
{
	struct sge_uld_rxq_info *rxq_info = adap->sge.uld_rxq_info[uld_type];
	int idx;

	for_each_uldrxq(rxq_info, idx)
		quiesce_rx(adap, &rxq_info->uldrxq[idx].rspq);
}

static void
free_sge_txq_uld(struct adapter *adap, struct sge_uld_txq_info *txq_info)
{
	int nq = txq_info->ntxq;
	int i;

	for (i = 0; i < nq; i++) {
		struct sge_uld_txq *txq = &txq_info->uldtxq[i];

		if (txq && txq->q.desc) {
			tasklet_kill(&txq->qresume_tsk);
			t4_ofld_eq_free(adap, adap->mbox, adap->pf, 0,
					txq->q.cntxt_id);
			free_tx_desc(adap, &txq->q, txq->q.in_use, false);
			kfree(txq->q.sdesc);
			__skb_queue_purge(&txq->sendq);
			free_txq(adap, &txq->q);
		}
	}
}

static int
alloc_sge_txq_uld(struct adapter *adap, struct sge_uld_txq_info *txq_info,
		  unsigned int uld_type)
{
	struct sge *s = &adap->sge;
	int nq = txq_info->ntxq;
	int i, j, err;

	j = nq / adap->params.nports;
	for (i = 0; i < nq; i++) {
		struct sge_uld_txq *txq = &txq_info->uldtxq[i];

		txq->q.size = 1024;
		err = t4_sge_alloc_uld_txq(adap, txq, adap->port[i / j],
					   s->fw_evtq.cntxt_id, uld_type);
		if (err)
			goto freeout;
	}
	return 0;
freeout:
	free_sge_txq_uld(adap, txq_info);
	return err;
}

static void
release_sge_txq_uld(struct adapter *adap, unsigned int uld_type)
{
	struct sge_uld_txq_info *txq_info = NULL;
	int tx_uld_type = TX_ULD(uld_type);

	txq_info = adap->sge.uld_txq_info[tx_uld_type];

	if (txq_info && atomic_dec_and_test(&txq_info->users)) {
		free_sge_txq_uld(adap, txq_info);
		kfree(txq_info->uldtxq);
		kfree(txq_info);
		adap->sge.uld_txq_info[tx_uld_type] = NULL;
	}
}

static int
setup_sge_txq_uld(struct adapter *adap, unsigned int uld_type,
		  const struct cxgb4_uld_info *uld_info)
{
	struct sge_uld_txq_info *txq_info = NULL;
	int tx_uld_type, i;

	tx_uld_type = TX_ULD(uld_type);
	txq_info = adap->sge.uld_txq_info[tx_uld_type];

	if ((tx_uld_type == CXGB4_TX_OFLD) && txq_info &&
	    (atomic_inc_return(&txq_info->users) > 1))
		return 0;

	txq_info = kzalloc(sizeof(*txq_info), GFP_KERNEL);
	if (!txq_info)
		return -ENOMEM;

	i = min_t(int, uld_info->ntxq, num_online_cpus());
	txq_info->ntxq = roundup(i, adap->params.nports);

	txq_info->uldtxq = kcalloc(txq_info->ntxq, sizeof(struct sge_uld_txq),
				   GFP_KERNEL);
	if (!txq_info->uldtxq) {
		kfree(txq_info);
		return -ENOMEM;
	}

	if (alloc_sge_txq_uld(adap, txq_info, tx_uld_type)) {
		kfree(txq_info->uldtxq);
		kfree(txq_info);
		return -ENOMEM;
	}

	atomic_inc(&txq_info->users);
	adap->sge.uld_txq_info[tx_uld_type] = txq_info;
	return 0;
}

static void uld_queue_init(struct adapter *adap, unsigned int uld_type,
			   struct cxgb4_lld_info *lli)
{
	struct sge_uld_rxq_info *rxq_info = adap->sge.uld_rxq_info[uld_type];

	lli->rxq_ids = rxq_info->rspq_id;
	lli->nrxq = rxq_info->nrxq;
	lli->ciq_ids = rxq_info->rspq_id + rxq_info->nrxq;
	lli->nciq = rxq_info->nciq;
}

int t4_uld_mem_alloc(struct adapter *adap)
{
	struct sge *s = &adap->sge;

	adap->uld = kcalloc(CXGB4_ULD_MAX, sizeof(*adap->uld), GFP_KERNEL);
	if (!adap->uld)
		return -ENOMEM;

	s->uld_rxq_info = kcalloc(CXGB4_ULD_MAX,
				  sizeof(struct sge_uld_rxq_info *),
				  GFP_KERNEL);
	if (!s->uld_rxq_info)
		goto err_uld;

	s->uld_txq_info = kcalloc(CXGB4_TX_MAX,
				  sizeof(struct sge_uld_txq_info *),
				  GFP_KERNEL);
	if (!s->uld_txq_info)
		goto err_uld_rx;
	return 0;

err_uld_rx:
	kfree(s->uld_rxq_info);
err_uld:
	kfree(adap->uld);
	return -ENOMEM;
}

void t4_uld_mem_free(struct adapter *adap)
{
	struct sge *s = &adap->sge;

	kfree(s->uld_txq_info);
	kfree(s->uld_rxq_info);
	kfree(adap->uld);
}

/* This function should be called with uld_mutex taken. */
static void cxgb4_shutdown_uld_adapter(struct adapter *adap, enum cxgb4_uld type)
{
	if (adap->uld[type].handle) {
		adap->uld[type].handle = NULL;
		adap->uld[type].add = NULL;
		release_sge_txq_uld(adap, type);

		if (adap->flags & FULL_INIT_DONE)
			quiesce_rx_uld(adap, type);

		if (adap->flags & USING_MSIX)
			free_msix_queue_irqs_uld(adap, type);

		free_sge_queues_uld(adap, type);
		free_queues_uld(adap, type);
	}
}

void t4_uld_clean_up(struct adapter *adap)
{
	unsigned int i;

	mutex_lock(&uld_mutex);
	for (i = 0; i < CXGB4_ULD_MAX; i++) {
		if (!adap->uld[i].handle)
			continue;

		cxgb4_shutdown_uld_adapter(adap, i);
	}
	mutex_unlock(&uld_mutex);
}

static void uld_init(struct adapter *adap, struct cxgb4_lld_info *lld)
{
	int i;

	lld->pdev = adap->pdev;
	lld->pf = adap->pf;
	lld->l2t = adap->l2t;
	lld->tids = &adap->tids;
	lld->ports = adap->port;
	lld->vr = &adap->vres;
	lld->mtus = adap->params.mtus;
	lld->ntxq = adap->sge.ofldqsets;
	lld->nchan = adap->params.nports;
	lld->nports = adap->params.nports;
	lld->wr_cred = adap->params.ofldq_wr_cred;
	lld->crypto = adap->params.crypto;
	lld->iscsi_iolen = MAXRXDATA_G(t4_read_reg(adap, TP_PARA_REG2_A));
	lld->iscsi_tagmask = t4_read_reg(adap, ULP_RX_ISCSI_TAGMASK_A);
	lld->iscsi_pgsz_order = t4_read_reg(adap, ULP_RX_ISCSI_PSZ_A);
	lld->iscsi_llimit = t4_read_reg(adap, ULP_RX_ISCSI_LLIMIT_A);
	lld->iscsi_ppm = &adap->iscsi_ppm;
	lld->adapter_type = adap->params.chip;
	lld->cclk_ps = 1000000000 / adap->params.vpd.cclk;
	lld->udb_density = 1 << adap->params.sge.eq_qpp;
	lld->ucq_density = 1 << adap->params.sge.iq_qpp;
	lld->filt_mode = adap->params.tp.vlan_pri_map;
	/* MODQ_REQ_MAP sets queues 0-3 to chan 0-3 */
	for (i = 0; i < NCHAN; i++)
		lld->tx_modq[i] = i;
	lld->gts_reg = adap->regs + MYPF_REG(SGE_PF_GTS_A);
	lld->db_reg = adap->regs + MYPF_REG(SGE_PF_KDOORBELL_A);
	lld->fw_vers = adap->params.fw_vers;
	lld->dbfifo_int_thresh = dbfifo_int_thresh;
	lld->sge_ingpadboundary = adap->sge.fl_align;
	lld->sge_egrstatuspagesize = adap->sge.stat_len;
	lld->sge_pktshift = adap->sge.pktshift;
	lld->ulp_crypto = adap->params.crypto;
	lld->enable_fw_ofld_conn = adap->flags & FW_OFLD_CONN;
	lld->max_ordird_qp = adap->params.max_ordird_qp;
	lld->max_ird_adapter = adap->params.max_ird_adapter;
	lld->ulptx_memwrite_dsgl = adap->params.ulptx_memwrite_dsgl;
	lld->nodeid = dev_to_node(adap->pdev_dev);
	lld->fr_nsmr_tpte_wr_support = adap->params.fr_nsmr_tpte_wr_support;
	lld->write_w_imm_support = adap->params.write_w_imm_support;
	lld->write_cmpl_support = adap->params.write_cmpl_support;
}

static int uld_attach(struct adapter *adap, unsigned int uld)
{
	struct cxgb4_lld_info lli;
	void *handle;

	uld_init(adap, &lli);
	uld_queue_init(adap, uld, &lli);

	handle = adap->uld[uld].add(&lli);
	if (IS_ERR(handle)) {
		dev_warn(adap->pdev_dev,
			 "could not attach to the %s driver, error %ld\n",
			 adap->uld[uld].name, PTR_ERR(handle));
		return PTR_ERR(handle);
	}

	adap->uld[uld].handle = handle;
	t4_register_netevent_notifier();

	if (adap->flags & FULL_INIT_DONE)
		adap->uld[uld].state_change(handle, CXGB4_STATE_UP);

	return 0;
}

/* cxgb4_register_uld - register an upper-layer driver
 * @type: the ULD type
 * @p: the ULD methods
 *
 * Registers an upper-layer driver with this driver and notifies the ULD
 * about any presently available devices that support its type.  Returns
 * %-EBUSY if a ULD of the same type is already registered.
 */
int cxgb4_register_uld(enum cxgb4_uld type,
		       const struct cxgb4_uld_info *p)
{
	unsigned int adap_idx = 0;
	struct adapter *adap;
	int ret = 0;

	if (type >= CXGB4_ULD_MAX)
		return -EINVAL;

	mutex_lock(&uld_mutex);
	list_for_each_entry(adap, &adapter_list, list_node) {
		if ((type == CXGB4_ULD_CRYPTO && !is_pci_uld(adap)) ||
		    (type != CXGB4_ULD_CRYPTO && !is_offload(adap)))
			continue;
		if (type == CXGB4_ULD_ISCSIT && is_t4(adap->params.chip))
			continue;
		ret = cfg_queues_uld(adap, type, p);
		if (ret)
			goto out;
		ret = setup_sge_queues_uld(adap, type, p->lro);
		if (ret)
			goto free_queues;
		if (adap->flags & USING_MSIX) {
			name_msix_vecs_uld(adap, type);
			ret = request_msix_queue_irqs_uld(adap, type);
			if (ret)
				goto free_rxq;
		}
		if (adap->flags & FULL_INIT_DONE)
			enable_rx_uld(adap, type);
		if (adap->uld[type].add) {
			ret = -EBUSY;
			goto free_irq;
		}
		ret = setup_sge_txq_uld(adap, type, p);
		if (ret)
			goto free_irq;
		adap->uld[type] = *p;
		ret = uld_attach(adap, type);
		if (ret)
			goto free_txq;
		adap_idx++;
	}
	mutex_unlock(&uld_mutex);
	return 0;

free_txq:
	release_sge_txq_uld(adap, type);
free_irq:
	if (adap->flags & FULL_INIT_DONE)
		quiesce_rx_uld(adap, type);
	if (adap->flags & USING_MSIX)
		free_msix_queue_irqs_uld(adap, type);
free_rxq:
	free_sge_queues_uld(adap, type);
free_queues:
	free_queues_uld(adap, type);
out:

	list_for_each_entry(adap, &adapter_list, list_node) {
		if ((type == CXGB4_ULD_CRYPTO && !is_pci_uld(adap)) ||
		    (type != CXGB4_ULD_CRYPTO && !is_offload(adap)))
			continue;
		if (type == CXGB4_ULD_ISCSIT && is_t4(adap->params.chip))
			continue;
		if (!adap_idx)
			break;
		adap->uld[type].handle = NULL;
		adap->uld[type].add = NULL;
		release_sge_txq_uld(adap, type);
		if (adap->flags & FULL_INIT_DONE)
			quiesce_rx_uld(adap, type);
		if (adap->flags & USING_MSIX)
			free_msix_queue_irqs_uld(adap, type);
		free_sge_queues_uld(adap, type);
		free_queues_uld(adap, type);
		adap_idx--;
	}
	mutex_unlock(&uld_mutex);
	return ret;
}
EXPORT_SYMBOL(cxgb4_register_uld);

/**
 *	cxgb4_unregister_uld - unregister an upper-layer driver
 *	@type: the ULD type
 *
 *	Unregisters an existing upper-layer driver.
 */
int cxgb4_unregister_uld(enum cxgb4_uld type)
{
	struct adapter *adap;

	if (type >= CXGB4_ULD_MAX)
		return -EINVAL;

	mutex_lock(&uld_mutex);
	list_for_each_entry(adap, &adapter_list, list_node) {
		if ((type == CXGB4_ULD_CRYPTO && !is_pci_uld(adap)) ||
		    (type != CXGB4_ULD_CRYPTO && !is_offload(adap)))
			continue;
		if (type == CXGB4_ULD_ISCSIT && is_t4(adap->params.chip))
			continue;

		cxgb4_shutdown_uld_adapter(adap, type);
	}
	mutex_unlock(&uld_mutex);

	return 0;
}
EXPORT_SYMBOL(cxgb4_unregister_uld);
