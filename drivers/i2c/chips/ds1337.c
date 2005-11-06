/*
 *  linux/drivers/i2c/chips/ds1337.c
 *
 *  Copyright (C) 2005 James Chapman <jchapman@katalix.com>
 *
 *	based on linux/drivers/acorn/char/pcf8583.c
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Driver for Dallas Semiconductor DS1337 and DS1339 real time clock chip
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/string.h>
#include <linux/rtc.h>		/* get the user-level API */
#include <linux/bcd.h>
#include <linux/list.h>

/* Device registers */
#define DS1337_REG_HOUR		2
#define DS1337_REG_DAY		3
#define DS1337_REG_DATE		4
#define DS1337_REG_MONTH	5
#define DS1337_REG_CONTROL	14
#define DS1337_REG_STATUS	15

/* FIXME - how do we export these interface constants? */
#define DS1337_GET_DATE		0
#define DS1337_SET_DATE		1

/*
 * Functions declaration
 */
static unsigned short normal_i2c[] = { 0x68, I2C_CLIENT_END };

I2C_CLIENT_INSMOD_1(ds1337);

static int ds1337_attach_adapter(struct i2c_adapter *adapter);
static int ds1337_detect(struct i2c_adapter *adapter, int address, int kind);
static void ds1337_init_client(struct i2c_client *client);
static int ds1337_detach_client(struct i2c_client *client);
static int ds1337_command(struct i2c_client *client, unsigned int cmd,
			  void *arg);

/*
 * Driver data (common to all clients)
 */
static struct i2c_driver ds1337_driver = {
	.owner		= THIS_MODULE,
	.name		= "ds1337",
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter	= ds1337_attach_adapter,
	.detach_client	= ds1337_detach_client,
	.command	= ds1337_command,
};

/*
 * Client data (each client gets its own)
 */
struct ds1337_data {
	struct i2c_client client;
	struct list_head list;
};

/*
 * Internal variables
 */
static LIST_HEAD(ds1337_clients);

static inline int ds1337_read(struct i2c_client *client, u8 reg, u8 *value)
{
	s32 tmp = i2c_smbus_read_byte_data(client, reg);

	if (tmp < 0)
		return -EIO;

	*value = tmp;

	return 0;
}

/*
 * Chip access functions
 */
static int ds1337_get_datetime(struct i2c_client *client, struct rtc_time *dt)
{
	int result;
	u8 buf[7];
	u8 val;
	struct i2c_msg msg[2];
	u8 offs = 0;

	if (!dt) {
		dev_dbg(&client->dev, "%s: EINVAL: dt=NULL\n", __FUNCTION__);
		return -EINVAL;
	}

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &offs;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = sizeof(buf);
	msg[1].buf = &buf[0];

	result = i2c_transfer(client->adapter, msg, 2);

	dev_dbg(&client->dev, "%s: [%d] %02x %02x %02x %02x %02x %02x %02x\n",
		__FUNCTION__, result, buf[0], buf[1], buf[2], buf[3],
		buf[4], buf[5], buf[6]);

	if (result == 2) {
		dt->tm_sec = BCD2BIN(buf[0]);
		dt->tm_min = BCD2BIN(buf[1]);
		val = buf[2] & 0x3f;
		dt->tm_hour = BCD2BIN(val);
		dt->tm_wday = BCD2BIN(buf[3]) - 1;
		dt->tm_mday = BCD2BIN(buf[4]);
		val = buf[5] & 0x7f;
		dt->tm_mon = BCD2BIN(val) - 1;
		dt->tm_year = BCD2BIN(buf[6]);
		if (buf[5] & 0x80)
			dt->tm_year += 100;

		dev_dbg(&client->dev, "%s: secs=%d, mins=%d, "
			"hours=%d, mday=%d, mon=%d, year=%d, wday=%d\n",
			__FUNCTION__, dt->tm_sec, dt->tm_min,
			dt->tm_hour, dt->tm_mday,
			dt->tm_mon, dt->tm_year, dt->tm_wday);

		return 0;
	}

	dev_err(&client->dev, "error reading data! %d\n", result);
	return -EIO;
}

static int ds1337_set_datetime(struct i2c_client *client, struct rtc_time *dt)
{
	int result;
	u8 buf[8];
	u8 val;
	struct i2c_msg msg[1];

	if (!dt) {
		dev_dbg(&client->dev, "%s: EINVAL: dt=NULL\n", __FUNCTION__);
		return -EINVAL;
	}

	dev_dbg(&client->dev, "%s: secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n", __FUNCTION__,
		dt->tm_sec, dt->tm_min, dt->tm_hour,
		dt->tm_mday, dt->tm_mon, dt->tm_year, dt->tm_wday);

	buf[0] = 0;		/* reg offset */
	buf[1] = BIN2BCD(dt->tm_sec);
	buf[2] = BIN2BCD(dt->tm_min);
	buf[3] = BIN2BCD(dt->tm_hour);
	buf[4] = BIN2BCD(dt->tm_wday) + 1;
	buf[5] = BIN2BCD(dt->tm_mday);
	buf[6] = BIN2BCD(dt->tm_mon) + 1;
	val = dt->tm_year;
	if (val >= 100) {
		val -= 100;
		buf[6] |= (1 << 7);
	}
	buf[7] = BIN2BCD(val);

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = sizeof(buf);
	msg[0].buf = &buf[0];

	result = i2c_transfer(client->adapter, msg, 1);
	if (result == 1)
		return 0;

	dev_err(&client->dev, "error writing data! %d\n", result);
	return -EIO;
}

static int ds1337_command(struct i2c_client *client, unsigned int cmd,
			  void *arg)
{
	dev_dbg(&client->dev, "%s: cmd=%d\n", __FUNCTION__, cmd);

	switch (cmd) {
	case DS1337_GET_DATE:
		return ds1337_get_datetime(client, arg);

	case DS1337_SET_DATE:
		return ds1337_set_datetime(client, arg);

	default:
		return -EINVAL;
	}
}

/*
 * Public API for access to specific device. Useful for low-level
 * RTC access from kernel code.
 */
int ds1337_do_command(int bus, int cmd, void *arg)
{
	struct list_head *walk;
	struct list_head *tmp;
	struct ds1337_data *data;

	list_for_each_safe(walk, tmp, &ds1337_clients) {
		data = list_entry(walk, struct ds1337_data, list);
		if (data->client.adapter->nr == bus)
			return ds1337_command(&data->client, cmd, arg);
	}

	return -ENODEV;
}

static int ds1337_attach_adapter(struct i2c_adapter *adapter)
{
	return i2c_probe(adapter, &addr_data, ds1337_detect);
}

/*
 * The following function does more than just detection. If detection
 * succeeds, it also registers the new chip.
 */
static int ds1337_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *new_client;
	struct ds1337_data *data;
	int err = 0;
	const char *name = "";

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_I2C))
		goto exit;

	if (!(data = kzalloc(sizeof(struct ds1337_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}
	INIT_LIST_HEAD(&data->list);

	/* The common I2C client data is placed right before the
	 * DS1337-specific data. 
	 */
	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &ds1337_driver;
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
	 *
	 * For detection, we read registers that are most likely to cause
	 * detection failure, i.e. those that have more bits with fixed
	 * or reserved values.
	 */

	/* Default to an DS1337 if forced */
	if (kind == 0)
		kind = ds1337;

	if (kind < 0) {		/* detection and identification */
		u8 data;

		/* Check that status register bits 6-2 are zero */
		if ((ds1337_read(new_client, DS1337_REG_STATUS, &data) < 0) ||
		    (data & 0x7c))
			goto exit_free;

		/* Check for a valid day register value */
		if ((ds1337_read(new_client, DS1337_REG_DAY, &data) < 0) ||
		    (data == 0) || (data & 0xf8))
			goto exit_free;

		/* Check for a valid date register value */
		if ((ds1337_read(new_client, DS1337_REG_DATE, &data) < 0) ||
		    (data == 0) || (data & 0xc0) || ((data & 0x0f) > 9) ||
		    (data >= 0x32))
			goto exit_free;

		/* Check for a valid month register value */
		if ((ds1337_read(new_client, DS1337_REG_MONTH, &data) < 0) ||
		    (data == 0) || (data & 0x60) || ((data & 0x0f) > 9) ||
		    ((data >= 0x13) && (data <= 0x19)))
			goto exit_free;

		/* Check that control register bits 6-5 are zero */
		if ((ds1337_read(new_client, DS1337_REG_CONTROL, &data) < 0) ||
		    (data & 0x60))
			goto exit_free;

		kind = ds1337;
	}

	if (kind == ds1337)
		name = "ds1337";

	/* We can fill in the remaining client fields */
	strlcpy(new_client->name, name, I2C_NAME_SIZE);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto exit_free;

	/* Initialize the DS1337 chip */
	ds1337_init_client(new_client);

	/* Add client to local list */
	list_add(&data->list, &ds1337_clients);

	return 0;

exit_free:
	kfree(data);
exit:
	return err;
}

static void ds1337_init_client(struct i2c_client *client)
{
	s32 val;

	/* Ensure that device is set in 24-hour mode */
	val = i2c_smbus_read_byte_data(client, DS1337_REG_HOUR);
	if ((val >= 0) && (val & (1 << 6)))
		i2c_smbus_write_byte_data(client, DS1337_REG_HOUR,
					  val & 0x3f);
}

static int ds1337_detach_client(struct i2c_client *client)
{
	int err;
	struct ds1337_data *data = i2c_get_clientdata(client);

	if ((err = i2c_detach_client(client)))
		return err;

	list_del(&data->list);
	kfree(data);
	return 0;
}

static int __init ds1337_init(void)
{
	return i2c_add_driver(&ds1337_driver);
}

static void __exit ds1337_exit(void)
{
	i2c_del_driver(&ds1337_driver);
}

MODULE_AUTHOR("James Chapman <jchapman@katalix.com>");
MODULE_DESCRIPTION("DS1337 RTC driver");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL_GPL(ds1337_do_command);

module_init(ds1337_init);
module_exit(ds1337_exit);
