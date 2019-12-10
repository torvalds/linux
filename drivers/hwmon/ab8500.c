// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson 2010 - 2013
 * Author: Martin Persson <martin.persson@stericsson.com>
 *         Hongbo Zhang <hongbo.zhang@linaro.org>
 *
 * When the AB8500 thermal warning temperature is reached (threshold cannot
 * be changed by SW), an interrupt is set, and if no further action is taken
 * within a certain time frame, kernel_power_off will be called.
 *
 * When AB8500 thermal shutdown temperature is reached a hardware shutdown of
 * the AB8500 will occur.
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/abx500/ab8500-bm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power/ab8500.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/iio/consumer.h>
#include "abx500.h"

#define DEFAULT_POWER_OFF_DELAY	(HZ * 10)
#define THERMAL_VCC		1800
#define PULL_UP_RESISTOR	47000

#define AB8500_SENSOR_AUX1		0
#define AB8500_SENSOR_AUX2		1
#define AB8500_SENSOR_BTEMP_BALL	2
#define AB8500_SENSOR_BAT_CTRL		3
#define NUM_MONITORED_SENSORS		4

struct ab8500_gpadc_cfg {
	const struct abx500_res_to_temp *temp_tbl;
	int tbl_sz;
	int vcc;
	int r_up;
};

struct ab8500_temp {
	struct iio_channel *aux1;
	struct iio_channel *aux2;
	struct ab8500_btemp *btemp;
	struct delayed_work power_off_work;
	struct ab8500_gpadc_cfg cfg;
	struct abx500_temp *abx500_data;
};

/*
 * The hardware connection is like this:
 * VCC----[ R_up ]-----[ NTC ]----GND
 * where R_up is pull-up resistance, and GPADC measures voltage on NTC.
 * and res_to_temp table is strictly sorted by falling resistance values.
 */
static int ab8500_voltage_to_temp(struct ab8500_gpadc_cfg *cfg,
		int v_ntc, int *temp)
{
	int r_ntc, i = 0, tbl_sz = cfg->tbl_sz;
	const struct abx500_res_to_temp *tbl = cfg->temp_tbl;

	if (cfg->vcc < 0 || v_ntc >= cfg->vcc)
		return -EINVAL;

	r_ntc = v_ntc * cfg->r_up / (cfg->vcc - v_ntc);
	if (r_ntc > tbl[0].resist || r_ntc < tbl[tbl_sz - 1].resist)
		return -EINVAL;

	while (!(r_ntc <= tbl[i].resist && r_ntc > tbl[i + 1].resist) &&
			i < tbl_sz - 2)
		i++;

	/* return milli-Celsius */
	*temp = tbl[i].temp * 1000 + ((tbl[i + 1].temp - tbl[i].temp) * 1000 *
		(r_ntc - tbl[i].resist)) / (tbl[i + 1].resist - tbl[i].resist);

	return 0;
}

static int ab8500_read_sensor(struct abx500_temp *data, u8 sensor, int *temp)
{
	int voltage, ret;
	struct ab8500_temp *ab8500_data = data->plat_data;

	if (sensor == AB8500_SENSOR_BTEMP_BALL) {
		*temp = ab8500_btemp_get_temp(ab8500_data->btemp);
	} else if (sensor == AB8500_SENSOR_BAT_CTRL) {
		*temp = ab8500_btemp_get_batctrl_temp(ab8500_data->btemp);
	} else if (sensor == AB8500_SENSOR_AUX1) {
		ret = iio_read_channel_processed(ab8500_data->aux1, &voltage);
		if (ret < 0)
			return ret;
		ret = ab8500_voltage_to_temp(&ab8500_data->cfg, voltage, temp);
		if (ret < 0)
			return ret;
	} else if (sensor == AB8500_SENSOR_AUX2) {
		ret = iio_read_channel_processed(ab8500_data->aux2, &voltage);
		if (ret < 0)
			return ret;
		ret = ab8500_voltage_to_temp(&ab8500_data->cfg, voltage, temp);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void ab8500_thermal_power_off(struct work_struct *work)
{
	struct ab8500_temp *ab8500_data = container_of(work,
				struct ab8500_temp, power_off_work.work);
	struct abx500_temp *abx500_data = ab8500_data->abx500_data;

	dev_warn(&abx500_data->pdev->dev, "Power off due to critical temp\n");

	kernel_power_off();
}

static ssize_t ab8500_show_name(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	return sprintf(buf, "ab8500\n");
}

static ssize_t ab8500_show_label(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	char *label;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int index = attr->index;

	switch (index) {
	case 1:
		label = "ext_adc1";
		break;
	case 2:
		label = "ext_adc2";
		break;
	case 3:
		label = "bat_temp";
		break;
	case 4:
		label = "bat_ctrl";
		break;
	default:
		return -EINVAL;
	}

	return sprintf(buf, "%s\n", label);
}

static int ab8500_temp_irq_handler(int irq, struct abx500_temp *data)
{
	struct ab8500_temp *ab8500_data = data->plat_data;

	dev_warn(&data->pdev->dev, "Power off in %d s\n",
		 DEFAULT_POWER_OFF_DELAY / HZ);

	schedule_delayed_work(&ab8500_data->power_off_work,
		DEFAULT_POWER_OFF_DELAY);
	return 0;
}

int abx500_hwmon_init(struct abx500_temp *data)
{
	struct ab8500_temp *ab8500_data;

	ab8500_data = devm_kzalloc(&data->pdev->dev, sizeof(*ab8500_data),
		GFP_KERNEL);
	if (!ab8500_data)
		return -ENOMEM;

	ab8500_data->btemp = ab8500_btemp_get();
	if (IS_ERR(ab8500_data->btemp))
		return PTR_ERR(ab8500_data->btemp);

	INIT_DELAYED_WORK(&ab8500_data->power_off_work,
			  ab8500_thermal_power_off);

	ab8500_data->cfg.vcc = THERMAL_VCC;
	ab8500_data->cfg.r_up = PULL_UP_RESISTOR;
	ab8500_data->cfg.temp_tbl = ab8500_temp_tbl_a_thermistor;
	ab8500_data->cfg.tbl_sz = ab8500_temp_tbl_a_size;

	data->plat_data = ab8500_data;
	ab8500_data->aux1 = devm_iio_channel_get(&data->pdev->dev, "aux1");
	if (IS_ERR(ab8500_data->aux1)) {
		if (PTR_ERR(ab8500_data->aux1) == -ENODEV)
			return -EPROBE_DEFER;
		dev_err(&data->pdev->dev, "failed to get AUX1 ADC channel\n");
		return PTR_ERR(ab8500_data->aux1);
	}
	ab8500_data->aux2 = devm_iio_channel_get(&data->pdev->dev, "aux2");
	if (IS_ERR(ab8500_data->aux2)) {
		if (PTR_ERR(ab8500_data->aux2) == -ENODEV)
			return -EPROBE_DEFER;
		dev_err(&data->pdev->dev, "failed to get AUX2 ADC channel\n");
		return PTR_ERR(ab8500_data->aux2);
	}

	data->gpadc_addr[0] = AB8500_SENSOR_AUX1;
	data->gpadc_addr[1] = AB8500_SENSOR_AUX2;
	data->gpadc_addr[2] = AB8500_SENSOR_BTEMP_BALL;
	data->gpadc_addr[3] = AB8500_SENSOR_BAT_CTRL;
	data->monitored_sensors = NUM_MONITORED_SENSORS;

	data->ops.read_sensor = ab8500_read_sensor;
	data->ops.irq_handler = ab8500_temp_irq_handler;
	data->ops.show_name = ab8500_show_name;
	data->ops.show_label = ab8500_show_label;
	data->ops.is_visible = NULL;

	return 0;
}
EXPORT_SYMBOL(abx500_hwmon_init);

MODULE_AUTHOR("Hongbo Zhang <hongbo.zhang@linaro.org>");
MODULE_DESCRIPTION("AB8500 temperature driver");
MODULE_LICENSE("GPL");
