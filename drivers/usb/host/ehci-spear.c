/*
* Driver for EHCI HCD on SPEAR SOC
*
* Copyright (C) 2010 ST Micro Electronics,
* Deepak Sikri <deepak.sikri@st.com>
*
* Based on various ehci-*.c drivers
*
* This file is subject to the terms and conditions of the GNU General Public
* License. See the file COPYING in the main directory of this archive for
* more details.
*/

#include <linux/platform_device.h>
#include <linux/clk.h>

struct spear_ehci {
	struct ehci_hcd ehci;
	struct clk *clk;
};

#define to_spear_ehci(hcd)	(struct spear_ehci *)hcd_to_ehci(hcd)

static void spear_start_ehci(struct spear_ehci *ehci)
{
	clk_enable(ehci->clk);
}

static void spear_stop_ehci(struct spear_ehci *ehci)
{
	clk_disable(ehci->clk);
}

static int ehci_spear_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int retval = 0;

	/* registers start at offset 0x0 */
	ehci->caps = hcd->regs;
	ehci->regs = hcd->regs + HC_LENGTH(ehci, ehci_readl(ehci,
				&ehci->caps->hc_capbase));
	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = ehci_readl(ehci, &ehci->caps->hcs_params);
	retval = ehci_halt(ehci);
	if (retval)
		return retval;

	retval = ehci_init(hcd);
	if (retval)
		return retval;

	ehci_reset(ehci);
	ehci_port_power(ehci, 0);

	return retval;
}

static const struct hc_driver ehci_spear_hc_driver = {
	.description			= hcd_name,
	.product_desc			= "SPEAr EHCI",
	.hcd_priv_size			= sizeof(struct spear_ehci),

	/* generic hardware linkage */
	.irq				= ehci_irq,
	.flags				= HCD_MEMORY | HCD_USB2,

	/* basic lifecycle operations */
	.reset				= ehci_spear_setup,
	.start				= ehci_run,
	.stop				= ehci_stop,
	.shutdown			= ehci_shutdown,

	/* managing i/o requests and associated device resources */
	.urb_enqueue			= ehci_urb_enqueue,
	.urb_dequeue			= ehci_urb_dequeue,
	.endpoint_disable		= ehci_endpoint_disable,
	.endpoint_reset			= ehci_endpoint_reset,

	/* scheduling support */
	.get_frame_number		= ehci_get_frame,

	/* root hub support */
	.hub_status_data		= ehci_hub_status_data,
	.hub_control			= ehci_hub_control,
	.bus_suspend			= ehci_bus_suspend,
	.bus_resume			= ehci_bus_resume,
	.relinquish_port		= ehci_relinquish_port,
	.port_handed_over		= ehci_port_handed_over,
	.clear_tt_buffer_complete	= ehci_clear_tt_buffer_complete,
};

static int spear_ehci_hcd_drv_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd ;
	struct spear_ehci *ehci;
	struct resource *res;
	struct clk *usbh_clk;
	const struct hc_driver *driver = &ehci_spear_hc_driver;
	int *pdata = pdev->dev.platform_data;
	int irq, retval;
	char clk_name[20] = "usbh_clk";

	if (pdata == NULL)
		return -EFAULT;

	if (usb_disabled())
		return -ENODEV;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		retval = irq;
		goto fail_irq_get;
	}

	if (*pdata >= 0)
		sprintf(clk_name, "usbh.%01d_clk", *pdata);

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

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len,
				driver->description)) {
		retval = -EBUSY;
		goto fail_request_resource;
	}

	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (hcd->regs == NULL) {
		dev_dbg(&pdev->dev, "error mapping memory\n");
		retval = -ENOMEM;
		goto fail_ioremap;
	}

	ehci = (struct spear_ehci *)hcd_to_ehci(hcd);
	ehci->clk = usbh_clk;

	spear_start_ehci(ehci);
	retval = usb_add_hcd(hcd, irq, IRQF_SHARED | IRQF_DISABLED);
	if (retval)
		goto fail_add_hcd;

	return retval;

fail_add_hcd:
	spear_stop_ehci(ehci);
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

	return retval ;
}

static int spear_ehci_hcd_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct spear_ehci *ehci_p = to_spear_ehci(hcd);

	if (!hcd)
		return 0;
	if (in_interrupt())
		BUG();
	usb_remove_hcd(hcd);

	if (ehci_p->clk)
		spear_stop_ehci(ehci_p);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);

	if (ehci_p->clk)
		clk_put(ehci_p->clk);

	return 0;
}

static struct platform_driver spear_ehci_hcd_driver = {
	.probe		= spear_ehci_hcd_drv_probe,
	.remove		= spear_ehci_hcd_drv_remove,
	.shutdown	= usb_hcd_platform_shutdown,
	.driver		= {
		.name = "spear-ehci",
		.bus = &platform_bus_type
	}
};

MODULE_ALIAS("platform:spear-ehci");
