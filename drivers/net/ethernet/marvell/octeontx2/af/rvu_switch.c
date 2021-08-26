// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2021 Marvell.
 */

#include <linux/bitfield.h>
#include "rvu.h"

static int rvu_switch_install_rx_rule(struct rvu *rvu, u16 pcifunc,
				      u16 chan_mask)
{
	struct npc_install_flow_req req = { 0 };
	struct npc_install_flow_rsp rsp = { 0 };
	struct rvu_pfvf *pfvf;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	/* If the pcifunc is not initialized then nothing to do.
	 * This same function will be called again via rvu_switch_update_rules
	 * after pcifunc is initialized.
	 */
	if (!test_bit(NIXLF_INITIALIZED, &pfvf->flags))
		return 0;

	ether_addr_copy(req.packet.dmac, pfvf->mac_addr);
	eth_broadcast_addr((u8 *)&req.mask.dmac);
	req.hdr.pcifunc = 0; /* AF is requester */
	req.vf = pcifunc;
	req.features = BIT_ULL(NPC_DMAC);
	req.channel = pfvf->rx_chan_base;
	req.chan_mask = chan_mask;
	req.intf = pfvf->nix_rx_intf;
	req.op = NIX_RX_ACTION_DEFAULT;
	req.default_rule = 1;

	return rvu_mbox_handler_npc_install_flow(rvu, &req, &rsp);
}

static int rvu_switch_install_tx_rule(struct rvu *rvu, u16 pcifunc, u16 entry)
{
	struct npc_install_flow_req req = { 0 };
	struct npc_install_flow_rsp rsp = { 0 };
	struct rvu_pfvf *pfvf;
	u8 lbkid;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	/* If the pcifunc is not initialized then nothing to do.
	 * This same function will be called again via rvu_switch_update_rules
	 * after pcifunc is initialized.
	 */
	if (!test_bit(NIXLF_INITIALIZED, &pfvf->flags))
		return 0;

	lbkid = pfvf->nix_blkaddr == BLKADDR_NIX0 ? 0 : 1;
	ether_addr_copy(req.packet.dmac, pfvf->mac_addr);
	eth_broadcast_addr((u8 *)&req.mask.dmac);
	req.hdr.pcifunc = 0; /* AF is requester */
	req.vf = pcifunc;
	req.entry = entry;
	req.features = BIT_ULL(NPC_DMAC);
	req.intf = pfvf->nix_tx_intf;
	req.op = NIX_TX_ACTIONOP_UCAST_CHAN;
	req.index = (lbkid << 8) | RVU_SWITCH_LBK_CHAN;
	req.set_cntr = 1;

	return rvu_mbox_handler_npc_install_flow(rvu, &req, &rsp);
}

static int rvu_switch_install_rules(struct rvu *rvu)
{
	struct rvu_switch *rswitch = &rvu->rswitch;
	u16 start = rswitch->start_entry;
	struct rvu_hwinfo *hw = rvu->hw;
	u16 pcifunc, entry = 0;
	int pf, vf, numvfs;
	int err;

	for (pf = 1; pf < hw->total_pfs; pf++) {
		if (!is_pf_cgxmapped(rvu, pf))
			continue;

		pcifunc = pf << 10;
		/* rvu_get_nix_blkaddr sets up the corresponding NIX block
		 * address and NIX RX and TX interfaces for a pcifunc.
		 * Generally it is called during attach call of a pcifunc but it
		 * is called here since we are pre-installing rules before
		 * nixlfs are attached
		 */
		rvu_get_nix_blkaddr(rvu, pcifunc);

		/* MCAM RX rule for a PF/VF already exists as default unicast
		 * rules installed by AF. Hence change the channel in those
		 * rules to ignore channel so that packets with the required
		 * DMAC received from LBK(by other PF/VFs in system) or from
		 * external world (from wire) are accepted.
		 */
		err = rvu_switch_install_rx_rule(rvu, pcifunc, 0x0);
		if (err) {
			dev_err(rvu->dev, "RX rule for PF%d failed(%d)\n",
				pf, err);
			return err;
		}

		err = rvu_switch_install_tx_rule(rvu, pcifunc, start + entry);
		if (err) {
			dev_err(rvu->dev, "TX rule for PF%d failed(%d)\n",
				pf, err);
			return err;
		}

		rswitch->entry2pcifunc[entry++] = pcifunc;

		rvu_get_pf_numvfs(rvu, pf, &numvfs, NULL);
		for (vf = 0; vf < numvfs; vf++) {
			pcifunc = pf << 10 | ((vf + 1) & 0x3FF);
			rvu_get_nix_blkaddr(rvu, pcifunc);

			err = rvu_switch_install_rx_rule(rvu, pcifunc, 0x0);
			if (err) {
				dev_err(rvu->dev,
					"RX rule for PF%dVF%d failed(%d)\n",
					pf, vf, err);
				return err;
			}

			err = rvu_switch_install_tx_rule(rvu, pcifunc,
							 start + entry);
			if (err) {
				dev_err(rvu->dev,
					"TX rule for PF%dVF%d failed(%d)\n",
					pf, vf, err);
				return err;
			}

			rswitch->entry2pcifunc[entry++] = pcifunc;
		}
	}

	return 0;
}

void rvu_switch_enable(struct rvu *rvu)
{
	struct npc_mcam_alloc_entry_req alloc_req = { 0 };
	struct npc_mcam_alloc_entry_rsp alloc_rsp = { 0 };
	struct npc_delete_flow_req uninstall_req = { 0 };
	struct npc_mcam_free_entry_req free_req = { 0 };
	struct rvu_switch *rswitch = &rvu->rswitch;
	struct msg_rsp rsp;
	int ret;

	alloc_req.contig = true;
	alloc_req.count = rvu->cgx_mapped_pfs + rvu->cgx_mapped_vfs;
	ret = rvu_mbox_handler_npc_mcam_alloc_entry(rvu, &alloc_req,
						    &alloc_rsp);
	if (ret) {
		dev_err(rvu->dev,
			"Unable to allocate MCAM entries\n");
		goto exit;
	}

	if (alloc_rsp.count != alloc_req.count) {
		dev_err(rvu->dev,
			"Unable to allocate %d MCAM entries, got %d\n",
			alloc_req.count, alloc_rsp.count);
		goto free_entries;
	}

	rswitch->entry2pcifunc = kcalloc(alloc_req.count, sizeof(u16),
					 GFP_KERNEL);
	if (!rswitch->entry2pcifunc)
		goto free_entries;

	rswitch->used_entries = alloc_rsp.count;
	rswitch->start_entry = alloc_rsp.entry;

	ret = rvu_switch_install_rules(rvu);
	if (ret)
		goto uninstall_rules;

	return;

uninstall_rules:
	uninstall_req.start = rswitch->start_entry;
	uninstall_req.end =  rswitch->start_entry + rswitch->used_entries - 1;
	rvu_mbox_handler_npc_delete_flow(rvu, &uninstall_req, &rsp);
	kfree(rswitch->entry2pcifunc);
free_entries:
	free_req.all = 1;
	rvu_mbox_handler_npc_mcam_free_entry(rvu, &free_req, &rsp);
exit:
	return;
}

void rvu_switch_disable(struct rvu *rvu)
{
	struct npc_delete_flow_req uninstall_req = { 0 };
	struct npc_mcam_free_entry_req free_req = { 0 };
	struct rvu_switch *rswitch = &rvu->rswitch;
	struct rvu_hwinfo *hw = rvu->hw;
	int pf, vf, numvfs;
	struct msg_rsp rsp;
	u16 pcifunc;
	int err;

	if (!rswitch->used_entries)
		return;

	for (pf = 1; pf < hw->total_pfs; pf++) {
		if (!is_pf_cgxmapped(rvu, pf))
			continue;

		pcifunc = pf << 10;
		err = rvu_switch_install_rx_rule(rvu, pcifunc, 0xFFF);
		if (err)
			dev_err(rvu->dev,
				"Reverting RX rule for PF%d failed(%d)\n",
				pf, err);

		rvu_get_pf_numvfs(rvu, pf, &numvfs, NULL);
		for (vf = 0; vf < numvfs; vf++) {
			pcifunc = pf << 10 | ((vf + 1) & 0x3FF);
			err = rvu_switch_install_rx_rule(rvu, pcifunc, 0xFFF);
			if (err)
				dev_err(rvu->dev,
					"Reverting RX rule for PF%dVF%d failed(%d)\n",
					pf, vf, err);
		}
	}

	uninstall_req.start = rswitch->start_entry;
	uninstall_req.end =  rswitch->start_entry + rswitch->used_entries - 1;
	free_req.all = 1;
	rvu_mbox_handler_npc_delete_flow(rvu, &uninstall_req, &rsp);
	rvu_mbox_handler_npc_mcam_free_entry(rvu, &free_req, &rsp);
	rswitch->used_entries = 0;
	kfree(rswitch->entry2pcifunc);
}

void rvu_switch_update_rules(struct rvu *rvu, u16 pcifunc)
{
	struct rvu_switch *rswitch = &rvu->rswitch;
	u32 max = rswitch->used_entries;
	u16 entry;

	if (!rswitch->used_entries)
		return;

	for (entry = 0; entry < max; entry++) {
		if (rswitch->entry2pcifunc[entry] == pcifunc)
			break;
	}

	if (entry >= max)
		return;

	rvu_switch_install_tx_rule(rvu, pcifunc, rswitch->start_entry + entry);
	rvu_switch_install_rx_rule(rvu, pcifunc, 0x0);
}
