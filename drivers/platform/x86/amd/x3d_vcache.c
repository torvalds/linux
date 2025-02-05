// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD 3D V-Cache Performance Optimizer Driver
 *
 * Copyright (c) 2024, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Authors: Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 *          Perry Yuan <perry.yuan@amd.com>
 *          Mario Limonciello <mario.limonciello@amd.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/array_size.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/sysfs.h>
#include <linux/uuid.h>

static char *x3d_mode = "frequency";
module_param(x3d_mode, charp, 0);
MODULE_PARM_DESC(x3d_mode, "Initial 3D-VCache mode; 'frequency' (default) or 'cache'");

#define DSM_REVISION_ID			0
#define DSM_SET_X3D_MODE		1

static guid_t x3d_guid = GUID_INIT(0xdff8e55f, 0xbcfd, 0x46fb, 0xba, 0x0a,
				   0xef, 0xd0, 0x45, 0x0f, 0x34, 0xee);

enum amd_x3d_mode_type {
	MODE_INDEX_FREQ,
	MODE_INDEX_CACHE,
};

static const char * const amd_x3d_mode_strings[] = {
	[MODE_INDEX_FREQ] = "frequency",
	[MODE_INDEX_CACHE] = "cache",
};

struct amd_x3d_dev {
	struct device *dev;
	acpi_handle ahandle;
	/* To protect x3d mode setting */
	struct mutex lock;
	enum amd_x3d_mode_type curr_mode;
};

static int amd_x3d_get_mode(struct amd_x3d_dev *data)
{
	guard(mutex)(&data->lock);

	return data->curr_mode;
}

static int amd_x3d_mode_switch(struct amd_x3d_dev *data, int new_state)
{
	union acpi_object *out, argv;

	guard(mutex)(&data->lock);
	argv.type = ACPI_TYPE_INTEGER;
	argv.integer.value = new_state;

	out = acpi_evaluate_dsm(data->ahandle, &x3d_guid, DSM_REVISION_ID,
				DSM_SET_X3D_MODE, &argv);
	if (!out) {
		dev_err(data->dev, "failed to evaluate _DSM\n");
		return -EINVAL;
	}

	data->curr_mode = new_state;

	kfree(out);

	return 0;
}

static ssize_t amd_x3d_mode_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct amd_x3d_dev *data = dev_get_drvdata(dev);
	int ret;

	ret = sysfs_match_string(amd_x3d_mode_strings, buf);
	if (ret < 0)
		return ret;

	ret = amd_x3d_mode_switch(data, ret);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t amd_x3d_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct amd_x3d_dev *data = dev_get_drvdata(dev);
	int mode = amd_x3d_get_mode(data);

	return sysfs_emit(buf, "%s\n", amd_x3d_mode_strings[mode]);
}
static DEVICE_ATTR_RW(amd_x3d_mode);

static struct attribute *amd_x3d_attrs[] = {
	&dev_attr_amd_x3d_mode.attr,
	NULL
};
ATTRIBUTE_GROUPS(amd_x3d);

static int amd_x3d_resume_handler(struct device *dev)
{
	struct amd_x3d_dev *data = dev_get_drvdata(dev);
	int ret = amd_x3d_get_mode(data);

	return amd_x3d_mode_switch(data, ret);
}

static DEFINE_SIMPLE_DEV_PM_OPS(amd_x3d_pm, NULL, amd_x3d_resume_handler);

static const struct acpi_device_id amd_x3d_acpi_ids[] = {
	{"AMDI0101"},
	{ },
};
MODULE_DEVICE_TABLE(acpi, amd_x3d_acpi_ids);

static int amd_x3d_probe(struct platform_device *pdev)
{
	struct amd_x3d_dev *data;
	acpi_handle handle;
	int ret;

	handle = ACPI_HANDLE(&pdev->dev);
	if (!handle)
		return -ENODEV;

	if (!acpi_check_dsm(handle, &x3d_guid, DSM_REVISION_ID, BIT(DSM_SET_X3D_MODE)))
		return -ENODEV;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = &pdev->dev;

	ret = devm_mutex_init(data->dev, &data->lock);
	if (ret)
		return ret;

	data->ahandle = handle;
	platform_set_drvdata(pdev, data);

	ret = match_string(amd_x3d_mode_strings, ARRAY_SIZE(amd_x3d_mode_strings), x3d_mode);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, -EINVAL, "invalid mode %s\n", x3d_mode);

	return amd_x3d_mode_switch(data, ret);
}

static struct platform_driver amd_3d_vcache_driver = {
	.driver = {
		.name = "amd_x3d_vcache",
		.dev_groups = amd_x3d_groups,
		.acpi_match_table = amd_x3d_acpi_ids,
		.pm = pm_sleep_ptr(&amd_x3d_pm),
	},
	.probe = amd_x3d_probe,
};
module_platform_driver(amd_3d_vcache_driver);

MODULE_DESCRIPTION("AMD 3D V-Cache Performance Optimizer Driver");
MODULE_LICENSE("GPL");
