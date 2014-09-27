/*
 * Marvell EBU SoC common clock handling
 *
 * Copyright (C) 2012 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 * Andrew Lunn <andrew@lunn.ch>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __CLK_MVEBU_COMMON_H_
#define __CLK_MVEBU_COMMON_H_

#include <linux/kernel.h>

extern spinlock_t ctrl_gating_lock;

struct device_node;

struct coreclk_ratio {
	int id;
	const char *name;
};

struct coreclk_soc_desc {
	u32 (*get_tclk_freq)(void __iomem *sar);
	u32 (*get_cpu_freq)(void __iomem *sar);
	void (*get_clk_ratio)(void __iomem *sar, int id, int *mult, int *div);
	bool (*is_sscg_enabled)(void __iomem *sar);
	u32 (*fix_sscg_deviation)(struct device_node *np, u32 system_clk);
	const struct coreclk_ratio *ratios;
	int num_ratios;
};

struct clk_gating_soc_desc {
	const char *name;
	const char *parent;
	int bit_idx;
	unsigned long flags;
};

void __init mvebu_coreclk_setup(struct device_node *np,
				const struct coreclk_soc_desc *desc);

void __init mvebu_clk_gating_setup(struct device_node *np,
				   const struct clk_gating_soc_desc *desc);

/*
 * This function is shared among the Kirkwood, Armada 370, Armada XP
 * and Armada 375 SoC
 */
u32 kirkwood_fix_sscg_deviation(struct device_node *np, u32 system_clk);
#endif
