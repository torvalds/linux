/*
* OHCI HCD (Host Controller Driver) for USB.
*
* Copyright (C) 2010 ST Microelectronics.
* Deepak Sikri<deepak.sikri@st.com>
*
* Based on various ohci-*.c drivers
*
* This file is licensed under the terms of the GNU General Public
* License version 2. This program is licensed "as is" without any
* warranty of any kind, whether express or implied.
*/

#include <linux/signal.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of.h>

struct spear_ohci {
	struct ohci_hcd ohci;
	struct clk *clk;
};

#define to_spear_ohci(hcd)	(struct spear_ohci *)hcd_to_ohci(hcd)

static void spear_start_ohci(struct spear_ohci *ohci)
{
	clk_prepare_enable(ohci->clk);
}

static void spear_stop_ohci(struct spear_ohci *ohci)
{
	clk_disable_unprepare(ohci->clk);
}

static int __devinit ohci_spear_start(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	int ret;

	ret = ohci_init(ohci);
	if (ret < 0)
		return ret;
	ohci->regs = hcd->regs;

	ret = ohci_run(ohci);
	if (ret < 0) {
		dev_err(hcd->self.controller, "can't start\n");
		ohci_stop(hcd);
		return ret;
	}

	create_debug_files(ohci);

#ifdef DEBUG
	ohci_dump(ohci, 1);
#endif
	return 0;
}

static const struct hc_driver ohci_spear_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "SPEAr OHCI",
	.hcd_priv_size		= sizeof(struct spear_ohci),

	/* generic hardware linkage */
	.irq			= ohci_irq,
	.flags			= HCD_USB11 | HCD_MEMORY,

	/* basic lifecycle operations */
	.start			= ohci_spear_start,
	.stop			= ohci_stop,
	.shutdown		= ohci_shutdown,
#ifdef	CONFIG_PM
	.bus_suspend		= ohci_bus_suspend,
	.bus_resume		= ohci_bus_resume,
#endif

	/* managing i/o requests and associated device resources */
	.urb_enqueue		= ohci_urb_enqueue,
	.urb_dequeue		= ohci_urb_dequeue,
	.endpoint_disable	= ohci_endpoint_disable,

	/* scheduling support */
	.get_frame_number	= ohci_get_frame,

	/* root hub support */
	.hub_status_data	= ohci_hub_status_data,
	.hub_control		= ohci_hub_control,

	.start_port_reset	= ohci_start_port_reset,
};

static u64 spear_ohci_dma_mask = DMA_BIT_MASK(32);

static int spear_ohci_hcd_drv_probe(struct platform_device *pdev)
{
	const struct hc_driver *driver = &ohci_spear_hc_driver;
	struct usb_hcd *hcd = NULL;
	struct clk *usbh_clk;
	struct spear_ohci *ohci_p;
	struct resource *res;
	int retval, irq;
	char clk_name[20] = "usbh_clk";
	static int instance = -1;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		retval = irq;
		goto fail_irq_get;
	}

	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we have dma capability bindings this can go away.
	 */
	if (!pdev->dev.dma_mask)
		pdev->dev.dma_mask = &spear_ohci_dma_mask;

	/*
	 * Increment the device instance, when probing via device-tree
	 */
	if (pdev->id < 0)
		instance++;
	else
		instance = pdev->id;
	sprintf(clk_name, "usbh.%01d_clk", instance);

	usbh_clk = clk_get(NULL, clk_name);
	if (IS_ERR(usbh_clk)) {
		dev_err(&pdev->dev, "Error getting interface clock\n");
		retval = PTR_ERR(usbh_clk);
		goto fail_get_usbh_clk;
	}

	hcd = usb_create_hcd(driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		retval = -ENOMEM;
		goto fail_create_hcd;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		retval = -ENODEV;
		goto fail_request_resource;
	}

	hcd->rsrc_start = pdev->resource[0].start;
	hcd->rsrc_len = resource_size(res);
	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, hcd_name)) {
		dev_dbg(&pdev->dev, "request_mem_region failed\n");
		retval = -EBUSY;
		goto fail_request_resource;
	}

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_dbg(&pdev->dev, "ioremap failed\n");
		retval = -ENOMEM;
		goto fail_ioremap;
	}

	ohci_p = (struct spear_ohci *)hcd_to_ohci(hcd);
	ohci_p->clk = usbh_clk;
	spear_start_ohci(ohci_p);
	ohci_hcd_init(hcd_to_ohci(hcd));

	retval = usb_add_hcd(hcd, platform_get_irq(pdev, 0), 0);
	if (retval == 0)
		return retval;

	spear_stop_ohci(ohci_p);
	iounmap(hcd->regs);
fail_ioremap:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
fail_request_resource:
	usb_put_hcd(hcd);
fail_create_hcd:
	clk_put(usbh_clk);
fail_get_usbh_clk:
fail_irq_get:
	dev_err(&pdev->dev, "init fail, %d\n", retval);

	return retval;
}

static int spear_ohci_hcd_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct spear_ohci *ohci_p = to_spear_ohci(hcd);

	usb_remove_hcd(hcd);
	if (ohci_p->clk)
		spear_stop_ohci(ohci_p);

	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);

	if (ohci_p->clk)
		clk_put(ohci_p->clk);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

#if defined(CONFIG_PM)
static int spear_ohci_hcd_drv_suspend(struct platform_device *dev,
		pm_message_t message)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	struct ohci_hcd	*ohci = hcd_to_ohci(hcd);
	struct spear_ohci *ohci_p = to_spear_ohci(hcd);

	if (time_before(jiffies, ohci->next_statechange))
		msleep(5);
	ohci->next_statechange = jiffies;

	spear_stop_ohci(ohci_p);
	return 0;
}

static int spear_ohci_hcd_drv_resume(struct platform_device *dev)
{
	struct usb_hcd *hcd = platform_get_drvdata(dev);
	struct ohci_hcd	*ohci = hcd_to_ohci(hcd);
	struct spear_ohci *ohci_p = to_spear_ohci(hcd);

	if (time_before(jiffies, ohci->next_statechange))
		msleep(5);
	ohci->next_statechange = jiffies;

	spear_start_ohci(ohci_p);
	ohci_resume(hcd, false);
	return 0;
}
#endif

static struct of_device_id spear_ohci_id_table[] __devinitdata = {
	{ .compatible = "st,spear600-ohci", },
	{ },
};

/* Driver definition to register with the platform bus */
static struct platform_driver spear_ohci_hcd_driver = {
	.probe =	spear_ohci_hcd_drv_probe,
	.remove =	spear_ohci_hcd_drv_remove,
#ifdef CONFIG_PM
	.suspend =	spear_ohci_hcd_drv_suspend,
	.resume =	spear_ohci_hcd_drv_resume,
#endif
	.driver = {
		.owner = THIS_MODULE,
		.name = "spear-ohci",
		.of_match_table = of_match_ptr(spear_ohci_id_table),
	},
};

MODULE_ALIAS("platform:spear-ohci");
