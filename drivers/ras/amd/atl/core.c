// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD Address Translation Library
 *
 * core.c : Module init and base translation functions
 *
 * Copyright (c) 2023, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Yazen Ghannam <Yazen.Ghannam@amd.com>
 */

#include <linux/module.h>
#include <asm/cpu_device_id.h>

#include "internal.h"

struct df_config df_cfg __read_mostly;

static int addr_over_limit(struct addr_ctx *ctx)
{
	u64 dram_limit_addr;

	if (df_cfg.rev >= DF4)
		dram_limit_addr = FIELD_GET(DF4_DRAM_LIMIT_ADDR, ctx->map.limit);
	else
		dram_limit_addr = FIELD_GET(DF2_DRAM_LIMIT_ADDR, ctx->map.limit);

	dram_limit_addr <<= DF_DRAM_BASE_LIMIT_LSB;
	dram_limit_addr |= GENMASK(DF_DRAM_BASE_LIMIT_LSB - 1, 0);

	/* Is calculated system address above DRAM limit address? */
	if (ctx->ret_addr > dram_limit_addr) {
		atl_debug(ctx, "Calculated address (0x%016llx) > DRAM limit (0x%016llx)",
			  ctx->ret_addr, dram_limit_addr);
		return -EINVAL;
	}

	return 0;
}

static bool legacy_hole_en(struct addr_ctx *ctx)
{
	u32 reg = ctx->map.base;

	if (df_cfg.rev >= DF4)
		reg = ctx->map.ctl;

	return FIELD_GET(DF_LEGACY_MMIO_HOLE_EN, reg);
}

static int add_legacy_hole(struct addr_ctx *ctx)
{
	u32 dram_hole_base;
	u8 func = 0;

	if (!legacy_hole_en(ctx))
		return 0;

	if (df_cfg.rev >= DF4)
		func = 7;

	if (df_indirect_read_broadcast(ctx->node_id, func, 0x104, &dram_hole_base))
		return -EINVAL;

	dram_hole_base &= DF_DRAM_HOLE_BASE_MASK;

	if (ctx->ret_addr >= dram_hole_base)
		ctx->ret_addr += (BIT_ULL(32) - dram_hole_base);

	return 0;
}

static u64 get_base_addr(struct addr_ctx *ctx)
{
	u64 base_addr;

	if (df_cfg.rev >= DF4)
		base_addr = FIELD_GET(DF4_BASE_ADDR, ctx->map.base);
	else
		base_addr = FIELD_GET(DF2_BASE_ADDR, ctx->map.base);

	return base_addr << DF_DRAM_BASE_LIMIT_LSB;
}

static int add_base_and_hole(struct addr_ctx *ctx)
{
	ctx->ret_addr += get_base_addr(ctx);

	if (add_legacy_hole(ctx))
		return -EINVAL;

	return 0;
}

static bool late_hole_remove(struct addr_ctx *ctx)
{
	if (df_cfg.rev == DF3p5)
		return true;

	if (df_cfg.rev == DF4)
		return true;

	if (ctx->map.intlv_mode == DF3_6CHAN)
		return true;

	return false;
}

unsigned long norm_to_sys_addr(u8 socket_id, u8 die_id, u8 coh_st_inst_id, unsigned long addr)
{
	struct addr_ctx ctx;

	if (df_cfg.rev == UNKNOWN)
		return -EINVAL;

	memset(&ctx, 0, sizeof(ctx));

	/* Start from the normalized address */
	ctx.ret_addr = addr;
	ctx.inst_id = coh_st_inst_id;

	ctx.inputs.norm_addr = addr;
	ctx.inputs.socket_id = socket_id;
	ctx.inputs.die_id = die_id;
	ctx.inputs.coh_st_inst_id = coh_st_inst_id;

	if (determine_node_id(&ctx, socket_id, die_id))
		return -EINVAL;

	if (get_address_map(&ctx))
		return -EINVAL;

	if (denormalize_address(&ctx))
		return -EINVAL;

	if (!late_hole_remove(&ctx) && add_base_and_hole(&ctx))
		return -EINVAL;

	if (dehash_address(&ctx))
		return -EINVAL;

	if (late_hole_remove(&ctx) && add_base_and_hole(&ctx))
		return -EINVAL;

	if (addr_over_limit(&ctx))
		return -EINVAL;

	return ctx.ret_addr;
}

static void check_for_legacy_df_access(void)
{
	/*
	 * All Zen-based systems before Family 19h use the legacy
	 * DF Indirect Access (FICAA/FICAD) offsets.
	 */
	if (boot_cpu_data.x86 < 0x19) {
		df_cfg.flags.legacy_ficaa = true;
		return;
	}

	/* All systems after Family 19h use the current offsets. */
	if (boot_cpu_data.x86 > 0x19)
		return;

	/* Some Family 19h systems use the legacy offsets. */
	switch (boot_cpu_data.x86_model) {
	case 0x00 ... 0x0f:
	case 0x20 ... 0x5f:
	       df_cfg.flags.legacy_ficaa = true;
	}
}

/*
 * This library provides functionality for AMD-based systems with a Data Fabric.
 * The set of systems with a Data Fabric is equivalent to the set of Zen-based systems
 * and the set of systems with the Scalable MCA feature at this time. However, these
 * are technically independent things.
 *
 * It's possible to match on the PCI IDs of the Data Fabric devices, but this will be
 * an ever expanding list. Instead, match on the SMCA and Zen features to cover all
 * relevant systems.
 */
static const struct x86_cpu_id amd_atl_cpuids[] = {
	X86_MATCH_FEATURE(X86_FEATURE_SMCA, NULL),
	X86_MATCH_FEATURE(X86_FEATURE_ZEN, NULL),
	{ }
};
MODULE_DEVICE_TABLE(x86cpu, amd_atl_cpuids);

static int __init amd_atl_init(void)
{
	if (!x86_match_cpu(amd_atl_cpuids))
		return -ENODEV;

	if (!amd_nb_num())
		return -ENODEV;

	check_for_legacy_df_access();

	if (get_df_system_info())
		return -ENODEV;

	/* Increment this module's recount so that it can't be easily unloaded. */
	__module_get(THIS_MODULE);
	amd_atl_register_decoder(convert_umc_mca_addr_to_sys_addr);

	pr_info("AMD Address Translation Library initialized");
	return 0;
}

/*
 * Exit function is only needed for testing and debug. Module unload must be
 * forced to override refcount check.
 */
static void __exit amd_atl_exit(void)
{
	amd_atl_unregister_decoder();
}

module_init(amd_atl_init);
module_exit(amd_atl_exit);

MODULE_LICENSE("GPL");
