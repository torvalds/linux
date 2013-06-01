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

	clkspec.args[0] = CGC_BIT_SDIO;
	orion_clkdev_add(NULL, "mvsdio",
			 of_clk_get_from_provider(&clkspec));

	/*
	 * The ethernet interfaces forget the MAC address assigned by
	 * u-boot if the clocks are turned off. Until proper DT support
	 * is available we always enable them for now.
	 */
	clkspec.args[0] = CGC_BIT_GE0;
	clk = of_clk_get_from_provider(&clkspec);
	orion_clkdev_add(NULL, "mv643xx_eth_port.0", clk);
	clk_prepare_enable(clk);

	clkspec.args[0] = CGC_BIT_GE1;
	clk = of_clk_get_from_provider(&clkspec);
	orion_clkdev_add(NULL, "mv643xx_eth_port.1", clk);
	clk_prepare_enable(clk);
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

	kirkwood_setup_wins();

	kirkwood_l2_init();

	kirkwood_cpufreq_init();

	/* Setup root of clk tree */
	kirkwood_of_clk_init();

	kirkwood_cpuidle_init();

#ifdef CONFIG_KEXEC
	kexec_reinit = kirkwood_enable_pcie;
#endif

	if (of_machine_is_compatible("globalscale,dreamplug"))
		dreamplug_init();

	if (of_machine_is_compatible("globalscale,guruplug"))
		guruplug_dt_init();

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

	if (of_machine_is_compatible("lacie,cloudbox") ||
	    of_machine_is_compatible("lacie,inetspace_v2") ||
	    of_machine_is_compatible("lacie,netspace_lite_v2") ||
	    of_machine_is_compatible("lacie,netspace_max_v2") ||
	    of_machine_is_compatible("lacie,netspace_mini_v2") ||
	    of_machine_is_compatible("lacie,netspace_v2"))
		ns2_init();

	if (of_machine_is_compatible("marvell,db-88f6281-bp") ||
	    of_machine_is_compatible("marvell,db-88f6282-bp"))
		db88f628x_init();

	if (of_machine_is_compatible("mpl,cec4"))
		mplcec4_init();

	if (of_machine_is_compatible("netgear,readynas-duo-v2"))
		netgear_readynas_init();

	if (of_machine_is_compatible("plathome,openblocks-a6"))
		openblocks_a6_init();

	if (of_machine_is_compatible("usi,topkick"))
		usi_topkick_init();

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char * const kirkwood_dt_board_compat[] = {
	"globalscale,dreamplug",
	"globalscale,guruplug",
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
	"lacie,cloudbox",
	"lacie,inetspace_v2",
	"lacie,netspace_lite_v2",
	"lacie,netspace_max_v2",
	"lacie,netspace_mini_v2",
	"lacie,netspace_v2",
	"marvell,db-88f6281-bp",
	"marvell,db-88f6282-bp",
	"mpl,cec4",
	"netgear,readynas-duo-v2",
	"plathome,openblocks-a6",
	"usi,topkick",
	"zyxel,nsa310",
	NULL
};

DT_MACHINE_START(KIRKWOOD_DT, "Marvell Kirkwood (Flattened Device Tree)")
	/* Maintainer: Jason Cooper <jason@lakedaemon.net> */
	.map_io		= kirkwood_map_io,
	.init_early	= kirkwood_init_early,
	.init_irq	= orion_dt_init_irq,
	.init_time	= kirkwood_timer_init,
	.init_machine	= kirkwood_dt_init,
	.restart	= kirkwood_restart,
	.dt_compat	= kirkwood_dt_board_compat,
MACHINE_END
