/*
 * RTC client/driver for the Maxim/Dallas DS1374 Real-Time Clock over I2C
 *
 * Based on code by Randy Vinson <rvinson@mvista.com>,
 * which was based on the m41t00.c by Mark Greer <mgreer@mvista.com>.
 *
 * Copyright (C) 2006-2007 Freescale Semiconductor
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
/*
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
#include <linux/workqueue.h>

#define DS1374_REG_TOD0		0x00 /* Time of Day */
#define DS1374_REG_TOD1		0x01
#define DS1374_REG_TOD2		0x02
#define DS1374_REG_TOD3		0x03
#define DS1374_REG_WDALM0	0x04 /* Watchdog/Alarm */
#define DS1374_REG_WDALM1	0x05
#define DS1374_REG_WDALM2	0x06
#define DS1374_REG_CR		0x07 /* Control */
#define DS1374_REG_CR_AIE	0x01 /* Alarm Int. Enable */
#define DS1374_REG_CR_WDALM	0x20 /* 1=Watchdog, 0=Alarm */
#define DS1374_REG_CR_WACE	0x40 /* WD/Alarm counter enable */
#define DS1374_REG_SR		0x08 /* Status */
#define DS1374_REG_SR_OSF	0x80 /* Oscillator Stop Flag */
#define DS1374_REG_SR_AF	0x01 /* Alarm Flag */
#define DS1374_REG_TCR		0x09 /* Trickle Charge */

static const struct i2c_device_id ds1374_id[] = {
	{ "ds1374", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ds1374_id);

struct ds1374 {
	struct i2c_client *client;
	struct rtc_device *rtc;
	struct work_struct work;

	/* The mutex protects alarm operations, and prevents a race
	 * between the enable_irq() in the workqueue and the free_irq()
	 * in the remove function.
	 */
	struct mutex mutex;
	int exiting;
};

static struct i2c_driver ds1374_driver;

static int ds1374_read_rtc(struct i2c_client *client, u32 *time,
                           int reg, int nbytes)
{
	u8 buf[4];
	int ret;
	int i;

	if (nbytes > 4) {
		WARN_ON(1);
		return -EINVAL;
	}

	ret = i2c_smbus_read_i2c_block_data(client, reg, nbytes, buf);

	if (ret < 0)
		return ret;
	if (ret < nbytes)
		return -EIO;

	for (i = nbytes - 1, *time = 0; i >= 0; i--)
		*time = (*time << 8) | buf[i];

	return 0;
}

static int ds1374_write_rtc(struct i2c_client *client, u32 time,
                            int reg, int nbytes)
{
	u8 buf[4];
	int i;

	if (nbytes > 4) {
		WARN_ON(1);
		return -EINVAL;
	}

	for (i = 0; i < nbytes; i++) {
		buf[i] = time & 0xff;
		time >>= 8;
	}

	return i2c_smbus_write_i2c_block_data(client, reg, nbytes, buf);
}

static int ds1374_check_rtc_status(struct i2c_client *client)
{
	int ret = 0;
	int control, stat;

	stat = i2c_smbus_read_byte_data(client, DS1374_REG_SR);
	if (stat < 0)
		return stat;

	if (stat & DS1374_REG_SR_OSF)
		dev_warn(&client->dev,
		         "oscillator discontinuity flagged, "
		         "time unreliable\n");

	stat &= ~(DS1374_REG_SR_OSF | DS1374_REG_SR_AF);

	ret = i2c_smbus_write_byte_data(client, DS1374_REG_SR, stat);
	if (ret < 0)
		return ret;

	/* If the alarm is pending, clear it before requesting
	 * the interrupt, so an interrupt event isn't reported
	 * before everything is initialized.
	 */

	control = i2c_smbus_read_byte_data(client, DS1374_REG_CR);
	if (control < 0)
		return control;

	control &= ~(DS1374_REG_CR_WACE | DS1374_REG_CR_AIE);
	return i2c_smbus_write_byte_data(client, DS1374_REG_CR, control);
}

static int ds1374_read_time(struct device *dev, struct rtc_time *time)
{
	struct i2c_client *client = to_i2c_client(dev);
	u32 itime;
	int ret;

	ret = ds1374_read_rtc(client, &itime, DS1374_REG_TOD0, 4);
	if (!ret)
		rtc_time_to_tm(itime, time);

	return ret;
}

static int ds1374_set_time(struct device *dev, struct rtc_time *time)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long itime;

	rtc_tm_to_time(time, &itime);
	return ds1374_write_rtc(client, itime, DS1374_REG_TOD0, 4);
}

/* The ds1374 has a decrementer for an alarm, rather than a comparator.
 * If the time of day is changed, then the alarm will need to be
 * reset.
 */
static int ds1374_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ds1374 *ds1374 = i2c_get_clientdata(client);
	u32 now, cur_alarm;
	int cr, sr;
	int ret = 0;

	if (client->irq <= 0)
		return -EINVAL;

	mutex_lock(&ds1374->mutex);

	cr = ret = i2c_smbus_read_byte_data(client, DS1374_REG_CR);
	if (ret < 0)
		goto out;

	sr = ret = i2c_smbus_read_byte_data(client, DS1374_REG_SR);
	if (ret < 0)
		goto out;

	ret = ds1374_read_rtc(client, &now, DS1374_REG_TOD0, 4);
	if (ret)
		goto out;

	ret = ds1374_read_rtc(client, &cur_alarm, DS1374_REG_WDALM0, 3);
	if (ret)
		goto out;

	rtc_time_to_tm(now + cur_alarm, &alarm->time);
	alarm->enabled = !!(cr & DS1374_REG_CR_WACE);
	alarm->pending = !!(sr & DS1374_REG_SR_AF);

out:
	mutex_unlock(&ds1374->mutex);
	return ret;
}

static int ds1374_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ds1374 *ds1374 = i2c_get_clientdata(client);
	struct rtc_time now;
	unsigned long new_alarm, itime;
	int cr;
	int ret = 0;

	if (client->irq <= 0)
		return -EINVAL;

	ret = ds1374_read_time(dev, &now);
	if (ret < 0)
		return ret;

	rtc_tm_to_time(&alarm->time, &new_alarm);
	rtc_tm_to_time(&now, &itime);

	new_alarm -= itime;

	/* This can happen due to races, in addition to dates that are
	 * truly in the past.  To avoid requiring the caller to check for
	 * races, dates in the past are assumed to be in the recent past
	 * (i.e. not something that we'd rather the caller know about via
	 * an error), and the alarm is set to go off as soon as possible.
	 */
	if (new_alarm <= 0)
		new_alarm = 1;

	mutex_lock(&ds1374->mutex);

	ret = cr = i2c_smbus_read_byte_data(client, DS1374_REG_CR);
	if (ret < 0)
		goto out;

	/* Disable any existing alarm before setting the new one
	 * (or lack thereof). */
	cr &= ~DS1374_REG_CR_WACE;

	ret = i2c_smbus_write_byte_data(client, DS1374_REG_CR, cr);
	if (ret < 0)
		goto out;

	ret = ds1374_write_rtc(client, new_alarm, DS1374_REG_WDALM0, 3);
	if (ret)
		goto out;

	if (alarm->enabled) {
		cr |= DS1374_REG_CR_WACE | DS1374_REG_CR_AIE;
		cr &= ~DS1374_REG_CR_WDALM;

		ret = i2c_smbus_write_byte_data(client, DS1374_REG_CR, cr);
	}

out:
	mutex_unlock(&ds1374->mutex);
	return ret;
}

static irqreturn_t ds1374_irq(int irq, void *dev_id)
{
	struct i2c_client *client = dev_id;
	struct ds1374 *ds1374 = i2c_get_clientdata(client);

	disable_irq_nosync(irq);
	schedule_work(&ds1374->work);
	return IRQ_HANDLED;
}

static void ds1374_work(struct work_struct *work)
{
	struct ds1374 *ds1374 = container_of(work, struct ds1374, work);
	struct i2c_client *client = ds1374->client;
	int stat, control;

	mutex_lock(&ds1374->mutex);

	stat = i2c_smbus_read_byte_data(client, DS1374_REG_SR);
	if (stat < 0)
		return;

	if (stat & DS1374_REG_SR_AF) {
		stat &= ~DS1374_REG_SR_AF;
		i2c_smbus_write_byte_data(client, DS1374_REG_SR, stat);

		control = i2c_smbus_read_byte_data(client, DS1374_REG_CR);
		if (control < 0)
			goto out;

		control &= ~(DS1374_REG_CR_WACE | DS1374_REG_CR_AIE);
		i2c_smbus_write_byte_data(client, DS1374_REG_CR, control);

		/* rtc_update_irq() assumes that it is called
		 * from IRQ-disabled context.
		 */
		local_irq_disable();
		rtc_update_irq(ds1374->rtc, 1, RTC_AF | RTC_IRQF);
		local_irq_enable();
	}

out:
	if (!ds1374->exiting)
		enable_irq(client->irq);

	mutex_unlock(&ds1374->mutex);
}

static int ds1374_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ds1374 *ds1374 = i2c_get_clientdata(client);
	int ret = -ENOIOCTLCMD;

	mutex_lock(&ds1374->mutex);

	switch (cmd) {
	case RTC_AIE_OFF:
		ret = i2c_smbus_read_byte_data(client, DS1374_REG_CR);
		if (ret < 0)
			goto out;

		ret &= ~DS1374_REG_CR_WACE;

		ret = i2c_smbus_write_byte_data(client, DS1374_REG_CR, ret);
		if (ret < 0)
			goto out;

		break;

	case RTC_AIE_ON:
		ret = i2c_smbus_read_byte_data(client, DS1374_REG_CR);
		if (ret < 0)
			goto out;

		ret |= DS1374_REG_CR_WACE | DS1374_REG_CR_AIE;
		ret &= ~DS1374_REG_CR_WDALM;

		ret = i2c_smbus_write_byte_data(client, DS1374_REG_CR, ret);
		if (ret < 0)
			goto out;

		break;
	}

out:
	mutex_unlock(&ds1374->mutex);
	return ret;
}

static const struct rtc_class_ops ds1374_rtc_ops = {
	.read_time = ds1374_read_time,
	.set_time = ds1374_set_time,
	.read_alarm = ds1374_read_alarm,
	.set_alarm = ds1374_set_alarm,
	.ioctl = ds1374_ioctl,
};

static int ds1374_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct ds1374 *ds1374;
	int ret;

	ds1374 = kzalloc(sizeof(struct ds1374), GFP_KERNEL);
	if (!ds1374)
		return -ENOMEM;

	ds1374->client = client;
	i2c_set_clientdata(client, ds1374);

	INIT_WORK(&ds1374->work, ds1374_work);
	mutex_init(&ds1374->mutex);

	ret = ds1374_check_rtc_status(client);
	if (ret)
		goto out_free;

	if (client->irq > 0) {
		ret = request_irq(client->irq, ds1374_irq, 0,
		                  "ds1374", client);
		if (ret) {
			dev_err(&client->dev, "unable to request IRQ\n");
			goto out_free;
		}
	}

	ds1374->rtc = rtc_device_register(client->name, &client->dev,
	                                  &ds1374_rtc_ops, THIS_MODULE);
	if (IS_ERR(ds1374->rtc)) {
		ret = PTR_ERR(ds1374->rtc);
		dev_err(&client->dev, "unable to register the class device\n");
		goto out_irq;
	}

	return 0;

out_irq:
	if (client->irq > 0)
		free_irq(client->irq, client);

out_free:
	i2c_set_clientdata(client, NULL);
	kfree(ds1374);
	return ret;
}

static int __devexit ds1374_remove(struct i2c_client *client)
{
	struct ds1374 *ds1374 = i2c_get_clientdata(client);

	if (client->irq > 0) {
		mutex_lock(&ds1374->mutex);
		ds1374->exiting = 1;
		mutex_unlock(&ds1374->mutex);

		free_irq(client->irq, client);
		flush_scheduled_work();
	}

	rtc_device_unregister(ds1374->rtc);
	i2c_set_clientdata(client, NULL);
	kfree(ds1374);
	return 0;
}

#ifdef CONFIG_PM
static int ds1374_suspend(struct i2c_client *client, pm_message_t state)
{
	if (client->irq >= 0 && device_may_wakeup(&client->dev))
		enable_irq_wake(client->irq);
	return 0;
}

static int ds1374_resume(struct i2c_client *client)
{
	if (client->irq >= 0 && device_may_wakeup(&client->dev))
		disable_irq_wake(client->irq);
	return 0;
}
#else
#define ds1374_suspend	NULL
#define ds1374_resume	NULL
#endif

static struct i2c_driver ds1374_driver = {
	.driver = {
		.name = "rtc-ds1374",
		.owner = THIS_MODULE,
	},
	.probe = ds1374_probe,
	.suspend = ds1374_suspend,
	.resume = ds1374_resume,
	.remove = __devexit_p(ds1374_remove),
	.id_table = ds1374_id,
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

MODULE_AUTHOR("Scott Wood <scottwood@freescale.com>");
MODULE_DESCRIPTION("Maxim/Dallas DS1374 RTC Driver");
MODULE_LICENSE("GPL");
