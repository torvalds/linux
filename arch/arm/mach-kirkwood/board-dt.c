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
#include <linux/of_address.h>
#include <linux/of_net.h>
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

#define MV643XX_ETH_MAC_ADDR_LOW	0x0414
#define MV643XX_ETH_MAC_ADDR_HIGH	0x0418

static void __init kirkwood_dt_eth_fixup(void)
{
	struct device_node *np;

	/*
	 * The ethernet interfaces forget the MAC address assigned by u-boot
	 * if the clocks are turned off. Usually, u-boot on kirkwood boards
	 * has no DT support to properly set local-mac-address property.
	 * As a workaround, we get the MAC address from mv643xx_eth registers
	 * and update the port device node if no valid MAC address is set.
	 */
	for_each_compatible_node(np, NULL, "marvell,kirkwood-eth-port") {
		struct device_node *pnp = of_get_parent(np);
		struct clk *clk;
		struct property *pmac;
		void __iomem *io;
		u8 *macaddr;
		u32 reg;

		if (!pnp)
			continue;

		/* skip disabled nodes or nodes with valid MAC address*/
		if (!of_device_is_available(pnp) || of_get_mac_address(np))
			goto eth_fixup_skip;

		clk = of_clk_get(pnp, 0);
		if (IS_ERR(clk))
			goto eth_fixup_skip;

		io = of_iomap(pnp, 0);
		if (!io)
			goto eth_fixup_no_map;

		/* ensure port clock is not gated to not hang CPU */
		clk_prepare_enable(clk);

		/* store MAC address register contents in local-mac-address */
		pr_err(FW_INFO "%s: local-mac-address is not set\n",
		       np->full_name);

		pmac = kzalloc(sizeof(*pmac) + 6, GFP_KERNEL);
		if (!pmac)
			goto eth_fixup_no_mem;

		pmac->value = pmac + 1;
		pmac->length = 6;
		pmac->name = kstrdup("local-mac-address", GFP_KERNEL);
		if (!pmac->name) {
			kfree(pmac);
			goto eth_fixup_no_mem;
		}

		macaddr = pmac->value;
		reg = readl(io + MV643XX_ETH_MAC_ADDR_HIGH);
		macaddr[0] = (reg >> 24) & 0xff;
		macaddr[1] = (reg >> 16) & 0xff;
		macaddr[2] = (reg >> 8) & 0xff;
		macaddr[3] = reg & 0xff;

		reg = readl(io + MV643XX_ETH_MAC_ADDR_LOW);
		macaddr[4] = (reg >> 8) & 0xff;
		macaddr[5] = reg & 0xff;

		of_update_property(np, pmac);

eth_fixup_no_mem:
		iounmap(io);
		clk_disable_unprepare(clk);
eth_fixup_no_map:
		clk_put(clk);
eth_fixup_skip:
		of_node_put(pnp);
	}
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
	kirkwood_cpuidle_init();
	/* Setup clocks for legacy devices */
	kirkwood_legacy_clk_init();

	kirkwood_pm_init();
	kirkwood_dt_eth_fixup();

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
