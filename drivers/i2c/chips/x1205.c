/*
 *  x1205.c - An i2c driver for the Xicor X1205 RTC
 *  Copyright 2004 Karen Spearel
 *  Copyright 2005 Alessandro Zummo
 *
 *  please send all reports to:
 *	kas11 at tampabay dot rr dot com
 *      a dot zummo at towertech dot it
 *
 *  based on the other drivers in this same directory.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/string.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/list.h>

#include <linux/x1205.h>

#define DRV_VERSION "0.9.9"

/* Addresses to scan: none. This chip is located at
 * 0x6f and uses a two bytes register addressing.
 * Two bytes need to be written to read a single register,
 * while most other chips just require one and take the second
 * one as the data to be written. To prevent corrupting
 * unknown chips, the user must explicitely set the probe parameter.
 */

static unsigned short normal_i2c[] = { I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD;
I2C_CLIENT_MODULE_PARM(hctosys,
	"Set the system time from the hardware clock upon initialization");

/* offsets into CCR area */

#define CCR_SEC			0
#define CCR_MIN			1
#define CCR_HOUR		2
#define CCR_MDAY		3
#define CCR_MONTH		4
#define CCR_YEAR		5
#define CCR_WDAY		6
#define CCR_Y2K			7

#define X1205_REG_SR		0x3F	/* status register */
#define X1205_REG_Y2K		0x37
#define X1205_REG_DW		0x36
#define X1205_REG_YR		0x35
#define X1205_REG_MO		0x34
#define X1205_REG_DT		0x33
#define X1205_REG_HR		0x32
#define X1205_REG_MN		0x31
#define X1205_REG_SC		0x30
#define X1205_REG_DTR		0x13
#define X1205_REG_ATR		0x12
#define X1205_REG_INT		0x11
#define X1205_REG_0		0x10
#define X1205_REG_Y2K1		0x0F
#define X1205_REG_DWA1		0x0E
#define X1205_REG_YRA1		0x0D
#define X1205_REG_MOA1		0x0C
#define X1205_REG_DTA1		0x0B
#define X1205_REG_HRA1		0x0A
#define X1205_REG_MNA1		0x09
#define X1205_REG_SCA1		0x08
#define X1205_REG_Y2K0		0x07
#define X1205_REG_DWA0		0x06
#define X1205_REG_YRA0		0x05
#define X1205_REG_MOA0		0x04
#define X1205_REG_DTA0		0x03
#define X1205_REG_HRA0		0x02
#define X1205_REG_MNA0		0x01
#define X1205_REG_SCA0		0x00

#define X1205_CCR_BASE		0x30	/* Base address of CCR */
#define X1205_ALM0_BASE		0x00	/* Base address of ALARM0 */

#define X1205_SR_RTCF		0x01	/* Clock failure */
#define X1205_SR_WEL		0x02	/* Write Enable Latch */
#define X1205_SR_RWEL		0x04	/* Register Write Enable */

#define X1205_DTR_DTR0		0x01
#define X1205_DTR_DTR1		0x02
#define X1205_DTR_DTR2		0x04

#define X1205_HR_MIL		0x80	/* Set in ccr.hour for 24 hr mode */

/* Prototypes */
static int x1205_attach(struct i2c_adapter *adapter);
static int x1205_detach(struct i2c_client *client);
static int x1205_probe(struct i2c_adapter *adapter, int address, int kind);
static int x1205_command(struct i2c_client *client, unsigned int cmd,
	void *arg);

static struct i2c_driver x1205_driver = {
	.driver = {
		.name	= "x1205",
	},
	.attach_adapter = &x1205_attach,
	.detach_client	= &x1205_detach,
};

struct x1205_data {
	struct i2c_client client;
	struct list_head list;
	unsigned int epoch;
};

static const unsigned char days_in_mo[] =
	{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static LIST_HEAD(x1205_clients);

/* Workaround until the I2C subsytem will allow to send
 * commands to a specific client. This function will send the command
 * to the first client.
 */
int x1205_do_command(unsigned int cmd, void *arg)
{
	struct list_head *walk;
	struct list_head *tmp;
	struct x1205_data *data;

	list_for_each_safe(walk, tmp, &x1205_clients) {
		data = list_entry(walk, struct x1205_data, list);
		return x1205_command(&data->client, cmd, arg);
	}

	return -ENODEV;
}

#define is_leap(year) \
	((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 400 == 0))

/* make sure the rtc_time values are in bounds */
static int x1205_validate_tm(struct rtc_time *tm)
{
	int year = tm->tm_year + 1900;

	if ((tm->tm_year < 70) || (tm->tm_year > 255))
		return -EINVAL;

	if ((tm->tm_mon > 11) || (tm->tm_mday == 0))
		return -EINVAL;

	if (tm->tm_mday > days_in_mo[tm->tm_mon]
		+ ((tm->tm_mon == 1) && is_leap(year)))
		return -EINVAL;

	if ((tm->tm_hour >= 24) || (tm->tm_min >= 60) || (tm->tm_sec >= 60))
		return -EINVAL;

	return 0;
}

/*
 * In the routines that deal directly with the x1205 hardware, we use
 * rtc_time -- month 0-11, hour 0-23, yr = calendar year-epoch
 * Epoch is initialized as 2000. Time is set to UTC.
 */
static int x1205_get_datetime(struct i2c_client *client, struct rtc_time *tm,
				u8 reg_base)
{
	unsigned char dt_addr[2] = { 0, reg_base };
	static unsigned char sr_addr[2] = { 0, X1205_REG_SR };

	unsigned char buf[8], sr;

	struct i2c_msg msgs[] = {
		{ client->addr, 0, 2, sr_addr },	/* setup read ptr */
		{ client->addr, I2C_M_RD, 1, &sr }, 	/* read status */
		{ client->addr, 0, 2, dt_addr },	/* setup read ptr */
		{ client->addr, I2C_M_RD, 8, buf },	/* read date */
	};

	struct x1205_data *data = i2c_get_clientdata(client);

	/* read status register */
	if ((i2c_transfer(client->adapter, &msgs[0], 2)) != 2) {
		dev_err(&client->dev, "%s: read error\n", __FUNCTION__);
		return -EIO;
	}

	/* check for battery failure */
	if (sr & X1205_SR_RTCF) {
		dev_warn(&client->dev,
			"Clock had a power failure, you must set the date.\n");
		return -EINVAL;
	}

	/* read date registers */
	if ((i2c_transfer(client->adapter, &msgs[2], 2)) != 2) {
		dev_err(&client->dev, "%s: read error\n", __FUNCTION__);
		return -EIO;
	}

	dev_dbg(&client->dev,
		"%s: raw read data - sec=%02x, min=%02x, hr=%02x, "
		"mday=%02x, mon=%02x, year=%02x, wday=%02x, y2k=%02x\n",
		__FUNCTION__,
		buf[0], buf[1], buf[2], buf[3],
		buf[4], buf[5], buf[6], buf[7]);

	tm->tm_sec = BCD2BIN(buf[CCR_SEC]);
	tm->tm_min = BCD2BIN(buf[CCR_MIN]);
	tm->tm_hour = BCD2BIN(buf[CCR_HOUR] & 0x3F); /* hr is 0-23 */
	tm->tm_mday = BCD2BIN(buf[CCR_MDAY]);
	tm->tm_mon = BCD2BIN(buf[CCR_MONTH]);
	data->epoch = BCD2BIN(buf[CCR_Y2K]) * 100;
	tm->tm_year = BCD2BIN(buf[CCR_YEAR]) + data->epoch - 1900;
	tm->tm_wday = buf[CCR_WDAY];

	dev_dbg(&client->dev, "%s: tm is secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__FUNCTION__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	return 0;
}

static int x1205_set_datetime(struct i2c_client *client, struct rtc_time *tm,
				int datetoo, u8 reg_base)
{
	int i, err, xfer;

	unsigned char buf[8];

	static const unsigned char wel[3] = { 0, X1205_REG_SR,
						X1205_SR_WEL };

	static const unsigned char rwel[3] = { 0, X1205_REG_SR,
						X1205_SR_WEL | X1205_SR_RWEL };

	static const unsigned char diswe[3] = { 0, X1205_REG_SR, 0 };

	struct x1205_data *data = i2c_get_clientdata(client);

	/* check if all values in the tm struct are correct */
	if ((err = x1205_validate_tm(tm)) < 0)
		return err;

	dev_dbg(&client->dev, "%s: secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__FUNCTION__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	buf[CCR_SEC] = BIN2BCD(tm->tm_sec);
	buf[CCR_MIN] = BIN2BCD(tm->tm_min);

	/* set hour and 24hr bit */
	buf[CCR_HOUR] = BIN2BCD(tm->tm_hour) | X1205_HR_MIL;

	/* should we also set the date? */
	if (datetoo) {
		buf[CCR_MDAY] = BIN2BCD(tm->tm_mday);

		/* month, 0 - 11 */
		buf[CCR_MONTH] = BIN2BCD(tm->tm_mon);

		/* year, since 1900 */
		buf[CCR_YEAR] = BIN2BCD(tm->tm_year + 1900 - data->epoch);
		buf[CCR_WDAY] = tm->tm_wday & 0x07;
		buf[CCR_Y2K] = BIN2BCD(data->epoch / 100);
	}

	/* this sequence is required to unlock the chip */
	xfer = i2c_master_send(client, wel, 3);
	if (xfer != 3) {
		dev_err(&client->dev, "%s: wel - %d\n", __FUNCTION__, xfer);
		return -EIO;
	}

	xfer = i2c_master_send(client, rwel, 3);
	if (xfer != 3) {
		dev_err(&client->dev, "%s: rwel - %d\n", __FUNCTION__, xfer);
		return -EIO;
	}

	/* write register's data */
	for (i = 0; i < (datetoo ? 8 : 3); i++) {
		unsigned char rdata[3] = { 0, reg_base + i, buf[i] };

		xfer = i2c_master_send(client, rdata, 3);
		if (xfer != 3) {
			dev_err(&client->dev,
				"%s: xfer=%d addr=%02x, data=%02x\n",
				__FUNCTION__,
				 xfer, rdata[1], rdata[2]);
			return -EIO;
		}
	};

	/* disable further writes */
	xfer = i2c_master_send(client, diswe, 3);
	if (xfer != 3) {
		dev_err(&client->dev, "%s: diswe - %d\n", __FUNCTION__, xfer);
		return -EIO;
	}

	return 0;
}

static int x1205_get_dtrim(struct i2c_client *client, int *trim)
{
	unsigned char dtr;
	static unsigned char dtr_addr[2] = { 0, X1205_REG_DTR };

	struct i2c_msg msgs[] = {
		{ client->addr, 0, 2, dtr_addr },	/* setup read ptr */
		{ client->addr, I2C_M_RD, 1, &dtr }, 	/* read dtr */
	};

	/* read dtr register */
	if ((i2c_transfer(client->adapter, &msgs[0], 2)) != 2) {
		dev_err(&client->dev, "%s: read error\n", __FUNCTION__);
		return -EIO;
	}

	dev_dbg(&client->dev, "%s: raw dtr=%x\n", __FUNCTION__, dtr);

	*trim = 0;

	if (dtr & X1205_DTR_DTR0)
		*trim += 20;

	if (dtr & X1205_DTR_DTR1)
		*trim += 10;

	if (dtr & X1205_DTR_DTR2)
		*trim = -*trim;

	return 0;
}

static int x1205_get_atrim(struct i2c_client *client, int *trim)
{
	s8 atr;
	static unsigned char atr_addr[2] = { 0, X1205_REG_ATR };

	struct i2c_msg msgs[] = {
		{ client->addr, 0, 2, atr_addr },	/* setup read ptr */
		{ client->addr, I2C_M_RD, 1, &atr }, 	/* read atr */
	};

	/* read atr register */
	if ((i2c_transfer(client->adapter, &msgs[0], 2)) != 2) {
		dev_err(&client->dev, "%s: read error\n", __FUNCTION__);
		return -EIO;
	}

	dev_dbg(&client->dev, "%s: raw atr=%x\n", __FUNCTION__, atr);

	/* atr is a two's complement value on 6 bits,
	 * perform sign extension. The formula is
	 * Catr = (atr * 0.25pF) + 11.00pF.
	 */
	if (atr & 0x20)
		atr |= 0xC0;

	dev_dbg(&client->dev, "%s: raw atr=%x (%d)\n", __FUNCTION__, atr, atr);

	*trim = (atr * 250) + 11000;

	dev_dbg(&client->dev, "%s: real=%d\n", __FUNCTION__, *trim);

	return 0;
}

static int x1205_hctosys(struct i2c_client *client)
{
	int err;

	struct rtc_time tm;
	struct timespec tv;

	err = x1205_command(client, X1205_CMD_GETDATETIME, &tm);

	if (err) {
		dev_err(&client->dev,
			"Unable to set the system clock\n");
		return err;
	}

	/* IMPORTANT: the RTC only stores whole seconds. It is arbitrary
	 * whether it stores the most close value or the value with partial
	 * seconds truncated. However, it is important that we use it to store
	 * the truncated value. This is because otherwise it is necessary,
	 * in an rtc sync function, to read both xtime.tv_sec and
	 * xtime.tv_nsec. On some processors (i.e. ARM), an atomic read
	 * of >32bits is not possible. So storing the most close value would
	 * slow down the sync API. So here we have the truncated value and
	 * the best guess is to add 0.5s.
	 */

	tv.tv_nsec = NSEC_PER_SEC >> 1;

	/* WARNING: this is not the C library 'mktime' call, it is a built in
	 * inline function from include/linux/time.h.  It expects (requires)
	 * the month to be in the range 1-12
	 */

	tv.tv_sec  = mktime(tm.tm_year + 1900, tm.tm_mon + 1,
				tm.tm_mday, tm.tm_hour,
				tm.tm_min, tm.tm_sec);

	do_settimeofday(&tv);

	dev_info(&client->dev,
		"setting the system clock to %d-%d-%d %d:%d:%d\n",
		tm.tm_year + 1900, tm.tm_mon + 1,
		tm.tm_mday, tm.tm_hour, tm.tm_min,
		tm.tm_sec);

	return 0;
}

struct x1205_limit
{
	unsigned char reg;
	unsigned char mask;
	unsigned char min;
	unsigned char max;
};

static int x1205_validate_client(struct i2c_client *client)
{
	int i, xfer;

	/* Probe array. We will read the register at the specified
	 * address and check if the given bits are zero.
	 */
	static const unsigned char probe_zero_pattern[] = {
		/* register, mask */
		X1205_REG_SR,	0x18,
		X1205_REG_DTR,	0xF8,
		X1205_REG_ATR,	0xC0,
		X1205_REG_INT,	0x18,
		X1205_REG_0,	0xFF,
	};

	static const struct x1205_limit probe_limits_pattern[] = {
		/* register, mask, min, max */
		{ X1205_REG_Y2K,	0xFF,	19,	20	},
		{ X1205_REG_DW,		0xFF,	0,	6	},
		{ X1205_REG_YR,		0xFF,	0,	99	},
		{ X1205_REG_MO,		0xFF,	0,	12	},
		{ X1205_REG_DT,		0xFF,	0,	31	},
		{ X1205_REG_HR,		0x7F,	0,	23	},
		{ X1205_REG_MN,		0xFF,	0,	59	},
		{ X1205_REG_SC,		0xFF,	0,	59	},
		{ X1205_REG_Y2K1,	0xFF,	19,	20	},
		{ X1205_REG_Y2K0,	0xFF,	19,	20	},
	};

	/* check that registers have bits a 0 where expected */
	for (i = 0; i < ARRAY_SIZE(probe_zero_pattern); i += 2) {
		unsigned char buf;

		unsigned char addr[2] = { 0, probe_zero_pattern[i] };

		struct i2c_msg msgs[2] = {
			{ client->addr, 0, 2, addr },
			{ client->addr, I2C_M_RD, 1, &buf },
		};

		xfer = i2c_transfer(client->adapter, msgs, 2);
		if (xfer != 2) {
			dev_err(&client->adapter->dev,
				"%s: could not read register %x\n",
				__FUNCTION__, addr[1]);

			return -EIO;
		}

		if ((buf & probe_zero_pattern[i+1]) != 0) {
			dev_err(&client->adapter->dev,
				"%s: register=%02x, zero pattern=%d, value=%x\n",
				__FUNCTION__, addr[1], i, buf);

			return -ENODEV;
		}
	}

	/* check limits (only registers with bcd values) */
	for (i = 0; i < ARRAY_SIZE(probe_limits_pattern); i++) {
		unsigned char reg, value;

		unsigned char addr[2] = { 0, probe_limits_pattern[i].reg };

		struct i2c_msg msgs[2] = {
			{ client->addr, 0, 2, addr },
			{ client->addr, I2C_M_RD, 1, &reg },
		};

		xfer = i2c_transfer(client->adapter, msgs, 2);

		if (xfer != 2) {
			dev_err(&client->adapter->dev,
				"%s: could not read register %x\n",
				__FUNCTION__, addr[1]);

			return -EIO;
		}

		value = BCD2BIN(reg & probe_limits_pattern[i].mask);

		if (value > probe_limits_pattern[i].max ||
			value < probe_limits_pattern[i].min) {
			dev_dbg(&client->adapter->dev,
				"%s: register=%x, lim pattern=%d, value=%d\n",
				__FUNCTION__, addr[1], i, value);

			return -ENODEV;
		}
	}

	return 0;
}

static int x1205_attach(struct i2c_adapter *adapter)
{
	dev_dbg(&adapter->dev, "%s\n", __FUNCTION__);

	return i2c_probe(adapter, &addr_data, x1205_probe);
}

int x1205_direct_attach(int adapter_id,
	struct i2c_client_address_data *address_data)
{
	int err;
	struct i2c_adapter *adapter = i2c_get_adapter(adapter_id);

	if (adapter) {
		err = i2c_probe(adapter,
			address_data, x1205_probe);

		i2c_put_adapter(adapter);

		return err;
	}

	return -ENODEV;
}

static int x1205_probe(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *client;
	struct x1205_data *data;

	int err = 0;

	dev_dbg(&adapter->dev, "%s\n", __FUNCTION__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit;
	}

	if (!(data = kzalloc(sizeof(struct x1205_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}

	/* Initialize our structures */
	data->epoch = 2000;

	client = &data->client;
	client->addr = address;
	client->driver = &x1205_driver;
	client->adapter	= adapter;

	strlcpy(client->name, "x1205", I2C_NAME_SIZE);

	i2c_set_clientdata(client, data);

	/* Verify the chip is really an X1205 */
	if (kind < 0) {
		if (x1205_validate_client(client) < 0) {
			err = -ENODEV;
			goto exit_kfree;
		}
	}

	/* Inform the i2c layer */
	if ((err = i2c_attach_client(client)))
		goto exit_kfree;

	list_add(&data->list, &x1205_clients);

	dev_info(&client->dev, "chip found, driver version " DRV_VERSION "\n");

	/* If requested, set the system time */
	if (hctosys)
		x1205_hctosys(client);

	return 0;

exit_kfree:
	kfree(data);

exit:
	return err;
}

static int x1205_detach(struct i2c_client *client)
{
	int err;
	struct x1205_data *data = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "%s\n", __FUNCTION__);

	if ((err = i2c_detach_client(client)))
		return err;

	list_del(&data->list);

	kfree(data);

	return 0;
}

static int x1205_command(struct i2c_client *client, unsigned int cmd,
	void *param)
{
	if (param == NULL)
		return -EINVAL;

	if (!capable(CAP_SYS_TIME))
		return -EACCES;

	dev_dbg(&client->dev, "%s: cmd=%d\n", __FUNCTION__, cmd);

	switch (cmd) {
	case X1205_CMD_GETDATETIME:
		return x1205_get_datetime(client, param, X1205_CCR_BASE);

	case X1205_CMD_SETTIME:
		return x1205_set_datetime(client, param, 0,
				X1205_CCR_BASE);

	case X1205_CMD_SETDATETIME:
		return x1205_set_datetime(client, param, 1,
				X1205_CCR_BASE);

	case X1205_CMD_GETALARM:
		return x1205_get_datetime(client, param, X1205_ALM0_BASE);

	case X1205_CMD_SETALARM:
		return x1205_set_datetime(client, param, 1,
				X1205_ALM0_BASE);

	case X1205_CMD_GETDTRIM:
		return x1205_get_dtrim(client, param);

	case X1205_CMD_GETATRIM:
		return x1205_get_atrim(client, param);

	default:
		return -EINVAL;
	}
}

static int __init x1205_init(void)
{
	return i2c_add_driver(&x1205_driver);
}

static void __exit x1205_exit(void)
{
	i2c_del_driver(&x1205_driver);
}

MODULE_AUTHOR(
	"Karen Spearel <kas11@tampabay.rr.com>, "
	"Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("Xicor X1205 RTC driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

EXPORT_SYMBOL_GPL(x1205_do_command);
EXPORT_SYMBOL_GPL(x1205_direct_attach);

module_init(x1205_init);
module_exit(x1205_exit);
