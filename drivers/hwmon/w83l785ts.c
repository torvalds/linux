/*
 * w83l785ts.c - Part of lm_sensors, Linux kernel modules for hardware
 *               monitoring
 * Copyright (C) 2003-2004  Jean Delvare <khali@linux-fr.org>
 *
 * Inspired from the lm83 driver. The W83L785TS-S is a sensor chip made
 * by Winbond. It reports a single external temperature with a 1 deg
 * resolution and a 3 deg accuracy. Datasheet can be obtained from
 * Winbond's website at:
 *   http://www.winbond-usa.com/products/winbond_products/pdfs/PCIC/W83L785TS-S.pdf
 *
 * Ported to Linux 2.6 by Wolfgang Ziegler <nuppla@gmx.at> and Jean Delvare
 * <khali@linux-fr.org>.
 *
 * Thanks to James Bolt <james@evilpenguin.com> for benchmarking the read
 * error handling mechanism.
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
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/err.h>

/* How many retries on register read error */
#define MAX_RETRIES	5

/*
 * Address to scan
 * Address is fully defined internally and cannot be changed.
 */

static unsigned short normal_i2c[] = { 0x2e, I2C_CLIENT_END };

/*
 * Insmod parameters
 */

I2C_CLIENT_INSMOD_1(w83l785ts);

/*
 * The W83L785TS-S registers
 * Manufacturer ID is 0x5CA3 for Winbond.
 */

#define W83L785TS_REG_MAN_ID1		0x4D
#define W83L785TS_REG_MAN_ID2		0x4C
#define W83L785TS_REG_CHIP_ID		0x4E
#define W83L785TS_REG_CONFIG		0x40
#define W83L785TS_REG_TYPE		0x52
#define W83L785TS_REG_TEMP		0x27
#define W83L785TS_REG_TEMP_OVER		0x53 /* not sure about this one */

/*
 * Conversions
 * The W83L785TS-S uses signed 8-bit values.
 */

#define TEMP_FROM_REG(val)	((val & 0x80 ? val-0x100 : val) * 1000)

/*
 * Functions declaration
 */

static int w83l785ts_attach_adapter(struct i2c_adapter *adapter);
static int w83l785ts_detect(struct i2c_adapter *adapter, int address,
	int kind);
static int w83l785ts_detach_client(struct i2c_client *client);
static u8 w83l785ts_read_value(struct i2c_client *client, u8 reg, u8 defval);
static struct w83l785ts_data *w83l785ts_update_device(struct device *dev);

/*
 * Driver data (common to all clients)
 */
 
static struct i2c_driver w83l785ts_driver = {
	.owner		= THIS_MODULE,
	.name		= "w83l785ts",
	.id		= I2C_DRIVERID_W83L785TS,
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter	= w83l785ts_attach_adapter,
	.detach_client	= w83l785ts_detach_client,
};

/*
 * Client data (each client gets its own)
 */

struct w83l785ts_data {
	struct i2c_client client;
	struct class_device *class_dev;
	struct semaphore update_lock;
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */

	/* registers values */
	u8 temp, temp_over;
};

/*
 * Sysfs stuff
 */

static ssize_t show_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83l785ts_data *data = w83l785ts_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp));
}

static ssize_t show_temp_over(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83l785ts_data *data = w83l785ts_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_over));
}

static DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL);
static DEVICE_ATTR(temp1_max, S_IRUGO, show_temp_over, NULL);

/*
 * Real code
 */

static int w83l785ts_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_probe(adapter, &addr_data, w83l785ts_detect);
}

/*
 * The following function does more than just detection. If detection
 * succeeds, it also registers the new chip.
 */
static int w83l785ts_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *new_client;
	struct w83l785ts_data *data;
	int err = 0;


	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		goto exit;

	if (!(data = kmalloc(sizeof(struct w83l785ts_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}
	memset(data, 0, sizeof(struct w83l785ts_data));


	/* The common I2C client data is placed right before the
	 * W83L785TS-specific data. */
	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &w83l785ts_driver;
	new_client->flags = 0;

	/*
	 * Now we do the remaining detection. A negative kind means that
	 * the driver was loaded with no force parameter (default), so we
	 * must both detect and identify the chip (actually there is only
	 * one possible kind of chip for now, W83L785TS-S). A zero kind means
	 * that the driver was loaded with the force parameter, the detection
	 * step shall be skipped. A positive kind means that the driver
	 * was loaded with the force parameter and a given kind of chip is
	 * requested, so both the detection and the identification steps
	 * are skipped.
	 */
	if (kind < 0) { /* detection */
		if (((w83l785ts_read_value(new_client,
		      W83L785TS_REG_CONFIG, 0) & 0x80) != 0x00)
		 || ((w83l785ts_read_value(new_client,
		      W83L785TS_REG_TYPE, 0) & 0xFC) != 0x00)) {
			dev_dbg(&adapter->dev,
				"W83L785TS-S detection failed at 0x%02x.\n",
				address);
			goto exit_free;
		}
	}

	if (kind <= 0) { /* identification */
		u16 man_id;
		u8 chip_id;

		man_id = (w83l785ts_read_value(new_client,
			 W83L785TS_REG_MAN_ID1, 0) << 8) +
			 w83l785ts_read_value(new_client,
			 W83L785TS_REG_MAN_ID2, 0);
		chip_id = w83l785ts_read_value(new_client,
			  W83L785TS_REG_CHIP_ID, 0);

		if (man_id == 0x5CA3) { /* Winbond */
			if (chip_id == 0x70) { /* W83L785TS-S */
				kind = w83l785ts;			
			}
		}
	
		if (kind <= 0) { /* identification failed */
			dev_info(&adapter->dev,
				 "Unsupported chip (man_id=0x%04X, "
				 "chip_id=0x%02X).\n", man_id, chip_id);
			goto exit_free;
		}
	}

	/* We can fill in the remaining client fields. */
	strlcpy(new_client->name, "w83l785ts", I2C_NAME_SIZE);
	data->valid = 0;
	init_MUTEX(&data->update_lock);

	/* Default values in case the first read fails (unlikely). */
	data->temp_over = data->temp = 0;

	/* Tell the I2C layer a new client has arrived. */
	if ((err = i2c_attach_client(new_client))) 
		goto exit_free;

	/*
	 * Initialize the W83L785TS chip
	 * Nothing yet, assume it is already started.
	 */

	/* Register sysfs hooks */
	data->class_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto exit_detach;
	}

	device_create_file(&new_client->dev, &dev_attr_temp1_input);
	device_create_file(&new_client->dev, &dev_attr_temp1_max);

	return 0;

exit_detach:
	i2c_detach_client(new_client);
exit_free:
	kfree(data);
exit:
	return err;
}

static int w83l785ts_detach_client(struct i2c_client *client)
{
	struct w83l785ts_data *data = i2c_get_clientdata(client);
	int err;

	hwmon_device_unregister(data->class_dev);

	if ((err = i2c_detach_client(client)))
		return err;

	kfree(data);
	return 0;
}

static u8 w83l785ts_read_value(struct i2c_client *client, u8 reg, u8 defval)
{
	int value, i;

	/* Frequent read errors have been reported on Asus boards, so we
	 * retry on read errors. If it still fails (unlikely), return the
	 * default value requested by the caller. */
	for (i = 1; i <= MAX_RETRIES; i++) {
		value = i2c_smbus_read_byte_data(client, reg);
		if (value >= 0) {
			dev_dbg(&client->dev, "Read 0x%02x from register "
				"0x%02x.\n", value, reg);
			return value;
		}
		dev_dbg(&client->dev, "Read failed, will retry in %d.\n", i);
		msleep(i);
	}

	dev_err(&client->dev, "Couldn't read value from register 0x%02x. "
		"Please report.\n", reg);
	return defval;
}

static struct w83l785ts_data *w83l785ts_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83l785ts_data *data = i2c_get_clientdata(client);

	down(&data->update_lock);

	if (!data->valid || time_after(jiffies, data->last_updated + HZ * 2)) {
		dev_dbg(&client->dev, "Updating w83l785ts data.\n");
		data->temp = w83l785ts_read_value(client,
			     W83L785TS_REG_TEMP, data->temp);
		data->temp_over = w83l785ts_read_value(client,
				  W83L785TS_REG_TEMP_OVER, data->temp_over);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	up(&data->update_lock);

	return data;
}

static int __init sensors_w83l785ts_init(void)
{
	return i2c_add_driver(&w83l785ts_driver);
}

static void __exit sensors_w83l785ts_exit(void)
{
	i2c_del_driver(&w83l785ts_driver);
}

MODULE_AUTHOR("Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("W83L785TS-S driver");
MODULE_LICENSE("GPL");

module_init(sensors_w83l785ts_init);
module_exit(sensors_w83l785ts_exit);
