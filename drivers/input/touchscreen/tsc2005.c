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

	struct timer_list	penup_timer;
	struct work_struct	penup_work;

	unsigned int		esd_timeout;
	struct timer_list	esd_timer;
	struct work_struct	esd_work;

	unsigned int		x_plate_ohm;

	bool			disabled;
	unsigned int		disable_depth;
	unsigned int		pen_down;

	void			(*set_reset)(bool enable);
};

static void tsc2005_cmd(struct tsc2005 *ts, u8 cmd)
{
	u8 tx;
	struct spi_message msg;
	struct spi_transfer xfer = { 0 };

	tx = TSC2005_CMD | TSC2005_CMD_12BIT | cmd;

	xfer.tx_buf = &tx;
	xfer.rx_buf = NULL;
	xfer.len = 1;
	xfer.bits_per_word = 8;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	spi_sync(ts->spi, &msg);
}

static void tsc2005_write(struct tsc2005 *ts, u8 reg, u16 value)
{
	u32 tx;
	struct spi_message msg;
	struct spi_transfer xfer = { 0 };

	tx = (reg | TSC2005_REG_PND0) << 16;
	tx |= value;

	xfer.tx_buf = &tx;
	xfer.rx_buf = NULL;
	xfer.len = 4;
	xfer.bits_per_word = 24;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	spi_sync(ts->spi, &msg);
}

static void tsc2005_setup_read(struct tsc2005_spi_rd *rd, u8 reg, bool last)
{
	rd->spi_tx		   = (reg | TSC2005_REG_READ) << 16;
	rd->spi_xfer.tx_buf	   = &rd->spi_tx;
	rd->spi_xfer.rx_buf	   = &rd->spi_rx;
	rd->spi_xfer.len	   = 4;
	rd->spi_xfer.bits_per_word = 24;
	rd->spi_xfer.cs_change	   = !last;
}

static void tsc2005_read(struct tsc2005 *ts, u8 reg, u16 *value)
{
	struct spi_message msg;
	struct tsc2005_spi_rd spi_rd = { { 0 }, 0, 0 };

	tsc2005_setup_read(&spi_rd, reg, 1);

	spi_message_init(&msg);
	spi_message_add_tail(&spi_rd.spi_xfer, &msg);
	spi_sync(ts->spi, &msg);
	*value = spi_rd.spi_rx;
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
			ts->pen_down = 1;
		}
	} else {
		input_report_abs(ts->idev, ABS_PRESSURE, 0);
		if (ts->pen_down) {
			input_report_key(ts->idev, BTN_TOUCH, 0);
			ts->pen_down = 0;
		}
	}
	input_sync(ts->idev);
	dev_dbg(&ts->spi->dev, "point(%4d,%4d), pressure (%4d)\n", x, y,
		pressure);
}

static irqreturn_t tsc2005_irq_handler(int irq, void *dev_id)
{
	struct tsc2005 *ts = dev_id;

	/* update the penup timer only if it's pending */
	mod_timer_pending(&ts->penup_timer,
			  jiffies + msecs_to_jiffies(TSC2005_PENUP_TIME_MS));

	return IRQ_WAKE_THREAD;
}

static irqreturn_t tsc2005_irq_thread(int irq, void *_ts)
{
	struct tsc2005 *ts = _ts;
	unsigned int pressure;
	u32 x;
	u32 y;
	u32 z1;
	u32 z2;

	mutex_lock(&ts->mutex);

	if (unlikely(ts->disable_depth))
		goto out;

	/* read the coordinates */
	spi_sync(ts->spi, &ts->spi_read_msg);
	x = ts->spi_x.spi_rx;
	y = ts->spi_y.spi_rx;
	z1 = ts->spi_z1.spi_rx;
	z2 = ts->spi_z2.spi_rx;

	/* validate position */
	if (unlikely(x > MAX_12BIT || y > MAX_12BIT))
		goto out;

	/* skip coords if the pressure components are out of range */
	if (unlikely(z1 == 0 || z2 > MAX_12BIT || z1 >= z2))
		goto out;

       /* skip point if this is a pen down with the exact same values as
	* the value before pen-up - that implies SPI fed us stale data
	*/
	if (!ts->pen_down &&
	ts->in_x == x &&
	ts->in_y == y &&
	ts->in_z1 == z1 &&
	ts->in_z2 == z2)
		goto out;

	/* At this point we are happy we have a valid and useful reading.
	* Remember it for later comparisons. We may now begin downsampling
	*/
	ts->in_x = x;
	ts->in_y = y;
	ts->in_z1 = z1;
	ts->in_z2 = z2;

	/* compute touch pressure resistance using equation #1 */
	pressure = x * (z2 - z1) / z1;
	pressure = pressure * ts->x_plate_ohm / 4096;
	if (unlikely(pressure > MAX_12BIT))
		goto out;

	tsc2005_update_pen_state(ts, x, y, pressure);

	/* set the penup timer */
	mod_timer(&ts->penup_timer,
		  jiffies + msecs_to_jiffies(TSC2005_PENUP_TIME_MS));

	if (!ts->esd_timeout)
		goto out;

	/* update the watchdog timer */
	mod_timer(&ts->esd_timer,
		  round_jiffies(jiffies + msecs_to_jiffies(ts->esd_timeout)));

out:
	mutex_unlock(&ts->mutex);
	return IRQ_HANDLED;
}

static void tsc2005_penup_timer(unsigned long data)
{
	struct tsc2005 *ts = (struct tsc2005 *)data;

	schedule_work(&ts->penup_work);
}

static void tsc2005_penup_work(struct work_struct *work)
{
	struct tsc2005 *ts = container_of(work, struct tsc2005, penup_work);

	mutex_lock(&ts->mutex);
	tsc2005_update_pen_state(ts, 0, 0, 0);
	mutex_unlock(&ts->mutex);
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

/* must be called with mutex held */
static void tsc2005_disable(struct tsc2005 *ts)
{
	if (ts->disable_depth++ != 0)
		return;
	disable_irq(ts->spi->irq);
	if (ts->esd_timeout)
		del_timer_sync(&ts->esd_timer);
	del_timer_sync(&ts->penup_timer);
	tsc2005_stop_scan(ts);
}

/* must be called with mutex held */
static void tsc2005_enable(struct tsc2005 *ts)
{
	if (--ts->disable_depth != 0)
		return;
	tsc2005_start_scan(ts);
	enable_irq(ts->spi->irq);
	if (!ts->esd_timeout)
		return;
	mod_timer(&ts->esd_timer,
		  round_jiffies(jiffies + msecs_to_jiffies(ts->esd_timeout)));
}

static ssize_t tsc2005_disable_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	struct tsc2005 *ts = spi_get_drvdata(spi);

	return sprintf(buf, "%u\n", ts->disabled);
}

static ssize_t tsc2005_disable_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct spi_device *spi = to_spi_device(dev);
	struct tsc2005 *ts = spi_get_drvdata(spi);
	unsigned long res;
	int i;

	if (strict_strtoul(buf, 10, &res) < 0)
		return -EINVAL;
	i = res ? 1 : 0;

	mutex_lock(&ts->mutex);
	if (i == ts->disabled)
		goto out;
	ts->disabled = i;
	if (i)
		tsc2005_disable(ts);
	else
		tsc2005_enable(ts);
out:
	mutex_unlock(&ts->mutex);
	return count;
}
static DEVICE_ATTR(disable, 0664, tsc2005_disable_show, tsc2005_disable_store);

static ssize_t tsc2005_selftest_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	struct tsc2005 *ts = spi_get_drvdata(spi);
	u16 temp_high;
	u16 temp_high_orig;
	u16 temp_high_test;
	unsigned int result;

	if (!ts->set_reset) {
		dev_warn(&ts->spi->dev,
			 "unable to selftest: no reset function\n");
		result = 0;
		goto out;
	}

	mutex_lock(&ts->mutex);

	/*
	 * Test TSC2005 communications via temp high register.
	 */
	tsc2005_disable(ts);
	result = 1;
	tsc2005_read(ts, TSC2005_REG_TEMP_HIGH, &temp_high_orig);
	temp_high_test = (temp_high_orig - 1) & MAX_12BIT;
	tsc2005_write(ts, TSC2005_REG_TEMP_HIGH, temp_high_test);
	tsc2005_read(ts, TSC2005_REG_TEMP_HIGH, &temp_high);
	if (temp_high != temp_high_test) {
		dev_warn(dev, "selftest failed: %d != %d\n",
			 temp_high, temp_high_test);
		result = 0;
	}

	/* hardware reset */
	ts->set_reset(0);
	usleep_range(100, 500); /* only 10us required */
	ts->set_reset(1);
	tsc2005_enable(ts);

	/* test that the reset really happened */
	tsc2005_read(ts, TSC2005_REG_TEMP_HIGH, &temp_high);
	if (temp_high != temp_high_orig) {
		dev_warn(dev, "selftest failed after reset: %d != %d\n",
			 temp_high, temp_high_orig);
		result = 0;
	}

	mutex_unlock(&ts->mutex);

out:
	return sprintf(buf, "%u\n", result);
}
static DEVICE_ATTR(selftest, S_IRUGO, tsc2005_selftest_show, NULL);

static void tsc2005_esd_timer(unsigned long data)
{
	struct tsc2005 *ts = (struct tsc2005 *)data;

	schedule_work(&ts->esd_work);
}

static void tsc2005_esd_work(struct work_struct *work)
{
	struct tsc2005 *ts = container_of(work, struct tsc2005, esd_work);
	u16 r;

	mutex_lock(&ts->mutex);

	if (ts->disable_depth)
		goto out;

	/*
	 * If we cannot read our known value from configuration register 0 then
	 * reset the controller as if from power-up and start scanning again.
	 */
	tsc2005_read(ts, TSC2005_REG_CFR0, &r);
	if ((r ^ TSC2005_CFR0_INITVALUE) & TSC2005_CFR0_RW_MASK) {
		dev_info(&ts->spi->dev, "TSC2005 not responding - resetting\n");
		ts->set_reset(0);
		tsc2005_update_pen_state(ts, 0, 0, 0);
		usleep_range(100, 500); /* only 10us required */
		ts->set_reset(1);
		tsc2005_start_scan(ts);
	}

	/* re-arm the watchdog */
	mod_timer(&ts->esd_timer,
		  round_jiffies(jiffies + msecs_to_jiffies(ts->esd_timeout)));

out:
	mutex_unlock(&ts->mutex);
}

static void __devinit tsc2005_setup_spi_xfer(struct tsc2005 *ts)
{
	tsc2005_setup_read(&ts->spi_x, TSC2005_REG_X, 0);
	tsc2005_setup_read(&ts->spi_y, TSC2005_REG_Y, 0);
	tsc2005_setup_read(&ts->spi_z1, TSC2005_REG_Z1, 0);
	tsc2005_setup_read(&ts->spi_z2, TSC2005_REG_Z2, 1);

	spi_message_init(&ts->spi_read_msg);
	spi_message_add_tail(&ts->spi_x.spi_xfer, &ts->spi_read_msg);
	spi_message_add_tail(&ts->spi_y.spi_xfer, &ts->spi_read_msg);
	spi_message_add_tail(&ts->spi_z1.spi_xfer, &ts->spi_read_msg);
	spi_message_add_tail(&ts->spi_z2.spi_xfer, &ts->spi_read_msg);
}

static struct attribute *tsc2005_attrs[] = {
	&dev_attr_disable.attr,
	&dev_attr_selftest.attr,
	NULL
};

static struct attribute_group tsc2005_attr_group = {
	.attrs = tsc2005_attrs,
};

static int __devinit tsc2005_setup(struct tsc2005 *ts,
				   struct tsc2005_platform_data *pdata)
{
	int r;
	int fudge_x;
	int fudge_y;
	int fudge_p;
	int p_max;
	int x_max;
	int y_max;

	mutex_init(&ts->mutex);

	tsc2005_setup_spi_xfer(ts);

	init_timer(&ts->penup_timer);
	setup_timer(&ts->penup_timer, tsc2005_penup_timer, (unsigned long)ts);
	INIT_WORK(&ts->penup_work, tsc2005_penup_work);

	fudge_x		= pdata->ts_x_fudge	   ? : 4;
	fudge_y		= pdata->ts_y_fudge	   ? : 8;
	fudge_p		= pdata->ts_pressure_fudge ? : 2;
	x_max		= pdata->ts_x_max	   ? : MAX_12BIT;
	y_max		= pdata->ts_y_max	   ? : MAX_12BIT;
	p_max		= pdata->ts_pressure_max   ? : MAX_12BIT;
	ts->x_plate_ohm	= pdata->ts_x_plate_ohm	   ? : 280;
	ts->esd_timeout	= pdata->esd_timeout_ms;
	ts->set_reset	= pdata->set_reset;

	ts->idev = input_allocate_device();
	if (ts->idev == NULL)
		return -ENOMEM;
	ts->idev->name = "TSC2005 touchscreen";
	snprintf(ts->phys, sizeof(ts->phys), "%s/input-ts",
		 dev_name(&ts->spi->dev));
	ts->idev->phys = ts->phys;
	ts->idev->evbit[0] = BIT(EV_ABS) | BIT(EV_KEY);
	ts->idev->absbit[0] = BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE);
	ts->idev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	input_set_abs_params(ts->idev, ABS_X, 0, x_max, fudge_x, 0);
	input_set_abs_params(ts->idev, ABS_Y, 0, y_max, fudge_y, 0);
	input_set_abs_params(ts->idev, ABS_PRESSURE, 0, p_max, fudge_p, 0);

	r = request_threaded_irq(ts->spi->irq, tsc2005_irq_handler,
				 tsc2005_irq_thread, IRQF_TRIGGER_RISING,
				 "tsc2005", ts);
	if (r) {
		dev_err(&ts->spi->dev, "request_threaded_irq(): %d\n", r);
		goto err1;
	}
	set_irq_wake(ts->spi->irq, 1);

	r = input_register_device(ts->idev);
	if (r) {
		dev_err(&ts->spi->dev, "input_register_device(): %d\n", r);
		goto err2;
	}

	r = sysfs_create_group(&ts->spi->dev.kobj, &tsc2005_attr_group);
	if (r)
		dev_warn(&ts->spi->dev, "sysfs entry creation failed: %d\n", r);

	tsc2005_start_scan(ts);

	if (!ts->esd_timeout || !ts->set_reset)
		goto done;

	/* start the optional ESD watchdog */
	setup_timer(&ts->esd_timer, tsc2005_esd_timer, (unsigned long)ts);
	INIT_WORK(&ts->esd_work, tsc2005_esd_work);
	mod_timer(&ts->esd_timer,
		  round_jiffies(jiffies + msecs_to_jiffies(ts->esd_timeout)));

done:
	return 0;

err2:
	free_irq(ts->spi->irq, ts);

err1:
	input_free_device(ts->idev);
	return r;
}

static int __devinit tsc2005_probe(struct spi_device *spi)
{
	struct tsc2005_platform_data *pdata = spi->dev.platform_data;
	struct tsc2005 *ts;
	int r;

	if (spi->irq < 0) {
		dev_dbg(&spi->dev, "no irq\n");
		return -ENODEV;
	}

	if (!pdata) {
		dev_dbg(&spi->dev, "no platform data\n");
		return -ENODEV;
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL)
		return -ENOMEM;

	spi_set_drvdata(spi, ts);
	ts->spi = spi;
	spi->dev.power.power_state = PMSG_ON;
	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	if (!spi->max_speed_hz)
		spi->max_speed_hz = TSC2005_SPI_MAX_SPEED_HZ;
	spi_setup(spi);

	r = tsc2005_setup(ts, pdata);
	if (r)
		kfree(ts);
	return r;
}

static int __devexit tsc2005_remove(struct spi_device *spi)
{
	struct tsc2005 *ts = spi_get_drvdata(spi);

	mutex_lock(&ts->mutex);
	tsc2005_disable(ts);
	mutex_unlock(&ts->mutex);

	if (ts->esd_timeout)
		del_timer_sync(&ts->esd_timer);
	del_timer_sync(&ts->penup_timer);

	flush_work(&ts->esd_work);
	flush_work(&ts->penup_work);

	sysfs_remove_group(&ts->spi->dev.kobj, &tsc2005_attr_group);
	free_irq(ts->spi->irq, ts);
	input_unregister_device(ts->idev);
	kfree(ts);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tsc2005_suspend(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct tsc2005 *ts = spi_get_drvdata(spi);

	mutex_lock(&ts->mutex);
	tsc2005_disable(ts);
	mutex_unlock(&ts->mutex);

	return 0;
}

static int tsc2005_resume(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct tsc2005 *ts = spi_get_drvdata(spi);

	mutex_lock(&ts->mutex);
	tsc2005_enable(ts);
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
	printk(KERN_INFO "TSC2005 driver initializing\n");
	return spi_register_driver(&tsc2005_driver);
}
module_init(tsc2005_init);

static void __exit tsc2005_exit(void)
{
	spi_unregister_driver(&tsc2005_driver);
}
module_exit(tsc2005_exit);

MODULE_AUTHOR("Lauri Leukkunen <lauri.leukkunen@nokia.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:tsc2005");
