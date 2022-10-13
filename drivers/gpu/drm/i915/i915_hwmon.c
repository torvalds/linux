// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/types.h>

#include "i915_drv.h"
#include "i915_hwmon.h"
#include "i915_reg.h"
#include "intel_mchbar_regs.h"

struct hwm_reg {
};

struct hwm_drvdata {
	struct i915_hwmon *hwmon;
	struct intel_uncore *uncore;
	struct device *hwmon_dev;
	char name[12];
};

struct i915_hwmon {
	struct hwm_drvdata ddat;
	struct mutex hwmon_lock;		/* counter overflow logic and rmw */
	struct hwm_reg rg;
};

static const struct hwmon_channel_info *hwm_info[] = {
	NULL
};

static umode_t
hwm_is_visible(const void *drvdata, enum hwmon_sensor_types type,
	       u32 attr, int channel)
{
	switch (type) {
	default:
		return 0;
	}
}

static int
hwm_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
	 int channel, long *val)
{
	switch (type) {
	default:
		return -EOPNOTSUPP;
	}
}

static int
hwm_write(struct device *dev, enum hwmon_sensor_types type, u32 attr,
	  int channel, long val)
{
	switch (type) {
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_ops hwm_ops = {
	.is_visible = hwm_is_visible,
	.read = hwm_read,
	.write = hwm_write,
};

static const struct hwmon_chip_info hwm_chip_info = {
	.ops = &hwm_ops,
	.info = hwm_info,
};

static void
hwm_get_preregistration_info(struct drm_i915_private *i915)
{
}

void i915_hwmon_register(struct drm_i915_private *i915)
{
	struct device *dev = i915->drm.dev;
	struct i915_hwmon *hwmon;
	struct device *hwmon_dev;
	struct hwm_drvdata *ddat;

	/* hwmon is available only for dGfx */
	if (!IS_DGFX(i915))
		return;

	hwmon = devm_kzalloc(dev, sizeof(*hwmon), GFP_KERNEL);
	if (!hwmon)
		return;

	i915->hwmon = hwmon;
	mutex_init(&hwmon->hwmon_lock);
	ddat = &hwmon->ddat;

	ddat->hwmon = hwmon;
	ddat->uncore = &i915->uncore;
	snprintf(ddat->name, sizeof(ddat->name), "i915");

	hwm_get_preregistration_info(i915);

	/*  hwmon_dev points to device hwmon<i> */
	hwmon_dev = devm_hwmon_device_register_with_info(dev, ddat->name,
							 ddat,
							 &hwm_chip_info,
							 NULL);
	if (IS_ERR(hwmon_dev)) {
		i915->hwmon = NULL;
		return;
	}

	ddat->hwmon_dev = hwmon_dev;
}

void i915_hwmon_unregister(struct drm_i915_private *i915)
{
	fetch_and_zero(&i915->hwmon);
}
