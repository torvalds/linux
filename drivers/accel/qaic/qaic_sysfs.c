// SPDX-License-Identifier: GPL-2.0-only

/* Copyright (c) 2020-2025, The Linux Foundation. All rights reserved. */

#include <drm/drm_file.h>
#include <drm/drm_managed.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>

#include "qaic.h"

#define NAME_LEN		14

struct dbc_attribute {
	struct device_attribute dev_attr;
	u32 dbc_id;
	char name[NAME_LEN];
};

static ssize_t dbc_state_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct dbc_attribute *dbc_attr = container_of(a, struct dbc_attribute, dev_attr);
	struct drm_minor *minor = dev_get_drvdata(dev);
	struct qaic_device *qdev;

	qdev = to_qaic_device(minor->dev);
	return sysfs_emit(buf, "%d\n", qdev->dbc[dbc_attr->dbc_id].state);
}

void set_dbc_state(struct qaic_device *qdev, u32 dbc_id, unsigned int state)
{
	struct device *kdev = to_accel_kdev(qdev->qddev);
	char *envp[3] = {};
	char state_str[16];
	char id_str[12];

	envp[0] = id_str;
	envp[1] = state_str;

	if (state >= DBC_STATE_MAX)
		return;
	if (dbc_id >= qdev->num_dbc)
		return;
	if (state == qdev->dbc[dbc_id].state)
		return;

	scnprintf(id_str, ARRAY_SIZE(id_str), "DBC_ID=%d", dbc_id);
	scnprintf(state_str, ARRAY_SIZE(state_str), "DBC_STATE=%d", state);

	qdev->dbc[dbc_id].state = state;
	kobject_uevent_env(&kdev->kobj, KOBJ_CHANGE, envp);
}

int qaic_sysfs_init(struct qaic_drm_device *qddev)
{
	struct device *kdev = to_accel_kdev(qddev);
	struct drm_device *drm = to_drm(qddev);
	u32 num_dbc = qddev->qdev->num_dbc;
	struct dbc_attribute *dbc_attrs;
	int i, ret;

	dbc_attrs = drmm_kcalloc(drm, num_dbc, sizeof(*dbc_attrs), GFP_KERNEL);
	if (!dbc_attrs)
		return -ENOMEM;

	for (i = 0; i < num_dbc; ++i) {
		struct dbc_attribute *dbc_attr = &dbc_attrs[i];

		sysfs_attr_init(&dbc_attr->dev_attr.attr);
		dbc_attr->dbc_id = i;
		scnprintf(dbc_attr->name, NAME_LEN, "dbc%d_state", i);
		dbc_attr->dev_attr.attr.name = dbc_attr->name;
		dbc_attr->dev_attr.attr.mode = 0444;
		dbc_attr->dev_attr.show = dbc_state_show;
		ret = sysfs_create_file(&kdev->kobj, &dbc_attr->dev_attr.attr);
		if (ret) {
			int j;

			for (j = 0; j < i; ++j) {
				dbc_attr = &dbc_attrs[j];
				sysfs_remove_file(&kdev->kobj, &dbc_attr->dev_attr.attr);
			}
			drmm_kfree(drm, dbc_attrs);
			return ret;
		}
	}

	qddev->sysfs_attrs = dbc_attrs;
	return 0;
}

void qaic_sysfs_remove(struct qaic_drm_device *qddev)
{
	struct dbc_attribute *dbc_attrs = qddev->sysfs_attrs;
	struct device *kdev = to_accel_kdev(qddev);
	u32 num_dbc = qddev->qdev->num_dbc;
	int i;

	if (!dbc_attrs)
		return;

	qddev->sysfs_attrs = NULL;
	for (i = 0; i < num_dbc; ++i)
		sysfs_remove_file(&kdev->kobj, &dbc_attrs[i].dev_attr.attr);
	drmm_kfree(to_drm(qddev), dbc_attrs);
}
