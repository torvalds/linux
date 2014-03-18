/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2013-2014, Intel Corporation.
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
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/uuid.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/pm_runtime.h>

#include <linux/mei.h>


#include "mei_dev.h"
#include "hw-txe.h"

static const struct pci_device_id mei_txe_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x0F18)}, /* Baytrail */
	{0, }
};
MODULE_DEVICE_TABLE(pci, mei_txe_pci_tbl);


static void mei_txe_pci_iounmap(struct pci_dev *pdev, struct mei_txe_hw *hw)
{
	int i;
	for (i = SEC_BAR; i < NUM_OF_MEM_BARS; i++) {
		if (hw->mem_addr[i]) {
			pci_iounmap(pdev, hw->mem_addr[i]);
			hw->mem_addr[i] = NULL;
		}
	}
}
/**
 * mei_probe - Device Initialization Routine
 *
 * @pdev: PCI device structure
 * @ent: entry in mei_txe_pci_tbl
 *
 * returns 0 on success, <0 on failure.
 */
static int mei_txe_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct mei_device *dev;
	struct mei_txe_hw *hw;
	int err;
	int i;

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

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(36));
	if (err) {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, "No suitable DMA available.\n");
			goto release_regions;
		}
	}

	/* allocates and initializes the mei dev structure */
	dev = mei_txe_dev_init(pdev);
	if (!dev) {
		err = -ENOMEM;
		goto release_regions;
	}
	hw = to_txe_hw(dev);

	/* mapping  IO device memory */
	for (i = SEC_BAR; i < NUM_OF_MEM_BARS; i++) {
		hw->mem_addr[i] = pci_iomap(pdev, i, 0);
		if (!hw->mem_addr[i]) {
			dev_err(&pdev->dev, "mapping I/O device memory failure.\n");
			err = -ENOMEM;
			goto free_device;
		}
	}


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
		goto free_device;
	}

	if (mei_start(dev)) {
		dev_err(&pdev->dev, "init hw failure.\n");
		err = -ENODEV;
		goto release_irq;
	}

	pm_runtime_set_autosuspend_delay(&pdev->dev, MEI_TXI_RPM_TIMEOUT);
	pm_runtime_use_autosuspend(&pdev->dev);

	err = mei_register(dev);
	if (err)
		goto release_irq;

	pci_set_drvdata(pdev, dev);

	pm_runtime_put_noidle(&pdev->dev);

	return 0;

release_irq:

	mei_cancel_work(dev);

	/* disable interrupts */
	mei_disable_interrupts(dev);

	free_irq(pdev->irq, dev);
	pci_disable_msi(pdev);

free_device:
	mei_txe_pci_iounmap(pdev, hw);

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
 * mei_remove - Device Removal Routine
 *
 * @pdev: PCI device structure
 *
 * mei_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.
 */
static void mei_txe_remove(struct pci_dev *pdev)
{
	struct mei_device *dev;
	struct mei_txe_hw *hw;

	dev = pci_get_drvdata(pdev);
	if (!dev) {
		dev_err(&pdev->dev, "mei: dev =NULL\n");
		return;
	}

	pm_runtime_get_noresume(&pdev->dev);

	hw = to_txe_hw(dev);

	mei_stop(dev);

	/* disable interrupts */
	mei_disable_interrupts(dev);
	free_irq(pdev->irq, dev);
	pci_disable_msi(pdev);

	pci_set_drvdata(pdev, NULL);

	mei_txe_pci_iounmap(pdev, hw);

	mei_deregister(dev);

	kfree(dev);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
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

#ifdef CONFIG_PM_RUNTIME
static int mei_txe_pm_runtime_idle(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct mei_device *dev;

	dev_dbg(&pdev->dev, "rpm: txe: runtime_idle\n");

	dev = pci_get_drvdata(pdev);
	if (!dev)
		return -ENODEV;
	if (mei_write_is_idle(dev))
		pm_schedule_suspend(device, MEI_TXI_RPM_TIMEOUT * 2);

	return -EBUSY;
}
static int mei_txe_pm_runtime_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct mei_device *dev;
	int ret;

	dev_dbg(&pdev->dev, "rpm: txe: runtime suspend\n");

	dev = pci_get_drvdata(pdev);
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);

	if (mei_write_is_idle(dev))
		ret = mei_txe_aliveness_set_sync(dev, 0);
	else
		ret = -EAGAIN;

	/*
	 * If everything is okay we're about to enter PCI low
	 * power state (D3) therefor we need to disable the
	 * interrupts towards host.
	 * However if device is not wakeable we do not enter
	 * D-low state and we need to keep the interrupt kicking
	 */
	 if (!ret && pci_dev_run_wake(pdev))
		mei_disable_interrupts(dev);

	dev_dbg(&pdev->dev, "rpm: txe: runtime suspend ret=%d\n", ret);

	mutex_unlock(&dev->device_lock);
	return ret;
}

static int mei_txe_pm_runtime_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct mei_device *dev;
	int ret;

	dev_dbg(&pdev->dev, "rpm: txe: runtime resume\n");

	dev = pci_get_drvdata(pdev);
	if (!dev)
		return -ENODEV;

	mutex_lock(&dev->device_lock);

	mei_enable_interrupts(dev);

	ret = mei_txe_aliveness_set_sync(dev, 1);

	mutex_unlock(&dev->device_lock);

	dev_dbg(&pdev->dev, "rpm: txe: runtime resume ret = %d\n", ret);

	return ret;
}
#endif /* CONFIG_PM_RUNTIME */

#ifdef CONFIG_PM
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
	.shutdown = mei_txe_remove,
	.driver.pm = MEI_TXE_PM_OPS,
};

module_pci_driver(mei_txe_driver);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) Trusted Execution Environment Interface");
MODULE_LICENSE("GPL v2");
