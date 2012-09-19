/*
 * Simple driver for Texas Instruments LM3556 LED Flash driver chip (Rev0x03)
 * Copyright (C) 2012 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Please refer Documentation/leds/leds-lm3556.txt file.
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/regmap.h>
#include <linux/platform_data/leds-lm3556.h>

#define REG_FILT_TIME			(0x0)
#define REG_IVFM_MODE			(0x1)
#define REG_NTC				(0x2)
#define REG_INDIC_TIME			(0x3)
#define REG_INDIC_BLINK			(0x4)
#define REG_INDIC_PERIOD		(0x5)
#define REG_TORCH_TIME			(0x6)
#define REG_CONF			(0x7)
#define REG_FLASH			(0x8)
#define REG_I_CTRL			(0x9)
#define REG_ENABLE			(0xA)
#define REG_FLAG			(0xB)
#define REG_MAX				(0xB)

#define IVFM_FILTER_TIME_SHIFT		(3)
#define UVLO_EN_SHIFT			(7)
#define HYSTERSIS_SHIFT			(5)
#define IVM_D_TH_SHIFT			(2)
#define IVFM_ADJ_MODE_SHIFT		(0)
#define NTC_EVENT_LVL_SHIFT		(5)
#define NTC_TRIP_TH_SHIFT		(2)
#define NTC_BIAS_I_LVL_SHIFT		(0)
#define INDIC_RAMP_UP_TIME_SHIFT	(3)
#define INDIC_RAMP_DN_TIME_SHIFT	(0)
#define INDIC_N_BLANK_SHIFT		(4)
#define INDIC_PULSE_TIME_SHIFT		(0)
#define INDIC_N_PERIOD_SHIFT		(0)
#define TORCH_RAMP_UP_TIME_SHIFT	(3)
#define TORCH_RAMP_DN_TIME_SHIFT	(0)
#define STROBE_USUAGE_SHIFT		(7)
#define STROBE_PIN_POLARITY_SHIFT	(6)
#define TORCH_PIN_POLARITY_SHIFT	(5)
#define TX_PIN_POLARITY_SHIFT		(4)
#define TX_EVENT_LVL_SHIFT		(3)
#define IVFM_EN_SHIFT			(2)
#define NTC_MODE_SHIFT			(1)
#define INDIC_MODE_SHIFT		(0)
#define INDUCTOR_I_LIMIT_SHIFT		(6)
#define FLASH_RAMP_TIME_SHIFT		(3)
#define FLASH_TOUT_TIME_SHIFT		(0)
#define TORCH_I_SHIFT			(4)
#define FLASH_I_SHIFT			(0)
#define NTC_EN_SHIFT			(7)
#define TX_PIN_EN_SHIFT			(6)
#define STROBE_PIN_EN_SHIFT		(5)
#define TORCH_PIN_EN_SHIFT		(4)
#define PRECHG_MODE_EN_SHIFT		(3)
#define PASS_MODE_ONLY_EN_SHIFT		(2)
#define MODE_BITS_SHIFT			(0)

#define IVFM_FILTER_TIME_MASK		(0x3)
#define UVLO_EN_MASK			(0x1)
#define HYSTERSIS_MASK			(0x3)
#define IVM_D_TH_MASK			(0x7)
#define IVFM_ADJ_MODE_MASK		(0x3)
#define NTC_EVENT_LVL_MASK		(0x1)
#define NTC_TRIP_TH_MASK		(0x7)
#define NTC_BIAS_I_LVL_MASK		(0x3)
#define INDIC_RAMP_UP_TIME_MASK		(0x7)
#define INDIC_RAMP_DN_TIME_MASK		(0x7)
#define INDIC_N_BLANK_MASK		(0x7)
#define INDIC_PULSE_TIME_MASK		(0x7)
#define INDIC_N_PERIOD_MASK		(0x7)
#define TORCH_RAMP_UP_TIME_MASK		(0x7)
#define TORCH_RAMP_DN_TIME_MASK		(0x7)
#define STROBE_USUAGE_MASK		(0x1)
#define STROBE_PIN_POLARITY_MASK	(0x1)
#define TORCH_PIN_POLARITY_MASK		(0x1)
#define TX_PIN_POLARITY_MASK		(0x1)
#define TX_EVENT_LVL_MASK		(0x1)
#define IVFM_EN_MASK			(0x1)
#define NTC_MODE_MASK			(0x1)
#define INDIC_MODE_MASK			(0x1)
#define INDUCTOR_I_LIMIT_MASK		(0x3)
#define FLASH_RAMP_TIME_MASK		(0x7)
#define FLASH_TOUT_TIME_MASK		(0x7)
#define TORCH_I_MASK			(0x7)
#define FLASH_I_MASK			(0xF)
#define NTC_EN_MASK			(0x1)
#define TX_PIN_EN_MASK			(0x1)
#define STROBE_PIN_EN_MASK		(0x1)
#define TORCH_PIN_EN_MASK		(0x1)
#define PRECHG_MODE_EN_MASK		(0x1)
#define PASS_MODE_ONLY_EN_MASK		(0x1)
#define MODE_BITS_MASK			(0x13)
#define EX_PIN_CONTROL_MASK		(0xF1)
#define EX_PIN_ENABLE_MASK		(0x70)

enum lm3556_indic_pulse_time {
	PULSE_TIME_0_MS = 0,
	PULSE_TIME_32_MS,
	PULSE_TIME_64_MS,
	PULSE_TIME_92_MS,
	PULSE_TIME_128_MS,
	PULSE_TIME_160_MS,
	PULSE_TIME_196_MS,
	PULSE_TIME_224_MS,
	PULSE_TIME_256_MS,
	PULSE_TIME_288_MS,
	PULSE_TIME_320_MS,
	PULSE_TIME_352_MS,
	PULSE_TIME_384_MS,
	PULSE_TIME_416_MS,
	PULSE_TIME_448_MS,
	PULSE_TIME_480_MS,
};

enum lm3556_indic_n_blank {
	INDIC_N_BLANK_0 = 0,
	INDIC_N_BLANK_1,
	INDIC_N_BLANK_2,
	INDIC_N_BLANK_3,
	INDIC_N_BLANK_4,
	INDIC_N_BLANK_5,
	INDIC_N_BLANK_6,
	INDIC_N_BLANK_7,
	INDIC_N_BLANK_8,
	INDIC_N_BLANK_9,
	INDIC_N_BLANK_10,
	INDIC_N_BLANK_11,
	INDIC_N_BLANK_12,
	INDIC_N_BLANK_13,
	INDIC_N_BLANK_14,
	INDIC_N_BLANK_15,
};

enum lm3556_indic_period {
	INDIC_PERIOD_0 = 0,
	INDIC_PERIOD_1,
	INDIC_PERIOD_2,
	INDIC_PERIOD_3,
	INDIC_PERIOD_4,
	INDIC_PERIOD_5,
	INDIC_PERIOD_6,
	INDIC_PERIOD_7,
};

enum lm3556_mode {
	MODES_STASNDBY = 0,
	MODES_INDIC,
	MODES_TORCH,
	MODES_FLASH
};

#define INDIC_PATTERN_SIZE 4

struct indicator {
	u8 blinking;
	u8 period_cnt;
};

struct lm3556_chip_data {
	struct device *dev;

	struct led_classdev cdev_flash;
	struct led_classdev cdev_torch;
	struct led_classdev cdev_indicator;

	struct lm3556_platform_data *pdata;
	struct regmap *regmap;
	struct mutex lock;

	unsigned int last_flag;
};

/* indicator pattern */
static struct indicator indicator_pattern[INDIC_PATTERN_SIZE] = {
	[0] = {(INDIC_N_BLANK_1 << INDIC_N_BLANK_SHIFT)
	       | PULSE_TIME_32_MS, INDIC_PERIOD_1},
	[1] = {(INDIC_N_BLANK_15 << INDIC_N_BLANK_SHIFT)
	       | PULSE_TIME_32_MS, INDIC_PERIOD_2},
	[2] = {(INDIC_N_BLANK_10 << INDIC_N_BLANK_SHIFT)
	       | PULSE_TIME_32_MS, INDIC_PERIOD_4},
	[3] = {(INDIC_N_BLANK_5 << INDIC_N_BLANK_SHIFT)
	       | PULSE_TIME_32_MS, INDIC_PERIOD_7},
};

/* chip initialize */
static int __devinit lm3556_chip_init(struct lm3556_chip_data *chip)
{
	unsigned int reg_val;
	int ret;
	struct lm3556_platform_data *pdata = chip->pdata;

	/* set config register */
	ret = regmap_read(chip->regmap, REG_CONF, &reg_val);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read REG_CONF Register\n");
		goto out;
	}

	reg_val &= (~EX_PIN_CONTROL_MASK);
	reg_val |= ((pdata->torch_pin_polarity & 0x01)
		    << TORCH_PIN_POLARITY_SHIFT);
	reg_val |= ((pdata->strobe_usuage & 0x01) << STROBE_USUAGE_SHIFT);
	reg_val |= ((pdata->strobe_pin_polarity & 0x01)
		    << STROBE_PIN_POLARITY_SHIFT);
	reg_val |= ((pdata->tx_pin_polarity & 0x01) << TX_PIN_POLARITY_SHIFT);
	reg_val |= ((pdata->indicator_mode & 0x01) << INDIC_MODE_SHIFT);

	ret = regmap_write(chip->regmap, REG_CONF, reg_val);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to write REG_CONF Regisgter\n");
		goto out;
	}

	/* set enable register */
	ret = regmap_read(chip->regmap, REG_ENABLE, &reg_val);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read REG_ENABLE Register\n");
		goto out;
	}

	reg_val &= (~EX_PIN_ENABLE_MASK);
	reg_val |= ((pdata->torch_pin_en & 0x01) << TORCH_PIN_EN_SHIFT);
	reg_val |= ((pdata->strobe_pin_en & 0x01) << STROBE_PIN_EN_SHIFT);
	reg_val |= ((pdata->tx_pin_en & 0x01) << TX_PIN_EN_SHIFT);

	ret = regmap_write(chip->regmap, REG_ENABLE, reg_val);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to write REG_ENABLE Regisgter\n");
		goto out;
	}

out:
	return ret;
}

/* chip control */
static int lm3556_control(struct lm3556_chip_data *chip,
			  u8 brightness, enum lm3556_mode opmode)
{
	int ret;
	struct lm3556_platform_data *pdata = chip->pdata;

	ret = regmap_read(chip->regmap, REG_FLAG, &chip->last_flag);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read REG_FLAG Register\n");
		goto out;
	}

	if (chip->last_flag)
		dev_info(chip->dev, "Last FLAG is 0x%x\n", chip->last_flag);

	/* brightness 0 means off-state */
	if (!brightness)
		opmode = MODES_STASNDBY;

	switch (opmode) {
	case MODES_TORCH:
		ret = regmap_update_bits(chip->regmap, REG_I_CTRL,
					 TORCH_I_MASK << TORCH_I_SHIFT,
					 (brightness - 1) << TORCH_I_SHIFT);

		if (pdata->torch_pin_en)
			opmode |= (TORCH_PIN_EN_MASK << TORCH_PIN_EN_SHIFT);
		break;

	case MODES_FLASH:
		ret = regmap_update_bits(chip->regmap, REG_I_CTRL,
					 FLASH_I_MASK << FLASH_I_SHIFT,
					 (brightness - 1) << FLASH_I_SHIFT);
		break;

	case MODES_INDIC:
		ret = regmap_update_bits(chip->regmap, REG_I_CTRL,
					 TORCH_I_MASK << TORCH_I_SHIFT,
					 (brightness - 1) << TORCH_I_SHIFT);
		break;

	case MODES_STASNDBY:
		if (pdata->torch_pin_en)
			opmode |= (TORCH_PIN_EN_MASK << TORCH_PIN_EN_SHIFT);
		break;

	default:
		return ret;
	}
	if (ret < 0) {
		dev_err(chip->dev, "Failed to write REG_I_CTRL Register\n");
		goto out;
	}
	ret = regmap_update_bits(chip->regmap, REG_ENABLE,
				 MODE_BITS_MASK << MODE_BITS_SHIFT,
				 opmode << MODE_BITS_SHIFT);

out:
	return ret;
}

/* torch */
static void lm3556_torch_brightness_set(struct led_classdev *cdev,
					enum led_brightness brightness)
{
	struct lm3556_chip_data *chip =
	    container_of(cdev, struct lm3556_chip_data, cdev_torch);

	mutex_lock(&chip->lock);
	lm3556_control(chip, brightness, MODES_TORCH);
	mutex_unlock(&chip->lock);
}

/* flash */
static void lm3556_strobe_brightness_set(struct led_classdev *cdev,
					 enum led_brightness brightness)
{
	struct lm3556_chip_data *chip =
	    container_of(cdev, struct lm3556_chip_data, cdev_flash);

	mutex_lock(&chip->lock);
	lm3556_control(chip, brightness, MODES_FLASH);
	mutex_unlock(&chip->lock);
}

/* indicator */
static void lm3556_indicator_brightness_set(struct led_classdev *cdev,
					    enum led_brightness brightness)
{
	struct lm3556_chip_data *chip =
	    container_of(cdev, struct lm3556_chip_data, cdev_indicator);

	mutex_lock(&chip->lock);
	lm3556_control(chip, brightness, MODES_INDIC);
	mutex_unlock(&chip->lock);
}

/* indicator pattern */
static ssize_t lm3556_indicator_pattern_store(struct device *dev,
					      struct device_attribute *devAttr,
					      const char *buf, size_t size)
{
	ssize_t ret;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct lm3556_chip_data *chip =
	    container_of(led_cdev, struct lm3556_chip_data, cdev_indicator);
	unsigned int state;

	ret = kstrtouint(buf, 10, &state);
	if (ret)
		goto out;
	if (state > INDIC_PATTERN_SIZE - 1)
		state = INDIC_PATTERN_SIZE - 1;

	ret = regmap_write(chip->regmap, REG_INDIC_BLINK,
			   indicator_pattern[state].blinking);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to write REG_ENABLE Regisgter\n");
		goto out;
	}

	ret = regmap_write(chip->regmap, REG_INDIC_PERIOD,
			   indicator_pattern[state].period_cnt);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to write REG_ENABLE Regisgter\n");
		goto out;
	}

	return size;
out:
	dev_err(chip->dev, "Indicator pattern doesn't saved\n");
	return size;
}

static DEVICE_ATTR(pattern, 0666, NULL, lm3556_indicator_pattern_store);

static const struct regmap_config lm3556_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = REG_MAX,
};

/* module initialize */
static int __devinit lm3556_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	struct lm3556_platform_data *pdata = client->dev.platform_data;
	struct lm3556_chip_data *chip;

	int err;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "i2c functionality check fail.\n");
		return -EOPNOTSUPP;
	}

	if (pdata == NULL) {
		dev_err(&client->dev, "Needs Platform Data.\n");
		return -ENODATA;
	}

	chip =
	    devm_kzalloc(&client->dev, sizeof(struct lm3556_chip_data),
			 GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &client->dev;
	chip->pdata = pdata;

	chip->regmap = devm_regmap_init_i2c(client, &lm3556_regmap);
	if (IS_ERR(chip->regmap)) {
		err = PTR_ERR(chip->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			err);
		return err;
	}

	mutex_init(&chip->lock);
	i2c_set_clientdata(client, chip);

	err = lm3556_chip_init(chip);
	if (err < 0)
		goto err_out;

	/* flash */
	chip->cdev_flash.name = "flash";
	chip->cdev_flash.max_brightness = 16;
	chip->cdev_flash.brightness_set = lm3556_strobe_brightness_set;
	err = led_classdev_register((struct device *)
				    &client->dev, &chip->cdev_flash);
	if (err < 0)
		goto err_out;
	/* torch */
	chip->cdev_torch.name = "torch";
	chip->cdev_torch.max_brightness = 8;
	chip->cdev_torch.brightness_set = lm3556_torch_brightness_set;
	err = led_classdev_register((struct device *)
				    &client->dev, &chip->cdev_torch);
	if (err < 0)
		goto err_create_torch_file;
	/* indicator */
	chip->cdev_indicator.name = "indicator";
	chip->cdev_indicator.max_brightness = 8;
	chip->cdev_indicator.brightness_set = lm3556_indicator_brightness_set;
	err = led_classdev_register((struct device *)
				    &client->dev, &chip->cdev_indicator);
	if (err < 0)
		goto err_create_indicator_file;

	err = device_create_file(chip->cdev_indicator.dev, &dev_attr_pattern);
	if (err < 0)
		goto err_create_pattern_file;

	dev_info(&client->dev, "LM3556 is initialized\n");
	return 0;

err_create_pattern_file:
	led_classdev_unregister(&chip->cdev_indicator);
err_create_indicator_file:
	led_classdev_unregister(&chip->cdev_torch);
err_create_torch_file:
	led_classdev_unregister(&chip->cdev_flash);
err_out:
	return err;
}

static int __devexit lm3556_remove(struct i2c_client *client)
{
	struct lm3556_chip_data *chip = i2c_get_clientdata(client);

	device_remove_file(chip->cdev_indicator.dev, &dev_attr_pattern);
	led_classdev_unregister(&chip->cdev_indicator);
	led_classdev_unregister(&chip->cdev_torch);
	led_classdev_unregister(&chip->cdev_flash);
	regmap_write(chip->regmap, REG_ENABLE, 0);
	return 0;
}

static const struct i2c_device_id lm3556_id[] = {
	{LM3556_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, lm3556_id);

static struct i2c_driver lm3556_i2c_driver = {
	.driver = {
		   .name = LM3556_NAME,
		   .owner = THIS_MODULE,
		   .pm = NULL,
		   },
	.probe = lm3556_probe,
	.remove = __devexit_p(lm3556_remove),
	.id_table = lm3556_id,
};

module_i2c_driver(lm3556_i2c_driver);

MODULE_DESCRIPTION("Texas Instruments Flash Lighting driver for LM3556");
MODULE_AUTHOR("Daniel Jeong <daniel.jeong@ti.com>");
MODULE_AUTHOR("G.Shark Jeong <gshark.jeong@gmail.com>");
MODULE_LICENSE("GPL v2");
