/*
 * drivers/i2c/chips/ds1374.c
 *
 * I2C client/driver for the Maxim/Dallas DS1374 Real-Time Clock
 *
 * Author: Randy Vinson <rvinson@mvista.com>
 *
 * Based on the m41t00.c by Mark Greer <mgreer@mvista.com>
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
/*
 * This i2c client/driver wedges between the drivers/char/genrtc.c RTC
 * interface and the SMBus interface of the i2c subsystem.
 * It would be more efficient to use i2c msgs/i2c_transfer directly but, as
 * recommened in .../Documentation/i2c/writing-clients section
 * "Sending and receiving", using SMBus level communication is preferred.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#define DS1374_REG_TOD0		0x00
#define DS1374_REG_TOD1		0x01
#define DS1374_REG_TOD2		0x02
#define DS1374_REG_TOD3		0x03
#define DS1374_REG_WDALM0	0x04
#define DS1374_REG_WDALM1	0x05
#define DS1374_REG_WDALM2	0x06
#define DS1374_REG_CR		0x07
#define DS1374_REG_SR		0x08
#define DS1374_REG_SR_OSF	0x80
#define DS1374_REG_TCR		0x09

#define	DS1374_DRV_NAME		"ds1374"

static DEFINE_MUTEX(ds1374_mutex);

static struct i2c_driver ds1374_driver;
static struct i2c_client *save_client;

static unsigned short ignore[] = { I2C_CLIENT_END };
static unsigned short normal_addr[] = { 0x68, I2C_CLIENT_END };

static struct i2c_client_address_data addr_data = {
	.normal_i2c = normal_addr,
	.probe = ignore,
	.ignore = ignore,
};

static ulong ds1374_read_rtc(void)
{
	ulong time = 0;
	int reg = DS1374_REG_WDALM0;

	while (reg--) {
		s32 tmp;
		if ((tmp = i2c_smbus_read_byte_data(save_client, reg)) < 0) {
			dev_warn(&save_client->dev,
				 "can't read from rtc chip\n");
			return 0;
		}
		time = (time << 8) | (tmp & 0xff);
	}
	return time;
}

static void ds1374_write_rtc(ulong time)
{
	int reg;

	for (reg = DS1374_REG_TOD0; reg < DS1374_REG_WDALM0; reg++) {
		if (i2c_smbus_write_byte_data(save_client, reg, time & 0xff)
		    < 0) {
			dev_warn(&save_client->dev,
				 "can't write to rtc chip\n");
			break;
		}
		time = time >> 8;
	}
}

static void ds1374_check_rtc_status(void)
{
	s32 tmp;

	tmp = i2c_smbus_read_byte_data(save_client, DS1374_REG_SR);
	if (tmp < 0) {
		dev_warn(&save_client->dev,
			 "can't read status from rtc chip\n");
		return;
	}
	if (tmp & DS1374_REG_SR_OSF) {
		dev_warn(&save_client->dev,
			 "oscillator discontinuity flagged, time unreliable\n");
		tmp &= ~DS1374_REG_SR_OSF;
		tmp = i2c_smbus_write_byte_data(save_client, DS1374_REG_SR,
						tmp & 0xff);
		if (tmp < 0)
			dev_warn(&save_client->dev,
				 "can't clear discontinuity notification\n");
	}
}

ulong ds1374_get_rtc_time(void)
{
	ulong t1, t2;
	int limit = 10;		/* arbitrary retry limit */

	mutex_lock(&ds1374_mutex);

	/*
	 * Since the reads are being performed one byte at a time using
	 * the SMBus vs a 4-byte i2c transfer, there is a chance that a
	 * carry will occur during the read. To detect this, 2 reads are
	 * performed and compared.
	 */
	do {
		t1 = ds1374_read_rtc();
		t2 = ds1374_read_rtc();
	} while (t1 != t2 && limit--);

	mutex_unlock(&ds1374_mutex);

	if (t1 != t2) {
		dev_warn(&save_client->dev,
			 "can't get consistent time from rtc chip\n");
		t1 = 0;
	}

	return t1;
}

static ulong new_time;

static void ds1374_set_work(struct work_struct *work)
{
	ulong t1, t2;
	int limit = 10;		/* arbitrary retry limit */

	t1 = new_time;

	mutex_lock(&ds1374_mutex);

	/*
	 * Since the writes are being performed one byte at a time using
	 * the SMBus vs a 4-byte i2c transfer, there is a chance that a
	 * carry will occur during the write. To detect this, the write
	 * value is read back and compared.
	 */
	do {
		ds1374_write_rtc(t1);
		t2 = ds1374_read_rtc();
	} while (t1 != t2 && limit--);

	mutex_unlock(&ds1374_mutex);

	if (t1 != t2)
		dev_warn(&save_client->dev,
			 "can't confirm time set from rtc chip\n");
}

static struct workqueue_struct *ds1374_workqueue;

static DECLARE_WORK(ds1374_work, ds1374_set_work);

int ds1374_set_rtc_time(ulong nowtime)
{
	new_time = nowtime;

	if (in_interrupt())
		queue_work(ds1374_workqueue, &ds1374_work);
	else
		ds1374_set_work(NULL);

	return 0;
}

/*
 *****************************************************************************
 *
 *	Driver Interface
 *
 *****************************************************************************
 */
static int ds1374_probe(struct i2c_adapter *adap, int addr, int kind)
{
	struct i2c_client *client;
	int rc;

	client = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	strncpy(client->name, DS1374_DRV_NAME, I2C_NAME_SIZE);
	client->addr = addr;
	client->adapter = adap;
	client->driver = &ds1374_driver;

	ds1374_workqueue = create_singlethread_workqueue("ds1374");
	if (!ds1374_workqueue) {
		kfree(client);
		return -ENOMEM;	/* most expected reason */
	}

	if ((rc = i2c_attach_client(client)) != 0) {
		kfree(client);
		return rc;
	}

	save_client = client;

	ds1374_check_rtc_status();

	return 0;
}

static int ds1374_attach(struct i2c_adapter *adap)
{
	return i2c_probe(adap, &addr_data, ds1374_probe);
}

static int ds1374_detach(struct i2c_client *client)
{
	int rc;

	if ((rc = i2c_detach_client(client)) == 0) {
		kfree(i2c_get_clientdata(client));
		destroy_workqueue(ds1374_workqueue);
	}
	return rc;
}

static struct i2c_driver ds1374_driver = {
	.driver = {
		.name	= DS1374_DRV_NAME,
	},
	.id = I2C_DRIVERID_DS1374,
	.attach_adapter = ds1374_attach,
	.detach_client = ds1374_detach,
};

static int __init ds1374_init(void)
{
	return i2c_add_driver(&ds1374_driver);
}

static void __exit ds1374_exit(void)
{
	i2c_del_driver(&ds1374_driver);
}

module_init(ds1374_init);
module_exit(ds1374_exit);

MODULE_AUTHOR("Randy Vinson <rvinson@mvista.com>");
MODULE_DESCRIPTION("Maxim/Dallas DS1374 RTC I2C Client Driver");
MODULE_LICENSE("GPL");
