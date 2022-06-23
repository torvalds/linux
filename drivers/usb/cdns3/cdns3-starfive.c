// SPDX-License-Identifier: GPL-2.0
/**
 * cdns-starfive.c - Cadence USB Controller
 *
 * Copyright (C) 2022 Starfive, Inc.
 * Author:	yanhong <yanhong.wang@starfivetech.com>
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/usb/otg.h>

#define USB_STRAP_HOST		(2 << 0x10)
#define USB_STRAP_DEVICE	(4 << 0X10)
#define USB_STRAP_MASK		0x70000U

#define USB_SUSPENDM_HOST	(1 << 0x13)
#define USB_SUSPENDM_DEVICE	(0 << 0x13)
#define USB_SUSPENDM_MASK	0x80000U

#define USB_SUSPENDM_BYPS_SHIFT	0x14U
#define USB_SUSPENDM_BYPS_MASK		0x100000U

#define USB_REFCLK_MODE_SHIFT	0x17U
#define USB_REFCLK_MODE_MASK	0x800000U

#define USB_PLL_EN_SHIFT	0x16U
#define USB_PLL_EN_MASK		0x400000U

#define USB_PDRSTN_SPLIT_SHIFT	0x11
#define USB_PDRSTN_SPLIT_MASK	0x20000U

#define PCIE_CKREF_SRC_SHIFT	0x12U
#define PCIE_CKREF_SRC_MASK	0xC0000U
#define PCIE_CLK_SEL_SHIFT	0x14U
#define PCIE_CLK_SEL_MASK	0x300000U
#define PCIE_PHY_MODE_SHIFT	0x14U
#define PCIE_PHY_MODE_MASK	0x300000U
#define PCIE_USB3_BUS_WIDTH_SHIFT	0x2U
#define PCIE_USB3_BUS_WIDTH_MASK	0xCU
#define PCIE_USB3_RATE_SHIFT		0x5U
#define PCIE_USB3_RATE_MASK		0x60U
#define PCIE_USB3_RX_STANDBY_SHIFT	0x7U
#define PCIE_USB3_RX_STANDBY_MASK	0x80U
#define PCIE_USB3_PHY_ENABLE_SHIFT	0x4U
#define PCIE_USB3_PHY_ENABLE_MASK	0x10U

#define USB_125M_CLK_RATE		125000000

struct cdns_starfive {
	struct device *dev;
	struct regmap *stg_syscon;
	struct regmap *sys_syscon;
	struct reset_control *resets;
	struct clk_bulk_data *clks;
	int num_clks;
	struct clk *usb_125m_clk;
	u32 sys_offset;
	u32 stg_offset_4;
	u32 stg_offset_196;
	u32 stg_offset_328;
	u32 stg_offset_500;
	bool usb2_only;
};

static int cdns_mode_init(struct platform_device *pdev,
				struct cdns_starfive *data)
{
	enum usb_dr_mode mode;

	/*usb 2.0 utmi phy init*/
	regmap_update_bits(data->stg_syscon, data->stg_offset_4,
		USB_SUSPENDM_BYPS_MASK, BIT(USB_SUSPENDM_BYPS_SHIFT));
	regmap_update_bits(data->stg_syscon, data->stg_offset_4,
		USB_PLL_EN_MASK, BIT(USB_PLL_EN_SHIFT));
	regmap_update_bits(data->stg_syscon, data->stg_offset_4,
		USB_REFCLK_MODE_MASK, BIT(USB_REFCLK_MODE_SHIFT));

	if (data->usb2_only) {

		/* disconnect usb 3.0 phy mode */
		regmap_update_bits(data->sys_syscon, data->sys_offset,
			USB_PDRSTN_SPLIT_MASK, BIT(USB_PDRSTN_SPLIT_SHIFT));
	} else {
		/*usb 3.0 pipe phy config*/
		regmap_update_bits(data->stg_syscon, data->stg_offset_196,
			PCIE_CKREF_SRC_MASK, (0<<PCIE_CKREF_SRC_SHIFT));
		regmap_update_bits(data->stg_syscon, data->stg_offset_196,
			PCIE_CLK_SEL_MASK, (0<<PCIE_CLK_SEL_SHIFT));
		regmap_update_bits(data->stg_syscon, data->stg_offset_328,
			PCIE_PHY_MODE_MASK, BIT(PCIE_PHY_MODE_SHIFT));
		regmap_update_bits(data->stg_syscon, data->stg_offset_500,
			PCIE_USB3_BUS_WIDTH_MASK, (0 << PCIE_USB3_BUS_WIDTH_SHIFT));
		regmap_update_bits(data->stg_syscon, data->stg_offset_500,
			PCIE_USB3_RATE_MASK, (0 << PCIE_USB3_RATE_SHIFT));
		regmap_update_bits(data->stg_syscon, data->stg_offset_500,
			PCIE_USB3_RX_STANDBY_MASK, (0 << PCIE_USB3_RX_STANDBY_SHIFT));
		regmap_update_bits(data->stg_syscon, data->stg_offset_500,
			PCIE_USB3_PHY_ENABLE_MASK, BIT(PCIE_USB3_PHY_ENABLE_SHIFT));

		/* connect usb 3.0 phy mode */
		regmap_update_bits(data->sys_syscon, data->sys_offset,
			USB_PDRSTN_SPLIT_MASK, (0 << USB_PDRSTN_SPLIT_SHIFT));
	}
	mode = usb_get_dr_mode(&pdev->dev);

	switch (mode) {
	case USB_DR_MODE_HOST:
		regmap_update_bits(data->stg_syscon,
			data->stg_offset_4,
			USB_STRAP_MASK,
			USB_STRAP_HOST);
		regmap_update_bits(data->stg_syscon,
			data->stg_offset_4,
			USB_SUSPENDM_MASK,
			USB_SUSPENDM_HOST);
		break;

	case USB_DR_MODE_PERIPHERAL:
		regmap_update_bits(data->stg_syscon, data->stg_offset_4,
			USB_STRAP_MASK, USB_STRAP_DEVICE);
		regmap_update_bits(data->stg_syscon, data->stg_offset_4,
			USB_SUSPENDM_MASK, USB_SUSPENDM_DEVICE);
		break;

	case USB_DR_MODE_UNKNOWN:
	case USB_DR_MODE_OTG:
	default:
		break;
	}

	return 0;
}


static int cdns_clk_rst_init(struct cdns_starfive *data)
{
	int ret;

	data->usb_125m_clk = devm_clk_get(data->dev, "125m");
	if (IS_ERR(data->usb_125m_clk)) {
		dev_err(data->dev, "Failed to get usb 125m clock\n");
		ret = PTR_ERR(data->usb_125m_clk);
		goto exit;
	}

	data->num_clks = devm_clk_bulk_get_all(data->dev, &data->clks);
	if (data->num_clks < 0) {
		dev_err(data->dev, "Failed to get usb clocks\n");
		ret = -ENODEV;
		goto exit;
	}

	/* Needs to set the USB_125M clock explicitly,
	 * since it's divided from pll0 clock, and the pll0 clock
	 * changes per the cpu frequency.
	 */
	ret = clk_set_rate(data->usb_125m_clk, USB_125M_CLK_RATE);
	if (ret) {
		dev_err(data->dev, "Failed to set usb 125m clock\n");
		goto exit;
	}
	ret = clk_bulk_prepare_enable(data->num_clks, data->clks);
	if (ret) {
		dev_err(data->dev, "Failed to enable clocks\n");
		goto exit;
	}

	data->resets = devm_reset_control_array_get_exclusive(data->dev);
	if (IS_ERR(data->resets)) {
		ret = PTR_ERR(data->resets);
		dev_err(data->dev, "Failed to get usb resets");
		goto err_clk_init;
	}
	ret = reset_control_deassert(data->resets);
	goto exit;

err_clk_init:
	clk_bulk_disable_unprepare(data->num_clks, data->clks);
exit:
	return ret;
}

static int cdns_starfive_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct cdns_starfive *data;
	struct of_phandle_args args;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	data->dev = dev;

	data->usb2_only = device_property_read_bool(dev, "starfive,usb2-only");
	ret = of_parse_phandle_with_fixed_args(pdev->dev.of_node,
						"starfive,stg-syscon", 4, 0, &args);
	if (ret < 0) {
		dev_err(dev, "Failed to parse starfive,stg-syscon\n");
		return -EINVAL;
	}

	data->stg_syscon = syscon_node_to_regmap(args.np);
	of_node_put(args.np);
	if (IS_ERR(data->stg_syscon))
		return PTR_ERR(data->stg_syscon);
	data->stg_offset_4 = args.args[0];
	data->stg_offset_196 = args.args[1];
	data->stg_offset_328 = args.args[2];
	data->stg_offset_500 = args.args[3];

	ret = of_parse_phandle_with_fixed_args(pdev->dev.of_node,
						"starfive,sys-syscon", 1, 0, &args);
	if (ret < 0) {
		dev_err(dev, "Failed to parse starfive,sys-syscon\n");
		return -EINVAL;
	}

	data->sys_syscon = syscon_node_to_regmap(args.np);
	of_node_put(args.np);
	if (IS_ERR(data->sys_syscon))
		return PTR_ERR(data->sys_syscon);
	data->sys_offset = args.args[0];

	cdns_mode_init(pdev, data);

	ret = cdns_clk_rst_init(data);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to init usb clk reset: %d\n", ret);
		goto exit;
	}

	ret = of_platform_populate(node, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to create children: %d\n", ret);
		goto exit;
	}

	return 0;
exit:
	return ret;
}

static int cdns_starfive_remove_core(struct device *dev, void *c)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);

	return 0;
}

static int cdns_starfive_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	device_for_each_child(dev, NULL, cdns_starfive_remove_core);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id cdns_starfive_of_match[] = {
	{ .compatible = "starfive,jh7110-cdns3", },
	{},
};
MODULE_DEVICE_TABLE(of, cdns_starfive_of_match);

static struct platform_driver cdns_starfive_driver = {
	.probe		= cdns_starfive_probe,
	.remove		= cdns_starfive_remove,
	.driver		= {
		.name	= "cdns3-starfive",
		.of_match_table	= cdns_starfive_of_match,
	},
};

module_platform_driver(cdns_starfive_driver);

MODULE_ALIAS("platform:cdns3-starfive");
MODULE_AUTHOR("YanHong Wang <yanhong.wang@starfivetech.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Cadence USB3 StarFive SoC platform");
