/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/irqchip.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx7-iomuxc-gpr.h>
#include <linux/of_platform.h>
#include <linux/phy.h>
#include <linux/regmap.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"

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

static int bcm54220_phy_fixup(struct phy_device *dev)
{
	/* enable RXC skew select RGMII copper mode */
	phy_write(dev, 0x1e, 0x21);
	phy_write(dev, 0x1f, 0x7ea8);
	phy_write(dev, 0x1e, 0x2f);
	phy_write(dev, 0x1f, 0x71b7);

	return 0;
}

#define PHY_ID_AR8031	0x004dd074
#define PHY_ID_BCM54220	0x600d8589

static void __init imx7d_enet_phy_init(void)
{
	if (IS_BUILTIN(CONFIG_PHYLIB)) {
		phy_register_fixup_for_uid(PHY_ID_AR8031, 0xffffffff,
					   ar8031_phy_fixup);
		phy_register_fixup_for_uid(PHY_ID_BCM54220, 0xffffffff,
					   bcm54220_phy_fixup);
	}
}

static void __init imx7d_enet_clk_sel(void)
{
	struct regmap *gpr;

	gpr = syscon_regmap_lookup_by_compatible("fsl,imx7d-iomuxc-gpr");
	if (!IS_ERR(gpr)) {
		regmap_update_bits(gpr, IOMUXC_GPR1, IMX7D_GPR1_ENET_TX_CLK_SEL_MASK, 0);
		regmap_update_bits(gpr, IOMUXC_GPR1, IMX7D_GPR1_ENET_CLK_DIR_MASK, 0);
	} else {
		pr_err("failed to find fsl,imx7d-iomux-gpr regmap\n");
	}
}

static inline void imx7d_enet_init(void)
{
	imx7d_enet_phy_init();
	imx7d_enet_clk_sel();
}

static void __init imx7d_init_machine(void)
{
	struct device *parent;

	parent = imx_soc_device_init();
	if (parent == NULL)
		pr_warn("failed to initialize soc device\n");

	imx_anatop_init();
	imx7d_enet_init();
}

static void __init imx7d_init_irq(void)
{
	imx_init_revision_from_anatop();
	imx_src_init();
	irqchip_init();
}

static const char *const imx7d_dt_compat[] __initconst = {
	"fsl,imx7d",
	"fsl,imx7s",
	NULL,
};

DT_MACHINE_START(IMX7D, "Freescale i.MX7 Dual (Device Tree)")
	.init_irq	= imx7d_init_irq,
	.init_machine	= imx7d_init_machine,
	.dt_compat	= imx7d_dt_compat,
MACHINE_END
