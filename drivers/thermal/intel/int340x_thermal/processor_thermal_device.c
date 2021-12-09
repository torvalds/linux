// SPDX-License-Identifier: GPL-2.0-only
/*
 * processor_thermal_device.c
 * Copyright (c) 2014, Intel Corporation.
 */
#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/thermal.h>
#include "int340x_thermal_zone.h"
#include "processor_thermal_device.h"
#include "../intel_soc_dts_iosf.h"

#define DRV_NAME "proc_thermal"

#define POWER_LIMIT_SHOW(index, suffix) \
static ssize_t power_limit_##index##_##suffix##_show(struct device *dev, \
					struct device_attribute *attr, \
					char *buf) \
{ \
	struct proc_thermal_device *proc_dev = dev_get_drvdata(dev); \
	\
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

static const struct attribute_group power_limit_attribute_group = {
	.attrs = power_limit_attrs,
	.name = "power_limits"
};

static ssize_t tcc_offset_degree_celsius_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	u64 val;
	int err;

	err = rdmsrl_safe(MSR_IA32_TEMPERATURE_TARGET, &val);
	if (err)
		return err;

	val = (val >> 24) & 0x3f;
	return sprintf(buf, "%d\n", (int)val);
}

static int tcc_offset_update(unsigned int tcc)
{
	u64 val;
	int err;

	if (tcc > 63)
		return -EINVAL;

	err = rdmsrl_safe(MSR_IA32_TEMPERATURE_TARGET, &val);
	if (err)
		return err;

	if (val & BIT(31))
		return -EPERM;

	val &= ~GENMASK_ULL(29, 24);
	val |= (tcc & 0x3f) << 24;

	err = wrmsrl_safe(MSR_IA32_TEMPERATURE_TARGET, val);
	if (err)
		return err;

	return 0;
}

static int tcc_offset_save = -1;

static ssize_t tcc_offset_degree_celsius_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	unsigned int tcc;
	u64 val;
	int err;

	err = rdmsrl_safe(MSR_PLATFORM_INFO, &val);
	if (err)
		return err;

	if (!(val & BIT(30)))
		return -EACCES;

	if (kstrtouint(buf, 0, &tcc))
		return -EINVAL;

	err = tcc_offset_update(tcc);
	if (err)
		return err;

	tcc_offset_save = tcc;

	return count;
}

static DEVICE_ATTR_RW(tcc_offset_degree_celsius);

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

static int read_temp_msr(int *temp)
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
					 int *temp)
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

static int proc_thermal_read_ppcc(struct proc_thermal_device *proc_priv)
{
	int i;
	acpi_status status;
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *elements, *ppcc;
	union acpi_object *p;
	int ret = 0;

	status = acpi_evaluate_object(proc_priv->adev->handle, "PPCC",
				      NULL, &buf);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	p = buf.pointer;
	if (!p || (p->type != ACPI_TYPE_PACKAGE)) {
		dev_err(proc_priv->dev, "Invalid PPCC data\n");
		ret = -EFAULT;
		goto free_buffer;
	}

	if (!p->package.count) {
		dev_err(proc_priv->dev, "Invalid PPCC package size\n");
		ret = -EFAULT;
		goto free_buffer;
	}

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

free_buffer:
	kfree(buf.pointer);

	return ret;
}

#define PROC_POWER_CAPABILITY_CHANGED	0x83
static void proc_thermal_notify(acpi_handle handle, u32 event, void *data)
{
	struct proc_thermal_device *proc_priv = data;

	if (!proc_priv)
		return;

	switch (event) {
	case PROC_POWER_CAPABILITY_CHANGED:
		proc_thermal_read_ppcc(proc_priv);
		int340x_thermal_zone_device_update(proc_priv->int340x_zone,
				THERMAL_DEVICE_POWER_CAPABILITY_CHANGED);
		break;
	default:
		dev_dbg(proc_priv->dev, "Unsupported event [0x%x]\n", event);
		break;
	}
}

int proc_thermal_add(struct device *dev, struct proc_thermal_device *proc_priv)
{
	struct acpi_device *adev;
	acpi_status status;
	unsigned long long tmp;
	struct thermal_zone_device_ops *ops = NULL;
	int ret;

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return -ENODEV;

	proc_priv->dev = dev;
	proc_priv->adev = adev;

	ret = proc_thermal_read_ppcc(proc_priv);
	if (ret)
		return ret;

	status = acpi_evaluate_integer(adev->handle, "_TMP", NULL, &tmp);
	if (ACPI_FAILURE(status)) {
		/* there is no _TMP method, add local method */
		stored_tjmax = get_tjmax();
		if (stored_tjmax > 0)
			ops = &proc_thermal_local_ops;
	}

	proc_priv->int340x_zone = int340x_thermal_zone_add(adev, ops);
	if (IS_ERR(proc_priv->int340x_zone)) {
		return PTR_ERR(proc_priv->int340x_zone);
	} else
		ret = 0;

	ret = acpi_install_notify_handler(adev->handle, ACPI_DEVICE_NOTIFY,
					  proc_thermal_notify,
					  (void *)proc_priv);
	if (ret)
		goto remove_zone;

	ret = sysfs_create_file(&dev->kobj, &dev_attr_tcc_offset_degree_celsius.attr);
	if (ret)
		goto remove_notify;

	ret = sysfs_create_group(&dev->kobj, &power_limit_attribute_group);
	if (ret) {
		sysfs_remove_file(&dev->kobj, &dev_attr_tcc_offset_degree_celsius.attr);
		goto remove_notify;
	}

	return 0;

remove_notify:
	acpi_remove_notify_handler(adev->handle,
				    ACPI_DEVICE_NOTIFY, proc_thermal_notify);
remove_zone:
	int340x_thermal_zone_remove(proc_priv->int340x_zone);

	return ret;
}
EXPORT_SYMBOL_GPL(proc_thermal_add);

void proc_thermal_remove(struct proc_thermal_device *proc_priv)
{
	acpi_remove_notify_handler(proc_priv->adev->handle,
				   ACPI_DEVICE_NOTIFY, proc_thermal_notify);
	int340x_thermal_zone_remove(proc_priv->int340x_zone);
	sysfs_remove_file(&proc_priv->dev->kobj, &dev_attr_tcc_offset_degree_celsius.attr);
	sysfs_remove_group(&proc_priv->dev->kobj,
			   &power_limit_attribute_group);
}
EXPORT_SYMBOL_GPL(proc_thermal_remove);

int proc_thermal_resume(struct device *dev)
{
	struct proc_thermal_device *proc_dev;

	proc_dev = dev_get_drvdata(dev);
	proc_thermal_read_ppcc(proc_dev);

	if (tcc_offset_save >= 0)
		tcc_offset_update(tcc_offset_save);

	return 0;
}
EXPORT_SYMBOL_GPL(proc_thermal_resume);

#define MCHBAR 0

static int proc_thermal_set_mmio_base(struct pci_dev *pdev, struct proc_thermal_device *proc_priv)
{
	int ret;

	ret = pcim_iomap_regions(pdev, 1 << MCHBAR, DRV_NAME);
	if (ret) {
		dev_err(&pdev->dev, "cannot reserve PCI memory region\n");
		return -ENOMEM;
	}

	proc_priv->mmio_base = pcim_iomap_table(pdev)[MCHBAR];

	return 0;
}

int proc_thermal_mmio_add(struct pci_dev *pdev,
			  struct proc_thermal_device *proc_priv,
			  kernel_ulong_t feature_mask)
{
	int ret;

	proc_priv->mmio_feature_mask = feature_mask;

	if (feature_mask) {
		ret = proc_thermal_set_mmio_base(pdev, proc_priv);
		if (ret)
			return ret;
	}

	if (feature_mask & PROC_THERMAL_FEATURE_RAPL) {
		ret = proc_thermal_rapl_add(pdev, proc_priv);
		if (ret) {
			dev_err(&pdev->dev, "failed to add RAPL MMIO interface\n");
			return ret;
		}
	}

	if (feature_mask & PROC_THERMAL_FEATURE_FIVR ||
	    feature_mask & PROC_THERMAL_FEATURE_DVFS) {
		ret = proc_thermal_rfim_add(pdev, proc_priv);
		if (ret) {
			dev_err(&pdev->dev, "failed to add RFIM interface\n");
			goto err_rem_rapl;
		}
	}

	if (feature_mask & PROC_THERMAL_FEATURE_MBOX) {
		ret = proc_thermal_mbox_add(pdev, proc_priv);
		if (ret) {
			dev_err(&pdev->dev, "failed to add MBOX interface\n");
			goto err_rem_rfim;
		}
	}

	return 0;

err_rem_rfim:
	proc_thermal_rfim_remove(pdev);
err_rem_rapl:
	proc_thermal_rapl_remove();

	return ret;
}
EXPORT_SYMBOL_GPL(proc_thermal_mmio_add);

void proc_thermal_mmio_remove(struct pci_dev *pdev, struct proc_thermal_device *proc_priv)
{
	if (proc_priv->mmio_feature_mask & PROC_THERMAL_FEATURE_RAPL)
		proc_thermal_rapl_remove();

	if (proc_priv->mmio_feature_mask & PROC_THERMAL_FEATURE_FIVR ||
	    proc_priv->mmio_feature_mask & PROC_THERMAL_FEATURE_DVFS)
		proc_thermal_rfim_remove(pdev);

	if (proc_priv->mmio_feature_mask & PROC_THERMAL_FEATURE_MBOX)
		proc_thermal_mbox_remove(pdev);
}
EXPORT_SYMBOL_GPL(proc_thermal_mmio_remove);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_DESCRIPTION("Processor Thermal Reporting Device Driver");
MODULE_LICENSE("GPL v2");
