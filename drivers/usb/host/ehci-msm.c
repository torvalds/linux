/* ehci-msm.c - HSUSB Host Controller Driver Implementation
 *
 * Copyright (c) 2008-2010, Code Aurora Forum. All rights reserved.
 *
 * Partly derived from ehci-fsl.c and ehci-hcd.c
 * Copyright (c) 2000-2004 by David Brownell
 * Copyright (c) 2005 MontaVista Software
 *
 * All source code in this file is licensed under the following license except
 * where indicated.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can find it at http://www.fsf.org
 */

#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>

#include <linux/usb/otg.h>
#include <linux/usb/msm_hsusb_hw.h>

#define MSM_USB_BASE (hcd->regs)

static struct otg_transceiver *otg;

/*
 * ehci_run defined in drivers/usb/host/ehci-hcd.c reset the controller and
 * the configuration settings in ehci_msm_reset vanish after controller is
 * reset. Resetting the controler in ehci_run seems to be un-necessary
 * provided HCD reset the controller before calling ehci_run. Most of the HCD
 * do but some are not. So this function is same as ehci_run but we don't
 * reset the controller here.
 */
static int ehci_msm_run(struct usb_hcd *hcd)
{
	struct ehci_hcd		*ehci = hcd_to_ehci(hcd);
	u32			temp;
	u32			hcc_params;

	hcd->uses_new_polling = 1;

	ehci_writel(ehci, ehci->periodic_dma, &ehci->regs->frame_list);
	ehci_writel(ehci, (u32)ehci->async->qh_dma, &ehci->regs->async_next);

	/*
	 * hcc_params controls whether ehci->regs->segment must (!!!)
	 * be used; it constrains QH/ITD/SITD and QTD locations.
	 * pci_pool consistent memory always uses segment zero.
	 * streaming mappings for I/O buffers, like pci_map_single(),
	 * can return segments above 4GB, if the device allows.
	 *
	 * NOTE:  the dma mask is visible through dma_supported(), so
	 * drivers can pass this info along ... like NETIF_F_HIGHDMA,
	 * Scsi_Host.highmem_io, and so forth.  It's readonly to all
	 * host side drivers though.
	 */
	hcc_params = ehci_readl(ehci, &ehci->caps->hcc_params);
	if (HCC_64BIT_ADDR(hcc_params))
		ehci_writel(ehci, 0, &ehci->regs->segment);

	/*
	 * Philips, Intel, and maybe others need CMD_RUN before the
	 * root hub will detect new devices (why?); NEC doesn't
	 */
	ehci->command &= ~(CMD_LRESET|CMD_IAAD|CMD_PSE|CMD_ASE|CMD_RESET);
	ehci->command |= CMD_RUN;
	ehci_writel(ehci, ehci->command, &ehci->regs->command);
	dbg_cmd(ehci, "init", ehci->command);

	/*
	 * Start, enabling full USB 2.0 functionality ... usb 1.1 devices
	 * are explicitly handed to companion controller(s), so no TT is
	 * involved with the root hub.  (Except where one is integrated,
	 * and there's no companion controller unless maybe for USB OTG.)
	 *
	 * Turning on the CF flag will transfer ownership of all ports
	 * from the companions to the EHCI controller.  If any of the
	 * companions are in the middle of a port reset at the time, it
	 * could cause trouble.  Write-locking ehci_cf_port_reset_rwsem
	 * guarantees that no resets are in progress.  After we set CF,
	 * a short delay lets the hardware catch up; new resets shouldn't
	 * be started before the port switching actions could complete.
	 */
	down_write(&ehci_cf_port_reset_rwsem);
	hcd->state = HC_STATE_RUNNING;
	ehci_writel(ehci, FLAG_CF, &ehci->regs->configured_flag);
	ehci_readl(ehci, &ehci->regs->command);	/* unblock posted writes */
	usleep_range(5000, 5500);
	up_write(&ehci_cf_port_reset_rwsem);
	ehci->last_periodic_enable = ktime_get_real();

	temp = HC_VERSION(ehci_readl(ehci, &ehci->caps->hc_capbase));
	ehci_info(ehci,
		"USB %x.%x started, EHCI %x.%02x%s\n",
		((ehci->sbrn & 0xf0)>>4), (ehci->sbrn & 0x0f),
		temp >> 8, temp & 0xff,
		ignore_oc ? ", overcurrent ignored" : "");

	ehci_writel(ehci, INTR_MASK,
		    &ehci->regs->intr_enable); /* Turn On Interrupts */

	/* GRR this is run-once init(), being done every time the HC starts.
	 * So long as they're part of class devices, we can't do it init()
	 * since the class device isn't created that early.
	 */
	create_debug_files(ehci);
	create_companion_file(ehci);

	return 0;
}

static int ehci_msm_reset(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int retval;

	ehci->caps = USB_CAPLENGTH;
	ehci->regs = USB_CAPLENGTH +
		HC_LENGTH(ehci_readl(ehci, &ehci->caps->hc_capbase));

	/* cache the data to minimize the chip reads*/
	ehci->hcs_params = ehci_readl(ehci, &ehci->caps->hcs_params);

	hcd->has_tt = 1;
	ehci->sbrn = HCD_USB2;

	/* data structure init */
	retval = ehci_init(hcd);
	if (retval)
		return retval;

	retval = ehci_reset(ehci);
	if (retval)
		return retval;

	/* bursts of unspecified length. */
	writel(0, USB_AHBBURST);
	/* Use the AHB transactor */
	writel(0, USB_AHBMODE);
	/* Disable streaming mode and select host mode */
	writel(0x13, USB_USBMODE);

	ehci_port_power(ehci, 1);
	return 0;
}

static struct hc_driver msm_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "Qualcomm On-Chip EHCI Host Controller",
	.hcd_priv_size		= sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq			= ehci_irq,
	.flags			= HCD_USB2 | HCD_MEMORY,

	.reset			= ehci_msm_reset,
	.start			= ehci_msm_run,

	.stop			= ehci_stop,
	.shutdown		= ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue		= ehci_urb_enqueue,
	.urb_dequeue		= ehci_urb_dequeue,
	.endpoint_disable	= ehci_endpoint_disable,
	.endpoint_reset		= ehci_endpoint_reset,
	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,

	/*
	 * scheduling support
	 */
	.get_frame_number	= ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data	= ehci_hub_status_data,
	.hub_control		= ehci_hub_control,
	.relinquish_port	= ehci_relinquish_port,
	.port_handed_over	= ehci_port_handed_over,

	/*
	 * PM support
	 */
	.bus_suspend		= ehci_bus_suspend,
	.bus_resume		= ehci_bus_resume,
};

static int ehci_msm_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	struct resource *res;
	int ret;

	dev_dbg(&pdev->dev, "ehci_msm proble\n");

	hcd = usb_create_hcd(&msm_hc_driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		return  -ENOMEM;
	}

	hcd->irq = platform_get_irq(pdev, 0);
	if (hcd->irq < 0) {
		dev_err(&pdev->dev, "Unable to get IRQ resource\n");
		ret = hcd->irq;
		goto put_hcd;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Unable to get memory resource\n");
		ret = -ENODEV;
		goto put_hcd;
	}

	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);
	if (!hcd->regs) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto put_hcd;
	}

	/*
	 * OTG driver takes care of PHY initialization, clock management,
	 * powering up VBUS, mapping of registers address space and power
	 * management.
	 */
	otg = otg_get_transceiver();
	if (!otg) {
		dev_err(&pdev->dev, "unable to find transceiver\n");
		ret = -ENODEV;
		goto unmap;
	}

	ret = otg_set_host(otg, &hcd->self);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to register with transceiver\n");
		goto put_transceiver;
	}

	device_init_wakeup(&pdev->dev, 1);
	/*
	 * OTG device parent of HCD takes care of putting
	 * hardware into low power mode.
	 */
	pm_runtime_no_callbacks(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;

put_transceiver:
	otg_put_transceiver(otg);
unmap:
	iounmap(hcd->regs);
put_hcd:
	usb_put_hcd(hcd);

	return ret;
}

static int __devexit ehci_msm_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	device_init_wakeup(&pdev->dev, 0);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);

	otg_set_host(otg, NULL);
	otg_put_transceiver(otg);

	usb_put_hcd(hcd);

	return 0;
}

#ifdef CONFIG_PM
static int ehci_msm_pm_suspend(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);
	bool wakeup = device_may_wakeup(dev);

	dev_dbg(dev, "ehci-msm PM suspend\n");

	/*
	 * EHCI helper function has also the same check before manipulating
	 * port wakeup flags.  We do check here the same condition before
	 * calling the same helper function to avoid bringing hardware
	 * from Low power mode when there is no need for adjusting port
	 * wakeup flags.
	 */
	if (hcd->self.root_hub->do_remote_wakeup && !wakeup) {
		pm_runtime_resume(dev);
		ehci_prepare_ports_for_controller_suspend(hcd_to_ehci(hcd),
				wakeup);
	}

	return 0;
}

static int ehci_msm_pm_resume(struct device *dev)
{
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	dev_dbg(dev, "ehci-msm PM resume\n");
	ehci_prepare_ports_for_controller_resume(hcd_to_ehci(hcd));

	return 0;
}
#else
#define ehci_msm_pm_suspend	NULL
#define ehci_msm_pm_resume	NULL
#endif

static const struct dev_pm_ops ehci_msm_dev_pm_ops = {
	.suspend         = ehci_msm_pm_suspend,
	.resume          = ehci_msm_pm_resume,
};

static struct platform_driver ehci_msm_driver = {
	.probe	= ehci_msm_probe,
	.remove	= __devexit_p(ehci_msm_remove),
	.driver = {
		   .name = "msm_hsusb_host",
		   .pm = &ehci_msm_dev_pm_ops,
	},
};
