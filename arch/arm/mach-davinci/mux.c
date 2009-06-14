/*
 * Utility to set the DAVINCI MUX register from a table in mux.h
 *
 * Author: Vladimir Barinov, MontaVista Software, Inc. <source@mvista.com>
 *
 * Based on linux/arch/arm/plat-omap/mux.c:
 * Copyright (C) 2003 - 2005 Nokia Corporation
 *
 * Written by Tony Lindgren
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Copyright (C) 2008 Texas Instruments.
 */
#include <linux/io.h>
#include <linux/module.h>
#include <linux/spinlock.h>

#include <mach/hardware.h>
#include <mach/mux.h>
#include <mach/common.h>

/*
 * Sets the DAVINCI MUX register based on the table
 */
int __init_or_module davinci_cfg_reg(const unsigned long index)
{
	static DEFINE_SPINLOCK(mux_spin_lock);
	struct davinci_soc_info *soc_info = &davinci_soc_info;
	void __iomem *base = soc_info->pinmux_base;
	unsigned long flags;
	const struct mux_config *cfg;
	unsigned int reg_orig = 0, reg = 0;
	unsigned int mask, warn = 0;

	if (!soc_info->pinmux_pins)
		BUG();

	if (index >= soc_info->pinmux_pins_num) {
		printk(KERN_ERR "Invalid pin mux index: %lu (%lu)\n",
		       index, soc_info->pinmux_pins_num);
		dump_stack();
		return -ENODEV;
	}

	cfg = &soc_info->pinmux_pins[index];

	if (cfg->name == NULL) {
		printk(KERN_ERR "No entry for the specified index\n");
		return -ENODEV;
	}

	/* Update the mux register in question */
	if (cfg->mask) {
		unsigned	tmp1, tmp2;

		spin_lock_irqsave(&mux_spin_lock, flags);
		reg_orig = __raw_readl(base + cfg->mux_reg);

		mask = (cfg->mask << cfg->mask_offset);
		tmp1 = reg_orig & mask;
		reg = reg_orig & ~mask;

		tmp2 = (cfg->mode << cfg->mask_offset);
		reg |= tmp2;

		if (tmp1 != tmp2)
			warn = 1;

		__raw_writel(reg, base + cfg->mux_reg);
		spin_unlock_irqrestore(&mux_spin_lock, flags);
	}

	if (warn) {
#ifdef CONFIG_DAVINCI_MUX_WARNINGS
		printk(KERN_WARNING "MUX: initialized %s\n", cfg->name);
#endif
	}

#ifdef CONFIG_DAVINCI_MUX_DEBUG
	if (cfg->debug || warn) {
		printk(KERN_WARNING "MUX: Setting register %s\n", cfg->name);
		printk(KERN_WARNING "	   %s (0x%08x) = 0x%08x -> 0x%08x\n",
		       cfg->mux_reg_name, cfg->mux_reg, reg_orig, reg);
	}
#endif

	return 0;
}
EXPORT_SYMBOL(davinci_cfg_reg);
