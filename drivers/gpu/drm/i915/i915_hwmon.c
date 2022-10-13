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
#include "gt/intel_gt_regs.h"

/*
 * SF_* - scale factors for particular quantities according to hwmon spec.
 * - voltage  - millivolts
 */
#define SF_VOLTAGE	1000

struct hwm_reg {
	i915_reg_t gt_perf_status;
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
	HWMON_CHANNEL_INFO(in, HWMON_I_INPUT),
	NULL
};

static umode_t
hwm_in_is_visible(const struct hwm_drvdata *ddat, u32 attr)
{
	struct drm_i915_private *i915 = ddat->uncore->i915;

	switch (attr) {
	case hwmon_in_input:
		return IS_DG1(i915) || IS_DG2(i915) ? 0444 : 0;
	default:
		return 0;
	}
}

static int
hwm_in_read(struct hwm_drvdata *ddat, u32 attr, long *val)
{
	struct i915_hwmon *hwmon = ddat->hwmon;
	intel_wakeref_t wakeref;
	u32 reg_value;

	switch (attr) {
	case hwmon_in_input:
		with_intel_runtime_pm(ddat->uncore->rpm, wakeref)
			reg_value = intel_uncore_read(ddat->uncore, hwmon->rg.gt_perf_status);
		/* HW register value in units of 2.5 millivolt */
		*val = DIV_ROUND_CLOSEST(REG_FIELD_GET(GEN12_VOLTAGE_MASK, reg_value) * 25, 10);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t
hwm_is_visible(const void *drvdata, enum hwmon_sensor_types type,
	       u32 attr, int channel)
{
	struct hwm_drvdata *ddat = (struct hwm_drvdata *)drvdata;

	switch (type) {
	case hwmon_in:
		return hwm_in_is_visible(ddat, attr);
	default:
		return 0;
	}
}

static int
hwm_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
	 int channel, long *val)
{
	struct hwm_drvdata *ddat = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_in:
		return hwm_in_read(ddat, attr, val);
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
	struct i915_hwmon *hwmon = i915->hwmon;

	/* Available for all Gen12+/dGfx */
	hwmon->rg.gt_perf_status = GEN12_RPSTAT1;
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
