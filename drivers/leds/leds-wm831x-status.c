/*
 * LED driver for WM831x status LEDs
 *
 * Copyright(C) 2009 Wolfson Microelectronics PLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/pdata.h>
#include <linux/mfd/wm831x/status.h>
#include <linux/module.h>


struct wm831x_status {
	struct led_classdev cdev;
	struct wm831x *wm831x;
	struct work_struct work;
	struct mutex mutex;

	spinlock_t value_lock;
	int reg;     /* Control register */
	int reg_val; /* Control register value */

	int blink;
	int blink_time;
	int blink_cyc;
	int src;
	enum led_brightness brightness;
};

#define to_wm831x_status(led_cdev) \
	container_of(led_cdev, struct wm831x_status, cdev)

static void wm831x_status_work(struct work_struct *work)
{
	struct wm831x_status *led = container_of(work, struct wm831x_status,
						 work);
	unsigned long flags;

	mutex_lock(&led->mutex);

	led->reg_val &= ~(WM831X_LED_SRC_MASK | WM831X_LED_MODE_MASK |
			  WM831X_LED_DUTY_CYC_MASK | WM831X_LED_DUR_MASK);

	spin_lock_irqsave(&led->value_lock, flags);

	led->reg_val |= led->src << WM831X_LED_SRC_SHIFT;
	if (led->blink) {
		led->reg_val |= 2 << WM831X_LED_MODE_SHIFT;
		led->reg_val |= led->blink_time << WM831X_LED_DUR_SHIFT;
		led->reg_val |= led->blink_cyc;
	} else {
		if (led->brightness != LED_OFF)
			led->reg_val |= 1 << WM831X_LED_MODE_SHIFT;
	}

	spin_unlock_irqrestore(&led->value_lock, flags);

	wm831x_reg_write(led->wm831x, led->reg, led->reg_val);

	mutex_unlock(&led->mutex);
}

static void wm831x_status_set(struct led_classdev *led_cdev,
			   enum led_brightness value)
{
	struct wm831x_status *led = to_wm831x_status(led_cdev);
	unsigned long flags;

	spin_lock_irqsave(&led->value_lock, flags);
	led->brightness = value;
	if (value == LED_OFF)
		led->blink = 0;
	schedule_work(&led->work);
	spin_unlock_irqrestore(&led->value_lock, flags);
}

static int wm831x_status_blink_set(struct led_classdev *led_cdev,
				   unsigned long *delay_on,
				   unsigned long *delay_off)
{
	struct wm831x_status *led = to_wm831x_status(led_cdev);
	unsigned long flags;
	int ret = 0;

	/* Pick some defaults if we've not been given times */
	if (*delay_on == 0 && *delay_off == 0) {
		*delay_on = 250;
		*delay_off = 250;
	}

	spin_lock_irqsave(&led->value_lock, flags);

	/* We only have a limited selection of settings, see if we can
	 * support the configuration we're being given */
	switch (*delay_on) {
	case 1000:
		led->blink_time = 0;
		break;
	case 250:
		led->blink_time = 1;
		break;
	case 125:
		led->blink_time = 2;
		break;
	case 62:
	case 63:
		/* Actually 62.5ms */
		led->blink_time = 3;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret == 0) {
		switch (*delay_off / *delay_on) {
		case 1:
			led->blink_cyc = 0;
			break;
		case 3:
			led->blink_cyc = 1;
			break;
		case 4:
			led->blink_cyc = 2;
			break;
		case 8:
			led->blink_cyc = 3;
			break;
		default:
			ret = -EINVAL;
			break;
		}
	}

	if (ret == 0)
		led->blink = 1;
	else
		led->blink = 0;

	/* Always update; if we fail turn off blinking since we expect
	 * a software fallback. */
	schedule_work(&led->work);

	spin_unlock_irqrestore(&led->value_lock, flags);

	return ret;
}

static const char * const led_src_texts[] = {
	"otp",
	"power",
	"charger",
	"soft",
};

static ssize_t wm831x_status_src_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct wm831x_status *led = to_wm831x_status(led_cdev);
	int i;
	ssize_t ret = 0;

	mutex_lock(&led->mutex);

	for (i = 0; i < ARRAY_SIZE(led_src_texts); i++)
		if (i == led->src)
			ret += sprintf(&buf[ret], "[%s] ", led_src_texts[i]);
		else
			ret += sprintf(&buf[ret], "%s ", led_src_texts[i]);

	mutex_unlock(&led->mutex);

	ret += sprintf(&buf[ret], "\n");

	return ret;
}

static ssize_t wm831x_status_src_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct wm831x_status *led = to_wm831x_status(led_cdev);
	char name[20];
	int i;
	size_t len;

	name[sizeof(name) - 1] = '\0';
	strncpy(name, buf, sizeof(name) - 1);
	len = strlen(name);

	if (len && name[len - 1] == '\n')
		name[len - 1] = '\0';

	for (i = 0; i < ARRAY_SIZE(led_src_texts); i++) {
		if (!strcmp(name, led_src_texts[i])) {
			mutex_lock(&led->mutex);

			led->src = i;
			schedule_work(&led->work);

			mutex_unlock(&led->mutex);
		}
	}

	return size;
}

static DEVICE_ATTR(src, 0644, wm831x_status_src_show, wm831x_status_src_store);

static int wm831x_status_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);
	struct wm831x_pdata *chip_pdata;
	struct wm831x_status_pdata pdata;
	struct wm831x_status *drvdata;
	struct resource *res;
	int id = pdev->id % ARRAY_SIZE(chip_pdata->status);
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_REG, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "No register resource\n");
		ret = -EINVAL;
		goto err;
	}

	drvdata = devm_kzalloc(&pdev->dev, sizeof(struct wm831x_status),
			       GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;
	dev_set_drvdata(&pdev->dev, drvdata);

	drvdata->wm831x = wm831x;
	drvdata->reg = res->start;

	if (wm831x->dev->platform_data)
		chip_pdata = wm831x->dev->platform_data;
	else
		chip_pdata = NULL;

	memset(&pdata, 0, sizeof(pdata));
	if (chip_pdata && chip_pdata->status[id])
		memcpy(&pdata, chip_pdata->status[id], sizeof(pdata));
	else
		pdata.name = dev_name(&pdev->dev);

	mutex_init(&drvdata->mutex);
	INIT_WORK(&drvdata->work, wm831x_status_work);
	spin_lock_init(&drvdata->value_lock);

	/* We cache the configuration register and read startup values
	 * from it. */
	drvdata->reg_val = wm831x_reg_read(wm831x, drvdata->reg);

	if (drvdata->reg_val & WM831X_LED_MODE_MASK)
		drvdata->brightness = LED_FULL;
	else
		drvdata->brightness = LED_OFF;

	/* Set a default source if configured, otherwise leave the
	 * current hardware setting.
	 */
	if (pdata.default_src == WM831X_STATUS_PRESERVE) {
		drvdata->src = drvdata->reg_val;
		drvdata->src &= WM831X_LED_SRC_MASK;
		drvdata->src >>= WM831X_LED_SRC_SHIFT;
	} else {
		drvdata->src = pdata.default_src - 1;
	}

	drvdata->cdev.name = pdata.name;
	drvdata->cdev.default_trigger = pdata.default_trigger;
	drvdata->cdev.brightness_set = wm831x_status_set;
	drvdata->cdev.blink_set = wm831x_status_blink_set;

	ret = led_classdev_register(wm831x->dev, &drvdata->cdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register LED: %d\n", ret);
		goto err_led;
	}

	ret = device_create_file(drvdata->cdev.dev, &dev_attr_src);
	if (ret != 0)
		dev_err(&pdev->dev,
			"No source control for LED: %d\n", ret);

	return 0;

err_led:
	led_classdev_unregister(&drvdata->cdev);
err:
	return ret;
}

static int wm831x_status_remove(struct platform_device *pdev)
{
	struct wm831x_status *drvdata = platform_get_drvdata(pdev);

	device_remove_file(drvdata->cdev.dev, &dev_attr_src);
	led_classdev_unregister(&drvdata->cdev);

	return 0;
}

static struct platform_driver wm831x_status_driver = {
	.driver = {
		   .name = "wm831x-status",
		   .owner = THIS_MODULE,
		   },
	.probe = wm831x_status_probe,
	.remove = wm831x_status_remove,
};

module_platform_driver(wm831x_status_driver);

MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_DESCRIPTION("WM831x status LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm831x-status");
