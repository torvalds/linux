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
			  struct sge_uld_rxq_info *rxq_info,
			  unsigned int nq, unsigned int offset, bool lro)
{
	struct sge *s = &adap->sge;
	struct sge_ofld_rxq *q = rxq_info->uldrxq + offset;
	unsigned short *ids = rxq_info->rspq_id + offset;
	unsigned int per_chan = nq / adap->params.nports;
	unsigned int msi_idx, bmap_idx;
	int i, err;

	if (adap->flags & USING_MSIX)
		msi_idx = 1;
	else
		msi_idx = -((int)s->intrq.abs_id + 1);

	for (i = 0; i < nq; i++, q++) {
		if (msi_idx >= 0) {
			bmap_idx = get_msix_idx_from_bmap(adap);
			adap->msi_idx++;
		}
		err = t4_sge_alloc_rxq(adap, &q->rspq, false,
				       adap->port[i / per_chan],
				       adap->msi_idx,
				       q->fl.size ? &q->fl : NULL,
				       uldrx_handler,
				       NULL,
				       0);
		if (err)
			goto freeout;
		if (msi_idx >= 0)
			rxq_info->msix_tbl[i + offset] = bmap_idx;
		memset(&q->stats, 0, sizeof(q->stats));
		if (ids)
			ids[i] = q->rspq.abs_id;
	}
	return 0;
freeout:
	q = rxq_info->uldrxq + offset;
	for ( ; i; i--, q++) {
		if (q->rspq.desc)
			free_rspq_fl(adap, &q->rspq,
				     q->fl.size ? &q->fl : NULL);
		adap->msi_idx--;
	}

	/* We need to free rxq also in case of ciq allocation failure */
	if (offset) {
		q = rxq_info->uldrxq + offset;
		for ( ; i; i--, q++) {
			if (q->rspq.desc)
				free_rspq_fl(adap, &q->rspq,
					     q->fl.size ? &q->fl : NULL);
			adap->msi_idx--;
		}
	}
	return err;
}

int setup_sge_queues_uld(struct adapter *adap, unsigned int uld_type, bool lro)
{
	struct sge_uld_rxq_info *rxq_info = adap->sge.uld_rxq_info[uld_type];

	if (adap->flags & USING_MSIX) {
		rxq_info->msix_tbl = kzalloc(rxq_info->nrxq + rxq_info->nciq,
					     GFP_KERNEL);
		if (!rxq_info->msix_tbl)
			return -ENOMEM;
	}

	return !(!alloc_uld_rxqs(adap, rxq_info, rxq_info->nrxq, 0, lro) &&
		 !alloc_uld_rxqs(adap, rxq_info, rxq_info->nciq,
				 rxq_info->nrxq, lro));
}

static void t4_free_uld_rxqs(struct adapter *adap, int n,
			     struct sge_ofld_rxq *q)
{
	for ( ; n; n--, q++) {
		if (q->rspq.desc)
			free_rspq_fl(adap, &q->rspq,
				     q->fl.size ? &q->fl : NULL);
		adap->msi_idx--;
	}
}

void free_sge_queues_uld(struct adapter *adap, unsigned int uld_type)
{
	struct sge_uld_rxq_info *rxq_info = adap->sge.uld_rxq_info[uld_type];

	if (rxq_info->nciq)
		t4_free_uld_rxqs(adap, rxq_info->nciq,
				 rxq_info->uldrxq + rxq_info->nrxq);
	t4_free_uld_rxqs(adap, rxq_info->nrxq, rxq_info->uldrxq);
	if (adap->flags & USING_MSIX)
		kfree(rxq_info->msix_tbl);
}

int cfg_queues_uld(struct adapter *adap, unsigned int uld_type,
		   const struct cxgb4_pci_uld_info *uld_info)
{
	struct sge *s = &adap->sge;
	struct sge_uld_rxq_info *rxq_info;
	int i, nrxq;

	rxq_info = kzalloc(sizeof(*rxq_info), GFP_KERNEL);
	if (!rxq_info)
		return -ENOMEM;

	if (uld_info->nrxq > s->nqs_per_uld)
		rxq_info->nrxq = s->nqs_per_uld;
	else
		rxq_info->nrxq = uld_info->nrxq;
	if (!uld_info->nciq)
		rxq_info->nciq = 0;
	else if (uld_info->nciq && uld_info->nciq > s->nqs_per_uld)
		rxq_info->nciq = s->nqs_per_uld;
	else
		rxq_info->nciq = uld_info->nciq;

	nrxq = rxq_info->nrxq + rxq_info->nciq; /* total rxq's */
	rxq_info->uldrxq = kcalloc(nrxq, sizeof(struct sge_ofld_rxq),
				   GFP_KERNEL);
	if (!rxq_info->uldrxq) {
		kfree(rxq_info);
		return -ENOMEM;
	}

	rxq_info->rspq_id = kcalloc(nrxq, sizeof(unsigned short), GFP_KERNEL);
	if (!rxq_info->uldrxq) {
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

	for (i = rxq_info->nrxq; i < nrxq; i++) {
		struct sge_ofld_rxq *r = &rxq_info->uldrxq[i];

		init_rspq(adap, &r->rspq, 5, 1, uld_info->ciq_size, 64);
		r->rspq.uld = uld_type;
		r->fl.size = 72;
	}

	memcpy(rxq_info->name, uld_info->name, IFNAMSIZ);
	adap->sge.uld_rxq_info[uld_type] = rxq_info;

	return 0;
}

void free_queues_uld(struct adapter *adap, unsigned int uld_type)
{
	struct sge_uld_rxq_info *rxq_info = adap->sge.uld_rxq_info[uld_type];

	kfree(rxq_info->rspq_id);
	kfree(rxq_info->uldrxq);
	kfree(rxq_info);
}

int request_msix_queue_irqs_uld(struct adapter *adap, unsigned int uld_type)
{
	struct sge_uld_rxq_info *rxq_info = adap->sge.uld_rxq_info[uld_type];
	int idx, bmap_idx, err = 0;

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
	while (--idx >= 0) {
		bmap_idx = rxq_info->msix_tbl[idx];
		free_msix_idx_in_bmap(adap, bmap_idx);
		free_irq(adap->msix_info_ulds[bmap_idx].vec,
			 &rxq_info->uldrxq[idx].rspq);
	}
	return err;
}

void free_msix_queue_irqs_uld(struct adapter *adap, unsigned int uld_type)
{
	struct sge_uld_rxq_info *rxq_info = adap->sge.uld_rxq_info[uld_type];
	int idx;

	for_each_uldrxq(rxq_info, idx) {
		unsigned int bmap_idx = rxq_info->msix_tbl[idx];

		free_msix_idx_in_bmap(adap, bmap_idx);
		free_irq(adap->msix_info_ulds[bmap_idx].vec,
			 &rxq_info->uldrxq[idx].rspq);
	}
}

void name_msix_vecs_uld(struct adapter *adap, unsigned int uld_type)
{
	struct sge_uld_rxq_info *rxq_info = adap->sge.uld_rxq_info[uld_type];
	int n = sizeof(adap->msix_info_ulds[0].desc);
	int idx;

	for_each_uldrxq(rxq_info, idx) {
		unsigned int bmap_idx = rxq_info->msix_tbl[idx];

		snprintf(adap->msix_info_ulds[bmap_idx].desc, n, "%s-%s%d",
			 adap->port[0]->name, rxq_info->name, idx);
	}
}

static void enable_rx(struct adapter *adap, struct sge_rspq *q)
{
	if (!q)
		return;

	if (q->handler) {
		cxgb_busy_poll_init_lock(q);
		napi_enable(&q->napi);
	}
	/* 0-increment GTS to start the timer and enable interrupts */
	t4_write_reg(adap, MYPF_REG(SGE_PF_GTS_A),
		     SEINTARM_V(q->intr_params) |
		     INGRESSQID_V(q->cntxt_id));
}

static void quiesce_rx(struct adapter *adap, struct sge_rspq *q)
{
	if (q && q->handler) {
		napi_disable(&q->napi);
		local_bh_disable();
		while (!cxgb_poll_lock_napi(q))
			mdelay(1);
		local_bh_enable();
	}
}

void enable_rx_uld(struct adapter *adap, unsigned int uld_type)
{
	struct sge_uld_rxq_info *rxq_info = adap->sge.uld_rxq_info[uld_type];
	int idx;

	for_each_uldrxq(rxq_info, idx)
		enable_rx(adap, &rxq_info->uldrxq[idx].rspq);
}

void quiesce_rx_uld(struct adapter *adap, unsigned int uld_type)
{
	struct sge_uld_rxq_info *rxq_info = adap->sge.uld_rxq_info[uld_type];
	int idx;

	for_each_uldrxq(rxq_info, idx)
		quiesce_rx(adap, &rxq_info->uldrxq[idx].rspq);
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

int uld_mem_alloc(struct adapter *adap)
{
	struct sge *s = &adap->sge;

	adap->uld = kcalloc(adap->num_uld, sizeof(*adap->uld), GFP_KERNEL);
	if (!adap->uld)
		return -ENOMEM;

	s->uld_rxq_info = kzalloc(adap->num_uld *
				  sizeof(struct sge_uld_rxq_info *),
				  GFP_KERNEL);
	if (!s->uld_rxq_info)
		goto err_uld;

	return 0;
err_uld:
	kfree(adap->uld);
	return -ENOMEM;
}

void uld_mem_free(struct adapter *adap)
{
	struct sge *s = &adap->sge;

	kfree(s->uld_rxq_info);
	kfree(adap->uld);
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
	lld->ntxq = adap->sge.iscsiqsets;
	lld->nchan = adap->params.nports;
	lld->nports = adap->params.nports;
	lld->wr_cred = adap->params.ofldq_wr_cred;
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
	lld->enable_fw_ofld_conn = adap->flags & FW_OFLD_CONN;
	lld->max_ordird_qp = adap->params.max_ordird_qp;
	lld->max_ird_adapter = adap->params.max_ird_adapter;
	lld->ulptx_memwrite_dsgl = adap->params.ulptx_memwrite_dsgl;
	lld->nodeid = dev_to_node(adap->pdev_dev);
}

static void uld_attach(struct adapter *adap, unsigned int uld)
{
	void *handle;
	struct cxgb4_lld_info lli;

	uld_init(adap, &lli);
	uld_queue_init(adap, uld, &lli);

	handle = adap->uld[uld].add(&lli);
	if (IS_ERR(handle)) {
		dev_warn(adap->pdev_dev,
			 "could not attach to the %s driver, error %ld\n",
			 adap->uld[uld].name, PTR_ERR(handle));
		return;
	}

	adap->uld[uld].handle = handle;

	if (adap->flags & FULL_INIT_DONE)
		adap->uld[uld].state_change(handle, CXGB4_STATE_UP);
}

int cxgb4_register_pci_uld(enum cxgb4_pci_uld type,
			   struct cxgb4_pci_uld_info *p)
{
	int ret = 0;
	struct adapter *adap;

	if (type >= CXGB4_PCI_ULD_MAX)
		return -EINVAL;

	mutex_lock(&uld_mutex);
	list_for_each_entry(adap, &adapter_list, list_node) {
		if (!is_pci_uld(adap))
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
		adap->uld[type] = *p;
		uld_attach(adap, type);
	}
	mutex_unlock(&uld_mutex);
	return 0;

free_irq:
	if (adap->flags & USING_MSIX)
		free_msix_queue_irqs_uld(adap, type);
free_rxq:
	free_sge_queues_uld(adap, type);
free_queues:
	free_queues_uld(adap, type);
out:
	mutex_unlock(&uld_mutex);
	return ret;
}
EXPORT_SYMBOL(cxgb4_register_pci_uld);

int cxgb4_unregister_pci_uld(enum cxgb4_pci_uld type)
{
	struct adapter *adap;

	if (type >= CXGB4_PCI_ULD_MAX)
		return -EINVAL;

	mutex_lock(&uld_mutex);
	list_for_each_entry(adap, &adapter_list, list_node) {
		if (!is_pci_uld(adap))
			continue;
		adap->uld[type].handle = NULL;
		adap->uld[type].add = NULL;
		if (adap->flags & FULL_INIT_DONE)
			quiesce_rx_uld(adap, type);
		if (adap->flags & USING_MSIX)
			free_msix_queue_irqs_uld(adap, type);
		free_sge_queues_uld(adap, type);
		free_queues_uld(adap, type);
	}
	mutex_unlock(&uld_mutex);

	return 0;
}
EXPORT_SYMBOL(cxgb4_unregister_pci_uld);
