// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc. */

#include <linux/kernel.h>

#include <ionic.h>
#include <ionic_lif.h>

#include "ionic_lif_cfg.h"

#define IONIC_MIN_RDMA_VERSION	0
#define IONIC_MAX_RDMA_VERSION	2

static u8 ionic_get_expdb(struct ionic_lif *lif)
{
	u8 expdb_support = 0;

	if (lif->ionic->idev.phy_cmb_expdb64_pages)
		expdb_support |= IONIC_EXPDB_64B_WQE;
	if (lif->ionic->idev.phy_cmb_expdb128_pages)
		expdb_support |= IONIC_EXPDB_128B_WQE;
	if (lif->ionic->idev.phy_cmb_expdb256_pages)
		expdb_support |= IONIC_EXPDB_256B_WQE;
	if (lif->ionic->idev.phy_cmb_expdb512_pages)
		expdb_support |= IONIC_EXPDB_512B_WQE;

	return expdb_support;
}

void ionic_fill_lif_cfg(struct ionic_lif *lif, struct ionic_lif_cfg *cfg)
{
	union ionic_lif_identity *ident = &lif->ionic->ident.lif;

	cfg->lif = lif;
	cfg->hwdev = &lif->ionic->pdev->dev;
	cfg->lif_index = lif->index;
	cfg->lif_hw_index = lif->hw_index;

	cfg->dbid = lif->kern_pid;
	cfg->dbid_count = le32_to_cpu(lif->ionic->ident.dev.ndbpgs_per_lif);
	cfg->dbpage = lif->kern_dbpage;
	cfg->intr_ctrl = lif->ionic->idev.intr_ctrl;

	cfg->db_phys = lif->ionic->bars[IONIC_PCI_BAR_DBELL].bus_addr;

	if (IONIC_VERSION(ident->rdma.version, ident->rdma.minor_version) >=
	    IONIC_VERSION(2, 1))
		cfg->page_size_supported =
		    le64_to_cpu(ident->rdma.page_size_cap);
	else
		cfg->page_size_supported = IONIC_PAGE_SIZE_SUPPORTED;

	cfg->rdma_version = ident->rdma.version;
	cfg->qp_opcodes = ident->rdma.qp_opcodes;
	cfg->admin_opcodes = ident->rdma.admin_opcodes;

	cfg->stats_type = le16_to_cpu(ident->rdma.stats_type);
	cfg->npts_per_lif = le32_to_cpu(ident->rdma.npts_per_lif);
	cfg->nmrs_per_lif = le32_to_cpu(ident->rdma.nmrs_per_lif);
	cfg->nahs_per_lif = le32_to_cpu(ident->rdma.nahs_per_lif);

	cfg->aq_base = le32_to_cpu(ident->rdma.aq_qtype.qid_base);
	cfg->cq_base = le32_to_cpu(ident->rdma.cq_qtype.qid_base);
	cfg->eq_base = le32_to_cpu(ident->rdma.eq_qtype.qid_base);

	/*
	 * ionic_create_rdma_admin() may reduce aq_count or eq_count if
	 * it is unable to allocate all that were requested.
	 * aq_count is tunable; see ionic_aq_count
	 * eq_count is tunable; see ionic_eq_count
	 */
	cfg->aq_count = le32_to_cpu(ident->rdma.aq_qtype.qid_count);
	cfg->eq_count = le32_to_cpu(ident->rdma.eq_qtype.qid_count);
	cfg->cq_count = le32_to_cpu(ident->rdma.cq_qtype.qid_count);
	cfg->qp_count = le32_to_cpu(ident->rdma.sq_qtype.qid_count);
	cfg->dbid_count = le32_to_cpu(lif->ionic->ident.dev.ndbpgs_per_lif);

	cfg->aq_qtype = ident->rdma.aq_qtype.qtype;
	cfg->sq_qtype = ident->rdma.sq_qtype.qtype;
	cfg->rq_qtype = ident->rdma.rq_qtype.qtype;
	cfg->cq_qtype = ident->rdma.cq_qtype.qtype;
	cfg->eq_qtype = ident->rdma.eq_qtype.qtype;
	cfg->udma_qgrp_shift = ident->rdma.udma_shift;
	cfg->udma_count = 2;

	cfg->max_stride = ident->rdma.max_stride;
	cfg->expdb_mask = ionic_get_expdb(lif);

	cfg->sq_expdb =
	    !!(lif->qtype_info[IONIC_QTYPE_TXQ].features & IONIC_QIDENT_F_EXPDB);
	cfg->rq_expdb =
	    !!(lif->qtype_info[IONIC_QTYPE_RXQ].features & IONIC_QIDENT_F_EXPDB);
}

struct net_device *ionic_lif_netdev(struct ionic_lif *lif)
{
	struct net_device *netdev = lif->netdev;

	dev_hold(netdev);
	return netdev;
}

void ionic_lif_fw_version(struct ionic_lif *lif, char *str, size_t len)
{
	strscpy(str, lif->ionic->idev.dev_info.fw_version, len);
}

u8 ionic_lif_asic_rev(struct ionic_lif *lif)
{
	return lif->ionic->idev.dev_info.asic_rev;
}
