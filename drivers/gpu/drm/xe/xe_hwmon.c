// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/hwmon.h>

#include <drm/drm_managed.h>
#include "regs/xe_gt_regs.h"
#include "regs/xe_mchbar_regs.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_hwmon.h"
#include "xe_mmio.h"
#include "xe_pcode.h"
#include "xe_pcode_api.h"

enum xe_hwmon_reg {
	REG_PKG_RAPL_LIMIT,
	REG_PKG_POWER_SKU,
	REG_PKG_POWER_SKU_UNIT,
};

enum xe_hwmon_reg_operation {
	REG_READ,
	REG_WRITE,
	REG_RMW,
};

/*
 * SF_* - scale factors for particular quantities according to hwmon spec.
 */
#define SF_POWER	1000000		/* microwatts */
#define SF_CURR		1000		/* milliamperes */

struct xe_hwmon {
	struct device *hwmon_dev;
	struct xe_gt *gt;
	struct mutex hwmon_lock; /* rmw operations*/
	int scl_shift_power;
};

static u32 xe_hwmon_get_reg(struct xe_hwmon *hwmon, enum xe_hwmon_reg hwmon_reg)
{
	struct xe_device *xe = gt_to_xe(hwmon->gt);
	struct xe_reg reg = XE_REG(0);

	switch (hwmon_reg) {
	case REG_PKG_RAPL_LIMIT:
		if (xe->info.platform == XE_DG2)
			reg = PCU_CR_PACKAGE_RAPL_LIMIT;
		else if (xe->info.platform == XE_PVC)
			reg = PVC_GT0_PACKAGE_RAPL_LIMIT;
		break;
	case REG_PKG_POWER_SKU:
		if (xe->info.platform == XE_DG2)
			reg = PCU_CR_PACKAGE_POWER_SKU;
		else if (xe->info.platform == XE_PVC)
			reg = PVC_GT0_PACKAGE_POWER_SKU;
		break;
	case REG_PKG_POWER_SKU_UNIT:
		if (xe->info.platform == XE_DG2)
			reg = PCU_CR_PACKAGE_POWER_SKU_UNIT;
		else if (xe->info.platform == XE_PVC)
			reg = PVC_GT0_PACKAGE_POWER_SKU_UNIT;
		break;
	default:
		drm_warn(&xe->drm, "Unknown xe hwmon reg id: %d\n", hwmon_reg);
		break;
	}

	return reg.raw;
}

static int xe_hwmon_process_reg(struct xe_hwmon *hwmon, enum xe_hwmon_reg hwmon_reg,
				enum xe_hwmon_reg_operation operation, u32 *value,
				u32 clr, u32 set)
{
	struct xe_reg reg;

	reg.raw = xe_hwmon_get_reg(hwmon, hwmon_reg);

	if (!reg.raw)
		return -EOPNOTSUPP;

	switch (operation) {
	case REG_READ:
		*value = xe_mmio_read32(hwmon->gt, reg);
		return 0;
	case REG_WRITE:
		xe_mmio_write32(hwmon->gt, reg, *value);
		return 0;
	case REG_RMW:
		*value = xe_mmio_rmw32(hwmon->gt, reg, clr, set);
		return 0;
	default:
		drm_warn(&gt_to_xe(hwmon->gt)->drm, "Invalid xe hwmon reg operation: %d\n",
			 operation);
		return -EOPNOTSUPP;
	}
}

static int xe_hwmon_process_reg_read64(struct xe_hwmon *hwmon,
				       enum xe_hwmon_reg hwmon_reg, u64 *value)
{
	struct xe_reg reg;

	reg.raw = xe_hwmon_get_reg(hwmon, hwmon_reg);

	if (!reg.raw)
		return -EOPNOTSUPP;

	*value = xe_mmio_read64_2x32(hwmon->gt, reg);

	return 0;
}

#define PL1_DISABLE 0

/*
 * HW allows arbitrary PL1 limits to be set but silently clamps these values to
 * "typical but not guaranteed" min/max values in REG_PKG_POWER_SKU. Follow the
 * same pattern for sysfs, allow arbitrary PL1 limits to be set but display
 * clamped values when read.
 */
static int xe_hwmon_power_max_read(struct xe_hwmon *hwmon, long *value)
{
	u32 reg_val;
	u64 reg_val64, min, max;

	xe_hwmon_process_reg(hwmon, REG_PKG_RAPL_LIMIT, REG_READ, &reg_val, 0, 0);
	/* Check if PL1 limit is disabled */
	if (!(reg_val & PKG_PWR_LIM_1_EN)) {
		*value = PL1_DISABLE;
		return 0;
	}

	reg_val = REG_FIELD_GET(PKG_PWR_LIM_1, reg_val);
	*value = mul_u64_u32_shr(reg_val, SF_POWER, hwmon->scl_shift_power);

	xe_hwmon_process_reg_read64(hwmon, REG_PKG_POWER_SKU, &reg_val64);
	min = REG_FIELD_GET(PKG_MIN_PWR, reg_val64);
	min = mul_u64_u32_shr(min, SF_POWER, hwmon->scl_shift_power);
	max = REG_FIELD_GET(PKG_MAX_PWR, reg_val64);
	max = mul_u64_u32_shr(max, SF_POWER, hwmon->scl_shift_power);

	if (min && max)
		*value = clamp_t(u64, *value, min, max);

	return 0;
}

static int xe_hwmon_power_max_write(struct xe_hwmon *hwmon, long value)
{
	u32 reg_val;

	/* Disable PL1 limit and verify, as limit cannot be disabled on all platforms */
	if (value == PL1_DISABLE) {
		xe_hwmon_process_reg(hwmon, REG_PKG_RAPL_LIMIT, REG_RMW, &reg_val,
				     PKG_PWR_LIM_1_EN, 0);
		xe_hwmon_process_reg(hwmon, REG_PKG_RAPL_LIMIT, REG_READ, &reg_val,
				     PKG_PWR_LIM_1_EN, 0);

		if (reg_val & PKG_PWR_LIM_1_EN)
			return -EOPNOTSUPP;
	}

	/* Computation in 64-bits to avoid overflow. Round to nearest. */
	reg_val = DIV_ROUND_CLOSEST_ULL((u64)value << hwmon->scl_shift_power, SF_POWER);
	reg_val = PKG_PWR_LIM_1_EN | REG_FIELD_PREP(PKG_PWR_LIM_1, reg_val);

	xe_hwmon_process_reg(hwmon, REG_PKG_RAPL_LIMIT, REG_RMW, &reg_val,
			     PKG_PWR_LIM_1_EN | PKG_PWR_LIM_1, reg_val);

	return 0;
}

static int xe_hwmon_power_rated_max_read(struct xe_hwmon *hwmon, long *value)
{
	u32 reg_val;

	xe_hwmon_process_reg(hwmon, REG_PKG_POWER_SKU, REG_READ, &reg_val, 0, 0);
	reg_val = REG_FIELD_GET(PKG_TDP, reg_val);
	*value = mul_u64_u32_shr(reg_val, SF_POWER, hwmon->scl_shift_power);

	return 0;
}

static const struct hwmon_channel_info *hwmon_info[] = {
	HWMON_CHANNEL_INFO(power, HWMON_P_MAX | HWMON_P_RATED_MAX | HWMON_P_CRIT),
	HWMON_CHANNEL_INFO(curr, HWMON_C_CRIT),
	NULL
};

/* I1 is exposed as power_crit or as curr_crit depending on bit 31 */
static int xe_hwmon_pcode_read_i1(struct xe_gt *gt, u32 *uval)
{
	/* Avoid Illegal Subcommand error */
	if (gt_to_xe(gt)->info.platform == XE_DG2)
		return -ENXIO;

	return xe_pcode_read(gt, PCODE_MBOX(PCODE_POWER_SETUP,
			     POWER_SETUP_SUBCOMMAND_READ_I1, 0),
			     uval, 0);
}

static int xe_hwmon_pcode_write_i1(struct xe_gt *gt, u32 uval)
{
	return xe_pcode_write(gt, PCODE_MBOX(PCODE_POWER_SETUP,
			      POWER_SETUP_SUBCOMMAND_WRITE_I1, 0),
			      uval);
}

static umode_t
xe_hwmon_power_is_visible(struct xe_hwmon *hwmon, u32 attr, int chan)
{
	u32 uval;

	switch (attr) {
	case hwmon_power_max:
		return xe_hwmon_get_reg(hwmon, REG_PKG_RAPL_LIMIT) ? 0664 : 0;
	case hwmon_power_rated_max:
		return xe_hwmon_get_reg(hwmon, REG_PKG_POWER_SKU) ? 0444 : 0;
	case hwmon_power_crit:
		return (xe_hwmon_pcode_read_i1(hwmon->gt, &uval) ||
			!(uval & POWER_SETUP_I1_WATTS)) ? 0 : 0644;
	default:
		return 0;
	}
}

static int
xe_hwmon_power_read(struct xe_hwmon *hwmon, u32 attr, int chan, long *val)
{
	int ret;
	u32 uval;

	switch (attr) {
	case hwmon_power_max:
		return xe_hwmon_power_max_read(hwmon, val);
	case hwmon_power_rated_max:
		return xe_hwmon_power_rated_max_read(hwmon, val);
	case hwmon_power_crit:
		ret = xe_hwmon_pcode_read_i1(hwmon->gt, &uval);
		if (ret)
			return ret;
		if (!(uval & POWER_SETUP_I1_WATTS))
			return -ENODEV;
		*val = mul_u64_u32_shr(REG_FIELD_GET(POWER_SETUP_I1_DATA_MASK, uval),
				       SF_POWER, POWER_SETUP_I1_SHIFT);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int
xe_hwmon_power_write(struct xe_hwmon *hwmon, u32 attr, int chan, long val)
{
	u32 uval;

	switch (attr) {
	case hwmon_power_max:
		return xe_hwmon_power_max_write(hwmon, val);
	case hwmon_power_crit:
		uval = DIV_ROUND_CLOSEST_ULL(val << POWER_SETUP_I1_SHIFT, SF_POWER);
		return xe_hwmon_pcode_write_i1(hwmon->gt, uval);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t
xe_hwmon_curr_is_visible(const struct xe_hwmon *hwmon, u32 attr)
{
	u32 uval;

	switch (attr) {
	case hwmon_curr_crit:
		return (xe_hwmon_pcode_read_i1(hwmon->gt, &uval) ||
			(uval & POWER_SETUP_I1_WATTS)) ? 0 : 0644;
	default:
		return 0;
	}
}

static int
xe_hwmon_curr_read(struct xe_hwmon *hwmon, u32 attr, long *val)
{
	int ret;
	u32 uval;

	switch (attr) {
	case hwmon_curr_crit:
		ret = xe_hwmon_pcode_read_i1(hwmon->gt, &uval);
		if (ret)
			return ret;
		if (uval & POWER_SETUP_I1_WATTS)
			return -ENODEV;
		*val = mul_u64_u32_shr(REG_FIELD_GET(POWER_SETUP_I1_DATA_MASK, uval),
				       SF_CURR, POWER_SETUP_I1_SHIFT);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int
xe_hwmon_curr_write(struct xe_hwmon *hwmon, u32 attr, long val)
{
	u32 uval;

	switch (attr) {
	case hwmon_curr_crit:
		uval = DIV_ROUND_CLOSEST_ULL(val << POWER_SETUP_I1_SHIFT, SF_CURR);
		return xe_hwmon_pcode_write_i1(hwmon->gt, uval);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t
xe_hwmon_is_visible(const void *drvdata, enum hwmon_sensor_types type,
		    u32 attr, int channel)
{
	struct xe_hwmon *hwmon = (struct xe_hwmon *)drvdata;
	int ret;

	xe_device_mem_access_get(gt_to_xe(hwmon->gt));

	switch (type) {
	case hwmon_power:
		ret = xe_hwmon_power_is_visible(hwmon, attr, channel);
		break;
	case hwmon_curr:
		ret = xe_hwmon_curr_is_visible(hwmon, attr);
		break;
	default:
		ret = 0;
		break;
	}

	xe_device_mem_access_put(gt_to_xe(hwmon->gt));

	return ret;
}

static int
xe_hwmon_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
	      int channel, long *val)
{
	struct xe_hwmon *hwmon = dev_get_drvdata(dev);
	int ret;

	xe_device_mem_access_get(gt_to_xe(hwmon->gt));

	switch (type) {
	case hwmon_power:
		ret = xe_hwmon_power_read(hwmon, attr, channel, val);
		break;
	case hwmon_curr:
		ret = xe_hwmon_curr_read(hwmon, attr, val);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	xe_device_mem_access_put(gt_to_xe(hwmon->gt));

	return ret;
}

static int
xe_hwmon_write(struct device *dev, enum hwmon_sensor_types type, u32 attr,
	       int channel, long val)
{
	struct xe_hwmon *hwmon = dev_get_drvdata(dev);
	int ret;

	xe_device_mem_access_get(gt_to_xe(hwmon->gt));

	switch (type) {
	case hwmon_power:
		ret = xe_hwmon_power_write(hwmon, attr, channel, val);
		break;
	case hwmon_curr:
		ret = xe_hwmon_curr_write(hwmon, attr, val);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	xe_device_mem_access_put(gt_to_xe(hwmon->gt));

	return ret;
}

static const struct hwmon_ops hwmon_ops = {
	.is_visible = xe_hwmon_is_visible,
	.read = xe_hwmon_read,
	.write = xe_hwmon_write,
};

static const struct hwmon_chip_info hwmon_chip_info = {
	.ops = &hwmon_ops,
	.info = hwmon_info,
};

static void
xe_hwmon_get_preregistration_info(struct xe_device *xe)
{
	struct xe_hwmon *hwmon = xe->hwmon;
	u32 val_sku_unit = 0;
	int ret;

	ret = xe_hwmon_process_reg(hwmon, REG_PKG_POWER_SKU_UNIT, REG_READ, &val_sku_unit, 0, 0);
	/*
	 * The contents of register PKG_POWER_SKU_UNIT do not change,
	 * so read it once and store the shift values.
	 */
	if (!ret)
		hwmon->scl_shift_power = REG_FIELD_GET(PKG_PWR_UNIT, val_sku_unit);
}

void xe_hwmon_register(struct xe_device *xe)
{
	struct device *dev = xe->drm.dev;
	struct xe_hwmon *hwmon;

	/* hwmon is available only for dGfx */
	if (!IS_DGFX(xe))
		return;

	hwmon = devm_kzalloc(dev, sizeof(*hwmon), GFP_KERNEL);
	if (!hwmon)
		return;

	xe->hwmon = hwmon;

	drmm_mutex_init(&xe->drm, &hwmon->hwmon_lock);

	/* primary GT to access device level properties */
	hwmon->gt = xe->tiles[0].primary_gt;

	xe_hwmon_get_preregistration_info(xe);

	drm_dbg(&xe->drm, "Register xe hwmon interface\n");

	/*  hwmon_dev points to device hwmon<i> */
	hwmon->hwmon_dev = devm_hwmon_device_register_with_info(dev, "xe", hwmon,
								&hwmon_chip_info,
								NULL);
	if (IS_ERR(hwmon->hwmon_dev)) {
		drm_warn(&xe->drm, "Failed to register xe hwmon (%pe)\n", hwmon->hwmon_dev);
		xe->hwmon = NULL;
		return;
	}
}

