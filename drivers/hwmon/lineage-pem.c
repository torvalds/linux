// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Lineage Compact Power Line series of power entry modules.
 *
 * Copyright (C) 2010, 2011 Ericsson AB.
 *
 * Documentation:
 *  http://www.lineagepower.com/oem/pdf/CPLI2C.pdf
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/jiffies.h>

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
	struct i2c_client *client;
	const struct attribute_group *groups[4];

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
	struct pem_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
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
static ssize_t pem_bool_show(struct device *dev, struct device_attribute *da,
			     char *buf)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(da);
	struct pem_data *data = pem_update_device(dev);
	u8 status;

	if (IS_ERR(data))
		return PTR_ERR(data);

	status = data->data_string[attr->nr] & attr->index;
	return snprintf(buf, PAGE_SIZE, "%d\n", !!status);
}

static ssize_t pem_data_show(struct device *dev, struct device_attribute *da,
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

static ssize_t pem_input_show(struct device *dev, struct device_attribute *da,
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

static ssize_t pem_fan_show(struct device *dev, struct device_attribute *da,
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
static SENSOR_DEVICE_ATTR_RO(in1_input, pem_data, PEM_DATA_VOUT_LSB);
static SENSOR_DEVICE_ATTR_2_RO(in1_alarm, pem_bool, PEM_DATA_ALARM_1,
			       ALRM1_VOUT_OUT_LIMIT);
static SENSOR_DEVICE_ATTR_2_RO(in1_crit_alarm, pem_bool, PEM_DATA_ALARM_1,
			       ALRM1_OV_VOLT_SHUTDOWN);
static SENSOR_DEVICE_ATTR_RO(in2_input, pem_input, PEM_INPUT_VOLTAGE);
static SENSOR_DEVICE_ATTR_2_RO(in2_alarm, pem_bool, PEM_DATA_ALARM_1,
			       ALRM1_VIN_OUT_LIMIT | ALRM1_PRIMARY_FAULT);

/* Currents */
static SENSOR_DEVICE_ATTR_RO(curr1_input, pem_data, PEM_DATA_CURRENT);
static SENSOR_DEVICE_ATTR_2_RO(curr1_alarm, pem_bool, PEM_DATA_ALARM_1,
			       ALRM1_VIN_OVERCURRENT);

/* Power */
static SENSOR_DEVICE_ATTR_RO(power1_input, pem_input, PEM_INPUT_POWER_LSB);
static SENSOR_DEVICE_ATTR_2_RO(power1_alarm, pem_bool, PEM_DATA_ALARM_1,
			       ALRM1_POWER_LIMIT);

/* Fans */
static SENSOR_DEVICE_ATTR_RO(fan1_input, pem_fan, PEM_FAN_FAN1);
static SENSOR_DEVICE_ATTR_RO(fan2_input, pem_fan, PEM_FAN_FAN2);
static SENSOR_DEVICE_ATTR_RO(fan3_input, pem_fan, PEM_FAN_FAN3);
static SENSOR_DEVICE_ATTR_2_RO(fan1_alarm, pem_bool, PEM_DATA_ALARM_2,
			       ALRM2_FAN_FAULT);

/* Temperatures */
static SENSOR_DEVICE_ATTR_RO(temp1_input, pem_data, PEM_DATA_TEMP);
static SENSOR_DEVICE_ATTR_RO(temp1_max, pem_data, PEM_DATA_TEMP_MAX);
static SENSOR_DEVICE_ATTR_RO(temp1_crit, pem_data, PEM_DATA_TEMP_CRIT);
static SENSOR_DEVICE_ATTR_2_RO(temp1_alarm, pem_bool, PEM_DATA_ALARM_1,
			       ALRM1_TEMP_WARNING);
static SENSOR_DEVICE_ATTR_2_RO(temp1_crit_alarm, pem_bool, PEM_DATA_ALARM_1,
			       ALRM1_TEMP_SHUTDOWN);
static SENSOR_DEVICE_ATTR_2_RO(temp1_fault, pem_bool, PEM_DATA_ALARM_2,
			       ALRM2_TEMP_FAULT);

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
	NULL
};

static const struct attribute_group pem_input_group = {
	.attrs = pem_input_attributes,
};

static struct attribute *pem_fan_attributes[] = {
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	NULL
};

static const struct attribute_group pem_fan_group = {
	.attrs = pem_fan_attributes,
};

static int pem_probe(struct i2c_client *client,
		     const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct pem_data *data;
	int ret, idx = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BLOCK_DATA
				     | I2C_FUNC_SMBUS_WRITE_BYTE))
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
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

	dev_info(dev, "Firmware revision %d.%d.%d\n",
		 data->firmware_rev[0], data->firmware_rev[1],
		 data->firmware_rev[2]);

	/* sysfs hooks */
	data->groups[idx++] = &pem_group;

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

	if (data->input_length)
		data->groups[idx++] = &pem_input_group;

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
		data->groups[idx++] = &pem_fan_group;
	}

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data, data->groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
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
	.id_table = pem_id,
};

module_i2c_driver(pem_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("Lineage CPL PEM hardware monitoring driver");
MODULE_LICENSE("GPL");
