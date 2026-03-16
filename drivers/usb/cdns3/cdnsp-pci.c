// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence USBSSP PCI Glue driver.
 *
 * Copyright (C) 2019 Cadence.
 *
 * Author: Pawel Laszczak <pawell@cadence.com>
 *
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci.h>

#include "core.h"

struct cdnsp_wrap {
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
#define PCI_BAR_OTG		0
#define PCI_BAR_DEV		2

#define PCI_DEV_FN_HOST_DEVICE	0
#define PCI_DEV_FN_OTG		1

#define PCI_DRIVER_NAME		"cdns-pci-usbssp"
#define PLAT_DRIVER_NAME	"cdns-usb3"

#define CHICKEN_APB_TIMEOUT_VALUE	0x1C20

static struct pci_dev *cdnsp_get_second_fun(struct pci_dev *pdev)
{
	/*
	 * Gets the second function.
	 * Platform has two function. The first keeps resources for
	 * Host/Device while the second keeps resources for DRD/OTG.
	 */
	if (pdev->device == PCI_DEVICE_ID_CDNS_USBSSP)
		return pci_get_device(pdev->vendor, PCI_DEVICE_ID_CDNS_USBSS, NULL);
	if (pdev->device == PCI_DEVICE_ID_CDNS_USBSS)
		return pci_get_device(pdev->vendor, PCI_DEVICE_ID_CDNS_USBSSP, NULL);

	return NULL;
}

static int cdnsp_pci_probe(struct pci_dev *pdev,
			   const struct pci_device_id *id)
{
	struct platform_device_info plat_info;
	static struct cdns3_platform_data pdata;
	struct cdnsp_wrap *wrap;
	struct resource *res;
	struct pci_dev *func;
	int ret = 0;

	/*
	 * For GADGET/HOST PCI (devfn) function number is 0,
	 * for OTG PCI (devfn) function number is 1.
	 */
	if (!id || (pdev->devfn != PCI_DEV_FN_HOST_DEVICE &&
		    pdev->devfn != PCI_DEV_FN_OTG))
		return -EINVAL;

	func = cdnsp_get_second_fun(pdev);
	if (!func)
		return -EINVAL;

	if (func->class == PCI_CLASS_SERIAL_USB_XHCI ||
	    pdev->class == PCI_CLASS_SERIAL_USB_XHCI) {
		ret = -EINVAL;
		goto put_pci;
	}

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Enabling PCI device has failed %d\n", ret);
		goto put_pci;
	}

	pci_set_master(pdev);

	if (pci_is_enabled(func)) {
		wrap = pci_get_drvdata(func);
	} else {
		wrap = kzalloc_obj(*wrap);
		if (!wrap) {
			ret = -ENOMEM;
			goto put_pci;
		}
	}

	res = wrap->dev_res;

	if (pdev->devfn == PCI_DEV_FN_HOST_DEVICE) {
		/* Function 0: host(BAR_0) + device(BAR_2). */
		dev_dbg(&pdev->dev, "Initialize Device resources\n");
		res[RES_DEV_ID].start = pci_resource_start(pdev, PCI_BAR_DEV);
		res[RES_DEV_ID].end = pci_resource_end(pdev, PCI_BAR_DEV);
		res[RES_DEV_ID].name = "dev";
		res[RES_DEV_ID].flags = IORESOURCE_MEM;
		dev_dbg(&pdev->dev, "USBSSP-DEV physical base addr: %pa\n",
			&res[RES_DEV_ID].start);

		res[RES_HOST_ID].start = pci_resource_start(pdev, PCI_BAR_HOST);
		res[RES_HOST_ID].end = pci_resource_end(pdev, PCI_BAR_HOST);
		res[RES_HOST_ID].name = "xhci";
		res[RES_HOST_ID].flags = IORESOURCE_MEM;
		dev_dbg(&pdev->dev, "USBSSP-XHCI physical base addr: %pa\n",
			&res[RES_HOST_ID].start);

		/* Interrupt for XHCI */
		wrap->dev_res[RES_IRQ_HOST_ID].start = pdev->irq;
		wrap->dev_res[RES_IRQ_HOST_ID].name = "host";
		wrap->dev_res[RES_IRQ_HOST_ID].flags = IORESOURCE_IRQ;

		/* Interrupt for device. It's the same as for HOST. */
		wrap->dev_res[RES_IRQ_PERIPHERAL_ID].start = pdev->irq;
		wrap->dev_res[RES_IRQ_PERIPHERAL_ID].name = "peripheral";
		wrap->dev_res[RES_IRQ_PERIPHERAL_ID].flags = IORESOURCE_IRQ;
	} else {
		res[RES_DRD_ID].start = pci_resource_start(pdev, PCI_BAR_OTG);
		res[RES_DRD_ID].end = pci_resource_end(pdev, PCI_BAR_OTG);
		res[RES_DRD_ID].name = "otg";
		res[RES_DRD_ID].flags = IORESOURCE_MEM;
		dev_dbg(&pdev->dev, "CDNSP-DRD physical base addr: %pa\n",
			&res[RES_DRD_ID].start);

		/* Interrupt for OTG/DRD. */
		wrap->dev_res[RES_IRQ_OTG_ID].start = pdev->irq;
		wrap->dev_res[RES_IRQ_OTG_ID].name = "otg";
		wrap->dev_res[RES_IRQ_OTG_ID].flags = IORESOURCE_IRQ;
	}

	if (pci_is_enabled(func)) {
		/* set up platform device info */
		pdata.override_apb_timeout = CHICKEN_APB_TIMEOUT_VALUE;
		memset(&plat_info, 0, sizeof(plat_info));
		plat_info.parent = &pdev->dev;
		plat_info.fwnode = pdev->dev.fwnode;
		plat_info.name = PLAT_DRIVER_NAME;
		plat_info.id = pdev->devfn;
		plat_info.res = wrap->dev_res;
		plat_info.num_res = ARRAY_SIZE(wrap->dev_res);
		plat_info.dma_mask = pdev->dma_mask;
		plat_info.data = &pdata;
		plat_info.size_data = sizeof(pdata);
		wrap->devfn = pdev->devfn;
		/* register platform device */
		wrap->plat_dev = platform_device_register_full(&plat_info);
		if (IS_ERR(wrap->plat_dev)) {
			ret = PTR_ERR(wrap->plat_dev);
			kfree(wrap);
			goto put_pci;
		}
	}

	pci_set_drvdata(pdev, wrap);
put_pci:
	pci_dev_put(func);
	return ret;
}

static void cdnsp_pci_remove(struct pci_dev *pdev)
{
	struct cdnsp_wrap *wrap;
	struct pci_dev *func;

	func = cdnsp_get_second_fun(pdev);
	wrap = pci_get_drvdata(pdev);

	if (wrap->devfn == pdev->devfn)
		platform_device_unregister(wrap->plat_dev);

	if (!pci_is_enabled(func))
		kfree(wrap);

	pci_dev_put(func);
}

static const struct pci_device_id cdnsp_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CDNS, PCI_DEVICE_ID_CDNS_USBSSP),
	  .class = PCI_CLASS_SERIAL_USB_DEVICE },
	{ PCI_DEVICE(PCI_VENDOR_ID_CDNS, PCI_DEVICE_ID_CDNS_USBSSP),
	  .class = PCI_CLASS_SERIAL_USB_CDNS },
	{ PCI_DEVICE(PCI_VENDOR_ID_CDNS, PCI_DEVICE_ID_CDNS_USBSS),
	  .class = PCI_CLASS_SERIAL_USB_CDNS },
	{ 0, }
};

static struct pci_driver cdnsp_pci_driver = {
	.name = PCI_DRIVER_NAME,
	.id_table = cdnsp_pci_ids,
	.probe = cdnsp_pci_probe,
	.remove = cdnsp_pci_remove,
};

module_pci_driver(cdnsp_pci_driver);
MODULE_DEVICE_TABLE(pci, cdnsp_pci_ids);

MODULE_ALIAS("pci:cdnsp");
MODULE_AUTHOR("Pawel Laszczak <pawell@cadence.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Cadence CDNSP PCI wrapper");
