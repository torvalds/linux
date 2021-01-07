// SPDX-License-Identifier: GPL-2.0-only
/*
 * processor_thermal_device.c
 * Copyright (c) 2014, Intel Corporation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/thermal.h>
#include <linux/cpuhotplug.h>
#include "int340x_thermal_zone.h"
#include "processor_thermal_device.h"
#include "../intel_soc_dts_iosf.h"

#define DRV_NAME "proc_thermal"

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
	struct proc_thermal_device *proc_dev = dev_get_drvdata(dev); \
	\
	if (proc_thermal_emum_mode == PROC_THERMAL_NONE) { \
		dev_warn(dev, "Attempted to get power limit before device was initialized!\n"); \
		return 0; \
	} \
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

	val = (val >> 24) & 0xff;
	return sprintf(buf, "%d\n", (int)val);
}

static int tcc_offset_update(int tcc)
{
	u64 val;
	int err;

	if (!tcc)
		return -EINVAL;

	err = rdmsrl_safe(MSR_IA32_TEMPERATURE_TARGET, &val);
	if (err)
		return err;

	val &= ~GENMASK_ULL(31, 24);
	val |= (tcc & 0xff) << 24;

	err = wrmsrl_safe(MSR_IA32_TEMPERATURE_TARGET, val);
	if (err)
		return err;

	return 0;
}

static int tcc_offset_save;

static ssize_t tcc_offset_degree_celsius_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	u64 val;
	int tcc, err;

	err = rdmsrl_safe(MSR_PLATFORM_INFO, &val);
	if (err)
		return err;

	if (!(val & BIT(30)))
		return -EACCES;

	if (kstrtoint(buf, 0, &tcc))
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


static int proc_thermal_add(struct device *dev,
			    struct proc_thermal_device **priv)
{
	struct proc_thermal_device *proc_priv;
	struct acpi_device *adev;
	acpi_status status;
	unsigned long long tmp;
	struct thermal_zone_device_ops *ops = NULL;
	int ret;

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return -ENODEV;

	proc_priv = devm_kzalloc(dev, sizeof(*proc_priv), GFP_KERNEL);
	if (!proc_priv)
		return -ENOMEM;

	proc_priv->dev = dev;
	proc_priv->adev = adev;
	*priv = proc_priv;

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

	return 0;

remove_zone:
	int340x_thermal_zone_remove(proc_priv->int340x_zone);

	return ret;
}

static void proc_thermal_remove(struct proc_thermal_device *proc_priv)
{
	acpi_remove_notify_handler(proc_priv->adev->handle,
				   ACPI_DEVICE_NOTIFY, proc_thermal_notify);
	int340x_thermal_zone_remove(proc_priv->int340x_zone);
	sysfs_remove_file(&proc_priv->dev->kobj, &dev_attr_tcc_offset_degree_celsius.attr);
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

	dev_info(&pdev->dev, "Creating sysfs group for PROC_THERMAL_PLATFORM_DEV\n");

	ret = sysfs_create_file(&pdev->dev.kobj, &dev_attr_tcc_offset_degree_celsius.attr);
	if (ret)
		return ret;

	ret = sysfs_create_group(&pdev->dev.kobj, &power_limit_attribute_group);
	if (ret)
		sysfs_remove_file(&pdev->dev.kobj, &dev_attr_tcc_offset_degree_celsius.attr);

	return ret;
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

#define MCHBAR 0

static int proc_thermal_set_mmio_base(struct pci_dev *pdev,
				      struct proc_thermal_device *proc_priv)
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

static int proc_thermal_mmio_add(struct pci_dev *pdev,
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

static void proc_thermal_mmio_remove(struct pci_dev *pdev)
{
	struct proc_thermal_device *proc_priv = pci_get_drvdata(pdev);

	if (proc_priv->mmio_feature_mask & PROC_THERMAL_FEATURE_RAPL)
		proc_thermal_rapl_remove();

	if (proc_priv->mmio_feature_mask & PROC_THERMAL_FEATURE_FIVR ||
	    proc_priv->mmio_feature_mask & PROC_THERMAL_FEATURE_DVFS)
		proc_thermal_rfim_remove(pdev);

	if (proc_priv->mmio_feature_mask & PROC_THERMAL_FEATURE_MBOX)
		proc_thermal_mbox_remove(pdev);
}

static int  proc_thermal_pci_probe(struct pci_dev *pdev,
				   const struct pci_device_id *id)
{
	struct proc_thermal_device *proc_priv;
	int ret;

	if (proc_thermal_emum_mode == PROC_THERMAL_PLATFORM_DEV) {
		dev_err(&pdev->dev, "error: enumerated as platform dev\n");
		return -ENODEV;
	}

	ret = pcim_enable_device(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "error: could not enable device\n");
		return ret;
	}

	ret = proc_thermal_add(&pdev->dev, &proc_priv);
	if (ret)
		return ret;

	pci_set_drvdata(pdev, proc_priv);
	proc_thermal_emum_mode = PROC_THERMAL_PCI;

	if (pdev->device == PCI_DEVICE_ID_INTEL_BSW_THERMAL) {
		/*
		 * Enumerate additional DTS sensors available via IOSF.
		 * But we are not treating as a failure condition, if
		 * there are no aux DTSs enabled or fails. This driver
		 * already exposes sensors, which can be accessed via
		 * ACPI/MSR. So we don't want to fail for auxiliary DTSs.
		 */
		proc_priv->soc_dts = intel_soc_dts_iosf_init(
					INTEL_SOC_DTS_INTERRUPT_MSI, 2, 0);

		if (!IS_ERR(proc_priv->soc_dts) && pdev->irq) {
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

	dev_info(&pdev->dev, "Creating sysfs group for PROC_THERMAL_PCI\n");

	ret = sysfs_create_file(&pdev->dev.kobj, &dev_attr_tcc_offset_degree_celsius.attr);
	if (ret)
		return ret;

	ret = sysfs_create_group(&pdev->dev.kobj, &power_limit_attribute_group);
	if (ret) {
		sysfs_remove_file(&pdev->dev.kobj, &dev_attr_tcc_offset_degree_celsius.attr);
		return ret;
	}

	ret = proc_thermal_mmio_add(pdev, proc_priv, id->driver_data);
	if (ret) {
		proc_thermal_remove(proc_priv);
		return ret;
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

	proc_thermal_mmio_remove(pdev);
	proc_thermal_remove(proc_priv);
}

#ifdef CONFIG_PM_SLEEP
static int proc_thermal_resume(struct device *dev)
{
	struct proc_thermal_device *proc_dev;

	proc_dev = dev_get_drvdata(dev);
	proc_thermal_read_ppcc(proc_dev);

	tcc_offset_update(tcc_offset_save);

	return 0;
}
#else
#define proc_thermal_resume NULL
#endif

static SIMPLE_DEV_PM_OPS(proc_thermal_pm, NULL, proc_thermal_resume);

static const struct pci_device_id proc_thermal_pci_ids[] = {
	{ PCI_DEVICE_DATA(INTEL, ADL_THERMAL, PROC_THERMAL_FEATURE_RAPL | PROC_THERMAL_FEATURE_FIVR | PROC_THERMAL_FEATURE_DVFS | PROC_THERMAL_FEATURE_MBOX) },
	{ PCI_DEVICE_DATA(INTEL, BDW_THERMAL, 0) },
	{ PCI_DEVICE_DATA(INTEL, BSW_THERMAL, 0) },
	{ PCI_DEVICE_DATA(INTEL, BXT0_THERMAL, 0) },
	{ PCI_DEVICE_DATA(INTEL, BXT1_THERMAL, 0) },
	{ PCI_DEVICE_DATA(INTEL, BXTX_THERMAL, 0) },
	{ PCI_DEVICE_DATA(INTEL, BXTP_THERMAL, 0) },
	{ PCI_DEVICE_DATA(INTEL, CNL_THERMAL, 0) },
	{ PCI_DEVICE_DATA(INTEL, CFL_THERMAL, 0) },
	{ PCI_DEVICE_DATA(INTEL, GLK_THERMAL, 0) },
	{ PCI_DEVICE_DATA(INTEL, HSB_THERMAL, 0) },
	{ PCI_DEVICE_DATA(INTEL, ICL_THERMAL, PROC_THERMAL_FEATURE_RAPL) },
	{ PCI_DEVICE_DATA(INTEL, JSL_THERMAL, 0) },
	{ PCI_DEVICE_DATA(INTEL, SKL_THERMAL, PROC_THERMAL_FEATURE_RAPL) },
	{ PCI_DEVICE_DATA(INTEL, TGL_THERMAL, PROC_THERMAL_FEATURE_RAPL | PROC_THERMAL_FEATURE_FIVR | PROC_THERMAL_FEATURE_MBOX) },
	{ },
};

MODULE_DEVICE_TABLE(pci, proc_thermal_pci_ids);

static struct pci_driver proc_thermal_pci_driver = {
	.name		= DRV_NAME,
	.probe		= proc_thermal_pci_probe,
	.remove		= proc_thermal_pci_remove,
	.id_table	= proc_thermal_pci_ids,
	.driver.pm	= &proc_thermal_pm,
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
		.pm = &proc_thermal_pm,
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
