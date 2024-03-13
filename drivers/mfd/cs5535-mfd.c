// SPDX-License-Identifier: GPL-2.0-only
/*
 * cs5535-mfd.c - core MFD driver for CS5535/CS5536 southbridges
 *
 * The CS5535 and CS5536 has an ISA bridge on the PCI bus that is
 * used for accessing GPIOs, MFGPTs, ACPI, etc.  Each subdevice has
 * an IO range that's specified in a single BAR.  The BAR order is
 * hardcoded in the CS553x specifications.
 *
 * Copyright (c) 2010  Andres Salomon <dilinger@queued.net>
 */

#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <asm/olpc.h>

#define DRV_NAME "cs5535-mfd"

enum cs5535_mfd_bars {
	SMB_BAR = 0,
	GPIO_BAR = 1,
	MFGPT_BAR = 2,
	PMS_BAR = 4,
	ACPI_BAR = 5,
	NR_BARS,
};

static struct resource cs5535_mfd_resources[NR_BARS];

static struct mfd_cell cs5535_mfd_cells[] = {
	{
		.name = "cs5535-smb",
		.num_resources = 1,
		.resources = &cs5535_mfd_resources[SMB_BAR],
	},
	{
		.name = "cs5535-gpio",
		.num_resources = 1,
		.resources = &cs5535_mfd_resources[GPIO_BAR],
	},
	{
		.name = "cs5535-mfgpt",
		.num_resources = 1,
		.resources = &cs5535_mfd_resources[MFGPT_BAR],
	},
	{
		.name = "cs5535-pms",
		.num_resources = 1,
		.resources = &cs5535_mfd_resources[PMS_BAR],
	},
};

static struct mfd_cell cs5535_olpc_mfd_cells[] = {
	{
		.name = "olpc-xo1-pm-acpi",
		.num_resources = 1,
		.resources = &cs5535_mfd_resources[ACPI_BAR],
	},
	{
		.name = "olpc-xo1-sci-acpi",
		.num_resources = 1,
		.resources = &cs5535_mfd_resources[ACPI_BAR],
	},
};

static int cs5535_mfd_probe(struct pci_dev *pdev,
		const struct pci_device_id *id)
{
	int err, bar;

	err = pci_enable_device(pdev);
	if (err)
		return err;

	for (bar = 0; bar < NR_BARS; bar++) {
		struct resource *r = &cs5535_mfd_resources[bar];

		r->flags = IORESOURCE_IO;
		r->start = pci_resource_start(pdev, bar);
		r->end = pci_resource_end(pdev, bar);
	}

	err = pci_request_region(pdev, PMS_BAR, DRV_NAME);
	if (err) {
		dev_err(&pdev->dev, "Failed to request PMS_BAR's IO region\n");
		goto err_disable;
	}

	err = mfd_add_devices(&pdev->dev, PLATFORM_DEVID_NONE, cs5535_mfd_cells,
			      ARRAY_SIZE(cs5535_mfd_cells), NULL, 0, NULL);
	if (err) {
		dev_err(&pdev->dev,
			"Failed to add CS5535 sub-devices: %d\n", err);
		goto err_release_pms;
	}

	if (machine_is_olpc()) {
		err = pci_request_region(pdev, ACPI_BAR, DRV_NAME);
		if (err) {
			dev_err(&pdev->dev,
				"Failed to request ACPI_BAR's IO region\n");
			goto err_remove_devices;
		}

		err = mfd_add_devices(&pdev->dev, PLATFORM_DEVID_NONE,
				      cs5535_olpc_mfd_cells,
				      ARRAY_SIZE(cs5535_olpc_mfd_cells),
				      NULL, 0, NULL);
		if (err) {
			dev_err(&pdev->dev,
				"Failed to add CS5535 OLPC sub-devices: %d\n",
				err);
			goto err_release_acpi;
		}
	}

	dev_info(&pdev->dev, "%zu devices registered.\n",
			ARRAY_SIZE(cs5535_mfd_cells));

	return 0;

err_release_acpi:
	pci_release_region(pdev, ACPI_BAR);
err_remove_devices:
	mfd_remove_devices(&pdev->dev);
err_release_pms:
	pci_release_region(pdev, PMS_BAR);
err_disable:
	pci_disable_device(pdev);
	return err;
}

static void cs5535_mfd_remove(struct pci_dev *pdev)
{
	mfd_remove_devices(&pdev->dev);

	if (machine_is_olpc())
		pci_release_region(pdev, ACPI_BAR);

	pci_release_region(pdev, PMS_BAR);
	pci_disable_device(pdev);
}

static const struct pci_device_id cs5535_mfd_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_NS, PCI_DEVICE_ID_NS_CS5535_ISA) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_CS5536_ISA) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, cs5535_mfd_pci_tbl);

static struct pci_driver cs5535_mfd_driver = {
	.name = DRV_NAME,
	.id_table = cs5535_mfd_pci_tbl,
	.probe = cs5535_mfd_probe,
	.remove = cs5535_mfd_remove,
};

module_pci_driver(cs5535_mfd_driver);

MODULE_AUTHOR("Andres Salomon <dilinger@queued.net>");
MODULE_DESCRIPTION("MFD driver for CS5535/CS5536 southbridge's ISA PCI device");
MODULE_LICENSE("GPL");
