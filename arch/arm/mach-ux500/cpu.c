/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/hardware/gic.h>
#include <asm/mach/map.h>
#include <asm/localtimer.h>

#include <plat/mtu.h>
#include <mach/hardware.h>
#include <mach/setup.h>
#include <mach/devices.h>
#include <mach/prcmu.h>

#include "clock.h"

static struct map_desc ux500_io_desc[] __initdata = {
	__IO_DEV_DESC(UX500_UART0_BASE, SZ_4K),
	__IO_DEV_DESC(UX500_UART2_BASE, SZ_4K),

	__IO_DEV_DESC(UX500_GIC_CPU_BASE, SZ_4K),
	__IO_DEV_DESC(UX500_GIC_DIST_BASE, SZ_4K),
	__IO_DEV_DESC(UX500_L2CC_BASE, SZ_4K),
	__IO_DEV_DESC(UX500_TWD_BASE, SZ_4K),
	__IO_DEV_DESC(UX500_SCU_BASE, SZ_4K),

	__IO_DEV_DESC(UX500_CLKRST1_BASE, SZ_4K),
	__IO_DEV_DESC(UX500_CLKRST2_BASE, SZ_4K),
	__IO_DEV_DESC(UX500_CLKRST3_BASE, SZ_4K),
	__IO_DEV_DESC(UX500_CLKRST5_BASE, SZ_4K),
	__IO_DEV_DESC(UX500_CLKRST6_BASE, SZ_4K),

	__IO_DEV_DESC(UX500_MTU0_BASE, SZ_4K),
	__IO_DEV_DESC(UX500_MTU1_BASE, SZ_4K),

	__IO_DEV_DESC(UX500_BACKUPRAM0_BASE, SZ_8K),
};

void __init ux500_map_io(void)
{
	iotable_init(ux500_io_desc, ARRAY_SIZE(ux500_io_desc));
}

void __init ux500_init_irq(void)
{
	gic_dist_init(0, __io_address(UX500_GIC_DIST_BASE), 29);
	gic_cpu_init(0, __io_address(UX500_GIC_CPU_BASE));

	/*
	 * Init clocks here so that they are available for system timer
	 * initialization.
	 */
	prcmu_early_init();
	clk_init();
}

#ifdef CONFIG_CACHE_L2X0
static inline void ux500_cache_wait(void __iomem *reg, unsigned long mask)
{
	/* wait for the operation to complete */
	while (readl_relaxed(reg) & mask)
		;
}

static inline void ux500_cache_sync(void)
{
	void __iomem *base = __io_address(UX500_L2CC_BASE);
	writel_relaxed(0, base + L2X0_CACHE_SYNC);
	ux500_cache_wait(base + L2X0_CACHE_SYNC, 1);
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
	void __iomem *l2x0_base = __io_address(UX500_L2CC_BASE);
	uint32_t l2x0_way_mask = (1<<16) - 1;	/* Bitmask of active ways */

	/* invalidate all ways */
	writel_relaxed(l2x0_way_mask, l2x0_base + L2X0_INV_WAY);
	ux500_cache_wait(l2x0_base + L2X0_INV_WAY, l2x0_way_mask);
	ux500_cache_sync();
}

static int ux500_l2x0_init(void)
{
	void __iomem *l2x0_base;

	l2x0_base = __io_address(UX500_L2CC_BASE);

	/* 64KB way size, 8 way associativity, force WA */
	l2x0_init(l2x0_base, 0x3e060000, 0xc0000fff);

	/* Override invalidate function */
	outer_cache.disable = ux500_l2x0_disable;
	outer_cache.inv_all = ux500_l2x0_inv_all;

	return 0;
}
early_initcall(ux500_l2x0_init);
#endif

static void __init ux500_timer_init(void)
{
#ifdef CONFIG_LOCAL_TIMERS
	/* Setup the local timer base */
	twd_base = __io_address(UX500_TWD_BASE);
#endif
	/* Setup the MTU base */
	if (cpu_is_u8500ed())
		mtu_base = __io_address(U8500_MTU0_BASE_ED);
	else
		mtu_base = __io_address(UX500_MTU0_BASE);

	nmdk_timer_init();
}

struct sys_timer ux500_timer = {
	.init	= ux500_timer_init,
};
