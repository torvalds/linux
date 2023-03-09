// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for MStar msg2638 touchscreens
 *
 * Copyright (c) 2021 Vincent Knecht <vincent.knecht@mailoo.org>
 *
 * Checksum and IRQ handler based on mstar_drv_common.c and
 * mstar_drv_mutual_fw_control.c
 * Copyright (c) 2006-2012 MStar Semiconductor, Inc.
 *
 * Driver structure based on zinitix.c by Michael Srba <Michael.Srba@seznam.cz>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#define MODE_DATA_RAW			0x5A

#define MSG2138_MAX_FINGERS		2
#define MSG2638_MAX_FINGERS		5

#define MAX_BUTTONS			4

#define CHIP_ON_DELAY_MS		15
#define FIRMWARE_ON_DELAY_MS		50
#define RESET_DELAY_MIN_US		10000
#define RESET_DELAY_MAX_US		11000

struct msg_chip_data {
	irq_handler_t irq_handler;
	unsigned int max_fingers;
};

struct msg2138_packet {
	u8	xy_hi; /* higher bits of x and y coordinates */
	u8	x_low;
	u8	y_low;
};

struct msg2138_touch_event {
	u8	magic;
	struct	msg2138_packet pkt[MSG2138_MAX_FINGERS];
	u8	checksum;
};

struct msg2638_packet {
	u8	xy_hi; /* higher bits of x and y coordinates */
	u8	x_low;
	u8	y_low;
	u8	pressure;
};

struct msg2638_touch_event {
	u8	mode;
	struct	msg2638_packet pkt[MSG2638_MAX_FINGERS];
	u8	proximity;
	u8	checksum;
};

struct msg2638_ts_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct touchscreen_properties prop;
	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpiod;
	int max_fingers;
	u32 keycodes[MAX_BUTTONS];
	int num_keycodes;
};

static u8 msg2638_checksum(u8 *data, u32 length)
{
	s32 sum = 0;
	u32 i;

	for (i = 0; i < length; i++)
		sum += data[i];

	return (u8)((-sum) & 0xFF);
}

static void msg2138_report_keys(struct msg2638_ts_data *msg2638, u8 keys)
{
	int i;

	/* keys can be 0x00 or 0xff when all keys have been released */
	if (keys == 0xff)
		keys = 0;

	for (i = 0; i < msg2638->num_keycodes; ++i)
		input_report_key(msg2638->input_dev, msg2638->keycodes[i],
				 keys & BIT(i));
}

static irqreturn_t msg2138_ts_irq_handler(int irq, void *msg2638_handler)
{
	struct msg2638_ts_data *msg2638 = msg2638_handler;
	struct i2c_client *client = msg2638->client;
	struct input_dev *input = msg2638->input_dev;
	struct msg2138_touch_event touch_event;
	u32 len = sizeof(touch_event);
	struct i2c_msg msg[] = {
		{
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= sizeof(touch_event),
			.buf	= (u8 *)&touch_event,
		},
	};
	struct msg2138_packet *p0, *p1;
	u16 x, y, delta_x, delta_y;
	int ret;

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret != ARRAY_SIZE(msg)) {
		dev_err(&client->dev,
			"Failed I2C transfer in irq handler: %d\n",
			ret < 0 ? ret : -EIO);
		goto out;
	}

	if (msg2638_checksum((u8 *)&touch_event, len - 1) !=
						touch_event.checksum) {
		dev_err(&client->dev, "Failed checksum!\n");
		goto out;
	}

	p0 = &touch_event.pkt[0];
	p1 = &touch_event.pkt[1];

	/* Ignore non-pressed finger data, but check for key code */
	if (p0->xy_hi == 0xFF && p0->x_low == 0xFF && p0->y_low == 0xFF) {
		if (p1->xy_hi == 0xFF && p1->y_low == 0xFF)
			msg2138_report_keys(msg2638, p1->x_low);
		goto report;
	}

	x = ((p0->xy_hi & 0xF0) << 4) | p0->x_low;
	y = ((p0->xy_hi & 0x0F) << 8) | p0->y_low;

	input_mt_slot(input, 0);
	input_mt_report_slot_state(input, MT_TOOL_FINGER, true);
	touchscreen_report_pos(input, &msg2638->prop, x, y, true);

	/* Ignore non-pressed finger data */
	if (p1->xy_hi == 0xFF && p1->x_low == 0xFF && p1->y_low == 0xFF)
		goto report;

	/* Second finger is reported as a delta position */
	delta_x = ((p1->xy_hi & 0xF0) << 4) | p1->x_low;
	delta_y = ((p1->xy_hi & 0x0F) << 8) | p1->y_low;

	/* Ignore second finger if both deltas equal 0 */
	if (delta_x == 0 && delta_y == 0)
		goto report;

	x += delta_x;
	y += delta_y;

	input_mt_slot(input, 1);
	input_mt_report_slot_state(input, MT_TOOL_FINGER, true);
	touchscreen_report_pos(input, &msg2638->prop, x, y, true);

report:
	input_mt_sync_frame(msg2638->input_dev);
	input_sync(msg2638->input_dev);

out:
	return IRQ_HANDLED;
}

static irqreturn_t msg2638_ts_irq_handler(int irq, void *msg2638_handler)
{
	struct msg2638_ts_data *msg2638 = msg2638_handler;
	struct i2c_client *client = msg2638->client;
	struct input_dev *input = msg2638->input_dev;
	struct msg2638_touch_event touch_event;
	u32 len = sizeof(touch_event);
	struct i2c_msg msg[] = {
		{
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= sizeof(touch_event),
			.buf	= (u8 *)&touch_event,
		},
	};
	struct msg2638_packet *p;
	u16 x, y;
	int ret;
	int i;

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret != ARRAY_SIZE(msg)) {
		dev_err(&client->dev,
			"Failed I2C transfer in irq handler: %d\n",
			ret < 0 ? ret : -EIO);
		goto out;
	}

	if (touch_event.mode != MODE_DATA_RAW)
		goto out;

	if (msg2638_checksum((u8 *)&touch_event, len - 1) !=
						touch_event.checksum) {
		dev_err(&client->dev, "Failed checksum!\n");
		goto out;
	}

	for (i = 0; i < msg2638->max_fingers; i++) {
		p = &touch_event.pkt[i];

		/* Ignore non-pressed finger data */
		if (p->xy_hi == 0xFF && p->x_low == 0xFF && p->y_low == 0xFF)
			continue;

		x = (((p->xy_hi & 0xF0) << 4) | p->x_low);
		y = (((p->xy_hi & 0x0F) << 8) | p->y_low);

		input_mt_slot(input, i);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, true);
		touchscreen_report_pos(input, &msg2638->prop, x, y, true);
	}

	input_mt_sync_frame(msg2638->input_dev);
	input_sync(msg2638->input_dev);

out:
	return IRQ_HANDLED;
}

static void msg2638_reset(struct msg2638_ts_data *msg2638)
{
	gpiod_set_value_cansleep(msg2638->reset_gpiod, 1);
	usleep_range(RESET_DELAY_MIN_US, RESET_DELAY_MAX_US);
	gpiod_set_value_cansleep(msg2638->reset_gpiod, 0);
	msleep(FIRMWARE_ON_DELAY_MS);
}

static int msg2638_start(struct msg2638_ts_data *msg2638)
{
	int error;

	error = regulator_bulk_enable(ARRAY_SIZE(msg2638->supplies),
				      msg2638->supplies);
	if (error) {
		dev_err(&msg2638->client->dev,
			"Failed to enable regulators: %d\n", error);
		return error;
	}

	msleep(CHIP_ON_DELAY_MS);

	msg2638_reset(msg2638);

	enable_irq(msg2638->client->irq);

	return 0;
}

static int msg2638_stop(struct msg2638_ts_data *msg2638)
{
	int error;

	disable_irq(msg2638->client->irq);

	error = regulator_bulk_disable(ARRAY_SIZE(msg2638->supplies),
				       msg2638->supplies);
	if (error) {
		dev_err(&msg2638->client->dev,
			"Failed to disable regulators: %d\n", error);
		return error;
	}

	return 0;
}

static int msg2638_input_open(struct input_dev *dev)
{
	struct msg2638_ts_data *msg2638 = input_get_drvdata(dev);

	return msg2638_start(msg2638);
}

static void msg2638_input_close(struct input_dev *dev)
{
	struct msg2638_ts_data *msg2638 = input_get_drvdata(dev);

	msg2638_stop(msg2638);
}

static int msg2638_init_input_dev(struct msg2638_ts_data *msg2638)
{
	struct device *dev = &msg2638->client->dev;
	struct input_dev *input_dev;
	int error;
	int i;

	input_dev = devm_input_allocate_device(dev);
	if (!input_dev) {
		dev_err(dev, "Failed to allocate input device.\n");
		return -ENOMEM;
	}

	input_set_drvdata(input_dev, msg2638);
	msg2638->input_dev = input_dev;

	input_dev->name = "MStar TouchScreen";
	input_dev->phys = "input/ts";
	input_dev->id.bustype = BUS_I2C;
	input_dev->open = msg2638_input_open;
	input_dev->close = msg2638_input_close;

	if (msg2638->num_keycodes) {
		input_dev->keycode = msg2638->keycodes;
		input_dev->keycodemax = msg2638->num_keycodes;
		input_dev->keycodesize = sizeof(msg2638->keycodes[0]);
		for (i = 0; i < msg2638->num_keycodes; i++)
			input_set_capability(input_dev,
					     EV_KEY, msg2638->keycodes[i]);
	}

	input_set_capability(input_dev, EV_ABS, ABS_MT_POSITION_X);
	input_set_capability(input_dev, EV_ABS, ABS_MT_POSITION_Y);

	touchscreen_parse_properties(input_dev, true, &msg2638->prop);
	if (!msg2638->prop.max_x || !msg2638->prop.max_y) {
		dev_err(dev, "touchscreen-size-x and/or touchscreen-size-y not set in properties\n");
		return -EINVAL;
	}

	error = input_mt_init_slots(input_dev, msg2638->max_fingers,
				    INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (error) {
		dev_err(dev, "Failed to initialize MT slots: %d\n", error);
		return error;
	}

	error = input_register_device(input_dev);
	if (error) {
		dev_err(dev, "Failed to register input device: %d\n", error);
		return error;
	}

	return 0;
}

static int msg2638_ts_probe(struct i2c_client *client)
{
	const struct msg_chip_data *chip_data;
	struct device *dev = &client->dev;
	struct msg2638_ts_data *msg2638;
	int error;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(dev, "Failed to assert adapter's support for plain I2C.\n");
		return -ENXIO;
	}

	msg2638 = devm_kzalloc(dev, sizeof(*msg2638), GFP_KERNEL);
	if (!msg2638)
		return -ENOMEM;

	msg2638->client = client;
	i2c_set_clientdata(client, msg2638);

	chip_data = device_get_match_data(&client->dev);
	if (!chip_data || !chip_data->max_fingers) {
		dev_err(dev, "Invalid or missing chip data\n");
		return -EINVAL;
	}

	msg2638->max_fingers = chip_data->max_fingers;

	msg2638->supplies[0].supply = "vdd";
	msg2638->supplies[1].supply = "vddio";
	error = devm_regulator_bulk_get(dev, ARRAY_SIZE(msg2638->supplies),
					msg2638->supplies);
	if (error) {
		dev_err(dev, "Failed to get regulators: %d\n", error);
		return error;
	}

	msg2638->reset_gpiod = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(msg2638->reset_gpiod)) {
		error = PTR_ERR(msg2638->reset_gpiod);
		dev_err(dev, "Failed to request reset GPIO: %d\n", error);
		return error;
	}

	msg2638->num_keycodes = device_property_count_u32(dev,
							  "linux,keycodes");
	if (msg2638->num_keycodes == -EINVAL) {
		msg2638->num_keycodes = 0;
	} else if (msg2638->num_keycodes < 0) {
		dev_err(dev, "Unable to parse linux,keycodes property: %d\n",
			msg2638->num_keycodes);
		return msg2638->num_keycodes;
	} else if (msg2638->num_keycodes > ARRAY_SIZE(msg2638->keycodes)) {
		dev_warn(dev, "Found %d linux,keycodes but max is %zd, ignoring the rest\n",
			 msg2638->num_keycodes, ARRAY_SIZE(msg2638->keycodes));
		msg2638->num_keycodes = ARRAY_SIZE(msg2638->keycodes);
	}

	if (msg2638->num_keycodes > 0) {
		error = device_property_read_u32_array(dev, "linux,keycodes",
						       msg2638->keycodes,
						       msg2638->num_keycodes);
		if (error) {
			dev_err(dev, "Unable to read linux,keycodes values: %d\n",
				error);
			return error;
		}
	}

	error = devm_request_threaded_irq(dev, client->irq,
					  NULL, chip_data->irq_handler,
					  IRQF_ONESHOT | IRQF_NO_AUTOEN,
					  client->name, msg2638);
	if (error) {
		dev_err(dev, "Failed to request IRQ: %d\n", error);
		return error;
	}

	error = msg2638_init_input_dev(msg2638);
	if (error) {
		dev_err(dev, "Failed to initialize input device: %d\n", error);
		return error;
	}

	return 0;
}

static int __maybe_unused msg2638_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct msg2638_ts_data *msg2638 = i2c_get_clientdata(client);

	mutex_lock(&msg2638->input_dev->mutex);

	if (input_device_enabled(msg2638->input_dev))
		msg2638_stop(msg2638);

	mutex_unlock(&msg2638->input_dev->mutex);

	return 0;
}

static int __maybe_unused msg2638_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct msg2638_ts_data *msg2638 = i2c_get_clientdata(client);
	int ret = 0;

	mutex_lock(&msg2638->input_dev->mutex);

	if (input_device_enabled(msg2638->input_dev))
		ret = msg2638_start(msg2638);

	mutex_unlock(&msg2638->input_dev->mutex);

	return ret;
}

static SIMPLE_DEV_PM_OPS(msg2638_pm_ops, msg2638_suspend, msg2638_resume);

static const struct msg_chip_data msg2138_data = {
	.irq_handler = msg2138_ts_irq_handler,
	.max_fingers = MSG2138_MAX_FINGERS,
};

static const struct msg_chip_data msg2638_data = {
	.irq_handler = msg2638_ts_irq_handler,
	.max_fingers = MSG2638_MAX_FINGERS,
};

static const struct of_device_id msg2638_of_match[] = {
	{ .compatible = "mstar,msg2138", .data = &msg2138_data },
	{ .compatible = "mstar,msg2638", .data = &msg2638_data },
	{ }
};
MODULE_DEVICE_TABLE(of, msg2638_of_match);

static struct i2c_driver msg2638_ts_driver = {
	.probe_new = msg2638_ts_probe,
	.driver = {
		.name = "MStar-TS",
		.pm = &msg2638_pm_ops,
		.of_match_table = msg2638_of_match,
	},
};
module_i2c_driver(msg2638_ts_driver);

MODULE_AUTHOR("Vincent Knecht <vincent.knecht@mailoo.org>");
MODULE_DESCRIPTION("MStar MSG2638 touchscreen driver");
MODULE_LICENSE("GPL v2");
