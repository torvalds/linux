// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/pci.h>

#include "rvu_struct.h"
#include "rvu_reg.h"
#include "rvu.h"
#include "npc.h"
#include "npc_profile.h"

#define RSVD_MCAM_ENTRIES_PER_PF	2 /* Bcast & Promisc */
#define RSVD_MCAM_ENTRIES_PER_NIXLF	1 /* Ucast for LFs */

#define NIXLF_UCAST_ENTRY	0
#define NIXLF_BCAST_ENTRY	1
#define NIXLF_PROMISC_ENTRY	2

#define NPC_PARSE_RESULT_DMAC_OFFSET	8

struct mcam_entry {
#define NPC_MAX_KWS_IN_KEY	7 /* Number of keywords in max keywidth */
	u64	kw[NPC_MAX_KWS_IN_KEY];
	u64	kw_mask[NPC_MAX_KWS_IN_KEY];
	u64	action;
	u64	vtag_action;
};

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

static int npc_get_nixlf_mcam_index(struct npc_mcam *mcam,
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
		else if (type == NIXLF_PROMISC_ENTRY)
			return index + 1;
	}

	return (mcam->nixlf_offset + (nixlf * RSVD_MCAM_ENTRIES_PER_NIXLF));
}

static int npc_get_bank(struct npc_mcam *mcam, int index)
{
	int bank = index / mcam->banksize;

	/* 0,1 & 2,3 banks are combined for this keysize */
	if (mcam->keysize == NPC_MCAM_KEY_X2)
		return bank ? 2 : 0;

	return bank;
}

static bool is_mcam_entry_enabled(struct rvu *rvu, struct npc_mcam *mcam,
				  int blkaddr, int index)
{
	int bank = npc_get_bank(mcam, index);
	u64 cfg;

	index &= (mcam->banksize - 1);
	cfg = rvu_read64(rvu, blkaddr, NPC_AF_MCAMEX_BANKX_CFG(index, bank));
	return (cfg & 1);
}

static void npc_enable_mcam_entry(struct rvu *rvu, struct npc_mcam *mcam,
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

static void npc_config_mcam_entry(struct rvu *rvu, struct npc_mcam *mcam,
				  int blkaddr, int index, u8 intf,
				  struct mcam_entry *entry, bool enable)
{
	int bank = npc_get_bank(mcam, index);
	int kw = 0, actbank, actindex;
	u64 cam0, cam1;

	actbank = bank; /* Save bank id, to set action later on */
	actindex = index;
	index &= (mcam->banksize - 1);

	/* CAM1 takes the comparison value and
	 * CAM0 specifies match for a bit in key being '0' or '1' or 'dontcare'.
	 * CAM1<n> = 0 & CAM0<n> = 1 => match if key<n> = 0
	 * CAM1<n> = 1 & CAM0<n> = 0 => match if key<n> = 1
	 * CAM1<n> = 0 & CAM0<n> = 0 => always match i.e dontcare.
	 */
	for (; bank < (actbank + mcam->banks_per_entry); bank++, kw = kw + 2) {
		/* Interface should be set in all banks */
		rvu_write64(rvu, blkaddr,
			    NPC_AF_MCAMEX_BANKX_CAMX_INTF(index, bank, 1),
			    intf);
		rvu_write64(rvu, blkaddr,
			    NPC_AF_MCAMEX_BANKX_CAMX_INTF(index, bank, 0),
			    ~intf & 0x3);

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

	/* Set 'action' */
	rvu_write64(rvu, blkaddr,
		    NPC_AF_MCAMEX_BANKX_ACTION(index, actbank), entry->action);

	/* Set TAG 'action' */
	rvu_write64(rvu, blkaddr, NPC_AF_MCAMEX_BANKX_TAG_ACT(index, actbank),
		    entry->vtag_action);

	/* Enable the entry */
	if (enable)
		npc_enable_mcam_entry(rvu, mcam, blkaddr, actindex, true);
	else
		npc_enable_mcam_entry(rvu, mcam, blkaddr, actindex, false);
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
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct mcam_entry entry = { {0} };
	struct nix_rx_action action;
	int blkaddr, index, kwi;
	u64 mac = 0;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return;

	for (index = ETH_ALEN - 1; index >= 0; index--)
		mac |= ((u64)*mac_addr++) << (8 * index);

	index = npc_get_nixlf_mcam_index(mcam, pcifunc,
					 nixlf, NIXLF_UCAST_ENTRY);

	/* Match ingress channel and DMAC */
	entry.kw[0] = chan;
	entry.kw_mask[0] = 0xFFFULL;

	kwi = NPC_PARSE_RESULT_DMAC_OFFSET / sizeof(u64);
	entry.kw[kwi] = mac;
	entry.kw_mask[kwi] = BIT_ULL(48) - 1;

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

	entry.action = *(u64 *)&action;
	npc_config_mcam_entry(rvu, mcam, blkaddr, index,
			      NIX_INTF_RX, &entry, true);
}

void rvu_npc_install_promisc_entry(struct rvu *rvu, u16 pcifunc,
				   int nixlf, u64 chan, bool allmulti)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct mcam_entry entry = { {0} };
	struct nix_rx_action action;
	int blkaddr, index, kwi;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return;

	/* Only PF or AF VF can add a promiscuous entry */
	if (pcifunc & RVU_PFVF_FUNC_MASK)
		return;

	index = npc_get_nixlf_mcam_index(mcam, pcifunc,
					 nixlf, NIXLF_PROMISC_ENTRY);

	entry.kw[0] = chan;
	entry.kw_mask[0] = 0xFFFULL;

	if (allmulti) {
		kwi = NPC_PARSE_RESULT_DMAC_OFFSET / sizeof(u64);
		entry.kw[kwi] = BIT_ULL(40); /* LSB bit of 1st byte in DMAC */
		entry.kw_mask[kwi] = BIT_ULL(40);
	}

	*(u64 *)&action = 0x00;
	action.op = NIX_RX_ACTIONOP_UCAST;
	action.pf_func = pcifunc;

	entry.action = *(u64 *)&action;
	npc_config_mcam_entry(rvu, mcam, blkaddr, index,
			      NIX_INTF_RX, &entry, true);
}

void rvu_npc_disable_promisc_entry(struct rvu *rvu, u16 pcifunc, int nixlf)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	int blkaddr, index;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return;

	/* Only PF's have a promiscuous entry */
	if (pcifunc & RVU_PFVF_FUNC_MASK)
		return;

	index = npc_get_nixlf_mcam_index(mcam, pcifunc,
					 nixlf, NIXLF_PROMISC_ENTRY);
	npc_enable_mcam_entry(rvu, mcam, blkaddr, index, false);
}

void rvu_npc_install_bcast_match_entry(struct rvu *rvu, u16 pcifunc,
				       int nixlf, u64 chan)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct mcam_entry entry = { {0} };
	struct nix_rx_action action;
#ifdef MCAST_MCE
	struct rvu_pfvf *pfvf;
#endif
	int blkaddr, index;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return;

	/* Only PF can add a bcast match entry */
	if (pcifunc & RVU_PFVF_FUNC_MASK)
		return;
#ifdef MCAST_MCE
	pfvf = rvu_get_pfvf(rvu, pcifunc & ~RVU_PFVF_FUNC_MASK);
#endif

	index = npc_get_nixlf_mcam_index(mcam, pcifunc,
					 nixlf, NIXLF_BCAST_ENTRY);

	/* Check for L2B bit and LMAC channel */
	entry.kw[0] = BIT_ULL(25) | chan;
	entry.kw_mask[0] = BIT_ULL(25) | 0xFFFULL;

	*(u64 *)&action = 0x00;
#ifdef MCAST_MCE
	/* Early silicon doesn't support pkt replication,
	 * so install entry with UCAST action, so that PF
	 * receives all broadcast packets.
	 */
	action.op = NIX_RX_ACTIONOP_MCAST;
	action.pf_func = pcifunc;
	action.index = pfvf->bcast_mce_idx;
#else
	action.op = NIX_RX_ACTIONOP_UCAST;
	action.pf_func = pcifunc;
#endif

	entry.action = *(u64 *)&action;
	npc_config_mcam_entry(rvu, mcam, blkaddr, index,
			      NIX_INTF_RX, &entry, true);
}

void rvu_npc_update_flowkey_alg_idx(struct rvu *rvu, u16 pcifunc, int nixlf,
				    int group, int alg_idx, int mcam_index)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct nix_rx_action action;
	int blkaddr, index, bank;

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
}

void rvu_npc_disable_mcam_entries(struct rvu *rvu, u16 pcifunc, int nixlf)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct nix_rx_action action;
	int blkaddr, index, bank;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return;

	/* Disable ucast MCAM match entry of this PF/VF */
	index = npc_get_nixlf_mcam_index(mcam, pcifunc,
					 nixlf, NIXLF_UCAST_ENTRY);
	npc_enable_mcam_entry(rvu, mcam, blkaddr, index, false);

	/* For PF, disable promisc and bcast MCAM match entries */
	if (!(pcifunc & RVU_PFVF_FUNC_MASK)) {
		index = npc_get_nixlf_mcam_index(mcam, pcifunc,
						 nixlf, NIXLF_BCAST_ENTRY);
		/* For bcast, disable only if it's action is not
		 * packet replication, incase if action is replication
		 * then this PF's nixlf is removed from bcast replication
		 * list.
		 */
		bank = npc_get_bank(mcam, index);
		index &= (mcam->banksize - 1);
		*(u64 *)&action = rvu_read64(rvu, blkaddr,
				     NPC_AF_MCAMEX_BANKX_ACTION(index, bank));
		if (action.op != NIX_RX_ACTIONOP_MCAST)
			npc_enable_mcam_entry(rvu, mcam, blkaddr, index, false);

		rvu_npc_disable_promisc_entry(rvu, pcifunc, nixlf);
	}
}

#define LDATA_EXTRACT_CONFIG(intf, lid, ltype, ld, cfg) \
	rvu_write64(rvu, blkaddr,			\
		NPC_AF_INTFX_LIDX_LTX_LDX_CFG(intf, lid, ltype, ld), cfg)

#define LDATA_FLAGS_CONFIG(intf, ld, flags, cfg)	\
	rvu_write64(rvu, blkaddr,			\
		NPC_AF_INTFX_LDATAX_FLAGSX_CFG(intf, ld, flags), cfg)

static void npc_config_ldata_extract(struct rvu *rvu, int blkaddr)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	int lid, ltype;
	int lid_count;
	u64 cfg;

	cfg = rvu_read64(rvu, blkaddr, NPC_AF_CONST);
	lid_count = (cfg >> 4) & 0xF;

	/* First clear any existing config i.e
	 * disable LDATA and FLAGS extraction.
	 */
	for (lid = 0; lid < lid_count; lid++) {
		for (ltype = 0; ltype < 16; ltype++) {
			LDATA_EXTRACT_CONFIG(NIX_INTF_RX, lid, ltype, 0, 0ULL);
			LDATA_EXTRACT_CONFIG(NIX_INTF_RX, lid, ltype, 1, 0ULL);
			LDATA_EXTRACT_CONFIG(NIX_INTF_TX, lid, ltype, 0, 0ULL);
			LDATA_EXTRACT_CONFIG(NIX_INTF_TX, lid, ltype, 1, 0ULL);

			LDATA_FLAGS_CONFIG(NIX_INTF_RX, 0, ltype, 0ULL);
			LDATA_FLAGS_CONFIG(NIX_INTF_RX, 1, ltype, 0ULL);
			LDATA_FLAGS_CONFIG(NIX_INTF_TX, 0, ltype, 0ULL);
			LDATA_FLAGS_CONFIG(NIX_INTF_TX, 1, ltype, 0ULL);
		}
	}

	/* If we plan to extract Outer IPv4 tuple for TCP/UDP pkts
	 * then 112bit key is not sufficient
	 */
	if (mcam->keysize != NPC_MCAM_KEY_X2)
		return;

	/* Start placing extracted data/flags from 64bit onwards, for now */
	/* Extract DMAC from the packet */
	cfg = (0x05 << 16) | BIT_ULL(7) | NPC_PARSE_RESULT_DMAC_OFFSET;
	LDATA_EXTRACT_CONFIG(NIX_INTF_RX, NPC_LID_LA, NPC_LT_LA_ETHER, 0, cfg);
}

static void npc_config_kpuaction(struct rvu *rvu, int blkaddr,
				 struct npc_kpu_profile_action *kpuaction,
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
			      struct npc_kpu_profile_cam *kpucam,
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
				    struct npc_kpu_profile *profile)
{
	int entry, num_entries, max_entries;

	if (profile->cam_entries != profile->action_entries) {
		dev_err(rvu->dev,
			"KPU%d: CAM and action entries [%d != %d] not equal\n",
			kpu, profile->cam_entries, profile->action_entries);
	}

	max_entries = rvu_read64(rvu, blkaddr, NPC_AF_CONST1) & 0xFFF;

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
	rvu_write64(rvu, blkaddr,
		    NPC_AF_KPUX_ENTRY_DISX(kpu, 0), enable_mask(num_entries));
	if (num_entries > 64) {
		rvu_write64(rvu, blkaddr,
			    NPC_AF_KPUX_ENTRY_DISX(kpu, 1),
			    enable_mask(num_entries - 64));
	}

	/* Enable this KPU */
	rvu_write64(rvu, blkaddr, NPC_AF_KPUX_CFG(kpu), 0x01);
}

static void npc_parser_profile_init(struct rvu *rvu, int blkaddr)
{
	struct rvu_hwinfo *hw = rvu->hw;
	int num_pkinds, num_kpus, idx;
	struct npc_pkind *pkind;

	/* Get HW limits */
	hw->npc_kpus = (rvu_read64(rvu, blkaddr, NPC_AF_CONST) >> 8) & 0x1F;

	/* Disable all KPUs and their entries */
	for (idx = 0; idx < hw->npc_kpus; idx++) {
		rvu_write64(rvu, blkaddr,
			    NPC_AF_KPUX_ENTRY_DISX(idx, 0), ~0ULL);
		rvu_write64(rvu, blkaddr,
			    NPC_AF_KPUX_ENTRY_DISX(idx, 1), ~0ULL);
		rvu_write64(rvu, blkaddr, NPC_AF_KPUX_CFG(idx), 0x00);
	}

	/* First program IKPU profile i.e PKIND configs.
	 * Check HW max count to avoid configuring junk or
	 * writing to unsupported CSR addresses.
	 */
	pkind = &hw->pkind;
	num_pkinds = ARRAY_SIZE(ikpu_action_entries);
	num_pkinds = min_t(int, pkind->rsrc.max, num_pkinds);

	for (idx = 0; idx < num_pkinds; idx++)
		npc_config_kpuaction(rvu, blkaddr,
				     &ikpu_action_entries[idx], 0, idx, true);

	/* Program KPU CAM and Action profiles */
	num_kpus = ARRAY_SIZE(npc_kpu_profiles);
	num_kpus = min_t(int, hw->npc_kpus, num_kpus);

	for (idx = 0; idx < num_kpus; idx++)
		npc_program_kpu_profile(rvu, blkaddr,
					idx, &npc_kpu_profiles[idx]);
}

static int npc_mcam_rsrcs_init(struct rvu *rvu, int blkaddr)
{
	int nixlf_count = rvu_get_nixlf_count(rvu);
	struct npc_mcam *mcam = &rvu->hw->mcam;
	int rsvd;
	u64 cfg;

	/* Get HW limits */
	cfg = rvu_read64(rvu, blkaddr, NPC_AF_CONST);
	mcam->banks = (cfg >> 44) & 0xF;
	mcam->banksize = (cfg >> 28) & 0xFFFF;

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

	mcam->entries = mcam->total_entries - rsvd;
	mcam->nixlf_offset = mcam->entries;
	mcam->pf_offset = mcam->nixlf_offset + nixlf_count;

	spin_lock_init(&mcam->lock);

	return 0;
}

int rvu_npc_init(struct rvu *rvu)
{
	struct npc_pkind *pkind = &rvu->hw->pkind;
	u64 keyz = NPC_MCAM_KEY_X2;
	int blkaddr, err;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0) {
		dev_err(rvu->dev, "%s: NPC block not implemented\n", __func__);
		return -ENODEV;
	}

	/* Allocate resource bimap for pkind*/
	pkind->rsrc.max = (rvu_read64(rvu, blkaddr,
				      NPC_AF_CONST1) >> 12) & 0xFF;
	err = rvu_alloc_bitmap(&pkind->rsrc);
	if (err)
		return err;

	/* Allocate mem for pkind to PF and channel mapping info */
	pkind->pfchan_map = devm_kcalloc(rvu->dev, pkind->rsrc.max,
					 sizeof(u32), GFP_KERNEL);
	if (!pkind->pfchan_map)
		return -ENOMEM;

	/* Configure KPU profile */
	npc_parser_profile_init(rvu, blkaddr);

	/* Config Outer L2, IPv4's NPC layer info */
	rvu_write64(rvu, blkaddr, NPC_AF_PCK_DEF_OL2,
		    (NPC_LID_LA << 8) | (NPC_LT_LA_ETHER << 4) | 0x0F);
	rvu_write64(rvu, blkaddr, NPC_AF_PCK_DEF_OIP4,
		    (NPC_LID_LC << 8) | (NPC_LT_LC_IP << 4) | 0x0F);

	/* Enable below for Rx pkts.
	 * - Outer IPv4 header checksum validation.
	 * - Detect outer L2 broadcast address and set NPC_RESULT_S[L2M].
	 */
	rvu_write64(rvu, blkaddr, NPC_AF_PCK_CFG,
		    rvu_read64(rvu, blkaddr, NPC_AF_PCK_CFG) |
		    BIT_ULL(6) | BIT_ULL(2));

	/* Set RX and TX side MCAM search key size.
	 * Also enable parse key extract nibbles suchthat except
	 * layer E to H, rest of the key is included for MCAM search.
	 */
	rvu_write64(rvu, blkaddr, NPC_AF_INTFX_KEX_CFG(NIX_INTF_RX),
		    ((keyz & 0x3) << 32) | ((1ULL << 20) - 1));
	rvu_write64(rvu, blkaddr, NPC_AF_INTFX_KEX_CFG(NIX_INTF_TX),
		    ((keyz & 0x3) << 32) | ((1ULL << 20) - 1));

	err = npc_mcam_rsrcs_init(rvu, blkaddr);
	if (err)
		return err;

	/* Config packet data and flags extraction into PARSE result */
	npc_config_ldata_extract(rvu, blkaddr);

	/* Set TX miss action to UCAST_DEFAULT i.e
	 * transmit the packet on NIX LF SQ's default channel.
	 */
	rvu_write64(rvu, blkaddr, NPC_AF_INTFX_MISS_ACT(NIX_INTF_TX),
		    NIX_TX_ACTIONOP_UCAST_DEFAULT);

	/* If MCAM lookup doesn't result in a match, drop the received packet */
	rvu_write64(rvu, blkaddr, NPC_AF_INTFX_MISS_ACT(NIX_INTF_RX),
		    NIX_RX_ACTIONOP_DROP);

	return 0;
}

void rvu_npc_freemem(struct rvu *rvu)
{
	struct npc_pkind *pkind = &rvu->hw->pkind;

	kfree(pkind->rsrc.bmap);
}
