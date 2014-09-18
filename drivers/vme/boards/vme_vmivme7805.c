/*
 * Support for the VMIVME-7805 board access to the Universe II bridge.
 *
 * Author: Arthur Benilov <arthur.benilov@iba-group.com>
 * Copyright 2010 Ion Beam Application, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/io.h>

#include "vme_vmivme7805.h"

static int vmic_probe(struct pci_dev *, const struct pci_device_id *);
static void vmic_remove(struct pci_dev *);

/** Base address to access FPGA register */
static void __iomem *vmic_base;

static const char driver_name[] = "vmivme_7805";

static const struct pci_device_id vmic_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_VMIC, PCI_DEVICE_ID_VTIMR) },
	{ },
};

static struct pci_driver vmic_driver = {
	.name = driver_name,
	.id_table = vmic_ids,
	.probe = vmic_probe,
	.remove = vmic_remove,
};

static int vmic_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int retval;
	u32 data;

	/* Enable the device */
	retval = pci_enable_device(pdev);
	if (retval) {
		dev_err(&pdev->dev, "Unable to enable device\n");
		goto err;
	}

	/* Map Registers */
	retval = pci_request_regions(pdev, driver_name);
	if (retval) {
		dev_err(&pdev->dev, "Unable to reserve resources\n");
		goto err_resource;
	}

	/* Map registers in BAR 0 */
	vmic_base = ioremap_nocache(pci_resource_start(pdev, 0), 16);
	if (!vmic_base) {
		dev_err(&pdev->dev, "Unable to remap CRG region\n");
		retval = -EIO;
		goto err_remap;
	}

	/* Clear the FPGA VME IF contents */
	iowrite32(0, vmic_base + VME_CONTROL);

	/* Clear any initial BERR  */
	data = ioread32(vmic_base + VME_CONTROL) & 0x00000FFF;
	data |= BM_VME_CONTROL_BERRST;
	iowrite32(data, vmic_base + VME_CONTROL);

	/* Enable the vme interface and byte swapping */
	data = ioread32(vmic_base + VME_CONTROL) & 0x00000FFF;
	data = data | BM_VME_CONTROL_MASTER_ENDIAN |
			BM_VME_CONTROL_SLAVE_ENDIAN |
			BM_VME_CONTROL_ABLE |
			BM_VME_CONTROL_BERRI |
			BM_VME_CONTROL_BPENA |
			BM_VME_CONTROL_VBENA;
	iowrite32(data, vmic_base + VME_CONTROL);

	return 0;

err_remap:
	pci_release_regions(pdev);
err_resource:
	pci_disable_device(pdev);
err:
	return retval;
}

static void vmic_remove(struct pci_dev *pdev)
{
	iounmap(vmic_base);
	pci_release_regions(pdev);
	pci_disable_device(pdev);

}

module_pci_driver(vmic_driver);

MODULE_DESCRIPTION("VMIVME-7805 board support driver");
MODULE_AUTHOR("Arthur Benilov <arthur.benilov@iba-group.com>");
MODULE_LICENSE("GPL");

