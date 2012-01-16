/*
	ads7828.c - lm_sensors driver for ads7828 12-bit 8-channel ADC
	(C) 2007 EADS Astrium

	This driver is based on the lm75 and other lm_sensors/hwmon drivers

	Written by Steve Hardy <shardy@redhat.com>

	Datasheet available at: http://focus.ti.com/lit/ds/symlink/ads7828.pdf

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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

/* The ADS7828 registers */
#define ADS7828_NCH 8 /* 8 channels of 12-bit A-D supported */
#define ADS7828_CMD_SD_SE 0x80 /* Single ended inputs */
#define ADS7828_CMD_SD_DIFF 0x00 /* Differential inputs */
#define ADS7828_CMD_PD0 0x0 /* Power Down between A-D conversions */
#define ADS7828_CMD_PD1 0x04 /* Internal ref OFF && A-D ON */
#define ADS7828_CMD_PD2 0x08 /* Internal ref ON && A-D OFF */
#define ADS7828_CMD_PD3 0x0C /* Internal ref ON && A-D ON */
#define ADS7828_INT_VREF_MV 2500 /* Internal vref is 2.5V, 2500mV */

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x48, 0x49, 0x4a, 0x4b,
	I2C_CLIENT_END };

/* Module parameters */
static bool se_input = 1; /* Default is SE, 0 == diff */
static bool int_vref = 1; /* Default is internal ref ON */
static int vref_mv = ADS7828_INT_VREF_MV; /* set if vref != 2.5V */
module_param(se_input, bool, S_IRUGO);
module_param(int_vref, bool, S_IRUGO);
module_param(vref_mv, int, S_IRUGO);

/* Global Variables */
static u8 ads7828_cmd_byte; /* cmd byte without channel bits */
static unsigned int ads7828_lsb_resol; /* resolution of the ADC sample lsb */

/* Each client has this additional data */
struct ads7828_data {
	struct device *hwmon_dev;
	struct mutex update_lock; /* mutex protect updates */
	char valid; /* !=0 if following fields are valid */
	unsigned long last_updated; /* In jiffies */
	u16 adc_input[ADS7828_NCH]; /* ADS7828_NCH 12-bit samples */
};

/* Function declaration - necessary due to function dependencies */
static int ads7828_detect(struct i2c_client *client,
			  struct i2c_board_info *info);
static int ads7828_probe(struct i2c_client *client,
			 const struct i2c_device_id *id);

static inline u8 channel_cmd_byte(int ch)
{
	/* cmd byte C2,C1,C0 - see datasheet */
	u8 cmd = (((ch>>1) | (ch&0x01)<<2)<<4);
	cmd |= ads7828_cmd_byte;
	return cmd;
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
			u8 cmd = channel_cmd_byte(ch);
			data->adc_input[ch] =
				i2c_smbus_read_word_swapped(client, cmd);
		}
		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/* sysfs callback function */
static ssize_t show_in(struct device *dev, struct device_attribute *da,
	char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ads7828_data *data = ads7828_update_device(dev);
	/* Print value (in mV as specified in sysfs-interface documentation) */
	return sprintf(buf, "%d\n", (data->adc_input[attr->index] *
		ads7828_lsb_resol)/1000);
}

#define in_reg(offset)\
static SENSOR_DEVICE_ATTR(in##offset##_input, S_IRUGO, show_in,\
	NULL, offset)

in_reg(0);
in_reg(1);
in_reg(2);
in_reg(3);
in_reg(4);
in_reg(5);
in_reg(6);
in_reg(7);

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
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id ads7828_id[] = {
	{ "ads7828", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ads7828_id);

/* This is the driver that will be inserted */
static struct i2c_driver ads7828_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = "ads7828",
	},
	.probe = ads7828_probe,
	.remove = ads7828_remove,
	.id_table = ads7828_id,
	.detect = ads7828_detect,
	.address_list = normal_i2c,
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int ads7828_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int ch;

	/* Check we have a valid client */
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -ENODEV;

	/* Now, we do the remaining detection. There is no identification
	dedicated register so attempt to sanity check using knowledge of
	the chip
	- Read from the 8 channel addresses
	- Check the top 4 bits of each result are not set (12 data bits)
	*/
	for (ch = 0; ch < ADS7828_NCH; ch++) {
		u16 in_data;
		u8 cmd = channel_cmd_byte(ch);
		in_data = i2c_smbus_read_word_swapped(client, cmd);
		if (in_data & 0xF000) {
			pr_debug("%s : Doesn't look like an ads7828 device\n",
				 __func__);
			return -ENODEV;
		}
	}

	strlcpy(info->type, "ads7828", I2C_NAME_SIZE);

	return 0;
}

static int ads7828_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct ads7828_data *data;
	int err;

	data = kzalloc(sizeof(struct ads7828_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	/* Register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &ads7828_group);
	if (err)
		goto exit_free;

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove;
	}

	return 0;

exit_remove:
	sysfs_remove_group(&client->dev.kobj, &ads7828_group);
exit_free:
	kfree(data);
exit:
	return err;
}

static int __init sensors_ads7828_init(void)
{
	/* Initialize the command byte according to module parameters */
	ads7828_cmd_byte = se_input ?
		ADS7828_CMD_SD_SE : ADS7828_CMD_SD_DIFF;
	ads7828_cmd_byte |= int_vref ?
		ADS7828_CMD_PD3 : ADS7828_CMD_PD1;

	/* Calculate the LSB resolution */
	ads7828_lsb_resol = (vref_mv*1000)/4096;

	return i2c_add_driver(&ads7828_driver);
}

static void __exit sensors_ads7828_exit(void)
{
	i2c_del_driver(&ads7828_driver);
}

MODULE_AUTHOR("Steve Hardy <shardy@redhat.com>");
MODULE_DESCRIPTION("ADS7828 driver");
MODULE_LICENSE("GPL");

module_init(sensors_ads7828_init);
module_exit(sensors_ads7828_exit);
