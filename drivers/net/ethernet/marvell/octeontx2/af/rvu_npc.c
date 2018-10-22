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
