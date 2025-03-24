// SPDX-License-Identifier: GPL-2.0
/*
 * UHCI HCD (Host Controller Driver) for GRLIB GRUSBHC
 *
 * Copyright (c) 2011 Jan Andersson <jan@gaisler.com>
 *
 * This file is based on UHCI PCI HCD:
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999-2002 Johannes Erdfelt, johannes@erdfelt.com
 * (C) Copyright 1999 Randy Dunlap
 * (C) Copyright 1999 Georg Acher, acher@in.tum.de
 * (C) Copyright 1999 Deti Fliegl, deti@fliegl.de
 * (C) Copyright 1999 Thomas Sailer, sailer@ife.ee.ethz.ch
 * (C) Copyright 1999 Roman Weissgaerber, weissg@vienna.at
 * (C) Copyright 2000 Yggdrasil Computing, Inc. (port of new PCI interface
 *               support from usb-ohci.c by Adam Richter, adam@yggdrasil.com).
 * (C) Copyright 1999 Gregory P. Smith (from usb-ohci.c)
 * (C) Copyright 2004-2007 Alan Stern, stern@rowland.harvard.edu
 */

#include <linux/device.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

static int uhci_grlib_init(struct usb_hcd *hcd)
{
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);

	/*
	 * Probe to determine the endianness of the controller.
	 * We know that bit 7 of the PORTSC1 register is always set
	 * and bit 15 is always clear.  If uhci_readw() yields a value
	 * with bit 7 (0x80) turned on then the current little-endian
	 * setting is correct.  Otherwise we assume the value was
	 * byte-swapped; hence the register interface and presumably
	 * also the descriptors are big-endian.
	 */
	if (!(uhci_readw(uhci, USBPORTSC1) & 0x80)) {
		uhci->big_endian_mmio = 1;
		uhci->big_endian_desc = 1;
	}

	uhci->rh_numports = uhci_count_ports(hcd);

	/* Set up pointers to generic functions */
	uhci->reset_hc = uhci_generic_reset_hc;
	uhci->check_and_reset_hc = uhci_generic_check_and_reset_hc;
	/* No special actions need to be taken for the functions below */
	uhci->configure_hc = NULL;
	uhci->resume_detect_interrupts_are_broken = NULL;
	uhci->global_suspend_mode_is_broken = NULL;

	/* Reset if the controller isn't already safely quiescent. */
	check_and_reset_hc(uhci);
	return 0;
}

static const struct hc_driver uhci_grlib_hc_driver = {
	.description =		hcd_name,
	.product_desc =		"GRLIB GRUSBHC UHCI Host Controller",
	.hcd_priv_size =	sizeof(struct uhci_hcd),

	/* Generic hardware linkage */
	.irq =			uhci_irq,
	.flags =		HCD_MEMORY | HCD_DMA | HCD_USB11,

	/* Basic lifecycle operations */
	.reset =		uhci_grlib_init,
	.start =		uhci_start,
#ifdef CONFIG_PM
	.pci_suspend =		NULL,
	.pci_resume =		NULL,
	.bus_suspend =		uhci_rh_suspend,
	.bus_resume =		uhci_rh_resume,
#endif
	.stop =			uhci_stop,

	.urb_enqueue =		uhci_urb_enqueue,
	.urb_dequeue =		uhci_urb_dequeue,

	.endpoint_disable =	uhci_hcd_endpoint_disable,
	.get_frame_number =	uhci_hcd_get_frame_number,

	.hub_status_data =	uhci_hub_status_data,
	.hub_control =		uhci_hub_control,
};


static int uhci_hcd_grlib_probe(struct platform_device *op)
{
	struct device_node *dn = op->dev.of_node;
	struct usb_hcd *hcd;
	struct uhci_hcd	*uhci = NULL;
	struct resource res;
	int irq;
	int rv;

	if (usb_disabled())
		return -ENODEV;

	dev_dbg(&op->dev, "initializing GRUSBHC UHCI USB Controller\n");

	rv = of_address_to_resource(dn, 0, &res);
	if (rv)
		return rv;

	/* usb_create_hcd requires dma_mask != NULL */
	op->dev.dma_mask = &op->dev.coherent_dma_mask;
	hcd = usb_create_hcd(&uhci_grlib_hc_driver, &op->dev,
			"GRUSBHC UHCI USB");
	if (!hcd)
		return -ENOMEM;

	hcd->rsrc_start = res.start;
	hcd->rsrc_len = resource_size(&res);

	irq = irq_of_parse_and_map(dn, 0);
	if (!irq) {
		printk(KERN_ERR "%s: irq_of_parse_and_map failed\n", __FILE__);
		rv = -EBUSY;
		goto err_usb;
	}

	hcd->regs = devm_ioremap_resource(&op->dev, &res);
	if (IS_ERR(hcd->regs)) {
		rv = PTR_ERR(hcd->regs);
		goto err_irq;
	}

	uhci = hcd_to_uhci(hcd);

	uhci->regs = hcd->regs;

	rv = usb_add_hcd(hcd, irq, 0);
	if (rv)
		goto err_irq;

	device_wakeup_enable(hcd->self.controller);
	return 0;

err_irq:
	irq_dispose_mapping(irq);
err_usb:
	usb_put_hcd(hcd);

	return rv;
}

static void uhci_hcd_grlib_remove(struct platform_device *op)
{
	struct usb_hcd *hcd = platform_get_drvdata(op);

	dev_dbg(&op->dev, "stopping GRLIB GRUSBHC UHCI USB Controller\n");

	usb_remove_hcd(hcd);

	irq_dispose_mapping(hcd->irq);
	usb_put_hcd(hcd);
}

/* Make sure the controller is quiescent and that we're not using it
 * any more.  This is mainly for the benefit of programs which, like kexec,
 * expect the hardware to be idle: not doing DMA or generating IRQs.
 *
 * This routine may be called in a damaged or failing kernel.  Hence we
 * do not acquire the spinlock before shutting down the controller.
 */
static void uhci_hcd_grlib_shutdown(struct platform_device *op)
{
	struct usb_hcd *hcd = platform_get_drvdata(op);

	uhci_hc_died(hcd_to_uhci(hcd));
}

static const struct of_device_id uhci_hcd_grlib_of_match[] = {
	{ .name = "GAISLER_UHCI", },
	{ .name = "01_027", },
	{},
};
MODULE_DEVICE_TABLE(of, uhci_hcd_grlib_of_match);


static struct platform_driver uhci_grlib_driver = {
	.probe		= uhci_hcd_grlib_probe,
	.remove		= uhci_hcd_grlib_remove,
	.shutdown	= uhci_hcd_grlib_shutdown,
	.driver = {
		.name = "grlib-uhci",
		.of_match_table = uhci_hcd_grlib_of_match,
	},
};
