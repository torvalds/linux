// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2020 - 2025 Mucse Corporation. */

#include <linux/pci.h>

#include "rnpgbe.h"

static const char rnpgbe_driver_name[] = "rnpgbe";

/* rnpgbe_pci_tbl - PCI Device ID Table
 *
 * { PCI_VDEVICE(Vendor ID, Device ID),
 *   private_data (used for different hw chip) }
 */
static struct pci_device_id rnpgbe_pci_tbl[] = {
	{ PCI_VDEVICE(MUCSE, RNPGBE_DEVICE_ID_N210), board_n210 },
	{ PCI_VDEVICE(MUCSE, RNPGBE_DEVICE_ID_N210L), board_n210 },
	{ PCI_VDEVICE(MUCSE, RNPGBE_DEVICE_ID_N500_DUAL_PORT), board_n500 },
	{ PCI_VDEVICE(MUCSE, RNPGBE_DEVICE_ID_N500_QUAD_PORT), board_n500 },
	/* required last entry */
	{0, },
};

/**
 * rnpgbe_probe - Device initialization routine
 * @pdev: PCI device information struct
 * @id: entry in rnpgbe_pci_tbl
 *
 * rnpgbe_probe initializes a PF adapter identified by a pci_dev
 * structure.
 *
 * Return: 0 on success, negative errno on failure
 **/
static int rnpgbe_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int err;

	err = pci_enable_device_mem(pdev);
	if (err)
		return err;

	err = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(56));
	if (err) {
		dev_err(&pdev->dev,
			"No usable DMA configuration, aborting %d\n", err);
		goto err_disable_dev;
	}

	err = pci_request_mem_regions(pdev, rnpgbe_driver_name);
	if (err) {
		dev_err(&pdev->dev,
			"pci_request_selected_regions failed %d\n", err);
		goto err_disable_dev;
	}

	pci_set_master(pdev);
	err = pci_save_state(pdev);
	if (err) {
		dev_err(&pdev->dev, "pci_save_state failed %d\n", err);
		goto err_free_regions;
	}

	return 0;
err_free_regions:
	pci_release_mem_regions(pdev);
err_disable_dev:
	pci_disable_device(pdev);
	return err;
}

/**
 * rnpgbe_remove - Device removal routine
 * @pdev: PCI device information struct
 *
 * rnpgbe_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device. This could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void rnpgbe_remove(struct pci_dev *pdev)
{
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
}

/**
 * rnpgbe_dev_shutdown - Device shutdown routine
 * @pdev: PCI device information struct
 **/
static void rnpgbe_dev_shutdown(struct pci_dev *pdev)
{
	pci_disable_device(pdev);
}

/**
 * rnpgbe_shutdown - Device shutdown routine
 * @pdev: PCI device information struct
 *
 * rnpgbe_shutdown is called by the PCI subsystem to alert the driver
 * that os shutdown. Device should setup wakeup state here.
 **/
static void rnpgbe_shutdown(struct pci_dev *pdev)
{
	rnpgbe_dev_shutdown(pdev);
}

static struct pci_driver rnpgbe_driver = {
	.name     = rnpgbe_driver_name,
	.id_table = rnpgbe_pci_tbl,
	.probe    = rnpgbe_probe,
	.remove   = rnpgbe_remove,
	.shutdown = rnpgbe_shutdown,
};

module_pci_driver(rnpgbe_driver);

MODULE_DEVICE_TABLE(pci, rnpgbe_pci_tbl);
MODULE_AUTHOR("Yibo Dong, <dong100@mucse.com>");
MODULE_DESCRIPTION("Mucse(R) 1 Gigabit PCI Express Network Driver");
MODULE_LICENSE("GPL");
