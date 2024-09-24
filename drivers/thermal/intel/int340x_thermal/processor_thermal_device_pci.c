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

static bool use_msi;
module_param(use_msi, bool, 0644);
MODULE_PARM_DESC(use_msi,
	"Use PCI MSI based interrupts for processor thermal device.");

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

/* List of supported MSI IDs (sources) */
enum proc_thermal_msi_ids {
	PKG_THERMAL,
	DDR_THERMAL,
	THERM_POWER_FLOOR,
	WORKLOAD_CHANGE,
	MSI_THERMAL_MAX
};

/* Stores IRQ associated with a MSI ID */
static int proc_thermal_msi_map[MSI_THERMAL_MAX];

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

static void proc_thermal_clear_soc_int_status(struct proc_thermal_device *proc_priv)
{
	u64 status;

	if (!(proc_priv->mmio_feature_mask &
	    (PROC_THERMAL_FEATURE_WT_HINT | PROC_THERMAL_FEATURE_POWER_FLOOR)))
		return;

	status = readq(proc_priv->mmio_base + SOC_WT_RES_INT_STATUS_OFFSET);
	writeq(status & ~SOC_WT_RES_INT_STATUS_MASK,
	       proc_priv->mmio_base + SOC_WT_RES_INT_STATUS_OFFSET);
}

static irqreturn_t proc_thermal_irq_thread_handler(int irq, void *devid)
{
	struct proc_thermal_pci *pci_info = devid;

	proc_thermal_wt_intr_callback(pci_info->pdev, pci_info->proc_priv);
	proc_thermal_power_floor_intr_callback(pci_info->pdev, pci_info->proc_priv);
	proc_thermal_clear_soc_int_status(pci_info->proc_priv);

	return IRQ_HANDLED;
}

static int proc_thermal_match_msi_irq(int irq)
{
	int i;

	if (!use_msi)
		goto msi_fail;

	for (i = 0; i < MSI_THERMAL_MAX; i++) {
		if (proc_thermal_msi_map[i] == irq)
			return i;
	}

msi_fail:
	return -EOPNOTSUPP;
}

static irqreturn_t proc_thermal_irq_handler(int irq, void *devid)
{
	struct proc_thermal_pci *pci_info = devid;
	struct proc_thermal_device *proc_priv;
	int ret = IRQ_NONE, msi_id;
	u32 status;

	proc_priv = pci_info->proc_priv;

	msi_id = proc_thermal_match_msi_irq(irq);

	if (proc_priv->mmio_feature_mask & PROC_THERMAL_FEATURE_WT_HINT) {
		if (msi_id == WORKLOAD_CHANGE || proc_thermal_check_wt_intr(pci_info->proc_priv))
			ret = IRQ_WAKE_THREAD;
	}

	if (proc_priv->mmio_feature_mask & PROC_THERMAL_FEATURE_POWER_FLOOR) {
		if (msi_id == THERM_POWER_FLOOR ||
		    proc_thermal_check_power_floor_intr(pci_info->proc_priv))
			ret = IRQ_WAKE_THREAD;
	}

	/*
	 * Since now there are two sources of interrupts: one from thermal threshold
	 * and another from workload hint, add a check if there was really a threshold
	 * interrupt before scheduling work function for thermal threshold.
	 */
	proc_thermal_mmio_read(pci_info, PROC_THERMAL_MMIO_INT_STATUS_0, &status);
	if (msi_id == PKG_THERMAL || status) {
		/* Disable enable interrupt flag */
		proc_thermal_mmio_write(pci_info, PROC_THERMAL_MMIO_INT_ENABLE_0, 0);
		pkg_thermal_schedule_work(&pci_info->work);
		ret = IRQ_HANDLED;
	}

	pci_write_config_byte(pci_info->pdev, 0xdc, 0x01);

	return ret;
}

static int sys_get_curr_temp(struct thermal_zone_device *tzd, int *temp)
{
	struct proc_thermal_pci *pci_info = thermal_zone_device_priv(tzd);
	u32 _temp;

	proc_thermal_mmio_read(pci_info, PROC_THERMAL_MMIO_PKG_TEMP, &_temp);
	*temp = (unsigned long)_temp * 1000;

	return 0;
}

static int sys_set_trip_temp(struct thermal_zone_device *tzd,
			     const struct thermal_trip *trip, int temp)
{
	struct proc_thermal_pci *pci_info = thermal_zone_device_priv(tzd);
	int tjmax, _temp;

	if (temp <= 0) {
		cancel_delayed_work_sync(&pci_info->work);
		proc_thermal_mmio_write(pci_info, PROC_THERMAL_MMIO_INT_ENABLE_0, 0);
		proc_thermal_mmio_write(pci_info, PROC_THERMAL_MMIO_THRES_0, 0);
		pci_info->stored_thres = 0;
		return 0;
	}

	proc_thermal_mmio_read(pci_info, PROC_THERMAL_MMIO_TJMAX, &tjmax);
	_temp = tjmax - (temp / 1000);
	if (_temp < 0)
		return -EINVAL;

	proc_thermal_mmio_write(pci_info, PROC_THERMAL_MMIO_THRES_0, _temp);
	proc_thermal_mmio_write(pci_info, PROC_THERMAL_MMIO_INT_ENABLE_0, 1);

	pci_info->stored_thres = temp;

	return 0;
}

static int get_trip_temp(struct proc_thermal_pci *pci_info)
{
	int temp, tjmax;

	proc_thermal_mmio_read(pci_info, PROC_THERMAL_MMIO_THRES_0, &temp);
	if (!temp)
		return THERMAL_TEMP_INVALID;

	proc_thermal_mmio_read(pci_info, PROC_THERMAL_MMIO_TJMAX, &tjmax);
	temp = (tjmax - temp) * 1000;

	return temp;
}

static const struct thermal_zone_device_ops tzone_ops = {
	.get_temp = sys_get_curr_temp,
	.set_trip_temp	= sys_set_trip_temp,
};

static struct thermal_zone_params tzone_params = {
	.governor_name = "user_space",
	.no_hwmon = true,
};

static bool msi_irq;

static void proc_thermal_free_msi(struct pci_dev *pdev, struct proc_thermal_pci *pci_info)
{
	int i;

	for (i = 0; i < MSI_THERMAL_MAX; i++) {
		if (proc_thermal_msi_map[i])
			devm_free_irq(&pdev->dev, proc_thermal_msi_map[i], pci_info);
	}

	pci_free_irq_vectors(pdev);
}

static int proc_thermal_setup_msi(struct pci_dev *pdev, struct proc_thermal_pci *pci_info)
{
	int ret, i, irq, count;

	count = pci_alloc_irq_vectors(pdev, 1, MSI_THERMAL_MAX, PCI_IRQ_MSI | PCI_IRQ_MSIX);
	if (count < 0) {
		dev_err(&pdev->dev, "Failed to allocate vectors!\n");
		return count;
	}

	dev_info(&pdev->dev, "msi enabled:%d msix enabled:%d\n", pdev->msi_enabled,
		 pdev->msix_enabled);

	for (i = 0; i < count; i++) {
		irq =  pci_irq_vector(pdev, i);

		ret = devm_request_threaded_irq(&pdev->dev, irq, proc_thermal_irq_handler,
						proc_thermal_irq_thread_handler,
						0, KBUILD_MODNAME, pci_info);
		if (ret) {
			dev_err(&pdev->dev, "Request IRQ %d failed\n", irq);
			goto err_free_msi_vectors;
		}

		proc_thermal_msi_map[i] = irq;
	}

	msi_irq = true;

	return 0;

err_free_msi_vectors:
	proc_thermal_free_msi(pdev, pci_info);

	return ret;
}

static int proc_thermal_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct proc_thermal_device *proc_priv;
	struct proc_thermal_pci *pci_info;
	struct thermal_trip psv_trip = {
		.type = THERMAL_TRIP_PASSIVE,
		.flags = THERMAL_TRIP_FLAG_RW_TEMP,
	};
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

	proc_priv->priv_data = pci_info;
	pci_info->proc_priv = proc_priv;
	pci_set_drvdata(pdev, proc_priv);

	ret = proc_thermal_mmio_add(pdev, proc_priv, id->driver_data);
	if (ret)
		return ret;

	ret = proc_thermal_add(&pdev->dev, proc_priv);
	if (ret) {
		dev_err(&pdev->dev, "error: proc_thermal_add, will continue\n");
		pci_info->no_legacy = 1;
	}

	psv_trip.temperature = get_trip_temp(pci_info);

	pci_info->tzone = thermal_zone_device_register_with_trips("TCPU_PCI", &psv_trip,
							1, pci_info,
							&tzone_ops,
							&tzone_params, 0, 0);
	if (IS_ERR(pci_info->tzone)) {
		ret = PTR_ERR(pci_info->tzone);
		goto err_del_legacy;
	}

	if (proc_priv->mmio_feature_mask & PROC_THERMAL_FEATURE_MSI_SUPPORT)
		use_msi = true;

	if (use_msi) {
		ret = proc_thermal_setup_msi(pdev, pci_info);
		if (ret)
			goto err_ret_tzone;
	} else {
		irq_flag = IRQF_SHARED;
		irq = pdev->irq;

		ret = devm_request_threaded_irq(&pdev->dev, irq, proc_thermal_irq_handler,
						proc_thermal_irq_thread_handler, irq_flag,
						KBUILD_MODNAME, pci_info);
		if (ret) {
			dev_err(&pdev->dev, "Request IRQ %d failed\n", pdev->irq);
			goto err_ret_tzone;
		}
	}

	ret = thermal_zone_device_enable(pci_info->tzone);
	if (ret)
		goto err_free_vectors;

	return 0;

err_free_vectors:
	if (msi_irq)
		proc_thermal_free_msi(pdev, pci_info);
err_ret_tzone:
	thermal_zone_device_unregister(pci_info->tzone);
err_del_legacy:
	if (!pci_info->no_legacy)
		proc_thermal_remove(proc_priv);
	proc_thermal_mmio_remove(pdev, proc_priv);
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

	if (msi_irq)
		proc_thermal_free_msi(pdev, pci_info);

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
	{ PCI_DEVICE_DATA(INTEL, ADL_THERMAL, PROC_THERMAL_FEATURE_RAPL |
	  PROC_THERMAL_FEATURE_FIVR | PROC_THERMAL_FEATURE_DVFS | PROC_THERMAL_FEATURE_WT_REQ) },
	{ PCI_DEVICE_DATA(INTEL, LNLM_THERMAL, PROC_THERMAL_FEATURE_MSI_SUPPORT |
	  PROC_THERMAL_FEATURE_RAPL | PROC_THERMAL_FEATURE_DLVR |
	  PROC_THERMAL_FEATURE_WT_HINT | PROC_THERMAL_FEATURE_POWER_FLOOR) },
	{ PCI_DEVICE_DATA(INTEL, MTLP_THERMAL, PROC_THERMAL_FEATURE_RAPL |
	  PROC_THERMAL_FEATURE_FIVR | PROC_THERMAL_FEATURE_DVFS | PROC_THERMAL_FEATURE_DLVR |
	  PROC_THERMAL_FEATURE_WT_HINT | PROC_THERMAL_FEATURE_POWER_FLOOR) },
	{ PCI_DEVICE_DATA(INTEL, ARL_S_THERMAL, PROC_THERMAL_FEATURE_RAPL |
	  PROC_THERMAL_FEATURE_DVFS | PROC_THERMAL_FEATURE_DLVR | PROC_THERMAL_FEATURE_WT_HINT) },
	{ PCI_DEVICE_DATA(INTEL, RPL_THERMAL, PROC_THERMAL_FEATURE_RAPL |
	  PROC_THERMAL_FEATURE_FIVR | PROC_THERMAL_FEATURE_DVFS | PROC_THERMAL_FEATURE_WT_REQ) },
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

MODULE_IMPORT_NS(INT340X_THERMAL);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_DESCRIPTION("Processor Thermal Reporting Device Driver");
MODULE_LICENSE("GPL v2");
