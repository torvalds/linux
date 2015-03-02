/*
 * processor_thermal_device.c
 * Copyright (c) 2014, Intel Corporation.
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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/thermal.h>
#include "int340x_thermal_zone.h"
#include "../intel_soc_dts_iosf.h"

/* Broadwell-U/HSB thermal reporting device */
#define PCI_DEVICE_ID_PROC_BDW_THERMAL	0x1603
#define PCI_DEVICE_ID_PROC_HSB_THERMAL	0x0A03

/* Braswell thermal reporting device */
#define PCI_DEVICE_ID_PROC_BSW_THERMAL	0x22DC

struct power_config {
	u32	index;
	u32	min_uw;
	u32	max_uw;
	u32	tmin_us;
	u32	tmax_us;
	u32	step_uw;
};

struct proc_thermal_device {
	struct device *dev;
	struct acpi_device *adev;
	struct power_config power_limits[2];
	struct int34x_thermal_zone *int340x_zone;
	struct intel_soc_dts_sensors *soc_dts;
};

enum proc_thermal_emum_mode_type {
	PROC_THERMAL_NONE,
	PROC_THERMAL_PCI,
	PROC_THERMAL_PLATFORM_DEV
};

/*
 * We can have only one type of enumeration, PCI or Platform,
 * not both. So we don't need instance specific data.
 */
static enum proc_thermal_emum_mode_type proc_thermal_emum_mode =
							PROC_THERMAL_NONE;

#define POWER_LIMIT_SHOW(index, suffix) \
static ssize_t power_limit_##index##_##suffix##_show(struct device *dev, \
					struct device_attribute *attr, \
					char *buf) \
{ \
	struct pci_dev *pci_dev; \
	struct platform_device *pdev; \
	struct proc_thermal_device *proc_dev; \
\
	if (proc_thermal_emum_mode == PROC_THERMAL_PLATFORM_DEV) { \
		pdev = to_platform_device(dev); \
		proc_dev = platform_get_drvdata(pdev); \
	} else { \
		pci_dev = to_pci_dev(dev); \
		proc_dev = pci_get_drvdata(pci_dev); \
	} \
	return sprintf(buf, "%lu\n",\
	(unsigned long)proc_dev->power_limits[index].suffix * 1000); \
}

POWER_LIMIT_SHOW(0, min_uw)
POWER_LIMIT_SHOW(0, max_uw)
POWER_LIMIT_SHOW(0, step_uw)
POWER_LIMIT_SHOW(0, tmin_us)
POWER_LIMIT_SHOW(0, tmax_us)

POWER_LIMIT_SHOW(1, min_uw)
POWER_LIMIT_SHOW(1, max_uw)
POWER_LIMIT_SHOW(1, step_uw)
POWER_LIMIT_SHOW(1, tmin_us)
POWER_LIMIT_SHOW(1, tmax_us)

static DEVICE_ATTR_RO(power_limit_0_min_uw);
static DEVICE_ATTR_RO(power_limit_0_max_uw);
static DEVICE_ATTR_RO(power_limit_0_step_uw);
static DEVICE_ATTR_RO(power_limit_0_tmin_us);
static DEVICE_ATTR_RO(power_limit_0_tmax_us);

static DEVICE_ATTR_RO(power_limit_1_min_uw);
static DEVICE_ATTR_RO(power_limit_1_max_uw);
static DEVICE_ATTR_RO(power_limit_1_step_uw);
static DEVICE_ATTR_RO(power_limit_1_tmin_us);
static DEVICE_ATTR_RO(power_limit_1_tmax_us);

static struct attribute *power_limit_attrs[] = {
	&dev_attr_power_limit_0_min_uw.attr,
	&dev_attr_power_limit_1_min_uw.attr,
	&dev_attr_power_limit_0_max_uw.attr,
	&dev_attr_power_limit_1_max_uw.attr,
	&dev_attr_power_limit_0_step_uw.attr,
	&dev_attr_power_limit_1_step_uw.attr,
	&dev_attr_power_limit_0_tmin_us.attr,
	&dev_attr_power_limit_1_tmin_us.attr,
	&dev_attr_power_limit_0_tmax_us.attr,
	&dev_attr_power_limit_1_tmax_us.attr,
	NULL
};

static struct attribute_group power_limit_attribute_group = {
	.attrs = power_limit_attrs,
	.name = "power_limits"
};

static int stored_tjmax; /* since it is fixed, we can have local storage */

static int get_tjmax(void)
{
	u32 eax, edx;
	u32 val;
	int err;

	err = rdmsr_safe(MSR_IA32_TEMPERATURE_TARGET, &eax, &edx);
	if (err)
		return err;

	val = (eax >> 16) & 0xff;
	if (val)
		return val;

	return -EINVAL;
}

static int read_temp_msr(unsigned long *temp)
{
	int cpu;
	u32 eax, edx;
	int err;
	unsigned long curr_temp_off = 0;

	*temp = 0;

	for_each_online_cpu(cpu) {
		err = rdmsr_safe_on_cpu(cpu, MSR_IA32_THERM_STATUS, &eax,
					&edx);
		if (err)
			goto err_ret;
		else {
			if (eax & 0x80000000) {
				curr_temp_off = (eax >> 16) & 0x7f;
				if (!*temp || curr_temp_off < *temp)
					*temp = curr_temp_off;
			} else {
				err = -EINVAL;
				goto err_ret;
			}
		}
	}

	return 0;
err_ret:
	return err;
}

static int proc_thermal_get_zone_temp(struct thermal_zone_device *zone,
					 unsigned long *temp)
{
	int ret;

	ret = read_temp_msr(temp);
	if (!ret)
		*temp = (stored_tjmax - *temp) * 1000;

	return ret;
}

static struct thermal_zone_device_ops proc_thermal_local_ops = {
	.get_temp       = proc_thermal_get_zone_temp,
};

static int proc_thermal_add(struct device *dev,
			    struct proc_thermal_device **priv)
{
	struct proc_thermal_device *proc_priv;
	struct acpi_device *adev;
	acpi_status status;
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *elements, *ppcc;
	union acpi_object *p;
	unsigned long long tmp;
	struct thermal_zone_device_ops *ops = NULL;
	int i;
	int ret;

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return -ENODEV;

	status = acpi_evaluate_object(adev->handle, "PPCC", NULL, &buf);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	p = buf.pointer;
	if (!p || (p->type != ACPI_TYPE_PACKAGE)) {
		dev_err(dev, "Invalid PPCC data\n");
		ret = -EFAULT;
		goto free_buffer;
	}
	if (!p->package.count) {
		dev_err(dev, "Invalid PPCC package size\n");
		ret = -EFAULT;
		goto free_buffer;
	}

	proc_priv = devm_kzalloc(dev, sizeof(*proc_priv), GFP_KERNEL);
	if (!proc_priv) {
		ret = -ENOMEM;
		goto free_buffer;
	}

	proc_priv->dev = dev;
	proc_priv->adev = adev;

	for (i = 0; i < min((int)p->package.count - 1, 2); ++i) {
		elements = &(p->package.elements[i+1]);
		if (elements->type != ACPI_TYPE_PACKAGE ||
		    elements->package.count != 6) {
			ret = -EFAULT;
			goto free_buffer;
		}
		ppcc = elements->package.elements;
		proc_priv->power_limits[i].index = ppcc[0].integer.value;
		proc_priv->power_limits[i].min_uw = ppcc[1].integer.value;
		proc_priv->power_limits[i].max_uw = ppcc[2].integer.value;
		proc_priv->power_limits[i].tmin_us = ppcc[3].integer.value;
		proc_priv->power_limits[i].tmax_us = ppcc[4].integer.value;
		proc_priv->power_limits[i].step_uw = ppcc[5].integer.value;
	}

	*priv = proc_priv;

	ret = sysfs_create_group(&dev->kobj,
				 &power_limit_attribute_group);
	if (ret)
		goto free_buffer;

	status = acpi_evaluate_integer(adev->handle, "_TMP", NULL, &tmp);
	if (ACPI_FAILURE(status)) {
		/* there is no _TMP method, add local method */
		stored_tjmax = get_tjmax();
		if (stored_tjmax > 0)
			ops = &proc_thermal_local_ops;
	}

	proc_priv->int340x_zone = int340x_thermal_zone_add(adev, ops);
	if (IS_ERR(proc_priv->int340x_zone)) {
		sysfs_remove_group(&proc_priv->dev->kobj,
			   &power_limit_attribute_group);
		ret = PTR_ERR(proc_priv->int340x_zone);
	} else
		ret = 0;

free_buffer:
	kfree(buf.pointer);

	return ret;
}

static void proc_thermal_remove(struct proc_thermal_device *proc_priv)
{
	int340x_thermal_zone_remove(proc_priv->int340x_zone);
	sysfs_remove_group(&proc_priv->dev->kobj,
			   &power_limit_attribute_group);
}

static int int3401_add(struct platform_device *pdev)
{
	struct proc_thermal_device *proc_priv;
	int ret;

	if (proc_thermal_emum_mode == PROC_THERMAL_PCI) {
		dev_err(&pdev->dev, "error: enumerated as PCI dev\n");
		return -ENODEV;
	}

	ret = proc_thermal_add(&pdev->dev, &proc_priv);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, proc_priv);
	proc_thermal_emum_mode = PROC_THERMAL_PLATFORM_DEV;

	return 0;
}

static int int3401_remove(struct platform_device *pdev)
{
	proc_thermal_remove(platform_get_drvdata(pdev));

	return 0;
}

static irqreturn_t proc_thermal_pci_msi_irq(int irq, void *devid)
{
	struct proc_thermal_device *proc_priv;
	struct pci_dev *pdev = devid;

	proc_priv = pci_get_drvdata(pdev);

	intel_soc_dts_iosf_interrupt_handler(proc_priv->soc_dts);

	return IRQ_HANDLED;
}

static int  proc_thermal_pci_probe(struct pci_dev *pdev,
				   const struct pci_device_id *unused)
{
	struct proc_thermal_device *proc_priv;
	int ret;

	if (proc_thermal_emum_mode == PROC_THERMAL_PLATFORM_DEV) {
		dev_err(&pdev->dev, "error: enumerated as platform dev\n");
		return -ENODEV;
	}

	ret = pci_enable_device(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "error: could not enable device\n");
		return ret;
	}

	ret = proc_thermal_add(&pdev->dev, &proc_priv);
	if (ret) {
		pci_disable_device(pdev);
		return ret;
	}

	pci_set_drvdata(pdev, proc_priv);
	proc_thermal_emum_mode = PROC_THERMAL_PCI;

	if (pdev->device == PCI_DEVICE_ID_PROC_BSW_THERMAL) {
		/*
		 * Enumerate additional DTS sensors available via IOSF.
		 * But we are not treating as a failure condition, if
		 * there are no aux DTSs enabled or fails. This driver
		 * already exposes sensors, which can be accessed via
		 * ACPI/MSR. So we don't want to fail for auxiliary DTSs.
		 */
		proc_priv->soc_dts = intel_soc_dts_iosf_init(
					INTEL_SOC_DTS_INTERRUPT_MSI, 2, 0);

		if (proc_priv->soc_dts && pdev->irq) {
			ret = pci_enable_msi(pdev);
			if (!ret) {
				ret = request_threaded_irq(pdev->irq, NULL,
						proc_thermal_pci_msi_irq,
						IRQF_ONESHOT, "proc_thermal",
						pdev);
				if (ret) {
					intel_soc_dts_iosf_exit(
							proc_priv->soc_dts);
					pci_disable_msi(pdev);
					proc_priv->soc_dts = NULL;
				}
			}
		} else
			dev_err(&pdev->dev, "No auxiliary DTSs enabled\n");
	}

	return 0;
}

static void  proc_thermal_pci_remove(struct pci_dev *pdev)
{
	struct proc_thermal_device *proc_priv = pci_get_drvdata(pdev);

	if (proc_priv->soc_dts) {
		intel_soc_dts_iosf_exit(proc_priv->soc_dts);
		if (pdev->irq) {
			free_irq(pdev->irq, pdev);
			pci_disable_msi(pdev);
		}
	}
	proc_thermal_remove(proc_priv);
	pci_disable_device(pdev);
}

static const struct pci_device_id proc_thermal_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PROC_BDW_THERMAL)},
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PROC_HSB_THERMAL)},
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_PROC_BSW_THERMAL)},
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, proc_thermal_pci_ids);

static struct pci_driver proc_thermal_pci_driver = {
	.name		= "proc_thermal",
	.probe		= proc_thermal_pci_probe,
	.remove		= proc_thermal_pci_remove,
	.id_table	= proc_thermal_pci_ids,
};

static const struct acpi_device_id int3401_device_ids[] = {
	{"INT3401", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, int3401_device_ids);

static struct platform_driver int3401_driver = {
	.probe = int3401_add,
	.remove = int3401_remove,
	.driver = {
		.name = "int3401 thermal",
		.acpi_match_table = int3401_device_ids,
	},
};

static int __init proc_thermal_init(void)
{
	int ret;

	ret = platform_driver_register(&int3401_driver);
	if (ret)
		return ret;

	ret = pci_register_driver(&proc_thermal_pci_driver);

	return ret;
}

static void __exit proc_thermal_exit(void)
{
	platform_driver_unregister(&int3401_driver);
	pci_unregister_driver(&proc_thermal_pci_driver);
}

module_init(proc_thermal_init);
module_exit(proc_thermal_exit);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_DESCRIPTION("Processor Thermal Reporting Device Driver");
MODULE_LICENSE("GPL v2");
