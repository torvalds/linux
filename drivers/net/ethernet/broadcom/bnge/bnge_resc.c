// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>

#include "bnge.h"
#include "bnge_hwrm.h"
#include "bnge_hwrm_lib.h"
#include "bnge_resc.h"

static u16 bnge_num_tx_to_cp(struct bnge_dev *bd, u16 tx)
{
	u16 tcs = bd->num_tc;

	if (!tcs)
		tcs = 1;

	return tx / tcs;
}

static u16 bnge_get_max_func_irqs(struct bnge_dev *bd)
{
	struct bnge_hw_resc *hw_resc = &bd->hw_resc;

	return min_t(u16, hw_resc->max_irqs, hw_resc->max_nqs);
}

static unsigned int bnge_get_max_func_stat_ctxs(struct bnge_dev *bd)
{
	return bd->hw_resc.max_stat_ctxs;
}

static unsigned int bnge_get_max_func_cp_rings(struct bnge_dev *bd)
{
	return bd->hw_resc.max_cp_rings;
}

static int bnge_aux_get_dflt_msix(struct bnge_dev *bd)
{
	int roce_msix = BNGE_MAX_ROCE_MSIX;

	return min_t(int, roce_msix, num_online_cpus() + 1);
}

u16 bnge_aux_get_msix(struct bnge_dev *bd)
{
	if (bnge_is_roce_en(bd))
		return bd->aux_num_msix;

	return 0;
}

static void bnge_aux_set_msix_num(struct bnge_dev *bd, u16 num)
{
	if (bnge_is_roce_en(bd))
		bd->aux_num_msix = num;
}

static u16 bnge_aux_get_stat_ctxs(struct bnge_dev *bd)
{
	if (bnge_is_roce_en(bd))
		return bd->aux_num_stat_ctxs;

	return 0;
}

static void bnge_aux_set_stat_ctxs(struct bnge_dev *bd, u16 num_aux_ctx)
{
	if (bnge_is_roce_en(bd))
		bd->aux_num_stat_ctxs = num_aux_ctx;
}

static u16 bnge_func_stat_ctxs_demand(struct bnge_dev *bd)
{
	return bd->nq_nr_rings + bnge_aux_get_stat_ctxs(bd);
}

static int bnge_get_dflt_aux_stat_ctxs(struct bnge_dev *bd)
{
	int stat_ctx = 0;

	if (bnge_is_roce_en(bd)) {
		stat_ctx = BNGE_MIN_ROCE_STAT_CTXS;

		if (!bd->pf.port_id && bd->port_count > 1)
			stat_ctx++;
	}

	return stat_ctx;
}

static u16 bnge_nqs_demand(struct bnge_dev *bd)
{
	return bd->nq_nr_rings + bnge_aux_get_msix(bd);
}

static u16 bnge_cprs_demand(struct bnge_dev *bd)
{
	return bd->tx_nr_rings + bd->rx_nr_rings;
}

static u16 bnge_get_avail_msix(struct bnge_dev *bd, int num)
{
	u16 max_irq = bnge_get_max_func_irqs(bd);
	u16 total_demand = bd->nq_nr_rings + num;

	if (max_irq < total_demand) {
		num = max_irq - bd->nq_nr_rings;
		if (num <= 0)
			return 0;
	}

	return num;
}

static u16 bnge_num_cp_to_tx(struct bnge_dev *bd, u16 tx_chunks)
{
	return tx_chunks * bd->num_tc;
}

int bnge_fix_rings_count(u16 *rx, u16 *tx, u16 max, bool shared)
{
	u16 _rx = *rx, _tx = *tx;

	if (shared) {
		*rx = min_t(u16, _rx, max);
		*tx = min_t(u16, _tx, max);
	} else {
		if (max < 2)
			return -ENOMEM;
		while (_rx + _tx > max) {
			if (_rx > _tx && _rx > 1)
				_rx--;
			else if (_tx > 1)
				_tx--;
		}
		*rx = _rx;
		*tx = _tx;
	}

	return 0;
}

static int bnge_adjust_rings(struct bnge_dev *bd, u16 *rx,
			     u16 *tx, u16 max_nq, bool sh)
{
	u16 tx_chunks = bnge_num_tx_to_cp(bd, *tx);

	if (tx_chunks != *tx) {
		u16 tx_saved = tx_chunks, rc;

		rc = bnge_fix_rings_count(rx, &tx_chunks, max_nq, sh);
		if (rc)
			return rc;
		if (tx_chunks != tx_saved)
			*tx = bnge_num_cp_to_tx(bd, tx_chunks);
		return 0;
	}

	return bnge_fix_rings_count(rx, tx, max_nq, sh);
}

int bnge_cal_nr_rss_ctxs(u16 rx_rings)
{
	if (!rx_rings)
		return 0;

	return bnge_adjust_pow_two(rx_rings - 1,
				   BNGE_RSS_TABLE_ENTRIES);
}

static u16 bnge_rss_ctxs_in_use(struct bnge_dev *bd,
				struct bnge_hw_rings *hwr)
{
	return bnge_cal_nr_rss_ctxs(hwr->grp);
}

static u16 bnge_get_total_vnics(struct bnge_dev *bd, u16 rx_rings)
{
	return 1;
}

u32 bnge_get_rxfh_indir_size(struct bnge_dev *bd)
{
	return bnge_cal_nr_rss_ctxs(bd->rx_nr_rings) *
	       BNGE_RSS_TABLE_ENTRIES;
}

static void bnge_set_dflt_rss_indir_tbl(struct bnge_dev *bd)
{
	u16 max_entries, pad;
	u32 *rss_indir_tbl;
	int i;

	max_entries = bnge_get_rxfh_indir_size(bd);
	rss_indir_tbl = &bd->rss_indir_tbl[0];

	for (i = 0; i < max_entries; i++)
		rss_indir_tbl[i] = ethtool_rxfh_indir_default(i,
							      bd->rx_nr_rings);

	pad = bd->rss_indir_tbl_entries - max_entries;
	if (pad)
		memset(&rss_indir_tbl[i], 0, pad * sizeof(*rss_indir_tbl));
}

static void bnge_copy_reserved_rings(struct bnge_dev *bd,
				     struct bnge_hw_rings *hwr)
{
	struct bnge_hw_resc *hw_resc = &bd->hw_resc;

	hwr->tx = hw_resc->resv_tx_rings;
	hwr->rx = hw_resc->resv_rx_rings;
	hwr->nq = hw_resc->resv_irqs;
	hwr->cmpl = hw_resc->resv_cp_rings;
	hwr->grp = hw_resc->resv_hw_ring_grps;
	hwr->vnic = hw_resc->resv_vnics;
	hwr->stat = hw_resc->resv_stat_ctxs;
	hwr->rss_ctx = hw_resc->resv_rsscos_ctxs;
}

static bool bnge_rings_ok(struct bnge_hw_rings *hwr)
{
	return hwr->tx && hwr->rx && hwr->nq && hwr->grp && hwr->vnic &&
	       hwr->stat && hwr->cmpl;
}

static bool bnge_need_reserve_rings(struct bnge_dev *bd)
{
	struct bnge_hw_resc *hw_resc = &bd->hw_resc;
	u16 cprs = bnge_cprs_demand(bd);
	u16 rx = bd->rx_nr_rings, stat;
	u16 nqs = bnge_nqs_demand(bd);
	u16 vnic;

	if (hw_resc->resv_tx_rings != bd->tx_nr_rings)
		return true;

	vnic = bnge_get_total_vnics(bd, rx);

	if (bnge_is_agg_reqd(bd))
		rx <<= 1;
	stat = bnge_func_stat_ctxs_demand(bd);
	if (hw_resc->resv_rx_rings != rx || hw_resc->resv_cp_rings != cprs ||
	    hw_resc->resv_vnics != vnic || hw_resc->resv_stat_ctxs != stat)
		return true;
	if (hw_resc->resv_irqs != nqs)
		return true;

	return false;
}

int bnge_reserve_rings(struct bnge_dev *bd)
{
	u16 aux_dflt_msix = bnge_aux_get_dflt_msix(bd);
	struct bnge_hw_rings hwr = {0};
	u16 rx_rings, old_rx_rings;
	u16 nq = bd->nq_nr_rings;
	u16 aux_msix = 0;
	bool sh = false;
	u16 tx_cp;
	int rc;

	if (!bnge_need_reserve_rings(bd))
		return 0;

	if (!bnge_aux_registered(bd)) {
		aux_msix = bnge_get_avail_msix(bd, aux_dflt_msix);
		if (!aux_msix)
			bnge_aux_set_stat_ctxs(bd, 0);

		if (aux_msix > aux_dflt_msix)
			aux_msix = aux_dflt_msix;
		hwr.nq = nq + aux_msix;
	} else {
		hwr.nq = bnge_nqs_demand(bd);
	}

	hwr.tx = bd->tx_nr_rings;
	hwr.rx = bd->rx_nr_rings;
	if (bd->flags & BNGE_EN_SHARED_CHNL)
		sh = true;
	hwr.cmpl = hwr.rx + hwr.tx;

	hwr.vnic = bnge_get_total_vnics(bd, hwr.rx);

	if (bnge_is_agg_reqd(bd))
		hwr.rx <<= 1;
	hwr.grp = bd->rx_nr_rings;
	hwr.rss_ctx = bnge_rss_ctxs_in_use(bd, &hwr);
	hwr.stat = bnge_func_stat_ctxs_demand(bd);
	old_rx_rings = bd->hw_resc.resv_rx_rings;

	rc = bnge_hwrm_reserve_rings(bd, &hwr);
	if (rc)
		return rc;

	bnge_copy_reserved_rings(bd, &hwr);

	rx_rings = hwr.rx;
	if (bnge_is_agg_reqd(bd)) {
		if (hwr.rx >= 2)
			rx_rings = hwr.rx >> 1;
		else
			return -ENOMEM;
	}

	rx_rings = min_t(u16, rx_rings, hwr.grp);
	hwr.nq = min_t(u16, hwr.nq, bd->nq_nr_rings);
	if (hwr.stat > bnge_aux_get_stat_ctxs(bd))
		hwr.stat -= bnge_aux_get_stat_ctxs(bd);
	hwr.nq = min_t(u16, hwr.nq, hwr.stat);

	/* Adjust the rings */
	rc = bnge_adjust_rings(bd, &rx_rings, &hwr.tx, hwr.nq, sh);
	if (bnge_is_agg_reqd(bd))
		hwr.rx = rx_rings << 1;
	tx_cp = hwr.tx;
	hwr.nq = sh ? max_t(u16, tx_cp, rx_rings) : tx_cp + rx_rings;
	bd->tx_nr_rings = hwr.tx;

	if (rx_rings != bd->rx_nr_rings)
		dev_warn(bd->dev, "RX rings resv reduced to %d than earlier %d requested\n",
			 rx_rings, bd->rx_nr_rings);

	bd->rx_nr_rings = rx_rings;
	bd->nq_nr_rings = hwr.nq;

	if (!bnge_rings_ok(&hwr))
		return -ENOMEM;

	if (old_rx_rings != bd->hw_resc.resv_rx_rings)
		bnge_set_dflt_rss_indir_tbl(bd);

	if (!bnge_aux_registered(bd)) {
		u16 resv_msix, resv_ctx, aux_ctxs;
		struct bnge_hw_resc *hw_resc;

		hw_resc = &bd->hw_resc;
		resv_msix = hw_resc->resv_irqs - bd->nq_nr_rings;
		aux_msix = min_t(u16, resv_msix, aux_msix);
		bnge_aux_set_msix_num(bd, aux_msix);
		resv_ctx = hw_resc->resv_stat_ctxs  - bd->nq_nr_rings;
		aux_ctxs = min(resv_ctx, bnge_aux_get_stat_ctxs(bd));
		bnge_aux_set_stat_ctxs(bd, aux_ctxs);
	}

	return rc;
}

int bnge_alloc_irqs(struct bnge_dev *bd)
{
	u16 aux_msix, tx_cp, num_entries;
	int i, irqs_demand, rc;
	u16 max, min = 1;

	irqs_demand = bnge_nqs_demand(bd);
	max = bnge_get_max_func_irqs(bd);
	if (irqs_demand > max)
		irqs_demand = max;

	if (!(bd->flags & BNGE_EN_SHARED_CHNL))
		min = 2;

	irqs_demand = pci_alloc_irq_vectors(bd->pdev, min, irqs_demand,
					    PCI_IRQ_MSIX);
	aux_msix = bnge_aux_get_msix(bd);
	if (irqs_demand < 0 || irqs_demand < aux_msix) {
		rc = -ENODEV;
		goto err_free_irqs;
	}

	num_entries = irqs_demand;
	if (pci_msix_can_alloc_dyn(bd->pdev))
		num_entries = max;
	bd->irq_tbl = kcalloc(num_entries, sizeof(*bd->irq_tbl), GFP_KERNEL);
	if (!bd->irq_tbl) {
		rc = -ENOMEM;
		goto err_free_irqs;
	}

	for (i = 0; i < irqs_demand; i++)
		bd->irq_tbl[i].vector = pci_irq_vector(bd->pdev, i);

	bd->irqs_acquired = irqs_demand;
	/* Reduce rings based upon num of vectors allocated.
	 * We dont need to consider NQs as they have been calculated
	 * and must be more than irqs_demand.
	 */
	rc = bnge_adjust_rings(bd, &bd->rx_nr_rings,
			       &bd->tx_nr_rings,
			       irqs_demand - aux_msix, min == 1);
	if (rc)
		goto err_free_irqs;

	tx_cp = bnge_num_tx_to_cp(bd, bd->tx_nr_rings);
	bd->nq_nr_rings = (min == 1) ?
		max_t(u16, tx_cp, bd->rx_nr_rings) :
		tx_cp + bd->rx_nr_rings;

	/* Readjust tx_nr_rings_per_tc */
	if (!bd->num_tc)
		bd->tx_nr_rings_per_tc = bd->tx_nr_rings;

	return 0;

err_free_irqs:
	dev_err(bd->dev, "Failed to allocate IRQs err = %d\n", rc);
	bnge_free_irqs(bd);
	return rc;
}

void bnge_free_irqs(struct bnge_dev *bd)
{
	pci_free_irq_vectors(bd->pdev);
	kfree(bd->irq_tbl);
	bd->irq_tbl = NULL;
}

static void _bnge_get_max_rings(struct bnge_dev *bd, u16 *max_rx,
				u16 *max_tx, u16 *max_nq)
{
	struct bnge_hw_resc *hw_resc = &bd->hw_resc;
	u16 max_ring_grps = 0, max_cp;
	int rc;

	*max_tx = hw_resc->max_tx_rings;
	*max_rx = hw_resc->max_rx_rings;
	*max_nq = min_t(int, bnge_get_max_func_irqs(bd),
			hw_resc->max_stat_ctxs);
	max_ring_grps = hw_resc->max_hw_ring_grps;
	if (bnge_is_agg_reqd(bd))
		*max_rx >>= 1;

	max_cp = bnge_get_max_func_cp_rings(bd);

	/* Fix RX and TX rings according to number of CPs available */
	rc = bnge_fix_rings_count(max_rx, max_tx, max_cp, false);
	if (rc) {
		*max_rx = 0;
		*max_tx = 0;
	}

	*max_rx = min_t(int, *max_rx, max_ring_grps);
}

static int bnge_get_max_rings(struct bnge_dev *bd, u16 *max_rx,
			      u16 *max_tx, bool shared)
{
	u16 rx, tx, nq;

	_bnge_get_max_rings(bd, &rx, &tx, &nq);
	*max_rx = rx;
	*max_tx = tx;
	if (!rx || !tx || !nq)
		return -ENOMEM;

	return bnge_fix_rings_count(max_rx, max_tx, nq, shared);
}

static int bnge_get_dflt_rings(struct bnge_dev *bd, u16 *max_rx, u16 *max_tx,
			       bool shared)
{
	int rc;

	rc = bnge_get_max_rings(bd, max_rx, max_tx, shared);
	if (rc) {
		dev_info(bd->dev, "Not enough rings available\n");
		return rc;
	}

	if (bnge_is_roce_en(bd)) {
		int max_cp, max_stat, max_irq;

		/* Reserve minimum resources for RoCE */
		max_cp = bnge_get_max_func_cp_rings(bd);
		max_stat = bnge_get_max_func_stat_ctxs(bd);
		max_irq = bnge_get_max_func_irqs(bd);
		if (max_cp <= BNGE_MIN_ROCE_CP_RINGS ||
		    max_irq <= BNGE_MIN_ROCE_CP_RINGS ||
		    max_stat <= BNGE_MIN_ROCE_STAT_CTXS)
			return 0;

		max_cp -= BNGE_MIN_ROCE_CP_RINGS;
		max_irq -= BNGE_MIN_ROCE_CP_RINGS;
		max_stat -= BNGE_MIN_ROCE_STAT_CTXS;
		max_cp = min_t(u16, max_cp, max_irq);
		max_cp = min_t(u16, max_cp, max_stat);
		rc = bnge_adjust_rings(bd, max_rx, max_tx, max_cp, shared);
		if (rc)
			rc = 0;
	}

	return rc;
}

/* In initial default shared ring setting, each shared ring must have a
 * RX/TX ring pair.
 */
static void bnge_trim_dflt_sh_rings(struct bnge_dev *bd)
{
	bd->nq_nr_rings = min_t(u16, bd->tx_nr_rings_per_tc, bd->rx_nr_rings);
	bd->rx_nr_rings = bd->nq_nr_rings;
	bd->tx_nr_rings_per_tc = bd->nq_nr_rings;
	bd->tx_nr_rings = bd->tx_nr_rings_per_tc;
}

static int bnge_net_init_dflt_rings(struct bnge_dev *bd, bool sh)
{
	u16 dflt_rings, max_rx_rings, max_tx_rings;
	int rc;

	if (sh)
		bd->flags |= BNGE_EN_SHARED_CHNL;

	dflt_rings = netif_get_num_default_rss_queues();

	rc = bnge_get_dflt_rings(bd, &max_rx_rings, &max_tx_rings, sh);
	if (rc)
		return rc;
	bd->rx_nr_rings = min_t(u16, dflt_rings, max_rx_rings);
	bd->tx_nr_rings_per_tc = min_t(u16, dflt_rings, max_tx_rings);
	if (sh)
		bnge_trim_dflt_sh_rings(bd);
	else
		bd->nq_nr_rings = bd->tx_nr_rings_per_tc + bd->rx_nr_rings;
	bd->tx_nr_rings = bd->tx_nr_rings_per_tc;

	rc = bnge_reserve_rings(bd);
	if (rc && rc != -ENODEV)
		dev_warn(bd->dev, "Unable to reserve tx rings\n");
	bd->tx_nr_rings_per_tc = bd->tx_nr_rings;
	if (sh)
		bnge_trim_dflt_sh_rings(bd);

	/* Rings may have been reduced, re-reserve them again */
	if (bnge_need_reserve_rings(bd)) {
		rc = bnge_reserve_rings(bd);
		if (rc && rc != -ENODEV)
			dev_warn(bd->dev, "Fewer rings reservation failed\n");
		bd->tx_nr_rings_per_tc = bd->tx_nr_rings;
	}
	if (rc) {
		bd->tx_nr_rings = 0;
		bd->rx_nr_rings = 0;
	}

	return rc;
}

static int bnge_alloc_rss_indir_tbl(struct bnge_dev *bd)
{
	u16 entries;

	entries = BNGE_MAX_RSS_TABLE_ENTRIES;

	bd->rss_indir_tbl_entries = entries;
	bd->rss_indir_tbl =
		kmalloc_array(entries, sizeof(*bd->rss_indir_tbl), GFP_KERNEL);
	if (!bd->rss_indir_tbl)
		return -ENOMEM;

	return 0;
}

int bnge_net_init_dflt_config(struct bnge_dev *bd)
{
	struct bnge_hw_resc *hw_resc;
	int rc;

	rc = bnge_alloc_rss_indir_tbl(bd);
	if (rc)
		return rc;

	rc = bnge_net_init_dflt_rings(bd, true);
	if (rc)
		goto err_free_tbl;

	hw_resc = &bd->hw_resc;
	bd->max_fltr = hw_resc->max_rx_em_flows + hw_resc->max_rx_wm_flows +
		       BNGE_L2_FLTR_MAX_FLTR;

	return 0;

err_free_tbl:
	kfree(bd->rss_indir_tbl);
	bd->rss_indir_tbl = NULL;
	return rc;
}

void bnge_net_uninit_dflt_config(struct bnge_dev *bd)
{
	kfree(bd->rss_indir_tbl);
	bd->rss_indir_tbl = NULL;
}

void bnge_aux_init_dflt_config(struct bnge_dev *bd)
{
	bd->aux_num_msix = bnge_aux_get_dflt_msix(bd);
	bd->aux_num_stat_ctxs = bnge_get_dflt_aux_stat_ctxs(bd);
}
