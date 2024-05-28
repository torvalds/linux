// SPDX-License-Identifier: GPL-2.0-only
/*
 * LED driver for Marvell 88PM860x
 *
 * Copyright (C) 2009 Marvell International Ltd.
 *	Haojian Zhuang <haojian.zhuang@marvell.com>
 */

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/mfd/88pm860x.h>
#include <linux/module.h>

#define LED_PWM_MASK		(0x1F)
#define LED_CURRENT_MASK	(0x07 << 5)

#define LED_BLINK_MASK		(0x7F)

#define LED_ON_CONTINUOUS	(0x0F << 3)

#define LED1_BLINK_EN		(1 << 1)
#define LED2_BLINK_EN		(1 << 2)

struct pm860x_led {
	struct led_classdev cdev;
	struct i2c_client *i2c;
	struct pm860x_chip *chip;
	struct mutex lock;
	char name[MFD_NAME_SIZE];

	int port;
	int iset;
	unsigned char brightness;
	unsigned char current_brightness;

	int reg_control;
	int reg_blink;
	int blink_mask;
};

static int led_power_set(struct pm860x_chip *chip, int port, int on)
{
	int ret = -EINVAL;

	switch (port) {
	case 0:
	case 1:
	case 2:
		ret = on ? pm8606_osc_enable(chip, RGB1_ENABLE) :
			pm8606_osc_disable(chip, RGB1_ENABLE);
		break;
	case 3:
	case 4:
	case 5:
		ret = on ? pm8606_osc_enable(chip, RGB2_ENABLE) :
			pm8606_osc_disable(chip, RGB2_ENABLE);
		break;
	}
	return ret;
}

static int pm860x_led_set(struct led_classdev *cdev,
			   enum led_brightness value)
{
	struct pm860x_led *led = container_of(cdev, struct pm860x_led, cdev);
	struct pm860x_chip *chip;
	unsigned char buf[3];
	int ret;

	chip = led->chip;
	mutex_lock(&led->lock);
	led->brightness = value >> 3;

	if ((led->current_brightness == 0) && led->brightness) {
		led_power_set(chip, led->port, 1);
		if (led->iset) {
			pm860x_set_bits(led->i2c, led->reg_control,
					LED_CURRENT_MASK, led->iset);
		}
		pm860x_set_bits(led->i2c, led->reg_blink,
				LED_BLINK_MASK, LED_ON_CONTINUOUS);
		pm860x_set_bits(led->i2c, PM8606_WLED3B, led->blink_mask,
				led->blink_mask);
	}
	pm860x_set_bits(led->i2c, led->reg_control, LED_PWM_MASK,
			led->brightness);

	if (led->brightness == 0) {
		pm860x_bulk_read(led->i2c, led->reg_control, 3, buf);
		ret = buf[0] & LED_PWM_MASK;
		ret |= buf[1] & LED_PWM_MASK;
		ret |= buf[2] & LED_PWM_MASK;
		if (ret == 0) {
			/* unset current since no led is lighting */
			pm860x_set_bits(led->i2c, led->reg_control,
					LED_CURRENT_MASK, 0);
			pm860x_set_bits(led->i2c, PM8606_WLED3B,
					led->blink_mask, 0);
			led_power_set(chip, led->port, 0);
		}
	}
	led->current_brightness = led->brightness;
	dev_dbg(chip->dev, "Update LED. (reg:%d, brightness:%d)\n",
		led->reg_control, led->brightness);
	mutex_unlock(&led->lock);

	return 0;
}

#ifdef CONFIG_OF
static int pm860x_led_dt_init(struct platform_device *pdev,
			      struct pm860x_led *data)
{
	struct device_node *nproot, *np;
	int iset = 0;

	if (!dev_of_node(pdev->dev.parent))
		return -ENODEV;
	nproot = of_get_child_by_name(dev_of_node(pdev->dev.parent), "leds");
	if (!nproot) {
		dev_err(&pdev->dev, "failed to find leds node\n");
		return -ENODEV;
	}
	for_each_available_child_of_node(nproot, np) {
		if (of_node_name_eq(np, data->name)) {
			of_property_read_u32(np, "marvell,88pm860x-iset",
					     &iset);
			data->iset = PM8606_LED_CURRENT(iset);
			of_node_put(np);
			break;
		}
	}
	of_node_put(nproot);
	return 0;
}
#else
#define pm860x_led_dt_init(x, y)	(-1)
#endif

static int pm860x_led_probe(struct platform_device *pdev)
{
	struct pm860x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct pm860x_led_pdata *pdata = dev_get_platdata(&pdev->dev);
	struct pm860x_led *data;
	struct resource *res;
	int ret = 0;

	data = devm_kzalloc(&pdev->dev, sizeof(struct pm860x_led), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;
	res = platform_get_resource_byname(pdev, IORESOURCE_REG, "control");
	if (!res) {
		dev_err(&pdev->dev, "No REG resource for control\n");
		return -ENXIO;
	}
	data->reg_control = res->start;
	res = platform_get_resource_byname(pdev, IORESOURCE_REG, "blink");
	if (!res) {
		dev_err(&pdev->dev, "No REG resource for blink\n");
		return -ENXIO;
	}
	data->reg_blink = res->start;
	memset(data->name, 0, MFD_NAME_SIZE);
	switch (pdev->id) {
	case 0:
		data->blink_mask = LED1_BLINK_EN;
		sprintf(data->name, "led0-red");
		break;
	case 1:
		data->blink_mask = LED1_BLINK_EN;
		sprintf(data->name, "led0-green");
		break;
	case 2:
		data->blink_mask = LED1_BLINK_EN;
		sprintf(data->name, "led0-blue");
		break;
	case 3:
		data->blink_mask = LED2_BLINK_EN;
		sprintf(data->name, "led1-red");
		break;
	case 4:
		data->blink_mask = LED2_BLINK_EN;
		sprintf(data->name, "led1-green");
		break;
	case 5:
		data->blink_mask = LED2_BLINK_EN;
		sprintf(data->name, "led1-blue");
		break;
	}
	data->chip = chip;
	data->i2c = (chip->id == CHIP_PM8606) ? chip->client : chip->companion;
	data->port = pdev->id;
	if (pm860x_led_dt_init(pdev, data))
		if (pdata)
			data->iset = pdata->iset;

	data->current_brightness = 0;
	data->cdev.name = data->name;
	data->cdev.brightness_set_blocking = pm860x_led_set;
	mutex_init(&data->lock);

	ret = led_classdev_register(chip->dev, &data->cdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register LED: %d\n", ret);
		return ret;
	}
	pm860x_led_set(&data->cdev, 0);

	platform_set_drvdata(pdev, data);

	return 0;
}

static void pm860x_led_remove(struct platform_device *pdev)
{
	struct pm860x_led *data = platform_get_drvdata(pdev);

	led_classdev_unregister(&data->cdev);
}

static struct platform_driver pm860x_led_driver = {
	.driver	= {
		.name	= "88pm860x-led",
	},
	.probe	= pm860x_led_probe,
	.remove_new = pm860x_led_remove,
};

module_platform_driver(pm860x_led_driver);

MODULE_DESCRIPTION("LED driver for Marvell PM860x");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:88pm860x-led");
