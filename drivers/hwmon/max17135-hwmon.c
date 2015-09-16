/*
 * Copyright (C) 2010-2015 Freescale Semiconductor, Inc.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
/*
 * max17135.c
 *
 * Based on the MAX1619 driver.
 * Copyright (C) 2003-2004 Alexey Fisher <fishor@mail.ru>
 *                         Jean Delvare <khali@linux-fr.org>
 *
 * The MAX17135 is a sensor chip made by Maxim.
 * It reports up to two temperatures (its own plus up to
 * one external one).
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/sysfs.h>
#include <linux/platform_device.h>
#include <linux/mfd/max17135.h>

/*
 * Conversions
 */
static int temp_from_reg(int val)
{
	return val >> 8;
}

/*
 * Functions declaration
 */
static int max17135_sensor_probe(struct platform_device *pdev);
static int max17135_sensor_remove(struct platform_device *pdev);

static const struct platform_device_id max17135_sns_id[] = {
	{ "max17135-sns", 0},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, max17135_sns_id);

/*
 * Driver data (common to all clients)
 */
static struct platform_driver max17135_sensor_driver = {
	.probe = max17135_sensor_probe,
	.remove = max17135_sensor_remove,
	.id_table = max17135_sns_id,
	.driver = {
		.name = "max17135_sensor",
	},
};

/*
 * Client data (each client gets its own)
 */
struct max17135_data {
	struct device *hwmon_dev;
};

/*
 * Sysfs stuff
 */
static ssize_t show_temp_input1(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	unsigned int reg_val;
	max17135_reg_read(REG_MAX17135_INT_TEMP, &reg_val);
	return snprintf(buf, PAGE_SIZE, "%d\n", temp_from_reg(reg_val));
}

static ssize_t show_temp_input2(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	unsigned int reg_val;
	max17135_reg_read(REG_MAX17135_EXT_TEMP, &reg_val);
	return snprintf(buf, PAGE_SIZE, "%d\n", temp_from_reg(reg_val));
}

static DEVICE_ATTR(temp1_input, S_IRUGO, show_temp_input1, NULL);
static DEVICE_ATTR(temp2_input, S_IRUGO, show_temp_input2, NULL);

static struct attribute *max17135_attributes[] = {
	&dev_attr_temp1_input.attr,
	&dev_attr_temp2_input.attr,
	NULL
};

static const struct attribute_group max17135_group = {
	.attrs = max17135_attributes,
};

/*
 * Real code
 */
static int max17135_sensor_probe(struct platform_device *pdev)
{
	struct max17135_data *data;
	int err;

	data = kzalloc(sizeof(struct max17135_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	/* Register sysfs hooks */
	err = sysfs_create_group(&pdev->dev.kobj, &max17135_group);
	if (err)
		goto exit_free;

	data->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove_files;
	}

	platform_set_drvdata(pdev, data);

	return 0;

exit_remove_files:
	sysfs_remove_group(&pdev->dev.kobj, &max17135_group);
exit_free:
	kfree(data);
exit:
	return err;
}

static int max17135_sensor_remove(struct platform_device *pdev)
{
	struct max17135_data *data = platform_get_drvdata(pdev);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&pdev->dev.kobj, &max17135_group);

	kfree(data);
	return 0;
}

static int __init sensors_max17135_init(void)
{
	return platform_driver_register(&max17135_sensor_driver);
}
module_init(sensors_max17135_init);

static void __exit sensors_max17135_exit(void)
{
	platform_driver_unregister(&max17135_sensor_driver);
}
module_exit(sensors_max17135_exit);

MODULE_DESCRIPTION("MAX17135 sensor driver");
MODULE_LICENSE("GPL");

