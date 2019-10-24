// SPDX-License-Identifier: GPL-2.0
/*
 * adm1029.c - Part of lm_sensors, Linux kernel modules for hardware monitoring
 *
 * Copyright (C) 2006 Corentin LABBE <clabbe.montjoie@gmail.com>
 *
 * Based on LM83 Driver by Jean Delvare <jdelvare@suse.de>
 *
 * Give only processor, motherboard temperatures and fan tachs
 * Very rare chip please let me know if you use it
 *
 * http://www.analog.com/UploadedFiles/Data_Sheets/ADM1029.pdf
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon.h>
#include <linux/err.h>
#include <linux/mutex.h>

/*
 * Addresses to scan
 */

static const unsigned short normal_i2c[] = { 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d,
						0x2e, 0x2f, I2C_CLIENT_END
};

/*
 * The ADM1029 registers
 * Manufacturer ID is 0x41 for Analog Devices
 */

#define ADM1029_REG_MAN_ID			0x0D
#define ADM1029_REG_CHIP_ID			0x0E
#define ADM1029_REG_CONFIG			0x01
#define ADM1029_REG_NB_FAN_SUPPORT		0x02

#define ADM1029_REG_TEMP_DEVICES_INSTALLED	0x06

#define ADM1029_REG_LOCAL_TEMP			0xA0
#define ADM1029_REG_REMOTE1_TEMP		0xA1
#define ADM1029_REG_REMOTE2_TEMP		0xA2

#define ADM1029_REG_LOCAL_TEMP_HIGH		0x90
#define ADM1029_REG_REMOTE1_TEMP_HIGH		0x91
#define ADM1029_REG_REMOTE2_TEMP_HIGH		0x92

#define ADM1029_REG_LOCAL_TEMP_LOW		0x98
#define ADM1029_REG_REMOTE1_TEMP_LOW		0x99
#define ADM1029_REG_REMOTE2_TEMP_LOW		0x9A

#define ADM1029_REG_FAN1			0x70
#define ADM1029_REG_FAN2			0x71

#define ADM1029_REG_FAN1_MIN			0x78
#define ADM1029_REG_FAN2_MIN			0x79

#define ADM1029_REG_FAN1_CONFIG			0x68
#define ADM1029_REG_FAN2_CONFIG			0x69

#define TEMP_FROM_REG(val)	((val) * 1000)

#define DIV_FROM_REG(val)	(1 << (((val) >> 6) - 1))

/* Registers to be checked by adm1029_update_device() */
static const u8 ADM1029_REG_TEMP[] = {
	ADM1029_REG_LOCAL_TEMP,
	ADM1029_REG_REMOTE1_TEMP,
	ADM1029_REG_REMOTE2_TEMP,
	ADM1029_REG_LOCAL_TEMP_HIGH,
	ADM1029_REG_REMOTE1_TEMP_HIGH,
	ADM1029_REG_REMOTE2_TEMP_HIGH,
	ADM1029_REG_LOCAL_TEMP_LOW,
	ADM1029_REG_REMOTE1_TEMP_LOW,
	ADM1029_REG_REMOTE2_TEMP_LOW,
};

static const u8 ADM1029_REG_FAN[] = {
	ADM1029_REG_FAN1,
	ADM1029_REG_FAN2,
	ADM1029_REG_FAN1_MIN,
	ADM1029_REG_FAN2_MIN,
};

static const u8 ADM1029_REG_FAN_DIV[] = {
	ADM1029_REG_FAN1_CONFIG,
	ADM1029_REG_FAN2_CONFIG,
};

/*
 * Client data (each client gets its own)
 */

struct adm1029_data {
	struct i2c_client *client;
	struct mutex update_lock; /* protect register access */
	char valid;		/* zero until following fields are valid */
	unsigned long last_updated;	/* in jiffies */

	/* registers values, signed for temperature, unsigned for other stuff */
	s8 temp[ARRAY_SIZE(ADM1029_REG_TEMP)];
	u8 fan[ARRAY_SIZE(ADM1029_REG_FAN)];
	u8 fan_div[ARRAY_SIZE(ADM1029_REG_FAN_DIV)];
};

/*
 * function that update the status of the chips (temperature for example)
 */
static struct adm1029_data *adm1029_update_device(struct device *dev)
{
	struct adm1029_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	mutex_lock(&data->update_lock);
	/*
	 * Use the "cache" Luke, don't recheck values
	 * if there are already checked not a long time later
	 */
	if (time_after(jiffies, data->last_updated + HZ * 2) || !data->valid) {
		int nr;

		dev_dbg(&client->dev, "Updating adm1029 data\n");

		for (nr = 0; nr < ARRAY_SIZE(ADM1029_REG_TEMP); nr++) {
			data->temp[nr] =
			    i2c_smbus_read_byte_data(client,
						     ADM1029_REG_TEMP[nr]);
		}
		for (nr = 0; nr < ARRAY_SIZE(ADM1029_REG_FAN); nr++) {
			data->fan[nr] =
			    i2c_smbus_read_byte_data(client,
						     ADM1029_REG_FAN[nr]);
		}
		for (nr = 0; nr < ARRAY_SIZE(ADM1029_REG_FAN_DIV); nr++) {
			data->fan_div[nr] =
			    i2c_smbus_read_byte_data(client,
						     ADM1029_REG_FAN_DIV[nr]);
		}

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/*
 * Sysfs stuff
 */

static ssize_t
temp_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adm1029_data *data = adm1029_update_device(dev);

	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[attr->index]));
}

static ssize_t
fan_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adm1029_data *data = adm1029_update_device(dev);
	u16 val;

	if (data->fan[attr->index] == 0 ||
	    (data->fan_div[attr->index] & 0xC0) == 0 ||
	    data->fan[attr->index] == 255) {
		return sprintf(buf, "0\n");
	}

	val = 1880 * 120 / DIV_FROM_REG(data->fan_div[attr->index])
	    / data->fan[attr->index];
	return sprintf(buf, "%d\n", val);
}

static ssize_t
fan_div_show(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adm1029_data *data = adm1029_update_device(dev);

	if ((data->fan_div[attr->index] & 0xC0) == 0)
		return sprintf(buf, "0\n");
	return sprintf(buf, "%d\n", DIV_FROM_REG(data->fan_div[attr->index]));
}

static ssize_t fan_div_store(struct device *dev,
			     struct device_attribute *devattr,
			     const char *buf, size_t count)
{
	struct adm1029_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	u8 reg;
	long val;
	int ret = kstrtol(buf, 10, &val);

	if (ret < 0)
		return ret;

	mutex_lock(&data->update_lock);

	/*Read actual config */
	reg = i2c_smbus_read_byte_data(client,
				       ADM1029_REG_FAN_DIV[attr->index]);

	switch (val) {
	case 1:
		val = 1;
		break;
	case 2:
		val = 2;
		break;
	case 4:
		val = 3;
		break;
	default:
		mutex_unlock(&data->update_lock);
		dev_err(&client->dev,
			"fan_div value %ld not supported. Choose one of 1, 2 or 4!\n",
			val);
		return -EINVAL;
	}
	/* Update the value */
	reg = (reg & 0x3F) | (val << 6);

	/* Update the cache */
	data->fan_div[attr->index] = reg;

	/* Write value */
	i2c_smbus_write_byte_data(client,
				  ADM1029_REG_FAN_DIV[attr->index], reg);
	mutex_unlock(&data->update_lock);

	return count;
}

/* Access rights on sysfs. */
static SENSOR_DEVICE_ATTR_RO(temp1_input, temp, 0);
static SENSOR_DEVICE_ATTR_RO(temp2_input, temp, 1);
static SENSOR_DEVICE_ATTR_RO(temp3_input, temp, 2);

static SENSOR_DEVICE_ATTR_RO(temp1_max, temp, 3);
static SENSOR_DEVICE_ATTR_RO(temp2_max, temp, 4);
static SENSOR_DEVICE_ATTR_RO(temp3_max, temp, 5);

static SENSOR_DEVICE_ATTR_RO(temp1_min, temp, 6);
static SENSOR_DEVICE_ATTR_RO(temp2_min, temp, 7);
static SENSOR_DEVICE_ATTR_RO(temp3_min, temp, 8);

static SENSOR_DEVICE_ATTR_RO(fan1_input, fan, 0);
static SENSOR_DEVICE_ATTR_RO(fan2_input, fan, 1);

static SENSOR_DEVICE_ATTR_RO(fan1_min, fan, 2);
static SENSOR_DEVICE_ATTR_RO(fan2_min, fan, 3);

static SENSOR_DEVICE_ATTR_RW(fan1_div, fan_div, 0);
static SENSOR_DEVICE_ATTR_RW(fan2_div, fan_div, 1);

static struct attribute *adm1029_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan1_div.dev_attr.attr,
	&sensor_dev_attr_fan2_div.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(adm1029);

/*
 * Real code
 */

/* Return 0 if detection is successful, -ENODEV otherwise */
static int adm1029_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	u8 man_id, chip_id, temp_devices_installed, nb_fan_support;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/*
	 * ADM1029 doesn't have CHIP ID, check just MAN ID
	 * For better detection we check also ADM1029_TEMP_DEVICES_INSTALLED,
	 * ADM1029_REG_NB_FAN_SUPPORT and compare it with possible values
	 * documented
	 */

	man_id = i2c_smbus_read_byte_data(client, ADM1029_REG_MAN_ID);
	chip_id = i2c_smbus_read_byte_data(client, ADM1029_REG_CHIP_ID);
	temp_devices_installed = i2c_smbus_read_byte_data(client,
					ADM1029_REG_TEMP_DEVICES_INSTALLED);
	nb_fan_support = i2c_smbus_read_byte_data(client,
						  ADM1029_REG_NB_FAN_SUPPORT);
	/* 0x41 is Analog Devices */
	if (man_id != 0x41 || (temp_devices_installed & 0xf9) != 0x01 ||
	    nb_fan_support != 0x03)
		return -ENODEV;

	if ((chip_id & 0xF0) != 0x00) {
		/*
		 * There are no "official" CHIP ID, so actually
		 * we use Major/Minor revision for that
		 */
		pr_info("Unknown major revision %x, please let us know\n",
			chip_id);
		return -ENODEV;
	}

	strlcpy(info->type, "adm1029", I2C_NAME_SIZE);

	return 0;
}

static int adm1029_init_client(struct i2c_client *client)
{
	u8 config;

	config = i2c_smbus_read_byte_data(client, ADM1029_REG_CONFIG);
	if ((config & 0x10) == 0) {
		i2c_smbus_write_byte_data(client, ADM1029_REG_CONFIG,
					  config | 0x10);
	}
	/* recheck config */
	config = i2c_smbus_read_byte_data(client, ADM1029_REG_CONFIG);
	if ((config & 0x10) == 0) {
		dev_err(&client->dev, "Initialization failed!\n");
		return 0;
	}
	return 1;
}

static int adm1029_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct adm1029_data *data;
	struct device *hwmon_dev;

	data = devm_kzalloc(dev, sizeof(struct adm1029_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->update_lock);

	/*
	 * Initialize the ADM1029 chip
	 * Check config register
	 */
	if (adm1029_init_client(client) == 0)
		return -ENODEV;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data,
							   adm1029_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id adm1029_id[] = {
	{ "adm1029", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adm1029_id);

static struct i2c_driver adm1029_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name = "adm1029",
	},
	.probe		= adm1029_probe,
	.id_table	= adm1029_id,
	.detect		= adm1029_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(adm1029_driver);

MODULE_AUTHOR("Corentin LABBE <clabbe.montjoie@gmail.com>");
MODULE_DESCRIPTION("adm1029 driver");
MODULE_LICENSE("GPL v2");
