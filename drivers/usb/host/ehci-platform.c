// SPDX-License-Identifier: GPL-2.0
/*
 * Generic platform ehci driver
 *
 * Copyright 2007 Steven Brown <sbrown@cortland.com>
 * Copyright 2010-2012 Hauke Mehrtens <hauke@hauke-m.de>
 * Copyright 2014 Hans de Goede <hdegoede@redhat.com>
 *
 * Derived from the ohci-ssb driver
 * Copyright 2007 Michael Buesch <m@bues.ch>
 *
 * Derived from the EHCI-PCI driver
 * Copyright (c) 2000-2004 by David Brownell
 *
 * Derived from the ohci-pci driver
 * Copyright 1999 Roman Weissgaerber
 * Copyright 2000-2002 David Brownell
 * Copyright 1999 Linus Torvalds
 * Copyright 1999 Gregory P. Smith
 */
#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/sys_soc.h>
#include <linux/timer.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/usb/ehci_pdriver.h>
#include <linux/usb/of.h>

#include "ehci.h"

#define DRIVER_DESC "EHCI generic platform driver"
#define EHCI_MAX_CLKS 4
#define hcd_to_ehci_priv(h) ((struct ehci_platform_priv *)hcd_to_ehci(h)->priv)

#define BCM_USB_FIFO_THRESHOLD	0x00800040

struct ehci_platform_priv {
	struct clk *clks[EHCI_MAX_CLKS];
	struct reset_control *rsts;
	bool reset_on_resume;
	bool quirk_poll;
	struct timer_list poll_timer;
	struct delayed_work poll_work;
};

static const char hcd_name[] = "ehci-platform";

static int ehci_platform_reset(struct usb_hcd *hcd)
{
	struct platform_device *pdev = to_platform_device(hcd->self.controller);
	struct usb_ehci_pdata *pdata = dev_get_platdata(&pdev->dev);
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int retval;

	ehci->has_synopsys_hc_bug = pdata->has_synopsys_hc_bug;

	if (pdata->pre_setup) {
		retval = pdata->pre_setup(hcd);
		if (retval < 0)
			return retval;
	}

	ehci->caps = hcd->regs + pdata->caps_offset;
	retval = ehci_setup(hcd);
	if (retval)
		return retval;

	if (pdata->no_io_watchdog)
		ehci->need_io_watchdog = 0;

	if (of_device_is_compatible(pdev->dev.of_node, "brcm,xgs-iproc-ehci"))
		ehci_writel(ehci, BCM_USB_FIFO_THRESHOLD,
			    &ehci->regs->brcm_insnreg[1]);

	return 0;
}

static int ehci_platform_power_on(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	struct ehci_platform_priv *priv = hcd_to_ehci_priv(hcd);
	int clk, ret;

	for (clk = 0; clk < EHCI_MAX_CLKS && priv->clks[clk]; clk++) {
		ret = clk_prepare_enable(priv->clks[clk]);
		if (ret)
			goto err_disable_clks;
	}

	return 0;

err_disable_clks:
	while (--clk >= 0)
		clk_disable_unprepare(priv->clks[clk]);

	return ret;
}

static void ehci_platform_power_off(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	struct ehci_platform_priv *priv = hcd_to_ehci_priv(hcd);
	int clk;

	for (clk = EHCI_MAX_CLKS - 1; clk >= 0; clk--)
		if (priv->clks[clk])
			clk_disable_unprepare(priv->clks[clk]);
}

static struct hc_driver __read_mostly ehci_platform_hc_driver;

static const struct ehci_driver_overrides platform_overrides __initconst = {
	.reset =		ehci_platform_reset,
	.extra_priv_size =	sizeof(struct ehci_platform_priv),
};

static struct usb_ehci_pdata ehci_platform_defaults = {
	.power_on =		ehci_platform_power_on,
	.power_suspend =	ehci_platform_power_off,
	.power_off =		ehci_platform_power_off,
};

/**
 * quirk_poll_check_port_status - Poll port_status if the device sticks
 * @ehci: the ehci hcd pointer
 *
 * Since EHCI/OHCI controllers on R-Car Gen3 SoCs are possible to be getting
 * stuck very rarely after a full/low usb device was disconnected. To
 * detect such a situation, the controllers require a special way which poll
 * the EHCI PORTSC register.
 *
 * Return: true if the controller's port_status indicated getting stuck
 */
static bool quirk_poll_check_port_status(struct ehci_hcd *ehci)
{
	u32 port_status = ehci_readl(ehci, &ehci->regs->port_status[0]);

	if (!(port_status & PORT_OWNER) &&
	     (port_status & PORT_POWER) &&
	    !(port_status & PORT_CONNECT) &&
	     (port_status & PORT_LS_MASK))
		return true;

	return false;
}

/**
 * quirk_poll_rebind_companion - rebind comanion device to recover
 * @ehci: the ehci hcd pointer
 *
 * Since EHCI/OHCI controllers on R-Car Gen3 SoCs are possible to be getting
 * stuck very rarely after a full/low usb device was disconnected. To
 * recover from such a situation, the controllers require changing the OHCI
 * functional state.
 */
static void quirk_poll_rebind_companion(struct ehci_hcd *ehci)
{
	struct device *companion_dev;
	struct usb_hcd *hcd = ehci_to_hcd(ehci);

	companion_dev = usb_of_get_companion_dev(hcd->self.controller);
	if (!companion_dev)
		return;

	device_release_driver(companion_dev);
	if (device_attach(companion_dev) < 0)
		ehci_err(ehci, "%s: failed\n", __func__);

	put_device(companion_dev);
}

static void quirk_poll_work(struct work_struct *work)
{
	struct ehci_platform_priv *priv =
		container_of(to_delayed_work(work), struct ehci_platform_priv,
			     poll_work);
	struct ehci_hcd *ehci = container_of((void *)priv, struct ehci_hcd,
					     priv);

	/* check the status twice to reduce misdetection rate */
	if (!quirk_poll_check_port_status(ehci))
		return;
	udelay(10);
	if (!quirk_poll_check_port_status(ehci))
		return;

	ehci_dbg(ehci, "%s: detected getting stuck. rebind now!\n", __func__);
	quirk_poll_rebind_companion(ehci);
}

static void quirk_poll_timer(struct timer_list *t)
{
	struct ehci_platform_priv *priv = from_timer(priv, t, poll_timer);
	struct ehci_hcd *ehci = container_of((void *)priv, struct ehci_hcd,
					     priv);

	if (quirk_poll_check_port_status(ehci)) {
		/*
		 * Now scheduling the work for testing the port more. Note that
		 * updating the status is possible to be delayed when
		 * reconnection. So, this uses delayed work with 5 ms delay
		 * to avoid misdetection.
		 */
		schedule_delayed_work(&priv->poll_work, msecs_to_jiffies(5));
	}

	mod_timer(&priv->poll_timer, jiffies + HZ);
}

static void quirk_poll_init(struct ehci_platform_priv *priv)
{
	INIT_DELAYED_WORK(&priv->poll_work, quirk_poll_work);
	timer_setup(&priv->poll_timer, quirk_poll_timer, 0);
	mod_timer(&priv->poll_timer, jiffies + HZ);
}

static void quirk_poll_end(struct ehci_platform_priv *priv)
{
	del_timer_sync(&priv->poll_timer);
	cancel_delayed_work(&priv->poll_work);
}

static const struct soc_device_attribute quirk_poll_match[] = {
	{ .family = "R-Car Gen3" },
	{ /* sentinel*/ }
};

static int ehci_platform_probe(struct platform_device *dev)
{
	struct usb_hcd *hcd;
	struct resource *res_mem;
	struct usb_ehci_pdata *pdata = dev_get_platdata(&dev->dev);
	struct ehci_platform_priv *priv;
	struct ehci_hcd *ehci;
	int err, irq, clk = 0;

	if (usb_disabled())
		return -ENODEV;

	/*
	 * Use reasonable defaults so platforms don't have to provide these
	 * with DT probing on ARM.
	 */
	if (!pdata)
		pdata = &ehci_platform_defaults;

	err = dma_coerce_mask_and_coherent(&dev->dev,
		pdata->dma_mask_64 ? DMA_BIT_MASK(64) : DMA_BIT_MASK(32));
	if (err) {
		dev_err(&dev->dev, "Error: DMA mask configuration failed\n");
		return err;
	}

	irq = platform_get_irq(dev, 0);
	if (irq < 0)
		return irq;

	hcd = usb_create_hcd(&ehci_platform_hc_driver, &dev->dev,
			     dev_name(&dev->dev));
	if (!hcd)
		return -ENOMEM;

	platform_set_drvdata(dev, hcd);
	dev->dev.platform_data = pdata;
	priv = hcd_to_ehci_priv(hcd);
	ehci = hcd_to_ehci(hcd);

	if (pdata == &ehci_platform_defaults && dev->dev.of_node) {
		if (of_property_read_bool(dev->dev.of_node, "big-endian-regs"))
			ehci->big_endian_mmio = 1;

		if (of_property_read_bool(dev->dev.of_node, "big-endian-desc"))
			ehci->big_endian_desc = 1;

		if (of_property_read_bool(dev->dev.of_node, "big-endian"))
			ehci->big_endian_mmio = ehci->big_endian_desc = 1;

		if (of_property_read_bool(dev->dev.of_node, "spurious-oc"))
			ehci->spurious_oc = 1;

		if (of_property_read_bool(dev->dev.of_node,
					  "needs-reset-on-resume"))
			priv->reset_on_resume = true;

		if (of_property_read_bool(dev->dev.of_node,
					  "has-transaction-translator"))
			hcd->has_tt = 1;

		if (of_device_is_compatible(dev->dev.of_node,
					    "aspeed,ast2500-ehci") ||
		    of_device_is_compatible(dev->dev.of_node,
					    "aspeed,ast2600-ehci"))
			ehci->is_aspeed = 1;

		if (soc_device_match(quirk_poll_match))
			priv->quirk_poll = true;

		for (clk = 0; clk < EHCI_MAX_CLKS; clk++) {
			priv->clks[clk] = of_clk_get(dev->dev.of_node, clk);
			if (IS_ERR(priv->clks[clk])) {
				err = PTR_ERR(priv->clks[clk]);
				if (err == -EPROBE_DEFER)
					goto err_put_clks;
				priv->clks[clk] = NULL;
				break;
			}
		}
	}

	priv->rsts = devm_reset_control_array_get_optional_shared(&dev->dev);
	if (IS_ERR(priv->rsts)) {
		err = PTR_ERR(priv->rsts);
		goto err_put_clks;
	}

	err = reset_control_deassert(priv->rsts);
	if (err)
		goto err_put_clks;

	if (pdata->big_endian_desc)
		ehci->big_endian_desc = 1;
	if (pdata->big_endian_mmio)
		ehci->big_endian_mmio = 1;
	if (pdata->has_tt)
		hcd->has_tt = 1;
	if (pdata->reset_on_resume)
		priv->reset_on_resume = true;
	if (pdata->spurious_oc)
		ehci->spurious_oc = 1;

#ifndef CONFIG_USB_EHCI_BIG_ENDIAN_MMIO
	if (ehci->big_endian_mmio) {
		dev_err(&dev->dev,
			"Error: CONFIG_USB_EHCI_BIG_ENDIAN_MMIO not set\n");
		err = -EINVAL;
		goto err_reset;
	}
#endif
#ifndef CONFIG_USB_EHCI_BIG_ENDIAN_DESC
	if (ehci->big_endian_desc) {
		dev_err(&dev->dev,
			"Error: CONFIG_USB_EHCI_BIG_ENDIAN_DESC not set\n");
		err = -EINVAL;
		goto err_reset;
	}
#endif

	if (pdata->power_on) {
		err = pdata->power_on(dev);
		if (err < 0)
			goto err_reset;
	}

	res_mem = platform_get_resource(dev, IORESOURCE_MEM, 0);
	hcd->regs = devm_ioremap_resource(&dev->dev, res_mem);
	if (IS_ERR(hcd->regs)) {
		err = PTR_ERR(hcd->regs);
		goto err_power;
	}
	hcd->rsrc_start = res_mem->start;
	hcd->rsrc_len = resource_size(res_mem);

	err = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (err)
		goto err_power;

	device_wakeup_enable(hcd->self.controller);
	device_enable_async_suspend(hcd->self.controller);
	platform_set_drvdata(dev, hcd);

	if (priv->quirk_poll)
		quirk_poll_init(priv);

	return err;

err_power:
	if (pdata->power_off)
		pdata->power_off(dev);
err_reset:
	reset_control_assert(priv->rsts);
err_put_clks:
	while (--clk >= 0)
		clk_put(priv->clks[clk]);

	if (pdata == &ehci_platform_defaults)
		dev->dev.platform_data = NULL;

	usb_put_hcd(hcd);

	return err;
}

static int ehci_platform_remove(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	struct usb_ehci_pdata *pdata = dev_get_platdata(&dev->dev);
	struct ehci_platform_priv *priv = hcd_to_ehci_priv(hcd);
	int clk;

	if (priv->quirk_poll)
		quirk_poll_end(priv);

	usb_remove_hcd(hcd);

	if (pdata->power_off)
		pdata->power_off(dev);

	reset_control_assert(priv->rsts);

	for (clk = 0; clk < EHCI_MAX_CLKS && priv->clks[clk]; clk++)
		clk_put(priv->clks[clk]);

	usb_put_hcd(hcd);

	if (pdata == &ehci_platform_defaults)
		dev->dev.platform_data = NULL;

	return 0;
}

static int __maybe_unused ehci_platform_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct usb_ehci_pdata *pdata = dev_get_platdata(dev);
	struct platform_device *pdev = to_platform_device(dev);
	struct ehci_platform_priv *priv = hcd_to_ehci_priv(hcd);
	bool do_wakeup = device_may_wakeup(dev);
	int ret;

	if (priv->quirk_poll)
		quirk_poll_end(priv);

	ret = ehci_suspend(hcd, do_wakeup);
	if (ret)
		return ret;

	if (pdata->power_suspend)
		pdata->power_suspend(pdev);

	return ret;
}

static int __maybe_unused ehci_platform_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	struct usb_ehci_pdata *pdata = dev_get_platdata(dev);
	struct platform_device *pdev = to_platform_device(dev);
	struct ehci_platform_priv *priv = hcd_to_ehci_priv(hcd);
	struct device *companion_dev;

	if (pdata->power_on) {
		int err = pdata->power_on(pdev);
		if (err < 0)
			return err;
	}

	companion_dev = usb_of_get_companion_dev(hcd->self.controller);
	if (companion_dev) {
		device_pm_wait_for_dev(hcd->self.controller, companion_dev);
		put_device(companion_dev);
	}

	ehci_resume(hcd, priv->reset_on_resume);

	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	if (priv->quirk_poll)
		quirk_poll_init(priv);

	return 0;
}

static const struct of_device_id vt8500_ehci_ids[] = {
	{ .compatible = "via,vt8500-ehci", },
	{ .compatible = "wm,prizm-ehci", },
	{ .compatible = "generic-ehci", },
	{ .compatible = "cavium,octeon-6335-ehci", },
	{}
};
MODULE_DEVICE_TABLE(of, vt8500_ehci_ids);

#ifdef CONFIG_ACPI
static const struct acpi_device_id ehci_acpi_match[] = {
	{ "PNP0D20", 0 }, /* EHCI controller without debug */
	{ }
};
MODULE_DEVICE_TABLE(acpi, ehci_acpi_match);
#endif

static const struct platform_device_id ehci_platform_table[] = {
	{ "ehci-platform", 0 },
	{ }
};
MODULE_DEVICE_TABLE(platform, ehci_platform_table);

static SIMPLE_DEV_PM_OPS(ehci_platform_pm_ops, ehci_platform_suspend,
	ehci_platform_resume);

static struct platform_driver ehci_platform_driver = {
	.id_table	= ehci_platform_table,
	.probe		= ehci_platform_probe,
	.remove		= ehci_platform_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver		= {
		.name	= "ehci-platform",
		.pm	= pm_ptr(&ehci_platform_pm_ops),
		.of_match_table = vt8500_ehci_ids,
		.acpi_match_table = ACPI_PTR(ehci_acpi_match),
	}
};

static int __init ehci_platform_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);

	ehci_init_driver(&ehci_platform_hc_driver, &platform_overrides);
	return platform_driver_register(&ehci_platform_driver);
}
module_init(ehci_platform_init);

static void __exit ehci_platform_cleanup(void)
{
	platform_driver_unregister(&ehci_platform_driver);
}
module_exit(ehci_platform_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Hauke Mehrtens");
MODULE_AUTHOR("Alan Stern");
MODULE_LICENSE("GPL");
