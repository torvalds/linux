/*
 * max1619.c - Part of lm_sensors, Linux kernel modules for hardware
 *             monitoring
 * Copyright (C) 2003-2004 Alexey Fisher <fishor@mail.ru>
 *                         Jean Delvare <khali@linux-fr.org>
 *
 * Based on the lm90 driver. The MAX1619 is a sensor chip made by Maxim.
 * It reports up to two temperatures (its own plus up to
 * one external one). Complete datasheet can be
 * obtained from Maxim's website at:
 *   http://pdfserv.maxim-ic.com/en/ds/MAX1619.pdf
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
#include <linux/err.h>

static unsigned short normal_i2c[] = { 0x18, 0x19, 0x1a,
					0x29, 0x2a, 0x2b,
					0x4c, 0x4d, 0x4e,
					I2C_CLIENT_END };

/*
 * Insmod parameters
 */

I2C_CLIENT_INSMOD_1(max1619);

/*
 * The MAX1619 registers
 */

#define MAX1619_REG_R_MAN_ID		0xFE
#define MAX1619_REG_R_CHIP_ID		0xFF
#define MAX1619_REG_R_CONFIG		0x03
#define MAX1619_REG_W_CONFIG		0x09
#define MAX1619_REG_R_CONVRATE		0x04
#define MAX1619_REG_W_CONVRATE		0x0A
#define MAX1619_REG_R_STATUS		0x02
#define MAX1619_REG_R_LOCAL_TEMP	0x00
#define MAX1619_REG_R_REMOTE_TEMP	0x01
#define MAX1619_REG_R_REMOTE_HIGH	0x07
#define MAX1619_REG_W_REMOTE_HIGH	0x0D
#define MAX1619_REG_R_REMOTE_LOW	0x08
#define MAX1619_REG_W_REMOTE_LOW	0x0E
#define MAX1619_REG_R_REMOTE_CRIT	0x10
#define MAX1619_REG_W_REMOTE_CRIT	0x12
#define MAX1619_REG_R_TCRIT_HYST	0x11
#define MAX1619_REG_W_TCRIT_HYST	0x13

/*
 * Conversions and various macros
 */

#define TEMP_FROM_REG(val)	((val & 0x80 ? val-0x100 : val) * 1000)
#define TEMP_TO_REG(val)	((val < 0 ? val+0x100*1000 : val) / 1000)

/*
 * Functions declaration
 */

static int max1619_attach_adapter(struct i2c_adapter *adapter);
static int max1619_detect(struct i2c_adapter *adapter, int address,
	int kind);
static void max1619_init_client(struct i2c_client *client);
static int max1619_detach_client(struct i2c_client *client);
static struct max1619_data *max1619_update_device(struct device *dev);

/*
 * Driver data (common to all clients)
 */

static struct i2c_driver max1619_driver = {
	.driver = {
		.name	= "max1619",
	},
	.attach_adapter	= max1619_attach_adapter,
	.detach_client	= max1619_detach_client,
};

/*
 * Client data (each client gets its own)
 */

struct max1619_data {
	struct i2c_client client;
	struct class_device *class_dev;
	struct semaphore update_lock;
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */

	/* registers values */
	u8 temp_input1; /* local */
	u8 temp_input2, temp_low2, temp_high2; /* remote */
	u8 temp_crit2;
	u8 temp_hyst2;
	u8 alarms; 
};

/*
 * Sysfs stuff
 */

#define show_temp(value) \
static ssize_t show_##value(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	struct max1619_data *data = max1619_update_device(dev); \
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->value)); \
}
show_temp(temp_input1);
show_temp(temp_input2);
show_temp(temp_low2);
show_temp(temp_high2);
show_temp(temp_crit2);
show_temp(temp_hyst2);

#define set_temp2(value, reg) \
static ssize_t set_##value(struct device *dev, struct device_attribute *attr, const char *buf, \
	size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct max1619_data *data = i2c_get_clientdata(client); \
	long val = simple_strtol(buf, NULL, 10); \
 \
	down(&data->update_lock); \
	data->value = TEMP_TO_REG(val); \
	i2c_smbus_write_byte_data(client, reg, data->value); \
	up(&data->update_lock); \
	return count; \
}

set_temp2(temp_low2, MAX1619_REG_W_REMOTE_LOW);
set_temp2(temp_high2, MAX1619_REG_W_REMOTE_HIGH);
set_temp2(temp_crit2, MAX1619_REG_W_REMOTE_CRIT);
set_temp2(temp_hyst2, MAX1619_REG_W_TCRIT_HYST);

static ssize_t show_alarms(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct max1619_data *data = max1619_update_device(dev);
	return sprintf(buf, "%d\n", data->alarms);
}

static DEVICE_ATTR(temp1_input, S_IRUGO, show_temp_input1, NULL);
static DEVICE_ATTR(temp2_input, S_IRUGO, show_temp_input2, NULL);
static DEVICE_ATTR(temp2_min, S_IWUSR | S_IRUGO, show_temp_low2,
	set_temp_low2);
static DEVICE_ATTR(temp2_max, S_IWUSR | S_IRUGO, show_temp_high2,
	set_temp_high2);
static DEVICE_ATTR(temp2_crit, S_IWUSR | S_IRUGO, show_temp_crit2,
	set_temp_crit2);
static DEVICE_ATTR(temp2_crit_hyst, S_IWUSR | S_IRUGO, show_temp_hyst2,
	set_temp_hyst2);
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);

/*
 * Real code
 */

static int max1619_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_probe(adapter, &addr_data, max1619_detect);
}

/*
 * The following function does more than just detection. If detection
 * succeeds, it also registers the new chip.
 */
static int max1619_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *new_client;
	struct max1619_data *data;
	int err = 0;
	const char *name = "";	
	u8 reg_config=0, reg_convrate=0, reg_status=0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		goto exit;

	if (!(data = kzalloc(sizeof(struct max1619_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}

	/* The common I2C client data is placed right before the
	   MAX1619-specific data. */
	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &max1619_driver;
	new_client->flags = 0;

	/*
	 * Now we do the remaining detection. A negative kind means that
	 * the driver was loaded with no force parameter (default), so we
	 * must both detect and identify the chip. A zero kind means that
	 * the driver was loaded with the force parameter, the detection
	 * step shall be skipped. A positive kind means that the driver
	 * was loaded with the force parameter and a given kind of chip is
	 * requested, so both the detection and the identification steps
	 * are skipped.
	 */
	if (kind < 0) { /* detection */
		reg_config = i2c_smbus_read_byte_data(new_client,
			      MAX1619_REG_R_CONFIG);
		reg_convrate = i2c_smbus_read_byte_data(new_client,
			       MAX1619_REG_R_CONVRATE);
		reg_status = i2c_smbus_read_byte_data(new_client,
				MAX1619_REG_R_STATUS);
		if ((reg_config & 0x03) != 0x00
		 || reg_convrate > 0x07 || (reg_status & 0x61 ) !=0x00) {
			dev_dbg(&adapter->dev,
				"MAX1619 detection failed at 0x%02x.\n",
				address);
			goto exit_free;
		}
	}

	if (kind <= 0) { /* identification */
		u8 man_id, chip_id;
	
		man_id = i2c_smbus_read_byte_data(new_client,
			 MAX1619_REG_R_MAN_ID);
		chip_id = i2c_smbus_read_byte_data(new_client,
			  MAX1619_REG_R_CHIP_ID);
		
		if ((man_id == 0x4D) && (chip_id == 0x04))
			kind = max1619;

		if (kind <= 0) { /* identification failed */
			dev_info(&adapter->dev,
			    "Unsupported chip (man_id=0x%02X, "
			    "chip_id=0x%02X).\n", man_id, chip_id);
			goto exit_free;
		}
	}

	if (kind == max1619)
		name = "max1619";

	/* We can fill in the remaining client fields */
	strlcpy(new_client->name, name, I2C_NAME_SIZE);
	data->valid = 0;
	init_MUTEX(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto exit_free;

	/* Initialize the MAX1619 chip */
	max1619_init_client(new_client);

	/* Register sysfs hooks */
	data->class_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto exit_detach;
	}

	device_create_file(&new_client->dev, &dev_attr_temp1_input);
	device_create_file(&new_client->dev, &dev_attr_temp2_input);
	device_create_file(&new_client->dev, &dev_attr_temp2_min);
	device_create_file(&new_client->dev, &dev_attr_temp2_max);
	device_create_file(&new_client->dev, &dev_attr_temp2_crit);
	device_create_file(&new_client->dev, &dev_attr_temp2_crit_hyst);
	device_create_file(&new_client->dev, &dev_attr_alarms);

	return 0;

exit_detach:
	i2c_detach_client(new_client);
exit_free:
	kfree(data);
exit:
	return err;
}

static void max1619_init_client(struct i2c_client *client)
{
	u8 config;

	/*
	 * Start the conversions.
	 */
	i2c_smbus_write_byte_data(client, MAX1619_REG_W_CONVRATE,
				  5); /* 2 Hz */
	config = i2c_smbus_read_byte_data(client, MAX1619_REG_R_CONFIG);
	if (config & 0x40)
		i2c_smbus_write_byte_data(client, MAX1619_REG_W_CONFIG,
					  config & 0xBF); /* run */
}

static int max1619_detach_client(struct i2c_client *client)
{
	struct max1619_data *data = i2c_get_clientdata(client);
	int err;

	hwmon_device_unregister(data->class_dev);

	if ((err = i2c_detach_client(client)))
		return err;

	kfree(data);
	return 0;
}

static struct max1619_data *max1619_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max1619_data *data = i2c_get_clientdata(client);

	down(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ * 2) || !data->valid) {
		dev_dbg(&client->dev, "Updating max1619 data.\n");
		data->temp_input1 = i2c_smbus_read_byte_data(client,
					MAX1619_REG_R_LOCAL_TEMP);
		data->temp_input2 = i2c_smbus_read_byte_data(client,
					MAX1619_REG_R_REMOTE_TEMP);
		data->temp_high2 = i2c_smbus_read_byte_data(client,
					MAX1619_REG_R_REMOTE_HIGH);
		data->temp_low2 = i2c_smbus_read_byte_data(client,
					MAX1619_REG_R_REMOTE_LOW);
		data->temp_crit2 = i2c_smbus_read_byte_data(client,
					MAX1619_REG_R_REMOTE_CRIT);
		data->temp_hyst2 = i2c_smbus_read_byte_data(client,
					MAX1619_REG_R_TCRIT_HYST);
		data->alarms = i2c_smbus_read_byte_data(client,
					MAX1619_REG_R_STATUS);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	up(&data->update_lock);

	return data;
}

static int __init sensors_max1619_init(void)
{
	return i2c_add_driver(&max1619_driver);
}

static void __exit sensors_max1619_exit(void)
{
	i2c_del_driver(&max1619_driver);
}

MODULE_AUTHOR("Alexey Fisher <fishor@mail.ru> and "
	"Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("MAX1619 sensor driver");
MODULE_LICENSE("GPL");

module_init(sensors_max1619_init);
module_exit(sensors_max1619_exit);
