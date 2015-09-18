/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <asm/outercache.h>
#include <asm/hardware/cache-l2x0.h>

#include "db8500-regs.h"
#include "id.h"

static int __init ux500_l2x0_unlock(void)
{
	int i;
	struct device_node *np;
	void __iomem *l2x0_base;

	np = of_find_compatible_node(NULL, NULL, "arm,pl310-cache");
	l2x0_base = of_iomap(np, 0);
	of_node_put(np);
	if (!l2x0_base)
		return -ENODEV;

	/*
	 * Unlock Data and Instruction Lock if locked. Ux500 U-Boot versions
	 * apparently locks both caches before jumping to the kernel. The
	 * l2x0 core will not touch the unlock registers if the l2x0 is
	 * already enabled, so we do it right here instead. The PL310 has
	 * 8 sets of registers, one per possible CPU.
	 */
	for (i = 0; i < 8; i++) {
		writel_relaxed(0x0, l2x0_base + L2X0_LOCKDOWN_WAY_D_BASE +
			       i * L2X0_LOCKDOWN_STRIDE);
		writel_relaxed(0x0, l2x0_base + L2X0_LOCKDOWN_WAY_I_BASE +
			       i * L2X0_LOCKDOWN_STRIDE);
	}
	iounmap(l2x0_base);
	return 0;
}

static void ux500_l2c310_write_sec(unsigned long val, unsigned reg)
{
	/*
	 * We can't write to secure registers as we are in non-secure
	 * mode, until we have some SMI service available.
	 */
}

static int __init ux500_l2x0_init(void)
{
	/* Multiplatform guard */
	if (!((cpu_is_u8500_family() || cpu_is_ux540_family())))
		return -ENODEV;

	/* Unlock before init */
	ux500_l2x0_unlock();
	outer_cache.write_sec = ux500_l2c310_write_sec;
	l2x0_of_init(0, ~0);

	return 0;
}
early_initcall(ux500_l2x0_init);
