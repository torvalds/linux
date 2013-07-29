/*
 * arch/arm/mach-dove/board-dt.c
 *
 * Marvell Dove 88AP510 System On Chip FDT Board
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/clk-provider.h>
#include <linux/clocksource.h>
#include <linux/irqchip.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_data/usb-ehci-orion.h>
#include <asm/hardware/cache-tauros2.h>
#include <asm/mach/arch.h>
#include <mach/dove.h>
#include <mach/pm.h>
#include <plat/common.h>
#include <plat/irq.h>
#include "common.h"

/*
 * There are still devices that doesn't even know about DT,
 * get clock gates here and add a clock lookup.
 */
static void __init dove_legacy_clk_init(void)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL,
					 "marvell,dove-gating-clock");
	struct of_phandle_args clkspec;

	clkspec.np = np;
	clkspec.args_count = 1;

	clkspec.args[0] = CLOCK_GATING_BIT_PCIE0;
	orion_clkdev_add("0", "pcie",
			 of_clk_get_from_provider(&clkspec));

	clkspec.args[0] = CLOCK_GATING_BIT_PCIE1;
	orion_clkdev_add("1", "pcie",
			 of_clk_get_from_provider(&clkspec));
}

static void __init dove_dt_time_init(void)
{
	of_clk_init(NULL);
	clocksource_of_init();
}

static void __init dove_dt_init(void)
{
	pr_info("Dove 88AP510 SoC\n");

#ifdef CONFIG_CACHE_TAUROS2
	tauros2_init(0);
#endif
	BUG_ON(mvebu_mbus_dt_init());

	/* Setup clocks for legacy devices */
	dove_legacy_clk_init();

	/* Internal devices not ported to DT yet */
	dove_pcie_init(1, 1);

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char * const dove_dt_board_compat[] = {
	"marvell,dove",
	NULL
};

DT_MACHINE_START(DOVE_DT, "Marvell Dove (Flattened Device Tree)")
	.map_io		= dove_map_io,
	.init_time	= dove_dt_time_init,
	.init_machine	= dove_dt_init,
	.restart	= dove_restart,
	.dt_compat	= dove_dt_board_compat,
MACHINE_END
