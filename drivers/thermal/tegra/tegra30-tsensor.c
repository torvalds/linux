// SPDX-License-Identifier: GPL-2.0
/*
 * Tegra30 SoC Thermal Sensor driver
 *
 * Based on downstream HWMON driver from NVIDIA.
 * Copyright (C) 2011 NVIDIA Corporation
 *
 * Author: Dmitry Osipenko <digetx@gmail.com>
 * Copyright (C) 2021 GRATE-DRIVER project
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/types.h>

#include <soc/tegra/fuse.h>

#include "../thermal_hwmon.h"

#define TSENSOR_SENSOR0_CONFIG0				0x0
#define TSENSOR_SENSOR0_CONFIG0_SENSOR_STOP		BIT(0)
#define TSENSOR_SENSOR0_CONFIG0_HW_FREQ_DIV_EN		BIT(1)
#define TSENSOR_SENSOR0_CONFIG0_THERMAL_RST_EN		BIT(2)
#define TSENSOR_SENSOR0_CONFIG0_DVFS_EN			BIT(3)
#define TSENSOR_SENSOR0_CONFIG0_INTR_OVERFLOW_EN	BIT(4)
#define TSENSOR_SENSOR0_CONFIG0_INTR_HW_FREQ_DIV_EN	BIT(5)
#define TSENSOR_SENSOR0_CONFIG0_INTR_THERMAL_RST_EN	BIT(6)
#define TSENSOR_SENSOR0_CONFIG0_M			GENMASK(23,  8)
#define TSENSOR_SENSOR0_CONFIG0_N			GENMASK(31, 24)

#define TSENSOR_SENSOR0_CONFIG1				0x8
#define TSENSOR_SENSOR0_CONFIG1_TH1			GENMASK(15,  0)
#define TSENSOR_SENSOR0_CONFIG1_TH2			GENMASK(31, 16)

#define TSENSOR_SENSOR0_CONFIG2				0xc
#define TSENSOR_SENSOR0_CONFIG2_TH3			GENMASK(15,  0)

#define TSENSOR_SENSOR0_STATUS0				0x18
#define TSENSOR_SENSOR0_STATUS0_STATE			GENMASK(2, 0)
#define TSENSOR_SENSOR0_STATUS0_INTR			BIT(8)
#define TSENSOR_SENSOR0_STATUS0_CURRENT_VALID		BIT(9)

#define TSENSOR_SENSOR0_TS_STATUS1			0x1c
#define TSENSOR_SENSOR0_TS_STATUS1_CURRENT_COUNT	GENMASK(31, 16)

#define TEGRA30_FUSE_TEST_PROG_VER			0x28

#define TEGRA30_FUSE_TSENSOR_CALIB			0x98
#define TEGRA30_FUSE_TSENSOR_CALIB_LOW			GENMASK(15,  0)
#define TEGRA30_FUSE_TSENSOR_CALIB_HIGH			GENMASK(31, 16)

#define TEGRA30_FUSE_SPARE_BIT				0x144

struct tegra_tsensor;

struct tegra_tsensor_calibration_data {
	int a, b, m, n, p, r;
};

struct tegra_tsensor_channel {
	void __iomem *regs;
	unsigned int id;
	struct tegra_tsensor *ts;
	struct thermal_zone_device *tzd;
};

struct tegra_tsensor {
	void __iomem *regs;
	bool swap_channels;
	struct clk *clk;
	struct device *dev;
	struct reset_control *rst;
	struct tegra_tsensor_channel ch[2];
	struct tegra_tsensor_calibration_data calib;
};

static int tegra_tsensor_hw_enable(const struct tegra_tsensor *ts)
{
	u32 val;
	int err;

	err = reset_control_assert(ts->rst);
	if (err) {
		dev_err(ts->dev, "failed to assert hardware reset: %d\n", err);
		return err;
	}

	err = clk_prepare_enable(ts->clk);
	if (err) {
		dev_err(ts->dev, "failed to enable clock: %d\n", err);
		return err;
	}

	fsleep(1000);

	err = reset_control_deassert(ts->rst);
	if (err) {
		dev_err(ts->dev, "failed to deassert hardware reset: %d\n", err);
		goto disable_clk;
	}

	/*
	 * Sensors are enabled after reset by default, but not gauging
	 * until clock counter is programmed.
	 *
	 * M: number of reference clock pulses after which every
	 *    temperature / voltage measurement is made
	 *
	 * N: number of reference clock counts for which the counter runs
	 */
	val  = FIELD_PREP(TSENSOR_SENSOR0_CONFIG0_M, 12500);
	val |= FIELD_PREP(TSENSOR_SENSOR0_CONFIG0_N, 255);

	/* apply the same configuration to both channels */
	writel_relaxed(val, ts->regs + 0x40 + TSENSOR_SENSOR0_CONFIG0);
	writel_relaxed(val, ts->regs + 0x80 + TSENSOR_SENSOR0_CONFIG0);

	return 0;

disable_clk:
	clk_disable_unprepare(ts->clk);

	return err;
}

static int tegra_tsensor_hw_disable(const struct tegra_tsensor *ts)
{
	int err;

	err = reset_control_assert(ts->rst);
	if (err) {
		dev_err(ts->dev, "failed to assert hardware reset: %d\n", err);
		return err;
	}

	clk_disable_unprepare(ts->clk);

	return 0;
}

static void devm_tegra_tsensor_hw_disable(void *data)
{
	const struct tegra_tsensor *ts = data;

	tegra_tsensor_hw_disable(ts);
}

static int tegra_tsensor_get_temp(struct thermal_zone_device *tz, int *temp)
{
	const struct tegra_tsensor_channel *tsc = thermal_zone_device_priv(tz);
	const struct tegra_tsensor *ts = tsc->ts;
	int err, c1, c2, c3, c4, counter;
	u32 val;

	/*
	 * Counter will be invalid if hardware is misprogrammed or not enough
	 * time passed since the time when sensor was enabled.
	 */
	err = readl_relaxed_poll_timeout(tsc->regs + TSENSOR_SENSOR0_STATUS0, val,
					 val & TSENSOR_SENSOR0_STATUS0_CURRENT_VALID,
					 21 * USEC_PER_MSEC,
					 21 * USEC_PER_MSEC * 50);
	if (err) {
		dev_err_once(ts->dev, "ch%u: counter invalid\n", tsc->id);
		return err;
	}

	val = readl_relaxed(tsc->regs + TSENSOR_SENSOR0_TS_STATUS1);
	counter = FIELD_GET(TSENSOR_SENSOR0_TS_STATUS1_CURRENT_COUNT, val);

	/*
	 * This shouldn't happen with a valid counter status, nevertheless
	 * lets verify the value since it's in a separate (from status)
	 * register.
	 */
	if (counter == 0xffff) {
		dev_err_once(ts->dev, "ch%u: counter overflow\n", tsc->id);
		return -EINVAL;
	}

	/*
	 * temperature = a * counter + b
	 * temperature = m * (temperature ^ 2) + n * temperature + p
	 */
	c1 = DIV_ROUND_CLOSEST(ts->calib.a * counter + ts->calib.b, 1000000);
	c1 = c1 ?: 1;
	c2 = DIV_ROUND_CLOSEST(ts->calib.p, c1);
	c3 = c1 * ts->calib.m;
	c4 = ts->calib.n;

	*temp = DIV_ROUND_CLOSEST(c1 * (c2 + c3 + c4), 1000);

	return 0;
}

static int tegra_tsensor_temp_to_counter(const struct tegra_tsensor *ts, int temp)
{
	int c1, c2;

	c1 = DIV_ROUND_CLOSEST(ts->calib.p - temp * 1000, ts->calib.m);
	c2 = -ts->calib.r - int_sqrt(ts->calib.r * ts->calib.r - c1);

	return DIV_ROUND_CLOSEST(c2 * 1000000 - ts->calib.b, ts->calib.a);
}

static int tegra_tsensor_set_trips(struct thermal_zone_device *tz, int low, int high)
{
	const struct tegra_tsensor_channel *tsc = thermal_zone_device_priv(tz);
	const struct tegra_tsensor *ts = tsc->ts;
	u32 val;

	/*
	 * TSENSOR doesn't trigger interrupt on the "low" temperature breach,
	 * hence bail out if high temperature is unspecified.
	 */
	if (high == INT_MAX)
		return 0;

	val = readl_relaxed(tsc->regs + TSENSOR_SENSOR0_CONFIG1);
	val &= ~TSENSOR_SENSOR0_CONFIG1_TH1;

	high = tegra_tsensor_temp_to_counter(ts, high);
	val |= FIELD_PREP(TSENSOR_SENSOR0_CONFIG1_TH1, high);
	writel_relaxed(val, tsc->regs + TSENSOR_SENSOR0_CONFIG1);

	return 0;
}

static const struct thermal_zone_device_ops ops = {
	.get_temp = tegra_tsensor_get_temp,
	.set_trips = tegra_tsensor_set_trips,
};

static bool
tegra_tsensor_handle_channel_interrupt(const struct tegra_tsensor *ts,
				       unsigned int id)
{
	const struct tegra_tsensor_channel *tsc = &ts->ch[id];
	u32 val;

	val = readl_relaxed(tsc->regs + TSENSOR_SENSOR0_STATUS0);
	writel_relaxed(val, tsc->regs + TSENSOR_SENSOR0_STATUS0);

	if (FIELD_GET(TSENSOR_SENSOR0_STATUS0_STATE, val) == 5)
		dev_err_ratelimited(ts->dev, "ch%u: counter overflowed\n", id);

	if (!FIELD_GET(TSENSOR_SENSOR0_STATUS0_INTR, val))
		return false;

	thermal_zone_device_update(tsc->tzd, THERMAL_EVENT_UNSPECIFIED);

	return true;
}

static irqreturn_t tegra_tsensor_isr(int irq, void *data)
{
	const struct tegra_tsensor *ts = data;
	bool handled = false;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ts->ch); i++)
		handled |= tegra_tsensor_handle_channel_interrupt(ts, i);

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static int tegra_tsensor_disable_hw_channel(const struct tegra_tsensor *ts,
					    unsigned int id)
{
	const struct tegra_tsensor_channel *tsc = &ts->ch[id];
	struct thermal_zone_device *tzd = tsc->tzd;
	u32 val;
	int err;

	if (!tzd)
		goto stop_channel;

	err = thermal_zone_device_disable(tzd);
	if (err) {
		dev_err(ts->dev, "ch%u: failed to disable zone: %d\n", id, err);
		return err;
	}

stop_channel:
	/* stop channel gracefully */
	val = readl_relaxed(tsc->regs + TSENSOR_SENSOR0_CONFIG0);
	val |= FIELD_PREP(TSENSOR_SENSOR0_CONFIG0_SENSOR_STOP, 1);
	writel_relaxed(val, tsc->regs + TSENSOR_SENSOR0_CONFIG0);

	return 0;
}

struct trip_temps {
	int hot_trip;
	int crit_trip;
};

static int tegra_tsensor_get_trips_cb(struct thermal_trip *trip, void *arg)
{
	struct trip_temps *temps = arg;

	if (trip->type == THERMAL_TRIP_HOT)
		temps->hot_trip = trip->temperature;
	else if (trip->type == THERMAL_TRIP_CRITICAL)
		temps->crit_trip = trip->temperature;

	return 0;
}

static void tegra_tsensor_get_hw_channel_trips(struct thermal_zone_device *tzd,
					       struct trip_temps *temps)
{
	/*
	 * 90C is the maximal critical temperature of all Tegra30 SoC variants,
	 * use it for the default trip if unspecified in a device-tree.
	 */
	temps->hot_trip  = 85000;
	temps->crit_trip = 90000;

	thermal_zone_for_each_trip(tzd, tegra_tsensor_get_trips_cb, temps);

	/* clamp hardware trips to the calibration limits */
	temps->hot_trip = clamp(temps->hot_trip, 25000, 90000);

	/*
	 * Kernel will perform a normal system shut down if it will
	 * see that critical temperature is breached, hence set the
	 * hardware limit by 5C higher in order to allow system to
	 * shut down gracefully before sending signal to the Power
	 * Management controller.
	 */
	temps->crit_trip = clamp(temps->crit_trip + 5000, 25000, 90000);
}

static int tegra_tsensor_enable_hw_channel(const struct tegra_tsensor *ts,
					   unsigned int id)
{
	const struct tegra_tsensor_channel *tsc = &ts->ch[id];
	struct thermal_zone_device *tzd = tsc->tzd;
	struct trip_temps temps = { 0 };
	int err;
	u32 val;

	if (!tzd) {
		val = readl_relaxed(tsc->regs + TSENSOR_SENSOR0_CONFIG0);
		val &= ~TSENSOR_SENSOR0_CONFIG0_SENSOR_STOP;
		writel_relaxed(val, tsc->regs + TSENSOR_SENSOR0_CONFIG0);

		return 0;
	}

	tegra_tsensor_get_hw_channel_trips(tzd, &temps);

	dev_info_once(ts->dev, "ch%u: PMC emergency shutdown trip set to %dC\n",
		      id, DIV_ROUND_CLOSEST(temps.crit_trip, 1000));

	temps.hot_trip  = tegra_tsensor_temp_to_counter(ts, temps.hot_trip);
	temps.crit_trip = tegra_tsensor_temp_to_counter(ts, temps.crit_trip);

	/* program LEVEL2 counter threshold */
	val = readl_relaxed(tsc->regs + TSENSOR_SENSOR0_CONFIG1);
	val &= ~TSENSOR_SENSOR0_CONFIG1_TH2;
	val |= FIELD_PREP(TSENSOR_SENSOR0_CONFIG1_TH2, temps.hot_trip);
	writel_relaxed(val, tsc->regs + TSENSOR_SENSOR0_CONFIG1);

	/* program LEVEL3 counter threshold */
	val = readl_relaxed(tsc->regs + TSENSOR_SENSOR0_CONFIG2);
	val &= ~TSENSOR_SENSOR0_CONFIG2_TH3;
	val |= FIELD_PREP(TSENSOR_SENSOR0_CONFIG2_TH3, temps.crit_trip);
	writel_relaxed(val, tsc->regs + TSENSOR_SENSOR0_CONFIG2);

	/*
	 * Enable sensor, emergency shutdown, interrupts for level 1/2/3
	 * breaches and counter overflow condition.
	 *
	 * Disable DIV2 throttle for now since we need to figure out how
	 * to integrate it properly with the thermal framework.
	 *
	 * Thermal levels supported by hardware:
	 *
	 *     Level 0 = cold
	 *     Level 1 = passive cooling (cpufreq DVFS)
	 *     Level 2 = passive cooling assisted by hardware (DIV2)
	 *     Level 3 = emergency shutdown assisted by hardware (PMC)
	 */
	val = readl_relaxed(tsc->regs + TSENSOR_SENSOR0_CONFIG0);
	val &= ~TSENSOR_SENSOR0_CONFIG0_SENSOR_STOP;
	val |= FIELD_PREP(TSENSOR_SENSOR0_CONFIG0_DVFS_EN, 1);
	val |= FIELD_PREP(TSENSOR_SENSOR0_CONFIG0_HW_FREQ_DIV_EN, 0);
	val |= FIELD_PREP(TSENSOR_SENSOR0_CONFIG0_THERMAL_RST_EN, 1);
	val |= FIELD_PREP(TSENSOR_SENSOR0_CONFIG0_INTR_OVERFLOW_EN, 1);
	val |= FIELD_PREP(TSENSOR_SENSOR0_CONFIG0_INTR_HW_FREQ_DIV_EN, 1);
	val |= FIELD_PREP(TSENSOR_SENSOR0_CONFIG0_INTR_THERMAL_RST_EN, 1);
	writel_relaxed(val, tsc->regs + TSENSOR_SENSOR0_CONFIG0);

	err = thermal_zone_device_enable(tzd);
	if (err) {
		dev_err(ts->dev, "ch%u: failed to enable zone: %d\n", id, err);
		return err;
	}

	return 0;
}

static bool tegra_tsensor_fuse_read_spare(unsigned int spare)
{
	u32 val = 0;

	tegra_fuse_readl(TEGRA30_FUSE_SPARE_BIT + spare * 4, &val);

	return !!val;
}

static int tegra_tsensor_nvmem_setup(struct tegra_tsensor *ts)
{
	u32 i, ate_ver = 0, cal = 0, t1_25C = 0, t2_90C = 0;
	int err, c1_25C, c2_90C;

	err = tegra_fuse_readl(TEGRA30_FUSE_TEST_PROG_VER, &ate_ver);
	if (err) {
		dev_err_probe(ts->dev, err, "failed to get ATE version\n");
		return err;
	}

	if (ate_ver < 8) {
		dev_info(ts->dev, "unsupported ATE version: %u\n", ate_ver);
		return -ENODEV;
	}

	/*
	 * We have two TSENSOR channels in a two different spots on SoC.
	 * Second channel provides more accurate data on older SoC versions,
	 * use it as a primary channel.
	 */
	if (ate_ver <= 21) {
		dev_info_once(ts->dev,
			      "older ATE version detected, channels remapped\n");
		ts->swap_channels = true;
	}

	err = tegra_fuse_readl(TEGRA30_FUSE_TSENSOR_CALIB, &cal);
	if (err) {
		dev_err(ts->dev, "failed to get calibration data: %d\n", err);
		return err;
	}

	/* get calibrated counter values for 25C/90C thresholds */
	c1_25C = FIELD_GET(TEGRA30_FUSE_TSENSOR_CALIB_LOW, cal);
	c2_90C = FIELD_GET(TEGRA30_FUSE_TSENSOR_CALIB_HIGH, cal);

	/* and calibrated temperatures corresponding to the counter values */
	for (i = 0; i < 7; i++) {
		t1_25C |= tegra_tsensor_fuse_read_spare(14 + i) << i;
		t1_25C |= tegra_tsensor_fuse_read_spare(21 + i) << i;

		t2_90C |= tegra_tsensor_fuse_read_spare(0 + i) << i;
		t2_90C |= tegra_tsensor_fuse_read_spare(7 + i) << i;
	}

	if (c2_90C - c1_25C <= t2_90C - t1_25C) {
		dev_err(ts->dev, "invalid calibration data: %d %d %u %u\n",
			c2_90C, c1_25C, t2_90C, t1_25C);
		return -EINVAL;
	}

	/* all calibration coefficients are premultiplied by 1000000 */

	ts->calib.a = DIV_ROUND_CLOSEST((t2_90C - t1_25C) * 1000000,
					(c2_90C - c1_25C));

	ts->calib.b = t1_25C * 1000000 - ts->calib.a * c1_25C;

	if (tegra_sku_info.revision == TEGRA_REVISION_A01) {
		ts->calib.m =     -2775;
		ts->calib.n =   1338811;
		ts->calib.p =  -7300000;
	} else {
		ts->calib.m =     -3512;
		ts->calib.n =   1528943;
		ts->calib.p = -11100000;
	}

	/* except the coefficient of a reduced quadratic equation */
	ts->calib.r = DIV_ROUND_CLOSEST(ts->calib.n, ts->calib.m * 2);

	dev_info_once(ts->dev,
		      "calibration: %d %d %u %u ATE ver: %u SoC rev: %u\n",
		      c2_90C, c1_25C, t2_90C, t1_25C, ate_ver,
		      tegra_sku_info.revision);

	return 0;
}

static int tegra_tsensor_register_channel(struct tegra_tsensor *ts,
					  unsigned int id)
{
	struct tegra_tsensor_channel *tsc = &ts->ch[id];
	unsigned int hw_id = ts->swap_channels ? !id : id;

	tsc->ts = ts;
	tsc->id = id;
	tsc->regs = ts->regs + 0x40 * (hw_id + 1);

	tsc->tzd = devm_thermal_of_zone_register(ts->dev, id, tsc, &ops);
	if (IS_ERR(tsc->tzd)) {
		if (PTR_ERR(tsc->tzd) != -ENODEV)
			return dev_err_probe(ts->dev, PTR_ERR(tsc->tzd),
					     "failed to register thermal zone\n");

		/*
		 * It's okay if sensor isn't assigned to any thermal zone
		 * in a device-tree.
		 */
		tsc->tzd = NULL;
		return 0;
	}

	devm_thermal_add_hwmon_sysfs(ts->dev, tsc->tzd);

	return 0;
}

static int tegra_tsensor_probe(struct platform_device *pdev)
{
	struct tegra_tsensor *ts;
	unsigned int i;
	int err, irq;

	ts = devm_kzalloc(&pdev->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ts->dev = &pdev->dev;
	platform_set_drvdata(pdev, ts);

	ts->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ts->regs))
		return PTR_ERR(ts->regs);

	ts->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(ts->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(ts->clk),
				     "failed to get clock\n");

	ts->rst = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(ts->rst))
		return dev_err_probe(&pdev->dev, PTR_ERR(ts->rst),
				     "failed to get reset control\n");

	err = tegra_tsensor_nvmem_setup(ts);
	if (err)
		return err;

	err = tegra_tsensor_hw_enable(ts);
	if (err)
		return err;

	err = devm_add_action_or_reset(&pdev->dev,
				       devm_tegra_tsensor_hw_disable,
				       ts);
	if (err)
		return err;

	for (i = 0; i < ARRAY_SIZE(ts->ch); i++) {
		err = tegra_tsensor_register_channel(ts, i);
		if (err)
			return err;
	}

	/*
	 * Enable the channels before setting the interrupt so
	 * set_trips() can not be called while we are setting up the
	 * register TSENSOR_SENSOR0_CONFIG1. With this we close a
	 * potential race window where we are setting up the TH2 and
	 * the temperature hits TH1 resulting to an update of the
	 * TSENSOR_SENSOR0_CONFIG1 register in the ISR.
	 */
	for (i = 0; i < ARRAY_SIZE(ts->ch); i++) {
		err = tegra_tsensor_enable_hw_channel(ts, i);
		if (err)
			return err;
	}

	err = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					tegra_tsensor_isr, IRQF_ONESHOT,
					"tegra_tsensor", ts);
	if (err)
		return dev_err_probe(&pdev->dev, err,
				     "failed to request interrupt\n");

	return 0;
}

static int __maybe_unused tegra_tsensor_suspend(struct device *dev)
{
	struct tegra_tsensor *ts = dev_get_drvdata(dev);
	unsigned int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(ts->ch); i++) {
		err = tegra_tsensor_disable_hw_channel(ts, i);
		if (err)
			goto enable_channel;
	}

	err = tegra_tsensor_hw_disable(ts);
	if (err)
		goto enable_channel;

	return 0;

enable_channel:
	while (i--)
		tegra_tsensor_enable_hw_channel(ts, i);

	return err;
}

static int __maybe_unused tegra_tsensor_resume(struct device *dev)
{
	struct tegra_tsensor *ts = dev_get_drvdata(dev);
	unsigned int i;
	int err;

	err = tegra_tsensor_hw_enable(ts);
	if (err)
		return err;

	for (i = 0; i < ARRAY_SIZE(ts->ch); i++) {
		err = tegra_tsensor_enable_hw_channel(ts, i);
		if (err)
			return err;
	}

	return 0;
}

static const struct dev_pm_ops tegra_tsensor_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(tegra_tsensor_suspend,
				      tegra_tsensor_resume)
};

static const struct of_device_id tegra_tsensor_of_match[] = {
	{ .compatible = "nvidia,tegra30-tsensor", },
	{},
};
MODULE_DEVICE_TABLE(of, tegra_tsensor_of_match);

static struct platform_driver tegra_tsensor_driver = {
	.probe = tegra_tsensor_probe,
	.driver = {
		.name = "tegra30-tsensor",
		.of_match_table = tegra_tsensor_of_match,
		.pm = &tegra_tsensor_pm_ops,
	},
};
module_platform_driver(tegra_tsensor_driver);

MODULE_DESCRIPTION("NVIDIA Tegra30 Thermal Sensor driver");
MODULE_AUTHOR("Dmitry Osipenko <digetx@gmail.com>");
MODULE_LICENSE("GPL");
