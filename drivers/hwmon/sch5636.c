// SPDX-License-Identifier: GPL-2.0-or-later
/***************************************************************************
 *   Copyright (C) 2011-2012 Hans de Goede <hdegoede@redhat.com>           *
 *                                                                         *
 ***************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/mod_devicetable.h>
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

	struct mutex update_lock;
	bool valid;			/* true if following fields are valid */
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
	data->valid = true;
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

static ssize_t name_show(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	return sysfs_emit(buf, "%s\n", DEVNAME);
}

static ssize_t in_value_show(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct sch5636_data *data = sch5636_update_device(dev);
	int val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	val = DIV_ROUND_CLOSEST(
		data->in[attr->index] * SCH5636_REG_IN_FACTORS[attr->index],
		255);
	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t in_label_show(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);

	return sysfs_emit(buf, "%s\n",
			  SCH5636_IN_LABELS[attr->index]);
}

static ssize_t temp_value_show(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct sch5636_data *data = sch5636_update_device(dev);
	int val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	val = (data->temp_val[attr->index] - 64) * 1000;
	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t temp_fault_show(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct sch5636_data *data = sch5636_update_device(dev);
	int val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	val = (data->temp_ctrl[attr->index] & SCH5636_TEMP_WORKING) ? 0 : 1;
	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t temp_alarm_show(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct sch5636_data *data = sch5636_update_device(dev);
	int val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	val = (data->temp_ctrl[attr->index] & SCH5636_TEMP_ALARM) ? 1 : 0;
	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t fan_value_show(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct sch5636_data *data = sch5636_update_device(dev);
	int val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	val = reg_to_rpm(data->fan_val[attr->index]);
	if (val < 0)
		return val;

	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t fan_fault_show(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct sch5636_data *data = sch5636_update_device(dev);
	int val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	val = (data->fan_ctrl[attr->index] & SCH5636_FAN_NOT_PRESENT) ? 1 : 0;
	return sysfs_emit(buf, "%d\n", val);
}

static ssize_t fan_alarm_show(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct sch5636_data *data = sch5636_update_device(dev);
	int val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	val = (data->fan_ctrl[attr->index] & SCH5636_FAN_ALARM) ? 1 : 0;
	return sysfs_emit(buf, "%d\n", val);
}

static struct sensor_device_attribute sch5636_attr[] = {
	SENSOR_ATTR_RO(name, name, 0),
	SENSOR_ATTR_RO(in0_input, in_value, 0),
	SENSOR_ATTR_RO(in0_label, in_label, 0),
	SENSOR_ATTR_RO(in1_input, in_value, 1),
	SENSOR_ATTR_RO(in1_label, in_label, 1),
	SENSOR_ATTR_RO(in2_input, in_value, 2),
	SENSOR_ATTR_RO(in2_label, in_label, 2),
	SENSOR_ATTR_RO(in3_input, in_value, 3),
	SENSOR_ATTR_RO(in3_label, in_label, 3),
	SENSOR_ATTR_RO(in4_input, in_value, 4),
	SENSOR_ATTR_RO(in4_label, in_label, 4),
};

static struct sensor_device_attribute sch5636_temp_attr[] = {
	SENSOR_ATTR_RO(temp1_input, temp_value, 0),
	SENSOR_ATTR_RO(temp1_fault, temp_fault, 0),
	SENSOR_ATTR_RO(temp1_alarm, temp_alarm, 0),
	SENSOR_ATTR_RO(temp2_input, temp_value, 1),
	SENSOR_ATTR_RO(temp2_fault, temp_fault, 1),
	SENSOR_ATTR_RO(temp2_alarm, temp_alarm, 1),
	SENSOR_ATTR_RO(temp3_input, temp_value, 2),
	SENSOR_ATTR_RO(temp3_fault, temp_fault, 2),
	SENSOR_ATTR_RO(temp3_alarm, temp_alarm, 2),
	SENSOR_ATTR_RO(temp4_input, temp_value, 3),
	SENSOR_ATTR_RO(temp4_fault, temp_fault, 3),
	SENSOR_ATTR_RO(temp4_alarm, temp_alarm, 3),
	SENSOR_ATTR_RO(temp5_input, temp_value, 4),
	SENSOR_ATTR_RO(temp5_fault, temp_fault, 4),
	SENSOR_ATTR_RO(temp5_alarm, temp_alarm, 4),
	SENSOR_ATTR_RO(temp6_input, temp_value, 5),
	SENSOR_ATTR_RO(temp6_fault, temp_fault, 5),
	SENSOR_ATTR_RO(temp6_alarm, temp_alarm, 5),
	SENSOR_ATTR_RO(temp7_input, temp_value, 6),
	SENSOR_ATTR_RO(temp7_fault, temp_fault, 6),
	SENSOR_ATTR_RO(temp7_alarm, temp_alarm, 6),
	SENSOR_ATTR_RO(temp8_input, temp_value, 7),
	SENSOR_ATTR_RO(temp8_fault, temp_fault, 7),
	SENSOR_ATTR_RO(temp8_alarm, temp_alarm, 7),
	SENSOR_ATTR_RO(temp9_input, temp_value, 8),
	SENSOR_ATTR_RO(temp9_fault, temp_fault, 8),
	SENSOR_ATTR_RO(temp9_alarm, temp_alarm, 8),
	SENSOR_ATTR_RO(temp10_input, temp_value, 9),
	SENSOR_ATTR_RO(temp10_fault, temp_fault, 9),
	SENSOR_ATTR_RO(temp10_alarm, temp_alarm, 9),
	SENSOR_ATTR_RO(temp11_input, temp_value, 10),
	SENSOR_ATTR_RO(temp11_fault, temp_fault, 10),
	SENSOR_ATTR_RO(temp11_alarm, temp_alarm, 10),
	SENSOR_ATTR_RO(temp12_input, temp_value, 11),
	SENSOR_ATTR_RO(temp12_fault, temp_fault, 11),
	SENSOR_ATTR_RO(temp12_alarm, temp_alarm, 11),
	SENSOR_ATTR_RO(temp13_input, temp_value, 12),
	SENSOR_ATTR_RO(temp13_fault, temp_fault, 12),
	SENSOR_ATTR_RO(temp13_alarm, temp_alarm, 12),
	SENSOR_ATTR_RO(temp14_input, temp_value, 13),
	SENSOR_ATTR_RO(temp14_fault, temp_fault, 13),
	SENSOR_ATTR_RO(temp14_alarm, temp_alarm, 13),
	SENSOR_ATTR_RO(temp15_input, temp_value, 14),
	SENSOR_ATTR_RO(temp15_fault, temp_fault, 14),
	SENSOR_ATTR_RO(temp15_alarm, temp_alarm, 14),
	SENSOR_ATTR_RO(temp16_input, temp_value, 15),
	SENSOR_ATTR_RO(temp16_fault, temp_fault, 15),
	SENSOR_ATTR_RO(temp16_alarm, temp_alarm, 15),
};

static struct sensor_device_attribute sch5636_fan_attr[] = {
	SENSOR_ATTR_RO(fan1_input, fan_value, 0),
	SENSOR_ATTR_RO(fan1_fault, fan_fault, 0),
	SENSOR_ATTR_RO(fan1_alarm, fan_alarm, 0),
	SENSOR_ATTR_RO(fan2_input, fan_value, 1),
	SENSOR_ATTR_RO(fan2_fault, fan_fault, 1),
	SENSOR_ATTR_RO(fan2_alarm, fan_alarm, 1),
	SENSOR_ATTR_RO(fan3_input, fan_value, 2),
	SENSOR_ATTR_RO(fan3_fault, fan_fault, 2),
	SENSOR_ATTR_RO(fan3_alarm, fan_alarm, 2),
	SENSOR_ATTR_RO(fan4_input, fan_value, 3),
	SENSOR_ATTR_RO(fan4_fault, fan_fault, 3),
	SENSOR_ATTR_RO(fan4_alarm, fan_alarm, 3),
	SENSOR_ATTR_RO(fan5_input, fan_value, 4),
	SENSOR_ATTR_RO(fan5_fault, fan_fault, 4),
	SENSOR_ATTR_RO(fan5_alarm, fan_alarm, 4),
	SENSOR_ATTR_RO(fan6_input, fan_value, 5),
	SENSOR_ATTR_RO(fan6_fault, fan_fault, 5),
	SENSOR_ATTR_RO(fan6_alarm, fan_alarm, 5),
	SENSOR_ATTR_RO(fan7_input, fan_value, 6),
	SENSOR_ATTR_RO(fan7_fault, fan_fault, 6),
	SENSOR_ATTR_RO(fan7_alarm, fan_alarm, 6),
	SENSOR_ATTR_RO(fan8_input, fan_value, 7),
	SENSOR_ATTR_RO(fan8_fault, fan_fault, 7),
	SENSOR_ATTR_RO(fan8_alarm, fan_alarm, 7),
};

static void sch5636_remove(struct platform_device *pdev)
{
	struct sch5636_data *data = platform_get_drvdata(pdev);
	int i;

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
}

static int sch5636_probe(struct platform_device *pdev)
{
	struct sch5636_data *data;
	int i, err, val, revision[2];
	char id[4];

	data = devm_kzalloc(&pdev->dev, sizeof(struct sch5636_data),
			    GFP_KERNEL);
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
		pr_err("Unknown Fujitsu id: %3pE (%3ph)\n", id, id);
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
	pr_info("Found %s chip at %#hx, revision: %d.%02d\n", DEVNAME,
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
	sch56xx_watchdog_register(&pdev->dev, data->addr, (revision[0] << 8) | revision[1],
				  &data->update_lock, 0);

	return 0;

error:
	sch5636_remove(pdev);
	return err;
}

static const struct platform_device_id sch5636_device_id[] = {
	{
		.name = "sch5636",
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, sch5636_device_id);

static struct platform_driver sch5636_driver = {
	.driver = {
		.name	= DRVNAME,
	},
	.probe		= sch5636_probe,
	.remove		= sch5636_remove,
	.id_table	= sch5636_device_id,
};

module_platform_driver(sch5636_driver);

MODULE_DESCRIPTION("SMSC SCH5636 Hardware Monitoring Driver");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL");
