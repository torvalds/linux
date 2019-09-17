// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014 - 2018, NVIDIA CORPORATION.  All rights reserved.
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
#include <linux/irq.h>
#include <linux/irqdomain.h>
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

#define THERMCTL_INTR_STATUS			0x84

#define TH_INTR_MD0_MASK			BIT(25)
#define TH_INTR_MU0_MASK			BIT(24)
#define TH_INTR_GD0_MASK			BIT(17)
#define TH_INTR_GU0_MASK			BIT(16)
#define TH_INTR_CD0_MASK			BIT(9)
#define TH_INTR_CU0_MASK			BIT(8)
#define TH_INTR_PD0_MASK			BIT(1)
#define TH_INTR_PU0_MASK			BIT(0)
#define TH_INTR_IGNORE_MASK			0xFCFCFCFC

#define THERMCTL_STATS_CTL			0x94
#define STATS_CTL_CLR_DN			0x8
#define STATS_CTL_EN_DN				0x4
#define STATS_CTL_CLR_UP			0x2
#define STATS_CTL_EN_UP				0x1

#define OC1_CFG					0x310
#define OC1_CFG_LONG_LATENCY_MASK		BIT(6)
#define OC1_CFG_HW_RESTORE_MASK			BIT(5)
#define OC1_CFG_PWR_GOOD_MASK_MASK		BIT(4)
#define OC1_CFG_THROTTLE_MODE_MASK		(0x3 << 2)
#define OC1_CFG_ALARM_POLARITY_MASK		BIT(1)
#define OC1_CFG_EN_THROTTLE_MASK		BIT(0)

#define OC1_CNT_THRESHOLD			0x314
#define OC1_THROTTLE_PERIOD			0x318
#define OC1_ALARM_COUNT				0x31c
#define OC1_FILTER				0x320
#define OC1_STATS				0x3a8

#define OC_INTR_STATUS				0x39c
#define OC_INTR_ENABLE				0x3a0
#define OC_INTR_DISABLE				0x3a4
#define OC_STATS_CTL				0x3c4
#define OC_STATS_CTL_CLR_ALL			0x2
#define OC_STATS_CTL_EN_ALL			0x1

#define OC_INTR_OC1_MASK			BIT(0)
#define OC_INTR_OC2_MASK			BIT(1)
#define OC_INTR_OC3_MASK			BIT(2)
#define OC_INTR_OC4_MASK			BIT(3)
#define OC_INTR_OC5_MASK			BIT(4)

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

/* gk20a nv_therm interface N:3 Mapping. Levels defined in tegra124-sochterm.h
 * level	vector
 * NONE		3'b000
 * LOW		3'b001
 * MED		3'b011
 * HIGH		3'b111
 */
#define THROT_LEVEL_TO_DEPTH(level)	((0x1 << (level)) - 1)

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

#define ALARM_OFFSET			0x14
#define ALARM_CFG(throt)		(OC1_CFG + \
					(ALARM_OFFSET * (throt - THROTTLE_OC1)))

#define ALARM_CNT_THRESHOLD(throt)	(OC1_CNT_THRESHOLD + \
					(ALARM_OFFSET * (throt - THROTTLE_OC1)))

#define ALARM_THROTTLE_PERIOD(throt)	(OC1_THROTTLE_PERIOD + \
					(ALARM_OFFSET * (throt - THROTTLE_OC1)))

#define ALARM_ALARM_COUNT(throt)	(OC1_ALARM_COUNT + \
					(ALARM_OFFSET * (throt - THROTTLE_OC1)))

#define ALARM_FILTER(throt)		(OC1_FILTER + \
					(ALARM_OFFSET * (throt - THROTTLE_OC1)))

#define ALARM_STATS(throt)		(OC1_STATS + \
					(4 * (throt - THROTTLE_OC1)))

/* get CCROC_THROT_PSKIP_xxx offset per HIGH/MED/LOW vect*/
#define CCROC_THROT_OFFSET			0x0c
#define CCROC_THROT_PSKIP_CTRL_CPU_REG(vect)    (CCROC_THROT_PSKIP_CTRL_CPU + \
						(CCROC_THROT_OFFSET * vect))
#define CCROC_THROT_PSKIP_RAMP_CPU_REG(vect)    (CCROC_THROT_PSKIP_RAMP_CPU + \
						(CCROC_THROT_OFFSET * vect))

/* get THERMCTL_LEVELx offset per CPU/GPU/MEM/TSENSE rg and LEVEL0~3 lv */
#define THERMCTL_LVL_REGS_SIZE		0x20
#define THERMCTL_LVL_REG(rg, lv)	((rg) + ((lv) * THERMCTL_LVL_REGS_SIZE))

#define OC_THROTTLE_MODE_DISABLED	0
#define OC_THROTTLE_MODE_BRIEF		2

static const int min_low_temp = -127000;
static const int max_high_temp = 127000;

enum soctherm_throttle_id {
	THROTTLE_LIGHT = 0,
	THROTTLE_HEAVY,
	THROTTLE_OC1,
	THROTTLE_OC2,
	THROTTLE_OC3,
	THROTTLE_OC4,
	THROTTLE_OC5, /* OC5 is reserved */
	THROTTLE_SIZE,
};

enum soctherm_oc_irq_id {
	TEGRA_SOC_OC_IRQ_1,
	TEGRA_SOC_OC_IRQ_2,
	TEGRA_SOC_OC_IRQ_3,
	TEGRA_SOC_OC_IRQ_4,
	TEGRA_SOC_OC_IRQ_5,
	TEGRA_SOC_OC_IRQ_MAX,
};

enum soctherm_throttle_dev_id {
	THROTTLE_DEV_CPU = 0,
	THROTTLE_DEV_GPU,
	THROTTLE_DEV_SIZE,
};

static const char *const throt_names[] = {
	[THROTTLE_LIGHT] = "light",
	[THROTTLE_HEAVY] = "heavy",
	[THROTTLE_OC1]   = "oc1",
	[THROTTLE_OC2]   = "oc2",
	[THROTTLE_OC3]   = "oc3",
	[THROTTLE_OC4]   = "oc4",
	[THROTTLE_OC5]   = "oc5",
};

struct tegra_soctherm;
struct tegra_thermctl_zone {
	void __iomem *reg;
	struct device *dev;
	struct tegra_soctherm *ts;
	struct thermal_zone_device *tz;
	const struct tegra_tsensor_group *sg;
};

struct soctherm_oc_cfg {
	u32 active_low;
	u32 throt_period;
	u32 alarm_cnt_thresh;
	u32 alarm_filter;
	u32 mode;
	bool intr_en;
};

struct soctherm_throt_cfg {
	const char *name;
	unsigned int id;
	u8 priority;
	u8 cpu_throt_level;
	u32 cpu_throt_depth;
	u32 gpu_throt_level;
	struct soctherm_oc_cfg oc_cfg;
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

	int thermal_irq;
	int edp_irq;

	u32 *calib;
	struct thermal_zone_device **thermctl_tzs;
	struct tegra_soctherm_soc *soc;

	struct soctherm_throt_cfg throt_cfgs[THROTTLE_SIZE];

	struct dentry *debugfs_dir;

	struct mutex thermctl_lock;
};

struct soctherm_oc_irq_chip_data {
	struct mutex		irq_lock; /* serialize OC IRQs */
	struct irq_chip		irq_chip;
	struct irq_domain	*domain;
	int			irq_enable;
};

static struct soctherm_oc_irq_chip_data soc_irq_cdata;

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

static int tsensor_group_thermtrip_get(struct tegra_soctherm *ts, int id)
{
	int i, temp = min_low_temp;
	struct tsensor_group_thermtrips *tt = ts->soc->thermtrips;

	if (id >= TEGRA124_SOCTHERM_SENSOR_NUM)
		return temp;

	if (tt) {
		for (i = 0; i < ts->soc->num_ttgs; i++) {
			if (tt[i].id == id)
				return tt[i].temp;
		}
	}

	return temp;
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
		/*
		 * If thermtrips property is set in DT,
		 * doesn't need to program critical type trip to HW,
		 * if not, program critical trip to HW.
		 */
		if (min_low_temp == tsensor_group_thermtrip_get(ts, sg->id))
			return thermtrip_program(dev, sg, temp);
		else
			return 0;

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

static int tegra_thermctl_get_trend(void *data, int trip,
				    enum thermal_trend *trend)
{
	struct tegra_thermctl_zone *zone = data;
	struct thermal_zone_device *tz = zone->tz;
	int trip_temp, temp, last_temp, ret;

	if (!tz)
		return -EINVAL;

	ret = tz->ops->get_trip_temp(zone->tz, trip, &trip_temp);
	if (ret)
		return ret;

	temp = READ_ONCE(tz->temperature);
	last_temp = READ_ONCE(tz->last_temperature);

	if (temp > trip_temp) {
		if (temp >= last_temp)
			*trend = THERMAL_TREND_RAISING;
		else
			*trend = THERMAL_TREND_STABLE;
	} else if (temp < trip_temp) {
		*trend = THERMAL_TREND_DROPPING;
	} else {
		*trend = THERMAL_TREND_STABLE;
	}

	return 0;
}

static void thermal_irq_enable(struct tegra_thermctl_zone *zn)
{
	u32 r;

	/* multiple zones could be handling and setting trips at once */
	mutex_lock(&zn->ts->thermctl_lock);
	r = readl(zn->ts->regs + THERMCTL_INTR_ENABLE);
	r = REG_SET_MASK(r, zn->sg->thermctl_isr_mask, TH_INTR_UP_DN_EN);
	writel(r, zn->ts->regs + THERMCTL_INTR_ENABLE);
	mutex_unlock(&zn->ts->thermctl_lock);
}

static void thermal_irq_disable(struct tegra_thermctl_zone *zn)
{
	u32 r;

	/* multiple zones could be handling and setting trips at once */
	mutex_lock(&zn->ts->thermctl_lock);
	r = readl(zn->ts->regs + THERMCTL_INTR_DISABLE);
	r = REG_SET_MASK(r, zn->sg->thermctl_isr_mask, 0);
	writel(r, zn->ts->regs + THERMCTL_INTR_DISABLE);
	mutex_unlock(&zn->ts->thermctl_lock);
}

static int tegra_thermctl_set_trips(void *data, int lo, int hi)
{
	struct tegra_thermctl_zone *zone = data;
	u32 r;

	thermal_irq_disable(zone);

	r = readl(zone->ts->regs + zone->sg->thermctl_lvl0_offset);
	r = REG_SET_MASK(r, THERMCTL_LVL0_CPU0_EN_MASK, 0);
	writel(r, zone->ts->regs + zone->sg->thermctl_lvl0_offset);

	lo = enforce_temp_range(zone->dev, lo) / zone->ts->soc->thresh_grain;
	hi = enforce_temp_range(zone->dev, hi) / zone->ts->soc->thresh_grain;
	dev_dbg(zone->dev, "%s hi:%d, lo:%d\n", __func__, hi, lo);

	r = REG_SET_MASK(r, zone->sg->thermctl_lvl0_up_thresh_mask, hi);
	r = REG_SET_MASK(r, zone->sg->thermctl_lvl0_dn_thresh_mask, lo);
	r = REG_SET_MASK(r, THERMCTL_LVL0_CPU0_EN_MASK, 1);
	writel(r, zone->ts->regs + zone->sg->thermctl_lvl0_offset);

	thermal_irq_enable(zone);

	return 0;
}

static const struct thermal_zone_of_device_ops tegra_of_thermal_ops = {
	.get_temp = tegra_thermctl_get_temp,
	.set_trip_temp = tegra_thermctl_set_trip_temp,
	.get_trend = tegra_thermctl_get_trend,
	.set_trips = tegra_thermctl_set_trips,
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
 * "THROTTLE" trip points , using "thermtrips", "critical" or "hot"
 * type trip_temp
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
	int i, trip, temperature, ret;

	/* Get thermtrips. If missing, try to get critical trips. */
	temperature = tsensor_group_thermtrip_get(ts, sg->id);
	if (min_low_temp == temperature)
		if (tz->ops->get_crit_temp(tz, &temperature))
			temperature = max_high_temp;

	ret = thermtrip_program(dev, sg, temperature);
	if (ret) {
		dev_err(dev, "thermtrip: %s: error during enable\n", sg->name);
		return ret;
	}

	dev_info(dev, "thermtrip: will shut down when %s reaches %d mC\n",
		 sg->name, temperature);

	ret = get_hot_temp(tz, &trip, &temperature);
	if (ret) {
		dev_info(dev, "throttrip: %s: missing hot temperature\n",
			 sg->name);
		return 0;
	}

	for (i = 0; i < THROTTLE_OC1; i++) {
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
		dev_info(dev, "throttrip: %s: missing throttle cdev\n",
			 sg->name);

	return 0;
}

static irqreturn_t soctherm_thermal_isr(int irq, void *dev_id)
{
	struct tegra_soctherm *ts = dev_id;
	u32 r;

	/* Case for no lock:
	 * Although interrupts are enabled in set_trips, there is still no need
	 * to lock here because the interrupts are disabled before programming
	 * new trip points. Hence there cant be a interrupt on the same sensor.
	 * An interrupt can however occur on a sensor while trips are being
	 * programmed on a different one. This beign a LEVEL interrupt won't
	 * cause a new interrupt but this is taken care of by the re-reading of
	 * the STATUS register in the thread function.
	 */
	r = readl(ts->regs + THERMCTL_INTR_STATUS);
	writel(r, ts->regs + THERMCTL_INTR_DISABLE);

	return IRQ_WAKE_THREAD;
}

/**
 * soctherm_thermal_isr_thread() - Handles a thermal interrupt request
 * @irq:       The interrupt number being requested; not used
 * @dev_id:    Opaque pointer to tegra_soctherm;
 *
 * Clears the interrupt status register if there are expected
 * interrupt bits set.
 * The interrupt(s) are then handled by updating the corresponding
 * thermal zones.
 *
 * An error is logged if any unexpected interrupt bits are set.
 *
 * Disabled interrupts are re-enabled.
 *
 * Return: %IRQ_HANDLED. Interrupt was handled and no further processing
 * is needed.
 */
static irqreturn_t soctherm_thermal_isr_thread(int irq, void *dev_id)
{
	struct tegra_soctherm *ts = dev_id;
	struct thermal_zone_device *tz;
	u32 st, ex = 0, cp = 0, gp = 0, pl = 0, me = 0;

	st = readl(ts->regs + THERMCTL_INTR_STATUS);

	/* deliberately clear expected interrupts handled in SW */
	cp |= st & TH_INTR_CD0_MASK;
	cp |= st & TH_INTR_CU0_MASK;

	gp |= st & TH_INTR_GD0_MASK;
	gp |= st & TH_INTR_GU0_MASK;

	pl |= st & TH_INTR_PD0_MASK;
	pl |= st & TH_INTR_PU0_MASK;

	me |= st & TH_INTR_MD0_MASK;
	me |= st & TH_INTR_MU0_MASK;

	ex |= cp | gp | pl | me;
	if (ex) {
		writel(ex, ts->regs + THERMCTL_INTR_STATUS);
		st &= ~ex;

		if (cp) {
			tz = ts->thermctl_tzs[TEGRA124_SOCTHERM_SENSOR_CPU];
			thermal_zone_device_update(tz,
						   THERMAL_EVENT_UNSPECIFIED);
		}

		if (gp) {
			tz = ts->thermctl_tzs[TEGRA124_SOCTHERM_SENSOR_GPU];
			thermal_zone_device_update(tz,
						   THERMAL_EVENT_UNSPECIFIED);
		}

		if (pl) {
			tz = ts->thermctl_tzs[TEGRA124_SOCTHERM_SENSOR_PLLX];
			thermal_zone_device_update(tz,
						   THERMAL_EVENT_UNSPECIFIED);
		}

		if (me) {
			tz = ts->thermctl_tzs[TEGRA124_SOCTHERM_SENSOR_MEM];
			thermal_zone_device_update(tz,
						   THERMAL_EVENT_UNSPECIFIED);
		}
	}

	/* deliberately ignore expected interrupts NOT handled in SW */
	ex |= TH_INTR_IGNORE_MASK;
	st &= ~ex;

	if (st) {
		/* Whine about any other unexpected INTR bits still set */
		pr_err("soctherm: Ignored unexpected INTRs 0x%08x\n", st);
		writel(st, ts->regs + THERMCTL_INTR_STATUS);
	}

	return IRQ_HANDLED;
}

/**
 * soctherm_oc_intr_enable() - Enables the soctherm over-current interrupt
 * @alarm:		The soctherm throttle id
 * @enable:		Flag indicating enable the soctherm over-current
 *			interrupt or disable it
 *
 * Enables a specific over-current pins @alarm to raise an interrupt if the flag
 * is set and the alarm corresponds to OC1, OC2, OC3, or OC4.
 */
static void soctherm_oc_intr_enable(struct tegra_soctherm *ts,
				    enum soctherm_throttle_id alarm,
				    bool enable)
{
	u32 r;

	if (!enable)
		return;

	r = readl(ts->regs + OC_INTR_ENABLE);
	switch (alarm) {
	case THROTTLE_OC1:
		r = REG_SET_MASK(r, OC_INTR_OC1_MASK, 1);
		break;
	case THROTTLE_OC2:
		r = REG_SET_MASK(r, OC_INTR_OC2_MASK, 1);
		break;
	case THROTTLE_OC3:
		r = REG_SET_MASK(r, OC_INTR_OC3_MASK, 1);
		break;
	case THROTTLE_OC4:
		r = REG_SET_MASK(r, OC_INTR_OC4_MASK, 1);
		break;
	default:
		r = 0;
		break;
	}
	writel(r, ts->regs + OC_INTR_ENABLE);
}

/**
 * soctherm_handle_alarm() - Handles soctherm alarms
 * @alarm:		The soctherm throttle id
 *
 * "Handles" over-current alarms (OC1, OC2, OC3, and OC4) by printing
 * a warning or informative message.
 *
 * Return: -EINVAL for @alarm = THROTTLE_OC3, otherwise 0 (success).
 */
static int soctherm_handle_alarm(enum soctherm_throttle_id alarm)
{
	int rv = -EINVAL;

	switch (alarm) {
	case THROTTLE_OC1:
		pr_debug("soctherm: Successfully handled OC1 alarm\n");
		rv = 0;
		break;

	case THROTTLE_OC2:
		pr_debug("soctherm: Successfully handled OC2 alarm\n");
		rv = 0;
		break;

	case THROTTLE_OC3:
		pr_debug("soctherm: Successfully handled OC3 alarm\n");
		rv = 0;
		break;

	case THROTTLE_OC4:
		pr_debug("soctherm: Successfully handled OC4 alarm\n");
		rv = 0;
		break;

	default:
		break;
	}

	if (rv)
		pr_err("soctherm: ERROR in handling %s alarm\n",
		       throt_names[alarm]);

	return rv;
}

/**
 * soctherm_edp_isr_thread() - log an over-current interrupt request
 * @irq:	OC irq number. Currently not being used. See description
 * @arg:	a void pointer for callback, currently not being used
 *
 * Over-current events are handled in hardware. This function is called to log
 * and handle any OC events that happened. Additionally, it checks every
 * over-current interrupt registers for registers are set but
 * was not expected (i.e. any discrepancy in interrupt status) by the function,
 * the discrepancy will logged.
 *
 * Return: %IRQ_HANDLED
 */
static irqreturn_t soctherm_edp_isr_thread(int irq, void *arg)
{
	struct tegra_soctherm *ts = arg;
	u32 st, ex, oc1, oc2, oc3, oc4;

	st = readl(ts->regs + OC_INTR_STATUS);

	/* deliberately clear expected interrupts handled in SW */
	oc1 = st & OC_INTR_OC1_MASK;
	oc2 = st & OC_INTR_OC2_MASK;
	oc3 = st & OC_INTR_OC3_MASK;
	oc4 = st & OC_INTR_OC4_MASK;
	ex = oc1 | oc2 | oc3 | oc4;

	pr_err("soctherm: OC ALARM 0x%08x\n", ex);
	if (ex) {
		writel(st, ts->regs + OC_INTR_STATUS);
		st &= ~ex;

		if (oc1 && !soctherm_handle_alarm(THROTTLE_OC1))
			soctherm_oc_intr_enable(ts, THROTTLE_OC1, true);

		if (oc2 && !soctherm_handle_alarm(THROTTLE_OC2))
			soctherm_oc_intr_enable(ts, THROTTLE_OC2, true);

		if (oc3 && !soctherm_handle_alarm(THROTTLE_OC3))
			soctherm_oc_intr_enable(ts, THROTTLE_OC3, true);

		if (oc4 && !soctherm_handle_alarm(THROTTLE_OC4))
			soctherm_oc_intr_enable(ts, THROTTLE_OC4, true);

		if (oc1 && soc_irq_cdata.irq_enable & BIT(0))
			handle_nested_irq(
				irq_find_mapping(soc_irq_cdata.domain, 0));

		if (oc2 && soc_irq_cdata.irq_enable & BIT(1))
			handle_nested_irq(
				irq_find_mapping(soc_irq_cdata.domain, 1));

		if (oc3 && soc_irq_cdata.irq_enable & BIT(2))
			handle_nested_irq(
				irq_find_mapping(soc_irq_cdata.domain, 2));

		if (oc4 && soc_irq_cdata.irq_enable & BIT(3))
			handle_nested_irq(
				irq_find_mapping(soc_irq_cdata.domain, 3));
	}

	if (st) {
		pr_err("soctherm: Ignored unexpected OC ALARM 0x%08x\n", st);
		writel(st, ts->regs + OC_INTR_STATUS);
	}

	return IRQ_HANDLED;
}

/**
 * soctherm_edp_isr() - Disables any active interrupts
 * @irq:	The interrupt request number
 * @arg:	Opaque pointer to an argument
 *
 * Writes to the OC_INTR_DISABLE register the over current interrupt status,
 * masking any asserted interrupts. Doing this prevents the same interrupts
 * from triggering this isr repeatedly. The thread woken by this isr will
 * handle asserted interrupts and subsequently unmask/re-enable them.
 *
 * The OC_INTR_DISABLE register indicates which OC interrupts
 * have been disabled.
 *
 * Return: %IRQ_WAKE_THREAD, handler requests to wake the handler thread
 */
static irqreturn_t soctherm_edp_isr(int irq, void *arg)
{
	struct tegra_soctherm *ts = arg;
	u32 r;

	if (!ts)
		return IRQ_NONE;

	r = readl(ts->regs + OC_INTR_STATUS);
	writel(r, ts->regs + OC_INTR_DISABLE);

	return IRQ_WAKE_THREAD;
}

/**
 * soctherm_oc_irq_lock() - locks the over-current interrupt request
 * @data:	Interrupt request data
 *
 * Looks up the chip data from @data and locks the mutex associated with
 * a particular over-current interrupt request.
 */
static void soctherm_oc_irq_lock(struct irq_data *data)
{
	struct soctherm_oc_irq_chip_data *d = irq_data_get_irq_chip_data(data);

	mutex_lock(&d->irq_lock);
}

/**
 * soctherm_oc_irq_sync_unlock() - Unlocks the OC interrupt request
 * @data:		Interrupt request data
 *
 * Looks up the interrupt request data @data and unlocks the mutex associated
 * with a particular over-current interrupt request.
 */
static void soctherm_oc_irq_sync_unlock(struct irq_data *data)
{
	struct soctherm_oc_irq_chip_data *d = irq_data_get_irq_chip_data(data);

	mutex_unlock(&d->irq_lock);
}

/**
 * soctherm_oc_irq_enable() - Enables the SOC_THERM over-current interrupt queue
 * @data:       irq_data structure of the chip
 *
 * Sets the irq_enable bit of SOC_THERM allowing SOC_THERM
 * to respond to over-current interrupts.
 *
 */
static void soctherm_oc_irq_enable(struct irq_data *data)
{
	struct soctherm_oc_irq_chip_data *d = irq_data_get_irq_chip_data(data);

	d->irq_enable |= BIT(data->hwirq);
}

/**
 * soctherm_oc_irq_disable() - Disables overcurrent interrupt requests
 * @irq_data:	The interrupt request information
 *
 * Clears the interrupt request enable bit of the overcurrent
 * interrupt request chip data.
 *
 * Return: Nothing is returned (void)
 */
static void soctherm_oc_irq_disable(struct irq_data *data)
{
	struct soctherm_oc_irq_chip_data *d = irq_data_get_irq_chip_data(data);

	d->irq_enable &= ~BIT(data->hwirq);
}

static int soctherm_oc_irq_set_type(struct irq_data *data, unsigned int type)
{
	return 0;
}

/**
 * soctherm_oc_irq_map() - SOC_THERM interrupt request domain mapper
 * @h:		Interrupt request domain
 * @virq:	Virtual interrupt request number
 * @hw:		Hardware interrupt request number
 *
 * Mapping callback function for SOC_THERM's irq_domain. When a SOC_THERM
 * interrupt request is called, the irq_domain takes the request's virtual
 * request number (much like a virtual memory address) and maps it to a
 * physical hardware request number.
 *
 * When a mapping doesn't already exist for a virtual request number, the
 * irq_domain calls this function to associate the virtual request number with
 * a hardware request number.
 *
 * Return: 0
 */
static int soctherm_oc_irq_map(struct irq_domain *h, unsigned int virq,
		irq_hw_number_t hw)
{
	struct soctherm_oc_irq_chip_data *data = h->host_data;

	irq_set_chip_data(virq, data);
	irq_set_chip(virq, &data->irq_chip);
	irq_set_nested_thread(virq, 1);
	return 0;
}

/**
 * soctherm_irq_domain_xlate_twocell() - xlate for soctherm interrupts
 * @d:      Interrupt request domain
 * @intspec:    Array of u32s from DTs "interrupt" property
 * @intsize:    Number of values inside the intspec array
 * @out_hwirq:  HW IRQ value associated with this interrupt
 * @out_type:   The IRQ SENSE type for this interrupt.
 *
 * This Device Tree IRQ specifier translation function will translate a
 * specific "interrupt" as defined by 2 DT values where the cell values map
 * the hwirq number + 1 and linux irq flags. Since the output is the hwirq
 * number, this function will subtract 1 from the value listed in DT.
 *
 * Return: 0
 */
static int soctherm_irq_domain_xlate_twocell(struct irq_domain *d,
	struct device_node *ctrlr, const u32 *intspec, unsigned int intsize,
	irq_hw_number_t *out_hwirq, unsigned int *out_type)
{
	if (WARN_ON(intsize < 2))
		return -EINVAL;

	/*
	 * The HW value is 1 index less than the DT IRQ values.
	 * i.e. OC4 goes to HW index 3.
	 */
	*out_hwirq = intspec[0] - 1;
	*out_type = intspec[1] & IRQ_TYPE_SENSE_MASK;
	return 0;
}

static const struct irq_domain_ops soctherm_oc_domain_ops = {
	.map	= soctherm_oc_irq_map,
	.xlate	= soctherm_irq_domain_xlate_twocell,
};

/**
 * soctherm_oc_int_init() - Initial enabling of the over
 * current interrupts
 * @np:	The devicetree node for soctherm
 * @num_irqs:	The number of new interrupt requests
 *
 * Sets the over current interrupt request chip data
 *
 * Return: 0 on success or if overcurrent interrupts are not enabled,
 * -ENOMEM (out of memory), or irq_base if the function failed to
 * allocate the irqs
 */
static int soctherm_oc_int_init(struct device_node *np, int num_irqs)
{
	if (!num_irqs) {
		pr_info("%s(): OC interrupts are not enabled\n", __func__);
		return 0;
	}

	mutex_init(&soc_irq_cdata.irq_lock);
	soc_irq_cdata.irq_enable = 0;

	soc_irq_cdata.irq_chip.name = "soc_therm_oc";
	soc_irq_cdata.irq_chip.irq_bus_lock = soctherm_oc_irq_lock;
	soc_irq_cdata.irq_chip.irq_bus_sync_unlock =
		soctherm_oc_irq_sync_unlock;
	soc_irq_cdata.irq_chip.irq_disable = soctherm_oc_irq_disable;
	soc_irq_cdata.irq_chip.irq_enable = soctherm_oc_irq_enable;
	soc_irq_cdata.irq_chip.irq_set_type = soctherm_oc_irq_set_type;
	soc_irq_cdata.irq_chip.irq_set_wake = NULL;

	soc_irq_cdata.domain = irq_domain_add_linear(np, num_irqs,
						     &soctherm_oc_domain_ops,
						     &soc_irq_cdata);

	if (!soc_irq_cdata.domain) {
		pr_err("%s: Failed to create IRQ domain\n", __func__);
		return -ENOMEM;
	}

	pr_debug("%s(): OC interrupts enabled successful\n", __func__);
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

DEFINE_SHOW_ATTRIBUTE(regs);

static void soctherm_debug_init(struct platform_device *pdev)
{
	struct tegra_soctherm *tegra = platform_get_drvdata(pdev);
	struct dentry *root;

	root = debugfs_create_dir("soctherm", NULL);

	tegra->debugfs_dir = root;

	debugfs_create_file("reg_contents", 0644, root, pdev, &regs_fops);
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

static int soctherm_thermtrips_parse(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra_soctherm *ts = dev_get_drvdata(dev);
	struct tsensor_group_thermtrips *tt = ts->soc->thermtrips;
	const int max_num_prop = ts->soc->num_ttgs * 2;
	u32 *tlb;
	int i, j, n, ret;

	if (!tt)
		return -ENOMEM;

	n = of_property_count_u32_elems(dev->of_node, "nvidia,thermtrips");
	if (n <= 0) {
		dev_info(dev,
			 "missing thermtrips, will use critical trips as shut down temp\n");
		return n;
	}

	n = min(max_num_prop, n);

	tlb = devm_kcalloc(&pdev->dev, max_num_prop, sizeof(u32), GFP_KERNEL);
	if (!tlb)
		return -ENOMEM;
	ret = of_property_read_u32_array(dev->of_node, "nvidia,thermtrips",
					 tlb, n);
	if (ret) {
		dev_err(dev, "invalid num ele: thermtrips:%d\n", ret);
		return ret;
	}

	i = 0;
	for (j = 0; j < n; j = j + 2) {
		if (tlb[j] >= TEGRA124_SOCTHERM_SENSOR_NUM)
			continue;

		tt[i].id = tlb[j];
		tt[i].temp = tlb[j + 1];
		i++;
	}

	return 0;
}

static void soctherm_oc_cfg_parse(struct device *dev,
				struct device_node *np_oc,
				struct soctherm_throt_cfg *stc)
{
	u32 val;

	if (of_property_read_bool(np_oc, "nvidia,polarity-active-low"))
		stc->oc_cfg.active_low = 1;
	else
		stc->oc_cfg.active_low = 0;

	if (!of_property_read_u32(np_oc, "nvidia,count-threshold", &val)) {
		stc->oc_cfg.intr_en = 1;
		stc->oc_cfg.alarm_cnt_thresh = val;
	}

	if (!of_property_read_u32(np_oc, "nvidia,throttle-period-us", &val))
		stc->oc_cfg.throt_period = val;

	if (!of_property_read_u32(np_oc, "nvidia,alarm-filter", &val))
		stc->oc_cfg.alarm_filter = val;

	/* BRIEF throttling by default, do not support STICKY */
	stc->oc_cfg.mode = OC_THROTTLE_MODE_BRIEF;
}

static int soctherm_throt_cfg_parse(struct device *dev,
				    struct device_node *np,
				    struct soctherm_throt_cfg *stc)
{
	struct tegra_soctherm *ts = dev_get_drvdata(dev);
	int ret;
	u32 val;

	ret = of_property_read_u32(np, "nvidia,priority", &val);
	if (ret) {
		dev_err(dev, "throttle-cfg: %s: invalid priority\n", stc->name);
		return -EINVAL;
	}
	stc->priority = val;

	ret = of_property_read_u32(np, ts->soc->use_ccroc ?
				   "nvidia,cpu-throt-level" :
				   "nvidia,cpu-throt-percent", &val);
	if (!ret) {
		if (ts->soc->use_ccroc &&
		    val <= TEGRA_SOCTHERM_THROT_LEVEL_HIGH)
			stc->cpu_throt_level = val;
		else if (!ts->soc->use_ccroc && val <= 100)
			stc->cpu_throt_depth = val;
		else
			goto err;
	} else {
		goto err;
	}

	ret = of_property_read_u32(np, "nvidia,gpu-throt-level", &val);
	if (!ret && val <= TEGRA_SOCTHERM_THROT_LEVEL_HIGH)
		stc->gpu_throt_level = val;
	else
		goto err;

	return 0;

err:
	dev_err(dev, "throttle-cfg: %s: no throt prop or invalid prop\n",
		stc->name);
	return -EINVAL;
}

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
	int i;

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
		int err;

		name = np_stcc->name;
		stc = find_throttle_cfg_by_name(ts, name);
		if (!stc) {
			dev_err(dev,
				"throttle-cfg: could not find %s\n", name);
			continue;
		}

		if (stc->init) {
			dev_err(dev, "throttle-cfg: %s: redefined!\n", name);
			of_node_put(np_stcc);
			break;
		}

		err = soctherm_throt_cfg_parse(dev, np_stcc, stc);
		if (err)
			continue;

		if (stc->id >= THROTTLE_OC1) {
			soctherm_oc_cfg_parse(dev, np_stcc, stc);
			stc->init = true;
		} else {

			tcd = thermal_of_cooling_device_register(np_stcc,
							 (char *)name, ts,
							 &throt_cooling_ops);
			if (IS_ERR_OR_NULL(tcd)) {
				dev_err(dev,
					"throttle-cfg: %s: failed to register cooling device\n",
					name);
				continue;
			}
			stc->cdev = tcd;
			stc->init = true;
		}

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
 * throttlectl_gpu_level_select() - selects throttling level for GPU
 * @throt: the LIGHT/HEAVY of throttle event id
 *
 * This function programs soctherm's interface to GK20a NV_THERM to select
 * pre-configured "Low", "Medium" or "Heavy" throttle levels.
 *
 * Return: boolean true if HW was programmed
 */
static void throttlectl_gpu_level_select(struct tegra_soctherm *ts,
					 enum soctherm_throttle_id throt)
{
	u32 r, level, throt_vect;

	level = ts->throt_cfgs[throt].gpu_throt_level;
	throt_vect = THROT_LEVEL_TO_DEPTH(level);
	r = readl(ts->regs + THROT_PSKIP_CTRL(throt, THROTTLE_DEV_GPU));
	r = REG_SET_MASK(r, THROT_PSKIP_CTRL_ENABLE_MASK, 1);
	r = REG_SET_MASK(r, THROT_PSKIP_CTRL_VECT_GPU_MASK, throt_vect);
	writel(r, ts->regs + THROT_PSKIP_CTRL(throt, THROTTLE_DEV_GPU));
}

static int soctherm_oc_cfg_program(struct tegra_soctherm *ts,
				      enum soctherm_throttle_id throt)
{
	u32 r;
	struct soctherm_oc_cfg *oc = &ts->throt_cfgs[throt].oc_cfg;

	if (oc->mode == OC_THROTTLE_MODE_DISABLED)
		return -EINVAL;

	r = REG_SET_MASK(0, OC1_CFG_HW_RESTORE_MASK, 1);
	r = REG_SET_MASK(r, OC1_CFG_THROTTLE_MODE_MASK, oc->mode);
	r = REG_SET_MASK(r, OC1_CFG_ALARM_POLARITY_MASK, oc->active_low);
	r = REG_SET_MASK(r, OC1_CFG_EN_THROTTLE_MASK, 1);
	writel(r, ts->regs + ALARM_CFG(throt));
	writel(oc->throt_period, ts->regs + ALARM_THROTTLE_PERIOD(throt));
	writel(oc->alarm_cnt_thresh, ts->regs + ALARM_CNT_THRESHOLD(throt));
	writel(oc->alarm_filter, ts->regs + ALARM_FILTER(throt));
	soctherm_oc_intr_enable(ts, throt, oc->intr_en);

	return 0;
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

	if ((throt >= THROTTLE_OC1) && (soctherm_oc_cfg_program(ts, throt)))
		return;

	/* Setup PSKIP parameters */
	if (ts->soc->use_ccroc)
		throttlectl_cpu_level_select(ts, throt);
	else
		throttlectl_cpu_mn(ts, throt);

	throttlectl_gpu_level_select(ts, throt);

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

static int soctherm_interrupts_init(struct platform_device *pdev,
				    struct tegra_soctherm *tegra)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;

	ret = soctherm_oc_int_init(np, TEGRA_SOC_OC_IRQ_MAX);
	if (ret < 0) {
		dev_err(&pdev->dev, "soctherm_oc_int_init failed\n");
		return ret;
	}

	tegra->thermal_irq = platform_get_irq(pdev, 0);
	if (tegra->thermal_irq < 0) {
		dev_dbg(&pdev->dev, "get 'thermal_irq' failed.\n");
		return 0;
	}

	tegra->edp_irq = platform_get_irq(pdev, 1);
	if (tegra->edp_irq < 0) {
		dev_dbg(&pdev->dev, "get 'edp_irq' failed.\n");
		return 0;
	}

	ret = devm_request_threaded_irq(&pdev->dev,
					tegra->thermal_irq,
					soctherm_thermal_isr,
					soctherm_thermal_isr_thread,
					IRQF_ONESHOT,
					dev_name(&pdev->dev),
					tegra);
	if (ret < 0) {
		dev_err(&pdev->dev, "request_irq 'thermal_irq' failed.\n");
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev,
					tegra->edp_irq,
					soctherm_edp_isr,
					soctherm_edp_isr_thread,
					IRQF_ONESHOT,
					"soctherm_edp",
					tegra);
	if (ret < 0) {
		dev_err(&pdev->dev, "request_irq 'edp_irq' failed.\n");
		return ret;
	}

	return 0;
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

	mutex_init(&tegra->thermctl_lock);
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
					   soc->num_ttgs, sizeof(z),
					   GFP_KERNEL);
	if (!tegra->thermctl_tzs)
		return -ENOMEM;

	err = soctherm_clk_enable(pdev, true);
	if (err)
		return err;

	soctherm_thermtrips_parse(pdev);

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

	err = soctherm_interrupts_init(pdev, tegra);

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
