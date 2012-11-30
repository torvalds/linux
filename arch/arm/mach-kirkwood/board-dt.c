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
#include <linux/clk/mvebu.h>
#include <linux/kexec.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/bridge-regs.h>
#include <linux/platform_data/usb-ehci-orion.h>
#include <plat/irq.h>
#include <plat/common.h>
#include "common.h"

static struct of_device_id kirkwood_dt_match_table[] __initdata = {
	{ .compatible = "simple-bus", },
	{ }
};

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

	clkspec.np = np;
	clkspec.args_count = 1;

	clkspec.args[0] = CGC_BIT_GE0;
	orion_clkdev_add(NULL, "mv643xx_eth_port.0",
			 of_clk_get_from_provider(&clkspec));

	clkspec.args[0] = CGC_BIT_PEX0;
	orion_clkdev_add("0", "pcie",
			 of_clk_get_from_provider(&clkspec));

	clkspec.args[0] = CGC_BIT_USB0;
	orion_clkdev_add(NULL, "orion-ehci.0",
			 of_clk_get_from_provider(&clkspec));

	clkspec.args[0] = CGC_BIT_PEX1;
	orion_clkdev_add("1", "pcie",
			 of_clk_get_from_provider(&clkspec));

	clkspec.args[0] = CGC_BIT_GE1;
	orion_clkdev_add(NULL, "mv643xx_eth_port.1",
			 of_clk_get_from_provider(&clkspec));

}

static void __init kirkwood_of_clk_init(void)
{
	mvebu_clocks_init();
	kirkwood_legacy_clk_init();
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

	kirkwood_setup_cpu_mbus();

	kirkwood_l2_init();

	/* Setup root of clk tree */
	kirkwood_of_clk_init();

#ifdef CONFIG_KEXEC
	kexec_reinit = kirkwood_enable_pcie;
#endif

	if (of_machine_is_compatible("globalscale,dreamplug"))
		dreamplug_init();

	if (of_machine_is_compatible("dlink,dns-kirkwood"))
		dnskw_init();

	if (of_machine_is_compatible("iom,iconnect"))
		iconnect_init();

	if (of_machine_is_compatible("raidsonic,ib-nas62x0"))
		ib62x0_init();

	if (of_machine_is_compatible("qnap,ts219"))
		qnap_dt_ts219_init();

	if (of_machine_is_compatible("seagate,dockstar"))
		dockstar_dt_init();

	if (of_machine_is_compatible("seagate,goflexnet"))
		goflexnet_init();

	if (of_machine_is_compatible("buffalo,lsxl"))
		lsxl_init();

	if (of_machine_is_compatible("iom,ix2-200"))
		iomega_ix2_200_init();

	if (of_machine_is_compatible("keymile,km_kirkwood"))
		km_kirkwood_init();

	of_platform_populate(NULL, kirkwood_dt_match_table, NULL, NULL);
}

static const char *kirkwood_dt_board_compat[] = {
	"globalscale,dreamplug",
	"dlink,dns-320",
	"dlink,dns-325",
	"iom,iconnect",
	"raidsonic,ib-nas62x0",
	"qnap,ts219",
	"seagate,dockstar",
	"seagate,goflexnet",
	"buffalo,lsxl",
	"iom,ix2-200",
	"keymile,km_kirkwood",
	NULL
};

DT_MACHINE_START(KIRKWOOD_DT, "Marvell Kirkwood (Flattened Device Tree)")
	/* Maintainer: Jason Cooper <jason@lakedaemon.net> */
	.map_io		= kirkwood_map_io,
	.init_early	= kirkwood_init_early,
	.init_irq	= orion_dt_init_irq,
	.timer		= &kirkwood_timer,
	.init_machine	= kirkwood_dt_init,
	.restart	= kirkwood_restart,
	.dt_compat	= kirkwood_dt_board_compat,
MACHINE_END
