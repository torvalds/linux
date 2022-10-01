// SPDX-License-Identifier: GPL-2.0
/* Marvell CN10K MCS driver
 *
 * Copyright (C) 2022 Marvell.
 */

#include <linux/types.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "mcs.h"
#include "rvu.h"
#include "lmac_common.h"

int rvu_mbox_handler_mcs_set_lmac_mode(struct rvu *rvu,
				       struct mcs_set_lmac_mode *req,
				       struct msg_rsp *rsp)
{
	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	if (BIT_ULL(req->lmac_id) & mcs->hw->lmac_bmap)
		mcs_set_lmac_mode(mcs, req->lmac_id, req->mode);

	return 0;
}

int rvu_mbox_handler_mcs_get_hw_info(struct rvu *rvu,
				     struct msg_req *req,
				     struct mcs_hw_info *rsp)
{
	struct mcs *mcs;

	if (!rvu->mcs_blk_cnt)
		return MCS_AF_ERR_NOT_MAPPED;

	/* MCS resources are same across all blocks */
	mcs = mcs_get_pdata(0);
	rsp->num_mcs_blks = rvu->mcs_blk_cnt;
	rsp->tcam_entries = mcs->hw->tcam_entries;
	rsp->secy_entries = mcs->hw->secy_entries;
	rsp->sc_entries = mcs->hw->sc_entries;
	rsp->sa_entries = mcs->hw->sa_entries;
	return 0;
}

int rvu_mbox_handler_mcs_port_reset(struct rvu *rvu, struct mcs_port_reset_req *req,
				    struct msg_rsp *rsp)
{
	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	mcs_reset_port(mcs, req->port_id, req->reset);

	return 0;
}

int rvu_mbox_handler_mcs_set_active_lmac(struct rvu *rvu,
					 struct mcs_set_active_lmac *req,
					 struct msg_rsp *rsp)
{
	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);
	if (!mcs)
		return MCS_AF_ERR_NOT_MAPPED;

	mcs->hw->lmac_bmap = req->lmac_bmap;
	mcs_set_lmac_channels(req->mcs_id, req->chan_base);
	return 0;
}

int rvu_mbox_handler_mcs_port_cfg_set(struct rvu *rvu, struct mcs_port_cfg_set_req *req,
				      struct msg_rsp *rsp)
{
	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	if (mcs->hw->lmac_cnt <= req->port_id || !(mcs->hw->lmac_bmap & BIT_ULL(req->port_id)))
		return -EINVAL;

	mcs_set_port_cfg(mcs, req);

	return 0;
}

int rvu_mbox_handler_mcs_port_cfg_get(struct rvu *rvu, struct mcs_port_cfg_get_req *req,
				      struct mcs_port_cfg_get_rsp *rsp)
{
	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	if (mcs->hw->lmac_cnt <= req->port_id || !(mcs->hw->lmac_bmap & BIT_ULL(req->port_id)))
		return -EINVAL;

	mcs_get_port_cfg(mcs, req, rsp);

	return 0;
}

int rvu_mbox_handler_mcs_custom_tag_cfg_get(struct rvu *rvu, struct mcs_custom_tag_cfg_get_req *req,
					    struct mcs_custom_tag_cfg_get_rsp *rsp)
{
	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	mcs_get_custom_tag_cfg(mcs, req, rsp);

	return 0;
}

static void rvu_mcs_set_lmac_bmap(struct rvu *rvu)
{
	struct mcs *mcs = mcs_get_pdata(0);
	unsigned long lmac_bmap;
	int cgx, lmac, port;

	for (port = 0; port < mcs->hw->lmac_cnt; port++) {
		cgx = port / rvu->hw->lmac_per_cgx;
		lmac = port % rvu->hw->lmac_per_cgx;
		if (!is_lmac_valid(rvu_cgx_pdata(cgx, rvu), lmac))
			continue;
		set_bit(port, &lmac_bmap);
	}
	mcs->hw->lmac_bmap = lmac_bmap;
}

int rvu_mcs_init(struct rvu *rvu)
{
	struct rvu_hwinfo *hw = rvu->hw;
	int lmac, err = 0, mcs_id;
	struct mcs *mcs;

	rvu->mcs_blk_cnt = mcs_get_blkcnt();

	if (!rvu->mcs_blk_cnt)
		return 0;

	/* Needed only for CN10K-B */
	if (rvu->mcs_blk_cnt == 1) {
		err = mcs_set_lmac_channels(0, hw->cgx_chan_base);
		if (err)
			return err;
		/* Set active lmacs */
		rvu_mcs_set_lmac_bmap(rvu);
	}

	for (mcs_id = 0; mcs_id < rvu->mcs_blk_cnt; mcs_id++) {
		mcs = mcs_get_pdata(mcs_id);
		for (lmac = 0; lmac < mcs->hw->lmac_cnt; lmac++)
			mcs_set_lmac_mode(mcs, lmac, 0);
	}

	return err;
}
