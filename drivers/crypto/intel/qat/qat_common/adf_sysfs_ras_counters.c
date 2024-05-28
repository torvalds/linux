// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */

#include <linux/sysfs.h>
#include <linux/pci.h>
#include <linux/string.h>

#include "adf_common_drv.h"
#include "adf_sysfs_ras_counters.h"

static ssize_t errors_correctable_show(struct device *dev,
				       struct device_attribute *dev_attr,
				       char *buf)
{
	struct adf_accel_dev *accel_dev;
	unsigned long counter;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	counter = ADF_RAS_ERR_CTR_READ(accel_dev->ras_errors, ADF_RAS_CORR);
	return scnprintf(buf, PAGE_SIZE, "%ld\n", counter);
}

static ssize_t errors_nonfatal_show(struct device *dev,
				    struct device_attribute *dev_attr,
				    char *buf)
{
	struct adf_accel_dev *accel_dev;
	unsigned long counter;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	counter = ADF_RAS_ERR_CTR_READ(accel_dev->ras_errors, ADF_RAS_UNCORR);
	return scnprintf(buf, PAGE_SIZE, "%ld\n", counter);
}

static ssize_t errors_fatal_show(struct device *dev,
				 struct device_attribute *dev_attr,
				 char *buf)
{
	struct adf_accel_dev *accel_dev;
	unsigned long counter;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	counter = ADF_RAS_ERR_CTR_READ(accel_dev->ras_errors, ADF_RAS_FATAL);
	return scnprintf(buf, PAGE_SIZE, "%ld\n", counter);
}

static ssize_t reset_error_counters_store(struct device *dev,
					  struct device_attribute *dev_attr,
					  const char *buf, size_t count)
{
	struct adf_accel_dev *accel_dev;

	if (buf[0] != '1' || count != 2)
		return -EINVAL;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	ADF_RAS_ERR_CTR_CLEAR(accel_dev->ras_errors);

	return count;
}

static DEVICE_ATTR_RO(errors_correctable);
static DEVICE_ATTR_RO(errors_nonfatal);
static DEVICE_ATTR_RO(errors_fatal);
static DEVICE_ATTR_WO(reset_error_counters);

static struct attribute *qat_ras_attrs[] = {
	&dev_attr_errors_correctable.attr,
	&dev_attr_errors_nonfatal.attr,
	&dev_attr_errors_fatal.attr,
	&dev_attr_reset_error_counters.attr,
	NULL,
};

static struct attribute_group qat_ras_group = {
	.attrs = qat_ras_attrs,
	.name = "qat_ras",
};

void adf_sysfs_start_ras(struct adf_accel_dev *accel_dev)
{
	if (!accel_dev->ras_errors.enabled)
		return;

	ADF_RAS_ERR_CTR_CLEAR(accel_dev->ras_errors);

	if (device_add_group(&GET_DEV(accel_dev), &qat_ras_group))
		dev_err(&GET_DEV(accel_dev),
			"Failed to create qat_ras attribute group.\n");

	accel_dev->ras_errors.sysfs_added = true;
}

void adf_sysfs_stop_ras(struct adf_accel_dev *accel_dev)
{
	if (!accel_dev->ras_errors.enabled)
		return;

	if (accel_dev->ras_errors.sysfs_added) {
		device_remove_group(&GET_DEV(accel_dev), &qat_ras_group);
		accel_dev->ras_errors.sysfs_added = false;
	}

	ADF_RAS_ERR_CTR_CLEAR(accel_dev->ras_errors);
}
