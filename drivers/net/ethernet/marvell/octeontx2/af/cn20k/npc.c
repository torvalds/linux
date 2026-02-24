// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU Admin Function driver
 *
 * Copyright (C) 2026 Marvell.
 *
 */
#include <linux/xarray.h>
#include <linux/bitfield.h>

#include "rvu.h"
#include "npc.h"
#include "npc_profile.h"
#include "rvu_npc_hash.h"
#include "rvu_npc.h"
#include "cn20k/npc.h"
#include "cn20k/reg.h"

static struct npc_priv_t npc_priv = {
	.num_banks = MAX_NUM_BANKS,
};

static const char *npc_kw_name[NPC_MCAM_KEY_MAX] = {
	[NPC_MCAM_KEY_DYN] = "DYNAMIC",
	[NPC_MCAM_KEY_X2] = "X2",
	[NPC_MCAM_KEY_X4] = "X4",
};

static const char *npc_dft_rule_name[NPC_DFT_RULE_MAX_ID] = {
	[NPC_DFT_RULE_PROMISC_ID] = "Promisc",
	[NPC_DFT_RULE_MCAST_ID] = "Mcast",
	[NPC_DFT_RULE_BCAST_ID] = "Bcast",
	[NPC_DFT_RULE_UCAST_ID] = "Ucast",
};

#define KEX_EXTR_CFG(bytesm1, hdr_ofs, ena, key_ofs)		\
		     (((bytesm1) << 16) | ((hdr_ofs) << 8) | ((ena) << 7) | \
		     ((key_ofs) & 0x3F))

#define NPC_DFT_RULE_ID_MK(pcifunc, id) \
	((pcifunc) | FIELD_PREP(GENMASK_ULL(31, 16), id))

#define NPC_DFT_RULE_ID_2_PCIFUNC(rid) \
	FIELD_GET(GENMASK_ULL(15, 0), rid)

#define NPC_DFT_RULE_ID_2_ID(rid) \
	FIELD_GET(GENMASK_ULL(31, 16), rid)

#define NPC_DFT_RULE_PRIO 127

static const char cn20k_def_pfl_name[] = "default";

static struct npc_mcam_kex_extr npc_mkex_extr_default = {
	.mkex_sign = MKEX_CN20K_SIGN,
	.name = "default",
	.kpu_version = NPC_KPU_PROFILE_VER,
	.keyx_cfg = {
		/* nibble: LA..LE (ltype only) + Error code + Channel */
		[NIX_INTF_RX] = ((u64)NPC_MCAM_KEY_DYN << 32) |
			NPC_PARSE_NIBBLE_INTF_RX |
				 NPC_CN20K_PARSE_NIBBLE_ERRCODE,

		/* nibble: LA..LE (ltype only) */
		[NIX_INTF_TX] = ((u64)NPC_MCAM_KEY_X2 << 32) |
			NPC_CN20K_PARSE_NIBBLE_INTF_TX,
	},
	.intf_extr_lid = {
	/* Default RX MCAM KEX profile */
	[NIX_INTF_RX] = { NPC_LID_LA, NPC_LID_LA, NPC_LID_LB, NPC_LID_LB,
			  NPC_LID_LC, NPC_LID_LC, NPC_LID_LD },
	[NIX_INTF_TX] = { NPC_LID_LA, NPC_LID_LA, NPC_LID_LB, NPC_LID_LB,
			  NPC_LID_LC, NPC_LID_LD },
	},
	.intf_extr_lt = {
	/* Default RX MCAM KEX profile */
	[NIX_INTF_RX] = {
		[0] = {
			/* Layer A: Ethernet: */
			[NPC_LT_LA_ETHER] =
				/* DMAC: 6 bytes, KW1[63:15] */
				KEX_EXTR_CFG(0x05, 0x0, 0x1,
					     NPC_KEXOF_DMAC + 1),
			[NPC_LT_LA_CPT_HDR] =
				/* DMAC: 6 bytes, KW1[63:15] */
				KEX_EXTR_CFG(0x05, 0x0, 0x1,
					     NPC_KEXOF_DMAC + 1),
		},
		[1] = {
			/* Layer A: Ethernet: */
			[NPC_LT_LA_ETHER] =
				/* Ethertype: 2 bytes, KW0[63:48] */
				KEX_EXTR_CFG(0x01, 0xc, 0x1, 0x6),
			[NPC_LT_LA_CPT_HDR] =
				/* Ethertype: 2 bytes, KW0[63:48] */
				KEX_EXTR_CFG(0x01, 0xc, 0x1, 0x6),
		},
		[2] = {
			/* Layer B: Single VLAN (CTAG) */
			[NPC_LT_LB_CTAG] =
				/* CTAG VLAN: 2 bytes, KW1[15:0] */
				KEX_EXTR_CFG(0x01, 0x2, 0x1, 0x8),
			/* Layer B: Stacked VLAN (STAG|QinQ) */
			[NPC_LT_LB_STAG_QINQ] =
				/* Outer VLAN: 2 bytes, KW1[15:0] */
				KEX_EXTR_CFG(0x01, 0x2, 0x1, 0x8),
			[NPC_LT_LB_FDSA] =
				/* SWITCH PORT: 1 byte, KW1[7:0] */
				KEX_EXTR_CFG(0x0, 0x1, 0x1, 0x8),
		},
		[3] = {
			[NPC_LT_LB_CTAG] =
				/* Ethertype: 2 bytes, KW0[63:48] */
				KEX_EXTR_CFG(0x01, 0x4, 0x1, 0x6),
			[NPC_LT_LB_STAG_QINQ] =
				/* Ethertype: 2 bytes, KW0[63:48] */
				KEX_EXTR_CFG(0x01, 0x8, 0x1, 0x6),
			[NPC_LT_LB_FDSA] =
				/* Ethertype: 2 bytes, KW0[63:48] */
				KEX_EXTR_CFG(0x01, 0x4, 0x1, 0x6),
		},
		[4] = {
			/* Layer C: IPv4 */
			[NPC_LT_LC_IP] =
				/* SIP+DIP: 8 bytes, KW3[7:0], KW2[63:8] */
				KEX_EXTR_CFG(0x07, 0xc, 0x1, 0x11),
			/* Layer C: IPv6 */
			[NPC_LT_LC_IP6] =
				/* Everything up to SADDR: 8 bytes, KW3[7:0],
				 * KW2[63:8]
				 */
				KEX_EXTR_CFG(0x07, 0x0, 0x1, 0x11),
		},
		[5] = {
			[NPC_LT_LC_IP] =
				/* TOS: 1 byte, KW2[7:0] */
				KEX_EXTR_CFG(0x0, 0x1, 0x1, 0x10),
		},
		[6] = {
			/* Layer D:UDP */
			[NPC_LT_LD_UDP] =
				/* SPORT+DPORT: 4 bytes, KW3[39:8] */
				KEX_EXTR_CFG(0x3, 0x0, 0x1, 0x19),
			/* Layer D:TCP */
			[NPC_LT_LD_TCP] =
				/* SPORT+DPORT: 4 bytes, KW3[39:8] */
				KEX_EXTR_CFG(0x3, 0x0, 0x1, 0x19),
		},
	},
	/* Default TX MCAM KEX profile */
	[NIX_INTF_TX] = {
		[0] = {
			/* Layer A: NIX_INST_HDR_S + Ethernet */
			/* NIX appends 8 bytes of NIX_INST_HDR_S at the
			 * start of each TX packet supplied to NPC.
			 */
			[NPC_LT_LA_IH_NIX_ETHER] =
				/* PF_FUNC: 2B , KW0 [47:32] */
				KEX_EXTR_CFG(0x01, 0x0, 0x1, 0x4),
			/* Layer A: HiGig2: */
			[NPC_LT_LA_IH_NIX_HIGIG2_ETHER] =
				/* PF_FUNC: 2B , KW0 [47:32] */
				KEX_EXTR_CFG(0x01, 0x0, 0x1, 0x4),
		},
		[1] = {
			[NPC_LT_LA_IH_NIX_ETHER] =
				/* SQ_ID 3 bytes, KW1[63:16] */
				KEX_EXTR_CFG(0x02, 0x02, 0x1, 0xa),
			[NPC_LT_LA_IH_NIX_HIGIG2_ETHER] =
				/* VID: 2 bytes, KW1[31:16] */
				KEX_EXTR_CFG(0x01, 0x10, 0x1, 0xa),
		},
		[2] = {
			/* Layer B: Single VLAN (CTAG) */
			[NPC_LT_LB_CTAG] =
				/* CTAG VLAN[2..3] KW0[63:48] */
				KEX_EXTR_CFG(0x01, 0x2, 0x1, 0x6),
			/* Layer B: Stacked VLAN (STAG|QinQ) */
			[NPC_LT_LB_STAG_QINQ] =
				/* Outer VLAN: 2 bytes, KW0[63:48] */
				KEX_EXTR_CFG(0x01, 0x2, 0x1, 0x6),
		},
		[3] = {
			[NPC_LT_LB_CTAG] =
				/* CTAG VLAN[2..3] KW1[15:0] */
				KEX_EXTR_CFG(0x01, 0x4, 0x1, 0x8),
			[NPC_LT_LB_STAG_QINQ] =
				/* Outer VLAN: 2 Bytes, KW1[15:0] */
				KEX_EXTR_CFG(0x01, 0x8, 0x1, 0x8),
		},
		[4] = {
			/* Layer C: IPv4 */
			[NPC_LT_LC_IP] =
				/* SIP+DIP: 8 bytes, KW2[63:0] */
				KEX_EXTR_CFG(0x07, 0xc, 0x1, 0x10),
			/* Layer C: IPv6 */
			[NPC_LT_LC_IP6] =
				/* Everything up to SADDR: 8 bytes, KW2[63:0] */
				KEX_EXTR_CFG(0x07, 0x0, 0x1, 0x10),
		},
		[5] = {
			/* Layer D:UDP */
			[NPC_LT_LD_UDP] =
				/* SPORT+DPORT: 4 bytes, KW3[31:0] */
				KEX_EXTR_CFG(0x3, 0x0, 0x1, 0x18),
			/* Layer D:TCP */
			[NPC_LT_LD_TCP] =
				/* SPORT+DPORT: 4 bytes, KW3[31:0] */
				KEX_EXTR_CFG(0x3, 0x0, 0x1, 0x18),
		},
	},
	},
};

struct npc_mcam_kex_extr *npc_mkex_extr_default_get(void)
{
	return &npc_mkex_extr_default;
}

static u16 npc_idx2vidx(u16 idx)
{
	unsigned long index;
	void *map;
	u16 vidx;
	int val;

	vidx = idx;
	index = idx;

	map = xa_load(&npc_priv.xa_idx2vidx_map, index);
	if (!map)
		goto done;

	val = xa_to_value(map);
	if (val == -1)
		goto done;

	vidx = val;

done:
	return vidx;
}

static bool npc_is_vidx(u16 vidx)
{
	return vidx >= npc_priv.bank_depth * 2;
}

static u16 npc_vidx2idx(u16 vidx)
{
	unsigned long sentinel = (unsigned long)-1;
	unsigned long index;
	void *map;
	int val;
	u16 idx;

	idx = vidx;
	index = vidx;

	map = xa_load(&npc_priv.xa_vidx2idx_map, index);
	if (!map)
		goto done;

	val = xa_to_value(map);
	if (val == sentinel)
		goto done;

	idx = val;

done:
	return idx;
}

u16 npc_cn20k_vidx2idx(u16 idx)
{
	if (!npc_priv.init_done)
		return idx;

	if (!npc_is_vidx(idx))
		return idx;

	return npc_vidx2idx(idx);
}

u16 npc_cn20k_idx2vidx(u16 idx)
{
	if (!npc_priv.init_done)
		return idx;

	if (npc_is_vidx(idx))
		return idx;

	return npc_idx2vidx(idx);
}

static int npc_vidx_maps_del_entry(struct rvu *rvu, u16 vidx, u16 *old_midx)
{
	u16 mcam_idx;
	void *map;

	if (!npc_is_vidx(vidx)) {
		dev_err(rvu->dev,
			"%s: vidx(%u) does not map to proper mcam idx\n",
			__func__, vidx);
		return -ESRCH;
	}

	mcam_idx = npc_vidx2idx(vidx);

	map = xa_erase(&npc_priv.xa_vidx2idx_map, vidx);
	if (!map) {
		dev_err(rvu->dev,
			"%s: vidx(%u) does not map to proper mcam idx\n",
			__func__, vidx);
		return -ESRCH;
	}

	map = xa_erase(&npc_priv.xa_idx2vidx_map, mcam_idx);
	if (!map) {
		dev_err(rvu->dev,
			"%s: vidx(%u) is not valid\n",
			__func__, vidx);
		return -ESRCH;
	}

	if (old_midx)
		*old_midx = mcam_idx;

	return 0;
}

static int npc_vidx_maps_modify(struct rvu *rvu, u16 vidx, u16 new_midx)
{
	u16 old_midx;
	void *map;
	int rc;

	if (!npc_is_vidx(vidx)) {
		dev_err(rvu->dev,
			"%s: vidx(%u) does not map to proper mcam idx\n",
			__func__, vidx);
		return -ESRCH;
	}

	map = xa_erase(&npc_priv.xa_vidx2idx_map, vidx);
	if (!map) {
		dev_err(rvu->dev,
			"%s: vidx(%u) could not be deleted from vidx2idx map\n",
			__func__, vidx);
		return -ESRCH;
	}

	old_midx = xa_to_value(map);

	rc = xa_insert(&npc_priv.xa_vidx2idx_map, vidx,
		       xa_mk_value(new_midx), GFP_KERNEL);
	if (rc) {
		dev_err(rvu->dev,
			"%s: vidx(%u) cannot be added to vidx2idx map\n",
			__func__, vidx);
		goto fail1;
	}

	map = xa_erase(&npc_priv.xa_idx2vidx_map, old_midx);
	if (!map) {
		dev_err(rvu->dev,
			"%s: old_midx(%u, vidx(%u)) cannot be added to idx2vidx map\n",
			__func__, old_midx, vidx);
		rc = -ESRCH;
		goto fail2;
	}

	rc = xa_insert(&npc_priv.xa_idx2vidx_map, new_midx,
		       xa_mk_value(vidx), GFP_KERNEL);
	if (rc) {
		dev_err(rvu->dev,
			"%s: new_midx(%u, vidx(%u)) cannot be added to idx2vidx map\n",
			__func__, new_midx, vidx);
		goto fail3;
	}

	return 0;

fail3:
	/* Restore vidx at old_midx location */
	if (xa_insert(&npc_priv.xa_idx2vidx_map, old_midx,
		      xa_mk_value(vidx), GFP_KERNEL))
		dev_err(rvu->dev,
			"%s: Error to roll back idx2vidx old_midx=%u vidx=%u\n",
			__func__, old_midx, vidx);
fail2:
	/* Erase new_midx inserted at vidx */
	if (!xa_erase(&npc_priv.xa_vidx2idx_map, vidx))
		dev_err(rvu->dev,
			"%s: Failed to roll back vidx2idx vidx=%u\n",
			__func__, vidx);

fail1:
	/* Restore old_midx at vidx location */
	if (xa_insert(&npc_priv.xa_vidx2idx_map, vidx,
		      xa_mk_value(old_midx), GFP_KERNEL))
		dev_err(rvu->dev,
			"%s: Failed to roll back vidx2idx to old_midx=%u, vidx=%u\n",
			__func__, old_midx, vidx);

	return rc;
}

static int npc_vidx_maps_add_entry(struct rvu *rvu, u16 mcam_idx, int pcifunc,
				   u16 *vidx)
{
	int rc, max, min;
	u32 id;

	/* Virtual index start from maximum mcam index + 1 */
	max = npc_priv.bank_depth * 2 * 2 - 1;
	min = npc_priv.bank_depth * 2;

	rc = xa_alloc(&npc_priv.xa_vidx2idx_map, &id,
		      xa_mk_value(mcam_idx),
		      XA_LIMIT(min, max), GFP_KERNEL);
	if (rc) {
		dev_err(rvu->dev,
			"%s: Failed to add to vidx2idx map (%u)\n",
			__func__, mcam_idx);
		goto fail1;
	}

	rc = xa_insert(&npc_priv.xa_idx2vidx_map, mcam_idx,
		       xa_mk_value(id), GFP_KERNEL);
	if (rc) {
		dev_err(rvu->dev,
			"%s: Failed to add to idx2vidx map (%u)\n",
			__func__, mcam_idx);
		goto fail2;
	}

	if (vidx)
		*vidx = id;

	return 0;

fail2:
	xa_erase(&npc_priv.xa_vidx2idx_map, id);
fail1:
	return rc;
}

static void npc_config_kpmcam(struct rvu *rvu, int blkaddr,
			      const struct npc_kpu_profile_cam *kpucam,
			      int kpm, int entry)
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
		    NPC_AF_KPMX_ENTRYX_CAMX(kpm, entry, 0), *(u64 *)&cam0);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_KPMX_ENTRYX_CAMX(kpm, entry, 1), *(u64 *)&cam1);
}

static void
npc_config_kpmaction(struct rvu *rvu, int blkaddr,
		     const struct npc_kpu_profile_action *kpuaction,
		     int kpm, int entry, bool pkind)
{
	struct npc_kpm_action0 action0 = {0};
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
		reg = NPC_AF_KPMX_ENTRYX_ACTION1(kpm, entry);

	rvu_write64(rvu, blkaddr, reg, *(u64 *)&action1);

	action0.byp_count = kpuaction->bypass_count & 0x7;
	action0.capture_ena = kpuaction->cap_ena & 1;
	action0.parse_done = kpuaction->parse_done & 1;
	action0.next_state = kpuaction->next_state & 0xf;
	action0.capture_lid = kpuaction->lid & 0x7;

	/* Parser functionality will work correctly even though
	 * upper flag bits are silently discarded
	 */
	action0.capture_ltype = kpuaction->ltype & 0xf;
	action0.capture_flags = kpuaction->flags & 0xf;
	action0.ptr_advance = kpuaction->ptr_advance;

	action0.var_len_offset = kpuaction->offset;
	action0.var_len_mask = kpuaction->mask;
	action0.var_len_right = kpuaction->right & 1;
	action0.var_len_shift = kpuaction->shift & 1;

	if (pkind)
		reg = NPC_AF_PKINDX_ACTION0(entry);
	else
		reg = NPC_AF_KPMX_ENTRYX_ACTION0(kpm, entry);

	rvu_write64(rvu, blkaddr, reg, *(u64 *)&action0);
}

static void
npc_program_single_kpm_profile(struct rvu *rvu, int blkaddr,
			       int kpm, int start_entry,
			       const struct npc_kpu_profile *profile)
{
	int entry, num_entries, max_entries;
	u64 idx;

	if (profile->cam_entries != profile->action_entries) {
		dev_err(rvu->dev,
			"kpm%d: CAM and action entries [%d != %d] not equal\n",
			kpm, profile->cam_entries, profile->action_entries);

		WARN(1, "Fatal error\n");
		return;
	}

	max_entries = rvu->hw->npc_kpu_entries / 2;
	entry = start_entry;
	/* Program CAM match entries for previous kpm extracted data */
	num_entries = min_t(int, profile->cam_entries, max_entries);
	for (idx = 0; entry < num_entries + start_entry; entry++, idx++)
		npc_config_kpmcam(rvu, blkaddr, &profile->cam[idx],
				  kpm, entry);

	entry = start_entry;
	/* Program this kpm's actions */
	num_entries = min_t(int, profile->action_entries, max_entries);
	for (idx = 0; entry < num_entries + start_entry; entry++, idx++)
		npc_config_kpmaction(rvu, blkaddr, &profile->action[idx],
				     kpm, entry, false);
}

static void
npc_enable_kpm_entry(struct rvu *rvu, int blkaddr, int kpm, int num_entries)
{
	u64 entry_mask;

	entry_mask = npc_enable_mask(num_entries);
	/* Disable first KPU_CN20K_MAX_CST_ENT entries for built-in profile */
	if (!rvu->kpu.custom)
		entry_mask |= GENMASK_ULL(KPU_CN20K_MAX_CST_ENT - 1, 0);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_KPMX_ENTRY_DISX(kpm, 0), entry_mask);
	if (num_entries <= 64) {
		/* Disable all the entries in W1, W2 and W3 */
		rvu_write64(rvu, blkaddr,
			    NPC_AF_KPMX_ENTRY_DISX(kpm, 1),
			    npc_enable_mask(0));
		rvu_write64(rvu, blkaddr,
			    NPC_AF_KPMX_ENTRY_DISX(kpm, 2),
			    npc_enable_mask(0));
		rvu_write64(rvu, blkaddr,
			    NPC_AF_KPMX_ENTRY_DISX(kpm, 3),
			    npc_enable_mask(0));
		return;
	}

	num_entries = num_entries - 64;
	entry_mask = npc_enable_mask(num_entries);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_KPMX_ENTRY_DISX(kpm, 1), entry_mask);
	if (num_entries <= 64) {
		/* Disable all the entries in W2 and W3 */
		rvu_write64(rvu, blkaddr,
			    NPC_AF_KPMX_ENTRY_DISX(kpm, 2),
			    npc_enable_mask(0));
		rvu_write64(rvu, blkaddr,
			    NPC_AF_KPMX_ENTRY_DISX(kpm, 3),
			    npc_enable_mask(0));
		return;
	}

	num_entries = num_entries - 64;
	entry_mask = npc_enable_mask(num_entries);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_KPMX_ENTRY_DISX(kpm, 2), entry_mask);
	if (num_entries <= 64) {
		/* Disable all the entries in W3 */
		rvu_write64(rvu, blkaddr,
			    NPC_AF_KPMX_ENTRY_DISX(kpm, 3),
			    npc_enable_mask(0));
		return;
	}

	num_entries = num_entries - 64;
	entry_mask = npc_enable_mask(num_entries);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_KPMX_ENTRY_DISX(kpm, 3), entry_mask);
}

#define KPU_OFFSET	8
static void npc_program_kpm_profile(struct rvu *rvu, int blkaddr, int num_kpms)
{
	const struct npc_kpu_profile *profile1, *profile2;
	int idx, total_cam_entries;

	for (idx = 0; idx < num_kpms; idx++) {
		profile1 = &rvu->kpu.kpu[idx];
		npc_program_single_kpm_profile(rvu, blkaddr, idx, 0, profile1);
		profile2 = &rvu->kpu.kpu[idx + KPU_OFFSET];
		npc_program_single_kpm_profile(rvu, blkaddr, idx,
					       profile1->cam_entries,
					       profile2);
		total_cam_entries = profile1->cam_entries +
			profile2->cam_entries;
		npc_enable_kpm_entry(rvu, blkaddr, idx, total_cam_entries);
		rvu_write64(rvu, blkaddr, NPC_AF_KPMX_PASS2_OFFSET(idx),
			    profile1->cam_entries);
		/* Enable the KPUs associated with this KPM */
		rvu_write64(rvu, blkaddr, NPC_AF_KPUX_CFG(idx), 0x01);
		rvu_write64(rvu, blkaddr, NPC_AF_KPUX_CFG(idx + KPU_OFFSET),
			    0x01);
	}
}

void npc_cn20k_parser_profile_init(struct rvu *rvu, int blkaddr)
{
	struct rvu_hwinfo *hw = rvu->hw;
	int num_pkinds, idx;

	/* Disable all KPMs and their entries */
	for (idx = 0; idx < hw->npc_kpms; idx++) {
		rvu_write64(rvu, blkaddr,
			    NPC_AF_KPMX_ENTRY_DISX(idx, 0), ~0ULL);
		rvu_write64(rvu, blkaddr,
			    NPC_AF_KPMX_ENTRY_DISX(idx, 1), ~0ULL);
		rvu_write64(rvu, blkaddr,
			    NPC_AF_KPMX_ENTRY_DISX(idx, 2), ~0ULL);
		rvu_write64(rvu, blkaddr,
			    NPC_AF_KPMX_ENTRY_DISX(idx, 3), ~0ULL);
	}

	for (idx = 0; idx < hw->npc_kpus; idx++)
		rvu_write64(rvu, blkaddr, NPC_AF_KPUX_CFG(idx), 0x00);

	/* Load and customize KPU profile. */
	npc_load_kpu_profile(rvu);

	/* Configure KPU and KPM mapping for second pass */
	rvu_write64(rvu, blkaddr, NPC_AF_KPM_PASS2_CFG, 0x76543210);

	/* First program IKPU profile i.e PKIND configs.
	 * Check HW max count to avoid configuring junk or
	 * writing to unsupported CSR addresses.
	 */
	num_pkinds = rvu->kpu.pkinds;
	num_pkinds = min_t(int, hw->npc_pkinds, num_pkinds);

	for (idx = 0; idx < num_pkinds; idx++)
		npc_config_kpmaction(rvu, blkaddr, &rvu->kpu.ikpu[idx],
				     0, idx, true);

	/* Program KPM CAM and Action profiles */
	npc_program_kpm_profile(rvu, blkaddr, hw->npc_kpms);
}

struct npc_priv_t *npc_priv_get(void)
{
	return &npc_priv;
}

static void npc_program_mkex_rx(struct rvu *rvu, int blkaddr,
				struct npc_mcam_kex_extr *mkex_extr,
				u8 intf)
{
	u8 num_extr = rvu->hw->npc_kex_extr;
	int extr, lt;
	u64 val;

	if (is_npc_intf_tx(intf))
		return;

	rvu_write64(rvu, blkaddr, NPC_AF_INTFX_KEX_CFG(intf),
		    mkex_extr->keyx_cfg[NIX_INTF_RX]);

	/* Program EXTRACTOR */
	for (extr = 0; extr < num_extr; extr++)
		rvu_write64(rvu, blkaddr,
			    NPC_AF_INTFX_EXTRACTORX_CFG(intf, extr),
			    mkex_extr->intf_extr_lid[intf][extr]);

	/* Program EXTRACTOR_LTYPE */
	for (extr = 0; extr < num_extr; extr++) {
		for (lt = 0; lt < NPC_MAX_LT; lt++) {
			val = mkex_extr->intf_extr_lt[intf][extr][lt];
			CN20K_SET_EXTR_LT(intf, extr, lt, val);
		}
	}
}

static void npc_program_mkex_tx(struct rvu *rvu, int blkaddr,
				struct npc_mcam_kex_extr *mkex_extr,
				u8 intf)
{
	u8 num_extr = rvu->hw->npc_kex_extr;
	int extr, lt;
	u64 val;

	if (is_npc_intf_rx(intf))
		return;

	rvu_write64(rvu, blkaddr, NPC_AF_INTFX_KEX_CFG(intf),
		    mkex_extr->keyx_cfg[NIX_INTF_TX]);

	/* Program EXTRACTOR */
	for (extr = 0; extr < num_extr; extr++)
		rvu_write64(rvu, blkaddr,
			    NPC_AF_INTFX_EXTRACTORX_CFG(intf, extr),
			    mkex_extr->intf_extr_lid[intf][extr]);

	/* Program EXTRACTOR_LTYPE */
	for (extr = 0; extr < num_extr; extr++) {
		for (lt = 0; lt < NPC_MAX_LT; lt++) {
			val = mkex_extr->intf_extr_lt[intf][extr][lt];
			CN20K_SET_EXTR_LT(intf, extr, lt, val);
		}
	}
}

static void npc_program_mkex_profile(struct rvu *rvu, int blkaddr,
				     struct npc_mcam_kex_extr *mkex_extr)
{
	struct rvu_hwinfo *hw = rvu->hw;
	u8 intf;

	for (intf = 0; intf < hw->npc_intfs; intf++) {
		npc_program_mkex_rx(rvu, blkaddr, mkex_extr, intf);
		npc_program_mkex_tx(rvu, blkaddr, mkex_extr, intf);
	}

	/* Programme mkex hash profile */
	npc_program_mkex_hash(rvu, blkaddr);
}

void npc_cn20k_load_mkex_profile(struct rvu *rvu, int blkaddr,
				 const char *mkex_profile)
{
	struct npc_mcam_kex_extr *mcam_kex_extr;
	struct device *dev = &rvu->pdev->dev;
	void __iomem *mkex_prfl_addr = NULL;
	u64 prfl_sz;
	int ret;

	/* If user not selected mkex profile */
	if (rvu->kpu_fwdata_sz ||
	    !strncmp(mkex_profile, cn20k_def_pfl_name, MKEX_NAME_LEN))
		goto program_mkex_extr;

	/* Setting up the mapping for mkex profile image */
	ret = npc_fwdb_prfl_img_map(rvu, &mkex_prfl_addr, &prfl_sz);
	if (ret < 0)
		goto program_mkex_extr;

	mcam_kex_extr = (struct npc_mcam_kex_extr __force *)mkex_prfl_addr;

	while (((s64)prfl_sz > 0) &&
	       (mcam_kex_extr->mkex_sign != MKEX_END_SIGN)) {
		/* Compare with mkex mod_param name string */
		if (mcam_kex_extr->mkex_sign == MKEX_CN20K_SIGN &&
		    !strncmp(mcam_kex_extr->name, mkex_profile,
			     MKEX_NAME_LEN)) {
			rvu->kpu.mcam_kex_prfl.mkex_extr = mcam_kex_extr;
			goto program_mkex_extr;
		}

		mcam_kex_extr++;
		prfl_sz -= sizeof(struct npc_mcam_kex_extr);
	}
	dev_warn(dev, "Failed to load requested profile: %s\n", mkex_profile);
	rvu->kpu.mcam_kex_prfl.mkex_extr = npc_mkex_extr_default_get();

program_mkex_extr:
	dev_info(rvu->dev, "Using %s mkex profile\n",
		 rvu->kpu.mcam_kex_prfl.mkex_extr->name);
	/* Program selected mkex profile */
	npc_program_mkex_profile(rvu, blkaddr,
				 rvu->kpu.mcam_kex_prfl.mkex_extr);
	if (mkex_prfl_addr)
		iounmap(mkex_prfl_addr);
}

void
npc_cn20k_enable_mcam_entry(struct rvu *rvu, int blkaddr,
			    int index, bool enable)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	int mcam_idx = index % mcam->banksize;
	int bank = index / mcam->banksize;
	u64 cfg, hw_prio;
	u8 kw_type;

	npc_mcam_idx_2_key_type(rvu, index, &kw_type);
	if (kw_type == NPC_MCAM_KEY_X2) {
		cfg = rvu_read64(rvu, blkaddr,
				 NPC_AF_CN20K_MCAMEX_BANKX_CFG_EXT(mcam_idx,
								   bank));
		hw_prio = cfg & GENMASK_ULL(14, 8);
		cfg = enable ? 1 : 0;
		cfg |= hw_prio;
		rvu_write64(rvu, blkaddr,
			    NPC_AF_CN20K_MCAMEX_BANKX_CFG_EXT(mcam_idx, bank),
			    cfg);
		return;
	}

	/* For NPC_CN20K_MCAM_KEY_X4 keys, both the banks
	 * need to be programmed with the same value.
	 */
	for (bank = 0; bank < mcam->banks_per_entry; bank++) {
		cfg = rvu_read64(rvu, blkaddr,
				 NPC_AF_CN20K_MCAMEX_BANKX_CFG_EXT(mcam_idx,
								   bank));
		hw_prio = cfg & GENMASK_ULL(14, 8);
		cfg = enable ? 1 : 0;
		cfg |= hw_prio;
		rvu_write64(rvu, blkaddr,
			    NPC_AF_CN20K_MCAMEX_BANKX_CFG_EXT(mcam_idx, bank),
			    cfg);
	}
}

void
npc_cn20k_clear_mcam_entry(struct rvu *rvu, int blkaddr, int bank, int index)
{
	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_INTF_EXT(index, bank, 1),
		    0);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_INTF_EXT(index, bank, 0),
		    0);

	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W0_EXT(index, bank, 1), 0);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W0_EXT(index, bank, 0), 0);

	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W1_EXT(index, bank, 1), 0);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W1_EXT(index, bank, 0), 0);

	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W2_EXT(index, bank, 1), 0);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W2_EXT(index, bank, 0), 0);

	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W3_EXT(index, bank, 1), 0);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W3_EXT(index, bank, 0), 0);

	/* Clear corresponding stats register */
	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_STAT_EXT(index, bank), 0);
}

static void npc_cn20k_get_keyword(struct cn20k_mcam_entry *entry, int idx,
				  u64 *cam0, u64 *cam1)
{
	u64 kw_mask;

	/* The two banks of every MCAM entry are used as a single double-wide
	 * entry that is compared with the search key as follows:
	 *
	 * NPC_AF_MCAME()_BANK(0)_CAM(0..1)_W0_EXT[MD] ->NPC_MCAM_KEY_X4_S[KW0]
	 * NPC_AF_MCAME()_BANK(0)_CAM(0..1)_W1_EXT[MD] ->NPC_MCAM_KEY_X4_S[KW1]
	 * NPC_AF_MCAME()_BANK(0)_CAM(0..1)_W2_EXT[MD] ->NPC_MCAM_KEY_X4_S[KW2]
	 * NPC_AF_MCAME()_BANK(0)_CAM(0..1)_W3_EXT[MD] ->NPC_MCAM_KEY_X4_S[KW3]
	 * NPC_AF_MCAME()_BANK(1)_CAM(0..1)_W0_EXT[MD] ->NPC_MCAM_KEY_X4_S[KW4]
	 * NPC_AF_MCAME()_BANK(1)_CAM(0..1)_W1_EXT[MD] ->NPC_MCAM_KEY_X4_S[KW5]
	 * NPC_AF_MCAME()_BANK(1)_CAM(0..1)_W2_EXT[MD] ->NPC_MCAM_KEY_X4_S[KW6]
	 * NPC_AF_MCAME()_BANK(1)_CAM(0..1)_W3_EXT[MD] ->NPC_MCAM_KEY_X4_S[KW7]
	 */
	*cam1 = entry->kw[idx];
	kw_mask = entry->kw_mask[idx];
	*cam1 &= kw_mask;
	*cam0 = ~*cam1 & kw_mask;
}

/*-----------------------------------------------------------------------------|
 *Kex type  |  mcam entry   |  cam1	   |	cam 0	|| <----- output ----> |
 *profile   |  len	    | (key type)   | (key type)	|| len	  |   type     |
 *-----------------------------------------------------------------------------|
 *X2	    |  256 (X2)	    |  001b	   |	110b	|| X2	  |    X2      |
 *-----------------------------------------------------------------------------|
 *X4	    |  256 (X2)	    |  000b	   |	000b	|| X2	  |  DYNAMIC   |
 *-----------------------------------------------------------------------------|
 *X4	    |  512 (X4)	    |  010b	   |	101b	|| X4	  |    X4      |
 *-----------------------------------------------------------------------------|
 *DYN	    |  256 (X2)	    |  000b	   |	000b	|| X2	  |  DYNAMIC   |
 *-----------------------------------------------------------------------------|
 *DYN	    |  512 (X4)	    |  010b	   |	101b	|| X4	  |    X4      |
 *-----------------------------------------------------------------------------|
 */
static void npc_cn20k_config_kw_x2(struct rvu *rvu, struct npc_mcam *mcam,
				   int blkaddr, int index, u8 intf,
				   struct cn20k_mcam_entry *entry,
				   int bank, u8 kw_type, int kw)
{
	u64 intf_ext = 0, intf_ext_mask = 0;
	u8 tx_intf_mask = ~intf & 0x3;
	u8 tx_intf = intf, kex_type;
	u8 kw_type_mask = ~kw_type;
	u64 cam0, cam1, kex_cfg;

	if (is_npc_intf_tx(intf)) {
		/* Last bit must be set and rest don't care
		 * for TX interfaces
		 */
		tx_intf_mask = 0x1;
		tx_intf = intf & tx_intf_mask;
		tx_intf_mask = ~tx_intf & tx_intf_mask;
	}

	kex_cfg = rvu_read64(rvu, blkaddr, NPC_AF_INTFX_KEX_CFG(intf));
	kex_type = (kex_cfg & GENMASK_ULL(34, 32)) >> 32;
	if ((kex_type == NPC_MCAM_KEY_DYN || kex_type == NPC_MCAM_KEY_X4) &&
	    kw_type == NPC_MCAM_KEY_X2) {
		kw_type = 0;
		kw_type_mask = 0;
	}

	intf_ext = ((u64)kw_type << 16) | tx_intf;
	intf_ext_mask = (((u64)kw_type_mask  << 16) & GENMASK_ULL(18, 16)) |
		tx_intf_mask;
	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_INTF_EXT(index, bank, 1),
		    intf_ext);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_INTF_EXT(index, bank, 0),
		    intf_ext_mask);

	/* Set the match key */
	npc_cn20k_get_keyword(entry, kw, &cam0, &cam1);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W0_EXT(index, bank, 1),
		    cam1);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W0_EXT(index, bank, 0),
		    cam0);

	npc_cn20k_get_keyword(entry, kw + 1, &cam0, &cam1);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W1_EXT(index, bank, 1),
		    cam1);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W1_EXT(index, bank, 0),
		    cam0);

	npc_cn20k_get_keyword(entry, kw + 2, &cam0, &cam1);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W2_EXT(index, bank, 1),
		    cam1);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W2_EXT(index, bank, 0),
		    cam0);

	npc_cn20k_get_keyword(entry, kw + 3, &cam0, &cam1);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W3_EXT(index, bank, 1),
		    cam1);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W3_EXT(index, bank, 0),
		    cam0);
}

static void npc_cn20k_config_kw_x4(struct rvu *rvu, struct npc_mcam *mcam,
				   int blkaddr, int index, u8 intf,
				   struct cn20k_mcam_entry *entry,
				   u8 kw_type)
{
	int kw = 0, bank;

	for (bank = 0; bank < mcam->banks_per_entry; bank++, kw = kw + 4)
		npc_cn20k_config_kw_x2(rvu, mcam, blkaddr,
				       index, intf,
				       entry, bank, kw_type, kw);
}

static void
npc_cn20k_set_mcam_bank_cfg(struct rvu *rvu, int blkaddr, int mcam_idx,
			    int bank, u8 kw_type, bool enable, u8 hw_prio)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u64 bank_cfg;

	bank_cfg = (u64)hw_prio << 8;
	if (enable)
		bank_cfg |= 0x1;

	if (kw_type == NPC_MCAM_KEY_X2) {
		rvu_write64(rvu, blkaddr,
			    NPC_AF_CN20K_MCAMEX_BANKX_CFG_EXT(mcam_idx, bank),
			    bank_cfg);
		return;
	}

	/* For NPC_MCAM_KEY_X4 keys, both the banks
	 * need to be programmed with the same value.
	 */
	for (bank = 0; bank < mcam->banks_per_entry; bank++) {
		rvu_write64(rvu, blkaddr,
			    NPC_AF_CN20K_MCAMEX_BANKX_CFG_EXT(mcam_idx, bank),
			    bank_cfg);
	}
}

void npc_cn20k_config_mcam_entry(struct rvu *rvu, int blkaddr, int index,
				 u8 intf, struct cn20k_mcam_entry *entry,
				 bool enable, u8 hw_prio)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	int mcam_idx = index % mcam->banksize;
	int bank = index / mcam->banksize;
	int kw = 0;
	u8 kw_type;

	/* Disable before mcam entry update */
	npc_cn20k_enable_mcam_entry(rvu, blkaddr, index, false);

	npc_mcam_idx_2_key_type(rvu, index, &kw_type);
	/* CAM1 takes the comparison value and
	 * CAM0 specifies match for a bit in key being '0' or '1' or 'dontcare'.
	 * CAM1<n> = 0 & CAM0<n> = 1 => match if key<n> = 0
	 * CAM1<n> = 1 & CAM0<n> = 0 => match if key<n> = 1
	 * CAM1<n> = 0 & CAM0<n> = 0 => always match i.e dontcare.
	 */
	if (kw_type == NPC_MCAM_KEY_X2) {
		/* Clear mcam entry to avoid writes being suppressed by NPC */
		npc_cn20k_clear_mcam_entry(rvu, blkaddr, bank, mcam_idx);
		npc_cn20k_config_kw_x2(rvu, mcam, blkaddr,
				       mcam_idx, intf, entry,
				       bank, kw_type, kw);
		/* Set 'action' */
		rvu_write64(rvu, blkaddr,
			    NPC_AF_CN20K_MCAMEX_BANKX_ACTIONX_EXT(mcam_idx,
								  bank, 0),
			    entry->action);

		/* Set TAG 'action' */
		rvu_write64(rvu, blkaddr,
			    NPC_AF_CN20K_MCAMEX_BANKX_ACTIONX_EXT(mcam_idx,
								  bank, 1),
			    entry->vtag_action);

		goto set_cfg;
	}
	/* Clear mcam entry to avoid writes being suppressed by NPC */
	npc_cn20k_clear_mcam_entry(rvu, blkaddr, 0, mcam_idx);
	npc_cn20k_clear_mcam_entry(rvu, blkaddr, 1, mcam_idx);

	npc_cn20k_config_kw_x4(rvu, mcam, blkaddr,
			       mcam_idx, intf, entry, kw_type);
	for (bank = 0; bank < mcam->banks_per_entry; bank++) {
		/* Set 'action' */
		rvu_write64(rvu, blkaddr,
			    NPC_AF_CN20K_MCAMEX_BANKX_ACTIONX_EXT(mcam_idx,
								  bank, 0),
			    entry->action);

		/* Set TAG 'action' */
		rvu_write64(rvu, blkaddr,
			    NPC_AF_CN20K_MCAMEX_BANKX_ACTIONX_EXT(mcam_idx,
								  bank, 1),
			    entry->vtag_action);
	}

set_cfg:
	/* TODO: */
	/* PF installing VF rule */
	npc_cn20k_set_mcam_bank_cfg(rvu, blkaddr, mcam_idx, bank,
				    kw_type, enable, hw_prio);
}

void npc_cn20k_copy_mcam_entry(struct rvu *rvu, int blkaddr, u16 src, u16 dest)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u64 cfg, sreg, dreg, soff, doff;
	u8 src_kwtype, dest_kwtype;
	int bank, i, sb, db;
	int dbank, sbank;

	dbank = npc_get_bank(mcam, dest);
	sbank = npc_get_bank(mcam, src);
	npc_mcam_idx_2_key_type(rvu, src, &src_kwtype);
	npc_mcam_idx_2_key_type(rvu, dest, &dest_kwtype);
	if (src_kwtype != dest_kwtype)
		return;

	src &= (mcam->banksize - 1);
	dest &= (mcam->banksize - 1);

	/* Copy INTF's, W0's, W1's, W2's, W3s CAM0 and CAM1 configuration */
	for (bank = 0; bank < mcam->banks_per_entry; bank++) {
		sb = sbank + bank;
		sreg = NPC_AF_CN20K_MCAMEX_BANKX_CAMX_INTF_EXT(src, sb, 0);
		db = dbank + bank;
		dreg = NPC_AF_CN20K_MCAMEX_BANKX_CAMX_INTF_EXT(dest, db, 0);
		for (i = 0; i < 10; i++) {
			cfg = rvu_read64(rvu, blkaddr, sreg + (i * 8));
			rvu_write64(rvu, blkaddr, dreg + (i * 8), cfg);
		}

		/* Copy action */
		for (i = 0; i < 3; i++) {
			soff = NPC_AF_CN20K_MCAMEX_BANKX_ACTIONX_EXT(src,
								     sb, i);
			cfg = rvu_read64(rvu, blkaddr, soff);

			doff = NPC_AF_CN20K_MCAMEX_BANKX_ACTIONX_EXT(dest, db,
								     i);
			rvu_write64(rvu, blkaddr, doff, cfg);
		}

		/* Copy bank configuration */
		cfg = rvu_read64(rvu, blkaddr,
				 NPC_AF_CN20K_MCAMEX_BANKX_CFG_EXT(src, sb));
		rvu_write64(rvu, blkaddr,
			    NPC_AF_CN20K_MCAMEX_BANKX_CFG_EXT(dest, db), cfg);
		if (src_kwtype == NPC_MCAM_KEY_X2)
			break;
	}
}

static void npc_cn20k_fill_entryword(struct cn20k_mcam_entry *entry, int idx,
				     u64 cam0, u64 cam1)
{
	entry->kw[idx] = cam1;
	entry->kw_mask[idx] = cam1 ^ cam0;
}

void npc_cn20k_read_mcam_entry(struct rvu *rvu, int blkaddr, u16 index,
			       struct cn20k_mcam_entry *entry,
			       u8 *intf, u8 *ena, u8 *hw_prio)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u64 cam0, cam1, bank_cfg, cfg;
	int kw = 0, bank;
	u8 kw_type;

	npc_mcam_idx_2_key_type(rvu, index, &kw_type);

	bank = npc_get_bank(mcam, index);
	index &= (mcam->banksize - 1);

	cfg = rvu_read64(rvu, blkaddr,
			 NPC_AF_CN20K_MCAMEX_BANKX_CAMX_INTF_EXT(index,
								 bank, 1)) & 3;
	*intf = cfg;

	bank_cfg = rvu_read64(rvu, blkaddr,
			      NPC_AF_CN20K_MCAMEX_BANKX_CFG_EXT(index, bank));
	*ena = bank_cfg & 0x1;
	*hw_prio = (bank_cfg & GENMASK_ULL(14, 8)) >> 8;
	if (kw_type == NPC_MCAM_KEY_X2) {
		cam1 = rvu_read64(rvu, blkaddr,
				  NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W0_EXT(index,
									bank,
									1));
		cam0 = rvu_read64(rvu, blkaddr,
				  NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W0_EXT(index,
									bank,
									0));
		npc_cn20k_fill_entryword(entry, kw, cam0, cam1);

		cam1 = rvu_read64(rvu, blkaddr,
				  NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W1_EXT(index,
									bank,
									1));
		cam0 = rvu_read64(rvu, blkaddr,
				  NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W1_EXT(index,
									bank,
									0));
		npc_cn20k_fill_entryword(entry, kw + 1, cam0, cam1);

		cam1 = rvu_read64(rvu, blkaddr,
				  NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W2_EXT(index,
									bank,
									1));
		cam0 = rvu_read64(rvu, blkaddr,
				  NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W2_EXT(index,
									bank,
									0));
		npc_cn20k_fill_entryword(entry, kw + 2, cam0, cam1);

		cam1 = rvu_read64(rvu, blkaddr,
				  NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W3_EXT(index,
									bank,
									1));
		cam0 = rvu_read64(rvu, blkaddr,
				  NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W3_EXT(index,
									bank,
									0));
		npc_cn20k_fill_entryword(entry, kw + 3, cam0, cam1);
		goto read_action;
	}

	for (bank = 0; bank < mcam->banks_per_entry; bank++, kw = kw + 4) {
		cam1 = rvu_read64(rvu, blkaddr,
				  NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W0_EXT(index,
									bank,
									1));
		cam0 = rvu_read64(rvu, blkaddr,
				  NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W0_EXT(index,
									bank,
									0));
		npc_cn20k_fill_entryword(entry, kw, cam0, cam1);

		cam1 = rvu_read64(rvu, blkaddr,
				  NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W1_EXT(index,
									bank,
									1));
		cam0 = rvu_read64(rvu, blkaddr,
				  NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W1_EXT(index,
									bank,
									0));
		npc_cn20k_fill_entryword(entry, kw + 1, cam0, cam1);

		cam1 = rvu_read64(rvu, blkaddr,
				  NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W2_EXT(index,
									bank,
									1));
		cam0 = rvu_read64(rvu, blkaddr,
				  NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W2_EXT(index,
									bank,
									0));
		npc_cn20k_fill_entryword(entry, kw + 2, cam0, cam1);

		cam1 = rvu_read64(rvu, blkaddr,
				  NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W3_EXT(index,
									bank,
									1));
		cam0 = rvu_read64(rvu, blkaddr,
				  NPC_AF_CN20K_MCAMEX_BANKX_CAMX_W3_EXT(index,
									bank,
									0));
		npc_cn20k_fill_entryword(entry, kw + 3, cam0, cam1);
	}

read_action:
	/* 'action' is set to same value for both bank '0' and '1'.
	 * Hence, reading bank '0' should be enough.
	 */
	cfg = rvu_read64(rvu, blkaddr,
			 NPC_AF_CN20K_MCAMEX_BANKX_ACTIONX_EXT(index, 0, 0));
	entry->action = cfg;

	cfg = rvu_read64(rvu, blkaddr,
			 NPC_AF_CN20K_MCAMEX_BANKX_ACTIONX_EXT(index, 0, 1));
	entry->vtag_action = cfg;
}

int rvu_mbox_handler_npc_cn20k_mcam_write_entry(struct rvu *rvu,
						struct npc_cn20k_mcam_write_entry_req *req,
						struct msg_rsp *rsp)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, req->hdr.pcifunc);
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u16 pcifunc = req->hdr.pcifunc;
	int blkaddr, rc;
	u8 nix_intf;

	req->entry = npc_cn20k_vidx2idx(req->entry);

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return NPC_MCAM_INVALID_REQ;

	mutex_lock(&mcam->lock);
	rc = npc_mcam_verify_entry(mcam, pcifunc, req->entry);
	if (rc)
		goto exit;

	if (!is_npc_interface_valid(rvu, req->intf)) {
		rc = NPC_MCAM_INVALID_REQ;
		goto exit;
	}

	if (is_npc_intf_tx(req->intf))
		nix_intf = pfvf->nix_tx_intf;
	else
		nix_intf = pfvf->nix_rx_intf;

	/* For AF installed rules, the nix_intf should be set to target NIX */
	if (is_pffunc_af(req->hdr.pcifunc))
		nix_intf = req->intf;

	npc_cn20k_config_mcam_entry(rvu, blkaddr, req->entry, nix_intf,
				    &req->entry_data, req->enable_entry,
				    req->hw_prio);

	rc = 0;
exit:
	mutex_unlock(&mcam->lock);
	return rc;
}

int rvu_mbox_handler_npc_cn20k_mcam_read_entry(struct rvu *rvu,
					       struct npc_mcam_read_entry_req *req,
					       struct npc_cn20k_mcam_read_entry_rsp *rsp)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u16 pcifunc = req->hdr.pcifunc;
	int blkaddr, rc;

	req->entry = npc_cn20k_vidx2idx(req->entry);

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return NPC_MCAM_INVALID_REQ;

	mutex_lock(&mcam->lock);
	rc = npc_mcam_verify_entry(mcam, pcifunc, req->entry);
	if (!rc)
		npc_cn20k_read_mcam_entry(rvu, blkaddr, req->entry,
					  &rsp->entry_data, &rsp->intf,
					  &rsp->enable, &rsp->hw_prio);

	mutex_unlock(&mcam->lock);
	return rc;
}

int rvu_mbox_handler_npc_cn20k_mcam_alloc_and_write_entry(struct rvu *rvu,
							  struct npc_cn20k_mcam_alloc_and_write_entry_req *req,
							  struct npc_mcam_alloc_and_write_entry_rsp *rsp)
{
	struct rvu_pfvf *pfvf = rvu_get_pfvf(rvu, req->hdr.pcifunc);
	struct npc_mcam_alloc_entry_req entry_req;
	struct npc_mcam_alloc_entry_rsp entry_rsp;
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u16 entry = NPC_MCAM_ENTRY_INVALID;
	int blkaddr, rc;
	u8 nix_intf;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0)
		return NPC_MCAM_INVALID_REQ;

	if (!is_npc_interface_valid(rvu, req->intf))
		return NPC_MCAM_INVALID_REQ;

	/* Try to allocate a MCAM entry */
	entry_req.hdr.pcifunc = req->hdr.pcifunc;
	entry_req.contig = true;
	entry_req.ref_prio = req->ref_prio;
	entry_req.ref_entry = req->ref_entry;
	entry_req.count = 1;
	entry_req.virt = req->virt;

	rc = rvu_mbox_handler_npc_mcam_alloc_entry(rvu,
						   &entry_req, &entry_rsp);
	if (rc)
		return rc;

	if (!entry_rsp.count)
		return NPC_MCAM_ALLOC_FAILED;

	/* entry_req.count is 1, so single entry is allocated */
	entry = npc_cn20k_vidx2idx(entry_rsp.entry);

	mutex_lock(&mcam->lock);

	if (is_npc_intf_tx(req->intf))
		nix_intf = pfvf->nix_tx_intf;
	else
		nix_intf = pfvf->nix_rx_intf;

	npc_cn20k_config_mcam_entry(rvu, blkaddr, entry, nix_intf,
				    &req->entry_data, req->enable_entry,
				    req->hw_prio);

	mutex_unlock(&mcam->lock);

	rsp->entry = entry_rsp.entry;
	return 0;
}

int rvu_mbox_handler_npc_cn20k_read_base_steer_rule(struct rvu *rvu,
						    struct msg_req *req,
						    struct npc_cn20k_mcam_read_base_rule_rsp *rsp)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	int index, blkaddr, nixlf, rc = 0;
	u16 pcifunc = req->hdr.pcifunc;
	u8 intf, enable, hw_prio;
	struct rvu_pfvf *pfvf;

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
	npc_cn20k_read_mcam_entry(rvu, blkaddr, index,
				  &rsp->entry, &intf,
				  &enable, &hw_prio);
	mutex_unlock(&mcam->lock);
out:
	return rc;
}

static u8 npc_map2cn20k_flag(u8 flag)
{
	switch (flag) {
	case NPC_F_LC_U_IP_FRAG:
		return NPC_CN20K_F_LC_L_IP_FRAG;

	case NPC_F_LC_U_IP6_FRAG:
		return NPC_CN20K_F_LC_L_IP6_FRAG;

	case NPC_F_LC_L_6TO4:
		return NPC_CN20K_F_LC_L_6TO4;

	case NPC_F_LC_L_MPLS_IN_IP:
		return NPC_CN20K_F_LC_U_MPLS_IN_IP;

	case NPC_F_LC_L_IP6_TUN_IP6:
		return NPC_CN20K_F_LC_U_IP6_TUN_IP6;

	case NPC_F_LC_L_IP6_MPLS_IN_IP:
		return NPC_CN20K_F_LC_U_IP6_MPLS_IN_IP;

	default:
		break;
	}

	WARN(1, "%s: Invalid flag=%u\n", __func__, flag);
	return 0xff;
}

void
npc_cn20k_update_action_entries_n_flags(struct rvu *rvu,
					struct npc_kpu_profile_adapter *pfl)
{
	struct npc_kpu_profile_action *action;
	int entries, ltype;
	u8 flags, val;

	for (int i = 0; i < pfl->kpus; i++) {
		action = pfl->kpu[i].action;
		entries = pfl->kpu[i].action_entries;

		for (int j = 0; j < entries; j++) {
			if (action[j].lid != NPC_LID_LC)
				continue;

			ltype = action[j].ltype;

			if (ltype != NPC_LT_LC_IP &&
			    ltype != NPC_LT_LC_IP6 &&
			    ltype != NPC_LT_LC_IP_OPT &&
			    ltype != NPC_LT_LC_IP6_EXT)
				continue;

			flags = action[j].flags;

			switch (flags) {
			case NPC_F_LC_U_IP_FRAG:
			case NPC_F_LC_U_IP6_FRAG:
			case NPC_F_LC_L_6TO4:
			case NPC_F_LC_L_MPLS_IN_IP:
			case NPC_F_LC_L_IP6_TUN_IP6:
			case NPC_F_LC_L_IP6_MPLS_IN_IP:
				val = npc_map2cn20k_flag(flags);
				if (val == 0xFF) {
					dev_err(rvu->dev,
						"%s: Error to get flag value\n",
						__func__);
					return;
				}

				action[j].flags = val;
				break;
			default:
				break;
			}
		}
	}
}

int npc_cn20k_apply_custom_kpu(struct rvu *rvu,
			       struct npc_kpu_profile_adapter *profile)
{
	size_t hdr_sz = sizeof(struct npc_cn20k_kpu_profile_fwdata);
	struct npc_cn20k_kpu_profile_fwdata *fw = rvu->kpu_fwdata;
	struct npc_kpu_profile_action *action;
	struct npc_kpu_profile_cam *cam;
	struct npc_kpu_fwdata *fw_kpu;
	size_t offset = 0;
	u16 kpu, entry;
	int entries;

	hdr_sz = sizeof(struct npc_cn20k_kpu_profile_fwdata);

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

	profile->mcam_kex_prfl.mkex_extr = &fw->mkex;
	if (profile->mcam_kex_prfl.mkex_extr->mkex_sign != MKEX_CN20K_SIGN) {
		dev_warn(rvu->dev, "Invalid MKEX profile signature:%llx\n",
			 profile->mcam_kex_prfl.mkex_extr->mkex_sign);
		return -EINVAL;
	}

	profile->custom = 1;
	profile->name = fw->name;
	profile->version = le64_to_cpu(fw->version);
	profile->lt_def = &fw->lt_def;

	for (kpu = 0; kpu < fw->kpus; kpu++) {
		fw_kpu = (struct npc_kpu_fwdata *)(fw->data + offset);
		if (fw_kpu->entries > KPU_CN20K_MAX_CST_ENT)
			dev_warn(rvu->dev,
				 "Too many custom entries on KPU%d: %d > %d\n",
				 kpu, fw_kpu->entries, KPU_CN20K_MAX_CST_ENT);
		entries = min(fw_kpu->entries, KPU_CN20K_MAX_CST_ENT);
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
	npc_cn20k_update_action_entries_n_flags(rvu, profile);

	return 0;
}

int npc_mcam_idx_2_key_type(struct rvu *rvu, u16 mcam_idx, u8 *key_type)
{
	struct npc_subbank *sb;
	int bank_off, sb_id;

	/* mcam_idx should be less than (2 * bank depth) */
	if (mcam_idx >= npc_priv.bank_depth * 2) {
		dev_err(rvu->dev, "%s: bad params\n",
			__func__);
		return -EINVAL;
	}

	/* find mcam offset per bank */
	bank_off = mcam_idx & (npc_priv.bank_depth - 1);

	/* Find subbank id */
	sb_id = bank_off / npc_priv.subbank_depth;

	/* Check if subbank id is more than maximum
	 * number of subbanks available
	 */
	if (sb_id >= npc_priv.num_subbanks) {
		dev_err(rvu->dev, "%s: invalid subbank %d\n",
			__func__, sb_id);
		return -EINVAL;
	}

	sb = &npc_priv.sb[sb_id];

	*key_type = sb->key_type;

	return 0;
}

static int npc_subbank_idx_2_mcam_idx(struct rvu *rvu, struct npc_subbank *sb,
				      u16 sub_off, u16 *mcam_idx)
{
	int off, bot;

	/* for x4 section, maximum allowed subbank index =
	 * subsection depth - 1
	 */
	if (sb->key_type == NPC_MCAM_KEY_X4 &&
	    sub_off >= npc_priv.subbank_depth) {
		dev_err(rvu->dev,
			"%s: Failed to get mcam idx (x4) sb->idx=%u sub_off=%u",
			__func__, sb->idx, sub_off);
		return -EINVAL;
	}

	/* for x2 section, maximum allowed subbank index =
	 * 2 * subsection depth - 1
	 */
	if (sb->key_type == NPC_MCAM_KEY_X2 &&
	    sub_off >= npc_priv.subbank_depth * 2) {
		dev_err(rvu->dev,
			"%s: Failed to get mcam idx (x2) sb->idx=%u sub_off=%u",
			__func__, sb->idx, sub_off);
		return -EINVAL;
	}

	/* Find subbank offset from respective subbank (w.r.t bank) */
	off = sub_off & (npc_priv.subbank_depth - 1);

	/* if subsection idx is in bank1, add bank depth,
	 * which is part of sb->b1b
	 */
	bot = sub_off >= npc_priv.subbank_depth ? sb->b1b : sb->b0b;

	*mcam_idx = bot + off;
	return 0;
}

static int npc_mcam_idx_2_subbank_idx(struct rvu *rvu, u16 mcam_idx,
				      struct npc_subbank **sb,
				      int *sb_off)
{
	int bank_off, sb_id;

	/* mcam_idx should be less than (2 * bank depth) */
	if (mcam_idx >= npc_priv.bank_depth * 2) {
		dev_err(rvu->dev, "%s: Invalid mcam idx %u\n",
			__func__, mcam_idx);
		return -EINVAL;
	}

	/* find mcam offset per bank */
	bank_off = mcam_idx & (npc_priv.bank_depth - 1);

	/* Find subbank id */
	sb_id = bank_off / npc_priv.subbank_depth;

	/* Check if subbank id is more than maximum
	 * number of subbanks available
	 */
	if (sb_id >= npc_priv.num_subbanks) {
		dev_err(rvu->dev, "%s: invalid subbank %d\n",
			__func__, sb_id);
		return -EINVAL;
	}

	*sb = &npc_priv.sb[sb_id];

	/* Subbank offset per bank */
	*sb_off = bank_off % npc_priv.subbank_depth;

	/* Index in a subbank should add subbank depth
	 * if it is in bank1
	 */
	if (mcam_idx >= npc_priv.bank_depth)
		*sb_off += npc_priv.subbank_depth;

	return 0;
}

static int __npc_subbank_contig_alloc(struct rvu *rvu,
				      struct npc_subbank *sb,
				      int key_type, int sidx,
				      int eidx, int prio,
				      int count, int t, int b,
				      unsigned long *bmap,
				      u16 *save)
{
	int k, offset, delta = 0;
	int cnt = 0, sbd;

	sbd = npc_priv.subbank_depth;

	if (sidx >= npc_priv.bank_depth)
		delta = sbd;

	switch (prio) {
	case NPC_MCAM_LOWER_PRIO:
	case NPC_MCAM_ANY_PRIO:
		/* Find an area of size 'count' from sidx to eidx */
		offset = bitmap_find_next_zero_area(bmap, sbd, sidx - b,
						    count, 0);

		if (offset >= sbd) {
			dev_err(rvu->dev,
				"%s: Could not find contiguous(%d) entries\n",
				__func__, count);
			return -EFAULT;
		}

		dev_dbg(rvu->dev,
			"%s: sidx=%d eidx=%d t=%d b=%d offset=%d count=%d delta=%d\n",
			__func__, sidx, eidx, t, b, offset,
			count, delta);

		for (cnt = 0; cnt < count; cnt++)
			save[cnt] = offset + cnt + delta;

		break;

	case NPC_MCAM_HIGHER_PRIO:
		/* Find an area of 'count' from eidx to sidx */
		for (k = eidx - b; cnt < count && k >= (sidx - b); k--) {
			/* If an intermediate slot is not free,
			 * reset the counter (cnt) to zero as
			 * request is for contiguous.
			 */
			if (test_bit(k, bmap)) {
				cnt = 0;
				continue;
			}

			save[cnt++] = k + delta;
		}
		break;
	}

	/* Found 'count' number of free slots */
	if (cnt == count)
		return 0;

	dev_dbg(rvu->dev,
		"%s: Could not find contiguous(%d) entries in subbank=%u\n",
		__func__, count, sb->idx);
	return -EFAULT;
}

static int __npc_subbank_non_contig_alloc(struct rvu *rvu,
					  struct npc_subbank *sb,
					  int key_type, int sidx,
					  int eidx, int prio,
					  int t, int b,
					  unsigned long *bmap,
					  int count, u16 *save,
					  bool max_alloc, int *alloc_cnt)
{
	unsigned long index;
	int cnt = 0, delta;
	int k, sbd;

	sbd = npc_priv.subbank_depth;
	delta = sidx >= npc_priv.bank_depth ? sbd : 0;

	switch (prio) {
		/* Find an area of size 'count' from sidx to eidx */
	case NPC_MCAM_LOWER_PRIO:
	case NPC_MCAM_ANY_PRIO:
		index = find_next_zero_bit(bmap, sbd, sidx - b);
		if (index >= sbd) {
			dev_err(rvu->dev,
				"%s: Error happened to alloc %u, bitmap_weight=%u, sb->idx=%u\n",
				__func__, count,
				bitmap_weight(bmap, sbd),
				sb->idx);
			break;
		}

		for (k = index; cnt < count && k <= (eidx - b); k++) {
			/* Skip used slots */
			if (test_bit(k, bmap))
				continue;

			save[cnt++] = k + delta;
		}
		break;

		/* Find an area of 'count' from eidx to sidx */
	case NPC_MCAM_HIGHER_PRIO:
		for (k = eidx - b; cnt < count && k >= (sidx - b); k--) {
			/* Skip used slots */
			if (test_bit(k, bmap))
				continue;

			save[cnt++] = k + delta;
		}
		break;
	}

	/* Update allocated 'cnt' to alloc_cnt */
	*alloc_cnt = cnt;

	/* Successfully allocated requested count slots */
	if (cnt == count)
		return 0;

	/* Allocation successful for cnt < count */
	if (max_alloc && cnt > 0)
		return 0;

	dev_dbg(rvu->dev,
		"%s: Could not find non contiguous entries(%u) in subbank(%u) cnt=%d max_alloc=%d\n",
		__func__, count, sb->idx, cnt, max_alloc);

	return -EFAULT;
}

static void __npc_subbank_sboff_2_off(struct rvu *rvu, struct npc_subbank *sb,
				      int sb_off, unsigned long **bmap,
				      int *off)
{
	int sbd;

	sbd = npc_priv.subbank_depth;

	*off = sb_off & (sbd - 1);
	*bmap = (sb_off >= sbd) ? sb->b1map : sb->b0map;
}

/* set/clear bitmap */
static bool __npc_subbank_mark_slot(struct rvu *rvu,
				    struct npc_subbank *sb,
				    int sb_off, bool set)
{
	unsigned long *bmap;
	int off;

	/* if sb_off >= subbank.depth, then slots are in
	 * bank1
	 */
	__npc_subbank_sboff_2_off(rvu, sb, sb_off, &bmap, &off);

	dev_dbg(rvu->dev,
		"%s: Marking set=%d sb_off=%d sb->idx=%d off=%d\n",
		__func__, set, sb_off, sb->idx, off);

	if (set) {
		/* Slot is already used */
		if (test_bit(off, bmap))
			return false;

		sb->free_cnt--;
		set_bit(off, bmap);
		return true;
	}

	/* Slot is already free */
	if (!test_bit(off, bmap))
		return false;

	sb->free_cnt++;
	clear_bit(off, bmap);
	return true;
}

static int __npc_subbank_mark_free(struct rvu *rvu, struct npc_subbank *sb)
{
	int rc, blkaddr;

	sb->flags = NPC_SUBBANK_FLAG_FREE;
	sb->key_type = 0;

	bitmap_clear(sb->b0map, 0, npc_priv.subbank_depth);
	bitmap_clear(sb->b1map, 0, npc_priv.subbank_depth);

	if (!xa_erase(&npc_priv.xa_sb_used, sb->arr_idx)) {
		dev_err(rvu->dev,
			"%s: Error to delete from xa_sb_used array\n",
			__func__);
		return -EFAULT;
	}

	rc = xa_insert(&npc_priv.xa_sb_free, sb->arr_idx,
		       xa_mk_value(sb->idx), GFP_KERNEL);
	if (rc) {
		rc = xa_insert(&npc_priv.xa_sb_used, sb->arr_idx,
			       xa_mk_value(sb->idx), GFP_KERNEL);
		if (rc)
			dev_err(rvu->dev,
				"%s: Failed to roll back sb(%u) arr_idx=%d\n",
				__func__, sb->idx, sb->arr_idx);

		dev_err(rvu->dev,
			"%s: Error to add sb(%u) to xa_sb_free array at arr_idx=%d\n",
			__func__, sb->idx, sb->arr_idx);
		return rc;
	}

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	rvu_write64(rvu, blkaddr,
		    NPC_AF_MCAM_SECTIONX_CFG_EXT(sb->idx),
		    NPC_MCAM_KEY_X2);

	return rc;
}

static int __npc_subbank_mark_used(struct rvu *rvu, struct npc_subbank *sb,
				   int key_type)
{
	int rc;

	sb->flags = NPC_SUBBANK_FLAG_USED;
	sb->key_type = key_type;
	if (key_type == NPC_MCAM_KEY_X4)
		sb->free_cnt = npc_priv.subbank_depth;
	else
		sb->free_cnt = 2 * npc_priv.subbank_depth;

	bitmap_clear(sb->b0map, 0, npc_priv.subbank_depth);
	bitmap_clear(sb->b1map, 0, npc_priv.subbank_depth);

	if (!xa_erase(&npc_priv.xa_sb_free, sb->arr_idx)) {
		dev_err(rvu->dev,
			"%s: Error to delete from xa_sb_free array\n",
			__func__);
		return -EFAULT;
	}

	rc = xa_insert(&npc_priv.xa_sb_used, sb->arr_idx,
		       xa_mk_value(sb->idx), GFP_KERNEL);
	if (rc)
		dev_err(rvu->dev,
			"%s: Error to add to xa_sb_used array\n", __func__);

	return rc;
}

static bool __npc_subbank_free(struct rvu *rvu, struct npc_subbank *sb,
			       u16 sb_off)
{
	bool deleted = false;
	unsigned long *bmap;
	int rc, off;

	deleted = __npc_subbank_mark_slot(rvu, sb, sb_off, false);
	if (!deleted)
		goto done;

	__npc_subbank_sboff_2_off(rvu, sb, sb_off, &bmap, &off);

	/* Check whether we can mark whole subbank as free */
	if (sb->key_type == NPC_MCAM_KEY_X4) {
		if (sb->free_cnt < npc_priv.subbank_depth)
			goto done;
	} else {
		if (sb->free_cnt < 2 * npc_priv.subbank_depth)
			goto done;
	}

	/* All slots in subbank are unused. Mark the subbank as free
	 * and add to free pool
	 */
	rc = __npc_subbank_mark_free(rvu, sb);
	if (rc)
		dev_err(rvu->dev, "%s: Error to free subbank\n", __func__);

done:
	return deleted;
}

static int
npc_subbank_free(struct rvu *rvu, struct npc_subbank *sb, u16 sb_off)
{
	bool deleted;

	mutex_lock(&sb->lock);
	deleted = __npc_subbank_free(rvu, sb, sb_off);
	mutex_unlock(&sb->lock);

	return deleted ? 0 : -EFAULT;
}

static int __npc_subbank_alloc(struct rvu *rvu, struct npc_subbank *sb,
			       int key_type, int ref, int limit, int prio,
			       bool contig, int count, u16 *mcam_idx,
			       int idx_sz, bool max_alloc, int *alloc_cnt)
{
	int cnt, t, b, i, blkaddr;
	bool new_sub_bank = false;
	unsigned long *bmap;
	u16 *save = NULL;
	int sidx, eidx;
	bool diffbank;
	int bw, bfree;
	int rc = 0;
	bool ret;

	/* Check if enough space is there to return requested number of
	 * mcam indexes in case of contiguous allocation
	 */
	if (!max_alloc && count > idx_sz) {
		dev_err(rvu->dev,
			"%s: Less space, count=%d idx_sz=%d sb_id=%d\n",
			__func__, count, idx_sz, sb->idx);
		return -ENOSPC;
	}

	/* Allocation on multiple subbank is not supported by this function.
	 * it means that ref and limit should be on same subbank.
	 *
	 * ref and limit values should be validated w.r.t prio as below.
	 * say ref = 100, limit = 200,
	 * if NPC_MCAM_LOWER_PRIO, allocate index 100
	 * if NPC_MCAM_HIGHER_PRIO, below sanity test returns error.
	 * if NPC_MCAM_ANY_PRIO, allocate index 100
	 *
	 * say ref = 200, limit = 100
	 * if NPC_MCAM_LOWER_PRIO, below sanity test returns error.
	 * if NPC_MCAM_HIGHER_PRIO, allocate index 200
	 * if NPC_MCAM_ANY_PRIO, allocate index 100
	 *
	 * Please note that NPC_MCAM_ANY_PRIO does not have any restriction
	 * on "ref" and "limit" values. ie, ref > limit and limit > ref
	 * are valid cases.
	 */
	if ((prio == NPC_MCAM_LOWER_PRIO && ref > limit) ||
	    (prio == NPC_MCAM_HIGHER_PRIO && ref < limit)) {
		dev_err(rvu->dev, "%s: Wrong ref_enty(%d) or limit(%d)\n",
			__func__, ref, limit);
		return -EINVAL;
	}

	/* x4 indexes are from 0 to bank size as it combines two x2 banks */
	if (key_type == NPC_MCAM_KEY_X4 &&
	    (ref >= npc_priv.bank_depth || limit >= npc_priv.bank_depth)) {
		dev_err(rvu->dev,
			"%s: Wrong ref_enty(%d) or limit(%d) for x4\n",
			__func__, ref, limit);
		return -EINVAL;
	}

	/* This function is called either bank0 or bank1 portion of a subbank.
	 * so ref and limit should be on same bank.
	 */
	diffbank = !!((ref & npc_priv.bank_depth) ^
		      (limit & npc_priv.bank_depth));
	if (diffbank) {
		dev_err(rvu->dev,
			"%s: request ref and limit should be from same bank\n",
			__func__);
		return -EINVAL;
	}

	sidx = min_t(int, limit, ref);
	eidx = max_t(int, limit, ref);

	/* Find total number of slots available; both used and free */
	cnt = eidx - sidx + 1;
	if (contig && cnt < count) {
		dev_err(rvu->dev,
			"%s: Wrong ref_enty(%d) or limit(%d) for count(%d)\n",
			__func__, ref, limit, count);
		return -EINVAL;
	}

	/* If subbank is free, check if requested number of indexes is less than
	 * or equal to mcam entries available in the subbank if contig.
	 */
	if (sb->flags & NPC_SUBBANK_FLAG_FREE) {
		if (contig && count > npc_priv.subbank_depth) {
			dev_err(rvu->dev, "%s: Less number of entries\n",
				__func__);
			return -ENOSPC;
		}

		new_sub_bank = true;
		goto process;
	}

	/* Flag should be set for all used subbanks */
	WARN_ONCE(!(sb->flags & NPC_SUBBANK_FLAG_USED),
		  "Used flag is not set(%#x)\n", sb->flags);

	/* If subbank key type does not match with requested key_type,
	 * return error
	 */
	if (sb->key_type != key_type) {
		dev_dbg(rvu->dev, "%s: subbank key_type mismatch\n", __func__);
		return -EINVAL;
	}

process:
	/* if ref or limit >= npc_priv.bank_depth, index are in bank1.
	 * else bank0.
	 */
	if (ref >= npc_priv.bank_depth) {
		bmap = sb->b1map;
		t = sb->b1t;
		b = sb->b1b;
	} else {
		bmap = sb->b0map;
		t = sb->b0t;
		b = sb->b0b;
	}

	/* Calculate free slots */
	bw = bitmap_weight(bmap, npc_priv.subbank_depth);
	bfree = npc_priv.subbank_depth - bw;

	if (!bfree) {
		dev_dbg(rvu->dev, "%s: subbank is full\n", __func__);
		return -ENOSPC;
	}

	/* If request is for contiguous , then max we can allocate is
	 * equal to subbank_depth
	 */
	if (contig && bfree < count) {
		dev_dbg(rvu->dev, "%s: no space for entry\n", __func__);
		return -ENOSPC;
	}

	/* 'save' array stores available indexes temporarily before
	 * marking it as allocated
	 */
	save = kcalloc(count, sizeof(u16), GFP_KERNEL);
	if (!save) {
		rc = -ENOMEM;
		goto err1;
	}

	if (contig) {
		rc =  __npc_subbank_contig_alloc(rvu, sb, key_type,
						 sidx, eidx, prio,
						 count, t, b,
						 bmap, save);
		/* contiguous allocation success means that
		 * requested number of free slots got
		 * allocated
		 */
		if (!rc)
			*alloc_cnt = count;

	} else {
		rc =  __npc_subbank_non_contig_alloc(rvu, sb, key_type,
						     sidx, eidx, prio,
						     t, b, bmap,
						     count, save,
						     max_alloc, alloc_cnt);
	}

	if (rc)
		goto err1;

	/* Mark new subbank bank as used */
	if (new_sub_bank) {
		blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
		if (blkaddr < 0) {
			dev_err(rvu->dev,
				"%s: NPC block not implemented\n", __func__);
			rc = -EFAULT;
			goto err1;
		}

		rc =  __npc_subbank_mark_used(rvu, sb, key_type);
		if (rc) {
			dev_err(rvu->dev,
				"%s: Error to mark subbank as used\n",
				__func__);
			goto err2;
		}

		/* Configure section type to key_type */
		rvu_write64(rvu, blkaddr,
			    NPC_AF_MCAM_SECTIONX_CFG_EXT(sb->idx),
			    key_type);
	}

	for (i = 0; i < *alloc_cnt; i++) {
		rc = npc_subbank_idx_2_mcam_idx(rvu, sb, save[i],
						&mcam_idx[i]);
		if (rc) {
			dev_err(rvu->dev,
				"%s: Error to find mcam idx for %u\n",
				__func__, save[i]);
			/* TODO: handle err case gracefully */
			goto err3;
		}

		/* Mark all slots as used */
		ret = __npc_subbank_mark_slot(rvu, sb, save[i], true);
		if (!ret) {
			dev_err(rvu->dev, "%s: Error to mark mcam_idx %u\n",
				__func__, mcam_idx[i]);
			rc = -EFAULT;
			goto err3;
		}
	}
	kfree(save);
	return 0;

err3:
	for (int j = 0; j < i; j++)
		__npc_subbank_mark_slot(rvu, sb, save[j], false);
err2:
	if (new_sub_bank)
		__npc_subbank_mark_free(rvu, sb);
err1:
	kfree(save);
	return rc;
}

static int
npc_subbank_alloc(struct rvu *rvu, struct npc_subbank *sb,
		  int key_type, int ref, int limit, int prio,
		  bool contig, int count, u16 *mcam_idx,
		  int idx_sz, bool max_alloc, int *alloc_cnt)
{
	int rc;

	mutex_lock(&sb->lock);
	rc = __npc_subbank_alloc(rvu, sb, key_type, ref, limit, prio,
				 contig, count, mcam_idx, idx_sz,
				 max_alloc, alloc_cnt);
	mutex_unlock(&sb->lock);

	return rc;
}

static int
npc_del_from_pf_maps(struct rvu *rvu, u16 mcam_idx)
{
	int pcifunc, idx;
	void *map;

	map = xa_erase(&npc_priv.xa_idx2pf_map, mcam_idx);
	if (!map) {
		dev_err(rvu->dev,
			"%s: failed to erase mcam_idx(%u) from xa_idx2pf map\n",
			__func__, mcam_idx);
		return -EFAULT;
	}

	pcifunc = xa_to_value(map);
	map = xa_load(&npc_priv.xa_pf_map, pcifunc);
	if (!map) {
		dev_err(rvu->dev,
			"%s: failed to find entry for (%u) from xa_pf_map, mcam=%u\n",
			__func__, pcifunc, mcam_idx);
		return -ESRCH;
	}

	idx = xa_to_value(map);

	map = xa_erase(&npc_priv.xa_pf2idx_map[idx], mcam_idx);
	if (!map) {
		dev_err(rvu->dev,
			"%s: failed to erase mcam_idx(%u) from xa_pf2idx_map map\n",
			__func__, mcam_idx);
		return -EFAULT;
	}
	return 0;
}

static int
npc_add_to_pf_maps(struct rvu *rvu, u16 mcam_idx, int pcifunc)
{
	int rc, idx;
	void *map;

	dev_dbg(rvu->dev,
		"%s: add2maps mcam_idx(%u) to xa_idx2pf map pcifunc=%#x\n",
		__func__, mcam_idx, pcifunc);

	rc = xa_insert(&npc_priv.xa_idx2pf_map, mcam_idx,
		       xa_mk_value(pcifunc), GFP_KERNEL);

	if (rc) {
		map = xa_load(&npc_priv.xa_idx2pf_map, mcam_idx);
		dev_err(rvu->dev,
			"%s: failed to insert mcam_idx(%u) to xa_idx2pf map, existing value=%lu\n",
			__func__, mcam_idx, xa_to_value(map));
		return -EFAULT;
	}

	map = xa_load(&npc_priv.xa_pf_map, pcifunc);
	if (!map) {
		dev_err(rvu->dev,
			"%s: failed to find pf map entry for pcifunc=%#x, mcam=%u\n",
			__func__, pcifunc, mcam_idx);
		return -ESRCH;
	}

	idx = xa_to_value(map);

	rc = xa_insert(&npc_priv.xa_pf2idx_map[idx], mcam_idx,
		       xa_mk_value(pcifunc), GFP_KERNEL);

	if (rc) {
		map = xa_load(&npc_priv.xa_pf2idx_map[idx], mcam_idx);
		xa_erase(&npc_priv.xa_idx2pf_map, mcam_idx);
		dev_err(rvu->dev,
			"%s: failed to insert mcam_idx(%u) to xa_pf2idx_map map, earlier value=%lu idx=%u\n",
			__func__, mcam_idx, xa_to_value(map), idx);

		return -EFAULT;
	}

	return 0;
}

static bool
npc_subbank_suits(struct npc_subbank *sb, int key_type)
{
	mutex_lock(&sb->lock);

	if (!sb->key_type) {
		mutex_unlock(&sb->lock);
		return true;
	}

	if (sb->key_type == key_type) {
		mutex_unlock(&sb->lock);
		return true;
	}

	mutex_unlock(&sb->lock);
	return false;
}

#define SB_ALIGN_UP(val)   (((val) + npc_priv.subbank_depth) & \
			    ~((npc_priv.subbank_depth) - 1))
#define SB_ALIGN_DOWN(val) ALIGN_DOWN((val), npc_priv.subbank_depth)

static void npc_subbank_iter_down(struct rvu *rvu,
				  int ref, int limit,
				  int *cur_ref, int *cur_limit,
				  bool *start, bool *stop)
{
	int align;

	*stop = false;

	/* ALIGN_DOWN the limit to current subbank boundary bottom index */
	if (*start) {
		*start = false;
		*cur_ref = ref;
		align = SB_ALIGN_DOWN(ref);
		if (align < limit) {
			*stop = true;
			*cur_limit = limit;
			return;
		}
		*cur_limit = align;
		return;
	}

	*cur_ref = *cur_limit - 1;
	align = *cur_ref - npc_priv.subbank_depth + 1;
	if (align <= limit) {
		*stop = true;
		*cur_limit = limit;
		return;
	}

	*cur_limit = align;
}

static void npc_subbank_iter_up(struct rvu *rvu,
				int ref, int limit,
				int *cur_ref, int *cur_limit,
				bool *start, bool *stop)
{
	int align;

	*stop = false;

	/* ALIGN_UP the limit to current subbank boundary top index */
	if (*start) {
		*start = false;
		*cur_ref = ref;

		/* Find next lower prio subbank's bottom index */
		align = SB_ALIGN_UP(ref);

		/* Crosses limit ? */
		if (align - 1 > limit) {
			*stop = true;
			*cur_limit = limit;
			return;
		}

		/* Current subbank's top index */
		*cur_limit = align - 1;
		return;
	}

	*cur_ref = *cur_limit + 1;
	align = *cur_ref + npc_priv.subbank_depth - 1;

	if (align >= limit) {
		*stop = true;
		*cur_limit = limit;
		return;
	}

	*cur_limit = align;
}

static int
npc_subbank_iter(struct rvu *rvu, int key_type,
		 int ref, int limit, int prio,
		 int *cur_ref, int *cur_limit,
		 bool *start, bool *stop)
{
	if (prio != NPC_MCAM_HIGHER_PRIO)
		npc_subbank_iter_up(rvu, ref, limit,
				    cur_ref, cur_limit,
				    start, stop);
	else
		npc_subbank_iter_down(rvu, ref, limit,
				      cur_ref, cur_limit,
				      start, stop);

	/* limit and ref should < bank_depth for x4 */
	if (key_type == NPC_MCAM_KEY_X4) {
		if (*cur_ref >= npc_priv.bank_depth)
			return -EINVAL;

		if (*cur_limit >= npc_priv.bank_depth)
			return -EINVAL;
	}
	/* limit and ref should < 2 * bank_depth, for x2 */
	if (*cur_ref >= 2 * npc_priv.bank_depth)
		return -EINVAL;

	if (*cur_limit >= 2 * npc_priv.bank_depth)
		return -EINVAL;

	return 0;
}

static int npc_idx_free(struct rvu *rvu, u16 *mcam_idx, int count,
			bool maps_del)
{
	struct npc_subbank *sb;
	u16 vidx, midx;
	int sb_off, i;
	bool ret;
	int rc;

	/* Check if we can dealloc indexes properly ? */
	for (i = 0; i < count; i++) {
		rc =  npc_mcam_idx_2_subbank_idx(rvu, npc_vidx2idx(mcam_idx[i]),
						 &sb, &sb_off);
		if (rc) {
			dev_err(rvu->dev,
				"Failed to free mcam idx=%u\n", mcam_idx[i]);
			return rc;
		}
	}

	for (i = 0; i < count; i++) {
		if (npc_is_vidx(mcam_idx[i])) {
			vidx = mcam_idx[i];
			midx = npc_vidx2idx(vidx);
		} else {
			midx = mcam_idx[i];
			vidx = npc_idx2vidx(midx);
		}

		if (midx >= npc_priv.bank_depth * npc_priv.num_banks) {
			dev_err(rvu->dev,
				"%s: Invalid mcam_idx=%u cannot be deleted\n",
				__func__, mcam_idx[i]);
			return -EINVAL;
		}

		rc =  npc_mcam_idx_2_subbank_idx(rvu, midx,
						 &sb, &sb_off);
		if (rc) {
			dev_err(rvu->dev,
				"%s: Failed to find subbank info for vidx=%u\n",
				__func__, vidx);
			return rc;
		}

		ret = npc_subbank_free(rvu, sb, sb_off);
		if (ret) {
			dev_err(rvu->dev,
				"%s: Failed to find subbank info for vidx=%u\n",
				__func__, vidx);
			return -EINVAL;
		}

		if (!maps_del)
			continue;

		rc = npc_del_from_pf_maps(rvu, midx);
		if (rc)
			return rc;

		/* If there is no vidx mapping; continue */
		if (vidx == midx)
			continue;

		rc = npc_vidx_maps_del_entry(rvu, vidx, NULL);
		if (rc)
			return rc;
	}

	return 0;
}

static int npc_multi_subbank_ref_alloc(struct rvu *rvu, int key_type,
				       int ref, int limit, int prio,
				       bool contig, int count,
				       u16 *mcam_idx)
{
	struct npc_subbank *sb;
	unsigned long *bmap;
	int sb_off, off, rc;
	int cnt = 0;
	bool bitset;

	if (prio != NPC_MCAM_HIGHER_PRIO) {
		while (ref <= limit) {
			/* Calculate subbank and subbank index */
			rc =  npc_mcam_idx_2_subbank_idx(rvu, ref,
							 &sb, &sb_off);
			if (rc)
				goto err;

			/* If subbank is not suitable for requested key type
			 * restart search from next subbank
			 */
			if (!npc_subbank_suits(sb, key_type)) {
				ref = SB_ALIGN_UP(ref);
				if (contig) {
					rc = npc_idx_free(rvu, mcam_idx,
							  cnt, false);
					if (rc)
						return rc;
					cnt = 0;
				}
				continue;
			}

			mutex_lock(&sb->lock);

			/* If subbank is free; mark it as used */
			if (sb->flags & NPC_SUBBANK_FLAG_FREE) {
				rc =  __npc_subbank_mark_used(rvu, sb,
							      key_type);
				if (rc) {
					mutex_unlock(&sb->lock);
					dev_err(rvu->dev,
						"%s:Error to add to use array\n",
						__func__);
					goto err;
				}
			}

			/* Find correct bmap */
			__npc_subbank_sboff_2_off(rvu, sb, sb_off, &bmap, &off);

			/* if bit is already set, reset 'cnt' */
			bitset = test_bit(off, bmap);
			if (bitset) {
				mutex_unlock(&sb->lock);
				if (contig) {
					rc = npc_idx_free(rvu, mcam_idx,
							  cnt, false);
					if (rc)
						return rc;
					cnt = 0;
				}

				ref++;
				continue;
			}

			set_bit(off, bmap);
			sb->free_cnt--;
			mcam_idx[cnt++] = ref;
			mutex_unlock(&sb->lock);

			if (cnt == count)
				return 0;
			ref++;
		}

		/* Could not allocate request count slots */
		goto err;
	}
	while (ref >= limit) {
		rc =  npc_mcam_idx_2_subbank_idx(rvu, ref,
						 &sb, &sb_off);
		if (rc)
			goto err;

		if (!npc_subbank_suits(sb, key_type)) {
			ref = SB_ALIGN_DOWN(ref) - 1;
			if (contig) {
				rc = npc_idx_free(rvu, mcam_idx, cnt, false);
				if (rc)
					return rc;

				cnt = 0;
			}
			continue;
		}

		mutex_lock(&sb->lock);

		if (sb->flags & NPC_SUBBANK_FLAG_FREE) {
			rc =  __npc_subbank_mark_used(rvu, sb, key_type);
			if (rc) {
				mutex_unlock(&sb->lock);
				dev_err(rvu->dev,
					"%s:Error to add to use array\n",
					__func__);
				goto err;
			}
		}

		__npc_subbank_sboff_2_off(rvu, sb, sb_off, &bmap, &off);
		bitset = test_bit(off, bmap);
		if (bitset) {
			mutex_unlock(&sb->lock);
			if (contig) {
				rc = npc_idx_free(rvu, mcam_idx, cnt, false);
				if (rc)
					return rc;

				cnt = 0;
			}
			ref--;
			continue;
		}

		mcam_idx[cnt++] = ref;
		sb->free_cnt--;
		set_bit(off, bmap);
		mutex_unlock(&sb->lock);

		if (cnt == count)
			return 0;
		ref--;
	}

err:
	rc = npc_idx_free(rvu, mcam_idx, cnt, false);
	if (rc)
		dev_err(rvu->dev,
			"%s: Error happened while freeing cnt=%u indexes\n",
			__func__, cnt);

	return -ENOSPC;
}

static int npc_subbank_free_cnt(struct rvu *rvu, struct npc_subbank *sb,
				int key_type)
{
	int cnt, spd;

	spd = npc_priv.subbank_depth;
	mutex_lock(&sb->lock);

	if (sb->flags & NPC_SUBBANK_FLAG_FREE)
		cnt = key_type == NPC_MCAM_KEY_X4 ? spd : 2 * spd;
	else
		cnt = sb->free_cnt;

	mutex_unlock(&sb->lock);
	return cnt;
}

static int npc_subbank_ref_alloc(struct rvu *rvu, int key_type,
				 int ref, int limit, int prio,
				 bool contig, int count,
				 u16 *mcam_idx)
{
	struct npc_subbank *sb1, *sb2;
	bool max_alloc, start, stop;
	int r, l, sb_idx1, sb_idx2;
	int tot = 0, rc;
	int alloc_cnt;

	max_alloc = !contig;

	start = true;
	stop = false;

	/* Loop until we cross the ref/limit boundary */
	while (!stop) {
		rc = npc_subbank_iter(rvu, key_type, ref, limit, prio,
				      &r, &l, &start, &stop);

		dev_dbg(rvu->dev,
			"%s: ref=%d limit=%d r=%d l=%d start=%d stop=%d tot=%d count=%d rc=%d\n",
			__func__, ref, limit, r, l,
			start, stop, tot, count, rc);

		if (rc)
			goto err;

		/* Find subbank and subbank index for ref */
		rc = npc_mcam_idx_2_subbank_idx(rvu, r, &sb1,
						&sb_idx1);
		if (rc)
			goto err;

		dev_dbg(rvu->dev,
			"%s: ref subbank=%d off=%d\n",
			__func__, sb1->idx, sb_idx1);

		/* Skip subbank if it is not available for the keytype */
		if (!npc_subbank_suits(sb1, key_type)) {
			dev_dbg(rvu->dev,
				"%s: not suitable sb=%d key_type=%d\n",
				__func__, sb1->idx, key_type);
			continue;
		}

		/* Find subbank and subbank index for limit */
		rc = npc_mcam_idx_2_subbank_idx(rvu, l, &sb2,
						&sb_idx2);
		if (rc)
			goto err;

		dev_dbg(rvu->dev,
			"%s: limit subbank=%d off=%d\n",
			__func__, sb_idx1, sb_idx2);

		/* subbank of ref and limit should be same */
		if (sb1 != sb2) {
			dev_err(rvu->dev,
				"%s: l(%d) and r(%d) are not in same subbank\n",
				__func__, r, l);
			goto err;
		}

		if (contig &&
		    npc_subbank_free_cnt(rvu, sb1, key_type) < count) {
			dev_dbg(rvu->dev, "%s: less count =%d\n",
				__func__,
				npc_subbank_free_cnt(rvu, sb1, key_type));
			continue;
		}

		/* Try in one bank of a subbank */
		alloc_cnt = 0;
		rc =  npc_subbank_alloc(rvu, sb1, key_type,
					r, l, prio, contig,
					count - tot, mcam_idx + tot,
					count - tot, max_alloc,
					&alloc_cnt);

		tot += alloc_cnt;

		dev_dbg(rvu->dev, "%s: Allocated tot=%d alloc_cnt=%d\n",
			__func__, tot, alloc_cnt);

		if (!rc && count == tot)
			return 0;
	}
err:
	dev_dbg(rvu->dev, "%s: Error to allocate\n",
		__func__);

	/* non contiguous allocation fails. We need to do clean up */
	if (max_alloc) {
		rc = npc_idx_free(rvu, mcam_idx, tot, false);
		if (rc)
			dev_err(rvu->dev,
				"%s: failed to free %u indexes\n",
				__func__, tot);
	}

	return -EFAULT;
}

/* Minimize allocation from bottom and top subbanks for noref allocations.
 * Default allocations are ref based, and will be allocated from top
 * subbanks (least priority subbanks). Since default allocation is at very
 * early stage of kernel netdev probes, this subbanks will be moved to
 * used subbanks list. This will pave a way for noref allocation from these
 * used subbanks. Skip allocation for these top and bottom, and try free
 * bank next. If none slot is available, come back and search in these
 * subbanks.
 */

static int npc_subbank_restricted_idxs[2];
static bool restrict_valid = true;

static bool npc_subbank_restrict_usage(struct rvu *rvu, int index)
{
	int i;

	if (!restrict_valid)
		return false;

	for (i = 0; i < ARRAY_SIZE(npc_subbank_restricted_idxs); i++) {
		if (index == npc_subbank_restricted_idxs[i])
			return true;
	}

	return false;
}

static int npc_subbank_noref_alloc(struct rvu *rvu, int key_type, bool contig,
				   int count, u16 *mcam_idx)
{
	struct npc_subbank *sb;
	unsigned long index;
	int tot = 0, rc;
	bool max_alloc;
	int alloc_cnt;
	int idx, i;
	void *val;

	max_alloc = !contig;

	/* Check used subbanks for free slots */
	xa_for_each(&npc_priv.xa_sb_used, index, val) {
		idx = xa_to_value(val);

		/* Minimize allocation from restricted subbanks
		 * in noref allocations.
		 */
		if (npc_subbank_restrict_usage(rvu, idx))
			continue;

		sb = &npc_priv.sb[idx];

		/* Skip if not suitable subbank */
		if (!npc_subbank_suits(sb, key_type))
			continue;

		if (contig && npc_subbank_free_cnt(rvu, sb, key_type) < count)
			continue;

		/* try in bank 0. Try passing ref and limit equal to
		 * subbank boundaries
		 */
		alloc_cnt = 0;
		rc =  npc_subbank_alloc(rvu, sb, key_type,
					sb->b0b, sb->b0t, 0,
					contig, count - tot,
					mcam_idx + tot,
					count - tot,
					max_alloc, &alloc_cnt);

		/* Non contiguous allocation may allocate less than
		 * requested 'count'.
		 */
		tot += alloc_cnt;

		dev_dbg(rvu->dev,
			"%s: Allocated %d from subbank %d, tot=%d count=%d\n",
			__func__, alloc_cnt, sb->idx, tot, count);

		/* Successfully allocated */
		if (!rc && count == tot)
			return 0;

		/* x4 entries can be allocated from bank 0 only */
		if (key_type == NPC_MCAM_KEY_X4)
			continue;

		/* try in bank 1 for x2 */
		alloc_cnt = 0;
		rc =  npc_subbank_alloc(rvu, sb, key_type,
					sb->b1b, sb->b1t, 0,
					contig, count - tot,
					mcam_idx + tot,
					count - tot, max_alloc,
					&alloc_cnt);

		tot += alloc_cnt;

		dev_dbg(rvu->dev,
			"%s: Allocated %d from subbank %d, tot=%d count=%d\n",
			__func__, alloc_cnt, sb->idx, tot, count);

		if (!rc && count == tot)
			return 0;
	}

	/* Allocate in free subbanks */
	xa_for_each(&npc_priv.xa_sb_free, index, val) {
		idx = xa_to_value(val);
		sb = &npc_priv.sb[idx];

		/* Minimize allocation from restricted subbanks
		 * in noref allocations.
		 */
		if (npc_subbank_restrict_usage(rvu, idx))
			continue;

		if (!npc_subbank_suits(sb, key_type))
			continue;

		/* try in bank 0 */
		alloc_cnt = 0;
		rc =  npc_subbank_alloc(rvu, sb, key_type,
					sb->b0b, sb->b0t, 0,
					contig, count - tot,
					mcam_idx + tot,
					count - tot,
					max_alloc, &alloc_cnt);

		tot += alloc_cnt;

		dev_dbg(rvu->dev,
			"%s: Allocated %d from subbank %d, tot=%d count=%d\n",
			__func__, alloc_cnt, sb->idx, tot, count);

		/* Successfully allocated */
		if (!rc && count == tot)
			return 0;

		/* x4 entries can be allocated from bank 0 only */
		if (key_type == NPC_MCAM_KEY_X4)
			continue;

		/* try in bank 1 for x2 */
		alloc_cnt = 0;
		rc =  npc_subbank_alloc(rvu, sb,
					key_type, sb->b1b, sb->b1t, 0,
					contig, count - tot,
					mcam_idx + tot, count - tot,
					max_alloc, &alloc_cnt);

		tot += alloc_cnt;

		dev_dbg(rvu->dev,
			"%s: Allocated %d from subbank %d, tot=%d count=%d\n",
			__func__, alloc_cnt, sb->idx, tot, count);

		if (!rc && count == tot)
			return 0;
	}

	/* Allocate from restricted subbanks */
	for (i = 0; restrict_valid &&
	     (i < ARRAY_SIZE(npc_subbank_restricted_idxs)); i++) {
		idx = npc_subbank_restricted_idxs[i];
		sb = &npc_priv.sb[idx];

		/* Skip if not suitable subbank */
		if (!npc_subbank_suits(sb, key_type))
			continue;

		if (contig && npc_subbank_free_cnt(rvu, sb, key_type) < count)
			continue;

		/* try in bank 0. Try passing ref and limit equal to
		 * subbank boundaries
		 */
		alloc_cnt = 0;
		rc =  npc_subbank_alloc(rvu, sb, key_type,
					sb->b0b, sb->b0t, 0,
					contig, count - tot,
					mcam_idx + tot,
					count - tot,
					max_alloc, &alloc_cnt);

		/* Non contiguous allocation may allocate less than
		 * requested 'count'.
		 */
		tot += alloc_cnt;

		dev_dbg(rvu->dev,
			"%s: Allocated %d from subbank %d, tot=%d count=%d\n",
			__func__, alloc_cnt, sb->idx, tot, count);

		/* Successfully allocated */
		if (!rc && count == tot)
			return 0;

		/* x4 entries can be allocated from bank 0 only */
		if (key_type == NPC_MCAM_KEY_X4)
			continue;

		/* try in bank 1 for x2 */
		alloc_cnt = 0;
		rc =  npc_subbank_alloc(rvu, sb, key_type,
					sb->b1b, sb->b1t, 0,
					contig, count - tot,
					mcam_idx + tot,
					count - tot, max_alloc,
					&alloc_cnt);

		tot += alloc_cnt;

		dev_dbg(rvu->dev,
			"%s: Allocated %d from subbank %d, tot=%d count=%d\n",
			__func__, alloc_cnt, sb->idx, tot, count);

		if (!rc && count == tot)
			return 0;
	}

	/* non contiguous allocation fails. We need to do clean up */
	if (max_alloc)
		npc_idx_free(rvu, mcam_idx, tot, false);

	dev_dbg(rvu->dev, "%s: non-contig allocation fails\n",
		__func__);

	return -EFAULT;
}

int npc_cn20k_idx_free(struct rvu *rvu, u16 *mcam_idx, int count)
{
	return npc_idx_free(rvu, mcam_idx, count, true);
}

int npc_cn20k_ref_idx_alloc(struct rvu *rvu, int pcifunc, int key_type,
			    int prio, u16 *mcam_idx, int ref, int limit,
			    bool contig, int count, bool virt)
{
	bool defrag_candidate = false;
	int i, eidx, rc, bd;
	bool ref_valid;
	u16 vidx;

	bd = npc_priv.bank_depth;

	/* Special case: ref == 0 && limit= 0 && prio == HIGH && count == 1
	 * Here user wants to allocate 0th entry
	 */
	if (!ref && !limit && prio == NPC_MCAM_HIGHER_PRIO &&
	    count == 1) {
		rc = npc_subbank_ref_alloc(rvu, key_type, ref, limit,
					   prio, contig, count, mcam_idx);

		if (rc)
			return rc;
		goto add2map;
	}

	ref_valid = !!(limit || ref);
	defrag_candidate = !ref_valid && !contig && virt;
	if (!ref_valid) {
		if (contig && count > npc_priv.subbank_depth)
			goto try_noref_multi_subbank;

		rc = npc_subbank_noref_alloc(rvu, key_type, contig,
					     count, mcam_idx);
		if (!rc)
			goto add2map;

try_noref_multi_subbank:
		eidx = (key_type == NPC_MCAM_KEY_X4) ? bd - 1 : 2 * bd - 1;

		if (prio == NPC_MCAM_HIGHER_PRIO)
			rc = npc_multi_subbank_ref_alloc(rvu, key_type,
							 eidx, 0,
							 NPC_MCAM_HIGHER_PRIO,
							 contig, count,
							 mcam_idx);
		else
			rc = npc_multi_subbank_ref_alloc(rvu, key_type,
							 0, eidx,
							 NPC_MCAM_LOWER_PRIO,
							 contig, count,
							 mcam_idx);

		if (!rc)
			goto add2map;

		return rc;
	}

	if ((prio == NPC_MCAM_LOWER_PRIO && ref > limit) ||
	    (prio == NPC_MCAM_HIGHER_PRIO && ref < limit)) {
		dev_err(rvu->dev, "%s: Wrong ref_enty(%d) or limit(%d)\n",
			__func__, ref, limit);
		return -EINVAL;
	}

	if ((key_type == NPC_MCAM_KEY_X4 && (ref >= bd || limit >= bd)) ||
	    (key_type == NPC_MCAM_KEY_X2 &&
	     (ref >= 2 * bd || limit >= 2 * bd))) {
		dev_err(rvu->dev, "%s: Wrong ref_enty(%d) or limit(%d)\n",
			__func__, ref, limit);
		return -EINVAL;
	}

	if (contig && count > npc_priv.subbank_depth)
		goto try_ref_multi_subbank;

	rc = npc_subbank_ref_alloc(rvu, key_type, ref, limit,
				   prio, contig, count, mcam_idx);
	if (!rc)
		goto add2map;

try_ref_multi_subbank:
	rc = npc_multi_subbank_ref_alloc(rvu, key_type,
					 ref, limit, prio,
					 contig, count, mcam_idx);
	if (!rc)
		goto add2map;

	return rc;

add2map:
	for (i = 0; i < count; i++) {
		rc = npc_add_to_pf_maps(rvu, mcam_idx[i], pcifunc);
		if (rc)
			goto err;

		if (!defrag_candidate)
			continue;

		rc = npc_vidx_maps_add_entry(rvu, mcam_idx[i], pcifunc, &vidx);
		if (rc) {
			npc_del_from_pf_maps(rvu, mcam_idx[i]);
			goto err;
		}

		/* Return vidx to caller */
		mcam_idx[i] = vidx;
	}

	return 0;
err:
	for (int j = 0; j < i; j++) {
		npc_del_from_pf_maps(rvu, npc_vidx2idx(mcam_idx[j]));

		if (!defrag_candidate)
			continue;

		npc_vidx_maps_del_entry(rvu, mcam_idx[j], NULL);
	}

	return rc;

}

void npc_cn20k_subbank_calc_free(struct rvu *rvu, int *x2_free,
				 int *x4_free, int *sb_free)
{
	struct npc_subbank *sb;
	int i;

	/* Reset all stats to zero */
	*x2_free = 0;
	*x4_free = 0;
	*sb_free = 0;

	for (i = 0; i < npc_priv.num_subbanks; i++) {
		sb = &npc_priv.sb[i];
		mutex_lock(&sb->lock);

		/* Count number of free subbanks */
		if (sb->flags & NPC_SUBBANK_FLAG_FREE) {
			(*sb_free)++;
			goto next;
		}

		/* Sumup x4 free count */
		if (sb->key_type == NPC_MCAM_KEY_X4) {
			(*x4_free) += sb->free_cnt;
			goto next;
		}

		/* Sumup x2 free counts */
		(*x2_free) += sb->free_cnt;
next:
		mutex_unlock(&sb->lock);
	}
}

int
rvu_mbox_handler_npc_cn20k_get_fcnt(struct rvu *rvu,
				    struct msg_req *req,
				    struct npc_cn20k_get_fcnt_rsp *rsp)
{
	npc_cn20k_subbank_calc_free(rvu, &rsp->free_x2,
				    &rsp->free_x4, &rsp->free_subbanks);
	return 0;
}

int
rvu_mbox_handler_npc_cn20k_get_kex_cfg(struct rvu *rvu,
				       struct msg_req *req,
				       struct npc_cn20k_get_kex_cfg_rsp *rsp)
{
	int extr, lt;

	rsp->rx_keyx_cfg = CN20K_GET_KEX_CFG(NIX_INTF_RX);
	rsp->tx_keyx_cfg = CN20K_GET_KEX_CFG(NIX_INTF_TX);

	/* Get EXTRACTOR LID */
	for (extr = 0; extr < NPC_MAX_EXTRACTOR; extr++) {
		rsp->intf_extr_lid[NIX_INTF_RX][extr] =
			CN20K_GET_EXTR_LID(NIX_INTF_RX, extr);
		rsp->intf_extr_lid[NIX_INTF_TX][extr] =
			CN20K_GET_EXTR_LID(NIX_INTF_TX, extr);
	}

	/* Get EXTRACTOR LTYPE */
	for (extr = 0; extr < NPC_MAX_EXTRACTOR; extr++) {
		for (lt = 0; lt < NPC_MAX_LT; lt++) {
			rsp->intf_extr_lt[NIX_INTF_RX][extr][lt] =
				CN20K_GET_EXTR_LT(NIX_INTF_RX, extr, lt);
			rsp->intf_extr_lt[NIX_INTF_TX][extr][lt] =
				CN20K_GET_EXTR_LT(NIX_INTF_TX, extr, lt);
		}
	}

	memcpy(rsp->mkex_pfl_name, rvu->mkex_pfl_name, MKEX_NAME_LEN);
	return 0;
}

static int *subbank_srch_order;

static void npc_populate_restricted_idxs(int num_subbanks)
{
	npc_subbank_restricted_idxs[0] = num_subbanks - 1;
	npc_subbank_restricted_idxs[1] = 0;
}

static int npc_create_srch_order(int cnt)
{
	int val = 0;

	subbank_srch_order = kcalloc(cnt, sizeof(int),
				     GFP_KERNEL);
	if (!subbank_srch_order)
		return -ENOMEM;

	/* cnt(subbank depth) is always a power of 2. There is a check in
	 * npc_priv_init() to check the same.
	 */
	for (int i = 0; i < cnt; i += 2) {
		subbank_srch_order[i] = cnt / 2 - val - 1;
		subbank_srch_order[i + 1] = cnt / 2 + 1 + val;
		val++;
	}

	subbank_srch_order[cnt - 1] = cnt / 2;
	return 0;
}

static void npc_subbank_init(struct rvu *rvu, struct npc_subbank *sb, int idx)
{
	mutex_init(&sb->lock);

	sb->b0b = idx * npc_priv.subbank_depth;
	sb->b0t = sb->b0b + npc_priv.subbank_depth - 1;

	sb->b1b = npc_priv.bank_depth + idx * npc_priv.subbank_depth;
	sb->b1t = sb->b1b + npc_priv.subbank_depth - 1;

	sb->flags = NPC_SUBBANK_FLAG_FREE;
	sb->idx = idx;
	sb->arr_idx = subbank_srch_order[idx];

	dev_dbg(rvu->dev, "%s: sb->idx=%u sb->arr_idx=%u\n",
		__func__, sb->idx, sb->arr_idx);

	/* Keep first and last subbank at end of free array; so that
	 * it will be used at last
	 */
	xa_store(&npc_priv.xa_sb_free, sb->arr_idx,
		 xa_mk_value(sb->idx), GFP_KERNEL);
}

static int npc_pcifunc_map_create(struct rvu *rvu)
{
	int pf, vf, numvfs;
	int cnt = 0;
	u16 pcifunc;
	u64 cfg;

	for (pf = 0; pf < rvu->hw->total_pfs; pf++) {
		cfg = rvu_read64(rvu, BLKADDR_RVUM, RVU_PRIV_PFX_CFG(pf));
		numvfs = (cfg >> 12) & 0xFF;

		/* Skip not enabled PFs */
		if (!(cfg & BIT_ULL(20)))
			goto chk_vfs;

		/* If Admin function, check on VFs */
		if (cfg & BIT_ULL(21))
			goto chk_vfs;

		pcifunc = pf << 9;

		xa_store(&npc_priv.xa_pf_map, (unsigned long)pcifunc,
			 xa_mk_value(cnt), GFP_KERNEL);

		cnt++;

chk_vfs:
		for (vf = 0; vf < numvfs; vf++) {
			pcifunc = (pf << 9) | (vf + 1);

			xa_store(&npc_priv.xa_pf_map, (unsigned long)pcifunc,
				 xa_mk_value(cnt), GFP_KERNEL);
			cnt++;
		}
	}

	return cnt;
}

struct npc_defrag_node {
	u8 idx;
	u8 key_type;
	bool valid;
	bool refs;
	u16 free_cnt;
	u16 vidx_cnt;
	u16 *vidx;
	struct list_head list;
};

static bool npc_defrag_skip_restricted_sb(int sb_id)
{
	int i;

	if (!restrict_valid)
		return false;

	for (i = 0; i < ARRAY_SIZE(npc_subbank_restricted_idxs); i++)
		if (sb_id == npc_subbank_restricted_idxs[i])
			return true;
	return false;
}

/* Find subbank with minimum number of virtual indexes */
static struct npc_defrag_node *npc_subbank_min_vidx(struct list_head *lh)
{
	struct npc_defrag_node *node, *tnode = NULL;
	int min = INT_MAX;

	list_for_each_entry(node, lh, list) {
		if (!node->valid)
			continue;

		/* if subbank has ref allocated mcam indexes, that subbank
		 * is not a good candidate to move out indexes.
		 */
		if (node->refs)
			continue;

		if (min > node->vidx_cnt) {
			min = node->vidx_cnt;
			tnode = node;
		}
	}

	return tnode;
}

/* Find subbank with maximum number of free spaces */
static struct npc_defrag_node *npc_subbank_max_free(struct list_head *lh)
{
	struct npc_defrag_node *node, *tnode = NULL;
	int max = INT_MIN;

	list_for_each_entry(node, lh, list) {
		if (!node->valid)
			continue;

		if (max < node->free_cnt) {
			max = node->free_cnt;
			tnode = node;
		}
	}

	return tnode;
}

static int npc_defrag_alloc_free_slots(struct rvu *rvu,
				       struct npc_defrag_node *f,
				       int cnt, u16 *save)
{
	int alloc_cnt1, alloc_cnt2;
	struct npc_subbank *sb;
	int rc, sb_off, i;
	bool deleted;

	sb = &npc_priv.sb[f->idx];

	alloc_cnt1 = 0;
	alloc_cnt2 = 0;

	rc = __npc_subbank_alloc(rvu, sb,
				 NPC_MCAM_KEY_X2, sb->b0b,
				 sb->b0t,
				 NPC_MCAM_LOWER_PRIO,
				 false, cnt, save, cnt, true,
				 &alloc_cnt1);
	if (alloc_cnt1 < cnt) {
		rc = __npc_subbank_alloc(rvu, sb,
					 NPC_MCAM_KEY_X2, sb->b1b,
					 sb->b1t,
					 NPC_MCAM_LOWER_PRIO,
					 false, cnt - alloc_cnt1,
					 save + alloc_cnt1,
					 cnt - alloc_cnt1,
					 true, &alloc_cnt2);
	}

	if (alloc_cnt1 + alloc_cnt2 != cnt) {
		dev_err(rvu->dev,
			"%s: Failed to alloc cnt=%u alloc_cnt1=%u alloc_cnt2=%u\n",
			__func__, cnt, alloc_cnt1, alloc_cnt2);
		goto fail_free_alloc;
	}
	return 0;

fail_free_alloc:
	for (i = 0; i < alloc_cnt1 + alloc_cnt2; i++) {
		rc =  npc_mcam_idx_2_subbank_idx(rvu, save[i],
						 &sb, &sb_off);
		if (rc) {
			dev_err(rvu->dev,
				"%s: Error to find subbank for mcam idx=%u\n",
				__func__, save[i]);
			break;
		}

		deleted = __npc_subbank_free(rvu, sb, sb_off);
		if (!deleted) {
			dev_err(rvu->dev,
				"%s: Error to free mcam idx=%u\n",
				__func__, save[i]);
			break;
		}
	}

	return rc;
}

static int npc_defrag_add_2_show_list(struct rvu *rvu, u16 old_midx,
				      u16 new_midx, u16 vidx)
{
	struct npc_defrag_show_node *node;

	node = kcalloc(1, sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	node->old_midx = old_midx;
	node->new_midx = new_midx;
	node->vidx = vidx;
	INIT_LIST_HEAD(&node->list);

	mutex_lock(&npc_priv.lock);
	list_add_tail(&node->list, &npc_priv.defrag_lh);
	mutex_unlock(&npc_priv.lock);

	return 0;
}

static
int npc_defrag_move_vdx_to_free(struct rvu *rvu,
				struct npc_defrag_node *f,
				struct npc_defrag_node *v,
				int cnt, u16 *save)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	int i, vidx_cnt, rc, sb_off;
	u16 new_midx, old_midx, vidx;
	struct npc_subbank *sb;
	bool deleted;
	u16 pcifunc;
	int blkaddr;
	void *map;
	u8 bank;
	u16 midx;
	u64 stats;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);

	vidx_cnt = v->vidx_cnt;
	for (i = 0; i < cnt; i++) {
		vidx = v->vidx[vidx_cnt - i - 1];
		old_midx = npc_vidx2idx(vidx);
		new_midx = save[cnt - i - 1];

		dev_dbg(rvu->dev,
			"%s: Moving %u ---> %u  (vidx=%u)\n",
			__func__,
			old_midx, new_midx, vidx);

		rc = npc_defrag_add_2_show_list(rvu, old_midx, new_midx, vidx);
		if (rc)
			dev_err(rvu->dev,
				"%s: Error happened to add to show list vidx=%u\n",
				__func__, vidx);

		/* Modify vidx to point to new mcam idx */
		rc = npc_vidx_maps_modify(rvu, vidx, new_midx);
		if (rc)
			return rc;

		midx = old_midx % mcam->banksize;
		bank = old_midx / mcam->banksize;
		stats = rvu_read64(rvu, blkaddr,
				   NPC_AF_CN20K_MCAMEX_BANKX_STAT_EXT(midx,
								      bank));

		npc_cn20k_enable_mcam_entry(rvu, blkaddr, old_midx, false);
		npc_cn20k_copy_mcam_entry(rvu, blkaddr, old_midx, new_midx);
		npc_cn20k_enable_mcam_entry(rvu, blkaddr, new_midx, true);

		midx = new_midx % mcam->banksize;
		bank = new_midx / mcam->banksize;
		rvu_write64(rvu, blkaddr,
			    NPC_AF_CN20K_MCAMEX_BANKX_STAT_EXT(midx, bank),
			    stats);

		/* Free the old mcam idx */
		rc =  npc_mcam_idx_2_subbank_idx(rvu, old_midx,
						 &sb, &sb_off);
		if (rc) {
			dev_err(rvu->dev,
				"%s: Unable to calculate subbank off for mcamidx=%u\n",
				__func__, old_midx);
			return rc;
		}

		deleted = __npc_subbank_free(rvu, sb, sb_off);
		if (!deleted) {
			dev_err(rvu->dev,
				"%s:  Failed to free mcamidx=%u sb=%u sb_off=%u\n",
				__func__, old_midx, sb->idx, sb_off);
			return -EFAULT;
		}

		/* save pcifunc */
		map = xa_load(&npc_priv.xa_idx2pf_map, old_midx);
		pcifunc = xa_to_value(map);

		/* delete from pf maps */
		rc =  npc_del_from_pf_maps(rvu, old_midx);
		if (rc) {
			dev_err(rvu->dev,
				"%s:  Failed to delete pf maps for mcamidx=%u\n",
				__func__, old_midx);
			return rc;
		}

		/* add new mcam_idx to pf map */
		rc = npc_add_to_pf_maps(rvu, new_midx, pcifunc);
		if (rc) {
			dev_err(rvu->dev,
				"%s:  Failed to add pf maps for mcamidx=%u\n",
				__func__, new_midx);
			return rc;
		}

		/* Remove from mcam maps */
		mcam->entry2pfvf_map[old_midx] = NPC_MCAM_INVALID_MAP;
		mcam->entry2cntr_map[old_midx] = NPC_MCAM_INVALID_MAP;
		npc_mcam_clear_bit(mcam, old_midx);

		mcam->entry2pfvf_map[new_midx] = pcifunc;
		/* Counter is not preserved */
		mcam->entry2cntr_map[new_midx] = new_midx;
		npc_mcam_set_bit(mcam, new_midx);

		/* Mark as invalid */
		v->vidx[vidx_cnt - i - 1] = -1;
		save[cnt - i - 1] = -1;

		f->free_cnt--;
		v->vidx_cnt--;
	}

	return 0;
}

static int npc_defrag_process(struct rvu *rvu, struct list_head *lh)
{
	struct npc_defrag_node *v = NULL;
	struct npc_defrag_node *f = NULL;
	int rc = 0, cnt;
	u16 *save;

	while (1) {
		/* Find subbank with minimum vidx */
		if (!v) {
			v = npc_subbank_min_vidx(lh);
			if (!v)
				break;
		}

		/* Find subbank with maximum free slots */
		if (!f) {
			f = npc_subbank_max_free(lh);
			if (!f)
				break;
		}

		if (!v->vidx_cnt) {
			list_del_init(&v->list);
			v = NULL;
			continue;
		}

		if (!f->free_cnt) {
			list_del_init(&f->list);
			f = NULL;
			continue;
		}

		/* If both subbanks are same, choose vidx and
		 * search for free list again
		 */
		if (f == v) {
			list_del_init(&f->list);
			f = NULL;
			continue;
		}

		/* Calculate minimum free slots needs to be allocated */
		cnt = f->free_cnt > v->vidx_cnt ? v->vidx_cnt :
			f->free_cnt;

		dev_dbg(rvu->dev,
			"%s: cnt=%u free_cnt=%u(sb=%u) vidx_cnt=%u(sb=%u)\n",
			__func__, cnt, f->free_cnt, f->idx,
			v->vidx_cnt, v->idx);

		/* Allocate an array to store newly allocated
		 * free slots (mcam indexes)
		 */
		save = kcalloc(cnt, sizeof(*save), GFP_KERNEL);
		if (!save) {
			rc = -ENOMEM;
			goto err;
		}

		/* Alloc free slots for existing vidx */
		rc = npc_defrag_alloc_free_slots(rvu, f, cnt, save);
		if (rc) {
			kfree(save);
			goto err;
		}

		/* Move vidx to free slots; update pf_map and vidx maps,
		 * and free existing vidx mcam slots
		 */
		rc = npc_defrag_move_vdx_to_free(rvu, f, v, cnt, save);
		if (rc) {
			kfree(save);
			goto err;
		}

		kfree(save);

		if (!f->free_cnt) {
			list_del_init(&f->list);
			f = NULL;
		}

		if (!v->vidx_cnt) {
			list_del_init(&v->list);
			v = NULL;
		}
	}

err:
	/* Whole defragmentation process is done within locks. if there
	 * is an error, it would be hard to roll back as index remove/add
	 * can fail again if it failed before. This would mean that there
	 * is bug in the index management algorithm.
	 * Return from here than rolling back.
	 */
	return rc;
}

static void npc_defrag_list_clear(void)
{
	struct npc_defrag_show_node *node, *next;

	mutex_lock(&npc_priv.lock);
	list_for_each_entry_safe(node, next, &npc_priv.defrag_lh, list) {
		list_del_init(&node->list);
		kfree(node);
	}

	mutex_unlock(&npc_priv.lock);
}

static void npc_lock_all_subbank(void)
{
	int i;

	for (i = 0; i < npc_priv.num_subbanks; i++)
		mutex_lock(&npc_priv.sb[i].lock);
}

static void npc_unlock_all_subbank(void)
{
	int i;

	for (i = npc_priv.num_subbanks - 1; i >= 0; i--)
		mutex_unlock(&npc_priv.sb[i].lock);
}

/* Only non-ref non-contigous mcam indexes
 * are picked for defrag process
 */
int npc_cn20k_defrag(struct rvu *rvu)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct npc_defrag_node *node, *tnode;
	struct list_head x4lh, x2lh, *lh;
	int rc = 0, i, sb_off, tot;
	struct npc_subbank *sb;
	unsigned long index;
	void *map;
	u16 midx;

	/* Free previous show list */
	npc_defrag_list_clear();

	INIT_LIST_HEAD(&x4lh);
	INIT_LIST_HEAD(&x2lh);

	node = kcalloc(npc_priv.num_subbanks, sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	/* Lock mcam */
	mutex_lock(&mcam->lock);
	npc_lock_all_subbank();

	/* Fill in node with subbank properties */
	for (i = 0; i < npc_priv.num_subbanks; i++) {
		sb = &npc_priv.sb[i];

		node[i].idx = i;
		node[i].key_type = sb->key_type;
		node[i].free_cnt = sb->free_cnt;
		node[i].vidx = kcalloc(npc_priv.subbank_depth * 2,
				       sizeof(*node[i].vidx),
				       GFP_KERNEL);
		if (!node[i].vidx) {
			rc = -ENOMEM;
			goto free_vidx;
		}

		/* If subbank is empty, dont include it in defrag
		 * process
		 */
		if (sb->flags & NPC_SUBBANK_FLAG_FREE) {
			node[i].valid = false;
			continue;
		}

		if (npc_defrag_skip_restricted_sb(i)) {
			node[i].valid = false;
			continue;
		}

		node[i].valid = true;
		INIT_LIST_HEAD(&node[i].list);

		/* Add node to x2 or x4 list */
		lh = sb->key_type == NPC_MCAM_KEY_X2 ? &x2lh : &x4lh;
		list_add_tail(&node[i].list, lh);
	}

	/* Filling vidx[] array with all vidx in that subbank */
	xa_for_each_start(&npc_priv.xa_vidx2idx_map, index, map,
			  npc_priv.bank_depth * 2) {
		midx = xa_to_value(map);
		rc =  npc_mcam_idx_2_subbank_idx(rvu, midx,
						 &sb, &sb_off);
		if (rc) {
			dev_err(rvu->dev,
				"%s: Error to get mcam_idx for vidx=%lu\n",
				__func__, index);
			goto free_vidx;
		}

		tnode = &node[sb->idx];
		tnode->vidx[tnode->vidx_cnt] = index;
		tnode->vidx_cnt++;
	}

	/* Mark all subbank which has ref allocation */
	for (i = 0; i < npc_priv.num_subbanks; i++) {
		tnode = &node[i];

		if (!tnode->valid)
			continue;

		tot = (tnode->key_type == NPC_MCAM_KEY_X2) ?
			npc_priv.subbank_depth * 2 : npc_priv.subbank_depth;

		if (node[i].vidx_cnt != tot - tnode->free_cnt)
			tnode->refs = true;
	}

	rc =  npc_defrag_process(rvu, &x2lh);
	if (rc)
		goto free_vidx;

	rc =  npc_defrag_process(rvu, &x4lh);
	if (rc)
		goto free_vidx;

free_vidx:
	npc_unlock_all_subbank();
	mutex_unlock(&mcam->lock);
	for (i = 0; i < npc_priv.num_subbanks; i++)
		kfree(node[i].vidx);
	kfree(node);
	return rc;
}

int rvu_mbox_handler_npc_defrag(struct rvu *rvu, struct msg_req *req,
				struct msg_rsp *rsp)
{
	return npc_cn20k_defrag(rvu);
}

int npc_cn20k_dft_rules_idx_get(struct rvu *rvu, u16 pcifunc, u16 *bcast,
				u16 *mcast, u16 *promisc, u16 *ucast)
{
	u16 *ptr[4] = {promisc, mcast, bcast, ucast};
	unsigned long idx;
	bool set = false;
	void *val;
	int i, j;

	if (!npc_priv.init_done)
		return 0;

	if (is_lbk_vf(rvu, pcifunc)) {
		if (!ptr[0])
			return -EINVAL;

		idx = NPC_DFT_RULE_ID_MK(pcifunc, NPC_DFT_RULE_PROMISC_ID);
		val = xa_load(&npc_priv.xa_pf2dfl_rmap, idx);
		if (!val) {
			pr_debug("%s: Failed to find %s index for pcifunc=%#x\n",
				 __func__,
				 npc_dft_rule_name[NPC_DFT_RULE_PROMISC_ID],
				 pcifunc);

			*ptr[0] = USHRT_MAX;
			return -ESRCH;
		}

		*ptr[0] = xa_to_value(val);
		return 0;
	}

	if (is_vf(pcifunc)) {
		if (!ptr[3])
			return -EINVAL;

		idx = NPC_DFT_RULE_ID_MK(pcifunc, NPC_DFT_RULE_UCAST_ID);
		val = xa_load(&npc_priv.xa_pf2dfl_rmap, idx);
		if (!val) {
			pr_debug("%s: Failed to find %s index for pcifunc=%#x\n",
				 __func__,
				 npc_dft_rule_name[NPC_DFT_RULE_UCAST_ID],
				 pcifunc);

			*ptr[3] = USHRT_MAX;
			return -ESRCH;
		}

		*ptr[3] = xa_to_value(val);
		return 0;
	}

	for (i = NPC_DFT_RULE_START_ID, j = 0; i < NPC_DFT_RULE_MAX_ID; i++,
	     j++) {
		if (!ptr[j])
			continue;

		idx = NPC_DFT_RULE_ID_MK(pcifunc, i);
		val = xa_load(&npc_priv.xa_pf2dfl_rmap, idx);
		if (!val) {
			pr_debug("%s: Failed to find %s index for pcifunc=%#x\n",
				 __func__,
				 npc_dft_rule_name[i], pcifunc);

			*ptr[j] = USHRT_MAX;
			continue;
		}

		*ptr[j] = xa_to_value(val);
		set = true;
	}

	return  set ? 0 : -ESRCH;
}

static bool npc_is_cgx_or_lbk(struct rvu *rvu, u16 pcifunc)
{
	return is_pf_cgxmapped(rvu, rvu_get_pf(rvu->pdev, pcifunc)) ||
		is_lbk_vf(rvu, pcifunc);
}

void npc_cn20k_dft_rules_free(struct rvu *rvu, u16 pcifunc)
{
	struct npc_mcam_free_entry_req free_req = { 0 };
	unsigned long index;
	struct msg_rsp rsp;
	u16 ptr[4];
	int rc, i;
	void *map;

	if (!npc_priv.init_done)
		return;

	if (!npc_is_cgx_or_lbk(rvu, pcifunc)) {
		dev_dbg(rvu->dev,
			"%s: dft rule allocation is only for cgx mapped device, pcifunc=%#x\n",
			__func__, pcifunc);
		return;
	}

	rc = npc_cn20k_dft_rules_idx_get(rvu, pcifunc, &ptr[0], &ptr[1],
					 &ptr[2], &ptr[3]);
	if (rc)
		return;

	/* LBK */
	if (is_lbk_vf(rvu, pcifunc)) {
		index = NPC_DFT_RULE_ID_MK(pcifunc, NPC_DFT_RULE_PROMISC_ID);
		map = xa_erase(&npc_priv.xa_pf2dfl_rmap, index);
		if (!map)
			dev_dbg(rvu->dev,
				"%s: Err from delete %s mcam idx from xarray (pcifunc=%#x\n",
				__func__,
				npc_dft_rule_name[NPC_DFT_RULE_PROMISC_ID],
				pcifunc);

		goto free_rules;
	}

	/* VF */
	if (is_vf(pcifunc)) {
		index = NPC_DFT_RULE_ID_MK(pcifunc, NPC_DFT_RULE_UCAST_ID);
		map = xa_erase(&npc_priv.xa_pf2dfl_rmap, index);
		if (!map)
			dev_dbg(rvu->dev,
				"%s: Err from delete %s mcam idx from xarray (pcifunc=%#x\n",
				__func__,
				npc_dft_rule_name[NPC_DFT_RULE_UCAST_ID],
				pcifunc);

		goto free_rules;
	}

	/* PF */
	for (i = NPC_DFT_RULE_START_ID; i < NPC_DFT_RULE_MAX_ID; i++)  {
		index = NPC_DFT_RULE_ID_MK(pcifunc, i);
		map = xa_erase(&npc_priv.xa_pf2dfl_rmap, index);
		if (!map)
			dev_dbg(rvu->dev,
				"%s: Err from delete %s mcam idx from xarray (pcifunc=%#x\n",
				__func__, npc_dft_rule_name[i],
				pcifunc);
	}

free_rules:

	free_req.hdr.pcifunc = pcifunc;
	free_req.all = 1;
	rc = rvu_mbox_handler_npc_mcam_free_entry(rvu, &free_req, &rsp);
	if (rc)
		dev_err(rvu->dev,
			"%s: Error deleting default entries (pcifunc=%#x\n",
			__func__, pcifunc);
}

int npc_cn20k_dft_rules_alloc(struct rvu *rvu, u16 pcifunc)
{
	struct npc_mcam_free_entry_req free_req = { 0 };
	u16 mcam_idx[4] = { 0 }, pf_ucast, pf_pcifunc;
	struct npc_mcam_alloc_entry_req req = { 0 };
	struct npc_mcam_alloc_entry_rsp rsp = { 0 };
	int ret, eidx, i, k, pf, cnt;
	struct rvu_pfvf *pfvf;
	unsigned long index;
	struct msg_rsp free_rsp;
	u16 b, m, p, u;

	if (!npc_priv.init_done)
		return 0;

	if (!npc_is_cgx_or_lbk(rvu, pcifunc)) {
		dev_dbg(rvu->dev,
			"%s: dft rule allocation is only for cgx mapped device, pcifunc=%#x\n",
			__func__, pcifunc);
		return 0;
	}

	/* Check if default rules are already alloced for this pcifunc */
	ret =  npc_cn20k_dft_rules_idx_get(rvu, pcifunc, &b, &m, &p, &u);
	if (!ret) {
		dev_dbg(rvu->dev,
			"%s: default rules are already installed (pcifunc=%#x)\n",
			__func__, pcifunc);
		dev_dbg(rvu->dev,
			"%s: bcast(%u) mcast(%u) promisc(%u) ucast(%u)\n",
			__func__, b, m, p, u);
		return 0;
	}

	/* Set ref index as lowest priority index */
	eidx = 2 * npc_priv.bank_depth - 1;

	/* Install only UCAST for VF */
	cnt = is_vf(pcifunc) ? 1 : ARRAY_SIZE(mcam_idx);

	/* For VF pcifunc, allocate default mcam indexes by taking
	 * ref as PF's ucast index.
	 */
	if (is_vf(pcifunc)) {
		pf = rvu_get_pf(rvu->pdev, pcifunc);
		pf_pcifunc = pf << RVU_CN20K_PFVF_PF_SHIFT;

		/* Get PF's ucast entry index */
		ret = npc_cn20k_dft_rules_idx_get(rvu, pf_pcifunc, NULL,
						  NULL, NULL, &pf_ucast);

		/* There is no PF rules installed; and VF installation comes
		 * first. PF may come later.
		 * TODO: Install PF rules before installing VF rules.
		 */

		/* Set PF's ucast as ref entry */
		if (!ret)
			eidx = pf_ucast;
	}

	pfvf = rvu_get_pfvf(rvu, pcifunc);
	pfvf->hw_prio = NPC_DFT_RULE_PRIO;

	req.contig = false;
	req.ref_prio = NPC_MCAM_HIGHER_PRIO;
	req.ref_entry = eidx;
	req.kw_type = NPC_MCAM_KEY_X2;
	req.count = cnt;
	req.hdr.pcifunc = pcifunc;

	ret = rvu_mbox_handler_npc_mcam_alloc_entry(rvu, &req, &rsp);

	/* successfully allocated index */
	if (!ret) {
		/* Copy indexes to local array */
		for (i = 0; i < cnt; i++)
			mcam_idx[i] = rsp.entry_list[i];

		goto chk_sanity;
	}

	/* If there is no slots available and request is for PF,
	 * return error.
	 */
	if (!is_vf(pcifunc)) {
		dev_err(rvu->dev,
			"%s: Default index allocation failed for pcifunc=%#x\n",
			__func__, pcifunc);
		return ret;
	}

	/* We could not find an index with higher priority index for VF.
	 * Find rule with lower priority index and set hardware priority
	 * as NPC_DFT_RULE_PRIO - 1 (higher hw priority)
	 */
	req.contig = false;
	req.kw_type = NPC_MCAM_KEY_X2;
	req.count = cnt;
	req.hdr.pcifunc = pcifunc;
	req.ref_prio = NPC_MCAM_LOWER_PRIO;
	req.ref_entry = eidx + 1;
	ret = rvu_mbox_handler_npc_mcam_alloc_entry(rvu, &req, &rsp);
	if (ret) {
		dev_err(rvu->dev,
			"%s: Default index allocation failed for pcifunc=%#x\n",
			__func__, pcifunc);
		return ret;
	}

	/* Copy indexes to local array */
	for (i = 0; i < cnt; i++)
		mcam_idx[i] = rsp.entry_list[i];

	pfvf->hw_prio = NPC_DFT_RULE_PRIO - 1;

chk_sanity:
	/* LBK */
	if (is_lbk_vf(rvu, pcifunc)) {
		index = NPC_DFT_RULE_ID_MK(pcifunc, NPC_DFT_RULE_PROMISC_ID);
		ret = xa_insert(&npc_priv.xa_pf2dfl_rmap, index,
				xa_mk_value(mcam_idx[0]), GFP_KERNEL);
		if (ret) {
			dev_err(rvu->dev,
				"%s: Err to insert %s mcam idx to xarray pcifunc=%#x\n",
				__func__,
				npc_dft_rule_name[NPC_DFT_RULE_PROMISC_ID],
				pcifunc);
			goto err;
		}

		goto done;
	}

	/* VF */
	if (is_vf(pcifunc)) {
		index = NPC_DFT_RULE_ID_MK(pcifunc, NPC_DFT_RULE_UCAST_ID);
		ret = xa_insert(&npc_priv.xa_pf2dfl_rmap, index,
				xa_mk_value(mcam_idx[0]), GFP_KERNEL);
		if (ret) {
			dev_err(rvu->dev,
				"%s: Err to insert %s mcam idx to xarray pcifunc=%#x\n",
				__func__,
				npc_dft_rule_name[NPC_DFT_RULE_UCAST_ID],
				pcifunc);
			goto err;
		}

		goto done;
	}

	/* PF */
	for (i = NPC_DFT_RULE_START_ID, k = 0; i < NPC_DFT_RULE_MAX_ID &&
	     k < cnt; i++, k++) {
		index = NPC_DFT_RULE_ID_MK(pcifunc, i);
		ret = xa_insert(&npc_priv.xa_pf2dfl_rmap, index,
				xa_mk_value(mcam_idx[k]), GFP_KERNEL);
		if (ret) {
			dev_err(rvu->dev,
				"%s: Err to insert %s mcam idx to xarray pcifunc=%#x\n",
				__func__, npc_dft_rule_name[i],
				pcifunc);
			for (int p = NPC_DFT_RULE_START_ID; p < i; p++) {
				index = NPC_DFT_RULE_ID_MK(pcifunc, p);
				xa_erase(&npc_priv.xa_pf2dfl_rmap, index);
			}
			goto err;
		}
	}

done:
	return 0;

err:
	free_req.hdr.pcifunc = pcifunc;
	free_req.all = 1;
	ret = rvu_mbox_handler_npc_mcam_free_entry(rvu, &free_req, &free_rsp);
	if (ret)
		dev_err(rvu->dev,
			"%s: Error deleting default entries (pcifunc=%#x\n",
			__func__, pcifunc);

	return -EFAULT;
}

static int npc_priv_init(struct rvu *rvu)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	int blkaddr, num_banks, bank_depth;
	int num_subbanks, subbank_depth;
	u64 npc_const1, npc_const2 = 0;
	struct npc_subbank *sb;
	u64 cfg;
	int i;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0) {
		dev_err(rvu->dev, "%s: NPC block not implemented\n",
			__func__);
		return -ENODEV;
	}

	npc_const1 = rvu_read64(rvu, blkaddr, NPC_AF_CONST1);
	if (npc_const1 & BIT_ULL(63))
		npc_const2 = rvu_read64(rvu, blkaddr, NPC_AF_CONST2);

	num_banks = mcam->banks;
	bank_depth = mcam->banksize;

	num_subbanks = FIELD_GET(GENMASK_ULL(39, 32), npc_const2);
	if (!num_subbanks) {
		dev_err(rvu->dev, "Number of subbanks is zero\n");
		return -EFAULT;
	}

	if (num_subbanks & (num_subbanks - 1)) {
		dev_err(rvu->dev,
			"subbanks cnt(%u) should be a power of 2\n",
			num_subbanks);
		return -EINVAL;
	}

	npc_priv.num_subbanks = num_subbanks;

	subbank_depth =	bank_depth / num_subbanks;

	npc_priv.bank_depth = bank_depth;
	npc_priv.subbank_depth = subbank_depth;

	/* Get kex configured key size */
	cfg = rvu_read64(rvu, blkaddr, NPC_AF_INTFX_KEX_CFG(0));
	npc_priv.kw = FIELD_GET(GENMASK_ULL(34, 32), cfg);

	dev_info(rvu->dev,
		 "banks=%u depth=%u, subbanks=%u depth=%u, key type=%s\n",
		 num_banks, bank_depth, num_subbanks, subbank_depth,
		 npc_kw_name[npc_priv.kw]);

	npc_priv.sb = kcalloc(num_subbanks, sizeof(struct npc_subbank),
			      GFP_KERNEL);
	if (!npc_priv.sb)
		return -ENOMEM;

	xa_init_flags(&npc_priv.xa_sb_used, XA_FLAGS_ALLOC);
	xa_init_flags(&npc_priv.xa_sb_free, XA_FLAGS_ALLOC);
	xa_init_flags(&npc_priv.xa_idx2pf_map, XA_FLAGS_ALLOC);
	xa_init_flags(&npc_priv.xa_pf_map, XA_FLAGS_ALLOC);
	xa_init_flags(&npc_priv.xa_pf2dfl_rmap, XA_FLAGS_ALLOC);
	xa_init_flags(&npc_priv.xa_idx2vidx_map, XA_FLAGS_ALLOC);
	xa_init_flags(&npc_priv.xa_vidx2idx_map, XA_FLAGS_ALLOC);

	if (npc_create_srch_order(num_subbanks))
		goto fail1;

	npc_populate_restricted_idxs(num_subbanks);

	/* Initialize subbanks */
	for (i = 0, sb = npc_priv.sb; i < num_subbanks; i++, sb++)
		npc_subbank_init(rvu, sb, i);

	/* Get number of pcifuncs in the system */
	npc_priv.pf_cnt = npc_pcifunc_map_create(rvu);
	npc_priv.xa_pf2idx_map = kcalloc(npc_priv.pf_cnt,
					 sizeof(struct xarray),
					 GFP_KERNEL);
	if (!npc_priv.xa_pf2idx_map)
		goto fail2;

	for (i = 0; i < npc_priv.pf_cnt; i++)
		xa_init_flags(&npc_priv.xa_pf2idx_map[i], XA_FLAGS_ALLOC);

	INIT_LIST_HEAD(&npc_priv.defrag_lh);
	mutex_init(&npc_priv.lock);

	return 0;

fail2:
	kfree(subbank_srch_order);
	subbank_srch_order = NULL;

fail1:
	xa_destroy(&npc_priv.xa_sb_used);
	xa_destroy(&npc_priv.xa_sb_free);
	xa_destroy(&npc_priv.xa_idx2pf_map);
	xa_destroy(&npc_priv.xa_pf_map);
	xa_destroy(&npc_priv.xa_pf2dfl_rmap);
	xa_destroy(&npc_priv.xa_idx2vidx_map);
	xa_destroy(&npc_priv.xa_vidx2idx_map);
	kfree(npc_priv.sb);
	npc_priv.sb = NULL;
	return -ENOMEM;
}

void npc_cn20k_deinit(struct rvu *rvu)
{
	int i;

	xa_destroy(&npc_priv.xa_sb_used);
	xa_destroy(&npc_priv.xa_sb_free);
	xa_destroy(&npc_priv.xa_idx2pf_map);
	xa_destroy(&npc_priv.xa_pf_map);
	xa_destroy(&npc_priv.xa_pf2dfl_rmap);
	xa_destroy(&npc_priv.xa_idx2vidx_map);
	xa_destroy(&npc_priv.xa_vidx2idx_map);

	for (i = 0; i < npc_priv.pf_cnt; i++)
		xa_destroy(&npc_priv.xa_pf2idx_map[i]);

	kfree(npc_priv.xa_pf2idx_map);
	/* No need to destroy mutex lock as it is
	 * part of subbank structure
	 */
	kfree(npc_priv.sb);
	kfree(subbank_srch_order);
}

static int npc_setup_mcam_section(struct rvu *rvu, int key_type)
{
	int blkaddr, sec;

	blkaddr = rvu_get_blkaddr(rvu, BLKTYPE_NPC, 0);
	if (blkaddr < 0) {
		dev_err(rvu->dev, "%s: NPC block not implemented\n", __func__);
		return -ENODEV;
	}

	for (sec = 0; sec < npc_priv.num_subbanks; sec++)
		rvu_write64(rvu, blkaddr,
			    NPC_AF_MCAM_SECTIONX_CFG_EXT(sec), key_type);

	return 0;
}

int npc_cn20k_init(struct rvu *rvu)
{
	int err;

	err = npc_priv_init(rvu);
	if (err) {
		dev_err(rvu->dev, "%s: Error to init\n",
			__func__);
		return err;
	}

	err = npc_setup_mcam_section(rvu, NPC_MCAM_KEY_X2);
	if (err) {
		dev_err(rvu->dev, "%s: mcam section configuration failure\n",
			__func__);
		return err;
	}

	npc_priv.init_done = true;

	return 0;
}
