// SPDX-License-Identifier: GPL-2.0-only
/*
 * dptf_pch_fivr:  DPTF PCH FIVR Participant driver
 * Copyright (c) 2020, Intel Corporation.
 */

#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

/*
 * Presentation of attributes which are defined for INT1045
 * They are:
 * freq_mhz_low_clock : Set PCH FIVR switching freq for
 *			FIVR clock 19.2MHz and 24MHz
 * freq_mhz_high_clock : Set PCH FIVR switching freq for
 *			FIVR clock 38.4MHz
 */
#define PCH_FIVR_SHOW(name, method) \
static ssize_t name##_show(struct device *dev,\
			   struct device_attribute *attr,\
			   char *buf)\
{\
	struct acpi_device *acpi_dev = dev_get_drvdata(dev);\
	unsigned long long val;\
	acpi_status status;\
\
	status = acpi_evaluate_integer(acpi_dev->handle, #method,\
				       NULL, &val);\
	if (ACPI_SUCCESS(status))\
		return sprintf(buf, "%d\n", (int)val);\
	else\
		return -EINVAL;\
}

#define PCH_FIVR_STORE(name, method) \
static ssize_t name##_store(struct device *dev,\
			    struct device_attribute *attr,\
			    const char *buf, size_t count)\
{\
	struct acpi_device *acpi_dev = dev_get_drvdata(dev);\
	acpi_status status;\
	u32 val;\
\
	if (kstrtouint(buf, 0, &val) < 0)\
		return -EINVAL;\
\
	status = acpi_execute_simple_method(acpi_dev->handle, #method, val);\
	if (ACPI_SUCCESS(status))\
		return count;\
\
	return -EINVAL;\
}

PCH_FIVR_SHOW(freq_mhz_low_clock, GFC0)
PCH_FIVR_SHOW(freq_mhz_high_clock, GFC1)
PCH_FIVR_STORE(freq_mhz_low_clock, RFC0)
PCH_FIVR_STORE(freq_mhz_high_clock, RFC1)

static DEVICE_ATTR_RW(freq_mhz_low_clock);
static DEVICE_ATTR_RW(freq_mhz_high_clock);

static struct attribute *fivr_attrs[] = {
	&dev_attr_freq_mhz_low_clock.attr,
	&dev_attr_freq_mhz_high_clock.attr,
	NULL
};

static const struct attribute_group pch_fivr_attribute_group = {
	.attrs = fivr_attrs,
	.name = "pch_fivr_switch_frequency"
};

static int pch_fivr_add(struct platform_device *pdev)
{
	struct acpi_device *acpi_dev;
	unsigned long long ptype;
	acpi_status status;
	int result;

	acpi_dev = ACPI_COMPANION(&(pdev->dev));
	if (!acpi_dev)
		return -ENODEV;

	status = acpi_evaluate_integer(acpi_dev->handle, "PTYP", NULL, &ptype);
	if (ACPI_FAILURE(status) || ptype != 0x05)
		return -ENODEV;

	result = sysfs_create_group(&pdev->dev.kobj,
				    &pch_fivr_attribute_group);
	if (result)
		return result;

	platform_set_drvdata(pdev, acpi_dev);

	return 0;
}

static int pch_fivr_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &pch_fivr_attribute_group);

	return 0;
}

static const struct acpi_device_id pch_fivr_device_ids[] = {
	{"INTC1045", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, pch_fivr_device_ids);

static struct platform_driver pch_fivr_driver = {
	.probe = pch_fivr_add,
	.remove = pch_fivr_remove,
	.driver = {
		.name = "DPTF PCH FIVR",
		.acpi_match_table = pch_fivr_device_ids,
	},
};

module_platform_driver(pch_fivr_driver);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ACPI DPTF PCH FIVR driver");
