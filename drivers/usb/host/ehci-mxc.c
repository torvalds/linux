/*
 * Copyright (c) 2008 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 * Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include <linux/platform_data/usb-ehci-mxc.h>

#include <asm/mach-types.h>

#include "ehci.h"

#define DRIVER_DESC "Freescale On-Chip EHCI Host driver"

static const char hcd_name[] = "ehci-mxc";

#define ULPI_VIEWPORT_OFFSET	0x170

struct ehci_mxc_priv {
	struct clk *usbclk, *ahbclk, *phyclk;
};

static struct hc_driver __read_mostly ehci_mxc_hc_driver;

static const struct ehci_driver_overrides ehci_mxc_overrides __initdata = {
	.extra_priv_size =	sizeof(struct ehci_mxc_priv),
};

static int ehci_mxc_drv_probe(struct platform_device *pdev)
{
	struct mxc_usbh_platform_data *pdata = pdev->dev.platform_data;
	struct usb_hcd *hcd;
	struct resource *res;
	int irq, ret;
	struct ehci_mxc_priv *priv;
	struct device *dev = &pdev->dev;
	struct ehci_hcd *ehci;

	dev_info(&pdev->dev, "initializing i.MX USB Controller\n");

	if (!pdata) {
		dev_err(dev, "No platform data given, bailing out.\n");
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);

	hcd = usb_create_hcd(&ehci_mxc_hc_driver, dev, dev_name(dev));
	if (!hcd)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Found HC with no register addr. Check setup!\n");
		ret = -ENODEV;
		goto err_alloc;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	hcd->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hcd->regs)) {
		ret = PTR_ERR(hcd->regs);
		goto err_alloc;
	}

	hcd->has_tt = 1;
	ehci = hcd_to_ehci(hcd);
	priv = (struct ehci_mxc_priv *) ehci->priv;

	/* enable clocks */
	priv->usbclk = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(priv->usbclk)) {
		ret = PTR_ERR(priv->usbclk);
		goto err_alloc;
	}
	clk_prepare_enable(priv->usbclk);

	priv->ahbclk = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(priv->ahbclk)) {
		ret = PTR_ERR(priv->ahbclk);
		goto err_clk_ahb;
	}
	clk_prepare_enable(priv->ahbclk);

	/* "dr" device has its own clock on i.MX51 */
	priv->phyclk = devm_clk_get(&pdev->dev, "phy");
	if (IS_ERR(priv->phyclk))
		priv->phyclk = NULL;
	if (priv->phyclk)
		clk_prepare_enable(priv->phyclk);


	/* call platform specific init function */
	if (pdata->init) {
		ret = pdata->init(pdev);
		if (ret) {
			dev_err(dev, "platform init failed\n");
			goto err_init;
		}
		/* platforms need some time to settle changed IO settings */
		mdelay(10);
	}

	/* EHCI registers start at offset 0x100 */
	ehci->caps = hcd->regs + 0x100;
	ehci->regs = hcd->regs + 0x100 +
		HC_LENGTH(ehci, ehci_readl(ehci, &ehci->caps->hc_capbase));

	/* set up the PORTSCx register */
	ehci_writel(ehci, pdata->portsc, &ehci->regs->port_status[0]);

	/* is this really needed? */
	msleep(10);

	/* Initialize the transceiver */
	if (pdata->otg) {
		pdata->otg->io_priv = hcd->regs + ULPI_VIEWPORT_OFFSET;
		ret = usb_phy_init(pdata->otg);
		if (ret) {
			dev_err(dev, "unable to init transceiver, probably missing\n");
			ret = -ENODEV;
			goto err_add;
		}
		ret = otg_set_vbus(pdata->otg->otg, 1);
		if (ret) {
			dev_err(dev, "unable to enable vbus on transceiver\n");
			goto err_add;
		}
	}

	platform_set_drvdata(pdev, hcd);

	ret = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (ret)
		goto err_add;

	return 0;

err_add:
	if (pdata && pdata->exit)
		pdata->exit(pdev);
err_init:
	if (priv->phyclk)
		clk_disable_unprepare(priv->phyclk);

	clk_disable_unprepare(priv->ahbclk);
err_clk_ahb:
	clk_disable_unprepare(priv->usbclk);
err_alloc:
	usb_put_hcd(hcd);
	return ret;
}

static int __exit ehci_mxc_drv_remove(struct platform_device *pdev)
{
	struct mxc_usbh_platform_data *pdata = pdev->dev.platform_data;
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	struct ehci_mxc_priv *priv = (struct ehci_mxc_priv *) ehci->priv;

	usb_remove_hcd(hcd);

	if (pdata && pdata->exit)
		pdata->exit(pdev);

	if (pdata->otg)
		usb_phy_shutdown(pdata->otg);

	clk_disable_unprepare(priv->usbclk);
	clk_disable_unprepare(priv->ahbclk);

	if (priv->phyclk)
		clk_disable_unprepare(priv->phyclk);

	usb_put_hcd(hcd);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static void ehci_mxc_drv_shutdown(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

MODULE_ALIAS("platform:mxc-ehci");

static struct platform_driver ehci_mxc_driver = {
	.probe = ehci_mxc_drv_probe,
	.remove = ehci_mxc_drv_remove,
	.shutdown = ehci_mxc_drv_shutdown,
	.driver = {
		   .name = "mxc-ehci",
	},
};

static int __init ehci_mxc_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	pr_info("%s: " DRIVER_DESC "\n", hcd_name);

	ehci_init_driver(&ehci_mxc_hc_driver, &ehci_mxc_overrides);
	return platform_driver_register(&ehci_mxc_driver);
}
module_init(ehci_mxc_init);

static void __exit ehci_mxc_cleanup(void)
{
	platform_driver_unregister(&ehci_mxc_driver);
}
module_exit(ehci_mxc_cleanup);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Sascha Hauer");
MODULE_LICENSE("GPL");
