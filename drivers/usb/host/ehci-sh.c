/*
 * SuperH EHCI host controller driver
 *
 * Copyright (C) 2010  Paul Mundt
 *
 * Based on ohci-sh.c and ehci-atmel.c.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/platform_data/ehci-sh.h>

struct ehci_sh_priv {
	struct clk *iclk, *fclk;
	struct usb_hcd *hcd;
};

static int ehci_sh_reset(struct usb_hcd *hcd)
{
	struct ehci_hcd	*ehci = hcd_to_ehci(hcd);

	ehci->caps = hcd->regs;

	return ehci_setup(hcd);
}

static const struct hc_driver ehci_sh_hc_driver = {
	.description			= hcd_name,
	.product_desc			= "SuperH EHCI",
	.hcd_priv_size			= sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq				= ehci_irq,
	.flags				= HCD_USB2 | HCD_MEMORY | HCD_BH,

	/*
	 * basic lifecycle operations
	 */
	.reset				= ehci_sh_reset,
	.start				= ehci_run,
	.stop				= ehci_stop,
	.shutdown			= ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue			= ehci_urb_enqueue,
	.urb_dequeue			= ehci_urb_dequeue,
	.endpoint_disable		= ehci_endpoint_disable,
	.endpoint_reset			= ehci_endpoint_reset,

	/*
	 * scheduling support
	 */
	.get_frame_number		= ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data		= ehci_hub_status_data,
	.hub_control			= ehci_hub_control,

#ifdef CONFIG_PM
	.bus_suspend			= ehci_bus_suspend,
	.bus_resume			= ehci_bus_resume,
#endif

	.relinquish_port		= ehci_relinquish_port,
	.port_handed_over		= ehci_port_handed_over,
	.clear_tt_buffer_complete	= ehci_clear_tt_buffer_complete,
};

static int ehci_hcd_sh_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct ehci_sh_priv *priv;
	struct ehci_sh_platdata *pdata;
	struct usb_hcd *hcd;
	int irq, ret;

	if (usb_disabled())
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"Found HC with no register addr. Check %s setup!\n",
			dev_name(&pdev->dev));
		ret = -ENODEV;
		goto fail_create_hcd;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(&pdev->dev,
			"Found HC with no IRQ. Check %s setup!\n",
			dev_name(&pdev->dev));
		ret = -ENODEV;
		goto fail_create_hcd;
	}

	pdata = dev_get_platdata(&pdev->dev);

	/* initialize hcd */
	hcd = usb_create_hcd(&ehci_sh_hc_driver, &pdev->dev,
			     dev_name(&pdev->dev));
	if (!hcd) {
		ret = -ENOMEM;
		goto fail_create_hcd;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	hcd->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hcd->regs)) {
		ret = PTR_ERR(hcd->regs);
		goto fail_request_resource;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(struct ehci_sh_priv),
			    GFP_KERNEL);
	if (!priv) {
		dev_dbg(&pdev->dev, "error allocating priv data\n");
		ret = -ENOMEM;
		goto fail_request_resource;
	}

	/* These are optional, we don't care if they fail */
	priv->fclk = devm_clk_get(&pdev->dev, "usb_fck");
	if (IS_ERR(priv->fclk))
		priv->fclk = NULL;

	priv->iclk = devm_clk_get(&pdev->dev, "usb_ick");
	if (IS_ERR(priv->iclk))
		priv->iclk = NULL;

	clk_enable(priv->fclk);
	clk_enable(priv->iclk);

	if (pdata && pdata->phy_init)
		pdata->phy_init();

	ret = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to add hcd");
		goto fail_add_hcd;
	}

	priv->hcd = hcd;
	platform_set_drvdata(pdev, priv);

	return ret;

fail_add_hcd:
	clk_disable(priv->iclk);
	clk_disable(priv->fclk);

fail_request_resource:
	usb_put_hcd(hcd);
fail_create_hcd:
	dev_err(&pdev->dev, "init %s fail, %d\n", dev_name(&pdev->dev), ret);

	return ret;
}

static int ehci_hcd_sh_remove(struct platform_device *pdev)
{
	struct ehci_sh_priv *priv = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = priv->hcd;

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);

	clk_disable(priv->fclk);
	clk_disable(priv->iclk);

	return 0;
}

static void ehci_hcd_sh_shutdown(struct platform_device *pdev)
{
	struct ehci_sh_priv *priv = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = priv->hcd;

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

static struct platform_driver ehci_hcd_sh_driver = {
	.probe		= ehci_hcd_sh_probe,
	.remove		= ehci_hcd_sh_remove,
	.shutdown	= ehci_hcd_sh_shutdown,
	.driver		= {
		.name	= "sh_ehci",
		.owner	= THIS_MODULE,
	},
};

MODULE_ALIAS("platform:sh_ehci");
