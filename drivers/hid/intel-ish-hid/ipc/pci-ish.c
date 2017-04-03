/*
 * PCI glue for ISHTP provider device (ISH) driver
 *
 * Copyright (c) 2014-2016, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#define CREATE_TRACE_POINTS
#include <trace/events/intel_ish.h>
#include "ishtp-dev.h"
#include "hw-ish.h"

static const struct pci_device_id ish_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, CHV_DEVICE_ID)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, BXT_Ax_DEVICE_ID)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, BXT_Bx_DEVICE_ID)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, APL_Ax_DEVICE_ID)},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, SPT_Ax_DEVICE_ID)},
	{0, }
};
MODULE_DEVICE_TABLE(pci, ish_pci_tbl);

/**
 * ish_event_tracer() - Callback function to dump trace messages
 * @dev:	ishtp device
 * @format:	printf style format
 *
 * Callback to direct log messages to Linux trace buffers
 */
static __printf(2, 3)
void ish_event_tracer(struct ishtp_device *dev, const char *format, ...)
{
	if (trace_ishtp_dump_enabled()) {
		va_list args;
		char tmp_buf[100];

		va_start(args, format);
		vsnprintf(tmp_buf, sizeof(tmp_buf), format, args);
		va_end(args);

		trace_ishtp_dump(tmp_buf);
	}
}

/**
 * ish_init() - Init function
 * @dev:	ishtp device
 *
 * This function initialize wait queues for suspend/resume and call
 * calls hadware initialization function. This will initiate
 * startup sequence
 *
 * Return: 0 for success or error code for failure
 */
static int ish_init(struct ishtp_device *dev)
{
	int ret;

	/* Set the state of ISH HW to start */
	ret = ish_hw_start(dev);
	if (ret) {
		dev_err(dev->devc, "ISH: hw start failed.\n");
		return ret;
	}

	/* Start the inter process communication to ISH processor */
	ret = ishtp_start(dev);
	if (ret) {
		dev_err(dev->devc, "ISHTP: Protocol init failed.\n");
		return ret;
	}

	return 0;
}

/**
 * ish_probe() - PCI driver probe callback
 * @pdev:	pci device
 * @ent:	pci device id
 *
 * Initialize PCI function, setup interrupt and call for ISH initialization
 *
 * Return: 0 for success or error code for failure
 */
static int ish_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct ishtp_device *dev;
	struct ish_hw *hw;
	int	ret;

	/* enable pci dev */
	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "ISH: Failed to enable PCI device\n");
		return ret;
	}

	/* set PCI host mastering */
	pci_set_master(pdev);

	/* pci request regions for ISH driver */
	ret = pci_request_regions(pdev, KBUILD_MODNAME);
	if (ret) {
		dev_err(&pdev->dev, "ISH: Failed to get PCI regions\n");
		goto disable_device;
	}

	/* allocates and initializes the ISH dev structure */
	dev = ish_dev_init(pdev);
	if (!dev) {
		ret = -ENOMEM;
		goto release_regions;
	}
	hw = to_ish_hw(dev);
	dev->print_log = ish_event_tracer;

	/* mapping IO device memory */
	hw->mem_addr = pci_iomap(pdev, 0, 0);
	if (!hw->mem_addr) {
		dev_err(&pdev->dev, "ISH: mapping I/O range failure\n");
		ret = -ENOMEM;
		goto free_device;
	}

	dev->pdev = pdev;

	pdev->dev_flags |= PCI_DEV_FLAGS_NO_D3;

	/* request and enable interrupt */
	ret = request_irq(pdev->irq, ish_irq_handler, IRQF_SHARED,
			  KBUILD_MODNAME, dev);
	if (ret) {
		dev_err(&pdev->dev, "ISH: request IRQ failure (%d)\n",
			pdev->irq);
		goto free_device;
	}

	dev_set_drvdata(dev->devc, dev);

	init_waitqueue_head(&dev->suspend_wait);
	init_waitqueue_head(&dev->resume_wait);

	ret = ish_init(dev);
	if (ret)
		goto free_irq;

	return 0;

free_irq:
	free_irq(pdev->irq, dev);
free_device:
	pci_iounmap(pdev, hw->mem_addr);
	kfree(dev);
release_regions:
	pci_release_regions(pdev);
disable_device:
	pci_clear_master(pdev);
	pci_disable_device(pdev);
	dev_err(&pdev->dev, "ISH: PCI driver initialization failed.\n");

	return ret;
}

/**
 * ish_remove() - PCI driver remove callback
 * @pdev:	pci device
 *
 * This function does cleanup of ISH on pci remove callback
 */
static void ish_remove(struct pci_dev *pdev)
{
	struct ishtp_device *ishtp_dev = pci_get_drvdata(pdev);
	struct ish_hw *hw = to_ish_hw(ishtp_dev);

	ishtp_bus_remove_all_clients(ishtp_dev, false);
	ish_device_disable(ishtp_dev);

	free_irq(pdev->irq, ishtp_dev);
	pci_iounmap(pdev, hw->mem_addr);
	pci_release_regions(pdev);
	pci_clear_master(pdev);
	pci_disable_device(pdev);
	kfree(ishtp_dev);
}

#ifdef CONFIG_PM
static struct device *ish_resume_device;

/* 50ms to get resume response */
#define WAIT_FOR_RESUME_ACK_MS		50

/**
 * ish_resume_handler() - Work function to complete resume
 * @work:	work struct
 *
 * The resume work function to complete resume function asynchronously.
 * There are two resume paths, one where ISH is not powered off,
 * in that case a simple resume message is enough, others we need
 * a reset sequence.
 */
static void ish_resume_handler(struct work_struct *work)
{
	struct pci_dev *pdev = to_pci_dev(ish_resume_device);
	struct ishtp_device *dev = pci_get_drvdata(pdev);
	uint32_t fwsts;
	int ret;

	/* Get ISH FW status */
	fwsts = IPC_GET_ISH_FWSTS(dev->ops->get_fw_status(dev));

	/*
	 * If currently, in ISH FW, sensor app is loaded or beyond that,
	 * it means ISH isn't powered off, in this case, send a resume message.
	 */
	if (fwsts >= FWSTS_SENSOR_APP_LOADED) {
		ishtp_send_resume(dev);

		/* Waiting to get resume response */
		if (dev->resume_flag)
			ret = wait_event_interruptible_timeout(dev->resume_wait,
				!dev->resume_flag,
				msecs_to_jiffies(WAIT_FOR_RESUME_ACK_MS));
	}

	/*
	 * If in ISH FW, sensor app isn't loaded yet, or no resume response.
	 * That means this platform is not S0ix compatible, or something is
	 * wrong with ISH FW. So on resume, full reboot of ISH processor will
	 * happen, so need to go through init sequence again.
	 */
	if (dev->resume_flag)
		ish_init(dev);
}

/**
 * ish_suspend() - ISH suspend callback
 * @device:	device pointer
 *
 * ISH suspend callback
 *
 * Return: 0 to the pm core
 */
static int ish_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct ishtp_device *dev = pci_get_drvdata(pdev);

	enable_irq_wake(pdev->irq);
	/*
	 * If previous suspend hasn't been asnwered then ISH is likely dead,
	 * don't attempt nested notification
	 */
	if (dev->suspend_flag)
		return	0;

	dev->resume_flag = 0;
	dev->suspend_flag = 1;
	ishtp_send_suspend(dev);

	/* 25 ms should be enough for live ISH to flush all IPC buf */
	if (dev->suspend_flag)
		wait_event_interruptible_timeout(dev->suspend_wait,
						 !dev->suspend_flag,
						  msecs_to_jiffies(25));

	return 0;
}

static DECLARE_WORK(resume_work, ish_resume_handler);
/**
 * ish_resume() - ISH resume callback
 * @device:	device pointer
 *
 * ISH resume callback
 *
 * Return: 0 to the pm core
 */
static int ish_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct ishtp_device *dev = pci_get_drvdata(pdev);

	ish_resume_device = device;
	dev->resume_flag = 1;

	disable_irq_wake(pdev->irq);
	schedule_work(&resume_work);

	return 0;
}

static const struct dev_pm_ops ish_pm_ops = {
	.suspend = ish_suspend,
	.resume = ish_resume,
};
#define ISHTP_ISH_PM_OPS	(&ish_pm_ops)
#else
#define ISHTP_ISH_PM_OPS	NULL
#endif /* CONFIG_PM */

static struct pci_driver ish_driver = {
	.name = KBUILD_MODNAME,
	.id_table = ish_pci_tbl,
	.probe = ish_probe,
	.remove = ish_remove,
	.driver.pm = ISHTP_ISH_PM_OPS,
};

module_pci_driver(ish_driver);

/* Original author */
MODULE_AUTHOR("Daniel Drubin <daniel.drubin@intel.com>");
/* Adoption to upstream Linux kernel */
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");

MODULE_DESCRIPTION("Intel(R) Integrated Sensor Hub PCI Device Driver");
MODULE_LICENSE("GPL");
