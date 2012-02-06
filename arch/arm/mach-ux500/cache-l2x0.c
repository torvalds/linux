/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/io.h>
#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>
#include <mach/hardware.h>
#include <mach/id.h>

static void __iomem *l2x0_base;

static int __init ux500_l2x0_unlock(void)
{
	int i;

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
	return 0;
}

static int __init ux500_l2x0_init(void)
{
	if (cpu_is_u5500())
		l2x0_base = __io_address(U5500_L2CC_BASE);
	else if (cpu_is_u8500())
		l2x0_base = __io_address(U8500_L2CC_BASE);
	else
		ux500_unknown_soc();

	/* Unlock before init */
	ux500_l2x0_unlock();

	/* 64KB way size, 8 way associativity, force WA */
	l2x0_init(l2x0_base, 0x3e060000, 0xc0000fff);

	/*
	 * We can't disable l2 as we are in non secure mode, currently
	 * this seems be called only during kexec path. So let's
	 * override outer.disable with nasty assignment until we have
	 * some SMI service available.
	 */
	outer_cache.disable = NULL;

	return 0;
}

early_initcall(ux500_l2x0_init);
