/*
 * Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author:
 *	Mikko Perttunen <mperttunen@nvidia.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/thermal.h>

#include <dt-bindings/thermal/tegra124-soctherm.h>

#include "../thermal_core.h"
#include "soctherm.h"

#define SENSOR_CONFIG0				0
#define SENSOR_CONFIG0_STOP			BIT(0)
#define SENSOR_CONFIG0_CPTR_OVER		BIT(2)
#define SENSOR_CONFIG0_OVER			BIT(3)
#define SENSOR_CONFIG0_TCALC_OVER		BIT(4)
#define SENSOR_CONFIG0_TALL_MASK		(0xfffff << 8)
#define SENSOR_CONFIG0_TALL_SHIFT		8

#define SENSOR_CONFIG1				4
#define SENSOR_CONFIG1_TSAMPLE_MASK		0x3ff
#define SENSOR_CONFIG1_TSAMPLE_SHIFT		0
#define SENSOR_CONFIG1_TIDDQ_EN_MASK		(0x3f << 15)
#define SENSOR_CONFIG1_TIDDQ_EN_SHIFT		15
#define SENSOR_CONFIG1_TEN_COUNT_MASK		(0x3f << 24)
#define SENSOR_CONFIG1_TEN_COUNT_SHIFT		24
#define SENSOR_CONFIG1_TEMP_ENABLE		BIT(31)

/*
 * SENSOR_CONFIG2 is defined in soctherm.h
 * because, it will be used by tegra_soctherm_fuse.c
 */

#define SENSOR_STATUS0				0xc
#define SENSOR_STATUS0_VALID_MASK		BIT(31)
#define SENSOR_STATUS0_CAPTURE_MASK		0xffff

#define SENSOR_STATUS1				0x10
#define SENSOR_STATUS1_TEMP_VALID_MASK		BIT(31)
#define SENSOR_STATUS1_TEMP_MASK		0xffff

#define READBACK_VALUE_MASK			0xff00
#define READBACK_VALUE_SHIFT			8
#define READBACK_ADD_HALF			BIT(7)
#define READBACK_NEGATE				BIT(0)

/*
 * THERMCTL_LEVEL0_GROUP_CPU is defined in soctherm.h
 * because it will be used by tegraxxx_soctherm.c
 */
#define THERMCTL_LVL0_CPU0_EN_MASK		BIT(8)
#define THERMCTL_LVL0_CPU0_CPU_THROT_MASK	(0x3 << 5)
#define THERMCTL_LVL0_CPU0_CPU_THROT_LIGHT	0x1
#define THERMCTL_LVL0_CPU0_CPU_THROT_HEAVY	0x2
#define THERMCTL_LVL0_CPU0_GPU_THROT_MASK	(0x3 << 3)
#define THERMCTL_LVL0_CPU0_GPU_THROT_LIGHT	0x1
#define THERMCTL_LVL0_CPU0_GPU_THROT_HEAVY	0x2
#define THERMCTL_LVL0_CPU0_MEM_THROT_MASK	BIT(2)
#define THERMCTL_LVL0_CPU0_STATUS_MASK		0x3

#define THERMCTL_LVL0_UP_STATS			0x10
#define THERMCTL_LVL0_DN_STATS			0x14

#define THERMCTL_STATS_CTL			0x94
#define STATS_CTL_CLR_DN			0x8
#define STATS_CTL_EN_DN				0x4
#define STATS_CTL_CLR_UP			0x2
#define STATS_CTL_EN_UP				0x1

#define THROT_GLOBAL_CFG			0x400
#define THROT_GLOBAL_ENB_MASK			BIT(0)

#define CPU_PSKIP_STATUS			0x418
#define XPU_PSKIP_STATUS_M_MASK			(0xff << 12)
#define XPU_PSKIP_STATUS_N_MASK			(0xff << 4)
#define XPU_PSKIP_STATUS_SW_OVERRIDE_MASK	BIT(1)
#define XPU_PSKIP_STATUS_ENABLED_MASK		BIT(0)

#define THROT_PRIORITY_LOCK			0x424
#define THROT_PRIORITY_LOCK_PRIORITY_MASK	0xff

#define THROT_STATUS				0x428
#define THROT_STATUS_BREACH_MASK		BIT(12)
#define THROT_STATUS_STATE_MASK			(0xff << 4)
#define THROT_STATUS_ENABLED_MASK		BIT(0)

#define THROT_PSKIP_CTRL_LITE_CPU		0x430
#define THROT_PSKIP_CTRL_ENABLE_MASK            BIT(31)
#define THROT_PSKIP_CTRL_DIVIDEND_MASK          (0xff << 8)
#define THROT_PSKIP_CTRL_DIVISOR_MASK           0xff
#define THROT_PSKIP_CTRL_VECT_GPU_MASK          (0x7 << 16)
#define THROT_PSKIP_CTRL_VECT_CPU_MASK          (0x7 << 8)
#define THROT_PSKIP_CTRL_VECT2_CPU_MASK         0x7

#define THROT_VECT_NONE				0x0 /* 3'b000 */
#define THROT_VECT_LOW				0x1 /* 3'b001 */
#define THROT_VECT_MED				0x3 /* 3'b011 */
#define THROT_VECT_HIGH				0x7 /* 3'b111 */

#define THROT_PSKIP_RAMP_LITE_CPU		0x434
#define THROT_PSKIP_RAMP_SEQ_BYPASS_MODE_MASK	BIT(31)
#define THROT_PSKIP_RAMP_DURATION_MASK		(0xffff << 8)
#define THROT_PSKIP_RAMP_STEP_MASK		0xff

#define THROT_PRIORITY_LITE			0x444
#define THROT_PRIORITY_LITE_PRIO_MASK		0xff

#define THROT_DELAY_LITE			0x448
#define THROT_DELAY_LITE_DELAY_MASK		0xff

/* car register offsets needed for enabling HW throttling */
#define CAR_SUPER_CCLKG_DIVIDER			0x36c
#define CDIVG_USE_THERM_CONTROLS_MASK		BIT(30)

/* ccroc register offsets needed for enabling HW throttling for Tegra132 */
#define CCROC_SUPER_CCLKG_DIVIDER		0x024

#define CCROC_GLOBAL_CFG			0x148

#define CCROC_THROT_PSKIP_RAMP_CPU		0x150
#define CCROC_THROT_PSKIP_RAMP_SEQ_BYPASS_MODE_MASK	BIT(31)
#define CCROC_THROT_PSKIP_RAMP_DURATION_MASK	(0xffff << 8)
#define CCROC_THROT_PSKIP_RAMP_STEP_MASK	0xff

#define CCROC_THROT_PSKIP_CTRL_CPU		0x154
#define CCROC_THROT_PSKIP_CTRL_ENB_MASK		BIT(31)
#define CCROC_THROT_PSKIP_CTRL_DIVIDEND_MASK	(0xff << 8)
#define CCROC_THROT_PSKIP_CTRL_DIVISOR_MASK	0xff

/* get val from register(r) mask bits(m) */
#define REG_GET_MASK(r, m)	(((r) & (m)) >> (ffs(m) - 1))
/* set val(v) to mask bits(m) of register(r) */
#define REG_SET_MASK(r, m, v)	(((r) & ~(m)) | \
				 (((v) & (m >> (ffs(m) - 1))) << (ffs(m) - 1)))

/* get dividend from the depth */
#define THROT_DEPTH_DIVIDEND(depth)	((256 * (100 - (depth)) / 100) - 1)

/* get THROT_PSKIP_xxx offset per LIGHT/HEAVY throt and CPU/GPU dev */
#define THROT_OFFSET			0x30
#define THROT_PSKIP_CTRL(throt, dev)	(THROT_PSKIP_CTRL_LITE_CPU + \
					(THROT_OFFSET * throt) + (8 * dev))
#define THROT_PSKIP_RAMP(throt, dev)	(THROT_PSKIP_RAMP_LITE_CPU + \
					(THROT_OFFSET * throt) + (8 * dev))

/* get THROT_xxx_CTRL offset per LIGHT/HEAVY throt */
#define THROT_PRIORITY_CTRL(throt)	(THROT_PRIORITY_LITE + \
					(THROT_OFFSET * throt))
#define THROT_DELAY_CTRL(throt)		(THROT_DELAY_LITE + \
					(THROT_OFFSET * throt))

/* get CCROC_THROT_PSKIP_xxx offset per HIGH/MED/LOW vect*/
#define CCROC_THROT_OFFSET			0x0c
#define CCROC_THROT_PSKIP_CTRL_CPU_REG(vect)    (CCROC_THROT_PSKIP_CTRL_CPU + \
						(CCROC_THROT_OFFSET * vect))
#define CCROC_THROT_PSKIP_RAMP_CPU_REG(vect)    (CCROC_THROT_PSKIP_RAMP_CPU + \
						(CCROC_THROT_OFFSET * vect))

/* get THERMCTL_LEVELx offset per CPU/GPU/MEM/TSENSE rg and LEVEL0~3 lv */
#define THERMCTL_LVL_REGS_SIZE		0x20
#define THERMCTL_LVL_REG(rg, lv)	((rg) + ((lv) * THERMCTL_LVL_REGS_SIZE))

static const int min_low_temp = -127000;
static const int max_high_temp = 127000;

enum soctherm_throttle_id {
	THROTTLE_LIGHT = 0,
	THROTTLE_HEAVY,
	THROTTLE_SIZE,
};

enum soctherm_throttle_dev_id {
	THROTTLE_DEV_CPU = 0,
	THROTTLE_DEV_GPU,
	THROTTLE_DEV_SIZE,
};

static const char *const throt_names[] = {
	[THROTTLE_LIGHT] = "light",
	[THROTTLE_HEAVY] = "heavy",
};

struct tegra_soctherm;
struct tegra_thermctl_zone {
	void __iomem *reg;
	struct device *dev;
	struct tegra_soctherm *ts;
	struct thermal_zone_device *tz;
	const struct tegra_tsensor_group *sg;
};

struct soctherm_throt_cfg {
	const char *name;
	unsigned int id;
	u8 priority;
	u8 cpu_throt_level;
	u32 cpu_throt_depth;
	struct thermal_cooling_device *cdev;
	bool init;
};

struct tegra_soctherm {
	struct reset_control *reset;
	struct clk *clock_tsensor;
	struct clk *clock_soctherm;
	void __iomem *regs;
	void __iomem *clk_regs;
	void __iomem *ccroc_regs;

	u32 *calib;
	struct thermal_zone_device **thermctl_tzs;
	struct tegra_soctherm_soc *soc;

	struct soctherm_throt_cfg throt_cfgs[THROTTLE_SIZE];

	struct dentry *debugfs_dir;
};

/**
 * ccroc_writel() - writes a value to a CCROC register
 * @ts: pointer to a struct tegra_soctherm
 * @v: the value to write
 * @reg: the register offset
 *
 * Writes @v to @reg.  No return value.
 */
static inline void ccroc_writel(struct tegra_soctherm *ts, u32 value, u32 reg)
{
	writel(value, (ts->ccroc_regs + reg));
}

/**
 * ccroc_readl() - reads specified register from CCROC IP block
 * @ts: pointer to a struct tegra_soctherm
 * @reg: register address to be read
 *
 * Return: the value of the register
 */
static inline u32 ccroc_readl(struct tegra_soctherm *ts, u32 reg)
{
	return readl(ts->ccroc_regs + reg);
}

static void enable_tsensor(struct tegra_soctherm *tegra, unsigned int i)
{
	const struct tegra_tsensor *sensor = &tegra->soc->tsensors[i];
	void __iomem *base = tegra->regs + sensor->base;
	unsigned int val;

	val = sensor->config->tall << SENSOR_CONFIG0_TALL_SHIFT;
	writel(val, base + SENSOR_CONFIG0);

	val  = (sensor->config->tsample - 1) << SENSOR_CONFIG1_TSAMPLE_SHIFT;
	val |= sensor->config->tiddq_en << SENSOR_CONFIG1_TIDDQ_EN_SHIFT;
	val |= sensor->config->ten_count << SENSOR_CONFIG1_TEN_COUNT_SHIFT;
	val |= SENSOR_CONFIG1_TEMP_ENABLE;
	writel(val, base + SENSOR_CONFIG1);

	writel(tegra->calib[i], base + SENSOR_CONFIG2);
}

/*
 * Translate from soctherm readback format to millicelsius.
 * The soctherm readback format in bits is as follows:
 *   TTTTTTTT H______N
 * where T's contain the temperature in Celsius,
 * H denotes an addition of 0.5 Celsius and N denotes negation
 * of the final value.
 */
static int translate_temp(u16 val)
{
	int t;

	t = ((val & READBACK_VALUE_MASK) >> READBACK_VALUE_SHIFT) * 1000;
	if (val & READBACK_ADD_HALF)
		t += 500;
	if (val & READBACK_NEGATE)
		t *= -1;

	return t;
}

static int tegra_thermctl_get_temp(void *data, int *out_temp)
{
	struct tegra_thermctl_zone *zone = data;
	u32 val;

	val = readl(zone->reg);
	val = REG_GET_MASK(val, zone->sg->sensor_temp_mask);
	*out_temp = translate_temp(val);

	return 0;
}

/**
 * enforce_temp_range() - check and enforce temperature range [min, max]
 * @trip_temp: the trip temperature to check
 *
 * Checks and enforces the permitted temperature range that SOC_THERM
 * HW can support This is
 * done while taking care of precision.
 *
 * Return: The precision adjusted capped temperature in millicelsius.
 */
static int enforce_temp_range(struct device *dev, int trip_temp)
{
	int temp;

	temp = clamp_val(trip_temp, min_low_temp, max_high_temp);
	if (temp != trip_temp)
		dev_info(dev, "soctherm: trip temperature %d forced to %d\n",
			 trip_temp, temp);
	return temp;
}

/**
 * thermtrip_program() - Configures the hardware to shut down the
 * system if a given sensor group reaches a given temperature
 * @dev: ptr to the struct device for the SOC_THERM IP block
 * @sg: pointer to the sensor group to set the thermtrip temperature for
 * @trip_temp: the temperature in millicelsius to trigger the thermal trip at
 *
 * Sets the thermal trip threshold of the given sensor group to be the
 * @trip_temp.  If this threshold is crossed, the hardware will shut
 * down.
 *
 * Note that, although @trip_temp is specified in millicelsius, the
 * hardware is programmed in degrees Celsius.
 *
 * Return: 0 upon success, or %-EINVAL upon failure.
 */
static int thermtrip_program(struct device *dev,
			     const struct tegra_tsensor_group *sg,
			     int trip_temp)
{
	struct tegra_soctherm *ts = dev_get_drvdata(dev);
	int temp;
	u32 r;

	if (!sg || !sg->thermtrip_threshold_mask)
		return -EINVAL;

	temp = enforce_temp_range(dev, trip_temp) / ts->soc->thresh_grain;

	r = readl(ts->regs + THERMCTL_THERMTRIP_CTL);
	r = REG_SET_MASK(r, sg->thermtrip_threshold_mask, temp);
	r = REG_SET_MASK(r, sg->thermtrip_enable_mask, 1);
	r = REG_SET_MASK(r, sg->thermtrip_any_en_mask, 0);
	writel(r, ts->regs + THERMCTL_THERMTRIP_CTL);

	return 0;
}

/**
 * throttrip_program() - Configures the hardware to throttle the
 * pulse if a given sensor group reaches a given temperature
 * @dev: ptr to the struct device for the SOC_THERM IP block
 * @sg: pointer to the sensor group to set the thermtrip temperature for
 * @stc: pointer to the throttle need to be triggered
 * @trip_temp: the temperature in millicelsius to trigger the thermal trip at
 *
 * Sets the thermal trip threshold and throttle event of the given sensor
 * group. If this threshold is crossed, the hardware will trigger the
 * throttle.
 *
 * Note that, although @trip_temp is specified in millicelsius, the
 * hardware is programmed in degrees Celsius.
 *
 * Return: 0 upon success, or %-EINVAL upon failure.
 */
static int throttrip_program(struct device *dev,
			     const struct tegra_tsensor_group *sg,
			     struct soctherm_throt_cfg *stc,
			     int trip_temp)
{
	struct tegra_soctherm *ts = dev_get_drvdata(dev);
	int temp, cpu_throt, gpu_throt;
	unsigned int throt;
	u32 r, reg_off;

	if (!sg || !stc || !stc->init)
		return -EINVAL;

	temp = enforce_temp_range(dev, trip_temp) / ts->soc->thresh_grain;

	/* Hardcode LIGHT on LEVEL1 and HEAVY on LEVEL2 */
	throt = stc->id;
	reg_off = THERMCTL_LVL_REG(sg->thermctl_lvl0_offset, throt + 1);

	if (throt == THROTTLE_LIGHT) {
		cpu_throt = THERMCTL_LVL0_CPU0_CPU_THROT_LIGHT;
		gpu_throt = THERMCTL_LVL0_CPU0_GPU_THROT_LIGHT;
	} else {
		cpu_throt = THERMCTL_LVL0_CPU0_CPU_THROT_HEAVY;
		gpu_throt = THERMCTL_LVL0_CPU0_GPU_THROT_HEAVY;
		if (throt != THROTTLE_HEAVY)
			dev_warn(dev,
				 "invalid throt id %d - assuming HEAVY",
				 throt);
	}

	r = readl(ts->regs + reg_off);
	r = REG_SET_MASK(r, sg->thermctl_lvl0_up_thresh_mask, temp);
	r = REG_SET_MASK(r, sg->thermctl_lvl0_dn_thresh_mask, temp);
	r = REG_SET_MASK(r, THERMCTL_LVL0_CPU0_CPU_THROT_MASK, cpu_throt);
	r = REG_SET_MASK(r, THERMCTL_LVL0_CPU0_GPU_THROT_MASK, gpu_throt);
	r = REG_SET_MASK(r, THERMCTL_LVL0_CPU0_EN_MASK, 1);
	writel(r, ts->regs + reg_off);

	return 0;
}

static struct soctherm_throt_cfg *
find_throttle_cfg_by_name(struct tegra_soctherm *ts, const char *name)
{
	unsigned int i;

	for (i = 0; ts->throt_cfgs[i].name; i++)
		if (!strcmp(ts->throt_cfgs[i].name, name))
			return &ts->throt_cfgs[i];

	return NULL;
}

static int tegra_thermctl_set_trip_temp(void *data, int trip, int temp)
{
	struct tegra_thermctl_zone *zone = data;
	struct thermal_zone_device *tz = zone->tz;
	struct tegra_soctherm *ts = zone->ts;
	const struct tegra_tsensor_group *sg = zone->sg;
	struct device *dev = zone->dev;
	enum thermal_trip_type type;
	int ret;

	if (!tz)
		return -EINVAL;

	ret = tz->ops->get_trip_type(tz, trip, &type);
	if (ret)
		return ret;

	if (type == THERMAL_TRIP_CRITICAL) {
		return thermtrip_program(dev, sg, temp);
	} else if (type == THERMAL_TRIP_HOT) {
		int i;

		for (i = 0; i < THROTTLE_SIZE; i++) {
			struct thermal_cooling_device *cdev;
			struct soctherm_throt_cfg *stc;

			if (!ts->throt_cfgs[i].init)
				continue;

			cdev = ts->throt_cfgs[i].cdev;
			if (get_thermal_instance(tz, cdev, trip))
				stc = find_throttle_cfg_by_name(ts, cdev->type);
			else
				continue;

			return throttrip_program(dev, sg, stc, temp);
		}
	}

	return 0;
}

static const struct thermal_zone_of_device_ops tegra_of_thermal_ops = {
	.get_temp = tegra_thermctl_get_temp,
	.set_trip_temp = tegra_thermctl_set_trip_temp,
};

static int get_hot_temp(struct thermal_zone_device *tz, int *trip, int *temp)
{
	int ntrips, i, ret;
	enum thermal_trip_type type;

	ntrips = of_thermal_get_ntrips(tz);
	if (ntrips <= 0)
		return -EINVAL;

	for (i = 0; i < ntrips; i++) {
		ret = tz->ops->get_trip_type(tz, i, &type);
		if (ret)
			return -EINVAL;
		if (type == THERMAL_TRIP_HOT) {
			ret = tz->ops->get_trip_temp(tz, i, temp);
			if (!ret)
				*trip = i;

			return ret;
		}
	}

	return -EINVAL;
}

/**
 * tegra_soctherm_set_hwtrips() - set HW trip point from DT data
 * @dev: struct device * of the SOC_THERM instance
 *
 * Configure the SOC_THERM HW trip points, setting "THERMTRIP"
 * "THROTTLE" trip points , using "critical" or "hot" type trip_temp
 * from thermal zone.
 * After they have been configured, THERMTRIP or THROTTLE will take
 * action when the configured SoC thermal sensor group reaches a
 * certain temperature.
 *
 * Return: 0 upon success, or a negative error code on failure.
 * "Success" does not mean that trips was enabled; it could also
 * mean that no node was found in DT.
 * THERMTRIP has been enabled successfully when a message similar to
 * this one appears on the serial console:
 * "thermtrip: will shut down when sensor group XXX reaches YYYYYY mC"
 * THROTTLE has been enabled successfully when a message similar to
 * this one appears on the serial console:
 * ""throttrip: will throttle when sensor group XXX reaches YYYYYY mC"
 */
static int tegra_soctherm_set_hwtrips(struct device *dev,
				      const struct tegra_tsensor_group *sg,
				      struct thermal_zone_device *tz)
{
	struct tegra_soctherm *ts = dev_get_drvdata(dev);
	struct soctherm_throt_cfg *stc;
	int i, trip, temperature;
	int ret;

	ret = tz->ops->get_crit_temp(tz, &temperature);
	if (ret) {
		dev_warn(dev, "thermtrip: %s: missing critical temperature\n",
			 sg->name);
		goto set_throttle;
	}

	ret = thermtrip_program(dev, sg, temperature);
	if (ret) {
		dev_err(dev, "thermtrip: %s: error during enable\n",
			sg->name);
		return ret;
	}

	dev_info(dev,
		 "thermtrip: will shut down when %s reaches %d mC\n",
		 sg->name, temperature);

set_throttle:
	ret = get_hot_temp(tz, &trip, &temperature);
	if (ret) {
		dev_warn(dev, "throttrip: %s: missing hot temperature\n",
			 sg->name);
		return 0;
	}

	for (i = 0; i < THROTTLE_SIZE; i++) {
		struct thermal_cooling_device *cdev;

		if (!ts->throt_cfgs[i].init)
			continue;

		cdev = ts->throt_cfgs[i].cdev;
		if (get_thermal_instance(tz, cdev, trip))
			stc = find_throttle_cfg_by_name(ts, cdev->type);
		else
			continue;

		ret = throttrip_program(dev, sg, stc, temperature);
		if (ret) {
			dev_err(dev, "throttrip: %s: error during enable\n",
				sg->name);
			return ret;
		}

		dev_info(dev,
			 "throttrip: will throttle when %s reaches %d mC\n",
			 sg->name, temperature);
		break;
	}

	if (i == THROTTLE_SIZE)
		dev_warn(dev, "throttrip: %s: missing throttle cdev\n",
			 sg->name);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int regs_show(struct seq_file *s, void *data)
{
	struct platform_device *pdev = s->private;
	struct tegra_soctherm *ts = platform_get_drvdata(pdev);
	const struct tegra_tsensor *tsensors = ts->soc->tsensors;
	const struct tegra_tsensor_group **ttgs = ts->soc->ttgs;
	u32 r, state;
	int i, level;

	seq_puts(s, "-----TSENSE (convert HW)-----\n");

	for (i = 0; i < ts->soc->num_tsensors; i++) {
		r = readl(ts->regs + tsensors[i].base + SENSOR_CONFIG1);
		state = REG_GET_MASK(r, SENSOR_CONFIG1_TEMP_ENABLE);

		seq_printf(s, "%s: ", tsensors[i].name);
		seq_printf(s, "En(%d) ", state);

		if (!state) {
			seq_puts(s, "\n");
			continue;
		}

		state = REG_GET_MASK(r, SENSOR_CONFIG1_TIDDQ_EN_MASK);
		seq_printf(s, "tiddq(%d) ", state);
		state = REG_GET_MASK(r, SENSOR_CONFIG1_TEN_COUNT_MASK);
		seq_printf(s, "ten_count(%d) ", state);
		state = REG_GET_MASK(r, SENSOR_CONFIG1_TSAMPLE_MASK);
		seq_printf(s, "tsample(%d) ", state + 1);

		r = readl(ts->regs + tsensors[i].base + SENSOR_STATUS1);
		state = REG_GET_MASK(r, SENSOR_STATUS1_TEMP_VALID_MASK);
		seq_printf(s, "Temp(%d/", state);
		state = REG_GET_MASK(r, SENSOR_STATUS1_TEMP_MASK);
		seq_printf(s, "%d) ", translate_temp(state));

		r = readl(ts->regs + tsensors[i].base + SENSOR_STATUS0);
		state = REG_GET_MASK(r, SENSOR_STATUS0_VALID_MASK);
		seq_printf(s, "Capture(%d/", state);
		state = REG_GET_MASK(r, SENSOR_STATUS0_CAPTURE_MASK);
		seq_printf(s, "%d) ", state);

		r = readl(ts->regs + tsensors[i].base + SENSOR_CONFIG0);
		state = REG_GET_MASK(r, SENSOR_CONFIG0_STOP);
		seq_printf(s, "Stop(%d) ", state);
		state = REG_GET_MASK(r, SENSOR_CONFIG0_TALL_MASK);
		seq_printf(s, "Tall(%d) ", state);
		state = REG_GET_MASK(r, SENSOR_CONFIG0_TCALC_OVER);
		seq_printf(s, "Over(%d/", state);
		state = REG_GET_MASK(r, SENSOR_CONFIG0_OVER);
		seq_printf(s, "%d/", state);
		state = REG_GET_MASK(r, SENSOR_CONFIG0_CPTR_OVER);
		seq_printf(s, "%d) ", state);

		r = readl(ts->regs + tsensors[i].base + SENSOR_CONFIG2);
		state = REG_GET_MASK(r, SENSOR_CONFIG2_THERMA_MASK);
		seq_printf(s, "Therm_A/B(%d/", state);
		state = REG_GET_MASK(r, SENSOR_CONFIG2_THERMB_MASK);
		seq_printf(s, "%d)\n", (s16)state);
	}

	r = readl(ts->regs + SENSOR_PDIV);
	seq_printf(s, "PDIV: 0x%x\n", r);

	r = readl(ts->regs + SENSOR_HOTSPOT_OFF);
	seq_printf(s, "HOTSPOT: 0x%x\n", r);

	seq_puts(s, "\n");
	seq_puts(s, "-----SOC_THERM-----\n");

	r = readl(ts->regs + SENSOR_TEMP1);
	state = REG_GET_MASK(r, SENSOR_TEMP1_CPU_TEMP_MASK);
	seq_printf(s, "Temperatures: CPU(%d) ", translate_temp(state));
	state = REG_GET_MASK(r, SENSOR_TEMP1_GPU_TEMP_MASK);
	seq_printf(s, " GPU(%d) ", translate_temp(state));
	r = readl(ts->regs + SENSOR_TEMP2);
	state = REG_GET_MASK(r, SENSOR_TEMP2_PLLX_TEMP_MASK);
	seq_printf(s, " PLLX(%d) ", translate_temp(state));
	state = REG_GET_MASK(r, SENSOR_TEMP2_MEM_TEMP_MASK);
	seq_printf(s, " MEM(%d)\n", translate_temp(state));

	for (i = 0; i < ts->soc->num_ttgs; i++) {
		seq_printf(s, "%s:\n", ttgs[i]->name);
		for (level = 0; level < 4; level++) {
			s32 v;
			u32 mask;
			u16 off = ttgs[i]->thermctl_lvl0_offset;

			r = readl(ts->regs + THERMCTL_LVL_REG(off, level));

			mask = ttgs[i]->thermctl_lvl0_up_thresh_mask;
			state = REG_GET_MASK(r, mask);
			v = sign_extend32(state, ts->soc->bptt - 1);
			v *= ts->soc->thresh_grain;
			seq_printf(s, "   %d: Up/Dn(%d /", level, v);

			mask = ttgs[i]->thermctl_lvl0_dn_thresh_mask;
			state = REG_GET_MASK(r, mask);
			v = sign_extend32(state, ts->soc->bptt - 1);
			v *= ts->soc->thresh_grain;
			seq_printf(s, "%d ) ", v);

			mask = THERMCTL_LVL0_CPU0_EN_MASK;
			state = REG_GET_MASK(r, mask);
			seq_printf(s, "En(%d) ", state);

			mask = THERMCTL_LVL0_CPU0_CPU_THROT_MASK;
			state = REG_GET_MASK(r, mask);
			seq_puts(s, "CPU Throt");
			if (!state)
				seq_printf(s, "(%s) ", "none");
			else if (state == THERMCTL_LVL0_CPU0_CPU_THROT_LIGHT)
				seq_printf(s, "(%s) ", "L");
			else if (state == THERMCTL_LVL0_CPU0_CPU_THROT_HEAVY)
				seq_printf(s, "(%s) ", "H");
			else
				seq_printf(s, "(%s) ", "H+L");

			mask = THERMCTL_LVL0_CPU0_GPU_THROT_MASK;
			state = REG_GET_MASK(r, mask);
			seq_puts(s, "GPU Throt");
			if (!state)
				seq_printf(s, "(%s) ", "none");
			else if (state == THERMCTL_LVL0_CPU0_GPU_THROT_LIGHT)
				seq_printf(s, "(%s) ", "L");
			else if (state == THERMCTL_LVL0_CPU0_GPU_THROT_HEAVY)
				seq_printf(s, "(%s) ", "H");
			else
				seq_printf(s, "(%s) ", "H+L");

			mask = THERMCTL_LVL0_CPU0_STATUS_MASK;
			state = REG_GET_MASK(r, mask);
			seq_printf(s, "Status(%s)\n",
				   state == 0 ? "LO" :
				   state == 1 ? "In" :
				   state == 2 ? "Res" : "HI");
		}
	}

	r = readl(ts->regs + THERMCTL_STATS_CTL);
	seq_printf(s, "STATS: Up(%s) Dn(%s)\n",
		   r & STATS_CTL_EN_UP ? "En" : "--",
		   r & STATS_CTL_EN_DN ? "En" : "--");

	for (level = 0; level < 4; level++) {
		u16 off;

		off = THERMCTL_LVL0_UP_STATS;
		r = readl(ts->regs + THERMCTL_LVL_REG(off, level));
		seq_printf(s, "  Level_%d Up(%d) ", level, r);

		off = THERMCTL_LVL0_DN_STATS;
		r = readl(ts->regs + THERMCTL_LVL_REG(off, level));
		seq_printf(s, "Dn(%d)\n", r);
	}

	r = readl(ts->regs + THERMCTL_THERMTRIP_CTL);
	state = REG_GET_MASK(r, ttgs[0]->thermtrip_any_en_mask);
	seq_printf(s, "Thermtrip Any En(%d)\n", state);
	for (i = 0; i < ts->soc->num_ttgs; i++) {
		state = REG_GET_MASK(r, ttgs[i]->thermtrip_enable_mask);
		seq_printf(s, "     %s En(%d) ", ttgs[i]->name, state);
		state = REG_GET_MASK(r, ttgs[i]->thermtrip_threshold_mask);
		state *= ts->soc->thresh_grain;
		seq_printf(s, "Thresh(%d)\n", state);
	}

	r = readl(ts->regs + THROT_GLOBAL_CFG);
	seq_puts(s, "\n");
	seq_printf(s, "GLOBAL THROTTLE CONFIG: 0x%08x\n", r);

	seq_puts(s, "---------------------------------------------------\n");
	r = readl(ts->regs + THROT_STATUS);
	state = REG_GET_MASK(r, THROT_STATUS_BREACH_MASK);
	seq_printf(s, "THROT STATUS: breach(%d) ", state);
	state = REG_GET_MASK(r, THROT_STATUS_STATE_MASK);
	seq_printf(s, "state(%d) ", state);
	state = REG_GET_MASK(r, THROT_STATUS_ENABLED_MASK);
	seq_printf(s, "enabled(%d)\n", state);

	r = readl(ts->regs + CPU_PSKIP_STATUS);
	if (ts->soc->use_ccroc) {
		state = REG_GET_MASK(r, XPU_PSKIP_STATUS_ENABLED_MASK);
		seq_printf(s, "CPU PSKIP STATUS: enabled(%d)\n", state);
	} else {
		state = REG_GET_MASK(r, XPU_PSKIP_STATUS_M_MASK);
		seq_printf(s, "CPU PSKIP STATUS: M(%d) ", state);
		state = REG_GET_MASK(r, XPU_PSKIP_STATUS_N_MASK);
		seq_printf(s, "N(%d) ", state);
		state = REG_GET_MASK(r, XPU_PSKIP_STATUS_ENABLED_MASK);
		seq_printf(s, "enabled(%d)\n", state);
	}

	return 0;
}

static int regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, regs_show, inode->i_private);
}

static const struct file_operations regs_fops = {
	.open		= regs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void soctherm_debug_init(struct platform_device *pdev)
{
	struct tegra_soctherm *tegra = platform_get_drvdata(pdev);
	struct dentry *root, *file;

	root = debugfs_create_dir("soctherm", NULL);
	if (!root) {
		dev_err(&pdev->dev, "failed to create debugfs directory\n");
		return;
	}

	tegra->debugfs_dir = root;

	file = debugfs_create_file("reg_contents", 0644, root,
				   pdev, &regs_fops);
	if (!file) {
		dev_err(&pdev->dev, "failed to create debugfs file\n");
		debugfs_remove_recursive(tegra->debugfs_dir);
		tegra->debugfs_dir = NULL;
	}
}
#else
static inline void soctherm_debug_init(struct platform_device *pdev) {}
#endif

static int soctherm_clk_enable(struct platform_device *pdev, bool enable)
{
	struct tegra_soctherm *tegra = platform_get_drvdata(pdev);
	int err;

	if (!tegra->clock_soctherm || !tegra->clock_tsensor)
		return -EINVAL;

	reset_control_assert(tegra->reset);

	if (enable) {
		err = clk_prepare_enable(tegra->clock_soctherm);
		if (err) {
			reset_control_deassert(tegra->reset);
			return err;
		}

		err = clk_prepare_enable(tegra->clock_tsensor);
		if (err) {
			clk_disable_unprepare(tegra->clock_soctherm);
			reset_control_deassert(tegra->reset);
			return err;
		}
	} else {
		clk_disable_unprepare(tegra->clock_tsensor);
		clk_disable_unprepare(tegra->clock_soctherm);
	}

	reset_control_deassert(tegra->reset);

	return 0;
}

static int throt_get_cdev_max_state(struct thermal_cooling_device *cdev,
				    unsigned long *max_state)
{
	*max_state = 1;
	return 0;
}

static int throt_get_cdev_cur_state(struct thermal_cooling_device *cdev,
				    unsigned long *cur_state)
{
	struct tegra_soctherm *ts = cdev->devdata;
	u32 r;

	r = readl(ts->regs + THROT_STATUS);
	if (REG_GET_MASK(r, THROT_STATUS_STATE_MASK))
		*cur_state = 1;
	else
		*cur_state = 0;

	return 0;
}

static int throt_set_cdev_state(struct thermal_cooling_device *cdev,
				unsigned long cur_state)
{
	return 0;
}

static const struct thermal_cooling_device_ops throt_cooling_ops = {
	.get_max_state = throt_get_cdev_max_state,
	.get_cur_state = throt_get_cdev_cur_state,
	.set_cur_state = throt_set_cdev_state,
};

/**
 * soctherm_init_hw_throt_cdev() - Parse the HW throttle configurations
 * and register them as cooling devices.
 */
static void soctherm_init_hw_throt_cdev(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra_soctherm *ts = dev_get_drvdata(dev);
	struct device_node *np_stc, *np_stcc;
	const char *name;
	u32 val;
	int i, r;

	for (i = 0; i < THROTTLE_SIZE; i++) {
		ts->throt_cfgs[i].name = throt_names[i];
		ts->throt_cfgs[i].id = i;
		ts->throt_cfgs[i].init = false;
	}

	np_stc = of_get_child_by_name(dev->of_node, "throttle-cfgs");
	if (!np_stc) {
		dev_info(dev,
			 "throttle-cfg: no throttle-cfgs - not enabling\n");
		return;
	}

	for_each_child_of_node(np_stc, np_stcc) {
		struct soctherm_throt_cfg *stc;
		struct thermal_cooling_device *tcd;

		name = np_stcc->name;
		stc = find_throttle_cfg_by_name(ts, name);
		if (!stc) {
			dev_err(dev,
				"throttle-cfg: could not find %s\n", name);
			continue;
		}

		r = of_property_read_u32(np_stcc, "nvidia,priority", &val);
		if (r) {
			dev_info(dev,
				 "throttle-cfg: %s: missing priority\n", name);
			continue;
		}
		stc->priority = val;

		if (ts->soc->use_ccroc) {
			r = of_property_read_u32(np_stcc,
						 "nvidia,cpu-throt-level",
						 &val);
			if (r) {
				dev_info(dev,
					 "throttle-cfg: %s: missing cpu-throt-level\n",
					 name);
				continue;
			}
			stc->cpu_throt_level = val;
		} else {
			r = of_property_read_u32(np_stcc,
						 "nvidia,cpu-throt-percent",
						 &val);
			if (r) {
				dev_info(dev,
					 "throttle-cfg: %s: missing cpu-throt-percent\n",
					 name);
				continue;
			}
			stc->cpu_throt_depth = val;
		}

		tcd = thermal_of_cooling_device_register(np_stcc,
							 (char *)name, ts,
							 &throt_cooling_ops);
		of_node_put(np_stcc);
		if (IS_ERR_OR_NULL(tcd)) {
			dev_err(dev,
				"throttle-cfg: %s: failed to register cooling device\n",
				name);
			continue;
		}

		stc->cdev = tcd;
		stc->init = true;
	}

	of_node_put(np_stc);
}

/**
 * throttlectl_cpu_level_cfg() - programs CCROC NV_THERM level config
 * @level: describing the level LOW/MED/HIGH of throttling
 *
 * It's necessary to set up the CPU-local CCROC NV_THERM instance with
 * the M/N values desired for each level. This function does this.
 *
 * This function pre-programs the CCROC NV_THERM levels in terms of
 * pre-configured "Low", "Medium" or "Heavy" throttle levels which are
 * mapped to THROT_LEVEL_LOW, THROT_LEVEL_MED and THROT_LEVEL_HVY.
 */
static void throttlectl_cpu_level_cfg(struct tegra_soctherm *ts, int level)
{
	u8 depth, dividend;
	u32 r;

	switch (level) {
	case TEGRA_SOCTHERM_THROT_LEVEL_LOW:
		depth = 50;
		break;
	case TEGRA_SOCTHERM_THROT_LEVEL_MED:
		depth = 75;
		break;
	case TEGRA_SOCTHERM_THROT_LEVEL_HIGH:
		depth = 80;
		break;
	case TEGRA_SOCTHERM_THROT_LEVEL_NONE:
		return;
	default:
		return;
	}

	dividend = THROT_DEPTH_DIVIDEND(depth);

	/* setup PSKIP in ccroc nv_therm registers */
	r = ccroc_readl(ts, CCROC_THROT_PSKIP_RAMP_CPU_REG(level));
	r = REG_SET_MASK(r, CCROC_THROT_PSKIP_RAMP_DURATION_MASK, 0xff);
	r = REG_SET_MASK(r, CCROC_THROT_PSKIP_RAMP_STEP_MASK, 0xf);
	ccroc_writel(ts, r, CCROC_THROT_PSKIP_RAMP_CPU_REG(level));

	r = ccroc_readl(ts, CCROC_THROT_PSKIP_CTRL_CPU_REG(level));
	r = REG_SET_MASK(r, CCROC_THROT_PSKIP_CTRL_ENB_MASK, 1);
	r = REG_SET_MASK(r, CCROC_THROT_PSKIP_CTRL_DIVIDEND_MASK, dividend);
	r = REG_SET_MASK(r, CCROC_THROT_PSKIP_CTRL_DIVISOR_MASK, 0xff);
	ccroc_writel(ts, r, CCROC_THROT_PSKIP_CTRL_CPU_REG(level));
}

/**
 * throttlectl_cpu_level_select() - program CPU pulse skipper config
 * @throt: the LIGHT/HEAVY of throttle event id
 *
 * Pulse skippers are used to throttle clock frequencies.  This
 * function programs the pulse skippers based on @throt and platform
 * data.  This function is used on SoCs which have CPU-local pulse
 * skipper control, such as T13x. It programs soctherm's interface to
 * Denver:CCROC NV_THERM in terms of Low, Medium and HIGH throttling
 * vectors. PSKIP_BYPASS mode is set as required per HW spec.
 */
static void throttlectl_cpu_level_select(struct tegra_soctherm *ts,
					 enum soctherm_throttle_id throt)
{
	u32 r, throt_vect;

	/* Denver:CCROC NV_THERM interface N:3 Mapping */
	switch (ts->throt_cfgs[throt].cpu_throt_level) {
	case TEGRA_SOCTHERM_THROT_LEVEL_LOW:
		throt_vect = THROT_VECT_LOW;
		break;
	case TEGRA_SOCTHERM_THROT_LEVEL_MED:
		throt_vect = THROT_VECT_MED;
		break;
	case TEGRA_SOCTHERM_THROT_LEVEL_HIGH:
		throt_vect = THROT_VECT_HIGH;
		break;
	default:
		throt_vect = THROT_VECT_NONE;
		break;
	}

	r = readl(ts->regs + THROT_PSKIP_CTRL(throt, THROTTLE_DEV_CPU));
	r = REG_SET_MASK(r, THROT_PSKIP_CTRL_ENABLE_MASK, 1);
	r = REG_SET_MASK(r, THROT_PSKIP_CTRL_VECT_CPU_MASK, throt_vect);
	r = REG_SET_MASK(r, THROT_PSKIP_CTRL_VECT2_CPU_MASK, throt_vect);
	writel(r, ts->regs + THROT_PSKIP_CTRL(throt, THROTTLE_DEV_CPU));

	/* bypass sequencer in soc_therm as it is programmed in ccroc */
	r = REG_SET_MASK(0, THROT_PSKIP_RAMP_SEQ_BYPASS_MODE_MASK, 1);
	writel(r, ts->regs + THROT_PSKIP_RAMP(throt, THROTTLE_DEV_CPU));
}

/**
 * throttlectl_cpu_mn() - program CPU pulse skipper configuration
 * @throt: the LIGHT/HEAVY of throttle event id
 *
 * Pulse skippers are used to throttle clock frequencies.  This
 * function programs the pulse skippers based on @throt and platform
 * data.  This function is used for CPUs that have "remote" pulse
 * skipper control, e.g., the CPU pulse skipper is controlled by the
 * SOC_THERM IP block.  (SOC_THERM is located outside the CPU
 * complex.)
 */
static void throttlectl_cpu_mn(struct tegra_soctherm *ts,
			       enum soctherm_throttle_id throt)
{
	u32 r;
	int depth;
	u8 dividend;

	depth = ts->throt_cfgs[throt].cpu_throt_depth;
	dividend = THROT_DEPTH_DIVIDEND(depth);

	r = readl(ts->regs + THROT_PSKIP_CTRL(throt, THROTTLE_DEV_CPU));
	r = REG_SET_MASK(r, THROT_PSKIP_CTRL_ENABLE_MASK, 1);
	r = REG_SET_MASK(r, THROT_PSKIP_CTRL_DIVIDEND_MASK, dividend);
	r = REG_SET_MASK(r, THROT_PSKIP_CTRL_DIVISOR_MASK, 0xff);
	writel(r, ts->regs + THROT_PSKIP_CTRL(throt, THROTTLE_DEV_CPU));

	r = readl(ts->regs + THROT_PSKIP_RAMP(throt, THROTTLE_DEV_CPU));
	r = REG_SET_MASK(r, THROT_PSKIP_RAMP_DURATION_MASK, 0xff);
	r = REG_SET_MASK(r, THROT_PSKIP_RAMP_STEP_MASK, 0xf);
	writel(r, ts->regs + THROT_PSKIP_RAMP(throt, THROTTLE_DEV_CPU));
}

/**
 * soctherm_throttle_program() - programs pulse skippers' configuration
 * @throt: the LIGHT/HEAVY of the throttle event id.
 *
 * Pulse skippers are used to throttle clock frequencies.
 * This function programs the pulse skippers.
 */
static void soctherm_throttle_program(struct tegra_soctherm *ts,
				      enum soctherm_throttle_id throt)
{
	u32 r;
	struct soctherm_throt_cfg stc = ts->throt_cfgs[throt];

	if (!stc.init)
		return;

	/* Setup PSKIP parameters */
	if (ts->soc->use_ccroc)
		throttlectl_cpu_level_select(ts, throt);
	else
		throttlectl_cpu_mn(ts, throt);

	r = REG_SET_MASK(0, THROT_PRIORITY_LITE_PRIO_MASK, stc.priority);
	writel(r, ts->regs + THROT_PRIORITY_CTRL(throt));

	r = REG_SET_MASK(0, THROT_DELAY_LITE_DELAY_MASK, 0);
	writel(r, ts->regs + THROT_DELAY_CTRL(throt));

	r = readl(ts->regs + THROT_PRIORITY_LOCK);
	r = REG_GET_MASK(r, THROT_PRIORITY_LOCK_PRIORITY_MASK);
	if (r >= stc.priority)
		return;
	r = REG_SET_MASK(0, THROT_PRIORITY_LOCK_PRIORITY_MASK,
			 stc.priority);
	writel(r, ts->regs + THROT_PRIORITY_LOCK);
}

static void tegra_soctherm_throttle(struct device *dev)
{
	struct tegra_soctherm *ts = dev_get_drvdata(dev);
	u32 v;
	int i;

	/* configure LOW, MED and HIGH levels for CCROC NV_THERM */
	if (ts->soc->use_ccroc) {
		throttlectl_cpu_level_cfg(ts, TEGRA_SOCTHERM_THROT_LEVEL_LOW);
		throttlectl_cpu_level_cfg(ts, TEGRA_SOCTHERM_THROT_LEVEL_MED);
		throttlectl_cpu_level_cfg(ts, TEGRA_SOCTHERM_THROT_LEVEL_HIGH);
	}

	/* Thermal HW throttle programming */
	for (i = 0; i < THROTTLE_SIZE; i++)
		soctherm_throttle_program(ts, i);

	v = REG_SET_MASK(0, THROT_GLOBAL_ENB_MASK, 1);
	if (ts->soc->use_ccroc) {
		ccroc_writel(ts, v, CCROC_GLOBAL_CFG);

		v = ccroc_readl(ts, CCROC_SUPER_CCLKG_DIVIDER);
		v = REG_SET_MASK(v, CDIVG_USE_THERM_CONTROLS_MASK, 1);
		ccroc_writel(ts, v, CCROC_SUPER_CCLKG_DIVIDER);
	} else {
		writel(v, ts->regs + THROT_GLOBAL_CFG);

		v = readl(ts->clk_regs + CAR_SUPER_CCLKG_DIVIDER);
		v = REG_SET_MASK(v, CDIVG_USE_THERM_CONTROLS_MASK, 1);
		writel(v, ts->clk_regs + CAR_SUPER_CCLKG_DIVIDER);
	}

	/* initialize stats collection */
	v = STATS_CTL_CLR_DN | STATS_CTL_EN_DN |
	    STATS_CTL_CLR_UP | STATS_CTL_EN_UP;
	writel(v, ts->regs + THERMCTL_STATS_CTL);
}

static void soctherm_init(struct platform_device *pdev)
{
	struct tegra_soctherm *tegra = platform_get_drvdata(pdev);
	const struct tegra_tsensor_group **ttgs = tegra->soc->ttgs;
	int i;
	u32 pdiv, hotspot;

	/* Initialize raw sensors */
	for (i = 0; i < tegra->soc->num_tsensors; ++i)
		enable_tsensor(tegra, i);

	/* program pdiv and hotspot offsets per THERM */
	pdiv = readl(tegra->regs + SENSOR_PDIV);
	hotspot = readl(tegra->regs + SENSOR_HOTSPOT_OFF);
	for (i = 0; i < tegra->soc->num_ttgs; ++i) {
		pdiv = REG_SET_MASK(pdiv, ttgs[i]->pdiv_mask,
				    ttgs[i]->pdiv);
		/* hotspot offset from PLLX, doesn't need to configure PLLX */
		if (ttgs[i]->id == TEGRA124_SOCTHERM_SENSOR_PLLX)
			continue;
		hotspot =  REG_SET_MASK(hotspot,
					ttgs[i]->pllx_hotspot_mask,
					ttgs[i]->pllx_hotspot_diff);
	}
	writel(pdiv, tegra->regs + SENSOR_PDIV);
	writel(hotspot, tegra->regs + SENSOR_HOTSPOT_OFF);

	/* Configure hw throttle */
	tegra_soctherm_throttle(&pdev->dev);
}

static const struct of_device_id tegra_soctherm_of_match[] = {
#ifdef CONFIG_ARCH_TEGRA_124_SOC
	{
		.compatible = "nvidia,tegra124-soctherm",
		.data = &tegra124_soctherm,
	},
#endif
#ifdef CONFIG_ARCH_TEGRA_132_SOC
	{
		.compatible = "nvidia,tegra132-soctherm",
		.data = &tegra132_soctherm,
	},
#endif
#ifdef CONFIG_ARCH_TEGRA_210_SOC
	{
		.compatible = "nvidia,tegra210-soctherm",
		.data = &tegra210_soctherm,
	},
#endif
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_soctherm_of_match);

static int tegra_soctherm_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct tegra_soctherm *tegra;
	struct thermal_zone_device *z;
	struct tsensor_shared_calib shared_calib;
	struct resource *res;
	struct tegra_soctherm_soc *soc;
	unsigned int i;
	int err;

	match = of_match_node(tegra_soctherm_of_match, pdev->dev.of_node);
	if (!match)
		return -ENODEV;

	soc = (struct tegra_soctherm_soc *)match->data;
	if (soc->num_ttgs > TEGRA124_SOCTHERM_SENSOR_NUM)
		return -EINVAL;

	tegra = devm_kzalloc(&pdev->dev, sizeof(*tegra), GFP_KERNEL);
	if (!tegra)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, tegra);

	tegra->soc = soc;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "soctherm-reg");
	tegra->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(tegra->regs)) {
		dev_err(&pdev->dev, "can't get soctherm registers");
		return PTR_ERR(tegra->regs);
	}

	if (!tegra->soc->use_ccroc) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   "car-reg");
		tegra->clk_regs = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(tegra->clk_regs)) {
			dev_err(&pdev->dev, "can't get car clk registers");
			return PTR_ERR(tegra->clk_regs);
		}
	} else {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   "ccroc-reg");
		tegra->ccroc_regs = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(tegra->ccroc_regs)) {
			dev_err(&pdev->dev, "can't get ccroc registers");
			return PTR_ERR(tegra->ccroc_regs);
		}
	}

	tegra->reset = devm_reset_control_get(&pdev->dev, "soctherm");
	if (IS_ERR(tegra->reset)) {
		dev_err(&pdev->dev, "can't get soctherm reset\n");
		return PTR_ERR(tegra->reset);
	}

	tegra->clock_tsensor = devm_clk_get(&pdev->dev, "tsensor");
	if (IS_ERR(tegra->clock_tsensor)) {
		dev_err(&pdev->dev, "can't get tsensor clock\n");
		return PTR_ERR(tegra->clock_tsensor);
	}

	tegra->clock_soctherm = devm_clk_get(&pdev->dev, "soctherm");
	if (IS_ERR(tegra->clock_soctherm)) {
		dev_err(&pdev->dev, "can't get soctherm clock\n");
		return PTR_ERR(tegra->clock_soctherm);
	}

	tegra->calib = devm_kcalloc(&pdev->dev,
				    soc->num_tsensors, sizeof(u32),
				    GFP_KERNEL);
	if (!tegra->calib)
		return -ENOMEM;

	/* calculate shared calibration data */
	err = tegra_calc_shared_calib(soc->tfuse, &shared_calib);
	if (err)
		return err;

	/* calculate tsensor calibaration data */
	for (i = 0; i < soc->num_tsensors; ++i) {
		err = tegra_calc_tsensor_calib(&soc->tsensors[i],
					       &shared_calib,
					       &tegra->calib[i]);
		if (err)
			return err;
	}

	tegra->thermctl_tzs = devm_kcalloc(&pdev->dev,
					   soc->num_ttgs, sizeof(*z),
					   GFP_KERNEL);
	if (!tegra->thermctl_tzs)
		return -ENOMEM;

	err = soctherm_clk_enable(pdev, true);
	if (err)
		return err;

	soctherm_init_hw_throt_cdev(pdev);

	soctherm_init(pdev);

	for (i = 0; i < soc->num_ttgs; ++i) {
		struct tegra_thermctl_zone *zone =
			devm_kzalloc(&pdev->dev, sizeof(*zone), GFP_KERNEL);
		if (!zone) {
			err = -ENOMEM;
			goto disable_clocks;
		}

		zone->reg = tegra->regs + soc->ttgs[i]->sensor_temp_offset;
		zone->dev = &pdev->dev;
		zone->sg = soc->ttgs[i];
		zone->ts = tegra;

		z = devm_thermal_zone_of_sensor_register(&pdev->dev,
							 soc->ttgs[i]->id, zone,
							 &tegra_of_thermal_ops);
		if (IS_ERR(z)) {
			err = PTR_ERR(z);
			dev_err(&pdev->dev, "failed to register sensor: %d\n",
				err);
			goto disable_clocks;
		}

		zone->tz = z;
		tegra->thermctl_tzs[soc->ttgs[i]->id] = z;

		/* Configure hw trip points */
		err = tegra_soctherm_set_hwtrips(&pdev->dev, soc->ttgs[i], z);
		if (err)
			goto disable_clocks;
	}

	soctherm_debug_init(pdev);

	return 0;

disable_clocks:
	soctherm_clk_enable(pdev, false);

	return err;
}

static int tegra_soctherm_remove(struct platform_device *pdev)
{
	struct tegra_soctherm *tegra = platform_get_drvdata(pdev);

	debugfs_remove_recursive(tegra->debugfs_dir);

	soctherm_clk_enable(pdev, false);

	return 0;
}

static int __maybe_unused soctherm_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	soctherm_clk_enable(pdev, false);

	return 0;
}

static int __maybe_unused soctherm_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct tegra_soctherm *tegra = platform_get_drvdata(pdev);
	struct tegra_soctherm_soc *soc = tegra->soc;
	int err, i;

	err = soctherm_clk_enable(pdev, true);
	if (err) {
		dev_err(&pdev->dev,
			"Resume failed: enable clocks failed\n");
		return err;
	}

	soctherm_init(pdev);

	for (i = 0; i < soc->num_ttgs; ++i) {
		struct thermal_zone_device *tz;

		tz = tegra->thermctl_tzs[soc->ttgs[i]->id];
		err = tegra_soctherm_set_hwtrips(dev, soc->ttgs[i], tz);
		if (err) {
			dev_err(&pdev->dev,
				"Resume failed: set hwtrips failed\n");
			return err;
		}
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(tegra_soctherm_pm, soctherm_suspend, soctherm_resume);

static struct platform_driver tegra_soctherm_driver = {
	.probe = tegra_soctherm_probe,
	.remove = tegra_soctherm_remove,
	.driver = {
		.name = "tegra_soctherm",
		.pm = &tegra_soctherm_pm,
		.of_match_table = tegra_soctherm_of_match,
	},
};
module_platform_driver(tegra_soctherm_driver);

MODULE_AUTHOR("Mikko Perttunen <mperttunen@nvidia.com>");
MODULE_DESCRIPTION("NVIDIA Tegra SOCTHERM thermal management driver");
MODULE_LICENSE("GPL v2");
