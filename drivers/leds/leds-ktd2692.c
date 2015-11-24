/*
 * LED driver : leds-ktd2692.c
 *
 * Copyright (C) 2015 Samsung Electronics
 * Ingi Kim <ingi2.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/led-class-flash.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>

/* Value related the movie mode */
#define KTD2692_MOVIE_MODE_CURRENT_LEVELS	16
#define KTD2692_MM_TO_FL_RATIO(x)		((x) / 3)
#define KTD2962_MM_MIN_CURR_THRESHOLD_SCALE	8

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

/* Set bit coding time for expresswire interface */
#define KTD2692_TIME_RESET_US			700
#define KTD2692_TIME_DATA_START_TIME_US		10
#define KTD2692_TIME_HIGH_END_OF_DATA_US	350
#define KTD2692_TIME_LOW_END_OF_DATA_US		10
#define KTD2692_TIME_SHORT_BITSET_US		4
#define KTD2692_TIME_LONG_BITSET_US		12

/* KTD2692 default length of name */
#define KTD2692_NAME_LENGTH			20

enum ktd2692_bitset {
	KTD2692_LOW = 0,
	KTD2692_HIGH,
};

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

struct ktd2692_context {
	/* Related LED Flash class device */
	struct led_classdev_flash fled_cdev;

	/* secures access to the device */
	struct mutex lock;
	struct regulator *regulator;
	struct work_struct work_brightness_set;

	struct gpio_desc *aux_gpio;
	struct gpio_desc *ctrl_gpio;

	enum ktd2692_led_mode mode;
	enum led_brightness torch_brightness;
};

static struct ktd2692_context *fled_cdev_to_led(
				struct led_classdev_flash *fled_cdev)
{
	return container_of(fled_cdev, struct ktd2692_context, fled_cdev);
}

static void ktd2692_expresswire_start(struct ktd2692_context *led)
{
	gpiod_direction_output(led->ctrl_gpio, KTD2692_HIGH);
	udelay(KTD2692_TIME_DATA_START_TIME_US);
}

static void ktd2692_expresswire_reset(struct ktd2692_context *led)
{
	gpiod_direction_output(led->ctrl_gpio, KTD2692_LOW);
	udelay(KTD2692_TIME_RESET_US);
}

static void ktd2692_expresswire_end(struct ktd2692_context *led)
{
	gpiod_direction_output(led->ctrl_gpio, KTD2692_LOW);
	udelay(KTD2692_TIME_LOW_END_OF_DATA_US);
	gpiod_direction_output(led->ctrl_gpio, KTD2692_HIGH);
	udelay(KTD2692_TIME_HIGH_END_OF_DATA_US);
}

static void ktd2692_expresswire_set_bit(struct ktd2692_context *led, bool bit)
{
	/*
	 * The Low Bit(0) and High Bit(1) is based on a time detection
	 * algorithm between time low and time high
	 * Time_(L_LB) : Low time of the Low Bit(0)
	 * Time_(H_LB) : High time of the LOW Bit(0)
	 * Time_(L_HB) : Low time of the High Bit(1)
	 * Time_(H_HB) : High time of the High Bit(1)
	 *
	 * It can be simplified to:
	 * Low Bit(0) : 2 * Time_(H_LB) < Time_(L_LB)
	 * High Bit(1) : 2 * Time_(L_HB) < Time_(H_HB)
	 * HIGH  ___           ____    _..     _________    ___
	 *          |_________|    |_..  |____|         |__|
	 * LOW        <L_LB>  <H_LB>     <L_HB>  <H_HB>
	 *          [  Low Bit (0) ]     [  High Bit(1) ]
	 */
	if (bit) {
		gpiod_direction_output(led->ctrl_gpio, KTD2692_LOW);
		udelay(KTD2692_TIME_SHORT_BITSET_US);
		gpiod_direction_output(led->ctrl_gpio, KTD2692_HIGH);
		udelay(KTD2692_TIME_LONG_BITSET_US);
	} else {
		gpiod_direction_output(led->ctrl_gpio, KTD2692_LOW);
		udelay(KTD2692_TIME_LONG_BITSET_US);
		gpiod_direction_output(led->ctrl_gpio, KTD2692_HIGH);
		udelay(KTD2692_TIME_SHORT_BITSET_US);
	}
}

static void ktd2692_expresswire_write(struct ktd2692_context *led, u8 value)
{
	int i;

	ktd2692_expresswire_start(led);
	for (i = 7; i >= 0; i--)
		ktd2692_expresswire_set_bit(led, value & BIT(i));
	ktd2692_expresswire_end(led);
}

static void ktd2692_brightness_set(struct ktd2692_context *led,
				   enum led_brightness brightness)
{
	mutex_lock(&led->lock);

	if (brightness == LED_OFF) {
		led->mode = KTD2692_MODE_DISABLE;
		gpiod_direction_output(led->aux_gpio, KTD2692_LOW);
	} else {
		ktd2692_expresswire_write(led, brightness |
					KTD2692_REG_MOVIE_CURRENT_BASE);
		led->mode = KTD2692_MODE_MOVIE;
	}

	ktd2692_expresswire_write(led, led->mode | KTD2692_REG_MODE_BASE);
	mutex_unlock(&led->lock);
}

static void ktd2692_brightness_set_work(struct work_struct *work)
{
	struct ktd2692_context *led =
		container_of(work, struct ktd2692_context, work_brightness_set);

	ktd2692_brightness_set(led, led->torch_brightness);
}

static void ktd2692_led_brightness_set(struct led_classdev *led_cdev,
				       enum led_brightness brightness)
{
	struct led_classdev_flash *fled_cdev = lcdev_to_flcdev(led_cdev);
	struct ktd2692_context *led = fled_cdev_to_led(fled_cdev);

	led->torch_brightness = brightness;
	schedule_work(&led->work_brightness_set);
}

static int ktd2692_led_brightness_set_sync(struct led_classdev *led_cdev,
					   enum led_brightness brightness)
{
	struct led_classdev_flash *fled_cdev = lcdev_to_flcdev(led_cdev);
	struct ktd2692_context *led = fled_cdev_to_led(fled_cdev);

	ktd2692_brightness_set(led, brightness);

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
		ktd2692_expresswire_write(led, flash_tm_reg
				| KTD2692_REG_FLASH_TIMEOUT_BASE);

		led->mode = KTD2692_MODE_FLASH;
		gpiod_direction_output(led->aux_gpio, KTD2692_HIGH);
	} else {
		led->mode = KTD2692_MODE_DISABLE;
		gpiod_direction_output(led->aux_gpio, KTD2692_LOW);
	}

	ktd2692_expresswire_write(led, led->mode | KTD2692_REG_MODE_BASE);

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
	ktd2692_expresswire_reset(led);
	gpiod_direction_output(led->aux_gpio, KTD2692_LOW);

	ktd2692_expresswire_write(led, (KTD2962_MM_MIN_CURR_THRESHOLD_SCALE - 1)
				 | KTD2692_REG_MM_MIN_CURR_THRESHOLD_BASE);
	ktd2692_expresswire_write(led, KTD2692_FLASH_MODE_CURR_PERCENT(45)
				 | KTD2692_REG_FLASH_CURRENT_BASE);
}

static int ktd2692_parse_dt(struct ktd2692_context *led, struct device *dev,
			    struct ktd2692_led_config_data *cfg)
{
	struct device_node *np = dev->of_node;
	struct device_node *child_node;
	int ret;

	if (!dev->of_node)
		return -ENXIO;

	led->ctrl_gpio = devm_gpiod_get(dev, "ctrl", GPIOD_ASIS);
	if (IS_ERR(led->ctrl_gpio)) {
		ret = PTR_ERR(led->ctrl_gpio);
		dev_err(dev, "cannot get ctrl-gpios %d\n", ret);
		return ret;
	}

	led->aux_gpio = devm_gpiod_get(dev, "aux", GPIOD_ASIS);
	if (IS_ERR(led->aux_gpio)) {
		ret = PTR_ERR(led->aux_gpio);
		dev_err(dev, "cannot get aux-gpios %d\n", ret);
		return ret;
	}

	led->regulator = devm_regulator_get(dev, "vin");
	if (IS_ERR(led->regulator))
		led->regulator = NULL;

	if (led->regulator) {
		ret = regulator_enable(led->regulator);
		if (ret)
			dev_err(dev, "Failed to enable supply: %d\n", ret);
	}

	child_node = of_get_next_available_child(np, NULL);
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
	if (ret)
		dev_err(dev, "failed to parse flash-max-timeout-us\n");

	of_node_put(child_node);
	return ret;
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

	ret = ktd2692_parse_dt(led, &pdev->dev, &led_cfg);
	if (ret)
		return ret;

	ktd2692_init_flash_timeout(fled_cdev, &led_cfg);
	ktd2692_init_movie_current_max(&led_cfg);

	fled_cdev->ops = &flash_ops;

	led_cdev->max_brightness = led_cfg.max_brightness;
	led_cdev->brightness_set = ktd2692_led_brightness_set;
	led_cdev->brightness_set_sync = ktd2692_led_brightness_set_sync;
	led_cdev->flags |= LED_CORE_SUSPENDRESUME | LED_DEV_CAP_FLASH;

	mutex_init(&led->lock);
	INIT_WORK(&led->work_brightness_set, ktd2692_brightness_set_work);

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

static int ktd2692_remove(struct platform_device *pdev)
{
	struct ktd2692_context *led = platform_get_drvdata(pdev);
	int ret;

	led_classdev_flash_unregister(&led->fled_cdev);
	cancel_work_sync(&led->work_brightness_set);

	if (led->regulator) {
		ret = regulator_disable(led->regulator);
		if (ret)
			dev_err(&pdev->dev,
				"Failed to disable supply: %d\n", ret);
	}

	mutex_destroy(&led->lock);

	return 0;
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
	.remove = ktd2692_remove,
};

module_platform_driver(ktd2692_driver);

MODULE_AUTHOR("Ingi Kim <ingi2.kim@samsung.com>");
MODULE_DESCRIPTION("Kinetic KTD2692 LED driver");
MODULE_LICENSE("GPL v2");
