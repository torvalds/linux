// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/hwmon-sysfs.h>
#include <linux/hwmon.h>
#include <linux/types.h>

#include <drm/drm_managed.h>
#include "regs/xe_gt_regs.h"
#include "regs/xe_mchbar_regs.h"
#include "regs/xe_pcode_regs.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_hwmon.h"
#include "xe_mmio.h"
#include "xe_pcode.h"
#include "xe_pcode_api.h"
#include "xe_sriov.h"

enum xe_hwmon_reg {
	REG_PKG_RAPL_LIMIT,
	REG_PKG_POWER_SKU,
	REG_PKG_POWER_SKU_UNIT,
	REG_GT_PERF_STATUS,
	REG_PKG_ENERGY_STATUS,
};

enum xe_hwmon_reg_operation {
	REG_READ32,
	REG_RMW32,
	REG_READ64,
};

/*
 * SF_* - scale factors for particular quantities according to hwmon spec.
 */
#define SF_POWER	1000000		/* microwatts */
#define SF_CURR		1000		/* milliamperes */
#define SF_VOLTAGE	1000		/* millivolts */
#define SF_ENERGY	1000000		/* microjoules */
#define SF_TIME		1000		/* milliseconds */

/**
 * struct xe_hwmon_energy_info - to accumulate energy
 */
struct xe_hwmon_energy_info {
	/** @reg_val_prev: previous energy reg val */
	u32 reg_val_prev;
	/** @accum_energy: accumulated energy */
	long accum_energy;
};

/**
 * struct xe_hwmon - xe hwmon data structure
 */
struct xe_hwmon {
	/** @hwmon_dev: hwmon device for xe */
	struct device *hwmon_dev;
	/** @gt: primary gt */
	struct xe_gt *gt;
	/** @hwmon_lock: lock for rw attributes*/
	struct mutex hwmon_lock;
	/** @scl_shift_power: pkg power unit */
	int scl_shift_power;
	/** @scl_shift_energy: pkg energy unit */
	int scl_shift_energy;
	/** @scl_shift_time: pkg time unit */
	int scl_shift_time;
	/** @ei: Energy info for energy1_input */
	struct xe_hwmon_energy_info ei;
};

static u32 xe_hwmon_get_reg(struct xe_hwmon *hwmon, enum xe_hwmon_reg hwmon_reg)
{
	struct xe_device *xe = gt_to_xe(hwmon->gt);
	struct xe_reg reg = XE_REG(0);

	switch (hwmon_reg) {
	case REG_PKG_RAPL_LIMIT:
		if (xe->info.platform == XE_PVC)
			reg = PVC_GT0_PACKAGE_RAPL_LIMIT;
		else if (xe->info.platform == XE_DG2)
			reg = PCU_CR_PACKAGE_RAPL_LIMIT;
		break;
	case REG_PKG_POWER_SKU:
		if (xe->info.platform == XE_PVC)
			reg = PVC_GT0_PACKAGE_POWER_SKU;
		else if (xe->info.platform == XE_DG2)
			reg = PCU_CR_PACKAGE_POWER_SKU;
		break;
	case REG_PKG_POWER_SKU_UNIT:
		if (xe->info.platform == XE_PVC)
			reg = PVC_GT0_PACKAGE_POWER_SKU_UNIT;
		else if (xe->info.platform == XE_DG2)
			reg = PCU_CR_PACKAGE_POWER_SKU_UNIT;
		break;
	case REG_GT_PERF_STATUS:
		if (xe->info.platform == XE_DG2)
			reg = GT_PERF_STATUS;
		break;
	case REG_PKG_ENERGY_STATUS:
		if (xe->info.platform == XE_PVC)
			reg = PVC_GT0_PLATFORM_ENERGY_STATUS;
		else if (xe->info.platform == XE_DG2)
			reg = PCU_CR_PACKAGE_ENERGY_STATUS;
		break;
	default:
		drm_warn(&xe->drm, "Unknown xe hwmon reg id: %d\n", hwmon_reg);
		break;
	}

	return reg.raw;
}

static void xe_hwmon_process_reg(struct xe_hwmon *hwmon, enum xe_hwmon_reg hwmon_reg,
				 enum xe_hwmon_reg_operation operation, u64 *value,
				 u32 clr, u32 set)
{
	struct xe_reg reg;

	reg.raw = xe_hwmon_get_reg(hwmon, hwmon_reg);

	if (!reg.raw)
		return;

	switch (operation) {
	case REG_READ32:
		*value = xe_mmio_read32(hwmon->gt, reg);
		break;
	case REG_RMW32:
		*value = xe_mmio_rmw32(hwmon->gt, reg, clr, set);
		break;
	case REG_READ64:
		*value = xe_mmio_read64_2x32(hwmon->gt, reg);
		break;
	default:
		drm_warn(&gt_to_xe(hwmon->gt)->drm, "Invalid xe hwmon reg operation: %d\n",
			 operation);
		break;
	}
}

#define PL1_DISABLE 0

/*
 * HW allows arbitrary PL1 limits to be set but silently clamps these values to
 * "typical but not guaranteed" min/max values in REG_PKG_POWER_SKU. Follow the
 * same pattern for sysfs, allow arbitrary PL1 limits to be set but display
 * clamped values when read.
 */
static void xe_hwmon_power_max_read(struct xe_hwmon *hwmon, long *value)
{
	u64 reg_val, min, max;

	mutex_lock(&hwmon->hwmon_lock);

	xe_hwmon_process_reg(hwmon, REG_PKG_RAPL_LIMIT, REG_READ32, &reg_val, 0, 0);
	/* Check if PL1 limit is disabled */
	if (!(reg_val & PKG_PWR_LIM_1_EN)) {
		*value = PL1_DISABLE;
		goto unlock;
	}

	reg_val = REG_FIELD_GET(PKG_PWR_LIM_1, reg_val);
	*value = mul_u64_u32_shr(reg_val, SF_POWER, hwmon->scl_shift_power);

	xe_hwmon_process_reg(hwmon, REG_PKG_POWER_SKU, REG_READ64, &reg_val, 0, 0);
	min = REG_FIELD_GET(PKG_MIN_PWR, reg_val);
	min = mul_u64_u32_shr(min, SF_POWER, hwmon->scl_shift_power);
	max = REG_FIELD_GET(PKG_MAX_PWR, reg_val);
	max = mul_u64_u32_shr(max, SF_POWER, hwmon->scl_shift_power);

	if (min && max)
		*value = clamp_t(u64, *value, min, max);
unlock:
	mutex_unlock(&hwmon->hwmon_lock);
}

static int xe_hwmon_power_max_write(struct xe_hwmon *hwmon, long value)
{
	int ret = 0;
	u64 reg_val;

	mutex_lock(&hwmon->hwmon_lock);

	/* Disable PL1 limit and verify, as limit cannot be disabled on all platforms */
	if (value == PL1_DISABLE) {
		xe_hwmon_process_reg(hwmon, REG_PKG_RAPL_LIMIT, REG_RMW32, &reg_val,
				     PKG_PWR_LIM_1_EN, 0);
		xe_hwmon_process_reg(hwmon, REG_PKG_RAPL_LIMIT, REG_READ32, &reg_val,
				     PKG_PWR_LIM_1_EN, 0);

		if (reg_val & PKG_PWR_LIM_1_EN) {
			ret = -EOPNOTSUPP;
			goto unlock;
		}
	}

	/* Computation in 64-bits to avoid overflow. Round to nearest. */
	reg_val = DIV_ROUND_CLOSEST_ULL((u64)value << hwmon->scl_shift_power, SF_POWER);
	reg_val = PKG_PWR_LIM_1_EN | REG_FIELD_PREP(PKG_PWR_LIM_1, reg_val);

	xe_hwmon_process_reg(hwmon, REG_PKG_RAPL_LIMIT, REG_RMW32, &reg_val,
			     PKG_PWR_LIM_1_EN | PKG_PWR_LIM_1, reg_val);
unlock:
	mutex_unlock(&hwmon->hwmon_lock);
	return ret;
}

static void xe_hwmon_power_rated_max_read(struct xe_hwmon *hwmon, long *value)
{
	u64 reg_val;

	xe_hwmon_process_reg(hwmon, REG_PKG_POWER_SKU, REG_READ32, &reg_val, 0, 0);
	reg_val = REG_FIELD_GET(PKG_TDP, reg_val);
	*value = mul_u64_u32_shr(reg_val, SF_POWER, hwmon->scl_shift_power);
}

/*
 * xe_hwmon_energy_get - Obtain energy value
 *
 * The underlying energy hardware register is 32-bits and is subject to
 * overflow. How long before overflow? For example, with an example
 * scaling bit shift of 14 bits (see register *PACKAGE_POWER_SKU_UNIT) and
 * a power draw of 1000 watts, the 32-bit counter will overflow in
 * approximately 4.36 minutes.
 *
 * Examples:
 *    1 watt:  (2^32 >> 14) /    1 W / (60 * 60 * 24) secs/day -> 3 days
 * 1000 watts: (2^32 >> 14) / 1000 W / 60             secs/min -> 4.36 minutes
 *
 * The function significantly increases overflow duration (from 4.36
 * minutes) by accumulating the energy register into a 'long' as allowed by
 * the hwmon API. Using x86_64 128 bit arithmetic (see mul_u64_u32_shr()),
 * a 'long' of 63 bits, SF_ENERGY of 1e6 (~20 bits) and
 * hwmon->scl_shift_energy of 14 bits we have 57 (63 - 20 + 14) bits before
 * energy1_input overflows. This at 1000 W is an overflow duration of 278 years.
 */
static void
xe_hwmon_energy_get(struct xe_hwmon *hwmon, long *energy)
{
	struct xe_hwmon_energy_info *ei = &hwmon->ei;
	u64 reg_val;

	xe_hwmon_process_reg(hwmon, REG_PKG_ENERGY_STATUS, REG_READ32,
			     &reg_val, 0, 0);

	if (reg_val >= ei->reg_val_prev)
		ei->accum_energy += reg_val - ei->reg_val_prev;
	else
		ei->accum_energy += UINT_MAX - ei->reg_val_prev + reg_val;

	ei->reg_val_prev = reg_val;

	*energy = mul_u64_u32_shr(ei->accum_energy, SF_ENERGY,
				  hwmon->scl_shift_energy);
}

static ssize_t
xe_hwmon_power1_max_interval_show(struct device *dev, struct device_attribute *attr,
				  char *buf)
{
	struct xe_hwmon *hwmon = dev_get_drvdata(dev);
	u32 x, y, x_w = 2; /* 2 bits */
	u64 r, tau4, out;

	xe_device_mem_access_get(gt_to_xe(hwmon->gt));

	mutex_lock(&hwmon->hwmon_lock);

	xe_hwmon_process_reg(hwmon, REG_PKG_RAPL_LIMIT,
			     REG_READ32, &r, 0, 0);

	mutex_unlock(&hwmon->hwmon_lock);

	xe_device_mem_access_put(gt_to_xe(hwmon->gt));

	x = REG_FIELD_GET(PKG_PWR_LIM_1_TIME_X, r);
	y = REG_FIELD_GET(PKG_PWR_LIM_1_TIME_Y, r);

	/*
	 * tau = 1.x * power(2,y), x = bits(23:22), y = bits(21:17)
	 *     = (4 | x) << (y - 2)
	 *
	 * Here (y - 2) ensures a 1.x fixed point representation of 1.x
	 * As x is 2 bits so 1.x can be 1.0, 1.25, 1.50, 1.75
	 *
	 * As y can be < 2, we compute tau4 = (4 | x) << y
	 * and then add 2 when doing the final right shift to account for units
	 */
	tau4 = ((1 << x_w) | x) << y;

	/* val in hwmon interface units (millisec) */
	out = mul_u64_u32_shr(tau4, SF_TIME, hwmon->scl_shift_time + x_w);

	return sysfs_emit(buf, "%llu\n", out);
}

static ssize_t
xe_hwmon_power1_max_interval_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct xe_hwmon *hwmon = dev_get_drvdata(dev);
	u32 x, y, rxy, x_w = 2; /* 2 bits */
	u64 tau4, r, max_win;
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	/*
	 * Max HW supported tau in '1.x * power(2,y)' format, x = 0, y = 0x12.
	 * The hwmon->scl_shift_time default of 0xa results in a max tau of 256 seconds.
	 *
	 * The ideal scenario is for PKG_MAX_WIN to be read from the PKG_PWR_SKU register.
	 * However, it is observed that existing discrete GPUs does not provide correct
	 * PKG_MAX_WIN value, therefore a using default constant value. For future discrete GPUs
	 * this may get resolved, in which case PKG_MAX_WIN should be obtained from PKG_PWR_SKU.
	 */
#define PKG_MAX_WIN_DEFAULT 0x12ull

	/*
	 * val must be < max in hwmon interface units. The steps below are
	 * explained in xe_hwmon_power1_max_interval_show()
	 */
	r = FIELD_PREP(PKG_MAX_WIN, PKG_MAX_WIN_DEFAULT);
	x = REG_FIELD_GET(PKG_MAX_WIN_X, r);
	y = REG_FIELD_GET(PKG_MAX_WIN_Y, r);
	tau4 = ((1 << x_w) | x) << y;
	max_win = mul_u64_u32_shr(tau4, SF_TIME, hwmon->scl_shift_time + x_w);

	if (val > max_win)
		return -EINVAL;

	/* val in hw units */
	val = DIV_ROUND_CLOSEST_ULL((u64)val << hwmon->scl_shift_time, SF_TIME);

	/*
	 * Convert val to 1.x * power(2,y)
	 * y = ilog2(val)
	 * x = (val - (1 << y)) >> (y - 2)
	 */
	if (!val) {
		y = 0;
		x = 0;
	} else {
		y = ilog2(val);
		x = (val - (1ul << y)) << x_w >> y;
	}

	rxy = REG_FIELD_PREP(PKG_PWR_LIM_1_TIME_X, x) | REG_FIELD_PREP(PKG_PWR_LIM_1_TIME_Y, y);

	xe_device_mem_access_get(gt_to_xe(hwmon->gt));

	mutex_lock(&hwmon->hwmon_lock);

	xe_hwmon_process_reg(hwmon, REG_PKG_RAPL_LIMIT, REG_RMW32, (u64 *)&r,
			     PKG_PWR_LIM_1_TIME, rxy);

	mutex_unlock(&hwmon->hwmon_lock);

	xe_device_mem_access_put(gt_to_xe(hwmon->gt));

	return count;
}

static SENSOR_DEVICE_ATTR(power1_max_interval, 0664,
			  xe_hwmon_power1_max_interval_show,
			  xe_hwmon_power1_max_interval_store, 0);

static struct attribute *hwmon_attributes[] = {
	&sensor_dev_attr_power1_max_interval.dev_attr.attr,
	NULL
};

static umode_t xe_hwmon_attributes_visible(struct kobject *kobj,
					   struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xe_hwmon *hwmon = dev_get_drvdata(dev);
	int ret = 0;

	xe_device_mem_access_get(gt_to_xe(hwmon->gt));

	if (attr == &sensor_dev_attr_power1_max_interval.dev_attr.attr)
		ret = xe_hwmon_get_reg(hwmon, REG_PKG_RAPL_LIMIT) ? attr->mode : 0;

	xe_device_mem_access_put(gt_to_xe(hwmon->gt));

	return ret;
}

static const struct attribute_group hwmon_attrgroup = {
	.attrs = hwmon_attributes,
	.is_visible = xe_hwmon_attributes_visible,
};

static const struct attribute_group *hwmon_groups[] = {
	&hwmon_attrgroup,
	NULL
};

static const struct hwmon_channel_info * const hwmon_info[] = {
	HWMON_CHANNEL_INFO(power, HWMON_P_MAX | HWMON_P_RATED_MAX | HWMON_P_CRIT),
	HWMON_CHANNEL_INFO(curr, HWMON_C_CRIT),
	HWMON_CHANNEL_INFO(in, HWMON_I_INPUT),
	HWMON_CHANNEL_INFO(energy, HWMON_E_INPUT),
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
			     uval, NULL);
}

static int xe_hwmon_pcode_write_i1(struct xe_gt *gt, u32 uval)
{
	return xe_pcode_write(gt, PCODE_MBOX(PCODE_POWER_SETUP,
			      POWER_SETUP_SUBCOMMAND_WRITE_I1, 0),
			      uval);
}

static int xe_hwmon_power_curr_crit_read(struct xe_hwmon *hwmon, long *value, u32 scale_factor)
{
	int ret;
	u32 uval;

	mutex_lock(&hwmon->hwmon_lock);

	ret = xe_hwmon_pcode_read_i1(hwmon->gt, &uval);
	if (ret)
		goto unlock;

	*value = mul_u64_u32_shr(REG_FIELD_GET(POWER_SETUP_I1_DATA_MASK, uval),
				 scale_factor, POWER_SETUP_I1_SHIFT);
unlock:
	mutex_unlock(&hwmon->hwmon_lock);
	return ret;
}

static int xe_hwmon_power_curr_crit_write(struct xe_hwmon *hwmon, long value, u32 scale_factor)
{
	int ret;
	u32 uval;

	mutex_lock(&hwmon->hwmon_lock);

	uval = DIV_ROUND_CLOSEST_ULL(value << POWER_SETUP_I1_SHIFT, scale_factor);
	ret = xe_hwmon_pcode_write_i1(hwmon->gt, uval);

	mutex_unlock(&hwmon->hwmon_lock);
	return ret;
}

static void xe_hwmon_get_voltage(struct xe_hwmon *hwmon, long *value)
{
	u64 reg_val;

	xe_hwmon_process_reg(hwmon, REG_GT_PERF_STATUS,
			     REG_READ32, &reg_val, 0, 0);
	/* HW register value in units of 2.5 millivolt */
	*value = DIV_ROUND_CLOSEST(REG_FIELD_GET(VOLTAGE_MASK, reg_val) * 2500, SF_VOLTAGE);
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
	switch (attr) {
	case hwmon_power_max:
		xe_hwmon_power_max_read(hwmon, val);
		return 0;
	case hwmon_power_rated_max:
		xe_hwmon_power_rated_max_read(hwmon, val);
		return 0;
	case hwmon_power_crit:
		return xe_hwmon_power_curr_crit_read(hwmon, val, SF_POWER);
	default:
		return -EOPNOTSUPP;
	}
}

static int
xe_hwmon_power_write(struct xe_hwmon *hwmon, u32 attr, int chan, long val)
{
	switch (attr) {
	case hwmon_power_max:
		return xe_hwmon_power_max_write(hwmon, val);
	case hwmon_power_crit:
		return xe_hwmon_power_curr_crit_write(hwmon, val, SF_POWER);
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
	switch (attr) {
	case hwmon_curr_crit:
		return xe_hwmon_power_curr_crit_read(hwmon, val, SF_CURR);
	default:
		return -EOPNOTSUPP;
	}
}

static int
xe_hwmon_curr_write(struct xe_hwmon *hwmon, u32 attr, long val)
{
	switch (attr) {
	case hwmon_curr_crit:
		return xe_hwmon_power_curr_crit_write(hwmon, val, SF_CURR);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t
xe_hwmon_in_is_visible(struct xe_hwmon *hwmon, u32 attr)
{
	switch (attr) {
	case hwmon_in_input:
		return xe_hwmon_get_reg(hwmon, REG_GT_PERF_STATUS) ? 0444 : 0;
	default:
		return 0;
	}
}

static int
xe_hwmon_in_read(struct xe_hwmon *hwmon, u32 attr, long *val)
{
	switch (attr) {
	case hwmon_in_input:
		xe_hwmon_get_voltage(hwmon, val);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t
xe_hwmon_energy_is_visible(struct xe_hwmon *hwmon, u32 attr)
{
	switch (attr) {
	case hwmon_energy_input:
		return xe_hwmon_get_reg(hwmon, REG_PKG_ENERGY_STATUS) ? 0444 : 0;
	default:
		return 0;
	}
}

static int
xe_hwmon_energy_read(struct xe_hwmon *hwmon, u32 attr, long *val)
{
	switch (attr) {
	case hwmon_energy_input:
		xe_hwmon_energy_get(hwmon, val);
		return 0;
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
	case hwmon_in:
		ret = xe_hwmon_in_is_visible(hwmon, attr);
		break;
	case hwmon_energy:
		ret = xe_hwmon_energy_is_visible(hwmon, attr);
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
	case hwmon_in:
		ret = xe_hwmon_in_read(hwmon, attr, val);
		break;
	case hwmon_energy:
		ret = xe_hwmon_energy_read(hwmon, attr, val);
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
	long energy;
	u64 val_sku_unit = 0;

	/*
	 * The contents of register PKG_POWER_SKU_UNIT do not change,
	 * so read it once and store the shift values.
	 */
	if (xe_hwmon_get_reg(hwmon, REG_PKG_POWER_SKU_UNIT)) {
		xe_hwmon_process_reg(hwmon, REG_PKG_POWER_SKU_UNIT,
				     REG_READ32, &val_sku_unit, 0, 0);
		hwmon->scl_shift_power = REG_FIELD_GET(PKG_PWR_UNIT, val_sku_unit);
		hwmon->scl_shift_energy = REG_FIELD_GET(PKG_ENERGY_UNIT, val_sku_unit);
		hwmon->scl_shift_time = REG_FIELD_GET(PKG_TIME_UNIT, val_sku_unit);
	}

	/*
	 * Initialize 'struct xe_hwmon_energy_info', i.e. set fields to the
	 * first value of the energy register read
	 */
	if (xe_hwmon_is_visible(hwmon, hwmon_energy, hwmon_energy_input, 0))
		xe_hwmon_energy_get(hwmon, &energy);
}

static void xe_hwmon_mutex_destroy(void *arg)
{
	struct xe_hwmon *hwmon = arg;

	mutex_destroy(&hwmon->hwmon_lock);
}

void xe_hwmon_register(struct xe_device *xe)
{
	struct device *dev = xe->drm.dev;
	struct xe_hwmon *hwmon;

	/* hwmon is available only for dGfx */
	if (!IS_DGFX(xe))
		return;

	/* hwmon is not available on VFs */
	if (IS_SRIOV_VF(xe))
		return;

	hwmon = devm_kzalloc(dev, sizeof(*hwmon), GFP_KERNEL);
	if (!hwmon)
		return;

	xe->hwmon = hwmon;

	mutex_init(&hwmon->hwmon_lock);
	if (devm_add_action_or_reset(dev, xe_hwmon_mutex_destroy, hwmon))
		return;

	/* primary GT to access device level properties */
	hwmon->gt = xe->tiles[0].primary_gt;

	xe_hwmon_get_preregistration_info(xe);

	drm_dbg(&xe->drm, "Register xe hwmon interface\n");

	/*  hwmon_dev points to device hwmon<i> */
	hwmon->hwmon_dev = devm_hwmon_device_register_with_info(dev, "xe", hwmon,
								&hwmon_chip_info,
								hwmon_groups);

	if (IS_ERR(hwmon->hwmon_dev)) {
		drm_warn(&xe->drm, "Failed to register xe hwmon (%pe)\n", hwmon->hwmon_dev);
		xe->hwmon = NULL;
		return;
	}
}

