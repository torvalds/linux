/***************************************************************************
 *   Copyright (C) 2011-2012 Hans de Goede <hdegoede@redhat.com>           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include "sch56xx-common.h"

#define DRVNAME "sch5636"
#define DEVNAME "theseus" /* We only support one model for now */

#define SCH5636_REG_FUJITSU_ID		0x780
#define SCH5636_REG_FUJITSU_REV		0x783

#define SCH5636_NO_INS			5
#define SCH5636_NO_TEMPS		16
#define SCH5636_NO_FANS			8

static const u16 SCH5636_REG_IN_VAL[SCH5636_NO_INS] = {
	0x22, 0x23, 0x24, 0x25, 0x189 };
static const u16 SCH5636_REG_IN_FACTORS[SCH5636_NO_INS] = {
	4400, 1500, 4000, 4400, 16000 };
static const char * const SCH5636_IN_LABELS[SCH5636_NO_INS] = {
	"3.3V", "VREF", "VBAT", "3.3AUX", "12V" };

static const u16 SCH5636_REG_TEMP_VAL[SCH5636_NO_TEMPS] = {
	0x2B, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x180, 0x181,
	0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C };
#define SCH5636_REG_TEMP_CTRL(i)	(0x790 + (i))
#define SCH5636_TEMP_WORKING		0x01
#define SCH5636_TEMP_ALARM		0x02
#define SCH5636_TEMP_DEACTIVATED	0x80

static const u16 SCH5636_REG_FAN_VAL[SCH5636_NO_FANS] = {
	0x2C, 0x2E, 0x30, 0x32, 0x62, 0x64, 0x66, 0x68 };
#define SCH5636_REG_FAN_CTRL(i)		(0x880 + (i))
/* FAULT in datasheet, but acts as an alarm */
#define SCH5636_FAN_ALARM		0x04
#define SCH5636_FAN_NOT_PRESENT		0x08
#define SCH5636_FAN_DEACTIVATED		0x80


struct sch5636_data {
	unsigned short addr;
	struct device *hwmon_dev;
	struct sch56xx_watchdog_data *watchdog;

	struct mutex update_lock;
	char valid;			/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */
	u8 in[SCH5636_NO_INS];
	u8 temp_val[SCH5636_NO_TEMPS];
	u8 temp_ctrl[SCH5636_NO_TEMPS];
	u16 fan_val[SCH5636_NO_FANS];
	u8 fan_ctrl[SCH5636_NO_FANS];
};

static struct sch5636_data *sch5636_update_device(struct device *dev)
{
	struct sch5636_data *data = dev_get_drvdata(dev);
	struct sch5636_data *ret = data;
	int i, val;

	mutex_lock(&data->update_lock);

	/* Cache the values for 1 second */
	if (data->valid && !time_after(jiffies, data->last_updated + HZ))
		goto abort;

	for (i = 0; i < SCH5636_NO_INS; i++) {
		val = sch56xx_read_virtual_reg(data->addr,
					       SCH5636_REG_IN_VAL[i]);
		if (unlikely(val < 0)) {
			ret = ERR_PTR(val);
			goto abort;
		}
		data->in[i] = val;
	}

	for (i = 0; i < SCH5636_NO_TEMPS; i++) {
		if (data->temp_ctrl[i] & SCH5636_TEMP_DEACTIVATED)
			continue;

		val = sch56xx_read_virtual_reg(data->addr,
					       SCH5636_REG_TEMP_VAL[i]);
		if (unlikely(val < 0)) {
			ret = ERR_PTR(val);
			goto abort;
		}
		data->temp_val[i] = val;

		val = sch56xx_read_virtual_reg(data->addr,
					       SCH5636_REG_TEMP_CTRL(i));
		if (unlikely(val < 0)) {
			ret = ERR_PTR(val);
			goto abort;
		}
		data->temp_ctrl[i] = val;
		/* Alarms need to be explicitly write-cleared */
		if (val & SCH5636_TEMP_ALARM) {
			sch56xx_write_virtual_reg(data->addr,
						SCH5636_REG_TEMP_CTRL(i), val);
		}
	}

	for (i = 0; i < SCH5636_NO_FANS; i++) {
		if (data->fan_ctrl[i] & SCH5636_FAN_DEACTIVATED)
			continue;

		val = sch56xx_read_virtual_reg16(data->addr,
						 SCH5636_REG_FAN_VAL[i]);
		if (unlikely(val < 0)) {
			ret = ERR_PTR(val);
			goto abort;
		}
		data->fan_val[i] = val;

		val = sch56xx_read_virtual_reg(data->addr,
					       SCH5636_REG_FAN_CTRL(i));
		if (unlikely(val < 0)) {
			ret = ERR_PTR(val);
			goto abort;
		}
		data->fan_ctrl[i] = val;
		/* Alarms need to be explicitly write-cleared */
		if (val & SCH5636_FAN_ALARM) {
			sch56xx_write_virtual_reg(data->addr,
						SCH5636_REG_FAN_CTRL(i), val);
		}
	}

	data->last_updated = jiffies;
	data->valid = 1;
abort:
	mutex_unlock(&data->update_lock);
	return ret;
}

static int reg_to_rpm(u16 reg)
{
	if (reg == 0)
		return -EIO;
	if (reg == 0xffff)
		return 0;

	return 5400540 / reg;
}

static ssize_t show_name(struct device *dev, struct device_attribute *devattr,
	char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", DEVNAME);
}

static ssize_t show_in_value(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct sch5636_data *data = sch5636_update_device(dev);
	int val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	val = DIV_ROUND_CLOSEST(
		data->in[attr->index] * SCH5636_REG_IN_FACTORS[attr->index],
		255);
	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t show_in_label(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	return snprintf(buf, PAGE_SIZE, "%s\n",
			SCH5636_IN_LABELS[attr->index]);
}

static ssize_t show_temp_value(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct sch5636_data *data = sch5636_update_device(dev);
	int val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	val = (data->temp_val[attr->index] - 64) * 1000;
	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t show_temp_fault(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct sch5636_data *data = sch5636_update_device(dev);
	int val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	val = (data->temp_ctrl[attr->index] & SCH5636_TEMP_WORKING) ? 0 : 1;
	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t show_temp_alarm(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct sch5636_data *data = sch5636_update_device(dev);
	int val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	val = (data->temp_ctrl[attr->index] & SCH5636_TEMP_ALARM) ? 1 : 0;
	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t show_fan_value(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct sch5636_data *data = sch5636_update_device(dev);
	int val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	val = reg_to_rpm(data->fan_val[attr->index]);
	if (val < 0)
		return val;

	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t show_fan_fault(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct sch5636_data *data = sch5636_update_device(dev);
	int val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	val = (data->fan_ctrl[attr->index] & SCH5636_FAN_NOT_PRESENT) ? 1 : 0;
	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t show_fan_alarm(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct sch5636_data *data = sch5636_update_device(dev);
	int val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	val = (data->fan_ctrl[attr->index] & SCH5636_FAN_ALARM) ? 1 : 0;
	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static struct sensor_device_attribute sch5636_attr[] = {
	SENSOR_ATTR(name, 0444, show_name, NULL, 0),
	SENSOR_ATTR(in0_input, 0444, show_in_value, NULL, 0),
	SENSOR_ATTR(in0_label, 0444, show_in_label, NULL, 0),
	SENSOR_ATTR(in1_input, 0444, show_in_value, NULL, 1),
	SENSOR_ATTR(in1_label, 0444, show_in_label, NULL, 1),
	SENSOR_ATTR(in2_input, 0444, show_in_value, NULL, 2),
	SENSOR_ATTR(in2_label, 0444, show_in_label, NULL, 2),
	SENSOR_ATTR(in3_input, 0444, show_in_value, NULL, 3),
	SENSOR_ATTR(in3_label, 0444, show_in_label, NULL, 3),
	SENSOR_ATTR(in4_input, 0444, show_in_value, NULL, 4),
	SENSOR_ATTR(in4_label, 0444, show_in_label, NULL, 4),
};

static struct sensor_device_attribute sch5636_temp_attr[] = {
	SENSOR_ATTR(temp1_input, 0444, show_temp_value, NULL, 0),
	SENSOR_ATTR(temp1_fault, 0444, show_temp_fault, NULL, 0),
	SENSOR_ATTR(temp1_alarm, 0444, show_temp_alarm, NULL, 0),
	SENSOR_ATTR(temp2_input, 0444, show_temp_value, NULL, 1),
	SENSOR_ATTR(temp2_fault, 0444, show_temp_fault, NULL, 1),
	SENSOR_ATTR(temp2_alarm, 0444, show_temp_alarm, NULL, 1),
	SENSOR_ATTR(temp3_input, 0444, show_temp_value, NULL, 2),
	SENSOR_ATTR(temp3_fault, 0444, show_temp_fault, NULL, 2),
	SENSOR_ATTR(temp3_alarm, 0444, show_temp_alarm, NULL, 2),
	SENSOR_ATTR(temp4_input, 0444, show_temp_value, NULL, 3),
	SENSOR_ATTR(temp4_fault, 0444, show_temp_fault, NULL, 3),
	SENSOR_ATTR(temp4_alarm, 0444, show_temp_alarm, NULL, 3),
	SENSOR_ATTR(temp5_input, 0444, show_temp_value, NULL, 4),
	SENSOR_ATTR(temp5_fault, 0444, show_temp_fault, NULL, 4),
	SENSOR_ATTR(temp5_alarm, 0444, show_temp_alarm, NULL, 4),
	SENSOR_ATTR(temp6_input, 0444, show_temp_value, NULL, 5),
	SENSOR_ATTR(temp6_fault, 0444, show_temp_fault, NULL, 5),
	SENSOR_ATTR(temp6_alarm, 0444, show_temp_alarm, NULL, 5),
	SENSOR_ATTR(temp7_input, 0444, show_temp_value, NULL, 6),
	SENSOR_ATTR(temp7_fault, 0444, show_temp_fault, NULL, 6),
	SENSOR_ATTR(temp7_alarm, 0444, show_temp_alarm, NULL, 6),
	SENSOR_ATTR(temp8_input, 0444, show_temp_value, NULL, 7),
	SENSOR_ATTR(temp8_fault, 0444, show_temp_fault, NULL, 7),
	SENSOR_ATTR(temp8_alarm, 0444, show_temp_alarm, NULL, 7),
	SENSOR_ATTR(temp9_input, 0444, show_temp_value, NULL, 8),
	SENSOR_ATTR(temp9_fault, 0444, show_temp_fault, NULL, 8),
	SENSOR_ATTR(temp9_alarm, 0444, show_temp_alarm, NULL, 8),
	SENSOR_ATTR(temp10_input, 0444, show_temp_value, NULL, 9),
	SENSOR_ATTR(temp10_fault, 0444, show_temp_fault, NULL, 9),
	SENSOR_ATTR(temp10_alarm, 0444, show_temp_alarm, NULL, 9),
	SENSOR_ATTR(temp11_input, 0444, show_temp_value, NULL, 10),
	SENSOR_ATTR(temp11_fault, 0444, show_temp_fault, NULL, 10),
	SENSOR_ATTR(temp11_alarm, 0444, show_temp_alarm, NULL, 10),
	SENSOR_ATTR(temp12_input, 0444, show_temp_value, NULL, 11),
	SENSOR_ATTR(temp12_fault, 0444, show_temp_fault, NULL, 11),
	SENSOR_ATTR(temp12_alarm, 0444, show_temp_alarm, NULL, 11),
	SENSOR_ATTR(temp13_input, 0444, show_temp_value, NULL, 12),
	SENSOR_ATTR(temp13_fault, 0444, show_temp_fault, NULL, 12),
	SENSOR_ATTR(temp13_alarm, 0444, show_temp_alarm, NULL, 12),
	SENSOR_ATTR(temp14_input, 0444, show_temp_value, NULL, 13),
	SENSOR_ATTR(temp14_fault, 0444, show_temp_fault, NULL, 13),
	SENSOR_ATTR(temp14_alarm, 0444, show_temp_alarm, NULL, 13),
	SENSOR_ATTR(temp15_input, 0444, show_temp_value, NULL, 14),
	SENSOR_ATTR(temp15_fault, 0444, show_temp_fault, NULL, 14),
	SENSOR_ATTR(temp15_alarm, 0444, show_temp_alarm, NULL, 14),
	SENSOR_ATTR(temp16_input, 0444, show_temp_value, NULL, 15),
	SENSOR_ATTR(temp16_fault, 0444, show_temp_fault, NULL, 15),
	SENSOR_ATTR(temp16_alarm, 0444, show_temp_alarm, NULL, 15),
};

static struct sensor_device_attribute sch5636_fan_attr[] = {
	SENSOR_ATTR(fan1_input, 0444, show_fan_value, NULL, 0),
	SENSOR_ATTR(fan1_fault, 0444, show_fan_fault, NULL, 0),
	SENSOR_ATTR(fan1_alarm, 0444, show_fan_alarm, NULL, 0),
	SENSOR_ATTR(fan2_input, 0444, show_fan_value, NULL, 1),
	SENSOR_ATTR(fan2_fault, 0444, show_fan_fault, NULL, 1),
	SENSOR_ATTR(fan2_alarm, 0444, show_fan_alarm, NULL, 1),
	SENSOR_ATTR(fan3_input, 0444, show_fan_value, NULL, 2),
	SENSOR_ATTR(fan3_fault, 0444, show_fan_fault, NULL, 2),
	SENSOR_ATTR(fan3_alarm, 0444, show_fan_alarm, NULL, 2),
	SENSOR_ATTR(fan4_input, 0444, show_fan_value, NULL, 3),
	SENSOR_ATTR(fan4_fault, 0444, show_fan_fault, NULL, 3),
	SENSOR_ATTR(fan4_alarm, 0444, show_fan_alarm, NULL, 3),
	SENSOR_ATTR(fan5_input, 0444, show_fan_value, NULL, 4),
	SENSOR_ATTR(fan5_fault, 0444, show_fan_fault, NULL, 4),
	SENSOR_ATTR(fan5_alarm, 0444, show_fan_alarm, NULL, 4),
	SENSOR_ATTR(fan6_input, 0444, show_fan_value, NULL, 5),
	SENSOR_ATTR(fan6_fault, 0444, show_fan_fault, NULL, 5),
	SENSOR_ATTR(fan6_alarm, 0444, show_fan_alarm, NULL, 5),
	SENSOR_ATTR(fan7_input, 0444, show_fan_value, NULL, 6),
	SENSOR_ATTR(fan7_fault, 0444, show_fan_fault, NULL, 6),
	SENSOR_ATTR(fan7_alarm, 0444, show_fan_alarm, NULL, 6),
	SENSOR_ATTR(fan8_input, 0444, show_fan_value, NULL, 7),
	SENSOR_ATTR(fan8_fault, 0444, show_fan_fault, NULL, 7),
	SENSOR_ATTR(fan8_alarm, 0444, show_fan_alarm, NULL, 7),
};

static int sch5636_remove(struct platform_device *pdev)
{
	struct sch5636_data *data = platform_get_drvdata(pdev);
	int i;

	if (data->watchdog)
		sch56xx_watchdog_unregister(data->watchdog);

	if (data->hwmon_dev)
		hwmon_device_unregister(data->hwmon_dev);

	for (i = 0; i < ARRAY_SIZE(sch5636_attr); i++)
		device_remove_file(&pdev->dev, &sch5636_attr[i].dev_attr);

	for (i = 0; i < SCH5636_NO_TEMPS * 3; i++)
		device_remove_file(&pdev->dev,
				   &sch5636_temp_attr[i].dev_attr);

	for (i = 0; i < SCH5636_NO_FANS * 3; i++)
		device_remove_file(&pdev->dev,
				   &sch5636_fan_attr[i].dev_attr);

	platform_set_drvdata(pdev, NULL);
	kfree(data);

	return 0;
}

static int __devinit sch5636_probe(struct platform_device *pdev)
{
	struct sch5636_data *data;
	int i, err, val, revision[2];
	char id[4];

	data = kzalloc(sizeof(struct sch5636_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->addr = platform_get_resource(pdev, IORESOURCE_IO, 0)->start;
	mutex_init(&data->update_lock);
	platform_set_drvdata(pdev, data);

	for (i = 0; i < 3; i++) {
		val = sch56xx_read_virtual_reg(data->addr,
					       SCH5636_REG_FUJITSU_ID + i);
		if (val < 0) {
			pr_err("Could not read Fujitsu id byte at %#x\n",
				SCH5636_REG_FUJITSU_ID + i);
			err = val;
			goto error;
		}
		id[i] = val;
	}
	id[i] = '\0';

	if (strcmp(id, "THS")) {
		pr_err("Unknown Fujitsu id: %02x%02x%02x\n",
		       id[0], id[1], id[2]);
		err = -ENODEV;
		goto error;
	}

	for (i = 0; i < 2; i++) {
		val = sch56xx_read_virtual_reg(data->addr,
					       SCH5636_REG_FUJITSU_REV + i);
		if (val < 0) {
			err = val;
			goto error;
		}
		revision[i] = val;
	}
	pr_info("Found %s chip at %#hx, revison: %d.%02d\n", DEVNAME,
		data->addr, revision[0], revision[1]);

	/* Read all temp + fan ctrl registers to determine which are active */
	for (i = 0; i < SCH5636_NO_TEMPS; i++) {
		val = sch56xx_read_virtual_reg(data->addr,
					       SCH5636_REG_TEMP_CTRL(i));
		if (unlikely(val < 0)) {
			err = val;
			goto error;
		}
		data->temp_ctrl[i] = val;
	}

	for (i = 0; i < SCH5636_NO_FANS; i++) {
		val = sch56xx_read_virtual_reg(data->addr,
					       SCH5636_REG_FAN_CTRL(i));
		if (unlikely(val < 0)) {
			err = val;
			goto error;
		}
		data->fan_ctrl[i] = val;
	}

	for (i = 0; i < ARRAY_SIZE(sch5636_attr); i++) {
		err = device_create_file(&pdev->dev,
					 &sch5636_attr[i].dev_attr);
		if (err)
			goto error;
	}

	for (i = 0; i < (SCH5636_NO_TEMPS * 3); i++) {
		if (data->temp_ctrl[i/3] & SCH5636_TEMP_DEACTIVATED)
			continue;

		err = device_create_file(&pdev->dev,
					&sch5636_temp_attr[i].dev_attr);
		if (err)
			goto error;
	}

	for (i = 0; i < (SCH5636_NO_FANS * 3); i++) {
		if (data->fan_ctrl[i/3] & SCH5636_FAN_DEACTIVATED)
			continue;

		err = device_create_file(&pdev->dev,
					&sch5636_fan_attr[i].dev_attr);
		if (err)
			goto error;
	}

	data->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		data->hwmon_dev = NULL;
		goto error;
	}

	/* Note failing to register the watchdog is not a fatal error */
	data->watchdog = sch56xx_watchdog_register(data->addr,
					(revision[0] << 8) | revision[1],
					&data->update_lock, 0);

	return 0;

error:
	sch5636_remove(pdev);
	return err;
}

static struct platform_driver sch5636_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRVNAME,
	},
	.probe		= sch5636_probe,
	.remove		= sch5636_remove,
};

module_platform_driver(sch5636_driver);

MODULE_DESCRIPTION("SMSC SCH5636 Hardware Monitoring Driver");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL");
