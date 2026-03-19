// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2026 Intel Corporation */
#include <linux/sysfs.h>
#include <linux/types.h>

#include "adf_anti_rb.h"
#include "adf_common_drv.h"
#include "adf_sysfs_anti_rb.h"

static ssize_t enforced_min_show(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	struct adf_accel_dev *accel_dev;
	int err;
	u8 svn;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	err = adf_anti_rb_query(accel_dev, ARB_ENFORCED_MIN_SVN, &svn);
	if (err)
		return err;

	return sysfs_emit(buf, "%u\n", svn);
}
static DEVICE_ATTR_RO(enforced_min);

static ssize_t active_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct adf_accel_dev *accel_dev;
	int err;
	u8 svn;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	err = adf_anti_rb_query(accel_dev, ARB_ACTIVE_SVN, &svn);
	if (err)
		return err;

	return sysfs_emit(buf, "%u\n", svn);
}
static DEVICE_ATTR_RO(active);

static ssize_t permanent_min_show(struct device *dev, struct device_attribute *attr,
				  char *buf)
{
	struct adf_accel_dev *accel_dev;
	int err;
	u8 svn;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	err = adf_anti_rb_query(accel_dev, ARB_PERMANENT_MIN_SVN, &svn);
	if (err)
		return err;

	return sysfs_emit(buf, "%u\n", svn);
}
static DEVICE_ATTR_RO(permanent_min);

static ssize_t commit_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct adf_accel_dev *accel_dev;
	bool val;
	int err;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	err = kstrtobool(buf, &val);
	if (err)
		return err;

	if (!val)
		return -EINVAL;

	err = adf_anti_rb_commit(accel_dev);
	if (err)
		return err;

	return count;
}
static DEVICE_ATTR_WO(commit);

static struct attribute *qat_svn_attrs[] = {
	&dev_attr_commit.attr,
	&dev_attr_active.attr,
	&dev_attr_enforced_min.attr,
	&dev_attr_permanent_min.attr,
	NULL
};

static const struct attribute_group qat_svn_group = {
	.attrs = qat_svn_attrs,
	.name = "qat_svn",
};

void adf_sysfs_start_arb(struct adf_accel_dev *accel_dev)
{
	struct adf_anti_rb_hw_data *anti_rb = GET_ANTI_RB_DATA(accel_dev);

	if (!anti_rb->anti_rb_enabled || !anti_rb->anti_rb_enabled(accel_dev))
		return;

	if (device_add_group(&GET_DEV(accel_dev), &qat_svn_group)) {
		dev_warn(&GET_DEV(accel_dev),
			 "Failed to create qat_svn attribute group\n");
		return;
	}

	anti_rb->sysfs_added = true;
}

void adf_sysfs_stop_arb(struct adf_accel_dev *accel_dev)
{
	struct adf_anti_rb_hw_data *anti_rb = GET_ANTI_RB_DATA(accel_dev);

	if (!anti_rb->sysfs_added)
		return;

	device_remove_group(&GET_DEV(accel_dev), &qat_svn_group);

	anti_rb->sysfs_added = false;
	anti_rb->svncheck_retry = 0;
}
