/*
 * I2C client/driver for the ST M41T00 Real-Time Clock chip.
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
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
#include <linux/mutex.h>
#include <linux/workqueue.h>

#include <asm/time.h>
#include <asm/rtc.h>

#define	M41T00_DRV_NAME		"m41t00"

static DEFINE_MUTEX(m41t00_mutex);

static struct i2c_driver m41t00_driver;
static struct i2c_client *save_client;

static unsigned short ignore[] = { I2C_CLIENT_END };
static unsigned short normal_addr[] = { 0x68, I2C_CLIENT_END };

static struct i2c_client_address_data addr_data = {
	.normal_i2c	= normal_addr,
	.probe		= ignore,
	.ignore		= ignore,
};

ulong
m41t00_get_rtc_time(void)
{
	s32 sec, min, hour, day, mon, year;
	s32 sec1, min1, hour1, day1, mon1, year1;
	ulong limit = 10;

	sec = min = hour = day = mon = year = 0;
	sec1 = min1 = hour1 = day1 = mon1 = year1 = 0;

	mutex_lock(&m41t00_mutex);
	do {
		if (((sec = i2c_smbus_read_byte_data(save_client, 0)) >= 0)
			&& ((min = i2c_smbus_read_byte_data(save_client, 1))
				>= 0)
			&& ((hour = i2c_smbus_read_byte_data(save_client, 2))
				>= 0)
			&& ((day = i2c_smbus_read_byte_data(save_client, 4))
				>= 0)
			&& ((mon = i2c_smbus_read_byte_data(save_client, 5))
				>= 0)
			&& ((year = i2c_smbus_read_byte_data(save_client, 6))
				>= 0)
			&& ((sec == sec1) && (min == min1) && (hour == hour1)
				&& (day == day1) && (mon == mon1)
				&& (year == year1)))

				break;

		sec1 = sec;
		min1 = min;
		hour1 = hour;
		day1 = day;
		mon1 = mon;
		year1 = year;
	} while (--limit > 0);
	mutex_unlock(&m41t00_mutex);

	if (limit == 0) {
		dev_warn(&save_client->dev,
			"m41t00: can't read rtc chip\n");
		sec = min = hour = day = mon = year = 0;
	}

	sec &= 0x7f;
	min &= 0x7f;
	hour &= 0x3f;
	day &= 0x3f;
	mon &= 0x1f;
	year &= 0xff;

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
}

static void
m41t00_set(void *arg)
{
	struct rtc_time	tm;
	ulong nowtime = *(ulong *)arg;

	to_tm(nowtime, &tm);
	tm.tm_year = (tm.tm_year - 1900) % 100;

	tm.tm_sec = BIN2BCD(tm.tm_sec);
	tm.tm_min = BIN2BCD(tm.tm_min);
	tm.tm_hour = BIN2BCD(tm.tm_hour);
	tm.tm_mon = BIN2BCD(tm.tm_mon);
	tm.tm_mday = BIN2BCD(tm.tm_mday);
	tm.tm_year = BIN2BCD(tm.tm_year);

	mutex_lock(&m41t00_mutex);
	if ((i2c_smbus_write_byte_data(save_client, 0, tm.tm_sec & 0x7f) < 0)
		|| (i2c_smbus_write_byte_data(save_client, 1, tm.tm_min & 0x7f)
			< 0)
		|| (i2c_smbus_write_byte_data(save_client, 2, tm.tm_hour & 0x3f)
			< 0)
		|| (i2c_smbus_write_byte_data(save_client, 4, tm.tm_mday & 0x3f)
			< 0)
		|| (i2c_smbus_write_byte_data(save_client, 5, tm.tm_mon & 0x1f)
			< 0)
		|| (i2c_smbus_write_byte_data(save_client, 6, tm.tm_year & 0xff)
			< 0))

		dev_warn(&save_client->dev,"m41t00: can't write to rtc chip\n");

	mutex_unlock(&m41t00_mutex);
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

	client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	strlcpy(client->name, M41T00_DRV_NAME, I2C_NAME_SIZE);
	client->addr = addr;
	client->adapter = adap;
	client->driver = &m41t00_driver;

	if ((rc = i2c_attach_client(client)) != 0) {
		kfree(client);
		return rc;
	}

	m41t00_wq = create_singlethread_workqueue("m41t00");
	save_client = client;
	return 0;
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
	return i2c_add_driver(&m41t00_driver);
}

static void __exit
m41t00_exit(void)
{
	i2c_del_driver(&m41t00_driver);
}

module_init(m41t00_init);
module_exit(m41t00_exit);

MODULE_AUTHOR("Mark A. Greer <mgreer@mvista.com>");
MODULE_DESCRIPTION("ST Microelectronics M41T00 RTC I2C Client Driver");
MODULE_LICENSE("GPL");
