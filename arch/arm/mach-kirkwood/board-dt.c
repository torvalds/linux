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
#include <linux/kexec.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/bridge-regs.h>
#include <plat/irq.h>
#include "common.h"

static struct of_device_id kirkwood_dt_match_table[] __initdata = {
	{ .compatible = "simple-bus", },
	{ }
};

struct of_dev_auxdata kirkwood_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("marvell,orion-spi", 0xf1010600, "orion_spi.0", NULL),
	OF_DEV_AUXDATA("marvell,mv64xxx-i2c", 0xf1011000, "mv64xxx_i2c.0",
		       NULL),
	OF_DEV_AUXDATA("marvell,orion-wdt", 0xf1020300, "orion_wdt", NULL),
	OF_DEV_AUXDATA("marvell,orion-sata", 0xf1080000, "sata_mv.0", NULL),
	OF_DEV_AUXDATA("marvell,orion-nand", 0xf4000000, "orion_nand", NULL),
	OF_DEV_AUXDATA("marvell,orion-crypto", 0xf1030000, "mv_crypto", NULL),
	{},
};

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
	kirkwood_clk_init();

	/* internal devices that every board has */
	kirkwood_xor0_init();
	kirkwood_xor1_init();

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

	of_platform_populate(NULL, kirkwood_dt_match_table,
			     kirkwood_auxdata_lookup, NULL);
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
