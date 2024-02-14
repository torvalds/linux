// SPDX-License-Identifier: GPL-2.0-only
/*
 * Processor thermal device for newer processors
 * Copyright (c) 2020, Intel Corporation.
 */

#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/thermal.h>

#include "int340x_thermal_zone.h"
#include "processor_thermal_device.h"

#define DRV_NAME "proc_thermal_pci"

struct proc_thermal_pci {
	struct pci_dev *pdev;
	struct proc_thermal_device *proc_priv;
	struct thermal_zone_device *tzone;
	struct delayed_work work;
	int stored_thres;
	int no_legacy;
};

enum proc_thermal_mmio_type {
	PROC_THERMAL_MMIO_TJMAX,
	PROC_THERMAL_MMIO_PP0_TEMP,
	PROC_THERMAL_MMIO_PP1_TEMP,
	PROC_THERMAL_MMIO_PKG_TEMP,
	PROC_THERMAL_MMIO_THRES_0,
	PROC_THERMAL_MMIO_THRES_1,
	PROC_THERMAL_MMIO_INT_ENABLE_0,
	PROC_THERMAL_MMIO_INT_ENABLE_1,
	PROC_THERMAL_MMIO_INT_STATUS_0,
	PROC_THERMAL_MMIO_INT_STATUS_1,
	PROC_THERMAL_MMIO_MAX
};

struct proc_thermal_mmio_info {
	enum proc_thermal_mmio_type mmio_type;
	u64	mmio_addr;
	u64	shift;
	u64	mask;
};

static struct proc_thermal_mmio_info proc_thermal_mmio_info[] = {
	{ PROC_THERMAL_MMIO_TJMAX, 0x599c, 16, 0xff },
	{ PROC_THERMAL_MMIO_PP0_TEMP, 0x597c, 0, 0xff },
	{ PROC_THERMAL_MMIO_PP1_TEMP, 0x5980, 0, 0xff },
	{ PROC_THERMAL_MMIO_PKG_TEMP, 0x5978, 0, 0xff },
	{ PROC_THERMAL_MMIO_THRES_0, 0x5820, 8, 0x7F },
	{ PROC_THERMAL_MMIO_THRES_1, 0x5820, 16, 0x7F },
	{ PROC_THERMAL_MMIO_INT_ENABLE_0, 0x5820, 15, 0x01 },
	{ PROC_THERMAL_MMIO_INT_ENABLE_1, 0x5820, 23, 0x01 },
	{ PROC_THERMAL_MMIO_INT_STATUS_0, 0x7200, 6, 0x01 },
	{ PROC_THERMAL_MMIO_INT_STATUS_1, 0x7200, 8, 0x01 },
};

#define B0D4_THERMAL_NOTIFY_DELAY	1000
static int notify_delay_ms = B0D4_THERMAL_NOTIFY_DELAY;

static void proc_thermal_mmio_read(struct proc_thermal_pci *pci_info,
				    enum proc_thermal_mmio_type type,
				    u32 *value)
{
	*value = ioread32(((u8 __iomem *)pci_info->proc_priv->mmio_base +
				proc_thermal_mmio_info[type].mmio_addr));
	*value >>= proc_thermal_mmio_info[type].shift;
	*value &= proc_thermal_mmio_info[type].mask;
}

static void proc_thermal_mmio_write(struct proc_thermal_pci *pci_info,
				     enum proc_thermal_mmio_type type,
				     u32 value)
{
	u32 current_val;
	u32 mask;

	current_val = ioread32(((u8 __iomem *)pci_info->proc_priv->mmio_base +
				proc_thermal_mmio_info[type].mmio_addr));
	mask = proc_thermal_mmio_info[type].mask << proc_thermal_mmio_info[type].shift;
	current_val &= ~mask;

	value &= proc_thermal_mmio_info[type].mask;
	value <<= proc_thermal_mmio_info[type].shift;

	current_val |= value;
	iowrite32(current_val, ((u8 __iomem *)pci_info->proc_priv->mmio_base +
				proc_thermal_mmio_info[type].mmio_addr));
}

/*
 * To avoid sending two many messages to user space, we have 1 second delay.
 * On interrupt we are disabling interrupt and enabling after 1 second.
 * This workload function is delayed by 1 second.
 */
static void proc_thermal_threshold_work_fn(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct proc_thermal_pci *pci_info = container_of(delayed_work,
						struct proc_thermal_pci, work);
	struct thermal_zone_device *tzone = pci_info->tzone;

	if (tzone)
		thermal_zone_device_update(tzone, THERMAL_TRIP_VIOLATED);

	/* Enable interrupt flag */
	proc_thermal_mmio_write(pci_info, PROC_THERMAL_MMIO_INT_ENABLE_0, 1);
}

static void pkg_thermal_schedule_work(struct delayed_work *work)
{
	unsigned long ms = msecs_to_jiffies(notify_delay_ms);

	schedule_delayed_work(work, ms);
}

static irqreturn_t proc_thermal_irq_handler(int irq, void *devid)
{
	struct proc_thermal_pci *pci_info = devid;
	u32 status;

	proc_thermal_mmio_read(pci_info, PROC_THERMAL_MMIO_INT_STATUS_0, &status);

	/* Disable enable interrupt flag */
	proc_thermal_mmio_write(pci_info, PROC_THERMAL_MMIO_INT_ENABLE_0, 0);
	pci_write_config_byte(pci_info->pdev, 0xdc, 0x01);

	pkg_thermal_schedule_work(&pci_info->work);

	return IRQ_HANDLED;
}

static int sys_get_curr_temp(struct thermal_zone_device *tzd, int *temp)
{
	struct proc_thermal_pci *pci_info = tzd->devdata;
	u32 _temp;

	proc_thermal_mmio_read(pci_info, PROC_THERMAL_MMIO_PKG_TEMP, &_temp);
	*temp = (unsigned long)_temp * 1000;

	return 0;
}

static int sys_get_trip_temp(struct thermal_zone_device *tzd,
			     int trip, int *temp)
{
	struct proc_thermal_pci *pci_info = tzd->devdata;
	u32 _temp;

	proc_thermal_mmio_read(pci_info, PROC_THERMAL_MMIO_THRES_0, &_temp);
	if (!_temp) {
		*temp = THERMAL_TEMP_INVALID;
	} else {
		int tjmax;

		proc_thermal_mmio_read(pci_info, PROC_THERMAL_MMIO_TJMAX, &tjmax);
		_temp = tjmax - _temp;
		*temp = (unsigned long)_temp * 1000;
	}

	return 0;
}

static int sys_get_trip_type(struct thermal_zone_device *tzd, int trip,
			      enum thermal_trip_type *type)
{
	*type = THERMAL_TRIP_PASSIVE;

	return 0;
}

static int sys_set_trip_temp(struct thermal_zone_device *tzd, int trip, int temp)
{
	struct proc_thermal_pci *pci_info = tzd->devdata;
	int tjmax, _temp;

	if (temp <= 0) {
		cancel_delayed_work_sync(&pci_info->work);
		proc_thermal_mmio_write(pci_info, PROC_THERMAL_MMIO_INT_ENABLE_0, 0);
		proc_thermal_mmio_write(pci_info, PROC_THERMAL_MMIO_THRES_0, 0);
		thermal_zone_device_disable(tzd);
		pci_info->stored_thres = 0;
		return 0;
	}

	proc_thermal_mmio_read(pci_info, PROC_THERMAL_MMIO_TJMAX, &tjmax);
	_temp = tjmax - (temp / 1000);
	if (_temp < 0)
		return -EINVAL;

	proc_thermal_mmio_write(pci_info, PROC_THERMAL_MMIO_THRES_0, _temp);
	proc_thermal_mmio_write(pci_info, PROC_THERMAL_MMIO_INT_ENABLE_0, 1);

	thermal_zone_device_enable(tzd);
	pci_info->stored_thres = temp;

	return 0;
}

static struct thermal_zone_device_ops tzone_ops = {
	.get_temp = sys_get_curr_temp,
	.get_trip_temp = sys_get_trip_temp,
	.get_trip_type = sys_get_trip_type,
	.set_trip_temp	= sys_set_trip_temp,
};

static struct thermal_zone_params tzone_params = {
	.governor_name = "user_space",
	.no_hwmon = true,
};

static int proc_thermal_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct proc_thermal_device *proc_priv;
	struct proc_thermal_pci *pci_info;
	int irq_flag = 0, irq, ret;

	proc_priv = devm_kzalloc(&pdev->dev, sizeof(*proc_priv), GFP_KERNEL);
	if (!proc_priv)
		return -ENOMEM;

	pci_info = devm_kzalloc(&pdev->dev, sizeof(*pci_info), GFP_KERNEL);
	if (!pci_info)
		return -ENOMEM;

	pci_info->pdev = pdev;
	ret = pcim_enable_device(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "error: could not enable device\n");
		return ret;
	}

	pci_set_master(pdev);

	INIT_DELAYED_WORK(&pci_info->work, proc_thermal_threshold_work_fn);

	ret = proc_thermal_add(&pdev->dev, proc_priv);
	if (ret) {
		dev_err(&pdev->dev, "error: proc_thermal_add, will continue\n");
		pci_info->no_legacy = 1;
	}

	proc_priv->priv_data = pci_info;
	pci_info->proc_priv = proc_priv;
	pci_set_drvdata(pdev, proc_priv);

	ret = proc_thermal_mmio_add(pdev, proc_priv, id->driver_data);
	if (ret)
		goto err_ret_thermal;

	pci_info->tzone = thermal_zone_device_register("TCPU_PCI", 1, 1, pci_info,
							&tzone_ops,
							&tzone_params, 0, 0);
	if (IS_ERR(pci_info->tzone)) {
		ret = PTR_ERR(pci_info->tzone);
		goto err_ret_mmio;
	}

	/* request and enable interrupt */
	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to allocate vectors!\n");
		goto err_ret_tzone;
	}
	if (!pdev->msi_enabled && !pdev->msix_enabled)
		irq_flag = IRQF_SHARED;

	irq =  pci_irq_vector(pdev, 0);
	ret = devm_request_threaded_irq(&pdev->dev, irq,
					proc_thermal_irq_handler, NULL,
					irq_flag, KBUILD_MODNAME, pci_info);
	if (ret) {
		dev_err(&pdev->dev, "Request IRQ %d failed\n", pdev->irq);
		goto err_free_vectors;
	}

	return 0;

err_free_vectors:
	pci_free_irq_vectors(pdev);
err_ret_tzone:
	thermal_zone_device_unregister(pci_info->tzone);
err_ret_mmio:
	proc_thermal_mmio_remove(pdev, proc_priv);
err_ret_thermal:
	if (!pci_info->no_legacy)
		proc_thermal_remove(proc_priv);
	pci_disable_device(pdev);

	return ret;
}

static void proc_thermal_pci_remove(struct pci_dev *pdev)
{
	struct proc_thermal_device *proc_priv = pci_get_drvdata(pdev);
	struct proc_thermal_pci *pci_info = proc_priv->priv_data;

	cancel_delayed_work_sync(&pci_info->work);

	proc_thermal_mmio_write(pci_info, PROC_THERMAL_MMIO_THRES_0, 0);
	proc_thermal_mmio_write(pci_info, PROC_THERMAL_MMIO_INT_ENABLE_0, 0);

	devm_free_irq(&pdev->dev, pdev->irq, pci_info);
	pci_free_irq_vectors(pdev);

	thermal_zone_device_unregister(pci_info->tzone);
	proc_thermal_mmio_remove(pdev, pci_info->proc_priv);
	if (!pci_info->no_legacy)
		proc_thermal_remove(proc_priv);
	pci_disable_device(pdev);
}

#ifdef CONFIG_PM_SLEEP
static int proc_thermal_pci_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct proc_thermal_device *proc_priv;
	struct proc_thermal_pci *pci_info;

	proc_priv = pci_get_drvdata(pdev);
	pci_info = proc_priv->priv_data;

	if (!pci_info->no_legacy)
		return proc_thermal_suspend(dev);

	return 0;
}
static int proc_thermal_pci_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct proc_thermal_device *proc_priv;
	struct proc_thermal_pci *pci_info;

	proc_priv = pci_get_drvdata(pdev);
	pci_info = proc_priv->priv_data;

	if (pci_info->stored_thres) {
		proc_thermal_mmio_write(pci_info, PROC_THERMAL_MMIO_THRES_0,
					 pci_info->stored_thres / 1000);
		proc_thermal_mmio_write(pci_info, PROC_THERMAL_MMIO_INT_ENABLE_0, 1);
	}

	if (!pci_info->no_legacy)
		return proc_thermal_resume(dev);

	return 0;
}
#else
#define proc_thermal_pci_suspend NULL
#define proc_thermal_pci_resume NULL
#endif

static SIMPLE_DEV_PM_OPS(proc_thermal_pci_pm, proc_thermal_pci_suspend,
			 proc_thermal_pci_resume);

static const struct pci_device_id proc_thermal_pci_ids[] = {
	{ PCI_DEVICE_DATA(INTEL, ADL_THERMAL, PROC_THERMAL_FEATURE_RAPL | PROC_THERMAL_FEATURE_FIVR | PROC_THERMAL_FEATURE_DVFS | PROC_THERMAL_FEATURE_MBOX) },
	{ PCI_DEVICE_DATA(INTEL, MTLP_THERMAL, PROC_THERMAL_FEATURE_RAPL | PROC_THERMAL_FEATURE_FIVR | PROC_THERMAL_FEATURE_DVFS | PROC_THERMAL_FEATURE_MBOX) },
	{ PCI_DEVICE_DATA(INTEL, RPL_THERMAL, PROC_THERMAL_FEATURE_RAPL | PROC_THERMAL_FEATURE_FIVR | PROC_THERMAL_FEATURE_DVFS | PROC_THERMAL_FEATURE_MBOX) },
	{ },
};

MODULE_DEVICE_TABLE(pci, proc_thermal_pci_ids);

static struct pci_driver proc_thermal_pci_driver = {
	.name		= DRV_NAME,
	.probe		= proc_thermal_pci_probe,
	.remove	= proc_thermal_pci_remove,
	.id_table	= proc_thermal_pci_ids,
	.driver.pm	= &proc_thermal_pci_pm,
};

module_pci_driver(proc_thermal_pci_driver);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_DESCRIPTION("Processor Thermal Reporting Device Driver");
MODULE_LICENSE("GPL v2");
