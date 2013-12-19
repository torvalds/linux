/*
 * emc6w201.c - Hardware monitoring driver for the SMSC EMC6W201
 * Copyright (C) 2011  Jean Delvare <khali@linux-fr.org>
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>

/*
 * Addresses to scan
 */

static const unsigned short normal_i2c[] = { 0x2c, 0x2d, 0x2e, I2C_CLIENT_END };

/*
 * The EMC6W201 registers
 */

#define EMC6W201_REG_IN(nr)		(0x20 + (nr))
#define EMC6W201_REG_TEMP(nr)		(0x26 + (nr))
#define EMC6W201_REG_FAN(nr)		(0x2C + (nr) * 2)
#define EMC6W201_REG_COMPANY		0x3E
#define EMC6W201_REG_VERSTEP		0x3F
#define EMC6W201_REG_CONFIG		0x40
#define EMC6W201_REG_IN_LOW(nr)		(0x4A + (nr) * 2)
#define EMC6W201_REG_IN_HIGH(nr)	(0x4B + (nr) * 2)
#define EMC6W201_REG_TEMP_LOW(nr)	(0x56 + (nr) * 2)
#define EMC6W201_REG_TEMP_HIGH(nr)	(0x57 + (nr) * 2)
#define EMC6W201_REG_FAN_MIN(nr)	(0x62 + (nr) * 2)

enum subfeature { input, min, max };

/*
 * Per-device data
 */

struct emc6w201_data {
	struct device *hwmon_dev;
	struct mutex update_lock;
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */

	/* registers values */
	u8 in[3][6];
	s8 temp[3][6];
	u16 fan[2][5];
};

/*
 * Combine LSB and MSB registers in a single value
 * Locking: must be called with data->update_lock held
 */
static u16 emc6w201_read16(struct i2c_client *client, u8 reg)
{
	int lsb, msb;

	lsb = i2c_smbus_read_byte_data(client, reg);
	msb = i2c_smbus_read_byte_data(client, reg + 1);
	if (unlikely(lsb < 0 || msb < 0)) {
		dev_err(&client->dev, "%d-bit %s failed at 0x%02x\n",
			16, "read", reg);
		return 0xFFFF;	/* Arbitrary value */
	}

	return (msb << 8) | lsb;
}

/*
 * Write 16-bit value to LSB and MSB registers
 * Locking: must be called with data->update_lock held
 */
static int emc6w201_write16(struct i2c_client *client, u8 reg, u16 val)
{
	int err;

	err = i2c_smbus_write_byte_data(client, reg, val & 0xff);
	if (likely(!err))
		err = i2c_smbus_write_byte_data(client, reg + 1, val >> 8);
	if (unlikely(err < 0))
		dev_err(&client->dev, "%d-bit %s failed at 0x%02x\n",
			16, "write", reg);

	return err;
}

/* Read 8-bit value from register */
static u8 emc6w201_read8(struct i2c_client *client, u8 reg)
{
	int val;

	val = i2c_smbus_read_byte_data(client, reg);
	if (unlikely(val < 0)) {
		dev_err(&client->dev, "%d-bit %s failed at 0x%02x\n",
			8, "read", reg);
		return 0x00;	/* Arbitrary value */
	}

	return val;
}

/* Write 8-bit value to register */
static int emc6w201_write8(struct i2c_client *client, u8 reg, u8 val)
{
	int err;

	err = i2c_smbus_write_byte_data(client, reg, val);
	if (unlikely(err < 0))
		dev_err(&client->dev, "%d-bit %s failed at 0x%02x\n",
			8, "write", reg);

	return err;
}

static struct emc6w201_data *emc6w201_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct emc6w201_data *data = i2c_get_clientdata(client);
	int nr;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {
		for (nr = 0; nr < 6; nr++) {
			data->in[input][nr] =
				emc6w201_read8(client,
						EMC6W201_REG_IN(nr));
			data->in[min][nr] =
				emc6w201_read8(client,
						EMC6W201_REG_IN_LOW(nr));
			data->in[max][nr] =
				emc6w201_read8(client,
						EMC6W201_REG_IN_HIGH(nr));
		}

		for (nr = 0; nr < 6; nr++) {
			data->temp[input][nr] =
				emc6w201_read8(client,
						EMC6W201_REG_TEMP(nr));
			data->temp[min][nr] =
				emc6w201_read8(client,
						EMC6W201_REG_TEMP_LOW(nr));
			data->temp[max][nr] =
				emc6w201_read8(client,
						EMC6W201_REG_TEMP_HIGH(nr));
		}

		for (nr = 0; nr < 5; nr++) {
			data->fan[input][nr] =
				emc6w201_read16(client,
						EMC6W201_REG_FAN(nr));
			data->fan[min][nr] =
				emc6w201_read16(client,
						EMC6W201_REG_FAN_MIN(nr));
		}

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/*
 * Sysfs callback functions
 */

static const s16 nominal_mv[6] = { 2500, 1500, 3300, 5000, 1500, 1500 };

static ssize_t show_in(struct device *dev, struct device_attribute *devattr,
	char *buf)
{
	struct emc6w201_data *data = emc6w201_update_device(dev);
	int sf = to_sensor_dev_attr_2(devattr)->index;
	int nr = to_sensor_dev_attr_2(devattr)->nr;

	return sprintf(buf, "%u\n",
		       (unsigned)data->in[sf][nr] * nominal_mv[nr] / 0xC0);
}

static ssize_t set_in(struct device *dev, struct device_attribute *devattr,
		      const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct emc6w201_data *data = i2c_get_clientdata(client);
	int sf = to_sensor_dev_attr_2(devattr)->index;
	int nr = to_sensor_dev_attr_2(devattr)->nr;
	int err;
	long val;
	u8 reg;

	err = kstrtol(buf, 10, &val);
	if (err < 0)
		return err;

	val = DIV_ROUND_CLOSEST(val * 0xC0, nominal_mv[nr]);
	reg = (sf == min) ? EMC6W201_REG_IN_LOW(nr)
			  : EMC6W201_REG_IN_HIGH(nr);

	mutex_lock(&data->update_lock);
	data->in[sf][nr] = clamp_val(val, 0, 255);
	err = emc6w201_write8(client, reg, data->in[sf][nr]);
	mutex_unlock(&data->update_lock);

	return err < 0 ? err : count;
}

static ssize_t show_temp(struct device *dev, struct device_attribute *devattr,
	char *buf)
{
	struct emc6w201_data *data = emc6w201_update_device(dev);
	int sf = to_sensor_dev_attr_2(devattr)->index;
	int nr = to_sensor_dev_attr_2(devattr)->nr;

	return sprintf(buf, "%d\n", (int)data->temp[sf][nr] * 1000);
}

static ssize_t set_temp(struct device *dev, struct device_attribute *devattr,
			const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct emc6w201_data *data = i2c_get_clientdata(client);
	int sf = to_sensor_dev_attr_2(devattr)->index;
	int nr = to_sensor_dev_attr_2(devattr)->nr;
	int err;
	long val;
	u8 reg;

	err = kstrtol(buf, 10, &val);
	if (err < 0)
		return err;

	val /= 1000;
	reg = (sf == min) ? EMC6W201_REG_TEMP_LOW(nr)
			  : EMC6W201_REG_TEMP_HIGH(nr);

	mutex_lock(&data->update_lock);
	data->temp[sf][nr] = clamp_val(val, -127, 128);
	err = emc6w201_write8(client, reg, data->temp[sf][nr]);
	mutex_unlock(&data->update_lock);

	return err < 0 ? err : count;
}

static ssize_t show_fan(struct device *dev, struct device_attribute *devattr,
	char *buf)
{
	struct emc6w201_data *data = emc6w201_update_device(dev);
	int sf = to_sensor_dev_attr_2(devattr)->index;
	int nr = to_sensor_dev_attr_2(devattr)->nr;
	unsigned rpm;

	if (data->fan[sf][nr] == 0 || data->fan[sf][nr] == 0xFFFF)
		rpm = 0;
	else
		rpm = 5400000U / data->fan[sf][nr];

	return sprintf(buf, "%u\n", rpm);
}

static ssize_t set_fan(struct device *dev, struct device_attribute *devattr,
		       const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct emc6w201_data *data = i2c_get_clientdata(client);
	int sf = to_sensor_dev_attr_2(devattr)->index;
	int nr = to_sensor_dev_attr_2(devattr)->nr;
	int err;
	unsigned long val;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	if (val == 0) {
		val = 0xFFFF;
	} else {
		val = DIV_ROUND_CLOSEST(5400000U, val);
		val = clamp_val(val, 0, 0xFFFE);
	}

	mutex_lock(&data->update_lock);
	data->fan[sf][nr] = val;
	err = emc6w201_write16(client, EMC6W201_REG_FAN_MIN(nr),
			       data->fan[sf][nr]);
	mutex_unlock(&data->update_lock);

	return err < 0 ? err : count;
}

static SENSOR_DEVICE_ATTR_2(in0_input, S_IRUGO, show_in, NULL, 0, input);
static SENSOR_DEVICE_ATTR_2(in0_min, S_IRUGO | S_IWUSR, show_in, set_in,
			    0, min);
static SENSOR_DEVICE_ATTR_2(in0_max, S_IRUGO | S_IWUSR, show_in, set_in,
			    0, max);
static SENSOR_DEVICE_ATTR_2(in1_input, S_IRUGO, show_in, NULL, 1, input);
static SENSOR_DEVICE_ATTR_2(in1_min, S_IRUGO | S_IWUSR, show_in, set_in,
			    1, min);
static SENSOR_DEVICE_ATTR_2(in1_max, S_IRUGO | S_IWUSR, show_in, set_in,
			    1, max);
static SENSOR_DEVICE_ATTR_2(in2_input, S_IRUGO, show_in, NULL, 2, input);
static SENSOR_DEVICE_ATTR_2(in2_min, S_IRUGO | S_IWUSR, show_in, set_in,
			    2, min);
static SENSOR_DEVICE_ATTR_2(in2_max, S_IRUGO | S_IWUSR, show_in, set_in,
			    2, max);
static SENSOR_DEVICE_ATTR_2(in3_input, S_IRUGO, show_in, NULL, 3, input);
static SENSOR_DEVICE_ATTR_2(in3_min, S_IRUGO | S_IWUSR, show_in, set_in,
			    3, min);
static SENSOR_DEVICE_ATTR_2(in3_max, S_IRUGO | S_IWUSR, show_in, set_in,
			    3, max);
static SENSOR_DEVICE_ATTR_2(in4_input, S_IRUGO, show_in, NULL, 4, input);
static SENSOR_DEVICE_ATTR_2(in4_min, S_IRUGO | S_IWUSR, show_in, set_in,
			    4, min);
static SENSOR_DEVICE_ATTR_2(in4_max, S_IRUGO | S_IWUSR, show_in, set_in,
			    4, max);
static SENSOR_DEVICE_ATTR_2(in5_input, S_IRUGO, show_in, NULL, 5, input);
static SENSOR_DEVICE_ATTR_2(in5_min, S_IRUGO | S_IWUSR, show_in, set_in,
			    5, min);
static SENSOR_DEVICE_ATTR_2(in5_max, S_IRUGO | S_IWUSR, show_in, set_in,
			    5, max);

static SENSOR_DEVICE_ATTR_2(temp1_input, S_IRUGO, show_temp, NULL, 0, input);
static SENSOR_DEVICE_ATTR_2(temp1_min, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    0, min);
static SENSOR_DEVICE_ATTR_2(temp1_max, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    0, max);
static SENSOR_DEVICE_ATTR_2(temp2_input, S_IRUGO, show_temp, NULL, 1, input);
static SENSOR_DEVICE_ATTR_2(temp2_min, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    1, min);
static SENSOR_DEVICE_ATTR_2(temp2_max, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    1, max);
static SENSOR_DEVICE_ATTR_2(temp3_input, S_IRUGO, show_temp, NULL, 2, input);
static SENSOR_DEVICE_ATTR_2(temp3_min, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    2, min);
static SENSOR_DEVICE_ATTR_2(temp3_max, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    2, max);
static SENSOR_DEVICE_ATTR_2(temp4_input, S_IRUGO, show_temp, NULL, 3, input);
static SENSOR_DEVICE_ATTR_2(temp4_min, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    3, min);
static SENSOR_DEVICE_ATTR_2(temp4_max, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    3, max);
static SENSOR_DEVICE_ATTR_2(temp5_input, S_IRUGO, show_temp, NULL, 4, input);
static SENSOR_DEVICE_ATTR_2(temp5_min, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    4, min);
static SENSOR_DEVICE_ATTR_2(temp5_max, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    4, max);
static SENSOR_DEVICE_ATTR_2(temp6_input, S_IRUGO, show_temp, NULL, 5, input);
static SENSOR_DEVICE_ATTR_2(temp6_min, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    5, min);
static SENSOR_DEVICE_ATTR_2(temp6_max, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    5, max);

static SENSOR_DEVICE_ATTR_2(fan1_input, S_IRUGO, show_fan, NULL, 0, input);
static SENSOR_DEVICE_ATTR_2(fan1_min, S_IRUGO | S_IWUSR, show_fan, set_fan,
			    0, min);
static SENSOR_DEVICE_ATTR_2(fan2_input, S_IRUGO, show_fan, NULL, 1, input);
static SENSOR_DEVICE_ATTR_2(fan2_min, S_IRUGO | S_IWUSR, show_fan, set_fan,
			    1, min);
static SENSOR_DEVICE_ATTR_2(fan3_input, S_IRUGO, show_fan, NULL, 2, input);
static SENSOR_DEVICE_ATTR_2(fan3_min, S_IRUGO | S_IWUSR, show_fan, set_fan,
			    2, min);
static SENSOR_DEVICE_ATTR_2(fan4_input, S_IRUGO, show_fan, NULL, 3, input);
static SENSOR_DEVICE_ATTR_2(fan4_min, S_IRUGO | S_IWUSR, show_fan, set_fan,
			    3, min);
static SENSOR_DEVICE_ATTR_2(fan5_input, S_IRUGO, show_fan, NULL, 4, input);
static SENSOR_DEVICE_ATTR_2(fan5_min, S_IRUGO | S_IWUSR, show_fan, set_fan,
			    4, min);

static struct attribute *emc6w201_attributes[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in0_min.dev_attr.attr,
	&sensor_dev_attr_in0_max.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in3_min.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in4_min.dev_attr.attr,
	&sensor_dev_attr_in4_max.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in5_min.dev_attr.attr,
	&sensor_dev_attr_in5_max.dev_attr.attr,

	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp4_input.dev_attr.attr,
	&sensor_dev_attr_temp4_min.dev_attr.attr,
	&sensor_dev_attr_temp4_max.dev_attr.attr,
	&sensor_dev_attr_temp5_input.dev_attr.attr,
	&sensor_dev_attr_temp5_min.dev_attr.attr,
	&sensor_dev_attr_temp5_max.dev_attr.attr,
	&sensor_dev_attr_temp6_input.dev_attr.attr,
	&sensor_dev_attr_temp6_min.dev_attr.attr,
	&sensor_dev_attr_temp6_max.dev_attr.attr,

	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan3_min.dev_attr.attr,
	&sensor_dev_attr_fan4_input.dev_attr.attr,
	&sensor_dev_attr_fan4_min.dev_attr.attr,
	&sensor_dev_attr_fan5_input.dev_attr.attr,
	&sensor_dev_attr_fan5_min.dev_attr.attr,
	NULL
};

static const struct attribute_group emc6w201_group = {
	.attrs = emc6w201_attributes,
};

/*
 * Driver interface
 */

/* Return 0 if detection is successful, -ENODEV otherwise */
static int emc6w201_detect(struct i2c_client *client,
			   struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int company, verstep, config;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* Identification */
	company = i2c_smbus_read_byte_data(client, EMC6W201_REG_COMPANY);
	if (company != 0x5C)
		return -ENODEV;
	verstep = i2c_smbus_read_byte_data(client, EMC6W201_REG_VERSTEP);
	if (verstep < 0 || (verstep & 0xF0) != 0xB0)
		return -ENODEV;
	if ((verstep & 0x0F) > 2) {
		dev_dbg(&client->dev, "Unknwown EMC6W201 stepping %d\n",
			verstep & 0x0F);
		return -ENODEV;
	}

	/* Check configuration */
	config = i2c_smbus_read_byte_data(client, EMC6W201_REG_CONFIG);
	if (config < 0 || (config & 0xF4) != 0x04)
		return -ENODEV;
	if (!(config & 0x01)) {
		dev_err(&client->dev, "Monitoring not enabled\n");
		return -ENODEV;
	}

	strlcpy(info->type, "emc6w201", I2C_NAME_SIZE);

	return 0;
}

static int emc6w201_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct emc6w201_data *data;
	int err;

	data = devm_kzalloc(&client->dev, sizeof(struct emc6w201_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	/* Create sysfs attribute */
	err = sysfs_create_group(&client->dev.kobj, &emc6w201_group);
	if (err)
		return err;

	/* Expose as a hwmon device */
	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove;
	}

	return 0;

 exit_remove:
	sysfs_remove_group(&client->dev.kobj, &emc6w201_group);
	return err;
}

static int emc6w201_remove(struct i2c_client *client)
{
	struct emc6w201_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &emc6w201_group);

	return 0;
}

static const struct i2c_device_id emc6w201_id[] = {
	{ "emc6w201", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, emc6w201_id);

static struct i2c_driver emc6w201_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "emc6w201",
	},
	.probe		= emc6w201_probe,
	.remove		= emc6w201_remove,
	.id_table	= emc6w201_id,
	.detect		= emc6w201_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(emc6w201_driver);

MODULE_AUTHOR("Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("SMSC EMC6W201 hardware monitoring driver");
MODULE_LICENSE("GPL");
