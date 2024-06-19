// SPDX-License-Identifier: GPL-2.0-only
/*
 * DRV2667 haptics driver family
 *
 * Author: Dan Murphy <dmurphy@ti.com>
 *
 * Copyright: (C) 2014 Texas Instruments, Inc.
 */

#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>

/* Contol registers */
#define DRV2667_STATUS	0x00
#define DRV2667_CTRL_1	0x01
#define DRV2667_CTRL_2	0x02
/* Waveform sequencer */
#define DRV2667_WV_SEQ_0	0x03
#define DRV2667_WV_SEQ_1	0x04
#define DRV2667_WV_SEQ_2	0x05
#define DRV2667_WV_SEQ_3	0x06
#define DRV2667_WV_SEQ_4	0x07
#define DRV2667_WV_SEQ_5	0x08
#define DRV2667_WV_SEQ_6	0x09
#define DRV2667_WV_SEQ_7	0x0A
#define DRV2667_FIFO		0x0B
#define DRV2667_PAGE		0xFF
#define DRV2667_MAX_REG		DRV2667_PAGE

#define DRV2667_PAGE_0		0x00
#define DRV2667_PAGE_1		0x01
#define DRV2667_PAGE_2		0x02
#define DRV2667_PAGE_3		0x03
#define DRV2667_PAGE_4		0x04
#define DRV2667_PAGE_5		0x05
#define DRV2667_PAGE_6		0x06
#define DRV2667_PAGE_7		0x07
#define DRV2667_PAGE_8		0x08

/* RAM fields */
#define DRV2667_RAM_HDR_SZ	0x0
/* RAM Header addresses */
#define DRV2667_RAM_START_HI	0x01
#define DRV2667_RAM_START_LO	0x02
#define DRV2667_RAM_STOP_HI		0x03
#define DRV2667_RAM_STOP_LO		0x04
#define DRV2667_RAM_REPEAT_CT	0x05
/* RAM data addresses */
#define DRV2667_RAM_AMP		0x06
#define DRV2667_RAM_FREQ	0x07
#define DRV2667_RAM_DURATION	0x08
#define DRV2667_RAM_ENVELOPE	0x09

/* Control 1 Register */
#define DRV2667_25_VPP_GAIN		0x00
#define DRV2667_50_VPP_GAIN		0x01
#define DRV2667_75_VPP_GAIN		0x02
#define DRV2667_100_VPP_GAIN	0x03
#define DRV2667_DIGITAL_IN		0xfc
#define DRV2667_ANALOG_IN		(1 << 2)

/* Control 2 Register */
#define DRV2667_GO			(1 << 0)
#define DRV2667_STANDBY		(1 << 6)
#define DRV2667_DEV_RST		(1 << 7)

/* RAM Envelope settings */
#define DRV2667_NO_ENV			0x00
#define DRV2667_32_MS_ENV		0x01
#define DRV2667_64_MS_ENV		0x02
#define DRV2667_96_MS_ENV		0x03
#define DRV2667_128_MS_ENV		0x04
#define DRV2667_160_MS_ENV		0x05
#define DRV2667_192_MS_ENV		0x06
#define DRV2667_224_MS_ENV		0x07
#define DRV2667_256_MS_ENV		0x08
#define DRV2667_512_MS_ENV		0x09
#define DRV2667_768_MS_ENV		0x0a
#define DRV2667_1024_MS_ENV		0x0b
#define DRV2667_1280_MS_ENV		0x0c
#define DRV2667_1536_MS_ENV		0x0d
#define DRV2667_1792_MS_ENV		0x0e
#define DRV2667_2048_MS_ENV		0x0f

/**
 * struct drv2667_data -
 * @input_dev: Pointer to the input device
 * @client: Pointer to the I2C client
 * @regmap: Register map of the device
 * @work: Work item used to off load the enable/disable of the vibration
 * @regulator: Pointer to the regulator for the IC
 * @page: Page number
 * @magnitude: Magnitude of the vibration event
 * @frequency: Frequency of the vibration event
**/
struct drv2667_data {
	struct input_dev *input_dev;
	struct i2c_client *client;
	struct regmap *regmap;
	struct work_struct work;
	struct regulator *regulator;
	u32 page;
	u32 magnitude;
	u32 frequency;
};

static const struct reg_default drv2667_reg_defs[] = {
	{ DRV2667_STATUS, 0x02 },
	{ DRV2667_CTRL_1, 0x28 },
	{ DRV2667_CTRL_2, 0x40 },
	{ DRV2667_WV_SEQ_0, 0x00 },
	{ DRV2667_WV_SEQ_1, 0x00 },
	{ DRV2667_WV_SEQ_2, 0x00 },
	{ DRV2667_WV_SEQ_3, 0x00 },
	{ DRV2667_WV_SEQ_4, 0x00 },
	{ DRV2667_WV_SEQ_5, 0x00 },
	{ DRV2667_WV_SEQ_6, 0x00 },
	{ DRV2667_WV_SEQ_7, 0x00 },
	{ DRV2667_FIFO, 0x00 },
	{ DRV2667_PAGE, 0x00 },
};

static int drv2667_set_waveform_freq(struct drv2667_data *haptics)
{
	unsigned int read_buf;
	int freq;
	int error;

	/* Per the data sheet:
	 * Sinusoid Frequency (Hz) = 7.8125 x Frequency
	 */
	freq = (haptics->frequency * 1000) / 78125;
	if (freq <= 0) {
		dev_err(&haptics->client->dev,
			"ERROR: Frequency calculated to %i\n", freq);
		return -EINVAL;
	}

	error = regmap_read(haptics->regmap, DRV2667_PAGE, &read_buf);
	if (error) {
		dev_err(&haptics->client->dev,
			"Failed to read the page number: %d\n", error);
		return -EIO;
	}

	if (read_buf == DRV2667_PAGE_0 ||
		haptics->page != read_buf) {
		error = regmap_write(haptics->regmap,
				DRV2667_PAGE, haptics->page);
		if (error) {
			dev_err(&haptics->client->dev,
				"Failed to set the page: %d\n", error);
			return -EIO;
		}
	}

	error = regmap_write(haptics->regmap, DRV2667_RAM_FREQ,	freq);
	if (error)
		dev_err(&haptics->client->dev,
				"Failed to set the frequency: %d\n", error);

	/* Reset back to original page */
	if (read_buf == DRV2667_PAGE_0 ||
		haptics->page != read_buf) {
		error = regmap_write(haptics->regmap, DRV2667_PAGE, read_buf);
		if (error) {
			dev_err(&haptics->client->dev,
				"Failed to set the page: %d\n", error);
			return -EIO;
		}
	}

	return error;
}

static void drv2667_worker(struct work_struct *work)
{
	struct drv2667_data *haptics = container_of(work, struct drv2667_data, work);
	int error;

	if (haptics->magnitude) {
		error = regmap_write(haptics->regmap,
				DRV2667_PAGE, haptics->page);
		if (error) {
			dev_err(&haptics->client->dev,
				"Failed to set the page: %d\n", error);
			return;
		}

		error = regmap_write(haptics->regmap, DRV2667_RAM_AMP,
				haptics->magnitude);
		if (error) {
			dev_err(&haptics->client->dev,
				"Failed to set the amplitude: %d\n", error);
			return;
		}

		error = regmap_write(haptics->regmap,
				DRV2667_PAGE, DRV2667_PAGE_0);
		if (error) {
			dev_err(&haptics->client->dev,
				"Failed to set the page: %d\n", error);
			return;
		}

		error = regmap_write(haptics->regmap,
				DRV2667_CTRL_2, DRV2667_GO);
		if (error) {
			dev_err(&haptics->client->dev,
				"Failed to set the GO bit: %d\n", error);
		}
	} else {
		error = regmap_update_bits(haptics->regmap, DRV2667_CTRL_2,
				DRV2667_GO, 0);
		if (error) {
			dev_err(&haptics->client->dev,
				"Failed to unset the GO bit: %d\n", error);
		}
	}
}

static int drv2667_haptics_play(struct input_dev *input, void *data,
				struct ff_effect *effect)
{
	struct drv2667_data *haptics = input_get_drvdata(input);

	if (effect->u.rumble.strong_magnitude > 0)
		haptics->magnitude = effect->u.rumble.strong_magnitude;
	else if (effect->u.rumble.weak_magnitude > 0)
		haptics->magnitude = effect->u.rumble.weak_magnitude;
	else
		haptics->magnitude = 0;

	schedule_work(&haptics->work);

	return 0;
}

static void drv2667_close(struct input_dev *input)
{
	struct drv2667_data *haptics = input_get_drvdata(input);
	int error;

	cancel_work_sync(&haptics->work);

	error = regmap_update_bits(haptics->regmap, DRV2667_CTRL_2,
				   DRV2667_STANDBY, DRV2667_STANDBY);
	if (error)
		dev_err(&haptics->client->dev,
			"Failed to enter standby mode: %d\n", error);
}

static const struct reg_sequence drv2667_init_regs[] = {
	{ DRV2667_CTRL_2, 0 },
	{ DRV2667_CTRL_1, DRV2667_25_VPP_GAIN },
	{ DRV2667_WV_SEQ_0, 1 },
	{ DRV2667_WV_SEQ_1, 0 }
};

static const struct reg_sequence drv2667_page1_init[] = {
	{ DRV2667_RAM_HDR_SZ, 0x05 },
	{ DRV2667_RAM_START_HI, 0x80 },
	{ DRV2667_RAM_START_LO, 0x06 },
	{ DRV2667_RAM_STOP_HI, 0x00 },
	{ DRV2667_RAM_STOP_LO, 0x09 },
	{ DRV2667_RAM_REPEAT_CT, 0 },
	{ DRV2667_RAM_DURATION, 0x05 },
	{ DRV2667_RAM_ENVELOPE, DRV2667_NO_ENV },
	{ DRV2667_RAM_AMP, 0x60 },
};

static int drv2667_init(struct drv2667_data *haptics)
{
	int error;

	/* Set default haptic frequency to 195Hz on Page 1*/
	haptics->frequency = 195;
	haptics->page = DRV2667_PAGE_1;

	error = regmap_register_patch(haptics->regmap,
				      drv2667_init_regs,
				      ARRAY_SIZE(drv2667_init_regs));
	if (error) {
		dev_err(&haptics->client->dev,
			"Failed to write init registers: %d\n",
			error);
		return error;
	}

	error = regmap_write(haptics->regmap, DRV2667_PAGE, haptics->page);
	if (error) {
		dev_err(&haptics->client->dev, "Failed to set page: %d\n",
			error);
		goto error_out;
	}

	error = drv2667_set_waveform_freq(haptics);
	if (error)
		goto error_page;

	error = regmap_register_patch(haptics->regmap,
				      drv2667_page1_init,
				      ARRAY_SIZE(drv2667_page1_init));
	if (error) {
		dev_err(&haptics->client->dev,
			"Failed to write page registers: %d\n",
			error);
		return error;
	}

	error = regmap_write(haptics->regmap, DRV2667_PAGE, DRV2667_PAGE_0);
	return error;

error_page:
	regmap_write(haptics->regmap, DRV2667_PAGE, DRV2667_PAGE_0);
error_out:
	return error;
}

static const struct regmap_config drv2667_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = DRV2667_MAX_REG,
	.reg_defaults = drv2667_reg_defs,
	.num_reg_defaults = ARRAY_SIZE(drv2667_reg_defs),
	.cache_type = REGCACHE_NONE,
};

static int drv2667_probe(struct i2c_client *client)
{
	struct drv2667_data *haptics;
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

	haptics->input_dev->name = "drv2667:haptics";
	haptics->input_dev->dev.parent = client->dev.parent;
	haptics->input_dev->close = drv2667_close;
	input_set_drvdata(haptics->input_dev, haptics);
	input_set_capability(haptics->input_dev, EV_FF, FF_RUMBLE);

	error = input_ff_create_memless(haptics->input_dev, NULL,
					drv2667_haptics_play);
	if (error) {
		dev_err(&client->dev, "input_ff_create() failed: %d\n",
			error);
		return error;
	}

	INIT_WORK(&haptics->work, drv2667_worker);

	haptics->client = client;
	i2c_set_clientdata(client, haptics);

	haptics->regmap = devm_regmap_init_i2c(client, &drv2667_regmap_config);
	if (IS_ERR(haptics->regmap)) {
		error = PTR_ERR(haptics->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			error);
		return error;
	}

	error = drv2667_init(haptics);
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

static int drv2667_suspend(struct device *dev)
{
	struct drv2667_data *haptics = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&haptics->input_dev->mutex);

	if (input_device_enabled(haptics->input_dev)) {
		ret = regmap_update_bits(haptics->regmap, DRV2667_CTRL_2,
					 DRV2667_STANDBY, DRV2667_STANDBY);
		if (ret) {
			dev_err(dev, "Failed to set standby mode\n");
			regulator_disable(haptics->regulator);
			goto out;
		}

		ret = regulator_disable(haptics->regulator);
		if (ret) {
			dev_err(dev, "Failed to disable regulator\n");
			regmap_update_bits(haptics->regmap,
					   DRV2667_CTRL_2,
					   DRV2667_STANDBY, 0);
		}
	}
out:
	mutex_unlock(&haptics->input_dev->mutex);
	return ret;
}

static int drv2667_resume(struct device *dev)
{
	struct drv2667_data *haptics = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&haptics->input_dev->mutex);

	if (input_device_enabled(haptics->input_dev)) {
		ret = regulator_enable(haptics->regulator);
		if (ret) {
			dev_err(dev, "Failed to enable regulator\n");
			goto out;
		}

		ret = regmap_update_bits(haptics->regmap, DRV2667_CTRL_2,
					 DRV2667_STANDBY, 0);
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

static DEFINE_SIMPLE_DEV_PM_OPS(drv2667_pm_ops, drv2667_suspend, drv2667_resume);

static const struct i2c_device_id drv2667_id[] = {
	{ "drv2667" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, drv2667_id);

#ifdef CONFIG_OF
static const struct of_device_id drv2667_of_match[] = {
	{ .compatible = "ti,drv2667", },
	{ }
};
MODULE_DEVICE_TABLE(of, drv2667_of_match);
#endif

static struct i2c_driver drv2667_driver = {
	.probe		= drv2667_probe,
	.driver		= {
		.name	= "drv2667-haptics",
		.of_match_table = of_match_ptr(drv2667_of_match),
		.pm	= pm_sleep_ptr(&drv2667_pm_ops),
	},
	.id_table = drv2667_id,
};
module_i2c_driver(drv2667_driver);

MODULE_DESCRIPTION("TI DRV2667 haptics driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
