// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Promontory 21 xHCI host controller PCI Bus Glue.
 *
 * This does not add any PROM21-specific USB or xHCI operation. It exists only
 * to publish an auxiliary device for integrated temperature sensor support.
 *
 * Copyright (C) 2026 Jihong Min <hurryman2212@gmail.com>
 */

#include <linux/auxiliary_bus.h>
#include <linux/device/devres.h>
#include <linux/errno.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_data/usb-xhci-prom21.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "xhci-pci.h"

struct prom21_xhci_auxdev {
	struct auxiliary_device *auxdev;
	struct prom21_xhci_pdata pdata;
	int id;
};

static DEFINE_IDA(prom21_xhci_auxdev_ida);

static void prom21_xhci_auxdev_release(struct device *dev, void *res)
{
	struct prom21_xhci_auxdev *prom21_auxdev = res;

	auxiliary_device_destroy(prom21_auxdev->auxdev);
	ida_free(&prom21_xhci_auxdev_ida, prom21_auxdev->id);
}

static int prom21_xhci_create_auxdev(struct pci_dev *pdev)
{
	struct prom21_xhci_auxdev *prom21_auxdev;
	struct usb_hcd *hcd = pci_get_drvdata(pdev);
	int ret;

	prom21_auxdev = devres_alloc(prom21_xhci_auxdev_release,
				     sizeof(*prom21_auxdev), GFP_KERNEL);
	if (!prom21_auxdev)
		return -ENOMEM;

	prom21_auxdev->pdata.pdev = pdev;
	prom21_auxdev->pdata.regs = hcd->regs;
	prom21_auxdev->pdata.rsrc_len = hcd->rsrc_len;

	prom21_auxdev->id = ida_alloc(&prom21_xhci_auxdev_ida, GFP_KERNEL);
	if (prom21_auxdev->id < 0) {
		ret = prom21_auxdev->id;
		goto err_free_devres;
	}

	prom21_auxdev->auxdev = auxiliary_device_create(&pdev->dev,
							KBUILD_MODNAME, "hwmon",
							&prom21_auxdev->pdata,
							prom21_auxdev->id);
	if (!prom21_auxdev->auxdev) {
		ret = -ENOMEM;
		goto err_free_ida;
	}

	devres_add(&pdev->dev, prom21_auxdev);
	return 0;

err_free_ida:
	ida_free(&prom21_xhci_auxdev_ida, prom21_auxdev->id);
err_free_devres:
	devres_free(prom21_auxdev);
	return ret;
}

static void prom21_xhci_destroy_auxdev(struct pci_dev *pdev)
{
	devres_release(&pdev->dev, prom21_xhci_auxdev_release, NULL, NULL);
}

static int prom21_xhci_probe(struct pci_dev *dev,
			     const struct pci_device_id *id)
{
	int retval;

	retval = xhci_pci_common_probe(dev, id);
	if (retval)
		return retval;

	retval = prom21_xhci_create_auxdev(dev);
	if (retval) {
		/*
		 * The auxiliary device only provides optional temperature sensor
		 * support. Keep the xHCI controller usable if it fails.
		 */
		dev_err(&dev->dev,
			"failed to create PROM21 hwmon auxiliary device: %d\n",
			retval);
	}

	return 0;
}

static void prom21_xhci_remove(struct pci_dev *dev)
{
	prom21_xhci_destroy_auxdev(dev);
	xhci_pci_remove(dev);
}

static const struct pci_device_id pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_PROM21_XHCI_43FC) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_PROM21_XHCI_43FD) },
	{ /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE(pci, pci_ids);

static struct pci_driver prom21_xhci_driver = {
	.name = "xhci-pci-prom21",
	.id_table = pci_ids,

	.probe = prom21_xhci_probe,
	.remove = prom21_xhci_remove,

	.shutdown = usb_hcd_pci_shutdown,
	.driver = {
		.pm = pm_ptr(&usb_hcd_pci_pm_ops),
	},
};
module_pci_driver(prom21_xhci_driver);

MODULE_AUTHOR("Jihong Min <hurryman2212@gmail.com>");
MODULE_DESCRIPTION("AMD Promontory 21 xHCI PCI Host Controller Driver");
MODULE_IMPORT_NS("xhci");
MODULE_LICENSE("GPL");
