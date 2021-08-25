// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "rvu_struct.h"
#include "rvu_reg.h"
#include "rvu.h"
#include "npc.h"
#include "cgx.h"
#include "npc_profile.h"

#define RSVD_MCAM_ENTRIES_PER_PF	3 /* Broadcast, Promisc and AllMulticast */
#define RSVD_MCAM_ENTRIES_PER_NIXLF	1 /* Ucast for LFs */

#define NPC_PARSE_RESULT_DMAC_OFFSET	8
#define NPC_HW_TSTAMP_OFFSET		8
#define NPC_KEX_CHAN_MASK		0xFFFULL
#define NPC_KEX_PF_FUNC_MASK		0xFFFFULL

#define ALIGN_8B_CEIL(__a)	(((__a) + 7) & (-8))

static const char def_pfl_name[] = "default";

static void npc_mcam_free_all_entries(struct rvu *rvu, struct npc_mcam *mcam,
				      int blkaddr, u16 pcifunc);
static void npc_mcam_free_all_counters(struct rvu *rvu, struct npc_mcam *mcam,
				       u16 pcifunc);

bool is_npc_intf_tx(u8 intf)
{
	return !!(intf & 0x1);
}

bool is_npc_intf_rx(u8 intf)
{
	return !(intf & 0x1);
}

bool is_npc_interface_valid(struct rvu *rvu, u8 intf)
{
	struct rvu_hwinfo *hw = rvu->hw;

	return intf < hw->npc_intfs;
}

int rvu_npc_get_tx_nibble_cfg(struct rvu *rvu, u64 nibble_ena)
{
	/* Due to a HW issue in these silicon versions, parse nibble enable
	 * configuration has to be identical for both Rx and Tx interfaces.
	 */
	if (is_rvu_96xx_B0(rvu))
		return nibble_ena;
	return 0;
}

static int npc_mcam_verify_pf_func(struct rvu *rvu,
				   struct mcam_entry *entry_data, u8 intf,
				   u16 pcifunc)
{
	u16 pf_func, pf_func_mask;

	if (is_npc_intf_rx(intf))
		return 0;

	pf_func_mask = (entry_data->kw_mask[0] >> 32) &
		NPC_KEX_PF_FUNC_MASK;
	pf_func = (entry_data->kw[0] >> 32) & NPC_KEX_PF_FUNC_MASK;

	pf_func = be16_to_cpu((__force __be16)pf_func);
	if (pf_func_mask != NPC_KEX_PF_FUNC_MASK ||
	    ((pf_func & ~RVU_PFVF_FUNC_MASK) !=
	     (pcifunc & ~RVU_PFVF_FUNC_MASK)))
		return -EINVAL;

	return 0;
}

void rvu_npc_set_pkind(struct rvu *rvu, int pkind, struct rvu_pfvf *pfvf)
{
	int blkaddr;
	u64 val = 0;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return;

	/* Config CPI base for the PKIND */
	val = pkind | 1ULL << 62;
	rvu_write64(rvu, blkaddr, NPC_AF_PKINDX_CPI_DEFX(pkind, 0), val);
}

int rvu_npc_get_pkind(struct rvu *rvu, u16 pf)
{
	struct npc_pkind *pkind = &rvu->hw->pkind;
	u32 map;
	int i;

	for (i = 0; i < pkind->rsrc.max; i++) {
		map = pkind->pfchan_map[i];
		if (((map >> 16) & 0x3F) == pf)
			return i;
	}
	return -1;
}

#define NPC_AF_ACTION0_PTR_ADVANCE	GENMASK_ULL(27, 20)

int npc_config_ts_kpuaction(struct rvu *rvu, int pf, u16 pcifunc, bool enable)
{
	int pkind, blkaddr;
	u64 val;

	pkind = rvu_npc_get_pkind(rvu, pf);
	if (pkind < 0) {
		dev_err(rvu->dev, "%s: pkind not mapped\n", __func__);
		return -EINVAL;
	}

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, pcifunc);
	if (blkaddr < 0) {
		dev_err(rvu->dev, "%s: NPC block not implemented\n", __func__);
		return -EINVAL;
	}

	val = rvu_read64(rvu, blkaddr, NPC_AF_PKINDX_ACTION0(pkind));
	val &= ~NPC_AF_ACTION0_PTR_ADVANCE;
	/* If timestamp is enabled then configure NPC to shift 8 bytes */
	if (enable)
		val |= FIELD_PREP(NPC_AF_ACTION0_PTR_ADVANCE,
				  NPC_HW_TSTAMP_OFFSET);
	rvu_write64(rvu, blkaddr, NPC_AF_PKINDX_ACTION0(pkind), val);

	return 0;
}

static int npc_get_ucast_mcam_index(struct npc_mcam *mcam, u16 pcifunc,
				    int nixlf)
{
	struct rvu_hwinfo *hw = container_of(mcam, struct rvu_hwinfo, mcam);
	struct rvu *rvu = hw->rvu;
	int blkaddr = 0, max = 0;
	struct rvu_block *block;
	struct rvu_pfvf *pfvf;

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	/* Given a PF/VF and NIX LF number calculate the unicast mcam
	 * entry index based on the NIX block assigned to the PF/VF.
	 */
	blkaddr = rvu_get_next_nix_blkaddr(rvu, blkaddr);
	while (blkaddr) {
		if (pfvf->nix_blkaddr == blkaddr)
			break;
		block = &rvu->hw->block[blkaddr];
		max += block->lf.max;
		blkaddr = rvu_get_next_nix_blkaddr(rvu, blkaddr);
	}

	return mcam->nixlf_offset + (max + nixlf) * RSVD_MCAM_ENTRIES_PER_NIXLF;
}

int npc_get_nixlf_mcam_index(struct npc_mcam *mcam,
			     u16 pcifunc, int nixlf, int type)
{
	int pf = rvu_get_pf(pcifunc);
	int index;

	/* Check if this is for a PF */
	if (pf && !(pcifunc & RVU_PFVF_FUNC_MASK)) {
		/* Reserved entries exclude PF0 */
		pf--;
		index = mcam->pf_offset + (pf * RSVD_MCAM_ENTRIES_PER_PF);
		/* Broadcast address matching entry should be first so
		 * that the packet can be replicated to all VFs.
		 */
		if (type == NIXLF_BCAST_ENTRY)
			return index;
		else if (type == NIXLF_ALLMULTI_ENTRY)
			return index + 1;
		else if (type == NIXLF_PROMISC_ENTRY)
			return index + 2;
	}

	return npc_get_ucast_mcam_index(mcam, pcifunc, nixlf);
}

int npc_get_bank(struct npc_mcam *mcam, int index)
{
	int bank = index / mcam->banksize;

	/* 0,1 & 2,3 banks are combined for this keysize */
	if (mcam->keysize == NPC_MCAM_KEY_X2)
		return bank ? 2 : 0;

	return bank;
}

bool is_mcam_entry_enabled(struct rvu *rvu, struct npc_mcam *mcam,
			   int blkaddr, int index)
{
	int bank = npc_get_bank(mcam, index);
	u64 cfg;

	index &= (mcam->banksize - 1);
	cfg = rvu_read64(rvu, blkaddr, NPC_AF_MCAMEX_BANKX_CFG(index, bank));
	return (cfg & 1);
}

void npc_enable_mcam_entry(struct rvu *rvu, struct npc_mcam *mcam,
			   int blkaddr, int index, bool enable)
{
	int bank = npc_get_bank(mcam, index);
	int actbank = bank;

	index &= (mcam->banksize - 1);
	for (; bank < (actbank + mcam->banks_per_entry); bank++) {
		rvu_write64(rvu, blkaddr,
			    NPC_AF_MCAMEX_BANKX_CFG(index, bank),
			    enable ? 1 : 0);
	}
}

static void npc_clear_mcam_entry(struct rvu *rvu, struct npc_mcam *mcam,
				 int blkaddr, int index)
{
	int bank = npc_get_bank(mcam, index);
	int actbank = bank;

	index &= (mcam->banksize - 1);
	for (; bank < (actbank + mcam->banks_per_entry); bank++) {
		rvu_write64(rvu, blkaddr,
			    NPC_AF_MCAMEX_BANKX_CAMX_INTF(index, bank, 1), 0);
		rvu_write64(rvu, blkaddr,
			    NPC_AF_MCAMEX_BANKX_CAMX_INTF(index, bank, 0), 0);

		rvu_write64(rvu, blkaddr,
			    NPC_AF_MCAMEX_BANKX_CAMX_W0(index, bank, 1), 0);
		rvu_write64(rvu, blkaddr,
			    NPC_AF_MCAMEX_BANKX_CAMX_W0(index, bank, 0), 0);

		rvu_write64(rvu, blkaddr,
			    NPC_AF_MCAMEX_BANKX_CAMX_W1(index, bank, 1), 0);
		rvu_write64(rvu, blkaddr,
			    NPC_AF_MCAMEX_BANKX_CAMX_W1(index, bank, 0), 0);
	}
}

static void npc_get_keyword(struct mcam_entry *entry, int idx,
			    u64 *cam0, u64 *cam1)
{
	u64 kw_mask = 0x00;

#define CAM_MASK(n)	(BIT_ULL(n) - 1)

	/* 0, 2, 4, 6 indices refer to BANKX_CAMX_W0 and
	 * 1, 3, 5, 7 indices refer to BANKX_CAMX_W1.
	 *
	 * Also, only 48 bits of BANKX_CAMX_W1 are valid.
	 */
	switch (idx) {
	case 0:
		/* BANK(X)_CAM_W0<63:0> = MCAM_KEY[KW0]<63:0> */
		*cam1 = entry->kw[0];
		kw_mask = entry->kw_mask[0];
		break;
	case 1:
		/* BANK(X)_CAM_W1<47:0> = MCAM_KEY[KW1]<47:0> */
		*cam1 = entry->kw[1] & CAM_MASK(48);
		kw_mask = entry->kw_mask[1] & CAM_MASK(48);
		break;
	case 2:
		/* BANK(X + 1)_CAM_W0<15:0> = MCAM_KEY[KW1]<63:48>
		 * BANK(X + 1)_CAM_W0<63:16> = MCAM_KEY[KW2]<47:0>
		 */
		*cam1 = (entry->kw[1] >> 48) & CAM_MASK(16);
		*cam1 |= ((entry->kw[2] & CAM_MASK(48)) << 16);
		kw_mask = (entry->kw_mask[1] >> 48) & CAM_MASK(16);
		kw_mask |= ((entry->kw_mask[2] & CAM_MASK(48)) << 16);
		break;
	case 3:
		/* BANK(X + 1)_CAM_W1<15:0> = MCAM_KEY[KW2]<63:48>
		 * BANK(X + 1)_CAM_W1<47:16> = MCAM_KEY[KW3]<31:0>
		 */
		*cam1 = (entry->kw[2] >> 48) & CAM_MASK(16);
		*cam1 |= ((entry->kw[3] & CAM_MASK(32)) << 16);
		kw_mask = (entry->kw_mask[2] >> 48) & CAM_MASK(16);
		kw_mask |= ((entry->kw_mask[3] & CAM_MASK(32)) << 16);
		break;
	case 4:
		/* BANK(X + 2)_CAM_W0<31:0> = MCAM_KEY[KW3]<63:32>
		 * BANK(X + 2)_CAM_W0<63:32> = MCAM_KEY[KW4]<31:0>
		 */
		*cam1 = (entry->kw[3] >> 32) & CAM_MASK(32);
		*cam1 |= ((entry->kw[4] & CAM_MASK(32)) << 32);
		kw_mask = (entry->kw_mask[3] >> 32) & CAM_MASK(32);
		kw_mask |= ((entry->kw_mask[4] & CAM_MASK(32)) << 32);
		break;
	case 5:
		/* BANK(X + 2)_CAM_W1<31:0> = MCAM_KEY[KW4]<63:32>
		 * BANK(X + 2)_CAM_W1<47:32> = MCAM_KEY[KW5]<15:0>
		 */
		*cam1 = (entry->kw[4] >> 32) & CAM_MASK(32);
		*cam1 |= ((entry->kw[5] & CAM_MASK(16)) << 32);
		kw_mask = (entry->kw_mask[4] >> 32) & CAM_MASK(32);
		kw_mask |= ((entry->kw_mask[5] & CAM_MASK(16)) << 32);
		break;
	case 6:
		/* BANK(X + 3)_CAM_W0<47:0> = MCAM_KEY[KW5]<63:16>
		 * BANK(X + 3)_CAM_W0<63:48> = MCAM_KEY[KW6]<15:0>
		 */
		*cam1 = (entry->kw[5] >> 16) & CAM_MASK(48);
		*cam1 |= ((entry->kw[6] & CAM_MASK(16)) << 48);
		kw_mask = (entry->kw_mask[5] >> 16) & CAM_MASK(48);
		kw_mask |= ((entry->kw_mask[6] & CAM_MASK(16)) << 48);
		break;
	case 7:
		/* BANK(X + 3)_CAM_W1<47:0> = MCAM_KEY[KW6]<63:16> */
		*cam1 = (entry->kw[6] >> 16) & CAM_MASK(48);
		kw_mask = (entry->kw_mask[6] >> 16) & CAM_MASK(48);
		break;
	}

	*cam1 &= kw_mask;
	*cam0 = ~*cam1 & kw_mask;
}

static void npc_fill_entryword(struct mcam_entry *entry, int idx,
			       u64 cam0, u64 cam1)
{
	/* Similar to npc_get_keyword, but fills mcam_entry structure from
	 * CAM registers.
	 */
	switch (idx) {
	case 0:
		entry->kw[0] = cam1;
		entry->kw_mask[0] = cam1 ^ cam0;
		break;
	case 1:
		entry->kw[1] = cam1;
		entry->kw_mask[1] = cam1 ^ cam0;
		break;
	case 2:
		entry->kw[1] |= (cam1 & CAM_MASK(16)) << 48;
		entry->kw[2] = (cam1 >> 16) & CAM_MASK(48);
		entry->kw_mask[1] |= ((cam1 ^ cam0) & CAM_MASK(16)) << 48;
		entry->kw_mask[2] = ((cam1 ^ cam0) >> 16) & CAM_MASK(48);
		break;
	case 3:
		entry->kw[2] |= (cam1 & CAM_MASK(16)) << 48;
		entry->kw[3] = (cam1 >> 16) & CAM_MASK(32);
		entry->kw_mask[2] |= ((cam1 ^ cam0) & CAM_MASK(16)) << 48;
		entry->kw_mask[3] = ((cam1 ^ cam0) >> 16) & CAM_MASK(32);
		break;
	case 4:
		entry->kw[3] |= (cam1 & CAM_MASK(32)) << 32;
		entry->kw[4] = (cam1 >> 32) & CAM_MASK(32);
		entry->kw_mask[3] |= ((cam1 ^ cam0) & CAM_MASK(32)) << 32;
		entry->kw_mask[4] = ((cam1 ^ cam0) >> 32) & CAM_MASK(32);
		break;
	case 5:
		entry->kw[4] |= (cam1 & CAM_MASK(32)) << 32;
		entry->kw[5] = (cam1 >> 32) & CAM_MASK(16);
		entry->kw_mask[4] |= ((cam1 ^ cam0) & CAM_MASK(32)) << 32;
		entry->kw_mask[5] = ((cam1 ^ cam0) >> 32) & CAM_MASK(16);
		break;
	case 6:
		entry->kw[5] |= (cam1 & CAM_MASK(48)) << 16;
		entry->kw[6] = (cam1 >> 48) & CAM_MASK(16);
		entry->kw_mask[5] |= ((cam1 ^ cam0) & CAM_MASK(48)) << 16;
		entry->kw_mask[6] = ((cam1 ^ cam0) >> 48) & CAM_MASK(16);
		break;
	case 7:
		entry->kw[6] |= (cam1 & CAM_MASK(48)) << 16;
		entry->kw_mask[6] |= ((cam1 ^ cam0) & CAM_MASK(48)) << 16;
		break;
	}
}

static u64 npc_get_default_entry_action(struct rvu *rvu, struct npc_mcam *mcam,
					int blkaddr, u16 pf_func)
{
	int bank, nixlf, index;

	/* get ucast entry rule entry index */
	nix_get_nixlf(rvu, pf_func, &nixlf, NULL);
	index = npc_get_nixlf_mcam_index(mcam, pf_func, nixlf,
					 NIXLF_UCAST_ENTRY);
	bank = npc_get_bank(mcam, index);
	index &= (mcam->banksize - 1);

	return rvu_read64(rvu, blkaddr,
			  NPC_AF_MCAMEX_BANKX_ACTION(index, bank));
}

static void npc_fixup_vf_rule(struct rvu *rvu, struct npc_mcam *mcam,
			      int blkaddr, int index, struct mcam_entry *entry,
			      bool *enable)
{
	u16 owner, target_func;
	struct rvu_pfvf *pfvf;
	u64 rx_action;

	owner = mcam->entry2pfvf_map[index];
	target_func = (entry->action >> 4) & 0xffff;
	/* do nothing when target is LBK/PF or owner is not PF */
	if (is_pffunc_af(owner) || is_afvf(target_func) ||
	    (owner & RVU_PFVF_FUNC_MASK) ||
	    !(target_func & RVU_PFVF_FUNC_MASK))
		return;

	/* save entry2target_pffunc */
	pfvf = rvu_get_pfvf(rvu, target_func);
	mcam->entry2target_pffunc[index] = target_func;

	/* don't enable rule when nixlf not attached or initialized */
	if (!(is_nixlf_attached(rvu, target_func) &&
	      test_bit(NIXLF_INITIALIZED, &pfvf->flags)))
		*enable = false;

	/* copy VF default entry action to the VF mcam entry */
	rx_action = npc_get_default_entry_action(rvu, mcam, blkaddr,
						 target_func);
	if (rx_action)
		entry->action = rx_action;
}

static void npc_config_mcam_entry(struct rvu *rvu, struct npc_mcam *mcam,
				  int blkaddr, int index, u8 intf,
				  struct mcam_entry *entry, bool enable)
{
	int bank = npc_get_bank(mcam, index);
	int kw = 0, actbank, actindex;
	u8 tx_intf_mask = ~intf & 0x3;
	u8 tx_intf = intf;
	u64 cam0, cam1;

	actbank = bank; /* Save bank id, to set action later on */
	actindex = index;
	index &= (mcam->banksize - 1);

	/* Disable before mcam entry update */
	npc_enable_mcam_entry(rvu, mcam, blkaddr, actindex, false);

	/* Clear mcam entry to avoid writes being suppressed by NPC */
	npc_clear_mcam_entry(rvu, mcam, blkaddr, actindex);

	/* CAM1 takes the comparison value and
	 * CAM0 specifies match for a bit in key being '0' or '1' or 'dontcare'.
	 * CAM1<n> = 0 & CAM0<n> = 1 => match if key<n> = 0
	 * CAM1<n> = 1 & CAM0<n> = 0 => match if key<n> = 1
	 * CAM1<n> = 0 & CAM0<n> = 0 => always match i.e dontcare.
	 */
	for (; bank < (actbank + mcam->banks_per_entry); bank++, kw = kw + 2) {
		/* Interface should be set in all banks */
		if (is_npc_intf_tx(intf)) {
			/* Last bit must be set and rest don't care
			 * for TX interfaces
			 */
			tx_intf_mask = 0x1;
			tx_intf = intf & tx_intf_mask;
			tx_intf_mask = ~tx_intf & tx_intf_mask;
		}

		rvu_write64(rvu, blkaddr,
			    NPC_AF_MCAMEX_BANKX_CAMX_INTF(index, bank, 1),
			    tx_intf);
		rvu_write64(rvu, blkaddr,
			    NPC_AF_MCAMEX_BANKX_CAMX_INTF(index, bank, 0),
			    tx_intf_mask);

		/* Set the match key */
		npc_get_keyword(entry, kw, &cam0, &cam1);
		rvu_write64(rvu, blkaddr,
			    NPC_AF_MCAMEX_BANKX_CAMX_W0(index, bank, 1), cam1);
		rvu_write64(rvu, blkaddr,
			    NPC_AF_MCAMEX_BANKX_CAMX_W0(index, bank, 0), cam0);

		npc_get_keyword(entry, kw + 1, &cam0, &cam1);
		rvu_write64(rvu, blkaddr,
			    NPC_AF_MCAMEX_BANKX_CAMX_W1(index, bank, 1), cam1);
		rvu_write64(rvu, blkaddr,
			    NPC_AF_MCAMEX_BANKX_CAMX_W1(index, bank, 0), cam0);
	}

	/* PF installing VF rule */
	if (intf == NIX_INTF_RX && actindex < mcam->bmap_entries)
		npc_fixup_vf_rule(rvu, mcam, blkaddr, index, entry, &enable);

	/* Set 'action' */
	rvu_write64(rvu, blkaddr,
		    NPC_AF_MCAMEX_BANKX_ACTION(index, actbank), entry->action);

	/* Set TAG 'action' */
	rvu_write64(rvu, blkaddr, NPC_AF_MCAMEX_BANKX_TAG_ACT(index, actbank),
		    entry->vtag_action);

	/* Enable the entry */
	if (enable)
		npc_enable_mcam_entry(rvu, mcam, blkaddr, actindex, true);
}

void npc_read_mcam_entry(struct rvu *rvu, struct npc_mcam *mcam,
			 int blkaddr, u16 src,
			 struct mcam_entry *entry, u8 *intf, u8 *ena)
{
	int sbank = npc_get_bank(mcam, src);
	int bank, kw = 0;
	u64 cam0, cam1;

	src &= (mcam->banksize - 1);
	bank = sbank;

	for (; bank < (sbank + mcam->banks_per_entry); bank++, kw = kw + 2) {
		cam1 = rvu_read64(rvu, blkaddr,
				  NPC_AF_MCAMEX_BANKX_CAMX_W0(src, bank, 1));
		cam0 = rvu_read64(rvu, blkaddr,
				  NPC_AF_MCAMEX_BANKX_CAMX_W0(src, bank, 0));
		npc_fill_entryword(entry, kw, cam0, cam1);

		cam1 = rvu_read64(rvu, blkaddr,
				  NPC_AF_MCAMEX_BANKX_CAMX_W1(src, bank, 1));
		cam0 = rvu_read64(rvu, blkaddr,
				  NPC_AF_MCAMEX_BANKX_CAMX_W1(src, bank, 0));
		npc_fill_entryword(entry, kw + 1, cam0, cam1);
	}

	entry->action = rvu_read64(rvu, blkaddr,
				   NPC_AF_MCAMEX_BANKX_ACTION(src, sbank));
	entry->vtag_action =
		rvu_read64(rvu, blkaddr,
			   NPC_AF_MCAMEX_BANKX_TAG_ACT(src, sbank));
	*intf = rvu_read64(rvu, blkaddr,
			   NPC_AF_MCAMEX_BANKX_CAMX_INTF(src, sbank, 1)) & 3;
	*ena = rvu_read64(rvu, blkaddr,
			  NPC_AF_MCAMEX_BANKX_CFG(src, sbank)) & 1;
}

static void npc_copy_mcam_entry(struct rvu *rvu, struct npc_mcam *mcam,
				int blkaddr, u16 src, u16 dest)
{
	int dbank = npc_get_bank(mcam, dest);
	int sbank = npc_get_bank(mcam, src);
	u64 cfg, sreg, dreg;
	int bank, i;

	src &= (mcam->banksize - 1);
	dest &= (mcam->banksize - 1);

	/* Copy INTF's, W0's, W1's CAM0 and CAM1 configuration */
	for (bank = 0; bank < mcam->banks_per_entry; bank++) {
		sreg = NPC_AF_MCAMEX_BANKX_CAMX_INTF(src, sbank + bank, 0);
		dreg = NPC_AF_MCAMEX_BANKX_CAMX_INTF(dest, dbank + bank, 0);
		for (i = 0; i < 6; i++) {
			cfg = rvu_read64(rvu, blkaddr, sreg + (i * 8));
			rvu_write64(rvu, blkaddr, dreg + (i * 8), cfg);
		}
	}

	/* Copy action */
	cfg = rvu_read64(rvu, blkaddr,
			 NPC_AF_MCAMEX_BANKX_ACTION(src, sbank));
	rvu_write64(rvu, blkaddr,
		    NPC_AF_MCAMEX_BANKX_ACTION(dest, dbank), cfg);

	/* Copy TAG action */
	cfg = rvu_read64(rvu, blkaddr,
			 NPC_AF_MCAMEX_BANKX_TAG_ACT(src, sbank));
	rvu_write64(rvu, blkaddr,
		    NPC_AF_MCAMEX_BANKX_TAG_ACT(dest, dbank), cfg);

	/* Enable or disable */
	cfg = rvu_read64(rvu, blkaddr,
			 NPC_AF_MCAMEX_BANKX_CFG(src, sbank));
	rvu_write64(rvu, blkaddr,
		    NPC_AF_MCAMEX_BANKX_CFG(dest, dbank), cfg);
}

static u64 npc_get_mcam_action(struct rvu *rvu, struct npc_mcam *mcam,
			       int blkaddr, int index)
{
	int bank = npc_get_bank(mcam, index);

	index &= (mcam->banksize - 1);
	return rvu_read64(rvu, blkaddr,
			  NPC_AF_MCAMEX_BANKX_ACTION(index, bank));
}

void rvu_npc_install_ucast_entry(struct rvu *rvu, u16 pcifunc,
				 int nixlf, u64 chan, u8 *mac_addr)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, pcifunc);
	struct npc_install_flow_req req = { 0 };
	struct npc_install_flow_rsp rsp = { 0 };
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct nix_rx_action action;
	int blkaddr, index;

	/* AF's and SDP VFs work in promiscuous mode */
	if (is_afvf(pcifunc) || is_sdp_vf(pcifunc))
		return;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return;

	index = npc_get_nixlf_mcam_index(mcam, pcifunc,
					 nixlf, NIXLF_UCAST_ENTRY);

	/* Don't change the action if entry is already enabled
	 * Otherwise RSS action may get overwritten.
	 */
	if (is_mcam_entry_enabled(rvu, mcam, blkaddr, index)) {
		*(u64 *)&action = npc_get_mcam_action(rvu, mcam,
						      blkaddr, index);
	} else {
		*(u64 *)&action = 0x00;
		action.op = NIX_RX_ACTIONOP_UCAST;
		action.pf_func = pcifunc;
	}

	req.default_rule = 1;
	ether_addr_copy(req.packet.dmac, mac_addr);
	eth_broadcast_addr((u8 *)&req.mask.dmac);
	req.features = BIT_ULL(NPC_DMAC);
	req.channel = chan;
	req.chan_mask = 0xFFFU;
	req.intf = pfvf->nix_rx_intf;
	req.op = action.op;
	req.hdr.pcifunc = 0; /* AF is requester */
	req.vf = action.pf_func;
	req.index = action.index;
	req.match_id = action.match_id;
	req.flow_key_alg = action.flow_key_alg;

	rvu_mbox_handler_npc_install_flow(rvu, &req, &rsp);
}

void rvu_npc_install_promisc_entry(struct rvu *rvu, u16 pcifunc,
				   int nixlf, u64 chan, u8 chan_cnt)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, pcifunc);
	struct npc_install_flow_req req = { 0 };
	struct npc_install_flow_rsp rsp = { 0 };
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct rvu_hwinfo *hw = rvu->hw;
	int blkaddr, ucast_idx, index;
	struct nix_rx_action action;
	u64 relaxed_mask;

	if (!hw->cap.nix_rx_multicast && is_cgx_vf(rvu, pcifunc))
		return;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return;

	index = npc_get_nixlf_mcam_index(mcam, pcifunc,
					 nixlf, NIXLF_PROMISC_ENTRY);

	if (is_cgx_vf(rvu, pcifunc))
		index = npc_get_nixlf_mcam_index(mcam,
						 pcifunc & ~RVU_PFVF_FUNC_MASK,
						 nixlf, NIXLF_PROMISC_ENTRY);

	/* If the corresponding PF's ucast action is RSS,
	 * use the same action for promisc also
	 */
	ucast_idx = npc_get_nixlf_mcam_index(mcam, pcifunc,
					     nixlf, NIXLF_UCAST_ENTRY);
	if (is_mcam_entry_enabled(rvu, mcam, blkaddr, ucast_idx))
		*(u64 *)&action = npc_get_mcam_action(rvu, mcam,
						      blkaddr, ucast_idx);

	if (action.op != NIX_RX_ACTIONOP_RSS) {
		*(u64 *)&action = 0x00;
		action.op = NIX_RX_ACTIONOP_UCAST;
	}

	/* RX_ACTION set to MCAST for CGX PF's */
	if (hw->cap.nix_rx_multicast && pfvf->use_mce_list &&
	    is_pf_cgxmapped(rvu, rvu_get_pf(pcifunc))) {
		*(u64 *)&action = 0x00;
		action.op = NIX_RX_ACTIONOP_MCAST;
		pfvf = rvu_get_pfvf(rvu, pcifunc & ~RVU_PFVF_FUNC_MASK);
		action.index = pfvf->promisc_mce_idx;
	}

	/* For cn10k the upper two bits of the channel number are
	 * cpt channel number. with masking out these bits in the
	 * mcam entry, same entry used for NIX will allow packets
	 * received from cpt for parsing.
	 */
	if (!is_rvu_otx2(rvu)) {
		req.chan_mask = NIX_CHAN_CPT_X2P_MASK;
	} else {
		req.chan_mask = 0xFFFU;
	}

	if (chan_cnt > 1) {
		if (!is_power_of_2(chan_cnt)) {
			dev_err(rvu->dev,
				"%s: channel count more than 1, must be power of 2\n", __func__);
			return;
		}
		relaxed_mask = GENMASK_ULL(BITS_PER_LONG_LONG - 1,
					   ilog2(chan_cnt));
		req.chan_mask &= relaxed_mask;
	}

	req.channel = chan;
	req.intf = pfvf->nix_rx_intf;
	req.entry = index;
	req.op = action.op;
	req.hdr.pcifunc = 0; /* AF is requester */
	req.vf = pcifunc;
	req.index = action.index;
	req.match_id = action.match_id;
	req.flow_key_alg = action.flow_key_alg;

	rvu_mbox_handler_npc_install_flow(rvu, &req, &rsp);
}

void rvu_npc_enable_promisc_entry(struct rvu *rvu, u16 pcifunc,
				  int nixlf, bool enable)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	int blkaddr, index;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return;

	/* Get 'pcifunc' of PF device */
	pcifunc = pcifunc & ~RVU_PFVF_FUNC_MASK;

	index = npc_get_nixlf_mcam_index(mcam, pcifunc,
					 nixlf, NIXLF_PROMISC_ENTRY);
	npc_enable_mcam_entry(rvu, mcam, blkaddr, index, enable);
}

void rvu_npc_install_bcast_match_entry(struct rvu *rvu, u16 pcifunc,
				       int nixlf, u64 chan)
{
	struct rvu_pfvf *pfvf;
	struct npc_install_flow_req req = { 0 };
	struct npc_install_flow_rsp rsp = { 0 };
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct rvu_hwinfo *hw = rvu->hw;
	int blkaddr, index;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return;

	/* Skip LBK VFs */
	if (is_afvf(pcifunc))
		return;

	/* If pkt replication is not supported,
	 * then only PF is allowed to add a bcast match entry.
	 */
	if (!hw->cap.nix_rx_multicast && is_vf(pcifunc))
		return;

	/* Get 'pcifunc' of PF device */
	pcifunc = pcifunc & ~RVU_PFVF_FUNC_MASK;
	pfvf = rvu_get_pfvf(rvu, pcifunc);
	index = npc_get_nixlf_mcam_index(mcam, pcifunc,
					 nixlf, NIXLF_BCAST_ENTRY);

	if (!hw->cap.nix_rx_multicast) {
		/* Early silicon doesn't support pkt replication,
		 * so install entry with UCAST action, so that PF
		 * receives all broadcast packets.
		 */
		req.op = NIX_RX_ACTIONOP_UCAST;
	} else {
		req.op = NIX_RX_ACTIONOP_MCAST;
		req.index = pfvf->bcast_mce_idx;
	}

	eth_broadcast_addr((u8 *)&req.packet.dmac);
	eth_broadcast_addr((u8 *)&req.mask.dmac);
	req.features = BIT_ULL(NPC_DMAC);
	req.channel = chan;
	req.chan_mask = 0xFFFU;
	req.intf = pfvf->nix_rx_intf;
	req.entry = index;
	req.hdr.pcifunc = 0; /* AF is requester */
	req.vf = pcifunc;

	rvu_mbox_handler_npc_install_flow(rvu, &req, &rsp);
}

void rvu_npc_enable_bcast_entry(struct rvu *rvu, u16 pcifunc, int nixlf,
				bool enable)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	int blkaddr, index;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return;

	/* Get 'pcifunc' of PF device */
	pcifunc = pcifunc & ~RVU_PFVF_FUNC_MASK;

	index = npc_get_nixlf_mcam_index(mcam, pcifunc, nixlf,
					 NIXLF_BCAST_ENTRY);
	npc_enable_mcam_entry(rvu, mcam, blkaddr, index, enable);
}

void rvu_npc_install_allmulti_entry(struct rvu *rvu, u16 pcifunc, int nixlf,
				    u64 chan)
{
	struct npc_install_flow_req req = { 0 };
	struct npc_install_flow_rsp rsp = { 0 };
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct rvu_hwinfo *hw = rvu->hw;
	int blkaddr, ucast_idx, index;
	u8 mac_addr[ETH_ALEN] = { 0 };
	struct nix_rx_action action;
	struct rvu_pfvf *pfvf;
	u16 vf_func;

	/* Only CGX PF/VF can add allmulticast entry */
	if (is_afvf(pcifunc) && is_sdp_vf(pcifunc))
		return;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return;

	/* Get 'pcifunc' of PF device */
	vf_func = pcifunc & RVU_PFVF_FUNC_MASK;
	pcifunc = pcifunc & ~RVU_PFVF_FUNC_MASK;
	pfvf = rvu_get_pfvf(rvu, pcifunc);
	index = npc_get_nixlf_mcam_index(mcam, pcifunc,
					 nixlf, NIXLF_ALLMULTI_ENTRY);

	/* If the corresponding PF's ucast action is RSS,
	 * use the same action for multicast entry also
	 */
	ucast_idx = npc_get_nixlf_mcam_index(mcam, pcifunc,
					     nixlf, NIXLF_UCAST_ENTRY);
	if (is_mcam_entry_enabled(rvu, mcam, blkaddr, ucast_idx))
		*(u64 *)&action = npc_get_mcam_action(rvu, mcam,
							blkaddr, ucast_idx);

	if (action.op != NIX_RX_ACTIONOP_RSS) {
		*(u64 *)&action = 0x00;
		action.op = NIX_RX_ACTIONOP_UCAST;
		action.pf_func = pcifunc;
	}

	/* RX_ACTION set to MCAST for CGX PF's */
	if (hw->cap.nix_rx_multicast && pfvf->use_mce_list) {
		*(u64 *)&action = 0x00;
		action.op = NIX_RX_ACTIONOP_MCAST;
		action.index = pfvf->mcast_mce_idx;
	}

	mac_addr[0] = 0x01;	/* LSB bit of 1st byte in DMAC */
	ether_addr_copy(req.packet.dmac, mac_addr);
	ether_addr_copy(req.mask.dmac, mac_addr);
	req.features = BIT_ULL(NPC_DMAC);

	/* For cn10k the upper two bits of the channel number are
	 * cpt channel number. with masking out these bits in the
	 * mcam entry, same entry used for NIX will allow packets
	 * received from cpt for parsing.
	 */
	if (!is_rvu_otx2(rvu))
		req.chan_mask = NIX_CHAN_CPT_X2P_MASK;
	else
		req.chan_mask = 0xFFFU;

	req.channel = chan;
	req.intf = pfvf->nix_rx_intf;
	req.entry = index;
	req.op = action.op;
	req.hdr.pcifunc = 0; /* AF is requester */
	req.vf = pcifunc | vf_func;
	req.index = action.index;
	req.match_id = action.match_id;
	req.flow_key_alg = action.flow_key_alg;

	rvu_mbox_handler_npc_install_flow(rvu, &req, &rsp);
}

void rvu_npc_enable_allmulti_entry(struct rvu *rvu, u16 pcifunc, int nixlf,
				   bool enable)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	int blkaddr, index;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return;

	/* Get 'pcifunc' of PF device */
	pcifunc = pcifunc & ~RVU_PFVF_FUNC_MASK;

	index = npc_get_nixlf_mcam_index(mcam, pcifunc, nixlf,
					 NIXLF_ALLMULTI_ENTRY);
	npc_enable_mcam_entry(rvu, mcam, blkaddr, index, enable);
}

static void npc_update_vf_flow_entry(struct rvu *rvu, struct npc_mcam *mcam,
				     int blkaddr, u16 pcifunc, u64 rx_action)
{
	int actindex, index, bank;
	bool enable;

	if (!(pcifunc & RVU_PFVF_FUNC_MASK))
		return;

	mutex_lock(&mcam->lock);
	for (index = 0; index < mcam->bmap_entries; index++) {
		if (mcam->entry2target_pffunc[index] == pcifunc) {
			bank = npc_get_bank(mcam, index);
			actindex = index;
			index &= (mcam->banksize - 1);

			/* read vf flow entry enable status */
			enable = is_mcam_entry_enabled(rvu, mcam, blkaddr,
						       actindex);
			/* disable before mcam entry update */
			npc_enable_mcam_entry(rvu, mcam, blkaddr, actindex,
					      false);
			/* update 'action' */
			rvu_write64(rvu, blkaddr,
				    NPC_AF_MCAMEX_BANKX_ACTION(index, bank),
				    rx_action);
			if (enable)
				npc_enable_mcam_entry(rvu, mcam, blkaddr,
						      actindex, true);
		}
	}
	mutex_unlock(&mcam->lock);
}

void rvu_npc_update_flowkey_alg_idx(struct rvu *rvu, u16 pcifunc, int nixlf,
				    int group, int alg_idx, int mcam_index)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct rvu_hwinfo *hw = rvu->hw;
	struct nix_rx_action action;
	int blkaddr, index, bank;
	struct rvu_pfvf *pfvf;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return;

	/* Check if this is for reserved default entry */
	if (mcam_index < 0) {
		if (group != DEFAULT_RSS_CONTEXT_GROUP)
			return;
		index = npc_get_nixlf_mcam_index(mcam, pcifunc,
						 nixlf, NIXLF_UCAST_ENTRY);
	} else {
		/* TODO: validate this mcam index */
		index = mcam_index;
	}

	if (index >= mcam->total_entries)
		return;

	bank = npc_get_bank(mcam, index);
	index &= (mcam->banksize - 1);

	*(u64 *)&action = rvu_read64(rvu, blkaddr,
				     NPC_AF_MCAMEX_BANKX_ACTION(index, bank));
	/* Ignore if no action was set earlier */
	if (!*(u64 *)&action)
		return;

	action.op = NIX_RX_ACTIONOP_RSS;
	action.pf_func = pcifunc;
	action.index = group;
	action.flow_key_alg = alg_idx;

	rvu_write64(rvu, blkaddr,
		    NPC_AF_MCAMEX_BANKX_ACTION(index, bank), *(u64 *)&action);

	/* update the VF flow rule action with the VF default entry action */
	if (mcam_index < 0)
		npc_update_vf_flow_entry(rvu, mcam, blkaddr, pcifunc,
					 *(u64 *)&action);

	/* update the action change in default rule */
	pfvf = rvu_get_pfvf(rvu, pcifunc);
	if (pfvf->def_ucast_rule)
		pfvf->def_ucast_rule->rx_action = action;

	index = npc_get_nixlf_mcam_index(mcam, pcifunc,
					 nixlf, NIXLF_PROMISC_ENTRY);

	/* If PF's promiscuous entry is enabled,
	 * Set RSS action for that entry as well
	 */
	if ((!hw->cap.nix_rx_multicast || !pfvf->use_mce_list) &&
	    is_mcam_entry_enabled(rvu, mcam, blkaddr, index)) {
		bank = npc_get_bank(mcam, index);
		index &= (mcam->banksize - 1);

		rvu_write64(rvu, blkaddr,
			    NPC_AF_MCAMEX_BANKX_ACTION(index, bank),
			    *(u64 *)&action);
	}
}

void npc_enadis_default_mce_entry(struct rvu *rvu, u16 pcifunc,
				  int nixlf, int type, bool enable)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct rvu_hwinfo *hw = rvu->hw;
	struct nix_mce_list *mce_list;
	int index, blkaddr, mce_idx;
	struct rvu_pfvf *pfvf;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return;

	index = npc_get_nixlf_mcam_index(mcam, pcifunc & ~RVU_PFVF_FUNC_MASK,
					 nixlf, type);

	/* disable MCAM entry when packet replication is not supported by hw */
	if (!hw->cap.nix_rx_multicast && !is_vf(pcifunc)) {
		npc_enable_mcam_entry(rvu, mcam, blkaddr, index, enable);
		return;
	}

	/* return incase mce list is not enabled */
	pfvf = rvu_get_pfvf(rvu, pcifunc & ~RVU_PFVF_FUNC_MASK);
	if (hw->cap.nix_rx_multicast && is_vf(pcifunc) &&
	    type != NIXLF_BCAST_ENTRY && !pfvf->use_mce_list)
		return;

	nix_get_mce_list(rvu, pcifunc, type, &mce_list, &mce_idx);

	nix_update_mce_list(rvu, pcifunc, mce_list,
			    mce_idx, index, enable);
	if (enable)
		npc_enable_mcam_entry(rvu, mcam, blkaddr, index, enable);
}

static void npc_enadis_default_entries(struct rvu *rvu, u16 pcifunc,
				       int nixlf, bool enable)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	int index, blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return;

	/* Ucast MCAM match entry of this PF/VF */
	index = npc_get_nixlf_mcam_index(mcam, pcifunc,
					 nixlf, NIXLF_UCAST_ENTRY);
	npc_enable_mcam_entry(rvu, mcam, blkaddr, index, enable);

	/* Nothing to do for VFs, on platforms where pkt replication
	 * is not supported
	 */
	if ((pcifunc & RVU_PFVF_FUNC_MASK) && !rvu->hw->cap.nix_rx_multicast)
		return;

	/* add/delete pf_func to broadcast MCE list */
	npc_enadis_default_mce_entry(rvu, pcifunc, nixlf,
				     NIXLF_BCAST_ENTRY, enable);
}

void rvu_npc_disable_default_entries(struct rvu *rvu, u16 pcifunc, int nixlf)
{
	npc_enadis_default_entries(rvu, pcifunc, nixlf, false);

	/* Delete multicast and promisc MCAM entries */
	npc_enadis_default_mce_entry(rvu, pcifunc, nixlf,
				     NIXLF_ALLMULTI_ENTRY, false);
	npc_enadis_default_mce_entry(rvu, pcifunc, nixlf,
				     NIXLF_PROMISC_ENTRY, false);
}

void rvu_npc_enable_default_entries(struct rvu *rvu, u16 pcifunc, int nixlf)
{
	/* Enables only broadcast match entry. Promisc/Allmulti are enabled
	 * in set_rx_mode mbox handler.
	 */
	npc_enadis_default_entries(rvu, pcifunc, nixlf, true);
}

void rvu_npc_disable_mcam_entries(struct rvu *rvu, u16 pcifunc, int nixlf)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, pcifunc);
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct rvu_npc_mcam_rule *rule, *tmp;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return;

	mutex_lock(&mcam->lock);

	/* Disable MCAM entries directing traffic to this 'pcifunc' */
	list_for_each_entry_safe(rule, tmp, &mcam->mcam_rules, list) {
		if (is_npc_intf_rx(rule->intf) &&
		    rule->rx_action.pf_func == pcifunc &&
		    rule->rx_action.op != NIX_RX_ACTIONOP_MCAST) {
			npc_enable_mcam_entry(rvu, mcam, blkaddr,
					      rule->entry, false);
			rule->enable = false;
			/* Indicate that default rule is disabled */
			if (rule->default_rule) {
				pfvf->def_ucast_rule = NULL;
				list_del(&rule->list);
				kfree(rule);
			}
		}
	}

	mutex_unlock(&mcam->lock);

	npc_mcam_disable_flows(rvu, pcifunc);

	rvu_npc_disable_default_entries(rvu, pcifunc, nixlf);
}

void rvu_npc_free_mcam_entries(struct rvu *rvu, u16 pcifunc, int nixlf)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct rvu_npc_mcam_rule *rule, *tmp;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return;

	mutex_lock(&mcam->lock);

	/* Free all MCAM entries owned by this 'pcifunc' */
	npc_mcam_free_all_entries(rvu, mcam, blkaddr, pcifunc);

	/* Free all MCAM counters owned by this 'pcifunc' */
	npc_mcam_free_all_counters(rvu, mcam, pcifunc);

	/* Delete MCAM entries owned by this 'pcifunc' */
	list_for_each_entry_safe(rule, tmp, &mcam->mcam_rules, list) {
		if (rule->owner == pcifunc && !rule->default_rule) {
			list_del(&rule->list);
			kfree(rule);
		}
	}

	mutex_unlock(&mcam->lock);

	rvu_npc_disable_default_entries(rvu, pcifunc, nixlf);
}

#define SET_KEX_LD(intf, lid, ltype, ld, cfg)	\
	rvu_write64(rvu, blkaddr,			\
		NPC_AF_INTFX_LIDX_LTX_LDX_CFG(intf, lid, ltype, ld), cfg)

#define SET_KEX_LDFLAGS(intf, ld, flags, cfg)	\
	rvu_write64(rvu, blkaddr,			\
		NPC_AF_INTFX_LDATAX_FLAGSX_CFG(intf, ld, flags), cfg)

static void npc_program_mkex_rx(struct rvu *rvu, int blkaddr,
				struct npc_mcam_kex *mkex, u8 intf)
{
	int lid, lt, ld, fl;

	if (is_npc_intf_tx(intf))
		return;

	rvu_write64(rvu, blkaddr, NPC_AF_INTFX_KEX_CFG(intf),
		    mkex->keyx_cfg[NIX_INTF_RX]);

	/* Program LDATA */
	for (lid = 0; lid < NPC_MAX_LID; lid++) {
		for (lt = 0; lt < NPC_MAX_LT; lt++) {
			for (ld = 0; ld < NPC_MAX_LD; ld++)
				SET_KEX_LD(intf, lid, lt, ld,
					   mkex->intf_lid_lt_ld[NIX_INTF_RX]
					   [lid][lt][ld]);
		}
	}
	/* Program LFLAGS */
	for (ld = 0; ld < NPC_MAX_LD; ld++) {
		for (fl = 0; fl < NPC_MAX_LFL; fl++)
			SET_KEX_LDFLAGS(intf, ld, fl,
					mkex->intf_ld_flags[NIX_INTF_RX]
					[ld][fl]);
	}
}

static void npc_program_mkex_tx(struct rvu *rvu, int blkaddr,
				struct npc_mcam_kex *mkex, u8 intf)
{
	int lid, lt, ld, fl;

	if (is_npc_intf_rx(intf))
		return;

	rvu_write64(rvu, blkaddr, NPC_AF_INTFX_KEX_CFG(intf),
		    mkex->keyx_cfg[NIX_INTF_TX]);

	/* Program LDATA */
	for (lid = 0; lid < NPC_MAX_LID; lid++) {
		for (lt = 0; lt < NPC_MAX_LT; lt++) {
			for (ld = 0; ld < NPC_MAX_LD; ld++)
				SET_KEX_LD(intf, lid, lt, ld,
					   mkex->intf_lid_lt_ld[NIX_INTF_TX]
					   [lid][lt][ld]);
		}
	}
	/* Program LFLAGS */
	for (ld = 0; ld < NPC_MAX_LD; ld++) {
		for (fl = 0; fl < NPC_MAX_LFL; fl++)
			SET_KEX_LDFLAGS(intf, ld, fl,
					mkex->intf_ld_flags[NIX_INTF_TX]
					[ld][fl]);
	}
}

static void npc_program_mkex_profile(struct rvu *rvu, int blkaddr,
				     struct npc_mcam_kex *mkex)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u8 intf;
	int ld;

	for (ld = 0; ld < NPC_MAX_LD; ld++)
		rvu_write64(rvu, blkaddr, NPC_AF_KEX_LDATAX_FLAGS_CFG(ld),
			    mkex->kex_ld_flags[ld]);

	for (intf = 0; intf < hw->npc_intfs; intf++) {
		npc_program_mkex_rx(rvu, blkaddr, mkex, intf);
		npc_program_mkex_tx(rvu, blkaddr, mkex, intf);
	}
}

static int npc_fwdb_prfl_img_map(struct rvu *rvu, void __iomem **prfl_img_addr,
				 u64 *size)
{
	u64 prfl_addr, prfl_sz;

	if (!rvu->fwdata)
		return -EINVAL;

	prfl_addr = rvu->fwdata->mcam_addr;
	prfl_sz = rvu->fwdata->mcam_sz;

	if (!prfl_addr || !prfl_sz)
		return -EINVAL;

	*prfl_img_addr = ioremap_wc(prfl_addr, prfl_sz);
	if (!(*prfl_img_addr))
		return -ENOMEM;

	*size = prfl_sz;

	return 0;
}

/* strtoull of "mkexprof" with base:36 */
#define MKEX_END_SIGN  0xdeadbeef

static void npc_load_mkex_profile(struct rvu *rvu, int blkaddr,
				  const char *mkex_profile)
{
	struct device *dev = &rvu->pdev->dev;
	struct npc_mcam_kex *mcam_kex;
	void __iomem *mkex_prfl_addr = NULL;
	u64 prfl_sz;
	int ret;

	/* If user not selected mkex profile */
	if (rvu->kpu_fwdata_sz ||
	    !strncmp(mkex_profile, def_pfl_name, MKEX_NAME_LEN))
		goto program_mkex;

	/* Setting up the mapping for mkex profile image */
	ret = npc_fwdb_prfl_img_map(rvu, &mkex_prfl_addr, &prfl_sz);
	if (ret < 0)
		goto program_mkex;

	mcam_kex = (struct npc_mcam_kex __force *)mkex_prfl_addr;

	while (((s64)prfl_sz > 0) && (mcam_kex->mkex_sign != MKEX_END_SIGN)) {
		/* Compare with mkex mod_param name string */
		if (mcam_kex->mkex_sign == MKEX_SIGN &&
		    !strncmp(mcam_kex->name, mkex_profile, MKEX_NAME_LEN)) {
			/* Due to an errata (35786) in A0/B0 pass silicon,
			 * parse nibble enable configuration has to be
			 * identical for both Rx and Tx interfaces.
			 */
			if (!is_rvu_96xx_B0(rvu) ||
			    mcam_kex->keyx_cfg[NIX_INTF_RX] == mcam_kex->keyx_cfg[NIX_INTF_TX])
				rvu->kpu.mkex = mcam_kex;
			goto program_mkex;
		}

		mcam_kex++;
		prfl_sz -= sizeof(struct npc_mcam_kex);
	}
	dev_warn(dev, "Failed to load requested profile: %s\n", mkex_profile);

program_mkex:
	dev_info(rvu->dev, "Using %s mkex profile\n", rvu->kpu.mkex->name);
	/* Program selected mkex profile */
	npc_program_mkex_profile(rvu, blkaddr, rvu->kpu.mkex);
	if (mkex_prfl_addr)
		iounmap(mkex_prfl_addr);
}

static void npc_config_kpuaction(struct rvu *rvu, int blkaddr,
				 const struct npc_kpu_profile_action *kpuaction,
				 int kpu, int entry, bool pkind)
{
	struct npc_kpu_action0 action0 = {0};
	struct npc_kpu_action1 action1 = {0};
	u64 reg;

	action1.errlev = kpuaction->errlev;
	action1.errcode = kpuaction->errcode;
	action1.dp0_offset = kpuaction->dp0_offset;
	action1.dp1_offset = kpuaction->dp1_offset;
	action1.dp2_offset = kpuaction->dp2_offset;

	if (pkind)
		reg = NPC_AF_PKINDX_ACTION1(entry);
	else
		reg = NPC_AF_KPUX_ENTRYX_ACTION1(kpu, entry);

	rvu_write64(rvu, blkaddr, reg, *(u64 *)&action1);

	action0.byp_count = kpuaction->bypass_count;
	action0.capture_ena = kpuaction->cap_ena;
	action0.parse_done = kpuaction->parse_done;
	action0.next_state = kpuaction->next_state;
	action0.capture_lid = kpuaction->lid;
	action0.capture_ltype = kpuaction->ltype;
	action0.capture_flags = kpuaction->flags;
	action0.ptr_advance = kpuaction->ptr_advance;
	action0.var_len_offset = kpuaction->offset;
	action0.var_len_mask = kpuaction->mask;
	action0.var_len_right = kpuaction->right;
	action0.var_len_shift = kpuaction->shift;

	if (pkind)
		reg = NPC_AF_PKINDX_ACTION0(entry);
	else
		reg = NPC_AF_KPUX_ENTRYX_ACTION0(kpu, entry);

	rvu_write64(rvu, blkaddr, reg, *(u64 *)&action0);
}

static void npc_config_kpucam(struct rvu *rvu, int blkaddr,
			      const struct npc_kpu_profile_cam *kpucam,
			      int kpu, int entry)
{
	struct npc_kpu_cam cam0 = {0};
	struct npc_kpu_cam cam1 = {0};

	cam1.state = kpucam->state & kpucam->state_mask;
	cam1.dp0_data = kpucam->dp0 & kpucam->dp0_mask;
	cam1.dp1_data = kpucam->dp1 & kpucam->dp1_mask;
	cam1.dp2_data = kpucam->dp2 & kpucam->dp2_mask;

	cam0.state = ~kpucam->state & kpucam->state_mask;
	cam0.dp0_data = ~kpucam->dp0 & kpucam->dp0_mask;
	cam0.dp1_data = ~kpucam->dp1 & kpucam->dp1_mask;
	cam0.dp2_data = ~kpucam->dp2 & kpucam->dp2_mask;

	rvu_write64(rvu, blkaddr,
		    NPC_AF_KPUX_ENTRYX_CAMX(kpu, entry, 0), *(u64 *)&cam0);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_KPUX_ENTRYX_CAMX(kpu, entry, 1), *(u64 *)&cam1);
}

static inline u64 enable_mask(int count)
{
	return (((count) < 64) ? ~(BIT_ULL(count) - 1) : (0x00ULL));
}

static void npc_program_kpu_profile(struct rvu *rvu, int blkaddr, int kpu,
				    const struct npc_kpu_profile *profile)
{
	int entry, num_entries, max_entries;
	u64 entry_mask;

	if (profile->cam_entries != profile->action_entries) {
		dev_err(rvu->dev,
			"KPU%d: CAM and action entries [%d != %d] not equal\n",
			kpu, profile->cam_entries, profile->action_entries);
	}

	max_entries = rvu->hw->npc_kpu_entries;

	/* Program CAM match entries for previous KPU extracted data */
	num_entries = min_t(int, profile->cam_entries, max_entries);
	for (entry = 0; entry < num_entries; entry++)
		npc_config_kpucam(rvu, blkaddr,
				  &profile->cam[entry], kpu, entry);

	/* Program this KPU's actions */
	num_entries = min_t(int, profile->action_entries, max_entries);
	for (entry = 0; entry < num_entries; entry++)
		npc_config_kpuaction(rvu, blkaddr, &profile->action[entry],
				     kpu, entry, false);

	/* Enable all programmed entries */
	num_entries = min_t(int, profile->action_entries, profile->cam_entries);
	entry_mask = enable_mask(num_entries);
	/* Disable first KPU_MAX_CST_ENT entries for built-in profile */
	if (!rvu->kpu.custom)
		entry_mask |= GENMASK_ULL(KPU_MAX_CST_ENT - 1, 0);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_KPUX_ENTRY_DISX(kpu, 0), entry_mask);
	if (num_entries > 64) {
		rvu_write64(rvu, blkaddr,
			    NPC_AF_KPUX_ENTRY_DISX(kpu, 1),
			    enable_mask(num_entries - 64));
	}

	/* Enable this KPU */
	rvu_write64(rvu, blkaddr, NPC_AF_KPUX_CFG(kpu), 0x01);
}

static int npc_prepare_default_kpu(struct npc_kpu_profile_adapter *profile)
{
	profile->custom = 0;
	profile->name = def_pfl_name;
	profile->version = NPC_KPU_PROFILE_VER;
	profile->ikpu = ikpu_action_entries;
	profile->pkinds = ARRAY_SIZE(ikpu_action_entries);
	profile->kpu = npc_kpu_profiles;
	profile->kpus = ARRAY_SIZE(npc_kpu_profiles);
	profile->lt_def = &npc_lt_defaults;
	profile->mkex = &npc_mkex_default;

	return 0;
}

static int npc_apply_custom_kpu(struct rvu *rvu,
				struct npc_kpu_profile_adapter *profile)
{
	size_t hdr_sz = sizeof(struct npc_kpu_profile_fwdata), offset = 0;
	struct npc_kpu_profile_fwdata *fw = rvu->kpu_fwdata;
	struct npc_kpu_profile_action *action;
	struct npc_kpu_profile_cam *cam;
	struct npc_kpu_fwdata *fw_kpu;
	int entries;
	u16 kpu, entry;

	if (rvu->kpu_fwdata_sz < hdr_sz) {
		dev_warn(rvu->dev, "Invalid KPU profile size\n");
		return -EINVAL;
	}
	if (le64_to_cpu(fw->signature) != KPU_SIGN) {
		dev_warn(rvu->dev, "Invalid KPU profile signature %llx\n",
			 fw->signature);
		return -EINVAL;
	}
	/* Verify if the using known profile structure */
	if (NPC_KPU_VER_MAJ(profile->version) >
	    NPC_KPU_VER_MAJ(NPC_KPU_PROFILE_VER)) {
		dev_warn(rvu->dev, "Not supported Major version: %d > %d\n",
			 NPC_KPU_VER_MAJ(profile->version),
			 NPC_KPU_VER_MAJ(NPC_KPU_PROFILE_VER));
		return -EINVAL;
	}
	/* Verify if profile is aligned with the required kernel changes */
	if (NPC_KPU_VER_MIN(profile->version) <
	    NPC_KPU_VER_MIN(NPC_KPU_PROFILE_VER)) {
		dev_warn(rvu->dev,
			 "Invalid KPU profile version: %d.%d.%d expected version <= %d.%d.%d\n",
			 NPC_KPU_VER_MAJ(profile->version),
			 NPC_KPU_VER_MIN(profile->version),
			 NPC_KPU_VER_PATCH(profile->version),
			 NPC_KPU_VER_MAJ(NPC_KPU_PROFILE_VER),
			 NPC_KPU_VER_MIN(NPC_KPU_PROFILE_VER),
			 NPC_KPU_VER_PATCH(NPC_KPU_PROFILE_VER));
		return -EINVAL;
	}
	/* Verify if profile fits the HW */
	if (fw->kpus > profile->kpus) {
		dev_warn(rvu->dev, "Not enough KPUs: %d > %ld\n", fw->kpus,
			 profile->kpus);
		return -EINVAL;
	}

	profile->custom = 1;
	profile->name = fw->name;
	profile->version = le64_to_cpu(fw->version);
	profile->mkex = &fw->mkex;
	profile->lt_def = &fw->lt_def;

	for (kpu = 0; kpu < fw->kpus; kpu++) {
		fw_kpu = (struct npc_kpu_fwdata *)(fw->data + offset);
		if (fw_kpu->entries > KPU_MAX_CST_ENT)
			dev_warn(rvu->dev,
				 "Too many custom entries on KPU%d: %d > %d\n",
				 kpu, fw_kpu->entries, KPU_MAX_CST_ENT);
		entries = min(fw_kpu->entries, KPU_MAX_CST_ENT);
		cam = (struct npc_kpu_profile_cam *)fw_kpu->data;
		offset += sizeof(*fw_kpu) + fw_kpu->entries * sizeof(*cam);
		action = (struct npc_kpu_profile_action *)(fw->data + offset);
		offset += fw_kpu->entries * sizeof(*action);
		if (rvu->kpu_fwdata_sz < hdr_sz + offset) {
			dev_warn(rvu->dev,
				 "Profile size mismatch on KPU%i parsing.\n",
				 kpu + 1);
			return -EINVAL;
		}
		for (entry = 0; entry < entries; entry++) {
			profile->kpu[kpu].cam[entry] = cam[entry];
			profile->kpu[kpu].action[entry] = action[entry];
		}
	}

	return 0;
}

static int npc_load_kpu_prfl_img(struct rvu *rvu, void __iomem *prfl_addr,
				 u64 prfl_sz, const char *kpu_profile)
{
	struct npc_kpu_profile_fwdata *kpu_data = NULL;
	int rc = -EINVAL;

	kpu_data = (struct npc_kpu_profile_fwdata __force *)prfl_addr;
	if (le64_to_cpu(kpu_data->signature) == KPU_SIGN &&
	    !strncmp(kpu_data->name, kpu_profile, KPU_NAME_LEN)) {
		dev_info(rvu->dev, "Loading KPU profile from firmware db: %s\n",
			 kpu_profile);
		rvu->kpu_fwdata = kpu_data;
		rvu->kpu_fwdata_sz = prfl_sz;
		rvu->kpu_prfl_addr = prfl_addr;
		rc = 0;
	}

	return rc;
}

static int npc_fwdb_detect_load_prfl_img(struct rvu *rvu, uint64_t prfl_sz,
					 const char *kpu_profile)
{
	struct npc_coalesced_kpu_prfl *img_data = NULL;
	int i = 0, rc = -EINVAL;
	void __iomem *kpu_prfl_addr;
	u16 offset;

	img_data = (struct npc_coalesced_kpu_prfl __force *)rvu->kpu_prfl_addr;
	if (le64_to_cpu(img_data->signature) == KPU_SIGN &&
	    !strncmp(img_data->name, kpu_profile, KPU_NAME_LEN)) {
		/* Loaded profile is a single KPU profile. */
		rc = npc_load_kpu_prfl_img(rvu, rvu->kpu_prfl_addr,
					   prfl_sz, kpu_profile);
		goto done;
	}

	/* Loaded profile is coalesced image, offset of first KPU profile.*/
	offset = offsetof(struct npc_coalesced_kpu_prfl, prfl_sz) +
		(img_data->num_prfl * sizeof(uint16_t));
	/* Check if mapped image is coalesced image. */
	while (i < img_data->num_prfl) {
		/* Profile image offsets are rounded up to next 8 multiple.*/
		offset = ALIGN_8B_CEIL(offset);
		kpu_prfl_addr = (void __iomem *)((uintptr_t)rvu->kpu_prfl_addr +
					 offset);
		rc = npc_load_kpu_prfl_img(rvu, kpu_prfl_addr,
					   img_data->prfl_sz[i], kpu_profile);
		if (!rc)
			break;
		/* Calculating offset of profile image based on profile size.*/
		offset += img_data->prfl_sz[i];
		i++;
	}
done:
	return rc;
}

static int npc_load_kpu_profile_fwdb(struct rvu *rvu, const char *kpu_profile)
{
	int ret = -EINVAL;
	u64 prfl_sz;

	/* Setting up the mapping for NPC profile image */
	ret = npc_fwdb_prfl_img_map(rvu, &rvu->kpu_prfl_addr, &prfl_sz);
	if (ret < 0)
		goto done;

	/* Detect if profile is coalesced or single KPU profile and load */
	ret = npc_fwdb_detect_load_prfl_img(rvu, prfl_sz, kpu_profile);
	if (ret == 0)
		goto done;

	/* Cleaning up if KPU profile image from fwdata is not valid. */
	if (rvu->kpu_prfl_addr) {
		iounmap(rvu->kpu_prfl_addr);
		rvu->kpu_prfl_addr = NULL;
		rvu->kpu_fwdata_sz = 0;
		rvu->kpu_fwdata = NULL;
	}

done:
	return ret;
}

static void npc_load_kpu_profile(struct rvu *rvu)
{
	struct npc_kpu_profile_adapter *profile = &rvu->kpu;
	const char *kpu_profile = rvu->kpu_pfl_name;
	const struct firmware *fw = NULL;
	bool retry_fwdb = false;

	/* If user not specified profile customization */
	if (!strncmp(kpu_profile, def_pfl_name, KPU_NAME_LEN))
		goto revert_to_default;
	/* First prepare default KPU, then we'll customize top entries. */
	npc_prepare_default_kpu(profile);

	/* Order of preceedence for load loading NPC profile (high to low)
	 * Firmware binary in filesystem.
	 * Firmware database method.
	 * Default KPU profile.
	 */
	if (!request_firmware(&fw, kpu_profile, rvu->dev)) {
		dev_info(rvu->dev, "Loading KPU profile from firmware: %s\n",
			 kpu_profile);
		rvu->kpu_fwdata = kzalloc(fw->size, GFP_KERNEL);
		if (rvu->kpu_fwdata) {
			memcpy(rvu->kpu_fwdata, fw->data, fw->size);
			rvu->kpu_fwdata_sz = fw->size;
		}
		release_firmware(fw);
		retry_fwdb = true;
		goto program_kpu;
	}

load_image_fwdb:
	/* Loading the KPU profile using firmware database */
	if (npc_load_kpu_profile_fwdb(rvu, kpu_profile))
		goto revert_to_default;

program_kpu:
	/* Apply profile customization if firmware was loaded. */
	if (!rvu->kpu_fwdata_sz || npc_apply_custom_kpu(rvu, profile)) {
		/* If image from firmware filesystem fails to load or invalid
		 * retry with firmware database method.
		 */
		if (rvu->kpu_fwdata || rvu->kpu_fwdata_sz) {
			/* Loading image from firmware database failed. */
			if (rvu->kpu_prfl_addr) {
				iounmap(rvu->kpu_prfl_addr);
				rvu->kpu_prfl_addr = NULL;
			} else {
				kfree(rvu->kpu_fwdata);
			}
			rvu->kpu_fwdata = NULL;
			rvu->kpu_fwdata_sz = 0;
			if (retry_fwdb) {
				retry_fwdb = false;
				goto load_image_fwdb;
			}
		}

		dev_warn(rvu->dev,
			 "Can't load KPU profile %s. Using default.\n",
			 kpu_profile);
		kfree(rvu->kpu_fwdata);
		rvu->kpu_fwdata = NULL;
		goto revert_to_default;
	}

	dev_info(rvu->dev, "Using custom profile '%s', version %d.%d.%d\n",
		 profile->name, NPC_KPU_VER_MAJ(profile->version),
		 NPC_KPU_VER_MIN(profile->version),
		 NPC_KPU_VER_PATCH(profile->version));

	return;

revert_to_default:
	npc_prepare_default_kpu(profile);
}

static void npc_parser_profile_init(struct rvu *rvu, int blkaddr)
{
	struct rvu_hwinfo *hw = rvu->hw;
	int num_pkinds, num_kpus, idx;

	/* Disable all KPUs and their entries */
	for (idx = 0; idx < hw->npc_kpus; idx++) {
		rvu_write64(rvu, blkaddr,
			    NPC_AF_KPUX_ENTRY_DISX(idx, 0), ~0ULL);
		rvu_write64(rvu, blkaddr,
			    NPC_AF_KPUX_ENTRY_DISX(idx, 1), ~0ULL);
		rvu_write64(rvu, blkaddr, NPC_AF_KPUX_CFG(idx), 0x00);
	}

	/* Load and customize KPU profile. */
	npc_load_kpu_profile(rvu);

	/* First program IKPU profile i.e PKIND configs.
	 * Check HW max count to avoid configuring junk or
	 * writing to unsupported CSR addresses.
	 */
	num_pkinds = rvu->kpu.pkinds;
	num_pkinds = min_t(int, hw->npc_pkinds, num_pkinds);

	for (idx = 0; idx < num_pkinds; idx++)
		npc_config_kpuaction(rvu, blkaddr, &rvu->kpu.ikpu[idx], 0, idx, true);

	/* Program KPU CAM and Action profiles */
	num_kpus = rvu->kpu.kpus;
	num_kpus = min_t(int, hw->npc_kpus, num_kpus);

	for (idx = 0; idx < num_kpus; idx++)
		npc_program_kpu_profile(rvu, blkaddr, idx, &rvu->kpu.kpu[idx]);
}

static int npc_mcam_rsrcs_init(struct rvu *rvu, int blkaddr)
{
	int nixlf_count = rvu_get_nixlf_count(rvu);
	struct npc_mcam *mcam = &rvu->hw->mcam;
	int rsvd, err;
	u16 index;
	int cntr;
	u64 cfg;

	/* Actual number of MCAM entries vary by entry size */
	cfg = (rvu_read64(rvu, blkaddr,
			  NPC_AF_INTFX_KEX_CFG(0)) >> 32) & 0x07;
	mcam->total_entries = (mcam->banks / BIT_ULL(cfg)) * mcam->banksize;
	mcam->keysize = cfg;

	/* Number of banks combined per MCAM entry */
	if (cfg == NPC_MCAM_KEY_X4)
		mcam->banks_per_entry = 4;
	else if (cfg == NPC_MCAM_KEY_X2)
		mcam->banks_per_entry = 2;
	else
		mcam->banks_per_entry = 1;

	/* Reserve one MCAM entry for each of the NIX LF to
	 * guarantee space to install default matching DMAC rule.
	 * Also reserve 2 MCAM entries for each PF for default
	 * channel based matching or 'bcast & promisc' matching to
	 * support BCAST and PROMISC modes of operation for PFs.
	 * PF0 is excluded.
	 */
	rsvd = (nixlf_count * RSVD_MCAM_ENTRIES_PER_NIXLF) +
		((rvu->hw->total_pfs - 1) * RSVD_MCAM_ENTRIES_PER_PF);
	if (mcam->total_entries <= rsvd) {
		dev_warn(rvu->dev,
			 "Insufficient NPC MCAM size %d for pkt I/O, exiting\n",
			 mcam->total_entries);
		return -ENOMEM;
	}

	mcam->bmap_entries = mcam->total_entries - rsvd;
	mcam->nixlf_offset = mcam->bmap_entries;
	mcam->pf_offset = mcam->nixlf_offset + nixlf_count;

	/* Allocate bitmaps for managing MCAM entries */
	mcam->bmap = devm_kcalloc(rvu->dev, BITS_TO_LONGS(mcam->bmap_entries),
				  sizeof(long), GFP_KERNEL);
	if (!mcam->bmap)
		return -ENOMEM;

	mcam->bmap_reverse = devm_kcalloc(rvu->dev,
					  BITS_TO_LONGS(mcam->bmap_entries),
					  sizeof(long), GFP_KERNEL);
	if (!mcam->bmap_reverse)
		return -ENOMEM;

	mcam->bmap_fcnt = mcam->bmap_entries;

	/* Alloc memory for saving entry to RVU PFFUNC allocation mapping */
	mcam->entry2pfvf_map = devm_kcalloc(rvu->dev, mcam->bmap_entries,
					    sizeof(u16), GFP_KERNEL);
	if (!mcam->entry2pfvf_map)
		return -ENOMEM;

	/* Reserve 1/8th of MCAM entries at the bottom for low priority
	 * allocations and another 1/8th at the top for high priority
	 * allocations.
	 */
	mcam->lprio_count = mcam->bmap_entries / 8;
	if (mcam->lprio_count > BITS_PER_LONG)
		mcam->lprio_count = round_down(mcam->lprio_count,
					       BITS_PER_LONG);
	mcam->lprio_start = mcam->bmap_entries - mcam->lprio_count;
	mcam->hprio_count = mcam->lprio_count;
	mcam->hprio_end = mcam->hprio_count;


	/* Allocate bitmap for managing MCAM counters and memory
	 * for saving counter to RVU PFFUNC allocation mapping.
	 */
	err = rvu_alloc_bitmap(&mcam->counters);
	if (err)
		return err;

	mcam->cntr2pfvf_map = devm_kcalloc(rvu->dev, mcam->counters.max,
					   sizeof(u16), GFP_KERNEL);
	if (!mcam->cntr2pfvf_map)
		goto free_mem;

	/* Alloc memory for MCAM entry to counter mapping and for tracking
	 * counter's reference count.
	 */
	mcam->entry2cntr_map = devm_kcalloc(rvu->dev, mcam->bmap_entries,
					    sizeof(u16), GFP_KERNEL);
	if (!mcam->entry2cntr_map)
		goto free_mem;

	mcam->cntr_refcnt = devm_kcalloc(rvu->dev, mcam->counters.max,
					 sizeof(u16), GFP_KERNEL);
	if (!mcam->cntr_refcnt)
		goto free_mem;

	/* Alloc memory for saving target device of mcam rule */
	mcam->entry2target_pffunc = devm_kcalloc(rvu->dev, mcam->total_entries,
						 sizeof(u16), GFP_KERNEL);
	if (!mcam->entry2target_pffunc)
		goto free_mem;

	for (index = 0; index < mcam->bmap_entries; index++) {
		mcam->entry2pfvf_map[index] = NPC_MCAM_INVALID_MAP;
		mcam->entry2cntr_map[index] = NPC_MCAM_INVALID_MAP;
	}

	for (cntr = 0; cntr < mcam->counters.max; cntr++)
		mcam->cntr2pfvf_map[cntr] = NPC_MCAM_INVALID_MAP;

	mutex_init(&mcam->lock);

	return 0;

free_mem:
	kfree(mcam->counters.bmap);
	return -ENOMEM;
}

static void rvu_npc_hw_init(struct rvu *rvu, int blkaddr)
{
	struct npc_pkind *pkind = &rvu->hw->pkind;
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct rvu_hwinfo *hw = rvu->hw;
	u64 npc_const, npc_const1;
	u64 npc_const2 = 0;

	npc_const = rvu_read64(rvu, blkaddr, NPC_AF_CONST);
	npc_const1 = rvu_read64(rvu, blkaddr, NPC_AF_CONST1);
	if (npc_const1 & BIT_ULL(63))
		npc_const2 = rvu_read64(rvu, blkaddr, NPC_AF_CONST2);

	pkind->rsrc.max = NPC_UNRESERVED_PKIND_COUNT;
	hw->npc_pkinds = (npc_const1 >> 12) & 0xFFULL;
	hw->npc_kpu_entries = npc_const1 & 0xFFFULL;
	hw->npc_kpus = (npc_const >> 8) & 0x1FULL;
	hw->npc_intfs = npc_const & 0xFULL;
	hw->npc_counters = (npc_const >> 48) & 0xFFFFULL;

	mcam->banks = (npc_const >> 44) & 0xFULL;
	mcam->banksize = (npc_const >> 28) & 0xFFFFULL;
	hw->npc_stat_ena = BIT_ULL(9);
	/* Extended set */
	if (npc_const2) {
		hw->npc_ext_set = true;
		/* 96xx supports only match_stats and npc_counters
		 * reflected in NPC_AF_CONST reg.
		 * STAT_SEL and ENA are at [0:8] and 9 bit positions.
		 * 98xx has both match_stat and ext and npc_counter
		 * reflected in NPC_AF_CONST2
		 * STAT_SEL_EXT added at [12:14] bit position.
		 * cn10k supports only ext and hence npc_counters in
		 * NPC_AF_CONST is 0 and npc_counters reflected in NPC_AF_CONST2.
		 * STAT_SEL bitpos incremented from [0:8] to [0:11] and ENA bit moved to 63
		 */
		if (!hw->npc_counters)
			hw->npc_stat_ena = BIT_ULL(63);
		hw->npc_counters = (npc_const2 >> 16) & 0xFFFFULL;
		mcam->banksize = npc_const2 & 0xFFFFULL;
	}

	mcam->counters.max = hw->npc_counters;
}

static void rvu_npc_setup_interfaces(struct rvu *rvu, int blkaddr)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct rvu_hwinfo *hw = rvu->hw;
	u64 nibble_ena, rx_kex, tx_kex;
	u8 intf;

	/* Reserve last counter for MCAM RX miss action which is set to
	 * drop packet. This way we will know how many pkts didn't match
	 * any MCAM entry.
	 */
	mcam->counters.max--;
	mcam->rx_miss_act_cntr = mcam->counters.max;

	rx_kex = npc_mkex_default.keyx_cfg[NIX_INTF_RX];
	tx_kex = npc_mkex_default.keyx_cfg[NIX_INTF_TX];
	nibble_ena = FIELD_GET(NPC_PARSE_NIBBLE, rx_kex);

	nibble_ena = rvu_npc_get_tx_nibble_cfg(rvu, nibble_ena);
	if (nibble_ena) {
		tx_kex &= ~NPC_PARSE_NIBBLE;
		tx_kex |= FIELD_PREP(NPC_PARSE_NIBBLE, nibble_ena);
		npc_mkex_default.keyx_cfg[NIX_INTF_TX] = tx_kex;
	}

	/* Configure RX interfaces */
	for (intf = 0; intf < hw->npc_intfs; intf++) {
		if (is_npc_intf_tx(intf))
			continue;

		/* Set RX MCAM search key size. LA..LE (ltype only) + Channel */
		rvu_write64(rvu, blkaddr, NPC_AF_INTFX_KEX_CFG(intf),
			    rx_kex);

		/* If MCAM lookup doesn't result in a match, drop the received
		 * packet. And map this action to a counter to count dropped
		 * packets.
		 */
		rvu_write64(rvu, blkaddr,
			    NPC_AF_INTFX_MISS_ACT(intf), NIX_RX_ACTIONOP_DROP);

		/* NPC_AF_INTFX_MISS_STAT_ACT[14:12] - counter[11:9]
		 * NPC_AF_INTFX_MISS_STAT_ACT[8:0] - counter[8:0]
		 */
		rvu_write64(rvu, blkaddr,
			    NPC_AF_INTFX_MISS_STAT_ACT(intf),
			    ((mcam->rx_miss_act_cntr >> 9) << 12) |
			    hw->npc_stat_ena | mcam->rx_miss_act_cntr);
	}

	/* Configure TX interfaces */
	for (intf = 0; intf < hw->npc_intfs; intf++) {
		if (is_npc_intf_rx(intf))
			continue;

		/* Extract Ltypes LID_LA to LID_LE */
		rvu_write64(rvu, blkaddr, NPC_AF_INTFX_KEX_CFG(intf),
			    tx_kex);

		/* Set TX miss action to UCAST_DEFAULT i.e
		 * transmit the packet on NIX LF SQ's default channel.
		 */
		rvu_write64(rvu, blkaddr,
			    NPC_AF_INTFX_MISS_ACT(intf),
			    NIX_TX_ACTIONOP_UCAST_DEFAULT);
	}
}

int rvu_npc_init(struct rvu *rvu)
{
	struct npc_kpu_profile_adapter *kpu = &rvu->kpu;
	struct npc_pkind *pkind = &rvu->hw->pkind;
	struct npc_mcam *mcam = &rvu->hw->mcam;
	int blkaddr, entry, bank, err;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0) {
		dev_err(rvu->dev, "%s: NPC block not implemented\n", __func__);
		return -ENODEV;
	}

	rvu_npc_hw_init(rvu, blkaddr);

	/* First disable all MCAM entries, to stop traffic towards NIXLFs */
	for (bank = 0; bank < mcam->banks; bank++) {
		for (entry = 0; entry < mcam->banksize; entry++)
			rvu_write64(rvu, blkaddr,
				    NPC_AF_MCAMEX_BANKX_CFG(entry, bank), 0);
	}

	err = rvu_alloc_bitmap(&pkind->rsrc);
	if (err)
		return err;
	/* Reserve PKIND#0 for LBKs. Power reset value of LBK_CH_PKIND is '0',
	 * no need to configure PKIND for all LBKs separately.
	 */
	rvu_alloc_rsrc(&pkind->rsrc);

	/* Allocate mem for pkind to PF and channel mapping info */
	pkind->pfchan_map = devm_kcalloc(rvu->dev, pkind->rsrc.max,
					 sizeof(u32), GFP_KERNEL);
	if (!pkind->pfchan_map)
		return -ENOMEM;

	/* Configure KPU profile */
	npc_parser_profile_init(rvu, blkaddr);

	/* Config Outer L2, IPv4's NPC layer info */
	rvu_write64(rvu, blkaddr, NPC_AF_PCK_DEF_OL2,
		    (kpu->lt_def->pck_ol2.lid << 8) | (kpu->lt_def->pck_ol2.ltype_match << 4) |
		    kpu->lt_def->pck_ol2.ltype_mask);
	rvu_write64(rvu, blkaddr, NPC_AF_PCK_DEF_OIP4,
		    (kpu->lt_def->pck_oip4.lid << 8) | (kpu->lt_def->pck_oip4.ltype_match << 4) |
		    kpu->lt_def->pck_oip4.ltype_mask);

	/* Config Inner IPV4 NPC layer info */
	rvu_write64(rvu, blkaddr, NPC_AF_PCK_DEF_IIP4,
		    (kpu->lt_def->pck_iip4.lid << 8) | (kpu->lt_def->pck_iip4.ltype_match << 4) |
		    kpu->lt_def->pck_iip4.ltype_mask);

	/* Enable below for Rx pkts.
	 * - Outer IPv4 header checksum validation.
	 * - Detect outer L2 broadcast address and set NPC_RESULT_S[L2M].
	 * - Inner IPv4 header checksum validation.
	 * - Set non zero checksum error code value
	 */
	rvu_write64(rvu, blkaddr, NPC_AF_PCK_CFG,
		    rvu_read64(rvu, blkaddr, NPC_AF_PCK_CFG) |
		    BIT_ULL(32) | BIT_ULL(24) | BIT_ULL(6) |
		    BIT_ULL(2) | BIT_ULL(1));

	rvu_npc_setup_interfaces(rvu, blkaddr);

	/* Configure MKEX profile */
	npc_load_mkex_profile(rvu, blkaddr, rvu->mkex_pfl_name);

	err = npc_mcam_rsrcs_init(rvu, blkaddr);
	if (err)
		return err;

	err = npc_flow_steering_init(rvu, blkaddr);
	if (err) {
		dev_err(rvu->dev,
			"Incorrect mkex profile loaded using default mkex\n");
		npc_load_mkex_profile(rvu, blkaddr, def_pfl_name);
	}

	return 0;
}

void rvu_npc_freemem(struct rvu *rvu)
{
	struct npc_pkind *pkind = &rvu->hw->pkind;
	struct npc_mcam *mcam = &rvu->hw->mcam;

	kfree(pkind->rsrc.bmap);
	kfree(mcam->counters.bmap);
	if (rvu->kpu_prfl_addr)
		iounmap(rvu->kpu_prfl_addr);
	else
		kfree(rvu->kpu_fwdata);
	mutex_destroy(&mcam->lock);
}

void rvu_npc_get_mcam_entry_alloc_info(struct rvu *rvu, u16 pcifunc,
				       int blkaddr, int *alloc_cnt,
				       int *enable_cnt)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	int entry;

	*alloc_cnt = 0;
	*enable_cnt = 0;

	for (entry = 0; entry < mcam->bmap_entries; entry++) {
		if (mcam->entry2pfvf_map[entry] == pcifunc) {
			(*alloc_cnt)++;
			if (is_mcam_entry_enabled(rvu, mcam, blkaddr, entry))
				(*enable_cnt)++;
		}
	}
}

void rvu_npc_get_mcam_counter_alloc_info(struct rvu *rvu, u16 pcifunc,
					 int blkaddr, int *alloc_cnt,
					 int *enable_cnt)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	int cntr;

	*alloc_cnt = 0;
	*enable_cnt = 0;

	for (cntr = 0; cntr < mcam->counters.max; cntr++) {
		if (mcam->cntr2pfvf_map[cntr] == pcifunc) {
			(*alloc_cnt)++;
			if (mcam->cntr_refcnt[cntr])
				(*enable_cnt)++;
		}
	}
}

static int npc_mcam_verify_entry(struct npc_mcam *mcam,
				 u16 pcifunc, int entry)
{
	/* verify AF installed entries */
	if (is_pffunc_af(pcifunc))
		return 0;
	/* Verify if entry is valid and if it is indeed
	 * allocated to the requesting PFFUNC.
	 */
	if (entry >= mcam->bmap_entries)
		return NPC_MCAM_INVALID_REQ;

	if (pcifunc != mcam->entry2pfvf_map[entry])
		return NPC_MCAM_PERM_DENIED;

	return 0;
}

static int npc_mcam_verify_counter(struct npc_mcam *mcam,
				   u16 pcifunc, int cntr)
{
	/* Verify if counter is valid and if it is indeed
	 * allocated to the requesting PFFUNC.
	 */
	if (cntr >= mcam->counters.max)
		return NPC_MCAM_INVALID_REQ;

	if (pcifunc != mcam->cntr2pfvf_map[cntr])
		return NPC_MCAM_PERM_DENIED;

	return 0;
}

static void npc_map_mcam_entry_and_cntr(struct rvu *rvu, struct npc_mcam *mcam,
					int blkaddr, u16 entry, u16 cntr)
{
	u16 index = entry & (mcam->banksize - 1);
	u32 bank = npc_get_bank(mcam, entry);
	struct rvu_hwinfo *hw = rvu->hw;

	/* Set mapping and increment counter's refcnt */
	mcam->entry2cntr_map[entry] = cntr;
	mcam->cntr_refcnt[cntr]++;
	/* Enable stats */
	rvu_write64(rvu, blkaddr,
		    NPC_AF_MCAMEX_BANKX_STAT_ACT(index, bank),
		    ((cntr >> 9) << 12) | hw->npc_stat_ena | cntr);
}

static void npc_unmap_mcam_entry_and_cntr(struct rvu *rvu,
					  struct npc_mcam *mcam,
					  int blkaddr, u16 entry, u16 cntr)
{
	u16 index = entry & (mcam->banksize - 1);
	u16 bank = npc_get_bank(mcam, entry);

	/* Remove mapping and reduce counter's refcnt */
	mcam->entry2cntr_map[entry] = NPC_MCAM_INVALID_MAP;
	mcam->cntr_refcnt[cntr]--;
	/* Disable stats */
	rvu_write64(rvu, blkaddr,
		    NPC_AF_MCAMEX_BANKX_STAT_ACT(index, bank), 0x00);
}

/* Sets MCAM entry in bitmap as used. Update
 * reverse bitmap too. Should be called with
 * 'mcam->lock' held.
 */
static void npc_mcam_set_bit(struct npc_mcam *mcam, u16 index)
{
	u16 entry, rentry;

	entry = index;
	rentry = mcam->bmap_entries - index - 1;

	__set_bit(entry, mcam->bmap);
	__set_bit(rentry, mcam->bmap_reverse);
	mcam->bmap_fcnt--;
}

/* Sets MCAM entry in bitmap as free. Update
 * reverse bitmap too. Should be called with
 * 'mcam->lock' held.
 */
static void npc_mcam_clear_bit(struct npc_mcam *mcam, u16 index)
{
	u16 entry, rentry;

	entry = index;
	rentry = mcam->bmap_entries - index - 1;

	__clear_bit(entry, mcam->bmap);
	__clear_bit(rentry, mcam->bmap_reverse);
	mcam->bmap_fcnt++;
}

static void npc_mcam_free_all_entries(struct rvu *rvu, struct npc_mcam *mcam,
				      int blkaddr, u16 pcifunc)
{
	u16 index, cntr;

	/* Scan all MCAM entries and free the ones mapped to 'pcifunc' */
	for (index = 0; index < mcam->bmap_entries; index++) {
		if (mcam->entry2pfvf_map[index] == pcifunc) {
			mcam->entry2pfvf_map[index] = NPC_MCAM_INVALID_MAP;
			/* Free the entry in bitmap */
			npc_mcam_clear_bit(mcam, index);
			/* Disable the entry */
			npc_enable_mcam_entry(rvu, mcam, blkaddr, index, false);

			/* Update entry2counter mapping */
			cntr = mcam->entry2cntr_map[index];
			if (cntr != NPC_MCAM_INVALID_MAP)
				npc_unmap_mcam_entry_and_cntr(rvu, mcam,
							      blkaddr, index,
							      cntr);
			mcam->entry2target_pffunc[index] = 0x0;
		}
	}
}

static void npc_mcam_free_all_counters(struct rvu *rvu, struct npc_mcam *mcam,
				       u16 pcifunc)
{
	u16 cntr;

	/* Scan all MCAM counters and free the ones mapped to 'pcifunc' */
	for (cntr = 0; cntr < mcam->counters.max; cntr++) {
		if (mcam->cntr2pfvf_map[cntr] == pcifunc) {
			mcam->cntr2pfvf_map[cntr] = NPC_MCAM_INVALID_MAP;
			mcam->cntr_refcnt[cntr] = 0;
			rvu_free_rsrc(&mcam->counters, cntr);
			/* This API is expected to be called after freeing
			 * MCAM entries, which inturn will remove
			 * 'entry to counter' mapping.
			 * No need to do it again.
			 */
		}
	}
}

/* Find area of contiguous free entries of size 'nr'.
 * If not found return max contiguous free entries available.
 */
static u16 npc_mcam_find_zero_area(unsigned long *map, u16 size, u16 start,
				   u16 nr, u16 *max_area)
{
	u16 max_area_start = 0;
	u16 index, next, end;

	*max_area = 0;

again:
	index = find_next_zero_bit(map, size, start);
	if (index >= size)
		return max_area_start;

	end = ((index + nr) >= size) ? size : index + nr;
	next = find_next_bit(map, end, index);
	if (*max_area < (next - index)) {
		*max_area = next - index;
		max_area_start = index;
	}

	if (next < end) {
		start = next + 1;
		goto again;
	}

	return max_area_start;
}

/* Find number of free MCAM entries available
 * within range i.e in between 'start' and 'end'.
 */
static u16 npc_mcam_get_free_count(unsigned long *map, u16 start, u16 end)
{
	u16 index, next;
	u16 fcnt = 0;

again:
	if (start >= end)
		return fcnt;

	index = find_next_zero_bit(map, end, start);
	if (index >= end)
		return fcnt;

	next = find_next_bit(map, end, index);
	if (next <= end) {
		fcnt += next - index;
		start = next + 1;
		goto again;
	}

	fcnt += end - index;
	return fcnt;
}

static void
npc_get_mcam_search_range_priority(struct npc_mcam *mcam,
				   struct npc_mcam_alloc_entry_req *req,
				   u16 *start, u16 *end, bool *reverse)
{
	u16 fcnt;

	if (req->priority == NPC_MCAM_HIGHER_PRIO)
		goto hprio;

	/* For a low priority entry allocation
	 * - If reference entry is not in hprio zone then
	 *      search range: ref_entry to end.
	 * - If reference entry is in hprio zone and if
	 *   request can be accomodated in non-hprio zone then
	 *      search range: 'start of middle zone' to 'end'
	 * - else search in reverse, so that less number of hprio
	 *   zone entries are allocated.
	 */

	*reverse = false;
	*start = req->ref_entry + 1;
	*end = mcam->bmap_entries;

	if (req->ref_entry >= mcam->hprio_end)
		return;

	fcnt = npc_mcam_get_free_count(mcam->bmap,
				       mcam->hprio_end, mcam->bmap_entries);
	if (fcnt > req->count)
		*start = mcam->hprio_end;
	else
		*reverse = true;
	return;

hprio:
	/* For a high priority entry allocation, search is always
	 * in reverse to preserve hprio zone entries.
	 * - If reference entry is not in lprio zone then
	 *      search range: 0 to ref_entry.
	 * - If reference entry is in lprio zone and if
	 *   request can be accomodated in middle zone then
	 *      search range: 'hprio_end' to 'lprio_start'
	 */

	*reverse = true;
	*start = 0;
	*end = req->ref_entry;

	if (req->ref_entry <= mcam->lprio_start)
		return;

	fcnt = npc_mcam_get_free_count(mcam->bmap,
				       mcam->hprio_end, mcam->lprio_start);
	if (fcnt < req->count)
		return;
	*start = mcam->hprio_end;
	*end = mcam->lprio_start;
}

static int npc_mcam_alloc_entries(struct npc_mcam *mcam, u16 pcifunc,
				  struct npc_mcam_alloc_entry_req *req,
				  struct npc_mcam_alloc_entry_rsp *rsp)
{
	u16 entry_list[NPC_MAX_NONCONTIG_ENTRIES];
	u16 fcnt, hp_fcnt, lp_fcnt;
	u16 start, end, index;
	int entry, next_start;
	bool reverse = false;
	unsigned long *bmap;
	u16 max_contig;

	mutex_lock(&mcam->lock);

	/* Check if there are any free entries */
	if (!mcam->bmap_fcnt) {
		mutex_unlock(&mcam->lock);
		return NPC_MCAM_ALLOC_FAILED;
	}

	/* MCAM entries are divided into high priority, middle and
	 * low priority zones. Idea is to not allocate top and lower
	 * most entries as much as possible, this is to increase
	 * probability of honouring priority allocation requests.
	 *
	 * Two bitmaps are used for mcam entry management,
	 * mcam->bmap for forward search i.e '0 to mcam->bmap_entries'.
	 * mcam->bmap_reverse for reverse search i.e 'mcam->bmap_entries to 0'.
	 *
	 * Reverse bitmap is used to allocate entries
	 * - when a higher priority entry is requested
	 * - when available free entries are less.
	 * Lower priority ones out of avaialble free entries are always
	 * chosen when 'high vs low' question arises.
	 */

	/* Get the search range for priority allocation request */
	if (req->priority) {
		npc_get_mcam_search_range_priority(mcam, req,
						   &start, &end, &reverse);
		goto alloc;
	}

	/* For a VF base MCAM match rule is set by its PF. And all the
	 * further MCAM rules installed by VF on its own are
	 * concatenated with the base rule set by its PF. Hence PF entries
	 * should be at lower priority compared to VF entries. Otherwise
	 * base rule is hit always and rules installed by VF will be of
	 * no use. Hence if the request is from PF and NOT a priority
	 * allocation request then allocate low priority entries.
	 */
	if (!(pcifunc & RVU_PFVF_FUNC_MASK))
		goto lprio_alloc;

	/* Find out the search range for non-priority allocation request
	 *
	 * Get MCAM free entry count in middle zone.
	 */
	lp_fcnt = npc_mcam_get_free_count(mcam->bmap,
					  mcam->lprio_start,
					  mcam->bmap_entries);
	hp_fcnt = npc_mcam_get_free_count(mcam->bmap, 0, mcam->hprio_end);
	fcnt = mcam->bmap_fcnt - lp_fcnt - hp_fcnt;

	/* Check if request can be accomodated in the middle zone */
	if (fcnt > req->count) {
		start = mcam->hprio_end;
		end = mcam->lprio_start;
	} else if ((fcnt + (hp_fcnt / 2) + (lp_fcnt / 2)) > req->count) {
		/* Expand search zone from half of hprio zone to
		 * half of lprio zone.
		 */
		start = mcam->hprio_end / 2;
		end = mcam->bmap_entries - (mcam->lprio_count / 2);
		reverse = true;
	} else {
		/* Not enough free entries, search all entries in reverse,
		 * so that low priority ones will get used up.
		 */
lprio_alloc:
		reverse = true;
		start = 0;
		end = mcam->bmap_entries;
	}

alloc:
	if (reverse) {
		bmap = mcam->bmap_reverse;
		start = mcam->bmap_entries - start;
		end = mcam->bmap_entries - end;
		index = start;
		start = end;
		end = index;
	} else {
		bmap = mcam->bmap;
	}

	if (req->contig) {
		/* Allocate requested number of contiguous entries, if
		 * unsuccessful find max contiguous entries available.
		 */
		index = npc_mcam_find_zero_area(bmap, end, start,
						req->count, &max_contig);
		rsp->count = max_contig;
		if (reverse)
			rsp->entry = mcam->bmap_entries - index - max_contig;
		else
			rsp->entry = index;
	} else {
		/* Allocate requested number of non-contiguous entries,
		 * if unsuccessful allocate as many as possible.
		 */
		rsp->count = 0;
		next_start = start;
		for (entry = 0; entry < req->count; entry++) {
			index = find_next_zero_bit(bmap, end, next_start);
			if (index >= end)
				break;

			next_start = start + (index - start) + 1;

			/* Save the entry's index */
			if (reverse)
				index = mcam->bmap_entries - index - 1;
			entry_list[entry] = index;
			rsp->count++;
		}
	}

	/* If allocating requested no of entries is unsucessful,
	 * expand the search range to full bitmap length and retry.
	 */
	if (!req->priority && (rsp->count < req->count) &&
	    ((end - start) != mcam->bmap_entries)) {
		reverse = true;
		start = 0;
		end = mcam->bmap_entries;
		goto alloc;
	}

	/* For priority entry allocation requests, if allocation is
	 * failed then expand search to max possible range and retry.
	 */
	if (req->priority && rsp->count < req->count) {
		if (req->priority == NPC_MCAM_LOWER_PRIO &&
		    (start != (req->ref_entry + 1))) {
			start = req->ref_entry + 1;
			end = mcam->bmap_entries;
			reverse = false;
			goto alloc;
		} else if ((req->priority == NPC_MCAM_HIGHER_PRIO) &&
			   ((end - start) != req->ref_entry)) {
			start = 0;
			end = req->ref_entry;
			reverse = true;
			goto alloc;
		}
	}

	/* Copy MCAM entry indices into mbox response entry_list.
	 * Requester always expects indices in ascending order, so
	 * so reverse the list if reverse bitmap is used for allocation.
	 */
	if (!req->contig && rsp->count) {
		index = 0;
		for (entry = rsp->count - 1; entry >= 0; entry--) {
			if (reverse)
				rsp->entry_list[index++] = entry_list[entry];
			else
				rsp->entry_list[entry] = entry_list[entry];
		}
	}

	/* Mark the allocated entries as used and set nixlf mapping */
	for (entry = 0; entry < rsp->count; entry++) {
		index = req->contig ?
			(rsp->entry + entry) : rsp->entry_list[entry];
		npc_mcam_set_bit(mcam, index);
		mcam->entry2pfvf_map[index] = pcifunc;
		mcam->entry2cntr_map[index] = NPC_MCAM_INVALID_MAP;
	}

	/* Update available free count in mbox response */
	rsp->free_count = mcam->bmap_fcnt;

	mutex_unlock(&mcam->lock);
	return 0;
}

int rvu_mbox_handler_npc_mcam_alloc_entry(struct rvu *rvu,
					  struct npc_mcam_alloc_entry_req *req,
					  struct npc_mcam_alloc_entry_rsp *rsp)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u16 pcifunc = req->hdr.pcifunc;
	int blkaddr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return NPC_MCAM_INVALID_REQ;

	rsp->entry = NPC_MCAM_ENTRY_INVALID;
	rsp->free_count = 0;

	/* Check if ref_entry is within range */
	if (req->priority && req->ref_entry >= mcam->bmap_entries) {
		dev_err(rvu->dev, "%s: reference entry %d is out of range\n",
			__func__, req->ref_entry);
		return NPC_MCAM_INVALID_REQ;
	}

	/* ref_entry can't be '0' if requested priority is high.
	 * Can't be last entry if requested priority is low.
	 */
	if ((!req->ref_entry && req->priority == NPC_MCAM_HIGHER_PRIO) ||
	    ((req->ref_entry == (mcam->bmap_entries - 1)) &&
	     req->priority == NPC_MCAM_LOWER_PRIO))
		return NPC_MCAM_INVALID_REQ;

	/* Since list of allocated indices needs to be sent to requester,
	 * max number of non-contiguous entries per mbox msg is limited.
	 */
	if (!req->contig && req->count > NPC_MAX_NONCONTIG_ENTRIES) {
		dev_err(rvu->dev,
			"%s: %d Non-contiguous MCAM entries requested is more than max (%d) allowed\n",
			__func__, req->count, NPC_MAX_NONCONTIG_ENTRIES);
		return NPC_MCAM_INVALID_REQ;
	}

	/* Alloc request from PFFUNC with no NIXLF attached should be denied */
	if (!is_pffunc_af(pcifunc) && !is_nixlf_attached(rvu, pcifunc))
		return NPC_MCAM_ALLOC_DENIED;

	return npc_mcam_alloc_entries(mcam, pcifunc, req, rsp);
}

int rvu_mbox_handler_npc_mcam_free_entry(struct rvu *rvu,
					 struct npc_mcam_free_entry_req *req,
					 struct msg_rsp *rsp)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u16 pcifunc = req->hdr.pcifunc;
	int blkaddr, rc = 0;
	u16 cntr;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return NPC_MCAM_INVALID_REQ;

	/* Free request from PFFUNC with no NIXLF attached, ignore */
	if (!is_pffunc_af(pcifunc) && !is_nixlf_attached(rvu, pcifunc))
		return NPC_MCAM_INVALID_REQ;

	mutex_lock(&mcam->lock);

	if (req->all)
		goto free_all;

	rc = npc_mcam_verify_entry(mcam, pcifunc, req->entry);
	if (rc)
		goto exit;

	mcam->entry2pfvf_map[req->entry] = NPC_MCAM_INVALID_MAP;
	mcam->entry2target_pffunc[req->entry] = 0x0;
	npc_mcam_clear_bit(mcam, req->entry);
	npc_enable_mcam_entry(rvu, mcam, blkaddr, req->entry, false);

	/* Update entry2counter mapping */
	cntr = mcam->entry2cntr_map[req->entry];
	if (cntr != NPC_MCAM_INVALID_MAP)
		npc_unmap_mcam_entry_and_cntr(rvu, mcam, blkaddr,
					      req->entry, cntr);

	goto exit;

free_all:
	/* Free up all entries allocated to requesting PFFUNC */
	npc_mcam_free_all_entries(rvu, mcam, blkaddr, pcifunc);
exit:
	mutex_unlock(&mcam->lock);
	return rc;
}

int rvu_mbox_handler_npc_mcam_read_entry(struct rvu *rvu,
					 struct npc_mcam_read_entry_req *req,
					 struct npc_mcam_read_entry_rsp *rsp)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u16 pcifunc = req->hdr.pcifunc;
	int blkaddr, rc;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return NPC_MCAM_INVALID_REQ;

	mutex_lock(&mcam->lock);
	rc = npc_mcam_verify_entry(mcam, pcifunc, req->entry);
	if (!rc) {
		npc_read_mcam_entry(rvu, mcam, blkaddr, req->entry,
				    &rsp->entry_data,
				    &rsp->intf, &rsp->enable);
	}

	mutex_unlock(&mcam->lock);
	return rc;
}

int rvu_mbox_handler_npc_mcam_write_entry(struct rvu *rvu,
					  struct npc_mcam_write_entry_req *req,
					  struct msg_rsp *rsp)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, req->hdr.pcifunc);
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u16 pcifunc = req->hdr.pcifunc;
	int blkaddr, rc;
	u8 nix_intf;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return NPC_MCAM_INVALID_REQ;

	mutex_lock(&mcam->lock);
	rc = npc_mcam_verify_entry(mcam, pcifunc, req->entry);
	if (rc)
		goto exit;

	if (req->set_cntr &&
	    npc_mcam_verify_counter(mcam, pcifunc, req->cntr)) {
		rc = NPC_MCAM_INVALID_REQ;
		goto exit;
	}

	if (!is_npc_interface_valid(rvu, req->intf)) {
		rc = NPC_MCAM_INVALID_REQ;
		goto exit;
	}

	if (is_npc_intf_tx(req->intf))
		nix_intf = pfvf->nix_tx_intf;
	else
		nix_intf = pfvf->nix_rx_intf;

	if (!is_pffunc_af(pcifunc) &&
	    npc_mcam_verify_pf_func(rvu, &req->entry_data, req->intf, pcifunc)) {
		rc = NPC_MCAM_INVALID_REQ;
		goto exit;
	}

	/* For AF installed rules, the nix_intf should be set to target NIX */
	if (is_pffunc_af(req->hdr.pcifunc))
		nix_intf = req->intf;

	npc_config_mcam_entry(rvu, mcam, blkaddr, req->entry, nix_intf,
			      &req->entry_data, req->enable_entry);

	if (req->set_cntr)
		npc_map_mcam_entry_and_cntr(rvu, mcam, blkaddr,
					    req->entry, req->cntr);

	rc = 0;
exit:
	mutex_unlock(&mcam->lock);
	return rc;
}

int rvu_mbox_handler_npc_mcam_ena_entry(struct rvu *rvu,
					struct npc_mcam_ena_dis_entry_req *req,
					struct msg_rsp *rsp)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u16 pcifunc = req->hdr.pcifunc;
	int blkaddr, rc;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return NPC_MCAM_INVALID_REQ;

	mutex_lock(&mcam->lock);
	rc = npc_mcam_verify_entry(mcam, pcifunc, req->entry);
	mutex_unlock(&mcam->lock);
	if (rc)
		return rc;

	npc_enable_mcam_entry(rvu, mcam, blkaddr, req->entry, true);

	return 0;
}

int rvu_mbox_handler_npc_mcam_dis_entry(struct rvu *rvu,
					struct npc_mcam_ena_dis_entry_req *req,
					struct msg_rsp *rsp)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u16 pcifunc = req->hdr.pcifunc;
	int blkaddr, rc;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return NPC_MCAM_INVALID_REQ;

	mutex_lock(&mcam->lock);
	rc = npc_mcam_verify_entry(mcam, pcifunc, req->entry);
	mutex_unlock(&mcam->lock);
	if (rc)
		return rc;

	npc_enable_mcam_entry(rvu, mcam, blkaddr, req->entry, false);

	return 0;
}

int rvu_mbox_handler_npc_mcam_shift_entry(struct rvu *rvu,
					  struct npc_mcam_shift_entry_req *req,
					  struct npc_mcam_shift_entry_rsp *rsp)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u16 pcifunc = req->hdr.pcifunc;
	u16 old_entry, new_entry;
	u16 index, cntr;
	int blkaddr, rc;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return NPC_MCAM_INVALID_REQ;

	if (req->shift_count > NPC_MCAM_MAX_SHIFTS)
		return NPC_MCAM_INVALID_REQ;

	mutex_lock(&mcam->lock);
	for (index = 0; index < req->shift_count; index++) {
		old_entry = req->curr_entry[index];
		new_entry = req->new_entry[index];

		/* Check if both old and new entries are valid and
		 * does belong to this PFFUNC or not.
		 */
		rc = npc_mcam_verify_entry(mcam, pcifunc, old_entry);
		if (rc)
			break;

		rc = npc_mcam_verify_entry(mcam, pcifunc, new_entry);
		if (rc)
			break;

		/* new_entry should not have a counter mapped */
		if (mcam->entry2cntr_map[new_entry] != NPC_MCAM_INVALID_MAP) {
			rc = NPC_MCAM_PERM_DENIED;
			break;
		}

		/* Disable the new_entry */
		npc_enable_mcam_entry(rvu, mcam, blkaddr, new_entry, false);

		/* Copy rule from old entry to new entry */
		npc_copy_mcam_entry(rvu, mcam, blkaddr, old_entry, new_entry);

		/* Copy counter mapping, if any */
		cntr = mcam->entry2cntr_map[old_entry];
		if (cntr != NPC_MCAM_INVALID_MAP) {
			npc_unmap_mcam_entry_and_cntr(rvu, mcam, blkaddr,
						      old_entry, cntr);
			npc_map_mcam_entry_and_cntr(rvu, mcam, blkaddr,
						    new_entry, cntr);
		}

		/* Enable new_entry and disable old_entry */
		npc_enable_mcam_entry(rvu, mcam, blkaddr, new_entry, true);
		npc_enable_mcam_entry(rvu, mcam, blkaddr, old_entry, false);
	}

	/* If shift has failed then report the failed index */
	if (index != req->shift_count) {
		rc = NPC_MCAM_PERM_DENIED;
		rsp->failed_entry_idx = index;
	}

	mutex_unlock(&mcam->lock);
	return rc;
}

int rvu_mbox_handler_npc_mcam_alloc_counter(struct rvu *rvu,
			struct npc_mcam_alloc_counter_req *req,
			struct npc_mcam_alloc_counter_rsp *rsp)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u16 pcifunc = req->hdr.pcifunc;
	u16 max_contig, cntr;
	int blkaddr, index;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return NPC_MCAM_INVALID_REQ;

	/* If the request is from a PFFUNC with no NIXLF attached, ignore */
	if (!is_pffunc_af(pcifunc) && !is_nixlf_attached(rvu, pcifunc))
		return NPC_MCAM_INVALID_REQ;

	/* Since list of allocated counter IDs needs to be sent to requester,
	 * max number of non-contiguous counters per mbox msg is limited.
	 */
	if (!req->contig && req->count > NPC_MAX_NONCONTIG_COUNTERS)
		return NPC_MCAM_INVALID_REQ;

	mutex_lock(&mcam->lock);

	/* Check if unused counters are available or not */
	if (!rvu_rsrc_free_count(&mcam->counters)) {
		mutex_unlock(&mcam->lock);
		return NPC_MCAM_ALLOC_FAILED;
	}

	rsp->count = 0;

	if (req->contig) {
		/* Allocate requested number of contiguous counters, if
		 * unsuccessful find max contiguous entries available.
		 */
		index = npc_mcam_find_zero_area(mcam->counters.bmap,
						mcam->counters.max, 0,
						req->count, &max_contig);
		rsp->count = max_contig;
		rsp->cntr = index;
		for (cntr = index; cntr < (index + max_contig); cntr++) {
			__set_bit(cntr, mcam->counters.bmap);
			mcam->cntr2pfvf_map[cntr] = pcifunc;
		}
	} else {
		/* Allocate requested number of non-contiguous counters,
		 * if unsuccessful allocate as many as possible.
		 */
		for (cntr = 0; cntr < req->count; cntr++) {
			index = rvu_alloc_rsrc(&mcam->counters);
			if (index < 0)
				break;
			rsp->cntr_list[cntr] = index;
			rsp->count++;
			mcam->cntr2pfvf_map[index] = pcifunc;
		}
	}

	mutex_unlock(&mcam->lock);
	return 0;
}

int rvu_mbox_handler_npc_mcam_free_counter(struct rvu *rvu,
		struct npc_mcam_oper_counter_req *req, struct msg_rsp *rsp)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u16 index, entry = 0;
	int blkaddr, err;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return NPC_MCAM_INVALID_REQ;

	mutex_lock(&mcam->lock);
	err = npc_mcam_verify_counter(mcam, req->hdr.pcifunc, req->cntr);
	if (err) {
		mutex_unlock(&mcam->lock);
		return err;
	}

	/* Mark counter as free/unused */
	mcam->cntr2pfvf_map[req->cntr] = NPC_MCAM_INVALID_MAP;
	rvu_free_rsrc(&mcam->counters, req->cntr);

	/* Disable all MCAM entry's stats which are using this counter */
	while (entry < mcam->bmap_entries) {
		if (!mcam->cntr_refcnt[req->cntr])
			break;

		index = find_next_bit(mcam->bmap, mcam->bmap_entries, entry);
		if (index >= mcam->bmap_entries)
			break;
		entry = index + 1;
		if (mcam->entry2cntr_map[index] != req->cntr)
			continue;

		npc_unmap_mcam_entry_and_cntr(rvu, mcam, blkaddr,
					      index, req->cntr);
	}

	mutex_unlock(&mcam->lock);
	return 0;
}

int rvu_mbox_handler_npc_mcam_unmap_counter(struct rvu *rvu,
		struct npc_mcam_unmap_counter_req *req, struct msg_rsp *rsp)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u16 index, entry = 0;
	int blkaddr, rc;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return NPC_MCAM_INVALID_REQ;

	mutex_lock(&mcam->lock);
	rc = npc_mcam_verify_counter(mcam, req->hdr.pcifunc, req->cntr);
	if (rc)
		goto exit;

	/* Unmap the MCAM entry and counter */
	if (!req->all) {
		rc = npc_mcam_verify_entry(mcam, req->hdr.pcifunc, req->entry);
		if (rc)
			goto exit;
		npc_unmap_mcam_entry_and_cntr(rvu, mcam, blkaddr,
					      req->entry, req->cntr);
		goto exit;
	}

	/* Disable all MCAM entry's stats which are using this counter */
	while (entry < mcam->bmap_entries) {
		if (!mcam->cntr_refcnt[req->cntr])
			break;

		index = find_next_bit(mcam->bmap, mcam->bmap_entries, entry);
		if (index >= mcam->bmap_entries)
			break;
		if (mcam->entry2cntr_map[index] != req->cntr)
			continue;

		entry = index + 1;
		npc_unmap_mcam_entry_and_cntr(rvu, mcam, blkaddr,
					      index, req->cntr);
	}
exit:
	mutex_unlock(&mcam->lock);
	return rc;
}

int rvu_mbox_handler_npc_mcam_clear_counter(struct rvu *rvu,
		struct npc_mcam_oper_counter_req *req, struct msg_rsp *rsp)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	int blkaddr, err;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return NPC_MCAM_INVALID_REQ;

	mutex_lock(&mcam->lock);
	err = npc_mcam_verify_counter(mcam, req->hdr.pcifunc, req->cntr);
	mutex_unlock(&mcam->lock);
	if (err)
		return err;

	rvu_write64(rvu, blkaddr, NPC_AF_MATCH_STATX(req->cntr), 0x00);

	return 0;
}

int rvu_mbox_handler_npc_mcam_counter_stats(struct rvu *rvu,
			struct npc_mcam_oper_counter_req *req,
			struct npc_mcam_oper_counter_rsp *rsp)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	int blkaddr, err;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return NPC_MCAM_INVALID_REQ;

	mutex_lock(&mcam->lock);
	err = npc_mcam_verify_counter(mcam, req->hdr.pcifunc, req->cntr);
	mutex_unlock(&mcam->lock);
	if (err)
		return err;

	rsp->stat = rvu_read64(rvu, blkaddr, NPC_AF_MATCH_STATX(req->cntr));
	rsp->stat &= BIT_ULL(48) - 1;

	return 0;
}

int rvu_mbox_handler_npc_mcam_alloc_and_write_entry(struct rvu *rvu,
			  struct npc_mcam_alloc_and_write_entry_req *req,
			  struct npc_mcam_alloc_and_write_entry_rsp *rsp)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, req->hdr.pcifunc);
	struct npc_mcam_alloc_counter_req cntr_req;
	struct npc_mcam_alloc_counter_rsp cntr_rsp;
	struct npc_mcam_alloc_entry_req entry_req;
	struct npc_mcam_alloc_entry_rsp entry_rsp;
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u16 entry = NPC_MCAM_ENTRY_INVALID;
	u16 cntr = NPC_MCAM_ENTRY_INVALID;
	int blkaddr, rc;
	u8 nix_intf;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return NPC_MCAM_INVALID_REQ;

	if (!is_npc_interface_valid(rvu, req->intf))
		return NPC_MCAM_INVALID_REQ;

	if (npc_mcam_verify_pf_func(rvu, &req->entry_data, req->intf,
				    req->hdr.pcifunc))
		return NPC_MCAM_INVALID_REQ;

	/* Try to allocate a MCAM entry */
	entry_req.hdr.pcifunc = req->hdr.pcifunc;
	entry_req.contig = true;
	entry_req.priority = req->priority;
	entry_req.ref_entry = req->ref_entry;
	entry_req.count = 1;

	rc = rvu_mbox_handler_npc_mcam_alloc_entry(rvu,
						   &entry_req, &entry_rsp);
	if (rc)
		return rc;

	if (!entry_rsp.count)
		return NPC_MCAM_ALLOC_FAILED;

	entry = entry_rsp.entry;

	if (!req->alloc_cntr)
		goto write_entry;

	/* Now allocate counter */
	cntr_req.hdr.pcifunc = req->hdr.pcifunc;
	cntr_req.contig = true;
	cntr_req.count = 1;

	rc = rvu_mbox_handler_npc_mcam_alloc_counter(rvu, &cntr_req, &cntr_rsp);
	if (rc) {
		/* Free allocated MCAM entry */
		mutex_lock(&mcam->lock);
		mcam->entry2pfvf_map[entry] = NPC_MCAM_INVALID_MAP;
		npc_mcam_clear_bit(mcam, entry);
		mutex_unlock(&mcam->lock);
		return rc;
	}

	cntr = cntr_rsp.cntr;

write_entry:
	mutex_lock(&mcam->lock);

	if (is_npc_intf_tx(req->intf))
		nix_intf = pfvf->nix_tx_intf;
	else
		nix_intf = pfvf->nix_rx_intf;

	npc_config_mcam_entry(rvu, mcam, blkaddr, entry, nix_intf,
			      &req->entry_data, req->enable_entry);

	if (req->alloc_cntr)
		npc_map_mcam_entry_and_cntr(rvu, mcam, blkaddr, entry, cntr);
	mutex_unlock(&mcam->lock);

	rsp->entry = entry;
	rsp->cntr = cntr;

	return 0;
}

#define GET_KEX_CFG(intf) \
	rvu_read64(rvu, BLKADDR_NPC, NPC_AF_INTFX_KEX_CFG(intf))

#define GET_KEX_FLAGS(ld) \
	rvu_read64(rvu, BLKADDR_NPC, NPC_AF_KEX_LDATAX_FLAGS_CFG(ld))

#define GET_KEX_LD(intf, lid, lt, ld)	\
	rvu_read64(rvu, BLKADDR_NPC,	\
		NPC_AF_INTFX_LIDX_LTX_LDX_CFG(intf, lid, lt, ld))

#define GET_KEX_LDFLAGS(intf, ld, fl)	\
	rvu_read64(rvu, BLKADDR_NPC,	\
		NPC_AF_INTFX_LDATAX_FLAGSX_CFG(intf, ld, fl))

int rvu_mbox_handler_npc_get_kex_cfg(struct rvu *rvu, struct msg_req *req,
				     struct npc_get_kex_cfg_rsp *rsp)
{
	int lid, lt, ld, fl;

	rsp->rx_keyx_cfg = GET_KEX_CFG(NIX_INTF_RX);
	rsp->tx_keyx_cfg = GET_KEX_CFG(NIX_INTF_TX);
	for (lid = 0; lid < NPC_MAX_LID; lid++) {
		for (lt = 0; lt < NPC_MAX_LT; lt++) {
			for (ld = 0; ld < NPC_MAX_LD; ld++) {
				rsp->intf_lid_lt_ld[NIX_INTF_RX][lid][lt][ld] =
					GET_KEX_LD(NIX_INTF_RX, lid, lt, ld);
				rsp->intf_lid_lt_ld[NIX_INTF_TX][lid][lt][ld] =
					GET_KEX_LD(NIX_INTF_TX, lid, lt, ld);
			}
		}
	}
	for (ld = 0; ld < NPC_MAX_LD; ld++)
		rsp->kex_ld_flags[ld] = GET_KEX_FLAGS(ld);

	for (ld = 0; ld < NPC_MAX_LD; ld++) {
		for (fl = 0; fl < NPC_MAX_LFL; fl++) {
			rsp->intf_ld_flags[NIX_INTF_RX][ld][fl] =
					GET_KEX_LDFLAGS(NIX_INTF_RX, ld, fl);
			rsp->intf_ld_flags[NIX_INTF_TX][ld][fl] =
					GET_KEX_LDFLAGS(NIX_INTF_TX, ld, fl);
		}
	}
	memcpy(rsp->mkex_pfl_name, rvu->mkex_pfl_name, MKEX_NAME_LEN);
	return 0;
}

int rvu_mbox_handler_npc_read_base_steer_rule(struct rvu *rvu,
					      struct msg_req *req,
					      struct npc_mcam_read_base_rule_rsp *rsp)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	int index, blkaddr, nixlf, rc = 0;
	u16 pcifunc = req->hdr.pcifunc;
	struct rvu_pfvf *pfvf;
	u8 intf, enable;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return NPC_MCAM_INVALID_REQ;

	/* Return the channel number in case of PF */
	if (!(pcifunc & RVU_PFVF_FUNC_MASK)) {
		pfvf = rvu_get_pfvf(rvu, pcifunc);
		rsp->entry.kw[0] = pfvf->rx_chan_base;
		rsp->entry.kw_mask[0] = 0xFFFULL;
		goto out;
	}

	/* Find the pkt steering rule installed by PF to this VF */
	mutex_lock(&mcam->lock);
	for (index = 0; index < mcam->bmap_entries; index++) {
		if (mcam->entry2target_pffunc[index] == pcifunc)
			goto read_entry;
	}

	rc = nix_get_nixlf(rvu, pcifunc, &nixlf, NULL);
	if (rc < 0) {
		mutex_unlock(&mcam->lock);
		goto out;
	}
	/* Read the default ucast entry if there is no pkt steering rule */
	index = npc_get_nixlf_mcam_index(mcam, pcifunc, nixlf,
					 NIXLF_UCAST_ENTRY);
read_entry:
	/* Read the mcam entry */
	npc_read_mcam_entry(rvu, mcam, blkaddr, index, &rsp->entry, &intf,
			    &enable);
	mutex_unlock(&mcam->lock);
out:
	return rc;
}

int rvu_mbox_handler_npc_mcam_entry_stats(struct rvu *rvu,
					  struct npc_mcam_get_stats_req *req,
					  struct npc_mcam_get_stats_rsp *rsp)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u16 index, cntr;
	int blkaddr;
	u64 regval;
	u32 bank;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return NPC_MCAM_INVALID_REQ;

	mutex_lock(&mcam->lock);

	index = req->entry & (mcam->banksize - 1);
	bank = npc_get_bank(mcam, req->entry);

	/* read MCAM entry STAT_ACT register */
	regval = rvu_read64(rvu, blkaddr, NPC_AF_MCAMEX_BANKX_STAT_ACT(index, bank));

	if (!(regval & rvu->hw->npc_stat_ena)) {
		rsp->stat_ena = 0;
		mutex_unlock(&mcam->lock);
		return 0;
	}

	cntr = regval & 0x1FF;

	rsp->stat_ena = 1;
	rsp->stat = rvu_read64(rvu, blkaddr, NPC_AF_MATCH_STATX(cntr));
	rsp->stat &= BIT_ULL(48) - 1;

	mutex_unlock(&mcam->lock);

	return 0;
}
