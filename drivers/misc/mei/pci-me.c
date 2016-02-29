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
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/uuid.h>
#include <linux/compat.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>

#include <linux/pm_runtime.h>

#include <linux/mei.h>

#include "mei_dev.h"
#include "client.h"
#include "hw-me-regs.h"
#include "hw-me.h"

/* mei_pci_tbl - PCI Device ID Table */
static const struct pci_device_id mei_me_pci_tbl[] = {
	{MEI_PCI_DEVICE(MEI_DEV_ID_82946GZ, mei_me_legacy_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_82G35, mei_me_legacy_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_82Q965, mei_me_legacy_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_82G965, mei_me_legacy_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_82GM965, mei_me_legacy_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_82GME965, mei_me_legacy_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_ICH9_82Q35, mei_me_legacy_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_ICH9_82G33, mei_me_legacy_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_ICH9_82Q33, mei_me_legacy_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_ICH9_82X38, mei_me_legacy_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_ICH9_3200, mei_me_legacy_cfg)},

	{MEI_PCI_DEVICE(MEI_DEV_ID_ICH9_6, mei_me_legacy_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_ICH9_7, mei_me_legacy_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_ICH9_8, mei_me_legacy_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_ICH9_9, mei_me_legacy_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_ICH9_10, mei_me_legacy_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_ICH9M_1, mei_me_legacy_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_ICH9M_2, mei_me_legacy_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_ICH9M_3, mei_me_legacy_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_ICH9M_4, mei_me_legacy_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_ICH10_1, mei_me_ich_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_ICH10_2, mei_me_ich_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_ICH10_3, mei_me_ich_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_ICH10_4, mei_me_ich_cfg)},

	{MEI_PCI_DEVICE(MEI_DEV_ID_IBXPK_1, mei_me_pch_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_IBXPK_2, mei_me_pch_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_CPT_1, mei_me_pch_cpt_pbg_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_PBG_1, mei_me_pch_cpt_pbg_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_PPT_1, mei_me_pch_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_PPT_2, mei_me_pch_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_PPT_3, mei_me_pch_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_LPT_H, mei_me_pch8_sps_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_LPT_W, mei_me_pch8_sps_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_LPT_LP, mei_me_pch8_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_LPT_HR, mei_me_pch8_sps_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_WPT_LP, mei_me_pch8_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_WPT_LP_2, mei_me_pch8_cfg)},

	{MEI_PCI_DEVICE(MEI_DEV_ID_SPT, mei_me_pch8_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_SPT_2, mei_me_pch8_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_SPT_H, mei_me_pch8_sps_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_SPT_H_2, mei_me_pch8_sps_cfg)},

	{MEI_PCI_DEVICE(MEI_DEV_ID_KBP, mei_me_pch8_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_KBP_2, mei_me_pch8_cfg)},

	{MEI_PCI_DEVICE(MEI_DEV_ID_BXT_M, mei_me_pch8_cfg)},
	{MEI_PCI_DEVICE(MEI_DEV_ID_APL_I, mei_me_pch8_cfg)},

	/* required last entry */
	{0, }
};

MODULE_DEVICE_TABLE(pci, mei_me_pci_tbl);

#ifdef CONFIG_PM
static inline void mei_me_set_pm_domain(struct mei_device *dev);
static inline void mei_me_unset_pm_domain(struct mei_device *dev);
#else
static inline void mei_me_set_pm_domain(struct mei_device *dev) {}
static inline void mei_me_unset_pm_domain(struct mei_device *dev) {}
#endif /* CONFIG_PM */

/**
 * mei_me_quirk_probe - probe for devices that doesn't valid ME interface
 *
 * @pdev: PCI device structure
 * @cfg: per generation config
 *
 * Return: true if ME Interface is valid, false otherwise
 */
static bool mei_me_quirk_probe(struct pci_dev *pdev,
				const struct mei_cfg *cfg)
{
	if (cfg->quirk_probe && cfg->quirk_probe(pdev)) {
		dev_info(&pdev->dev, "Device doesn't have valid ME Interface\n");
		return false;
	}

	return true;
}

/**
 * mei_me_probe - Device Initialization Routine
 *
 * @pdev: PCI device structure
 * @ent: entry in kcs_pci_tbl
 *
 * Return: 0 on success, <0 on failure.
 */
static int mei_me_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	const struct mei_cfg *cfg = (struct mei_cfg *)(ent->driver_data);
	struct mei_device *dev;
	struct mei_me_hw *hw;
	unsigned int irqflags;
	int err;


	if (!mei_me_quirk_probe(pdev, cfg))
		return -ENODEV;

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

	if (dma_set_mask(&pdev->dev, DMA_BIT_MASK(64)) ||
	    dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64))) {

		err = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
		if (err)
			err = dma_set_coherent_mask(&pdev->dev,
						    DMA_BIT_MASK(32));
	}
	if (err) {
		dev_err(&pdev->dev, "No usable DMA configuration, aborting\n");
		goto release_regions;
	}


	/* allocates and initializes the mei dev structure */
	dev = mei_me_dev_init(pdev, cfg);
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
	irqflags = pci_dev_msi_enabled(pdev) ? IRQF_ONESHOT : IRQF_SHARED;

	err = request_threaded_irq(pdev->irq,
			mei_me_irq_quick_handler,
			mei_me_irq_thread_handler,
			irqflags, KBUILD_MODNAME, dev);
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

	pm_runtime_set_autosuspend_delay(&pdev->dev, MEI_ME_RPM_TIMEOUT);
	pm_runtime_use_autosuspend(&pdev->dev);

	err = mei_register(dev, &pdev->dev);
	if (err)
		goto release_irq;

	pci_set_drvdata(pdev, dev);

	schedule_delayed_work(&dev->timer_work, HZ);

	/*
	* For not wake-able HW runtime pm framework
	* can't be used on pci device level.
	* Use domain runtime pm callbacks instead.
	*/
	if (!pci_dev_run_wake(pdev))
		mei_me_set_pm_domain(dev);

	if (mei_pg_is_enabled(dev))
		pm_runtime_put_noidle(&pdev->dev);

	dev_dbg(&pdev->dev, "initialization successful.\n");

	return 0;

release_irq:
	mei_cancel_work(dev);
	mei_disable_interrupts(dev);
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
	dev_err(&pdev->dev, "initialization failed.\n");
	return err;
}

/**
 * mei_me_remove - Device Removal Routine
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

	dev = pci_get_drvdata(pdev);
	if (!dev)
		return;

	if (mei_pg_is_enabled(dev))
		pm_runtime_get_noresume(&pdev->dev);

	hw = to_me_hw(dev);


	dev_dbg(&pdev->dev, "stop\n");
	mei_stop(dev);

	if (!pci_dev_run_wake(pdev))
		mei_me_unset_pm_domain(dev);

	/* disable interrupts */
	mei_disable_interrupts(dev);

	free_irq(pdev->irq, dev);
	pci_disable_msi(pdev);

	if (hw->mem_addr)
		pci_iounmap(pdev, hw->mem_addr);

	mei_deregister(dev);

	kfree(dev);

	pci_release_regions(pdev);
	pci_disable_device(pdev);


}
#ifdef CONFIG_PM_SLEEP
static int mei_me_pci_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct mei_device *dev = pci_get_drvdata(pdev);

	if (!dev)
		return -ENODEV;

	dev_dbg(&pdev->dev, "suspend\n");

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
	unsigned int irqflags;
	int err;

	dev = pci_get_drvdata(pdev);
	if (!dev)
		return -ENODEV;

	pci_enable_msi(pdev);

	irqflags = pci_dev_msi_enabled(pdev) ? IRQF_ONESHOT : IRQF_SHARED;

	/* request and enable interrupt */
	err = request_threaded_irq(pdev->irq,
			mei_me_irq_quick_handler,
			mei_me_irq_thread_handler,
			irqflags, KBUILD_MODNAME, dev);

	if (err) {
		dev_err(&pdev->dev, "request_threaded_irq failed: irq = %d.\n",
				pdev->irq);
		return err;
	}

	err = mei_restart(dev);
	if (err)
		return err;

	/* Start timer if stopped in suspend */
	schedule_delayed_work(&dev->timer_work, HZ);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM
static int mei_me_pm_runtime_idle(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct mei_device *dev;

	dev_dbg(&pdev->dev, "rpm: me: runtime_idle\n");

	dev = pci_get_drvdata(pdev);
	if (!dev)
		return -ENODEV;
	if (mei_write_is_idle(dev))
		pm_runtime_autosuspend(device);

	return -EBUSY;
}

static int mei_me_pm_runtime_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct mei_device *dev;
	int ret;

	dev_dbg(&pdev->dev, "rpm: me: runtime suspend\n");

	dev = pci_get_drvdata(pdev);
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);

	if (mei_write_is_idle(dev))
		ret = mei_me_pg_enter_sync(dev);
	else
		ret = -EAGAIN;

	mutex_unlock(&dev->device_lock);

	dev_dbg(&pdev->dev, "rpm: me: runtime suspend ret=%d\n", ret);

	return ret;
}

static int mei_me_pm_runtime_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct mei_device *dev;
	int ret;

	dev_dbg(&pdev->dev, "rpm: me: runtime resume\n");

	dev = pci_get_drvdata(pdev);
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);

	ret = mei_me_pg_exit_sync(dev);

	mutex_unlock(&dev->device_lock);

	dev_dbg(&pdev->dev, "rpm: me: runtime resume ret = %d\n", ret);

	return ret;
}

/**
 * mei_me_set_pm_domain - fill and set pm domain structure for device
 *
 * @dev: mei_device
 */
static inline void mei_me_set_pm_domain(struct mei_device *dev)
{
	struct pci_dev *pdev  = to_pci_dev(dev->dev);

	if (pdev->dev.bus && pdev->dev.bus->pm) {
		dev->pg_domain.ops = *pdev->dev.bus->pm;

		dev->pg_domain.ops.runtime_suspend = mei_me_pm_runtime_suspend;
		dev->pg_domain.ops.runtime_resume = mei_me_pm_runtime_resume;
		dev->pg_domain.ops.runtime_idle = mei_me_pm_runtime_idle;

		pdev->dev.pm_domain = &dev->pg_domain;
	}
}

/**
 * mei_me_unset_pm_domain - clean pm domain structure for device
 *
 * @dev: mei_device
 */
static inline void mei_me_unset_pm_domain(struct mei_device *dev)
{
	/* stop using pm callbacks if any */
	dev->dev->pm_domain = NULL;
}

static const struct dev_pm_ops mei_me_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mei_me_pci_suspend,
				mei_me_pci_resume)
	SET_RUNTIME_PM_OPS(
		mei_me_pm_runtime_suspend,
		mei_me_pm_runtime_resume,
		mei_me_pm_runtime_idle)
};

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
