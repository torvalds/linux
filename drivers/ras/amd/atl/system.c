// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD Address Translation Library
 *
 * system.c : Functions to read and save system-wide data
 *
 * Copyright (c) 2023, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Yazen Ghannam <Yazen.Ghannam@amd.com>
 */

#include "internal.h"

int determine_node_id(struct addr_ctx *ctx, u8 socket_id, u8 die_id)
{
	u16 socket_id_bits, die_id_bits;

	if (socket_id > 0 && df_cfg.socket_id_mask == 0) {
		atl_debug(ctx, "Invalid socket inputs: socket_id=%u socket_id_mask=0x%x",
			  socket_id, df_cfg.socket_id_mask);
		return -EINVAL;
	}

	/* Do each step independently to avoid shift out-of-bounds issues. */
	socket_id_bits =	socket_id;
	socket_id_bits <<=	df_cfg.socket_id_shift;
	socket_id_bits &=	df_cfg.socket_id_mask;

	if (die_id > 0 && df_cfg.die_id_mask == 0) {
		atl_debug(ctx, "Invalid die inputs: die_id=%u die_id_mask=0x%x",
			  die_id, df_cfg.die_id_mask);
		return -EINVAL;
	}

	/* Do each step independently to avoid shift out-of-bounds issues. */
	die_id_bits =		die_id;
	die_id_bits <<=		df_cfg.die_id_shift;
	die_id_bits &=		df_cfg.die_id_mask;

	ctx->node_id = (socket_id_bits | die_id_bits) >> df_cfg.node_id_shift;
	return 0;
}

static void df2_get_masks_shifts(u32 mask0)
{
	df_cfg.socket_id_shift		= FIELD_GET(DF2_SOCKET_ID_SHIFT, mask0);
	df_cfg.socket_id_mask		= FIELD_GET(DF2_SOCKET_ID_MASK, mask0);
	df_cfg.die_id_shift		= FIELD_GET(DF2_DIE_ID_SHIFT, mask0);
	df_cfg.die_id_mask		= FIELD_GET(DF2_DIE_ID_MASK, mask0);
	df_cfg.node_id_shift		= df_cfg.die_id_shift;
	df_cfg.node_id_mask		= df_cfg.socket_id_mask | df_cfg.die_id_mask;
	df_cfg.component_id_mask	= ~df_cfg.node_id_mask;
}

static void df3_get_masks_shifts(u32 mask0, u32 mask1)
{
	df_cfg.component_id_mask	= FIELD_GET(DF3_COMPONENT_ID_MASK, mask0);
	df_cfg.node_id_mask		= FIELD_GET(DF3_NODE_ID_MASK, mask0);

	df_cfg.node_id_shift		= FIELD_GET(DF3_NODE_ID_SHIFT, mask1);
	df_cfg.socket_id_shift		= FIELD_GET(DF3_SOCKET_ID_SHIFT, mask1);
	df_cfg.socket_id_mask		= FIELD_GET(DF3_SOCKET_ID_MASK, mask1);
	df_cfg.die_id_mask		= FIELD_GET(DF3_DIE_ID_MASK, mask1);
}

static void df3p5_get_masks_shifts(u32 mask0, u32 mask1, u32 mask2)
{
	df_cfg.component_id_mask	= FIELD_GET(DF4_COMPONENT_ID_MASK, mask0);
	df_cfg.node_id_mask		= FIELD_GET(DF4_NODE_ID_MASK, mask0);

	df_cfg.node_id_shift		= FIELD_GET(DF3_NODE_ID_SHIFT, mask1);
	df_cfg.socket_id_shift		= FIELD_GET(DF4_SOCKET_ID_SHIFT, mask1);

	df_cfg.socket_id_mask		= FIELD_GET(DF4_SOCKET_ID_MASK, mask2);
	df_cfg.die_id_mask		= FIELD_GET(DF4_DIE_ID_MASK, mask2);
}

static void df4_get_masks_shifts(u32 mask0, u32 mask1, u32 mask2)
{
	df3p5_get_masks_shifts(mask0, mask1, mask2);

	if (!(df_cfg.flags.socket_id_shift_quirk && df_cfg.socket_id_shift == 1))
		return;

	df_cfg.socket_id_shift	= 0;
	df_cfg.socket_id_mask	= 1;
	df_cfg.die_id_shift	= 0;
	df_cfg.die_id_mask	= 0;
	df_cfg.node_id_shift	= 8;
	df_cfg.node_id_mask	= 0x100;
}

static int df4_get_fabric_id_mask_registers(void)
{
	u32 mask0, mask1, mask2;

	/* Read D18F4x1B0 (SystemFabricIdMask0) */
	if (df_indirect_read_broadcast(0, 4, 0x1B0, &mask0))
		return -EINVAL;

	/* Read D18F4x1B4 (SystemFabricIdMask1) */
	if (df_indirect_read_broadcast(0, 4, 0x1B4, &mask1))
		return -EINVAL;

	/* Read D18F4x1B8 (SystemFabricIdMask2) */
	if (df_indirect_read_broadcast(0, 4, 0x1B8, &mask2))
		return -EINVAL;

	df4_get_masks_shifts(mask0, mask1, mask2);
	return 0;
}

static int df4_determine_df_rev(u32 reg)
{
	df_cfg.rev = FIELD_GET(DF_MINOR_REVISION, reg) < 5 ? DF4 : DF4p5;

	/* Check for special cases or quirks based on Device/Vendor IDs.*/

	/* Read D18F0x000 (DeviceVendorId0) */
	if (df_indirect_read_broadcast(0, 0, 0, &reg))
		return -EINVAL;

	if (reg == DF_FUNC0_ID_ZEN4_SERVER)
		df_cfg.flags.socket_id_shift_quirk = 1;

	if (reg == DF_FUNC0_ID_MI300) {
		df_cfg.flags.heterogeneous = 1;

		if (get_umc_info_mi300())
			return -EINVAL;
	}

	return df4_get_fabric_id_mask_registers();
}

static int determine_df_rev_legacy(void)
{
	u32 fabric_id_mask0, fabric_id_mask1, fabric_id_mask2;

	/*
	 * Check for DF3.5.
	 *
	 * Component ID Mask must be non-zero. Register D18F1x150 is
	 * reserved pre-DF3.5, so value will be Read-as-Zero.
	 */

	/* Read D18F1x150 (SystemFabricIdMask0). */
	if (df_indirect_read_broadcast(0, 1, 0x150, &fabric_id_mask0))
		return -EINVAL;

	if (FIELD_GET(DF4_COMPONENT_ID_MASK, fabric_id_mask0)) {
		df_cfg.rev = DF3p5;

		/* Read D18F1x154 (SystemFabricIdMask1) */
		if (df_indirect_read_broadcast(0, 1, 0x154, &fabric_id_mask1))
			return -EINVAL;

		/* Read D18F1x158 (SystemFabricIdMask2) */
		if (df_indirect_read_broadcast(0, 1, 0x158, &fabric_id_mask2))
			return -EINVAL;

		df3p5_get_masks_shifts(fabric_id_mask0, fabric_id_mask1, fabric_id_mask2);
		return 0;
	}

	/*
	 * Check for DF3.
	 *
	 * Component ID Mask must be non-zero. Field is Read-as-Zero on DF2.
	 */

	/* Read D18F1x208 (SystemFabricIdMask). */
	if (df_indirect_read_broadcast(0, 1, 0x208, &fabric_id_mask0))
		return -EINVAL;

	if (FIELD_GET(DF3_COMPONENT_ID_MASK, fabric_id_mask0)) {
		df_cfg.rev = DF3;

		/* Read D18F1x20C (SystemFabricIdMask1) */
		if (df_indirect_read_broadcast(0, 1, 0x20C, &fabric_id_mask1))
			return -EINVAL;

		df3_get_masks_shifts(fabric_id_mask0, fabric_id_mask1);
		return 0;
	}

	/* Default to DF2. */
	df_cfg.rev = DF2;
	df2_get_masks_shifts(fabric_id_mask0);
	return 0;
}

static int determine_df_rev(void)
{
	u32 reg;
	u8 rev;

	if (df_cfg.rev != UNKNOWN)
		return 0;

	/* Read D18F0x40 (FabricBlockInstanceCount). */
	if (df_indirect_read_broadcast(0, 0, 0x40, &reg))
		return -EINVAL;

	/*
	 * Revision fields added for DF4 and later.
	 *
	 * Major revision of '0' is found pre-DF4. Field is Read-as-Zero.
	 */
	rev = FIELD_GET(DF_MAJOR_REVISION, reg);
	if (!rev)
		return determine_df_rev_legacy();

	/*
	 * Fail out for major revisions other than '4'.
	 *
	 * Explicit support should be added for newer systems to avoid issues.
	 */
	if (rev == 4)
		return df4_determine_df_rev(reg);

	return -EINVAL;
}

static void get_num_maps(void)
{
	switch (df_cfg.rev) {
	case DF2:
	case DF3:
	case DF3p5:
		df_cfg.num_coh_st_maps	= 2;
		break;
	case DF4:
	case DF4p5:
		df_cfg.num_coh_st_maps	= 4;
		break;
	default:
		atl_debug_on_bad_df_rev();
	}
}

static void apply_node_id_shift(void)
{
	if (df_cfg.rev == DF2)
		return;

	df_cfg.die_id_shift		= df_cfg.node_id_shift;
	df_cfg.die_id_mask		<<= df_cfg.node_id_shift;
	df_cfg.socket_id_mask		<<= df_cfg.node_id_shift;
	df_cfg.socket_id_shift		+= df_cfg.node_id_shift;
}

static void dump_df_cfg(void)
{
	pr_debug("rev=0x%x",				df_cfg.rev);

	pr_debug("component_id_mask=0x%x",		df_cfg.component_id_mask);
	pr_debug("die_id_mask=0x%x",			df_cfg.die_id_mask);
	pr_debug("node_id_mask=0x%x",			df_cfg.node_id_mask);
	pr_debug("socket_id_mask=0x%x",			df_cfg.socket_id_mask);

	pr_debug("die_id_shift=0x%x",			df_cfg.die_id_shift);
	pr_debug("node_id_shift=0x%x",			df_cfg.node_id_shift);
	pr_debug("socket_id_shift=0x%x",		df_cfg.socket_id_shift);

	pr_debug("num_coh_st_maps=%u",			df_cfg.num_coh_st_maps);

	pr_debug("flags.legacy_ficaa=%u",		df_cfg.flags.legacy_ficaa);
	pr_debug("flags.socket_id_shift_quirk=%u",	df_cfg.flags.socket_id_shift_quirk);
}

int get_df_system_info(void)
{
	if (determine_df_rev()) {
		pr_warn("amd_atl: Failed to determine DF Revision");
		df_cfg.rev = UNKNOWN;
		return -EINVAL;
	}

	apply_node_id_shift();

	get_num_maps();

	dump_df_cfg();

	return 0;
}
