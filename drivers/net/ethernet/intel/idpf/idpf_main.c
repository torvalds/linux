// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include "idpf.h"
#include "idpf_devids.h"

#define DRV_SUMMARY	"Intel(R) Infrastructure Data Path Function Linux Driver"

MODULE_DESCRIPTION(DRV_SUMMARY);
MODULE_LICENSE("GPL");

/**
 * idpf_remove - Device removal routine
 * @pdev: PCI device information struct
 */
static void idpf_remove(struct pci_dev *pdev)
{
	struct idpf_adapter *adapter = pci_get_drvdata(pdev);

	pci_set_drvdata(pdev, NULL);
	kfree(adapter);
}

/**
 * idpf_shutdown - PCI callback for shutting down device
 * @pdev: PCI device information struct
 */
static void idpf_shutdown(struct pci_dev *pdev)
{
	idpf_remove(pdev);

	if (system_state == SYSTEM_POWER_OFF)
		pci_set_power_state(pdev, PCI_D3hot);
}

/**
 * idpf_cfg_hw - Initialize HW struct
 * @adapter: adapter to setup hw struct for
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_cfg_hw(struct idpf_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct idpf_hw *hw = &adapter->hw;

	hw->hw_addr = pcim_iomap_table(pdev)[0];
	if (!hw->hw_addr) {
		pci_err(pdev, "failed to allocate PCI iomap table\n");

		return -ENOMEM;
	}

	hw->back = adapter;

	return 0;
}

/**
 * idpf_probe - Device initialization routine
 * @pdev: PCI device information struct
 * @ent: entry in idpf_pci_tbl
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	struct idpf_adapter *adapter;
	int err;

	adapter = kzalloc(sizeof(*adapter), GFP_KERNEL);
	if (!adapter)
		return -ENOMEM;
	adapter->pdev = pdev;

	err = pcim_enable_device(pdev);
	if (err)
		goto err_free;

	err = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));
	if (err) {
		pci_err(pdev, "pcim_iomap_regions failed %pe\n", ERR_PTR(err));

		goto err_free;
	}

	/* set up for high or low dma */
	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (err) {
		pci_err(pdev, "DMA configuration failed: %pe\n", ERR_PTR(err));

		goto err_free;
	}

	pci_set_master(pdev);
	pci_set_drvdata(pdev, adapter);

	/* setup msglvl */
	adapter->msg_enable = netif_msg_init(-1, IDPF_AVAIL_NETIF_M);

	err = idpf_cfg_hw(adapter);
	if (err) {
		dev_err(dev, "Failed to configure HW structure for adapter: %d\n",
			err);
		goto err_free;
	}

	return 0;

err_free:
	kfree(adapter);
	return err;
}

/* idpf_pci_tbl - PCI Dev idpf ID Table
 */
static const struct pci_device_id idpf_pci_tbl[] = {
	{ PCI_VDEVICE(INTEL, IDPF_DEV_ID_PF)},
	{ PCI_VDEVICE(INTEL, IDPF_DEV_ID_VF)},
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(pci, idpf_pci_tbl);

static struct pci_driver idpf_driver = {
	.name			= KBUILD_MODNAME,
	.id_table		= idpf_pci_tbl,
	.probe			= idpf_probe,
	.remove			= idpf_remove,
	.shutdown		= idpf_shutdown,
};
module_pci_driver(idpf_driver);
