/*
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/i2c/mms114.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

/* Write only registers */
#define MMS114_MODE_CONTROL		0x01
#define MMS114_OPERATION_MODE_MASK	0xE
#define MMS114_ACTIVE			(1 << 1)

#define MMS114_XY_RESOLUTION_H		0x02
#define MMS114_X_RESOLUTION		0x03
#define MMS114_Y_RESOLUTION		0x04
#define MMS114_CONTACT_THRESHOLD	0x05
#define MMS114_MOVING_THRESHOLD		0x06

/* Read only registers */
#define MMS114_PACKET_SIZE		0x0F
#define MMS114_INFOMATION		0x10
#define MMS114_TSP_REV			0xF0

/* Minimum delay time is 50us between stop and start signal of i2c */
#define MMS114_I2C_DELAY		50

/* 200ms needs after power on */
#define MMS114_POWERON_DELAY		200

/* Touchscreen absolute values */
#define MMS114_MAX_AREA			0xff

#define MMS114_MAX_TOUCH		10
#define MMS114_PACKET_NUM		8

/* Touch type */
#define MMS114_TYPE_NONE		0
#define MMS114_TYPE_TOUCHSCREEN		1
#define MMS114_TYPE_TOUCHKEY		2

struct mms114_data {
	struct i2c_client	*client;
	struct input_dev	*input_dev;
	struct regulator	*core_reg;
	struct regulator	*io_reg;
	const struct mms114_platform_data	*pdata;

	/* Use cache data for mode control register(write only) */
	u8			cache_mode_control;
};

struct mms114_touch {
	u8 id:4, reserved_bit4:1, type:2, pressed:1;
	u8 x_hi:4, y_hi:4;
	u8 x_lo;
	u8 y_lo;
	u8 width;
	u8 strength;
	u8 reserved[2];
} __packed;

static int __mms114_read_reg(struct mms114_data *data, unsigned int reg,
			     unsigned int len, u8 *val)
{
	struct i2c_client *client = data->client;
	struct i2c_msg xfer[2];
	u8 buf = reg & 0xff;
	int error;

	if (reg <= MMS114_MODE_CONTROL && reg + len > MMS114_MODE_CONTROL)
		BUG();

	/* Write register: use repeated start */
	xfer[0].addr = client->addr;
	xfer[0].flags = I2C_M_TEN | I2C_M_NOSTART;
	xfer[0].len = 1;
	xfer[0].buf = &buf;

	/* Read data */
	xfer[1].addr = client->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = len;
	xfer[1].buf = val;

	error = i2c_transfer(client->adapter, xfer, 2);
	if (error != 2) {
		dev_err(&client->dev,
			"%s: i2c transfer failed (%d)\n", __func__, error);
		return error < 0 ? error : -EIO;
	}
	udelay(MMS114_I2C_DELAY);

	return 0;
}

static int mms114_read_reg(struct mms114_data *data, unsigned int reg)
{
	u8 val;
	int error;

	if (reg == MMS114_MODE_CONTROL)
		return data->cache_mode_control;

	error = __mms114_read_reg(data, reg, 1, &val);
	return error < 0 ? error : val;
}

static int mms114_write_reg(struct mms114_data *data, unsigned int reg,
			    unsigned int val)
{
	struct i2c_client *client = data->client;
	u8 buf[2];
	int error;

	buf[0] = reg & 0xff;
	buf[1] = val & 0xff;

	error = i2c_master_send(client, buf, 2);
	if (error != 2) {
		dev_err(&client->dev,
			"%s: i2c send failed (%d)\n", __func__, error);
		return error < 0 ? error : -EIO;
	}
	udelay(MMS114_I2C_DELAY);

	if (reg == MMS114_MODE_CONTROL)
		data->cache_mode_control = val;

	return 0;
}

static void mms114_process_mt(struct mms114_data *data, struct mms114_touch *touch)
{
	const struct mms114_platform_data *pdata = data->pdata;
	struct i2c_client *client = data->client;
	struct input_dev *input_dev = data->input_dev;
	unsigned int id;
	unsigned int x;
	unsigned int y;

	if (touch->id > MMS114_MAX_TOUCH) {
		dev_err(&client->dev, "Wrong touch id (%d)\n", touch->id);
		return;
	}

	if (touch->type != MMS114_TYPE_TOUCHSCREEN) {
		dev_err(&client->dev, "Wrong touch type (%d)\n", touch->type);
		return;
	}

	id = touch->id - 1;
	x = touch->x_lo | touch->x_hi << 8;
	y = touch->y_lo | touch->y_hi << 8;
	if (x > pdata->x_size || y > pdata->y_size) {
		dev_dbg(&client->dev,
			"Wrong touch coordinates (%d, %d)\n", x, y);
		return;
	}

	if (pdata->x_invert)
		x = pdata->x_size - x;
	if (pdata->y_invert)
		y = pdata->y_size - y;

	dev_dbg(&client->dev,
		"id: %d, type: %d, pressed: %d, x: %d, y: %d, width: %d, strength: %d\n",
		id, touch->type, touch->pressed,
		x, y, touch->width, touch->strength);

	input_mt_slot(input_dev, id);
	input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, touch->pressed);

	if (touch->pressed) {
		input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, touch->width);
		input_report_abs(input_dev, ABS_MT_POSITION_X, x);
		input_report_abs(input_dev, ABS_MT_POSITION_Y, y);
		input_report_abs(input_dev, ABS_MT_PRESSURE, touch->strength);
	}
}

static irqreturn_t mms114_interrupt(int irq, void *dev_id)
{
	struct mms114_data *data = dev_id;
	struct input_dev *input_dev = data->input_dev;
	struct mms114_touch touch[MMS114_MAX_TOUCH];
	int packet_size;
	int touch_size;
	int index;
	int error;

	mutex_lock(&input_dev->mutex);
	if (!input_dev->users) {
		mutex_unlock(&input_dev->mutex);
		goto out;
	}
	mutex_unlock(&input_dev->mutex);

	packet_size = mms114_read_reg(data, MMS114_PACKET_SIZE);
	if (packet_size <= 0)
		goto out;

	touch_size = packet_size / MMS114_PACKET_NUM;

	error = __mms114_read_reg(data, MMS114_INFOMATION, packet_size,
			(u8 *)touch);
	if (error < 0)
		goto out;

	for (index = 0; index < touch_size; index++)
		mms114_process_mt(data, touch + index);

	input_mt_report_pointer_emulation(data->input_dev, true);
	input_sync(data->input_dev);

out:
	return IRQ_HANDLED;
}

static int mms114_set_active(struct mms114_data *data, bool active)
{
	int val;

	val = mms114_read_reg(data, MMS114_MODE_CONTROL);
	if (val < 0)
		return val;

	val &= ~MMS114_OPERATION_MODE_MASK;

	/* If active is false, sleep mode */
	if (active)
		val |= MMS114_ACTIVE;

	return mms114_write_reg(data, MMS114_MODE_CONTROL, val);
}

static int mms114_get_version(struct mms114_data *data)
{
	struct device *dev = &data->client->dev;
	u8 buf[6];
	int error;

	error = __mms114_read_reg(data, MMS114_TSP_REV, 6, buf);
	if (error < 0)
		return error;

	dev_info(dev, "TSP Rev: 0x%x, HW Rev: 0x%x, Firmware Ver: 0x%x\n",
		 buf[0], buf[1], buf[3]);

	return 0;
}

static int mms114_setup_regs(struct mms114_data *data)
{
	const struct mms114_platform_data *pdata = data->pdata;
	int val;
	int error;

	error = mms114_get_version(data);
	if (error < 0)
		return error;

	error = mms114_set_active(data, true);
	if (error < 0)
		return error;

	val = (pdata->x_size >> 8) & 0xf;
	val |= ((pdata->y_size >> 8) & 0xf) << 4;
	error = mms114_write_reg(data, MMS114_XY_RESOLUTION_H, val);
	if (error < 0)
		return error;

	val = pdata->x_size & 0xff;
	error = mms114_write_reg(data, MMS114_X_RESOLUTION, val);
	if (error < 0)
		return error;

	val = pdata->y_size & 0xff;
	error = mms114_write_reg(data, MMS114_Y_RESOLUTION, val);
	if (error < 0)
		return error;

	if (pdata->contact_threshold) {
		error = mms114_write_reg(data, MMS114_CONTACT_THRESHOLD,
				pdata->contact_threshold);
		if (error < 0)
			return error;
	}

	if (pdata->moving_threshold) {
		error = mms114_write_reg(data, MMS114_MOVING_THRESHOLD,
				pdata->moving_threshold);
		if (error < 0)
			return error;
	}

	return 0;
}

static int mms114_start(struct mms114_data *data)
{
	struct i2c_client *client = data->client;
	int error;

	error = regulator_enable(data->core_reg);
	if (error) {
		dev_err(&client->dev, "Failed to enable avdd: %d\n", error);
		return error;
	}

	error = regulator_enable(data->io_reg);
	if (error) {
		dev_err(&client->dev, "Failed to enable vdd: %d\n", error);
		regulator_disable(data->core_reg);
		return error;
	}

	mdelay(MMS114_POWERON_DELAY);

	error = mms114_setup_regs(data);
	if (error < 0) {
		regulator_disable(data->io_reg);
		regulator_disable(data->core_reg);
		return error;
	}

	if (data->pdata->cfg_pin)
		data->pdata->cfg_pin(true);

	enable_irq(client->irq);

	return 0;
}

static void mms114_stop(struct mms114_data *data)
{
	struct i2c_client *client = data->client;
	int error;

	disable_irq(client->irq);

	if (data->pdata->cfg_pin)
		data->pdata->cfg_pin(false);

	error = regulator_disable(data->io_reg);
	if (error)
		dev_warn(&client->dev, "Failed to disable vdd: %d\n", error);

	error = regulator_disable(data->core_reg);
	if (error)
		dev_warn(&client->dev, "Failed to disable avdd: %d\n", error);
}

static int mms114_input_open(struct input_dev *dev)
{
	struct mms114_data *data = input_get_drvdata(dev);

	return mms114_start(data);
}

static void mms114_input_close(struct input_dev *dev)
{
	struct mms114_data *data = input_get_drvdata(dev);

	mms114_stop(data);
}

#ifdef CONFIG_OF
static struct mms114_platform_data *mms114_parse_dt(struct device *dev)
{
	struct mms114_platform_data *pdata;
	struct device_node *np = dev->of_node;

	if (!np)
		return NULL;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(dev, "failed to allocate platform data\n");
		return NULL;
	}

	if (of_property_read_u32(np, "x-size", &pdata->x_size)) {
		dev_err(dev, "failed to get x-size property\n");
		return NULL;
	};

	if (of_property_read_u32(np, "y-size", &pdata->y_size)) {
		dev_err(dev, "failed to get y-size property\n");
		return NULL;
	};

	of_property_read_u32(np, "contact-threshold",
				&pdata->contact_threshold);
	of_property_read_u32(np, "moving-threshold",
				&pdata->moving_threshold);

	if (of_find_property(np, "x-invert", NULL))
		pdata->x_invert = true;
	if (of_find_property(np, "y-invert", NULL))
		pdata->y_invert = true;

	return pdata;
}
#else
static inline struct mms114_platform_data *mms114_parse_dt(struct device *dev)
{
	return NULL;
}
#endif

static int mms114_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	const struct mms114_platform_data *pdata;
	struct mms114_data *data;
	struct input_dev *input_dev;
	int error;

	pdata = dev_get_platdata(&client->dev);
	if (!pdata)
		pdata = mms114_parse_dt(&client->dev);

	if (!pdata) {
		dev_err(&client->dev, "Need platform data\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_PROTOCOL_MANGLING)) {
		dev_err(&client->dev,
			"Need i2c bus that supports protocol mangling\n");
		return -ENODEV;
	}

	data = devm_kzalloc(&client->dev, sizeof(struct mms114_data),
			    GFP_KERNEL);
	input_dev = devm_input_allocate_device(&client->dev);
	if (!data || !input_dev) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	data->client = client;
	data->input_dev = input_dev;
	data->pdata = pdata;

	input_dev->name = "MELFAS MMS114 Touchscreen";
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;
	input_dev->open = mms114_input_open;
	input_dev->close = mms114_input_close;

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);
	input_set_abs_params(input_dev, ABS_X, 0, data->pdata->x_size, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, data->pdata->y_size, 0, 0);

	/* For multi touch */
	input_mt_init_slots(input_dev, MMS114_MAX_TOUCH, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR,
			     0, MMS114_MAX_AREA, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X,
			     0, data->pdata->x_size, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y,
			     0, data->pdata->y_size, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, 255, 0, 0);

	input_set_drvdata(input_dev, data);
	i2c_set_clientdata(client, data);

	data->core_reg = devm_regulator_get(&client->dev, "avdd");
	if (IS_ERR(data->core_reg)) {
		error = PTR_ERR(data->core_reg);
		dev_err(&client->dev,
			"Unable to get the Core regulator (%d)\n", error);
		return error;
	}

	data->io_reg = devm_regulator_get(&client->dev, "vdd");
	if (IS_ERR(data->io_reg)) {
		error = PTR_ERR(data->io_reg);
		dev_err(&client->dev,
			"Unable to get the IO regulator (%d)\n", error);
		return error;
	}

	error = devm_request_threaded_irq(&client->dev, client->irq, NULL,
			mms114_interrupt, IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			dev_name(&client->dev), data);
	if (error) {
		dev_err(&client->dev, "Failed to register interrupt\n");
		return error;
	}
	disable_irq(client->irq);

	error = input_register_device(data->input_dev);
	if (error) {
		dev_err(&client->dev, "Failed to register input device\n");
		return error;
	}

	return 0;
}

static int __maybe_unused mms114_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mms114_data *data = i2c_get_clientdata(client);
	struct input_dev *input_dev = data->input_dev;
	int id;

	/* Release all touch */
	for (id = 0; id < MMS114_MAX_TOUCH; id++) {
		input_mt_slot(input_dev, id);
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, false);
	}

	input_mt_report_pointer_emulation(input_dev, true);
	input_sync(input_dev);

	mutex_lock(&input_dev->mutex);
	if (input_dev->users)
		mms114_stop(data);
	mutex_unlock(&input_dev->mutex);

	return 0;
}

static int __maybe_unused mms114_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mms114_data *data = i2c_get_clientdata(client);
	struct input_dev *input_dev = data->input_dev;
	int error;

	mutex_lock(&input_dev->mutex);
	if (input_dev->users) {
		error = mms114_start(data);
		if (error < 0) {
			mutex_unlock(&input_dev->mutex);
			return error;
		}
	}
	mutex_unlock(&input_dev->mutex);

	return 0;
}

static SIMPLE_DEV_PM_OPS(mms114_pm_ops, mms114_suspend, mms114_resume);

static const struct i2c_device_id mms114_id[] = {
	{ "mms114", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mms114_id);

#ifdef CONFIG_OF
static const struct of_device_id mms114_dt_match[] = {
	{ .compatible = "melfas,mms114" },
	{ }
};
#endif

static struct i2c_driver mms114_driver = {
	.driver = {
		.name	= "mms114",
		.owner	= THIS_MODULE,
		.pm	= &mms114_pm_ops,
		.of_match_table = of_match_ptr(mms114_dt_match),
	},
	.probe		= mms114_probe,
	.id_table	= mms114_id,
};

module_i2c_driver(mms114_driver);

/* Module information */
MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_DESCRIPTION("MELFAS mms114 Touchscreen driver");
MODULE_LICENSE("GPL");
