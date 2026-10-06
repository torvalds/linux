// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD SFH sysfs attributes
 *
 * Copyright (c) 2026, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/string_choices.h>
#include <linux/sysfs.h>

#include "amd_sfh_common.h"
#include "amd_sfh_sysfs.h"
#include "sfh1_1/amd_sfh_init.h"
#include "sfh1_1/amd_sfh_interface.h"

static ssize_t hpd_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct amd_mp2_dev *mp2 = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", str_enabled_disabled(mp2->dev_en.is_hpd_enabled));
}

static ssize_t hpd_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct amd_mp2_dev *mp2 = dev_get_drvdata(dev);
	bool enabled;
	int ret;

	ret = kstrtobool(buf, &enabled);
	if (ret)
		return ret;

	mp2->sfh1_1_ops->toggle_hpd(mp2, enabled);

	return count;
}
static DEVICE_ATTR_RW(hpd);

/* Operating-mode telemetry reported by the SRA sensor, exposed read-only. */
static int amd_sfh_read_op_mode(struct amd_mp2_dev *mp2, struct sfh_op_mode *mode)
{
	struct sfh_accel_data accel_data;
	void __iomem *sensoraddr;

	if (!mp2->sfh1_1_ops || !mp2->dev_en.is_sra_present)
		return -ENODEV;

	if (mp2->mp2_ver >= MP2_VER_1_2) {
		sensoraddr = mp2->vsbase +
			(SRA_IDX * SENSOR_DATA_MEM_SIZE_DEFAULT) +
			OFFSET_SENSOR_DATA_DEFAULT;
		memcpy_fromio(&accel_data, sensoraddr, sizeof(struct sfh_accel_data));
		mode->val = accel_data.c2p3_data;
	} else {
		mode->val = readl(mp2->mmio + amd_get_c2p_val(mp2, 3));
	}

	return 0;
}

#define SFH_OP_MODE_ATTR(_name, _field)					\
static ssize_t _name##_show(struct device *dev,				\
			    struct device_attribute *attr, char *buf)	\
{									\
	struct amd_mp2_dev *mp2 = dev_get_drvdata(dev);			\
	struct sfh_op_mode mode;					\
	int ret;							\
									\
	ret = amd_sfh_read_op_mode(mp2, &mode);				\
	if (ret)							\
		return ret;						\
									\
	return sysfs_emit(buf, "%u\n", mode.op_mode._field);		\
}									\
static DEVICE_ATTR_RO(_name)

SFH_OP_MODE_ATTR(in_bag_state, inbagstate);
SFH_OP_MODE_ATTR(out_bag_state, outbagstate);
SFH_OP_MODE_ATTR(on_table_state, ontablestate);
SFH_OP_MODE_ATTR(hinge_angle, angle);
SFH_OP_MODE_ATTR(lid_status, lidstatus);
SFH_OP_MODE_ATTR(device_mode, devicemode);

static umode_t sfh_attr_is_visible(struct kobject *kobj, struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct amd_mp2_dev *mp2 = dev_get_drvdata(dev);

	if (!mp2->sfh1_1_ops)
		return 0;

	if (attr == &dev_attr_hpd.attr) {
		if (!mp2->dev_en.is_hpd_present)
			return 0;
	} else if (!mp2->dev_en.is_sra_present) {
		/* operating-mode telemetry requires the SRA sensor */
		return 0;
	}

	return attr->mode;
}

static struct attribute *sfh_attrs[] = {
	&dev_attr_hpd.attr,
	&dev_attr_in_bag_state.attr,
	&dev_attr_out_bag_state.attr,
	&dev_attr_on_table_state.attr,
	&dev_attr_hinge_angle.attr,
	&dev_attr_lid_status.attr,
	&dev_attr_device_mode.attr,
	NULL,
};

static struct attribute_group sfh_attr_group = {
	.attrs = sfh_attrs,
	.is_visible = sfh_attr_is_visible,
};

const struct attribute_group *amd_sfh_groups[] = {
	&sfh_attr_group,
	NULL,
};

int amd_sfh_update_sysfs(struct amd_mp2_dev *mp2)
{
	return sysfs_update_group(&mp2->pdev->dev.kobj, &sfh_attr_group);
}
