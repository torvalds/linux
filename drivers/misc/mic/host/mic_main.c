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
 *
 * Global TODO's across the driver to be added after initial base
 * patches are accepted upstream:
 * 1) Enable DMA support.
 * 2) Enable per vring interrupt support.
 */
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/suspend.h>

#include <linux/mic_common.h>
#include "../common/mic_dev.h"
#include "mic_device.h"
#include "mic_x100.h"
#include "mic_smpt.h"
#include "mic_fops.h"
#include "mic_virtio.h"

static const char mic_driver_name[] = "mic";

static DEFINE_PCI_DEVICE_TABLE(mic_pci_tbl) = {
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
/* Class of MIC devices for sysfs accessibility. */
static struct class *g_mic_class;
/* Base device node number for MIC devices */
static dev_t g_mic_devno;

static const struct file_operations mic_fops = {
	.open = mic_open,
	.release = mic_release,
	.unlocked_ioctl = mic_ioctl,
	.poll = mic_poll,
	.mmap = mic_mmap,
	.owner = THIS_MODULE,
};

/* Initialize the device page */
static int mic_dp_init(struct mic_device *mdev)
{
	mdev->dp = kzalloc(MIC_DP_SIZE, GFP_KERNEL);
	if (!mdev->dp) {
		dev_err(mdev->sdev->parent, "%s %d err %d\n",
			__func__, __LINE__, -ENOMEM);
		return -ENOMEM;
	}

	mdev->dp_dma_addr = mic_map_single(mdev,
		mdev->dp, MIC_DP_SIZE);
	if (mic_map_error(mdev->dp_dma_addr)) {
		kfree(mdev->dp);
		dev_err(mdev->sdev->parent, "%s %d err %d\n",
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
 * mic_shutdown_db - Shutdown doorbell interrupt handler.
 */
static irqreturn_t mic_shutdown_db(int irq, void *data)
{
	struct mic_device *mdev = data;
	struct mic_bootparam *bootparam = mdev->dp;

	mdev->ops->ack_interrupt(mdev);

	switch (bootparam->shutdown_status) {
	case MIC_HALTED:
	case MIC_POWER_OFF:
	case MIC_RESTART:
		/* Fall through */
	case MIC_CRASHED:
		schedule_work(&mdev->shutdown_work);
		break;
	default:
		break;
	};
	return IRQ_HANDLED;
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
* mic_pm_notifier: Notifier callback function that handles
* PM notifications.
*
* @notifier_block: The notifier structure.
* @pm_event: The event for which the driver was notified.
* @unused: Meaningless. Always NULL.
*
* returns NOTIFY_DONE
*/
static int mic_pm_notifier(struct notifier_block *notifier,
		unsigned long pm_event, void *unused)
{
	struct mic_device *mdev = container_of(notifier,
		struct mic_device, pm_notifier);

	switch (pm_event) {
	case PM_HIBERNATION_PREPARE:
		/* Fall through */
	case PM_SUSPEND_PREPARE:
		mic_prepare_suspend(mdev);
		break;
	case PM_POST_HIBERNATION:
		/* Fall through */
	case PM_POST_SUSPEND:
		/* Fall through */
	case PM_POST_RESTORE:
		mic_complete_resume(mdev);
		break;
	case PM_RESTORE_PREPARE:
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

/**
 * mic_device_init - Allocates and initializes the MIC device structure
 *
 * @mdev: pointer to mic_device instance
 * @pdev: The pci device structure
 *
 * returns none.
 */
static int
mic_device_init(struct mic_device *mdev, struct pci_dev *pdev)
{
	int rc;

	mdev->family = mic_get_family(pdev);
	mdev->stepping = pdev->revision;
	mic_ops_init(mdev);
	mic_sysfs_init(mdev);
	mutex_init(&mdev->mic_mutex);
	mdev->irq_info.next_avail_src = 0;
	INIT_WORK(&mdev->reset_trigger_work, mic_reset_trigger_work);
	INIT_WORK(&mdev->shutdown_work, mic_shutdown_work);
	init_completion(&mdev->reset_wait);
	INIT_LIST_HEAD(&mdev->vdev_list);
	mdev->pm_notifier.notifier_call = mic_pm_notifier;
	rc = register_pm_notifier(&mdev->pm_notifier);
	if (rc) {
		dev_err(&pdev->dev, "register_pm_notifier failed rc %d\n",
			rc);
		goto register_pm_notifier_fail;
	}
	return 0;
register_pm_notifier_fail:
	flush_work(&mdev->shutdown_work);
	flush_work(&mdev->reset_trigger_work);
	return rc;
}

/**
 * mic_device_uninit - Frees resources allocated during mic_device_init(..)
 *
 * @mdev: pointer to mic_device instance
 *
 * returns none
 */
static void mic_device_uninit(struct mic_device *mdev)
{
	/* The cmdline sysfs entry might have allocated cmdline */
	kfree(mdev->cmdline);
	kfree(mdev->firmware);
	kfree(mdev->ramdisk);
	kfree(mdev->bootmode);
	flush_work(&mdev->reset_trigger_work);
	flush_work(&mdev->shutdown_work);
	unregister_pm_notifier(&mdev->pm_notifier);
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

	rc = mic_device_init(mdev, pdev);
	if (rc) {
		dev_err(&pdev->dev, "mic_device_init failed rc %d\n", rc);
		goto device_init_fail;
	}

	rc = pci_enable_device(pdev);
	if (rc) {
		dev_err(&pdev->dev, "failed to enable pci device.\n");
		goto uninit_device;
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

	mdev->sdev = device_create_with_groups(g_mic_class, &pdev->dev,
		MKDEV(MAJOR(g_mic_devno), mdev->id), NULL,
		mdev->attr_group, "mic%d", mdev->id);
	if (IS_ERR(mdev->sdev)) {
		rc = PTR_ERR(mdev->sdev);
		dev_err(&pdev->dev,
			"device_create_with_groups failed rc %d\n", rc);
		goto smpt_uninit;
	}
	mdev->state_sysfs = sysfs_get_dirent(mdev->sdev->kobj.sd, "state");
	if (!mdev->state_sysfs) {
		rc = -ENODEV;
		dev_err(&pdev->dev, "sysfs_get_dirent failed rc %d\n", rc);
		goto destroy_device;
	}

	rc = mic_dp_init(mdev);
	if (rc) {
		dev_err(&pdev->dev, "mic_dp_init failed rc %d\n", rc);
		goto sysfs_put;
	}
	mutex_lock(&mdev->mic_mutex);

	mdev->shutdown_db = mic_next_db(mdev);
	mdev->shutdown_cookie = mic_request_irq(mdev, mic_shutdown_db,
		"shutdown-interrupt", mdev, mdev->shutdown_db, MIC_INTR_DB);
	if (IS_ERR(mdev->shutdown_cookie)) {
		rc = PTR_ERR(mdev->shutdown_cookie);
		mutex_unlock(&mdev->mic_mutex);
		goto dp_uninit;
	}
	mutex_unlock(&mdev->mic_mutex);
	mic_bootparam_init(mdev);

	mic_create_debug_dir(mdev);
	cdev_init(&mdev->cdev, &mic_fops);
	mdev->cdev.owner = THIS_MODULE;
	rc = cdev_add(&mdev->cdev, MKDEV(MAJOR(g_mic_devno), mdev->id), 1);
	if (rc) {
		dev_err(&pdev->dev, "cdev_add err id %d rc %d\n", mdev->id, rc);
		goto cleanup_debug_dir;
	}
	return 0;
cleanup_debug_dir:
	mic_delete_debug_dir(mdev);
	mutex_lock(&mdev->mic_mutex);
	mic_free_irq(mdev, mdev->shutdown_cookie, mdev);
	mutex_unlock(&mdev->mic_mutex);
dp_uninit:
	mic_dp_uninit(mdev);
sysfs_put:
	sysfs_put(mdev->state_sysfs);
destroy_device:
	device_destroy(g_mic_class, MKDEV(MAJOR(g_mic_devno), mdev->id));
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
uninit_device:
	mic_device_uninit(mdev);
device_init_fail:
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

	mic_stop(mdev, false);
	cdev_del(&mdev->cdev);
	mic_delete_debug_dir(mdev);
	mutex_lock(&mdev->mic_mutex);
	mic_free_irq(mdev, mdev->shutdown_cookie, mdev);
	mutex_unlock(&mdev->mic_mutex);
	flush_work(&mdev->shutdown_work);
	mic_dp_uninit(mdev);
	sysfs_put(mdev->state_sysfs);
	device_destroy(g_mic_class, MKDEV(MAJOR(g_mic_devno), mdev->id));
	mic_smpt_uninit(mdev);
	mic_free_interrupts(mdev, pdev);
	iounmap(mdev->mmio.va);
	iounmap(mdev->aper.va);
	mic_device_uninit(mdev);
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

	ret = alloc_chrdev_region(&g_mic_devno, 0,
		MIC_MAX_NUM_DEVS, mic_driver_name);
	if (ret) {
		pr_err("alloc_chrdev_region failed ret %d\n", ret);
		goto error;
	}

	g_mic_class = class_create(THIS_MODULE, mic_driver_name);
	if (IS_ERR(g_mic_class)) {
		ret = PTR_ERR(g_mic_class);
		pr_err("class_create failed ret %d\n", ret);
		goto cleanup_chrdev;
	}

	mic_init_debugfs();
	ida_init(&g_mic_ida);
	ret = pci_register_driver(&mic_driver);
	if (ret) {
		pr_err("pci_register_driver failed ret %d\n", ret);
		goto cleanup_debugfs;
	}
	return ret;
cleanup_debugfs:
	mic_exit_debugfs();
	class_destroy(g_mic_class);
cleanup_chrdev:
	unregister_chrdev_region(g_mic_devno, MIC_MAX_NUM_DEVS);
error:
	return ret;
}

static void __exit mic_exit(void)
{
	pci_unregister_driver(&mic_driver);
	ida_destroy(&g_mic_ida);
	mic_exit_debugfs();
	class_destroy(g_mic_class);
	unregister_chrdev_region(g_mic_devno, MIC_MAX_NUM_DEVS);
}

module_init(mic_init);
module_exit(mic_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) MIC X100 Host driver");
MODULE_LICENSE("GPL v2");
