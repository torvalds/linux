// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for Rockchip SoCs.
 *
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd.
 *		http://www.rock-chips.com
 *
 * Author: Simon Xue <xxm@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include "pcie-designware.h"

/*
 * The upper 16 bits of PCIE_CLIENT_CONFIG are a write
 * mask for the lower 16 bits.
 */
#define HIWORD_UPDATE(mask, val) (((mask) << 16) | (val))
#define HIWORD_UPDATE_BIT(val)	HIWORD_UPDATE(val, val)

#define to_rockchip_pcie(x) dev_get_drvdata((x)->dev)

#define PCIE_CLIENT_RC_MODE		HIWORD_UPDATE_BIT(0x40)
#define PCIE_CLIENT_ENABLE_LTSSM	HIWORD_UPDATE_BIT(0xc)
#define PCIE_SMLH_LINKUP		BIT(16)
#define PCIE_RDLH_LINKUP		BIT(17)
#define PCIE_LINKUP			(PCIE_SMLH_LINKUP | PCIE_RDLH_LINKUP)
#define PCIE_L0S_ENTRY			0x11
#define PCIE_CLIENT_GENERAL_CONTROL	0x0
#define PCIE_CLIENT_GENERAL_DEBUG	0x104
#define PCIE_CLIENT_HOT_RESET_CTRL      0x180
#define PCIE_CLIENT_LTSSM_STATUS	0x300
#define PCIE_LTSSM_ENABLE_ENHANCE       BIT(4)
#define PCIE_LTSSM_STATUS_MASK		GENMASK(5, 0)

struct rockchip_pcie {
	struct dw_pcie			pci;
	void __iomem			*apb_base;
	struct phy			*phy;
	struct clk_bulk_data		*clks;
	unsigned int			clk_cnt;
	struct reset_control		*rst;
	struct gpio_desc		*rst_gpio;
	struct regulator                *vpcie3v3;
};

static int rockchip_pcie_readl_apb(struct rockchip_pcie *rockchip,
					     u32 reg)
{
	return readl_relaxed(rockchip->apb_base + reg);
}

static void rockchip_pcie_writel_apb(struct rockchip_pcie *rockchip,
						u32 val, u32 reg)
{
	writel_relaxed(val, rockchip->apb_base + reg);
}

static void rockchip_pcie_enable_ltssm(struct rockchip_pcie *rockchip)
{
	rockchip_pcie_writel_apb(rockchip, PCIE_CLIENT_ENABLE_LTSSM,
				 PCIE_CLIENT_GENERAL_CONTROL);
}

static int rockchip_pcie_link_up(struct dw_pcie *pci)
{
	struct rockchip_pcie *rockchip = to_rockchip_pcie(pci);
	u32 val = rockchip_pcie_readl_apb(rockchip, PCIE_CLIENT_LTSSM_STATUS);

	if ((val & PCIE_LINKUP) == PCIE_LINKUP &&
	    (val & PCIE_LTSSM_STATUS_MASK) == PCIE_L0S_ENTRY)
		return 1;

	return 0;
}

static int rockchip_pcie_start_link(struct dw_pcie *pci)
{
	struct rockchip_pcie *rockchip = to_rockchip_pcie(pci);

	/* Reset device */
	gpiod_set_value_cansleep(rockchip->rst_gpio, 0);

	rockchip_pcie_enable_ltssm(rockchip);

	/*
	 * PCIe requires the refclk to be stable for 100µs prior to releasing
	 * PERST. See table 2-4 in section 2.6.2 AC Specifications of the PCI
	 * Express Card Electromechanical Specification, 1.1. However, we don't
	 * know if the refclk is coming from RC's PHY or external OSC. If it's
	 * from RC, so enabling LTSSM is the just right place to release #PERST.
	 * We need more extra time as before, rather than setting just
	 * 100us as we don't know how long should the device need to reset.
	 */
	msleep(100);
	gpiod_set_value_cansleep(rockchip->rst_gpio, 1);

	return 0;
}

static int rockchip_pcie_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct rockchip_pcie *rockchip = to_rockchip_pcie(pci);
	u32 val = HIWORD_UPDATE_BIT(PCIE_LTSSM_ENABLE_ENHANCE);

	/* LTSSM enable control mode */
	rockchip_pcie_writel_apb(rockchip, val, PCIE_CLIENT_HOT_RESET_CTRL);

	rockchip_pcie_writel_apb(rockchip, PCIE_CLIENT_RC_MODE,
				 PCIE_CLIENT_GENERAL_CONTROL);

	return 0;
}

static const struct dw_pcie_host_ops rockchip_pcie_host_ops = {
	.host_init = rockchip_pcie_host_init,
};

static int rockchip_pcie_clk_init(struct rockchip_pcie *rockchip)
{
	struct device *dev = rockchip->pci.dev;
	int ret;

	ret = devm_clk_bulk_get_all(dev, &rockchip->clks);
	if (ret < 0)
		return ret;

	rockchip->clk_cnt = ret;

	return clk_bulk_prepare_enable(rockchip->clk_cnt, rockchip->clks);
}

static int rockchip_pcie_resource_get(struct platform_device *pdev,
				      struct rockchip_pcie *rockchip)
{
	rockchip->apb_base = devm_platform_ioremap_resource_byname(pdev, "apb");
	if (IS_ERR(rockchip->apb_base))
		return PTR_ERR(rockchip->apb_base);

	rockchip->rst_gpio = devm_gpiod_get_optional(&pdev->dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(rockchip->rst_gpio))
		return PTR_ERR(rockchip->rst_gpio);

	return 0;
}

static int rockchip_pcie_phy_init(struct rockchip_pcie *rockchip)
{
	struct device *dev = rockchip->pci.dev;
	int ret;

	rockchip->phy = devm_phy_get(dev, "pcie-phy");
	if (IS_ERR(rockchip->phy))
		return dev_err_probe(dev, PTR_ERR(rockchip->phy),
				     "missing PHY\n");

	ret = phy_init(rockchip->phy);
	if (ret < 0)
		return ret;

	ret = phy_power_on(rockchip->phy);
	if (ret)
		phy_exit(rockchip->phy);

	return ret;
}

static void rockchip_pcie_phy_deinit(struct rockchip_pcie *rockchip)
{
	phy_exit(rockchip->phy);
	phy_power_off(rockchip->phy);
}

static int rockchip_pcie_reset_control_release(struct rockchip_pcie *rockchip)
{
	struct device *dev = rockchip->pci.dev;

	rockchip->rst = devm_reset_control_array_get_exclusive(dev);
	if (IS_ERR(rockchip->rst))
		return dev_err_probe(dev, PTR_ERR(rockchip->rst),
				     "failed to get reset lines\n");

	return reset_control_deassert(rockchip->rst);
}

static const struct dw_pcie_ops dw_pcie_ops = {
	.link_up = rockchip_pcie_link_up,
	.start_link = rockchip_pcie_start_link,
};

static int rockchip_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_pcie *rockchip;
	struct pcie_port *pp;
	int ret;

	rockchip = devm_kzalloc(dev, sizeof(*rockchip), GFP_KERNEL);
	if (!rockchip)
		return -ENOMEM;

	platform_set_drvdata(pdev, rockchip);

	rockchip->pci.dev = dev;
	rockchip->pci.ops = &dw_pcie_ops;

	pp = &rockchip->pci.pp;
	pp->ops = &rockchip_pcie_host_ops;

	ret = rockchip_pcie_resource_get(pdev, rockchip);
	if (ret)
		return ret;

	/* DON'T MOVE ME: must be enable before PHY init */
	rockchip->vpcie3v3 = devm_regulator_get_optional(dev, "vpcie3v3");
	if (IS_ERR(rockchip->vpcie3v3)) {
		if (PTR_ERR(rockchip->vpcie3v3) != -ENODEV)
			return dev_err_probe(dev, PTR_ERR(rockchip->vpcie3v3),
					"failed to get vpcie3v3 regulator\n");
		rockchip->vpcie3v3 = NULL;
	} else {
		ret = regulator_enable(rockchip->vpcie3v3);
		if (ret) {
			dev_err(dev, "failed to enable vpcie3v3 regulator\n");
			return ret;
		}
	}

	ret = rockchip_pcie_phy_init(rockchip);
	if (ret)
		goto disable_regulator;

	ret = rockchip_pcie_reset_control_release(rockchip);
	if (ret)
		goto deinit_phy;

	ret = rockchip_pcie_clk_init(rockchip);
	if (ret)
		goto deinit_phy;

	ret = dw_pcie_host_init(pp);
	if (!ret)
		return 0;

	clk_bulk_disable_unprepare(rockchip->clk_cnt, rockchip->clks);
deinit_phy:
	rockchip_pcie_phy_deinit(rockchip);
disable_regulator:
	if (rockchip->vpcie3v3)
		regulator_disable(rockchip->vpcie3v3);

	return ret;
}

static const struct of_device_id rockchip_pcie_of_match[] = {
	{ .compatible = "rockchip,rk3568-pcie", },
	{},
};

static struct platform_driver rockchip_pcie_driver = {
	.driver = {
		.name	= "rockchip-dw-pcie",
		.of_match_table = rockchip_pcie_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = rockchip_pcie_probe,
};
builtin_platform_driver(rockchip_pcie_driver);
