// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2013-2020, Intel Corporation. All rights reserved.
 * Intel Management Engine Interface (Intel MEI) Linux driver
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>

#include <linux/mei.h>


#include "mei_dev.h"
#include "hw-txe.h"

static const struct pci_device_id mei_txe_pci_tbl[] = {
	{PCI_VDEVICE(INTEL, 0x0F18)}, /* Baytrail */
	{PCI_VDEVICE(INTEL, 0x2298)}, /* Cherrytrail */

	{0, }
};
MODULE_DEVICE_TABLE(pci, mei_txe_pci_tbl);

#ifdef CONFIG_PM
static inline void mei_txe_set_pm_domain(struct mei_device *dev);
static inline void mei_txe_unset_pm_domain(struct mei_device *dev);
#else
static inline void mei_txe_set_pm_domain(struct mei_device *dev) {}
static inline void mei_txe_unset_pm_domain(struct mei_device *dev) {}
#endif /* CONFIG_PM */

/**
 * mei_txe_probe - Device Initialization Routine
 *
 * @pdev: PCI device structure
 * @ent: entry in mei_txe_pci_tbl
 *
 * Return: 0 on success, <0 on failure.
 */
static int mei_txe_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct mei_device *dev;
	struct mei_txe_hw *hw;
	const int mask = BIT(SEC_BAR) | BIT(BRIDGE_BAR);
	int err;

	/* enable pci dev */
	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "failed to enable pci device.\n");
		goto end;
	}
	/* set PCI host mastering  */
	pci_set_master(pdev);
	/* pci request regions and mapping IO device memory for mei driver */
	err = pcim_iomap_regions(pdev, mask, KBUILD_MODNAME);
	if (err) {
		dev_err(&pdev->dev, "failed to get pci regions.\n");
		goto end;
	}

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(36));
	if (err) {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, "No suitable DMA available.\n");
			goto end;
		}
	}

	/* allocates and initializes the mei dev structure */
	dev = mei_txe_dev_init(pdev);
	if (!dev) {
		err = -ENOMEM;
		goto end;
	}
	hw = to_txe_hw(dev);
	hw->mem_addr = pcim_iomap_table(pdev);

	pci_enable_msi(pdev);

	/* clear spurious interrupts */
	mei_clear_interrupts(dev);

	/* request and enable interrupt  */
	if (pci_dev_msi_enabled(pdev))
		err = request_threaded_irq(pdev->irq,
			NULL,
			mei_txe_irq_thread_handler,
			IRQF_ONESHOT, KBUILD_MODNAME, dev);
	else
		err = request_threaded_irq(pdev->irq,
			mei_txe_irq_quick_handler,
			mei_txe_irq_thread_handler,
			IRQF_SHARED, KBUILD_MODNAME, dev);
	if (err) {
		dev_err(&pdev->dev, "mei: request_threaded_irq failure. irq = %d\n",
			pdev->irq);
		goto end;
	}

	if (mei_start(dev)) {
		dev_err(&pdev->dev, "init hw failure.\n");
		err = -ENODEV;
		goto release_irq;
	}

	pm_runtime_set_autosuspend_delay(&pdev->dev, MEI_TXI_RPM_TIMEOUT);
	pm_runtime_use_autosuspend(&pdev->dev);

	err = mei_register(dev, &pdev->dev);
	if (err)
		goto stop;

	pci_set_drvdata(pdev, dev);

	/*
	 * MEI requires to resume from runtime suspend mode
	 * in order to perform link reset flow upon system suspend.
	 */
	dev_pm_set_driver_flags(&pdev->dev, DPM_FLAG_NO_DIRECT_COMPLETE);

	/*
	 * TXE maps runtime suspend/resume to own power gating states,
	 * hence we need to go around native PCI runtime service which
	 * eventually brings the device into D3cold/hot state.
	 * But the TXE device cannot wake up from D3 unlike from own
	 * power gating. To get around PCI device native runtime pm,
	 * TXE uses runtime pm domain handlers which take precedence.
	 */
	mei_txe_set_pm_domain(dev);

	pm_runtime_put_noidle(&pdev->dev);

	return 0;

stop:
	mei_stop(dev);
release_irq:
	mei_cancel_work(dev);
	mei_disable_interrupts(dev);
	free_irq(pdev->irq, dev);
end:
	dev_err(&pdev->dev, "initialization failed.\n");
	return err;
}

/**
 * mei_txe_shutdown- Device Shutdown Routine
 *
 * @pdev: PCI device structure
 *
 *  mei_txe_shutdown is called from the reboot notifier
 *  it's a simplified version of remove so we go down
 *  faster.
 */
static void mei_txe_shutdown(struct pci_dev *pdev)
{
	struct mei_device *dev;

	dev = pci_get_drvdata(pdev);
	if (!dev)
		return;

	dev_dbg(&pdev->dev, "shutdown\n");
	mei_stop(dev);

	mei_txe_unset_pm_domain(dev);

	mei_disable_interrupts(dev);
	free_irq(pdev->irq, dev);
}

/**
 * mei_txe_remove - Device Removal Routine
 *
 * @pdev: PCI device structure
 *
 * mei_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.
 */
static void mei_txe_remove(struct pci_dev *pdev)
{
	struct mei_device *dev;

	dev = pci_get_drvdata(pdev);
	if (!dev) {
		dev_err(&pdev->dev, "mei: dev == NULL\n");
		return;
	}

	pm_runtime_get_noresume(&pdev->dev);

	mei_stop(dev);

	mei_txe_unset_pm_domain(dev);

	mei_disable_interrupts(dev);
	free_irq(pdev->irq, dev);

	mei_deregister(dev);
}


#ifdef CONFIG_PM_SLEEP
static int mei_txe_pci_suspend(struct device *device)
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

static int mei_txe_pci_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct mei_device *dev;
	int err;

	dev = pci_get_drvdata(pdev);
	if (!dev)
		return -ENODEV;

	pci_enable_msi(pdev);

	mei_clear_interrupts(dev);

	/* request and enable interrupt */
	if (pci_dev_msi_enabled(pdev))
		err = request_threaded_irq(pdev->irq,
			NULL,
			mei_txe_irq_thread_handler,
			IRQF_ONESHOT, KBUILD_MODNAME, dev);
	else
		err = request_threaded_irq(pdev->irq,
			mei_txe_irq_quick_handler,
			mei_txe_irq_thread_handler,
			IRQF_SHARED, KBUILD_MODNAME, dev);
	if (err) {
		dev_err(&pdev->dev, "request_threaded_irq failed: irq = %d.\n",
				pdev->irq);
		return err;
	}

	err = mei_restart(dev);

	return err;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM
static int mei_txe_pm_runtime_idle(struct device *device)
{
	struct mei_device *dev;

	dev_dbg(device, "rpm: txe: runtime_idle\n");

	dev = dev_get_drvdata(device);
	if (!dev)
		return -ENODEV;
	if (mei_write_is_idle(dev))
		pm_runtime_autosuspend(device);

	return -EBUSY;
}
static int mei_txe_pm_runtime_suspend(struct device *device)
{
	struct mei_device *dev;
	int ret;

	dev_dbg(device, "rpm: txe: runtime suspend\n");

	dev = dev_get_drvdata(device);
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);

	if (mei_write_is_idle(dev))
		ret = mei_txe_aliveness_set_sync(dev, 0);
	else
		ret = -EAGAIN;

	/* keep irq on we are staying in D0 */

	dev_dbg(device, "rpm: txe: runtime suspend ret=%d\n", ret);

	mutex_unlock(&dev->device_lock);

	if (ret && ret != -EAGAIN)
		schedule_work(&dev->reset_work);

	return ret;
}

static int mei_txe_pm_runtime_resume(struct device *device)
{
	struct mei_device *dev;
	int ret;

	dev_dbg(device, "rpm: txe: runtime resume\n");

	dev = dev_get_drvdata(device);
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);

	mei_enable_interrupts(dev);

	ret = mei_txe_aliveness_set_sync(dev, 1);

	mutex_unlock(&dev->device_lock);

	dev_dbg(device, "rpm: txe: runtime resume ret = %d\n", ret);

	if (ret)
		schedule_work(&dev->reset_work);

	return ret;
}

/**
 * mei_txe_set_pm_domain - fill and set pm domain structure for device
 *
 * @dev: mei_device
 */
static inline void mei_txe_set_pm_domain(struct mei_device *dev)
{
	struct pci_dev *pdev  = to_pci_dev(dev->dev);

	if (pdev->dev.bus && pdev->dev.bus->pm) {
		dev->pg_domain.ops = *pdev->dev.bus->pm;

		dev->pg_domain.ops.runtime_suspend = mei_txe_pm_runtime_suspend;
		dev->pg_domain.ops.runtime_resume = mei_txe_pm_runtime_resume;
		dev->pg_domain.ops.runtime_idle = mei_txe_pm_runtime_idle;

		dev_pm_domain_set(&pdev->dev, &dev->pg_domain);
	}
}

/**
 * mei_txe_unset_pm_domain - clean pm domain structure for device
 *
 * @dev: mei_device
 */
static inline void mei_txe_unset_pm_domain(struct mei_device *dev)
{
	/* stop using pm callbacks if any */
	dev_pm_domain_set(dev->dev, NULL);
}

static const struct dev_pm_ops mei_txe_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mei_txe_pci_suspend,
				mei_txe_pci_resume)
	SET_RUNTIME_PM_OPS(
		mei_txe_pm_runtime_suspend,
		mei_txe_pm_runtime_resume,
		mei_txe_pm_runtime_idle)
};

#define MEI_TXE_PM_OPS	(&mei_txe_pm_ops)
#else
#define MEI_TXE_PM_OPS	NULL
#endif /* CONFIG_PM */

/*
 *  PCI driver structure
 */
static struct pci_driver mei_txe_driver = {
	.name = KBUILD_MODNAME,
	.id_table = mei_txe_pci_tbl,
	.probe = mei_txe_probe,
	.remove = mei_txe_remove,
	.shutdown = mei_txe_shutdown,
	.driver.pm = MEI_TXE_PM_OPS,
};

module_pci_driver(mei_txe_driver);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) Trusted Execution Environment Interface");
MODULE_LICENSE("GPL v2");
