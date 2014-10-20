/*
 * Copyright 2014-2015 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/irqchip.h>
#include <linux/of_platform.h>
#include <linux/phy.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx6q-iomuxc-gpr.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"
#include "cpuidle.h"

static int ar8031_phy_fixup(struct phy_device *dev)
{
	u16 val;

	/* Set RGMII IO voltage to 1.8V */
	phy_write(dev, 0x1d, 0x1f);
	phy_write(dev, 0x1e, 0x8);

	/* disable phy AR8031 SmartEEE function. */
	phy_write(dev, 0xd, 0x3);
	phy_write(dev, 0xe, 0x805d);
	phy_write(dev, 0xd, 0x4003);
	val = phy_read(dev, 0xe);
	val &= ~(0x1 << 8);
	phy_write(dev, 0xe, val);

	/* introduce tx clock delay */
	phy_write(dev, 0x1d, 0x5);
	val = phy_read(dev, 0x1e);
	val |= 0x0100;
	phy_write(dev, 0x1e, val);

	return 0;
}

#define PHY_ID_AR8031   0x004dd074
static void __init imx6sx_enet_phy_init(void)
{
	if (IS_BUILTIN(CONFIG_PHYLIB))
		phy_register_fixup_for_uid(PHY_ID_AR8031, 0xffffffff,
					   ar8031_phy_fixup);
}

static void __init imx6sx_enet_clk_sel(void)
{
	struct regmap *gpr;

	gpr = syscon_regmap_lookup_by_compatible("fsl,imx6sx-iomuxc-gpr");
	if (!IS_ERR(gpr)) {
		regmap_update_bits(gpr, IOMUXC_GPR1,
				   IMX6SX_GPR1_FEC_CLOCK_MUX_SEL_MASK, 0);
		regmap_update_bits(gpr, IOMUXC_GPR1,
				   IMX6SX_GPR1_FEC_CLOCK_PAD_DIR_MASK, 0);
	} else {
		pr_err("failed to find fsl,imx6sx-iomux-gpr regmap\n");
	}
}

static inline void imx6sx_enet_init(void)
{
	imx6_enet_mac_init("fsl,imx6sx-fec", "fsl,imx6sx-ocotp");
	imx6sx_enet_phy_init();
	imx6sx_enet_clk_sel();
}

static void __init imx6sx_init_machine(void)
{
	struct device *parent;

	parent = imx_soc_device_init();
	if (parent == NULL)
		pr_warn("failed to initialize soc device\n");

	of_platform_populate(NULL, of_default_bus_match_table, NULL, parent);

	imx6sx_enet_init();
	imx_anatop_init();
	imx6sx_pm_init();
}

static void __init imx6sx_init_irq(void)
{
	imx_gpc_check_dt();
	imx_init_revision_from_anatop();
	imx_init_l2cache();
	imx_src_init();
	irqchip_init();
}

static void __init imx6sx_init_late(void)
{
	if (IS_ENABLED(CONFIG_ARM_IMX6Q_CPUFREQ))
		platform_device_register_simple("imx6q-cpufreq", -1, NULL, 0);

	imx6sx_cpuidle_init();
}

static void __init imx6sx_map_io(void)
{
	debug_ll_io_init();
	imx6_pm_map_io();
	imx_busfreq_map_io();
}

static const char * const imx6sx_dt_compat[] __initconst = {
	"fsl,imx6sx",
	NULL,
};

DT_MACHINE_START(IMX6SX, "Freescale i.MX6 SoloX (Device Tree)")
	.map_io		= imx6sx_map_io,
	.init_irq	= imx6sx_init_irq,
	.init_machine	= imx6sx_init_machine,
	.dt_compat	= imx6sx_dt_compat,
	.init_late	= imx6sx_init_late,
MACHINE_END
