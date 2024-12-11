// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for LEDs connected to the Intel Cherry Trail Whiskey Cove PMIC
 *
 * Copyright 2019 Yauhen Kharuzhy <jekhor@gmail.com>
 * Copyright 2023 Hans de Goede <hansg@kernel.org>
 *
 * Register info comes from the Lenovo Yoga Book Android opensource code
 * available from Lenovo. File lenovo_yb1_x90f_l_osc_201803.7z path in the 7z:
 * YB1_source_code/kernel/cht/drivers/misc/charger_gp_led.c
 */

#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/suspend.h>

#define CHT_WC_LED1_CTRL		0x5e1f
#define CHT_WC_LED1_FSM			0x5e20
#define CHT_WC_LED1_PWM			0x5e21

#define CHT_WC_LED2_CTRL		0x4fdf
#define CHT_WC_LED2_FSM			0x4fe0
#define CHT_WC_LED2_PWM			0x4fe1

#define CHT_WC_LED1_SWCTL		BIT(0)		/* HW or SW control of charging led */
#define CHT_WC_LED1_ON			BIT(1)

#define CHT_WC_LED2_ON			BIT(0)
#define CHT_WC_LED_I_MA2_5		(2 << 2)	/* LED current limit */
#define CHT_WC_LED_I_MASK		GENMASK(3, 2)	/* LED current limit mask */

#define CHT_WC_LED_F_1_4_HZ		(0 << 4)
#define CHT_WC_LED_F_1_2_HZ		(1 << 4)
#define CHT_WC_LED_F_1_HZ		(2 << 4)
#define CHT_WC_LED_F_2_HZ		(3 << 4)
#define CHT_WC_LED_F_MASK		GENMASK(5, 4)

#define CHT_WC_LED_EFF_OFF		(0 << 1)
#define CHT_WC_LED_EFF_ON		(1 << 1)
#define CHT_WC_LED_EFF_BLINKING		(2 << 1)
#define CHT_WC_LED_EFF_BREATHING	(3 << 1)
#define CHT_WC_LED_EFF_MASK		GENMASK(2, 1)

#define CHT_WC_LED_COUNT		2

struct cht_wc_led_regs {
	/* Register addresses */
	u16 ctrl;
	u16 fsm;
	u16 pwm;
	/* Mask + values for turning the LED on/off */
	u8 on_off_mask;
	u8 on_val;
	u8 off_val;
};

struct cht_wc_led_saved_regs {
	unsigned int ctrl;
	unsigned int fsm;
	unsigned int pwm;
};

struct cht_wc_led {
	struct led_classdev cdev;
	const struct cht_wc_led_regs *regs;
	struct regmap *regmap;
	struct mutex mutex;
	struct cht_wc_led_saved_regs saved_regs;
};

struct cht_wc_leds {
	struct cht_wc_led leds[CHT_WC_LED_COUNT];
	/* Saved LED1 initial register values */
	struct cht_wc_led_saved_regs led1_initial_regs;
};

static const struct cht_wc_led_regs cht_wc_led_regs[CHT_WC_LED_COUNT] = {
	{
		.ctrl		= CHT_WC_LED1_CTRL,
		.fsm		= CHT_WC_LED1_FSM,
		.pwm		= CHT_WC_LED1_PWM,
		.on_off_mask	= CHT_WC_LED1_SWCTL | CHT_WC_LED1_ON,
		.on_val		= CHT_WC_LED1_SWCTL | CHT_WC_LED1_ON,
		.off_val	= CHT_WC_LED1_SWCTL,
	},
	{
		.ctrl		= CHT_WC_LED2_CTRL,
		.fsm		= CHT_WC_LED2_FSM,
		.pwm		= CHT_WC_LED2_PWM,
		.on_off_mask	= CHT_WC_LED2_ON,
		.on_val		= CHT_WC_LED2_ON,
		.off_val	= 0,
	},
};

static const char * const cht_wc_leds_names[CHT_WC_LED_COUNT] = {
	"platform::" LED_FUNCTION_CHARGING,
	"platform::" LED_FUNCTION_INDICATOR,
};

static int cht_wc_leds_brightness_set(struct led_classdev *cdev,
				      enum led_brightness value)
{
	struct cht_wc_led *led = container_of(cdev, struct cht_wc_led, cdev);
	int ret;

	mutex_lock(&led->mutex);

	if (!value) {
		ret = regmap_update_bits(led->regmap, led->regs->ctrl,
					 led->regs->on_off_mask, led->regs->off_val);
		if (ret < 0) {
			dev_err(cdev->dev, "Failed to turn off: %d\n", ret);
			goto out;
		}

		/* Disable HW blinking */
		ret = regmap_update_bits(led->regmap, led->regs->fsm,
					 CHT_WC_LED_EFF_MASK, CHT_WC_LED_EFF_ON);
		if (ret < 0)
			dev_err(cdev->dev, "Failed to update LED FSM reg: %d\n", ret);
	} else {
		ret = regmap_write(led->regmap, led->regs->pwm, value);
		if (ret < 0) {
			dev_err(cdev->dev, "Failed to set brightness: %d\n", ret);
			goto out;
		}

		ret = regmap_update_bits(led->regmap, led->regs->ctrl,
					 led->regs->on_off_mask, led->regs->on_val);
		if (ret < 0)
			dev_err(cdev->dev, "Failed to turn on: %d\n", ret);
	}
out:
	mutex_unlock(&led->mutex);
	return ret;
}

static enum led_brightness cht_wc_leds_brightness_get(struct led_classdev *cdev)
{
	struct cht_wc_led *led = container_of(cdev, struct cht_wc_led, cdev);
	unsigned int val;
	int ret;

	mutex_lock(&led->mutex);

	ret = regmap_read(led->regmap, led->regs->ctrl, &val);
	if (ret < 0) {
		dev_err(cdev->dev, "Failed to read LED CTRL reg: %d\n", ret);
		ret = 0;
		goto done;
	}

	val &= led->regs->on_off_mask;
	if (val != led->regs->on_val) {
		ret = 0;
		goto done;
	}

	ret = regmap_read(led->regmap, led->regs->pwm, &val);
	if (ret < 0) {
		dev_err(cdev->dev, "Failed to read LED PWM reg: %d\n", ret);
		ret = 0;
		goto done;
	}

	ret = val;
done:
	mutex_unlock(&led->mutex);

	return ret;
}

/* Return blinking period for given CTRL reg value */
static unsigned long cht_wc_leds_get_period(int ctrl)
{
	ctrl &= CHT_WC_LED_F_MASK;

	switch (ctrl) {
	case CHT_WC_LED_F_1_4_HZ:
		return 1000 * 4;
	case CHT_WC_LED_F_1_2_HZ:
		return 1000 * 2;
	case CHT_WC_LED_F_1_HZ:
		return 1000;
	case CHT_WC_LED_F_2_HZ:
		return 1000 / 2;
	}

	return 0;
}

/*
 * Find suitable hardware blink mode for given period.
 * period < 750 ms - select 2 HZ
 * 750 ms <= period < 1500 ms - select 1 HZ
 * 1500 ms <= period < 3000 ms - select 1/2 HZ
 * 3000 ms <= period < 5000 ms - select 1/4 HZ
 * 5000 ms <= period - return -1
 */
static int cht_wc_leds_find_freq(unsigned long period)
{
	if (period < 750)
		return CHT_WC_LED_F_2_HZ;
	else if (period < 1500)
		return CHT_WC_LED_F_1_HZ;
	else if (period < 3000)
		return CHT_WC_LED_F_1_2_HZ;
	else if (period < 5000)
		return CHT_WC_LED_F_1_4_HZ;
	else
		return -1;
}

static int cht_wc_leds_set_effect(struct led_classdev *cdev,
				  unsigned long *delay_on,
				  unsigned long *delay_off,
				  u8 effect)
{
	struct cht_wc_led *led = container_of(cdev, struct cht_wc_led, cdev);
	int ctrl, ret;

	mutex_lock(&led->mutex);

	/* Blink with 1 Hz as default if nothing specified */
	if (!*delay_on && !*delay_off)
		*delay_on = *delay_off = 500;

	ctrl = cht_wc_leds_find_freq(*delay_on + *delay_off);
	if (ctrl < 0) {
		/* Disable HW blinking */
		ret = regmap_update_bits(led->regmap, led->regs->fsm,
					 CHT_WC_LED_EFF_MASK, CHT_WC_LED_EFF_ON);
		if (ret < 0)
			dev_err(cdev->dev, "Failed to update LED FSM reg: %d\n", ret);

		/* Fallback to software timer */
		*delay_on = *delay_off = 0;
		ret = -EINVAL;
		goto done;
	}

	ret = regmap_update_bits(led->regmap, led->regs->fsm,
				 CHT_WC_LED_EFF_MASK, effect);
	if (ret < 0)
		dev_err(cdev->dev, "Failed to update LED FSM reg: %d\n", ret);

	/* Set the frequency and make sure the LED is on */
	ret = regmap_update_bits(led->regmap, led->regs->ctrl,
				 CHT_WC_LED_F_MASK | led->regs->on_off_mask,
				 ctrl | led->regs->on_val);
	if (ret < 0)
		dev_err(cdev->dev, "Failed to update LED CTRL reg: %d\n", ret);

	*delay_off = *delay_on = cht_wc_leds_get_period(ctrl) / 2;

done:
	mutex_unlock(&led->mutex);

	return ret;
}

static int cht_wc_leds_blink_set(struct led_classdev *cdev,
				 unsigned long *delay_on,
				 unsigned long *delay_off)
{
	u8 effect = CHT_WC_LED_EFF_BLINKING;

	/*
	 * The desired default behavior of LED1 / the charge LED is breathing
	 * while charging and on/solid when full. Since triggers cannot select
	 * breathing, blink_set() gets called when charging. Use slow breathing
	 * when the default "charging-blink-full-solid" trigger is used to
	 * achieve the desired default behavior.
	 */
	if (cdev->flags & LED_INIT_DEFAULT_TRIGGER) {
		*delay_on = *delay_off = 1000;
		effect = CHT_WC_LED_EFF_BREATHING;
	}

	return cht_wc_leds_set_effect(cdev, delay_on, delay_off, effect);
}

static int cht_wc_leds_pattern_set(struct led_classdev *cdev,
				   struct led_pattern *pattern,
				   u32 len, int repeat)
{
	unsigned long delay_off, delay_on;

	if (repeat > 0 || len != 2 ||
	    pattern[0].brightness != 0 || pattern[1].brightness != 1 ||
	    pattern[0].delta_t != pattern[1].delta_t ||
	    (pattern[0].delta_t != 250 && pattern[0].delta_t != 500 &&
	     pattern[0].delta_t != 1000 && pattern[0].delta_t != 2000))
		return -EINVAL;

	delay_off = pattern[0].delta_t;
	delay_on  = pattern[1].delta_t;

	return cht_wc_leds_set_effect(cdev, &delay_on, &delay_off, CHT_WC_LED_EFF_BREATHING);
}

static int cht_wc_leds_pattern_clear(struct led_classdev *cdev)
{
	return cht_wc_leds_brightness_set(cdev, 0);
}

static int cht_wc_led_save_regs(struct cht_wc_led *led,
				struct cht_wc_led_saved_regs *saved_regs)
{
	int ret;

	ret = regmap_read(led->regmap, led->regs->ctrl, &saved_regs->ctrl);
	if (ret < 0)
		return ret;

	ret = regmap_read(led->regmap, led->regs->fsm, &saved_regs->fsm);
	if (ret < 0)
		return ret;

	return regmap_read(led->regmap, led->regs->pwm, &saved_regs->pwm);
}

static void cht_wc_led_restore_regs(struct cht_wc_led *led,
				    const struct cht_wc_led_saved_regs *saved_regs)
{
	regmap_write(led->regmap, led->regs->ctrl, saved_regs->ctrl);
	regmap_write(led->regmap, led->regs->fsm, saved_regs->fsm);
	regmap_write(led->regmap, led->regs->pwm, saved_regs->pwm);
}

static int cht_wc_leds_probe(struct platform_device *pdev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(pdev->dev.parent);
	struct cht_wc_leds *leds;
	int ret;
	int i;

	/*
	 * On the Lenovo Yoga Tab 3 the LED1 driver output is actually
	 * connected to a haptic feedback motor rather then a LED.
	 * So do not register a LED classdev there (LED2 is unused).
	 */
	if (pmic->cht_wc_model == INTEL_CHT_WC_LENOVO_YT3_X90)
		return -ENODEV;

	leds = devm_kzalloc(&pdev->dev, sizeof(*leds), GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	/*
	 * LED1 might be in hw-controlled mode when this driver gets loaded; and
	 * since the PMIC is always powered by the battery any changes made are
	 * permanent. Save LED1 regs to restore them on remove() or shutdown().
	 */
	leds->leds[0].regs = &cht_wc_led_regs[0];
	leds->leds[0].regmap = pmic->regmap;
	ret = cht_wc_led_save_regs(&leds->leds[0], &leds->led1_initial_regs);
	if (ret < 0)
		return ret;

	/* Set LED1 default trigger based on machine model */
	switch (pmic->cht_wc_model) {
	case INTEL_CHT_WC_GPD_WIN_POCKET:
		leds->leds[0].cdev.default_trigger = "max170xx_battery-charging-blink-full-solid";
		break;
	case INTEL_CHT_WC_XIAOMI_MIPAD2:
		leds->leds[0].cdev.default_trigger = "bq27520-0-charging-blink-full-solid";
		break;
	case INTEL_CHT_WC_LENOVO_YOGABOOK1:
		leds->leds[0].cdev.default_trigger = "bq27542-0-charging-blink-full-solid";
		break;
	default:
		dev_warn(&pdev->dev, "Unknown model, no default charging trigger\n");
		break;
	}

	for (i = 0; i < CHT_WC_LED_COUNT; i++) {
		struct cht_wc_led *led = &leds->leds[i];

		led->regs = &cht_wc_led_regs[i];
		led->regmap = pmic->regmap;
		mutex_init(&led->mutex);
		led->cdev.name = cht_wc_leds_names[i];
		led->cdev.brightness_set_blocking = cht_wc_leds_brightness_set;
		led->cdev.brightness_get = cht_wc_leds_brightness_get;
		led->cdev.blink_set = cht_wc_leds_blink_set;
		led->cdev.pattern_set = cht_wc_leds_pattern_set;
		led->cdev.pattern_clear = cht_wc_leds_pattern_clear;
		led->cdev.max_brightness = 255;

		ret = led_classdev_register(&pdev->dev, &led->cdev);
		if (ret < 0)
			return ret;
	}

	platform_set_drvdata(pdev, leds);
	return 0;
}

static void cht_wc_leds_remove(struct platform_device *pdev)
{
	struct cht_wc_leds *leds = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < CHT_WC_LED_COUNT; i++)
		led_classdev_unregister(&leds->leds[i].cdev);

	/* Restore LED1 regs if hw-control was active else leave LED1 off */
	if (!(leds->led1_initial_regs.ctrl & CHT_WC_LED1_SWCTL))
		cht_wc_led_restore_regs(&leds->leds[0], &leds->led1_initial_regs);
}

static void cht_wc_leds_disable(struct platform_device *pdev)
{
	struct cht_wc_leds *leds = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < CHT_WC_LED_COUNT; i++)
		cht_wc_leds_brightness_set(&leds->leds[i].cdev, 0);

	/* Restore LED1 regs if hw-control was active else leave LED1 off */
	if (!(leds->led1_initial_regs.ctrl & CHT_WC_LED1_SWCTL))
		cht_wc_led_restore_regs(&leds->leds[0], &leds->led1_initial_regs);
}

/* On suspend save current settings and turn LEDs off */
static int cht_wc_leds_suspend(struct device *dev)
{
	struct cht_wc_leds *leds = dev_get_drvdata(dev);
	int i, ret;

	for (i = 0; i < CHT_WC_LED_COUNT; i++) {
		ret = cht_wc_led_save_regs(&leds->leds[i], &leds->leds[i].saved_regs);
		if (ret < 0)
			return ret;
	}

	cht_wc_leds_disable(to_platform_device(dev));
	return 0;
}

/* On resume restore the saved settings */
static int cht_wc_leds_resume(struct device *dev)
{
	struct cht_wc_leds *leds = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < CHT_WC_LED_COUNT; i++)
		cht_wc_led_restore_regs(&leds->leds[i], &leds->leds[i].saved_regs);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(cht_wc_leds_pm, cht_wc_leds_suspend, cht_wc_leds_resume);

static struct platform_driver cht_wc_leds_driver = {
	.probe = cht_wc_leds_probe,
	.remove = cht_wc_leds_remove,
	.shutdown = cht_wc_leds_disable,
	.driver = {
		.name = "cht_wcove_leds",
		.pm = pm_sleep_ptr(&cht_wc_leds_pm),
	},
};
module_platform_driver(cht_wc_leds_driver);

MODULE_ALIAS("platform:cht_wcove_leds");
MODULE_DESCRIPTION("Intel Cherry Trail Whiskey Cove PMIC LEDs driver");
MODULE_AUTHOR("Yauhen Kharuzhy <jekhor@gmail.com>");
MODULE_LICENSE("GPL");
