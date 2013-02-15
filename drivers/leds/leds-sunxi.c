/* driver/misc/leds-sunxi.c
 *
 *  LED driver for sunxi platform (modified generic leds-gpio driver)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <plat/sys_config.h>
#include <linux/workqueue.h>

struct sunxi_gpio_data {
	unsigned gpio_handler;
	script_gpio_set_t info;
	char pin_name[16];
	char led_name[64];
};

struct gpio_led_data {
	struct led_classdev cdev;
	unsigned gpio;
	struct work_struct work;
	u8 new_level;
	u8 can_sleep;
	u8 active_low;
	u8 blinking;
	int (*platform_gpio_blink_set)(unsigned gpio, int state,
			unsigned long *delay_on, unsigned long *delay_off);
};

struct sunxi_leds_priv {
	int num_leds;
	struct gpio_led_data leds[];
};

static int sunxi_leds_num;
static struct gpio_led *sunxi_leds;
static struct sunxi_gpio_data *psunxi_leds;
static struct gpio_led_platform_data gpio_led_data;
struct platform_device *pdev;

/* Get gpio pin value */
int sunxi_gpio_get_value(unsigned gpio)
{
	int  ret;
	user_gpio_set_t gpio_info[1];

	if (gpio >= sunxi_leds_num)
		return -1;

	ret = gpio_get_one_pin_status(psunxi_leds[gpio].gpio_handler,
				gpio_info, psunxi_leds[gpio].pin_name, 1);

	return gpio_info->data;
}

/* Set pin value (output mode) */
void sunxi_gpio_set_value(unsigned gpio, int value)
{
	int ret ;

	if (gpio >= sunxi_leds_num)
		return;

	ret = gpio_write_one_pin_value(psunxi_leds[gpio].gpio_handler,
					value, psunxi_leds[gpio].pin_name);

	return;
}

/* Set pin direction -> out (mul_sel = 1), pin_data -> value */
static int sunxi_direction_output(unsigned gpio, int value)
{
	int ret;

	if (gpio >= sunxi_leds_num)
		return -1;

	ret =  gpio_set_one_pin_io_status(psunxi_leds[gpio].gpio_handler, 1,
					psunxi_leds[gpio].pin_name);
	if (!ret)
		ret = gpio_write_one_pin_value(psunxi_leds[gpio].gpio_handler,
					value, psunxi_leds[gpio].pin_name);

	return ret;
}

/* Check if gpio num requested and valid */
static int sunxi_gpio_is_valid(unsigned gpio)
{
	if (gpio >= sunxi_leds_num)
		return 0;

	if (psunxi_leds[gpio].gpio_handler)
		return 1;

	return 0;
}

/* Unregister led device */
static void delete_sunxi_led(struct gpio_led_data *led)
{
	if (!sunxi_gpio_is_valid(led->gpio))
		return;

	led_classdev_unregister(&led->cdev);
	cancel_work_sync(&led->work);
}

/* Unregister & remove all leds */
static int __devexit sunxi_led_remove(struct platform_device *pdev)
{
	struct sunxi_leds_priv *priv = dev_get_drvdata(&pdev->dev);
	int i;

	for (i = 0; i < priv->num_leds; i++)
		delete_sunxi_led(&priv->leds[i]);

	dev_set_drvdata(&pdev->dev, NULL);
	kfree(priv);

	return 0;
}

static inline int sizeof_sunxi_leds_priv(int num_leds)
{
	return sizeof(struct sunxi_leds_priv) +
		(sizeof(struct gpio_led_data) * num_leds);
}

static void sunxi_led_work(struct work_struct *work)
{
	struct gpio_led_data	*led_dat =
		container_of(work, struct gpio_led_data, work);

	if (led_dat->blinking) {
		led_dat->platform_gpio_blink_set(led_dat->gpio,
						 led_dat->new_level,
						 NULL, NULL);
		led_dat->blinking = 0;
	} else
		sunxi_gpio_set_value(led_dat->gpio, led_dat->new_level);
}

static void sunxi_led_set(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	struct gpio_led_data *led_dat =
		container_of(led_cdev, struct gpio_led_data, cdev);
	int level;

	if (value == LED_OFF)
		level = 0;
	else
		level = 1;

	if (led_dat->active_low)
		level = !level;

	if (led_dat->can_sleep) {
		led_dat->new_level = level;
		schedule_work(&led_dat->work);
	} else {
		if (led_dat->blinking) {
			led_dat->platform_gpio_blink_set(led_dat->gpio, level,
							 NULL, NULL);
			led_dat->blinking = 0;
		} else
			sunxi_gpio_set_value(led_dat->gpio, level);
	}
}

static int sunxi_blink_set(struct led_classdev *led_cdev,
	unsigned long *delay_on, unsigned long *delay_off)
{
	struct gpio_led_data *led_dat =
		container_of(led_cdev, struct gpio_led_data, cdev);

	led_dat->blinking = 1;
	return led_dat->platform_gpio_blink_set(led_dat->gpio, GPIO_LED_BLINK,
						delay_on, delay_off);
}

static int __devinit create_sunxi_led(const struct gpio_led *template,
	struct gpio_led_data *led_dat, struct device *parent,
	int (*blink_set)(unsigned, int, unsigned long *, unsigned long *))
{
	int ret = 0;
	int state = 0;

	led_dat->gpio = -1;

	/* skip leds that aren't available */
	if (!sunxi_gpio_is_valid(template->gpio)) {
		printk(KERN_INFO "Skipping unavailable LED gpio %d (%s)\n",
				template->gpio, template->name);
		return 0;
	}

	led_dat->cdev.name = template->name;
	led_dat->cdev.default_trigger = template->default_trigger;
	led_dat->gpio = template->gpio;
	led_dat->can_sleep = 0;
	led_dat->active_low = template->active_low;
	led_dat->blinking = 0;
	if (blink_set) {
		led_dat->platform_gpio_blink_set = blink_set;
		led_dat->cdev.blink_set = sunxi_blink_set;
	}
	led_dat->cdev.brightness_set = sunxi_led_set;
	if (template->default_state == LEDS_GPIO_DEFSTATE_KEEP) {
		state = sunxi_gpio_get_value(led_dat->gpio);
		if (led_dat->active_low)
			state = !state;
	} else
		state = (template->default_state == LEDS_GPIO_DEFSTATE_ON);
	led_dat->cdev.brightness = state ? LED_FULL : LED_OFF;
	if (!template->retain_state_suspended)
		led_dat->cdev.flags |= LED_CORE_SUSPENDRESUME;

	ret = sunxi_direction_output(led_dat->gpio,
					led_dat->active_low ^ state);
	if (ret < 0)
		goto err;

	INIT_WORK(&led_dat->work, sunxi_led_work);

	ret = led_classdev_register(parent, &led_dat->cdev);
	if (ret < 0)
		goto err;

	return 0;

err:
	return ret;
}

static int __devinit sunxi_led_probe(struct platform_device *pdev)
{
	struct gpio_led_platform_data *pdata = pdev->dev.platform_data;
	struct sunxi_leds_priv *priv;
	int i, ret = 0;

	if (pdata && pdata->num_leds) {
		priv = kzalloc(sizeof_sunxi_leds_priv(pdata->num_leds),
				GFP_KERNEL);
		if (!priv)
			return -ENOMEM;

		priv->num_leds = pdata->num_leds;
		for (i = 0; i < priv->num_leds; i++) {
			ret = create_sunxi_led(&pdata->leds[i],
					      &priv->leds[i],
					      &pdev->dev,
					      pdata->gpio_blink_set);

			if (ret < 0) {
				/* On failure: unwind the led creations */
				for (i = i - 1; i >= 0; i--)
					delete_sunxi_led(&priv->leds[i]);

				kfree(priv);
				return ret;
			}
		}
	} else {
		return -ENODEV;
	}

	platform_set_drvdata(pdev, priv);
	return 0;
}

static struct platform_driver sunxi_led_driver = {
	.probe		= sunxi_led_probe,
	.remove		= __devexit_p(sunxi_led_remove),
	.driver		= {
		.name	= "leds-sunxi",
		.owner	= THIS_MODULE,
	},
};

static int __init sunxi_leds_init(void)
{
	int i, err, value;
	int sunxi_leds_used = 0;
	struct sunxi_gpio_data *leds_i;
	struct gpio_led *dleds_i;
	char key[20];

	/* parse script.bin for [leds_para] section
	   leds_used/leds_num/leds_pin_x/leds_name_x */

	pr_info("sunxi_leds driver init\n");
	err = script_parser_fetch("leds_para", "leds_used", &sunxi_leds_used,
					sizeof(sunxi_leds_used)/sizeof(int));
	if (err) {
		/* Not error - just info */
		pr_info("sunxi leds can't find script data '[leds_para]' 'leds_used'\n");
		return err;
	}

	if (!sunxi_leds_used) {
		pr_info("%s leds_used is false. Skip leds initialization\n",
			__func__);
		err = 0;
		return err;
	}

	err = script_parser_fetch("leds_para", "leds_num", &sunxi_leds_num,
					sizeof(sunxi_leds_num)/sizeof(int));
	if (err) {
		pr_err("%s script_parser_fetch '[leds_para]' 'leds_num' error\n",
			__func__);
		return err;
	}

	if (!sunxi_leds_num) {
		pr_info("%s sunxi_leds_num is none. Skip leds initialization\n",
			__func__);
		err = 0;
		return err;
	}

	/* allocate memory for leds gpio data and platform device data */
	psunxi_leds = kzalloc(sizeof(struct sunxi_gpio_data) * sunxi_leds_num,
				GFP_KERNEL);

	sunxi_leds = kzalloc(sizeof(struct gpio_led) * sunxi_leds_num,
				GFP_KERNEL);

	if ((!psunxi_leds) || (!sunxi_leds)) {
		pr_err("%s kzalloc failed\n", __func__);
		err = -ENOMEM;
		goto exit;
	}

	leds_i = psunxi_leds;
	dleds_i = sunxi_leds;

	/* parse leds gpio/name script data */
	for (i = 0; i < sunxi_leds_num; i++) {

		/* make next script entry name */
		sprintf(leds_i->pin_name, "leds_pin_%d", i+1);

		/* fetch next led name */
		sprintf(key, "leds_name_%d", i + 1);
		err = script_parser_fetch("leds_para", key,
					  (int *)leds_i->led_name,
					  sizeof(leds_i->led_name)/sizeof(int));
		if (err) {
			pr_err("%s script_parser_fetch '[leds_para]' '%s' error\n",
				__func__, key);
			goto exit;
		}

		/* fetch next led gpio information */
		sprintf(key, "leds_pin_%d", i + 1);
		err = script_parser_fetch("leds_para", key,
					(int *)&leds_i->info,
					sizeof(script_gpio_set_t));

		if (err) {
			pr_err("%s script_parser_fetch '[leds_para]' '%s' error\n",
				__func__, key);
			break;
		}

		/* reserve gpio for led */
		leds_i->gpio_handler = gpio_request_ex("leds_para", key);
		if (!leds_i->gpio_handler) {
			pr_err("%s can't request '[leds_para]' '%s', already used ?",
				__func__, key);
			break;
		}

		/* Fill gpio_led structure */
		dleds_i->name = leds_i->led_name;
		dleds_i->default_state = LEDS_GPIO_DEFSTATE_OFF;
		dleds_i->gpio = i;
		dleds_i->default_trigger = "none";

		sprintf(key, "leds_default_%d", i + 1); value = 0;
		err = script_parser_fetch("leds_para", key, &value, 1);
		if (err == 0)
			dleds_i->default_state = value;

		sprintf(key, "leds_inverted_%d", i + 1); value = 0;
		err = script_parser_fetch("leds_para", key, &value, 1);
		if (err == 0 && value)
			dleds_i->active_low = 1;

		leds_i++;
		dleds_i++;
	}

	gpio_led_data.num_leds = sunxi_leds_num;
	gpio_led_data.leds = sunxi_leds;
	gpio_led_data.gpio_blink_set = NULL;

	pdev = platform_device_alloc("leds-sunxi", -1);
	if (!pdev)
		goto exit;

	err = platform_device_add_data(pdev, &gpio_led_data,
					sizeof(gpio_led_data));
	if (err)
		goto exit;

	err = platform_device_add(pdev);
	if (err)
		goto exit;

	return platform_driver_register(&sunxi_led_driver);

exit:
	if (err != -ENOMEM) {

		for (i = 0; i < sunxi_leds_num; i++) {
			if (psunxi_leds[i].gpio_handler)
				gpio_release(psunxi_leds[i].gpio_handler, 1);
		}

		kfree(psunxi_leds);
		kfree(sunxi_leds);
		return err;
	}

	return err;
}

static void __exit sunxi_leds_exit(void)
{
	int i;
	pr_info("sunxi_leds driver unload\n");

	/* unregister driver */
	platform_driver_unregister(&sunxi_led_driver);

	/* remove platform device */
	platform_device_unregister(pdev);

	for (i = 0; i < sunxi_leds_num; i++) {
		if (psunxi_leds[i].gpio_handler)
			gpio_release(psunxi_leds[i].gpio_handler, 1);
	}

	/* free allocated memory */
	kfree(psunxi_leds);
	kfree(sunxi_leds);
}

module_init(sunxi_leds_init);
module_exit(sunxi_leds_exit);

MODULE_ALIAS("platform:leds-sunxi");
MODULE_DESCRIPTION("sunxi led driver");
MODULE_AUTHOR("Alexandr V. Shutko <alex@shutko.ru>");
MODULE_LICENSE("GPL");
