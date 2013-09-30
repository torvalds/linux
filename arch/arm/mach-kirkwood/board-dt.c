/*
 * Copyright 2012 (C), Jason Cooper <jason@lakedaemon.net>
 *
 * arch/arm/mach-kirkwood/board-dt.c
 *
 * Flattened Device Tree board initialization
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/clk-provider.h>
#include <linux/clocksource.h>
#include <linux/dma-mapping.h>
#include <linux/irqchip.h>
#include <linux/kexec.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/bridge-regs.h>
#include <linux/platform_data/usb-ehci-orion.h>
#include <plat/irq.h>
#include <plat/common.h>
#include "common.h"

/*
 * There are still devices that doesn't know about DT yet.  Get clock
 * gates here and add a clock lookup alias, so that old platform
 * devices still work.
*/

static void __init kirkwood_legacy_clk_init(void)
{

	struct device_node *np = of_find_compatible_node(
		NULL, NULL, "marvell,kirkwood-gating-clock");
	struct of_phandle_args clkspec;
	struct clk *clk;

	clkspec.np = np;
	clkspec.args_count = 1;

	clkspec.args[0] = CGC_BIT_PEX0;
	orion_clkdev_add("0", "pcie",
			 of_clk_get_from_provider(&clkspec));

	clkspec.args[0] = CGC_BIT_PEX1;
	orion_clkdev_add("1", "pcie",
			 of_clk_get_from_provider(&clkspec));

	/*
	 * The ethernet interfaces forget the MAC address assigned by
	 * u-boot if the clocks are turned off. Until proper DT support
	 * is available we always enable them for now.
	 */
	clkspec.args[0] = CGC_BIT_GE0;
	clk = of_clk_get_from_provider(&clkspec);
	clk_prepare_enable(clk);

	clkspec.args[0] = CGC_BIT_GE1;
	clk = of_clk_get_from_provider(&clkspec);
	clk_prepare_enable(clk);
}

static void __init kirkwood_dt_time_init(void)
{
	of_clk_init(NULL);
	clocksource_of_init();
}

static void __init kirkwood_dt_init(void)
{
	pr_info("Kirkwood: %s, TCLK=%d.\n", kirkwood_id(), kirkwood_tclk);

	/*
	 * Disable propagation of mbus errors to the CPU local bus,
	 * as this causes mbus errors (which can occur for example
	 * for PCI aborts) to throw CPU aborts, which we're not set
	 * up to deal with.
	 */
	writel(readl(CPU_CONFIG) & ~CPU_CONFIG_ERROR_PROP, CPU_CONFIG);

	BUG_ON(mvebu_mbus_dt_init());
	kirkwood_setup_wins();

	kirkwood_l2_init();

	kirkwood_cpufreq_init();

	/* Setup clocks for legacy devices */
	kirkwood_legacy_clk_init();

	kirkwood_pm_init();
	kirkwood_cpuidle_init();

#ifdef CONFIG_KEXEC
	kexec_reinit = kirkwood_enable_pcie;
#endif

	if (of_machine_is_compatible("marvell,mv88f6281gtw-ge"))
		mv88f6281gtw_ge_init();

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char * const kirkwood_dt_board_compat[] = {
	"marvell,kirkwood",
	NULL
};

DT_MACHINE_START(KIRKWOOD_DT, "Marvell Kirkwood (Flattened Device Tree)")
	/* Maintainer: Jason Cooper <jason@lakedaemon.net> */
	.map_io		= kirkwood_map_io,
	.init_time	= kirkwood_dt_time_init,
	.init_machine	= kirkwood_dt_init,
	.restart	= kirkwood_restart,
	.dt_compat	= kirkwood_dt_board_compat,
MACHINE_END
