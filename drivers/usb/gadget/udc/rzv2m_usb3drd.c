// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/V2M USB3DRD driver
 *
 * Copyright (C) 2022 Renesas Electronics Corporation
 */

#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/usb/rzv2m_usb3drd.h>

#define USB_PERI_DRD_CON	0x000

#define USB_PERI_DRD_CON_PERI_RST	BIT(31)
#define USB_PERI_DRD_CON_HOST_RST	BIT(30)
#define USB_PERI_DRD_CON_PERI_CON	BIT(24)

static void rzv2m_usb3drd_set_bit(struct rzv2m_usb3drd *usb3, u32 bits,
				  u32 offs)
{
	u32 val = readl(usb3->reg + offs);

	val |= bits;
	writel(val, usb3->reg + offs);
}

static void rzv2m_usb3drd_clear_bit(struct rzv2m_usb3drd *usb3, u32 bits,
				    u32 offs)
{
	u32 val = readl(usb3->reg + offs);

	val &= ~bits;
	writel(val, usb3->reg + offs);
}

void rzv2m_usb3drd_reset(struct device *dev, bool host)
{
	struct rzv2m_usb3drd *usb3 = dev_get_drvdata(dev);

	if (host) {
		rzv2m_usb3drd_clear_bit(usb3, USB_PERI_DRD_CON_PERI_CON,
					USB_PERI_DRD_CON);
		rzv2m_usb3drd_clear_bit(usb3, USB_PERI_DRD_CON_HOST_RST,
					USB_PERI_DRD_CON);
		rzv2m_usb3drd_set_bit(usb3, USB_PERI_DRD_CON_PERI_RST,
				      USB_PERI_DRD_CON);
	} else {
		rzv2m_usb3drd_set_bit(usb3, USB_PERI_DRD_CON_PERI_CON,
				      USB_PERI_DRD_CON);
		rzv2m_usb3drd_set_bit(usb3, USB_PERI_DRD_CON_HOST_RST,
				      USB_PERI_DRD_CON);
		rzv2m_usb3drd_clear_bit(usb3, USB_PERI_DRD_CON_PERI_RST,
					USB_PERI_DRD_CON);
	}
}
EXPORT_SYMBOL_GPL(rzv2m_usb3drd_reset);

static int rzv2m_usb3drd_remove(struct platform_device *pdev)
{
	struct rzv2m_usb3drd *usb3 = platform_get_drvdata(pdev);

	of_platform_depopulate(usb3->dev);
	pm_runtime_put(usb3->dev);
	pm_runtime_disable(&pdev->dev);
	reset_control_assert(usb3->drd_rstc);

	return 0;
}

static int rzv2m_usb3drd_probe(struct platform_device *pdev)
{
	struct rzv2m_usb3drd *usb3;
	int ret;

	usb3 = devm_kzalloc(&pdev->dev, sizeof(*usb3), GFP_KERNEL);
	if (!usb3)
		return -ENOMEM;

	usb3->dev = &pdev->dev;

	usb3->drd_irq = platform_get_irq_byname(pdev, "drd");
	if (usb3->drd_irq < 0)
		return usb3->drd_irq;

	usb3->reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(usb3->reg))
		return PTR_ERR(usb3->reg);

	platform_set_drvdata(pdev, usb3);

	usb3->drd_rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(usb3->drd_rstc))
		return dev_err_probe(&pdev->dev, PTR_ERR(usb3->drd_rstc),
				     "failed to get drd reset");

	reset_control_deassert(usb3->drd_rstc);
	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_resume_and_get(usb3->dev);
	if (ret)
		goto err_rst;

	ret = of_platform_populate(usb3->dev->of_node, NULL, NULL, usb3->dev);
	if (ret)
		goto err_pm;

	return 0;

err_pm:
	pm_runtime_put(usb3->dev);

err_rst:
	pm_runtime_disable(&pdev->dev);
	reset_control_assert(usb3->drd_rstc);
	return ret;
}

static const struct of_device_id rzv2m_usb3drd_of_match[] = {
	{ .compatible = "renesas,rzv2m-usb3drd", },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzv2m_usb3drd_of_match);

static struct platform_driver rzv2m_usb3drd_driver = {
	.driver = {
		.name = "rzv2m-usb3drd",
		.of_match_table = rzv2m_usb3drd_of_match,
	},
	.probe = rzv2m_usb3drd_probe,
	.remove = rzv2m_usb3drd_remove,
};
module_platform_driver(rzv2m_usb3drd_driver);

MODULE_AUTHOR("Biju Das <biju.das.jz@bp.renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/V2M USB3DRD driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rzv2m_usb3drd");
