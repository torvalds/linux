// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence USBSS DRD Driver.
 *
 * Copyright (C) 2018-2020 Cadence.
 * Copyright (C) 2017-2018 NXP
 * Copyright (C) 2019 Texas Instruments
 *
 *
 * Author: Peter Chen <peter.chen@nxp.com>
 *         Pawel Laszczak <pawell@cadence.com>
 *         Roger Quadros <rogerq@ti.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "core.h"
#include "gadget-export.h"

static int set_phy_power_on(struct cdns *cdns)
{
	int ret;

	ret = phy_power_on(cdns->usb2_phy);
	if (ret)
		return ret;

	ret = phy_power_on(cdns->usb3_phy);
	if (ret)
		phy_power_off(cdns->usb2_phy);

	return ret;
}

static void set_phy_power_off(struct cdns *cdns)
{
	phy_power_off(cdns->usb3_phy);
	phy_power_off(cdns->usb2_phy);
}

/**
 * cdns3_plat_probe - probe for cdns3 core device
 * @pdev: Pointer to cdns3 core platform device
 *
 * Returns 0 on success otherwise negative errno
 */
static int cdns3_plat_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource	*res;
	struct cdns *cdns;
	void __iomem *regs;
	int ret;

	cdns = devm_kzalloc(dev, sizeof(*cdns), GFP_KERNEL);
	if (!cdns)
		return -ENOMEM;

	cdns->dev = dev;
	cdns->pdata = dev_get_platdata(dev);

	platform_set_drvdata(pdev, cdns);

	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "host");
	if (!res) {
		dev_err(dev, "missing host IRQ\n");
		return -ENODEV;
	}

	cdns->xhci_res[0] = *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "xhci");
	if (!res) {
		dev_err(dev, "couldn't get xhci resource\n");
		return -ENXIO;
	}

	cdns->xhci_res[1] = *res;

	cdns->dev_irq = platform_get_irq_byname(pdev, "peripheral");

	if (cdns->dev_irq < 0)
		return cdns->dev_irq;

	regs = devm_platform_ioremap_resource_byname(pdev, "dev");
	if (IS_ERR(regs))
		return PTR_ERR(regs);
	cdns->dev_regs	= regs;

	cdns->otg_irq = platform_get_irq_byname(pdev, "otg");
	if (cdns->otg_irq < 0)
		return cdns->otg_irq;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "otg");
	if (!res) {
		dev_err(dev, "couldn't get otg resource\n");
		return -ENXIO;
	}

	cdns->phyrst_a_enable = device_property_read_bool(dev, "cdns,phyrst-a-enable");

	cdns->otg_res = *res;

	cdns->wakeup_irq = platform_get_irq_byname_optional(pdev, "wakeup");
	if (cdns->wakeup_irq == -EPROBE_DEFER)
		return cdns->wakeup_irq;
	else if (cdns->wakeup_irq == 0)
		return -EINVAL;

	if (cdns->wakeup_irq < 0) {
		dev_dbg(dev, "couldn't get wakeup irq\n");
		cdns->wakeup_irq = 0x0;
	}

	cdns->usb2_phy = devm_phy_optional_get(dev, "cdns3,usb2-phy");
	if (IS_ERR(cdns->usb2_phy))
		return PTR_ERR(cdns->usb2_phy);

	ret = phy_init(cdns->usb2_phy);
	if (ret)
		return ret;

	cdns->usb3_phy = devm_phy_optional_get(dev, "cdns3,usb3-phy");
	if (IS_ERR(cdns->usb3_phy))
		return PTR_ERR(cdns->usb3_phy);

	ret = phy_init(cdns->usb3_phy);
	if (ret)
		goto err_phy3_init;

	ret = set_phy_power_on(cdns);
	if (ret)
		goto err_phy_power_on;

	cdns->gadget_init = cdns3_gadget_init;

	ret = cdns_init(cdns);
	if (ret)
		goto err_cdns_init;

	device_set_wakeup_capable(dev, true);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	if (!(cdns->pdata && (cdns->pdata->quirks & CDNS3_DEFAULT_PM_RUNTIME_ALLOW)))
		pm_runtime_forbid(dev);

	/*
	 * The controller needs less time between bus and controller suspend,
	 * and we also needs a small delay to avoid frequently entering low
	 * power mode.
	 */
	pm_runtime_set_autosuspend_delay(dev, 20);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_use_autosuspend(dev);

	return 0;

err_cdns_init:
	set_phy_power_off(cdns);
err_phy_power_on:
	phy_exit(cdns->usb3_phy);
err_phy3_init:
	phy_exit(cdns->usb2_phy);

	return ret;
}

/**
 * cdns3_remove - unbind drd driver and clean up
 * @pdev: Pointer to Linux platform device
 *
 * Returns 0 on success otherwise negative errno
 */
static int cdns3_plat_remove(struct platform_device *pdev)
{
	struct cdns *cdns = platform_get_drvdata(pdev);
	struct device *dev = cdns->dev;

	pm_runtime_get_sync(dev);
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	cdns_remove(cdns);
	set_phy_power_off(cdns);
	phy_exit(cdns->usb2_phy);
	phy_exit(cdns->usb3_phy);
	return 0;
}

#ifdef CONFIG_PM

static int cdns3_set_platform_suspend(struct device *dev,
				      bool suspend, bool wakeup)
{
	struct cdns *cdns = dev_get_drvdata(dev);
	int ret = 0;

	if (cdns->pdata && cdns->pdata->platform_suspend)
		ret = cdns->pdata->platform_suspend(dev, suspend, wakeup);

	return ret;
}

static int cdns3_controller_suspend(struct device *dev, pm_message_t msg)
{
	struct cdns *cdns = dev_get_drvdata(dev);
	bool wakeup;
	unsigned long flags;

	if (cdns->in_lpm)
		return 0;

	if (PMSG_IS_AUTO(msg))
		wakeup = true;
	else
		wakeup = device_may_wakeup(dev);

	cdns3_set_platform_suspend(cdns->dev, true, wakeup);
	set_phy_power_off(cdns);
	spin_lock_irqsave(&cdns->lock, flags);
	cdns->in_lpm = true;
	spin_unlock_irqrestore(&cdns->lock, flags);
	dev_dbg(cdns->dev, "%s ends\n", __func__);

	return 0;
}

static int cdns3_controller_resume(struct device *dev, pm_message_t msg)
{
	struct cdns *cdns = dev_get_drvdata(dev);
	int ret;
	unsigned long flags;

	if (!cdns->in_lpm)
		return 0;

	ret = set_phy_power_on(cdns);
	if (ret)
		return ret;

	cdns3_set_platform_suspend(cdns->dev, false, false);

	spin_lock_irqsave(&cdns->lock, flags);
	cdns_resume(cdns, !PMSG_IS_AUTO(msg));
	cdns->in_lpm = false;
	spin_unlock_irqrestore(&cdns->lock, flags);
	if (cdns->wakeup_pending) {
		cdns->wakeup_pending = false;
		enable_irq(cdns->wakeup_irq);
	}
	dev_dbg(cdns->dev, "%s ends\n", __func__);

	return ret;
}

static int cdns3_plat_runtime_suspend(struct device *dev)
{
	return cdns3_controller_suspend(dev, PMSG_AUTO_SUSPEND);
}

static int cdns3_plat_runtime_resume(struct device *dev)
{
	return cdns3_controller_resume(dev, PMSG_AUTO_RESUME);
}

#ifdef CONFIG_PM_SLEEP

static int cdns3_plat_suspend(struct device *dev)
{
	struct cdns *cdns = dev_get_drvdata(dev);

	cdns_suspend(cdns);

	return cdns3_controller_suspend(dev, PMSG_SUSPEND);
}

static int cdns3_plat_resume(struct device *dev)
{
	return cdns3_controller_resume(dev, PMSG_RESUME);
}
#endif /* CONFIG_PM_SLEEP */
#endif /* CONFIG_PM */

static const struct dev_pm_ops cdns3_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cdns3_plat_suspend, cdns3_plat_resume)
	SET_RUNTIME_PM_OPS(cdns3_plat_runtime_suspend,
			   cdns3_plat_runtime_resume, NULL)
};

#ifdef CONFIG_OF
static const struct of_device_id of_cdns3_match[] = {
	{ .compatible = "cdns,usb3" },
	{ },
};
MODULE_DEVICE_TABLE(of, of_cdns3_match);
#endif

static struct platform_driver cdns3_driver = {
	.probe		= cdns3_plat_probe,
	.remove		= cdns3_plat_remove,
	.driver		= {
		.name	= "cdns-usb3",
		.of_match_table	= of_match_ptr(of_cdns3_match),
		.pm	= &cdns3_pm_ops,
	},
};

module_platform_driver(cdns3_driver);

MODULE_ALIAS("platform:cdns3");
MODULE_AUTHOR("Pawel Laszczak <pawell@cadence.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Cadence USB3 DRD Controller Driver");
