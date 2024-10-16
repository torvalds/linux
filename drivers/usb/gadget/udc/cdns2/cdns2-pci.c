// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence USBHS-DEV controller - PCI Glue driver.
 *
 * Copyright (C) 2023 Cadence.
 *
 * Author: Pawel Laszczak <pawell@cadence.com>
 *
 */

#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/pci.h>

#include "cdns2-gadget.h"

#define PCI_DRIVER_NAME		"cdns-pci-usbhs"
#define PCI_DEVICE_ID_CDNS_USB2	0x0120
#define PCI_BAR_DEV		0
#define PCI_DEV_FN_DEVICE	0

static int cdns2_pci_probe(struct pci_dev *pdev,
			   const struct pci_device_id *id)
{
	resource_size_t rsrc_start, rsrc_len;
	struct device *dev = &pdev->dev;
	struct cdns2_device *priv_dev;
	struct resource *res;
	int ret;

	/* For GADGET PCI (devfn) function number is 0. */
	if (!id || pdev->devfn != PCI_DEV_FN_DEVICE ||
	    pdev->class != PCI_CLASS_SERIAL_USB_DEVICE)
		return -EINVAL;

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Enabling PCI device has failed %d\n", ret);
		return ret;
	}

	pci_set_master(pdev);

	priv_dev = devm_kzalloc(&pdev->dev, sizeof(*priv_dev), GFP_KERNEL);
	if (!priv_dev)
		return -ENOMEM;

	dev_dbg(dev, "Initialize resources\n");
	rsrc_start = pci_resource_start(pdev, PCI_BAR_DEV);
	rsrc_len = pci_resource_len(pdev, PCI_BAR_DEV);

	res = devm_request_mem_region(dev, rsrc_start, rsrc_len, "dev");
	if (!res) {
		dev_dbg(dev, "controller already in use\n");
		return -EBUSY;
	}

	priv_dev->regs = devm_ioremap(dev, rsrc_start, rsrc_len);
	if (!priv_dev->regs) {
		dev_dbg(dev, "error mapping memory\n");
		return -EFAULT;
	}

	priv_dev->irq = pdev->irq;
	dev_dbg(dev, "USBSS-DEV physical base addr: %pa\n",
		&rsrc_start);

	priv_dev->dev = dev;

	priv_dev->eps_supported = 0x000f000f;
	priv_dev->onchip_tx_buf = 16;
	priv_dev->onchip_rx_buf = 16;

	ret = cdns2_gadget_init(priv_dev);
	if (ret)
		return ret;

	pci_set_drvdata(pdev, priv_dev);

	device_wakeup_enable(&pdev->dev);
	if (pci_dev_run_wake(pdev))
		pm_runtime_put_noidle(&pdev->dev);

	return 0;
}

static void cdns2_pci_remove(struct pci_dev *pdev)
{
	struct cdns2_device *priv_dev = pci_get_drvdata(pdev);

	if (pci_dev_run_wake(pdev))
		pm_runtime_get_noresume(&pdev->dev);

	cdns2_gadget_remove(priv_dev);
}

static int cdns2_pci_suspend(struct device *dev)
{
	struct cdns2_device *priv_dev = dev_get_drvdata(dev);

	return cdns2_gadget_suspend(priv_dev);
}

static int cdns2_pci_resume(struct device *dev)
{
	struct cdns2_device *priv_dev = dev_get_drvdata(dev);

	return cdns2_gadget_resume(priv_dev, 1);
}

static const struct dev_pm_ops cdns2_pci_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(cdns2_pci_suspend, cdns2_pci_resume)
};

static const struct pci_device_id cdns2_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CDNS, PCI_DEVICE_ID_CDNS_USB2),
	  .class = PCI_CLASS_SERIAL_USB_DEVICE },
	{ 0, }
};

static struct pci_driver cdns2_pci_driver = {
	.name = "cdns2-pci",
	.id_table = cdns2_pci_ids,
	.probe = cdns2_pci_probe,
	.remove = cdns2_pci_remove,
	.driver = {
		.pm = pm_ptr(&cdns2_pci_pm_ops),
	}
};

module_pci_driver(cdns2_pci_driver);
MODULE_DEVICE_TABLE(pci, cdns2_pci_ids);

MODULE_ALIAS("pci:cdns2");
MODULE_AUTHOR("Pawel Laszczak <pawell@cadence.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cadence CDNS2 PCI driver");
