/*
 * LM73 Sensor driver
 * Based on LM75
 *
 * Copyright (C) 2007, CenoSYS (www.cenosys.com).
 * Copyright (C) 2009, Bollore telecom (www.bolloretelecom.eu).
 *
 * Guillaume Ligneul <guillaume.ligneul@gmail.com>
 * Adrien Demarez <adrien.demarez@bolloretelecom.eu>
 * Jeremy Laine <jeremy.laine@bolloretelecom.eu>
 *
 * This software program is licensed subject to the GNU General Public License
 * (GPL).Version 2,June 1991, available at
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>


/* Addresses scanned */
static const unsigned short normal_i2c[] = { 0x48, 0x49, 0x4a, 0x4c,
					0x4d, 0x4e, I2C_CLIENT_END };

/* LM73 registers */
#define LM73_REG_INPUT		0x00
#define LM73_REG_CONF		0x01
#define LM73_REG_MAX		0x02
#define LM73_REG_MIN		0x03
#define LM73_REG_CTRL		0x04
#define LM73_REG_ID		0x07

#define LM73_ID			0x9001 /* or 0x190 after a swab16() */
#define DRVNAME			"lm73"
#define LM73_TEMP_MIN		(-40)
#define LM73_TEMP_MAX		150

/*-----------------------------------------------------------------------*/


static ssize_t set_temp(struct device *dev, struct device_attribute *da,
			const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct i2c_client *client = to_i2c_client(dev);
	long temp;
	short value;

	int status = strict_strtol(buf, 10, &temp);
	if (status < 0)
		return status;

	/* Write value */
	value = (short) SENSORS_LIMIT(temp/250, (LM73_TEMP_MIN*4),
		(LM73_TEMP_MAX*4)) << 5;
	i2c_smbus_write_word_data(client, attr->index, swab16(value));
	return count;
}

static ssize_t show_temp(struct device *dev, struct device_attribute *da,
			 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct i2c_client *client = to_i2c_client(dev);
	/* use integer division instead of equivalent right shift to
	   guarantee arithmetic shift and preserve the sign */
	int temp = ((s16) (swab16(i2c_smbus_read_word_data(client,
		attr->index)))*250) / 32;
	return sprintf(buf, "%d\n", temp);
}


/*-----------------------------------------------------------------------*/

/* sysfs attributes for hwmon */

static SENSOR_DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO,
			show_temp, set_temp, LM73_REG_MAX);
static SENSOR_DEVICE_ATTR(temp1_min, S_IWUSR | S_IRUGO,
			show_temp, set_temp, LM73_REG_MIN);
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO,
			show_temp, NULL, LM73_REG_INPUT);


static struct attribute *lm73_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,

	NULL
};

static const struct attribute_group lm73_group = {
	.attrs = lm73_attributes,
};

/*-----------------------------------------------------------------------*/

/* device probe and removal */

static int
lm73_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *hwmon_dev;
	int status;

	/* Register sysfs hooks */
	status = sysfs_create_group(&client->dev.kobj, &lm73_group);
	if (status)
		return status;

	hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(hwmon_dev)) {
		status = PTR_ERR(hwmon_dev);
		goto exit_remove;
	}
	i2c_set_clientdata(client, hwmon_dev);

	dev_info(&client->dev, "%s: sensor '%s'\n",
		 dev_name(hwmon_dev), client->name);

	return 0;

exit_remove:
	sysfs_remove_group(&client->dev.kobj, &lm73_group);
	return status;
}

static int lm73_remove(struct i2c_client *client)
{
	struct device *hwmon_dev = i2c_get_clientdata(client);

	hwmon_device_unregister(hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &lm73_group);
	i2c_set_clientdata(client, NULL);
	return 0;
}

static const struct i2c_device_id lm73_ids[] = {
	{ "lm73", 0 },
	{ /* LIST END */ }
};
MODULE_DEVICE_TABLE(i2c, lm73_ids);

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm73_detect(struct i2c_client *new_client,
			struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = new_client->adapter;
	u16 id;
	u8 ctrl;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA |
					I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	/* Check device ID */
	id = i2c_smbus_read_word_data(new_client, LM73_REG_ID);
	ctrl = i2c_smbus_read_byte_data(new_client, LM73_REG_CTRL);
	if ((id != LM73_ID) || (ctrl & 0x10))
		return -ENODEV;

	strlcpy(info->type, "lm73", I2C_NAME_SIZE);

	return 0;
}

static struct i2c_driver lm73_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "lm73",
	},
	.probe		= lm73_probe,
	.remove		= lm73_remove,
	.id_table	= lm73_ids,
	.detect		= lm73_detect,
	.address_list	= normal_i2c,
};

/* module glue */

static int __init sensors_lm73_init(void)
{
	return i2c_add_driver(&lm73_driver);
}

static void __exit sensors_lm73_exit(void)
{
	i2c_del_driver(&lm73_driver);
}

MODULE_AUTHOR("Guillaume Ligneul <guillaume.ligneul@gmail.com>");
MODULE_DESCRIPTION("LM73 driver");
MODULE_LICENSE("GPL");

module_init(sensors_lm73_init);
module_exit(sensors_lm73_exit);
