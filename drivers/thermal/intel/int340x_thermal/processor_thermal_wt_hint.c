// SPDX-License-Identifier: GPL-2.0-only
/*
 * processor thermal device interface for reading workload type hints
 * from the user space. The hints are provided by the firmware.
 *
 * Operation:
 * When user space enables workload type prediction:
 * - Use mailbox to:
 *	Configure notification delay
 *	Enable processor thermal device interrupt
 *
 * - The predicted workload type can be read from MMIO:
 *	Offset 0x5B18 shows if there was an interrupt
 *	active for change in workload type and also
 *	predicted workload type.
 *
 * Two interface functions are provided to call when there is a
 * thermal device interrupt:
 * - proc_thermal_check_wt_intr():
 *     Check if the interrupt is for change in workload type. Called from
 *     interrupt context.
 *
 * - proc_thermal_wt_intr_callback():
 *     Callback for interrupt processing in thread context. This involves
 *	sending notification to user space that there is a change in the
 *     workload type.
 *
 * Copyright (c) 2023, Intel Corporation.
 */

#include <linux/bitfield.h>
#include <linux/pci.h>
#include "processor_thermal_device.h"

#define SOC_WT				GENMASK_ULL(47, 40)

#define SOC_WT_PREDICTION_INT_ENABLE_BIT	23

#define SOC_WT_PREDICTION_INT_ACTIVE	BIT(2)

/*
 * Closest possible to 1 Second is 1024 ms with programmed time delay
 * of 0x0A.
 */
static u8 notify_delay = 0x0A;
static u16 notify_delay_ms = 1024;

static DEFINE_MUTEX(wt_lock);
static u8 wt_enable;

/* Show current predicted workload type index */
static ssize_t workload_type_index_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct proc_thermal_device *proc_priv;
	struct pci_dev *pdev = to_pci_dev(dev);
	u64 status = 0;
	int wt;

	mutex_lock(&wt_lock);
	if (!wt_enable) {
		mutex_unlock(&wt_lock);
		return -ENODATA;
	}

	proc_priv = pci_get_drvdata(pdev);

	status = readq(proc_priv->mmio_base + SOC_WT_RES_INT_STATUS_OFFSET);

	mutex_unlock(&wt_lock);

	wt = FIELD_GET(SOC_WT, status);

	return sysfs_emit(buf, "%d\n", wt);
}

static DEVICE_ATTR_RO(workload_type_index);

static ssize_t workload_hint_enable_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	return sysfs_emit(buf, "%d\n", wt_enable);
}

static ssize_t workload_hint_enable_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t size)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	u8 mode;
	int ret;

	if (kstrtou8(buf, 10, &mode) || mode > 1)
		return -EINVAL;

	mutex_lock(&wt_lock);

	if (mode)
		ret = processor_thermal_mbox_interrupt_config(pdev, true,
							      SOC_WT_PREDICTION_INT_ENABLE_BIT,
							      notify_delay);
	else
		ret = processor_thermal_mbox_interrupt_config(pdev, false,
							      SOC_WT_PREDICTION_INT_ENABLE_BIT, 0);

	if (ret)
		goto ret_enable_store;

	ret = size;
	wt_enable = mode;

ret_enable_store:
	mutex_unlock(&wt_lock);

	return ret;
}

static DEVICE_ATTR_RW(workload_hint_enable);

static ssize_t notification_delay_ms_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	return sysfs_emit(buf, "%u\n", notify_delay_ms);
}

static ssize_t notification_delay_ms_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t size)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	u16 new_tw;
	int ret;
	u8 tm;

	/*
	 * Time window register value:
	 * Formula: (1 + x/4) * power(2,y)
	 * x = 2 msbs, that is [30:29] y = 5 [28:24]
	 * in INTR_CONFIG register.
	 * The result will be in milli seconds.
	 * Here, just keep x = 0, and just change y.
	 * First round up the user value to power of 2 and
	 * then take log2, to get "y" value to program.
	 */
	ret = kstrtou16(buf, 10, &new_tw);
	if (ret)
		return ret;

	if (!new_tw)
		return -EINVAL;

	new_tw = roundup_pow_of_two(new_tw);
	tm = ilog2(new_tw);
	if (tm > 31)
		return -EINVAL;

	mutex_lock(&wt_lock);

	/* If the workload hint was already enabled, then update with the new delay */
	if (wt_enable)
		ret = processor_thermal_mbox_interrupt_config(pdev, true,
							      SOC_WT_PREDICTION_INT_ENABLE_BIT,
							      tm);

	if (!ret) {
		ret = size;
		notify_delay = tm;
		notify_delay_ms = new_tw;
	}

	mutex_unlock(&wt_lock);

	return ret;
}

static DEVICE_ATTR_RW(notification_delay_ms);

static struct attribute *workload_hint_attrs[] = {
	&dev_attr_workload_type_index.attr,
	&dev_attr_workload_hint_enable.attr,
	&dev_attr_notification_delay_ms.attr,
	NULL
};

static const struct attribute_group workload_hint_attribute_group = {
	.attrs = workload_hint_attrs,
	.name = "workload_hint"
};

/*
 * Callback to check if the interrupt for prediction is active.
 * Caution: Called from the interrupt context.
 */
bool proc_thermal_check_wt_intr(struct proc_thermal_device *proc_priv)
{
	u64 int_status;

	int_status = readq(proc_priv->mmio_base + SOC_WT_RES_INT_STATUS_OFFSET);
	if (int_status & SOC_WT_PREDICTION_INT_ACTIVE)
		return true;

	return false;
}
EXPORT_SYMBOL_NS_GPL(proc_thermal_check_wt_intr, "INT340X_THERMAL");

/* Callback to notify user space */
void proc_thermal_wt_intr_callback(struct pci_dev *pdev, struct proc_thermal_device *proc_priv)
{
	u64 status;

	status = readq(proc_priv->mmio_base + SOC_WT_RES_INT_STATUS_OFFSET);
	if (!(status & SOC_WT_PREDICTION_INT_ACTIVE))
		return;

	sysfs_notify(&pdev->dev.kobj, "workload_hint", "workload_type_index");
}
EXPORT_SYMBOL_NS_GPL(proc_thermal_wt_intr_callback, "INT340X_THERMAL");

static bool workload_hint_created;

int proc_thermal_wt_hint_add(struct pci_dev *pdev, struct proc_thermal_device *proc_priv)
{
	int ret;

	ret = sysfs_create_group(&pdev->dev.kobj, &workload_hint_attribute_group);
	if (ret)
		return ret;

	workload_hint_created = true;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(proc_thermal_wt_hint_add, "INT340X_THERMAL");

void proc_thermal_wt_hint_remove(struct pci_dev *pdev)
{
	mutex_lock(&wt_lock);
	if (wt_enable)
		processor_thermal_mbox_interrupt_config(pdev, false,
							SOC_WT_PREDICTION_INT_ENABLE_BIT,
							0);
	mutex_unlock(&wt_lock);

	if (workload_hint_created)
		sysfs_remove_group(&pdev->dev.kobj, &workload_hint_attribute_group);

	workload_hint_created = false;
}
EXPORT_SYMBOL_NS_GPL(proc_thermal_wt_hint_remove, "INT340X_THERMAL");

MODULE_IMPORT_NS("INT340X_THERMAL");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Processor Thermal Work Load type hint Interface");
