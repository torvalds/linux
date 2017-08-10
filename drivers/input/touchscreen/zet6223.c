/*
 * Copyright (C) 2016, Jelle van der Waa <jelle@vdwaa.nl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <asm/unaligned.h>

#define ZET6223_MAX_FINGERS		16
#define ZET6223_MAX_PKT_SIZE		(3 + 4 * ZET6223_MAX_FINGERS)

#define ZET6223_CMD_INFO		0xB2
#define ZET6223_CMD_INFO_LENGTH		17
#define ZET6223_VALID_PACKET		0x3c

#define ZET6223_POWER_ON_DELAY_MSEC	30

struct zet6223_ts {
	struct i2c_client *client;
	struct input_dev *input;
	struct regulator *vcc;
	struct regulator *vio;
	struct touchscreen_properties prop;
	struct regulator_bulk_data supplies[2];
	u16 max_x;
	u16 max_y;
	u8 fingernum;
};

static int zet6223_start(struct input_dev *dev)
{
	struct zet6223_ts *ts = input_get_drvdata(dev);

	enable_irq(ts->client->irq);

	return 0;
}

static void zet6223_stop(struct input_dev *dev)
{
	struct zet6223_ts *ts = input_get_drvdata(dev);

	disable_irq(ts->client->irq);
}

static irqreturn_t zet6223_irq(int irq, void *dev_id)
{
	struct zet6223_ts *ts = dev_id;
	u16 finger_bits;

	/*
	 * First 3 bytes are an identifier, two bytes of finger data.
	 * X, Y data per finger is 4 bytes.
	 */
	u8 bufsize = 3 + 4 * ts->fingernum;
	u8 buf[ZET6223_MAX_PKT_SIZE];
	int i;
	int ret;
	int error;

	ret = i2c_master_recv(ts->client, buf, bufsize);
	if (ret != bufsize) {
		error = ret < 0 ? ret : -EIO;
		dev_err_ratelimited(&ts->client->dev,
				    "Error reading input data: %d\n", error);
		return IRQ_HANDLED;
	}

	if (buf[0] != ZET6223_VALID_PACKET)
		return IRQ_HANDLED;

	finger_bits = get_unaligned_be16(buf + 1);
	for (i = 0; i < ts->fingernum; i++) {
		if (!(finger_bits & BIT(15 - i)))
			continue;

		input_mt_slot(ts->input, i);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, true);
		input_event(ts->input, EV_ABS, ABS_MT_POSITION_X,
				((buf[i + 3] >> 4) << 8) + buf[i + 4]);
		input_event(ts->input, EV_ABS, ABS_MT_POSITION_Y,
				((buf[i + 3] & 0xF) << 8) + buf[i + 5]);
	}

	input_mt_sync_frame(ts->input);
	input_sync(ts->input);

	return IRQ_HANDLED;
}

static void zet6223_power_off(void *_ts)
{
	struct zet6223_ts *ts = _ts;

	regulator_bulk_disable(ARRAY_SIZE(ts->supplies), ts->supplies);
}

static int zet6223_power_on(struct zet6223_ts *ts)
{
	struct device *dev = &ts->client->dev;
	int error;

	ts->supplies[0].supply = "vio";
	ts->supplies[1].supply = "vcc";

	error = devm_regulator_bulk_get(dev, ARRAY_SIZE(ts->supplies),
					ts->supplies);
	if (error)
		return error;

	error = regulator_bulk_enable(ARRAY_SIZE(ts->supplies), ts->supplies);
	if (error)
		return error;

	msleep(ZET6223_POWER_ON_DELAY_MSEC);

	error = devm_add_action_or_reset(dev, zet6223_power_off, ts);
	if (error) {
		dev_err(dev, "failed to install poweroff action: %d\n", error);
		return error;
	}

	return 0;
}

static int zet6223_query_device(struct zet6223_ts *ts)
{
	u8 buf[ZET6223_CMD_INFO_LENGTH];
	u8 cmd = ZET6223_CMD_INFO;
	int ret;
	int error;

	ret = i2c_master_send(ts->client, &cmd, sizeof(cmd));
	if (ret != sizeof(cmd)) {
		error = ret < 0 ? ret : -EIO;
		dev_err(&ts->client->dev,
			"touchpanel info cmd failed: %d\n", error);
		return error;
	}

	ret = i2c_master_recv(ts->client, buf, sizeof(buf));
	if (ret != sizeof(buf)) {
		error = ret < 0 ? ret : -EIO;
		dev_err(&ts->client->dev,
			"failed to retrieve touchpanel info: %d\n", error);
		return error;
	}

	ts->fingernum = buf[15] & 0x7F;
	if (ts->fingernum > ZET6223_MAX_FINGERS) {
		dev_warn(&ts->client->dev,
			 "touchpanel reports %d fingers, limiting to %d\n",
			 ts->fingernum, ZET6223_MAX_FINGERS);
		ts->fingernum = ZET6223_MAX_FINGERS;
	}

	ts->max_x = get_unaligned_le16(&buf[8]);
	ts->max_y = get_unaligned_le16(&buf[10]);

	return 0;
}

static int zet6223_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct zet6223_ts *ts;
	struct input_dev *input;
	int error;

	if (!client->irq) {
		dev_err(dev, "no irq specified\n");
		return -EINVAL;
	}

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->client = client;

	error = zet6223_power_on(ts);
	if (error)
		return error;

	error = zet6223_query_device(ts);
	if (error)
		return error;

	ts->input = input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	input_set_drvdata(input, ts);

	input->name = client->name;
	input->id.bustype = BUS_I2C;
	input->open = zet6223_start;
	input->close = zet6223_stop;

	input_set_abs_params(input, ABS_MT_POSITION_X, 0, ts->max_x, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, ts->max_y, 0, 0);

	touchscreen_parse_properties(input, true, &ts->prop);

	error = input_mt_init_slots(input, ts->fingernum,
				    INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (error)
		return error;

	error = devm_request_threaded_irq(dev, client->irq, NULL, zet6223_irq,
					  IRQF_ONESHOT, client->name, ts);
	if (error) {
		dev_err(dev, "failed to request irq %d: %d\n",
			client->irq, error);
		return error;
	}

	zet6223_stop(input);

	error = input_register_device(input);
	if (error)
		return error;

	return 0;
}

static const struct of_device_id zet6223_of_match[] = {
	{ .compatible = "zeitec,zet6223" },
	{ }
};
MODULE_DEVICE_TABLE(of, zet6223_of_match);

static const struct i2c_device_id zet6223_id[] = {
	{ "zet6223", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, zet6223_id);

static struct i2c_driver zet6223_driver = {
	.driver = {
		.name = "zet6223",
		.of_match_table = zet6223_of_match,
	},
	.probe = zet6223_probe,
	.id_table = zet6223_id
};
module_i2c_driver(zet6223_driver);

MODULE_AUTHOR("Jelle van der Waa <jelle@vdwaa.nl>");
MODULE_DESCRIPTION("ZEITEC zet622x I2C touchscreen driver");
MODULE_LICENSE("GPL");
