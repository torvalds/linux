/*
 * Copyright 2013-2016 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/busfreq-imx.h>
#include <linux/clk.h>
#include <linux/cpu_cooling.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/device_cooling.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/thermal.h>
#include <linux/types.h>

#define REG_SET		0x4
#define REG_CLR		0x8
#define REG_TOG		0xc

#define MISC0				0x0150
#define MISC0_REFTOP_SELBIASOFF		(1 << 3)
#define MISC1				0x0160
#define MISC1_IRQ_TEMPHIGH		(1 << 29)
/* Below LOW and PANIC bits are only for TEMPMON_IMX6SX */
#define MISC1_IRQ_TEMPLOW		(1 << 28)
#define MISC1_IRQ_TEMPPANIC		(1 << 27)

/* i.MX6 specific */
#define IMX6_TEMPSENSE0				0X180
#define IMX6_TEMPSENSE0_ALARM_VALUE_SHIFT	20
#define IMX6_TEMPSENSE0_ALARM_VALUE_MASK	(0xfff << 20)
#define IMX6_TEMPSENSE0_TEMP_CNT_SHIFT		8
#define IMX6_TEMPSENSE0_TEMP_CNT_MASK		(0xfff << 8)
#define IMX6_TEMPSENSE0_FINISHED		(1 << 2)
#define IMX6_TEMPSENSE0_MEASURE_TEMP		(1 << 1)
#define IMX6_TEMPSENSE0_POWER_DOWN		(1 << 0)

#define IMX6_TEMPSENSE1				0X190
#define IMX6_TEMPSENSE1_MEASURE_FREQ		0xffff
#define IMX6_TEMPSENSE1_MEASURE_FREQ_SHIFT	0

/* Below TEMPSENSE2 is only for TEMPMON_IMX6SX */
#define TEMPSENSE2			0x0290
#define TEMPSENSE2_LOW_VALUE_SHIFT	0
#define TEMPSENSE2_LOW_VALUE_MASK	0xfff
#define TEMPSENSE2_PANIC_VALUE_SHIFT	16
#define TEMPSENSE2_PANIC_VALUE_MASK	0xfff0000

/* i.MX7D specific */
#define IMX7_ANADIG_DIGPROG			0x800
#define IMX7_TEMPSENSE0				0X300
#define IMX7_TEMPSENSE0_PANIC_ALARM_SHIFT	18
#define IMX7_TEMPSENSE0_PANIC_ALARM_MASK	(0x1ff << 18)
#define IMX7_TEMPSENSE0_HIGH_ALARM_SHIFT	9
#define IMX7_TEMPSENSE0_HIGH_ALARM_MASK		(0x1ff << 9)
#define IMX7_TEMPSENSE0_LOW_ALARM_SHIFT		0
#define IMX7_TEMPSENSE0_LOW_ALARM_MASK		0x1ff

#define IMX7_TEMPSENSE1				0X310
#define IMX7_TEMPSENSE1_MEASURE_FREQ_SHIFT	16
#define IMX7_TEMPSENSE1_MEASURE_FREQ_MASK	(0xffff << 16)
#define IMX7_TEMPSENSE1_FINISHED		(1 << 11)
#define IMX7_TEMPSENSE1_MEASURE_TEMP		(1 << 10)
#define IMX7_TEMPSENSE1_POWER_DOWN		(1 << 9)
#define IMX7_TEMPSENSE1_TEMP_VALUE_SHIFT	0
#define IMX7_TEMPSENSE1_TEMP_VALUE_MASK		0x1ff

#define IMX6_OCOTP_ANA1		0x04e0
#define IMX7_OCOTP_ANA1		0x04f0

/* The driver supports 1 passive trip point and 1 critical trip point */
enum imx_thermal_trip {
	IMX_TRIP_PASSIVE,
	IMX_TRIP_CRITICAL,
	IMX_TRIP_NUM,
};

/*
 * It defines the temperature in millicelsius for passive trip point
 * that will trigger cooling action when crossed.
 */
#define IMX_TEMP_PASSIVE		85000
#define IMX_TEMP_PASSIVE_COOL_DELTA	10000

#define IMX_POLLING_DELAY		2000 /* millisecond */
#define IMX_PASSIVE_DELAY		1000

#define FACTOR0				10000000
#define FACTOR1				15423
#define FACTOR2				4148468
#define OFFSET				3580661

#define TEMPMON_IMX6Q			1
#define TEMPMON_IMX6SX			2
#define TEMPMON_IMX7			3

/* the register offsets and bitfields may change across
 * i.MX SOCs, use below struct as a description of the
 * register.
 */

struct thermal_soc_data {
	u32 sensor_ctrl;	/* tempmon sensor basic control */
	u32 power_down_mask;
	u32 measure_temp_mask;

	u32 measure_freq_ctrl;
	u32 measure_freq_mask;
	u32 measure_freq_shift;

	u32 temp_data;
	u32 temp_value_mask;
	u32 temp_value_shift;
	u32 temp_valid_mask;

	u32 panic_alarm_ctrl;
	u32 panic_alarm_mask;
	u32 panic_alarm_shift;

	u32 high_alarm_ctrl;
	u32 high_alarm_mask;
	u32 high_alarm_shift;

	u32 low_alarm_ctrl;
	u32 low_alarm_mask;
	u32 low_alarm_shift;

	u32 version;
};

static struct thermal_soc_data thermal_imx6q_data = {
	.version = TEMPMON_IMX6Q,

	.sensor_ctrl = IMX6_TEMPSENSE0,
	.power_down_mask = IMX6_TEMPSENSE0_POWER_DOWN,
	.measure_temp_mask = IMX6_TEMPSENSE0_MEASURE_TEMP,

	.measure_freq_ctrl = IMX6_TEMPSENSE1,
	.measure_freq_shift = IMX6_TEMPSENSE1_MEASURE_FREQ_SHIFT,
	.measure_freq_mask = IMX6_TEMPSENSE1_MEASURE_FREQ,

	.temp_data = IMX6_TEMPSENSE0,
	.temp_value_mask = IMX6_TEMPSENSE0_TEMP_CNT_MASK,
	.temp_value_shift = IMX6_TEMPSENSE0_TEMP_CNT_SHIFT,
	.temp_valid_mask = IMX6_TEMPSENSE0_FINISHED,

	.high_alarm_ctrl = IMX6_TEMPSENSE0,
	.high_alarm_mask = IMX6_TEMPSENSE0_ALARM_VALUE_MASK,
	.high_alarm_shift = IMX6_TEMPSENSE0_ALARM_VALUE_SHIFT,
};

static struct thermal_soc_data thermal_imx6sx_data = {
	.version = TEMPMON_IMX6SX,

	.sensor_ctrl = IMX6_TEMPSENSE0,
	.power_down_mask = IMX6_TEMPSENSE0_POWER_DOWN,
	.measure_temp_mask = IMX6_TEMPSENSE0_MEASURE_TEMP,

	.measure_freq_ctrl = IMX6_TEMPSENSE1,
	.measure_freq_shift = IMX6_TEMPSENSE1_MEASURE_FREQ_SHIFT,
	.measure_freq_mask = IMX6_TEMPSENSE1_MEASURE_FREQ,

	.temp_data = IMX6_TEMPSENSE0,
	.temp_value_mask = IMX6_TEMPSENSE0_TEMP_CNT_MASK,
	.temp_value_shift = IMX6_TEMPSENSE0_TEMP_CNT_SHIFT,
	.temp_valid_mask = IMX6_TEMPSENSE0_FINISHED,

	.high_alarm_ctrl = IMX6_TEMPSENSE0,
	.high_alarm_mask = IMX6_TEMPSENSE0_ALARM_VALUE_MASK,
	.high_alarm_shift = IMX6_TEMPSENSE0_ALARM_VALUE_SHIFT,

	.panic_alarm_ctrl = TEMPSENSE2,
	.panic_alarm_mask = TEMPSENSE2_PANIC_VALUE_MASK,
	.panic_alarm_shift = TEMPSENSE2_PANIC_VALUE_SHIFT,
};

static struct thermal_soc_data thermal_imx7d_data = {
	.version = TEMPMON_IMX7,

	.sensor_ctrl = IMX7_TEMPSENSE1,
	.power_down_mask = IMX7_TEMPSENSE1_POWER_DOWN,
	.measure_temp_mask = IMX7_TEMPSENSE1_MEASURE_TEMP,

	.measure_freq_ctrl = IMX7_TEMPSENSE1,
	.measure_freq_shift = IMX7_TEMPSENSE1_MEASURE_FREQ_SHIFT,
	.measure_freq_mask = IMX7_TEMPSENSE1_MEASURE_FREQ_MASK,

	.temp_data = IMX7_TEMPSENSE1,
	.temp_value_mask = IMX7_TEMPSENSE1_TEMP_VALUE_MASK,
	.temp_value_shift = IMX7_TEMPSENSE1_TEMP_VALUE_SHIFT,
	.temp_valid_mask = IMX7_TEMPSENSE1_FINISHED,

	.panic_alarm_ctrl = IMX7_TEMPSENSE1,
	.panic_alarm_mask = IMX7_TEMPSENSE0_PANIC_ALARM_MASK,
	.panic_alarm_shift = IMX7_TEMPSENSE0_PANIC_ALARM_SHIFT,

	.high_alarm_ctrl = IMX7_TEMPSENSE0,
	.high_alarm_mask = IMX7_TEMPSENSE0_HIGH_ALARM_MASK,
	.high_alarm_shift = IMX7_TEMPSENSE0_HIGH_ALARM_SHIFT,

	.low_alarm_ctrl = IMX7_TEMPSENSE0,
	.low_alarm_mask = IMX7_TEMPSENSE0_LOW_ALARM_MASK,
	.low_alarm_shift = IMX7_TEMPSENSE0_LOW_ALARM_SHIFT,

};

struct imx_thermal_data {
	struct thermal_zone_device *tz;
	struct thermal_cooling_device *cdev[2];
	enum thermal_device_mode mode;
	struct regmap *tempmon;
	u32 c1, c2; /* See formula in imx_get_sensor_data() */
	unsigned long temp_passive;
	unsigned long temp_critical;
	unsigned long alarm_temp;
	unsigned long last_temp;
	bool irq_enabled;
	int irq;
	struct clk *thermal_clk;
	struct mutex mutex;
	const struct thermal_soc_data *socdata;
};

static struct imx_thermal_data *imx_thermal_data;
static int skip_finish_check;

static void imx_set_panic_temp(struct imx_thermal_data *data,
			       signed long panic_temp)
{
	const struct thermal_soc_data *soc_data = data->socdata;
	struct regmap *map = data->tempmon;
	int critical_value;

	if (data->socdata->version == TEMPMON_IMX7)
		critical_value = panic_temp / 1000 + data->c1 - 25;
	else
		critical_value = (data->c2 - panic_temp) / data->c1;

	regmap_write(map, soc_data->panic_alarm_ctrl + REG_CLR,
		     soc_data->panic_alarm_mask);
	regmap_write(map, soc_data->panic_alarm_ctrl + REG_SET,
		     critical_value << soc_data->panic_alarm_shift);
}

static void imx_set_alarm_temp(struct imx_thermal_data *data,
			       signed long alarm_temp)
{
	const struct thermal_soc_data *soc_data = data->socdata;
	struct regmap *map = data->tempmon;
	int alarm_value;

	data->alarm_temp = alarm_temp;

	if (data->socdata->version == TEMPMON_IMX7)
		alarm_value = alarm_temp / 1000 + data->c1 - 25;
	else
		alarm_value = (data->c2 - alarm_temp) / data->c1;

	regmap_write(map, soc_data->high_alarm_ctrl + REG_CLR,
		     soc_data->high_alarm_mask);
	regmap_write(map, soc_data->high_alarm_ctrl + REG_SET,
		     alarm_value << soc_data->high_alarm_shift);
}

static int imx_get_temp(struct thermal_zone_device *tz, unsigned long *temp)
{
	struct imx_thermal_data *data = tz->devdata;
	const struct thermal_soc_data *soc_data = data->socdata;
	struct regmap *map = data->tempmon;
	unsigned int n_meas;
	bool wait;
	u32 val;

	mutex_lock(&data->mutex);
	if (data->mode == THERMAL_DEVICE_ENABLED) {
		/* Check if a measurement is currently in progress */
		regmap_read(map, soc_data->temp_data, &val);
		wait = !(val & soc_data->temp_valid_mask);
	} else {
		/*
		 * Every time we measure the temperature, we will power on the
		 * temperature sensor, enable measurements, take a reading,
		 * disable measurements, power off the temperature sensor.
		 */
		clk_prepare_enable(data->thermal_clk);
		regmap_write(map, soc_data->sensor_ctrl + REG_CLR,
			    soc_data->power_down_mask);
		regmap_write(map, soc_data->sensor_ctrl + REG_SET,
			    soc_data->measure_temp_mask);

		wait = true;
	}

	/*
	 * According to the temp sensor designers, it may require up to ~17us
	 * to complete a measurement.
	 */
	if (wait) {
		/*
		 * On i.MX7 TO1.0, the finish bit can only keep 1us after
		 * the measured data available. It is hard for software to
		 * polling this bit. So wait for 20ms to make sure the
		 * measured data is valid.
		 */
		if (data->socdata->version == TEMPMON_IMX7 && skip_finish_check)
			msleep(20);
		 else
			usleep_range(20, 50);
		regmap_read(map, soc_data->temp_data, &val);
	}

	if (data->mode != THERMAL_DEVICE_ENABLED) {
		regmap_write(map, soc_data->sensor_ctrl + REG_CLR,
			     soc_data->measure_temp_mask);
		regmap_write(map, soc_data->sensor_ctrl + REG_SET,
			     soc_data->power_down_mask);
		clk_disable_unprepare(data->thermal_clk);
	}

	if (!skip_finish_check && ((val & soc_data->temp_valid_mask) == 0)) {
		dev_dbg(&tz->device, "temp measurement never finished\n");
		mutex_unlock(&data->mutex);
		return -EAGAIN;
	}

	n_meas = (val & soc_data->temp_value_mask) >> soc_data->temp_value_shift;
	/* See imx_get_sensor_data() for formula derivation */
	*temp = data->c2 - n_meas * data->c1;
	if (data->socdata->version == TEMPMON_IMX7)
		*temp = (n_meas - data->c1 + 25) * 1000;
	else
		*temp = data->c2 - n_meas * data->c1;

	/* Update alarm value to next higher trip point for TEMPMON_IMX6Q */
	if (data->socdata->version == TEMPMON_IMX6Q) {
		if (data->alarm_temp == data->temp_passive &&
			*temp >= data->temp_passive)
			imx_set_alarm_temp(data, data->temp_critical);
		if (data->alarm_temp == data->temp_critical &&
			*temp < data->temp_passive) {
			imx_set_alarm_temp(data, data->temp_passive);
			dev_dbg(&tz->device, "thermal alarm off: T < %lu\n",
				data->alarm_temp / 1000);
		}
	}

	if (*temp != data->last_temp) {
		dev_dbg(&tz->device, "millicelsius: %ld\n", *temp);
		data->last_temp = *temp;
	}

	/* Reenable alarm IRQ if temperature below alarm temperature */
	if (!data->irq_enabled && *temp < data->alarm_temp) {
		data->irq_enabled = true;
		enable_irq(data->irq);
	}
	mutex_unlock(&data->mutex);

	return 0;
}

static int imx_get_mode(struct thermal_zone_device *tz,
			enum thermal_device_mode *mode)
{
	struct imx_thermal_data *data = tz->devdata;

	*mode = data->mode;

	return 0;
}

static int imx_set_mode(struct thermal_zone_device *tz,
			enum thermal_device_mode mode)
{
	struct imx_thermal_data *data = tz->devdata;
	const struct thermal_soc_data *soc_data = data->socdata;
	struct regmap *map = data->tempmon;

	if (mode == THERMAL_DEVICE_ENABLED) {
		tz->polling_delay = IMX_POLLING_DELAY;
		tz->passive_delay = IMX_PASSIVE_DELAY;

		regmap_write(map, soc_data->sensor_ctrl + REG_CLR,
			     soc_data->power_down_mask);
		regmap_write(map, soc_data->sensor_ctrl + REG_SET,
			     soc_data->measure_temp_mask);
		if (!data->irq_enabled) {
			data->irq_enabled = true;
			enable_irq(data->irq);
		}
	} else {
		regmap_write(map, soc_data->sensor_ctrl + REG_CLR,
			     soc_data->measure_temp_mask);
		regmap_write(map, soc_data->sensor_ctrl + REG_SET,
			     soc_data->power_down_mask);

		tz->polling_delay = 0;
		tz->passive_delay = 0;

		if (data->irq_enabled) {
			disable_irq(data->irq);
			data->irq_enabled = false;
		}
	}

	data->mode = mode;
	thermal_zone_device_update(tz);

	return 0;
}

static int imx_get_trip_type(struct thermal_zone_device *tz, int trip,
			     enum thermal_trip_type *type)
{
	*type = (trip == IMX_TRIP_PASSIVE) ? THERMAL_TRIP_PASSIVE :
					     THERMAL_TRIP_CRITICAL;
	return 0;
}

static int imx_get_crit_temp(struct thermal_zone_device *tz,
			     unsigned long *temp)
{
	struct imx_thermal_data *data = tz->devdata;

	*temp = data->temp_critical;
	return 0;
}

static int imx_get_trip_temp(struct thermal_zone_device *tz, int trip,
			     unsigned long *temp)
{
	struct imx_thermal_data *data = tz->devdata;

	*temp = (trip == IMX_TRIP_PASSIVE) ? data->temp_passive :
					     data->temp_critical;
	return 0;
}

static int imx_set_trip_temp(struct thermal_zone_device *tz, int trip,
			     unsigned long temp)
{
	struct imx_thermal_data *data = tz->devdata;

	if (trip == IMX_TRIP_CRITICAL) {
		data->temp_critical = temp;
		if (data->socdata->version == TEMPMON_IMX6SX)
			imx_set_panic_temp(data, temp);
	}

	if (trip == IMX_TRIP_PASSIVE) {
		if (temp > IMX_TEMP_PASSIVE)
			return -EINVAL;
		data->temp_passive = temp;
		imx_set_alarm_temp(data, temp);
	}

	return 0;
}

static int imx_bind(struct thermal_zone_device *tz,
		    struct thermal_cooling_device *cdev)
{
	int ret;

	ret = thermal_zone_bind_cooling_device(tz, IMX_TRIP_PASSIVE, cdev,
					       THERMAL_NO_LIMIT,
					       THERMAL_NO_LIMIT);
	if (ret) {
		dev_err(&tz->device,
			"binding zone %s with cdev %s failed:%d\n",
			tz->type, cdev->type, ret);
		return ret;
	}

	return 0;
}

static int imx_unbind(struct thermal_zone_device *tz,
		      struct thermal_cooling_device *cdev)
{
	int ret;

	ret = thermal_zone_unbind_cooling_device(tz, IMX_TRIP_PASSIVE, cdev);
	if (ret) {
		dev_err(&tz->device,
			"unbinding zone %s with cdev %s failed:%d\n",
			tz->type, cdev->type, ret);
		return ret;
	}

	return 0;
}

static int imx_get_trend(struct thermal_zone_device *tz,
	int trip, enum thermal_trend *trend)
{
	int ret;
	unsigned long trip_temp;

	ret = imx_get_trip_temp(tz, trip, &trip_temp);
	if (ret < 0)
		return ret;

	if (tz->temperature >= (trip_temp - IMX_TEMP_PASSIVE_COOL_DELTA))
		*trend = THERMAL_TREND_RAISE_FULL;
	else
		*trend = THERMAL_TREND_DROP_FULL;

	return 0;
}
static struct thermal_zone_device_ops imx_tz_ops = {
	.bind = imx_bind,
	.unbind = imx_unbind,
	.get_temp = imx_get_temp,
	.get_mode = imx_get_mode,
	.set_mode = imx_set_mode,
	.get_trip_type = imx_get_trip_type,
	.get_trip_temp = imx_get_trip_temp,
	.get_crit_temp = imx_get_crit_temp,
	.set_trip_temp = imx_set_trip_temp,
	.get_trend = imx_get_trend,
};

static inline void imx6_calibrate_data(struct imx_thermal_data *data, u32 val)
{
	int t1, t2, n1, n2;
	u64 temp64;
	/*
	 * Sensor data layout:
	 *   [31:20] - sensor value @ 25C
	 *    [19:8] - sensor value of hot
	 *     [7:0] - hot temperature value
	 * Use universal formula now and only need sensor value @ 25C
	 * slope = 0.4297157 - (0.0015976 * 25C fuse)
	 */
	n1 = val >> 20;
	n2 = (val & 0xfff00) >> 8;
	t2 = val & 0xff;
	t1 = 25; /* t1 always 25C */

	/*
	 * Derived from linear interpolation:
	 * slope = 0.4297157 - (0.0015976 * 25C fuse)
	 * slope = (FACTOR2 - FACTOR1 * n1) / FACTOR0
	 * offset = OFFSET / 1000000
	 * (Nmeas - n1) / (Tmeas - t1) = slope
	 * We want to reduce this down to the minimum computation necessary
	 * for each temperature read.  Also, we want Tmeas in millicelsius
	 * and we don't want to lose precision from integer division. So...
	 * Tmeas = (Nmeas - n1) / slope + t1 + offset
	 * milli_Tmeas = 1000 * (Nmeas - n1) / slope + 1000 * t1 + OFFSET / 1000
	 * milli_Tmeas = -1000 * (n1 - Nmeas) / slope + 1000 * t1 + OFFSET /1000
	 * Let constant c1 = (-1000 / slope)
	 * milli_Tmeas = (n1 - Nmeas) * c1 + 1000 * t1 + OFFSET / 1000
	 * Let constant c2 = n1 *c1 + 1000 * t1 + OFFSET / 1000
	 * milli_Tmeas = c2 - Nmeas * c1
	 */
	temp64 = FACTOR0;
	temp64 *= 1000;
	do_div(temp64, FACTOR1 * n1 - FACTOR2);
	data->c1 = temp64;
	temp64 = OFFSET;
	do_div(temp64, 1000);
	data->c2 = n1 * data->c1 + 1000 * t1 + temp64;
}

/*
 * On i.MX7, we only use the calibration data at 25C to get the temp,
 * Tmeas = ( Nmeas - n1) + 25; n1 is the fuse value for 25C.
 */
static inline void imx7_calibrate_data(struct imx_thermal_data *data, u32 val)
{
	data->c1 = (val >> 9) & 0x1ff;
}

static int imx_get_sensor_data(struct platform_device *pdev)
{
	struct imx_thermal_data *data = platform_get_drvdata(pdev);
	struct regmap *map;
	int ret;
	u32 val;

	map = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
					      "fsl,tempmon-data");
	if (IS_ERR(map)) {
		ret = PTR_ERR(map);
		dev_err(&pdev->dev, "failed to get sensor regmap: %d\n", ret);
		return ret;
	}

	if (data->socdata->version == TEMPMON_IMX7)
		ret = regmap_read(map, IMX7_OCOTP_ANA1, &val);
	else
		ret = regmap_read(map, IMX6_OCOTP_ANA1, &val);

	if (ret) {
		dev_err(&pdev->dev, "failed to read sensor data: %d\n", ret);
		return ret;
	}

	if (val == 0 || val == ~0) {
		dev_err(&pdev->dev, "invalid sensor calibration data\n");
		return -EINVAL;
	}

	if (data->socdata->version == TEMPMON_IMX7)
		imx7_calibrate_data(data, val);
	else
		imx6_calibrate_data(data, val);

	/*
	 * Set the default passive cooling trip point to IMX_TEMP_PASSIVE.
	 * Can be changed from userspace.
	 */
	data->temp_passive = IMX_TEMP_PASSIVE;

	/*
	 * Set the default critical trip point to 20 C higher
	 * than passive trip point. Can be changed from userspace.
	 */
	data->temp_critical = IMX_TEMP_PASSIVE + 20 * 1000;

	return 0;
}

static irqreturn_t imx_thermal_alarm_irq(int irq, void *dev)
{
	struct imx_thermal_data *data = dev;

	disable_irq_nosync(irq);
	data->irq_enabled = false;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t imx_thermal_alarm_irq_thread(int irq, void *dev)
{
	struct imx_thermal_data *data = dev;

	dev_dbg(&data->tz->device, "THERMAL ALARM: T > %lu\n",
		data->alarm_temp / 1000);

	thermal_zone_device_update(data->tz);

	return IRQ_HANDLED;
}

static const struct of_device_id of_imx_thermal_match[] = {
	{ .compatible = "fsl,imx6q-tempmon", .data = &thermal_imx6q_data, },
	{ .compatible = "fsl,imx6sx-tempmon", .data = &thermal_imx6sx_data, },
	{ .compatible = "fsl,imx7d-tempmon", .data = &thermal_imx7d_data, },
	{ /* end */ }
};
MODULE_DEVICE_TABLE(of, of_imx_thermal_match);

static int thermal_notifier_event(struct notifier_block *this,
					unsigned long event, void *ptr)
{
	const struct thermal_soc_data *soc_data = imx_thermal_data->socdata;
	struct regmap *map = imx_thermal_data->tempmon;

	mutex_lock(&imx_thermal_data->mutex);

	switch (event) {
	/*
	 * In low_bus_freq_mode, the thermal sensor auto measurement
	 * can be disabled to low the power consumption.
	 */
	case LOW_BUSFREQ_ENTER:
		regmap_write(map, soc_data->sensor_ctrl + REG_CLR,
			     soc_data->measure_temp_mask);
		regmap_write(map, soc_data->sensor_ctrl + REG_SET,
			     soc_data->power_down_mask);
		imx_thermal_data->mode = THERMAL_DEVICE_DISABLED;
		disable_irq(imx_thermal_data->irq);
		clk_disable_unprepare(imx_thermal_data->thermal_clk);
		break;

	/* Enabled thermal auto measurement when exiting low_bus_freq_mode */
	case LOW_BUSFREQ_EXIT:
		clk_prepare_enable(imx_thermal_data->thermal_clk);
		regmap_write(map, soc_data->sensor_ctrl + REG_CLR,
			     soc_data->power_down_mask);
		regmap_write(map, soc_data->sensor_ctrl + REG_SET,
			     soc_data->measure_temp_mask);
		imx_thermal_data->mode = THERMAL_DEVICE_ENABLED;
		enable_irq(imx_thermal_data->irq);
		break;

	default:
		break;
	}
	mutex_unlock(&imx_thermal_data->mutex);

	return NOTIFY_OK;
}

static struct notifier_block thermal_notifier = {
	.notifier_call = thermal_notifier_event,
};

static int imx_thermal_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
		of_match_device(of_imx_thermal_match, &pdev->dev);
	struct imx_thermal_data *data;
	struct regmap *map;
	int measure_freq;
	int ret, revision;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	imx_thermal_data = data;

	map = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "fsl,tempmon");
	if (IS_ERR(map)) {
		ret = PTR_ERR(map);
		dev_err(&pdev->dev, "failed to get tempmon regmap: %d\n", ret);
		return ret;
	}
	data->tempmon = map;

	data->socdata = of_id->data;

	/* make sure the IRQ flag is clear before enabling irq on i.MX6SX */
	if (data->socdata->version == TEMPMON_IMX6SX) {
		regmap_write(map, MISC1 + REG_CLR, MISC1_IRQ_TEMPHIGH |
			MISC1_IRQ_TEMPLOW | MISC1_IRQ_TEMPPANIC);
		/*
		 * reset value of LOW ALARM is incorrect, set it to lowest
		 * value to avoid false trigger of low alarm.
		 */
		regmap_write(map, TEMPSENSE2 + REG_SET,
			TEMPSENSE2_LOW_VALUE_MASK);
	}

	data->irq = platform_get_irq(pdev, 0);
	if (data->irq < 0)
		return data->irq;

	platform_set_drvdata(pdev, data);

	ret = imx_get_sensor_data(pdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to get sensor data\n");
		return ret;
	}

	/*
	 * for i.MX7D TO1.0, finish bit is not available, check the
	 * SOC revision to skip checking the finish bit status.
	 */
	regmap_read(map, IMX7_ANADIG_DIGPROG, &revision);
	if ((revision & 0xff) == 0x10)
		skip_finish_check = 1;

	/* Make sure sensor is in known good state for measurements */
	regmap_write(map, data->socdata->sensor_ctrl + REG_CLR,
		     data->socdata->power_down_mask);
	regmap_write(map, data->socdata->sensor_ctrl + REG_CLR,
		     data->socdata->measure_temp_mask);
	regmap_write(map, data->socdata->measure_freq_ctrl + REG_CLR,
		     data->socdata->measure_freq_mask);
	if (data->socdata->version != TEMPMON_IMX7)
		regmap_write(map, MISC0 + REG_SET, MISC0_REFTOP_SELBIASOFF);
	regmap_write(map, data->socdata->sensor_ctrl + REG_SET,
		     data->socdata->power_down_mask);

	data->cdev[0] = cpufreq_cooling_register(cpu_present_mask);
	if (IS_ERR(data->cdev[0])) {
		ret = PTR_ERR(data->cdev[0]);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"failed to register cpufreq cooling device: %d\n",
				ret);
		return ret;
	}

	data->cdev[1] = devfreq_cooling_register();
	if (IS_ERR(data->cdev[1])) {
		ret = PTR_ERR(data->cdev[1]);
		if (ret != -EPROBE_DEFER) {
			dev_err(&pdev->dev,
				"failed to register cpufreq cooling device: %d\n",
				ret);
			cpufreq_cooling_unregister(data->cdev[0]);
		}
		return ret;
	}

	data->thermal_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(data->thermal_clk)) {
		ret = PTR_ERR(data->thermal_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"failed to get thermal clk: %d\n", ret);
		cpufreq_cooling_unregister(data->cdev[0]);
		return ret;
	}

	/*
	 * Thermal sensor needs clk on to get correct value, normally
	 * we should enable its clk before taking measurement and disable
	 * clk after measurement is done, but if alarm function is enabled,
	 * hardware will auto measure the temperature periodically, so we
	 * need to keep the clk always on for alarm function.
	 */
	ret = clk_prepare_enable(data->thermal_clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable thermal clk: %d\n", ret);
		cpufreq_cooling_unregister(data->cdev[0]);
		devfreq_cooling_unregister(data->cdev[1]);
		return ret;
	}

	mutex_init(&data->mutex);
	data->tz = thermal_zone_device_register("imx_thermal_zone",
						IMX_TRIP_NUM,
						(1 << IMX_TRIP_NUM) - 1, data,
						&imx_tz_ops, NULL,
						IMX_PASSIVE_DELAY,
						IMX_POLLING_DELAY);
	if (IS_ERR(data->tz)) {
		ret = PTR_ERR(data->tz);
		dev_err(&pdev->dev,
			"failed to register thermal zone device %d\n", ret);
		clk_disable_unprepare(data->thermal_clk);
		cpufreq_cooling_unregister(data->cdev[0]);
		devfreq_cooling_unregister(data->cdev[1]);
		return ret;
	}

	/* Enable measurements at ~ 10 Hz */
	regmap_write(map, data->socdata->measure_freq_ctrl + REG_CLR,
		     data->socdata->measure_freq_mask);
	measure_freq = DIV_ROUND_UP(32768, 10); /* 10 Hz */
	regmap_write(map, data->socdata->measure_freq_ctrl + REG_SET,
		     measure_freq << data->socdata->measure_freq_shift);
	imx_set_alarm_temp(data, data->temp_passive);

	if (data->socdata->version == TEMPMON_IMX6SX)
		imx_set_panic_temp(data, data->temp_critical);

	regmap_write(map, data->socdata->sensor_ctrl + REG_CLR,
		     data->socdata->power_down_mask);
	regmap_write(map, data->socdata->sensor_ctrl + REG_SET,
		     data->socdata->measure_temp_mask);

	ret = devm_request_threaded_irq(&pdev->dev, data->irq,
			imx_thermal_alarm_irq, imx_thermal_alarm_irq_thread,
			0, "imx_thermal", data);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request alarm irq: %d\n", ret);
		return ret;
	}

	data->irq_enabled = true;
	data->mode = THERMAL_DEVICE_ENABLED;

	/* register the busfreq notifier called in low bus freq */
	if (data->socdata->version != TEMPMON_IMX7)
		register_busfreq_notifier(&thermal_notifier);

	return 0;
}

static int imx_thermal_remove(struct platform_device *pdev)
{
	struct imx_thermal_data *data = platform_get_drvdata(pdev);
	struct regmap *map = data->tempmon;

	/* Disable measurements */
	regmap_write(map, data->socdata->sensor_ctrl + REG_SET,
		     data->socdata->power_down_mask);
	if (!IS_ERR(data->thermal_clk))
		clk_disable_unprepare(data->thermal_clk);

	/* unregister the busfreq notifier called in low bus freq */
	if (data->socdata->version != TEMPMON_IMX7)
		unregister_busfreq_notifier(&thermal_notifier);

	thermal_zone_device_unregister(data->tz);
	cpufreq_cooling_unregister(data->cdev[0]);
	devfreq_cooling_unregister(data->cdev[1]);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int imx_thermal_suspend(struct device *dev)
{
	struct imx_thermal_data *data = dev_get_drvdata(dev);
	struct regmap *map = data->tempmon;

	/*
	 * Need to disable thermal sensor, otherwise, when thermal core
	 * try to get temperature before thermal sensor resume, a wrong
	 * temperature will be read as the thermal sensor is powered
	 * down.
	 */
	regmap_write(map, data->socdata->sensor_ctrl + REG_CLR,
		     data->socdata->measure_temp_mask);
	regmap_write(map, data->socdata->sensor_ctrl + REG_SET,
		     data->socdata->power_down_mask);
	data->mode = THERMAL_DEVICE_DISABLED;
	clk_disable_unprepare(data->thermal_clk);

	return 0;
}

static int imx_thermal_resume(struct device *dev)
{
	struct imx_thermal_data *data = dev_get_drvdata(dev);
	struct regmap *map = data->tempmon;

	clk_prepare_enable(data->thermal_clk);
	/* Enabled thermal sensor after resume */
	regmap_write(map, data->socdata->sensor_ctrl + REG_CLR,
		     data->socdata->power_down_mask);
	regmap_write(map, data->socdata->sensor_ctrl + REG_SET,
		     data->socdata->measure_temp_mask);
	data->mode = THERMAL_DEVICE_ENABLED;

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(imx_thermal_pm_ops,
			 imx_thermal_suspend, imx_thermal_resume);

static struct platform_driver imx_thermal = {
	.driver = {
		.name	= "imx_thermal",
		.pm	= &imx_thermal_pm_ops,
		.of_match_table = of_imx_thermal_match,
	},
	.probe		= imx_thermal_probe,
	.remove		= imx_thermal_remove,
};
module_platform_driver(imx_thermal);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("Thermal driver for Freescale i.MX SoCs");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx-thermal");
