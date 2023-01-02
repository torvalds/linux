// SPDX-License-Identifier: GPL-2.0-only
/*
 * DRV2665 haptics driver family
 *
 * Author: Dan Murphy <dmurphy@ti.com>
 *
 * Copyright: (C) 2015 Texas Instruments, Inc.
 */

#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>

/* Contol registers */
#define DRV2665_STATUS	0x00
#define DRV2665_CTRL_1	0x01
#define DRV2665_CTRL_2	0x02
#define DRV2665_FIFO	0x0b

/* Status Register */
#define DRV2665_FIFO_FULL		BIT(0)
#define DRV2665_FIFO_EMPTY		BIT(1)

/* Control 1 Register */
#define DRV2665_25_VPP_GAIN		0x00
#define DRV2665_50_VPP_GAIN		0x01
#define DRV2665_75_VPP_GAIN		0x02
#define DRV2665_100_VPP_GAIN		0x03
#define DRV2665_DIGITAL_IN		0xfc
#define DRV2665_ANALOG_IN		BIT(2)

/* Control 2 Register */
#define DRV2665_BOOST_EN		BIT(1)
#define DRV2665_STANDBY			BIT(6)
#define DRV2665_DEV_RST			BIT(7)
#define DRV2665_5_MS_IDLE_TOUT		0x00
#define DRV2665_10_MS_IDLE_TOUT		0x04
#define DRV2665_15_MS_IDLE_TOUT		0x08
#define DRV2665_20_MS_IDLE_TOUT		0x0c

/**
 * struct drv2665_data -
 * @input_dev: Pointer to the input device
 * @client: Pointer to the I2C client
 * @regmap: Register map of the device
 * @work: Work item used to off load the enable/disable of the vibration
 * @regulator: Pointer to the regulator for the IC
 */
struct drv2665_data {
	struct input_dev *input_dev;
	struct i2c_client *client;
	struct regmap *regmap;
	struct work_struct work;
	struct regulator *regulator;
};

/* 8kHz Sine wave to stream to the FIFO */
static const u8 drv2665_sine_wave_form[] = {
	0x00, 0x10, 0x20, 0x2e, 0x3c, 0x48, 0x53, 0x5b, 0x61, 0x65, 0x66,
	0x65, 0x61, 0x5b, 0x53, 0x48, 0x3c, 0x2e, 0x20, 0x10,
	0x00, 0xf0, 0xe0, 0xd2, 0xc4, 0xb8, 0xad, 0xa5, 0x9f, 0x9b, 0x9a,
	0x9b, 0x9f, 0xa5, 0xad, 0xb8, 0xc4, 0xd2, 0xe0, 0xf0, 0x00,
};

static const struct reg_default drv2665_reg_defs[] = {
	{ DRV2665_STATUS, 0x02 },
	{ DRV2665_CTRL_1, 0x28 },
	{ DRV2665_CTRL_2, 0x40 },
	{ DRV2665_FIFO, 0x00 },
};

static void drv2665_worker(struct work_struct *work)
{
	struct drv2665_data *haptics =
				container_of(work, struct drv2665_data, work);
	unsigned int read_buf;
	int error;

	error = regmap_read(haptics->regmap, DRV2665_STATUS, &read_buf);
	if (error) {
		dev_err(&haptics->client->dev,
			"Failed to read status: %d\n", error);
		return;
	}

	if (read_buf & DRV2665_FIFO_EMPTY) {
		error = regmap_bulk_write(haptics->regmap,
					  DRV2665_FIFO,
					  drv2665_sine_wave_form,
					  ARRAY_SIZE(drv2665_sine_wave_form));
		if (error) {
			dev_err(&haptics->client->dev,
				"Failed to write FIFO: %d\n", error);
			return;
		}
	}
}

static int drv2665_haptics_play(struct input_dev *input, void *data,
				struct ff_effect *effect)
{
	struct drv2665_data *haptics = input_get_drvdata(input);

	schedule_work(&haptics->work);

	return 0;
}

static void drv2665_close(struct input_dev *input)
{
	struct drv2665_data *haptics = input_get_drvdata(input);
	int error;

	cancel_work_sync(&haptics->work);

	error = regmap_update_bits(haptics->regmap, DRV2665_CTRL_2,
				   DRV2665_STANDBY, DRV2665_STANDBY);
	if (error)
		dev_err(&haptics->client->dev,
			"Failed to enter standby mode: %d\n", error);
}

static const struct reg_sequence drv2665_init_regs[] = {
	{ DRV2665_CTRL_2, 0 | DRV2665_10_MS_IDLE_TOUT },
	{ DRV2665_CTRL_1, DRV2665_25_VPP_GAIN },
};

static int drv2665_init(struct drv2665_data *haptics)
{
	int error;

	error = regmap_register_patch(haptics->regmap,
				      drv2665_init_regs,
				      ARRAY_SIZE(drv2665_init_regs));
	if (error) {
		dev_err(&haptics->client->dev,
			"Failed to write init registers: %d\n",
			error);
		return error;
	}

	return 0;
}

static const struct regmap_config drv2665_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = DRV2665_FIFO,
	.reg_defaults = drv2665_reg_defs,
	.num_reg_defaults = ARRAY_SIZE(drv2665_reg_defs),
	.cache_type = REGCACHE_NONE,
};

static int drv2665_probe(struct i2c_client *client)
{
	struct drv2665_data *haptics;
	int error;

	haptics = devm_kzalloc(&client->dev, sizeof(*haptics), GFP_KERNEL);
	if (!haptics)
		return -ENOMEM;

	haptics->regulator = devm_regulator_get(&client->dev, "vbat");
	if (IS_ERR(haptics->regulator)) {
		error = PTR_ERR(haptics->regulator);
		dev_err(&client->dev,
			"unable to get regulator, error: %d\n", error);
		return error;
	}

	haptics->input_dev = devm_input_allocate_device(&client->dev);
	if (!haptics->input_dev) {
		dev_err(&client->dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}

	haptics->input_dev->name = "drv2665:haptics";
	haptics->input_dev->dev.parent = client->dev.parent;
	haptics->input_dev->close = drv2665_close;
	input_set_drvdata(haptics->input_dev, haptics);
	input_set_capability(haptics->input_dev, EV_FF, FF_RUMBLE);

	error = input_ff_create_memless(haptics->input_dev, NULL,
					drv2665_haptics_play);
	if (error) {
		dev_err(&client->dev, "input_ff_create() failed: %d\n",
			error);
		return error;
	}

	INIT_WORK(&haptics->work, drv2665_worker);

	haptics->client = client;
	i2c_set_clientdata(client, haptics);

	haptics->regmap = devm_regmap_init_i2c(client, &drv2665_regmap_config);
	if (IS_ERR(haptics->regmap)) {
		error = PTR_ERR(haptics->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			error);
		return error;
	}

	error = drv2665_init(haptics);
	if (error) {
		dev_err(&client->dev, "Device init failed: %d\n", error);
		return error;
	}

	error = input_register_device(haptics->input_dev);
	if (error) {
		dev_err(&client->dev, "couldn't register input device: %d\n",
			error);
		return error;
	}

	return 0;
}

static int drv2665_suspend(struct device *dev)
{
	struct drv2665_data *haptics = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&haptics->input_dev->mutex);

	if (input_device_enabled(haptics->input_dev)) {
		ret = regmap_update_bits(haptics->regmap, DRV2665_CTRL_2,
					 DRV2665_STANDBY, DRV2665_STANDBY);
		if (ret) {
			dev_err(dev, "Failed to set standby mode\n");
			regulator_disable(haptics->regulator);
			goto out;
		}

		ret = regulator_disable(haptics->regulator);
		if (ret) {
			dev_err(dev, "Failed to disable regulator\n");
			regmap_update_bits(haptics->regmap,
					   DRV2665_CTRL_2,
					   DRV2665_STANDBY, 0);
		}
	}
out:
	mutex_unlock(&haptics->input_dev->mutex);
	return ret;
}

static int drv2665_resume(struct device *dev)
{
	struct drv2665_data *haptics = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&haptics->input_dev->mutex);

	if (input_device_enabled(haptics->input_dev)) {
		ret = regulator_enable(haptics->regulator);
		if (ret) {
			dev_err(dev, "Failed to enable regulator\n");
			goto out;
		}

		ret = regmap_update_bits(haptics->regmap, DRV2665_CTRL_2,
					 DRV2665_STANDBY, 0);
		if (ret) {
			dev_err(dev, "Failed to unset standby mode\n");
			regulator_disable(haptics->regulator);
			goto out;
		}

	}

out:
	mutex_unlock(&haptics->input_dev->mutex);
	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(drv2665_pm_ops, drv2665_suspend, drv2665_resume);

static const struct i2c_device_id drv2665_id[] = {
	{ "drv2665", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, drv2665_id);

#ifdef CONFIG_OF
static const struct of_device_id drv2665_of_match[] = {
	{ .compatible = "ti,drv2665", },
	{ }
};
MODULE_DEVICE_TABLE(of, drv2665_of_match);
#endif

static struct i2c_driver drv2665_driver = {
	.probe_new	= drv2665_probe,
	.driver		= {
		.name	= "drv2665-haptics",
		.of_match_table = of_match_ptr(drv2665_of_match),
		.pm	= pm_sleep_ptr(&drv2665_pm_ops),
	},
	.id_table = drv2665_id,
};
module_i2c_driver(drv2665_driver);

MODULE_DESCRIPTION("TI DRV2665 haptics driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
