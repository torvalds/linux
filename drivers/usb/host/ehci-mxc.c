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

#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/slab.h>

#include <linux/platform_data/usb-ehci-mxc.h>

#include <asm/mach-types.h>

#define ULPI_VIEWPORT_OFFSET	0x170

struct ehci_mxc_priv {
	struct clk *usbclk, *ahbclk, *phyclk;
	struct usb_hcd *hcd;
};

/* called during probe() after chip reset completes */
static int ehci_mxc_setup(struct usb_hcd *hcd)
{
	hcd->has_tt = 1;

	return ehci_setup(hcd);
}

static const struct hc_driver ehci_mxc_hc_driver = {
	.description = hcd_name,
	.product_desc = "Freescale On-Chip EHCI Host Controller",
	.hcd_priv_size = sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq = ehci_irq,
	.flags = HCD_USB2 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.reset = ehci_mxc_setup,
	.start = ehci_run,
	.stop = ehci_stop,
	.shutdown = ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue = ehci_urb_enqueue,
	.urb_dequeue = ehci_urb_dequeue,
	.endpoint_disable = ehci_endpoint_disable,
	.endpoint_reset = ehci_endpoint_reset,

	/*
	 * scheduling support
	 */
	.get_frame_number = ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data = ehci_hub_status_data,
	.hub_control = ehci_hub_control,
	.bus_suspend = ehci_bus_suspend,
	.bus_resume = ehci_bus_resume,
	.relinquish_port = ehci_relinquish_port,
	.port_handed_over = ehci_port_handed_over,

	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,
};

static int ehci_mxc_drv_probe(struct platform_device *pdev)
{
	struct mxc_usbh_platform_data *pdata = pdev->dev.platform_data;
	struct usb_hcd *hcd;
	struct resource *res;
	int irq, ret;
	unsigned int flags;
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

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Found HC with no register addr. Check setup!\n");
		ret = -ENODEV;
		goto err_alloc;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	hcd->regs = devm_request_and_ioremap(&pdev->dev, res);
	if (!hcd->regs) {
		dev_err(dev, "error mapping memory\n");
		ret = -EFAULT;
		goto err_alloc;
	}

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

	ehci = hcd_to_ehci(hcd);

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

	priv->hcd = hcd;
	platform_set_drvdata(pdev, priv);

	ret = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (ret)
		goto err_add;

	if (pdata->otg) {
		/*
		 * efikamx and efikasb have some hardware bug which is
		 * preventing usb to work unless CHRGVBUS is set.
		 * It's in violation of USB specs
		 */
		if (machine_is_mx51_efikamx() || machine_is_mx51_efikasb()) {
			flags = usb_phy_io_read(pdata->otg,
							ULPI_OTG_CTRL);
			flags |= ULPI_OTG_CTRL_CHRGVBUS;
			ret = usb_phy_io_write(pdata->otg, flags,
							ULPI_OTG_CTRL);
			if (ret) {
				dev_err(dev, "unable to set CHRVBUS\n");
				goto err_add;
			}
		}
	}

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
	struct ehci_mxc_priv *priv = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = priv->hcd;

	if (pdata && pdata->exit)
		pdata->exit(pdev);

	if (pdata->otg)
		usb_phy_shutdown(pdata->otg);

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);
	platform_set_drvdata(pdev, NULL);

	clk_disable_unprepare(priv->usbclk);
	clk_disable_unprepare(priv->ahbclk);

	if (priv->phyclk)
		clk_disable_unprepare(priv->phyclk);

	return 0;
}

static void ehci_mxc_drv_shutdown(struct platform_device *pdev)
{
	struct ehci_mxc_priv *priv = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = priv->hcd;

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

MODULE_ALIAS("platform:mxc-ehci");

static struct platform_driver ehci_mxc_driver = {
	.probe = ehci_mxc_drv_probe,
	.remove = __exit_p(ehci_mxc_drv_remove),
	.shutdown = ehci_mxc_drv_shutdown,
	.driver = {
		   .name = "mxc-ehci",
	},
};
