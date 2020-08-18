// SPDX-License-Identifier: GPL-2.0
/**
 * dwc3-keystone.c - Keystone Specific Glue layer
 *
 * Copyright (C) 2010-2013 Texas Instruments Incorporated - https://www.ti.com
 *
 * Author: WingMan Kwok <w-kwok2@ti.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/pm_runtime.h>

/* USBSS register offsets */
#define USBSS_REVISION		0x0000
#define USBSS_SYSCONFIG		0x0010
#define USBSS_IRQ_EOI		0x0018
#define USBSS_IRQSTATUS_RAW_0	0x0020
#define USBSS_IRQSTATUS_0	0x0024
#define USBSS_IRQENABLE_SET_0	0x0028
#define USBSS_IRQENABLE_CLR_0	0x002c

/* IRQ register bits */
#define USBSS_IRQ_EOI_LINE(n)	BIT(n)
#define USBSS_IRQ_EVENT_ST	BIT(0)
#define USBSS_IRQ_COREIRQ_EN	BIT(0)
#define USBSS_IRQ_COREIRQ_CLR	BIT(0)

struct dwc3_keystone {
	struct device			*dev;
	void __iomem			*usbss;
	struct phy			*usb3_phy;
};

static inline u32 kdwc3_readl(void __iomem *base, u32 offset)
{
	return readl(base + offset);
}

static inline void kdwc3_writel(void __iomem *base, u32 offset, u32 value)
{
	writel(value, base + offset);
}

static void kdwc3_enable_irqs(struct dwc3_keystone *kdwc)
{
	u32 val;

	val = kdwc3_readl(kdwc->usbss, USBSS_IRQENABLE_SET_0);
	val |= USBSS_IRQ_COREIRQ_EN;
	kdwc3_writel(kdwc->usbss, USBSS_IRQENABLE_SET_0, val);
}

static void kdwc3_disable_irqs(struct dwc3_keystone *kdwc)
{
	u32 val;

	val = kdwc3_readl(kdwc->usbss, USBSS_IRQENABLE_SET_0);
	val &= ~USBSS_IRQ_COREIRQ_EN;
	kdwc3_writel(kdwc->usbss, USBSS_IRQENABLE_SET_0, val);
}

static irqreturn_t dwc3_keystone_interrupt(int irq, void *_kdwc)
{
	struct dwc3_keystone	*kdwc = _kdwc;

	kdwc3_writel(kdwc->usbss, USBSS_IRQENABLE_CLR_0, USBSS_IRQ_COREIRQ_CLR);
	kdwc3_writel(kdwc->usbss, USBSS_IRQSTATUS_0, USBSS_IRQ_EVENT_ST);
	kdwc3_writel(kdwc->usbss, USBSS_IRQENABLE_SET_0, USBSS_IRQ_COREIRQ_EN);
	kdwc3_writel(kdwc->usbss, USBSS_IRQ_EOI, USBSS_IRQ_EOI_LINE(0));

	return IRQ_HANDLED;
}

static int kdwc3_probe(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	struct device_node	*node = pdev->dev.of_node;
	struct dwc3_keystone	*kdwc;
	int			error, irq;

	kdwc = devm_kzalloc(dev, sizeof(*kdwc), GFP_KERNEL);
	if (!kdwc)
		return -ENOMEM;

	platform_set_drvdata(pdev, kdwc);

	kdwc->dev = dev;

	kdwc->usbss = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(kdwc->usbss))
		return PTR_ERR(kdwc->usbss);

	/* PSC dependency on AM65 needs SERDES0 to be powered before USB0 */
	kdwc->usb3_phy = devm_phy_optional_get(dev, "usb3-phy");
	if (IS_ERR(kdwc->usb3_phy)) {
		error = PTR_ERR(kdwc->usb3_phy);
		if (error != -EPROBE_DEFER)
			dev_err(dev, "couldn't get usb3 phy: %d\n", error);

		return error;
	}

	phy_pm_runtime_get_sync(kdwc->usb3_phy);

	error = phy_reset(kdwc->usb3_phy);
	if (error < 0) {
		dev_err(dev, "usb3 phy reset failed: %d\n", error);
		return error;
	}

	error = phy_init(kdwc->usb3_phy);
	if (error < 0) {
		dev_err(dev, "usb3 phy init failed: %d\n", error);
		return error;
	}

	error = phy_power_on(kdwc->usb3_phy);
	if (error < 0) {
		dev_err(dev, "usb3 phy power on failed: %d\n", error);
		phy_exit(kdwc->usb3_phy);
		return error;
	}

	pm_runtime_enable(kdwc->dev);
	error = pm_runtime_get_sync(kdwc->dev);
	if (error < 0) {
		dev_err(kdwc->dev, "pm_runtime_get_sync failed, error %d\n",
			error);
		goto err_irq;
	}

	/* IRQ processing not required currently for AM65 */
	if (of_device_is_compatible(node, "ti,am654-dwc3"))
		goto skip_irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		error = irq;
		goto err_irq;
	}

	error = devm_request_irq(dev, irq, dwc3_keystone_interrupt, IRQF_SHARED,
			dev_name(dev), kdwc);
	if (error) {
		dev_err(dev, "failed to request IRQ #%d --> %d\n",
				irq, error);
		goto err_irq;
	}

	kdwc3_enable_irqs(kdwc);

skip_irq:
	error = of_platform_populate(node, NULL, NULL, dev);
	if (error) {
		dev_err(&pdev->dev, "failed to create dwc3 core\n");
		goto err_core;
	}

	return 0;

err_core:
	kdwc3_disable_irqs(kdwc);
err_irq:
	pm_runtime_put_sync(kdwc->dev);
	pm_runtime_disable(kdwc->dev);
	phy_power_off(kdwc->usb3_phy);
	phy_exit(kdwc->usb3_phy);
	phy_pm_runtime_put_sync(kdwc->usb3_phy);

	return error;
}

static int kdwc3_remove_core(struct device *dev, void *c)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);

	return 0;
}

static int kdwc3_remove(struct platform_device *pdev)
{
	struct dwc3_keystone *kdwc = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;

	if (!of_device_is_compatible(node, "ti,am654-dwc3"))
		kdwc3_disable_irqs(kdwc);

	device_for_each_child(&pdev->dev, NULL, kdwc3_remove_core);
	pm_runtime_put_sync(kdwc->dev);
	pm_runtime_disable(kdwc->dev);

	phy_power_off(kdwc->usb3_phy);
	phy_exit(kdwc->usb3_phy);
	phy_pm_runtime_put_sync(kdwc->usb3_phy);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id kdwc3_of_match[] = {
	{ .compatible = "ti,keystone-dwc3", },
	{ .compatible = "ti,am654-dwc3" },
	{},
};
MODULE_DEVICE_TABLE(of, kdwc3_of_match);

static struct platform_driver kdwc3_driver = {
	.probe		= kdwc3_probe,
	.remove		= kdwc3_remove,
	.driver		= {
		.name	= "keystone-dwc3",
		.of_match_table	= kdwc3_of_match,
	},
};

module_platform_driver(kdwc3_driver);

MODULE_ALIAS("platform:keystone-dwc3");
MODULE_AUTHOR("WingMan Kwok <w-kwok2@ti.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 KEYSTONE Glue Layer");
