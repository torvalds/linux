// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Physcial Function ethernet driver
 *
 * Copyright (C) 2020 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>
#include <linux/of.h>
#include <linux/if_vlan.h>
#include <linux/iommu.h>
#include <net/ip.h>

#include "otx2_common.h"

#define DRV_NAME	"octeontx2-nicpf"
#define DRV_STRING	"Marvell OcteonTX2 NIC Physical Function Driver"
#define DRV_VERSION	"1.0"

/* Supported devices */
static const struct pci_device_id otx2_pf_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, PCI_DEVID_OCTEONTX2_RVU_PF) },
	{ 0, }  /* end of table */
};

MODULE_AUTHOR("Marvell International Ltd.");
MODULE_DESCRIPTION(DRV_STRING);
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
MODULE_DEVICE_TABLE(pci, otx2_pf_id_table);

static int otx2_set_real_num_queues(struct net_device *netdev,
				    int tx_queues, int rx_queues)
{
	int err;

	err = netif_set_real_num_tx_queues(netdev, tx_queues);
	if (err) {
		netdev_err(netdev,
			   "Failed to set no of Tx queues: %d\n", tx_queues);
		return err;
	}

	err = netif_set_real_num_rx_queues(netdev, rx_queues);
	if (err)
		netdev_err(netdev,
			   "Failed to set no of Rx queues: %d\n", rx_queues);
	return err;
}

static int otx2_open(struct net_device *netdev)
{
	netif_carrier_off(netdev);

	return 0;
}

static int otx2_stop(struct net_device *netdev)
{
	return 0;
}

static const struct net_device_ops otx2_netdev_ops = {
	.ndo_open		= otx2_open,
	.ndo_stop		= otx2_stop,
};

static int otx2_check_pf_usable(struct otx2_nic *nic)
{
	u64 rev;

	rev = otx2_read64(nic, RVU_PF_BLOCK_ADDRX_DISC(BLKADDR_RVUM));
	rev = (rev >> 12) & 0xFF;
	/* Check if AF has setup revision for RVUM block,
	 * otherwise this driver probe should be deferred
	 * until AF driver comes up.
	 */
	if (!rev) {
		dev_warn(nic->dev,
			 "AF is not initialized, deferring probe\n");
		return -EPROBE_DEFER;
	}
	return 0;
}

static int otx2_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct net_device *netdev;
	struct otx2_nic *pf;
	struct otx2_hw *hw;
	int err, qcount;

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device\n");
		return err;
	}

	err = pci_request_regions(pdev, DRV_NAME);
	if (err) {
		dev_err(dev, "PCI request regions failed 0x%x\n", err);
		return err;
	}

	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(48));
	if (err) {
		dev_err(dev, "DMA mask config failed, abort\n");
		goto err_release_regions;
	}

	pci_set_master(pdev);

	/* Set number of queues */
	qcount = min_t(int, num_online_cpus(), num_online_cpus());

	netdev = alloc_etherdev_mqs(sizeof(*pf), qcount, qcount);
	if (!netdev) {
		err = -ENOMEM;
		goto err_release_regions;
	}

	pci_set_drvdata(pdev, netdev);
	SET_NETDEV_DEV(netdev, &pdev->dev);
	pf = netdev_priv(netdev);
	pf->netdev = netdev;
	pf->pdev = pdev;
	pf->dev = dev;

	hw = &pf->hw;
	hw->pdev = pdev;
	hw->rx_queues = qcount;
	hw->tx_queues = qcount;
	hw->max_queues = qcount;

	/* Map CSRs */
	pf->reg_base = pcim_iomap(pdev, PCI_CFG_REG_BAR_NUM, 0);
	if (!pf->reg_base) {
		dev_err(dev, "Unable to map physical function CSRs, aborting\n");
		err = -ENOMEM;
		goto err_free_netdev;
	}

	err = otx2_check_pf_usable(pf);
	if (err)
		goto err_free_netdev;

	err = otx2_set_real_num_queues(netdev, hw->tx_queues, hw->rx_queues);
	if (err)
		goto err_free_netdev;

	netdev->netdev_ops = &otx2_netdev_ops;

	err = register_netdev(netdev);
	if (err) {
		dev_err(dev, "Failed to register netdevice\n");
		goto err_free_netdev;
	}

	return 0;

err_free_netdev:
	pci_set_drvdata(pdev, NULL);
	free_netdev(netdev);
err_release_regions:
	pci_release_regions(pdev);
	return err;
}

static void otx2_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct otx2_nic *pf;

	if (!netdev)
		return;

	pf = netdev_priv(netdev);

	unregister_netdev(netdev);
	pci_free_irq_vectors(pf->pdev);
	pci_set_drvdata(pdev, NULL);
	free_netdev(netdev);
	pci_release_regions(pdev);
}

static struct pci_driver otx2_pf_driver = {
	.name = DRV_NAME,
	.id_table = otx2_pf_id_table,
	.probe = otx2_probe,
	.shutdown = otx2_remove,
	.remove = otx2_remove,
};

static int __init otx2_rvupf_init_module(void)
{
	pr_info("%s: %s\n", DRV_NAME, DRV_STRING);

	return pci_register_driver(&otx2_pf_driver);
}

static void __exit otx2_rvupf_cleanup_module(void)
{
	pci_unregister_driver(&otx2_pf_driver);
}

module_init(otx2_rvupf_init_module);
module_exit(otx2_rvupf_cleanup_module);
