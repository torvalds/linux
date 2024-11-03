// SPDX-License-Identifier: GPL-2.0-only
/*
 * LED driver : leds-ktd2692.c
 *
 * Copyright (C) 2015 Samsung Electronics
 * Ingi Kim <ingi2.kim@samsung.com>
 */

#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/leds-expresswire.h>
#include <linux/led-class-flash.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

/* Value related the movie mode */
#define KTD2692_MOVIE_MODE_CURRENT_LEVELS	16
#define KTD2692_MM_TO_FL_RATIO(x)		((x) / 3)
#define KTD2692_MM_MIN_CURR_THRESHOLD_SCALE	8

/* Value related the flash mode */
#define KTD2692_FLASH_MODE_TIMEOUT_LEVELS	8
#define KTD2692_FLASH_MODE_TIMEOUT_DISABLE	0
#define KTD2692_FLASH_MODE_CURR_PERCENT(x)	(((x) * 16) / 100)

/* Macro for getting offset of flash timeout */
#define GET_TIMEOUT_OFFSET(timeout, step)	((timeout) / (step))

/* Base register address */
#define KTD2692_REG_LVP_BASE			0x00
#define KTD2692_REG_FLASH_TIMEOUT_BASE		0x20
#define KTD2692_REG_MM_MIN_CURR_THRESHOLD_BASE	0x40
#define KTD2692_REG_MOVIE_CURRENT_BASE		0x60
#define KTD2692_REG_FLASH_CURRENT_BASE		0x80
#define KTD2692_REG_MODE_BASE			0xA0

/* KTD2692 default length of name */
#define KTD2692_NAME_LENGTH			20

/* Movie / Flash Mode Control */
enum ktd2692_led_mode {
	KTD2692_MODE_DISABLE = 0,	/* default */
	KTD2692_MODE_MOVIE,
	KTD2692_MODE_FLASH,
};

struct ktd2692_led_config_data {
	/* maximum LED current in movie mode */
	u32 movie_max_microamp;
	/* maximum LED current in flash mode */
	u32 flash_max_microamp;
	/* maximum flash timeout */
	u32 flash_max_timeout;
	/* max LED brightness level */
	enum led_brightness max_brightness;
};

const struct expresswire_timing ktd2692_timing = {
	.poweroff_us = 700,
	.data_start_us = 10,
	.end_of_data_low_us = 10,
	.end_of_data_high_us = 350,
	.short_bitset_us = 4,
	.long_bitset_us = 12
};

struct ktd2692_context {
	/* Common ExpressWire properties (ctrl GPIO and timing) */
	struct expresswire_common_props props;

	/* Related LED Flash class device */
	struct led_classdev_flash fled_cdev;

	/* secures access to the device */
	struct mutex lock;
	struct regulator *regulator;

	struct gpio_desc *aux_gpio;

	enum ktd2692_led_mode mode;
	enum led_brightness torch_brightness;
};

static struct ktd2692_context *fled_cdev_to_led(
				struct led_classdev_flash *fled_cdev)
{
	return container_of(fled_cdev, struct ktd2692_context, fled_cdev);
}

static int ktd2692_led_brightness_set(struct led_classdev *led_cdev,
				       enum led_brightness brightness)
{
	struct led_classdev_flash *fled_cdev = lcdev_to_flcdev(led_cdev);
	struct ktd2692_context *led = fled_cdev_to_led(fled_cdev);

	mutex_lock(&led->lock);

	if (brightness == LED_OFF) {
		led->mode = KTD2692_MODE_DISABLE;
		gpiod_direction_output(led->aux_gpio, 0);
	} else {
		expresswire_write_u8(&led->props, brightness |
					KTD2692_REG_MOVIE_CURRENT_BASE);
		led->mode = KTD2692_MODE_MOVIE;
	}

	expresswire_write_u8(&led->props, led->mode | KTD2692_REG_MODE_BASE);
	mutex_unlock(&led->lock);

	return 0;
}

static int ktd2692_led_flash_strobe_set(struct led_classdev_flash *fled_cdev,
					bool state)
{
	struct ktd2692_context *led = fled_cdev_to_led(fled_cdev);
	struct led_flash_setting *timeout = &fled_cdev->timeout;
	u32 flash_tm_reg;

	mutex_lock(&led->lock);

	if (state) {
		flash_tm_reg = GET_TIMEOUT_OFFSET(timeout->val, timeout->step);
		expresswire_write_u8(&led->props, flash_tm_reg
				| KTD2692_REG_FLASH_TIMEOUT_BASE);

		led->mode = KTD2692_MODE_FLASH;
		gpiod_direction_output(led->aux_gpio, 1);
	} else {
		led->mode = KTD2692_MODE_DISABLE;
		gpiod_direction_output(led->aux_gpio, 0);
	}

	expresswire_write_u8(&led->props, led->mode | KTD2692_REG_MODE_BASE);

	fled_cdev->led_cdev.brightness = LED_OFF;
	led->mode = KTD2692_MODE_DISABLE;

	mutex_unlock(&led->lock);

	return 0;
}

static int ktd2692_led_flash_timeout_set(struct led_classdev_flash *fled_cdev,
					 u32 timeout)
{
	return 0;
}

static void ktd2692_init_movie_current_max(struct ktd2692_led_config_data *cfg)
{
	u32 offset, step;
	u32 movie_current_microamp;

	offset = KTD2692_MOVIE_MODE_CURRENT_LEVELS;
	step = KTD2692_MM_TO_FL_RATIO(cfg->flash_max_microamp)
		/ KTD2692_MOVIE_MODE_CURRENT_LEVELS;

	do {
		movie_current_microamp = step * offset;
		offset--;
	} while ((movie_current_microamp > cfg->movie_max_microamp) &&
		(offset > 0));

	cfg->max_brightness = offset;
}

static void ktd2692_init_flash_timeout(struct led_classdev_flash *fled_cdev,
				       struct ktd2692_led_config_data *cfg)
{
	struct led_flash_setting *setting;

	setting = &fled_cdev->timeout;
	setting->min = KTD2692_FLASH_MODE_TIMEOUT_DISABLE;
	setting->max = cfg->flash_max_timeout;
	setting->step = cfg->flash_max_timeout
			/ (KTD2692_FLASH_MODE_TIMEOUT_LEVELS - 1);
	setting->val = cfg->flash_max_timeout;
}

static void ktd2692_setup(struct ktd2692_context *led)
{
	led->mode = KTD2692_MODE_DISABLE;
	expresswire_power_off(&led->props);
	gpiod_direction_output(led->aux_gpio, 0);

	expresswire_write_u8(&led->props, (KTD2692_MM_MIN_CURR_THRESHOLD_SCALE - 1)
				 | KTD2692_REG_MM_MIN_CURR_THRESHOLD_BASE);
	expresswire_write_u8(&led->props, KTD2692_FLASH_MODE_CURR_PERCENT(45)
				 | KTD2692_REG_FLASH_CURRENT_BASE);
}

static void regulator_disable_action(void *_data)
{
	struct device *dev = _data;
	struct ktd2692_context *led = dev_get_drvdata(dev);
	int ret;

	ret = regulator_disable(led->regulator);
	if (ret)
		dev_err(dev, "Failed to disable supply: %d\n", ret);
}

static int ktd2692_parse_dt(struct ktd2692_context *led, struct device *dev,
			    struct ktd2692_led_config_data *cfg)
{
	struct device_node *np = dev_of_node(dev);
	int ret;

	if (!np)
		return -ENXIO;

	led->props.ctrl_gpio = devm_gpiod_get(dev, "ctrl", GPIOD_ASIS);
	ret = PTR_ERR_OR_ZERO(led->props.ctrl_gpio);
	if (ret)
		return dev_err_probe(dev, ret, "cannot get ctrl-gpios\n");

	led->aux_gpio = devm_gpiod_get_optional(dev, "aux", GPIOD_ASIS);
	if (IS_ERR(led->aux_gpio))
		return dev_err_probe(dev, PTR_ERR(led->aux_gpio), "cannot get aux-gpios\n");

	led->regulator = devm_regulator_get(dev, "vin");
	if (IS_ERR(led->regulator))
		led->regulator = NULL;

	if (led->regulator) {
		ret = regulator_enable(led->regulator);
		if (ret) {
			dev_err(dev, "Failed to enable supply: %d\n", ret);
		} else {
			ret = devm_add_action_or_reset(dev,
						regulator_disable_action, dev);
			if (ret)
				return ret;
		}
	}

	struct device_node *child_node __free(device_node) =
		of_get_next_available_child(np, NULL);
	if (!child_node) {
		dev_err(dev, "No DT child node found for connected LED.\n");
		return -EINVAL;
	}

	led->fled_cdev.led_cdev.name =
		of_get_property(child_node, "label", NULL) ? : child_node->name;

	ret = of_property_read_u32(child_node, "led-max-microamp",
				   &cfg->movie_max_microamp);
	if (ret) {
		dev_err(dev, "failed to parse led-max-microamp\n");
		return ret;
	}

	ret = of_property_read_u32(child_node, "flash-max-microamp",
				   &cfg->flash_max_microamp);
	if (ret) {
		dev_err(dev, "failed to parse flash-max-microamp\n");
		return ret;
	}

	ret = of_property_read_u32(child_node, "flash-max-timeout-us",
				   &cfg->flash_max_timeout);
	if (ret) {
		dev_err(dev, "failed to parse flash-max-timeout-us\n");
		return ret;
	}

	return 0;
}

static const struct led_flash_ops flash_ops = {
	.strobe_set = ktd2692_led_flash_strobe_set,
	.timeout_set = ktd2692_led_flash_timeout_set,
};

static int ktd2692_probe(struct platform_device *pdev)
{
	struct ktd2692_context *led;
	struct led_classdev *led_cdev;
	struct led_classdev_flash *fled_cdev;
	struct ktd2692_led_config_data led_cfg;
	int ret;

	led = devm_kzalloc(&pdev->dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	fled_cdev = &led->fled_cdev;
	led_cdev = &fled_cdev->led_cdev;
	led->props.timing = ktd2692_timing;

	ret = ktd2692_parse_dt(led, &pdev->dev, &led_cfg);
	if (ret)
		return ret;

	ktd2692_init_flash_timeout(fled_cdev, &led_cfg);
	ktd2692_init_movie_current_max(&led_cfg);

	fled_cdev->ops = &flash_ops;

	led_cdev->max_brightness = led_cfg.max_brightness;
	led_cdev->brightness_set_blocking = ktd2692_led_brightness_set;
	led_cdev->flags |= LED_CORE_SUSPENDRESUME | LED_DEV_CAP_FLASH;

	mutex_init(&led->lock);

	platform_set_drvdata(pdev, led);

	ret = led_classdev_flash_register(&pdev->dev, fled_cdev);
	if (ret) {
		dev_err(&pdev->dev, "can't register LED %s\n", led_cdev->name);
		mutex_destroy(&led->lock);
		return ret;
	}

	ktd2692_setup(led);

	return 0;
}

static void ktd2692_remove(struct platform_device *pdev)
{
	struct ktd2692_context *led = platform_get_drvdata(pdev);

	led_classdev_flash_unregister(&led->fled_cdev);

	mutex_destroy(&led->lock);
}

static const struct of_device_id ktd2692_match[] = {
	{ .compatible = "kinetic,ktd2692", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ktd2692_match);

static struct platform_driver ktd2692_driver = {
	.driver = {
		.name  = "ktd2692",
		.of_match_table = ktd2692_match,
	},
	.probe  = ktd2692_probe,
	.remove_new = ktd2692_remove,
};

module_platform_driver(ktd2692_driver);

MODULE_IMPORT_NS(EXPRESSWIRE);
MODULE_AUTHOR("Ingi Kim <ingi2.kim@samsung.com>");
MODULE_DESCRIPTION("Kinetic KTD2692 LED driver");
MODULE_LICENSE("GPL v2");
