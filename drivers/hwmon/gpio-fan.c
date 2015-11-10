/*
 * gpio-fan.c - Hwmon driver for fans connected to GPIO lines.
 *
 * Copyright (C) 2010 LaCie
 *
 * Author: Simon Guinot <sguinot@lacie.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
#include <linux/gpio.h>
#include <linux/gpio-fan.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/thermal.h>

struct gpio_fan_data {
	struct platform_device	*pdev;
	struct device		*hwmon_dev;
	/* Cooling device if any */
	struct thermal_cooling_device *cdev;
	struct mutex		lock; /* lock GPIOs operations. */
	int			num_ctrl;
	unsigned		*ctrl;
	int			num_speed;
	struct gpio_fan_speed	*speed;
	int			speed_index;
#ifdef CONFIG_PM_SLEEP
	int			resume_speed;
#endif
	bool			pwm_enable;
	struct gpio_fan_alarm	*alarm;
	struct work_struct	alarm_work;
};

/*
 * Alarm GPIO.
 */

static void fan_alarm_notify(struct work_struct *ws)
{
	struct gpio_fan_data *fan_data =
		container_of(ws, struct gpio_fan_data, alarm_work);

	sysfs_notify(&fan_data->pdev->dev.kobj, NULL, "fan1_alarm");
	kobject_uevent(&fan_data->pdev->dev.kobj, KOBJ_CHANGE);
}

static irqreturn_t fan_alarm_irq_handler(int irq, void *dev_id)
{
	struct gpio_fan_data *fan_data = dev_id;

	schedule_work(&fan_data->alarm_work);

	return IRQ_NONE;
}

static ssize_t show_fan_alarm(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct gpio_fan_data *fan_data = dev_get_drvdata(dev);
	struct gpio_fan_alarm *alarm = fan_data->alarm;
	int value = gpio_get_value_cansleep(alarm->gpio);

	if (alarm->active_low)
		value = !value;

	return sprintf(buf, "%d\n", value);
}

static DEVICE_ATTR(fan1_alarm, S_IRUGO, show_fan_alarm, NULL);

static int fan_alarm_init(struct gpio_fan_data *fan_data,
			  struct gpio_fan_alarm *alarm)
{
	int err;
	int alarm_irq;
	struct platform_device *pdev = fan_data->pdev;

	fan_data->alarm = alarm;

	err = devm_gpio_request(&pdev->dev, alarm->gpio, "GPIO fan alarm");
	if (err)
		return err;

	err = gpio_direction_input(alarm->gpio);
	if (err)
		return err;

	/*
	 * If the alarm GPIO don't support interrupts, just leave
	 * without initializing the fail notification support.
	 */
	alarm_irq = gpio_to_irq(alarm->gpio);
	if (alarm_irq < 0)
		return 0;

	INIT_WORK(&fan_data->alarm_work, fan_alarm_notify);
	irq_set_irq_type(alarm_irq, IRQ_TYPE_EDGE_BOTH);
	err = devm_request_irq(&pdev->dev, alarm_irq, fan_alarm_irq_handler,
			       IRQF_SHARED, "GPIO fan alarm", fan_data);
	return err;
}

/*
 * Control GPIOs.
 */

/* Must be called with fan_data->lock held, except during initialization. */
static void __set_fan_ctrl(struct gpio_fan_data *fan_data, int ctrl_val)
{
	int i;

	for (i = 0; i < fan_data->num_ctrl; i++)
		gpio_set_value_cansleep(fan_data->ctrl[i], (ctrl_val >> i) & 1);
}

static int __get_fan_ctrl(struct gpio_fan_data *fan_data)
{
	int i;
	int ctrl_val = 0;

	for (i = 0; i < fan_data->num_ctrl; i++) {
		int value;

		value = gpio_get_value_cansleep(fan_data->ctrl[i]);
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

	dev_warn(&fan_data->pdev->dev,
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

static ssize_t show_pwm(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct gpio_fan_data *fan_data = dev_get_drvdata(dev);
	u8 pwm = fan_data->speed_index * 255 / (fan_data->num_speed - 1);

	return sprintf(buf, "%d\n", pwm);
}

static ssize_t set_pwm(struct device *dev, struct device_attribute *attr,
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

static ssize_t show_pwm_enable(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct gpio_fan_data *fan_data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", fan_data->pwm_enable);
}

static ssize_t set_pwm_enable(struct device *dev, struct device_attribute *attr,
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

static ssize_t show_pwm_mode(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "0\n");
}

static ssize_t show_rpm_min(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct gpio_fan_data *fan_data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", fan_data->speed[0].rpm);
}

static ssize_t show_rpm_max(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct gpio_fan_data *fan_data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n",
		       fan_data->speed[fan_data->num_speed - 1].rpm);
}

static ssize_t show_rpm(struct device *dev,
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

static DEVICE_ATTR(pwm1, S_IRUGO | S_IWUSR, show_pwm, set_pwm);
static DEVICE_ATTR(pwm1_enable, S_IRUGO | S_IWUSR,
		   show_pwm_enable, set_pwm_enable);
static DEVICE_ATTR(pwm1_mode, S_IRUGO, show_pwm_mode, NULL);
static DEVICE_ATTR(fan1_min, S_IRUGO, show_rpm_min, NULL);
static DEVICE_ATTR(fan1_max, S_IRUGO, show_rpm_max, NULL);
static DEVICE_ATTR(fan1_input, S_IRUGO, show_rpm, NULL);
static DEVICE_ATTR(fan1_target, S_IRUGO | S_IWUSR, show_rpm, set_rpm);

static umode_t gpio_fan_is_visible(struct kobject *kobj,
				   struct attribute *attr, int index)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct gpio_fan_data *data = dev_get_drvdata(dev);

	if (index == 0 && !data->alarm)
		return 0;
	if (index > 0 && !data->ctrl)
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

static int fan_ctrl_init(struct gpio_fan_data *fan_data,
			 struct gpio_fan_platform_data *pdata)
{
	struct platform_device *pdev = fan_data->pdev;
	int num_ctrl = pdata->num_ctrl;
	unsigned *ctrl = pdata->ctrl;
	int i, err;

	for (i = 0; i < num_ctrl; i++) {
		err = devm_gpio_request(&pdev->dev, ctrl[i],
					"GPIO fan control");
		if (err)
			return err;

		err = gpio_direction_output(ctrl[i],
					    gpio_get_value_cansleep(ctrl[i]));
		if (err)
			return err;
	}

	fan_data->num_ctrl = num_ctrl;
	fan_data->ctrl = ctrl;
	fan_data->num_speed = pdata->num_speed;
	fan_data->speed = pdata->speed;
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
	int r;

	if (!fan_data)
		return -EINVAL;

	r = get_fan_speed_index(fan_data);
	if (r < 0)
		return r;

	*state = r;
	return 0;
}

static int gpio_fan_set_cur_state(struct thermal_cooling_device *cdev,
				  unsigned long state)
{
	struct gpio_fan_data *fan_data = cdev->devdata;

	if (!fan_data)
		return -EINVAL;

	set_fan_speed(fan_data, state);
	return 0;
}

static const struct thermal_cooling_device_ops gpio_fan_cool_ops = {
	.get_max_state = gpio_fan_get_max_state,
	.get_cur_state = gpio_fan_get_cur_state,
	.set_cur_state = gpio_fan_set_cur_state,
};

#ifdef CONFIG_OF_GPIO
/*
 * Translate OpenFirmware node properties into platform_data
 */
static int gpio_fan_get_of_pdata(struct device *dev,
			    struct gpio_fan_platform_data *pdata)
{
	struct device_node *node;
	struct gpio_fan_speed *speed;
	unsigned *ctrl;
	unsigned i;
	u32 u;
	struct property *prop;
	const __be32 *p;

	node = dev->of_node;

	/* Alarm GPIO if one exists */
	if (of_gpio_named_count(node, "alarm-gpios") > 0) {
		struct gpio_fan_alarm *alarm;
		int val;
		enum of_gpio_flags flags;

		alarm = devm_kzalloc(dev, sizeof(struct gpio_fan_alarm),
					GFP_KERNEL);
		if (!alarm)
			return -ENOMEM;

		val = of_get_named_gpio_flags(node, "alarm-gpios", 0, &flags);
		if (val < 0)
			return val;
		alarm->gpio = val;
		alarm->active_low = flags & OF_GPIO_ACTIVE_LOW;

		pdata->alarm = alarm;
	}

	/* Fill GPIO pin array */
	pdata->num_ctrl = of_gpio_count(node);
	if (pdata->num_ctrl <= 0) {
		if (pdata->alarm)
			return 0;
		dev_err(dev, "DT properties empty / missing");
		return -ENODEV;
	}
	ctrl = devm_kzalloc(dev, pdata->num_ctrl * sizeof(unsigned),
				GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;
	for (i = 0; i < pdata->num_ctrl; i++) {
		int val;

		val = of_get_gpio(node, i);
		if (val < 0)
			return val;
		ctrl[i] = val;
	}
	pdata->ctrl = ctrl;

	/* Get number of RPM/ctrl_val pairs in speed map */
	prop = of_find_property(node, "gpio-fan,speed-map", &i);
	if (!prop) {
		dev_err(dev, "gpio-fan,speed-map DT property missing");
		return -ENODEV;
	}
	i = i / sizeof(u32);
	if (i == 0 || i & 1) {
		dev_err(dev, "gpio-fan,speed-map contains zero/odd number of entries");
		return -ENODEV;
	}
	pdata->num_speed = i / 2;

	/*
	 * Populate speed map
	 * Speed map is in the form <RPM ctrl_val RPM ctrl_val ...>
	 * this needs splitting into pairs to create gpio_fan_speed structs
	 */
	speed = devm_kzalloc(dev,
			pdata->num_speed * sizeof(struct gpio_fan_speed),
			GFP_KERNEL);
	if (!speed)
		return -ENOMEM;
	p = NULL;
	for (i = 0; i < pdata->num_speed; i++) {
		p = of_prop_next_u32(prop, p, &u);
		if (!p)
			return -ENODEV;
		speed[i].rpm = u;
		p = of_prop_next_u32(prop, p, &u);
		if (!p)
			return -ENODEV;
		speed[i].ctrl_val = u;
	}
	pdata->speed = speed;

	return 0;
}

static const struct of_device_id of_gpio_fan_match[] = {
	{ .compatible = "gpio-fan", },
	{},
};
MODULE_DEVICE_TABLE(of, of_gpio_fan_match);
#endif /* CONFIG_OF_GPIO */

static int gpio_fan_probe(struct platform_device *pdev)
{
	int err;
	struct gpio_fan_data *fan_data;
	struct gpio_fan_platform_data *pdata = dev_get_platdata(&pdev->dev);

	fan_data = devm_kzalloc(&pdev->dev, sizeof(struct gpio_fan_data),
				GFP_KERNEL);
	if (!fan_data)
		return -ENOMEM;

#ifdef CONFIG_OF_GPIO
	if (!pdata) {
		pdata = devm_kzalloc(&pdev->dev,
					sizeof(struct gpio_fan_platform_data),
					GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		err = gpio_fan_get_of_pdata(&pdev->dev, pdata);
		if (err)
			return err;
	}
#else /* CONFIG_OF_GPIO */
	if (!pdata)
		return -EINVAL;
#endif /* CONFIG_OF_GPIO */

	fan_data->pdev = pdev;
	platform_set_drvdata(pdev, fan_data);
	mutex_init(&fan_data->lock);

	/* Configure alarm GPIO if available. */
	if (pdata->alarm) {
		err = fan_alarm_init(fan_data, pdata->alarm);
		if (err)
			return err;
	}

	/* Configure control GPIOs if available. */
	if (pdata->ctrl && pdata->num_ctrl > 0) {
		if (!pdata->speed || pdata->num_speed <= 1)
			return -EINVAL;
		err = fan_ctrl_init(fan_data, pdata);
		if (err)
			return err;
	}

	/* Make this driver part of hwmon class. */
	fan_data->hwmon_dev =
		devm_hwmon_device_register_with_groups(&pdev->dev,
						       "gpio_fan", fan_data,
						       gpio_fan_groups);
	if (IS_ERR(fan_data->hwmon_dev))
		return PTR_ERR(fan_data->hwmon_dev);
#ifdef CONFIG_OF_GPIO
	/* Optional cooling device register for Device tree platforms */
	fan_data->cdev = thermal_of_cooling_device_register(pdev->dev.of_node,
							    "gpio-fan",
							    fan_data,
							    &gpio_fan_cool_ops);
#else /* CONFIG_OF_GPIO */
	/* Optional cooling device register for non Device tree platforms */
	fan_data->cdev = thermal_cooling_device_register("gpio-fan", fan_data,
							 &gpio_fan_cool_ops);
#endif /* CONFIG_OF_GPIO */

	dev_info(&pdev->dev, "GPIO fan initialized\n");

	return 0;
}

static int gpio_fan_remove(struct platform_device *pdev)
{
	struct gpio_fan_data *fan_data = platform_get_drvdata(pdev);

	if (!IS_ERR(fan_data->cdev))
		thermal_cooling_device_unregister(fan_data->cdev);

	if (fan_data->ctrl)
		set_fan_speed(fan_data, 0);

	return 0;
}

static void gpio_fan_shutdown(struct platform_device *pdev)
{
	gpio_fan_remove(pdev);
}

#ifdef CONFIG_PM_SLEEP
static int gpio_fan_suspend(struct device *dev)
{
	struct gpio_fan_data *fan_data = dev_get_drvdata(dev);

	if (fan_data->ctrl) {
		fan_data->resume_speed = fan_data->speed_index;
		set_fan_speed(fan_data, 0);
	}

	return 0;
}

static int gpio_fan_resume(struct device *dev)
{
	struct gpio_fan_data *fan_data = dev_get_drvdata(dev);

	if (fan_data->ctrl)
		set_fan_speed(fan_data, fan_data->resume_speed);

	return 0;
}

static SIMPLE_DEV_PM_OPS(gpio_fan_pm, gpio_fan_suspend, gpio_fan_resume);
#define GPIO_FAN_PM	(&gpio_fan_pm)
#else
#define GPIO_FAN_PM	NULL
#endif

static struct platform_driver gpio_fan_driver = {
	.probe		= gpio_fan_probe,
	.remove		= gpio_fan_remove,
	.shutdown	= gpio_fan_shutdown,
	.driver	= {
		.name	= "gpio-fan",
		.pm	= GPIO_FAN_PM,
#ifdef CONFIG_OF_GPIO
		.of_match_table = of_match_ptr(of_gpio_fan_match),
#endif
	},
};

module_platform_driver(gpio_fan_driver);

MODULE_AUTHOR("Simon Guinot <sguinot@lacie.com>");
MODULE_DESCRIPTION("GPIO FAN driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:gpio-fan");
