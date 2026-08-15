// SPDX-License-Identifier: GPL-2.0
// Melfas MMS114/MMS136/MMS152 touchscreen device driver
//
// Copyright (c) 2012 Samsung Electronics Co., Ltd.
// Author: Joonyoung Shim <jy0922.shim@samsung.com>

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

/* Write only registers */
#define MMS114_MODE_CONTROL		0x01
#define MMS114_OPERATION_MODE_MASK	0xE
#define MMS114_ACTIVE			BIT(1)

#define MMS114_XY_RESOLUTION_H		0x02
#define MMS114_X_RESOLUTION		0x03
#define MMS114_Y_RESOLUTION		0x04
#define MMS114_CONTACT_THRESHOLD	0x05
#define MMS114_MOVING_THRESHOLD		0x06

/* Read only registers */
#define MMS114_PACKET_SIZE		0x0F
#define MMS114_INFORMATION		0x10
#define MMS114_TSP_REV			0xF0

#define MMS152_FW_REV			0xE1
#define MMS152_COMPAT_GROUP		0xF2

/* Minimum delay time is 50us between stop and start signal of i2c */
#define MMS114_I2C_DELAY		50

/* 200ms needs after power on */
#define MMS114_POWERON_DELAY		200

/* Touchscreen absolute values */
#define MMS114_MAX_AREA			0xff

#define MMS114_MAX_TOUCHKEYS		15
#define MMS114_MAX_TOUCH		10
#define MMS114_EVENT_SIZE		8
#define MMS136_EVENT_SIZE		6

/* Touch type */
#define MMS114_TYPE_NONE		0
#define MMS114_TYPE_TOUCHSCREEN		1
#define MMS114_TYPE_TOUCHKEY		2

struct mms114_data {
	const struct mms_chip	*chip;
	struct i2c_client	*client;
	struct input_dev	*input_dev;
	struct regulator	*core_reg;
	struct regulator	*io_reg;
	struct touchscreen_properties props;
	unsigned int		contact_threshold;
	unsigned int		moving_threshold;

	u32 keycodes[MMS114_MAX_TOUCHKEYS];
	int num_keycodes;

	/* Use cache data for mode control register(write only) */
	u8			cache_mode_control;
};

struct mms_chip {
	const char *name;
	int event_size;
	bool has_config_regs;
	int (*get_version)(struct mms114_data *data);
};

#define MMS114_FLAGS_ID_MASK		GENMASK(3, 0)
#define MMS114_FLAGS_TYPE_MASK		GENMASK(6, 5)
#define MMS114_FLAGS_PRESSED_MASK	BIT(7)

#define MMS114_XY_HI_X_MASK		GENMASK(3, 0)
#define MMS114_XY_HI_Y_MASK		GENMASK(7, 4)

struct mms114_touch {
	u8 flags;
	u8 xy_hi;
	u8 x_lo;
	u8 y_lo;
	u8 width;
	u8 strength;
	u8 reserved[2];
} __packed;

static int __mms114_read_reg(struct mms114_data *data, u8 reg,
			     unsigned int len, void *val)
{
	struct i2c_client *client = data->client;
	struct i2c_msg xfer[2];
	u8 buf = reg;
	int error;

	if (WARN_ON(reg <= MMS114_MODE_CONTROL && reg + len > MMS114_MODE_CONTROL))
		return -EINVAL;

	/* Write register */
	xfer[0].addr = client->addr;
	xfer[0].flags = client->flags & I2C_M_TEN;
	xfer[0].len = 1;
	xfer[0].buf = &buf;

	/* Read data */
	xfer[1].addr = client->addr;
	xfer[1].flags = (client->flags & I2C_M_TEN) | I2C_M_RD;
	xfer[1].len = len;
	xfer[1].buf = val;

	error = i2c_transfer(client->adapter, xfer, 2);
	if (error != 2) {
		dev_err(&client->dev,
			"%s: i2c transfer failed (%d)\n", __func__, error);
		return error < 0 ? error : -EIO;
	}
	usleep_range(MMS114_I2C_DELAY, MMS114_I2C_DELAY + 50);

	return 0;
}

static int mms114_read_reg(struct mms114_data *data, u8 reg)
{
	u8 val;
	int error;

	if (reg == MMS114_MODE_CONTROL)
		return data->cache_mode_control;

	error = __mms114_read_reg(data, reg, 1, &val);
	return error < 0 ? error : val;
}

static int mms114_write_reg(struct mms114_data *data, u8 reg, u8 val)
{
	struct i2c_client *client = data->client;
	u8 buf[2];
	int error;

	buf[0] = reg;
	buf[1] = val;

	error = i2c_master_send(client, buf, 2);
	if (error != 2) {
		dev_err(&client->dev,
			"%s: i2c send failed (%d)\n", __func__, error);
		return error < 0 ? error : -EIO;
	}
	usleep_range(MMS114_I2C_DELAY, MMS114_I2C_DELAY + 50);

	if (reg == MMS114_MODE_CONTROL)
		data->cache_mode_control = val;

	return 0;
}

static int mms114_get_version(struct mms114_data *data)
{
	struct device *dev = &data->client->dev;
	u8 buf[6];
	int error;

	error = __mms114_read_reg(data, MMS114_TSP_REV, 6, buf);
	if (error)
		return error;

	dev_info(dev, "TSP Rev: 0x%x, HW Rev: 0x%x, Firmware Ver: 0x%x\n",
		 buf[0], buf[1], buf[3]);
	return 0;
}

static int mms152_get_version(struct mms114_data *data)
{
	struct device *dev = &data->client->dev;
	u8 buf[3];
	int group;
	int error;

	error = __mms114_read_reg(data, MMS152_FW_REV, 3, buf);
	if (error)
		return error;

	group = i2c_smbus_read_byte_data(data->client, MMS152_COMPAT_GROUP);
	if (group < 0)
		return group;

	dev_info(dev, "TSP FW Rev: bootloader 0x%x / core 0x%x / config 0x%x, Compat group: %c\n",
		 buf[0], buf[1], buf[2], group);
	return 0;
}

static int mms345l_get_version(struct mms114_data *data)
{
	struct device *dev = &data->client->dev;
	u8 buf[3];
	int error;

	error = __mms114_read_reg(data, MMS152_FW_REV, 3, buf);
	if (error)
		return error;

	dev_info(dev, "TSP FW Rev: bootloader 0x%x / core 0x%x / config 0x%x\n",
		 buf[0], buf[1], buf[2]);
	return 0;
}

static const struct mms_chip mms114_descriptor = {
	.name = "MMS114",
	.event_size = MMS114_EVENT_SIZE,
	.has_config_regs = true,
	.get_version = mms114_get_version,
};

static const struct mms_chip mms134s_descriptor = {
	.name = "MMS134S",
	.event_size = MMS136_EVENT_SIZE,
	.has_config_regs = true,
	.get_version = mms114_get_version,
};

static const struct mms_chip mms136_descriptor = {
	.name = "MMS136",
	.event_size = MMS136_EVENT_SIZE,
	.has_config_regs = true,
	.get_version = mms114_get_version,
};

static const struct mms_chip mms152_descriptor = {
	.name = "MMS152",
	.event_size = MMS114_EVENT_SIZE,
	.has_config_regs = false,
	.get_version = mms152_get_version,
};

static const struct mms_chip mms345l_descriptor = {
	.name = "MMS345L",
	.event_size = MMS114_EVENT_SIZE,
	.has_config_regs = false,
	.get_version = mms345l_get_version,
};

static void mms114_process_mt(struct mms114_data *data, struct mms114_touch *touch)
{
	struct i2c_client *client = data->client;
	struct input_dev *input_dev = data->input_dev;
	unsigned int id = FIELD_GET(MMS114_FLAGS_ID_MASK, touch->flags);
	unsigned int type = FIELD_GET(MMS114_FLAGS_TYPE_MASK, touch->flags);
	bool pressed = FIELD_GET(MMS114_FLAGS_PRESSED_MASK, touch->flags);
	unsigned int x;
	unsigned int y;

	if (id == 0 || id > MMS114_MAX_TOUCH) {
		dev_err(&client->dev, "Wrong touch id (%d)\n", id);
		return;
	}

	id--;
	x = touch->x_lo | FIELD_GET(MMS114_XY_HI_X_MASK, touch->xy_hi) << 8;
	y = touch->y_lo | FIELD_GET(MMS114_XY_HI_Y_MASK, touch->xy_hi) << 8;

	dev_dbg(&client->dev,
		"id: %d, type: %d, pressed: %d, x: %d, y: %d, width: %d, strength: %d\n",
		id, type, pressed,
		x, y, touch->width, touch->strength);

	input_mt_slot(input_dev, id);
	input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, pressed);

	if (pressed) {
		touchscreen_report_pos(input_dev, &data->props, x, y, true);
		input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, touch->width);
		input_report_abs(input_dev, ABS_MT_PRESSURE, touch->strength);
	}
}

static void mms114_process_touchkey(struct mms114_data *data,
				    struct mms114_touch *touch)
{
	struct i2c_client *client = data->client;
	struct input_dev *input_dev = data->input_dev;
	unsigned int keycode_id;
	unsigned int id = FIELD_GET(MMS114_FLAGS_ID_MASK, touch->flags);
	bool pressed = FIELD_GET(MMS114_FLAGS_PRESSED_MASK, touch->flags);

	if (id == 0)
		return;

	if (id > data->num_keycodes) {
		dev_err(&client->dev, "Wrong touch id for touchkey (%d)\n",
			id);
		return;
	}

	keycode_id = id - 1;
	dev_dbg(&client->dev, "keycode id: %d, pressed: %d\n", keycode_id,
		pressed);

	input_report_key(input_dev, data->keycodes[keycode_id], pressed);
}

static irqreturn_t mms114_interrupt(int irq, void *dev_id)
{
	struct mms114_data *data = dev_id;
	struct i2c_client *client = data->client;
	struct mms114_touch touch[MMS114_MAX_TOUCH];
	struct mms114_touch *t;
	int event_size = data->chip->event_size;
	int packet_size;
	int touch_size;
	int index;
	int error;

	packet_size = mms114_read_reg(data, MMS114_PACKET_SIZE);
	if (packet_size <= 0)
		goto out;

	if (packet_size > sizeof(touch)) {
		dev_err(&client->dev, "Invalid packet size %d (max %zu)\n",
			packet_size, sizeof(touch));
		goto out;
	}

	touch_size = packet_size / event_size;

	error = __mms114_read_reg(data, MMS114_INFORMATION, packet_size, touch);
	if (error)
		goto out;

	for (index = 0; index < touch_size; index++) {
		t = (struct mms114_touch *)((u8 *)touch + index * event_size);
		unsigned int type = FIELD_GET(MMS114_FLAGS_TYPE_MASK, t->flags);

		switch (type) {
		case MMS114_TYPE_TOUCHSCREEN:
			mms114_process_mt(data, t);
			break;

		case MMS114_TYPE_TOUCHKEY:
			mms114_process_touchkey(data, t);
			break;

		default:
			dev_err(&client->dev, "Wrong touch type (%d)\n",
				type);
			break;
		}
	}

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

static int mms114_setup_regs(struct mms114_data *data)
{
	const struct touchscreen_properties *props = &data->props;
	int val;
	int error;

	error = data->chip->get_version(data);
	if (error)
		return error;

	if (!data->chip->has_config_regs)
		return 0;

	error = mms114_set_active(data, true);
	if (error < 0)
		return error;

	val = (props->max_x >> 8) & 0xf;
	val |= ((props->max_y >> 8) & 0xf) << 4;
	error = mms114_write_reg(data, MMS114_XY_RESOLUTION_H, val);
	if (error < 0)
		return error;

	val = props->max_x & 0xff;
	error = mms114_write_reg(data, MMS114_X_RESOLUTION, val);
	if (error < 0)
		return error;

	val = props->max_y & 0xff;
	error = mms114_write_reg(data, MMS114_Y_RESOLUTION, val);
	if (error < 0)
		return error;

	if (data->contact_threshold) {
		error = mms114_write_reg(data, MMS114_CONTACT_THRESHOLD,
					 data->contact_threshold);
		if (error < 0)
			return error;
	}

	if (data->moving_threshold) {
		error = mms114_write_reg(data, MMS114_MOVING_THRESHOLD,
					 data->moving_threshold);
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

	msleep(MMS114_POWERON_DELAY);

	error = mms114_setup_regs(data);
	if (error < 0) {
		regulator_disable(data->io_reg);
		regulator_disable(data->core_reg);
		return error;
	}

	enable_irq(client->irq);

	return 0;
}

static void mms114_stop(struct mms114_data *data)
{
	struct i2c_client *client = data->client;
	int error;

	disable_irq(client->irq);

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

static int mms114_parse_legacy_bindings(struct mms114_data *data)
{
	struct device *dev = &data->client->dev;
	struct touchscreen_properties *props = &data->props;

	if (device_property_read_u32(dev, "x-size", &props->max_x)) {
		dev_dbg(dev, "failed to get legacy x-size property\n");
		return -EINVAL;
	}

	if (device_property_read_u32(dev, "y-size", &props->max_y)) {
		dev_dbg(dev, "failed to get legacy y-size property\n");
		return -EINVAL;
	}

	device_property_read_u32(dev, "contact-threshold",
				 &data->contact_threshold);
	device_property_read_u32(dev, "moving-threshold",
				 &data->moving_threshold);

	if (device_property_read_bool(dev, "x-invert"))
		props->invert_x = true;
	if (device_property_read_bool(dev, "y-invert"))
		props->invert_y = true;

	props->swap_x_y = false;

	return 0;
}

static int mms114_probe(struct i2c_client *client)
{
	struct mms114_data *data;
	struct input_dev *input_dev;
	int error;
	int i;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "Not supported I2C adapter\n");
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

	data->chip = i2c_get_match_data(client);
	if (!data->chip)
		return -EINVAL;

	data->num_keycodes = device_property_count_u32(&client->dev,
						       "linux,keycodes");
	if (data->num_keycodes == -EINVAL) {
		data->num_keycodes = 0;
	} else if (data->num_keycodes < 0) {
		dev_err(&client->dev,
			"Unable to parse linux,keycodes property: %d\n",
			data->num_keycodes);
		return data->num_keycodes;
	} else if (data->num_keycodes > MMS114_MAX_TOUCHKEYS) {
		dev_warn(&client->dev,
			 "Found %d linux,keycodes but max is %d, ignoring the rest\n",
			 data->num_keycodes, MMS114_MAX_TOUCHKEYS);
		data->num_keycodes = MMS114_MAX_TOUCHKEYS;
	}

	if (data->num_keycodes > 0) {
		error = device_property_read_u32_array(&client->dev,
						       "linux,keycodes",
						       data->keycodes,
						       data->num_keycodes);
		if (error) {
			dev_err(&client->dev,
				"Unable to read linux,keycodes values: %d\n",
				error);
			return error;
		}

		input_dev->keycode = data->keycodes;
		input_dev->keycodemax = data->num_keycodes;
		input_dev->keycodesize = sizeof(data->keycodes[0]);
		for (i = 0; i < data->num_keycodes; i++)
			input_set_capability(input_dev,
					     EV_KEY, data->keycodes[i]);
	}

	input_set_capability(input_dev, EV_ABS, ABS_MT_POSITION_X);
	input_set_capability(input_dev, EV_ABS, ABS_MT_POSITION_Y);
	input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR,
			     0, MMS114_MAX_AREA, 0, 0);

	touchscreen_parse_properties(input_dev, true, &data->props);
	if (!data->props.max_x || !data->props.max_y) {
		dev_dbg(&client->dev,
			"missing X/Y size properties, trying legacy bindings\n");
		error = mms114_parse_legacy_bindings(data);
		if (error)
			return error;

		input_set_abs_params(input_dev, ABS_MT_POSITION_X,
				     0, data->props.max_x, 0, 0);
		input_set_abs_params(input_dev, ABS_MT_POSITION_Y,
				     0, data->props.max_y, 0, 0);
	}

	if (data->chip->has_config_regs) {
		/*
		 * The firmware handles movement and pressure fuzz, so
		 * don't duplicate that in software.
		 */
		data->moving_threshold = input_abs_get_fuzz(input_dev,
							    ABS_MT_POSITION_X);
		data->contact_threshold = input_abs_get_fuzz(input_dev,
							     ABS_MT_PRESSURE);
		input_abs_set_fuzz(input_dev, ABS_MT_POSITION_X, 0);
		input_abs_set_fuzz(input_dev, ABS_MT_POSITION_Y, 0);
		input_abs_set_fuzz(input_dev, ABS_MT_PRESSURE, 0);
	}

	input_dev->name = devm_kasprintf(&client->dev, GFP_KERNEL,
					 "MELFAS %s Touchscreen",
					 data->chip->name);
	if (!input_dev->name)
		return -ENOMEM;

	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;
	input_dev->open = mms114_input_open;
	input_dev->close = mms114_input_close;

	error = input_mt_init_slots(input_dev, MMS114_MAX_TOUCH,
				    INPUT_MT_DIRECT);
	if (error)
		return error;

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

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, mms114_interrupt,
					  IRQF_ONESHOT | IRQF_NO_AUTOEN,
					  dev_name(&client->dev), data);
	if (error) {
		dev_err(&client->dev, "Failed to register interrupt\n");
		return error;
	}

	error = input_register_device(data->input_dev);
	if (error) {
		dev_err(&client->dev, "Failed to register input device\n");
		return error;
	}

	return 0;
}

static int mms114_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mms114_data *data = i2c_get_clientdata(client);
	struct input_dev *input_dev = data->input_dev;
	int id;

	/* Release all touch */
	for (id = 0; id < MMS114_MAX_TOUCH; id++) {
		input_mt_slot(input_dev, id);
		input_mt_report_slot_inactive(input_dev);
	}

	input_mt_report_pointer_emulation(input_dev, true);
	input_sync(input_dev);

	guard(mutex)(&input_dev->mutex);

	if (input_device_enabled(input_dev))
		mms114_stop(data);

	return 0;
}

static int mms114_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mms114_data *data = i2c_get_clientdata(client);
	struct input_dev *input_dev = data->input_dev;
	int error;

	guard(mutex)(&input_dev->mutex);

	if (input_device_enabled(input_dev)) {
		error = mms114_start(data);
		if (error)
			return error;
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(mms114_pm_ops, mms114_suspend, mms114_resume);

static const struct i2c_device_id mms114_id[] = {
	{ .name = "mms114", .driver_data = (kernel_ulong_t)&mms114_descriptor },
	{ .name = "mms134s", .driver_data = (kernel_ulong_t)&mms134s_descriptor },
	{ .name = "mms136", .driver_data = (kernel_ulong_t)&mms136_descriptor },
	{ .name = "mms152", .driver_data = (kernel_ulong_t)&mms152_descriptor },
	{ .name = "mms345l", .driver_data = (kernel_ulong_t)&mms345l_descriptor },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mms114_id);

#ifdef CONFIG_OF
static const struct of_device_id mms114_dt_match[] = {
	{ .compatible = "melfas,mms114", .data = &mms114_descriptor },
	{ .compatible = "melfas,mms134s", .data = &mms134s_descriptor },
	{ .compatible = "melfas,mms136", .data = &mms136_descriptor },
	{ .compatible = "melfas,mms152", .data = &mms152_descriptor },
	{ .compatible = "melfas,mms345l", .data = &mms345l_descriptor },
	{ }
};
MODULE_DEVICE_TABLE(of, mms114_dt_match);
#endif

static struct i2c_driver mms114_driver = {
	.driver = {
		.name	= "mms114",
		.pm	= pm_sleep_ptr(&mms114_pm_ops),
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
