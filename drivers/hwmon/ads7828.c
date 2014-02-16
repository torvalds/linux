/*
 * ads7828.c - driver for TI ADS7828 8-channel A/D converter and compatibles
 * (C) 2007 EADS Astrium
 *
 * This driver is based on the lm75 and other lm_sensors/hwmon drivers
 *
 * Written by Steve Hardy <shardy@redhat.com>
 *
 * ADS7830 support, by Guillaume Roguez <guillaume.roguez@savoirfairelinux.com>
 *
 * For further information, see the Documentation/hwmon/ads7828 file.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_data/ads7828.h>
#include <linux/slab.h>

/* The ADS7828 registers */
#define ADS7828_NCH		8	/* 8 channels supported */
#define ADS7828_CMD_SD_SE	0x80	/* Single ended inputs */
#define ADS7828_CMD_PD1		0x04	/* Internal vref OFF && A/D ON */
#define ADS7828_CMD_PD3		0x0C	/* Internal vref ON && A/D ON */
#define ADS7828_INT_VREF_MV	2500	/* Internal vref is 2.5V, 2500mV */
#define ADS7828_EXT_VREF_MV_MIN	50	/* External vref min value 0.05V */
#define ADS7828_EXT_VREF_MV_MAX	5250	/* External vref max value 5.25V */

/* List of supported devices */
enum ads7828_chips { ads7828, ads7830 };

/* Client specific data */
struct ads7828_data {
	struct device *hwmon_dev;
	struct mutex update_lock;	/* Mutex protecting updates */
	unsigned long last_updated;	/* Last updated time (in jiffies) */
	u16 adc_input[ADS7828_NCH];	/* ADS7828_NCH samples */
	bool valid;			/* Validity flag */
	bool diff_input;		/* Differential input */
	bool ext_vref;			/* External voltage reference */
	unsigned int vref_mv;		/* voltage reference value */
	u8 cmd_byte;			/* Command byte without channel bits */
	unsigned int lsb_resol;		/* Resolution of the ADC sample LSB */
	s32 (*read_channel)(const struct i2c_client *client, u8 command);
};

/* Command byte C2,C1,C0 - see datasheet */
static inline u8 ads7828_cmd_byte(u8 cmd, int ch)
{
	return cmd | (((ch >> 1) | (ch & 0x01) << 2) << 4);
}

/* Update data for the device (all 8 channels) */
static struct ads7828_data *ads7828_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ads7828_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
			|| !data->valid) {
		unsigned int ch;
		dev_dbg(&client->dev, "Starting ads7828 update\n");

		for (ch = 0; ch < ADS7828_NCH; ch++) {
			u8 cmd = ads7828_cmd_byte(data->cmd_byte, ch);
			data->adc_input[ch] = data->read_channel(client, cmd);
		}
		data->last_updated = jiffies;
		data->valid = true;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/* sysfs callback function */
static ssize_t ads7828_show_in(struct device *dev, struct device_attribute *da,
			       char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ads7828_data *data = ads7828_update_device(dev);
	unsigned int value = DIV_ROUND_CLOSEST(data->adc_input[attr->index] *
					       data->lsb_resol, 1000);

	return sprintf(buf, "%d\n", value);
}

static SENSOR_DEVICE_ATTR(in0_input, S_IRUGO, ads7828_show_in, NULL, 0);
static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, ads7828_show_in, NULL, 1);
static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO, ads7828_show_in, NULL, 2);
static SENSOR_DEVICE_ATTR(in3_input, S_IRUGO, ads7828_show_in, NULL, 3);
static SENSOR_DEVICE_ATTR(in4_input, S_IRUGO, ads7828_show_in, NULL, 4);
static SENSOR_DEVICE_ATTR(in5_input, S_IRUGO, ads7828_show_in, NULL, 5);
static SENSOR_DEVICE_ATTR(in6_input, S_IRUGO, ads7828_show_in, NULL, 6);
static SENSOR_DEVICE_ATTR(in7_input, S_IRUGO, ads7828_show_in, NULL, 7);

static struct attribute *ads7828_attributes[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	NULL
};

static const struct attribute_group ads7828_group = {
	.attrs = ads7828_attributes,
};

static int ads7828_remove(struct i2c_client *client)
{
	struct ads7828_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &ads7828_group);

	return 0;
}

static int ads7828_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct ads7828_platform_data *pdata = dev_get_platdata(&client->dev);
	struct ads7828_data *data;
	int err;

	data = devm_kzalloc(&client->dev, sizeof(struct ads7828_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (pdata) {
		data->diff_input = pdata->diff_input;
		data->ext_vref = pdata->ext_vref;
		if (data->ext_vref)
			data->vref_mv = pdata->vref_mv;
	}

	/* Bound Vref with min/max values if it was provided */
	if (data->vref_mv)
		data->vref_mv = clamp_val(data->vref_mv,
					  ADS7828_EXT_VREF_MV_MIN,
					  ADS7828_EXT_VREF_MV_MAX);
	else
		data->vref_mv = ADS7828_INT_VREF_MV;

	/* ADS7828 uses 12-bit samples, while ADS7830 is 8-bit */
	if (id->driver_data == ads7828) {
		data->lsb_resol = DIV_ROUND_CLOSEST(data->vref_mv * 1000, 4096);
		data->read_channel = i2c_smbus_read_word_swapped;
	} else {
		data->lsb_resol = DIV_ROUND_CLOSEST(data->vref_mv * 1000, 256);
		data->read_channel = i2c_smbus_read_byte_data;
	}

	data->cmd_byte = data->ext_vref ? ADS7828_CMD_PD1 : ADS7828_CMD_PD3;
	if (!data->diff_input)
		data->cmd_byte |= ADS7828_CMD_SD_SE;

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	err = sysfs_create_group(&client->dev.kobj, &ads7828_group);
	if (err)
		return err;

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto error;
	}

	return 0;

error:
	sysfs_remove_group(&client->dev.kobj, &ads7828_group);
	return err;
}

static const struct i2c_device_id ads7828_device_ids[] = {
	{ "ads7828", ads7828 },
	{ "ads7830", ads7830 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ads7828_device_ids);

static struct i2c_driver ads7828_driver = {
	.driver = {
		.name = "ads7828",
	},

	.id_table = ads7828_device_ids,
	.probe = ads7828_probe,
	.remove = ads7828_remove,
};

module_i2c_driver(ads7828_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Steve Hardy <shardy@redhat.com>");
MODULE_DESCRIPTION("Driver for TI ADS7828 A/D converter and compatibles");
