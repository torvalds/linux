// SPDX-License-Identifier: GPL-2.0-only
/*
 *  drivers/rtc/rtc-pcf8583.c
 *
 *  Copyright (C) 2000 Russell King
 *  Copyright (C) 2008 Wolfram Sang & Juergen Beisert, Pengutronix
 *
 *  Driver for PCF8583 RTC & RAM chip
 *
 *  Converted to the generic RTC susbsystem by G. Liakhovetski (2006)
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/rtc.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/bcd.h>

struct rtc_mem {
	unsigned int	loc;
	unsigned int	nr;
	unsigned char	*data;
};

struct pcf8583 {
	struct rtc_device *rtc;
	unsigned char ctrl;
};

#define CTRL_STOP	0x80
#define CTRL_HOLD	0x40
#define CTRL_32KHZ	0x00
#define CTRL_MASK	0x08
#define CTRL_ALARMEN	0x04
#define CTRL_ALARM	0x02
#define CTRL_TIMER	0x01


static struct i2c_driver pcf8583_driver;

#define get_ctrl(x)    ((struct pcf8583 *)i2c_get_clientdata(x))->ctrl
#define set_ctrl(x, v) get_ctrl(x) = v

#define CMOS_YEAR	(64 + 128)
#define CMOS_CHECKSUM	(63)

static int pcf8583_get_datetime(struct i2c_client *client, struct rtc_time *dt)
{
	unsigned char buf[8], addr[1] = { 1 };
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = addr,
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 6,
			.buf = buf,
		}
	};
	int ret;

	memset(buf, 0, sizeof(buf));

	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret == 2) {
		dt->tm_year = buf[4] >> 6;
		dt->tm_wday = buf[5] >> 5;

		buf[4] &= 0x3f;
		buf[5] &= 0x1f;

		dt->tm_sec = bcd2bin(buf[1]);
		dt->tm_min = bcd2bin(buf[2]);
		dt->tm_hour = bcd2bin(buf[3]);
		dt->tm_mday = bcd2bin(buf[4]);
		dt->tm_mon = bcd2bin(buf[5]) - 1;
	}

	return ret == 2 ? 0 : -EIO;
}

static int pcf8583_set_datetime(struct i2c_client *client, struct rtc_time *dt, int datetoo)
{
	unsigned char buf[8];
	int ret, len = 6;

	buf[0] = 0;
	buf[1] = get_ctrl(client) | 0x80;
	buf[2] = 0;
	buf[3] = bin2bcd(dt->tm_sec);
	buf[4] = bin2bcd(dt->tm_min);
	buf[5] = bin2bcd(dt->tm_hour);

	if (datetoo) {
		len = 8;
		buf[6] = bin2bcd(dt->tm_mday) | (dt->tm_year << 6);
		buf[7] = bin2bcd(dt->tm_mon + 1)  | (dt->tm_wday << 5);
	}

	ret = i2c_master_send(client, (char *)buf, len);
	if (ret != len)
		return -EIO;

	buf[1] = get_ctrl(client);
	ret = i2c_master_send(client, (char *)buf, 2);

	return ret == 2 ? 0 : -EIO;
}

static int pcf8583_get_ctrl(struct i2c_client *client, unsigned char *ctrl)
{
	*ctrl = get_ctrl(client);
	return 0;
}

static int pcf8583_set_ctrl(struct i2c_client *client, unsigned char *ctrl)
{
	unsigned char buf[2];

	buf[0] = 0;
	buf[1] = *ctrl;
	set_ctrl(client, *ctrl);

	return i2c_master_send(client, (char *)buf, 2);
}

static int pcf8583_read_mem(struct i2c_client *client, struct rtc_mem *mem)
{
	unsigned char addr[1];
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = addr,
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = mem->nr,
			.buf = mem->data,
		}
	};

	if (mem->loc < 8)
		return -EINVAL;

	addr[0] = mem->loc;

	return i2c_transfer(client->adapter, msgs, 2) == 2 ? 0 : -EIO;
}

static int pcf8583_write_mem(struct i2c_client *client, struct rtc_mem *mem)
{
	unsigned char buf[9];
	int ret;

	if (mem->loc < 8 || mem->nr > 8)
		return -EINVAL;

	buf[0] = mem->loc;
	memcpy(buf + 1, mem->data, mem->nr);

	ret = i2c_master_send(client, buf, mem->nr + 1);
	return ret == mem->nr + 1 ? 0 : -EIO;
}

static int pcf8583_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char ctrl, year[2];
	struct rtc_mem mem = {
		.loc = CMOS_YEAR,
		.nr = sizeof(year),
		.data = year
	};
	int real_year, year_offset, err;

	/*
	 * Ensure that the RTC is running.
	 */
	pcf8583_get_ctrl(client, &ctrl);
	if (ctrl & (CTRL_STOP | CTRL_HOLD)) {
		unsigned char new_ctrl = ctrl & ~(CTRL_STOP | CTRL_HOLD);

		dev_warn(dev, "resetting control %02x -> %02x\n",
			ctrl, new_ctrl);

		err = pcf8583_set_ctrl(client, &new_ctrl);
		if (err < 0)
			return err;
	}

	if (pcf8583_get_datetime(client, tm) ||
	    pcf8583_read_mem(client, &mem))
		return -EIO;

	real_year = year[0];

	/*
	 * The RTC year holds the LSB two bits of the current
	 * year, which should reflect the LSB two bits of the
	 * CMOS copy of the year.  Any difference indicates
	 * that we have to correct the CMOS version.
	 */
	year_offset = tm->tm_year - (real_year & 3);
	if (year_offset < 0)
		/*
		 * RTC year wrapped.  Adjust it appropriately.
		 */
		year_offset += 4;

	tm->tm_year = (real_year + year_offset + year[1] * 100) - 1900;

	return 0;
}

static int pcf8583_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned char year[2], chk;
	struct rtc_mem cmos_year  = {
		.loc = CMOS_YEAR,
		.nr = sizeof(year),
		.data = year
	};
	struct rtc_mem cmos_check = {
		.loc = CMOS_CHECKSUM,
		.nr = 1,
		.data = &chk
	};
	unsigned int proper_year = tm->tm_year + 1900;
	int ret;

	/*
	 * The RTC's own 2-bit year must reflect the least
	 * significant two bits of the CMOS year.
	 */

	ret = pcf8583_set_datetime(client, tm, 1);
	if (ret)
		return ret;

	ret = pcf8583_read_mem(client, &cmos_check);
	if (ret)
		return ret;

	ret = pcf8583_read_mem(client, &cmos_year);
	if (ret)
		return ret;

	chk -= year[1] + year[0];

	year[1] = proper_year / 100;
	year[0] = proper_year % 100;

	chk += year[1] + year[0];

	ret = pcf8583_write_mem(client, &cmos_year);

	if (ret)
		return ret;

	ret = pcf8583_write_mem(client, &cmos_check);

	return ret;
}

static const struct rtc_class_ops pcf8583_rtc_ops = {
	.read_time	= pcf8583_rtc_read_time,
	.set_time	= pcf8583_rtc_set_time,
};

static int pcf8583_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct pcf8583 *pcf8583;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	pcf8583 = devm_kzalloc(&client->dev, sizeof(struct pcf8583),
				GFP_KERNEL);
	if (!pcf8583)
		return -ENOMEM;

	i2c_set_clientdata(client, pcf8583);

	pcf8583->rtc = devm_rtc_device_register(&client->dev,
				pcf8583_driver.driver.name,
				&pcf8583_rtc_ops, THIS_MODULE);

	return PTR_ERR_OR_ZERO(pcf8583->rtc);
}

static const struct i2c_device_id pcf8583_id[] = {
	{ "pcf8583", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pcf8583_id);

static struct i2c_driver pcf8583_driver = {
	.driver = {
		.name	= "pcf8583",
	},
	.probe		= pcf8583_probe,
	.id_table	= pcf8583_id,
};

module_i2c_driver(pcf8583_driver);

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("PCF8583 I2C RTC driver");
MODULE_LICENSE("GPL");
