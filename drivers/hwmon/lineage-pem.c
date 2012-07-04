/*
 * Driver for Lineage Compact Power Line series of power entry modules.
 *
 * Copyright (C) 2010, 2011 Ericsson AB.
 *
 * Documentation:
 *  http://www.lineagepower.com/oem/pdf/CPLI2C.pdf
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

/*
 * This driver supports various Lineage Compact Power Line DC/DC and AC/DC
 * converters such as CP1800, CP2000AC, CP2000DC, CP2100DC, and others.
 *
 * The devices are nominally PMBus compliant. However, most standard PMBus
 * commands are not supported. Specifically, all hardware monitoring and
 * status reporting commands are non-standard. For this reason, a standard
 * PMBus driver can not be used.
 *
 * All Lineage CPL devices have a built-in I2C bus master selector (PCA9541).
 * To ensure device access, this driver should only be used as client driver
 * to the pca9541 I2C master selector driver.
 */

/* Command codes */
#define PEM_OPERATION		0x01
#define PEM_CLEAR_INFO_FLAGS	0x03
#define PEM_VOUT_COMMAND	0x21
#define PEM_VOUT_OV_FAULT_LIMIT	0x40
#define PEM_READ_DATA_STRING	0xd0
#define PEM_READ_INPUT_STRING	0xdc
#define PEM_READ_FIRMWARE_REV	0xdd
#define PEM_READ_RUN_TIMER	0xde
#define PEM_FAN_HI_SPEED	0xdf
#define PEM_FAN_NORMAL_SPEED	0xe0
#define PEM_READ_FAN_SPEED	0xe1

/* offsets in data string */
#define PEM_DATA_STATUS_2	0
#define PEM_DATA_STATUS_1	1
#define PEM_DATA_ALARM_2	2
#define PEM_DATA_ALARM_1	3
#define PEM_DATA_VOUT_LSB	4
#define PEM_DATA_VOUT_MSB	5
#define PEM_DATA_CURRENT	6
#define PEM_DATA_TEMP		7

/* Virtual entries, to report constants */
#define PEM_DATA_TEMP_MAX	10
#define PEM_DATA_TEMP_CRIT	11

/* offsets in input string */
#define PEM_INPUT_VOLTAGE	0
#define PEM_INPUT_POWER_LSB	1
#define PEM_INPUT_POWER_MSB	2

/* offsets in fan data */
#define PEM_FAN_ADJUSTMENT	0
#define PEM_FAN_FAN1		1
#define PEM_FAN_FAN2		2
#define PEM_FAN_FAN3		3

/* Status register bits */
#define STS1_OUTPUT_ON		(1 << 0)
#define STS1_LEDS_FLASHING	(1 << 1)
#define STS1_EXT_FAULT		(1 << 2)
#define STS1_SERVICE_LED_ON	(1 << 3)
#define STS1_SHUTDOWN_OCCURRED	(1 << 4)
#define STS1_INT_FAULT		(1 << 5)
#define STS1_ISOLATION_TEST_OK	(1 << 6)

#define STS2_ENABLE_PIN_HI	(1 << 0)
#define STS2_DATA_OUT_RANGE	(1 << 1)
#define STS2_RESTARTED_OK	(1 << 1)
#define STS2_ISOLATION_TEST_FAIL (1 << 3)
#define STS2_HIGH_POWER_CAP	(1 << 4)
#define STS2_INVALID_INSTR	(1 << 5)
#define STS2_WILL_RESTART	(1 << 6)
#define STS2_PEC_ERR		(1 << 7)

/* Alarm register bits */
#define ALRM1_VIN_OUT_LIMIT	(1 << 0)
#define ALRM1_VOUT_OUT_LIMIT	(1 << 1)
#define ALRM1_OV_VOLT_SHUTDOWN	(1 << 2)
#define ALRM1_VIN_OVERCURRENT	(1 << 3)
#define ALRM1_TEMP_WARNING	(1 << 4)
#define ALRM1_TEMP_SHUTDOWN	(1 << 5)
#define ALRM1_PRIMARY_FAULT	(1 << 6)
#define ALRM1_POWER_LIMIT	(1 << 7)

#define ALRM2_5V_OUT_LIMIT	(1 << 1)
#define ALRM2_TEMP_FAULT	(1 << 2)
#define ALRM2_OV_LOW		(1 << 3)
#define ALRM2_DCDC_TEMP_HIGH	(1 << 4)
#define ALRM2_PRI_TEMP_HIGH	(1 << 5)
#define ALRM2_NO_PRIMARY	(1 << 6)
#define ALRM2_FAN_FAULT		(1 << 7)

#define FIRMWARE_REV_LEN	4
#define DATA_STRING_LEN		9
#define INPUT_STRING_LEN	5	/* 4 for most devices	*/
#define FAN_SPEED_LEN		5

struct pem_data {
	struct device *hwmon_dev;

	struct mutex update_lock;
	bool valid;
	bool fans_supported;
	int input_length;
	unsigned long last_updated;	/* in jiffies */

	u8 firmware_rev[FIRMWARE_REV_LEN];
	u8 data_string[DATA_STRING_LEN];
	u8 input_string[INPUT_STRING_LEN];
	u8 fan_speed[FAN_SPEED_LEN];
};

static int pem_read_block(struct i2c_client *client, u8 command, u8 *data,
			  int data_len)
{
	u8 block_buffer[I2C_SMBUS_BLOCK_MAX];
	int result;

	result = i2c_smbus_read_block_data(client, command, block_buffer);
	if (unlikely(result < 0))
		goto abort;
	if (unlikely(result == 0xff || result != data_len)) {
		result = -EIO;
		goto abort;
	}
	memcpy(data, block_buffer, data_len);
	result = 0;
abort:
	return result;
}

static struct pem_data *pem_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pem_data *data = i2c_get_clientdata(client);
	struct pem_data *ret = data;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {
		int result;

		/* Read data string */
		result = pem_read_block(client, PEM_READ_DATA_STRING,
					data->data_string,
					sizeof(data->data_string));
		if (unlikely(result < 0)) {
			ret = ERR_PTR(result);
			goto abort;
		}

		/* Read input string */
		if (data->input_length) {
			result = pem_read_block(client, PEM_READ_INPUT_STRING,
						data->input_string,
						data->input_length);
			if (unlikely(result < 0)) {
				ret = ERR_PTR(result);
				goto abort;
			}
		}

		/* Read fan speeds */
		if (data->fans_supported) {
			result = pem_read_block(client, PEM_READ_FAN_SPEED,
						data->fan_speed,
						sizeof(data->fan_speed));
			if (unlikely(result < 0)) {
				ret = ERR_PTR(result);
				goto abort;
			}
		}

		i2c_smbus_write_byte(client, PEM_CLEAR_INFO_FLAGS);

		data->last_updated = jiffies;
		data->valid = 1;
	}
abort:
	mutex_unlock(&data->update_lock);
	return ret;
}

static long pem_get_data(u8 *data, int len, int index)
{
	long val;

	switch (index) {
	case PEM_DATA_VOUT_LSB:
		val = (data[index] + (data[index+1] << 8)) * 5 / 2;
		break;
	case PEM_DATA_CURRENT:
		val = data[index] * 200;
		break;
	case PEM_DATA_TEMP:
		val = data[index] * 1000;
		break;
	case PEM_DATA_TEMP_MAX:
		val = 97 * 1000;	/* 97 degrees C per datasheet */
		break;
	case PEM_DATA_TEMP_CRIT:
		val = 107 * 1000;	/* 107 degrees C per datasheet */
		break;
	default:
		WARN_ON_ONCE(1);
		val = 0;
	}
	return val;
}

static long pem_get_input(u8 *data, int len, int index)
{
	long val;

	switch (index) {
	case PEM_INPUT_VOLTAGE:
		if (len == INPUT_STRING_LEN)
			val = (data[index] + (data[index+1] << 8) - 75) * 1000;
		else
			val = (data[index] - 75) * 1000;
		break;
	case PEM_INPUT_POWER_LSB:
		if (len == INPUT_STRING_LEN)
			index++;
		val = (data[index] + (data[index+1] << 8)) * 1000000L;
		break;
	default:
		WARN_ON_ONCE(1);
		val = 0;
	}
	return val;
}

static long pem_get_fan(u8 *data, int len, int index)
{
	long val;

	switch (index) {
	case PEM_FAN_FAN1:
	case PEM_FAN_FAN2:
	case PEM_FAN_FAN3:
		val = data[index] * 100;
		break;
	default:
		WARN_ON_ONCE(1);
		val = 0;
	}
	return val;
}

/*
 * Show boolean, either a fault or an alarm.
 * .nr points to the register, .index is the bit mask to check
 */
static ssize_t pem_show_bool(struct device *dev,
			     struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(da);
	struct pem_data *data = pem_update_device(dev);
	u8 status;

	if (IS_ERR(data))
		return PTR_ERR(data);

	status = data->data_string[attr->nr] & attr->index;
	return snprintf(buf, PAGE_SIZE, "%d\n", !!status);
}

static ssize_t pem_show_data(struct device *dev, struct device_attribute *da,
			     char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct pem_data *data = pem_update_device(dev);
	long value;

	if (IS_ERR(data))
		return PTR_ERR(data);

	value = pem_get_data(data->data_string, sizeof(data->data_string),
			     attr->index);

	return snprintf(buf, PAGE_SIZE, "%ld\n", value);
}

static ssize_t pem_show_input(struct device *dev, struct device_attribute *da,
			      char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct pem_data *data = pem_update_device(dev);
	long value;

	if (IS_ERR(data))
		return PTR_ERR(data);

	value = pem_get_input(data->input_string, sizeof(data->input_string),
			      attr->index);

	return snprintf(buf, PAGE_SIZE, "%ld\n", value);
}

static ssize_t pem_show_fan(struct device *dev, struct device_attribute *da,
			    char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct pem_data *data = pem_update_device(dev);
	long value;

	if (IS_ERR(data))
		return PTR_ERR(data);

	value = pem_get_fan(data->fan_speed, sizeof(data->fan_speed),
			    attr->index);

	return snprintf(buf, PAGE_SIZE, "%ld\n", value);
}

/* Voltages */
static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, pem_show_data, NULL,
			  PEM_DATA_VOUT_LSB);
static SENSOR_DEVICE_ATTR_2(in1_alarm, S_IRUGO, pem_show_bool, NULL,
			    PEM_DATA_ALARM_1, ALRM1_VOUT_OUT_LIMIT);
static SENSOR_DEVICE_ATTR_2(in1_crit_alarm, S_IRUGO, pem_show_bool, NULL,
			    PEM_DATA_ALARM_1, ALRM1_OV_VOLT_SHUTDOWN);
static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO, pem_show_input, NULL,
			  PEM_INPUT_VOLTAGE);
static SENSOR_DEVICE_ATTR_2(in2_alarm, S_IRUGO, pem_show_bool, NULL,
			    PEM_DATA_ALARM_1,
			    ALRM1_VIN_OUT_LIMIT | ALRM1_PRIMARY_FAULT);

/* Currents */
static SENSOR_DEVICE_ATTR(curr1_input, S_IRUGO, pem_show_data, NULL,
			  PEM_DATA_CURRENT);
static SENSOR_DEVICE_ATTR_2(curr1_alarm, S_IRUGO, pem_show_bool, NULL,
			    PEM_DATA_ALARM_1, ALRM1_VIN_OVERCURRENT);

/* Power */
static SENSOR_DEVICE_ATTR(power1_input, S_IRUGO, pem_show_input, NULL,
			  PEM_INPUT_POWER_LSB);
static SENSOR_DEVICE_ATTR_2(power1_alarm, S_IRUGO, pem_show_bool, NULL,
			    PEM_DATA_ALARM_1, ALRM1_POWER_LIMIT);

/* Fans */
static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, pem_show_fan, NULL,
			  PEM_FAN_FAN1);
static SENSOR_DEVICE_ATTR(fan2_input, S_IRUGO, pem_show_fan, NULL,
			  PEM_FAN_FAN2);
static SENSOR_DEVICE_ATTR(fan3_input, S_IRUGO, pem_show_fan, NULL,
			  PEM_FAN_FAN3);
static SENSOR_DEVICE_ATTR_2(fan1_alarm, S_IRUGO, pem_show_bool, NULL,
			    PEM_DATA_ALARM_2, ALRM2_FAN_FAULT);

/* Temperatures */
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, pem_show_data, NULL,
			  PEM_DATA_TEMP);
static SENSOR_DEVICE_ATTR(temp1_max, S_IRUGO, pem_show_data, NULL,
			  PEM_DATA_TEMP_MAX);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO, pem_show_data, NULL,
			  PEM_DATA_TEMP_CRIT);
static SENSOR_DEVICE_ATTR_2(temp1_alarm, S_IRUGO, pem_show_bool, NULL,
			    PEM_DATA_ALARM_1, ALRM1_TEMP_WARNING);
static SENSOR_DEVICE_ATTR_2(temp1_crit_alarm, S_IRUGO, pem_show_bool, NULL,
			    PEM_DATA_ALARM_1, ALRM1_TEMP_SHUTDOWN);
static SENSOR_DEVICE_ATTR_2(temp1_fault, S_IRUGO, pem_show_bool, NULL,
			    PEM_DATA_ALARM_2, ALRM2_TEMP_FAULT);

static struct attribute *pem_attributes[] = {
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,

	&sensor_dev_attr_curr1_alarm.dev_attr.attr,

	&sensor_dev_attr_power1_alarm.dev_attr.attr,

	&sensor_dev_attr_fan1_alarm.dev_attr.attr,

	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_fault.dev_attr.attr,

	NULL,
};

static const struct attribute_group pem_group = {
	.attrs = pem_attributes,
};

static struct attribute *pem_input_attributes[] = {
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_curr1_input.dev_attr.attr,
	&sensor_dev_attr_power1_input.dev_attr.attr,
};

static const struct attribute_group pem_input_group = {
	.attrs = pem_input_attributes,
};

static struct attribute *pem_fan_attributes[] = {
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
};

static const struct attribute_group pem_fan_group = {
	.attrs = pem_fan_attributes,
};

static int pem_probe(struct i2c_client *client,
		     const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct pem_data *data;
	int ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BLOCK_DATA
				     | I2C_FUNC_SMBUS_WRITE_BYTE))
		return -ENODEV;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	/*
	 * We use the next two commands to determine if the device is really
	 * there.
	 */
	ret = pem_read_block(client, PEM_READ_FIRMWARE_REV,
			     data->firmware_rev, sizeof(data->firmware_rev));
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte(client, PEM_CLEAR_INFO_FLAGS);
	if (ret < 0)
		return ret;

	dev_info(&client->dev, "Firmware revision %d.%d.%d\n",
		 data->firmware_rev[0], data->firmware_rev[1],
		 data->firmware_rev[2]);

	/* Register sysfs hooks */
	ret = sysfs_create_group(&client->dev.kobj, &pem_group);
	if (ret)
		return ret;

	/*
	 * Check if input readings are supported.
	 * This is the case if we can read input data,
	 * and if the returned data is not all zeros.
	 * Note that input alarms are always supported.
	 */
	ret = pem_read_block(client, PEM_READ_INPUT_STRING,
			     data->input_string,
			     sizeof(data->input_string) - 1);
	if (!ret && (data->input_string[0] || data->input_string[1] ||
		     data->input_string[2]))
		data->input_length = sizeof(data->input_string) - 1;
	else if (ret < 0) {
		/* Input string is one byte longer for some devices */
		ret = pem_read_block(client, PEM_READ_INPUT_STRING,
				    data->input_string,
				    sizeof(data->input_string));
		if (!ret && (data->input_string[0] || data->input_string[1] ||
			    data->input_string[2] || data->input_string[3]))
			data->input_length = sizeof(data->input_string);
	}
	ret = 0;
	if (data->input_length) {
		ret = sysfs_create_group(&client->dev.kobj, &pem_input_group);
		if (ret)
			goto out_remove_groups;
	}

	/*
	 * Check if fan speed readings are supported.
	 * This is the case if we can read fan speed data,
	 * and if the returned data is not all zeros.
	 * Note that the fan alarm is always supported.
	 */
	ret = pem_read_block(client, PEM_READ_FAN_SPEED,
			     data->fan_speed,
			     sizeof(data->fan_speed));
	if (!ret && (data->fan_speed[0] || data->fan_speed[1] ||
		     data->fan_speed[2] || data->fan_speed[3])) {
		data->fans_supported = true;
		ret = sysfs_create_group(&client->dev.kobj, &pem_fan_group);
		if (ret)
			goto out_remove_groups;
	}

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		ret = PTR_ERR(data->hwmon_dev);
		goto out_remove_groups;
	}

	return 0;

out_remove_groups:
	sysfs_remove_group(&client->dev.kobj, &pem_input_group);
	sysfs_remove_group(&client->dev.kobj, &pem_fan_group);
	sysfs_remove_group(&client->dev.kobj, &pem_group);
	return ret;
}

static int pem_remove(struct i2c_client *client)
{
	struct pem_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);

	sysfs_remove_group(&client->dev.kobj, &pem_input_group);
	sysfs_remove_group(&client->dev.kobj, &pem_fan_group);
	sysfs_remove_group(&client->dev.kobj, &pem_group);

	return 0;
}

static const struct i2c_device_id pem_id[] = {
	{"lineage_pem", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, pem_id);

static struct i2c_driver pem_driver = {
	.driver = {
		   .name = "lineage_pem",
		   },
	.probe = pem_probe,
	.remove = pem_remove,
	.id_table = pem_id,
};

module_i2c_driver(pem_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("Lineage CPL PEM hardware monitoring driver");
MODULE_LICENSE("GPL");
