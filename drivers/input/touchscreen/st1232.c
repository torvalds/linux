// SPDX-License-Identifier: GPL-2.0
/*
 * ST1232 Touchscreen Controller Driver
 *
 * Copyright (C) 2010 Renesas Solutions Corp.
 *	Tony SIM <chinyeow.sim.xt@renesas.com>
 *
 * Using code from:
 *  - android.git.kernel.org: projects/kernel/common.git: synaptics_i2c_rmi.c
 *	Copyright (C) 2007 Google, Inc.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/input/touchscreen.h>

#define ST1232_TS_NAME	"st1232-ts"
#define ST1633_TS_NAME	"st1633-ts"

struct st1232_ts_finger {
	u16 x;
	u16 y;
	u8 t;
	bool is_valid;
};

struct st_chip_info {
	bool	have_z;
	u16	max_x;
	u16	max_y;
	u16	max_area;
	u16	max_fingers;
	u8	start_reg;
};

struct st1232_ts_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct touchscreen_properties prop;
	struct dev_pm_qos_request low_latency_req;
	struct gpio_desc *reset_gpio;
	const struct st_chip_info *chip_info;
	int read_buf_len;
	u8 *read_buf;
	struct st1232_ts_finger *finger;
};

static int st1232_ts_read_data(struct st1232_ts_data *ts)
{
	struct st1232_ts_finger *finger = ts->finger;
	struct i2c_client *client = ts->client;
	struct i2c_msg msg[2];
	int error;
	int i, y;
	u8 start_reg = ts->chip_info->start_reg;
	u8 *buf = ts->read_buf;

	/* read touchscreen data */
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &start_reg;

	msg[1].addr = ts->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = ts->read_buf_len;
	msg[1].buf = buf;

	error = i2c_transfer(client->adapter, msg, 2);
	if (error < 0)
		return error;

	for (i = 0, y = 0; i < ts->chip_info->max_fingers; i++, y += 3) {
		finger[i].is_valid = buf[i + y] >> 7;
		if (finger[i].is_valid) {
			finger[i].x = ((buf[i + y] & 0x0070) << 4) | buf[i + 1];
			finger[i].y = ((buf[i + y] & 0x0007) << 8) | buf[i + 2];

			/* st1232 includes a z-axis / touch strength */
			if (ts->chip_info->have_z)
				finger[i].t = buf[i + 6];
		}
	}

	return 0;
}

static irqreturn_t st1232_ts_irq_handler(int irq, void *dev_id)
{
	struct st1232_ts_data *ts = dev_id;
	struct st1232_ts_finger *finger = ts->finger;
	struct input_dev *input_dev = ts->input_dev;
	int count = 0;
	int i, ret;

	ret = st1232_ts_read_data(ts);
	if (ret < 0)
		goto end;

	/* multi touch protocol */
	for (i = 0; i < ts->chip_info->max_fingers; i++) {
		if (!finger[i].is_valid)
			continue;

		if (ts->chip_info->have_z)
			input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR,
					 finger[i].t);

		touchscreen_report_pos(input_dev, &ts->prop,
					finger[i].x, finger[i].y, true);
		input_mt_sync(input_dev);
		count++;
	}

	/* SYN_MT_REPORT only if no contact */
	if (!count) {
		input_mt_sync(input_dev);
		if (ts->low_latency_req.dev) {
			dev_pm_qos_remove_request(&ts->low_latency_req);
			ts->low_latency_req.dev = NULL;
		}
	} else if (!ts->low_latency_req.dev) {
		/* First contact, request 100 us latency. */
		dev_pm_qos_add_ancestor_request(&ts->client->dev,
						&ts->low_latency_req,
						DEV_PM_QOS_RESUME_LATENCY, 100);
	}

	/* SYN_REPORT */
	input_sync(input_dev);

end:
	return IRQ_HANDLED;
}

static void st1232_ts_power(struct st1232_ts_data *ts, bool poweron)
{
	if (ts->reset_gpio)
		gpiod_set_value_cansleep(ts->reset_gpio, !poweron);
}

static const struct st_chip_info st1232_chip_info = {
	.have_z		= true,
	.max_x		= 0x31f, /* 800 - 1 */
	.max_y		= 0x1df, /* 480 -1 */
	.max_area	= 0xff,
	.max_fingers	= 2,
	.start_reg	= 0x12,
};

static const struct st_chip_info st1633_chip_info = {
	.have_z		= false,
	.max_x		= 0x13f, /* 320 - 1 */
	.max_y		= 0x1df, /* 480 -1 */
	.max_area	= 0x00,
	.max_fingers	= 5,
	.start_reg	= 0x12,
};

static int st1232_ts_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	const struct st_chip_info *match;
	struct st1232_ts_data *ts;
	struct st1232_ts_finger *finger;
	struct input_dev *input_dev;
	int error;

	match = device_get_match_data(&client->dev);
	if (!match && id)
		match = (const void *)id->driver_data;
	if (!match) {
		dev_err(&client->dev, "unknown device model\n");
		return -ENODEV;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "need I2C_FUNC_I2C\n");
		return -EIO;
	}

	if (!client->irq) {
		dev_err(&client->dev, "no IRQ?\n");
		return -EINVAL;
	}

	ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->chip_info = match;
	ts->finger = devm_kcalloc(&client->dev,
				  ts->chip_info->max_fingers, sizeof(*finger),
				  GFP_KERNEL);
	if (!ts->finger)
		return -ENOMEM;

	/* allocate a buffer according to the number of registers to read */
	ts->read_buf_len = ts->chip_info->max_fingers * 4;
	ts->read_buf = devm_kzalloc(&client->dev, ts->read_buf_len, GFP_KERNEL);
	if (!ts->read_buf)
		return -ENOMEM;

	input_dev = devm_input_allocate_device(&client->dev);
	if (!input_dev)
		return -ENOMEM;

	ts->client = client;
	ts->input_dev = input_dev;

	ts->reset_gpio = devm_gpiod_get_optional(&client->dev, NULL,
						 GPIOD_OUT_HIGH);
	if (IS_ERR(ts->reset_gpio)) {
		error = PTR_ERR(ts->reset_gpio);
		dev_err(&client->dev, "Unable to request GPIO pin: %d.\n",
			error);
		return error;
	}

	st1232_ts_power(ts, true);

	input_dev->name = "st1232-touchscreen";
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;

	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
	__set_bit(EV_SYN, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(EV_ABS, input_dev->evbit);

	if (ts->chip_info->have_z)
		input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0,
				     ts->chip_info->max_area, 0, 0);

	input_set_abs_params(input_dev, ABS_MT_POSITION_X,
			     0, ts->chip_info->max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y,
			     0, ts->chip_info->max_y, 0, 0);

	touchscreen_parse_properties(input_dev, true, &ts->prop);

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, st1232_ts_irq_handler,
					  IRQF_ONESHOT,
					  client->name, ts);
	if (error) {
		dev_err(&client->dev, "Failed to register interrupt\n");
		return error;
	}

	error = input_register_device(ts->input_dev);
	if (error) {
		dev_err(&client->dev, "Unable to register %s input device\n",
			input_dev->name);
		return error;
	}

	i2c_set_clientdata(client, ts);
	device_init_wakeup(&client->dev, 1);

	return 0;
}

static int st1232_ts_remove(struct i2c_client *client)
{
	struct st1232_ts_data *ts = i2c_get_clientdata(client);

	st1232_ts_power(ts, false);

	return 0;
}

static int __maybe_unused st1232_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct st1232_ts_data *ts = i2c_get_clientdata(client);

	if (device_may_wakeup(&client->dev)) {
		enable_irq_wake(client->irq);
	} else {
		disable_irq(client->irq);
		st1232_ts_power(ts, false);
	}

	return 0;
}

static int __maybe_unused st1232_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct st1232_ts_data *ts = i2c_get_clientdata(client);

	if (device_may_wakeup(&client->dev)) {
		disable_irq_wake(client->irq);
	} else {
		st1232_ts_power(ts, true);
		enable_irq(client->irq);
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(st1232_ts_pm_ops,
			 st1232_ts_suspend, st1232_ts_resume);

static const struct i2c_device_id st1232_ts_id[] = {
	{ ST1232_TS_NAME, (unsigned long)&st1232_chip_info },
	{ ST1633_TS_NAME, (unsigned long)&st1633_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(i2c, st1232_ts_id);

static const struct of_device_id st1232_ts_dt_ids[] = {
	{ .compatible = "sitronix,st1232", .data = &st1232_chip_info },
	{ .compatible = "sitronix,st1633", .data = &st1633_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, st1232_ts_dt_ids);

static struct i2c_driver st1232_ts_driver = {
	.probe		= st1232_ts_probe,
	.remove		= st1232_ts_remove,
	.id_table	= st1232_ts_id,
	.driver = {
		.name	= ST1232_TS_NAME,
		.of_match_table = st1232_ts_dt_ids,
		.pm	= &st1232_ts_pm_ops,
	},
};

module_i2c_driver(st1232_ts_driver);

MODULE_AUTHOR("Tony SIM <chinyeow.sim.xt@renesas.com>");
MODULE_AUTHOR("Martin Kepplinger <martin.kepplinger@ginzinger.com>");
MODULE_DESCRIPTION("SITRONIX ST1232 Touchscreen Controller Driver");
MODULE_LICENSE("GPL v2");
