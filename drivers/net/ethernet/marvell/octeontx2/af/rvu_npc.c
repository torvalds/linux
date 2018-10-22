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

int rvu_npc_init(struct rvu *rvu)
{
	struct npc_pkind *pkind = &rvu->hw->pkind;
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

	return 0;
}

void rvu_npc_freemem(struct rvu *rvu)
{
	struct npc_pkind *pkind = &rvu->hw->pkind;

	kfree(pkind->rsrc.bmap);
}
