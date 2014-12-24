/*
 * Copyright 2014 Freescale Semiconductor, Inc.
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
#include <linux/fec.h>
#include <linux/netdevice.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"
#include "cpuidle.h"

static struct fec_platform_data fec_pdata[2];

static void imx6sx_fec1_sleep_enable(int enabled)
{
	struct regmap *gpr;

	gpr = syscon_regmap_lookup_by_compatible("fsl,imx6sx-iomuxc-gpr");
	if (!IS_ERR(gpr)) {
		if (enabled)
			regmap_update_bits(gpr, IOMUXC_GPR4,
					   IMX6SX_GPR4_FEC_ENET1_STOP_REQ,
					   IMX6SX_GPR4_FEC_ENET1_STOP_REQ);
		else
			regmap_update_bits(gpr, IOMUXC_GPR4,
					   IMX6SX_GPR4_FEC_ENET1_STOP_REQ, 0);
	} else
		pr_err("failed to find fsl,imx6sx-iomux-gpr regmap\n");
}

static void imx6sx_fec2_sleep_enable(int enabled)
{
	struct regmap *gpr;

	gpr = syscon_regmap_lookup_by_compatible("fsl,imx6sx-iomuxc-gpr");
	if (!IS_ERR(gpr)) {
		if (enabled)
			regmap_update_bits(gpr, IOMUXC_GPR4,
					   IMX6SX_GPR4_FEC_ENET2_STOP_REQ,
					   IMX6SX_GPR4_FEC_ENET2_STOP_REQ);
		else
			regmap_update_bits(gpr, IOMUXC_GPR4,
					   IMX6SX_GPR4_FEC_ENET2_STOP_REQ, 0);
	} else
		pr_err("failed to find fsl,imx6sx-iomux-gpr regmap\n");
}

static void __init imx6sx_enet_plt_init(void)
{
	struct device_node *np;

	np = of_find_node_by_path("/soc/aips-bus@02100000/ethernet@02188000");
	if (np && of_get_property(np, "fsl,magic-packet", NULL))
		fec_pdata[0].sleep_mode_enable = imx6sx_fec1_sleep_enable;
	np = of_find_node_by_path("/soc/aips-bus@02100000/ethernet@021b4000");
	if (np && of_get_property(np, "fsl,magic-packet", NULL))
		fec_pdata[1].sleep_mode_enable = imx6sx_fec2_sleep_enable;
}

static int ar8031_phy_fixup(struct phy_device *dev)
{
	u16 val;

	/* Set RGMII IO voltage to 1.8V */
	phy_write(dev, 0x1d, 0x1f);
	phy_write(dev, 0x1e, 0x8);

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
	imx_init_revision_from_anatop();
	imx_init_l2cache();
	imx_src_init();
	imx_gpc_init();
	irqchip_init();
}

static void __init imx6sx_init_late(void)
{
	imx6q_cpuidle_init();

	if (IS_ENABLED(CONFIG_ARM_IMX6Q_CPUFREQ))
		platform_device_register_simple("imx6q-cpufreq", -1, NULL, 0);
}

static const char * const imx6sx_dt_compat[] __initconst = {
	"fsl,imx6sx",
	NULL,
};

DT_MACHINE_START(IMX6SX, "Freescale i.MX6 SoloX (Device Tree)")
	.init_irq	= imx6sx_init_irq,
	.init_machine	= imx6sx_init_machine,
	.dt_compat	= imx6sx_dt_compat,
	.init_late	= imx6sx_init_late,
MACHINE_END
