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
 * - power  - microwatts
 */
#define SF_VOLTAGE	1000
#define SF_POWER	1000000

struct hwm_reg {
	i915_reg_t gt_perf_status;
	i915_reg_t pkg_power_sku_unit;
	i915_reg_t pkg_power_sku;
	i915_reg_t pkg_rapl_limit;
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
	int scl_shift_power;
};

static void
hwm_locked_with_pm_intel_uncore_rmw(struct hwm_drvdata *ddat,
				    i915_reg_t reg, u32 clear, u32 set)
{
	struct i915_hwmon *hwmon = ddat->hwmon;
	struct intel_uncore *uncore = ddat->uncore;
	intel_wakeref_t wakeref;

	mutex_lock(&hwmon->hwmon_lock);

	with_intel_runtime_pm(uncore->rpm, wakeref)
		intel_uncore_rmw(uncore, reg, clear, set);

	mutex_unlock(&hwmon->hwmon_lock);
}

/*
 * This function's return type of u64 allows for the case where the scaling
 * of the field taken from the 32-bit register value might cause a result to
 * exceed 32 bits.
 */
static u64
hwm_field_read_and_scale(struct hwm_drvdata *ddat, i915_reg_t rgadr,
			 u32 field_msk, int nshift, u32 scale_factor)
{
	struct intel_uncore *uncore = ddat->uncore;
	intel_wakeref_t wakeref;
	u32 reg_value;

	with_intel_runtime_pm(uncore->rpm, wakeref)
		reg_value = intel_uncore_read(uncore, rgadr);

	reg_value = REG_FIELD_GET(field_msk, reg_value);

	return mul_u64_u32_shr(reg_value, scale_factor, nshift);
}

static void
hwm_field_scale_and_write(struct hwm_drvdata *ddat, i915_reg_t rgadr,
			  u32 field_msk, int nshift,
			  unsigned int scale_factor, long lval)
{
	u32 nval;
	u32 bits_to_clear;
	u32 bits_to_set;

	/* Computation in 64-bits to avoid overflow. Round to nearest. */
	nval = DIV_ROUND_CLOSEST_ULL((u64)lval << nshift, scale_factor);

	bits_to_clear = field_msk;
	bits_to_set = FIELD_PREP(field_msk, nval);

	hwm_locked_with_pm_intel_uncore_rmw(ddat, rgadr,
					    bits_to_clear, bits_to_set);
}

static const struct hwmon_channel_info *hwm_info[] = {
	HWMON_CHANNEL_INFO(in, HWMON_I_INPUT),
	HWMON_CHANNEL_INFO(power, HWMON_P_MAX | HWMON_P_RATED_MAX),
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
hwm_power_is_visible(const struct hwm_drvdata *ddat, u32 attr, int chan)
{
	struct i915_hwmon *hwmon = ddat->hwmon;

	switch (attr) {
	case hwmon_power_max:
		return i915_mmio_reg_valid(hwmon->rg.pkg_rapl_limit) ? 0664 : 0;
	case hwmon_power_rated_max:
		return i915_mmio_reg_valid(hwmon->rg.pkg_power_sku) ? 0444 : 0;
	default:
		return 0;
	}
}

static int
hwm_power_read(struct hwm_drvdata *ddat, u32 attr, int chan, long *val)
{
	struct i915_hwmon *hwmon = ddat->hwmon;

	switch (attr) {
	case hwmon_power_max:
		*val = hwm_field_read_and_scale(ddat,
						hwmon->rg.pkg_rapl_limit,
						PKG_PWR_LIM_1,
						hwmon->scl_shift_power,
						SF_POWER);
		return 0;
	case hwmon_power_rated_max:
		*val = hwm_field_read_and_scale(ddat,
						hwmon->rg.pkg_power_sku,
						PKG_PKG_TDP,
						hwmon->scl_shift_power,
						SF_POWER);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int
hwm_power_write(struct hwm_drvdata *ddat, u32 attr, int chan, long val)
{
	struct i915_hwmon *hwmon = ddat->hwmon;

	switch (attr) {
	case hwmon_power_max:
		hwm_field_scale_and_write(ddat,
					  hwmon->rg.pkg_rapl_limit,
					  PKG_PWR_LIM_1,
					  hwmon->scl_shift_power,
					  SF_POWER, val);
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
	case hwmon_power:
		return hwm_power_is_visible(ddat, attr, channel);
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
	case hwmon_power:
		return hwm_power_read(ddat, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int
hwm_write(struct device *dev, enum hwmon_sensor_types type, u32 attr,
	  int channel, long val)
{
	struct hwm_drvdata *ddat = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_power:
		return hwm_power_write(ddat, attr, channel, val);
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
	struct intel_uncore *uncore = &i915->uncore;
	intel_wakeref_t wakeref;
	u32 val_sku_unit = 0;

	/* Available for all Gen12+/dGfx */
	hwmon->rg.gt_perf_status = GEN12_RPSTAT1;

	if (IS_DG1(i915) || IS_DG2(i915)) {
		hwmon->rg.pkg_power_sku_unit = PCU_PACKAGE_POWER_SKU_UNIT;
		hwmon->rg.pkg_power_sku = PCU_PACKAGE_POWER_SKU;
		hwmon->rg.pkg_rapl_limit = PCU_PACKAGE_RAPL_LIMIT;
	} else {
		hwmon->rg.pkg_power_sku_unit = INVALID_MMIO_REG;
		hwmon->rg.pkg_power_sku = INVALID_MMIO_REG;
		hwmon->rg.pkg_rapl_limit = INVALID_MMIO_REG;
	}

	with_intel_runtime_pm(uncore->rpm, wakeref) {
		/*
		 * The contents of register hwmon->rg.pkg_power_sku_unit do not change,
		 * so read it once and store the shift values.
		 */
		if (i915_mmio_reg_valid(hwmon->rg.pkg_power_sku_unit))
			val_sku_unit = intel_uncore_read(uncore,
							 hwmon->rg.pkg_power_sku_unit);

		hwmon->scl_shift_power = REG_FIELD_GET(PKG_PWR_UNIT, val_sku_unit);
	}
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
