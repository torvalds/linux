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

struct gpio_fan_data {
	struct platform_device	*pdev;
	struct device		*hwmon_dev;
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
	int value = gpio_get_value(alarm->gpio);

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

	err = device_create_file(&pdev->dev, &dev_attr_fan1_alarm);
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
	if (err)
		goto err_free_sysfs;

	return 0;

err_free_sysfs:
	device_remove_file(&pdev->dev, &dev_attr_fan1_alarm);
	return err;
}

static void fan_alarm_free(struct gpio_fan_data *fan_data)
{
	struct platform_device *pdev = fan_data->pdev;

	device_remove_file(&pdev->dev, &dev_attr_fan1_alarm);
}

/*
 * Control GPIOs.
 */

/* Must be called with fan_data->lock held, except during initialization. */
static void __set_fan_ctrl(struct gpio_fan_data *fan_data, int ctrl_val)
{
	int i;

	for (i = 0; i < fan_data->num_ctrl; i++)
		gpio_set_value(fan_data->ctrl[i], (ctrl_val >> i) & 1);
}

static int __get_fan_ctrl(struct gpio_fan_data *fan_data)
{
	int i;
	int ctrl_val = 0;

	for (i = 0; i < fan_data->num_ctrl; i++) {
		int value;

		value = gpio_get_value(fan_data->ctrl[i]);
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

	return -EINVAL;
}

static int rpm_to_speed_index(struct gpio_fan_data *fan_data, int rpm)
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

static struct attribute *gpio_fan_ctrl_attributes[] = {
	&dev_attr_pwm1.attr,
	&dev_attr_pwm1_enable.attr,
	&dev_attr_pwm1_mode.attr,
	&dev_attr_fan1_input.attr,
	&dev_attr_fan1_target.attr,
	&dev_attr_fan1_min.attr,
	&dev_attr_fan1_max.attr,
	NULL
};

static const struct attribute_group gpio_fan_ctrl_group = {
	.attrs = gpio_fan_ctrl_attributes,
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

		err = gpio_direction_output(ctrl[i], gpio_get_value(ctrl[i]));
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
		return -ENODEV;

	err = sysfs_create_group(&pdev->dev.kobj, &gpio_fan_ctrl_group);
	return err;
}

static void fan_ctrl_free(struct gpio_fan_data *fan_data)
{
	struct platform_device *pdev = fan_data->pdev;

	sysfs_remove_group(&pdev->dev.kobj, &gpio_fan_ctrl_group);
}

/*
 * Platform driver.
 */

static ssize_t show_name(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "gpio-fan\n");
}

static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

static int __devinit gpio_fan_probe(struct platform_device *pdev)
{
	int err;
	struct gpio_fan_data *fan_data;
	struct gpio_fan_platform_data *pdata = pdev->dev.platform_data;

	if (!pdata)
		return -EINVAL;

	fan_data = devm_kzalloc(&pdev->dev, sizeof(struct gpio_fan_data),
				GFP_KERNEL);
	if (!fan_data)
		return -ENOMEM;

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
		if (!pdata->speed || pdata->num_speed <= 1) {
			err = -EINVAL;
			goto err_free_alarm;
		}
		err = fan_ctrl_init(fan_data, pdata);
		if (err)
			goto err_free_alarm;
	}

	err = device_create_file(&pdev->dev, &dev_attr_name);
	if (err)
		goto err_free_ctrl;

	/* Make this driver part of hwmon class. */
	fan_data->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(fan_data->hwmon_dev)) {
		err = PTR_ERR(fan_data->hwmon_dev);
		goto err_remove_name;
	}

	dev_info(&pdev->dev, "GPIO fan initialized\n");

	return 0;

err_remove_name:
	device_remove_file(&pdev->dev, &dev_attr_name);
err_free_ctrl:
	if (fan_data->ctrl)
		fan_ctrl_free(fan_data);
err_free_alarm:
	if (fan_data->alarm)
		fan_alarm_free(fan_data);
	return err;
}

static int __devexit gpio_fan_remove(struct platform_device *pdev)
{
	struct gpio_fan_data *fan_data = platform_get_drvdata(pdev);

	hwmon_device_unregister(fan_data->hwmon_dev);
	device_remove_file(&pdev->dev, &dev_attr_name);
	if (fan_data->alarm)
		fan_alarm_free(fan_data);
	if (fan_data->ctrl)
		fan_ctrl_free(fan_data);

	return 0;
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
#define GPIO_FAN_PM	&gpio_fan_pm
#else
#define GPIO_FAN_PM	NULL
#endif

static struct platform_driver gpio_fan_driver = {
	.probe		= gpio_fan_probe,
	.remove		= __devexit_p(gpio_fan_remove),
	.driver	= {
		.name	= "gpio-fan",
		.pm	= GPIO_FAN_PM,
	},
};

module_platform_driver(gpio_fan_driver);

MODULE_AUTHOR("Simon Guinot <sguinot@lacie.com>");
MODULE_DESCRIPTION("GPIO FAN driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:gpio-fan");
