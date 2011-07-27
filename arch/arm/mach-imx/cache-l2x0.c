/*
 * Copyright (C) 2009-2010 Pengutronix
 * Sascha Hauer <s.hauer@pengutronix.de>
 * Juergen Beisert <j.beisert@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/err.h>
#include <linux/kernel.h>

#include <asm/hardware/cache-l2x0.h>

#include <mach/hardware.h>

static int mxc_init_l2x0(void)
{
	void __iomem *l2x0_base;
	void __iomem *clkctl_base;

	if (!cpu_is_mx31() && !cpu_is_mx35())
		return 0;

/*
 * First of all, we must repair broken chip settings. There are some
 * i.MX35 CPUs in the wild, comming with bogus L2 cache settings. These
 * misconfigured CPUs will run amok immediately when the L2 cache gets enabled.
 * Workaraound is to setup the correct register setting prior enabling the
 * L2 cache. This should not hurt already working CPUs, as they are using the
 * same value.
 */
#define L2_MEM_VAL 0x10

	clkctl_base = ioremap(MX35_CLKCTL_BASE_ADDR, 4096);
	if (clkctl_base != NULL) {
		writel(0x00000515, clkctl_base + L2_MEM_VAL);
		iounmap(clkctl_base);
	} else {
		pr_err("L2 cache: Cannot fix timing. Trying to continue without\n");
	}

	l2x0_base = ioremap(MX3x_L2CC_BASE_ADDR, 4096);
	if (IS_ERR(l2x0_base)) {
		printk(KERN_ERR "remapping L2 cache area failed with %ld\n",
				PTR_ERR(l2x0_base));
		return 0;
	}

	l2x0_init(l2x0_base, 0x00030024, 0x00000000);

	return 0;
}
arch_initcall(mxc_init_l2x0);
