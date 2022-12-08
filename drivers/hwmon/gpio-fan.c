// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * gpio-fan.c - Hwmon driver for fans connected to GPIO lines.
 *
 * Copyright (C) 2010 LaCie
 *
 * Author: Simon Guinot <sguinot@lacie.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/hwmon.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/thermal.h>

struct gpio_fan_speed {
	int rpm;
	int ctrl_val;
};

struct gpio_fan_data {
	struct device		*dev;
	struct device		*hwmon_dev;
	/* Cooling device if any */
	struct thermal_cooling_device *cdev;
	struct mutex		lock; /* lock GPIOs operations. */
	int			num_gpios;
	struct gpio_desc	**gpios;
	int			num_speed;
	struct gpio_fan_speed	*speed;
	int			speed_index;
	int			resume_speed;
	bool			pwm_enable;
	struct gpio_desc	*alarm_gpio;
	struct work_struct	alarm_work;
};

/*
 * Alarm GPIO.
 */

static void fan_alarm_notify(struct work_struct *ws)
{
	struct gpio_fan_data *fan_data =
		container_of(ws, struct gpio_fan_data, alarm_work);

	sysfs_notify(&fan_data->hwmon_dev->kobj, NULL, "fan1_alarm");
	kobject_uevent(&fan_data->hwmon_dev->kobj, KOBJ_CHANGE);
}

static irqreturn_t fan_alarm_irq_handler(int irq, void *dev_id)
{
	struct gpio_fan_data *fan_data = dev_id;

	schedule_work(&fan_data->alarm_work);

	return IRQ_NONE;
}

static ssize_t fan1_alarm_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct gpio_fan_data *fan_data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n",
		       gpiod_get_value_cansleep(fan_data->alarm_gpio));
}

static DEVICE_ATTR_RO(fan1_alarm);

static int fan_alarm_init(struct gpio_fan_data *fan_data)
{
	int alarm_irq;
	struct device *dev = fan_data->dev;

	/*
	 * If the alarm GPIO don't support interrupts, just leave
	 * without initializing the fail notification support.
	 */
	alarm_irq = gpiod_to_irq(fan_data->alarm_gpio);
	if (alarm_irq <= 0)
		return 0;

	INIT_WORK(&fan_data->alarm_work, fan_alarm_notify);
	irq_set_irq_type(alarm_irq, IRQ_TYPE_EDGE_BOTH);
	return devm_request_irq(dev, alarm_irq, fan_alarm_irq_handler,
				IRQF_SHARED, "GPIO fan alarm", fan_data);
}

/*
 * Control GPIOs.
 */

/* Must be called with fan_data->lock held, except during initialization. */
static void __set_fan_ctrl(struct gpio_fan_data *fan_data, int ctrl_val)
{
	int i;

	for (i = 0; i < fan_data->num_gpios; i++)
		gpiod_set_value_cansleep(fan_data->gpios[i],
					 (ctrl_val >> i) & 1);
}

static int __get_fan_ctrl(struct gpio_fan_data *fan_data)
{
	int i;
	int ctrl_val = 0;

	for (i = 0; i < fan_data->num_gpios; i++) {
		int value;

		value = gpiod_get_value_cansleep(fan_data->gpios[i]);
		ctrl_val |= (value << i);
	}
	return ctrl_val;
}

/* Must be called with fan_data->lock held, except during initialization. */
static void set_fan_speed(struct gpio_fan_data *fan_data, int speed_index)
{
	if (fan_data->speed_index == speed_index)
		return;

	__set_fan_ctrl(fan_data, fan_data->speed[speed_index].ctrl_val);
	fan_data->speed_index = speed_index;
}

static int get_fan_speed_index(struct gpio_fan_data *fan_data)
{
	int ctrl_val = __get_fan_ctrl(fan_data);
	int i;

	for (i = 0; i < fan_data->num_speed; i++)
		if (fan_data->speed[i].ctrl_val == ctrl_val)
			return i;

	dev_warn(fan_data->dev,
		 "missing speed array entry for GPIO value 0x%x\n", ctrl_val);

	return -ENODEV;
}

static int rpm_to_speed_index(struct gpio_fan_data *fan_data, unsigned long rpm)
{
	struct gpio_fan_speed *speed = fan_data->speed;
	int i;

	for (i = 0; i < fan_data->num_speed; i++)
		if (speed[i].rpm >= rpm)
			return i;

	return fan_data->num_speed - 1;
}

static ssize_t pwm1_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct gpio_fan_data *fan_data = dev_get_drvdata(dev);
	u8 pwm = fan_data->speed_index * 255 / (fan_data->num_speed - 1);

	return sprintf(buf, "%d\n", pwm);
}

static ssize_t pwm1_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct gpio_fan_data *fan_data = dev_get_drvdata(dev);
	unsigned long pwm;
	int speed_index;
	int ret = count;

	if (kstrtoul(buf, 10, &pwm) || pwm > 255)
		return -EINVAL;

	mutex_lock(&fan_data->lock);

	if (!fan_data->pwm_enable) {
		ret = -EPERM;
		goto exit_unlock;
	}

	speed_index = DIV_ROUND_UP(pwm * (fan_data->num_speed - 1), 255);
	set_fan_speed(fan_data, speed_index);

exit_unlock:
	mutex_unlock(&fan_data->lock);

	return ret;
}

static ssize_t pwm1_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct gpio_fan_data *fan_data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", fan_data->pwm_enable);
}

static ssize_t pwm1_enable_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct gpio_fan_data *fan_data = dev_get_drvdata(dev);
	unsigned long val;

	if (kstrtoul(buf, 10, &val) || val > 1)
		return -EINVAL;

	if (fan_data->pwm_enable == val)
		return count;

	mutex_lock(&fan_data->lock);

	fan_data->pwm_enable = val;

	/* Disable manual control mode: set fan at full speed. */
	if (val == 0)
		set_fan_speed(fan_data, fan_data->num_speed - 1);

	mutex_unlock(&fan_data->lock);

	return count;
}

static ssize_t pwm1_mode_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "0\n");
}

static ssize_t fan1_min_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct gpio_fan_data *fan_data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", fan_data->speed[0].rpm);
}

static ssize_t fan1_max_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct gpio_fan_data *fan_data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n",
		       fan_data->speed[fan_data->num_speed - 1].rpm);
}

static ssize_t fan1_input_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct gpio_fan_data *fan_data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", fan_data->speed[fan_data->speed_index].rpm);
}

static ssize_t set_rpm(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct gpio_fan_data *fan_data = dev_get_drvdata(dev);
	unsigned long rpm;
	int ret = count;

	if (kstrtoul(buf, 10, &rpm))
		return -EINVAL;

	mutex_lock(&fan_data->lock);

	if (!fan_data->pwm_enable) {
		ret = -EPERM;
		goto exit_unlock;
	}

	set_fan_speed(fan_data, rpm_to_speed_index(fan_data, rpm));

exit_unlock:
	mutex_unlock(&fan_data->lock);

	return ret;
}

static DEVICE_ATTR_RW(pwm1);
static DEVICE_ATTR_RW(pwm1_enable);
static DEVICE_ATTR_RO(pwm1_mode);
static DEVICE_ATTR_RO(fan1_min);
static DEVICE_ATTR_RO(fan1_max);
static DEVICE_ATTR_RO(fan1_input);
static DEVICE_ATTR(fan1_target, 0644, fan1_input_show, set_rpm);

static umode_t gpio_fan_is_visible(struct kobject *kobj,
				   struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct gpio_fan_data *data = dev_get_drvdata(dev);

	if (index == 0 && !data->alarm_gpio)
		return 0;
	if (index > 0 && !data->gpios)
		return 0;

	return attr->mode;
}

static struct attribute *gpio_fan_attributes[] = {
	&dev_attr_fan1_alarm.attr,		/* 0 */
	&dev_attr_pwm1.attr,			/* 1 */
	&dev_attr_pwm1_enable.attr,
	&dev_attr_pwm1_mode.attr,
	&dev_attr_fan1_input.attr,
	&dev_attr_fan1_target.attr,
	&dev_attr_fan1_min.attr,
	&dev_attr_fan1_max.attr,
	NULL
};

static const struct attribute_group gpio_fan_group = {
	.attrs = gpio_fan_attributes,
	.is_visible = gpio_fan_is_visible,
};

static const struct attribute_group *gpio_fan_groups[] = {
	&gpio_fan_group,
	NULL
};

static int fan_ctrl_init(struct gpio_fan_data *fan_data)
{
	int num_gpios = fan_data->num_gpios;
	struct gpio_desc **gpios = fan_data->gpios;
	int i, err;

	for (i = 0; i < num_gpios; i++) {
		/*
		 * The GPIO descriptors were retrieved with GPIOD_ASIS so here
		 * we set the GPIO into output mode, carefully preserving the
		 * current value by setting it to whatever it is already set
		 * (no surprise changes in default fan speed).
		 */
		err = gpiod_direction_output(gpios[i],
					gpiod_get_value_cansleep(gpios[i]));
		if (err)
			return err;
	}

	fan_data->pwm_enable = true; /* Enable manual fan speed control. */
	fan_data->speed_index = get_fan_speed_index(fan_data);
	if (fan_data->speed_index < 0)
		return fan_data->speed_index;

	return 0;
}

static int gpio_fan_get_max_state(struct thermal_cooling_device *cdev,
				  unsigned long *state)
{
	struct gpio_fan_data *fan_data = cdev->devdata;

	if (!fan_data)
		return -EINVAL;

	*state = fan_data->num_speed - 1;
	return 0;
}

static int gpio_fan_get_cur_state(struct thermal_cooling_device *cdev,
				  unsigned long *state)
{
	struct gpio_fan_data *fan_data = cdev->devdata;

	if (!fan_data)
		return -EINVAL;

	*state = fan_data->speed_index;
	return 0;
}

static int gpio_fan_set_cur_state(struct thermal_cooling_device *cdev,
				  unsigned long state)
{
	struct gpio_fan_data *fan_data = cdev->devdata;

	if (!fan_data)
		return -EINVAL;

	if (state >= fan_data->num_speed)
		return -EINVAL;

	set_fan_speed(fan_data, state);
	return 0;
}

static const struct thermal_cooling_device_ops gpio_fan_cool_ops = {
	.get_max_state = gpio_fan_get_max_state,
	.get_cur_state = gpio_fan_get_cur_state,
	.set_cur_state = gpio_fan_set_cur_state,
};

/*
 * Translate OpenFirmware node properties into platform_data
 */
static int gpio_fan_get_of_data(struct gpio_fan_data *fan_data)
{
	struct gpio_fan_speed *speed;
	struct device *dev = fan_data->dev;
	struct device_node *np = dev->of_node;
	struct gpio_desc **gpios;
	unsigned i;
	u32 u;
	struct property *prop;
	const __be32 *p;

	/* Alarm GPIO if one exists */
	fan_data->alarm_gpio = devm_gpiod_get_optional(dev, "alarm", GPIOD_IN);
	if (IS_ERR(fan_data->alarm_gpio))
		return PTR_ERR(fan_data->alarm_gpio);

	/* Fill GPIO pin array */
	fan_data->num_gpios = gpiod_count(dev, NULL);
	if (fan_data->num_gpios <= 0) {
		if (fan_data->alarm_gpio)
			return 0;
		dev_err(dev, "DT properties empty / missing");
		return -ENODEV;
	}
	gpios = devm_kcalloc(dev,
			     fan_data->num_gpios, sizeof(struct gpio_desc *),
			     GFP_KERNEL);
	if (!gpios)
		return -ENOMEM;
	for (i = 0; i < fan_data->num_gpios; i++) {
		gpios[i] = devm_gpiod_get_index(dev, NULL, i, GPIOD_ASIS);
		if (IS_ERR(gpios[i]))
			return PTR_ERR(gpios[i]);
	}
	fan_data->gpios = gpios;

	/* Get number of RPM/ctrl_val pairs in speed map */
	prop = of_find_property(np, "gpio-fan,speed-map", &i);
	if (!prop) {
		dev_err(dev, "gpio-fan,speed-map DT property missing");
		return -ENODEV;
	}
	i = i / sizeof(u32);
	if (i == 0 || i & 1) {
		dev_err(dev, "gpio-fan,speed-map contains zero/odd number of entries");
		return -ENODEV;
	}
	fan_data->num_speed = i / 2;

	/*
	 * Populate speed map
	 * Speed map is in the form <RPM ctrl_val RPM ctrl_val ...>
	 * this needs splitting into pairs to create gpio_fan_speed structs
	 */
	speed = devm_kcalloc(dev,
			fan_data->num_speed, sizeof(struct gpio_fan_speed),
			GFP_KERNEL);
	if (!speed)
		return -ENOMEM;
	p = NULL;
	for (i = 0; i < fan_data->num_speed; i++) {
		p = of_prop_next_u32(prop, p, &u);
		if (!p)
			return -ENODEV;
		speed[i].rpm = u;
		p = of_prop_next_u32(prop, p, &u);
		if (!p)
			return -ENODEV;
		speed[i].ctrl_val = u;
	}
	fan_data->speed = speed;

	return 0;
}

static const struct of_device_id of_gpio_fan_match[] = {
	{ .compatible = "gpio-fan", },
	{},
};
MODULE_DEVICE_TABLE(of, of_gpio_fan_match);

static void gpio_fan_stop(void *data)
{
	set_fan_speed(data, 0);
}

static int gpio_fan_probe(struct platform_device *pdev)
{
	int err;
	struct gpio_fan_data *fan_data;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	fan_data = devm_kzalloc(dev, sizeof(struct gpio_fan_data),
				GFP_KERNEL);
	if (!fan_data)
		return -ENOMEM;

	fan_data->dev = dev;
	err = gpio_fan_get_of_data(fan_data);
	if (err)
		return err;

	platform_set_drvdata(pdev, fan_data);
	mutex_init(&fan_data->lock);

	/* Configure control GPIOs if available. */
	if (fan_data->gpios && fan_data->num_gpios > 0) {
		if (!fan_data->speed || fan_data->num_speed <= 1)
			return -EINVAL;
		err = fan_ctrl_init(fan_data);
		if (err)
			return err;
		err = devm_add_action_or_reset(dev, gpio_fan_stop, fan_data);
		if (err)
			return err;
	}

	/* Make this driver part of hwmon class. */
	fan_data->hwmon_dev =
		devm_hwmon_device_register_with_groups(dev,
						       "gpio_fan", fan_data,
						       gpio_fan_groups);
	if (IS_ERR(fan_data->hwmon_dev))
		return PTR_ERR(fan_data->hwmon_dev);

	/* Configure alarm GPIO if available. */
	if (fan_data->alarm_gpio) {
		err = fan_alarm_init(fan_data);
		if (err)
			return err;
	}

	/* Optional cooling device register for Device tree platforms */
	fan_data->cdev = devm_thermal_of_cooling_device_register(dev, np,
				"gpio-fan", fan_data, &gpio_fan_cool_ops);

	dev_info(dev, "GPIO fan initialized\n");

	return 0;
}

static void gpio_fan_shutdown(struct platform_device *pdev)
{
	struct gpio_fan_data *fan_data = platform_get_drvdata(pdev);

	if (fan_data->gpios)
		set_fan_speed(fan_data, 0);
}

static int gpio_fan_suspend(struct device *dev)
{
	struct gpio_fan_data *fan_data = dev_get_drvdata(dev);

	if (fan_data->gpios) {
		fan_data->resume_speed = fan_data->speed_index;
		set_fan_speed(fan_data, 0);
	}

	return 0;
}

static int gpio_fan_resume(struct device *dev)
{
	struct gpio_fan_data *fan_data = dev_get_drvdata(dev);

	if (fan_data->gpios)
		set_fan_speed(fan_data, fan_data->resume_speed);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(gpio_fan_pm, gpio_fan_suspend, gpio_fan_resume);

static struct platform_driver gpio_fan_driver = {
	.probe		= gpio_fan_probe,
	.shutdown	= gpio_fan_shutdown,
	.driver	= {
		.name	= "gpio-fan",
		.pm	= pm_sleep_ptr(&gpio_fan_pm),
		.of_match_table = of_match_ptr(of_gpio_fan_match),
	},
};

module_platform_driver(gpio_fan_driver);

MODULE_AUTHOR("Simon Guinot <sguinot@lacie.com>");
MODULE_DESCRIPTION("GPIO FAN driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:gpio-fan");
