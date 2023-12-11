// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2021 Aspeed Technology Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <asm/io.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/mfd/syscon.h>

static const struct of_device_id aspeed_usb_ahp_dt_ids[] = {
	{
		.compatible = "aspeed,ast2600-usb2ahp",
	},
	{
		.compatible = "aspeed,ast2700-usb3ahp",
	},
	{
		.compatible = "aspeed,ast2700-usb3bhp",
	},
		{
		.compatible = "aspeed,ast2700-usb2ahp",
	},
	{
		.compatible = "aspeed,ast2700-usb2bhp",
	},
};
MODULE_DEVICE_TABLE(of, aspeed_usb_ahp_dt_ids);

static int aspeed_usb_ahp_probe(struct platform_device *pdev)
{
	struct clk		*clk;
	struct reset_control	*rst;
	struct regmap		*device;
	bool is_pcie_xhci;
	int rc = 0;

	if (of_device_is_compatible(pdev->dev.of_node,
				    "ast2600-usb2ahp")) {
		dev_info(&pdev->dev, "Initialized AST2600 USB2AHP\n");
		return 0;
	}

	if (of_device_is_compatible(pdev->dev.of_node,
				    "aspeed,ast2700-usb3ahp") ||
	    of_device_is_compatible(pdev->dev.of_node,
				    "aspeed,ast2700-usb3bhp")) {
		is_pcie_xhci = true;
	} else if (of_device_is_compatible(pdev->dev.of_node,
					   "aspeed,ast2700-usb2ahp") ||
		   of_device_is_compatible(pdev->dev.of_node,
					   "aspeed,ast2700-usb2bhp")) {
		is_pcie_xhci = false;
	}
	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	rc = clk_prepare_enable(clk);
	if (rc) {
		dev_err(&pdev->dev, "Unable to enable clock (%d)\n", rc);
		return rc;
	}

	rst = devm_reset_control_get_shared(&pdev->dev, NULL);
	if (IS_ERR(rst)) {
		rc = PTR_ERR(rst);
		goto err;
	}
	rc = reset_control_deassert(rst);
	if (rc)
		goto err;

	device = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "aspeed,device");
	if (IS_ERR(device)) {
		dev_err(&pdev->dev, "failed to find device regmap\n");
		goto err;
	}

	if (is_pcie_xhci) {
		//EnPCIaMSI_EnPCIaIntA_EnPCIaMst_EnPCIaDev
		/* Turn on PCIe xHCI without MSI */
		regmap_update_bits(device, 0x70,
				   BIT(19) | BIT(11) | BIT(3),
				   BIT(19) | BIT(11) | BIT(3));
	} else {
		//EnPCIaMSI_EnPCIaIntA_EnPCIaMst_EnPCIaDev
		/* Turn on PCIe EHCI without MSI */
		regmap_update_bits(device, 0x70,
				   BIT(18) | BIT(10) | BIT(2),
				   BIT(18) | BIT(10) | BIT(2));
	}
	dev_info(&pdev->dev, "Initialized AST2700 USB Host PCIe\n");
	return 0;
err:
	if (clk)
		clk_disable_unprepare(clk);
	return rc;
}

static int aspeed_usb_ahp_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "Remove USB Host PCIe\n");

	return 0;
}

static struct platform_driver aspeed_usb_ahp_driver = {
	.probe		= aspeed_usb_ahp_probe,
	.remove		= aspeed_usb_ahp_remove,
	.driver		= {
		.name	= KBUILD_MODNAME,
		.of_match_table	= aspeed_usb_ahp_dt_ids,
	},
};
module_platform_driver(aspeed_usb_ahp_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Neal Liu <neal_liu@aspeedtech.com>");
