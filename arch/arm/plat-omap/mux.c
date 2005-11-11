/*
 * linux/arch/arm/plat-omap/mux.c
 *
 * Utility to set the Omap MUX and PULL_DWN registers from a table in mux.h
 *
 * Copyright (C) 2003 - 2005 Nokia Corporation
 *
 * Written by Tony Lindgren <tony.lindgren@nokia.com>
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
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/io.h>
#include <linux/spinlock.h>
#include <asm/arch/mux.h>

#ifdef CONFIG_OMAP_MUX

#define OMAP24XX_L4_BASE	0x48000000
#define OMAP24XX_PULL_ENA	(1 << 3)
#define OMAP24XX_PULL_UP	(1 << 4)

static struct pin_config * pin_table;
static unsigned long pin_table_sz;

extern struct pin_config * omap730_pins;
extern struct pin_config * omap1xxx_pins;
extern struct pin_config * omap24xx_pins;

int __init omap_mux_register(struct pin_config * pins, unsigned long size)
{
	pin_table = pins;
	pin_table_sz = size;

	return 0;
}

/*
 * Sets the Omap MUX and PULL_DWN registers based on the table
 */
int __init_or_module omap_cfg_reg(const unsigned long index)
{
	static DEFINE_SPINLOCK(mux_spin_lock);

	unsigned long flags;
	struct pin_config *cfg;
	unsigned int reg_orig = 0, reg = 0, pu_pd_orig = 0, pu_pd = 0,
		pull_orig = 0, pull = 0;
	unsigned int mask, warn = 0;

	if (!pin_table)
		BUG();

	if (index >= pin_table_sz) {
		printk(KERN_ERR "Invalid pin mux index: %lu (%lu)\n",
		       index, pin_table_sz);
		dump_stack();
		return -ENODEV;
	}

	cfg = (struct pin_config *)&pin_table[index];
	if (cpu_is_omap24xx()) {
		u8 reg = 0;

		reg |= cfg->mask & 0x7;
		if (cfg->pull_val)
			reg |= OMAP24XX_PULL_ENA;
		if(cfg->pu_pd_val)
			reg |= OMAP24XX_PULL_UP;
#ifdef CONFIG_OMAP_MUX_DEBUG
		printk("Muxing %s (0x%08x): 0x%02x -> 0x%02x\n",
		       cfg->name, OMAP24XX_L4_BASE + cfg->mux_reg,
		       omap_readb(OMAP24XX_L4_BASE + cfg->mux_reg), reg);
#endif
		omap_writeb(reg, OMAP24XX_L4_BASE + cfg->mux_reg);

		return 0;
	}

	/* Check the mux register in question */
	if (cfg->mux_reg) {
		unsigned	tmp1, tmp2;

		spin_lock_irqsave(&mux_spin_lock, flags);
		reg_orig = omap_readl(cfg->mux_reg);

		/* The mux registers always seem to be 3 bits long */
		mask = (0x7 << cfg->mask_offset);
		tmp1 = reg_orig & mask;
		reg = reg_orig & ~mask;

		tmp2 = (cfg->mask << cfg->mask_offset);
		reg |= tmp2;

		if (tmp1 != tmp2)
			warn = 1;

		omap_writel(reg, cfg->mux_reg);
		spin_unlock_irqrestore(&mux_spin_lock, flags);
	}

	/* Check for pull up or pull down selection on 1610 */
	if (!cpu_is_omap1510()) {
		if (cfg->pu_pd_reg && cfg->pull_val) {
			spin_lock_irqsave(&mux_spin_lock, flags);
			pu_pd_orig = omap_readl(cfg->pu_pd_reg);
			mask = 1 << cfg->pull_bit;

			if (cfg->pu_pd_val) {
				if (!(pu_pd_orig & mask))
					warn = 1;
				/* Use pull up */
				pu_pd = pu_pd_orig | mask;
			} else {
				if (pu_pd_orig & mask)
					warn = 1;
				/* Use pull down */
				pu_pd = pu_pd_orig & ~mask;
			}
			omap_writel(pu_pd, cfg->pu_pd_reg);
			spin_unlock_irqrestore(&mux_spin_lock, flags);
		}
	}

	/* Check for an associated pull down register */
	if (cfg->pull_reg) {
		spin_lock_irqsave(&mux_spin_lock, flags);
		pull_orig = omap_readl(cfg->pull_reg);
		mask = 1 << cfg->pull_bit;

		if (cfg->pull_val) {
			if (pull_orig & mask)
				warn = 1;
			/* Low bit = pull enabled */
			pull = pull_orig & ~mask;
		} else {
			if (!(pull_orig & mask))
				warn = 1;
			/* High bit = pull disabled */
			pull = pull_orig | mask;
		}

		omap_writel(pull, cfg->pull_reg);
		spin_unlock_irqrestore(&mux_spin_lock, flags);
	}

	if (warn) {
#ifdef CONFIG_OMAP_MUX_WARNINGS
		printk(KERN_WARNING "MUX: initialized %s\n", cfg->name);
#endif
	}

#ifdef CONFIG_OMAP_MUX_DEBUG
	if (cfg->debug || warn) {
		printk("MUX: Setting register %s\n", cfg->name);
		printk("      %s (0x%08x) = 0x%08x -> 0x%08x\n",
		       cfg->mux_reg_name, cfg->mux_reg, reg_orig, reg);

		if (!cpu_is_omap1510()) {
			if (cfg->pu_pd_reg && cfg->pull_val) {
				printk("      %s (0x%08x) = 0x%08x -> 0x%08x\n",
				       cfg->pu_pd_name, cfg->pu_pd_reg,
				       pu_pd_orig, pu_pd);
			}
		}

		if (cfg->pull_reg)
			printk("      %s (0x%08x) = 0x%08x -> 0x%08x\n",
			       cfg->pull_name, cfg->pull_reg, pull_orig, pull);
	}
#endif

#ifdef CONFIG_OMAP_MUX_ERRORS
	return warn ? -ETXTBSY : 0;
#else
	return 0;
#endif
}
EXPORT_SYMBOL(omap_cfg_reg);
#else
#define omap_mux_init() do {} while(0)
#define omap_cfg_reg(x)	do {} while(0)
#endif	/* CONFIG_OMAP_MUX */
