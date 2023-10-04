// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/kernel.h>

#include "adf_accel_devices.h"
#include "adf_pm_dbgfs.h"

static ssize_t pm_status_read(struct file *f, char __user *buf, size_t count,
			      loff_t *pos)
{
	struct adf_accel_dev *accel_dev = file_inode(f)->i_private;
	struct adf_pm pm = accel_dev->power_management;

	if (pm.print_pm_status)
		return pm.print_pm_status(accel_dev, buf, count, pos);

	return count;
}

static const struct file_operations pm_status_fops = {
	.owner = THIS_MODULE,
	.read = pm_status_read,
};

void adf_pm_dbgfs_add(struct adf_accel_dev *accel_dev)
{
	struct adf_pm *pm = &accel_dev->power_management;

	if (!pm->present || !pm->print_pm_status)
		return;

	pm->debugfs_pm_status = debugfs_create_file("pm_status", 0400,
						    accel_dev->debugfs_dir,
						    accel_dev, &pm_status_fops);
}

void adf_pm_dbgfs_rm(struct adf_accel_dev *accel_dev)
{
	struct adf_pm *pm = &accel_dev->power_management;

	if (!pm->present)
		return;

	debugfs_remove(pm->debugfs_pm_status);
	pm->debugfs_pm_status = NULL;
}
