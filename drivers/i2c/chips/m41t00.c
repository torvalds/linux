/*
 * I2C client/driver for the ST M41T00 family of i2c rtc chips.
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2005, 2006 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
/*
 * This i2c client/driver wedges between the drivers/char/genrtc.c RTC
 * interface and the SMBus interface of the i2c subsystem.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/m41t00.h>
#include <asm/time.h>
#include <asm/rtc.h>

static struct i2c_driver m41t00_driver;
static struct i2c_client *save_client;

static unsigned short ignore[] = { I2C_CLIENT_END };
static unsigned short normal_addr[] = { I2C_CLIENT_END, I2C_CLIENT_END };

static struct i2c_client_address_data addr_data = {
	.normal_i2c	= normal_addr,
	.probe		= ignore,
	.ignore		= ignore,
};

struct m41t00_chip_info {
	u8	type;
	char	*name;
	u8	read_limit;
	u8	sec;		/* Offsets for chip regs */
	u8	min;
	u8	hour;
	u8	day;
	u8	mon;
	u8	year;
	u8	alarm_mon;
	u8	alarm_hour;
	u8	sqw;
	u8	sqw_freq;
};

static struct m41t00_chip_info m41t00_chip_info_tbl[] = {
	{
		.type		= M41T00_TYPE_M41T00,
		.name		= "m41t00",
		.read_limit	= 5,
		.sec		= 0,
		.min		= 1,
		.hour		= 2,
		.day		= 4,
		.mon		= 5,
		.year		= 6,
	},
	{
		.type		= M41T00_TYPE_M41T81,
		.name		= "m41t81",
		.read_limit	= 1,
		.sec		= 1,
		.min		= 2,
		.hour		= 3,
		.day		= 5,
		.mon		= 6,
		.year		= 7,
		.alarm_mon	= 0xa,
		.alarm_hour	= 0xc,
		.sqw		= 0x13,
	},
	{
		.type		= M41T00_TYPE_M41T85,
		.name		= "m41t85",
		.read_limit	= 1,
		.sec		= 1,
		.min		= 2,
		.hour		= 3,
		.day		= 5,
		.mon		= 6,
		.year		= 7,
		.alarm_mon	= 0xa,
		.alarm_hour	= 0xc,
		.sqw		= 0x13,
	},
};
static struct m41t00_chip_info *m41t00_chip;

ulong
m41t00_get_rtc_time(void)
{
	s32 sec, min, hour, day, mon, year;
	s32 sec1, min1, hour1, day1, mon1, year1;
	u8 reads = 0;
	u8 buf[8], msgbuf[1] = { 0 }; /* offset into rtc's regs */
	struct i2c_msg msgs[] = {
		{
			.addr	= save_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= msgbuf,
		},
		{
			.addr	= save_client->addr,
			.flags	= I2C_M_RD,
			.len	= 8,
			.buf	= buf,
		},
	};

	sec = min = hour = day = mon = year = 0;

	do {
		if (i2c_transfer(save_client->adapter, msgs, 2) < 0)
			goto read_err;

		sec1 = sec;
		min1 = min;
		hour1 = hour;
		day1 = day;
		mon1 = mon;
		year1 = year;

		sec = buf[m41t00_chip->sec] & 0x7f;
		min = buf[m41t00_chip->min] & 0x7f;
		hour = buf[m41t00_chip->hour] & 0x3f;
		day = buf[m41t00_chip->day] & 0x3f;
		mon = buf[m41t00_chip->mon] & 0x1f;
		year = buf[m41t00_chip->year];
	} while ((++reads < m41t00_chip->read_limit) && ((sec != sec1)
			|| (min != min1) || (hour != hour1) || (day != day1)
			|| (mon != mon1) || (year != year1)));

	if ((m41t00_chip->read_limit > 1) && ((sec != sec1) || (min != min1)
			|| (hour != hour1) || (day != day1) || (mon != mon1)
			|| (year != year1)))
		goto read_err;

	sec = BCD2BIN(sec);
	min = BCD2BIN(min);
	hour = BCD2BIN(hour);
	day = BCD2BIN(day);
	mon = BCD2BIN(mon);
	year = BCD2BIN(year);

	year += 1900;
	if (year < 1970)
		year += 100;

	return mktime(year, mon, day, hour, min, sec);

read_err:
	dev_err(&save_client->dev, "m41t00_get_rtc_time: Read error\n");
	return 0;
}
EXPORT_SYMBOL_GPL(m41t00_get_rtc_time);

static void
m41t00_set(void *arg)
{
	struct rtc_time	tm;
	int nowtime = *(int *)arg;
	s32 sec, min, hour, day, mon, year;
	u8 wbuf[9], *buf = &wbuf[1], msgbuf[1] = { 0 };
	struct i2c_msg msgs[] = {
		{
			.addr	= save_client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= msgbuf,
		},
		{
			.addr	= save_client->addr,
			.flags	= I2C_M_RD,
			.len	= 8,
			.buf	= buf,
		},
	};

	to_tm(nowtime, &tm);
	tm.tm_year = (tm.tm_year - 1900) % 100;

	sec = BIN2BCD(tm.tm_sec);
	min = BIN2BCD(tm.tm_min);
	hour = BIN2BCD(tm.tm_hour);
	day = BIN2BCD(tm.tm_mday);
	mon = BIN2BCD(tm.tm_mon);
	year = BIN2BCD(tm.tm_year);

	/* Read reg values into buf[0..7]/wbuf[1..8] */
	if (i2c_transfer(save_client->adapter, msgs, 2) < 0) {
		dev_err(&save_client->dev, "m41t00_set: Read error\n");
		return;
	}

	wbuf[0] = 0; /* offset into rtc's regs */
	buf[m41t00_chip->sec] = (buf[m41t00_chip->sec] & ~0x7f) | (sec & 0x7f);
	buf[m41t00_chip->min] = (buf[m41t00_chip->min] & ~0x7f) | (min & 0x7f);
	buf[m41t00_chip->hour] = (buf[m41t00_chip->hour] & ~0x3f) | (hour& 0x3f);
	buf[m41t00_chip->day] = (buf[m41t00_chip->day] & ~0x3f) | (day & 0x3f);
	buf[m41t00_chip->mon] = (buf[m41t00_chip->mon] & ~0x1f) | (mon & 0x1f);

	if (i2c_master_send(save_client, wbuf, 9) < 0)
		dev_err(&save_client->dev, "m41t00_set: Write error\n");
}

static ulong new_time;
static struct workqueue_struct *m41t00_wq;
static DECLARE_WORK(m41t00_work, m41t00_set, &new_time);

int
m41t00_set_rtc_time(ulong nowtime)
{
	new_time = nowtime;

	if (in_interrupt())
		queue_work(m41t00_wq, &m41t00_work);
	else
		m41t00_set(&new_time);

	return 0;
}
EXPORT_SYMBOL_GPL(m41t00_set_rtc_time);

/*
 *****************************************************************************
 *
 *	platform_data Driver Interface
 *
 *****************************************************************************
 */
static int __init
m41t00_platform_probe(struct platform_device *pdev)
{
	struct m41t00_platform_data *pdata;
	int i;

	if (pdev && (pdata = pdev->dev.platform_data)) {
		normal_addr[0] = pdata->i2c_addr;

		for (i=0; i<ARRAY_SIZE(m41t00_chip_info_tbl); i++)
			if (m41t00_chip_info_tbl[i].type == pdata->type) {
				m41t00_chip = &m41t00_chip_info_tbl[i];
				m41t00_chip->sqw_freq = pdata->sqw_freq;
				return 0;
			}
	}
	return -ENODEV;
}

static int __exit
m41t00_platform_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver m41t00_platform_driver = {
	.probe  = m41t00_platform_probe,
	.remove = m41t00_platform_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name  = M41T00_DRV_NAME,
	},
};

/*
 *****************************************************************************
 *
 *	Driver Interface
 *
 *****************************************************************************
 */
static int
m41t00_probe(struct i2c_adapter *adap, int addr, int kind)
{
	struct i2c_client *client;
	int rc;

	if (!i2c_check_functionality(adap, I2C_FUNC_I2C
			| I2C_FUNC_SMBUS_BYTE_DATA))
		return 0;

	client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	strlcpy(client->name, m41t00_chip->name, I2C_NAME_SIZE);
	client->addr = addr;
	client->adapter = adap;
	client->driver = &m41t00_driver;

	if ((rc = i2c_attach_client(client)))
		goto attach_err;

	if (m41t00_chip->type != M41T00_TYPE_M41T00) {
		/* If asked, disable SQW, set SQW frequency & re-enable */
		if (m41t00_chip->sqw_freq)
			if (((rc = i2c_smbus_read_byte_data(client,
					m41t00_chip->alarm_mon)) < 0)
			 || ((rc = i2c_smbus_write_byte_data(client,
					m41t00_chip->alarm_mon, rc & ~0x40)) <0)
			 || ((rc = i2c_smbus_write_byte_data(client,
					m41t00_chip->sqw,
					m41t00_chip->sqw_freq)) < 0)
			 || ((rc = i2c_smbus_write_byte_data(client,
					m41t00_chip->alarm_mon, rc | 0x40)) <0))
				goto sqw_err;

		/* Make sure HT (Halt Update) bit is cleared */
		if ((rc = i2c_smbus_read_byte_data(client,
				m41t00_chip->alarm_hour)) < 0)
			goto ht_err;

		if (rc & 0x40)
			if ((rc = i2c_smbus_write_byte_data(client,
					m41t00_chip->alarm_hour, rc & ~0x40))<0)
				goto ht_err;
	}

	/* Make sure ST (stop) bit is cleared */
	if ((rc = i2c_smbus_read_byte_data(client, m41t00_chip->sec)) < 0)
		goto st_err;

	if (rc & 0x80)
		if ((rc = i2c_smbus_write_byte_data(client, m41t00_chip->sec,
				rc & ~0x80)) < 0)
			goto st_err;

	m41t00_wq = create_singlethread_workqueue(m41t00_chip->name);
	save_client = client;
	return 0;

st_err:
	dev_err(&client->dev, "m41t00_probe: Can't clear ST bit\n");
	goto attach_err;
ht_err:
	dev_err(&client->dev, "m41t00_probe: Can't clear HT bit\n");
	goto attach_err;
sqw_err:
	dev_err(&client->dev, "m41t00_probe: Can't set SQW Frequency\n");
attach_err:
	kfree(client);
	return rc;
}

static int
m41t00_attach(struct i2c_adapter *adap)
{
	return i2c_probe(adap, &addr_data, m41t00_probe);
}

static int
m41t00_detach(struct i2c_client *client)
{
	int rc;

	if ((rc = i2c_detach_client(client)) == 0) {
		kfree(client);
		destroy_workqueue(m41t00_wq);
	}
	return rc;
}

static struct i2c_driver m41t00_driver = {
	.driver = {
		.name	= M41T00_DRV_NAME,
	},
	.id		= I2C_DRIVERID_STM41T00,
	.attach_adapter	= m41t00_attach,
	.detach_client	= m41t00_detach,
};

static int __init
m41t00_init(void)
{
	int rc;

	if (!(rc = platform_driver_register(&m41t00_platform_driver)))
		rc = i2c_add_driver(&m41t00_driver);
	return rc;
}

static void __exit
m41t00_exit(void)
{
	i2c_del_driver(&m41t00_driver);
	platform_driver_unregister(&m41t00_platform_driver);
}

module_init(m41t00_init);
module_exit(m41t00_exit);

MODULE_AUTHOR("Mark A. Greer <mgreer@mvista.com>");
MODULE_DESCRIPTION("ST Microelectronics M41T00 RTC I2C Client Driver");
MODULE_LICENSE("GPL");
