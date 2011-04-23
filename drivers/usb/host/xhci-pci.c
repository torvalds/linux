/*
 * xHCI host controller driver PCI Bus Glue.
 *
 * Copyright (C) 2008 Intel Corp.
 *
 * Author: Sarah Sharp
 * Some code borrowed from the Linux EHCI driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#include <linux/pci.h>

#include "xhci.h"

/* Device for a quirk */
#define PCI_VENDOR_ID_FRESCO_LOGIC	0x1b73
#define PCI_DEVICE_ID_FRESCO_LOGIC_PDK	0x1000

static const char hcd_name[] = "xhci_hcd";

/* called after powerup, by probe or system-pm "wakeup" */
static int xhci_pci_reinit(struct xhci_hcd *xhci, struct pci_dev *pdev)
{
	/*
	 * TODO: Implement finding debug ports later.
	 * TODO: see if there are any quirks that need to be added to handle
	 * new extended capabilities.
	 */

	/* PCI Memory-Write-Invalidate cycle support is optional (uncommon) */
	if (!pci_set_mwi(pdev))
		xhci_dbg(xhci, "MWI active\n");

	xhci_dbg(xhci, "Finished xhci_pci_reinit\n");
	return 0;
}

/* called during probe() after chip reset completes */
static int xhci_pci_setup(struct usb_hcd *hcd)
{
	struct xhci_hcd		*xhci;
	struct pci_dev		*pdev = to_pci_dev(hcd->self.controller);
	int			retval;
	u32			temp;

	hcd->self.sg_tablesize = TRBS_PER_SEGMENT - 2;

	if (usb_hcd_is_primary_hcd(hcd)) {
		xhci = kzalloc(sizeof(struct xhci_hcd), GFP_KERNEL);
		if (!xhci)
			return -ENOMEM;
		*((struct xhci_hcd **) hcd->hcd_priv) = xhci;
		xhci->main_hcd = hcd;
		/* Mark the first roothub as being USB 2.0.
		 * The xHCI driver will register the USB 3.0 roothub.
		 */
		hcd->speed = HCD_USB2;
		hcd->self.root_hub->speed = USB_SPEED_HIGH;
		/*
		 * USB 2.0 roothub under xHCI has an integrated TT,
		 * (rate matching hub) as opposed to having an OHCI/UHCI
		 * companion controller.
		 */
		hcd->has_tt = 1;
	} else {
		/* xHCI private pointer was set in xhci_pci_probe for the second
		 * registered roothub.
		 */
		xhci = hcd_to_xhci(hcd);
		temp = xhci_readl(xhci, &xhci->cap_regs->hcc_params);
		if (HCC_64BIT_ADDR(temp)) {
			xhci_dbg(xhci, "Enabling 64-bit DMA addresses.\n");
			dma_set_mask(hcd->self.controller, DMA_BIT_MASK(64));
		} else {
			dma_set_mask(hcd->self.controller, DMA_BIT_MASK(32));
		}
		return 0;
	}

	xhci->cap_regs = hcd->regs;
	xhci->op_regs = hcd->regs +
		HC_LENGTH(xhci_readl(xhci, &xhci->cap_regs->hc_capbase));
	xhci->run_regs = hcd->regs +
		(xhci_readl(xhci, &xhci->cap_regs->run_regs_off) & RTSOFF_MASK);
	/* Cache read-only capability registers */
	xhci->hcs_params1 = xhci_readl(xhci, &xhci->cap_regs->hcs_params1);
	xhci->hcs_params2 = xhci_readl(xhci, &xhci->cap_regs->hcs_params2);
	xhci->hcs_params3 = xhci_readl(xhci, &xhci->cap_regs->hcs_params3);
	xhci->hcc_params = xhci_readl(xhci, &xhci->cap_regs->hc_capbase);
	xhci->hci_version = HC_VERSION(xhci->hcc_params);
	xhci->hcc_params = xhci_readl(xhci, &xhci->cap_regs->hcc_params);
	xhci_print_registers(xhci);

	/* Look for vendor-specific quirks */
	if (pdev->vendor == PCI_VENDOR_ID_FRESCO_LOGIC &&
			pdev->device == PCI_DEVICE_ID_FRESCO_LOGIC_PDK &&
			pdev->revision == 0x0) {
			xhci->quirks |= XHCI_RESET_EP_QUIRK;
			xhci_dbg(xhci, "QUIRK: Fresco Logic xHC needs configure"
					" endpoint cmd after reset endpoint\n");
	}
	if (pdev->vendor == PCI_VENDOR_ID_NEC)
		xhci->quirks |= XHCI_NEC_HOST;

	/* AMD PLL quirk */
	if (pdev->vendor == PCI_VENDOR_ID_AMD && usb_amd_find_chipset_info())
		xhci->quirks |= XHCI_AMD_PLL_FIX;

	/* Make sure the HC is halted. */
	retval = xhci_halt(xhci);
	if (retval)
		goto error;

	xhci_dbg(xhci, "Resetting HCD\n");
	/* Reset the internal HC memory state and registers. */
	retval = xhci_reset(xhci);
	if (retval)
		goto error;
	xhci_dbg(xhci, "Reset complete\n");

	temp = xhci_readl(xhci, &xhci->cap_regs->hcc_params);
	if (HCC_64BIT_ADDR(temp)) {
		xhci_dbg(xhci, "Enabling 64-bit DMA addresses.\n");
		dma_set_mask(hcd->self.controller, DMA_BIT_MASK(64));
	} else {
		dma_set_mask(hcd->self.controller, DMA_BIT_MASK(32));
	}

	xhci_dbg(xhci, "Calling HCD init\n");
	/* Initialize HCD and host controller data structures. */
	retval = xhci_init(hcd);
	if (retval)
		goto error;
	xhci_dbg(xhci, "Called HCD init\n");

	pci_read_config_byte(pdev, XHCI_SBRN_OFFSET, &xhci->sbrn);
	xhci_dbg(xhci, "Got SBRN %u\n", (unsigned int) xhci->sbrn);

	/* Find any debug ports */
	retval = xhci_pci_reinit(xhci, pdev);
	if (!retval)
		return retval;

error:
	kfree(xhci);
	return retval;
}

/*
 * We need to register our own PCI probe function (instead of the USB core's
 * function) in order to create a second roothub under xHCI.
 */
static int xhci_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int retval;
	struct xhci_hcd *xhci;
	struct hc_driver *driver;
	struct usb_hcd *hcd;

	driver = (struct hc_driver *)id->driver_data;
	/* Register the USB 2.0 roothub.
	 * FIXME: USB core must know to register the USB 2.0 roothub first.
	 * This is sort of silly, because we could just set the HCD driver flags
	 * to say USB 2.0, but I'm not sure what the implications would be in
	 * the other parts of the HCD code.
	 */
	retval = usb_hcd_pci_probe(dev, id);

	if (retval)
		return retval;

	/* USB 2.0 roothub is stored in the PCI device now. */
	hcd = dev_get_drvdata(&dev->dev);
	xhci = hcd_to_xhci(hcd);
	xhci->shared_hcd = usb_create_shared_hcd(driver, &dev->dev,
				pci_name(dev), hcd);
	if (!xhci->shared_hcd) {
		retval = -ENOMEM;
		goto dealloc_usb2_hcd;
	}

	/* Set the xHCI pointer before xhci_pci_setup() (aka hcd_driver.reset)
	 * is called by usb_add_hcd().
	 */
	*((struct xhci_hcd **) xhci->shared_hcd->hcd_priv) = xhci;

	retval = usb_add_hcd(xhci->shared_hcd, dev->irq,
			IRQF_DISABLED | IRQF_SHARED);
	if (retval)
		goto put_usb3_hcd;
	/* Roothub already marked as USB 3.0 speed */
	return 0;

put_usb3_hcd:
	usb_put_hcd(xhci->shared_hcd);
dealloc_usb2_hcd:
	usb_hcd_pci_remove(dev);
	return retval;
}

static void xhci_pci_remove(struct pci_dev *dev)
{
	struct xhci_hcd *xhci;

	xhci = hcd_to_xhci(pci_get_drvdata(dev));
	if (xhci->shared_hcd) {
		usb_remove_hcd(xhci->shared_hcd);
		usb_put_hcd(xhci->shared_hcd);
	}
	usb_hcd_pci_remove(dev);
	kfree(xhci);
}

#ifdef CONFIG_PM
static int xhci_pci_suspend(struct usb_hcd *hcd, bool do_wakeup)
{
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
	int	retval = 0;

	if (hcd->state != HC_STATE_SUSPENDED ||
			xhci->shared_hcd->state != HC_STATE_SUSPENDED)
		return -EINVAL;

	retval = xhci_suspend(xhci);

	return retval;
}

static int xhci_pci_resume(struct usb_hcd *hcd, bool hibernated)
{
	struct xhci_hcd		*xhci = hcd_to_xhci(hcd);
	int			retval = 0;

	retval = xhci_resume(xhci, hibernated);
	return retval;
}
#endif /* CONFIG_PM */

static const struct hc_driver xhci_pci_hc_driver = {
	.description =		hcd_name,
	.product_desc =		"xHCI Host Controller",
	.hcd_priv_size =	sizeof(struct xhci_hcd *),

	/*
	 * generic hardware linkage
	 */
	.irq =			xhci_irq,
	.flags =		HCD_MEMORY | HCD_USB3 | HCD_SHARED,

	/*
	 * basic lifecycle operations
	 */
	.reset =		xhci_pci_setup,
	.start =		xhci_run,
#ifdef CONFIG_PM
	.pci_suspend =          xhci_pci_suspend,
	.pci_resume =           xhci_pci_resume,
#endif
	.stop =			xhci_stop,
	.shutdown =		xhci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		xhci_urb_enqueue,
	.urb_dequeue =		xhci_urb_dequeue,
	.alloc_dev =		xhci_alloc_dev,
	.free_dev =		xhci_free_dev,
	.alloc_streams =	xhci_alloc_streams,
	.free_streams =		xhci_free_streams,
	.add_endpoint =		xhci_add_endpoint,
	.drop_endpoint =	xhci_drop_endpoint,
	.endpoint_reset =	xhci_endpoint_reset,
	.check_bandwidth =	xhci_check_bandwidth,
	.reset_bandwidth =	xhci_reset_bandwidth,
	.address_device =	xhci_address_device,
	.update_hub_device =	xhci_update_hub_device,
	.reset_device =		xhci_discover_or_reset_device,

	/*
	 * scheduling support
	 */
	.get_frame_number =	xhci_get_frame,

	/* Root hub support */
	.hub_control =		xhci_hub_control,
	.hub_status_data =	xhci_hub_status_data,
	.bus_suspend =		xhci_bus_suspend,
	.bus_resume =		xhci_bus_resume,
};

/*-------------------------------------------------------------------------*/

/* PCI driver selection metadata; PCI hotplugging uses this */
static const struct pci_device_id pci_ids[] = { {
	/* handle any USB 3.0 xHCI controller */
	PCI_DEVICE_CLASS(PCI_CLASS_SERIAL_USB_XHCI, ~0),
	.driver_data =	(unsigned long) &xhci_pci_hc_driver,
	},
	{ /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE(pci, pci_ids);

/* pci driver glue; this is a "new style" PCI driver module */
static struct pci_driver xhci_pci_driver = {
	.name =		(char *) hcd_name,
	.id_table =	pci_ids,

	.probe =	xhci_pci_probe,
	.remove =	xhci_pci_remove,
	/* suspend and resume implemented later */

	.shutdown = 	usb_hcd_pci_shutdown,
#ifdef CONFIG_PM_SLEEP
	.driver = {
		.pm = &usb_hcd_pci_pm_ops
	},
#endif
};

int xhci_register_pci(void)
{
	return pci_register_driver(&xhci_pci_driver);
}

void xhci_unregister_pci(void)
{
	pci_unregister_driver(&xhci_pci_driver);
}
