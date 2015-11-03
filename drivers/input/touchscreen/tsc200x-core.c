/*
 * TSC2004/TSC2005 touchscreen driver core
 *
 * Copyright (C) 2006-2010 Nokia Corporation
 * Copyright (C) 2015 QWERTY Embedded Design
 * Copyright (C) 2015 EMAC Inc.
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
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/of.h>
#include <linux/spi/tsc2005.h>
#include <linux/regulator/consumer.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>
#include "tsc200x-core.h"

/*
 * The touchscreen interface operates as follows:
 *
 * 1) Pen is pressed against the touchscreen.
 * 2) TSC2005 performs AD conversion.
 * 3) After the conversion is done TSC2005 drives DAV line down.
 * 4) GPIO IRQ is received and tsc2005_irq_thread() is scheduled.
 * 5) tsc2005_irq_thread() queues up a transfer to fetch the x, y, z1, z2
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

static const struct regmap_range tsc2005_writable_ranges[] = {
	regmap_reg_range(TSC2005_REG_AUX_HIGH, TSC2005_REG_CFR2),
};

static const struct regmap_access_table tsc2005_writable_table = {
	.yes_ranges = tsc2005_writable_ranges,
	.n_yes_ranges = ARRAY_SIZE(tsc2005_writable_ranges),
};

const struct regmap_config tsc200x_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.reg_stride = 0x08,
	.max_register = 0x78,
	.read_flag_mask = TSC2005_REG_READ,
	.write_flag_mask = TSC2005_REG_PND0,
	.wr_table = &tsc2005_writable_table,
	.use_single_rw = true,
};
EXPORT_SYMBOL_GPL(tsc200x_regmap_config);

struct tsc2005_data {
	u16 x;
	u16 y;
	u16 z1;
	u16 z2;
} __packed;
#define TSC2005_DATA_REGS 4

struct tsc2005 {
	struct device           *dev;
	struct regmap		*regmap;
	__u16                   bustype;

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

	struct regulator	*vio;

	struct gpio_desc	*reset_gpio;
	void			(*set_reset)(bool enable);
	int			(*tsc200x_cmd)(struct device *dev, u8 cmd);
	int			irq;
};

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
	dev_dbg(ts->dev, "point(%4d,%4d), pressure (%4d)\n", x, y,
		pressure);
}

static irqreturn_t tsc2005_irq_thread(int irq, void *_ts)
{
	struct tsc2005 *ts = _ts;
	unsigned long flags;
	unsigned int pressure;
	struct tsc2005_data tsdata;
	int error;

	/* read the coordinates */
	error = regmap_bulk_read(ts->regmap, TSC2005_REG_X, &tsdata,
				 TSC2005_DATA_REGS);
	if (unlikely(error))
		goto out;

	/* validate position */
	if (unlikely(tsdata.x > MAX_12BIT || tsdata.y > MAX_12BIT))
		goto out;

	/* Skip reading if the pressure components are out of range */
	if (unlikely(tsdata.z1 == 0 || tsdata.z2 > MAX_12BIT))
		goto out;
	if (unlikely(tsdata.z1 >= tsdata.z2))
		goto out;

       /*
	* Skip point if this is a pen down with the exact same values as
	* the value before pen-up - that implies SPI fed us stale data
	*/
	if (!ts->pen_down &&
	    ts->in_x == tsdata.x && ts->in_y == tsdata.y &&
	    ts->in_z1 == tsdata.z1 && ts->in_z2 == tsdata.z2) {
		goto out;
	}

	/*
	 * At this point we are happy we have a valid and useful reading.
	 * Remember it for later comparisons. We may now begin downsampling.
	 */
	ts->in_x = tsdata.x;
	ts->in_y = tsdata.y;
	ts->in_z1 = tsdata.z1;
	ts->in_z2 = tsdata.z2;

	/* Compute touch pressure resistance using equation #1 */
	pressure = tsdata.x * (tsdata.z2 - tsdata.z1) / tsdata.z1;
	pressure = pressure * ts->x_plate_ohm / 4096;
	if (unlikely(pressure > MAX_12BIT))
		goto out;

	spin_lock_irqsave(&ts->lock, flags);

	tsc2005_update_pen_state(ts, tsdata.x, tsdata.y, pressure);
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
	regmap_write(ts->regmap, TSC2005_REG_CFR0, TSC2005_CFR0_INITVALUE);
	regmap_write(ts->regmap, TSC2005_REG_CFR1, TSC2005_CFR1_INITVALUE);
	regmap_write(ts->regmap, TSC2005_REG_CFR2, TSC2005_CFR2_INITVALUE);
	ts->tsc200x_cmd(ts->dev, TSC2005_CMD_NORMAL);
}

static void tsc2005_stop_scan(struct tsc2005 *ts)
{
	ts->tsc200x_cmd(ts->dev, TSC2005_CMD_STOP);
}

static void tsc2005_set_reset(struct tsc2005 *ts, bool enable)
{
	if (ts->reset_gpio)
		gpiod_set_value_cansleep(ts->reset_gpio, enable);
	else if (ts->set_reset)
		ts->set_reset(enable);
}

/* must be called with ts->mutex held */
static void __tsc2005_disable(struct tsc2005 *ts)
{
	tsc2005_stop_scan(ts);

	disable_irq(ts->irq);
	del_timer_sync(&ts->penup_timer);

	cancel_delayed_work_sync(&ts->esd_work);

	enable_irq(ts->irq);
}

/* must be called with ts->mutex held */
static void __tsc2005_enable(struct tsc2005 *ts)
{
	tsc2005_start_scan(ts);

	if (ts->esd_timeout && (ts->set_reset || ts->reset_gpio)) {
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
	struct tsc2005 *ts = dev_get_drvdata(dev);
	unsigned int temp_high;
	unsigned int temp_high_orig;
	unsigned int temp_high_test;
	bool success = true;
	int error;

	mutex_lock(&ts->mutex);

	/*
	 * Test TSC2005 communications via temp high register.
	 */
	__tsc2005_disable(ts);

	error = regmap_read(ts->regmap, TSC2005_REG_TEMP_HIGH, &temp_high_orig);
	if (error) {
		dev_warn(dev, "selftest failed: read error %d\n", error);
		success = false;
		goto out;
	}

	temp_high_test = (temp_high_orig - 1) & MAX_12BIT;

	error = regmap_write(ts->regmap, TSC2005_REG_TEMP_HIGH, temp_high_test);
	if (error) {
		dev_warn(dev, "selftest failed: write error %d\n", error);
		success = false;
		goto out;
	}

	error = regmap_read(ts->regmap, TSC2005_REG_TEMP_HIGH, &temp_high);
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
	tsc2005_set_reset(ts, false);
	usleep_range(100, 500); /* only 10us required */
	tsc2005_set_reset(ts, true);

	if (!success)
		goto out;

	/* test that the reset really happened */
	error = regmap_read(ts->regmap, TSC2005_REG_TEMP_HIGH, &temp_high);
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
	struct tsc2005 *ts = dev_get_drvdata(dev);
	umode_t mode = attr->mode;

	if (attr == &dev_attr_selftest.attr) {
		if (!ts->set_reset && !ts->reset_gpio)
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
	unsigned int r;

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
	error = regmap_read(ts->regmap, TSC2005_REG_CFR0, &r);
	if (!error &&
	    !((r ^ TSC2005_CFR0_INITVALUE) & TSC2005_CFR0_RW_MASK)) {
		goto out;
	}

	/*
	 * If we could not read our known value from configuration register 0
	 * then we should reset the controller as if from power-up and start
	 * scanning again.
	 */
	dev_info(ts->dev, "TSC2005 not responding - resetting\n");

	disable_irq(ts->irq);
	del_timer_sync(&ts->penup_timer);

	tsc2005_update_pen_state(ts, 0, 0, 0);

	tsc2005_set_reset(ts, false);
	usleep_range(100, 500); /* only 10us required */
	tsc2005_set_reset(ts, true);

	enable_irq(ts->irq);
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

int tsc200x_probe(struct device *dev, int irq, __u16 bustype,
		  struct regmap *regmap,
		  int (*tsc200x_cmd)(struct device *dev, u8 cmd))
{
	const struct tsc2005_platform_data *pdata = dev_get_platdata(dev);
	struct device_node *np = dev->of_node;

	struct tsc2005 *ts;
	struct input_dev *input_dev;
	unsigned int max_x = MAX_12BIT;
	unsigned int max_y = MAX_12BIT;
	unsigned int max_p = MAX_12BIT;
	unsigned int fudge_x = TSC2005_DEF_X_FUZZ;
	unsigned int fudge_y = TSC2005_DEF_Y_FUZZ;
	unsigned int fudge_p = TSC2005_DEF_P_FUZZ;
	unsigned int x_plate_ohm = TSC2005_DEF_RESISTOR;
	unsigned int esd_timeout;
	int error;

	if (!np && !pdata) {
		dev_err(dev, "no platform data\n");
		return -ENODEV;
	}

	if (irq <= 0) {
		dev_err(dev, "no irq\n");
		return -ENODEV;
	}

	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	if (!tsc200x_cmd) {
		dev_err(dev, "no cmd function\n");
		return -ENODEV;
	}

	if (pdata) {
		fudge_x	= pdata->ts_x_fudge;
		fudge_y	= pdata->ts_y_fudge;
		fudge_p	= pdata->ts_pressure_fudge;
		max_x	= pdata->ts_x_max;
		max_y	= pdata->ts_y_max;
		max_p	= pdata->ts_pressure_max;
		x_plate_ohm = pdata->ts_x_plate_ohm;
		esd_timeout = pdata->esd_timeout_ms;
	} else {
		x_plate_ohm = TSC2005_DEF_RESISTOR;
		of_property_read_u32(np, "ti,x-plate-ohms", &x_plate_ohm);
		esd_timeout = 0;
		of_property_read_u32(np, "ti,esd-recovery-timeout-ms",
								&esd_timeout);
	}

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	input_dev = devm_input_allocate_device(dev);
	if (!input_dev)
		return -ENOMEM;

	ts->irq = irq;
	ts->dev = dev;
	ts->idev = input_dev;
	ts->regmap = regmap;
	ts->tsc200x_cmd = tsc200x_cmd;
	ts->x_plate_ohm = x_plate_ohm;
	ts->esd_timeout = esd_timeout;

	ts->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ts->reset_gpio)) {
		error = PTR_ERR(ts->reset_gpio);
		dev_err(dev, "error acquiring reset gpio: %d\n", error);
		return error;
	}

	ts->vio = devm_regulator_get_optional(dev, "vio");
	if (IS_ERR(ts->vio)) {
		error = PTR_ERR(ts->vio);
		dev_err(dev, "vio regulator missing (%d)", error);
		return error;
	}

	if (!ts->reset_gpio && pdata)
		ts->set_reset = pdata->set_reset;

	mutex_init(&ts->mutex);

	spin_lock_init(&ts->lock);
	setup_timer(&ts->penup_timer, tsc2005_penup_timer, (unsigned long)ts);

	INIT_DELAYED_WORK(&ts->esd_work, tsc2005_esd_work);

	snprintf(ts->phys, sizeof(ts->phys),
		 "%s/input-ts", dev_name(dev));

	input_dev->name = "TSC2005 touchscreen";
	input_dev->phys = ts->phys;
	input_dev->id.bustype = bustype;
	input_dev->dev.parent = dev;
	input_dev->evbit[0] = BIT(EV_ABS) | BIT(EV_KEY);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	input_set_abs_params(input_dev, ABS_X, 0, max_x, fudge_x, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, max_y, fudge_y, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, max_p, fudge_p, 0);

	if (np)
		touchscreen_parse_properties(input_dev, false);

	input_dev->open = tsc2005_open;
	input_dev->close = tsc2005_close;

	input_set_drvdata(input_dev, ts);

	/* Ensure the touchscreen is off */
	tsc2005_stop_scan(ts);

	error = devm_request_threaded_irq(dev, irq, NULL,
					  tsc2005_irq_thread,
					  IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					  "tsc2005", ts);
	if (error) {
		dev_err(dev, "Failed to request irq, err: %d\n", error);
		return error;
	}

	/* enable regulator for DT */
	if (ts->vio) {
		error = regulator_enable(ts->vio);
		if (error)
			return error;
	}

	dev_set_drvdata(dev, ts);
	error = sysfs_create_group(&dev->kobj, &tsc2005_attr_group);
	if (error) {
		dev_err(dev,
			"Failed to create sysfs attributes, err: %d\n", error);
		goto disable_regulator;
	}

	error = input_register_device(ts->idev);
	if (error) {
		dev_err(dev,
			"Failed to register input device, err: %d\n", error);
		goto err_remove_sysfs;
	}

	irq_set_irq_wake(irq, 1);
	return 0;

err_remove_sysfs:
	sysfs_remove_group(&dev->kobj, &tsc2005_attr_group);
disable_regulator:
	if (ts->vio)
		regulator_disable(ts->vio);
	return error;
}
EXPORT_SYMBOL_GPL(tsc200x_probe);

int tsc200x_remove(struct device *dev)
{
	struct tsc2005 *ts = dev_get_drvdata(dev);

	sysfs_remove_group(&dev->kobj, &tsc2005_attr_group);

	if (ts->vio)
		regulator_disable(ts->vio);

	return 0;
}
EXPORT_SYMBOL_GPL(tsc200x_remove);

static int __maybe_unused tsc2005_suspend(struct device *dev)
{
	struct tsc2005 *ts = dev_get_drvdata(dev);

	mutex_lock(&ts->mutex);

	if (!ts->suspended && ts->opened)
		__tsc2005_disable(ts);

	ts->suspended = true;

	mutex_unlock(&ts->mutex);

	return 0;
}

static int __maybe_unused tsc2005_resume(struct device *dev)
{
	struct tsc2005 *ts = dev_get_drvdata(dev);

	mutex_lock(&ts->mutex);

	if (ts->suspended && ts->opened)
		__tsc2005_enable(ts);

	ts->suspended = false;

	mutex_unlock(&ts->mutex);

	return 0;
}

SIMPLE_DEV_PM_OPS(tsc200x_pm_ops, tsc2005_suspend, tsc2005_resume);
EXPORT_SYMBOL_GPL(tsc200x_pm_ops);

MODULE_AUTHOR("Lauri Leukkunen <lauri.leukkunen@nokia.com>");
MODULE_DESCRIPTION("TSC2005 Touchscreen Driver");
MODULE_LICENSE("GPL");
