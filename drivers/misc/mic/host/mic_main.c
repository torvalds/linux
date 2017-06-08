/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel MIC Host driver.
 */
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/poll.h>

#include <linux/mic_common.h>
#include "../common/mic_dev.h"
#include "mic_device.h"
#include "mic_x100.h"
#include "mic_smpt.h"

static const char mic_driver_name[] = "mic";

static const struct pci_device_id mic_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MIC_X100_PCI_DEVICE_2250)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MIC_X100_PCI_DEVICE_2251)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MIC_X100_PCI_DEVICE_2252)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MIC_X100_PCI_DEVICE_2253)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MIC_X100_PCI_DEVICE_2254)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MIC_X100_PCI_DEVICE_2255)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MIC_X100_PCI_DEVICE_2256)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MIC_X100_PCI_DEVICE_2257)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MIC_X100_PCI_DEVICE_2258)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MIC_X100_PCI_DEVICE_2259)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MIC_X100_PCI_DEVICE_225a)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MIC_X100_PCI_DEVICE_225b)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MIC_X100_PCI_DEVICE_225c)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MIC_X100_PCI_DEVICE_225d)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MIC_X100_PCI_DEVICE_225e)},

	/* required last entry */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, mic_pci_tbl);

/* ID allocator for MIC devices */
static struct ida g_mic_ida;

/* Initialize the device page */
static int mic_dp_init(struct mic_device *mdev)
{
	mdev->dp = kzalloc(MIC_DP_SIZE, GFP_KERNEL);
	if (!mdev->dp)
		return -ENOMEM;

	mdev->dp_dma_addr = mic_map_single(mdev,
		mdev->dp, MIC_DP_SIZE);
	if (mic_map_error(mdev->dp_dma_addr)) {
		kfree(mdev->dp);
		dev_err(&mdev->pdev->dev, "%s %d err %d\n",
			__func__, __LINE__, -ENOMEM);
		return -ENOMEM;
	}
	mdev->ops->write_spad(mdev, MIC_DPLO_SPAD, mdev->dp_dma_addr);
	mdev->ops->write_spad(mdev, MIC_DPHI_SPAD, mdev->dp_dma_addr >> 32);
	return 0;
}

/* Uninitialize the device page */
static void mic_dp_uninit(struct mic_device *mdev)
{
	mic_unmap_single(mdev, mdev->dp_dma_addr, MIC_DP_SIZE);
	kfree(mdev->dp);
}

/**
 * mic_ops_init: Initialize HW specific operation tables.
 *
 * @mdev: pointer to mic_device instance
 *
 * returns none.
 */
static void mic_ops_init(struct mic_device *mdev)
{
	switch (mdev->family) {
	case MIC_FAMILY_X100:
		mdev->ops = &mic_x100_ops;
		mdev->intr_ops = &mic_x100_intr_ops;
		mdev->smpt_ops = &mic_x100_smpt_ops;
		break;
	default:
		break;
	}
}

/**
 * mic_get_family - Determine hardware family to which this MIC belongs.
 *
 * @pdev: The pci device structure
 *
 * returns family.
 */
static enum mic_hw_family mic_get_family(struct pci_dev *pdev)
{
	enum mic_hw_family family;

	switch (pdev->device) {
	case MIC_X100_PCI_DEVICE_2250:
	case MIC_X100_PCI_DEVICE_2251:
	case MIC_X100_PCI_DEVICE_2252:
	case MIC_X100_PCI_DEVICE_2253:
	case MIC_X100_PCI_DEVICE_2254:
	case MIC_X100_PCI_DEVICE_2255:
	case MIC_X100_PCI_DEVICE_2256:
	case MIC_X100_PCI_DEVICE_2257:
	case MIC_X100_PCI_DEVICE_2258:
	case MIC_X100_PCI_DEVICE_2259:
	case MIC_X100_PCI_DEVICE_225a:
	case MIC_X100_PCI_DEVICE_225b:
	case MIC_X100_PCI_DEVICE_225c:
	case MIC_X100_PCI_DEVICE_225d:
	case MIC_X100_PCI_DEVICE_225e:
		family = MIC_FAMILY_X100;
		break;
	default:
		family = MIC_FAMILY_UNKNOWN;
		break;
	}
	return family;
}

/**
 * mic_device_init - Allocates and initializes the MIC device structure
 *
 * @mdev: pointer to mic_device instance
 * @pdev: The pci device structure
 *
 * returns none.
 */
static void
mic_device_init(struct mic_device *mdev, struct pci_dev *pdev)
{
	mdev->pdev = pdev;
	mdev->family = mic_get_family(pdev);
	mdev->stepping = pdev->revision;
	mic_ops_init(mdev);
	mutex_init(&mdev->mic_mutex);
	mdev->irq_info.next_avail_src = 0;
}

/**
 * mic_probe - Device Initialization Routine
 *
 * @pdev: PCI device structure
 * @ent: entry in mic_pci_tbl
 *
 * returns 0 on success, < 0 on failure.
 */
static int mic_probe(struct pci_dev *pdev,
		     const struct pci_device_id *ent)
{
	int rc;
	struct mic_device *mdev;

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev) {
		rc = -ENOMEM;
		dev_err(&pdev->dev, "mdev kmalloc failed rc %d\n", rc);
		goto mdev_alloc_fail;
	}
	mdev->id = ida_simple_get(&g_mic_ida, 0, MIC_MAX_NUM_DEVS, GFP_KERNEL);
	if (mdev->id < 0) {
		rc = mdev->id;
		dev_err(&pdev->dev, "ida_simple_get failed rc %d\n", rc);
		goto ida_fail;
	}

	mic_device_init(mdev, pdev);

	rc = pci_enable_device(pdev);
	if (rc) {
		dev_err(&pdev->dev, "failed to enable pci device.\n");
		goto ida_remove;
	}

	pci_set_master(pdev);

	rc = pci_request_regions(pdev, mic_driver_name);
	if (rc) {
		dev_err(&pdev->dev, "failed to get pci regions.\n");
		goto disable_device;
	}

	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (rc) {
		dev_err(&pdev->dev, "Cannot set DMA mask\n");
		goto release_regions;
	}

	mdev->mmio.pa = pci_resource_start(pdev, mdev->ops->mmio_bar);
	mdev->mmio.len = pci_resource_len(pdev, mdev->ops->mmio_bar);
	mdev->mmio.va = pci_ioremap_bar(pdev, mdev->ops->mmio_bar);
	if (!mdev->mmio.va) {
		dev_err(&pdev->dev, "Cannot remap MMIO BAR\n");
		rc = -EIO;
		goto release_regions;
	}

	mdev->aper.pa = pci_resource_start(pdev, mdev->ops->aper_bar);
	mdev->aper.len = pci_resource_len(pdev, mdev->ops->aper_bar);
	mdev->aper.va = ioremap_wc(mdev->aper.pa, mdev->aper.len);
	if (!mdev->aper.va) {
		dev_err(&pdev->dev, "Cannot remap Aperture BAR\n");
		rc = -EIO;
		goto unmap_mmio;
	}

	mdev->intr_ops->intr_init(mdev);
	rc = mic_setup_interrupts(mdev, pdev);
	if (rc) {
		dev_err(&pdev->dev, "mic_setup_interrupts failed %d\n", rc);
		goto unmap_aper;
	}
	rc = mic_smpt_init(mdev);
	if (rc) {
		dev_err(&pdev->dev, "smpt_init failed %d\n", rc);
		goto free_interrupts;
	}

	pci_set_drvdata(pdev, mdev);

	rc = mic_dp_init(mdev);
	if (rc) {
		dev_err(&pdev->dev, "mic_dp_init failed rc %d\n", rc);
		goto smpt_uninit;
	}
	mic_bootparam_init(mdev);
	mic_create_debug_dir(mdev);

	mdev->cosm_dev = cosm_register_device(&mdev->pdev->dev, &cosm_hw_ops);
	if (IS_ERR(mdev->cosm_dev)) {
		rc = PTR_ERR(mdev->cosm_dev);
		dev_err(&pdev->dev, "cosm_add_device failed rc %d\n", rc);
		goto cleanup_debug_dir;
	}
	return 0;
cleanup_debug_dir:
	mic_delete_debug_dir(mdev);
	mic_dp_uninit(mdev);
smpt_uninit:
	mic_smpt_uninit(mdev);
free_interrupts:
	mic_free_interrupts(mdev, pdev);
unmap_aper:
	iounmap(mdev->aper.va);
unmap_mmio:
	iounmap(mdev->mmio.va);
release_regions:
	pci_release_regions(pdev);
disable_device:
	pci_disable_device(pdev);
ida_remove:
	ida_simple_remove(&g_mic_ida, mdev->id);
ida_fail:
	kfree(mdev);
mdev_alloc_fail:
	dev_err(&pdev->dev, "Probe failed rc %d\n", rc);
	return rc;
}

/**
 * mic_remove - Device Removal Routine
 * mic_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.
 *
 * @pdev: PCI device structure
 */
static void mic_remove(struct pci_dev *pdev)
{
	struct mic_device *mdev;

	mdev = pci_get_drvdata(pdev);
	if (!mdev)
		return;

	cosm_unregister_device(mdev->cosm_dev);
	mic_delete_debug_dir(mdev);
	mic_dp_uninit(mdev);
	mic_smpt_uninit(mdev);
	mic_free_interrupts(mdev, pdev);
	iounmap(mdev->aper.va);
	iounmap(mdev->mmio.va);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	ida_simple_remove(&g_mic_ida, mdev->id);
	kfree(mdev);
}

static struct pci_driver mic_driver = {
	.name = mic_driver_name,
	.id_table = mic_pci_tbl,
	.probe = mic_probe,
	.remove = mic_remove
};

static int __init mic_init(void)
{
	int ret;

	request_module("mic_x100_dma");
	mic_init_debugfs();
	ida_init(&g_mic_ida);
	ret = pci_register_driver(&mic_driver);
	if (ret) {
		pr_err("pci_register_driver failed ret %d\n", ret);
		goto cleanup_debugfs;
	}
	return 0;
cleanup_debugfs:
	ida_destroy(&g_mic_ida);
	mic_exit_debugfs();
	return ret;
}

static void __exit mic_exit(void)
{
	pci_unregister_driver(&mic_driver);
	ida_destroy(&g_mic_ida);
	mic_exit_debugfs();
}

module_init(mic_init);
module_exit(mic_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) MIC X100 Host driver");
MODULE_LICENSE("GPL v2");
