// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/input/touchscreen/tsc2007.c
 *
 * Copyright (c) 2008 MtekVision Co., Ltd.
 *	Kwangwoo Lee <kwlee@mtekvision.com>
 *
 * Using code from:
 *  - ads7846.c
 *	Copyright (c) 2005 David Brownell
 *	Copyright (c) 2006 Nokia Corporation
 *  - corgi_ts.c
 *	Copyright (C) 2004-2005 Richard Purdie
 *  - omap_ts.[hc], ads7846.h, ts_osk.c
 *	Copyright (C) 2002 MontaVista Software
 *	Copyright (C) 2004 Texas Instruments
 *	Copyright (C) 2005 Dirk Behme
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/math64.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/platform_data/tsc2007.h>
#include "tsc2007.h"

int tsc2007_xfer(struct tsc2007 *tsc, u8 cmd)
{
	s32 data;
	u16 val;

	data = i2c_smbus_read_word_data(tsc->client, cmd);
	if (data < 0) {
		dev_err(&tsc->client->dev, "i2c io error: %d\n", data);
		return data;
	}

	/* The protocol and raw data format from i2c interface:
	 * S Addr Wr [A] Comm [A] S Addr Rd [A] [DataLow] A [DataHigh] NA P
	 * Where DataLow has [D11-D4], DataHigh has [D3-D0 << 4 | Dummy 4bit].
	 */
	val = swab16(data) >> 4;

	dev_dbg(&tsc->client->dev, "data: 0x%x, val: 0x%x\n", data, val);

	return val;
}

static void tsc2007_read_values(struct tsc2007 *tsc, struct ts_event *tc)
{
	/* y- still on; turn on only y+ (and ADC) */
	tc->y = tsc2007_xfer(tsc, READ_Y);

	/* turn y- off, x+ on, then leave in lowpower */
	tc->x = tsc2007_xfer(tsc, READ_X);

	/* turn y+ off, x- on; we'll use formula #1 */
	tc->z1 = tsc2007_xfer(tsc, READ_Z1);
	tc->z2 = tsc2007_xfer(tsc, READ_Z2);

	/* Prepare for next touch reading - power down ADC, enable PENIRQ */
	tsc2007_xfer(tsc, PWRDOWN);
}

u32 tsc2007_calculate_resistance(struct tsc2007 *tsc, struct ts_event *tc)
{
	u64 rt = 0;

	/* range filtering */
	if (tc->x == MAX_12BIT)
		tc->x = 0;

	if (likely(tc->x && tc->z1)) {
		/* compute touch resistance using equation #1 */
		rt = tc->z2 - tc->z1;
		rt *= tc->x;
		rt *= tsc->x_plate_ohms;
		rt = div_u64(rt, tc->z1);
		rt = (rt + 2047) >> 12;
	}

	if (rt > U32_MAX)
		return U32_MAX;
	return (u32) rt;
}

bool tsc2007_is_pen_down(struct tsc2007 *ts)
{
	/*
	 * NOTE: We can't rely on the pressure to determine the pen down
	 * state, even though this controller has a pressure sensor.
	 * The pressure value can fluctuate for quite a while after
	 * lifting the pen and in some cases may not even settle at the
	 * expected value.
	 *
	 * The only safe way to check for the pen up condition is in the
	 * work function by reading the pen signal state (it's a GPIO
	 * and IRQ). Unfortunately such callback is not always available,
	 * in that case we assume that the pen is down and expect caller
	 * to fall back on the pressure reading.
	 */

	if (!ts->get_pendown_state)
		return true;

	return ts->get_pendown_state(&ts->client->dev);
}

static irqreturn_t tsc2007_soft_irq(int irq, void *handle)
{
	struct tsc2007 *ts = handle;
	struct input_dev *input = ts->input;
	struct ts_event tc;
	u32 rt;

	while (!ts->stopped && tsc2007_is_pen_down(ts)) {

		/* pen is down, continue with the measurement */

		mutex_lock(&ts->mlock);
		tsc2007_read_values(ts, &tc);
		mutex_unlock(&ts->mlock);

		rt = tsc2007_calculate_resistance(ts, &tc);

		if (!rt && !ts->get_pendown_state) {
			/*
			 * If pressure reported is 0 and we don't have
			 * callback to check pendown state, we have to
			 * assume that pen was lifted up.
			 */
			break;
		}

		if (rt <= ts->max_rt) {
			dev_dbg(&ts->client->dev,
				"DOWN point(%4d,%4d), resistance (%4u)\n",
				tc.x, tc.y, rt);

			rt = ts->max_rt - rt;

			input_report_key(input, BTN_TOUCH, 1);
			touchscreen_report_pos(input, &ts->prop, tc.x, tc.y, false);
			input_report_abs(input, ABS_PRESSURE, rt);

			input_sync(input);

		} else {
			/*
			 * Sample found inconsistent by debouncing or pressure is
			 * beyond the maximum. Don't report it to user space,
			 * repeat at least once more the measurement.
			 */
			dev_dbg(&ts->client->dev, "ignored pressure %d\n", rt);
		}

		wait_event_timeout(ts->wait, ts->stopped, ts->poll_period);
	}

	dev_dbg(&ts->client->dev, "UP\n");

	input_report_key(input, BTN_TOUCH, 0);
	input_report_abs(input, ABS_PRESSURE, 0);
	input_sync(input);

	if (ts->clear_penirq)
		ts->clear_penirq();

	return IRQ_HANDLED;
}

static void tsc2007_stop(struct tsc2007 *ts)
{
	ts->stopped = true;
	mb();
	wake_up(&ts->wait);

	if (ts->irq)
		disable_irq(ts->irq);
}

static int tsc2007_open(struct input_dev *input_dev)
{
	struct tsc2007 *ts = input_get_drvdata(input_dev);
	int err;

	ts->stopped = false;
	mb();

	if (ts->irq)
		enable_irq(ts->irq);

	/* Prepare for touch readings - power down ADC and enable PENIRQ */
	err = tsc2007_xfer(ts, PWRDOWN);
	if (err < 0) {
		tsc2007_stop(ts);
		return err;
	}

	return 0;
}

static void tsc2007_close(struct input_dev *input_dev)
{
	struct tsc2007 *ts = input_get_drvdata(input_dev);

	tsc2007_stop(ts);
}

static int tsc2007_get_pendown_state_gpio(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct tsc2007 *ts = i2c_get_clientdata(client);

	return gpiod_get_value_cansleep(ts->gpiod);
}

static int tsc2007_probe_properties(struct device *dev, struct tsc2007 *ts)
{
	u32 val32;
	u64 val64;

	if (!device_property_read_u32(dev, "ti,max-rt", &val32))
		ts->max_rt = val32;
	else
		ts->max_rt = MAX_12BIT;

	if (!device_property_read_u32(dev, "ti,fuzzx", &val32))
		ts->fuzzx = val32;

	if (!device_property_read_u32(dev, "ti,fuzzy", &val32))
		ts->fuzzy = val32;

	if (!device_property_read_u32(dev, "ti,fuzzz", &val32))
		ts->fuzzz = val32;

	if (!device_property_read_u64(dev, "ti,poll-period", &val64))
		ts->poll_period = msecs_to_jiffies(val64);
	else
		ts->poll_period = msecs_to_jiffies(1);

	if (!device_property_read_u32(dev, "ti,x-plate-ohms", &val32)) {
		ts->x_plate_ohms = val32;
	} else {
		dev_err(dev, "Missing ti,x-plate-ohms device property\n");
		return -EINVAL;
	}

	ts->gpiod = devm_gpiod_get_optional(dev, NULL, GPIOD_IN);
	if (IS_ERR(ts->gpiod))
		return PTR_ERR(ts->gpiod);

	if (ts->gpiod)
		ts->get_pendown_state = tsc2007_get_pendown_state_gpio;
	else
		dev_dbg(dev, "Pen down GPIO is not specified in properties\n");

	return 0;
}

static int tsc2007_probe_pdev(struct device *dev, struct tsc2007 *ts,
			      const struct tsc2007_platform_data *pdata,
			      const struct i2c_device_id *id)
{
	ts->model             = pdata->model;
	ts->x_plate_ohms      = pdata->x_plate_ohms;
	ts->max_rt            = pdata->max_rt ? : MAX_12BIT;
	ts->poll_period       = msecs_to_jiffies(pdata->poll_period ? : 1);
	ts->get_pendown_state = pdata->get_pendown_state;
	ts->clear_penirq      = pdata->clear_penirq;
	ts->fuzzx             = pdata->fuzzx;
	ts->fuzzy             = pdata->fuzzy;
	ts->fuzzz             = pdata->fuzzz;

	if (pdata->x_plate_ohms == 0) {
		dev_err(dev, "x_plate_ohms is not set up in platform data\n");
		return -EINVAL;
	}

	return 0;
}

static void tsc2007_call_exit_platform_hw(void *data)
{
	struct device *dev = data;
	const struct tsc2007_platform_data *pdata = dev_get_platdata(dev);

	pdata->exit_platform_hw();
}

static int tsc2007_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	const struct tsc2007_platform_data *pdata =
		dev_get_platdata(&client->dev);
	struct tsc2007 *ts;
	struct input_dev *input_dev;
	int err;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -EIO;

	ts = devm_kzalloc(&client->dev, sizeof(struct tsc2007), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	if (pdata)
		err = tsc2007_probe_pdev(&client->dev, ts, pdata, id);
	else
		err = tsc2007_probe_properties(&client->dev, ts);
	if (err)
		return err;

	input_dev = devm_input_allocate_device(&client->dev);
	if (!input_dev)
		return -ENOMEM;

	i2c_set_clientdata(client, ts);

	ts->client = client;
	ts->irq = client->irq;
	ts->input = input_dev;

	init_waitqueue_head(&ts->wait);
	mutex_init(&ts->mlock);

	snprintf(ts->phys, sizeof(ts->phys),
		 "%s/input0", dev_name(&client->dev));

	input_dev->name = "TSC2007 Touchscreen";
	input_dev->phys = ts->phys;
	input_dev->id.bustype = BUS_I2C;

	input_dev->open = tsc2007_open;
	input_dev->close = tsc2007_close;

	input_set_drvdata(input_dev, ts);

	input_set_capability(input_dev, EV_KEY, BTN_TOUCH);
	input_set_abs_params(input_dev, ABS_X, 0, MAX_12BIT, ts->fuzzx, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, MAX_12BIT, ts->fuzzy, 0);
	touchscreen_parse_properties(input_dev, false, &ts->prop);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, MAX_12BIT,
			     ts->fuzzz, 0);

	if (pdata) {
		if (pdata->exit_platform_hw) {
			err = devm_add_action(&client->dev,
					      tsc2007_call_exit_platform_hw,
					      &client->dev);
			if (err) {
				dev_err(&client->dev,
					"Failed to register exit_platform_hw action, %d\n",
					err);
				return err;
			}
		}

		if (pdata->init_platform_hw)
			pdata->init_platform_hw();
	}

	if (ts->irq) {
		err = devm_request_threaded_irq(&client->dev, ts->irq,
						NULL, tsc2007_soft_irq,
						IRQF_ONESHOT,
						client->dev.driver->name, ts);
		if (err) {
			dev_err(&client->dev, "Failed to request irq %d: %d\n",
				ts->irq, err);
			return err;
		}

		tsc2007_stop(ts);
	}

	/* power down the chip (TSC2007_SETUP does not ACK on I2C) */
	err = tsc2007_xfer(ts, PWRDOWN);
	if (err < 0) {
		dev_err(&client->dev,
			"Failed to setup chip: %d\n", err);
		return err;	/* chip does not respond */
	}

	err = input_register_device(input_dev);
	if (err) {
		dev_err(&client->dev,
			"Failed to register input device: %d\n", err);
		return err;
	}

	err =  tsc2007_iio_configure(ts);
	if (err) {
		dev_err(&client->dev,
			"Failed to register with IIO: %d\n", err);
		return err;
	}

	return 0;
}

static const struct i2c_device_id tsc2007_idtable[] = {
	{ "tsc2007" },
	{ }
};

MODULE_DEVICE_TABLE(i2c, tsc2007_idtable);

static const struct of_device_id tsc2007_of_match[] = {
	{ .compatible = "ti,tsc2007" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tsc2007_of_match);

static struct i2c_driver tsc2007_driver = {
	.driver = {
		.name	= "tsc2007",
		.of_match_table = tsc2007_of_match,
	},
	.id_table	= tsc2007_idtable,
	.probe		= tsc2007_probe,
};

module_i2c_driver(tsc2007_driver);

MODULE_AUTHOR("Kwangwoo Lee <kwlee@mtekvision.com>");
MODULE_DESCRIPTION("TSC2007 TouchScreen Driver");
MODULE_LICENSE("GPL");
