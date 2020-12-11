// SPDX-License-Identifier: GPL-2.0+

#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define A500_EC_LED_DELAY_USEC	(100 * 1000)

enum {
	REG_RESET_LEDS = 0x40,
	REG_POWER_LED_ON = 0x42,
	REG_CHARGE_LED_ON = 0x43,
	REG_ANDROID_LEDS_OFF = 0x5a,
};

struct a500_led {
	struct led_classdev cdev;
	const struct reg_sequence *enable_seq;
	struct a500_led *other;
	struct regmap *rmap;
};

static const struct reg_sequence a500_ec_leds_reset_seq[] = {
	REG_SEQ(REG_RESET_LEDS, 0x0, A500_EC_LED_DELAY_USEC),
	REG_SEQ(REG_ANDROID_LEDS_OFF, 0x0, A500_EC_LED_DELAY_USEC),
};

static const struct reg_sequence a500_ec_white_led_enable_seq[] = {
	REG_SEQ(REG_POWER_LED_ON, 0x0, A500_EC_LED_DELAY_USEC),
};

static const struct reg_sequence a500_ec_orange_led_enable_seq[] = {
	REG_SEQ(REG_CHARGE_LED_ON, 0x0, A500_EC_LED_DELAY_USEC),
};

static int a500_ec_led_brightness_set(struct led_classdev *led_cdev,
				      enum led_brightness value)
{
	struct a500_led *led = container_of(led_cdev, struct a500_led, cdev);
	struct reg_sequence control_seq[2];
	unsigned int num_regs = 1;

	if (value) {
		control_seq[0] = led->enable_seq[0];
	} else {
		/*
		 * There is no separate controls which can disable LEDs
		 * individually, there is only RESET_LEDS command that turns
		 * off both LEDs.
		 *
		 * RESET_LEDS turns off both LEDs, thus restore other LED if
		 * it's turned ON.
		 */
		if (led->other->cdev.brightness)
			num_regs = 2;

		control_seq[0] = a500_ec_leds_reset_seq[0];
		control_seq[1] = led->other->enable_seq[0];
	}

	return regmap_multi_reg_write(led->rmap, control_seq, num_regs);
}

static int a500_ec_leds_probe(struct platform_device *pdev)
{
	struct a500_led *white_led, *orange_led;
	struct regmap *rmap;
	int err;

	rmap = dev_get_regmap(pdev->dev.parent, "KB930");
	if (!rmap)
		return -EINVAL;

	/* reset and turn off LEDs */
	regmap_multi_reg_write(rmap, a500_ec_leds_reset_seq, 2);

	white_led = devm_kzalloc(&pdev->dev, sizeof(*white_led), GFP_KERNEL);
	if (!white_led)
		return -ENOMEM;

	white_led->cdev.name = "power:white";
	white_led->cdev.brightness_set_blocking = a500_ec_led_brightness_set;
	white_led->cdev.flags = LED_CORE_SUSPENDRESUME;
	white_led->cdev.max_brightness = 1;
	white_led->enable_seq = a500_ec_white_led_enable_seq;
	white_led->rmap = rmap;

	orange_led = devm_kzalloc(&pdev->dev, sizeof(*orange_led), GFP_KERNEL);
	if (!orange_led)
		return -ENOMEM;

	orange_led->cdev.name = "power:orange";
	orange_led->cdev.brightness_set_blocking = a500_ec_led_brightness_set;
	orange_led->cdev.flags = LED_CORE_SUSPENDRESUME;
	orange_led->cdev.max_brightness = 1;
	orange_led->enable_seq = a500_ec_orange_led_enable_seq;
	orange_led->rmap = rmap;

	white_led->other = orange_led;
	orange_led->other = white_led;

	err = devm_led_classdev_register(&pdev->dev, &white_led->cdev);
	if (err) {
		dev_err(&pdev->dev, "failed to register white LED\n");
		return err;
	}

	err = devm_led_classdev_register(&pdev->dev, &orange_led->cdev);
	if (err) {
		dev_err(&pdev->dev, "failed to register orange LED\n");
		return err;
	}

	return 0;
}

static struct platform_driver a500_ec_leds_driver = {
	.driver = {
		.name = "acer-a500-iconia-leds",
	},
	.probe = a500_ec_leds_probe,
};
module_platform_driver(a500_ec_leds_driver);

MODULE_DESCRIPTION("LED driver for Acer Iconia Tab A500 Power Button");
MODULE_AUTHOR("Dmitry Osipenko <digetx@gmail.com>");
MODULE_ALIAS("platform:acer-a500-iconia-leds");
MODULE_LICENSE("GPL");
