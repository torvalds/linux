// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence PCI Glue driver.
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
#include "gadget-export.h"

#define PCI_BAR_HOST		0
#define PCI_BAR_OTG		0
#define PCI_BAR_DEV		2

#define PCI_DEV_FN_HOST_DEVICE	0
#define PCI_DEV_FN_OTG		1

#define PCI_DRIVER_NAME		"cdns-pci-usbssp"
#define PLAT_DRIVER_NAME	"cdns-usbssp"

#define CDNS_VENDOR_ID		0x17cd
#define CDNS_DEVICE_ID		0x0200
#define CDNS_DRD_ID		0x0100
#define CDNS_DRD_IF		(PCI_CLASS_SERIAL_USB << 8 | 0x80)

static struct pci_dev *cdnsp_get_second_fun(struct pci_dev *pdev)
{
	/*
	 * Gets the second function.
	 * Platform has two function. The fist keeps resources for
	 * Host/Device while the secon keeps resources for DRD/OTG.
	 */
	if (pdev->device == CDNS_DEVICE_ID)
		return  pci_get_device(pdev->vendor, CDNS_DRD_ID, NULL);
	else if (pdev->device == CDNS_DRD_ID)
		return pci_get_device(pdev->vendor, CDNS_DEVICE_ID, NULL);

	return NULL;
}

static int cdnsp_pci_probe(struct pci_dev *pdev,
			   const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct pci_dev *func;
	struct resource *res;
	struct cdns *cdnsp;
	int ret;

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
		cdnsp = pci_get_drvdata(func);
	} else {
		cdnsp = kzalloc(sizeof(*cdnsp), GFP_KERNEL);
		if (!cdnsp) {
			ret = -ENOMEM;
			goto disable_pci;
		}
	}

	/* For GADGET device function number is 0. */
	if (pdev->devfn == 0) {
		resource_size_t rsrc_start, rsrc_len;

		/* Function 0: host(BAR_0) + device(BAR_1).*/
		dev_dbg(dev, "Initialize resources\n");
		rsrc_start = pci_resource_start(pdev, PCI_BAR_DEV);
		rsrc_len = pci_resource_len(pdev, PCI_BAR_DEV);
		res = devm_request_mem_region(dev, rsrc_start, rsrc_len, "dev");
		if (!res) {
			dev_dbg(dev, "controller already in use\n");
			ret = -EBUSY;
			goto free_cdnsp;
		}

		cdnsp->dev_regs = devm_ioremap(dev, rsrc_start, rsrc_len);
		if (!cdnsp->dev_regs) {
			dev_dbg(dev, "error mapping memory\n");
			ret = -EFAULT;
			goto free_cdnsp;
		}

		cdnsp->dev_irq = pdev->irq;
		dev_dbg(dev, "USBSS-DEV physical base addr: %pa\n",
			&rsrc_start);

		res = &cdnsp->xhci_res[0];
		res->start = pci_resource_start(pdev, PCI_BAR_HOST);
		res->end = pci_resource_end(pdev, PCI_BAR_HOST);
		res->name = "xhci";
		res->flags = IORESOURCE_MEM;
		dev_dbg(dev, "USBSS-XHCI physical base addr: %pa\n",
			&res->start);

		/* Interrupt for XHCI, */
		res = &cdnsp->xhci_res[1];
		res->start = pdev->irq;
		res->name = "host";
		res->flags = IORESOURCE_IRQ;
	} else {
		res = &cdnsp->otg_res;
		res->start = pci_resource_start(pdev, PCI_BAR_OTG);
		res->end =   pci_resource_end(pdev, PCI_BAR_OTG);
		res->name = "otg";
		res->flags = IORESOURCE_MEM;
		dev_dbg(dev, "CDNSP-DRD physical base addr: %pa\n",
			&res->start);

		/* Interrupt for OTG/DRD. */
		cdnsp->otg_irq = pdev->irq;
	}

	if (pci_is_enabled(func)) {
		cdnsp->dev = dev;
		cdnsp->gadget_init = cdnsp_gadget_init;

		ret = cdns_init(cdnsp);
		if (ret)
			goto free_cdnsp;
	}

	pci_set_drvdata(pdev, cdnsp);

	device_wakeup_enable(&pdev->dev);
	if (pci_dev_run_wake(pdev))
		pm_runtime_put_noidle(&pdev->dev);

	return 0;

free_cdnsp:
	if (!pci_is_enabled(func))
		kfree(cdnsp);

disable_pci:
	pci_disable_device(pdev);

put_pci:
	pci_dev_put(func);

	return ret;
}

static void cdnsp_pci_remove(struct pci_dev *pdev)
{
	struct cdns *cdnsp;
	struct pci_dev *func;

	func = cdnsp_get_second_fun(pdev);
	cdnsp = (struct cdns *)pci_get_drvdata(pdev);

	if (pci_dev_run_wake(pdev))
		pm_runtime_get_noresume(&pdev->dev);

	if (pci_is_enabled(func)) {
		cdns_remove(cdnsp);
	} else {
		kfree(cdnsp);
	}

	pci_dev_put(func);
}

static int __maybe_unused cdnsp_pci_suspend(struct device *dev)
{
	struct cdns *cdns = dev_get_drvdata(dev);

	return cdns_suspend(cdns);
}

static int __maybe_unused cdnsp_pci_resume(struct device *dev)
{
	struct cdns *cdns = dev_get_drvdata(dev);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&cdns->lock, flags);
	ret = cdns_resume(cdns, 1);
	spin_unlock_irqrestore(&cdns->lock, flags);

	return ret;
}

static const struct dev_pm_ops cdnsp_pci_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cdnsp_pci_suspend, cdnsp_pci_resume)
};

static const struct pci_device_id cdnsp_pci_ids[] = {
	{ PCI_VENDOR_ID_CDNS, CDNS_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,
	  PCI_CLASS_SERIAL_USB_DEVICE, PCI_ANY_ID },
	{ PCI_VENDOR_ID_CDNS, CDNS_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID,
	  CDNS_DRD_IF, PCI_ANY_ID },
	{ PCI_VENDOR_ID_CDNS, CDNS_DRD_ID, PCI_ANY_ID, PCI_ANY_ID,
	  CDNS_DRD_IF, PCI_ANY_ID },
	{ 0, }
};

static struct pci_driver cdnsp_pci_driver = {
	.name = "cdnsp-pci",
	.id_table = &cdnsp_pci_ids[0],
	.probe = cdnsp_pci_probe,
	.remove = cdnsp_pci_remove,
	.driver = {
		.pm = &cdnsp_pci_pm_ops,
	}
};

module_pci_driver(cdnsp_pci_driver);
MODULE_DEVICE_TABLE(pci, cdnsp_pci_ids);

MODULE_ALIAS("pci:cdnsp");
MODULE_AUTHOR("Pawel Laszczak <pawell@cadence.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Cadence CDNSP PCI driver");
