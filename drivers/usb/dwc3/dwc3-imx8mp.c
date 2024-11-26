// SPDX-License-Identifier: GPL-2.0
/*
 * dwc3-imx8mp.c - NXP imx8mp Specific Glue layer
 *
 * Copyright (c) 2020 NXP.
 */

#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "core.h"

/* USB wakeup registers */
#define USB_WAKEUP_CTRL			0x00

/* Global wakeup interrupt enable, also used to clear interrupt */
#define USB_WAKEUP_EN			BIT(31)
/* Wakeup from connect or disconnect, only for superspeed */
#define USB_WAKEUP_SS_CONN		BIT(5)
/* 0 select vbus_valid, 1 select sessvld */
#define USB_WAKEUP_VBUS_SRC_SESS_VAL	BIT(4)
/* Enable signal for wake up from u3 state */
#define USB_WAKEUP_U3_EN		BIT(3)
/* Enable signal for wake up from id change */
#define USB_WAKEUP_ID_EN		BIT(2)
/* Enable signal for wake up from vbus change */
#define	USB_WAKEUP_VBUS_EN		BIT(1)
/* Enable signal for wake up from dp/dm change */
#define USB_WAKEUP_DPDM_EN		BIT(0)

#define USB_WAKEUP_EN_MASK		GENMASK(5, 0)

/* USB glue registers */
#define USB_CTRL0		0x00
#define USB_CTRL1		0x04

#define USB_CTRL0_PORTPWR_EN	BIT(12) /* 1 - PPC enabled (default) */
#define USB_CTRL0_USB3_FIXED	BIT(22) /* 1 - USB3 permanent attached */
#define USB_CTRL0_USB2_FIXED	BIT(23) /* 1 - USB2 permanent attached */

#define USB_CTRL1_OC_POLARITY	BIT(16) /* 0 - HIGH / 1 - LOW */
#define USB_CTRL1_PWR_POLARITY	BIT(17) /* 0 - HIGH / 1 - LOW */

struct dwc3_imx8mp {
	struct device			*dev;
	struct platform_device		*dwc3;
	void __iomem			*hsio_blk_base;
	void __iomem			*glue_base;
	struct clk			*hsio_clk;
	struct clk			*suspend_clk;
	int				irq;
	bool				pm_suspended;
	bool				wakeup_pending;
};

static void imx8mp_configure_glue(struct dwc3_imx8mp *dwc3_imx)
{
	struct device *dev = dwc3_imx->dev;
	u32 value;

	if (!dwc3_imx->glue_base)
		return;

	value = readl(dwc3_imx->glue_base + USB_CTRL0);

	if (device_property_read_bool(dev, "fsl,permanently-attached"))
		value |= (USB_CTRL0_USB2_FIXED | USB_CTRL0_USB3_FIXED);
	else
		value &= ~(USB_CTRL0_USB2_FIXED | USB_CTRL0_USB3_FIXED);

	if (device_property_read_bool(dev, "fsl,disable-port-power-control"))
		value &= ~(USB_CTRL0_PORTPWR_EN);
	else
		value |= USB_CTRL0_PORTPWR_EN;

	writel(value, dwc3_imx->glue_base + USB_CTRL0);

	value = readl(dwc3_imx->glue_base + USB_CTRL1);
	if (device_property_read_bool(dev, "fsl,over-current-active-low"))
		value |= USB_CTRL1_OC_POLARITY;
	else
		value &= ~USB_CTRL1_OC_POLARITY;

	if (device_property_read_bool(dev, "fsl,power-active-low"))
		value |= USB_CTRL1_PWR_POLARITY;
	else
		value &= ~USB_CTRL1_PWR_POLARITY;

	writel(value, dwc3_imx->glue_base + USB_CTRL1);
}

static void dwc3_imx8mp_wakeup_enable(struct dwc3_imx8mp *dwc3_imx,
				      pm_message_t msg)
{
	struct dwc3	*dwc3 = platform_get_drvdata(dwc3_imx->dwc3);
	u32		val;

	if (!dwc3)
		return;

	val = readl(dwc3_imx->hsio_blk_base + USB_WAKEUP_CTRL);

	if ((dwc3->current_dr_role == DWC3_GCTL_PRTCAP_HOST) && dwc3->xhci) {
		val |= USB_WAKEUP_EN | USB_WAKEUP_DPDM_EN;
		if (PMSG_IS_AUTO(msg))
			val |= USB_WAKEUP_SS_CONN | USB_WAKEUP_U3_EN;
	} else {
		val |= USB_WAKEUP_EN | USB_WAKEUP_VBUS_EN |
		       USB_WAKEUP_VBUS_SRC_SESS_VAL;
	}

	writel(val, dwc3_imx->hsio_blk_base + USB_WAKEUP_CTRL);
}

static void dwc3_imx8mp_wakeup_disable(struct dwc3_imx8mp *dwc3_imx)
{
	u32 val;

	val = readl(dwc3_imx->hsio_blk_base + USB_WAKEUP_CTRL);
	val &= ~(USB_WAKEUP_EN | USB_WAKEUP_EN_MASK);
	writel(val, dwc3_imx->hsio_blk_base + USB_WAKEUP_CTRL);
}

static const struct property_entry dwc3_imx8mp_properties[] = {
	PROPERTY_ENTRY_BOOL("xhci-missing-cas-quirk"),
	PROPERTY_ENTRY_BOOL("xhci-skip-phy-init-quirk"),
	{},
};

static const struct software_node dwc3_imx8mp_swnode = {
	.properties = dwc3_imx8mp_properties,
};

static irqreturn_t dwc3_imx8mp_interrupt(int irq, void *_dwc3_imx)
{
	struct dwc3_imx8mp	*dwc3_imx = _dwc3_imx;
	struct dwc3		*dwc = platform_get_drvdata(dwc3_imx->dwc3);

	if (!dwc3_imx->pm_suspended)
		return IRQ_HANDLED;

	disable_irq_nosync(dwc3_imx->irq);
	dwc3_imx->wakeup_pending = true;

	if ((dwc->current_dr_role == DWC3_GCTL_PRTCAP_HOST) && dwc->xhci)
		pm_runtime_resume(&dwc->xhci->dev);
	else if (dwc->current_dr_role == DWC3_GCTL_PRTCAP_DEVICE)
		pm_runtime_get(dwc->dev);

	return IRQ_HANDLED;
}

static int dwc3_imx8mp_probe(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	struct device_node	*node = dev->of_node;
	struct dwc3_imx8mp	*dwc3_imx;
	struct resource		*res;
	int			err, irq;

	if (!node) {
		dev_err(dev, "device node not found\n");
		return -EINVAL;
	}

	dwc3_imx = devm_kzalloc(dev, sizeof(*dwc3_imx), GFP_KERNEL);
	if (!dwc3_imx)
		return -ENOMEM;

	platform_set_drvdata(pdev, dwc3_imx);

	dwc3_imx->dev = dev;

	dwc3_imx->hsio_blk_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dwc3_imx->hsio_blk_base))
		return PTR_ERR(dwc3_imx->hsio_blk_base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_warn(dev, "Base address for glue layer missing. Continuing without, some features are missing though.");
	} else {
		dwc3_imx->glue_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(dwc3_imx->glue_base))
			return PTR_ERR(dwc3_imx->glue_base);
	}

	dwc3_imx->hsio_clk = devm_clk_get_enabled(dev, "hsio");
	if (IS_ERR(dwc3_imx->hsio_clk))
		return dev_err_probe(dev, PTR_ERR(dwc3_imx->hsio_clk),
				     "Failed to get hsio clk\n");

	dwc3_imx->suspend_clk = devm_clk_get_enabled(dev, "suspend");
	if (IS_ERR(dwc3_imx->suspend_clk))
		return dev_err_probe(dev, PTR_ERR(dwc3_imx->suspend_clk),
				     "Failed to get suspend clk\n");

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	dwc3_imx->irq = irq;

	struct device_node *dwc3_np __free(device_node) = of_get_compatible_child(node,
										  "snps,dwc3");
	if (!dwc3_np)
		return dev_err_probe(dev, -ENODEV, "failed to find dwc3 core child\n");

	imx8mp_configure_glue(dwc3_imx);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	err = pm_runtime_get_sync(dev);
	if (err < 0)
		goto disable_rpm;

	err = device_add_software_node(dev, &dwc3_imx8mp_swnode);
	if (err) {
		err = -ENODEV;
		dev_err(dev, "failed to add software node\n");
		goto disable_rpm;
	}

	err = of_platform_populate(node, NULL, NULL, dev);
	if (err) {
		dev_err(&pdev->dev, "failed to create dwc3 core\n");
		goto remove_swnode;
	}

	dwc3_imx->dwc3 = of_find_device_by_node(dwc3_np);
	if (!dwc3_imx->dwc3) {
		dev_err(dev, "failed to get dwc3 platform device\n");
		err = -ENODEV;
		goto depopulate;
	}

	err = devm_request_threaded_irq(dev, irq, NULL, dwc3_imx8mp_interrupt,
					IRQF_ONESHOT, dev_name(dev), dwc3_imx);
	if (err) {
		dev_err(dev, "failed to request IRQ #%d --> %d\n", irq, err);
		goto depopulate;
	}

	device_set_wakeup_capable(dev, true);
	pm_runtime_put(dev);

	return 0;

depopulate:
	of_platform_depopulate(dev);
remove_swnode:
	device_remove_software_node(dev);
disable_rpm:
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);

	return err;
}

static void dwc3_imx8mp_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	pm_runtime_get_sync(dev);
	of_platform_depopulate(dev);
	device_remove_software_node(dev);

	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
}

static int dwc3_imx8mp_suspend(struct dwc3_imx8mp *dwc3_imx, pm_message_t msg)
{
	if (dwc3_imx->pm_suspended)
		return 0;

	/* Wakeup enable */
	if (PMSG_IS_AUTO(msg) || device_may_wakeup(dwc3_imx->dev))
		dwc3_imx8mp_wakeup_enable(dwc3_imx, msg);

	dwc3_imx->pm_suspended = true;

	return 0;
}

static int dwc3_imx8mp_resume(struct dwc3_imx8mp *dwc3_imx, pm_message_t msg)
{
	struct dwc3	*dwc = platform_get_drvdata(dwc3_imx->dwc3);
	int ret = 0;

	if (!dwc3_imx->pm_suspended)
		return 0;

	/* Wakeup disable */
	dwc3_imx8mp_wakeup_disable(dwc3_imx);
	dwc3_imx->pm_suspended = false;

	/* Upon power loss any previous configuration is lost, restore it */
	imx8mp_configure_glue(dwc3_imx);

	if (dwc3_imx->wakeup_pending) {
		dwc3_imx->wakeup_pending = false;
		if (dwc->current_dr_role == DWC3_GCTL_PRTCAP_DEVICE) {
			pm_runtime_mark_last_busy(dwc->dev);
			pm_runtime_put_autosuspend(dwc->dev);
		} else {
			/*
			 * Add wait for xhci switch from suspend
			 * clock to normal clock to detect connection.
			 */
			usleep_range(9000, 10000);
		}
		enable_irq(dwc3_imx->irq);
	}

	return ret;
}

static int dwc3_imx8mp_pm_suspend(struct device *dev)
{
	struct dwc3_imx8mp *dwc3_imx = dev_get_drvdata(dev);
	int ret;

	ret = dwc3_imx8mp_suspend(dwc3_imx, PMSG_SUSPEND);

	if (device_may_wakeup(dwc3_imx->dev))
		enable_irq_wake(dwc3_imx->irq);
	else
		clk_disable_unprepare(dwc3_imx->suspend_clk);

	clk_disable_unprepare(dwc3_imx->hsio_clk);
	dev_dbg(dev, "dwc3 imx8mp pm suspend.\n");

	return ret;
}

static int dwc3_imx8mp_pm_resume(struct device *dev)
{
	struct dwc3_imx8mp *dwc3_imx = dev_get_drvdata(dev);
	int ret;

	if (device_may_wakeup(dwc3_imx->dev)) {
		disable_irq_wake(dwc3_imx->irq);
	} else {
		ret = clk_prepare_enable(dwc3_imx->suspend_clk);
		if (ret)
			return ret;
	}

	ret = clk_prepare_enable(dwc3_imx->hsio_clk);
	if (ret) {
		clk_disable_unprepare(dwc3_imx->suspend_clk);
		return ret;
	}

	ret = dwc3_imx8mp_resume(dwc3_imx, PMSG_RESUME);

	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	dev_dbg(dev, "dwc3 imx8mp pm resume.\n");

	return ret;
}

static int dwc3_imx8mp_runtime_suspend(struct device *dev)
{
	struct dwc3_imx8mp *dwc3_imx = dev_get_drvdata(dev);

	dev_dbg(dev, "dwc3 imx8mp runtime suspend.\n");

	return dwc3_imx8mp_suspend(dwc3_imx, PMSG_AUTO_SUSPEND);
}

static int dwc3_imx8mp_runtime_resume(struct device *dev)
{
	struct dwc3_imx8mp *dwc3_imx = dev_get_drvdata(dev);

	dev_dbg(dev, "dwc3 imx8mp runtime resume.\n");

	return dwc3_imx8mp_resume(dwc3_imx, PMSG_AUTO_RESUME);
}

static const struct dev_pm_ops dwc3_imx8mp_dev_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(dwc3_imx8mp_pm_suspend, dwc3_imx8mp_pm_resume)
	RUNTIME_PM_OPS(dwc3_imx8mp_runtime_suspend, dwc3_imx8mp_runtime_resume,
		       NULL)
};

static const struct of_device_id dwc3_imx8mp_of_match[] = {
	{ .compatible = "fsl,imx8mp-dwc3", },
	{},
};
MODULE_DEVICE_TABLE(of, dwc3_imx8mp_of_match);

static struct platform_driver dwc3_imx8mp_driver = {
	.probe		= dwc3_imx8mp_probe,
	.remove_new	= dwc3_imx8mp_remove,
	.driver		= {
		.name	= "imx8mp-dwc3",
		.pm	= pm_ptr(&dwc3_imx8mp_dev_pm_ops),
		.of_match_table	= dwc3_imx8mp_of_match,
	},
};

module_platform_driver(dwc3_imx8mp_driver);

MODULE_ALIAS("platform:imx8mp-dwc3");
MODULE_AUTHOR("jun.li@nxp.com");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 imx8mp Glue Layer");
