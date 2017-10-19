/*
 * Hisilicon thermal sensor driver
 *
 * Copyright (c) 2014-2015 Hisilicon Limited.
 * Copyright (c) 2014-2015 Linaro Limited.
 *
 * Xinwei Kong <kong.kongxinwei@hisilicon.com>
 * Leo Yan <leo.yan@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include "thermal_core.h"

#define TEMP0_LAG			(0x0)
#define TEMP0_TH			(0x4)
#define TEMP0_RST_TH			(0x8)
#define TEMP0_CFG			(0xC)
#define TEMP0_CFG_SS_MSK		(0xF000)
#define TEMP0_CFG_HDAK_MSK		(0x30)
#define TEMP0_EN			(0x10)
#define TEMP0_INT_EN			(0x14)
#define TEMP0_INT_CLR			(0x18)
#define TEMP0_RST_MSK			(0x1C)
#define TEMP0_VALUE			(0x28)

#define HISI_TEMP_BASE			(-60000)
#define HISI_TEMP_RESET			(100000)
#define HISI_TEMP_STEP			(784)
#define HISI_TEMP_LAG			(3500)

#define HISI_MAX_SENSORS		4
#define HISI_DEFAULT_SENSOR		2

struct hisi_thermal_sensor {
	struct hisi_thermal_data *thermal;
	struct thermal_zone_device *tzd;

	long sensor_temp;
	uint32_t id;
	uint32_t thres_temp;
};

struct hisi_thermal_data {
	struct mutex thermal_lock;    /* protects register data */
	struct platform_device *pdev;
	struct clk *clk;
	struct hisi_thermal_sensor sensors;
	int irq;
	void __iomem *regs;
};

/*
 * The temperature computation on the tsensor is as follow:
 *	Unit: millidegree Celsius
 *	Step: 255/200 (0.7843)
 *	Temperature base: -60°C
 *
 * The register is programmed in temperature steps, every step is 784
 * millidegree and begins at -60 000 m°C
 *
 * The temperature from the steps:
 *
 *	Temp = TempBase + (steps x 784)
 *
 * and the steps from the temperature:
 *
 *	steps = (Temp - TempBase) / 784
 *
 */
static inline int hisi_thermal_step_to_temp(int step)
{
	return HISI_TEMP_BASE + (step * HISI_TEMP_STEP);
}

static inline long hisi_thermal_temp_to_step(long temp)
{
	return (temp - HISI_TEMP_BASE) / HISI_TEMP_STEP;
}

static inline long hisi_thermal_round_temp(int temp)
{
	return hisi_thermal_step_to_temp(
		hisi_thermal_temp_to_step(temp));
}

/*
 * The lag register contains 5 bits encoding the temperature in steps.
 *
 * Each time the temperature crosses the threshold boundary, an
 * interrupt is raised. It could be when the temperature is going
 * above the threshold or below. However, if the temperature is
 * fluctuating around this value due to the load, we can receive
 * several interrupts which may not desired.
 *
 * We can setup a temperature representing the delta between the
 * threshold and the current temperature when the temperature is
 * decreasing.
 *
 * For instance: the lag register is 5°C, the threshold is 65°C, when
 * the temperature reaches 65°C an interrupt is raised and when the
 * temperature decrease to 65°C - 5°C another interrupt is raised.
 *
 * A very short lag can lead to an interrupt storm, a long lag
 * increase the latency to react to the temperature changes.  In our
 * case, that is not really a problem as we are polling the
 * temperature.
 *
 * [0:4] : lag register
 *
 * The temperature is coded in steps, cf. HISI_TEMP_STEP.
 *
 * Min : 0x00 :  0.0 °C
 * Max : 0x1F : 24.3 °C
 *
 * The 'value' parameter is in milliCelsius.
 */
static inline void hisi_thermal_set_lag(void __iomem *addr, int value)
{
	writel((value / HISI_TEMP_STEP) & 0x1F, addr + TEMP0_LAG);
}

static inline void hisi_thermal_alarm_clear(void __iomem *addr, int value)
{
	writel(value, addr + TEMP0_INT_CLR);
}

static inline void hisi_thermal_alarm_enable(void __iomem *addr, int value)
{
	writel(value, addr + TEMP0_INT_EN);
}

static inline void hisi_thermal_alarm_set(void __iomem *addr, int temp)
{
	writel(hisi_thermal_temp_to_step(temp) | 0x0FFFFFF00, addr + TEMP0_TH);
}

static inline void hisi_thermal_reset_set(void __iomem *addr, int temp)
{
	writel(hisi_thermal_temp_to_step(temp), addr + TEMP0_RST_TH);
}

static inline void hisi_thermal_reset_enable(void __iomem *addr, int value)
{
	writel(value, addr + TEMP0_RST_MSK);
}

static inline void hisi_thermal_enable(void __iomem *addr, int value)
{
	writel(value, addr + TEMP0_EN);
}

static inline int hisi_thermal_get_temperature(void __iomem *addr)
{
	return hisi_thermal_step_to_temp(readl(addr + TEMP0_VALUE));
}

/*
 * Temperature configuration register - Sensor selection
 *
 * Bits [19:12]
 *
 * 0x0: local sensor (default)
 * 0x1: remote sensor 1 (ACPU cluster 1)
 * 0x2: remote sensor 2 (ACPU cluster 0)
 * 0x3: remote sensor 3 (G3D)
 */
static inline void hisi_thermal_sensor_select(void __iomem *addr, int sensor)
{
	writel((readl(addr + TEMP0_CFG) & ~TEMP0_CFG_SS_MSK) |
	       (sensor << 12), addr + TEMP0_CFG);
}

/*
 * Temperature configuration register - Hdak conversion polling interval
 *
 * Bits [5:4]
 *
 * 0x0 :   0.768 ms
 * 0x1 :   6.144 ms
 * 0x2 :  49.152 ms
 * 0x3 : 393.216 ms
 */
static inline void hisi_thermal_hdak_set(void __iomem *addr, int value)
{
	writel((readl(addr + TEMP0_CFG) & ~TEMP0_CFG_HDAK_MSK) |
	       (value << 4), addr + TEMP0_CFG);
}

static void hisi_thermal_disable_sensor(struct hisi_thermal_data *data)
{
	mutex_lock(&data->thermal_lock);

	/* disable sensor module */
	hisi_thermal_enable(data->regs, 0);
	hisi_thermal_alarm_enable(data->regs, 0);
	hisi_thermal_reset_enable(data->regs, 0);

	mutex_unlock(&data->thermal_lock);
}

static int hisi_thermal_get_temp(void *_sensor, int *temp)
{
	struct hisi_thermal_sensor *sensor = _sensor;
	struct hisi_thermal_data *data = sensor->thermal;

	*temp = hisi_thermal_get_temperature(data->regs);

	dev_dbg(&data->pdev->dev, "id=%d, temp=%d, thres=%d\n",
		sensor->id, *temp, sensor->thres_temp);

	return 0;
}

static const struct thermal_zone_of_device_ops hisi_of_thermal_ops = {
	.get_temp = hisi_thermal_get_temp,
};

static irqreturn_t hisi_thermal_alarm_irq_thread(int irq, void *dev)
{
	struct hisi_thermal_data *data = dev;
	struct hisi_thermal_sensor *sensor = &data->sensors;
	int temp;

	hisi_thermal_alarm_clear(data->regs, 1);

	temp = hisi_thermal_get_temperature(data->regs);

	if (temp >= sensor->thres_temp) {
		dev_crit(&data->pdev->dev, "THERMAL ALARM: %d > %d\n",
			 temp, sensor->thres_temp);

		thermal_zone_device_update(data->sensors.tzd,
					   THERMAL_EVENT_UNSPECIFIED);

	} else if (temp < sensor->thres_temp) {
		dev_crit(&data->pdev->dev, "THERMAL ALARM stopped: %d < %d\n",
			 temp, sensor->thres_temp);
	}

	return IRQ_HANDLED;
}

static int hisi_thermal_register_sensor(struct platform_device *pdev,
					struct hisi_thermal_data *data,
					struct hisi_thermal_sensor *sensor,
					int index)
{
	int ret, i;
	const struct thermal_trip *trip;

	sensor->id = index;
	sensor->thermal = data;

	sensor->tzd = devm_thermal_zone_of_sensor_register(&pdev->dev,
				sensor->id, sensor, &hisi_of_thermal_ops);
	if (IS_ERR(sensor->tzd)) {
		ret = PTR_ERR(sensor->tzd);
		sensor->tzd = NULL;
		dev_err(&pdev->dev, "failed to register sensor id %d: %d\n",
			sensor->id, ret);
		return ret;
	}

	trip = of_thermal_get_trip_points(sensor->tzd);

	for (i = 0; i < of_thermal_get_ntrips(sensor->tzd); i++) {
		if (trip[i].type == THERMAL_TRIP_PASSIVE) {
			sensor->thres_temp = hisi_thermal_round_temp(trip[i].temperature);
			break;
		}
	}

	return 0;
}

static const struct of_device_id of_hisi_thermal_match[] = {
	{ .compatible = "hisilicon,tsensor" },
	{ /* end */ }
};
MODULE_DEVICE_TABLE(of, of_hisi_thermal_match);

static void hisi_thermal_toggle_sensor(struct hisi_thermal_sensor *sensor,
				       bool on)
{
	struct thermal_zone_device *tzd = sensor->tzd;

	tzd->ops->set_mode(tzd,
		on ? THERMAL_DEVICE_ENABLED : THERMAL_DEVICE_DISABLED);
}

static int hisi_thermal_setup(struct hisi_thermal_data *data)
{
	struct hisi_thermal_sensor *sensor;

	sensor = &data->sensors;

	/* disable module firstly */
	hisi_thermal_reset_enable(data->regs, 0);
	hisi_thermal_enable(data->regs, 0);

	/* select sensor id */
	hisi_thermal_sensor_select(data->regs, sensor->id);

	/* setting the hdak time */
	hisi_thermal_hdak_set(data->regs, 0);

	/* setting lag value between current temp and the threshold */
	hisi_thermal_set_lag(data->regs, HISI_TEMP_LAG);

	/* enable for interrupt */
	hisi_thermal_alarm_set(data->regs, sensor->thres_temp);

	hisi_thermal_reset_set(data->regs, HISI_TEMP_RESET);

	/* enable module */
	hisi_thermal_reset_enable(data->regs, 1);
	hisi_thermal_enable(data->regs, 1);

	hisi_thermal_alarm_clear(data->regs, 0);
	hisi_thermal_alarm_enable(data->regs, 1);

	return 0;
}

static int hisi_thermal_probe(struct platform_device *pdev)
{
	struct hisi_thermal_data *data;
	struct resource *res;
	int ret;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->thermal_lock);
	data->pdev = pdev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->regs)) {
		dev_err(&pdev->dev, "failed to get io address\n");
		return PTR_ERR(data->regs);
	}

	data->irq = platform_get_irq(pdev, 0);
	if (data->irq < 0)
		return data->irq;

	platform_set_drvdata(pdev, data);

	data->clk = devm_clk_get(&pdev->dev, "thermal_clk");
	if (IS_ERR(data->clk)) {
		ret = PTR_ERR(data->clk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"failed to get thermal clk: %d\n", ret);
		return ret;
	}

	/* enable clock for thermal */
	ret = clk_prepare_enable(data->clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable thermal clk: %d\n", ret);
		return ret;
	}

	ret = hisi_thermal_register_sensor(pdev, data,
					   &data->sensors,
					   HISI_DEFAULT_SENSOR);
	if (ret) {
		dev_err(&pdev->dev, "failed to register thermal sensor: %d\n",
			ret);
		return ret;
	}

	ret = hisi_thermal_setup(data);
	if (ret) {
		dev_err(&pdev->dev, "Failed to setup the sensor: %d\n", ret);
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev, data->irq, NULL,
					hisi_thermal_alarm_irq_thread,
					IRQF_ONESHOT, "hisi_thermal", data);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request alarm irq: %d\n", ret);
		return ret;
	}

	hisi_thermal_toggle_sensor(&data->sensors, true);

	return 0;
}

static int hisi_thermal_remove(struct platform_device *pdev)
{
	struct hisi_thermal_data *data = platform_get_drvdata(pdev);
	struct hisi_thermal_sensor *sensor = &data->sensors;

	hisi_thermal_toggle_sensor(sensor, false);
	hisi_thermal_disable_sensor(data);
	clk_disable_unprepare(data->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int hisi_thermal_suspend(struct device *dev)
{
	struct hisi_thermal_data *data = dev_get_drvdata(dev);

	hisi_thermal_disable_sensor(data);

	clk_disable_unprepare(data->clk);

	return 0;
}

static int hisi_thermal_resume(struct device *dev)
{
	struct hisi_thermal_data *data = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(data->clk);
	if (ret)
		return ret;

	hisi_thermal_setup(data);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(hisi_thermal_pm_ops,
			 hisi_thermal_suspend, hisi_thermal_resume);

static struct platform_driver hisi_thermal_driver = {
	.driver = {
		.name		= "hisi_thermal",
		.pm		= &hisi_thermal_pm_ops,
		.of_match_table = of_hisi_thermal_match,
	},
	.probe	= hisi_thermal_probe,
	.remove	= hisi_thermal_remove,
};

module_platform_driver(hisi_thermal_driver);

MODULE_AUTHOR("Xinwei Kong <kong.kongxinwei@hisilicon.com>");
MODULE_AUTHOR("Leo Yan <leo.yan@linaro.org>");
MODULE_DESCRIPTION("Hisilicon thermal driver");
MODULE_LICENSE("GPL v2");
