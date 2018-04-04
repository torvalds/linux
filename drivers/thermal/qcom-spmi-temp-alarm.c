/*
 * Copyright (c) 2011-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/thermal.h>

#define QPNP_TM_REG_TYPE		0x04
#define QPNP_TM_REG_SUBTYPE		0x05
#define QPNP_TM_REG_STATUS		0x08
#define QPNP_TM_REG_SHUTDOWN_CTRL1	0x40
#define QPNP_TM_REG_ALARM_CTRL		0x46

#define QPNP_TM_TYPE			0x09
#define QPNP_TM_SUBTYPE			0x08

#define STATUS_STAGE_MASK		0x03

#define SHUTDOWN_CTRL1_THRESHOLD_MASK	0x03

#define ALARM_CTRL_FORCE_ENABLE		0x80

/*
 * Trip point values based on threshold control
 * 0 = {105 C, 125 C, 145 C}
 * 1 = {110 C, 130 C, 150 C}
 * 2 = {115 C, 135 C, 155 C}
 * 3 = {120 C, 140 C, 160 C}
*/
#define TEMP_STAGE_STEP			20000	/* Stage step: 20.000 C */
#define TEMP_STAGE_HYSTERESIS		2000

#define TEMP_THRESH_MIN			105000	/* Threshold Min: 105 C */
#define TEMP_THRESH_STEP		5000	/* Threshold step: 5 C */

#define THRESH_MIN			0

/* Temperature in Milli Celsius reported during stage 0 if no ADC is present */
#define DEFAULT_TEMP			37000

struct qpnp_tm_chip {
	struct regmap			*map;
	struct thermal_zone_device	*tz_dev;
	long				temp;
	unsigned int			thresh;
	unsigned int			stage;
	unsigned int			prev_stage;
	unsigned int			base;
	struct iio_channel		*adc;
};

static int qpnp_tm_read(struct qpnp_tm_chip *chip, u16 addr, u8 *data)
{
	unsigned int val;
	int ret;

	ret = regmap_read(chip->map, chip->base + addr, &val);
	if (ret < 0)
		return ret;

	*data = val;
	return 0;
}

static int qpnp_tm_write(struct qpnp_tm_chip *chip, u16 addr, u8 data)
{
	return regmap_write(chip->map, chip->base + addr, data);
}

/*
 * This function updates the internal temp value based on the
 * current thermal stage and threshold as well as the previous stage
 */
static int qpnp_tm_update_temp_no_adc(struct qpnp_tm_chip *chip)
{
	unsigned int stage;
	int ret;
	u8 reg = 0;

	ret = qpnp_tm_read(chip, QPNP_TM_REG_STATUS, &reg);
	if (ret < 0)
		return ret;

	stage = reg & STATUS_STAGE_MASK;

	if (stage > chip->stage) {
		/* increasing stage, use lower bound */
		chip->temp = (stage - 1) * TEMP_STAGE_STEP +
			     chip->thresh * TEMP_THRESH_STEP +
			     TEMP_STAGE_HYSTERESIS + TEMP_THRESH_MIN;
	} else if (stage < chip->stage) {
		/* decreasing stage, use upper bound */
		chip->temp = stage * TEMP_STAGE_STEP +
			     chip->thresh * TEMP_THRESH_STEP -
			     TEMP_STAGE_HYSTERESIS + TEMP_THRESH_MIN;
	}

	chip->stage = stage;

	return 0;
}

static int qpnp_tm_get_temp(void *data, int *temp)
{
	struct qpnp_tm_chip *chip = data;
	int ret, mili_celsius;

	if (!temp)
		return -EINVAL;

	if (!chip->adc) {
		ret = qpnp_tm_update_temp_no_adc(chip);
		if (ret < 0)
			return ret;
	} else {
		ret = iio_read_channel_processed(chip->adc, &mili_celsius);
		if (ret < 0)
			return ret;

		chip->temp = mili_celsius;
	}

	*temp = chip->temp < 0 ? 0 : chip->temp;

	return 0;
}

static const struct thermal_zone_of_device_ops qpnp_tm_sensor_ops = {
	.get_temp = qpnp_tm_get_temp,
};

static irqreturn_t qpnp_tm_isr(int irq, void *data)
{
	struct qpnp_tm_chip *chip = data;

	thermal_zone_device_update(chip->tz_dev, THERMAL_EVENT_UNSPECIFIED);

	return IRQ_HANDLED;
}

/*
 * This function initializes the internal temp value based on only the
 * current thermal stage and threshold. Setup threshold control and
 * disable shutdown override.
 */
static int qpnp_tm_init(struct qpnp_tm_chip *chip)
{
	int ret;
	u8 reg;

	chip->thresh = THRESH_MIN;
	chip->temp = DEFAULT_TEMP;

	ret = qpnp_tm_read(chip, QPNP_TM_REG_STATUS, &reg);
	if (ret < 0)
		return ret;

	chip->stage = reg & STATUS_STAGE_MASK;

	if (chip->stage)
		chip->temp = chip->thresh * TEMP_THRESH_STEP +
			     (chip->stage - 1) * TEMP_STAGE_STEP +
			     TEMP_THRESH_MIN;

	/*
	 * Set threshold and disable software override of stage 2 and 3
	 * shutdowns.
	 */
	reg = chip->thresh & SHUTDOWN_CTRL1_THRESHOLD_MASK;
	ret = qpnp_tm_write(chip, QPNP_TM_REG_SHUTDOWN_CTRL1, reg);
	if (ret < 0)
		return ret;

	/* Enable the thermal alarm PMIC module in always-on mode. */
	reg = ALARM_CTRL_FORCE_ENABLE;
	ret = qpnp_tm_write(chip, QPNP_TM_REG_ALARM_CTRL, reg);

	return ret;
}

static int qpnp_tm_probe(struct platform_device *pdev)
{
	struct qpnp_tm_chip *chip;
	struct device_node *node;
	u8 type, subtype;
	u32 res;
	int ret, irq;

	node = pdev->dev.of_node;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, chip);

	chip->map = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chip->map)
		return -ENXIO;

	ret = of_property_read_u32(node, "reg", &res);
	if (ret < 0)
		return ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	/* ADC based measurements are optional */
	chip->adc = devm_iio_channel_get(&pdev->dev, "thermal");
	if (IS_ERR(chip->adc)) {
		ret = PTR_ERR(chip->adc);
		chip->adc = NULL;
		if (ret == -EPROBE_DEFER)
			return ret;
	}

	chip->base = res;

	ret = qpnp_tm_read(chip, QPNP_TM_REG_TYPE, &type);
	if (ret < 0) {
		dev_err(&pdev->dev, "could not read type\n");
		return ret;
	}

	ret = qpnp_tm_read(chip, QPNP_TM_REG_SUBTYPE, &subtype);
	if (ret < 0) {
		dev_err(&pdev->dev, "could not read subtype\n");
		return ret;
	}

	if (type != QPNP_TM_TYPE || subtype != QPNP_TM_SUBTYPE) {
		dev_err(&pdev->dev, "invalid type 0x%02x or subtype 0x%02x\n",
			type, subtype);
		return -ENODEV;
	}

	ret = qpnp_tm_init(chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "init failed\n");
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL, qpnp_tm_isr,
					IRQF_ONESHOT, node->name, chip);
	if (ret < 0)
		return ret;

	chip->tz_dev = devm_thermal_zone_of_sensor_register(&pdev->dev, 0, chip,
							&qpnp_tm_sensor_ops);
	if (IS_ERR(chip->tz_dev)) {
		dev_err(&pdev->dev, "failed to register sensor\n");
		return PTR_ERR(chip->tz_dev);
	}

	return 0;
}

static const struct of_device_id qpnp_tm_match_table[] = {
	{ .compatible = "qcom,spmi-temp-alarm" },
	{ }
};
MODULE_DEVICE_TABLE(of, qpnp_tm_match_table);

static struct platform_driver qpnp_tm_driver = {
	.driver = {
		.name = "spmi-temp-alarm",
		.of_match_table = qpnp_tm_match_table,
	},
	.probe  = qpnp_tm_probe,
};
module_platform_driver(qpnp_tm_driver);

MODULE_ALIAS("platform:spmi-temp-alarm");
MODULE_DESCRIPTION("QPNP PMIC Temperature Alarm driver");
MODULE_LICENSE("GPL v2");
