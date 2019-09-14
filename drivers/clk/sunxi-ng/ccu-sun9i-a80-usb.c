// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016 Chen-Yu Tsai. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#include "ccu_common.h"
#include "ccu_gate.h"
#include "ccu_reset.h"

#include "ccu-sun9i-a80-usb.h"

static const struct clk_parent_data clk_parent_hosc[] = {
	{ .fw_name = "hosc" },
};

static const struct clk_parent_data clk_parent_bus[] = {
	{ .fw_name = "bus" },
};

static SUNXI_CCU_GATE_DATA(bus_hci0_clk, "bus-hci0", clk_parent_bus, 0x0, BIT(1), 0);
static SUNXI_CCU_GATE_DATA(usb_ohci0_clk, "usb-ohci0", clk_parent_hosc, 0x0, BIT(2), 0);
static SUNXI_CCU_GATE_DATA(bus_hci1_clk, "bus-hci1", clk_parent_bus, 0x0, BIT(3), 0);
static SUNXI_CCU_GATE_DATA(bus_hci2_clk, "bus-hci2", clk_parent_bus, 0x0, BIT(5), 0);
static SUNXI_CCU_GATE_DATA(usb_ohci2_clk, "usb-ohci2", clk_parent_hosc, 0x0, BIT(6), 0);

static SUNXI_CCU_GATE_DATA(usb0_phy_clk, "usb0-phy", clk_parent_hosc, 0x4, BIT(1), 0);
static SUNXI_CCU_GATE_DATA(usb1_hsic_clk, "usb1-hsic", clk_parent_hosc, 0x4, BIT(2), 0);
static SUNXI_CCU_GATE_DATA(usb1_phy_clk, "usb1-phy", clk_parent_hosc, 0x4, BIT(3), 0);
static SUNXI_CCU_GATE_DATA(usb2_hsic_clk, "usb2-hsic", clk_parent_hosc, 0x4, BIT(4), 0);
static SUNXI_CCU_GATE_DATA(usb2_phy_clk, "usb2-phy", clk_parent_hosc, 0x4, BIT(5), 0);
static SUNXI_CCU_GATE_DATA(usb_hsic_clk, "usb-hsic", clk_parent_hosc, 0x4, BIT(10), 0);

static struct ccu_common *sun9i_a80_usb_clks[] = {
	&bus_hci0_clk.common,
	&usb_ohci0_clk.common,
	&bus_hci1_clk.common,
	&bus_hci2_clk.common,
	&usb_ohci2_clk.common,

	&usb0_phy_clk.common,
	&usb1_hsic_clk.common,
	&usb1_phy_clk.common,
	&usb2_hsic_clk.common,
	&usb2_phy_clk.common,
	&usb_hsic_clk.common,
};

static struct clk_hw_onecell_data sun9i_a80_usb_hw_clks = {
	.hws	= {
		[CLK_BUS_HCI0]	= &bus_hci0_clk.common.hw,
		[CLK_USB_OHCI0]	= &usb_ohci0_clk.common.hw,
		[CLK_BUS_HCI1]	= &bus_hci1_clk.common.hw,
		[CLK_BUS_HCI2]	= &bus_hci2_clk.common.hw,
		[CLK_USB_OHCI2]	= &usb_ohci2_clk.common.hw,

		[CLK_USB0_PHY]	= &usb0_phy_clk.common.hw,
		[CLK_USB1_HSIC]	= &usb1_hsic_clk.common.hw,
		[CLK_USB1_PHY]	= &usb1_phy_clk.common.hw,
		[CLK_USB2_HSIC]	= &usb2_hsic_clk.common.hw,
		[CLK_USB2_PHY]	= &usb2_phy_clk.common.hw,
		[CLK_USB_HSIC]	= &usb_hsic_clk.common.hw,
	},
	.num	= CLK_NUMBER,
};

static struct ccu_reset_map sun9i_a80_usb_resets[] = {
	[RST_USB0_HCI]		= { 0x0, BIT(17) },
	[RST_USB1_HCI]		= { 0x0, BIT(18) },
	[RST_USB2_HCI]		= { 0x0, BIT(19) },

	[RST_USB0_PHY]		= { 0x4, BIT(17) },
	[RST_USB1_HSIC]		= { 0x4, BIT(18) },
	[RST_USB1_PHY]		= { 0x4, BIT(19) },
	[RST_USB2_HSIC]		= { 0x4, BIT(20) },
	[RST_USB2_PHY]		= { 0x4, BIT(21) },
};

static const struct sunxi_ccu_desc sun9i_a80_usb_clk_desc = {
	.ccu_clks	= sun9i_a80_usb_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun9i_a80_usb_clks),

	.hw_clks	= &sun9i_a80_usb_hw_clks,

	.resets		= sun9i_a80_usb_resets,
	.num_resets	= ARRAY_SIZE(sun9i_a80_usb_resets),
};

static int sun9i_a80_usb_clk_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct clk *bus_clk;
	void __iomem *reg;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reg = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	bus_clk = devm_clk_get(&pdev->dev, "bus");
	if (IS_ERR(bus_clk)) {
		ret = PTR_ERR(bus_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Couldn't get bus clk: %d\n", ret);
		return ret;
	}

	/* The bus clock needs to be enabled for us to access the registers */
	ret = clk_prepare_enable(bus_clk);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't enable bus clk: %d\n", ret);
		return ret;
	}

	ret = sunxi_ccu_probe(pdev->dev.of_node, reg,
			      &sun9i_a80_usb_clk_desc);
	if (ret)
		goto err_disable_clk;

	return 0;

err_disable_clk:
	clk_disable_unprepare(bus_clk);
	return ret;
}

static const struct of_device_id sun9i_a80_usb_clk_ids[] = {
	{ .compatible = "allwinner,sun9i-a80-usb-clks" },
	{ }
};

static struct platform_driver sun9i_a80_usb_clk_driver = {
	.probe	= sun9i_a80_usb_clk_probe,
	.driver	= {
		.name	= "sun9i-a80-usb-clks",
		.of_match_table	= sun9i_a80_usb_clk_ids,
	},
};
builtin_platform_driver(sun9i_a80_usb_clk_driver);
