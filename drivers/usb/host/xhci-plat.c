// SPDX-License-Identifier: GPL-2.0
/*
 * xhci-plat.c - xHCI host controller driver platform Bus Glue.
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - https://www.ti.com
 * Author: Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 *
 * A lot of code borrowed from the Linux xHCI driver.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/usb/phy.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/usb/of.h>
#include <linux/reset.h>

#include "xhci.h"
#include "xhci-plat.h"
#include "xhci-mvebu.h"

static struct hc_driver __read_mostly xhci_plat_hc_driver;

static int xhci_plat_setup(struct usb_hcd *hcd);
static int xhci_plat_start(struct usb_hcd *hcd);

static const struct xhci_driver_overrides xhci_plat_overrides __initconst = {
	.extra_priv_size = sizeof(struct xhci_plat_priv),
	.reset = xhci_plat_setup,
	.start = xhci_plat_start,
};

static void xhci_priv_plat_start(struct usb_hcd *hcd)
{
	struct xhci_plat_priv *priv = hcd_to_xhci_priv(hcd);

	if (priv->plat_start)
		priv->plat_start(hcd);
}

static int xhci_priv_init_quirk(struct usb_hcd *hcd)
{
	struct xhci_plat_priv *priv = hcd_to_xhci_priv(hcd);

	if (!priv->init_quirk)
		return 0;

	return priv->init_quirk(hcd);
}

static int xhci_priv_suspend_quirk(struct usb_hcd *hcd)
{
	struct xhci_plat_priv *priv = hcd_to_xhci_priv(hcd);

	if (!priv->suspend_quirk)
		return 0;

	return priv->suspend_quirk(hcd);
}

static int xhci_priv_resume_quirk(struct usb_hcd *hcd)
{
	struct xhci_plat_priv *priv = hcd_to_xhci_priv(hcd);

	if (!priv->resume_quirk)
		return 0;

	return priv->resume_quirk(hcd);
}

static void xhci_plat_quirks(struct device *dev, struct xhci_hcd *xhci)
{
	struct xhci_plat_priv *priv = xhci_to_priv(xhci);

	xhci->quirks |= priv->quirks;
}

/* called during probe() after chip reset completes */
static int xhci_plat_setup(struct usb_hcd *hcd)
{
	int ret;


	ret = xhci_priv_init_quirk(hcd);
	if (ret)
		return ret;

	return xhci_gen_setup(hcd, xhci_plat_quirks);
}

static int xhci_plat_start(struct usb_hcd *hcd)
{
	xhci_priv_plat_start(hcd);
	return xhci_run(hcd);
}

#ifdef CONFIG_OF
static const struct xhci_plat_priv xhci_plat_marvell_armada = {
	.init_quirk = xhci_mvebu_mbus_init_quirk,
};

static const struct xhci_plat_priv xhci_plat_marvell_armada3700 = {
	.quirks = XHCI_RESET_ON_RESUME,
};

static const struct xhci_plat_priv xhci_plat_brcm = {
	.quirks = XHCI_RESET_ON_RESUME | XHCI_SUSPEND_RESUME_CLKS,
};

static const struct of_device_id usb_xhci_of_match[] = {
	{
		.compatible = "generic-xhci",
	}, {
		.compatible = "xhci-platform",
	}, {
		.compatible = "marvell,armada-375-xhci",
		.data = &xhci_plat_marvell_armada,
	}, {
		.compatible = "marvell,armada-380-xhci",
		.data = &xhci_plat_marvell_armada,
	}, {
		.compatible = "marvell,armada3700-xhci",
		.data = &xhci_plat_marvell_armada3700,
	}, {
		.compatible = "brcm,xhci-brcm-v2",
		.data = &xhci_plat_brcm,
	}, {
		.compatible = "brcm,bcm2711-xhci",
		.data = &xhci_plat_brcm,
	}, {
		.compatible = "brcm,bcm7445-xhci",
		.data = &xhci_plat_brcm,
	},
	{},
};
MODULE_DEVICE_TABLE(of, usb_xhci_of_match);
#endif

int xhci_plat_probe(struct platform_device *pdev, struct device *sysdev, const struct xhci_plat_priv *priv_match)
{
	const struct hc_driver	*driver;
	struct device		*tmpdev;
	struct xhci_hcd		*xhci;
	struct resource         *res;
	struct usb_hcd		*hcd, *usb3_hcd;
	int			ret;
	int			irq;
	struct xhci_plat_priv	*priv = NULL;
	const struct of_device_id *of_match;

	if (usb_disabled())
		return -ENODEV;

	driver = &xhci_plat_hc_driver;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	if (!sysdev)
		sysdev = &pdev->dev;

	ret = dma_set_mask_and_coherent(sysdev, DMA_BIT_MASK(64));
	if (ret)
		return ret;

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);

	hcd = __usb_create_hcd(driver, sysdev, &pdev->dev,
			       dev_name(&pdev->dev), NULL);
	if (!hcd) {
		ret = -ENOMEM;
		goto disable_runtime;
	}

	hcd->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(hcd->regs)) {
		ret = PTR_ERR(hcd->regs);
		goto put_hcd;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	xhci = hcd_to_xhci(hcd);

	xhci->allow_single_roothub = 1;

	/*
	 * Not all platforms have clks so it is not an error if the
	 * clock do not exist.
	 */
	xhci->reg_clk = devm_clk_get_optional(&pdev->dev, "reg");
	if (IS_ERR(xhci->reg_clk)) {
		ret = PTR_ERR(xhci->reg_clk);
		goto put_hcd;
	}

	xhci->clk = devm_clk_get_optional(&pdev->dev, NULL);
	if (IS_ERR(xhci->clk)) {
		ret = PTR_ERR(xhci->clk);
		goto put_hcd;
	}

	xhci->reset = devm_reset_control_array_get_optional_shared(&pdev->dev);
	if (IS_ERR(xhci->reset)) {
		ret = PTR_ERR(xhci->reset);
		goto put_hcd;
	}

	ret = reset_control_deassert(xhci->reset);
	if (ret)
		goto put_hcd;

	ret = clk_prepare_enable(xhci->reg_clk);
	if (ret)
		goto err_reset;

	ret = clk_prepare_enable(xhci->clk);
	if (ret)
		goto disable_reg_clk;

	if (priv_match) {
		priv = hcd_to_xhci_priv(hcd);
		/* Just copy data for now */
		*priv = *priv_match;
	}

	device_set_wakeup_capable(&pdev->dev, true);

	xhci->main_hcd = hcd;

	/* imod_interval is the interrupt moderation value in nanoseconds. */
	xhci->imod_interval = 40000;

	/* Iterate over all parent nodes for finding quirks */
	for (tmpdev = &pdev->dev; tmpdev; tmpdev = tmpdev->parent) {

		if (device_property_read_bool(tmpdev, "usb2-lpm-disable"))
			xhci->quirks |= XHCI_HW_LPM_DISABLE;

		if (device_property_read_bool(tmpdev, "usb3-lpm-capable"))
			xhci->quirks |= XHCI_LPM_SUPPORT;

		if (device_property_read_bool(tmpdev, "quirk-broken-port-ped"))
			xhci->quirks |= XHCI_BROKEN_PORT_PED;

		if (device_property_read_bool(tmpdev, "xhci-sg-trb-cache-size-quirk"))
			xhci->quirks |= XHCI_SG_TRB_CACHE_SIZE_QUIRK;

		if (device_property_read_bool(tmpdev, "write-64-hi-lo-quirk"))
			xhci->quirks |= XHCI_WRITE_64_HI_LO;

		if (device_property_read_bool(tmpdev, "xhci-missing-cas-quirk"))
			xhci->quirks |= XHCI_MISSING_CAS;

		if (device_property_read_bool(tmpdev, "xhci-skip-phy-init-quirk"))
			xhci->quirks |= XHCI_SKIP_PHY_INIT;

		device_property_read_u32(tmpdev, "imod-interval-ns",
					 &xhci->imod_interval);
		device_property_read_u16(tmpdev, "num-hc-interrupters",
					 &xhci->max_interrupters);
	}

	/*
	 * Drivers such as dwc3 manages PHYs themself (and rely on driver name
	 * matching for the xhci platform device).
	 */
	of_match = of_match_device(pdev->dev.driver->of_match_table, &pdev->dev);
	if (of_match) {
		hcd->usb_phy = devm_usb_get_phy_by_phandle(sysdev, "usb-phy", 0);
		if (IS_ERR(hcd->usb_phy)) {
			ret = PTR_ERR(hcd->usb_phy);
			if (ret == -EPROBE_DEFER)
				goto disable_clk;
			hcd->usb_phy = NULL;
		} else {
			ret = usb_phy_init(hcd->usb_phy);
			if (ret)
				goto disable_clk;
		}
	}

	hcd->tpl_support = of_usb_host_tpl_support(sysdev->of_node);

	if ((priv && (priv->quirks & XHCI_SKIP_PHY_INIT)) ||
	    (xhci->quirks & XHCI_SKIP_PHY_INIT))
		hcd->skip_phy_initialization = 1;

	if (priv && (priv->quirks & XHCI_SG_TRB_CACHE_SIZE_QUIRK))
		xhci->quirks |= XHCI_SG_TRB_CACHE_SIZE_QUIRK;

	ret = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (ret)
		goto disable_usb_phy;

	if (!xhci_has_one_roothub(xhci)) {
		xhci->shared_hcd = __usb_create_hcd(driver, sysdev, &pdev->dev,
						    dev_name(&pdev->dev), hcd);
		if (!xhci->shared_hcd) {
			ret = -ENOMEM;
			goto dealloc_usb2_hcd;
		}

		if (of_match) {
			xhci->shared_hcd->usb_phy = devm_usb_get_phy_by_phandle(sysdev,
										"usb-phy", 1);
			if (IS_ERR(xhci->shared_hcd->usb_phy)) {
				xhci->shared_hcd->usb_phy = NULL;
			} else {
				ret = usb_phy_init(xhci->shared_hcd->usb_phy);
				if (ret)
					dev_err(sysdev, "%s init usb3phy fail (ret=%d)\n",
						__func__, ret);
			}
		}

		xhci->shared_hcd->tpl_support = hcd->tpl_support;
	}

	usb3_hcd = xhci_get_usb3_hcd(xhci);
	if (usb3_hcd && HCC_MAX_PSA(xhci->hcc_params) >= 4 &&
	    !(xhci->quirks & XHCI_BROKEN_STREAMS))
		usb3_hcd->can_do_streams = 1;

	if (xhci->shared_hcd) {
		xhci->shared_hcd->rsrc_start = hcd->rsrc_start;
		xhci->shared_hcd->rsrc_len = hcd->rsrc_len;
		ret = usb_add_hcd(xhci->shared_hcd, irq, IRQF_SHARED);
		if (ret)
			goto put_usb3_hcd;
	}

	device_enable_async_suspend(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	/*
	 * Prevent runtime pm from being on as default, users should enable
	 * runtime pm using power/control in sysfs.
	 */
	pm_runtime_forbid(&pdev->dev);

	return 0;


put_usb3_hcd:
	usb_put_hcd(xhci->shared_hcd);

dealloc_usb2_hcd:
	usb_remove_hcd(hcd);

disable_usb_phy:
	usb_phy_shutdown(hcd->usb_phy);

disable_clk:
	clk_disable_unprepare(xhci->clk);

disable_reg_clk:
	clk_disable_unprepare(xhci->reg_clk);

err_reset:
	reset_control_assert(xhci->reset);

put_hcd:
	usb_put_hcd(hcd);

disable_runtime:
	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return ret;
}
EXPORT_SYMBOL_GPL(xhci_plat_probe);

static int xhci_generic_plat_probe(struct platform_device *pdev)
{
	const struct xhci_plat_priv *priv_match;
	struct device *sysdev;
	int ret;

	/*
	 * sysdev must point to a device that is known to the system firmware
	 * or PCI hardware. We handle these three cases here:
	 * 1. xhci_plat comes from firmware
	 * 2. xhci_plat is child of a device from firmware (dwc3-plat)
	 * 3. xhci_plat is grandchild of a pci device (dwc3-pci)
	 */
	for (sysdev = &pdev->dev; sysdev; sysdev = sysdev->parent) {
		if (is_of_node(sysdev->fwnode) ||
			is_acpi_device_node(sysdev->fwnode))
			break;
		else if (dev_is_pci(sysdev))
			break;
	}

	if (!sysdev)
		sysdev = &pdev->dev;

	if (WARN_ON(!sysdev->dma_mask)) {
		/* Platform did not initialize dma_mask */
		ret = dma_coerce_mask_and_coherent(sysdev, DMA_BIT_MASK(64));
		if (ret)
			return ret;
	}

	if (pdev->dev.of_node)
		priv_match = of_device_get_match_data(&pdev->dev);
	else
		priv_match = dev_get_platdata(&pdev->dev);

	return xhci_plat_probe(pdev, sysdev, priv_match);
}

void xhci_plat_remove(struct platform_device *dev)
{
	struct usb_hcd	*hcd = platform_get_drvdata(dev);
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
	struct clk *clk = xhci->clk;
	struct clk *reg_clk = xhci->reg_clk;
	struct usb_hcd *shared_hcd = xhci->shared_hcd;

	xhci->xhc_state |= XHCI_STATE_REMOVING;
	pm_runtime_get_sync(&dev->dev);

	if (shared_hcd) {
		usb_remove_hcd(shared_hcd);
		xhci->shared_hcd = NULL;
	}

	usb_phy_shutdown(hcd->usb_phy);

	usb_remove_hcd(hcd);

	if (shared_hcd)
		usb_put_hcd(shared_hcd);

	clk_disable_unprepare(clk);
	clk_disable_unprepare(reg_clk);
	reset_control_assert(xhci->reset);
	usb_put_hcd(hcd);

	pm_runtime_disable(&dev->dev);
	pm_runtime_put_noidle(&dev->dev);
	pm_runtime_set_suspended(&dev->dev);
}
EXPORT_SYMBOL_GPL(xhci_plat_remove);

static int xhci_plat_suspend(struct device *dev)
{
	struct usb_hcd	*hcd = dev_get_drvdata(dev);
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
	int ret;

	if (pm_runtime_suspended(dev))
		pm_runtime_resume(dev);

	ret = xhci_priv_suspend_quirk(hcd);
	if (ret)
		return ret;
	/*
	 * xhci_suspend() needs `do_wakeup` to know whether host is allowed
	 * to do wakeup during suspend.
	 */
	ret = xhci_suspend(xhci, device_may_wakeup(dev));
	if (ret)
		return ret;

	if (!device_may_wakeup(dev) && (xhci->quirks & XHCI_SUSPEND_RESUME_CLKS)) {
		clk_disable_unprepare(xhci->clk);
		clk_disable_unprepare(xhci->reg_clk);
	}

	return 0;
}

static int xhci_plat_resume_common(struct device *dev, bool power_lost)
{
	struct usb_hcd	*hcd = dev_get_drvdata(dev);
	struct xhci_plat_priv *priv = hcd_to_xhci_priv(hcd);
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
	int ret;

	if (!device_may_wakeup(dev) && (xhci->quirks & XHCI_SUSPEND_RESUME_CLKS)) {
		ret = clk_prepare_enable(xhci->clk);
		if (ret)
			return ret;

		ret = clk_prepare_enable(xhci->reg_clk);
		if (ret) {
			clk_disable_unprepare(xhci->clk);
			return ret;
		}
	}

	ret = xhci_priv_resume_quirk(hcd);
	if (ret)
		goto disable_clks;

	ret = xhci_resume(xhci, power_lost || priv->power_lost, false);
	if (ret)
		goto disable_clks;

	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;

disable_clks:
	if (!device_may_wakeup(dev) && (xhci->quirks & XHCI_SUSPEND_RESUME_CLKS)) {
		clk_disable_unprepare(xhci->clk);
		clk_disable_unprepare(xhci->reg_clk);
	}

	return ret;
}

static int xhci_plat_resume(struct device *dev)
{
	return xhci_plat_resume_common(dev, false);
}

static int xhci_plat_restore(struct device *dev)
{
	return xhci_plat_resume_common(dev, true);
}

static int __maybe_unused xhci_plat_runtime_suspend(struct device *dev)
{
	struct usb_hcd  *hcd = dev_get_drvdata(dev);
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	int ret;

	ret = xhci_priv_suspend_quirk(hcd);
	if (ret)
		return ret;

	return xhci_suspend(xhci, true);
}

static int __maybe_unused xhci_plat_runtime_resume(struct device *dev)
{
	struct usb_hcd  *hcd = dev_get_drvdata(dev);
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);

	return xhci_resume(xhci, false, true);
}

const struct dev_pm_ops xhci_plat_pm_ops = {
	.suspend = pm_sleep_ptr(xhci_plat_suspend),
	.resume = pm_sleep_ptr(xhci_plat_resume),
	.freeze = pm_sleep_ptr(xhci_plat_suspend),
	.thaw = pm_sleep_ptr(xhci_plat_resume),
	.poweroff = pm_sleep_ptr(xhci_plat_suspend),
	.restore = pm_sleep_ptr(xhci_plat_restore),

	SET_RUNTIME_PM_OPS(xhci_plat_runtime_suspend,
			   xhci_plat_runtime_resume,
			   NULL)
};
EXPORT_SYMBOL_GPL(xhci_plat_pm_ops);

#ifdef CONFIG_ACPI
static const struct acpi_device_id usb_xhci_acpi_match[] = {
	/* XHCI-compliant USB Controller */
	{ "PNP0D10", },
	{ "PNP0D15", },
	{ }
};
MODULE_DEVICE_TABLE(acpi, usb_xhci_acpi_match);
#endif

static struct platform_driver usb_generic_xhci_driver = {
	.probe	= xhci_generic_plat_probe,
	.remove = xhci_plat_remove,
	.shutdown = usb_hcd_platform_shutdown,
	.driver	= {
		.name = "xhci-hcd",
		.pm = &xhci_plat_pm_ops,
		.of_match_table = of_match_ptr(usb_xhci_of_match),
		.acpi_match_table = ACPI_PTR(usb_xhci_acpi_match),
	},
};
MODULE_ALIAS("platform:xhci-hcd");

static int __init xhci_plat_init(void)
{
	xhci_init_driver(&xhci_plat_hc_driver, &xhci_plat_overrides);
	return platform_driver_register(&usb_generic_xhci_driver);
}
module_init(xhci_plat_init);

static void __exit xhci_plat_exit(void)
{
	platform_driver_unregister(&usb_generic_xhci_driver);
}
module_exit(xhci_plat_exit);

MODULE_DESCRIPTION("xHCI Platform Host Controller Driver");
MODULE_LICENSE("GPL");
