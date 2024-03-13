// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence USBSS PCI Glue driver
 *
 * Copyright (C) 2018-2019 Cadence.
 *
 * Author: Pawel Laszczak <pawell@cadence.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

struct cdns3_wrap {
	struct platform_device *plat_dev;
	struct resource dev_res[6];
	int devfn;
};

#define RES_IRQ_HOST_ID		0
#define RES_IRQ_PERIPHERAL_ID	1
#define RES_IRQ_OTG_ID		2
#define RES_HOST_ID		3
#define RES_DEV_ID		4
#define RES_DRD_ID		5

#define PCI_BAR_HOST		0
#define PCI_BAR_DEV		2
#define PCI_BAR_OTG		0

#define PCI_DEV_FN_HOST_DEVICE	0
#define PCI_DEV_FN_OTG		1

#define PCI_DRIVER_NAME		"cdns3-pci-usbss"
#define PLAT_DRIVER_NAME	"cdns-usb3"

#define CDNS_VENDOR_ID		0x17cd
#define CDNS_DEVICE_ID		0x0100

static struct pci_dev *cdns3_get_second_fun(struct pci_dev *pdev)
{
	struct pci_dev *func;

	/*
	 * Gets the second function.
	 * It's little tricky, but this platform has two function.
	 * The fist keeps resources for Host/Device while the second
	 * keeps resources for DRD/OTG.
	 */
	func = pci_get_device(pdev->vendor, pdev->device, NULL);
	if (unlikely(!func))
		return NULL;

	if (func->devfn == pdev->devfn) {
		func = pci_get_device(pdev->vendor, pdev->device, func);
		if (unlikely(!func))
			return NULL;
	}

	if (func->devfn != PCI_DEV_FN_HOST_DEVICE &&
	    func->devfn != PCI_DEV_FN_OTG) {
		return NULL;
	}

	return func;
}

static int cdns3_pci_probe(struct pci_dev *pdev,
			   const struct pci_device_id *id)
{
	struct platform_device_info plat_info;
	struct cdns3_wrap *wrap;
	struct resource *res;
	struct pci_dev *func;
	int err;

	/*
	 * for GADGET/HOST PCI (devfn) function number is 0,
	 * for OTG PCI (devfn) function number is 1
	 */
	if (!id || (pdev->devfn != PCI_DEV_FN_HOST_DEVICE &&
		    pdev->devfn != PCI_DEV_FN_OTG))
		return -EINVAL;

	func = cdns3_get_second_fun(pdev);
	if (unlikely(!func))
		return -EINVAL;

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Enabling PCI device has failed %d\n", err);
		return err;
	}

	pci_set_master(pdev);

	if (pci_is_enabled(func)) {
		wrap = pci_get_drvdata(func);
	} else {
		wrap = kzalloc(sizeof(*wrap), GFP_KERNEL);
		if (!wrap) {
			pci_disable_device(pdev);
			return -ENOMEM;
		}
	}

	res = wrap->dev_res;

	if (pdev->devfn == PCI_DEV_FN_HOST_DEVICE) {
		/* function 0: host(BAR_0) + device(BAR_1).*/
		dev_dbg(&pdev->dev, "Initialize Device resources\n");
		res[RES_DEV_ID].start = pci_resource_start(pdev, PCI_BAR_DEV);
		res[RES_DEV_ID].end =   pci_resource_end(pdev, PCI_BAR_DEV);
		res[RES_DEV_ID].name = "dev";
		res[RES_DEV_ID].flags = IORESOURCE_MEM;
		dev_dbg(&pdev->dev, "USBSS-DEV physical base addr: %pa\n",
			&res[RES_DEV_ID].start);

		res[RES_HOST_ID].start = pci_resource_start(pdev, PCI_BAR_HOST);
		res[RES_HOST_ID].end = pci_resource_end(pdev, PCI_BAR_HOST);
		res[RES_HOST_ID].name = "xhci";
		res[RES_HOST_ID].flags = IORESOURCE_MEM;
		dev_dbg(&pdev->dev, "USBSS-XHCI physical base addr: %pa\n",
			&res[RES_HOST_ID].start);

		/* Interrupt for XHCI */
		wrap->dev_res[RES_IRQ_HOST_ID].start = pdev->irq;
		wrap->dev_res[RES_IRQ_HOST_ID].name = "host";
		wrap->dev_res[RES_IRQ_HOST_ID].flags = IORESOURCE_IRQ;

		/* Interrupt device. It's the same as for HOST. */
		wrap->dev_res[RES_IRQ_PERIPHERAL_ID].start = pdev->irq;
		wrap->dev_res[RES_IRQ_PERIPHERAL_ID].name = "peripheral";
		wrap->dev_res[RES_IRQ_PERIPHERAL_ID].flags = IORESOURCE_IRQ;
	} else {
		res[RES_DRD_ID].start = pci_resource_start(pdev, PCI_BAR_OTG);
		res[RES_DRD_ID].end =   pci_resource_end(pdev, PCI_BAR_OTG);
		res[RES_DRD_ID].name = "otg";
		res[RES_DRD_ID].flags = IORESOURCE_MEM;
		dev_dbg(&pdev->dev, "USBSS-DRD physical base addr: %pa\n",
			&res[RES_DRD_ID].start);

		/* Interrupt for OTG/DRD. */
		wrap->dev_res[RES_IRQ_OTG_ID].start = pdev->irq;
		wrap->dev_res[RES_IRQ_OTG_ID].name = "otg";
		wrap->dev_res[RES_IRQ_OTG_ID].flags = IORESOURCE_IRQ;
	}

	if (pci_is_enabled(func)) {
		/* set up platform device info */
		memset(&plat_info, 0, sizeof(plat_info));
		plat_info.parent = &pdev->dev;
		plat_info.fwnode = pdev->dev.fwnode;
		plat_info.name = PLAT_DRIVER_NAME;
		plat_info.id = pdev->devfn;
		wrap->devfn  = pdev->devfn;
		plat_info.res = wrap->dev_res;
		plat_info.num_res = ARRAY_SIZE(wrap->dev_res);
		plat_info.dma_mask = pdev->dma_mask;
		/* register platform device */
		wrap->plat_dev = platform_device_register_full(&plat_info);
		if (IS_ERR(wrap->plat_dev)) {
			pci_disable_device(pdev);
			err = PTR_ERR(wrap->plat_dev);
			kfree(wrap);
			return err;
		}
	}

	pci_set_drvdata(pdev, wrap);
	return err;
}

static void cdns3_pci_remove(struct pci_dev *pdev)
{
	struct cdns3_wrap *wrap;
	struct pci_dev *func;

	func = cdns3_get_second_fun(pdev);

	wrap = (struct cdns3_wrap *)pci_get_drvdata(pdev);
	if (wrap->devfn == pdev->devfn)
		platform_device_unregister(wrap->plat_dev);

	if (!pci_is_enabled(func))
		kfree(wrap);
}

static const struct pci_device_id cdns3_pci_ids[] = {
	{ PCI_DEVICE(CDNS_VENDOR_ID, CDNS_DEVICE_ID), },
	{ 0, }
};

static struct pci_driver cdns3_pci_driver = {
	.name = PCI_DRIVER_NAME,
	.id_table = cdns3_pci_ids,
	.probe = cdns3_pci_probe,
	.remove = cdns3_pci_remove,
};

module_pci_driver(cdns3_pci_driver);
MODULE_DEVICE_TABLE(pci, cdns3_pci_ids);

MODULE_AUTHOR("Pawel Laszczak <pawell@cadence.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Cadence USBSS PCI wrapper");
