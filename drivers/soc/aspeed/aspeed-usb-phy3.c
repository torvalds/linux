// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2023 Aspeed Technology Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <asm/io.h>
#include <linux/clk.h>
#include <linux/reset.h>

#define	AST_USB_PHY3S00		0x800	/* PHY SRAM Control/Status #1 */
#define AST_USB_PHY3S04		0x804	/* PHY SRAM Control/Status #2 */
#define AST_USB_PHY3C00		0x808	/* PHY PCS Control/Status #1 */
#define AST_USB_PHY3C04		0x80C	/* PHY PCS Control/Status #2 */
#define AST_USB_PHY3P00		0x810	/* PHY PCS Protocol Setting #1 */
#define AST_USB_PHY3P04		0x814	/* PHY PCS Protocol Setting #2 */
#define AST_USB_PHY3P08		0x818	/* PHY PCS Protocol Setting #3 */
#define AST_USB_PHY3P0C		0x81C	/* PHY PCS Protocol Setting #4	*/
#define AST_USB_DWC_CMD		0xB80	/* DWC3 Commands base address offest */

#define DWC_CRTL_NUM	2

#define USB_PHY3_INIT_DONE	BIT(15)	/* BIT15: USB3.1 Phy internal SRAM iniitalization done */
#define USB_PHY3_SRAM_BYPASS	BIT(7)	/* USB3.1 Phy SRAM bypass */
#define USB_PHY3_SRAM_EXT_LOAD	BIT(6)	/* USB3.1 Phy SRAM external load done */

struct usb_dwc3_ctrl {
	u32 offset;
	u32 value;
};

static const struct of_device_id aspeed_usb_phy3_dt_ids[] = {
	{
		.compatible = "aspeed,ast2700-phy3a",
	},
	{
		.compatible = "aspeed,ast2700-phy3b",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, aspeed_usb_phy3_dt_ids);

static int aspeed_usb_phy3_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	void __iomem *reg_base;
	u32 val;
	bool bypass_phy_sram_quirk;
	struct clk				*clk;
	struct reset_control	*rst;
	int timeout = 100;
	int rc = 0;
	struct usb_dwc3_ctrl ctrl_data[DWC_CRTL_NUM];
	int i, j;

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

	reg_base = of_iomap(node, 0);

	while ((readl(reg_base + AST_USB_PHY3S00) & USB_PHY3_INIT_DONE)
			!= USB_PHY3_INIT_DONE) {
		usleep_range(100, 110);
		if (--timeout == 0) {
			dev_err(&pdev->dev, "Wait phy3 init timed out\n");
			rc = -ETIMEDOUT;
			goto err;
		}
	}

	bypass_phy_sram_quirk =
		device_property_read_bool(&pdev->dev, "aspeed,bypass_phy_sram_quirk");

	val = readl(reg_base + AST_USB_PHY3S00);

	if (bypass_phy_sram_quirk)
		val |= USB_PHY3_SRAM_BYPASS;
	else
		val |= USB_PHY3_SRAM_EXT_LOAD;
	writel(val, reg_base + AST_USB_PHY3S00);

	/* Set PHY PCFGI[54]: protocol1_ext_rx_los_lfps_en for better compatibility */
	val = readl(reg_base + AST_USB_PHY3P04) | BIT(22);
	writel(val, reg_base + AST_USB_PHY3P04);

	rc = of_property_read_u32_array(node, "ctrl", (u32 *)ctrl_data,
					DWC_CRTL_NUM * 2);
	if (rc < 0) {
		dev_info(&pdev->dev, "No ctrl property to set\n");
		goto done;
	}

	/* xHCI DWC specific command initially set when PCIe xHCI enable */
	for (i = 0, j = AST_USB_DWC_CMD; i < DWC_CRTL_NUM; i++) {
		/* 48-bits Command:
		 * CMD1: Data -> DWC CMD [31:0], Address -> DWC CMD [47:32]
		 * CMD2: Data -> DWC CMD [79:48], Address -> DWC CMD [95:80]
		 * ... and etc.
		 */
		if (i % 2 == 0) {
			writel(ctrl_data[i].value, reg_base + j);
			j += 4;

			writel(ctrl_data[i].offset & 0xFFFF, reg_base + j);
		} else {
			val = readl(reg_base + j) & 0xFFFF;
			val |= ((ctrl_data[i].value & 0xFFFF) << 16);
			writel(val, reg_base + j);
			j += 4;

			val = (ctrl_data[i].offset << 16) | (ctrl_data[i].value >> 16);
			writel(val, reg_base + j);
			j += 4;
		}
	}
done:
	dev_info(&pdev->dev, "Initialized USB PHY3\n");

	return 0;

err:
	if (clk)
		clk_disable_unprepare(clk);
	return rc;
}

static int aspeed_usb_phy3_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver aspeed_usb_phy3_driver = {
	.probe		= aspeed_usb_phy3_probe,
	.remove		= aspeed_usb_phy3_remove,
	.driver		= {
		.name	= KBUILD_MODNAME,
		.of_match_table	= aspeed_usb_phy3_dt_ids,
	},
};
module_platform_driver(aspeed_usb_phy3_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joe Wang <joe_wang@aspeedtech.com>");
