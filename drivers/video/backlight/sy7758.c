// SPDX-License-Identifier: GPL-2.0-only
/*
 * Silergy SY7758 6-channel High Efficiency LED Driver
 *
 * Copyright (C) 2025 Kancy Joe <kancy2333@outlook.com>
 * Copyright (C) 2026 Linaro Limited
 * Author: Neil Armstrong <neil.armstrong@linaro.org>
 */
#include <linux/backlight.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/bitfield.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#define DEFAULT_BRIGHTNESS	1024
#define MAX_BRIGHTNESS		4080
#define REG_MAX			0xAE

/* Registers */
#define REG_DEV_CTL		0x01
#define REG_DEV_ID		0x03
#define REG_BRT_12BIT_L		0x10
#define REG_BRT_12BIT_H		0x11

/* OTP memory */
#define REG_OTP_CFG0		0xA0
#define REG_OTP_CFG1		0xA1
#define REG_OTP_CFG2		0xA2
#define REG_OTP_CFG5		0xA5
#define REG_OTP_CFG9		0xA9

/* Fields */
#define BIT_DEV_CTL_FAST	BIT(7)
#define MSK_DEV_CTL_BRT_MODE	GENMASK(2, 1)
#define BIT_DEV_CTL_BL_CTLB	BIT(0)

#define MSK_BRT_12BIT_L		GENMASK(7, 0)
#define MSK_BRT_12BIT_H		GENMASK(3, 0)

#define MSK_CFG0_CURRENT_LOW	GENMASK(7, 0)

#define BIT_CFG1_PDET_STDBY	BIT(7)
#define MSK_CFG1_CURRENT_MAX	GENMASK(6, 4)
#define MSK_CFG1_CURRENT_HIGH	GENMASK(3, 0)

#define BIT_CFG2_UVLO_EN	BIT(5)
#define BIT_CFG2_UVLO_TH	BIT(4)
#define BIT_CFG2_BL_ON		BIT(3)
#define BIT_CFG2_ISET_EN	BIT(2)
#define BIT_CFG2_BST_ESET_EN	BIT(1)

#define BIT_CFG5_PWM_DIRECT	BIT(7)
#define MSK_CFG5_PS_MODE	GENMASK(6, 4)
#define MSK_CFG5_PWM_FREQ	GENMASK(3, 0)

#define MSK_CFG9_VBST_MAX	GENMASK(7, 5)
#define BIT_CFG9_JUMP_EN	BIT(4)
#define MSK_CFG9_JUMP_TH	GENMASK(3, 2)
#define MSK_CFG9_JUMP_VOLTAGE	GENMASK(1, 0)

struct sy7758 {
	struct i2c_client *client;
	struct regmap *regmap;
	struct gpio_desc *gpio;
	struct backlight_device *bl;
};

static const struct regmap_config sy7758_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = REG_MAX,
};

static int sy7758_backlight_update_status(struct backlight_device *backlight_dev)
{
	struct sy7758 *sydev = bl_get_data(backlight_dev);
	unsigned int brightness = backlight_get_brightness(backlight_dev);
	int ret;

	ret = regmap_write(sydev->regmap, REG_BRT_12BIT_L,
			   FIELD_PREP(MSK_BRT_12BIT_L,
				      brightness & 0xff));
	if (ret)
		return ret;

	ret = regmap_write(sydev->regmap, REG_BRT_12BIT_H,
			   FIELD_PREP(MSK_BRT_12BIT_H,
				      (brightness >> 8) & 0xf));
	if (ret)
		return ret;

	return 0;
}

static const struct backlight_ops sy7758_backlight_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = sy7758_backlight_update_status,
};

static int sy7758_init(struct sy7758 *sydev)
{
	int ret = 0;

	ret = regmap_write(sydev->regmap, REG_DEV_CTL,
			   BIT_DEV_CTL_FAST | BIT_DEV_CTL_BL_CTLB |
			   FIELD_PREP(MSK_DEV_CTL_BRT_MODE, 2));
	if (ret)
		return ret;

	ret = regmap_write(sydev->regmap, REG_BRT_12BIT_L,
			   FIELD_PREP(MSK_BRT_12BIT_L,
				      DEFAULT_BRIGHTNESS & 0xff));
	if (ret)
		return ret;

	ret = regmap_write(sydev->regmap, REG_BRT_12BIT_H,
			   FIELD_PREP(MSK_BRT_12BIT_H,
				      (DEFAULT_BRIGHTNESS >> 8)));
	if (ret)
		return ret;

	ret = regmap_write(sydev->regmap, REG_OTP_CFG5,
			   FIELD_PREP(MSK_CFG5_PS_MODE, 6) |
			   FIELD_PREP(MSK_CFG5_PWM_FREQ, 4));
	if (ret)
		return ret;

	ret = regmap_write(sydev->regmap, REG_OTP_CFG0,
			   FIELD_PREP(MSK_CFG0_CURRENT_LOW, 85));
	if (ret)
		return ret;

	ret = regmap_write(sydev->regmap, REG_OTP_CFG1,
			   BIT_CFG1_PDET_STDBY |
			   FIELD_PREP(MSK_CFG1_CURRENT_MAX, 1) |
			   FIELD_PREP(MSK_CFG1_CURRENT_HIGH, 10));
	if (ret)
		return ret;

	ret = regmap_write(sydev->regmap, REG_OTP_CFG9,
			   FIELD_PREP(MSK_CFG9_VBST_MAX, 4));
	if (ret)
		return ret;

	ret = regmap_write(sydev->regmap, REG_OTP_CFG2,
			   BIT_CFG2_BL_ON | BIT_CFG2_UVLO_EN);
	if (ret)
		return ret;

	return 0;
}

static int sy7758_probe(struct i2c_client *client)
{
	struct backlight_properties props = { };
	struct device *dev = &client->dev;
	struct sy7758 *sydev;
	unsigned int dev_id;
	int ret;

	sydev = devm_kzalloc(dev, sizeof(*sydev), GFP_KERNEL);
	if (!sydev)
		return -ENOMEM;

	i2c_set_clientdata(client, sydev);

	/* Initialize regmap */
	sydev->client = client;
	sydev->regmap = devm_regmap_init_i2c(client, &sy7758_regmap_config);
	if (IS_ERR(sydev->regmap))
		return dev_err_probe(dev, PTR_ERR(sydev->regmap),
				     "failed to init regmap\n");

	/* Get and enable regulator */
	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulator\n");

	fsleep(100);

	/* Get enable GPIO and set to high */
	sydev->gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(sydev->gpio))
		return dev_err_probe(dev, PTR_ERR(sydev->gpio),
				     "failed to get enable GPIO\n");

	/* Let some time for HW to settle */
	fsleep(10000);

	/* try read and check device id */
	ret = regmap_read(sydev->regmap, REG_DEV_ID, &dev_id);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to read device id\n");
	if (dev_id != 0x63) {
		dev_err(dev, "unexpected device id: 0x%02x\n", dev_id);
		return -ENODEV;
	}

	/* Initialize and set default brightness */
	ret = sy7758_init(sydev);
	if (ret)
		return ret;

	props.type = BACKLIGHT_RAW;
	props.max_brightness = MAX_BRIGHTNESS;
	props.brightness = DEFAULT_BRIGHTNESS;
	props.scale = BACKLIGHT_SCALE_LINEAR;

	sydev->bl = devm_backlight_device_register(dev, "sy7758-backlight",
						   dev, sydev, &sy7758_backlight_ops,
						   &props);
	if (IS_ERR(sydev->bl))
		return dev_err_probe(dev, PTR_ERR(sydev->bl),
				     "failed to register backlight device\n");

	return backlight_update_status(sydev->bl);
}

static void sy7758_remove(struct i2c_client *client)
{
	struct sy7758 *sydev = i2c_get_clientdata(client);

	backlight_disable(sydev->bl);
}

static const struct i2c_device_id sy7758_ids[] = {
	{ "sy7758" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sy7758_ids);

static const struct of_device_id sy7758_match_table[] = {
	{ .compatible = "silergy,sy7758", },
	{ },
};
MODULE_DEVICE_TABLE(of, sy7758_match_table);

static struct i2c_driver sy7758_driver = {
	.driver = {
		.name = "sy7758",
		.of_match_table = sy7758_match_table,
	},
	.probe = sy7758_probe,
	.remove = sy7758_remove,
	.id_table = sy7758_ids,
};

module_i2c_driver(sy7758_driver);

MODULE_DESCRIPTION("Silergy SY7758 Backlight Driver");
MODULE_AUTHOR("Kancy Joe <kancy2333@outlook.com>");
MODULE_AUTHOR("Neil Armstrong <neil.armstrong@linaro.org>");
MODULE_LICENSE("GPL");
