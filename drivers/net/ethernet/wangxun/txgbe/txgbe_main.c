// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/aer.h>
#include <linux/etherdevice.h>

#include "../libwx/wx_type.h"
#include "../libwx/wx_hw.h"
#include "txgbe.h"

char txgbe_driver_name[] = "txgbe";

/* txgbe_pci_tbl - PCI Device ID Table
 *
 * Wildcard entries (PCI_ANY_ID) should come last
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static const struct pci_device_id txgbe_pci_tbl[] = {
	{ PCI_VDEVICE(WANGXUN, TXGBE_DEV_ID_SP1000), 0},
	{ PCI_VDEVICE(WANGXUN, TXGBE_DEV_ID_WX1820), 0},
	/* required last entry */
	{ .device = 0 }
};

#define DEFAULT_DEBUG_LEVEL_SHIFT 3

static void txgbe_check_minimum_link(struct txgbe_adapter *adapter)
{
	struct pci_dev *pdev;

	pdev = adapter->pdev;
	pcie_print_link_status(pdev);
}

/**
 * txgbe_enumerate_functions - Get the number of ports this device has
 * @adapter: adapter structure
 *
 * This function enumerates the phsyical functions co-located on a single slot,
 * in order to determine how many ports a device has. This is most useful in
 * determining the required GT/s of PCIe bandwidth necessary for optimal
 * performance.
 **/
static int txgbe_enumerate_functions(struct txgbe_adapter *adapter)
{
	struct pci_dev *entry, *pdev = adapter->pdev;
	int physfns = 0;

	list_for_each_entry(entry, &pdev->bus->devices, bus_list) {
		/* When the devices on the bus don't all match our device ID,
		 * we can't reliably determine the correct number of
		 * functions. This can occur if a function has been direct
		 * attached to a virtual machine using VT-d.
		 */
		if (entry->vendor != pdev->vendor ||
		    entry->device != pdev->device)
			return -EINVAL;

		physfns++;
	}

	return physfns;
}

/**
 * txgbe_sw_init - Initialize general software structures (struct txgbe_adapter)
 * @adapter: board private structure to initialize
 **/
static int txgbe_sw_init(struct txgbe_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct txgbe_hw *hw = &adapter->hw;
	struct wx_hw *wxhw = &hw->wxhw;
	int err;

	wxhw->hw_addr = adapter->io_addr;
	wxhw->pdev = pdev;

	/* PCI config space info */
	err = wx_sw_init(wxhw);
	if (err < 0) {
		netif_err(adapter, probe, adapter->netdev,
			  "read of internal subsystem device id failed\n");
		return err;
	}

	return 0;
}

static void txgbe_dev_shutdown(struct pci_dev *pdev, bool *enable_wake)
{
	struct txgbe_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev = adapter->netdev;

	netif_device_detach(netdev);

	pci_disable_device(pdev);
}

static void txgbe_shutdown(struct pci_dev *pdev)
{
	bool wake;

	txgbe_dev_shutdown(pdev, &wake);

	if (system_state == SYSTEM_POWER_OFF) {
		pci_wake_from_d3(pdev, wake);
		pci_set_power_state(pdev, PCI_D3hot);
	}
}

/**
 * txgbe_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in txgbe_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * txgbe_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 **/
static int txgbe_probe(struct pci_dev *pdev,
		       const struct pci_device_id __always_unused *ent)
{
	struct txgbe_adapter *adapter = NULL;
	struct txgbe_hw *hw = NULL;
	struct wx_hw *wxhw = NULL;
	struct net_device *netdev;
	int err, expected_gts;

	err = pci_enable_device_mem(pdev);
	if (err)
		return err;

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		dev_err(&pdev->dev,
			"No usable DMA configuration, aborting\n");
		goto err_pci_disable_dev;
	}

	err = pci_request_selected_regions(pdev,
					   pci_select_bars(pdev, IORESOURCE_MEM),
					   txgbe_driver_name);
	if (err) {
		dev_err(&pdev->dev,
			"pci_request_selected_regions failed 0x%x\n", err);
		goto err_pci_disable_dev;
	}

	pci_enable_pcie_error_reporting(pdev);
	pci_set_master(pdev);

	netdev = devm_alloc_etherdev_mqs(&pdev->dev,
					 sizeof(struct txgbe_adapter),
					 TXGBE_MAX_TX_QUEUES,
					 TXGBE_MAX_RX_QUEUES);
	if (!netdev) {
		err = -ENOMEM;
		goto err_pci_release_regions;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);

	adapter = netdev_priv(netdev);
	adapter->netdev = netdev;
	adapter->pdev = pdev;
	hw = &adapter->hw;
	wxhw = &hw->wxhw;
	adapter->msg_enable = (1 << DEFAULT_DEBUG_LEVEL_SHIFT) - 1;

	adapter->io_addr = devm_ioremap(&pdev->dev,
					pci_resource_start(pdev, 0),
					pci_resource_len(pdev, 0));
	if (!adapter->io_addr) {
		err = -EIO;
		goto err_pci_release_regions;
	}

	strncpy(netdev->name, pci_name(pdev), sizeof(netdev->name) - 1);

	/* setup the private structure */
	err = txgbe_sw_init(adapter);
	if (err)
		goto err_pci_release_regions;

	/* check if flash load is done after hw power up */
	err = wx_check_flash_load(wxhw, TXGBE_SPI_ILDR_STATUS_PERST);
	if (err)
		goto err_pci_release_regions;
	err = wx_check_flash_load(wxhw, TXGBE_SPI_ILDR_STATUS_PWRRST);
	if (err)
		goto err_pci_release_regions;

	netdev->features |= NETIF_F_HIGHDMA;

	pci_set_drvdata(pdev, adapter);

	/* calculate the expected PCIe bandwidth required for optimal
	 * performance. Note that some older parts will never have enough
	 * bandwidth due to being older generation PCIe parts. We clamp these
	 * parts to ensure that no warning is displayed, as this could confuse
	 * users otherwise.
	 */
	expected_gts = txgbe_enumerate_functions(adapter) * 10;

	/* don't check link if we failed to enumerate functions */
	if (expected_gts > 0)
		txgbe_check_minimum_link(adapter);
	else
		dev_warn(&pdev->dev, "Failed to enumerate PF devices.\n");

	return 0;

err_pci_release_regions:
	pci_disable_pcie_error_reporting(pdev);
	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));
err_pci_disable_dev:
	pci_disable_device(pdev);
	return err;
}

/**
 * txgbe_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * txgbe_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void txgbe_remove(struct pci_dev *pdev)
{
	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));

	pci_disable_pcie_error_reporting(pdev);

	pci_disable_device(pdev);
}

static struct pci_driver txgbe_driver = {
	.name     = txgbe_driver_name,
	.id_table = txgbe_pci_tbl,
	.probe    = txgbe_probe,
	.remove   = txgbe_remove,
	.shutdown = txgbe_shutdown,
};

module_pci_driver(txgbe_driver);

MODULE_DEVICE_TABLE(pci, txgbe_pci_tbl);
MODULE_AUTHOR("Beijing WangXun Technology Co., Ltd, <software@trustnetic.com>");
MODULE_DESCRIPTION("WangXun(R) 10 Gigabit PCI Express Network Driver");
MODULE_LICENSE("GPL");
