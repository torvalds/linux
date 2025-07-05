// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCI glue for ISHTP provider device (ISH) driver
 *
 * Copyright (c) 2014-2016, Intel Corporation.
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/suspend.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#define CREATE_TRACE_POINTS
#include <trace/events/intel_ish.h>
#include "ishtp-dev.h"
#include "hw-ish.h"

enum ishtp_driver_data_index {
	ISHTP_DRIVER_DATA_NONE,
	ISHTP_DRIVER_DATA_LNL_M,
	ISHTP_DRIVER_DATA_PTL,
	ISHTP_DRIVER_DATA_WCL,
};

#define ISH_FW_GEN_LNL_M "lnlm"
#define ISH_FW_GEN_PTL "ptl"
#define ISH_FW_GEN_WCL "wcl"

#define ISH_FIRMWARE_PATH(gen) "intel/ish/ish_" gen ".bin"
#define ISH_FIRMWARE_PATH_ALL "intel/ish/ish_*.bin"

static struct ishtp_driver_data ishtp_driver_data[] = {
	[ISHTP_DRIVER_DATA_LNL_M] = {
		.fw_generation = ISH_FW_GEN_LNL_M,
	},
	[ISHTP_DRIVER_DATA_PTL] = {
		.fw_generation = ISH_FW_GEN_PTL,
	},
	[ISHTP_DRIVER_DATA_WCL] = {
		.fw_generation = ISH_FW_GEN_WCL,
	},
};

static const struct pci_device_id ish_pci_tbl[] = {
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_CHV)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_BXT_Ax)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_BXT_Bx)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_APL_Ax)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_SPT_Ax)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_CNL_Ax)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_GLK_Ax)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_CNL_H)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_ICL_MOBILE)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_SPT_H)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_CML_LP)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_CMP_H)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_EHL_Ax)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_TGL_LP)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_TGL_H)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_ADL_S)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_ADL_P)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_ADL_N)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_RPL_S)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_MTL_P)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_ARL_H)},
	{PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_ISH_ARL_S)},
	{PCI_DEVICE_DATA(INTEL, ISH_LNL_M, ISHTP_DRIVER_DATA_LNL_M)},
	{PCI_DEVICE_DATA(INTEL, ISH_PTL_H, ISHTP_DRIVER_DATA_PTL)},
	{PCI_DEVICE_DATA(INTEL, ISH_PTL_P, ISHTP_DRIVER_DATA_PTL)},
	{PCI_DEVICE_DATA(INTEL, ISH_WCL, ISHTP_DRIVER_DATA_WCL)},
	{}
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

static const struct pci_device_id ish_invalid_pci_ids[] = {
	/* Mehlow platform special pci ids */
	{PCI_VDEVICE(INTEL, 0xA309)},
	{PCI_VDEVICE(INTEL, 0xA30A)},
	{}
};

static inline bool ish_should_enter_d0i3(struct pci_dev *pdev)
{
	return !pm_suspend_via_firmware() || pdev->device == PCI_DEVICE_ID_INTEL_ISH_CHV;
}

static inline bool ish_should_leave_d0i3(struct pci_dev *pdev)
{
	return !pm_resume_via_firmware() || pdev->device == PCI_DEVICE_ID_INTEL_ISH_CHV;
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
	int ret;
	struct ish_hw *hw;
	unsigned long irq_flag = 0;
	struct ishtp_device *ishtp;
	struct device *dev = &pdev->dev;

	/* Check for invalid platforms for ISH support */
	if (pci_dev_present(ish_invalid_pci_ids))
		return -ENODEV;

	/* enable pci dev */
	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(dev, "ISH: Failed to enable PCI device\n");
		return ret;
	}

	/* set PCI host mastering */
	pci_set_master(pdev);

	/* pci request regions for ISH driver */
	ret = pcim_iomap_regions(pdev, 1 << 0, KBUILD_MODNAME);
	if (ret) {
		dev_err(dev, "ISH: Failed to get PCI regions\n");
		return ret;
	}

	/* allocates and initializes the ISH dev structure */
	ishtp = ish_dev_init(pdev);
	if (!ishtp) {
		ret = -ENOMEM;
		return ret;
	}
	hw = to_ish_hw(ishtp);
	ishtp->print_log = ish_event_tracer;
	ishtp->driver_data = &ishtp_driver_data[ent->driver_data];

	/* mapping IO device memory */
	hw->mem_addr = pcim_iomap_table(pdev)[0];
	ishtp->pdev = pdev;

	/* request and enable interrupt */
	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0) {
		dev_err(dev, "ISH: Failed to allocate IRQ vectors\n");
		return ret;
	}

	if (!pdev->msi_enabled && !pdev->msix_enabled)
		irq_flag = IRQF_SHARED;

	ret = devm_request_irq(dev, pdev->irq, ish_irq_handler,
			       irq_flag, KBUILD_MODNAME, ishtp);
	if (ret) {
		dev_err(dev, "ISH: request IRQ %d failed\n", pdev->irq);
		return ret;
	}

	dev_set_drvdata(ishtp->devc, ishtp);

	init_waitqueue_head(&ishtp->suspend_wait);
	init_waitqueue_head(&ishtp->resume_wait);

	/* Enable PME for EHL */
	if (pdev->device == PCI_DEVICE_ID_INTEL_ISH_EHL_Ax)
		device_init_wakeup(dev, true);

	ret = ish_init(ishtp);
	if (ret)
		return ret;

	return 0;
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

	ishtp_bus_remove_all_clients(ishtp_dev, false);
	ish_device_disable(ishtp_dev);
}


/**
 * ish_shutdown() - PCI driver shutdown callback
 * @pdev:	pci device
 *
 * This function sets up wakeup for S5
 */
static void ish_shutdown(struct pci_dev *pdev)
{
	if (pdev->device == PCI_DEVICE_ID_INTEL_ISH_EHL_Ax)
		pci_prepare_to_sleep(pdev);
}

static struct device __maybe_unused *ish_resume_device;

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
static void __maybe_unused ish_resume_handler(struct work_struct *work)
{
	struct pci_dev *pdev = to_pci_dev(ish_resume_device);
	struct ishtp_device *dev = pci_get_drvdata(pdev);
	uint32_t fwsts = dev->ops->get_fw_status(dev);

	if (ish_should_leave_d0i3(pdev) && !dev->suspend_flag
			&& IPC_IS_ISH_ILUP(fwsts)) {
		if (device_may_wakeup(&pdev->dev))
			disable_irq_wake(pdev->irq);

		ish_set_host_ready(dev);

		ishtp_send_resume(dev);

		/* Waiting to get resume response */
		if (dev->resume_flag)
			wait_event_interruptible_timeout(dev->resume_wait,
				!dev->resume_flag,
				msecs_to_jiffies(WAIT_FOR_RESUME_ACK_MS));

		/*
		 * If the flag is not cleared, something is wrong with ISH FW.
		 * So on resume, need to go through init sequence again.
		 */
		if (dev->resume_flag)
			ish_init(dev);
	} else {
		/*
		 * Resume from the D3, full reboot of ISH processor will happen,
		 * so need to go through init sequence again.
		 */
		ish_init(dev);
	}
}

/**
 * ish_suspend() - ISH suspend callback
 * @device:	device pointer
 *
 * ISH suspend callback
 *
 * Return: 0 to the pm core
 */
static int __maybe_unused ish_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct ishtp_device *dev = pci_get_drvdata(pdev);

	if (ish_should_enter_d0i3(pdev)) {
		/*
		 * If previous suspend hasn't been asnwered then ISH is likely
		 * dead, don't attempt nested notification
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

		if (dev->suspend_flag) {
			/*
			 * It looks like FW halt, clear the DMA bit, and put
			 * ISH into D3, and FW would reset on resume.
			 */
			ish_disable_dma(dev);
		} else {
			/*
			 * Save state so PCI core will keep the device at D0,
			 * the ISH would enter D0i3
			 */
			pci_save_state(pdev);

			if (device_may_wakeup(&pdev->dev))
				enable_irq_wake(pdev->irq);
		}
	} else {
		/*
		 * Clear the DMA bit before putting ISH into D3,
		 * or ISH FW would reset automatically.
		 */
		ish_disable_dma(dev);
	}

	return 0;
}

static __maybe_unused DECLARE_WORK(resume_work, ish_resume_handler);
/**
 * ish_resume() - ISH resume callback
 * @device:	device pointer
 *
 * ISH resume callback
 *
 * Return: 0 to the pm core
 */
static int __maybe_unused ish_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct ishtp_device *dev = pci_get_drvdata(pdev);

	ish_resume_device = device;
	dev->resume_flag = 1;

	schedule_work(&resume_work);

	return 0;
}

static SIMPLE_DEV_PM_OPS(ish_pm_ops, ish_suspend, ish_resume);

static ssize_t base_version_show(struct device *cdev,
				 struct device_attribute *attr, char *buf)
{
	struct ishtp_device *dev = dev_get_drvdata(cdev);

	return sysfs_emit(buf, "%u.%u.%u.%u\n", dev->base_ver.major,
			  dev->base_ver.minor, dev->base_ver.hotfix,
			  dev->base_ver.build);
}
static DEVICE_ATTR_RO(base_version);

static ssize_t project_version_show(struct device *cdev,
				    struct device_attribute *attr, char *buf)
{
	struct ishtp_device *dev = dev_get_drvdata(cdev);

	return sysfs_emit(buf, "%u.%u.%u.%u\n", dev->prj_ver.major,
			  dev->prj_ver.minor, dev->prj_ver.hotfix,
			  dev->prj_ver.build);
}
static DEVICE_ATTR_RO(project_version);

static struct attribute *ish_firmware_attrs[] = {
	&dev_attr_base_version.attr,
	&dev_attr_project_version.attr,
	NULL
};

static umode_t firmware_is_visible(struct kobject *kobj, struct attribute *attr,
				   int i)
{
	struct ishtp_device *dev = dev_get_drvdata(kobj_to_dev(kobj));

	return dev->driver_data->fw_generation ? attr->mode : 0;
}

static const struct attribute_group ish_firmware_group = {
	.name = "firmware",
	.attrs = ish_firmware_attrs,
	.is_visible = firmware_is_visible,
};

__ATTRIBUTE_GROUPS(ish_firmware);

static struct pci_driver ish_driver = {
	.name = KBUILD_MODNAME,
	.id_table = ish_pci_tbl,
	.probe = ish_probe,
	.remove = ish_remove,
	.shutdown = ish_shutdown,
	.driver.pm = &ish_pm_ops,
	.dev_groups = ish_firmware_groups,
};

module_pci_driver(ish_driver);

/* Original author */
MODULE_AUTHOR("Daniel Drubin <daniel.drubin@intel.com>");
/* Adoption to upstream Linux kernel */
MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");

MODULE_DESCRIPTION("Intel(R) Integrated Sensor Hub PCI Device Driver");
MODULE_LICENSE("GPL");

MODULE_FIRMWARE(ISH_FIRMWARE_PATH(ISH_FW_GEN_LNL_M));
MODULE_FIRMWARE(ISH_FIRMWARE_PATH_ALL);
