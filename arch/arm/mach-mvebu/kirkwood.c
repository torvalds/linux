/*
 * Copyright 2012 (C), Jason Cooper <jason@lakedaemon.net>
 *
 * arch/arm/mach-mvebu/kirkwood.c
 *
 * Flattened Device Tree board initialization
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mbus.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <asm/hardware/cache-feroceon-l2.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include "kirkwood.h"
#include "kirkwood-pm.h"
#include "common.h"
#include "board.h"

static struct resource kirkwood_cpufreq_resources[] = {
	[0] = {
		.start  = CPU_CONTROL_PHYS,
		.end    = CPU_CONTROL_PHYS + 3,
		.flags  = IORESOURCE_MEM,
	},
};

static struct platform_device kirkwood_cpufreq_device = {
	.name		= "kirkwood-cpufreq",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(kirkwood_cpufreq_resources),
	.resource	= kirkwood_cpufreq_resources,
};

static void __init kirkwood_cpufreq_init(void)
{
	platform_device_register(&kirkwood_cpufreq_device);
}

static struct resource kirkwood_cpuidle_resource[] = {
	{
		.flags	= IORESOURCE_MEM,
		.start	= DDR_OPERATION_BASE,
		.end	= DDR_OPERATION_BASE + 3,
	},
};

static struct platform_device kirkwood_cpuidle = {
	.name		= "kirkwood_cpuidle",
	.id		= -1,
	.resource	= kirkwood_cpuidle_resource,
	.num_resources	= 1,
};

static void __init kirkwood_cpuidle_init(void)
{
	platform_device_register(&kirkwood_cpuidle);
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

/*
 * Disable propagation of mbus errors to the CPU local bus, as this
 * causes mbus errors (which can occur for example for PCI aborts) to
 * throw CPU aborts, which we're not set up to deal with.
 */
void kirkwood_disable_mbus_error_propagation(void)
{
	void __iomem *cpu_config;

	cpu_config = ioremap(CPU_CONFIG_PHYS, 4);
	writel(readl(cpu_config) & ~CPU_CONFIG_ERROR_PROP, cpu_config);
}

static struct of_dev_auxdata auxdata[] __initdata = {
	OF_DEV_AUXDATA("marvell,kirkwood-audio", 0xf10a0000,
		       "mvebu-audio", NULL),
	{ /* sentinel */ }
};

static void __init kirkwood_dt_init(void)
{
	kirkwood_disable_mbus_error_propagation();

	BUG_ON(mvebu_mbus_dt_init(false));

#ifdef CONFIG_CACHE_FEROCEON_L2
	feroceon_of_init();
#endif
	kirkwood_cpufreq_init();
	kirkwood_cpuidle_init();

	kirkwood_pm_init();
	kirkwood_dt_eth_fixup();

	if (of_machine_is_compatible("lacie,netxbig"))
		netxbig_init();

	of_platform_populate(NULL, of_default_bus_match_table, auxdata, NULL);
}

static const char * const kirkwood_dt_board_compat[] = {
	"marvell,kirkwood",
	NULL
};

DT_MACHINE_START(KIRKWOOD_DT, "Marvell Kirkwood (Flattened Device Tree)")
	/* Maintainer: Jason Cooper <jason@lakedaemon.net> */
	.init_machine	= kirkwood_dt_init,
	.restart	= mvebu_restart,
	.dt_compat	= kirkwood_dt_board_compat,
MACHINE_END
