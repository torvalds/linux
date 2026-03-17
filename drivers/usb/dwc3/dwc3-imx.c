// SPDX-License-Identifier: GPL-2.0
/*
 * dwc3-imx.c - NXP i.MX Soc USB3 Specific Glue layer
 *
 * Copyright 2026 NXP
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
#include "glue.h"

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

struct dwc3_imx {
	struct dwc3	dwc;
	struct device	*dev;
	void __iomem	*blkctl_base;
	void __iomem	*glue_base;
	struct clk	*hsio_clk;
	struct clk	*suspend_clk;
	int		irq;
	bool		pm_suspended;
	bool		wakeup_pending;
	unsigned	permanent_attached:1;
	unsigned	disable_pwr_ctrl:1;
	unsigned	overcur_active_low:1;
	unsigned	power_active_low:1;
};

#define to_dwc3_imx(d) container_of((d), struct dwc3_imx, dwc)

static void dwc3_imx_get_property(struct dwc3_imx *dwc_imx)
{
	struct device	*dev = dwc_imx->dev;

	dwc_imx->permanent_attached =
		device_property_read_bool(dev, "fsl,permanently-attached");
	dwc_imx->disable_pwr_ctrl =
		device_property_read_bool(dev, "fsl,disable-port-power-control");
	dwc_imx->overcur_active_low =
		device_property_read_bool(dev, "fsl,over-current-active-low");
	dwc_imx->power_active_low =
		device_property_read_bool(dev, "fsl,power-active-low");
}

static void dwc3_imx_configure_glue(struct dwc3_imx *dwc_imx)
{
	u32		value;

	if (!dwc_imx->glue_base)
		return;

	value = readl(dwc_imx->glue_base + USB_CTRL0);

	if (dwc_imx->permanent_attached)
		value |= USB_CTRL0_USB2_FIXED | USB_CTRL0_USB3_FIXED;
	else
		value &= ~(USB_CTRL0_USB2_FIXED | USB_CTRL0_USB3_FIXED);

	if (dwc_imx->disable_pwr_ctrl)
		value &= ~USB_CTRL0_PORTPWR_EN;
	else
		value |= USB_CTRL0_PORTPWR_EN;

	writel(value, dwc_imx->glue_base + USB_CTRL0);

	value = readl(dwc_imx->glue_base + USB_CTRL1);
	if (dwc_imx->overcur_active_low)
		value |= USB_CTRL1_OC_POLARITY;
	else
		value &= ~USB_CTRL1_OC_POLARITY;

	if (dwc_imx->power_active_low)
		value |= USB_CTRL1_PWR_POLARITY;
	else
		value &= ~USB_CTRL1_PWR_POLARITY;

	writel(value, dwc_imx->glue_base + USB_CTRL1);
}

static void dwc3_imx_wakeup_enable(struct dwc3_imx *dwc_imx, pm_message_t msg)
{
	struct dwc3	*dwc = &dwc_imx->dwc;
	u32		val;

	val = readl(dwc_imx->blkctl_base + USB_WAKEUP_CTRL);

	if (dwc->current_dr_role == DWC3_GCTL_PRTCAP_HOST && dwc->xhci) {
		val |= USB_WAKEUP_EN | USB_WAKEUP_DPDM_EN;
		if (PMSG_IS_AUTO(msg))
			val |= USB_WAKEUP_SS_CONN | USB_WAKEUP_U3_EN;
	} else {
		val |= USB_WAKEUP_EN | USB_WAKEUP_VBUS_EN |
		       USB_WAKEUP_VBUS_SRC_SESS_VAL;
	}

	writel(val, dwc_imx->blkctl_base + USB_WAKEUP_CTRL);
}

static void dwc3_imx_wakeup_disable(struct dwc3_imx *dwc_imx)
{
	u32	val;

	val = readl(dwc_imx->blkctl_base + USB_WAKEUP_CTRL);
	val &= ~(USB_WAKEUP_EN | USB_WAKEUP_EN_MASK);
	writel(val, dwc_imx->blkctl_base + USB_WAKEUP_CTRL);
}

static irqreturn_t dwc3_imx_interrupt(int irq, void *data)
{
	struct dwc3_imx	*dwc_imx = data;
	struct dwc3	*dwc = &dwc_imx->dwc;

	if (!dwc_imx->pm_suspended)
		return IRQ_HANDLED;

	disable_irq_nosync(dwc_imx->irq);
	dwc_imx->wakeup_pending = true;

	if (dwc->current_dr_role == DWC3_GCTL_PRTCAP_HOST && dwc->xhci)
		pm_runtime_resume(&dwc->xhci->dev);
	else if (dwc->current_dr_role == DWC3_GCTL_PRTCAP_DEVICE)
		pm_runtime_get(dwc->dev);

	return IRQ_HANDLED;
}

static void dwc3_imx_pre_set_role(struct dwc3 *dwc, enum usb_role role)
{
	if (role == USB_ROLE_HOST)
		/*
		 * For xhci host, we need disable dwc core auto
		 * suspend, because during this auto suspend delay(5s),
		 * xhci host RUN_STOP is cleared and wakeup is not
		 * enabled, if device is inserted, xhci host can't
		 * response the connection.
		 */
		pm_runtime_dont_use_autosuspend(dwc->dev);
	else
		pm_runtime_use_autosuspend(dwc->dev);
}

static struct dwc3_glue_ops dwc3_imx_glue_ops = {
	.pre_set_role = dwc3_imx_pre_set_role,
};

static const struct property_entry dwc3_imx_properties[] = {
	PROPERTY_ENTRY_BOOL("xhci-missing-cas-quirk"),
	PROPERTY_ENTRY_BOOL("xhci-skip-phy-init-quirk"),
	{},
};

static const struct software_node dwc3_imx_swnode = {
	.properties = dwc3_imx_properties,
};

static int dwc3_imx_probe(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	struct dwc3_imx		*dwc_imx;
	struct dwc3		*dwc;
	struct resource		*res;
	const char		*irq_name;
	struct dwc3_probe_data	probe_data = {};
	int			ret, irq;

	dwc_imx = devm_kzalloc(dev, sizeof(*dwc_imx), GFP_KERNEL);
	if (!dwc_imx)
		return -ENOMEM;

	platform_set_drvdata(pdev, dwc_imx);
	dwc_imx->dev = dev;

	dwc3_imx_get_property(dwc_imx);

	dwc_imx->blkctl_base = devm_platform_ioremap_resource_byname(pdev, "blkctl");
	if (IS_ERR(dwc_imx->blkctl_base))
		return PTR_ERR(dwc_imx->blkctl_base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "glue");
	if (!res) {
		dev_warn(dev, "Base address for glue layer missing\n");
	} else {
		dwc_imx->glue_base = devm_ioremap_resource(dev, res);
		if (IS_ERR(dwc_imx->glue_base))
			return PTR_ERR(dwc_imx->glue_base);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "core");
	if (!res)
		return dev_err_probe(dev, -ENODEV, "missing core memory resource\n");

	dwc_imx->hsio_clk = devm_clk_get_enabled(dev, "hsio");
	if (IS_ERR(dwc_imx->hsio_clk))
		return dev_err_probe(dev, PTR_ERR(dwc_imx->hsio_clk),
				     "Failed to get hsio clk\n");

	dwc_imx->suspend_clk = devm_clk_get_enabled(dev, "suspend");
	if (IS_ERR(dwc_imx->suspend_clk))
		return dev_err_probe(dev, PTR_ERR(dwc_imx->suspend_clk),
				     "Failed to get suspend clk\n");

	irq = platform_get_irq_byname(pdev, "wakeup");
	if (irq < 0)
		return irq;
	dwc_imx->irq = irq;

	irq_name = devm_kasprintf(dev, GFP_KERNEL, "%s:wakeup", dev_name(dev));
	if (!irq_name)
		return dev_err_probe(dev, -ENOMEM, "failed to create irq_name\n");

	ret = devm_request_threaded_irq(dev, irq, NULL, dwc3_imx_interrupt,
					IRQF_ONESHOT | IRQF_NO_AUTOEN,
					irq_name, dwc_imx);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request IRQ #%d\n", irq);

	ret = device_add_software_node(dev, &dwc3_imx_swnode);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add software node\n");

	dwc3_imx_configure_glue(dwc_imx);

	dwc = &dwc_imx->dwc;
	dwc->dev = dev;
	dwc->glue_ops = &dwc3_imx_glue_ops;

	probe_data.res = res;
	probe_data.dwc = dwc;
	probe_data.properties = DWC3_DEFAULT_PROPERTIES;
	probe_data.properties.needs_full_reinit = true;

	ret = dwc3_core_probe(&probe_data);
	if (ret) {
		device_remove_software_node(dev);
		return ret;
	}

	device_set_wakeup_capable(dev, true);
	return 0;
}

static void dwc3_imx_remove(struct platform_device *pdev)
{
	struct device	*dev = &pdev->dev;
	struct dwc3	*dwc = dev_get_drvdata(dev);

	dwc3_core_remove(dwc);
	device_remove_software_node(dev);
}

static void dwc3_imx_suspend(struct dwc3_imx *dwc_imx, pm_message_t msg)
{
	if (dwc_imx->pm_suspended)
		return;

	if (PMSG_IS_AUTO(msg) || device_may_wakeup(dwc_imx->dev))
		dwc3_imx_wakeup_enable(dwc_imx, msg);

	enable_irq(dwc_imx->irq);
	dwc_imx->pm_suspended = true;
}

static void dwc3_imx_resume(struct dwc3_imx *dwc_imx, pm_message_t msg)
{
	struct dwc3	*dwc = &dwc_imx->dwc;

	if (!dwc_imx->pm_suspended)
		return;

	dwc_imx->pm_suspended = false;
	if (!dwc_imx->wakeup_pending)
		disable_irq_nosync(dwc_imx->irq);

	dwc3_imx_wakeup_disable(dwc_imx);

	/* Upon power loss any previous configuration is lost, restore it */
	dwc3_imx_configure_glue(dwc_imx);

	if (dwc_imx->wakeup_pending) {
		dwc_imx->wakeup_pending = false;
		if (dwc->current_dr_role == DWC3_GCTL_PRTCAP_DEVICE)
			pm_runtime_put_autosuspend(dwc->dev);
		else
			/*
			 * Add wait for xhci switch from suspend
			 * clock to normal clock to detect connection.
			 */
			usleep_range(9000, 10000);
	}
}

static int dwc3_imx_runtime_suspend(struct device *dev)
{
	struct dwc3	*dwc = dev_get_drvdata(dev);
	struct dwc3_imx	*dwc_imx = to_dwc3_imx(dwc);
	int		ret;

	ret = dwc3_runtime_suspend(dwc);
	if (ret)
		return ret;

	dwc3_imx_suspend(dwc_imx, PMSG_AUTO_SUSPEND);
	return 0;
}

static int dwc3_imx_runtime_resume(struct device *dev)
{
	struct dwc3	*dwc = dev_get_drvdata(dev);
	struct dwc3_imx	*dwc_imx = to_dwc3_imx(dwc);

	dwc3_imx_resume(dwc_imx, PMSG_AUTO_RESUME);
	return dwc3_runtime_resume(dwc);
}

static int dwc3_imx_runtime_idle(struct device *dev)
{
	return dwc3_runtime_idle(dev_get_drvdata(dev));
}

static int dwc3_imx_pm_suspend(struct device *dev)
{
	struct dwc3	*dwc = dev_get_drvdata(dev);
	struct dwc3_imx *dwc_imx = to_dwc3_imx(dwc);
	int		ret;

	ret = dwc3_pm_suspend(dwc);
	if (ret)
		return ret;

	dwc3_imx_suspend(dwc_imx, PMSG_SUSPEND);

	if (device_may_wakeup(dev)) {
		enable_irq_wake(dwc_imx->irq);
		device_set_out_band_wakeup(dev);
	} else {
		clk_disable_unprepare(dwc_imx->suspend_clk);
	}

	clk_disable_unprepare(dwc_imx->hsio_clk);

	return 0;
}

static int dwc3_imx_pm_resume(struct device *dev)
{
	struct dwc3	*dwc = dev_get_drvdata(dev);
	struct dwc3_imx *dwc_imx = to_dwc3_imx(dwc);
	int		ret;

	if (device_may_wakeup(dwc_imx->dev)) {
		disable_irq_wake(dwc_imx->irq);
	} else {
		ret = clk_prepare_enable(dwc_imx->suspend_clk);
		if (ret)
			return ret;
	}

	ret = clk_prepare_enable(dwc_imx->hsio_clk);
	if (ret) {
		clk_disable_unprepare(dwc_imx->suspend_clk);
		return ret;
	}

	dwc3_imx_resume(dwc_imx, PMSG_RESUME);

	ret = dwc3_pm_resume(dwc);
	if (ret)
		return ret;

	return 0;
}

static void dwc3_imx_complete(struct device *dev)
{
	dwc3_pm_complete(dev_get_drvdata(dev));
}

static int dwc3_imx_prepare(struct device *dev)
{
	return dwc3_pm_prepare(dev_get_drvdata(dev));
}

static const struct dev_pm_ops dwc3_imx_dev_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(dwc3_imx_pm_suspend, dwc3_imx_pm_resume)
	RUNTIME_PM_OPS(dwc3_imx_runtime_suspend, dwc3_imx_runtime_resume,
		       dwc3_imx_runtime_idle)
	.complete = pm_sleep_ptr(dwc3_imx_complete),
	.prepare = pm_sleep_ptr(dwc3_imx_prepare),
};

static const struct of_device_id dwc3_imx_of_match[] = {
	{ .compatible = "nxp,imx8mp-dwc3", },
	{},
};
MODULE_DEVICE_TABLE(of, dwc3_imx_of_match);

static struct platform_driver dwc3_imx_driver = {
	.probe		= dwc3_imx_probe,
	.remove		= dwc3_imx_remove,
	.driver		= {
		.name	= "imx-dwc3",
		.pm	= pm_ptr(&dwc3_imx_dev_pm_ops),
		.of_match_table	= dwc3_imx_of_match,
	},
};

module_platform_driver(dwc3_imx_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DesignWare USB3 i.MX Glue Layer");
