/* SPDX-License-Identifier: GPL-2.0 */
/*
 * R-Car Gen3 Clock Pulse Generator Library
 *
 * Copyright (C) 2015-2018 Glider bvba
 * Copyright (C) 2019 Renesas Electronics Corp.
 *
 * Based on clk-rcar-gen3.c
 *
 * Copyright (C) 2015 Renesas Electronics Corp.
 */

#ifndef __CLK_RENESAS_RCAR_CPG_LIB_H__
#define __CLK_RENESAS_RCAR_CPG_LIB_H__

extern spinlock_t cpg_lock;

struct cpg_simple_notifier {
	struct notifier_block nb;
	void __iomem *reg;
	u32 saved;
};

void cpg_simple_notifier_register(struct raw_notifier_head *notifiers,
				  struct cpg_simple_notifier *csn);

void cpg_reg_modify(void __iomem *reg, u32 clear, u32 set);

struct clk * __init cpg_sd_clk_register(const char *name,
	void __iomem *base, unsigned int offset, const char *parent_name,
	struct raw_notifier_head *notifiers, bool skip_first);

struct clk * __init cpg_rpc_clk_register(const char *name,
	void __iomem *rpcckcr, const char *parent_name,
	struct raw_notifier_head *notifiers);

struct clk * __init cpg_rpcd2_clk_register(const char *name,
					   void __iomem *rpcckcr,
					   const char *parent_name);
#endif
