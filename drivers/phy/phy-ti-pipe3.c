/*
 * phy-ti-pipe3 - PIPE3 PHY driver.
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/phy/phy.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <linux/phy/omap_control_phy.h>
#include <linux/of_platform.h>

#define	PLL_STATUS		0x00000004
#define	PLL_GO			0x00000008
#define	PLL_CONFIGURATION1	0x0000000C
#define	PLL_CONFIGURATION2	0x00000010
#define	PLL_CONFIGURATION3	0x00000014
#define	PLL_CONFIGURATION4	0x00000020

#define	PLL_REGM_MASK		0x001FFE00
#define	PLL_REGM_SHIFT		0x9
#define	PLL_REGM_F_MASK		0x0003FFFF
#define	PLL_REGM_F_SHIFT	0x0
#define	PLL_REGN_MASK		0x000001FE
#define	PLL_REGN_SHIFT		0x1
#define	PLL_SELFREQDCO_MASK	0x0000000E
#define	PLL_SELFREQDCO_SHIFT	0x1
#define	PLL_SD_MASK		0x0003FC00
#define	PLL_SD_SHIFT		10
#define	SET_PLL_GO		0x1
#define PLL_LDOPWDN		BIT(15)
#define PLL_TICOPWDN		BIT(16)
#define	PLL_LOCK		0x2
#define	PLL_IDLE		0x1

/*
 * This is an Empirical value that works, need to confirm the actual
 * value required for the PIPE3PHY_PLL_CONFIGURATION2.PLL_IDLE status
 * to be correctly reflected in the PIPE3PHY_PLL_STATUS register.
 */
#define PLL_IDLE_TIME	100	/* in milliseconds */
#define PLL_LOCK_TIME	100	/* in milliseconds */

struct pipe3_dpll_params {
	u16	m;
	u8	n;
	u8	freq:3;
	u8	sd;
	u32	mf;
};

struct pipe3_dpll_map {
	unsigned long rate;
	struct pipe3_dpll_params params;
};

struct ti_pipe3 {
	void __iomem		*pll_ctrl_base;
	struct device		*dev;
	struct device		*control_dev;
	struct clk		*wkupclk;
	struct clk		*sys_clk;
	struct clk		*refclk;
	struct clk		*div_clk;
	struct pipe3_dpll_map	*dpll_map;
};

static struct pipe3_dpll_map dpll_map_usb[] = {
	{12000000, {1250, 5, 4, 20, 0} },	/* 12 MHz */
	{16800000, {3125, 20, 4, 20, 0} },	/* 16.8 MHz */
	{19200000, {1172, 8, 4, 20, 65537} },	/* 19.2 MHz */
	{20000000, {1000, 7, 4, 10, 0} },	/* 20 MHz */
	{26000000, {1250, 12, 4, 20, 0} },	/* 26 MHz */
	{38400000, {3125, 47, 4, 20, 92843} },	/* 38.4 MHz */
	{ },					/* Terminator */
};

static struct pipe3_dpll_map dpll_map_sata[] = {
	{12000000, {1000, 7, 4, 6, 0} },	/* 12 MHz */
	{16800000, {714, 7, 4, 6, 0} },		/* 16.8 MHz */
	{19200000, {625, 7, 4, 6, 0} },		/* 19.2 MHz */
	{20000000, {600, 7, 4, 6, 0} },		/* 20 MHz */
	{26000000, {461, 7, 4, 6, 0} },		/* 26 MHz */
	{38400000, {312, 7, 4, 6, 0} },		/* 38.4 MHz */
	{ },					/* Terminator */
};

static inline u32 ti_pipe3_readl(void __iomem *addr, unsigned offset)
{
	return __raw_readl(addr + offset);
}

static inline void ti_pipe3_writel(void __iomem *addr, unsigned offset,
	u32 data)
{
	__raw_writel(data, addr + offset);
}

static struct pipe3_dpll_params *ti_pipe3_get_dpll_params(struct ti_pipe3 *phy)
{
	unsigned long rate;
	struct pipe3_dpll_map *dpll_map = phy->dpll_map;

	rate = clk_get_rate(phy->sys_clk);

	for (; dpll_map->rate; dpll_map++) {
		if (rate == dpll_map->rate)
			return &dpll_map->params;
	}

	dev_err(phy->dev, "No DPLL configuration for %lu Hz SYS CLK\n", rate);

	return NULL;
}

static int ti_pipe3_enable_clocks(struct ti_pipe3 *phy);
static void ti_pipe3_disable_clocks(struct ti_pipe3 *phy);

static int ti_pipe3_power_off(struct phy *x)
{
	struct ti_pipe3 *phy = phy_get_drvdata(x);

	omap_control_phy_power(phy->control_dev, 0);

	return 0;
}

static int ti_pipe3_power_on(struct phy *x)
{
	struct ti_pipe3 *phy = phy_get_drvdata(x);

	omap_control_phy_power(phy->control_dev, 1);

	return 0;
}

static int ti_pipe3_dpll_wait_lock(struct ti_pipe3 *phy)
{
	u32		val;
	unsigned long	timeout;

	timeout = jiffies + msecs_to_jiffies(PLL_LOCK_TIME);
	do {
		cpu_relax();
		val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_STATUS);
		if (val & PLL_LOCK)
			return 0;
	} while (!time_after(jiffies, timeout));

	dev_err(phy->dev, "DPLL failed to lock\n");
	return -EBUSY;
}

static int ti_pipe3_dpll_program(struct ti_pipe3 *phy)
{
	u32			val;
	struct pipe3_dpll_params *dpll_params;

	dpll_params = ti_pipe3_get_dpll_params(phy);
	if (!dpll_params)
		return -EINVAL;

	val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_CONFIGURATION1);
	val &= ~PLL_REGN_MASK;
	val |= dpll_params->n << PLL_REGN_SHIFT;
	ti_pipe3_writel(phy->pll_ctrl_base, PLL_CONFIGURATION1, val);

	val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_CONFIGURATION2);
	val &= ~PLL_SELFREQDCO_MASK;
	val |= dpll_params->freq << PLL_SELFREQDCO_SHIFT;
	ti_pipe3_writel(phy->pll_ctrl_base, PLL_CONFIGURATION2, val);

	val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_CONFIGURATION1);
	val &= ~PLL_REGM_MASK;
	val |= dpll_params->m << PLL_REGM_SHIFT;
	ti_pipe3_writel(phy->pll_ctrl_base, PLL_CONFIGURATION1, val);

	val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_CONFIGURATION4);
	val &= ~PLL_REGM_F_MASK;
	val |= dpll_params->mf << PLL_REGM_F_SHIFT;
	ti_pipe3_writel(phy->pll_ctrl_base, PLL_CONFIGURATION4, val);

	val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_CONFIGURATION3);
	val &= ~PLL_SD_MASK;
	val |= dpll_params->sd << PLL_SD_SHIFT;
	ti_pipe3_writel(phy->pll_ctrl_base, PLL_CONFIGURATION3, val);

	ti_pipe3_writel(phy->pll_ctrl_base, PLL_GO, SET_PLL_GO);

	return ti_pipe3_dpll_wait_lock(phy);
}

static int ti_pipe3_init(struct phy *x)
{
	struct ti_pipe3 *phy = phy_get_drvdata(x);
	u32 val;
	int ret = 0;

	ti_pipe3_enable_clocks(phy);
	/*
	 * Set pcie_pcs register to 0x96 for proper functioning of phy
	 * as recommended in AM572x TRM SPRUHZ6, section 18.5.2.2, table
	 * 18-1804.
	 */
	if (of_device_is_compatible(phy->dev->of_node, "ti,phy-pipe3-pcie")) {
		omap_control_pcie_pcs(phy->control_dev, 0x96);
		return 0;
	}

	/* Bring it out of IDLE if it is IDLE */
	val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_CONFIGURATION2);
	if (val & PLL_IDLE) {
		val &= ~PLL_IDLE;
		ti_pipe3_writel(phy->pll_ctrl_base, PLL_CONFIGURATION2, val);
		ret = ti_pipe3_dpll_wait_lock(phy);
	}

	/* Program the DPLL only if not locked */
	val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_STATUS);
	if (!(val & PLL_LOCK))
		if (ti_pipe3_dpll_program(phy))
			return -EINVAL;

	return ret;
}

static int ti_pipe3_exit(struct phy *x)
{
	struct ti_pipe3 *phy = phy_get_drvdata(x);
	u32 val;
	unsigned long timeout;

	/* SATA DPLL can't be powered down due to Errata i783 */
	if (of_device_is_compatible(phy->dev->of_node, "ti,phy-pipe3-sata"))
		return 0;

	/* PCIe doesn't have internal DPLL */
	if (!of_device_is_compatible(phy->dev->of_node, "ti,phy-pipe3-pcie")) {
		/* Put DPLL in IDLE mode */
		val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_CONFIGURATION2);
		val |= PLL_IDLE;
		ti_pipe3_writel(phy->pll_ctrl_base, PLL_CONFIGURATION2, val);

		/* wait for LDO and Oscillator to power down */
		timeout = jiffies + msecs_to_jiffies(PLL_IDLE_TIME);
		do {
			cpu_relax();
			val = ti_pipe3_readl(phy->pll_ctrl_base, PLL_STATUS);
			if ((val & PLL_TICOPWDN) && (val & PLL_LDOPWDN))
				break;
		} while (!time_after(jiffies, timeout));

		if (!(val & PLL_TICOPWDN) || !(val & PLL_LDOPWDN)) {
			dev_err(phy->dev, "Failed to power down: PLL_STATUS 0x%x\n",
				val);
			return -EBUSY;
		}
	}

	ti_pipe3_disable_clocks(phy);

	return 0;
}
static struct phy_ops ops = {
	.init		= ti_pipe3_init,
	.exit		= ti_pipe3_exit,
	.power_on	= ti_pipe3_power_on,
	.power_off	= ti_pipe3_power_off,
	.owner		= THIS_MODULE,
};

static const struct of_device_id ti_pipe3_id_table[];

static int ti_pipe3_probe(struct platform_device *pdev)
{
	struct ti_pipe3 *phy;
	struct phy *generic_phy;
	struct phy_provider *phy_provider;
	struct resource *res;
	struct device_node *node = pdev->dev.of_node;
	struct device_node *control_node;
	struct platform_device *control_pdev;
	const struct of_device_id *match;
	struct clk *clk;

	phy = devm_kzalloc(&pdev->dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->dev		= &pdev->dev;

	if (!of_device_is_compatible(node, "ti,phy-pipe3-pcie")) {
		match = of_match_device(ti_pipe3_id_table, &pdev->dev);
		if (!match)
			return -EINVAL;

		phy->dpll_map = (struct pipe3_dpll_map *)match->data;
		if (!phy->dpll_map) {
			dev_err(&pdev->dev, "no DPLL data\n");
			return -EINVAL;
		}

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   "pll_ctrl");
		phy->pll_ctrl_base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(phy->pll_ctrl_base))
			return PTR_ERR(phy->pll_ctrl_base);

		phy->sys_clk = devm_clk_get(phy->dev, "sysclk");
		if (IS_ERR(phy->sys_clk)) {
			dev_err(&pdev->dev, "unable to get sysclk\n");
			return -EINVAL;
		}
	}

	phy->refclk = devm_clk_get(phy->dev, "refclk");
	if (IS_ERR(phy->refclk)) {
		dev_err(&pdev->dev, "unable to get refclk\n");
		/* older DTBs have missing refclk in SATA PHY
		 * so don't bail out in case of SATA PHY.
		 */
		if (!of_device_is_compatible(node, "ti,phy-pipe3-sata"))
			return PTR_ERR(phy->refclk);
	}

	if (!of_device_is_compatible(node, "ti,phy-pipe3-sata")) {
		phy->wkupclk = devm_clk_get(phy->dev, "wkupclk");
		if (IS_ERR(phy->wkupclk)) {
			dev_err(&pdev->dev, "unable to get wkupclk\n");
			return PTR_ERR(phy->wkupclk);
		}
	} else {
		phy->wkupclk = ERR_PTR(-ENODEV);
	}

	if (of_device_is_compatible(node, "ti,phy-pipe3-pcie")) {

		clk = devm_clk_get(phy->dev, "dpll_ref");
		if (IS_ERR(clk)) {
			dev_err(&pdev->dev, "unable to get dpll ref clk\n");
			return PTR_ERR(clk);
		}
		clk_set_rate(clk, 1500000000);

		clk = devm_clk_get(phy->dev, "dpll_ref_m2");
		if (IS_ERR(clk)) {
			dev_err(&pdev->dev, "unable to get dpll ref m2 clk\n");
			return PTR_ERR(clk);
		}
		clk_set_rate(clk, 100000000);

		clk = devm_clk_get(phy->dev, "phy-div");
		if (IS_ERR(clk)) {
			dev_err(&pdev->dev, "unable to get phy-div clk\n");
			return PTR_ERR(clk);
		}
		clk_set_rate(clk, 100000000);

		phy->div_clk = devm_clk_get(phy->dev, "div-clk");
		if (IS_ERR(phy->div_clk)) {
			dev_err(&pdev->dev, "unable to get div-clk\n");
			return PTR_ERR(phy->div_clk);
		}
	} else {
		phy->div_clk = ERR_PTR(-ENODEV);
	}

	control_node = of_parse_phandle(node, "ctrl-module", 0);
	if (!control_node) {
		dev_err(&pdev->dev, "Failed to get control device phandle\n");
		return -EINVAL;
	}

	control_pdev = of_find_device_by_node(control_node);
	if (!control_pdev) {
		dev_err(&pdev->dev, "Failed to get control device\n");
		return -EINVAL;
	}

	phy->control_dev = &control_pdev->dev;

	omap_control_phy_power(phy->control_dev, 0);

	platform_set_drvdata(pdev, phy);
	pm_runtime_enable(phy->dev);
	/* Prevent auto-disable of refclk for SATA PHY due to Errata i783 */
	if (of_device_is_compatible(node, "ti,phy-pipe3-sata"))
		if (!IS_ERR(phy->refclk))
			clk_prepare_enable(phy->refclk);

	generic_phy = devm_phy_create(phy->dev, NULL, &ops);
	if (IS_ERR(generic_phy))
		return PTR_ERR(generic_phy);

	phy_set_drvdata(generic_phy, phy);
	phy_provider = devm_of_phy_provider_register(phy->dev,
			of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return PTR_ERR(phy_provider);

	return 0;
}

static int ti_pipe3_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static int ti_pipe3_enable_clocks(struct ti_pipe3 *phy)
{
	int ret = 0;

	if (!IS_ERR(phy->refclk)) {
		ret = clk_prepare_enable(phy->refclk);
		if (ret) {
			dev_err(phy->dev, "Failed to enable refclk %d\n", ret);
			return ret;
		}
	}

	if (!IS_ERR(phy->wkupclk)) {
		ret = clk_prepare_enable(phy->wkupclk);
		if (ret) {
			dev_err(phy->dev, "Failed to enable wkupclk %d\n", ret);
			goto disable_refclk;
		}
	}

	if (!IS_ERR(phy->div_clk)) {
		ret = clk_prepare_enable(phy->div_clk);
		if (ret) {
			dev_err(phy->dev, "Failed to enable div_clk %d\n", ret);
			goto disable_wkupclk;
		}
	}

	return 0;

disable_wkupclk:
	if (!IS_ERR(phy->wkupclk))
		clk_disable_unprepare(phy->wkupclk);

disable_refclk:
	if (!IS_ERR(phy->refclk))
		clk_disable_unprepare(phy->refclk);

	return ret;
}

static void ti_pipe3_disable_clocks(struct ti_pipe3 *phy)
{
	if (!IS_ERR(phy->wkupclk))
		clk_disable_unprepare(phy->wkupclk);
	if (!IS_ERR(phy->refclk))
		clk_disable_unprepare(phy->refclk);
	if (!IS_ERR(phy->div_clk))
		clk_disable_unprepare(phy->div_clk);
}

static const struct of_device_id ti_pipe3_id_table[] = {
	{
		.compatible = "ti,phy-usb3",
		.data = dpll_map_usb,
	},
	{
		.compatible = "ti,omap-usb3",
		.data = dpll_map_usb,
	},
	{
		.compatible = "ti,phy-pipe3-sata",
		.data = dpll_map_sata,
	},
	{
		.compatible = "ti,phy-pipe3-pcie",
	},
	{}
};
MODULE_DEVICE_TABLE(of, ti_pipe3_id_table);

static struct platform_driver ti_pipe3_driver = {
	.probe		= ti_pipe3_probe,
	.remove		= ti_pipe3_remove,
	.driver		= {
		.name	= "ti-pipe3",
		.of_match_table = ti_pipe3_id_table,
	},
};

module_platform_driver(ti_pipe3_driver);

MODULE_ALIAS("platform:ti_pipe3");
MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("TI PIPE3 phy driver");
MODULE_LICENSE("GPL v2");
