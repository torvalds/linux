/*
 * Copyright (C) ST-Ericsson 2010 - 2013
 * Author: Martin Persson <martin.persson@stericsson.com>
 *         Hongbo Zhang <hongbo.zhang@linaro.org>
 * License Terms: GNU General Public License v2
 *
 * ABX500 does not provide auto ADC, so to monitor the required temperatures,
 * a periodic work is used. It is more important to not wake up the CPU than
 * to perform this job, hence the use of a deferred delay.
 *
 * A deferred delay for thermal monitor is considered safe because:
 * If the chip gets too hot during a sleep state it's most likely due to
 * external factors, such as the surrounding temperature. I.e. no SW decisions
 * will make any difference.
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>
#include "abx500.h"

#define DEFAULT_MONITOR_DELAY	HZ
#define DEFAULT_MAX_TEMP	130

static inline void schedule_monitor(struct abx500_temp *data)
{
	data->work_active = true;
	schedule_delayed_work(&data->work, DEFAULT_MONITOR_DELAY);
}

static void threshold_updated(struct abx500_temp *data)
{
	int i;
	for (i = 0; i < data->monitored_sensors; i++)
		if (data->max[i] != 0 || data->min[i] != 0) {
			schedule_monitor(data);
			return;
		}

	dev_dbg(&data->pdev->dev, "No active thresholds.\n");
	cancel_delayed_work_sync(&data->work);
	data->work_active = false;
}

static void gpadc_monitor(struct work_struct *work)
{
	int temp, i, ret;
	char alarm_node[30];
	bool updated_min_alarm, updated_max_alarm;
	struct abx500_temp *data;

	data = container_of(work, struct abx500_temp, work.work);
	mutex_lock(&data->lock);

	for (i = 0; i < data->monitored_sensors; i++) {
		/* Thresholds are considered inactive if set to 0 */
		if (data->max[i] == 0 && data->min[i] == 0)
			continue;

		if (data->max[i] < data->min[i])
			continue;

		ret = data->ops.read_sensor(data, data->gpadc_addr[i], &temp);
		if (ret < 0) {
			dev_err(&data->pdev->dev, "GPADC read failed\n");
			continue;
		}

		updated_min_alarm = false;
		updated_max_alarm = false;

		if (data->min[i] != 0) {
			if (temp < data->min[i]) {
				if (data->min_alarm[i] == false) {
					data->min_alarm[i] = true;
					updated_min_alarm = true;
				}
			} else {
				if (data->min_alarm[i] == true) {
					data->min_alarm[i] = false;
					updated_min_alarm = true;
				}
			}
		}
		if (data->max[i] != 0) {
			if (temp > data->max[i]) {
				if (data->max_alarm[i] == false) {
					data->max_alarm[i] = true;
					updated_max_alarm = true;
				}
			} else if (temp < data->max[i] - data->max_hyst[i]) {
				if (data->max_alarm[i] == true) {
					data->max_alarm[i] = false;
					updated_max_alarm = true;
				}
			}
		}

		if (updated_min_alarm) {
			ret = sprintf(alarm_node, "temp%d_min_alarm", i + 1);
			sysfs_notify(&data->pdev->dev.kobj, NULL, alarm_node);
		}
		if (updated_max_alarm) {
			ret = sprintf(alarm_node, "temp%d_max_alarm", i + 1);
			sysfs_notify(&data->pdev->dev.kobj, NULL, alarm_node);
		}
	}

	schedule_monitor(data);
	mutex_unlock(&data->lock);
}

/* HWMON sysfs interfaces */
static ssize_t show_name(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	struct abx500_temp *data = dev_get_drvdata(dev);
	/* Show chip name */
	return data->ops.show_name(dev, devattr, buf);
}

static ssize_t show_label(struct device *dev,
			  struct device_attribute *devattr, char *buf)
{
	struct abx500_temp *data = dev_get_drvdata(dev);
	/* Show each sensor label */
	return data->ops.show_label(dev, devattr, buf);
}

static ssize_t show_input(struct device *dev,
			  struct device_attribute *devattr, char *buf)
{
	int ret, temp;
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	u8 gpadc_addr = data->gpadc_addr[attr->index];

	ret = data->ops.read_sensor(data, gpadc_addr, &temp);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", temp);
}

/* Set functions (RW nodes) */
static ssize_t set_min(struct device *dev, struct device_attribute *devattr,
		       const char *buf, size_t count)
{
	unsigned long val;
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int res = kstrtol(buf, 10, &val);
	if (res < 0)
		return res;

	val = clamp_val(val, 0, DEFAULT_MAX_TEMP);

	mutex_lock(&data->lock);
	data->min[attr->index] = val;
	threshold_updated(data);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t set_max(struct device *dev, struct device_attribute *devattr,
		       const char *buf, size_t count)
{
	unsigned long val;
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int res = kstrtol(buf, 10, &val);
	if (res < 0)
		return res;

	val = clamp_val(val, 0, DEFAULT_MAX_TEMP);

	mutex_lock(&data->lock);
	data->max[attr->index] = val;
	threshold_updated(data);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t set_max_hyst(struct device *dev,
			    struct device_attribute *devattr,
			    const char *buf, size_t count)
{
	unsigned long val;
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int res = kstrtoul(buf, 10, &val);
	if (res < 0)
		return res;

	val = clamp_val(val, 0, DEFAULT_MAX_TEMP);

	mutex_lock(&data->lock);
	data->max_hyst[attr->index] = val;
	threshold_updated(data);
	mutex_unlock(&data->lock);

	return count;
}

/* Show functions (RO nodes) */
static ssize_t show_min(struct device *dev,
			struct device_attribute *devattr, char *buf)
{
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	return sprintf(buf, "%ld\n", data->min[attr->index]);
}

static ssize_t show_max(struct device *dev,
			struct device_attribute *devattr, char *buf)
{
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	return sprintf(buf, "%ld\n", data->max[attr->index]);
}

static ssize_t show_max_hyst(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	return sprintf(buf, "%ld\n", data->max_hyst[attr->index]);
}

static ssize_t show_min_alarm(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	return sprintf(buf, "%d\n", data->min_alarm[attr->index]);
}

static ssize_t show_max_alarm(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct abx500_temp *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	return sprintf(buf, "%d\n", data->max_alarm[attr->index]);
}

static umode_t abx500_attrs_visible(struct kobject *kobj,
				   struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct abx500_temp *data = dev_get_drvdata(dev);

	if (data->ops.is_visible)
		return data->ops.is_visible(attr, n);

	return attr->mode;
}

/* Chip name, required by hwmon */
static SENSOR_DEVICE_ATTR(name, S_IRUGO, show_name, NULL, 0);

/* GPADC - SENSOR1 */
static SENSOR_DEVICE_ATTR(temp1_label, S_IRUGO, show_label, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_input, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_min, S_IWUSR | S_IRUGO, show_min, set_min, 0);
static SENSOR_DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO, show_max, set_max, 0);
static SENSOR_DEVICE_ATTR(temp1_max_hyst, S_IWUSR | S_IRUGO,
			  show_max_hyst, set_max_hyst, 0);
static SENSOR_DEVICE_ATTR(temp1_min_alarm, S_IRUGO, show_min_alarm, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO, show_max_alarm, NULL, 0);

/* GPADC - SENSOR2 */
static SENSOR_DEVICE_ATTR(temp2_label, S_IRUGO, show_label, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_input, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_min, S_IWUSR | S_IRUGO, show_min, set_min, 1);
static SENSOR_DEVICE_ATTR(temp2_max, S_IWUSR | S_IRUGO, show_max, set_max, 1);
static SENSOR_DEVICE_ATTR(temp2_max_hyst, S_IWUSR | S_IRUGO,
			  show_max_hyst, set_max_hyst, 1);
static SENSOR_DEVICE_ATTR(temp2_min_alarm, S_IRUGO, show_min_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_max_alarm, S_IRUGO, show_max_alarm, NULL, 1);

/* GPADC - SENSOR3 */
static SENSOR_DEVICE_ATTR(temp3_label, S_IRUGO, show_label, NULL, 2);
static SENSOR_DEVICE_ATTR(temp3_input, S_IRUGO, show_input, NULL, 2);
static SENSOR_DEVICE_ATTR(temp3_min, S_IWUSR | S_IRUGO, show_min, set_min, 2);
static SENSOR_DEVICE_ATTR(temp3_max, S_IWUSR | S_IRUGO, show_max, set_max, 2);
static SENSOR_DEVICE_ATTR(temp3_max_hyst, S_IWUSR | S_IRUGO,
			  show_max_hyst, set_max_hyst, 2);
static SENSOR_DEVICE_ATTR(temp3_min_alarm, S_IRUGO, show_min_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(temp3_max_alarm, S_IRUGO, show_max_alarm, NULL, 2);

/* GPADC - SENSOR4 */
static SENSOR_DEVICE_ATTR(temp4_label, S_IRUGO, show_label, NULL, 3);
static SENSOR_DEVICE_ATTR(temp4_input, S_IRUGO, show_input, NULL, 3);
static SENSOR_DEVICE_ATTR(temp4_min, S_IWUSR | S_IRUGO, show_min, set_min, 3);
static SENSOR_DEVICE_ATTR(temp4_max, S_IWUSR | S_IRUGO, show_max, set_max, 3);
static SENSOR_DEVICE_ATTR(temp4_max_hyst, S_IWUSR | S_IRUGO,
			  show_max_hyst, set_max_hyst, 3);
static SENSOR_DEVICE_ATTR(temp4_min_alarm, S_IRUGO, show_min_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(temp4_max_alarm, S_IRUGO, show_max_alarm, NULL, 3);

struct attribute *abx500_temp_attributes[] = {
	&sensor_dev_attr_name.dev_attr.attr,

	&sensor_dev_attr_temp1_label.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,

	&sensor_dev_attr_temp2_label.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp2_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max_alarm.dev_attr.attr,

	&sensor_dev_attr_temp3_label.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp3_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp3_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_max_alarm.dev_attr.attr,

	&sensor_dev_attr_temp4_label.dev_attr.attr,
	&sensor_dev_attr_temp4_input.dev_attr.attr,
	&sensor_dev_attr_temp4_min.dev_attr.attr,
	&sensor_dev_attr_temp4_max.dev_attr.attr,
	&sensor_dev_attr_temp4_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp4_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp4_max_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group abx500_temp_group = {
	.attrs = abx500_temp_attributes,
	.is_visible = abx500_attrs_visible,
};

static irqreturn_t abx500_temp_irq_handler(int irq, void *irq_data)
{
	struct platform_device *pdev = irq_data;
	struct abx500_temp *data = platform_get_drvdata(pdev);

	data->ops.irq_handler(irq, data);
	return IRQ_HANDLED;
}

static int setup_irqs(struct platform_device *pdev)
{
	int ret;
	int irq = platform_get_irq_byname(pdev, "ABX500_TEMP_WARM");

	if (irq < 0) {
		dev_err(&pdev->dev, "Get irq by name failed\n");
		return irq;
	}

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
		abx500_temp_irq_handler, IRQF_NO_SUSPEND, "abx500-temp", pdev);
	if (ret < 0)
		dev_err(&pdev->dev, "Request threaded irq failed (%d)\n", ret);

	return ret;
}

static int abx500_temp_probe(struct platform_device *pdev)
{
	struct abx500_temp *data;
	int err;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->pdev = pdev;
	mutex_init(&data->lock);

	/* Chip specific initialization */
	err = abx500_hwmon_init(data);
	if (err	< 0 || !data->ops.read_sensor || !data->ops.show_name ||
			!data->ops.show_label)
		return err;

	INIT_DEFERRABLE_WORK(&data->work, gpadc_monitor);

	platform_set_drvdata(pdev, data);

	err = sysfs_create_group(&pdev->dev.kobj, &abx500_temp_group);
	if (err < 0) {
		dev_err(&pdev->dev, "Create sysfs group failed (%d)\n", err);
		return err;
	}

	data->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		dev_err(&pdev->dev, "Class registration failed (%d)\n", err);
		goto exit_sysfs_group;
	}

	if (data->ops.irq_handler) {
		err = setup_irqs(pdev);
		if (err < 0)
			goto exit_hwmon_reg;
	}
	return 0;

exit_hwmon_reg:
	hwmon_device_unregister(data->hwmon_dev);
exit_sysfs_group:
	sysfs_remove_group(&pdev->dev.kobj, &abx500_temp_group);
	return err;
}

static int abx500_temp_remove(struct platform_device *pdev)
{
	struct abx500_temp *data = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&data->work);
	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&pdev->dev.kobj, &abx500_temp_group);

	return 0;
}

static int abx500_temp_suspend(struct platform_device *pdev,
			       pm_message_t state)
{
	struct abx500_temp *data = platform_get_drvdata(pdev);

	if (data->work_active)
		cancel_delayed_work_sync(&data->work);

	return 0;
}

static int abx500_temp_resume(struct platform_device *pdev)
{
	struct abx500_temp *data = platform_get_drvdata(pdev);

	if (data->work_active)
		schedule_monitor(data);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id abx500_temp_match[] = {
	{ .compatible = "stericsson,abx500-temp" },
	{},
};
#endif

static struct platform_driver abx500_temp_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "abx500-temp",
		.of_match_table = of_match_ptr(abx500_temp_match),
	},
	.suspend = abx500_temp_suspend,
	.resume = abx500_temp_resume,
	.probe = abx500_temp_probe,
	.remove = abx500_temp_remove,
};

module_platform_driver(abx500_temp_driver);

MODULE_AUTHOR("Martin Persson <martin.persson@stericsson.com>");
MODULE_DESCRIPTION("ABX500 temperature driver");
MODULE_LICENSE("GPL");
