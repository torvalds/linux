/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2003-2012, Intel Corporation.
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/aio.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/uuid.h>
#include <linux/compat.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>

#include <linux/mei.h>

#include "mei_dev.h"
#include "hw-me.h"
#include "client.h"

/* AMT device is a singleton on the platform */
static struct pci_dev *mei_pdev;

/* mei_pci_tbl - PCI Device ID Table */
static DEFINE_PCI_DEVICE_TABLE(mei_me_pci_tbl) = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_82946GZ)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_82G35)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_82Q965)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_82G965)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_82GM965)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_82GME965)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_82Q35)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_82G33)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_82Q33)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_82X38)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_3200)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_6)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_7)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_8)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_9)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9_10)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9M_1)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9M_2)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9M_3)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH9M_4)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH10_1)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH10_2)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH10_3)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_ICH10_4)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_IBXPK_1)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_IBXPK_2)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_CPT_1)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_PBG_1)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_PPT_1)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_PPT_2)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_PPT_3)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_LPT)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, MEI_DEV_ID_LPT_LP)},

	/* required last entry */
	{0, }
};

MODULE_DEVICE_TABLE(pci, mei_me_pci_tbl);

static DEFINE_MUTEX(mei_mutex);

/**
 * mei_quirk_probe - probe for devices that doesn't valid ME interface
 * @pdev: PCI device structure
 * @ent: entry into pci_device_table
 *
 * returns true if ME Interface is valid, false otherwise
 */
static bool mei_me_quirk_probe(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	u32 reg;
	if (ent->device == MEI_DEV_ID_PBG_1) {
		pci_read_config_dword(pdev, 0x48, &reg);
		/* make sure that bit 9 is up and bit 10 is down */
		if ((reg & 0x600) == 0x200) {
			dev_info(&pdev->dev, "Device doesn't have valid ME Interface\n");
			return false;
		}
	}
	return true;
}
/**
 * mei_probe - Device Initialization Routine
 *
 * @pdev: PCI device structure
 * @ent: entry in kcs_pci_tbl
 *
 * returns 0 on success, <0 on failure.
 */
static int mei_me_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct mei_device *dev;
	struct mei_me_hw *hw;
	int err;

	mutex_lock(&mei_mutex);

	if (!mei_me_quirk_probe(pdev, ent)) {
		err = -ENODEV;
		goto end;
	}

	if (mei_pdev) {
		err = -EEXIST;
		goto end;
	}
	/* enable pci dev */
	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "failed to enable pci device.\n");
		goto end;
	}
	/* set PCI host mastering  */
	pci_set_master(pdev);
	/* pci request regions for mei driver */
	err = pci_request_regions(pdev, KBUILD_MODNAME);
	if (err) {
		dev_err(&pdev->dev, "failed to get pci regions.\n");
		goto disable_device;
	}
	/* allocates and initializes the mei dev structure */
	dev = mei_me_dev_init(pdev);
	if (!dev) {
		err = -ENOMEM;
		goto release_regions;
	}
	hw = to_me_hw(dev);
	/* mapping  IO device memory */
	hw->mem_addr = pci_iomap(pdev, 0, 0);
	if (!hw->mem_addr) {
		dev_err(&pdev->dev, "mapping I/O device memory failure.\n");
		err = -ENOMEM;
		goto free_device;
	}
	pci_enable_msi(pdev);

	 /* request and enable interrupt */
	if (pci_dev_msi_enabled(pdev))
		err = request_threaded_irq(pdev->irq,
			NULL,
			mei_me_irq_thread_handler,
			IRQF_ONESHOT, KBUILD_MODNAME, dev);
	else
		err = request_threaded_irq(pdev->irq,
			mei_me_irq_quick_handler,
			mei_me_irq_thread_handler,
			IRQF_SHARED, KBUILD_MODNAME, dev);

	if (err) {
		dev_err(&pdev->dev, "request_threaded_irq failure. irq = %d\n",
		       pdev->irq);
		goto disable_msi;
	}

	if (mei_start(dev)) {
		dev_err(&pdev->dev, "init hw failure.\n");
		err = -ENODEV;
		goto release_irq;
	}

	err = mei_register(dev);
	if (err)
		goto release_irq;

	mei_pdev = pdev;
	pci_set_drvdata(pdev, dev);

	schedule_delayed_work(&dev->timer_work, HZ);

	mutex_unlock(&mei_mutex);

	pr_debug("initialization successful.\n");

	return 0;

release_irq:
	mei_disable_interrupts(dev);
	flush_scheduled_work();
	free_irq(pdev->irq, dev);
disable_msi:
	pci_disable_msi(pdev);
	pci_iounmap(pdev, hw->mem_addr);
free_device:
	kfree(dev);
release_regions:
	pci_release_regions(pdev);
disable_device:
	pci_disable_device(pdev);
end:
	mutex_unlock(&mei_mutex);
	dev_err(&pdev->dev, "initialization failed.\n");
	return err;
}

/**
 * mei_remove - Device Removal Routine
 *
 * @pdev: PCI device structure
 *
 * mei_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.
 */
static void mei_me_remove(struct pci_dev *pdev)
{
	struct mei_device *dev;
	struct mei_me_hw *hw;

	if (mei_pdev != pdev)
		return;

	dev = pci_get_drvdata(pdev);
	if (!dev)
		return;

	hw = to_me_hw(dev);


	dev_err(&pdev->dev, "stop\n");
	mei_stop(dev);

	mei_pdev = NULL;

	/* disable interrupts */
	mei_disable_interrupts(dev);

	free_irq(pdev->irq, dev);
	pci_disable_msi(pdev);
	pci_set_drvdata(pdev, NULL);

	if (hw->mem_addr)
		pci_iounmap(pdev, hw->mem_addr);

	mei_deregister(dev);

	kfree(dev);

	pci_release_regions(pdev);
	pci_disable_device(pdev);


}
#ifdef CONFIG_PM
static int mei_me_pci_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct mei_device *dev = pci_get_drvdata(pdev);

	if (!dev)
		return -ENODEV;

	dev_err(&pdev->dev, "suspend\n");

	mei_stop(dev);

	mei_disable_interrupts(dev);

	free_irq(pdev->irq, dev);
	pci_disable_msi(pdev);

	return 0;
}

static int mei_me_pci_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct mei_device *dev;
	int err;

	dev = pci_get_drvdata(pdev);
	if (!dev)
		return -ENODEV;

	pci_enable_msi(pdev);

	/* request and enable interrupt */
	if (pci_dev_msi_enabled(pdev))
		err = request_threaded_irq(pdev->irq,
			NULL,
			mei_me_irq_thread_handler,
			IRQF_ONESHOT, KBUILD_MODNAME, dev);
	else
		err = request_threaded_irq(pdev->irq,
			mei_me_irq_quick_handler,
			mei_me_irq_thread_handler,
			IRQF_SHARED, KBUILD_MODNAME, dev);

	if (err) {
		dev_err(&pdev->dev, "request_threaded_irq failed: irq = %d.\n",
				pdev->irq);
		return err;
	}

	mutex_lock(&dev->device_lock);
	dev->dev_state = MEI_DEV_POWER_UP;
	mei_reset(dev, 1);
	mutex_unlock(&dev->device_lock);

	/* Start timer if stopped in suspend */
	schedule_delayed_work(&dev->timer_work, HZ);

	return err;
}
static SIMPLE_DEV_PM_OPS(mei_me_pm_ops, mei_me_pci_suspend, mei_me_pci_resume);
#define MEI_ME_PM_OPS	(&mei_me_pm_ops)
#else
#define MEI_ME_PM_OPS	NULL
#endif /* CONFIG_PM */
/*
 *  PCI driver structure
 */
static struct pci_driver mei_me_driver = {
	.name = KBUILD_MODNAME,
	.id_table = mei_me_pci_tbl,
	.probe = mei_me_probe,
	.remove = mei_me_remove,
	.shutdown = mei_me_remove,
	.driver.pm = MEI_ME_PM_OPS,
};

module_pci_driver(mei_me_driver);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) Management Engine Interface");
MODULE_LICENSE("GPL v2");
