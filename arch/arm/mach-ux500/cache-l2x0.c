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

static inline void ux500_cache_wait(void __iomem *reg, unsigned long mask)
{
	/* wait for the operation to complete */
	while (readl_relaxed(reg) & mask)
		cpu_relax();
}

static inline void ux500_cache_sync(void)
{
	writel_relaxed(0, l2x0_base + L2X0_CACHE_SYNC);
	ux500_cache_wait(l2x0_base + L2X0_CACHE_SYNC, 1);
}

/*
 * The L2 cache cannot be turned off in the non-secure world.
 * Dummy until a secure service is in place.
 */
static void ux500_l2x0_disable(void)
{
}

/*
 * This is only called when doing a kexec, just after turning off the L2
 * and L1 cache, and it is surrounded by a spinlock in the generic version.
 * However, we're not really turning off the L2 cache right now and the
 * PL310 does not support exclusive accesses (used to implement the spinlock).
 * So, the invalidation needs to be done without the spinlock.
 */
static void ux500_l2x0_inv_all(void)
{
	uint32_t l2x0_way_mask = (1<<16) - 1;	/* Bitmask of active ways */

	/* invalidate all ways */
	writel_relaxed(l2x0_way_mask, l2x0_base + L2X0_INV_WAY);
	ux500_cache_wait(l2x0_base + L2X0_INV_WAY, l2x0_way_mask);
	ux500_cache_sync();
}

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

	/* Override invalidate function */
	outer_cache.disable = ux500_l2x0_disable;
	outer_cache.inv_all = ux500_l2x0_inv_all;

	return 0;
}

early_initcall(ux500_l2x0_init);
