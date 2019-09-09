// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2018 - All Rights Reserved
 * Author: David Hernandez Sanchez <david.hernandezsanchez@st.com> for
 * STMicroelectronics.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

#include "../thermal_core.h"
#include "../thermal_hwmon.h"

/* DTS register offsets */
#define DTS_CFGR1_OFFSET	0x0
#define DTS_T0VALR1_OFFSET	0x8
#define DTS_RAMPVALR_OFFSET	0X10
#define DTS_ITR1_OFFSET		0x14
#define DTS_DR_OFFSET		0x1C
#define DTS_SR_OFFSET		0x20
#define DTS_ITENR_OFFSET	0x24
#define DTS_CIFR_OFFSET		0x28

/* DTS_CFGR1 register mask definitions */
#define HSREF_CLK_DIV_MASK	GENMASK(30, 24)
#define TS1_SMP_TIME_MASK	GENMASK(19, 16)
#define TS1_INTRIG_SEL_MASK	GENMASK(11, 8)

/* DTS_T0VALR1 register mask definitions */
#define TS1_T0_MASK		GENMASK(17, 16)
#define TS1_FMT0_MASK		GENMASK(15, 0)

/* DTS_RAMPVALR register mask definitions */
#define TS1_RAMP_COEFF_MASK	GENMASK(15, 0)

/* DTS_ITR1 register mask definitions */
#define TS1_HITTHD_MASK		GENMASK(31, 16)
#define TS1_LITTHD_MASK		GENMASK(15, 0)

/* DTS_DR register mask definitions */
#define TS1_MFREQ_MASK		GENMASK(15, 0)

/* Less significant bit position definitions */
#define TS1_T0_POS		16
#define TS1_SMP_TIME_POS	16
#define TS1_HITTHD_POS		16
#define HSREF_CLK_DIV_POS	24

/* DTS_CFGR1 bit definitions */
#define TS1_EN			BIT(0)
#define TS1_START		BIT(4)
#define REFCLK_SEL		BIT(20)
#define REFCLK_LSE		REFCLK_SEL
#define Q_MEAS_OPT		BIT(21)
#define CALIBRATION_CONTROL	Q_MEAS_OPT

/* DTS_SR bit definitions */
#define TS_RDY			BIT(15)
/* Bit definitions below are common for DTS_SR, DTS_ITENR and DTS_CIFR */
#define HIGH_THRESHOLD		BIT(2)
#define LOW_THRESHOLD		BIT(1)

/* Constants */
#define ADJUST			100
#define ONE_MHZ			1000000
#define POLL_TIMEOUT		5000
#define STARTUP_TIME		40
#define TS1_T0_VAL0		30
#define TS1_T0_VAL1		130
#define NO_HW_TRIG		0

/* The Thermal Framework expects millidegrees */
#define mcelsius(temp)		((temp) * 1000)

/* The Sensor expects oC degrees */
#define celsius(temp)		((temp) / 1000)

struct stm_thermal_sensor {
	struct device *dev;
	struct thermal_zone_device *th_dev;
	enum thermal_device_mode mode;
	struct clk *clk;
	int high_temp;
	int low_temp;
	int temp_critical;
	int temp_passive;
	unsigned int low_temp_enabled;
	int num_trips;
	int irq;
	unsigned int irq_enabled;
	void __iomem *base;
	int t0, fmt0, ramp_coeff;
};

static irqreturn_t stm_thermal_alarm_irq(int irq, void *sdata)
{
	struct stm_thermal_sensor *sensor = sdata;

	disable_irq_nosync(irq);
	sensor->irq_enabled = false;

	return IRQ_WAKE_THREAD;
}

static irqreturn_t stm_thermal_alarm_irq_thread(int irq, void *sdata)
{
	u32 value;
	struct stm_thermal_sensor *sensor = sdata;

	/* read IT reason in SR and clear flags */
	value = readl_relaxed(sensor->base + DTS_SR_OFFSET);

	if ((value & LOW_THRESHOLD) == LOW_THRESHOLD)
		writel_relaxed(LOW_THRESHOLD, sensor->base + DTS_CIFR_OFFSET);

	if ((value & HIGH_THRESHOLD) == HIGH_THRESHOLD)
		writel_relaxed(HIGH_THRESHOLD, sensor->base + DTS_CIFR_OFFSET);

	thermal_zone_device_update(sensor->th_dev, THERMAL_EVENT_UNSPECIFIED);

	return IRQ_HANDLED;
}

static int stm_sensor_power_on(struct stm_thermal_sensor *sensor)
{
	int ret;
	u32 value;

	/* Enable sensor */
	value = readl_relaxed(sensor->base + DTS_CFGR1_OFFSET);
	value |= TS1_EN;
	writel_relaxed(value, sensor->base + DTS_CFGR1_OFFSET);

	/*
	 * The DTS block can be enabled by setting TSx_EN bit in
	 * DTS_CFGRx register. It requires a startup time of
	 * 40μs. Use 5 ms as arbitrary timeout.
	 */
	ret = readl_poll_timeout(sensor->base + DTS_SR_OFFSET,
				 value, (value & TS_RDY),
				 STARTUP_TIME, POLL_TIMEOUT);
	if (ret)
		return ret;

	/* Start continuous measuring */
	value = readl_relaxed(sensor->base +
			      DTS_CFGR1_OFFSET);
	value |= TS1_START;
	writel_relaxed(value, sensor->base +
		       DTS_CFGR1_OFFSET);

	return 0;
}

static int stm_sensor_power_off(struct stm_thermal_sensor *sensor)
{
	u32 value;

	/* Stop measuring */
	value = readl_relaxed(sensor->base + DTS_CFGR1_OFFSET);
	value &= ~TS1_START;
	writel_relaxed(value, sensor->base + DTS_CFGR1_OFFSET);

	/* Ensure stop is taken into account */
	usleep_range(STARTUP_TIME, POLL_TIMEOUT);

	/* Disable sensor */
	value = readl_relaxed(sensor->base + DTS_CFGR1_OFFSET);
	value &= ~TS1_EN;
	writel_relaxed(value, sensor->base + DTS_CFGR1_OFFSET);

	/* Ensure disable is taken into account */
	return readl_poll_timeout(sensor->base + DTS_SR_OFFSET, value,
				  !(value & TS_RDY),
				  STARTUP_TIME, POLL_TIMEOUT);
}

static int stm_thermal_calibration(struct stm_thermal_sensor *sensor)
{
	u32 value, clk_freq;
	u32 prescaler;

	/* Figure out prescaler value for PCLK during calibration */
	clk_freq = clk_get_rate(sensor->clk);
	if (!clk_freq)
		return -EINVAL;

	prescaler = 0;
	clk_freq /= ONE_MHZ;
	if (clk_freq) {
		while (prescaler <= clk_freq)
			prescaler++;
	}

	value = readl_relaxed(sensor->base + DTS_CFGR1_OFFSET);

	/* Clear prescaler */
	value &= ~HSREF_CLK_DIV_MASK;

	/* Set prescaler. pclk_freq/prescaler < 1MHz */
	value |= (prescaler << HSREF_CLK_DIV_POS);

	/* Select PCLK as reference clock */
	value &= ~REFCLK_SEL;

	/* Set maximal sampling time for better precision */
	value |= TS1_SMP_TIME_MASK;

	/* Measure with calibration */
	value &= ~CALIBRATION_CONTROL;

	/* select trigger */
	value &= ~TS1_INTRIG_SEL_MASK;
	value |= NO_HW_TRIG;

	writel_relaxed(value, sensor->base + DTS_CFGR1_OFFSET);

	return 0;
}

/* Fill in DTS structure with factory sensor values */
static int stm_thermal_read_factory_settings(struct stm_thermal_sensor *sensor)
{
	/* Retrieve engineering calibration temperature */
	sensor->t0 = readl_relaxed(sensor->base + DTS_T0VALR1_OFFSET) &
					TS1_T0_MASK;
	if (!sensor->t0)
		sensor->t0 = TS1_T0_VAL0;
	else
		sensor->t0 = TS1_T0_VAL1;

	/* Retrieve fmt0 and put it on Hz */
	sensor->fmt0 = ADJUST * (readl_relaxed(sensor->base +
				 DTS_T0VALR1_OFFSET) & TS1_FMT0_MASK);

	/* Retrieve ramp coefficient */
	sensor->ramp_coeff = readl_relaxed(sensor->base + DTS_RAMPVALR_OFFSET) &
					   TS1_RAMP_COEFF_MASK;

	if (!sensor->fmt0 || !sensor->ramp_coeff) {
		dev_err(sensor->dev, "%s: wrong setting\n", __func__);
		return -EINVAL;
	}

	dev_dbg(sensor->dev, "%s: T0 = %doC, FMT0 = %dHz, RAMP_COEFF = %dHz/oC",
		__func__, sensor->t0, sensor->fmt0, sensor->ramp_coeff);

	return 0;
}

static int stm_thermal_calculate_threshold(struct stm_thermal_sensor *sensor,
					   int temp, u32 *th)
{
	int freqM;
	u32 sampling_time;

	/* Retrieve the number of periods to sample */
	sampling_time = (readl_relaxed(sensor->base + DTS_CFGR1_OFFSET) &
			TS1_SMP_TIME_MASK) >> TS1_SMP_TIME_POS;

	/* Figure out the CLK_PTAT frequency for a given temperature */
	freqM = ((temp - sensor->t0) * sensor->ramp_coeff)
		 + sensor->fmt0;

	dev_dbg(sensor->dev, "%s: freqM for threshold = %d Hz",
		__func__, freqM);

	/* Figure out the threshold sample number */
	*th = clk_get_rate(sensor->clk);
	if (!*th)
		return -EINVAL;

	*th = *th / freqM;

	*th *= sampling_time;

	return 0;
}

static int stm_thermal_set_threshold(struct stm_thermal_sensor *sensor)
{
	u32 value, th;
	int ret;

	value = readl_relaxed(sensor->base + DTS_ITR1_OFFSET);

	/* Erase threshold content */
	value &= ~(TS1_LITTHD_MASK | TS1_HITTHD_MASK);

	/* Retrieve the sample threshold number th for a given temperature */
	ret = stm_thermal_calculate_threshold(sensor, sensor->high_temp, &th);
	if (ret)
		return ret;

	value |= th & TS1_LITTHD_MASK;

	if (sensor->low_temp_enabled) {
		/* Retrieve the sample threshold */
		ret = stm_thermal_calculate_threshold(sensor, sensor->low_temp,
						      &th);
		if (ret)
			return ret;

		value |= (TS1_HITTHD_MASK  & (th << TS1_HITTHD_POS));
	}

	/* Write value on the Low interrupt threshold */
	writel_relaxed(value, sensor->base + DTS_ITR1_OFFSET);

	return 0;
}

/* Disable temperature interrupt */
static int stm_disable_irq(struct stm_thermal_sensor *sensor)
{
	u32 value;

	/* Disable IT generation for low and high thresholds */
	value = readl_relaxed(sensor->base + DTS_ITENR_OFFSET);
	writel_relaxed(value & ~(LOW_THRESHOLD | HIGH_THRESHOLD),
		       sensor->base + DTS_ITENR_OFFSET);

	dev_dbg(sensor->dev, "%s: IT disabled on sensor side", __func__);

	return 0;
}

/* Enable temperature interrupt */
static int stm_enable_irq(struct stm_thermal_sensor *sensor)
{
	u32 value;

	/*
	 * Code below enables High temperature threshold using a low threshold
	 * sampling value
	 */

	/* Make sure LOW_THRESHOLD IT is clear before enabling */
	writel_relaxed(LOW_THRESHOLD, sensor->base + DTS_CIFR_OFFSET);

	/* Enable IT generation for low threshold */
	value = readl_relaxed(sensor->base + DTS_ITENR_OFFSET);
	value |= LOW_THRESHOLD;

	/* Enable the low temperature threshold if needed */
	if (sensor->low_temp_enabled) {
		/* Make sure HIGH_THRESHOLD IT is clear before enabling */
		writel_relaxed(HIGH_THRESHOLD, sensor->base + DTS_CIFR_OFFSET);

		/* Enable IT generation for high threshold */
		value |= HIGH_THRESHOLD;
	}

	/* Enable thresholds */
	writel_relaxed(value, sensor->base + DTS_ITENR_OFFSET);

	dev_dbg(sensor->dev, "%s: IT enabled on sensor side", __func__);

	return 0;
}

static int stm_thermal_update_threshold(struct stm_thermal_sensor *sensor)
{
	int ret;

	sensor->mode = THERMAL_DEVICE_DISABLED;

	ret = stm_sensor_power_off(sensor);
	if (ret)
		return ret;

	ret = stm_disable_irq(sensor);
	if (ret)
		return ret;

	ret = stm_thermal_set_threshold(sensor);
	if (ret)
		return ret;

	ret = stm_enable_irq(sensor);
	if (ret)
		return ret;

	ret = stm_sensor_power_on(sensor);
	if (ret)
		return ret;

	sensor->mode = THERMAL_DEVICE_ENABLED;

	return 0;
}

/* Callback to get temperature from HW */
static int stm_thermal_get_temp(void *data, int *temp)
{
	struct stm_thermal_sensor *sensor = data;
	u32 sampling_time;
	int freqM, ret;

	if (sensor->mode != THERMAL_DEVICE_ENABLED)
		return -EAGAIN;

	/* Retrieve the number of samples */
	ret = readl_poll_timeout(sensor->base + DTS_DR_OFFSET, freqM,
				 (freqM & TS1_MFREQ_MASK), STARTUP_TIME,
				 POLL_TIMEOUT);

	if (ret)
		return ret;

	if (!freqM)
		return -ENODATA;

	/* Retrieve the number of periods sampled */
	sampling_time = (readl_relaxed(sensor->base + DTS_CFGR1_OFFSET) &
			TS1_SMP_TIME_MASK) >> TS1_SMP_TIME_POS;

	/* Figure out the number of samples per period */
	freqM /= sampling_time;

	/* Figure out the CLK_PTAT frequency */
	freqM = clk_get_rate(sensor->clk) / freqM;
	if (!freqM)
		return -EINVAL;

	dev_dbg(sensor->dev, "%s: freqM=%d\n", __func__, freqM);

	/* Figure out the temperature in mili celsius */
	*temp = mcelsius(sensor->t0 + ((freqM - sensor->fmt0) /
			 sensor->ramp_coeff));

	dev_dbg(sensor->dev, "%s: temperature = %d millicelsius",
		__func__, *temp);

	/* Update thresholds */
	if (sensor->num_trips > 1) {
		/* Update alarm threshold value to next higher trip point */
		if (sensor->high_temp == sensor->temp_passive &&
		    celsius(*temp) >= sensor->temp_passive) {
			sensor->high_temp = sensor->temp_critical;
			sensor->low_temp = sensor->temp_passive;
			sensor->low_temp_enabled = true;
			ret = stm_thermal_update_threshold(sensor);
			if (ret)
				return ret;
		}

		if (sensor->high_temp == sensor->temp_critical &&
		    celsius(*temp) < sensor->temp_passive) {
			sensor->high_temp = sensor->temp_passive;
			sensor->low_temp_enabled = false;
			ret = stm_thermal_update_threshold(sensor);
			if (ret)
				return ret;
		}

		/*
		 * Re-enable alarm IRQ if temperature below critical
		 * temperature
		 */
		if (!sensor->irq_enabled &&
		    (celsius(*temp) < sensor->temp_critical)) {
			sensor->irq_enabled = true;
			enable_irq(sensor->irq);
		}
	}

	return 0;
}

/* Registers DTS irq to be visible by GIC */
static int stm_register_irq(struct stm_thermal_sensor *sensor)
{
	struct device *dev = sensor->dev;
	struct platform_device *pdev = to_platform_device(dev);
	int ret;

	sensor->irq = platform_get_irq(pdev, 0);
	if (sensor->irq < 0) {
		dev_err(dev, "%s: Unable to find IRQ\n", __func__);
		return sensor->irq;
	}

	ret = devm_request_threaded_irq(dev, sensor->irq,
					stm_thermal_alarm_irq,
					stm_thermal_alarm_irq_thread,
					IRQF_ONESHOT,
					dev->driver->name, sensor);
	if (ret) {
		dev_err(dev, "%s: Failed to register IRQ %d\n", __func__,
			sensor->irq);
		return ret;
	}

	sensor->irq_enabled = true;

	dev_dbg(dev, "%s: thermal IRQ registered", __func__);

	return 0;
}

static int stm_thermal_sensor_off(struct stm_thermal_sensor *sensor)
{
	int ret;

	ret = stm_sensor_power_off(sensor);
	if (ret)
		return ret;

	clk_disable_unprepare(sensor->clk);

	return 0;
}

static int stm_thermal_prepare(struct stm_thermal_sensor *sensor)
{
	int ret;
	struct device *dev = sensor->dev;

	ret = clk_prepare_enable(sensor->clk);
	if (ret)
		return ret;

	ret = stm_thermal_read_factory_settings(sensor);
	if (ret)
		goto thermal_unprepare;

	ret = stm_thermal_calibration(sensor);
	if (ret)
		goto thermal_unprepare;

	/* Set threshold(s) for IRQ */
	ret = stm_thermal_set_threshold(sensor);
	if (ret)
		goto thermal_unprepare;

	ret = stm_enable_irq(sensor);
	if (ret)
		goto thermal_unprepare;

	ret = stm_sensor_power_on(sensor);
	if (ret) {
		dev_err(dev, "%s: failed to power on sensor\n", __func__);
		goto irq_disable;
	}

	return 0;

irq_disable:
	stm_disable_irq(sensor);

thermal_unprepare:
	clk_disable_unprepare(sensor->clk);

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int stm_thermal_suspend(struct device *dev)
{
	int ret;
	struct stm_thermal_sensor *sensor = dev_get_drvdata(dev);

	ret = stm_thermal_sensor_off(sensor);
	if (ret)
		return ret;

	sensor->mode = THERMAL_DEVICE_DISABLED;

	return 0;
}

static int stm_thermal_resume(struct device *dev)
{
	int ret;
	struct stm_thermal_sensor *sensor = dev_get_drvdata(dev);

	ret = stm_thermal_prepare(sensor);
	if (ret)
		return ret;

	sensor->mode = THERMAL_DEVICE_ENABLED;

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

SIMPLE_DEV_PM_OPS(stm_thermal_pm_ops, stm_thermal_suspend, stm_thermal_resume);

static const struct thermal_zone_of_device_ops stm_tz_ops = {
	.get_temp	= stm_thermal_get_temp,
};

static const struct of_device_id stm_thermal_of_match[] = {
		{ .compatible = "st,stm32-thermal"},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, stm_thermal_of_match);

static int stm_thermal_probe(struct platform_device *pdev)
{
	struct stm_thermal_sensor *sensor;
	struct resource *res;
	const struct thermal_trip *trip;
	void __iomem *base;
	int ret, i;

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "%s: device tree node not found\n",
			__func__);
		return -EINVAL;
	}

	sensor = devm_kzalloc(&pdev->dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	platform_set_drvdata(pdev, sensor);

	sensor->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	/* Populate sensor */
	sensor->base = base;

	sensor->clk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(sensor->clk)) {
		dev_err(&pdev->dev, "%s: failed to fetch PCLK clock\n",
			__func__);
		return PTR_ERR(sensor->clk);
	}

	/* Register IRQ into GIC */
	ret = stm_register_irq(sensor);
	if (ret)
		return ret;

	sensor->th_dev = devm_thermal_zone_of_sensor_register(&pdev->dev, 0,
							      sensor,
							      &stm_tz_ops);

	if (IS_ERR(sensor->th_dev)) {
		dev_err(&pdev->dev, "%s: thermal zone sensor registering KO\n",
			__func__);
		ret = PTR_ERR(sensor->th_dev);
		return ret;
	}

	if (!sensor->th_dev->ops->get_crit_temp) {
		/* Critical point must be provided */
		ret = -EINVAL;
		goto err_tz;
	}

	ret = sensor->th_dev->ops->get_crit_temp(sensor->th_dev,
			&sensor->temp_critical);
	if (ret) {
		dev_err(&pdev->dev,
			"Not able to read critical_temp: %d\n", ret);
		goto err_tz;
	}

	sensor->temp_critical = celsius(sensor->temp_critical);

	/* Set thresholds for IRQ */
	sensor->high_temp = sensor->temp_critical;

	trip = of_thermal_get_trip_points(sensor->th_dev);
	sensor->num_trips = of_thermal_get_ntrips(sensor->th_dev);

	/* Find out passive temperature if it exists */
	for (i = (sensor->num_trips - 1); i >= 0;  i--) {
		if (trip[i].type == THERMAL_TRIP_PASSIVE) {
			sensor->temp_passive = celsius(trip[i].temperature);
			/* Update high temperature threshold */
			sensor->high_temp = sensor->temp_passive;
			}
	}

	/*
	 * Ensure low_temp_enabled flag is disabled.
	 * By disabling low_temp_enabled, low threshold IT will not be
	 * configured neither enabled because it is not needed as high
	 * threshold is set on the lowest temperature trip point after
	 * probe.
	 */
	sensor->low_temp_enabled = false;

	/* Configure and enable HW sensor */
	ret = stm_thermal_prepare(sensor);
	if (ret) {
		dev_err(&pdev->dev,
			"Not able to enable sensor: %d\n", ret);
		goto err_tz;
	}

	/*
	 * Thermal_zone doesn't enable hwmon as default,
	 * enable it here
	 */
	sensor->th_dev->tzp->no_hwmon = false;
	ret = thermal_add_hwmon_sysfs(sensor->th_dev);
	if (ret)
		goto err_tz;

	sensor->mode = THERMAL_DEVICE_ENABLED;

	dev_info(&pdev->dev, "%s: Driver initialized successfully\n",
		 __func__);

	return 0;

err_tz:
	thermal_zone_of_sensor_unregister(&pdev->dev, sensor->th_dev);
	return ret;
}

static int stm_thermal_remove(struct platform_device *pdev)
{
	struct stm_thermal_sensor *sensor = platform_get_drvdata(pdev);

	stm_thermal_sensor_off(sensor);
	thermal_remove_hwmon_sysfs(sensor->th_dev);
	thermal_zone_of_sensor_unregister(&pdev->dev, sensor->th_dev);

	return 0;
}

static struct platform_driver stm_thermal_driver = {
	.driver = {
		.name	= "stm_thermal",
		.pm     = &stm_thermal_pm_ops,
		.of_match_table = stm_thermal_of_match,
	},
	.probe		= stm_thermal_probe,
	.remove		= stm_thermal_remove,
};
module_platform_driver(stm_thermal_driver);

MODULE_DESCRIPTION("STMicroelectronics STM32 Thermal Sensor Driver");
MODULE_AUTHOR("David Hernandez Sanchez <david.hernandezsanchez@st.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:stm_thermal");
