/* Intel Ethernet Switch Host Interface Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#include <linux/module.h>

#include "fm10k.h"

/**
 * fm10k_pci_tbl - PCI Device ID Table
 *
 * Wildcard entries (PCI_ANY_ID) should come last
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static const struct pci_device_id fm10k_pci_tbl[] = {
	{ PCI_VDEVICE(INTEL, FM10K_DEV_ID_PF) },
	/* required last entry */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, fm10k_pci_tbl);

/**
 * fm10k_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in fm10k_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * fm10k_probe initializes an interface identified by a pci_dev structure.
 * The OS initialization, configuring of the interface private structure,
 * and a hardware reset occur.
 **/
static int fm10k_probe(struct pci_dev *pdev,
		       const struct pci_device_id *ent)
{
	int err;
	u64 dma_mask;

	err = pci_enable_device_mem(pdev);
	if (err)
		return err;

	/* By default fm10k only supports a 48 bit DMA mask */
	dma_mask = DMA_BIT_MASK(48) | dma_get_required_mask(&pdev->dev);

	if ((dma_mask <= DMA_BIT_MASK(32)) ||
	    dma_set_mask_and_coherent(&pdev->dev, dma_mask)) {
		dma_mask &= DMA_BIT_MASK(32);

		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		err = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
		if (err) {
			err = dma_set_coherent_mask(&pdev->dev,
						    DMA_BIT_MASK(32));
			if (err) {
				dev_err(&pdev->dev,
					"No usable DMA configuration, aborting\n");
				goto err_dma;
			}
		}
	}

	err = pci_request_selected_regions(pdev,
					   pci_select_bars(pdev,
							   IORESOURCE_MEM),
					   fm10k_driver_name);
	if (err) {
		dev_err(&pdev->dev,
			"pci_request_selected_regions failed 0x%x\n", err);
		goto err_pci_reg;
	}

	pci_set_master(pdev);
	pci_save_state(pdev);

	return 0;

err_pci_reg:
err_dma:
	pci_disable_device(pdev);
	return err;
}

/**
 * fm10k_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * fm10k_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void fm10k_remove(struct pci_dev *pdev)
{
	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));

	pci_disable_device(pdev);
}

static struct pci_driver fm10k_driver = {
	.name			= fm10k_driver_name,
	.id_table		= fm10k_pci_tbl,
	.probe			= fm10k_probe,
	.remove			= fm10k_remove,
};

/**
 * fm10k_register_pci_driver - register driver interface
 *
 * This funciton is called on module load in order to register the driver.
 **/
int fm10k_register_pci_driver(void)
{
	return pci_register_driver(&fm10k_driver);
}

/**
 * fm10k_unregister_pci_driver - unregister driver interface
 *
 * This funciton is called on module unload in order to remove the driver.
 **/
void fm10k_unregister_pci_driver(void)
{
	pci_unregister_driver(&fm10k_driver);
}
