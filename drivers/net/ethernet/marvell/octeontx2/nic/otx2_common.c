// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Ethernet driver
 *
 * Copyright (C) 2020 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/pci.h>

#include "otx2_reg.h"
#include "otx2_common.h"
#include "otx2_struct.h"

int otx2_config_nix(struct otx2_nic *pfvf)
{
	struct nix_lf_alloc_req  *nixlf;
	struct nix_lf_alloc_rsp *rsp;
	int err;

	pfvf->qset.xqe_size = NIX_XQESZ_W16 ? 128 : 512;

	/* Get memory to put this msg */
	nixlf = otx2_mbox_alloc_msg_nix_lf_alloc(&pfvf->mbox);
	if (!nixlf)
		return -ENOMEM;

	/* Set RQ/SQ/CQ counts */
	nixlf->rq_cnt = pfvf->hw.rx_queues;
	nixlf->sq_cnt = pfvf->hw.tx_queues;
	nixlf->cq_cnt = pfvf->qset.cq_cnt;
	nixlf->xqe_sz = NIX_XQESZ_W16;
	/* We don't know absolute NPA LF idx attached.
	 * AF will replace 'RVU_DEFAULT_PF_FUNC' with
	 * NPA LF attached to this RVU PF/VF.
	 */
	nixlf->npa_func = RVU_DEFAULT_PF_FUNC;
	/* Disable alignment pad, enable L2 length check,
	 * enable L4 TCP/UDP checksum verification.
	 */
	nixlf->rx_cfg = BIT_ULL(33) | BIT_ULL(35) | BIT_ULL(37);

	err = otx2_sync_mbox_msg(&pfvf->mbox);
	if (err)
		return err;

	rsp = (struct nix_lf_alloc_rsp *)otx2_mbox_get_rsp(&pfvf->mbox.mbox, 0,
							   &nixlf->hdr);
	if (IS_ERR(rsp))
		return PTR_ERR(rsp);

	if (rsp->qints < 1)
		return -ENXIO;

	return rsp->hdr.rc;
}

int otx2_config_npa(struct otx2_nic *pfvf)
{
	struct otx2_qset *qset = &pfvf->qset;
	struct npa_lf_alloc_req  *npalf;
	struct otx2_hw *hw = &pfvf->hw;
	int aura_cnt;

	/* Pool - Stack of free buffer pointers
	 * Aura - Alloc/frees pointers from/to pool for NIX DMA.
	 */

	if (!hw->pool_cnt)
		return -EINVAL;

	qset->pool = devm_kzalloc(pfvf->dev, sizeof(struct otx2_pool) *
				  hw->pool_cnt, GFP_KERNEL);
	if (!qset->pool)
		return -ENOMEM;

	/* Get memory to put this msg */
	npalf = otx2_mbox_alloc_msg_npa_lf_alloc(&pfvf->mbox);
	if (!npalf)
		return -ENOMEM;

	/* Set aura and pool counts */
	npalf->nr_pools = hw->pool_cnt;
	aura_cnt = ilog2(roundup_pow_of_two(hw->pool_cnt));
	npalf->aura_sz = (aura_cnt >= ilog2(128)) ? (aura_cnt - 6) : 1;

	return otx2_sync_mbox_msg(&pfvf->mbox);
}

int otx2_detach_resources(struct mbox *mbox)
{
	struct rsrc_detach *detach;

	otx2_mbox_lock(mbox);
	detach = otx2_mbox_alloc_msg_detach_resources(mbox);
	if (!detach) {
		otx2_mbox_unlock(mbox);
		return -ENOMEM;
	}

	/* detach all */
	detach->partial = false;

	/* Send detach request to AF */
	otx2_mbox_msg_send(&mbox->mbox, 0);
	otx2_mbox_unlock(mbox);
	return 0;
}

int otx2_attach_npa_nix(struct otx2_nic *pfvf)
{
	struct rsrc_attach *attach;
	struct msg_req *msix;
	int err;

	otx2_mbox_lock(&pfvf->mbox);
	/* Get memory to put this msg */
	attach = otx2_mbox_alloc_msg_attach_resources(&pfvf->mbox);
	if (!attach) {
		otx2_mbox_unlock(&pfvf->mbox);
		return -ENOMEM;
	}

	attach->npalf = true;
	attach->nixlf = true;

	/* Send attach request to AF */
	err = otx2_sync_mbox_msg(&pfvf->mbox);
	if (err) {
		otx2_mbox_unlock(&pfvf->mbox);
		return err;
	}

	/* Get NPA and NIX MSIX vector offsets */
	msix = otx2_mbox_alloc_msg_msix_offset(&pfvf->mbox);
	if (!msix) {
		otx2_mbox_unlock(&pfvf->mbox);
		return -ENOMEM;
	}

	err = otx2_sync_mbox_msg(&pfvf->mbox);
	if (err) {
		otx2_mbox_unlock(&pfvf->mbox);
		return err;
	}
	otx2_mbox_unlock(&pfvf->mbox);

	if (pfvf->hw.npa_msixoff == MSIX_VECTOR_INVALID ||
	    pfvf->hw.nix_msixoff == MSIX_VECTOR_INVALID) {
		dev_err(pfvf->dev,
			"RVUPF: Invalid MSIX vector offset for NPA/NIX\n");
		return -EINVAL;
	}

	return 0;
}

/* Mbox message handlers */
void mbox_handler_npa_lf_alloc(struct otx2_nic *pfvf,
			       struct npa_lf_alloc_rsp *rsp)
{
	pfvf->hw.stack_pg_ptrs = rsp->stack_pg_ptrs;
	pfvf->hw.stack_pg_bytes = rsp->stack_pg_bytes;
}

void mbox_handler_nix_lf_alloc(struct otx2_nic *pfvf,
			       struct nix_lf_alloc_rsp *rsp)
{
	pfvf->hw.sqb_size = rsp->sqb_size;
	pfvf->hw.rx_chan_base = rsp->rx_chan_base;
	pfvf->hw.tx_chan_base = rsp->tx_chan_base;
}

void mbox_handler_msix_offset(struct otx2_nic *pfvf,
			      struct msix_offset_rsp *rsp)
{
	pfvf->hw.npa_msixoff = rsp->npa_msixoff;
	pfvf->hw.nix_msixoff = rsp->nix_msixoff;
}

#define M(_name, _id, _fn_name, _req_type, _rsp_type)			\
int __weak								\
otx2_mbox_up_handler_ ## _fn_name(struct otx2_nic *pfvf,		\
				struct _req_type *req,			\
				struct _rsp_type *rsp)			\
{									\
	/* Nothing to do here */					\
	return 0;							\
}									\
EXPORT_SYMBOL(otx2_mbox_up_handler_ ## _fn_name);
MBOX_UP_CGX_MESSAGES
#undef M
