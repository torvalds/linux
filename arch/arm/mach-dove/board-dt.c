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
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_data/usb-ehci-orion.h>
#include <asm/hardware/cache-tauros2.h>
#include <asm/mach/arch.h>
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

	clkspec.args[0] = CLOCK_GATING_BIT_GBE;
	orion_clkdev_add(NULL, "mv643xx_eth_port.0",
			 of_clk_get_from_provider(&clkspec));

	clkspec.args[0] = CLOCK_GATING_BIT_PCIE0;
	orion_clkdev_add("0", "pcie",
			 of_clk_get_from_provider(&clkspec));

	clkspec.args[0] = CLOCK_GATING_BIT_PCIE1;
	orion_clkdev_add("1", "pcie",
			 of_clk_get_from_provider(&clkspec));
}

static void __init dove_of_clk_init(void)
{
	of_clk_init(NULL);
	dove_legacy_clk_init();
}

static struct mv643xx_eth_platform_data dove_dt_ge00_data = {
	.phy_addr = MV643XX_ETH_PHY_ADDR_DEFAULT,
};

static void __init dove_dt_init(void)
{
	pr_info("Dove 88AP510 SoC\n");

#ifdef CONFIG_CACHE_TAUROS2
	tauros2_init(0);
#endif
	dove_setup_cpu_wins();

	/* Setup root of clk tree */
	dove_of_clk_init();

	/* Internal devices not ported to DT yet */
	dove_ge00_init(&dove_dt_ge00_data);
	dove_pcie_init(1, 1);

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char * const dove_dt_board_compat[] = {
	"marvell,dove",
	NULL
};

DT_MACHINE_START(DOVE_DT, "Marvell Dove (Flattened Device Tree)")
	.map_io		= dove_map_io,
	.init_early	= dove_init_early,
	.init_irq	= orion_dt_init_irq,
	.init_time	= dove_timer_init,
	.init_machine	= dove_dt_init,
	.restart	= dove_restart,
	.dt_compat	= dove_dt_board_compat,
MACHINE_END
