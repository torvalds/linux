// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2020 - 2025 Mucse Corporation. */

#include <linux/pci.h>
#include <net/rtnetlink.h>
#include <linux/etherdevice.h>

#include "rnpgbe.h"
#include "rnpgbe_hw.h"

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
 * rnpgbe_add_adapter - Add netdev for this pci_dev
 * @pdev: PCI device information structure
 * @board_type: board type
 *
 * rnpgbe_add_adapter initializes a netdev for this pci_dev
 * structure. Initializes Bar map, private structure, and a
 * hardware reset occur.
 *
 * Return: 0 on success, negative errno on failure
 **/
static int rnpgbe_add_adapter(struct pci_dev *pdev,
			      int board_type)
{
	struct net_device *netdev;
	void __iomem *hw_addr;
	struct mucse *mucse;
	struct mucse_hw *hw;
	int err;

	netdev = alloc_etherdev_mq(sizeof(struct mucse), RNPGBE_MAX_QUEUES);
	if (!netdev)
		return -ENOMEM;

	SET_NETDEV_DEV(netdev, &pdev->dev);
	mucse = netdev_priv(netdev);
	mucse->netdev = netdev;
	mucse->pdev = pdev;
	pci_set_drvdata(pdev, mucse);

	hw = &mucse->hw;
	hw_addr = devm_ioremap(&pdev->dev,
			       pci_resource_start(pdev, 2),
			       pci_resource_len(pdev, 2));
	if (!hw_addr) {
		err = -EIO;
		goto err_free_net;
	}

	hw->hw_addr = hw_addr;
	err = rnpgbe_init_hw(hw, board_type);
	if (err) {
		dev_err(&pdev->dev, "Init hw err %d\n", err);
		goto err_free_net;
	}

	return 0;

err_free_net:
	free_netdev(netdev);
	return err;
}

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
	int board_type = id->driver_data;
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

	err = rnpgbe_add_adapter(pdev, board_type);
	if (err)
		goto err_free_regions;

	return 0;
err_free_regions:
	pci_release_mem_regions(pdev);
err_disable_dev:
	pci_disable_device(pdev);
	return err;
}

/**
 * rnpgbe_rm_adapter - Remove netdev for this mucse structure
 * @pdev: PCI device information struct
 *
 * rnpgbe_rm_adapter remove a netdev for this mucse structure
 **/
static void rnpgbe_rm_adapter(struct pci_dev *pdev)
{
	struct mucse *mucse = pci_get_drvdata(pdev);
	struct net_device *netdev;

	if (!mucse)
		return;
	netdev = mucse->netdev;
	free_netdev(netdev);
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
	rnpgbe_rm_adapter(pdev);
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
}

/**
 * rnpgbe_dev_shutdown - Device shutdown routine
 * @pdev: PCI device information struct
 **/
static void rnpgbe_dev_shutdown(struct pci_dev *pdev)
{
	struct mucse *mucse = pci_get_drvdata(pdev);
	struct net_device *netdev = mucse->netdev;

	rtnl_lock();
	netif_device_detach(netdev);
	rtnl_unlock();
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
