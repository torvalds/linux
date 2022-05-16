// SPDX-License-Identifier: GPL-2.0-only
/*
 * Backlight driver for the Kinetic KTD253
 * Based on code and know-how from the Samsung GT-S7710
 * Gareth Phillips <gareth.phillips@samsung.com>
 */
#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>

/* Current ratio is n/32 from 1/32 to 32/32 */
#define KTD253_MIN_RATIO 1
#define KTD253_MAX_RATIO 32
#define KTD253_DEFAULT_RATIO 13

#define KTD253_T_LOW_NS (200 + 10) /* Additional 10ns as safety factor */
#define KTD253_T_HIGH_NS (200 + 10) /* Additional 10ns as safety factor */
#define KTD253_T_OFF_MS 3

struct ktd253_backlight {
	struct device *dev;
	struct backlight_device *bl;
	struct gpio_desc *gpiod;
	u16 ratio;
};

static int ktd253_backlight_update_status(struct backlight_device *bl)
{
	struct ktd253_backlight *ktd253 = bl_get_data(bl);
	int brightness = backlight_get_brightness(bl);
	u16 target_ratio;
	u16 current_ratio = ktd253->ratio;
	unsigned long flags;

	dev_dbg(ktd253->dev, "new brightness/ratio: %d/32\n", brightness);

	target_ratio = brightness;

	if (target_ratio == current_ratio)
		/* This is already right */
		return 0;

	if (target_ratio == 0) {
		gpiod_set_value_cansleep(ktd253->gpiod, 0);
		/*
		 * We need to keep the GPIO low for at least this long
		 * to actually switch the KTD253 off.
		 */
		msleep(KTD253_T_OFF_MS);
		ktd253->ratio = 0;
		return 0;
	}

	if (current_ratio == 0) {
		gpiod_set_value_cansleep(ktd253->gpiod, 1);
		ndelay(KTD253_T_HIGH_NS);
		/* We always fall back to this when we power on */
		current_ratio = KTD253_MAX_RATIO;
	}

	/*
	 * WARNING:
	 * The loop to set the correct current level is performed
	 * with interrupts disabled as it is timing critical.
	 * The maximum number of cycles of the loop is 32
	 * so the time taken will be (T_LOW_NS + T_HIGH_NS + loop_time) * 32,
	 */
	local_irq_save(flags);
	while (current_ratio != target_ratio) {
		/*
		 * These GPIO operations absolutely can NOT sleep so no
		 * _cansleep suffixes, and no using GPIO expanders on
		 * slow buses for this!
		 */
		gpiod_set_value(ktd253->gpiod, 0);
		ndelay(KTD253_T_LOW_NS);
		gpiod_set_value(ktd253->gpiod, 1);
		ndelay(KTD253_T_HIGH_NS);
		/* After 1/32 we loop back to 32/32 */
		if (current_ratio == KTD253_MIN_RATIO)
			current_ratio = KTD253_MAX_RATIO;
		else
			current_ratio--;
	}
	local_irq_restore(flags);
	ktd253->ratio = current_ratio;

	dev_dbg(ktd253->dev, "new ratio set to %d/32\n", target_ratio);

	return 0;
}

static const struct backlight_ops ktd253_backlight_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= ktd253_backlight_update_status,
};

static int ktd253_backlight_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct backlight_device *bl;
	struct ktd253_backlight *ktd253;
	u32 max_brightness;
	u32 brightness;
	int ret;

	ktd253 = devm_kzalloc(dev, sizeof(*ktd253), GFP_KERNEL);
	if (!ktd253)
		return -ENOMEM;
	ktd253->dev = dev;

	ret = device_property_read_u32(dev, "max-brightness", &max_brightness);
	if (ret)
		max_brightness = KTD253_MAX_RATIO;
	if (max_brightness > KTD253_MAX_RATIO) {
		/* Clamp brightness to hardware max */
		dev_err(dev, "illegal max brightness specified\n");
		max_brightness = KTD253_MAX_RATIO;
	}

	ret = device_property_read_u32(dev, "default-brightness", &brightness);
	if (ret)
		brightness = KTD253_DEFAULT_RATIO;
	if (brightness > max_brightness) {
		/* Clamp default brightness to max brightness */
		dev_err(dev, "default brightness exceeds max brightness\n");
		brightness = max_brightness;
	}

	if (brightness)
		/* This will be the default ratio when the KTD253 is enabled */
		ktd253->ratio = KTD253_MAX_RATIO;
	else
		ktd253->ratio = 0;

	ktd253->gpiod = devm_gpiod_get(dev, "enable",
				       brightness ? GPIOD_OUT_HIGH :
				       GPIOD_OUT_LOW);
	if (IS_ERR(ktd253->gpiod)) {
		ret = PTR_ERR(ktd253->gpiod);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "gpio line missing or invalid.\n");
		return ret;
	}
	gpiod_set_consumer_name(ktd253->gpiod, dev_name(dev));

	bl = devm_backlight_device_register(dev, dev_name(dev), dev, ktd253,
					    &ktd253_backlight_ops, NULL);
	if (IS_ERR(bl)) {
		dev_err(dev, "failed to register backlight\n");
		return PTR_ERR(bl);
	}
	bl->props.max_brightness = max_brightness;
	/* When we just enable the GPIO line we set max brightness */
	if (brightness) {
		bl->props.brightness = brightness;
		bl->props.power = FB_BLANK_UNBLANK;
	} else {
		bl->props.brightness = 0;
		bl->props.power = FB_BLANK_POWERDOWN;
	}

	ktd253->bl = bl;
	platform_set_drvdata(pdev, bl);
	backlight_update_status(bl);

	return 0;
}

static const struct of_device_id ktd253_backlight_of_match[] = {
	{ .compatible = "kinetic,ktd253" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ktd253_backlight_of_match);

static struct platform_driver ktd253_backlight_driver = {
	.driver = {
		.name = "ktd253-backlight",
		.of_match_table = ktd253_backlight_of_match,
	},
	.probe		= ktd253_backlight_probe,
};
module_platform_driver(ktd253_backlight_driver);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("Kinetic KTD253 Backlight Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ktd253-backlight");
