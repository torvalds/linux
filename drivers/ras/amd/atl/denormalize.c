// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD Address Translation Library
 *
 * denormalize.c : Functions to account for interleaving bits
 *
 * Copyright (c) 2023, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Yazen Ghannam <Yazen.Ghannam@amd.com>
 */

#include "internal.h"

/*
 * Returns the Destination Fabric ID. This is the first (lowest)
 * COH_ST Fabric ID used within a DRAM Address map.
 */
static u16 get_dst_fabric_id(struct addr_ctx *ctx)
{
	switch (df_cfg.rev) {
	case DF2:	return FIELD_GET(DF2_DST_FABRIC_ID,	ctx->map.limit);
	case DF3:	return FIELD_GET(DF3_DST_FABRIC_ID,	ctx->map.limit);
	case DF3p5:	return FIELD_GET(DF3p5_DST_FABRIC_ID,	ctx->map.limit);
	case DF4:	return FIELD_GET(DF4_DST_FABRIC_ID,	ctx->map.ctl);
	case DF4p5:	return FIELD_GET(DF4p5_DST_FABRIC_ID,	ctx->map.ctl);
	default:
			atl_debug_on_bad_df_rev();
			return 0;
	}
}

/*
 * Make a contiguous gap in address for N bits starting at bit P.
 *
 * Example:
 * address bits:		[20:0]
 * # of interleave bits    (n):	3
 * starting interleave bit (p):	8
 *
 * expanded address bits:	[20+n : n+p][n+p-1 : p][p-1 : 0]
 *				[23   :  11][10    : 8][7   : 0]
 */
static u64 make_space_for_coh_st_id_at_intlv_bit(struct addr_ctx *ctx)
{
	return expand_bits(ctx->map.intlv_bit_pos,
			   ctx->map.total_intlv_bits,
			   ctx->ret_addr);
}

/*
 * Make two gaps in address for N bits.
 * First gap is a single bit at bit P.
 * Second gap is the remaining N-1 bits at bit 12.
 *
 * Example:
 * address bits:		[20:0]
 * # of interleave bits    (n):	3
 * starting interleave bit (p):	8
 *
 * First gap
 * expanded address bits:	[20+1 : p+1][p][p-1 : 0]
 *				[21   :   9][8][7   : 0]
 *
 * Second gap uses result from first.
 *				r = n - 1; remaining interleave bits
 * expanded address bits:	[21+r : 12+r][12+r-1: 12][11 : 0]
 *				[23   :   14][13    : 12][11 : 0]
 */
static u64 make_space_for_coh_st_id_split_2_1(struct addr_ctx *ctx)
{
	/* Make a single space at the interleave bit. */
	u64 denorm_addr = expand_bits(ctx->map.intlv_bit_pos, 1, ctx->ret_addr);

	/* Done if there's only a single interleave bit. */
	if (ctx->map.total_intlv_bits <= 1)
		return denorm_addr;

	/* Make spaces for the remaining interleave bits starting at bit 12. */
	return expand_bits(12, ctx->map.total_intlv_bits - 1, denorm_addr);
}

/*
 * Make space for CS ID at bits [14:8] as follows:
 *
 * 8 channels	-> bits [10:8]
 * 16 channels	-> bits [11:8]
 * 32 channels	-> bits [14,11:8]
 *
 * 1 die	-> N/A
 * 2 dies	-> bit  [12]
 * 4 dies	-> bits [13:12]
 */
static u64 make_space_for_coh_st_id_mi300(struct addr_ctx *ctx)
{
	u8 num_intlv_bits = ilog2(ctx->map.num_intlv_chan);
	u64 denorm_addr;

	if (ctx->map.intlv_bit_pos != 8) {
		pr_debug("Invalid interleave bit: %u", ctx->map.intlv_bit_pos);
		return ~0ULL;
	}

	/* Channel bits. Covers up to 4 bits at [11:8]. */
	denorm_addr = expand_bits(8, min(num_intlv_bits, 4), ctx->ret_addr);

	/* Die bits. Always starts at [12]. */
	denorm_addr = expand_bits(12, ilog2(ctx->map.num_intlv_dies), denorm_addr);

	/* Additional channel bit at [14]. */
	if (num_intlv_bits > 4)
		denorm_addr = expand_bits(14, 1, denorm_addr);

	return denorm_addr;
}

/*
 * Take the current calculated address and shift enough bits in the middle
 * to make a gap where the interleave bits will be inserted.
 */
static u64 make_space_for_coh_st_id(struct addr_ctx *ctx)
{
	switch (ctx->map.intlv_mode) {
	case NOHASH_2CHAN:
	case NOHASH_4CHAN:
	case NOHASH_8CHAN:
	case NOHASH_16CHAN:
	case NOHASH_32CHAN:
	case DF2_2CHAN_HASH:
		return make_space_for_coh_st_id_at_intlv_bit(ctx);

	case DF3_COD4_2CHAN_HASH:
	case DF3_COD2_4CHAN_HASH:
	case DF3_COD1_8CHAN_HASH:
	case DF4_NPS4_2CHAN_HASH:
	case DF4_NPS2_4CHAN_HASH:
	case DF4_NPS1_8CHAN_HASH:
	case DF4p5_NPS4_2CHAN_1K_HASH:
	case DF4p5_NPS4_2CHAN_2K_HASH:
	case DF4p5_NPS2_4CHAN_2K_HASH:
	case DF4p5_NPS1_8CHAN_2K_HASH:
	case DF4p5_NPS1_16CHAN_2K_HASH:
		return make_space_for_coh_st_id_split_2_1(ctx);

	case MI3_HASH_8CHAN:
	case MI3_HASH_16CHAN:
	case MI3_HASH_32CHAN:
		return make_space_for_coh_st_id_mi300(ctx);

	default:
		atl_debug_on_bad_intlv_mode(ctx);
		return ~0ULL;
	}
}

static u16 get_coh_st_id_df2(struct addr_ctx *ctx)
{
	u8 num_socket_intlv_bits = ilog2(ctx->map.num_intlv_sockets);
	u8 num_die_intlv_bits = ilog2(ctx->map.num_intlv_dies);
	u8 num_intlv_bits;
	u16 coh_st_id, mask;

	coh_st_id = ctx->coh_st_fabric_id - get_dst_fabric_id(ctx);

	/* Channel interleave bits */
	num_intlv_bits = order_base_2(ctx->map.num_intlv_chan);
	mask = GENMASK(num_intlv_bits - 1, 0);
	coh_st_id &= mask;

	/* Die interleave bits */
	if (num_die_intlv_bits) {
		u16 die_bits;

		mask = GENMASK(num_die_intlv_bits - 1, 0);
		die_bits = ctx->coh_st_fabric_id & df_cfg.die_id_mask;
		die_bits >>= df_cfg.die_id_shift;

		coh_st_id |= (die_bits & mask) << num_intlv_bits;
		num_intlv_bits += num_die_intlv_bits;
	}

	/* Socket interleave bits */
	if (num_socket_intlv_bits) {
		u16 socket_bits;

		mask = GENMASK(num_socket_intlv_bits - 1, 0);
		socket_bits = ctx->coh_st_fabric_id & df_cfg.socket_id_mask;
		socket_bits >>= df_cfg.socket_id_shift;

		coh_st_id |= (socket_bits & mask) << num_intlv_bits;
	}

	return coh_st_id;
}

static u16 get_coh_st_id_df4(struct addr_ctx *ctx)
{
	/*
	 * Start with the original component mask and the number of interleave
	 * bits for the channels in this map.
	 */
	u8 num_intlv_bits = ilog2(ctx->map.num_intlv_chan);
	u16 mask = df_cfg.component_id_mask;

	u16 socket_bits;

	/* Set the derived Coherent Station ID to the input Coherent Station Fabric ID. */
	u16 coh_st_id = ctx->coh_st_fabric_id & mask;

	/*
	 * Subtract the "base" Destination Fabric ID.
	 * This accounts for systems with disabled Coherent Stations.
	 */
	coh_st_id -= get_dst_fabric_id(ctx) & mask;

	/*
	 * Generate and use a new mask based on the number of bits
	 * needed for channel interleaving in this map.
	 */
	mask = GENMASK(num_intlv_bits - 1, 0);
	coh_st_id &= mask;

	/* Done if socket interleaving is not enabled. */
	if (ctx->map.num_intlv_sockets <= 1)
		return coh_st_id;

	/*
	 * Figure out how many bits are needed for the number of
	 * interleaved sockets. And shift the derived Coherent Station ID to account
	 * for these.
	 */
	num_intlv_bits = ilog2(ctx->map.num_intlv_sockets);
	coh_st_id <<= num_intlv_bits;

	/* Generate a new mask for the socket interleaving bits. */
	mask = GENMASK(num_intlv_bits - 1, 0);

	/* Get the socket interleave bits from the original Coherent Station Fabric ID. */
	socket_bits = (ctx->coh_st_fabric_id & df_cfg.socket_id_mask) >> df_cfg.socket_id_shift;

	/* Apply the appropriate socket bits to the derived Coherent Station ID. */
	coh_st_id |= socket_bits & mask;

	return coh_st_id;
}

/*
 * MI300 hash has:
 * (C)hannel[3:0]	= coh_st_id[3:0]
 * (S)tack[0]		= coh_st_id[4]
 * (D)ie[1:0]		= coh_st_id[6:5]
 *
 * Hashed coh_st_id is swizzled so that Stack bit is at the end.
 * coh_st_id = SDDCCCC
 */
static u16 get_coh_st_id_mi300(struct addr_ctx *ctx)
{
	u8 channel_bits, die_bits, stack_bit;
	u16 die_id;

	/* Subtract the "base" Destination Fabric ID. */
	ctx->coh_st_fabric_id -= get_dst_fabric_id(ctx);

	die_id = (ctx->coh_st_fabric_id & df_cfg.die_id_mask) >> df_cfg.die_id_shift;

	channel_bits	= FIELD_GET(GENMASK(3, 0), ctx->coh_st_fabric_id);
	stack_bit	= FIELD_GET(BIT(4), ctx->coh_st_fabric_id) << 6;
	die_bits	= die_id << 4;

	return stack_bit | die_bits | channel_bits;
}

/*
 * Derive the correct Coherent Station ID that represents the interleave bits
 * used within the system physical address. This accounts for the
 * interleave mode, number of interleaved channels/dies/sockets, and
 * other system/mode-specific bit swizzling.
 *
 * Returns:	Coherent Station ID on success.
 *		All bits set on error.
 */
static u16 calculate_coh_st_id(struct addr_ctx *ctx)
{
	switch (ctx->map.intlv_mode) {
	case NOHASH_2CHAN:
	case NOHASH_4CHAN:
	case NOHASH_8CHAN:
	case NOHASH_16CHAN:
	case NOHASH_32CHAN:
	case DF3_COD4_2CHAN_HASH:
	case DF3_COD2_4CHAN_HASH:
	case DF3_COD1_8CHAN_HASH:
	case DF2_2CHAN_HASH:
		return get_coh_st_id_df2(ctx);

	case DF4_NPS4_2CHAN_HASH:
	case DF4_NPS2_4CHAN_HASH:
	case DF4_NPS1_8CHAN_HASH:
	case DF4p5_NPS4_2CHAN_1K_HASH:
	case DF4p5_NPS4_2CHAN_2K_HASH:
	case DF4p5_NPS2_4CHAN_2K_HASH:
	case DF4p5_NPS1_8CHAN_2K_HASH:
	case DF4p5_NPS1_16CHAN_2K_HASH:
		return get_coh_st_id_df4(ctx);

	case MI3_HASH_8CHAN:
	case MI3_HASH_16CHAN:
	case MI3_HASH_32CHAN:
		return get_coh_st_id_mi300(ctx);

	/* COH_ST ID is simply the COH_ST Fabric ID adjusted by the Destination Fabric ID. */
	case DF4p5_NPS2_4CHAN_1K_HASH:
	case DF4p5_NPS1_8CHAN_1K_HASH:
	case DF4p5_NPS1_16CHAN_1K_HASH:
		return ctx->coh_st_fabric_id - get_dst_fabric_id(ctx);

	default:
		atl_debug_on_bad_intlv_mode(ctx);
		return ~0;
	}
}

static u64 insert_coh_st_id_at_intlv_bit(struct addr_ctx *ctx, u64 denorm_addr, u16 coh_st_id)
{
	return denorm_addr | (coh_st_id << ctx->map.intlv_bit_pos);
}

static u64 insert_coh_st_id_split_2_1(struct addr_ctx *ctx, u64 denorm_addr, u16 coh_st_id)
{
	/* Insert coh_st_id[0] at the interleave bit. */
	denorm_addr |= (coh_st_id & BIT(0)) << ctx->map.intlv_bit_pos;

	/* Insert coh_st_id[2:1] at bit 12. */
	denorm_addr |= (coh_st_id & GENMASK(2, 1)) << 11;

	return denorm_addr;
}

static u64 insert_coh_st_id_split_2_2(struct addr_ctx *ctx, u64 denorm_addr, u16 coh_st_id)
{
	/* Insert coh_st_id[1:0] at bit 8. */
	denorm_addr |= (coh_st_id & GENMASK(1, 0)) << 8;

	/*
	 * Insert coh_st_id[n:2] at bit 12. 'n' could be 2 or 3.
	 * Grab both because bit 3 will be clear if unused.
	 */
	denorm_addr |= (coh_st_id & GENMASK(3, 2)) << 10;

	return denorm_addr;
}

static u64 insert_coh_st_id(struct addr_ctx *ctx, u64 denorm_addr, u16 coh_st_id)
{
	switch (ctx->map.intlv_mode) {
	case NOHASH_2CHAN:
	case NOHASH_4CHAN:
	case NOHASH_8CHAN:
	case NOHASH_16CHAN:
	case NOHASH_32CHAN:
	case MI3_HASH_8CHAN:
	case MI3_HASH_16CHAN:
	case MI3_HASH_32CHAN:
	case DF2_2CHAN_HASH:
		return insert_coh_st_id_at_intlv_bit(ctx, denorm_addr, coh_st_id);

	case DF3_COD4_2CHAN_HASH:
	case DF3_COD2_4CHAN_HASH:
	case DF3_COD1_8CHAN_HASH:
	case DF4_NPS4_2CHAN_HASH:
	case DF4_NPS2_4CHAN_HASH:
	case DF4_NPS1_8CHAN_HASH:
	case DF4p5_NPS4_2CHAN_1K_HASH:
	case DF4p5_NPS4_2CHAN_2K_HASH:
	case DF4p5_NPS2_4CHAN_2K_HASH:
	case DF4p5_NPS1_8CHAN_2K_HASH:
	case DF4p5_NPS1_16CHAN_2K_HASH:
		return insert_coh_st_id_split_2_1(ctx, denorm_addr, coh_st_id);

	case DF4p5_NPS2_4CHAN_1K_HASH:
	case DF4p5_NPS1_8CHAN_1K_HASH:
	case DF4p5_NPS1_16CHAN_1K_HASH:
		return insert_coh_st_id_split_2_2(ctx, denorm_addr, coh_st_id);

	default:
		atl_debug_on_bad_intlv_mode(ctx);
		return ~0ULL;
	}
}

/*
 * MI300 systems have a fixed, hardware-defined physical-to-logical
 * Coherent Station mapping. The Remap registers are not used.
 */
static const u16 phy_to_log_coh_st_map_mi300[] = {
	12, 13, 14, 15,
	 8,  9, 10, 11,
	 4,  5,  6,  7,
	 0,  1,  2,  3,
	28, 29, 30, 31,
	24, 25, 26, 27,
	20, 21, 22, 23,
	16, 17, 18, 19,
};

static u16 get_logical_coh_st_fabric_id_mi300(struct addr_ctx *ctx)
{
	if (ctx->inst_id >= ARRAY_SIZE(phy_to_log_coh_st_map_mi300)) {
		atl_debug(ctx, "Instance ID out of range");
		return ~0;
	}

	return phy_to_log_coh_st_map_mi300[ctx->inst_id] | (ctx->node_id << df_cfg.node_id_shift);
}

static u16 get_logical_coh_st_fabric_id(struct addr_ctx *ctx)
{
	u16 component_id, log_fabric_id;

	/* Start with the physical COH_ST Fabric ID. */
	u16 phys_fabric_id = ctx->coh_st_fabric_id;

	if (df_cfg.rev == DF4p5 && df_cfg.flags.heterogeneous)
		return get_logical_coh_st_fabric_id_mi300(ctx);

	/* Skip logical ID lookup if remapping is disabled. */
	if (!FIELD_GET(DF4_REMAP_EN, ctx->map.ctl) &&
	    ctx->map.intlv_mode != DF3_6CHAN)
		return phys_fabric_id;

	/* Mask off the Node ID bits to get the "local" Component ID. */
	component_id = phys_fabric_id & df_cfg.component_id_mask;

	/*
	 * Search the list of logical Component IDs for the one that
	 * matches this physical Component ID.
	 */
	for (log_fabric_id = 0; log_fabric_id < MAX_COH_ST_CHANNELS; log_fabric_id++) {
		if (ctx->map.remap_array[log_fabric_id] == component_id)
			break;
	}

	if (log_fabric_id == MAX_COH_ST_CHANNELS)
		atl_debug(ctx, "COH_ST remap entry not found for 0x%x",
			  log_fabric_id);

	/* Get the Node ID bits from the physical and apply to the logical. */
	return (phys_fabric_id & df_cfg.node_id_mask) | log_fabric_id;
}

static int denorm_addr_common(struct addr_ctx *ctx)
{
	u64 denorm_addr;
	u16 coh_st_id;

	/*
	 * Convert the original physical COH_ST Fabric ID to a logical value.
	 * This is required for non-power-of-two and other interleaving modes.
	 */
	ctx->coh_st_fabric_id = get_logical_coh_st_fabric_id(ctx);

	denorm_addr = make_space_for_coh_st_id(ctx);
	coh_st_id = calculate_coh_st_id(ctx);
	ctx->ret_addr = insert_coh_st_id(ctx, denorm_addr, coh_st_id);
	return 0;
}

static int denorm_addr_df3_6chan(struct addr_ctx *ctx)
{
	u16 coh_st_id = ctx->coh_st_fabric_id & df_cfg.component_id_mask;
	u8 total_intlv_bits = ctx->map.total_intlv_bits;
	u8 low_bit, intlv_bit = ctx->map.intlv_bit_pos;
	u64 msb_intlv_bits, temp_addr_a, temp_addr_b;
	u8 np2_bits = ctx->map.np2_bits;

	if (ctx->map.intlv_mode != DF3_6CHAN)
		return -EINVAL;

	/*
	 * 'np2_bits' holds the number of bits needed to cover the
	 * amount of memory (rounded up) in this map using 64K chunks.
	 *
	 * Example:
	 * Total memory in map:			6GB
	 * Rounded up to next power-of-2:	8GB
	 * Number of 64K chunks:		0x20000
	 * np2_bits = log2(# of chunks):	17
	 *
	 * Get the two most-significant interleave bits from the
	 * input address based on the following:
	 *
	 * [15 + np2_bits - total_intlv_bits : 14 + np2_bits - total_intlv_bits]
	 */
	low_bit = 14 + np2_bits - total_intlv_bits;
	msb_intlv_bits = ctx->ret_addr >> low_bit;
	msb_intlv_bits &= 0x3;

	/*
	 * If MSB are 11b, then logical COH_ST ID is 6 or 7.
	 * Need to adjust based on the mod3 result.
	 */
	if (msb_intlv_bits == 3) {
		u8 addr_mod, phys_addr_msb, msb_coh_st_id;

		/* Get the remaining interleave bits from the input address. */
		temp_addr_b = GENMASK_ULL(low_bit - 1, intlv_bit) & ctx->ret_addr;
		temp_addr_b >>= intlv_bit;

		/* Calculate the logical COH_ST offset based on mod3. */
		addr_mod = temp_addr_b % 3;

		/* Get COH_ST ID bits [2:1]. */
		msb_coh_st_id = (coh_st_id >> 1) & 0x3;

		/* Get the bit that starts the physical address bits. */
		phys_addr_msb = (intlv_bit + np2_bits + 1);
		phys_addr_msb &= BIT(0);
		phys_addr_msb++;
		phys_addr_msb *= 3 - addr_mod + msb_coh_st_id;
		phys_addr_msb %= 3;

		/* Move the physical address MSB to the correct place. */
		temp_addr_b |= phys_addr_msb << (low_bit - total_intlv_bits - intlv_bit);

		/* Generate a new COH_ST ID as follows: coh_st_id = [1, 1, coh_st_id[0]] */
		coh_st_id &= BIT(0);
		coh_st_id |= GENMASK(2, 1);
	} else {
		temp_addr_b = GENMASK_ULL(63, intlv_bit) & ctx->ret_addr;
		temp_addr_b >>= intlv_bit;
	}

	temp_addr_a = GENMASK_ULL(intlv_bit - 1, 0) & ctx->ret_addr;
	temp_addr_b <<= intlv_bit + total_intlv_bits;

	ctx->ret_addr = temp_addr_a | temp_addr_b;
	ctx->ret_addr |= coh_st_id << intlv_bit;
	return 0;
}

static int denorm_addr_df4_np2(struct addr_ctx *ctx)
{
	bool hash_ctl_64k, hash_ctl_2M, hash_ctl_1G;
	u16 group, group_offset, log_coh_st_offset;
	unsigned int mod_value, shift_value;
	u16 mask = df_cfg.component_id_mask;
	u64 temp_addr_a, temp_addr_b;
	bool hash_pa8, hashed_bit;

	switch (ctx->map.intlv_mode) {
	case DF4_NPS4_3CHAN_HASH:
		mod_value	= 3;
		shift_value	= 13;
		break;
	case DF4_NPS2_6CHAN_HASH:
		mod_value	= 3;
		shift_value	= 12;
		break;
	case DF4_NPS1_12CHAN_HASH:
		mod_value	= 3;
		shift_value	= 11;
		break;
	case DF4_NPS2_5CHAN_HASH:
		mod_value	= 5;
		shift_value	= 13;
		break;
	case DF4_NPS1_10CHAN_HASH:
		mod_value	= 5;
		shift_value	= 12;
		break;
	default:
		atl_debug_on_bad_intlv_mode(ctx);
		return -EINVAL;
	};

	if (ctx->map.num_intlv_sockets == 1) {
		hash_pa8	= BIT_ULL(shift_value) & ctx->ret_addr;
		temp_addr_a	= remove_bits(shift_value, shift_value, ctx->ret_addr);
	} else {
		hash_pa8	= ctx->coh_st_fabric_id & df_cfg.socket_id_mask;
		temp_addr_a	= ctx->ret_addr;
	}

	/* Make a gap for the real bit [8]. */
	temp_addr_a = expand_bits(8, 1, temp_addr_a);

	/* Make an additional gap for bits [13:12], as appropriate.*/
	if (ctx->map.intlv_mode == DF4_NPS2_6CHAN_HASH ||
	    ctx->map.intlv_mode == DF4_NPS1_10CHAN_HASH) {
		temp_addr_a = expand_bits(13, 1, temp_addr_a);
	} else if (ctx->map.intlv_mode == DF4_NPS1_12CHAN_HASH) {
		temp_addr_a = expand_bits(12, 2, temp_addr_a);
	}

	/* Keep bits [13:0]. */
	temp_addr_a &= GENMASK_ULL(13, 0);

	/* Get the appropriate high bits. */
	shift_value += 1 - ilog2(ctx->map.num_intlv_sockets);
	temp_addr_b = GENMASK_ULL(63, shift_value) & ctx->ret_addr;
	temp_addr_b >>= shift_value;
	temp_addr_b *= mod_value;

	/*
	 * Coherent Stations are divided into groups.
	 *
	 * Multiples of 3 (mod3) are divided into quadrants.
	 * e.g. NP4_3CHAN ->	[0, 1, 2] [6, 7, 8]
	 *			[3, 4, 5] [9, 10, 11]
	 *
	 * Multiples of 5 (mod5) are divided into sides.
	 * e.g. NP2_5CHAN ->	[0, 1, 2, 3, 4] [5, 6, 7, 8, 9]
	 */

	 /*
	  * Calculate the logical offset for the COH_ST within its DRAM Address map.
	  * e.g. if map includes [5, 6, 7, 8, 9] and target instance is '8', then
	  *	 log_coh_st_offset = 8 - 5 = 3
	  */
	log_coh_st_offset = (ctx->coh_st_fabric_id & mask) - (get_dst_fabric_id(ctx) & mask);

	/*
	 * Figure out the group number.
	 *
	 * Following above example,
	 * log_coh_st_offset = 3
	 * mod_value = 5
	 * group = 3 / 5 = 0
	 */
	group = log_coh_st_offset / mod_value;

	/*
	 * Figure out the offset within the group.
	 *
	 * Following above example,
	 * log_coh_st_offset = 3
	 * mod_value = 5
	 * group_offset = 3 % 5 = 3
	 */
	group_offset = log_coh_st_offset % mod_value;

	/* Adjust group_offset if the hashed bit [8] is set. */
	if (hash_pa8) {
		if (!group_offset)
			group_offset = mod_value - 1;
		else
			group_offset--;
	}

	/* Add in the group offset to the high bits. */
	temp_addr_b += group_offset;

	/* Shift the high bits to the proper starting position. */
	temp_addr_b <<= 14;

	/* Combine the high and low bits together. */
	ctx->ret_addr = temp_addr_a | temp_addr_b;

	/* Account for hashing here instead of in dehash_address(). */
	hash_ctl_64k	= FIELD_GET(DF4_HASH_CTL_64K, ctx->map.ctl);
	hash_ctl_2M	= FIELD_GET(DF4_HASH_CTL_2M, ctx->map.ctl);
	hash_ctl_1G	= FIELD_GET(DF4_HASH_CTL_1G, ctx->map.ctl);

	hashed_bit = !!hash_pa8;
	hashed_bit ^= FIELD_GET(BIT_ULL(14), ctx->ret_addr);
	hashed_bit ^= FIELD_GET(BIT_ULL(16), ctx->ret_addr) & hash_ctl_64k;
	hashed_bit ^= FIELD_GET(BIT_ULL(21), ctx->ret_addr) & hash_ctl_2M;
	hashed_bit ^= FIELD_GET(BIT_ULL(30), ctx->ret_addr) & hash_ctl_1G;

	ctx->ret_addr |= hashed_bit << 8;

	/* Done for 3 and 5 channel. */
	if (ctx->map.intlv_mode == DF4_NPS4_3CHAN_HASH ||
	    ctx->map.intlv_mode == DF4_NPS2_5CHAN_HASH)
		return 0;

	/* Select the proper 'group' bit to use for Bit 13. */
	if (ctx->map.intlv_mode == DF4_NPS1_12CHAN_HASH)
		hashed_bit = !!(group & BIT(1));
	else
		hashed_bit = group & BIT(0);

	hashed_bit ^= FIELD_GET(BIT_ULL(18), ctx->ret_addr) & hash_ctl_64k;
	hashed_bit ^= FIELD_GET(BIT_ULL(23), ctx->ret_addr) & hash_ctl_2M;
	hashed_bit ^= FIELD_GET(BIT_ULL(32), ctx->ret_addr) & hash_ctl_1G;

	ctx->ret_addr |= hashed_bit << 13;

	/* Done for 6 and 10 channel. */
	if (ctx->map.intlv_mode != DF4_NPS1_12CHAN_HASH)
		return 0;

	hashed_bit = group & BIT(0);
	hashed_bit ^= FIELD_GET(BIT_ULL(17), ctx->ret_addr) & hash_ctl_64k;
	hashed_bit ^= FIELD_GET(BIT_ULL(22), ctx->ret_addr) & hash_ctl_2M;
	hashed_bit ^= FIELD_GET(BIT_ULL(31), ctx->ret_addr) & hash_ctl_1G;

	ctx->ret_addr |= hashed_bit << 12;
	return 0;
}

int denormalize_address(struct addr_ctx *ctx)
{
	switch (ctx->map.intlv_mode) {
	case NONE:
		return 0;
	case DF4_NPS4_3CHAN_HASH:
	case DF4_NPS2_6CHAN_HASH:
	case DF4_NPS1_12CHAN_HASH:
	case DF4_NPS2_5CHAN_HASH:
	case DF4_NPS1_10CHAN_HASH:
		return denorm_addr_df4_np2(ctx);
	case DF3_6CHAN:
		return denorm_addr_df3_6chan(ctx);
	default:
		return denorm_addr_common(ctx);
	}
}
