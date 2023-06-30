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
#include "intel_pcode.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_regs.h"

/*
 * SF_* - scale factors for particular quantities according to hwmon spec.
 * - voltage  - millivolts
 * - power  - microwatts
 * - curr   - milliamperes
 * - energy - microjoules
 * - time   - milliseconds
 */
#define SF_VOLTAGE	1000
#define SF_POWER	1000000
#define SF_CURR		1000
#define SF_ENERGY	1000000
#define SF_TIME		1000

struct hwm_reg {
	i915_reg_t gt_perf_status;
	i915_reg_t pkg_power_sku_unit;
	i915_reg_t pkg_power_sku;
	i915_reg_t pkg_rapl_limit;
	i915_reg_t energy_status_all;
	i915_reg_t energy_status_tile;
};

struct hwm_energy_info {
	u32 reg_val_prev;
	long accum_energy;			/* Accumulated energy for energy1_input */
};

struct hwm_drvdata {
	struct i915_hwmon *hwmon;
	struct intel_uncore *uncore;
	struct device *hwmon_dev;
	struct hwm_energy_info ei;		/*  Energy info for energy1_input */
	char name[12];
	int gt_n;
	bool reset_in_progress;
	wait_queue_head_t waitq;
};

struct i915_hwmon {
	struct hwm_drvdata ddat;
	struct hwm_drvdata ddat_gt[I915_MAX_GT];
	struct mutex hwmon_lock;		/* counter overflow logic and rmw */
	struct hwm_reg rg;
	int scl_shift_power;
	int scl_shift_energy;
	int scl_shift_time;
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

/*
 * hwm_energy - Obtain energy value
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
hwm_energy(struct hwm_drvdata *ddat, long *energy)
{
	struct intel_uncore *uncore = ddat->uncore;
	struct i915_hwmon *hwmon = ddat->hwmon;
	struct hwm_energy_info *ei = &ddat->ei;
	intel_wakeref_t wakeref;
	i915_reg_t rgaddr;
	u32 reg_val;

	if (ddat->gt_n >= 0)
		rgaddr = hwmon->rg.energy_status_tile;
	else
		rgaddr = hwmon->rg.energy_status_all;

	mutex_lock(&hwmon->hwmon_lock);

	with_intel_runtime_pm(uncore->rpm, wakeref)
		reg_val = intel_uncore_read(uncore, rgaddr);

	if (reg_val >= ei->reg_val_prev)
		ei->accum_energy += reg_val - ei->reg_val_prev;
	else
		ei->accum_energy += UINT_MAX - ei->reg_val_prev + reg_val;
	ei->reg_val_prev = reg_val;

	*energy = mul_u64_u32_shr(ei->accum_energy, SF_ENERGY,
				  hwmon->scl_shift_energy);
	mutex_unlock(&hwmon->hwmon_lock);
}

static ssize_t
hwm_power1_max_interval_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct hwm_drvdata *ddat = dev_get_drvdata(dev);
	struct i915_hwmon *hwmon = ddat->hwmon;
	intel_wakeref_t wakeref;
	u32 r, x, y, x_w = 2; /* 2 bits */
	u64 tau4, out;

	with_intel_runtime_pm(ddat->uncore->rpm, wakeref)
		r = intel_uncore_read(ddat->uncore, hwmon->rg.pkg_rapl_limit);

	x = REG_FIELD_GET(PKG_PWR_LIM_1_TIME_X, r);
	y = REG_FIELD_GET(PKG_PWR_LIM_1_TIME_Y, r);
	/*
	 * tau = 1.x * power(2,y), x = bits(23:22), y = bits(21:17)
	 *     = (4 | x) << (y - 2)
	 * where (y - 2) ensures a 1.x fixed point representation of 1.x
	 * However because y can be < 2, we compute
	 *     tau4 = (4 | x) << y
	 * but add 2 when doing the final right shift to account for units
	 */
	tau4 = ((1 << x_w) | x) << y;
	/* val in hwmon interface units (millisec) */
	out = mul_u64_u32_shr(tau4, SF_TIME, hwmon->scl_shift_time + x_w);

	return sysfs_emit(buf, "%llu\n", out);
}

static ssize_t
hwm_power1_max_interval_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct hwm_drvdata *ddat = dev_get_drvdata(dev);
	struct i915_hwmon *hwmon = ddat->hwmon;
	u32 x, y, rxy, x_w = 2; /* 2 bits */
	u64 tau4, r, max_win;
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	/*
	 * Max HW supported tau in '1.x * power(2,y)' format, x = 0, y = 0x12
	 * The hwmon->scl_shift_time default of 0xa results in a max tau of 256 seconds
	 */
#define PKG_MAX_WIN_DEFAULT 0x12ull

	/*
	 * val must be < max in hwmon interface units. The steps below are
	 * explained in i915_power1_max_interval_show()
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
	/* Convert to 1.x * power(2,y) */
	if (!val) {
		/* Avoid ilog2(0) */
		y = 0;
		x = 0;
	} else {
		y = ilog2(val);
		/* x = (val - (1 << y)) >> (y - 2); */
		x = (val - (1ul << y)) << x_w >> y;
	}

	rxy = REG_FIELD_PREP(PKG_PWR_LIM_1_TIME_X, x) | REG_FIELD_PREP(PKG_PWR_LIM_1_TIME_Y, y);

	hwm_locked_with_pm_intel_uncore_rmw(ddat, hwmon->rg.pkg_rapl_limit,
					    PKG_PWR_LIM_1_TIME, rxy);
	return count;
}

static SENSOR_DEVICE_ATTR(power1_max_interval, 0664,
			  hwm_power1_max_interval_show,
			  hwm_power1_max_interval_store, 0);

static struct attribute *hwm_attributes[] = {
	&sensor_dev_attr_power1_max_interval.dev_attr.attr,
	NULL
};

static umode_t hwm_attributes_visible(struct kobject *kobj,
				      struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct hwm_drvdata *ddat = dev_get_drvdata(dev);
	struct i915_hwmon *hwmon = ddat->hwmon;

	if (attr == &sensor_dev_attr_power1_max_interval.dev_attr.attr)
		return i915_mmio_reg_valid(hwmon->rg.pkg_rapl_limit) ? attr->mode : 0;

	return 0;
}

static const struct attribute_group hwm_attrgroup = {
	.attrs = hwm_attributes,
	.is_visible = hwm_attributes_visible,
};

static const struct attribute_group *hwm_groups[] = {
	&hwm_attrgroup,
	NULL
};

static const struct hwmon_channel_info * const hwm_info[] = {
	HWMON_CHANNEL_INFO(in, HWMON_I_INPUT),
	HWMON_CHANNEL_INFO(power, HWMON_P_MAX | HWMON_P_RATED_MAX | HWMON_P_CRIT),
	HWMON_CHANNEL_INFO(energy, HWMON_E_INPUT),
	HWMON_CHANNEL_INFO(curr, HWMON_C_CRIT),
	NULL
};

static const struct hwmon_channel_info * const hwm_gt_info[] = {
	HWMON_CHANNEL_INFO(energy, HWMON_E_INPUT),
	NULL
};

/* I1 is exposed as power_crit or as curr_crit depending on bit 31 */
static int hwm_pcode_read_i1(struct drm_i915_private *i915, u32 *uval)
{
	/* Avoid ILLEGAL_SUBCOMMAND "mailbox access failed" warning in snb_pcode_read */
	if (IS_DG1(i915) || IS_DG2(i915))
		return -ENXIO;

	return snb_pcode_read_p(&i915->uncore, PCODE_POWER_SETUP,
				POWER_SETUP_SUBCOMMAND_READ_I1, 0, uval);
}

static int hwm_pcode_write_i1(struct drm_i915_private *i915, u32 uval)
{
	return  snb_pcode_write_p(&i915->uncore, PCODE_POWER_SETUP,
				  POWER_SETUP_SUBCOMMAND_WRITE_I1, 0, uval);
}

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
	struct drm_i915_private *i915 = ddat->uncore->i915;
	struct i915_hwmon *hwmon = ddat->hwmon;
	u32 uval;

	switch (attr) {
	case hwmon_power_max:
		return i915_mmio_reg_valid(hwmon->rg.pkg_rapl_limit) ? 0664 : 0;
	case hwmon_power_rated_max:
		return i915_mmio_reg_valid(hwmon->rg.pkg_power_sku) ? 0444 : 0;
	case hwmon_power_crit:
		return (hwm_pcode_read_i1(i915, &uval) ||
			!(uval & POWER_SETUP_I1_WATTS)) ? 0 : 0644;
	default:
		return 0;
	}
}

#define PL1_DISABLE 0

/*
 * HW allows arbitrary PL1 limits to be set but silently clamps these values to
 * "typical but not guaranteed" min/max values in rg.pkg_power_sku. Follow the
 * same pattern for sysfs, allow arbitrary PL1 limits to be set but display
 * clamped values when read. Write/read I1 also follows the same pattern.
 */
static int
hwm_power_max_read(struct hwm_drvdata *ddat, long *val)
{
	struct i915_hwmon *hwmon = ddat->hwmon;
	intel_wakeref_t wakeref;
	u64 r, min, max;

	/* Check if PL1 limit is disabled */
	with_intel_runtime_pm(ddat->uncore->rpm, wakeref)
		r = intel_uncore_read(ddat->uncore, hwmon->rg.pkg_rapl_limit);
	if (!(r & PKG_PWR_LIM_1_EN)) {
		*val = PL1_DISABLE;
		return 0;
	}

	*val = hwm_field_read_and_scale(ddat,
					hwmon->rg.pkg_rapl_limit,
					PKG_PWR_LIM_1,
					hwmon->scl_shift_power,
					SF_POWER);

	with_intel_runtime_pm(ddat->uncore->rpm, wakeref)
		r = intel_uncore_read64(ddat->uncore, hwmon->rg.pkg_power_sku);
	min = REG_FIELD_GET(PKG_MIN_PWR, r);
	min = mul_u64_u32_shr(min, SF_POWER, hwmon->scl_shift_power);
	max = REG_FIELD_GET(PKG_MAX_PWR, r);
	max = mul_u64_u32_shr(max, SF_POWER, hwmon->scl_shift_power);

	if (min && max)
		*val = clamp_t(u64, *val, min, max);

	return 0;
}

static int
hwm_power_max_write(struct hwm_drvdata *ddat, long val)
{
	struct i915_hwmon *hwmon = ddat->hwmon;
	intel_wakeref_t wakeref;
	DEFINE_WAIT(wait);
	int ret = 0;
	u32 nval;

	/* Block waiting for GuC reset to complete when needed */
	for (;;) {
		mutex_lock(&hwmon->hwmon_lock);

		prepare_to_wait(&ddat->waitq, &wait, TASK_INTERRUPTIBLE);

		if (!hwmon->ddat.reset_in_progress)
			break;

		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		mutex_unlock(&hwmon->hwmon_lock);

		schedule();
	}
	finish_wait(&ddat->waitq, &wait);
	if (ret)
		goto unlock;

	wakeref = intel_runtime_pm_get(ddat->uncore->rpm);

	/* Disable PL1 limit and verify, because the limit cannot be disabled on all platforms */
	if (val == PL1_DISABLE) {
		intel_uncore_rmw(ddat->uncore, hwmon->rg.pkg_rapl_limit,
				 PKG_PWR_LIM_1_EN, 0);
		nval = intel_uncore_read(ddat->uncore, hwmon->rg.pkg_rapl_limit);

		if (nval & PKG_PWR_LIM_1_EN)
			ret = -ENODEV;
		goto exit;
	}

	/* Computation in 64-bits to avoid overflow. Round to nearest. */
	nval = DIV_ROUND_CLOSEST_ULL((u64)val << hwmon->scl_shift_power, SF_POWER);
	nval = PKG_PWR_LIM_1_EN | REG_FIELD_PREP(PKG_PWR_LIM_1, nval);

	intel_uncore_rmw(ddat->uncore, hwmon->rg.pkg_rapl_limit,
			 PKG_PWR_LIM_1_EN | PKG_PWR_LIM_1, nval);
exit:
	intel_runtime_pm_put(ddat->uncore->rpm, wakeref);
unlock:
	mutex_unlock(&hwmon->hwmon_lock);
	return ret;
}

static int
hwm_power_read(struct hwm_drvdata *ddat, u32 attr, int chan, long *val)
{
	struct i915_hwmon *hwmon = ddat->hwmon;
	int ret;
	u32 uval;

	switch (attr) {
	case hwmon_power_max:
		return hwm_power_max_read(ddat, val);
	case hwmon_power_rated_max:
		*val = hwm_field_read_and_scale(ddat,
						hwmon->rg.pkg_power_sku,
						PKG_PKG_TDP,
						hwmon->scl_shift_power,
						SF_POWER);
		return 0;
	case hwmon_power_crit:
		ret = hwm_pcode_read_i1(ddat->uncore->i915, &uval);
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
hwm_power_write(struct hwm_drvdata *ddat, u32 attr, int chan, long val)
{
	u32 uval;

	switch (attr) {
	case hwmon_power_max:
		return hwm_power_max_write(ddat, val);
	case hwmon_power_crit:
		uval = DIV_ROUND_CLOSEST_ULL(val << POWER_SETUP_I1_SHIFT, SF_POWER);
		return hwm_pcode_write_i1(ddat->uncore->i915, uval);
	default:
		return -EOPNOTSUPP;
	}
}

void i915_hwmon_power_max_disable(struct drm_i915_private *i915, bool *old)
{
	struct i915_hwmon *hwmon = i915->hwmon;
	u32 r;

	if (!hwmon || !i915_mmio_reg_valid(hwmon->rg.pkg_rapl_limit))
		return;

	mutex_lock(&hwmon->hwmon_lock);

	hwmon->ddat.reset_in_progress = true;
	r = intel_uncore_rmw(hwmon->ddat.uncore, hwmon->rg.pkg_rapl_limit,
			     PKG_PWR_LIM_1_EN, 0);
	*old = !!(r & PKG_PWR_LIM_1_EN);

	mutex_unlock(&hwmon->hwmon_lock);
}

void i915_hwmon_power_max_restore(struct drm_i915_private *i915, bool old)
{
	struct i915_hwmon *hwmon = i915->hwmon;

	if (!hwmon || !i915_mmio_reg_valid(hwmon->rg.pkg_rapl_limit))
		return;

	mutex_lock(&hwmon->hwmon_lock);

	intel_uncore_rmw(hwmon->ddat.uncore, hwmon->rg.pkg_rapl_limit,
			 PKG_PWR_LIM_1_EN, old ? PKG_PWR_LIM_1_EN : 0);
	hwmon->ddat.reset_in_progress = false;
	wake_up_all(&hwmon->ddat.waitq);

	mutex_unlock(&hwmon->hwmon_lock);
}

static umode_t
hwm_energy_is_visible(const struct hwm_drvdata *ddat, u32 attr)
{
	struct i915_hwmon *hwmon = ddat->hwmon;
	i915_reg_t rgaddr;

	switch (attr) {
	case hwmon_energy_input:
		if (ddat->gt_n >= 0)
			rgaddr = hwmon->rg.energy_status_tile;
		else
			rgaddr = hwmon->rg.energy_status_all;
		return i915_mmio_reg_valid(rgaddr) ? 0444 : 0;
	default:
		return 0;
	}
}

static int
hwm_energy_read(struct hwm_drvdata *ddat, u32 attr, long *val)
{
	switch (attr) {
	case hwmon_energy_input:
		hwm_energy(ddat, val);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t
hwm_curr_is_visible(const struct hwm_drvdata *ddat, u32 attr)
{
	struct drm_i915_private *i915 = ddat->uncore->i915;
	u32 uval;

	switch (attr) {
	case hwmon_curr_crit:
		return (hwm_pcode_read_i1(i915, &uval) ||
			(uval & POWER_SETUP_I1_WATTS)) ? 0 : 0644;
	default:
		return 0;
	}
}

static int
hwm_curr_read(struct hwm_drvdata *ddat, u32 attr, long *val)
{
	int ret;
	u32 uval;

	switch (attr) {
	case hwmon_curr_crit:
		ret = hwm_pcode_read_i1(ddat->uncore->i915, &uval);
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
hwm_curr_write(struct hwm_drvdata *ddat, u32 attr, long val)
{
	u32 uval;

	switch (attr) {
	case hwmon_curr_crit:
		uval = DIV_ROUND_CLOSEST_ULL(val << POWER_SETUP_I1_SHIFT, SF_CURR);
		return hwm_pcode_write_i1(ddat->uncore->i915, uval);
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
	case hwmon_energy:
		return hwm_energy_is_visible(ddat, attr);
	case hwmon_curr:
		return hwm_curr_is_visible(ddat, attr);
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
	case hwmon_energy:
		return hwm_energy_read(ddat, attr, val);
	case hwmon_curr:
		return hwm_curr_read(ddat, attr, val);
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
	case hwmon_curr:
		return hwm_curr_write(ddat, attr, val);
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

static umode_t
hwm_gt_is_visible(const void *drvdata, enum hwmon_sensor_types type,
		  u32 attr, int channel)
{
	struct hwm_drvdata *ddat = (struct hwm_drvdata *)drvdata;

	switch (type) {
	case hwmon_energy:
		return hwm_energy_is_visible(ddat, attr);
	default:
		return 0;
	}
}

static int
hwm_gt_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
	    int channel, long *val)
{
	struct hwm_drvdata *ddat = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_energy:
		return hwm_energy_read(ddat, attr, val);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_ops hwm_gt_ops = {
	.is_visible = hwm_gt_is_visible,
	.read = hwm_gt_read,
};

static const struct hwmon_chip_info hwm_gt_chip_info = {
	.ops = &hwm_gt_ops,
	.info = hwm_gt_info,
};

static void
hwm_get_preregistration_info(struct drm_i915_private *i915)
{
	struct i915_hwmon *hwmon = i915->hwmon;
	struct intel_uncore *uncore = &i915->uncore;
	struct hwm_drvdata *ddat = &hwmon->ddat;
	intel_wakeref_t wakeref;
	u32 val_sku_unit = 0;
	struct intel_gt *gt;
	long energy;
	int i;

	/* Available for all Gen12+/dGfx */
	hwmon->rg.gt_perf_status = GEN12_RPSTAT1;

	if (IS_DG1(i915) || IS_DG2(i915)) {
		hwmon->rg.pkg_power_sku_unit = PCU_PACKAGE_POWER_SKU_UNIT;
		hwmon->rg.pkg_power_sku = PCU_PACKAGE_POWER_SKU;
		hwmon->rg.pkg_rapl_limit = PCU_PACKAGE_RAPL_LIMIT;
		hwmon->rg.energy_status_all = PCU_PACKAGE_ENERGY_STATUS;
		hwmon->rg.energy_status_tile = INVALID_MMIO_REG;
	} else if (IS_XEHPSDV(i915)) {
		hwmon->rg.pkg_power_sku_unit = GT0_PACKAGE_POWER_SKU_UNIT;
		hwmon->rg.pkg_power_sku = INVALID_MMIO_REG;
		hwmon->rg.pkg_rapl_limit = GT0_PACKAGE_RAPL_LIMIT;
		hwmon->rg.energy_status_all = GT0_PLATFORM_ENERGY_STATUS;
		hwmon->rg.energy_status_tile = GT0_PACKAGE_ENERGY_STATUS;
	} else {
		hwmon->rg.pkg_power_sku_unit = INVALID_MMIO_REG;
		hwmon->rg.pkg_power_sku = INVALID_MMIO_REG;
		hwmon->rg.pkg_rapl_limit = INVALID_MMIO_REG;
		hwmon->rg.energy_status_all = INVALID_MMIO_REG;
		hwmon->rg.energy_status_tile = INVALID_MMIO_REG;
	}

	with_intel_runtime_pm(uncore->rpm, wakeref) {
		/*
		 * The contents of register hwmon->rg.pkg_power_sku_unit do not change,
		 * so read it once and store the shift values.
		 */
		if (i915_mmio_reg_valid(hwmon->rg.pkg_power_sku_unit))
			val_sku_unit = intel_uncore_read(uncore,
							 hwmon->rg.pkg_power_sku_unit);
	}

	hwmon->scl_shift_power = REG_FIELD_GET(PKG_PWR_UNIT, val_sku_unit);
	hwmon->scl_shift_energy = REG_FIELD_GET(PKG_ENERGY_UNIT, val_sku_unit);
	hwmon->scl_shift_time = REG_FIELD_GET(PKG_TIME_UNIT, val_sku_unit);

	/*
	 * Initialize 'struct hwm_energy_info', i.e. set fields to the
	 * first value of the energy register read
	 */
	if (i915_mmio_reg_valid(hwmon->rg.energy_status_all))
		hwm_energy(ddat, &energy);
	if (i915_mmio_reg_valid(hwmon->rg.energy_status_tile)) {
		for_each_gt(gt, i915, i)
			hwm_energy(&hwmon->ddat_gt[i], &energy);
	}
}

void i915_hwmon_register(struct drm_i915_private *i915)
{
	struct device *dev = i915->drm.dev;
	struct i915_hwmon *hwmon;
	struct device *hwmon_dev;
	struct hwm_drvdata *ddat;
	struct hwm_drvdata *ddat_gt;
	struct intel_gt *gt;
	int i;

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
	ddat->gt_n = -1;
	init_waitqueue_head(&ddat->waitq);

	for_each_gt(gt, i915, i) {
		ddat_gt = hwmon->ddat_gt + i;

		ddat_gt->hwmon = hwmon;
		ddat_gt->uncore = gt->uncore;
		snprintf(ddat_gt->name, sizeof(ddat_gt->name), "i915_gt%u", i);
		ddat_gt->gt_n = i;
	}

	hwm_get_preregistration_info(i915);

	/*  hwmon_dev points to device hwmon<i> */
	hwmon_dev = devm_hwmon_device_register_with_info(dev, ddat->name,
							 ddat,
							 &hwm_chip_info,
							 hwm_groups);
	if (IS_ERR(hwmon_dev)) {
		i915->hwmon = NULL;
		return;
	}

	ddat->hwmon_dev = hwmon_dev;

	for_each_gt(gt, i915, i) {
		ddat_gt = hwmon->ddat_gt + i;
		/*
		 * Create per-gt directories only if a per-gt attribute is
		 * visible. Currently this is only energy
		 */
		if (!hwm_gt_is_visible(ddat_gt, hwmon_energy, hwmon_energy_input, 0))
			continue;

		hwmon_dev = devm_hwmon_device_register_with_info(dev, ddat_gt->name,
								 ddat_gt,
								 &hwm_gt_chip_info,
								 NULL);
		if (!IS_ERR(hwmon_dev))
			ddat_gt->hwmon_dev = hwmon_dev;
	}
}

void i915_hwmon_unregister(struct drm_i915_private *i915)
{
	fetch_and_zero(&i915->hwmon);
}
