/*
 * xhci-aeoliat.c - xHCI host controller driver for Aeolia (Sony PS4)
 *
 * Borrows code from xhci-pci.c, hcd-pci.c, and xhci-plat.c.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <asm/ps4.h>

#include "xhci.h"

#define PCI_DEVICE_ID_AXHCI		0x90a4

static const char hcd_name[] = "xhci_aeolia";

static struct hc_driver __read_mostly xhci_aeolia_hc_driver;

#define NR_DEVICES 3

struct aeolia_xhci {
	int nr_irqs;
	struct usb_hcd *hcd[NR_DEVICES];
};

static int xhci_aeolia_setup(struct usb_hcd *hcd);

static const struct xhci_driver_overrides xhci_aeolia_overrides __initconst = {
	.extra_priv_size = sizeof(struct xhci_hcd),
	.reset = xhci_aeolia_setup,
};

static void xhci_aeolia_quirks(struct device *dev, struct xhci_hcd *xhci)
{
	/*
	 * Do not try to enable MSIs, we provide the MSIs ourselves
	 */
	xhci->quirks |= XHCI_PLAT;
}

/* called during probe() after chip reset completes */
static int xhci_aeolia_setup(struct usb_hcd *hcd)
{
	return xhci_gen_setup(hcd, xhci_aeolia_quirks);
}

static int xhci_aeolia_probe_one(struct pci_dev *dev, int index)
{
	int retval;
	struct aeolia_xhci *axhci = pci_get_drvdata(dev);
	struct hc_driver *driver = &xhci_aeolia_hc_driver;
	struct usb_hcd *hcd;
	struct xhci_hcd *xhci;
	int irq = (axhci->nr_irqs > 1) ? (dev->irq + index) : dev->irq;

	hcd = usb_create_hcd(driver, &dev->dev, pci_name(dev));
	pci_set_drvdata(dev, axhci); /* usb_create_hcd clobbers this */
	if (!hcd)
		return -ENOMEM;
	
	hcd->rsrc_start = pci_resource_start(dev, 2 * index);
	hcd->rsrc_len = pci_resource_len(dev, 2 * index);
	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len,
			driver->description)) {
		dev_dbg(&dev->dev, "controller already in use\n");
		retval = -EBUSY;
		goto put_hcd;
	}
	hcd->regs = pci_ioremap_bar(dev, 2 * index);
	if (hcd->regs == NULL) {
		dev_dbg(&dev->dev, "error mapping memory\n");
		retval = -EFAULT;
		goto release_mem_region;
	}
	
	device_wakeup_enable(hcd->self.controller);

	xhci = hcd_to_xhci(hcd);
	xhci->main_hcd = hcd;
	xhci->shared_hcd = usb_create_shared_hcd(driver, &dev->dev,
			pci_name(dev), hcd);
	if (!xhci->shared_hcd) {
		retval = -ENOMEM;
		goto unmap_registers;
	}

	retval = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (retval)
		goto put_usb3_hcd;

	retval = usb_add_hcd(xhci->shared_hcd, irq, IRQF_SHARED);
	if (retval)
		goto dealloc_usb2_hcd;

	axhci->hcd[index] = hcd;

	return 0;

dealloc_usb2_hcd:
	usb_remove_hcd(hcd);
put_usb3_hcd:
	usb_put_hcd(xhci->shared_hcd);
unmap_registers:
	iounmap(hcd->regs);
release_mem_region:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
put_hcd:
	usb_put_hcd(hcd);
	dev_err(&dev->dev, "init %s(%d) fail, %d\n",
			pci_name(dev), index, retval);
	return retval;
}

static void xhci_aeolia_remove_one(struct pci_dev *dev, int index)
{
	struct aeolia_xhci *axhci = pci_get_drvdata(dev);
	struct usb_hcd *hcd = axhci->hcd[index];
	struct xhci_hcd *xhci;

	if (!hcd)
		return;
	xhci = hcd_to_xhci(hcd);

	usb_remove_hcd(xhci->shared_hcd);
	usb_remove_hcd(hcd);
	usb_put_hcd(xhci->shared_hcd);
	usb_put_hcd(hcd);
	axhci->hcd[index] = NULL;
}


static int xhci_aeolia_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int idx;
	int retval;
	struct aeolia_xhci *axhci;

	if (apcie_status() == 0)
		return -EPROBE_DEFER;

	if (pci_enable_device(dev) < 0)
		return -ENODEV;

	axhci = kzalloc(sizeof(*axhci), GFP_KERNEL);
	if (!axhci) {
		retval = -ENOMEM;
		goto disable_device;
	}
	pci_set_drvdata(dev, axhci);

	axhci->nr_irqs = retval = apcie_assign_irqs(dev, NR_DEVICES);
	if (retval < 0) {
		goto free_axhci;
	}

	pci_set_master(dev);

	for (idx = 0; idx < NR_DEVICES; idx++) {
		retval = xhci_aeolia_probe_one(dev, idx);
		if (retval)
			goto remove_hcds;
	}
	
	return 0;

remove_hcds:
	while (idx--)
		xhci_aeolia_remove_one(dev, idx);
	apcie_free_irqs(dev->irq, axhci->nr_irqs);
free_axhci:
	kfree(axhci);
disable_device:
	pci_disable_device(dev);
	return retval;
}

static void xhci_aeolia_remove(struct pci_dev *dev)
{
	int idx;
	struct aeolia_xhci *axhci = pci_get_drvdata(dev);

	for (idx = 0; idx < NR_DEVICES; idx++)
		xhci_aeolia_remove_one(dev, idx);
	
	apcie_free_irqs(dev->irq, axhci->nr_irqs);
	kfree(axhci);
	pci_disable_device(dev);
}


static const struct pci_device_id pci_ids[] = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_SONY, PCI_DEVICE_ID_AXHCI),
	},
	{ /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE(pci, pci_ids);

#ifdef CONFIG_PM_SLEEP
static int xhci_aeolia_suspend(struct device *dev)
{
	int idx;
	struct aeolia_xhci *axhci = dev_get_drvdata(dev);
	struct xhci_hcd	*xhci;
	int retval;
	
	for (idx = 0; idx < NR_DEVICES; idx++) {
		xhci = hcd_to_xhci(axhci->hcd[idx]);
		retval = xhci_suspend(xhci, device_may_wakeup(dev));
		if (retval < 0)
			goto resume;
	}
	return 0;

resume:
	while (idx--) {
		xhci = hcd_to_xhci(axhci->hcd[idx]);
		xhci_resume(xhci, 0);
	}
	return retval;
}

static int xhci_aeolia_resume(struct device *dev)
{
	int idx;
	struct aeolia_xhci *axhci = dev_get_drvdata(dev);
	struct xhci_hcd	*xhci;
	int retval;

	for (idx = 0; idx < NR_DEVICES; idx++) {
		xhci = hcd_to_xhci(axhci->hcd[idx]);
		retval = xhci_resume(xhci, 0);
		if (retval < 0)
			return retval;
	}
}

static const struct dev_pm_ops xhci_aeolia_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xhci_aeolia_suspend, xhci_aeolia_resume)
};
#endif

/* pci driver glue; this is a "new style" PCI driver module */
static struct pci_driver xhci_aeolia_driver = {
	.name =		"xhci_aeolia",
	.id_table =	pci_ids,

	.probe =	xhci_aeolia_probe,
	.remove =	xhci_aeolia_remove,
	/* suspend and resume implemented later */

	.shutdown = 	usb_hcd_pci_shutdown,
#ifdef CONFIG_PM_SLEEP
	.driver = {
		.pm = &xhci_aeolia_pm_ops
	},
#endif
};

static int __init xhci_aeolia_init(void)
{
	xhci_init_driver(&xhci_aeolia_hc_driver, &xhci_aeolia_overrides);
	return pci_register_driver(&xhci_aeolia_driver);
}
module_init(xhci_aeolia_init);

static void __exit xhci_aeolia_exit(void)
{
	pci_unregister_driver(&xhci_aeolia_driver);
}
module_exit(xhci_aeolia_exit);

MODULE_DESCRIPTION("xHCI Aeolia Host Controller Driver");
MODULE_LICENSE("GPL");
