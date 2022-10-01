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

int rvu_mbox_handler_mcs_clear_stats(struct rvu *rvu,
				     struct mcs_clear_stats *req,
				     struct msg_rsp *rsp)
{
	u16 pcifunc = req->hdr.pcifunc;
	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	mutex_lock(&mcs->stats_lock);
	if (req->all)
		mcs_clear_all_stats(mcs, pcifunc, req->dir);
	else
		mcs_clear_stats(mcs, req->type, req->id, req->dir);

	mutex_unlock(&mcs->stats_lock);
	return 0;
}

int rvu_mbox_handler_mcs_get_flowid_stats(struct rvu *rvu,
					  struct mcs_stats_req *req,
					  struct mcs_flowid_stats *rsp)
{
	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	/* In CNF10K-B, before reading the statistics,
	 * MCSX_MIL_GLOBAL.FORCE_CLK_EN_IP needs to be set
	 * to get accurate statistics
	 */
	if (mcs->hw->mcs_blks > 1)
		mcs_set_force_clk_en(mcs, true);

	mutex_lock(&mcs->stats_lock);
	mcs_get_flowid_stats(mcs, rsp, req->id, req->dir);
	mutex_unlock(&mcs->stats_lock);

	/* Clear MCSX_MIL_GLOBAL.FORCE_CLK_EN_IP after reading
	 * the statistics
	 */
	if (mcs->hw->mcs_blks > 1)
		mcs_set_force_clk_en(mcs, false);

	return 0;
}

int rvu_mbox_handler_mcs_get_secy_stats(struct rvu *rvu,
					struct mcs_stats_req *req,
					struct mcs_secy_stats *rsp)
{	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	if (mcs->hw->mcs_blks > 1)
		mcs_set_force_clk_en(mcs, true);

	mutex_lock(&mcs->stats_lock);

	if (req->dir == MCS_RX)
		mcs_get_rx_secy_stats(mcs, rsp, req->id);
	else
		mcs_get_tx_secy_stats(mcs, rsp, req->id);

	mutex_unlock(&mcs->stats_lock);

	if (mcs->hw->mcs_blks > 1)
		mcs_set_force_clk_en(mcs, false);

	return 0;
}

int rvu_mbox_handler_mcs_get_sc_stats(struct rvu *rvu,
				      struct mcs_stats_req *req,
				      struct mcs_sc_stats *rsp)
{
	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	if (mcs->hw->mcs_blks > 1)
		mcs_set_force_clk_en(mcs, true);

	mutex_lock(&mcs->stats_lock);
	mcs_get_sc_stats(mcs, rsp, req->id, req->dir);
	mutex_unlock(&mcs->stats_lock);

	if (mcs->hw->mcs_blks > 1)
		mcs_set_force_clk_en(mcs, false);

	return 0;
}

int rvu_mbox_handler_mcs_get_sa_stats(struct rvu *rvu,
				      struct mcs_stats_req *req,
				      struct mcs_sa_stats *rsp)
{
	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	if (mcs->hw->mcs_blks > 1)
		mcs_set_force_clk_en(mcs, true);

	mutex_lock(&mcs->stats_lock);
	mcs_get_sa_stats(mcs, rsp, req->id, req->dir);
	mutex_unlock(&mcs->stats_lock);

	if (mcs->hw->mcs_blks > 1)
		mcs_set_force_clk_en(mcs, false);

	return 0;
}

int rvu_mbox_handler_mcs_get_port_stats(struct rvu *rvu,
					struct mcs_stats_req *req,
					struct mcs_port_stats *rsp)
{
	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	if (mcs->hw->mcs_blks > 1)
		mcs_set_force_clk_en(mcs, true);

	mutex_lock(&mcs->stats_lock);
	mcs_get_port_stats(mcs, rsp, req->id, req->dir);
	mutex_unlock(&mcs->stats_lock);

	if (mcs->hw->mcs_blks > 1)
		mcs_set_force_clk_en(mcs, false);

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

int rvu_mcs_flr_handler(struct rvu *rvu, u16 pcifunc)
{
	struct mcs *mcs;
	int mcs_id;

	/* CNF10K-B mcs0-6 are mapped to RPM2-8*/
	if (rvu->mcs_blk_cnt > 1) {
		for (mcs_id = 0; mcs_id < rvu->mcs_blk_cnt; mcs_id++) {
			mcs = mcs_get_pdata(mcs_id);
			mcs_free_all_rsrc(mcs, MCS_RX, pcifunc);
			mcs_free_all_rsrc(mcs, MCS_TX, pcifunc);
		}
	} else {
		/* CN10K-B has only one mcs block */
		mcs = mcs_get_pdata(0);
		mcs_free_all_rsrc(mcs, MCS_RX, pcifunc);
		mcs_free_all_rsrc(mcs, MCS_TX, pcifunc);
	}
	return 0;
}

int rvu_mbox_handler_mcs_flowid_ena_entry(struct rvu *rvu,
					  struct mcs_flowid_ena_dis_entry *req,
					  struct msg_rsp *rsp)
{
	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);
	mcs_ena_dis_flowid_entry(mcs, req->flow_id, req->dir, req->ena);
	return 0;
}

int rvu_mbox_handler_mcs_pn_table_write(struct rvu *rvu,
					struct mcs_pn_table_write_req *req,
					struct msg_rsp *rsp)
{
	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);
	mcs_pn_table_write(mcs, req->pn_id, req->next_pn, req->dir);
	return 0;
}

int rvu_mbox_handler_mcs_set_pn_threshold(struct rvu *rvu,
					  struct mcs_set_pn_threshold *req,
					  struct msg_rsp *rsp)
{
	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	mcs_pn_threshold_set(mcs, req);

	return 0;
}

int rvu_mbox_handler_mcs_rx_sc_sa_map_write(struct rvu *rvu,
					    struct mcs_rx_sc_sa_map *req,
					    struct msg_rsp *rsp)
{
	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);
	mcs->mcs_ops->mcs_rx_sa_mem_map_write(mcs, req);
	return 0;
}

int rvu_mbox_handler_mcs_tx_sc_sa_map_write(struct rvu *rvu,
					    struct mcs_tx_sc_sa_map *req,
					    struct msg_rsp *rsp)
{
	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);
	mcs->mcs_ops->mcs_tx_sa_mem_map_write(mcs, req);

	return 0;
}

int rvu_mbox_handler_mcs_sa_plcy_write(struct rvu *rvu,
				       struct mcs_sa_plcy_write_req *req,
				       struct msg_rsp *rsp)
{
	struct mcs *mcs;
	int i;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	for (i = 0; i < req->sa_cnt; i++)
		mcs_sa_plcy_write(mcs, &req->plcy[i][0],
				  req->sa_index[i], req->dir);
	return 0;
}

int rvu_mbox_handler_mcs_rx_sc_cam_write(struct rvu *rvu,
					 struct mcs_rx_sc_cam_write_req *req,
					 struct msg_rsp *rsp)
{
	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);
	mcs_rx_sc_cam_write(mcs, req->sci, req->secy_id, req->sc_id);
	return 0;
}

int rvu_mbox_handler_mcs_secy_plcy_write(struct rvu *rvu,
					 struct mcs_secy_plcy_write_req *req,
					 struct msg_rsp *rsp)
{	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	mcs_secy_plcy_write(mcs, req->plcy,
			    req->secy_id, req->dir);
	return 0;
}

int rvu_mbox_handler_mcs_flowid_entry_write(struct rvu *rvu,
					    struct mcs_flowid_entry_write_req *req,
					    struct msg_rsp *rsp)
{
	struct secy_mem_map map;
	struct mcs *mcs;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	/* TODO validate the flowid */
	mcs_flowid_entry_write(mcs, req->data, req->mask,
			       req->flow_id, req->dir);
	map.secy = req->secy_id;
	map.sc = req->sc_id;
	map.ctrl_pkt = req->ctrl_pkt;
	map.flow_id = req->flow_id;
	map.sci = req->sci;
	mcs->mcs_ops->mcs_flowid_secy_map(mcs, &map, req->dir);
	if (req->ena)
		mcs_ena_dis_flowid_entry(mcs, req->flow_id,
					 req->dir, true);
	return 0;
}

int rvu_mbox_handler_mcs_free_resources(struct rvu *rvu,
					struct mcs_free_rsrc_req *req,
					struct msg_rsp *rsp)
{
	u16 pcifunc = req->hdr.pcifunc;
	struct mcs_rsrc_map *map;
	struct mcs *mcs;
	int rc;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	if (req->dir == MCS_RX)
		map = &mcs->rx;
	else
		map = &mcs->tx;

	mutex_lock(&rvu->rsrc_lock);
	/* Free all the cam resources mapped to PF/VF */
	if (req->all) {
		rc = mcs_free_all_rsrc(mcs, req->dir, pcifunc);
		goto exit;
	}

	switch (req->rsrc_type) {
	case MCS_RSRC_TYPE_FLOWID:
		rc = mcs_free_rsrc(&map->flow_ids, map->flowid2pf_map, req->rsrc_id, pcifunc);
		mcs_ena_dis_flowid_entry(mcs, req->rsrc_id, req->dir, false);
		break;
	case MCS_RSRC_TYPE_SECY:
		rc =  mcs_free_rsrc(&map->secy, map->secy2pf_map, req->rsrc_id, pcifunc);
		mcs_clear_secy_plcy(mcs, req->rsrc_id, req->dir);
		break;
	case MCS_RSRC_TYPE_SC:
		rc = mcs_free_rsrc(&map->sc, map->sc2pf_map, req->rsrc_id, pcifunc);
		/* Disable SC CAM only on RX side */
		if (req->dir == MCS_RX)
			mcs_ena_dis_sc_cam_entry(mcs, req->rsrc_id, false);
		break;
	case MCS_RSRC_TYPE_SA:
		rc = mcs_free_rsrc(&map->sa, map->sa2pf_map, req->rsrc_id, pcifunc);
		break;
	}
exit:
	mutex_unlock(&rvu->rsrc_lock);
	return rc;
}

int rvu_mbox_handler_mcs_alloc_resources(struct rvu *rvu,
					 struct mcs_alloc_rsrc_req *req,
					 struct mcs_alloc_rsrc_rsp *rsp)
{
	u16 pcifunc = req->hdr.pcifunc;
	struct mcs_rsrc_map *map;
	struct mcs *mcs;
	int rsrc_id, i;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	if (req->dir == MCS_RX)
		map = &mcs->rx;
	else
		map = &mcs->tx;

	mutex_lock(&rvu->rsrc_lock);

	if (req->all) {
		rsrc_id = mcs_alloc_all_rsrc(mcs, &rsp->flow_ids[0],
					     &rsp->secy_ids[0],
					     &rsp->sc_ids[0],
					     &rsp->sa_ids[0],
					     &rsp->sa_ids[1],
					     pcifunc, req->dir);
		goto exit;
	}

	switch (req->rsrc_type) {
	case MCS_RSRC_TYPE_FLOWID:
		for (i = 0; i < req->rsrc_cnt; i++) {
			rsrc_id = mcs_alloc_rsrc(&map->flow_ids, map->flowid2pf_map, pcifunc);
			if (rsrc_id < 0)
				goto exit;
			rsp->flow_ids[i] = rsrc_id;
			rsp->rsrc_cnt++;
		}
		break;
	case MCS_RSRC_TYPE_SECY:
		for (i = 0; i < req->rsrc_cnt; i++) {
			rsrc_id = mcs_alloc_rsrc(&map->secy, map->secy2pf_map, pcifunc);
			if (rsrc_id < 0)
				goto exit;
			rsp->secy_ids[i] = rsrc_id;
			rsp->rsrc_cnt++;
		}
		break;
	case MCS_RSRC_TYPE_SC:
		for (i = 0; i < req->rsrc_cnt; i++) {
			rsrc_id = mcs_alloc_rsrc(&map->sc, map->sc2pf_map, pcifunc);
			if (rsrc_id < 0)
				goto exit;
			rsp->sc_ids[i] = rsrc_id;
			rsp->rsrc_cnt++;
		}
		break;
	case MCS_RSRC_TYPE_SA:
		for (i = 0; i < req->rsrc_cnt; i++) {
			rsrc_id = mcs_alloc_rsrc(&map->sa, map->sa2pf_map, pcifunc);
			if (rsrc_id < 0)
				goto exit;
			rsp->sa_ids[i] = rsrc_id;
			rsp->rsrc_cnt++;
		}
		break;
	}

	rsp->rsrc_type = req->rsrc_type;
	rsp->dir = req->dir;
	rsp->mcs_id = req->mcs_id;
	rsp->all = req->all;

exit:
	if (rsrc_id < 0)
		dev_err(rvu->dev, "Failed to allocate the mcs resources for PCIFUNC:%d\n", pcifunc);
	mutex_unlock(&rvu->rsrc_lock);
	return 0;
}

int rvu_mbox_handler_mcs_alloc_ctrl_pkt_rule(struct rvu *rvu,
					     struct mcs_alloc_ctrl_pkt_rule_req *req,
					     struct mcs_alloc_ctrl_pkt_rule_rsp *rsp)
{
	u16 pcifunc = req->hdr.pcifunc;
	struct mcs_rsrc_map *map;
	struct mcs *mcs;
	int rsrc_id;
	u16 offset;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	map = (req->dir == MCS_RX) ? &mcs->rx : &mcs->tx;

	mutex_lock(&rvu->rsrc_lock);

	switch (req->rule_type) {
	case MCS_CTRL_PKT_RULE_TYPE_ETH:
		offset = MCS_CTRLPKT_ETYPE_RULE_OFFSET;
		break;
	case MCS_CTRL_PKT_RULE_TYPE_DA:
		offset = MCS_CTRLPKT_DA_RULE_OFFSET;
		break;
	case MCS_CTRL_PKT_RULE_TYPE_RANGE:
		offset = MCS_CTRLPKT_DA_RANGE_RULE_OFFSET;
		break;
	case MCS_CTRL_PKT_RULE_TYPE_COMBO:
		offset = MCS_CTRLPKT_COMBO_RULE_OFFSET;
		break;
	case MCS_CTRL_PKT_RULE_TYPE_MAC:
		offset = MCS_CTRLPKT_MAC_EN_RULE_OFFSET;
		break;
	}

	rsrc_id = mcs_alloc_ctrlpktrule(&map->ctrlpktrule, map->ctrlpktrule2pf_map, offset,
					pcifunc);
	if (rsrc_id < 0)
		goto exit;

	rsp->rule_idx = rsrc_id;
	rsp->rule_type = req->rule_type;
	rsp->dir = req->dir;
	rsp->mcs_id = req->mcs_id;

	mutex_unlock(&rvu->rsrc_lock);
	return 0;
exit:
	if (rsrc_id < 0)
		dev_err(rvu->dev, "Failed to allocate the mcs ctrl pkt rule for PCIFUNC:%d\n",
			pcifunc);
	mutex_unlock(&rvu->rsrc_lock);
	return rsrc_id;
}

int rvu_mbox_handler_mcs_free_ctrl_pkt_rule(struct rvu *rvu,
					    struct mcs_free_ctrl_pkt_rule_req *req,
					    struct msg_rsp *rsp)
{
	struct mcs *mcs;
	int rc;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	mutex_lock(&rvu->rsrc_lock);

	rc = mcs_free_ctrlpktrule(mcs, req);

	mutex_unlock(&rvu->rsrc_lock);

	return rc;
}

int rvu_mbox_handler_mcs_ctrl_pkt_rule_write(struct rvu *rvu,
					     struct mcs_ctrl_pkt_rule_write_req *req,
					     struct msg_rsp *rsp)
{
	struct mcs *mcs;
	int rc;

	if (req->mcs_id >= rvu->mcs_blk_cnt)
		return MCS_AF_ERR_INVALID_MCSID;

	mcs = mcs_get_pdata(req->mcs_id);

	rc = mcs_ctrlpktrule_write(mcs, req);

	return rc;
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

	/* Install default tcam bypass entry and set port to operational mode */
	for (mcs_id = 0; mcs_id < rvu->mcs_blk_cnt; mcs_id++) {
		mcs = mcs_get_pdata(mcs_id);
		mcs_install_flowid_bypass_entry(mcs);
		for (lmac = 0; lmac < mcs->hw->lmac_cnt; lmac++)
			mcs_set_lmac_mode(mcs, lmac, 0);
	}

	return err;
}
