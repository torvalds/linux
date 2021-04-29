// SPDX-License-Identifier: GPL-2.0
/*
 * dwc3-imx8mp.c - NXP imx8mp Specific Glue layer
 *
 * Copyright (c) 2020 NXP.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
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

struct dwc3_imx8mp {
	struct device			*dev;
	struct platform_device		*dwc3;
	void __iomem			*glue_base;
	struct clk			*hsio_clk;
	struct clk			*suspend_clk;
	int				irq;
	bool				pm_suspended;
	bool				wakeup_pending;
};

static void dwc3_imx8mp_wakeup_enable(struct dwc3_imx8mp *dwc3_imx)
{
	struct dwc3	*dwc3 = platform_get_drvdata(dwc3_imx->dwc3);
	u32		val;

	if (!dwc3)
		return;

	val = readl(dwc3_imx->glue_base + USB_WAKEUP_CTRL);

	if ((dwc3->current_dr_role == DWC3_GCTL_PRTCAP_HOST) && dwc3->xhci)
		val |= USB_WAKEUP_EN | USB_WAKEUP_SS_CONN |
		       USB_WAKEUP_U3_EN | USB_WAKEUP_DPDM_EN;
	else if (dwc3->current_dr_role == DWC3_GCTL_PRTCAP_DEVICE)
		val |= USB_WAKEUP_EN | USB_WAKEUP_VBUS_EN |
		       USB_WAKEUP_VBUS_SRC_SESS_VAL;

	writel(val, dwc3_imx->glue_base + USB_WAKEUP_CTRL);
}

static void dwc3_imx8mp_wakeup_disable(struct dwc3_imx8mp *dwc3_imx)
{
	u32 val;

	val = readl(dwc3_imx->glue_base + USB_WAKEUP_CTRL);
	val &= ~(USB_WAKEUP_EN | USB_WAKEUP_EN_MASK);
	writel(val, dwc3_imx->glue_base + USB_WAKEUP_CTRL);
}

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
	struct device_node	*dwc3_np, *node = dev->of_node;
	struct dwc3_imx8mp	*dwc3_imx;
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

	dwc3_imx->glue_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dwc3_imx->glue_base))
		return PTR_ERR(dwc3_imx->glue_base);

	dwc3_imx->hsio_clk = devm_clk_get(dev, "hsio");
	if (IS_ERR(dwc3_imx->hsio_clk)) {
		err = PTR_ERR(dwc3_imx->hsio_clk);
		dev_err(dev, "Failed to get hsio clk, err=%d\n", err);
		return err;
	}

	err = clk_prepare_enable(dwc3_imx->hsio_clk);
	if (err) {
		dev_err(dev, "Failed to enable hsio clk, err=%d\n", err);
		return err;
	}

	dwc3_imx->suspend_clk = devm_clk_get(dev, "suspend");
	if (IS_ERR(dwc3_imx->suspend_clk)) {
		err = PTR_ERR(dwc3_imx->suspend_clk);
		dev_err(dev, "Failed to get suspend clk, err=%d\n", err);
		goto disable_hsio_clk;
	}

	err = clk_prepare_enable(dwc3_imx->suspend_clk);
	if (err) {
		dev_err(dev, "Failed to enable suspend clk, err=%d\n", err);
		goto disable_hsio_clk;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		err = irq;
		goto disable_clks;
	}
	dwc3_imx->irq = irq;

	err = devm_request_threaded_irq(dev, irq, NULL, dwc3_imx8mp_interrupt,
					IRQF_ONESHOT, dev_name(dev), dwc3_imx);
	if (err) {
		dev_err(dev, "failed to request IRQ #%d --> %d\n", irq, err);
		goto disable_clks;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	err = pm_runtime_get_sync(dev);
	if (err < 0)
		goto disable_rpm;

	dwc3_np = of_get_child_by_name(node, "dwc3");
	if (!dwc3_np) {
		dev_err(dev, "failed to find dwc3 core child\n");
		goto disable_rpm;
	}

	err = of_platform_populate(node, NULL, NULL, dev);
	if (err) {
		dev_err(&pdev->dev, "failed to create dwc3 core\n");
		goto err_node_put;
	}

	dwc3_imx->dwc3 = of_find_device_by_node(dwc3_np);
	if (!dwc3_imx->dwc3) {
		dev_err(dev, "failed to get dwc3 platform device\n");
		err = -ENODEV;
		goto depopulate;
	}
	of_node_put(dwc3_np);

	device_set_wakeup_capable(dev, true);
	pm_runtime_put(dev);

	return 0;

depopulate:
	of_platform_depopulate(dev);
err_node_put:
	of_node_put(dwc3_np);
disable_rpm:
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
disable_clks:
	clk_disable_unprepare(dwc3_imx->suspend_clk);
disable_hsio_clk:
	clk_disable_unprepare(dwc3_imx->hsio_clk);

	return err;
}

static int dwc3_imx8mp_remove(struct platform_device *pdev)
{
	struct dwc3_imx8mp *dwc3_imx = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	pm_runtime_get_sync(dev);
	of_platform_depopulate(dev);

	clk_disable_unprepare(dwc3_imx->suspend_clk);
	clk_disable_unprepare(dwc3_imx->hsio_clk);

	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static int __maybe_unused dwc3_imx8mp_suspend(struct dwc3_imx8mp *dwc3_imx,
					      pm_message_t msg)
{
	if (dwc3_imx->pm_suspended)
		return 0;

	/* Wakeup enable */
	if (PMSG_IS_AUTO(msg) || device_may_wakeup(dwc3_imx->dev))
		dwc3_imx8mp_wakeup_enable(dwc3_imx);

	dwc3_imx->pm_suspended = true;

	return 0;
}

static int __maybe_unused dwc3_imx8mp_resume(struct dwc3_imx8mp *dwc3_imx,
					     pm_message_t msg)
{
	struct dwc3	*dwc = platform_get_drvdata(dwc3_imx->dwc3);
	int ret = 0;

	if (!dwc3_imx->pm_suspended)
		return 0;

	/* Wakeup disable */
	dwc3_imx8mp_wakeup_disable(dwc3_imx);
	dwc3_imx->pm_suspended = false;

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

static int __maybe_unused dwc3_imx8mp_pm_suspend(struct device *dev)
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

static int __maybe_unused dwc3_imx8mp_pm_resume(struct device *dev)
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
	if (ret)
		return ret;

	ret = dwc3_imx8mp_resume(dwc3_imx, PMSG_RESUME);

	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	dev_dbg(dev, "dwc3 imx8mp pm resume.\n");

	return ret;
}

static int __maybe_unused dwc3_imx8mp_runtime_suspend(struct device *dev)
{
	struct dwc3_imx8mp *dwc3_imx = dev_get_drvdata(dev);

	dev_dbg(dev, "dwc3 imx8mp runtime suspend.\n");

	return dwc3_imx8mp_suspend(dwc3_imx, PMSG_AUTO_SUSPEND);
}

static int __maybe_unused dwc3_imx8mp_runtime_resume(struct device *dev)
{
	struct dwc3_imx8mp *dwc3_imx = dev_get_drvdata(dev);

	dev_dbg(dev, "dwc3 imx8mp runtime resume.\n");

	return dwc3_imx8mp_resume(dwc3_imx, PMSG_AUTO_RESUME);
}

static const struct dev_pm_ops dwc3_imx8mp_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_imx8mp_pm_suspend, dwc3_imx8mp_pm_resume)
	SET_RUNTIME_PM_OPS(dwc3_imx8mp_runtime_suspend,
			   dwc3_imx8mp_runtime_resume, NULL)
};

static const struct of_device_id dwc3_imx8mp_of_match[] = {
	{ .compatible = "fsl,imx8mp-dwc3", },
	{},
};
MODULE_DEVICE_TABLE(of, dwc3_imx8mp_of_match);

static struct platform_driver dwc3_imx8mp_driver = {
	.probe		= dwc3_imx8mp_probe,
	.remove		= dwc3_imx8mp_remove,
	.driver		= {
		.name	= "imx8mp-dwc3",
		.pm	= &dwc3_imx8mp_dev_pm_ops,
		.of_match_table	= dwc3_imx8mp_of_match,
	},
};

module_platform_driver(dwc3_imx8mp_driver);

MODULE_ALIAS("platform:imx8mp-dwc3");
MODULE_AUTHOR("jun.li@nxp.com");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 imx8mp Glue Layer");
