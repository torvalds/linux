// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2024 Marvell.
 *
 */

#include <linux/bitfield.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "rvu.h"
#include "rvu_reg.h"

static u16 rvu_rep_get_vlan_id(struct rvu *rvu, u16 pcifunc)
{
	int id;

	for (id = 0; id < rvu->rep_cnt; id++)
		if (rvu->rep2pfvf_map[id] == pcifunc)
			return id;
	return 0;
}

static int rvu_rep_tx_vlan_cfg(struct rvu *rvu,  u16 pcifunc,
			       u16 vlan_tci, int *vidx)
{
	struct nix_vtag_config_rsp rsp = {};
	struct nix_vtag_config req = {};
	u64 etype = ETH_P_8021Q;
	int err;

	/* Insert vlan tag */
	req.hdr.pcifunc = pcifunc;
	req.vtag_size = VTAGSIZE_T4;
	req.cfg_type = 0; /* tx vlan cfg */
	req.tx.cfg_vtag0 = true;
	req.tx.vtag0 = FIELD_PREP(NIX_VLAN_ETYPE_MASK, etype) | vlan_tci;

	err = rvu_mbox_handler_nix_vtag_cfg(rvu, &req, &rsp);
	if (err) {
		dev_err(rvu->dev, "Tx vlan config failed\n");
		return err;
	}
	*vidx = rsp.vtag0_idx;
	return 0;
}

static int rvu_rep_rx_vlan_cfg(struct rvu *rvu, u16 pcifunc)
{
	struct nix_vtag_config req = {};
	struct nix_vtag_config_rsp rsp;

	/* config strip, capture and size */
	req.hdr.pcifunc = pcifunc;
	req.vtag_size = VTAGSIZE_T4;
	req.cfg_type = 1; /* rx vlan cfg */
	req.rx.vtag_type = NIX_AF_LFX_RX_VTAG_TYPE0;
	req.rx.strip_vtag = true;
	req.rx.capture_vtag = false;

	return rvu_mbox_handler_nix_vtag_cfg(rvu, &req, &rsp);
}

static int rvu_rep_install_rx_rule(struct rvu *rvu, u16 pcifunc,
				   u16 entry, bool rte)
{
	struct npc_install_flow_req req = {};
	struct npc_install_flow_rsp rsp = {};
	struct rvu_pfvf *pfvf;
	u16 vlan_tci, rep_id;

	pfvf = rvu_get_pfvf(rvu, pcifunc);

	/* To steer the traffic from Representee to Representor */
	rep_id = rvu_rep_get_vlan_id(rvu, pcifunc);
	if (rte) {
		vlan_tci = rep_id | BIT_ULL(8);
		req.vf = rvu->rep_pcifunc;
		req.op = NIX_RX_ACTIONOP_UCAST;
		req.index = rep_id;
	} else {
		vlan_tci = rep_id;
		req.vf = pcifunc;
		req.op = NIX_RX_ACTION_DEFAULT;
	}

	rvu_rep_rx_vlan_cfg(rvu, req.vf);
	req.entry = entry;
	req.hdr.pcifunc = 0; /* AF is requester */
	req.features = BIT_ULL(NPC_OUTER_VID) | BIT_ULL(NPC_VLAN_ETYPE_CTAG);
	req.vtag0_valid = true;
	req.vtag0_type = NIX_AF_LFX_RX_VTAG_TYPE0;
	req.packet.vlan_etype = cpu_to_be16(ETH_P_8021Q);
	req.mask.vlan_etype = cpu_to_be16(ETH_P_8021Q);
	req.packet.vlan_tci = cpu_to_be16(vlan_tci);
	req.mask.vlan_tci = cpu_to_be16(0xffff);

	req.channel = RVU_SWITCH_LBK_CHAN;
	req.chan_mask = 0xffff;
	req.intf = pfvf->nix_rx_intf;

	return rvu_mbox_handler_npc_install_flow(rvu, &req, &rsp);
}

static int rvu_rep_install_tx_rule(struct rvu *rvu, u16 pcifunc, u16 entry,
				   bool rte)
{
	struct npc_install_flow_req req = {};
	struct npc_install_flow_rsp rsp = {};
	struct rvu_pfvf *pfvf;
	int vidx, err;
	u16 vlan_tci;
	u8 lbkid;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	vlan_tci = rvu_rep_get_vlan_id(rvu, pcifunc);
	if (rte)
		vlan_tci |= BIT_ULL(8);

	err = rvu_rep_tx_vlan_cfg(rvu, pcifunc, vlan_tci, &vidx);
	if (err)
		return err;

	lbkid = pfvf->nix_blkaddr == BLKADDR_NIX0 ? 0 : 1;
	req.hdr.pcifunc = 0; /* AF is requester */
	if (rte) {
		req.vf = pcifunc;
	} else {
		req.vf = rvu->rep_pcifunc;
		req.packet.sq_id = vlan_tci;
		req.mask.sq_id = 0xffff;
	}

	req.entry = entry;
	req.intf = pfvf->nix_tx_intf;
	req.op = NIX_TX_ACTIONOP_UCAST_CHAN;
	req.index = (lbkid << 8) | RVU_SWITCH_LBK_CHAN;
	req.set_cntr = 1;
	req.vtag0_def = vidx;
	req.vtag0_op = 1;
	return rvu_mbox_handler_npc_install_flow(rvu, &req, &rsp);
}

int rvu_rep_install_mcam_rules(struct rvu *rvu)
{
	struct rvu_switch *rswitch = &rvu->rswitch;
	u16 start = rswitch->start_entry;
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc, entry = 0;
	int pf, vf, numvfs;
	int err, nixlf, i;
	u8 rep;

	for (pf = 1; pf < hw->total_pfs; pf++) {
		if (!is_pf_cgxmapped(rvu, pf))
			continue;

		pcifunc = pf << RVU_PFVF_PF_SHIFT;
		rvu_get_nix_blkaddr(rvu, pcifunc);
		rep = true;
		for (i = 0; i < 2; i++) {
			err = rvu_rep_install_rx_rule(rvu, pcifunc,
						      start + entry, rep);
			if (err)
				return err;
			rswitch->entry2pcifunc[entry++] = pcifunc;

			err = rvu_rep_install_tx_rule(rvu, pcifunc,
						      start + entry, rep);
			if (err)
				return err;
			rswitch->entry2pcifunc[entry++] = pcifunc;
			rep = false;
		}

		rvu_get_pf_numvfs(rvu, pf, &numvfs, NULL);
		for (vf = 0; vf < numvfs; vf++) {
			pcifunc = pf << RVU_PFVF_PF_SHIFT |
				  ((vf + 1) & RVU_PFVF_FUNC_MASK);
			rvu_get_nix_blkaddr(rvu, pcifunc);

			/* Skip installimg rules if nixlf is not attached */
			err = nix_get_nixlf(rvu, pcifunc, &nixlf, NULL);
			if (err)
				continue;
			rep = true;
			for (i = 0; i < 2; i++) {
				err = rvu_rep_install_rx_rule(rvu, pcifunc,
							      start + entry,
							      rep);
				if (err)
					return err;
				rswitch->entry2pcifunc[entry++] = pcifunc;

				err = rvu_rep_install_tx_rule(rvu, pcifunc,
							      start + entry,
							      rep);
				if (err)
					return err;
				rswitch->entry2pcifunc[entry++] = pcifunc;
				rep = false;
			}
		}
	}
	return 0;
}

void rvu_rep_update_rules(struct rvu *rvu, u16 pcifunc, bool ena)
{
	struct rvu_switch *rswitch = &rvu->rswitch;
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u32 max = rswitch->used_entries;
	int blkaddr;
	u16 entry;

	if (!rswitch->used_entries)
		return;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);

	if (blkaddr < 0)
		return;

	rvu_switch_enable_lbk_link(rvu, pcifunc, ena);
	mutex_lock(&mcam->lock);
	for (entry = 0; entry < max; entry++) {
		if (rswitch->entry2pcifunc[entry] == pcifunc)
			npc_enable_mcam_entry(rvu, mcam, blkaddr, entry, ena);
	}
	mutex_unlock(&mcam->lock);
}

int rvu_rep_pf_init(struct rvu *rvu)
{
	u16 pcifunc = rvu->rep_pcifunc;
	struct rvu_pfvf *pfvf;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	set_bit(NIXLF_INITIALIZED, &pfvf->flags);
	rvu_switch_enable_lbk_link(rvu, pcifunc, true);
	rvu_rep_rx_vlan_cfg(rvu, pcifunc);
	return 0;
}

int rvu_mbox_handler_esw_cfg(struct rvu *rvu, struct esw_cfg_req *req,
			     struct msg_rsp *rsp)
{
	if (req->hdr.pcifunc != rvu->rep_pcifunc)
		return 0;

	rvu->rep_mode = req->ena;
	return 0;
}

int rvu_mbox_handler_get_rep_cnt(struct rvu *rvu, struct msg_req *req,
				 struct get_rep_cnt_rsp *rsp)
{
	int pf, vf, numvfs, hwvf, rep = 0;
	u16 pcifunc;

	rvu->rep_pcifunc = req->hdr.pcifunc;
	rsp->rep_cnt = rvu->cgx_mapped_pfs + rvu->cgx_mapped_vfs;
	rvu->rep_cnt = rsp->rep_cnt;

	rvu->rep2pfvf_map = devm_kzalloc(rvu->dev, rvu->rep_cnt *
					 sizeof(u16), GFP_KERNEL);
	if (!rvu->rep2pfvf_map)
		return -ENOMEM;

	for (pf = 0; pf < rvu->hw->total_pfs; pf++) {
		if (!is_pf_cgxmapped(rvu, pf))
			continue;
		pcifunc = pf << RVU_PFVF_PF_SHIFT;
		rvu->rep2pfvf_map[rep] = pcifunc;
		rsp->rep_pf_map[rep] = pcifunc;
		rep++;
		rvu_get_pf_numvfs(rvu, pf, &numvfs, &hwvf);
		for (vf = 0; vf < numvfs; vf++) {
			rvu->rep2pfvf_map[rep] = pcifunc |
				((vf + 1) & RVU_PFVF_FUNC_MASK);
			rsp->rep_pf_map[rep] = rvu->rep2pfvf_map[rep];
			rep++;
		}
	}
	return 0;
}
