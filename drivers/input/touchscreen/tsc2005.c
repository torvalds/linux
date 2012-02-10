/*
 * TSC2005 touchscreen driver
 *
 * Copyright (C) 2006-2010 Nokia Corporation
 *
 * Author: Lauri Leukkunen <lauri.leukkunen@nokia.com>
 * based on TSC2301 driver by Klaus K. Pedersen <klaus.k.pedersen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/spi/spi.h>
#include <linux/spi/tsc2005.h>

/*
 * The touchscreen interface operates as follows:
 *
 * 1) Pen is pressed against the touchscreen.
 * 2) TSC2005 performs AD conversion.
 * 3) After the conversion is done TSC2005 drives DAV line down.
 * 4) GPIO IRQ is received and tsc2005_irq_thread() is scheduled.
 * 5) tsc2005_irq_thread() queues up an spi transfer to fetch the x, y, z1, z2
 *    values.
 * 6) tsc2005_irq_thread() reports coordinates to input layer and sets up
 *    tsc2005_penup_timer() to be called after TSC2005_PENUP_TIME_MS (40ms).
 * 7) When the penup timer expires, there have not been touch or DAV interrupts
 *    during the last 40ms which means the pen has been lifted.
 *
 * ESD recovery via a hardware reset is done if the TSC2005 doesn't respond
 * after a configurable period (in ms) of activity. If esd_timeout is 0, the
 * watchdog is disabled.
 */

/* control byte 1 */
#define TSC2005_CMD			0x80
#define TSC2005_CMD_NORMAL		0x00
#define TSC2005_CMD_STOP		0x01
#define TSC2005_CMD_12BIT		0x04

/* control byte 0 */
#define TSC2005_REG_READ		0x0001
#define TSC2005_REG_PND0		0x0002
#define TSC2005_REG_X			0x0000
#define TSC2005_REG_Y			0x0008
#define TSC2005_REG_Z1			0x0010
#define TSC2005_REG_Z2			0x0018
#define TSC2005_REG_TEMP_HIGH		0x0050
#define TSC2005_REG_CFR0		0x0060
#define TSC2005_REG_CFR1		0x0068
#define TSC2005_REG_CFR2		0x0070

/* configuration register 0 */
#define TSC2005_CFR0_PRECHARGE_276US	0x0040
#define TSC2005_CFR0_STABTIME_1MS	0x0300
#define TSC2005_CFR0_CLOCK_1MHZ		0x1000
#define TSC2005_CFR0_RESOLUTION12	0x2000
#define TSC2005_CFR0_PENMODE		0x8000
#define TSC2005_CFR0_INITVALUE		(TSC2005_CFR0_STABTIME_1MS    | \
					 TSC2005_CFR0_CLOCK_1MHZ      | \
					 TSC2005_CFR0_RESOLUTION12    | \
					 TSC2005_CFR0_PRECHARGE_276US | \
					 TSC2005_CFR0_PENMODE)

/* bits common to both read and write of configuration register 0 */
#define	TSC2005_CFR0_RW_MASK		0x3fff

/* configuration register 1 */
#define TSC2005_CFR1_BATCHDELAY_4MS	0x0003
#define TSC2005_CFR1_INITVALUE		TSC2005_CFR1_BATCHDELAY_4MS

/* configuration register 2 */
#define TSC2005_CFR2_MAVE_Z		0x0004
#define TSC2005_CFR2_MAVE_Y		0x0008
#define TSC2005_CFR2_MAVE_X		0x0010
#define TSC2005_CFR2_AVG_7		0x0800
#define TSC2005_CFR2_MEDIUM_15		0x3000
#define TSC2005_CFR2_INITVALUE		(TSC2005_CFR2_MAVE_X	| \
					 TSC2005_CFR2_MAVE_Y	| \
					 TSC2005_CFR2_MAVE_Z	| \
					 TSC2005_CFR2_MEDIUM_15	| \
					 TSC2005_CFR2_AVG_7)

#define MAX_12BIT			0xfff
#define TSC2005_SPI_MAX_SPEED_HZ	10000000
#define TSC2005_PENUP_TIME_MS		40

struct tsc2005_spi_rd {
	struct spi_transfer	spi_xfer;
	u32			spi_tx;
	u32			spi_rx;
};

struct tsc2005 {
	struct spi_device	*spi;

	struct spi_message      spi_read_msg;
	struct tsc2005_spi_rd	spi_x;
	struct tsc2005_spi_rd	spi_y;
	struct tsc2005_spi_rd	spi_z1;
	struct tsc2005_spi_rd	spi_z2;

	struct input_dev	*idev;
	char			phys[32];

	struct mutex		mutex;

	/* raw copy of previous x,y,z */
	int			in_x;
	int			in_y;
	int                     in_z1;
	int			in_z2;

	spinlock_t		lock;
	struct timer_list	penup_timer;

	unsigned int		esd_timeout;
	struct delayed_work	esd_work;
	unsigned long		last_valid_interrupt;

	unsigned int		x_plate_ohm;

	bool			opened;
	bool			suspended;

	bool			pen_down;

	void			(*set_reset)(bool enable);
};

static int tsc2005_cmd(struct tsc2005 *ts, u8 cmd)
{
	u8 tx = TSC2005_CMD | TSC2005_CMD_12BIT | cmd;
	struct spi_transfer xfer = {
		.tx_buf		= &tx,
		.len		= 1,
		.bits_per_word	= 8,
	};
	struct spi_message msg;
	int error;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	error = spi_sync(ts->spi, &msg);
	if (error) {
		dev_err(&ts->spi->dev, "%s: failed, command: %x, error: %d\n",
			__func__, cmd, error);
		return error;
	}

	return 0;
}

static int tsc2005_write(struct tsc2005 *ts, u8 reg, u16 value)
{
	u32 tx = ((reg | TSC2005_REG_PND0) << 16) | value;
	struct spi_transfer xfer = {
		.tx_buf		= &tx,
		.len		= 4,
		.bits_per_word	= 24,
	};
	struct spi_message msg;
	int error;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	error = spi_sync(ts->spi, &msg);
	if (error) {
		dev_err(&ts->spi->dev,
			"%s: failed, register: %x, value: %x, error: %d\n",
			__func__, reg, value, error);
		return error;
	}

	return 0;
}

static void tsc2005_setup_read(struct tsc2005_spi_rd *rd, u8 reg, bool last)
{
	memset(rd, 0, sizeof(*rd));

	rd->spi_tx		   = (reg | TSC2005_REG_READ) << 16;
	rd->spi_xfer.tx_buf	   = &rd->spi_tx;
	rd->spi_xfer.rx_buf	   = &rd->spi_rx;
	rd->spi_xfer.len	   = 4;
	rd->spi_xfer.bits_per_word = 24;
	rd->spi_xfer.cs_change	   = !last;
}

static int tsc2005_read(struct tsc2005 *ts, u8 reg, u16 *value)
{
	struct tsc2005_spi_rd spi_rd;
	struct spi_message msg;
	int error;

	tsc2005_setup_read(&spi_rd, reg, true);

	spi_message_init(&msg);
	spi_message_add_tail(&spi_rd.spi_xfer, &msg);

	error = spi_sync(ts->spi, &msg);
	if (error)
		return error;

	*value = spi_rd.spi_rx;
	return 0;
}

static void tsc2005_update_pen_state(struct tsc2005 *ts,
				     int x, int y, int pressure)
{
	if (pressure) {
		input_report_abs(ts->idev, ABS_X, x);
		input_report_abs(ts->idev, ABS_Y, y);
		input_report_abs(ts->idev, ABS_PRESSURE, pressure);
		if (!ts->pen_down) {
			input_report_key(ts->idev, BTN_TOUCH, !!pressure);
			ts->pen_down = true;
		}
	} else {
		input_report_abs(ts->idev, ABS_PRESSURE, 0);
		if (ts->pen_down) {
			input_report_key(ts->idev, BTN_TOUCH, 0);
			ts->pen_down = false;
		}
	}
	input_sync(ts->idev);
	dev_dbg(&ts->spi->dev, "point(%4d,%4d), pressure (%4d)\n", x, y,
		pressure);
}

static irqreturn_t tsc2005_irq_thread(int irq, void *_ts)
{
	struct tsc2005 *ts = _ts;
	unsigned long flags;
	unsigned int pressure;
	u32 x, y;
	u32 z1, z2;
	int error;

	/* read the coordinates */
	error = spi_sync(ts->spi, &ts->spi_read_msg);
	if (unlikely(error))
		goto out;

	x = ts->spi_x.spi_rx;
	y = ts->spi_y.spi_rx;
	z1 = ts->spi_z1.spi_rx;
	z2 = ts->spi_z2.spi_rx;

	/* validate position */
	if (unlikely(x > MAX_12BIT || y > MAX_12BIT))
		goto out;

	/* Skip reading if the pressure components are out of range */
	if (unlikely(z1 == 0 || z2 > MAX_12BIT || z1 >= z2))
		goto out;

       /*
	* Skip point if this is a pen down with the exact same values as
	* the value before pen-up - that implies SPI fed us stale data
	*/
	if (!ts->pen_down &&
	    ts->in_x == x && ts->in_y == y &&
	    ts->in_z1 == z1 && ts->in_z2 == z2) {
		goto out;
	}

	/*
	 * At this point we are happy we have a valid and useful reading.
	 * Remember it for later comparisons. We may now begin downsampling.
	 */
	ts->in_x = x;
	ts->in_y = y;
	ts->in_z1 = z1;
	ts->in_z2 = z2;

	/* Compute touch pressure resistance using equation #1 */
	pressure = x * (z2 - z1) / z1;
	pressure = pressure * ts->x_plate_ohm / 4096;
	if (unlikely(pressure > MAX_12BIT))
		goto out;

	spin_lock_irqsave(&ts->lock, flags);

	tsc2005_update_pen_state(ts, x, y, pressure);
	mod_timer(&ts->penup_timer,
		  jiffies + msecs_to_jiffies(TSC2005_PENUP_TIME_MS));

	spin_unlock_irqrestore(&ts->lock, flags);

	ts->last_valid_interrupt = jiffies;
out:
	return IRQ_HANDLED;
}

static void tsc2005_penup_timer(unsigned long data)
{
	struct tsc2005 *ts = (struct tsc2005 *)data;
	unsigned long flags;

	spin_lock_irqsave(&ts->lock, flags);
	tsc2005_update_pen_state(ts, 0, 0, 0);
	spin_unlock_irqrestore(&ts->lock, flags);
}

static void tsc2005_start_scan(struct tsc2005 *ts)
{
	tsc2005_write(ts, TSC2005_REG_CFR0, TSC2005_CFR0_INITVALUE);
	tsc2005_write(ts, TSC2005_REG_CFR1, TSC2005_CFR1_INITVALUE);
	tsc2005_write(ts, TSC2005_REG_CFR2, TSC2005_CFR2_INITVALUE);
	tsc2005_cmd(ts, TSC2005_CMD_NORMAL);
}

static void tsc2005_stop_scan(struct tsc2005 *ts)
{
	tsc2005_cmd(ts, TSC2005_CMD_STOP);
}

/* must be called with ts->mutex held */
static void __tsc2005_disable(struct tsc2005 *ts)
{
	tsc2005_stop_scan(ts);

	disable_irq(ts->spi->irq);
	del_timer_sync(&ts->penup_timer);

	cancel_delayed_work_sync(&ts->esd_work);

	enable_irq(ts->spi->irq);
}

/* must be called with ts->mutex held */
static void __tsc2005_enable(struct tsc2005 *ts)
{
	tsc2005_start_scan(ts);

	if (ts->esd_timeout && ts->set_reset) {
		ts->last_valid_interrupt = jiffies;
		schedule_delayed_work(&ts->esd_work,
				round_jiffies_relative(
					msecs_to_jiffies(ts->esd_timeout)));
	}

}

static ssize_t tsc2005_selftest_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	struct tsc2005 *ts = spi_get_drvdata(spi);
	u16 temp_high;
	u16 temp_high_orig;
	u16 temp_high_test;
	bool success = true;
	int error;

	mutex_lock(&ts->mutex);

	/*
	 * Test TSC2005 communications via temp high register.
	 */
	__tsc2005_disable(ts);

	error = tsc2005_read(ts, TSC2005_REG_TEMP_HIGH, &temp_high_orig);
	if (error) {
		dev_warn(dev, "selftest failed: read error %d\n", error);
		success = false;
		goto out;
	}

	temp_high_test = (temp_high_orig - 1) & MAX_12BIT;

	error = tsc2005_write(ts, TSC2005_REG_TEMP_HIGH, temp_high_test);
	if (error) {
		dev_warn(dev, "selftest failed: write error %d\n", error);
		success = false;
		goto out;
	}

	error = tsc2005_read(ts, TSC2005_REG_TEMP_HIGH, &temp_high);
	if (error) {
		dev_warn(dev, "selftest failed: read error %d after write\n",
			 error);
		success = false;
		goto out;
	}

	if (temp_high != temp_high_test) {
		dev_warn(dev, "selftest failed: %d != %d\n",
			 temp_high, temp_high_test);
		success = false;
	}

	/* hardware reset */
	ts->set_reset(false);
	usleep_range(100, 500); /* only 10us required */
	ts->set_reset(true);

	if (!success)
		goto out;

	/* test that the reset really happened */
	error = tsc2005_read(ts, TSC2005_REG_TEMP_HIGH, &temp_high);
	if (error) {
		dev_warn(dev, "selftest failed: read error %d after reset\n",
			 error);
		success = false;
		goto out;
	}

	if (temp_high != temp_high_orig) {
		dev_warn(dev, "selftest failed after reset: %d != %d\n",
			 temp_high, temp_high_orig);
		success = false;
	}

out:
	__tsc2005_enable(ts);
	mutex_unlock(&ts->mutex);

	return sprintf(buf, "%d\n", success);
}

static DEVICE_ATTR(selftest, S_IRUGO, tsc2005_selftest_show, NULL);

static struct attribute *tsc2005_attrs[] = {
	&dev_attr_selftest.attr,
	NULL
};

static umode_t tsc2005_attr_is_visible(struct kobject *kobj,
				      struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct spi_device *spi = to_spi_device(dev);
	struct tsc2005 *ts = spi_get_drvdata(spi);
	umode_t mode = attr->mode;

	if (attr == &dev_attr_selftest.attr) {
		if (!ts->set_reset)
			mode = 0;
	}

	return mode;
}

static const struct attribute_group tsc2005_attr_group = {
	.is_visible	= tsc2005_attr_is_visible,
	.attrs		= tsc2005_attrs,
};

static void tsc2005_esd_work(struct work_struct *work)
{
	struct tsc2005 *ts = container_of(work, struct tsc2005, esd_work.work);
	int error;
	u16 r;

	if (!mutex_trylock(&ts->mutex)) {
		/*
		 * If the mutex is taken, it means that disable or enable is in
		 * progress. In that case just reschedule the work. If the work
		 * is not needed, it will be canceled by disable.
		 */
		goto reschedule;
	}

	if (time_is_after_jiffies(ts->last_valid_interrupt +
				  msecs_to_jiffies(ts->esd_timeout)))
		goto out;

	/* We should be able to read register without disabling interrupts. */
	error = tsc2005_read(ts, TSC2005_REG_CFR0, &r);
	if (!error &&
	    !((r ^ TSC2005_CFR0_INITVALUE) & TSC2005_CFR0_RW_MASK)) {
		goto out;
	}

	/*
	 * If we could not read our known value from configuration register 0
	 * then we should reset the controller as if from power-up and start
	 * scanning again.
	 */
	dev_info(&ts->spi->dev, "TSC2005 not responding - resetting\n");

	disable_irq(ts->spi->irq);
	del_timer_sync(&ts->penup_timer);

	tsc2005_update_pen_state(ts, 0, 0, 0);

	ts->set_reset(false);
	usleep_range(100, 500); /* only 10us required */
	ts->set_reset(true);

	enable_irq(ts->spi->irq);
	tsc2005_start_scan(ts);

out:
	mutex_unlock(&ts->mutex);
reschedule:
	/* re-arm the watchdog */
	schedule_delayed_work(&ts->esd_work,
			      round_jiffies_relative(
					msecs_to_jiffies(ts->esd_timeout)));
}

static int tsc2005_open(struct input_dev *input)
{
	struct tsc2005 *ts = input_get_drvdata(input);

	mutex_lock(&ts->mutex);

	if (!ts->suspended)
		__tsc2005_enable(ts);

	ts->opened = true;

	mutex_unlock(&ts->mutex);

	return 0;
}

static void tsc2005_close(struct input_dev *input)
{
	struct tsc2005 *ts = input_get_drvdata(input);

	mutex_lock(&ts->mutex);

	if (!ts->suspended)
		__tsc2005_disable(ts);

	ts->opened = false;

	mutex_unlock(&ts->mutex);
}

static void __devinit tsc2005_setup_spi_xfer(struct tsc2005 *ts)
{
	tsc2005_setup_read(&ts->spi_x, TSC2005_REG_X, false);
	tsc2005_setup_read(&ts->spi_y, TSC2005_REG_Y, false);
	tsc2005_setup_read(&ts->spi_z1, TSC2005_REG_Z1, false);
	tsc2005_setup_read(&ts->spi_z2, TSC2005_REG_Z2, true);

	spi_message_init(&ts->spi_read_msg);
	spi_message_add_tail(&ts->spi_x.spi_xfer, &ts->spi_read_msg);
	spi_message_add_tail(&ts->spi_y.spi_xfer, &ts->spi_read_msg);
	spi_message_add_tail(&ts->spi_z1.spi_xfer, &ts->spi_read_msg);
	spi_message_add_tail(&ts->spi_z2.spi_xfer, &ts->spi_read_msg);
}

static int __devinit tsc2005_probe(struct spi_device *spi)
{
	const struct tsc2005_platform_data *pdata = spi->dev.platform_data;
	struct tsc2005 *ts;
	struct input_dev *input_dev;
	unsigned int max_x, max_y, max_p;
	unsigned int fudge_x, fudge_y, fudge_p;
	int error;

	if (!pdata) {
		dev_dbg(&spi->dev, "no platform data\n");
		return -ENODEV;
	}

	fudge_x	= pdata->ts_x_fudge	   ? : 4;
	fudge_y	= pdata->ts_y_fudge	   ? : 8;
	fudge_p	= pdata->ts_pressure_fudge ? : 2;
	max_x	= pdata->ts_x_max	   ? : MAX_12BIT;
	max_y	= pdata->ts_y_max	   ? : MAX_12BIT;
	max_p	= pdata->ts_pressure_max   ? : MAX_12BIT;

	if (spi->irq <= 0) {
		dev_dbg(&spi->dev, "no irq\n");
		return -ENODEV;
	}

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	if (!spi->max_speed_hz)
		spi->max_speed_hz = TSC2005_SPI_MAX_SPEED_HZ;

	error = spi_setup(spi);
	if (error)
		return error;

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ts || !input_dev) {
		error = -ENOMEM;
		goto err_free_mem;
	}

	ts->spi = spi;
	ts->idev = input_dev;

	ts->x_plate_ohm	= pdata->ts_x_plate_ohm	? : 280;
	ts->esd_timeout	= pdata->esd_timeout_ms;
	ts->set_reset	= pdata->set_reset;

	mutex_init(&ts->mutex);

	spin_lock_init(&ts->lock);
	setup_timer(&ts->penup_timer, tsc2005_penup_timer, (unsigned long)ts);

	INIT_DELAYED_WORK(&ts->esd_work, tsc2005_esd_work);

	tsc2005_setup_spi_xfer(ts);

	snprintf(ts->phys, sizeof(ts->phys),
		 "%s/input-ts", dev_name(&spi->dev));

	input_dev->name = "TSC2005 touchscreen";
	input_dev->phys = ts->phys;
	input_dev->id.bustype = BUS_SPI;
	input_dev->dev.parent = &spi->dev;
	input_dev->evbit[0] = BIT(EV_ABS) | BIT(EV_KEY);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	input_set_abs_params(input_dev, ABS_X, 0, max_x, fudge_x, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, max_y, fudge_y, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, max_p, fudge_p, 0);

	input_dev->open = tsc2005_open;
	input_dev->close = tsc2005_close;

	input_set_drvdata(input_dev, ts);

	/* Ensure the touchscreen is off */
	tsc2005_stop_scan(ts);

	error = request_threaded_irq(spi->irq, NULL, tsc2005_irq_thread,
				     IRQF_TRIGGER_RISING, "tsc2005", ts);
	if (error) {
		dev_err(&spi->dev, "Failed to request irq, err: %d\n", error);
		goto err_free_mem;
	}

	spi_set_drvdata(spi, ts);
	error = sysfs_create_group(&spi->dev.kobj, &tsc2005_attr_group);
	if (error) {
		dev_err(&spi->dev,
			"Failed to create sysfs attributes, err: %d\n", error);
		goto err_clear_drvdata;
	}

	error = input_register_device(ts->idev);
	if (error) {
		dev_err(&spi->dev,
			"Failed to register input device, err: %d\n", error);
		goto err_remove_sysfs;
	}

	irq_set_irq_wake(spi->irq, 1);
	return 0;

err_remove_sysfs:
	sysfs_remove_group(&spi->dev.kobj, &tsc2005_attr_group);
err_clear_drvdata:
	spi_set_drvdata(spi, NULL);
	free_irq(spi->irq, ts);
err_free_mem:
	input_free_device(input_dev);
	kfree(ts);
	return error;
}

static int __devexit tsc2005_remove(struct spi_device *spi)
{
	struct tsc2005 *ts = spi_get_drvdata(spi);

	sysfs_remove_group(&ts->spi->dev.kobj, &tsc2005_attr_group);

	free_irq(ts->spi->irq, ts);
	input_unregister_device(ts->idev);
	kfree(ts);

	spi_set_drvdata(spi, NULL);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tsc2005_suspend(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct tsc2005 *ts = spi_get_drvdata(spi);

	mutex_lock(&ts->mutex);

	if (!ts->suspended && ts->opened)
		__tsc2005_disable(ts);

	ts->suspended = true;

	mutex_unlock(&ts->mutex);

	return 0;
}

static int tsc2005_resume(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct tsc2005 *ts = spi_get_drvdata(spi);

	mutex_lock(&ts->mutex);

	if (ts->suspended && ts->opened)
		__tsc2005_enable(ts);

	ts->suspended = false;

	mutex_unlock(&ts->mutex);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(tsc2005_pm_ops, tsc2005_suspend, tsc2005_resume);

static struct spi_driver tsc2005_driver = {
	.driver	= {
		.name	= "tsc2005",
		.owner	= THIS_MODULE,
		.pm	= &tsc2005_pm_ops,
	},
	.probe	= tsc2005_probe,
	.remove	= __devexit_p(tsc2005_remove),
};

static int __init tsc2005_init(void)
{
	return spi_register_driver(&tsc2005_driver);
}
module_init(tsc2005_init);

static void __exit tsc2005_exit(void)
{
	spi_unregister_driver(&tsc2005_driver);
}
module_exit(tsc2005_exit);

MODULE_AUTHOR("Lauri Leukkunen <lauri.leukkunen@nokia.com>");
MODULE_DESCRIPTION("TSC2005 Touchscreen Driver");
MODULE_LICENSE("GPL");
