/*
 * linux/arch/arm/plat-omap/mux.c
 *
 * Utility to set the Omap MUX and PULL_DWN registers from a table in mux.h
 *
 * Copyright (C) 2003 - 2008 Nokia Corporation
 *
 * Written by Tony Lindgren
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/io.h>
#include <linux/spinlock.h>
#include <asm/arch/mux.h>

#ifdef CONFIG_OMAP_MUX

static struct omap_mux_cfg *mux_cfg;

int __init omap_mux_register(struct omap_mux_cfg *arch_mux_cfg)
{
	if (!arch_mux_cfg || !arch_mux_cfg->pins || arch_mux_cfg->size == 0
			|| !arch_mux_cfg->cfg_reg) {
		printk(KERN_ERR "Invalid pin table\n");
		return -EINVAL;
	}

	mux_cfg = arch_mux_cfg;

	return 0;
}

/*
 * Sets the Omap MUX and PULL_DWN registers based on the table
 */
int __init_or_module omap_cfg_reg(const unsigned long index)
{
	struct pin_config *reg;

	if (mux_cfg == NULL) {
		printk(KERN_ERR "Pin mux table not initialized\n");
		return -ENODEV;
	}

	if (index >= mux_cfg->size) {
		printk(KERN_ERR "Invalid pin mux index: %lu (%lu)\n",
		       index, mux_cfg->size);
		dump_stack();
		return -ENODEV;
	}

	reg = (struct pin_config *)&mux_cfg->pins[index];

	if (!mux_cfg->cfg_reg)
		return -ENODEV;

	return mux_cfg->cfg_reg(reg);
}
EXPORT_SYMBOL(omap_cfg_reg);
#else
#define omap_mux_init() do {} while(0)
#define omap_cfg_reg(x)	do {} while(0)
#endif	/* CONFIG_OMAP_MUX */
