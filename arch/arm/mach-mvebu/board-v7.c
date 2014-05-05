/*
 * Device Tree support for Armada 370 and XP platforms.
 *
 * Copyright (C) 2012 Marvell
 *
 * Lior Amsalem <alior@marvell.com>
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/clocksource.h>
#include <linux/dma-mapping.h>
#include <linux/mbus.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include "armada-370-xp.h"
#include "common.h"
#include "coherency.h"
#include "mvebu-soc-id.h"

/*
 * Early versions of Armada 375 SoC have a bug where the BootROM
 * leaves an external data abort pending. The kernel is hit by this
 * data abort as soon as it enters userspace, because it unmasks the
 * data aborts at this moment. We register a custom abort handler
 * below to ignore the first data abort to work around this
 * problem.
 */
static int armada_375_external_abort_wa(unsigned long addr, unsigned int fsr,
					struct pt_regs *regs)
{
	static int ignore_first;

	if (!ignore_first && fsr == 0x1406) {
		ignore_first = 1;
		return 0;
	}

	return 1;
}

static void __init mvebu_timer_and_clk_init(void)
{
	of_clk_init(NULL);
	clocksource_of_init();
	coherency_init();
	BUG_ON(mvebu_mbus_dt_init());
#ifdef CONFIG_CACHE_L2X0
	l2x0_of_init(0, ~0UL);
#endif

	if (of_machine_is_compatible("marvell,armada375"))
		hook_fault_code(16 + 6, armada_375_external_abort_wa, SIGBUS, 0,
				"imprecise external abort");
}

static void __init i2c_quirk(void)
{
	struct device_node *np;
	u32 dev, rev;

	/*
	 * Only revisons more recent than A0 support the offload
	 * mechanism. We can exit only if we are sure that we can
	 * get the SoC revision and it is more recent than A0.
	 */
	if (mvebu_get_soc_id(&rev, &dev) == 0 && dev > MV78XX0_A0_REV)
		return;

	for_each_compatible_node(np, NULL, "marvell,mv78230-i2c") {
		struct property *new_compat;

		new_compat = kzalloc(sizeof(*new_compat), GFP_KERNEL);

		new_compat->name = kstrdup("compatible", GFP_KERNEL);
		new_compat->length = sizeof("marvell,mv78230-a0-i2c");
		new_compat->value = kstrdup("marvell,mv78230-a0-i2c",
						GFP_KERNEL);

		of_update_property(np, new_compat);
	}
	return;
}

static void __init mvebu_dt_init(void)
{
	if (of_machine_is_compatible("plathome,openblocks-ax3-4"))
		i2c_quirk();
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char * const armada_370_xp_dt_compat[] = {
	"marvell,armada-370-xp",
	NULL,
};

DT_MACHINE_START(ARMADA_370_XP_DT, "Marvell Armada 370/XP (Device Tree)")
	.smp		= smp_ops(armada_xp_smp_ops),
	.init_machine	= mvebu_dt_init,
	.init_time	= mvebu_timer_and_clk_init,
	.restart	= mvebu_restart,
	.dt_compat	= armada_370_xp_dt_compat,
MACHINE_END

static const char * const armada_375_dt_compat[] = {
	"marvell,armada375",
	NULL,
};

DT_MACHINE_START(ARMADA_375_DT, "Marvell Armada 375 (Device Tree)")
	.init_time	= mvebu_timer_and_clk_init,
	.restart	= mvebu_restart,
	.dt_compat	= armada_375_dt_compat,
MACHINE_END

static const char * const armada_38x_dt_compat[] = {
	"marvell,armada380",
	"marvell,armada385",
	NULL,
};

DT_MACHINE_START(ARMADA_38X_DT, "Marvell Armada 380/385 (Device Tree)")
	.init_time	= mvebu_timer_and_clk_init,
	.restart	= mvebu_restart,
	.dt_compat	= armada_38x_dt_compat,
MACHINE_END
