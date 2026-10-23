// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "mpnic.h"
#include "mpnic_netdev.h"
#include "mpnic_txrx.h"

#define PCI_DEVICE_ID_META_MPNIC	0x0014

static void mpnic_mmio_err(struct mpnic_dev *mpd, u32 reg)
{
	/* Hardware is giving us all 1's reads, assume it is gone */
	WRITE_ONCE(mpd->uc_addr0, NULL);

	dev_err(mpd->dev,
		"Failed read (idx 0x%x AKA addr 0x%x), disabled CSR access, awaiting reset\n",
		reg, reg << 2);

	/* Tell the stack the device has lost its PCIe link */
	if (mpd->netdev)
		netif_device_detach(mpd->netdev);
}

u64 mpnic_rd64(struct mpnic_dev *mpd, u32 reg)
{
	u32 __iomem *csr = READ_ONCE(mpd->uc_addr0);
	u64 value;

	if (!csr)
		return ~0ULL;

	value = readq(csr + reg);

	/* If any bits are 0 value should be valid */
	if (~value)
		return value;

	/* All ones can be a valid value, so confirm against a register
	 * which never reads that way on a live device.
	 */
	if (reg != MPNIC_BDQ_SPARE && ~readq(csr + MPNIC_BDQ_SPARE))
		return value;

	mpnic_mmio_err(mpd, reg);

	return ~0ULL;
}

static void mpnic_service_task(struct work_struct *work)
{
	struct mpnic_dev *mpd = container_of(to_delayed_work(work),
					     struct mpnic_dev, service_task);
	struct net_device *netdev = mpd->netdev;

	netdev_lock(netdev);

	if (netif_carrier_ok(netdev))
		mpnic_napi_depletion_check(netdev_priv(netdev));

	if (netif_running(netdev))
		schedule_delayed_work(&mpd->service_task, HZ);

	netdev_unlock(netdev);
}

static struct mpnic_dev *mpnic_alloc(struct pci_dev *pdev)
{
	struct mpnic_dev *mpd;

	mpd = kzalloc_obj(*mpd);
	if (!mpd)
		return NULL;

	pci_set_drvdata(pdev, mpd);
	mpd->dev = &pdev->dev;

	mpd->dsn = pci_get_dsn(pdev);
	mpd->mps = pcie_get_mps(pdev);
	mpd->readrq = pcie_get_readrq(pdev);
	mpd->relaxed_ord = pcie_relaxed_ordering_enabled(pdev);

	INIT_DELAYED_WORK(&mpd->service_task, mpnic_service_task);

	return mpd;
}

/**
 * mpnic_probe - Device initialization routine
 * @pdev: PCI device information struct
 * @ent: entry in mpnic_pci_tbl
 *
 * Return: 0 on success, negative on failure
 **/
static int mpnic_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *netdev;
	void __iomem *uc_addr0;
	struct mpnic_dev *mpd;
	int err;

	if (pdev->error_state != pci_channel_io_normal) {
		dev_err(&pdev->dev,
			"PCI device still in an error state. Unable to load...\n");
		return -EIO;
	}

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "PCI enable device failed: %d\n", err);
		return err;
	}

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(46));
	if (err) {
		dev_err(&pdev->dev, "DMA configuration failed: %d\n", err);
		return err;
	}

	mpd = mpnic_alloc(pdev);
	if (!mpd)
		return -ENOMEM;

	uc_addr0 = pcim_iomap_region(pdev, 0, MPNIC_DRV_NAME);
	if (IS_ERR(uc_addr0)) {
		err = PTR_ERR(uc_addr0);
		dev_err(&pdev->dev, "Mapping the register file failed: %d\n",
			err);
		goto err_free_mpd;
	}
	mpd->uc_addr0 = uc_addr0;

	pci_set_master(pdev);
	pci_save_state(pdev);

	err = mpnic_alloc_irqs(mpd);
	if (err)
		goto err_free_mpd;

	err = mpnic_dev_init(mpd);
	if (err)
		goto err_free_irqs;

	netdev = mpnic_netdev_alloc(mpd);
	if (!netdev) {
		dev_err(&pdev->dev, "Netdev allocation failed\n");
		err = -ENOMEM;
		goto err_free_irqs;
	}

	err = mpnic_netdev_register(netdev);
	if (err) {
		dev_err(&pdev->dev, "Netdev registration failed: %d\n", err);
		goto err_free_netdev;
	}

	return 0;

err_free_netdev:
	mpnic_netdev_free(mpd);
err_free_irqs:
	mpnic_free_irqs(mpd);
err_free_mpd:
	kfree(mpd);

	return err;
}

/**
 * mpnic_remove - Device removal routine
 * @pdev: PCI device information struct
 **/
static void mpnic_remove(struct pci_dev *pdev)
{
	struct mpnic_dev *mpd = pci_get_drvdata(pdev);

	unregister_netdev(mpd->netdev);
	cancel_delayed_work_sync(&mpd->service_task);
	mpnic_netdev_free(mpd);
	mpnic_free_irqs(mpd);
	kfree(mpd);
}

static const struct pci_device_id mpnic_pci_tbl[] = {
	{ PCI_VDEVICE(META, PCI_DEVICE_ID_META_MPNIC) },
	/* required last entry */
	{}
};
MODULE_DEVICE_TABLE(pci, mpnic_pci_tbl);

static struct pci_driver mpnic_driver = {
	.name		= MPNIC_DRV_NAME,
	.id_table	= mpnic_pci_tbl,
	.probe		= mpnic_probe,
	.remove		= mpnic_remove,
};

module_pci_driver(mpnic_driver);

MODULE_DESCRIPTION("Meta Platforms Network Interface Controller");
MODULE_LICENSE("GPL");
