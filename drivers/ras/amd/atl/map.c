// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD Address Translation Library
 *
 * map.c : Functions to read and decode DRAM address maps
 *
 * Copyright (c) 2023, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Yazen Ghannam <Yazen.Ghannam@amd.com>
 */

#include "internal.h"

static int df2_get_intlv_mode(struct addr_ctx *ctx)
{
	ctx->map.intlv_mode = FIELD_GET(DF2_INTLV_NUM_CHAN, ctx->map.base);

	if (ctx->map.intlv_mode == 8)
		ctx->map.intlv_mode = DF2_2CHAN_HASH;

	if (ctx->map.intlv_mode != NONE &&
	    ctx->map.intlv_mode != NOHASH_2CHAN &&
	    ctx->map.intlv_mode != DF2_2CHAN_HASH)
		return -EINVAL;

	return 0;
}

static int df3_get_intlv_mode(struct addr_ctx *ctx)
{
	ctx->map.intlv_mode = FIELD_GET(DF3_INTLV_NUM_CHAN, ctx->map.base);
	return 0;
}

static int df3p5_get_intlv_mode(struct addr_ctx *ctx)
{
	ctx->map.intlv_mode = FIELD_GET(DF3p5_INTLV_NUM_CHAN, ctx->map.base);

	if (ctx->map.intlv_mode == DF3_6CHAN)
		return -EINVAL;

	return 0;
}

static int df4_get_intlv_mode(struct addr_ctx *ctx)
{
	ctx->map.intlv_mode = FIELD_GET(DF4_INTLV_NUM_CHAN, ctx->map.intlv);

	if (ctx->map.intlv_mode == DF3_COD4_2CHAN_HASH ||
	    ctx->map.intlv_mode == DF3_COD2_4CHAN_HASH ||
	    ctx->map.intlv_mode == DF3_COD1_8CHAN_HASH ||
	    ctx->map.intlv_mode == DF3_6CHAN)
		return -EINVAL;

	return 0;
}

static int df4p5_get_intlv_mode(struct addr_ctx *ctx)
{
	ctx->map.intlv_mode = FIELD_GET(DF4p5_INTLV_NUM_CHAN, ctx->map.intlv);

	if (ctx->map.intlv_mode <= NOHASH_32CHAN)
		return 0;

	if (ctx->map.intlv_mode >= MI3_HASH_8CHAN &&
	    ctx->map.intlv_mode <= MI3_HASH_32CHAN)
		return 0;

	/*
	 * Modes matching the ranges above are returned as-is.
	 *
	 * All other modes are "fixed up" by adding 20h to make a unique value.
	 */
	ctx->map.intlv_mode += 0x20;

	return 0;
}

static int get_intlv_mode(struct addr_ctx *ctx)
{
	int ret;

	switch (df_cfg.rev) {
	case DF2:
		ret = df2_get_intlv_mode(ctx);
		break;
	case DF3:
		ret = df3_get_intlv_mode(ctx);
		break;
	case DF3p5:
		ret = df3p5_get_intlv_mode(ctx);
		break;
	case DF4:
		ret = df4_get_intlv_mode(ctx);
		break;
	case DF4p5:
		ret = df4p5_get_intlv_mode(ctx);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret)
		atl_debug_on_bad_df_rev();

	return ret;
}

static u64 get_hi_addr_offset(u32 reg_dram_offset)
{
	u8 shift = DF_DRAM_BASE_LIMIT_LSB;
	u64 hi_addr_offset;

	switch (df_cfg.rev) {
	case DF2:
		hi_addr_offset = FIELD_GET(DF2_HI_ADDR_OFFSET, reg_dram_offset);
		break;
	case DF3:
	case DF3p5:
		hi_addr_offset = FIELD_GET(DF3_HI_ADDR_OFFSET, reg_dram_offset);
		break;
	case DF4:
	case DF4p5:
		hi_addr_offset = FIELD_GET(DF4_HI_ADDR_OFFSET, reg_dram_offset);
		break;
	default:
		hi_addr_offset = 0;
		atl_debug_on_bad_df_rev();
	}

	if (df_cfg.rev == DF4p5 && df_cfg.flags.heterogeneous)
		shift = MI300_DRAM_LIMIT_LSB;

	return hi_addr_offset << shift;
}

/*
 * Returns:	0 if offset is disabled.
 *		1 if offset is enabled.
 *		-EINVAL on error.
 */
static int get_dram_offset(struct addr_ctx *ctx, u64 *norm_offset)
{
	u32 reg_dram_offset;
	u8 map_num;

	/* Should not be called for map 0. */
	if (!ctx->map.num) {
		atl_debug(ctx, "Trying to find DRAM offset for map 0");
		return -EINVAL;
	}

	/*
	 * DramOffset registers don't exist for map 0, so the base register
	 * actually refers to map 1.
	 * Adjust the map_num for the register offsets.
	 */
	map_num = ctx->map.num - 1;

	if (df_cfg.rev >= DF4) {
		/* Read D18F7x140 (DramOffset) */
		if (df_indirect_read_instance(ctx->node_id, 7, 0x140 + (4 * map_num),
					      ctx->inst_id, &reg_dram_offset))
			return -EINVAL;

	} else {
		/* Read D18F0x1B4 (DramOffset) */
		if (df_indirect_read_instance(ctx->node_id, 0, 0x1B4 + (4 * map_num),
					      ctx->inst_id, &reg_dram_offset))
			return -EINVAL;
	}

	if (!FIELD_GET(DF_HI_ADDR_OFFSET_EN, reg_dram_offset))
		return 0;

	*norm_offset = get_hi_addr_offset(reg_dram_offset);

	return 1;
}

static int df3_6ch_get_dram_addr_map(struct addr_ctx *ctx)
{
	u16 dst_fabric_id = FIELD_GET(DF3_DST_FABRIC_ID, ctx->map.limit);
	u8 i, j, shift = 4, mask = 0xF;
	u32 reg, offset = 0x60;
	u16 dst_node_id;

	/* Get Socket 1 register. */
	if (dst_fabric_id & df_cfg.socket_id_mask)
		offset = 0x68;

	/* Read D18F0x06{0,8} (DF::Skt0CsTargetRemap0)/(DF::Skt0CsTargetRemap1) */
	if (df_indirect_read_broadcast(ctx->node_id, 0, offset, &reg))
		return -EINVAL;

	/* Save 8 remap entries. */
	for (i = 0, j = 0; i < 8; i++, j++)
		ctx->map.remap_array[i] = (reg >> (j * shift)) & mask;

	dst_node_id = dst_fabric_id & df_cfg.node_id_mask;
	dst_node_id >>= df_cfg.node_id_shift;

	/* Read D18F2x090 (DF::Np2ChannelConfig) */
	if (df_indirect_read_broadcast(dst_node_id, 2, 0x90, &reg))
		return -EINVAL;

	ctx->map.np2_bits = FIELD_GET(DF_LOG2_ADDR_64K_SPACE0, reg);
	return 0;
}

static int df2_get_dram_addr_map(struct addr_ctx *ctx)
{
	/* Read D18F0x110 (DramBaseAddress). */
	if (df_indirect_read_instance(ctx->node_id, 0, 0x110 + (8 * ctx->map.num),
				      ctx->inst_id, &ctx->map.base))
		return -EINVAL;

	/* Read D18F0x114 (DramLimitAddress). */
	if (df_indirect_read_instance(ctx->node_id, 0, 0x114 + (8 * ctx->map.num),
				      ctx->inst_id, &ctx->map.limit))
		return -EINVAL;

	return 0;
}

static int df3_get_dram_addr_map(struct addr_ctx *ctx)
{
	if (df2_get_dram_addr_map(ctx))
		return -EINVAL;

	/* Read D18F0x3F8 (DfGlobalCtl). */
	if (df_indirect_read_instance(ctx->node_id, 0, 0x3F8,
				      ctx->inst_id, &ctx->map.ctl))
		return -EINVAL;

	return 0;
}

static int df4_get_dram_addr_map(struct addr_ctx *ctx)
{
	u8 remap_sel, i, j, shift = 4, mask = 0xF;
	u32 remap_reg;

	/* Read D18F7xE00 (DramBaseAddress). */
	if (df_indirect_read_instance(ctx->node_id, 7, 0xE00 + (16 * ctx->map.num),
				      ctx->inst_id, &ctx->map.base))
		return -EINVAL;

	/* Read D18F7xE04 (DramLimitAddress). */
	if (df_indirect_read_instance(ctx->node_id, 7, 0xE04 + (16 * ctx->map.num),
				      ctx->inst_id, &ctx->map.limit))
		return -EINVAL;

	/* Read D18F7xE08 (DramAddressCtl). */
	if (df_indirect_read_instance(ctx->node_id, 7, 0xE08 + (16 * ctx->map.num),
				      ctx->inst_id, &ctx->map.ctl))
		return -EINVAL;

	/* Read D18F7xE0C (DramAddressIntlv). */
	if (df_indirect_read_instance(ctx->node_id, 7, 0xE0C + (16 * ctx->map.num),
				      ctx->inst_id, &ctx->map.intlv))
		return -EINVAL;

	/* Check if Remap Enable bit is valid. */
	if (!FIELD_GET(DF4_REMAP_EN, ctx->map.ctl))
		return 0;

	/* Fill with bogus values, because '0' is a valid value. */
	memset(&ctx->map.remap_array, 0xFF, sizeof(ctx->map.remap_array));

	/* Get Remap registers. */
	remap_sel = FIELD_GET(DF4_REMAP_SEL, ctx->map.ctl);

	/* Read D18F7x180 (CsTargetRemap0A). */
	if (df_indirect_read_instance(ctx->node_id, 7, 0x180 + (8 * remap_sel),
				      ctx->inst_id, &remap_reg))
		return -EINVAL;

	/* Save first 8 remap entries. */
	for (i = 0, j = 0; i < 8; i++, j++)
		ctx->map.remap_array[i] = (remap_reg >> (j * shift)) & mask;

	/* Read D18F7x184 (CsTargetRemap0B). */
	if (df_indirect_read_instance(ctx->node_id, 7, 0x184 + (8 * remap_sel),
				      ctx->inst_id, &remap_reg))
		return -EINVAL;

	/* Save next 8 remap entries. */
	for (i = 8, j = 0; i < 16; i++, j++)
		ctx->map.remap_array[i] = (remap_reg >> (j * shift)) & mask;

	return 0;
}

static int df4p5_get_dram_addr_map(struct addr_ctx *ctx)
{
	u8 remap_sel, i, j, shift = 5, mask = 0x1F;
	u32 remap_reg;

	/* Read D18F7x200 (DramBaseAddress). */
	if (df_indirect_read_instance(ctx->node_id, 7, 0x200 + (16 * ctx->map.num),
				      ctx->inst_id, &ctx->map.base))
		return -EINVAL;

	/* Read D18F7x204 (DramLimitAddress). */
	if (df_indirect_read_instance(ctx->node_id, 7, 0x204 + (16 * ctx->map.num),
				      ctx->inst_id, &ctx->map.limit))
		return -EINVAL;

	/* Read D18F7x208 (DramAddressCtl). */
	if (df_indirect_read_instance(ctx->node_id, 7, 0x208 + (16 * ctx->map.num),
				      ctx->inst_id, &ctx->map.ctl))
		return -EINVAL;

	/* Read D18F7x20C (DramAddressIntlv). */
	if (df_indirect_read_instance(ctx->node_id, 7, 0x20C + (16 * ctx->map.num),
				      ctx->inst_id, &ctx->map.intlv))
		return -EINVAL;

	/* Check if Remap Enable bit is valid. */
	if (!FIELD_GET(DF4_REMAP_EN, ctx->map.ctl))
		return 0;

	/* Fill with bogus values, because '0' is a valid value. */
	memset(&ctx->map.remap_array, 0xFF, sizeof(ctx->map.remap_array));

	/* Get Remap registers. */
	remap_sel = FIELD_GET(DF4p5_REMAP_SEL, ctx->map.ctl);

	/* Read D18F7x180 (CsTargetRemap0A). */
	if (df_indirect_read_instance(ctx->node_id, 7, 0x180 + (24 * remap_sel),
				      ctx->inst_id, &remap_reg))
		return -EINVAL;

	/* Save first 6 remap entries. */
	for (i = 0, j = 0; i < 6; i++, j++)
		ctx->map.remap_array[i] = (remap_reg >> (j * shift)) & mask;

	/* Read D18F7x184 (CsTargetRemap0B). */
	if (df_indirect_read_instance(ctx->node_id, 7, 0x184 + (24 * remap_sel),
				      ctx->inst_id, &remap_reg))
		return -EINVAL;

	/* Save next 6 remap entries. */
	for (i = 6, j = 0; i < 12; i++, j++)
		ctx->map.remap_array[i] = (remap_reg >> (j * shift)) & mask;

	/* Read D18F7x188 (CsTargetRemap0C). */
	if (df_indirect_read_instance(ctx->node_id, 7, 0x188 + (24 * remap_sel),
				      ctx->inst_id, &remap_reg))
		return -EINVAL;

	/* Save next 6 remap entries. */
	for (i = 12, j = 0; i < 18; i++, j++)
		ctx->map.remap_array[i] = (remap_reg >> (j * shift)) & mask;

	return 0;
}

static int get_dram_addr_map(struct addr_ctx *ctx)
{
	switch (df_cfg.rev) {
	case DF2:	return df2_get_dram_addr_map(ctx);
	case DF3:
	case DF3p5:	return df3_get_dram_addr_map(ctx);
	case DF4:	return df4_get_dram_addr_map(ctx);
	case DF4p5:	return df4p5_get_dram_addr_map(ctx);
	default:
			atl_debug_on_bad_df_rev();
			return -EINVAL;
	}
}

static int get_coh_st_fabric_id(struct addr_ctx *ctx)
{
	u32 reg;

	/*
	 * On MI300 systems, the Coherent Station Fabric ID is derived
	 * later. And it does not depend on the register value.
	 */
	if (df_cfg.rev == DF4p5 && df_cfg.flags.heterogeneous)
		return 0;

	/* Read D18F0x50 (FabricBlockInstanceInformation3). */
	if (df_indirect_read_instance(ctx->node_id, 0, 0x50, ctx->inst_id, &reg))
		return -EINVAL;

	if (df_cfg.rev < DF4p5)
		ctx->coh_st_fabric_id = FIELD_GET(DF2_COH_ST_FABRIC_ID, reg);
	else
		ctx->coh_st_fabric_id = FIELD_GET(DF4p5_COH_ST_FABRIC_ID, reg);

	return 0;
}

static int find_normalized_offset(struct addr_ctx *ctx, u64 *norm_offset)
{
	u64 last_offset = 0;
	int ret;

	for (ctx->map.num = 1; ctx->map.num < df_cfg.num_coh_st_maps; ctx->map.num++) {
		ret = get_dram_offset(ctx, norm_offset);
		if (ret < 0)
			return ret;

		/* Continue search if this map's offset is not enabled. */
		if (!ret)
			continue;

		/* Enabled offsets should never be 0. */
		if (*norm_offset == 0) {
			atl_debug(ctx, "Enabled map %u offset is 0", ctx->map.num);
			return -EINVAL;
		}

		/* Offsets should always increase from one map to the next. */
		if (*norm_offset <= last_offset) {
			atl_debug(ctx, "Map %u offset (0x%016llx) <= previous (0x%016llx)",
				  ctx->map.num, *norm_offset, last_offset);
			return -EINVAL;
		}

		/* Match if this map's offset is less than the current calculated address. */
		if (ctx->ret_addr >= *norm_offset)
			break;

		last_offset = *norm_offset;
	}

	/*
	 * Finished search without finding a match.
	 * Reset to map 0 and no offset.
	 */
	if (ctx->map.num >= df_cfg.num_coh_st_maps) {
		ctx->map.num = 0;
		*norm_offset = 0;
	}

	return 0;
}

static bool valid_map(struct addr_ctx *ctx)
{
	if (df_cfg.rev >= DF4)
		return FIELD_GET(DF_ADDR_RANGE_VAL, ctx->map.ctl);
	else
		return FIELD_GET(DF_ADDR_RANGE_VAL, ctx->map.base);
}

static int get_address_map_common(struct addr_ctx *ctx)
{
	u64 norm_offset = 0;

	if (get_coh_st_fabric_id(ctx))
		return -EINVAL;

	if (find_normalized_offset(ctx, &norm_offset))
		return -EINVAL;

	if (get_dram_addr_map(ctx))
		return -EINVAL;

	if (!valid_map(ctx))
		return -EINVAL;

	ctx->ret_addr -= norm_offset;

	return 0;
}

static u8 get_num_intlv_chan(struct addr_ctx *ctx)
{
	switch (ctx->map.intlv_mode) {
	case NONE:
		return 1;
	case NOHASH_2CHAN:
	case DF2_2CHAN_HASH:
	case DF3_COD4_2CHAN_HASH:
	case DF4_NPS4_2CHAN_HASH:
	case DF4p5_NPS4_2CHAN_1K_HASH:
	case DF4p5_NPS4_2CHAN_2K_HASH:
		return 2;
	case DF4_NPS4_3CHAN_HASH:
	case DF4p5_NPS4_3CHAN_1K_HASH:
	case DF4p5_NPS4_3CHAN_2K_HASH:
		return 3;
	case NOHASH_4CHAN:
	case DF3_COD2_4CHAN_HASH:
	case DF4_NPS2_4CHAN_HASH:
	case DF4p5_NPS2_4CHAN_1K_HASH:
	case DF4p5_NPS2_4CHAN_2K_HASH:
		return 4;
	case DF4_NPS2_5CHAN_HASH:
	case DF4p5_NPS2_5CHAN_1K_HASH:
	case DF4p5_NPS2_5CHAN_2K_HASH:
		return 5;
	case DF3_6CHAN:
	case DF4_NPS2_6CHAN_HASH:
	case DF4p5_NPS2_6CHAN_1K_HASH:
	case DF4p5_NPS2_6CHAN_2K_HASH:
		return 6;
	case NOHASH_8CHAN:
	case DF3_COD1_8CHAN_HASH:
	case DF4_NPS1_8CHAN_HASH:
	case MI3_HASH_8CHAN:
	case DF4p5_NPS1_8CHAN_1K_HASH:
	case DF4p5_NPS1_8CHAN_2K_HASH:
		return 8;
	case DF4_NPS1_10CHAN_HASH:
	case DF4p5_NPS1_10CHAN_1K_HASH:
	case DF4p5_NPS1_10CHAN_2K_HASH:
		return 10;
	case DF4_NPS1_12CHAN_HASH:
	case DF4p5_NPS1_12CHAN_1K_HASH:
	case DF4p5_NPS1_12CHAN_2K_HASH:
		return 12;
	case NOHASH_16CHAN:
	case MI3_HASH_16CHAN:
	case DF4p5_NPS1_16CHAN_1K_HASH:
	case DF4p5_NPS1_16CHAN_2K_HASH:
		return 16;
	case DF4p5_NPS0_24CHAN_1K_HASH:
	case DF4p5_NPS0_24CHAN_2K_HASH:
		return 24;
	case NOHASH_32CHAN:
	case MI3_HASH_32CHAN:
		return 32;
	default:
		atl_debug_on_bad_intlv_mode(ctx);
		return 0;
	}
}

static void calculate_intlv_bits(struct addr_ctx *ctx)
{
	ctx->map.num_intlv_chan = get_num_intlv_chan(ctx);

	ctx->map.total_intlv_chan = ctx->map.num_intlv_chan;
	ctx->map.total_intlv_chan *= ctx->map.num_intlv_dies;
	ctx->map.total_intlv_chan *= ctx->map.num_intlv_sockets;

	/*
	 * Get the number of bits needed to cover this many channels.
	 * order_base_2() rounds up automatically.
	 */
	ctx->map.total_intlv_bits = order_base_2(ctx->map.total_intlv_chan);
}

static u8 get_intlv_bit_pos(struct addr_ctx *ctx)
{
	u8 addr_sel = 0;

	switch (df_cfg.rev) {
	case DF2:
		addr_sel = FIELD_GET(DF2_INTLV_ADDR_SEL, ctx->map.base);
		break;
	case DF3:
	case DF3p5:
		addr_sel = FIELD_GET(DF3_INTLV_ADDR_SEL, ctx->map.base);
		break;
	case DF4:
	case DF4p5:
		addr_sel = FIELD_GET(DF4_INTLV_ADDR_SEL, ctx->map.intlv);
		break;
	default:
		atl_debug_on_bad_df_rev();
		break;
	}

	/* Add '8' to get the 'interleave bit position'. */
	return addr_sel + 8;
}

static u8 get_num_intlv_dies(struct addr_ctx *ctx)
{
	u8 dies = 0;

	switch (df_cfg.rev) {
	case DF2:
		dies = FIELD_GET(DF2_INTLV_NUM_DIES, ctx->map.limit);
		break;
	case DF3:
		dies = FIELD_GET(DF3_INTLV_NUM_DIES, ctx->map.base);
		break;
	case DF3p5:
		dies = FIELD_GET(DF3p5_INTLV_NUM_DIES, ctx->map.base);
		break;
	case DF4:
	case DF4p5:
		dies = FIELD_GET(DF4_INTLV_NUM_DIES, ctx->map.intlv);
		break;
	default:
		atl_debug_on_bad_df_rev();
		break;
	}

	/* Register value is log2, e.g. 0 -> 1 die, 1 -> 2 dies, etc. */
	return 1 << dies;
}

static u8 get_num_intlv_sockets(struct addr_ctx *ctx)
{
	u8 sockets = 0;

	switch (df_cfg.rev) {
	case DF2:
		sockets = FIELD_GET(DF2_INTLV_NUM_SOCKETS, ctx->map.limit);
		break;
	case DF3:
	case DF3p5:
		sockets = FIELD_GET(DF2_INTLV_NUM_SOCKETS, ctx->map.base);
		break;
	case DF4:
	case DF4p5:
		sockets = FIELD_GET(DF4_INTLV_NUM_SOCKETS, ctx->map.intlv);
		break;
	default:
		atl_debug_on_bad_df_rev();
		break;
	}

	/* Register value is log2, e.g. 0 -> 1 sockets, 1 -> 2 sockets, etc. */
	return 1 << sockets;
}

static int get_global_map_data(struct addr_ctx *ctx)
{
	if (get_intlv_mode(ctx))
		return -EINVAL;

	if (ctx->map.intlv_mode == DF3_6CHAN &&
	    df3_6ch_get_dram_addr_map(ctx))
		return -EINVAL;

	ctx->map.intlv_bit_pos		= get_intlv_bit_pos(ctx);
	ctx->map.num_intlv_dies		= get_num_intlv_dies(ctx);
	ctx->map.num_intlv_sockets	= get_num_intlv_sockets(ctx);
	calculate_intlv_bits(ctx);

	return 0;
}

/*
 * Verify the interleave bits are correct in the different interleaving
 * settings.
 *
 * If @num_intlv_dies and/or @num_intlv_sockets are 1, it means the
 * respective interleaving is disabled.
 */
static inline bool map_bits_valid(struct addr_ctx *ctx, u8 bit1, u8 bit2,
				  u8 num_intlv_dies, u8 num_intlv_sockets)
{
	if (!(ctx->map.intlv_bit_pos == bit1 || ctx->map.intlv_bit_pos == bit2)) {
		pr_debug("Invalid interleave bit: %u", ctx->map.intlv_bit_pos);
		return false;
	}

	if (ctx->map.num_intlv_dies > num_intlv_dies) {
		pr_debug("Invalid number of interleave dies: %u", ctx->map.num_intlv_dies);
		return false;
	}

	if (ctx->map.num_intlv_sockets > num_intlv_sockets) {
		pr_debug("Invalid number of interleave sockets: %u", ctx->map.num_intlv_sockets);
		return false;
	}

	return true;
}

static int validate_address_map(struct addr_ctx *ctx)
{
	switch (ctx->map.intlv_mode) {
	case DF2_2CHAN_HASH:
	case DF3_COD4_2CHAN_HASH:
	case DF3_COD2_4CHAN_HASH:
	case DF3_COD1_8CHAN_HASH:
		if (!map_bits_valid(ctx, 8, 9, 1, 1))
			goto err;
		break;

	case DF4_NPS4_2CHAN_HASH:
	case DF4_NPS2_4CHAN_HASH:
	case DF4_NPS1_8CHAN_HASH:
	case DF4p5_NPS4_2CHAN_1K_HASH:
	case DF4p5_NPS4_2CHAN_2K_HASH:
	case DF4p5_NPS2_4CHAN_1K_HASH:
	case DF4p5_NPS2_4CHAN_2K_HASH:
	case DF4p5_NPS1_8CHAN_1K_HASH:
	case DF4p5_NPS1_8CHAN_2K_HASH:
	case DF4p5_NPS1_16CHAN_1K_HASH:
	case DF4p5_NPS1_16CHAN_2K_HASH:
		if (!map_bits_valid(ctx, 8, 8, 1, 2))
			goto err;
		break;

	case MI3_HASH_8CHAN:
	case MI3_HASH_16CHAN:
	case MI3_HASH_32CHAN:
		if (!map_bits_valid(ctx, 8, 8, 4, 1))
			goto err;
		break;

	/* Nothing to do for modes that don't need special validation checks. */
	default:
		break;
	}

	return 0;

err:
	atl_debug(ctx, "Inconsistent address map");
	return -EINVAL;
}

static void dump_address_map(struct dram_addr_map *map)
{
	u8 i;

	pr_debug("intlv_mode=0x%x",		map->intlv_mode);
	pr_debug("num=0x%x",			map->num);
	pr_debug("base=0x%x",			map->base);
	pr_debug("limit=0x%x",			map->limit);
	pr_debug("ctl=0x%x",			map->ctl);
	pr_debug("intlv=0x%x",			map->intlv);

	for (i = 0; i < MAX_COH_ST_CHANNELS; i++)
		pr_debug("remap_array[%u]=0x%x", i, map->remap_array[i]);

	pr_debug("intlv_bit_pos=%u",		map->intlv_bit_pos);
	pr_debug("num_intlv_chan=%u",		map->num_intlv_chan);
	pr_debug("num_intlv_dies=%u",		map->num_intlv_dies);
	pr_debug("num_intlv_sockets=%u",	map->num_intlv_sockets);
	pr_debug("total_intlv_chan=%u",		map->total_intlv_chan);
	pr_debug("total_intlv_bits=%u",		map->total_intlv_bits);
}

int get_address_map(struct addr_ctx *ctx)
{
	int ret;

	ret = get_address_map_common(ctx);
	if (ret)
		return ret;

	ret = get_global_map_data(ctx);
	if (ret)
		return ret;

	dump_address_map(&ctx->map);

	ret = validate_address_map(ctx);
	if (ret)
		return ret;

	return ret;
}
