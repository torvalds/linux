/*
 * Driver for ChipOne icn8318 i2c touchscreen controller
 *
 * Copyright (c) 2015 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Red Hat authors:
 * Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/module.h>
#include <linux/of.h>

#define ICN8318_REG_POWER		4
#define ICN8318_REG_TOUCHDATA		16

#define ICN8318_POWER_ACTIVE		0
#define ICN8318_POWER_MONITOR		1
#define ICN8318_POWER_HIBERNATE		2

#define ICN8318_MAX_TOUCHES		5

struct icn8318_touch {
	__u8 slot;
	__be16 x;
	__be16 y;
	__u8 pressure;	/* Seems more like finger width then pressure really */
	__u8 event;
/* The difference between 2 and 3 is unclear */
#define ICN8318_EVENT_NO_DATA	1 /* No finger seen yet since wakeup */
#define ICN8318_EVENT_UPDATE1	2 /* New or updated coordinates */
#define ICN8318_EVENT_UPDATE2	3 /* New or updated coordinates */
#define ICN8318_EVENT_END	4 /* Finger lifted */
} __packed;

struct icn8318_touch_data {
	__u8 softbutton;
	__u8 touch_count;
	struct icn8318_touch touches[ICN8318_MAX_TOUCHES];
} __packed;

struct icn8318_data {
	struct i2c_client *client;
	struct input_dev *input;
	struct gpio_desc *wake_gpio;
	u32 max_x;
	u32 max_y;
	bool invert_x;
	bool invert_y;
	bool swap_x_y;
};

static int icn8318_read_touch_data(struct i2c_client *client,
				   struct icn8318_touch_data *touch_data)
{
	u8 reg = ICN8318_REG_TOUCHDATA;
	struct i2c_msg msg[2] = {
		{
			.addr = client->addr,
			.len = 1,
			.buf = &reg
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = sizeof(struct icn8318_touch_data),
			.buf = (u8 *)touch_data
		}
	};

	return i2c_transfer(client->adapter, msg, 2);
}

static inline bool icn8318_touch_active(u8 event)
{
	return (event == ICN8318_EVENT_UPDATE1) ||
	       (event == ICN8318_EVENT_UPDATE2);
}

static irqreturn_t icn8318_irq(int irq, void *dev_id)
{
	struct icn8318_data *data = dev_id;
	struct device *dev = &data->client->dev;
	struct icn8318_touch_data touch_data;
	int i, ret, x, y;

	ret = icn8318_read_touch_data(data->client, &touch_data);
	if (ret < 0) {
		dev_err(dev, "Error reading touch data: %d\n", ret);
		return IRQ_HANDLED;
	}

	if (touch_data.softbutton) {
		/*
		 * Other data is invalid when a softbutton is pressed.
		 * This needs some extra devicetree bindings to map the icn8318
		 * softbutton codes to evdev codes. Currently no known devices
		 * use this.
		 */
		return IRQ_HANDLED;
	}

	if (touch_data.touch_count > ICN8318_MAX_TOUCHES) {
		dev_warn(dev, "Too much touches %d > %d\n",
			 touch_data.touch_count, ICN8318_MAX_TOUCHES);
		touch_data.touch_count = ICN8318_MAX_TOUCHES;
	}

	for (i = 0; i < touch_data.touch_count; i++) {
		struct icn8318_touch *touch = &touch_data.touches[i];
		bool act = icn8318_touch_active(touch->event);

		input_mt_slot(data->input, touch->slot);
		input_mt_report_slot_state(data->input, MT_TOOL_FINGER, act);
		if (!act)
			continue;

		x = be16_to_cpu(touch->x);
		y = be16_to_cpu(touch->y);

		if (data->invert_x)
			x = data->max_x - x;

		if (data->invert_y)
			y = data->max_y - y;

		if (!data->swap_x_y) {
			input_event(data->input, EV_ABS, ABS_MT_POSITION_X, x);
			input_event(data->input, EV_ABS, ABS_MT_POSITION_Y, y);
		} else {
			input_event(data->input, EV_ABS, ABS_MT_POSITION_X, y);
			input_event(data->input, EV_ABS, ABS_MT_POSITION_Y, x);
		}
	}

	input_mt_sync_frame(data->input);
	input_sync(data->input);

	return IRQ_HANDLED;
}

static int icn8318_start(struct input_dev *dev)
{
	struct icn8318_data *data = input_get_drvdata(dev);

	enable_irq(data->client->irq);
	gpiod_set_value_cansleep(data->wake_gpio, 1);

	return 0;
}

static void icn8318_stop(struct input_dev *dev)
{
	struct icn8318_data *data = input_get_drvdata(dev);

	disable_irq(data->client->irq);
	i2c_smbus_write_byte_data(data->client, ICN8318_REG_POWER,
				  ICN8318_POWER_HIBERNATE);
	gpiod_set_value_cansleep(data->wake_gpio, 0);
}

#ifdef CONFIG_PM_SLEEP
static int icn8318_suspend(struct device *dev)
{
	struct icn8318_data *data = i2c_get_clientdata(to_i2c_client(dev));

	mutex_lock(&data->input->mutex);
	if (data->input->users)
		icn8318_stop(data->input);
	mutex_unlock(&data->input->mutex);

	return 0;
}

static int icn8318_resume(struct device *dev)
{
	struct icn8318_data *data = i2c_get_clientdata(to_i2c_client(dev));

	mutex_lock(&data->input->mutex);
	if (data->input->users)
		icn8318_start(data->input);
	mutex_unlock(&data->input->mutex);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(icn8318_pm_ops, icn8318_suspend, icn8318_resume);

static int icn8318_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;
	struct icn8318_data *data;
	struct input_dev *input;
	u32 fuzz_x = 0, fuzz_y = 0;
	int error;

	if (!client->irq) {
		dev_err(dev, "Error no irq specified\n");
		return -EINVAL;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->wake_gpio = devm_gpiod_get(dev, "wake", GPIOD_OUT_LOW);
	if (IS_ERR(data->wake_gpio)) {
		error = PTR_ERR(data->wake_gpio);
		if (error != -EPROBE_DEFER)
			dev_err(dev, "Error getting wake gpio: %d\n", error);
		return error;
	}

	if (of_property_read_u32(np, "touchscreen-size-x", &data->max_x) ||
	    of_property_read_u32(np, "touchscreen-size-y", &data->max_y)) {
		dev_err(dev, "Error touchscreen-size-x and/or -y missing\n");
		return -EINVAL;
	}

	/* Optional */
	of_property_read_u32(np, "touchscreen-fuzz-x", &fuzz_x);
	of_property_read_u32(np, "touchscreen-fuzz-y", &fuzz_y);
	data->invert_x = of_property_read_bool(np, "touchscreen-inverted-x");
	data->invert_y = of_property_read_bool(np, "touchscreen-inverted-y");
	data->swap_x_y = of_property_read_bool(np, "touchscreen-swapped-x-y");

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	input->name = client->name;
	input->id.bustype = BUS_I2C;
	input->open = icn8318_start;
	input->close = icn8318_stop;
	input->dev.parent = dev;

	if (!data->swap_x_y) {
		input_set_abs_params(input, ABS_MT_POSITION_X, 0,
				     data->max_x, fuzz_x, 0);
		input_set_abs_params(input, ABS_MT_POSITION_Y, 0,
				     data->max_y, fuzz_y, 0);
	} else {
		input_set_abs_params(input, ABS_MT_POSITION_X, 0,
				     data->max_y, fuzz_y, 0);
		input_set_abs_params(input, ABS_MT_POSITION_Y, 0,
				     data->max_x, fuzz_x, 0);
	}

	error = input_mt_init_slots(input, ICN8318_MAX_TOUCHES,
				    INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (error)
		return error;

	data->client = client;
	data->input = input;
	input_set_drvdata(input, data);

	error = devm_request_threaded_irq(dev, client->irq, NULL, icn8318_irq,
					  IRQF_ONESHOT, client->name, data);
	if (error) {
		dev_err(dev, "Error requesting irq: %d\n", error);
		return error;
	}

	/* Stop device till opened */
	icn8318_stop(data->input);

	error = input_register_device(input);
	if (error)
		return error;

	i2c_set_clientdata(client, data);

	return 0;
}

static const struct of_device_id icn8318_of_match[] = {
	{ .compatible = "chipone,icn8318" },
	{ }
};
MODULE_DEVICE_TABLE(of, icn8318_of_match);

/* This is useless for OF-enabled devices, but it is needed by I2C subsystem */
static const struct i2c_device_id icn8318_i2c_id[] = {
	{ },
};
MODULE_DEVICE_TABLE(i2c, icn8318_i2c_id);

static struct i2c_driver icn8318_driver = {
	.driver = {
		.name	= "chipone_icn8318",
		.pm	= &icn8318_pm_ops,
		.of_match_table = icn8318_of_match,
	},
	.probe = icn8318_probe,
	.id_table = icn8318_i2c_id,
};

module_i2c_driver(icn8318_driver);

MODULE_DESCRIPTION("ChipOne icn8318 I2C Touchscreen Driver");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL");
