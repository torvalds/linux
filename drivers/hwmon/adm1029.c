/*
 * adm1029.c - Part of lm_sensors, Linux kernel modules for hardware monitoring
 *
 * Copyright (C) 2006 Corentin LABBE <corentin.labbe@geomatys.fr>
 *
 * Based on LM83 Driver by Jean Delvare <khali@linux-fr.org>
 *
 * Give only processor, motherboard temperatures and fan tachs
 * Very rare chip please let me know if you use it
 *
 * http://www.analog.com/UploadedFiles/Data_Sheets/ADM1029.pdf
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2 of the License
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

#define DIV_FROM_REG(val)	( 1 << (((val) >> 6) - 1))

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
 * Functions declaration
 */

static int adm1029_probe(struct i2c_client *client,
			 const struct i2c_device_id *id);
static int adm1029_detect(struct i2c_client *client,
			  struct i2c_board_info *info);
static int adm1029_remove(struct i2c_client *client);
static struct adm1029_data *adm1029_update_device(struct device *dev);
static int adm1029_init_client(struct i2c_client *client);

/*
 * Driver data (common to all clients)
 */

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
	.remove		= adm1029_remove,
	.id_table	= adm1029_id,
	.detect		= adm1029_detect,
	.address_list	= normal_i2c,
};

/*
 * Client data (each client gets its own)
 */

struct adm1029_data {
	struct device *hwmon_dev;
	struct mutex update_lock;
	char valid;		/* zero until following fields are valid */
	unsigned long last_updated;	/* in jiffies */

	/* registers values, signed for temperature, unsigned for other stuff */
	s8 temp[ARRAY_SIZE(ADM1029_REG_TEMP)];
	u8 fan[ARRAY_SIZE(ADM1029_REG_FAN)];
	u8 fan_div[ARRAY_SIZE(ADM1029_REG_FAN_DIV)];
};

/*
 * Sysfs stuff
 */

static ssize_t
show_temp(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adm1029_data *data = adm1029_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[attr->index]));
}

static ssize_t
show_fan(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adm1029_data *data = adm1029_update_device(dev);
	u16 val;
	if (data->fan[attr->index] == 0
	    || (data->fan_div[attr->index] & 0xC0) == 0
	    || data->fan[attr->index] == 255) {
		return sprintf(buf, "0\n");
	}

	val = 1880 * 120 / DIV_FROM_REG(data->fan_div[attr->index])
	    / data->fan[attr->index];
	return sprintf(buf, "%d\n", val);
}

static ssize_t
show_fan_div(struct device *dev, struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adm1029_data *data = adm1029_update_device(dev);
	if ((data->fan_div[attr->index] & 0xC0) == 0)
		return sprintf(buf, "0\n");
	return sprintf(buf, "%d\n", DIV_FROM_REG(data->fan_div[attr->index]));
}

static ssize_t set_fan_div(struct device *dev,
	    struct device_attribute *devattr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm1029_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	long val = simple_strtol(buf, NULL, 10);
	u8 reg;

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
		dev_err(&client->dev, "fan_div value %ld not "
			"supported. Choose one of 1, 2 or 4!\n", val);
		return -EINVAL;
	}
	/* Update the value */
	reg = (reg & 0x3F) | (val << 6);

	/* Write value */
	i2c_smbus_write_byte_data(client,
				  ADM1029_REG_FAN_DIV[attr->index], reg);
	mutex_unlock(&data->update_lock);

	return count;
}

/*
Access rights on sysfs, S_IRUGO stand for Is Readable by User, Group and Others
			S_IWUSR stand for Is Writable by User
*/
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_temp, NULL, 1);
static SENSOR_DEVICE_ATTR(temp3_input, S_IRUGO, show_temp, NULL, 2);

static SENSOR_DEVICE_ATTR(temp1_max, S_IRUGO, show_temp, NULL, 3);
static SENSOR_DEVICE_ATTR(temp2_max, S_IRUGO, show_temp, NULL, 4);
static SENSOR_DEVICE_ATTR(temp3_max, S_IRUGO, show_temp, NULL, 5);

static SENSOR_DEVICE_ATTR(temp1_min, S_IRUGO, show_temp, NULL, 6);
static SENSOR_DEVICE_ATTR(temp2_min, S_IRUGO, show_temp, NULL, 7);
static SENSOR_DEVICE_ATTR(temp3_min, S_IRUGO, show_temp, NULL, 8);

static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, show_fan, NULL, 0);
static SENSOR_DEVICE_ATTR(fan2_input, S_IRUGO, show_fan, NULL, 1);

static SENSOR_DEVICE_ATTR(fan1_min, S_IRUGO, show_fan, NULL, 2);
static SENSOR_DEVICE_ATTR(fan2_min, S_IRUGO, show_fan, NULL, 3);

static SENSOR_DEVICE_ATTR(fan1_div, S_IRUGO | S_IWUSR,
			  show_fan_div, set_fan_div, 0);
static SENSOR_DEVICE_ATTR(fan2_div, S_IRUGO | S_IWUSR,
			  show_fan_div, set_fan_div, 1);

static struct attribute *adm1029_attributes[] = {
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

static const struct attribute_group adm1029_group = {
	.attrs = adm1029_attributes,
};

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

	/* ADM1029 doesn't have CHIP ID, check just MAN ID
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
	if (man_id != 0x41 || (temp_devices_installed & 0xf9) != 0x01
	    || nb_fan_support != 0x03)
		return -ENODEV;

	if ((chip_id & 0xF0) != 0x00) {
		/* There are no "official" CHIP ID, so actually
		 * we use Major/Minor revision for that */
		pr_info("adm1029: Unknown major revision %x, "
			"please let us know\n", chip_id);
		return -ENODEV;
	}

	strlcpy(info->type, "adm1029", I2C_NAME_SIZE);

	return 0;
}

static int adm1029_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct adm1029_data *data;
	int err;

	data = kzalloc(sizeof(struct adm1029_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	/*
	 * Initialize the ADM1029 chip
	 * Check config register
	 */
	if (adm1029_init_client(client) == 0) {
		err = -ENODEV;
		goto exit_free;
	}

	/* Register sysfs hooks */
	if ((err = sysfs_create_group(&client->dev.kobj, &adm1029_group)))
		goto exit_free;

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove_files;
	}

	return 0;

 exit_remove_files:
	sysfs_remove_group(&client->dev.kobj, &adm1029_group);
 exit_free:
	kfree(data);
 exit:
	return err;
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

static int adm1029_remove(struct i2c_client *client)
{
	struct adm1029_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &adm1029_group);

	kfree(data);
	return 0;
}

/*
function that update the status of the chips (temperature for example)
*/
static struct adm1029_data *adm1029_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm1029_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);
	/*
	 * Use the "cache" Luke, don't recheck values
	 * if there are already checked not a long time later
	 */
	if (time_after(jiffies, data->last_updated + HZ * 2)
	 || !data->valid) {
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
	Common module stuff
*/
static int __init sensors_adm1029_init(void)
{

	return i2c_add_driver(&adm1029_driver);
}

static void __exit sensors_adm1029_exit(void)
{

	i2c_del_driver(&adm1029_driver);
}

MODULE_AUTHOR("Corentin LABBE <corentin.labbe@geomatys.fr>");
MODULE_DESCRIPTION("adm1029 driver");
MODULE_LICENSE("GPL v2");

module_init(sensors_adm1029_init);
module_exit(sensors_adm1029_exit);
