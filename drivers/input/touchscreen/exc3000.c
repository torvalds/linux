// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for I2C connected EETI EXC3000 multiple touch controller
 *
 * Copyright (C) 2017 Ahmet Inan <inan@distec.de>
 *
 * minimal implementation based on egalax_ts.c and egalax_i2c.c
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/sizes.h>
#include <linux/timer.h>
#include <asm/unaligned.h>

#define EXC3000_NUM_SLOTS		10
#define EXC3000_SLOTS_PER_FRAME		5
#define EXC3000_LEN_FRAME		66
#define EXC3000_LEN_POINT		10

#define EXC3000_LEN_MODEL_NAME		16
#define EXC3000_LEN_FW_VERSION		16

#define EXC3000_MT1_EVENT		0x06
#define EXC3000_MT2_EVENT		0x18

#define EXC3000_TIMEOUT_MS		100

#define EXC3000_RESET_MS		10
#define EXC3000_READY_MS		100

static const struct i2c_device_id exc3000_id[];

struct eeti_dev_info {
	const char *name;
	int max_xy;
};

enum eeti_dev_id {
	EETI_EXC3000,
	EETI_EXC80H60,
	EETI_EXC80H84,
};

static struct eeti_dev_info exc3000_info[] = {
	[EETI_EXC3000] = {
		.name = "EETI EXC3000 Touch Screen",
		.max_xy = SZ_4K - 1,
	},
	[EETI_EXC80H60] = {
		.name = "EETI EXC80H60 Touch Screen",
		.max_xy = SZ_16K - 1,
	},
	[EETI_EXC80H84] = {
		.name = "EETI EXC80H84 Touch Screen",
		.max_xy = SZ_16K - 1,
	},
};

struct exc3000_data {
	struct i2c_client *client;
	const struct eeti_dev_info *info;
	struct input_dev *input;
	struct touchscreen_properties prop;
	struct gpio_desc *reset;
	struct timer_list timer;
	u8 buf[2 * EXC3000_LEN_FRAME];
	struct completion wait_event;
	struct mutex query_lock;
	int query_result;
	char model[EXC3000_LEN_MODEL_NAME];
	char fw_version[EXC3000_LEN_FW_VERSION];
};

static void exc3000_report_slots(struct input_dev *input,
				 struct touchscreen_properties *prop,
				 const u8 *buf, int num)
{
	for (; num--; buf += EXC3000_LEN_POINT) {
		if (buf[0] & BIT(0)) {
			input_mt_slot(input, buf[1]);
			input_mt_report_slot_state(input, MT_TOOL_FINGER, true);
			touchscreen_report_pos(input, prop,
					       get_unaligned_le16(buf + 2),
					       get_unaligned_le16(buf + 4),
					       true);
		}
	}
}

static void exc3000_timer(struct timer_list *t)
{
	struct exc3000_data *data = from_timer(data, t, timer);

	input_mt_sync_frame(data->input);
	input_sync(data->input);
}

static int exc3000_read_frame(struct exc3000_data *data, u8 *buf)
{
	struct i2c_client *client = data->client;
	u8 expected_event = EXC3000_MT1_EVENT;
	int ret;

	if (data->info->max_xy == SZ_16K - 1)
		expected_event = EXC3000_MT2_EVENT;

	ret = i2c_master_send(client, "'", 2);
	if (ret < 0)
		return ret;

	if (ret != 2)
		return -EIO;

	ret = i2c_master_recv(client, buf, EXC3000_LEN_FRAME);
	if (ret < 0)
		return ret;

	if (ret != EXC3000_LEN_FRAME)
		return -EIO;

	if (get_unaligned_le16(buf) != EXC3000_LEN_FRAME)
		return -EINVAL;

	if (buf[2] != expected_event)
		return -EINVAL;

	return 0;
}

static int exc3000_read_data(struct exc3000_data *data,
			     u8 *buf, int *n_slots)
{
	int error;

	error = exc3000_read_frame(data, buf);
	if (error)
		return error;

	*n_slots = buf[3];
	if (!*n_slots || *n_slots > EXC3000_NUM_SLOTS)
		return -EINVAL;

	if (*n_slots > EXC3000_SLOTS_PER_FRAME) {
		/* Read 2nd frame to get the rest of the contacts. */
		error = exc3000_read_frame(data, buf + EXC3000_LEN_FRAME);
		if (error)
			return error;

		/* 2nd chunk must have number of contacts set to 0. */
		if (buf[EXC3000_LEN_FRAME + 3] != 0)
			return -EINVAL;
	}

	return 0;
}

static int exc3000_query_interrupt(struct exc3000_data *data)
{
	u8 *buf = data->buf;
	int error;

	error = i2c_master_recv(data->client, buf, EXC3000_LEN_FRAME);
	if (error < 0)
		return error;

	if (buf[0] != 'B')
		return -EPROTO;

	if (buf[4] == 'E')
		strlcpy(data->model, buf + 5, sizeof(data->model));
	else if (buf[4] == 'D')
		strlcpy(data->fw_version, buf + 5, sizeof(data->fw_version));
	else
		return -EPROTO;

	return 0;
}

static irqreturn_t exc3000_interrupt(int irq, void *dev_id)
{
	struct exc3000_data *data = dev_id;
	struct input_dev *input = data->input;
	u8 *buf = data->buf;
	int slots, total_slots;
	int error;

	if (mutex_is_locked(&data->query_lock)) {
		data->query_result = exc3000_query_interrupt(data);
		complete(&data->wait_event);
		goto out;
	}

	error = exc3000_read_data(data, buf, &total_slots);
	if (error) {
		/* Schedule a timer to release "stuck" contacts */
		mod_timer(&data->timer,
			  jiffies + msecs_to_jiffies(EXC3000_TIMEOUT_MS));
		goto out;
	}

	/*
	 * We read full state successfully, no contacts will be "stuck".
	 */
	del_timer_sync(&data->timer);

	while (total_slots > 0) {
		slots = min(total_slots, EXC3000_SLOTS_PER_FRAME);
		exc3000_report_slots(input, &data->prop, buf + 4, slots);
		total_slots -= slots;
		buf += EXC3000_LEN_FRAME;
	}

	input_mt_sync_frame(input);
	input_sync(input);

out:
	return IRQ_HANDLED;
}

static ssize_t fw_version_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct exc3000_data *data = i2c_get_clientdata(client);
	static const u8 request[68] = {
		0x67, 0x00, 0x42, 0x00, 0x03, 0x01, 'D', 0x00
	};
	int error;

	mutex_lock(&data->query_lock);

	data->query_result = -ETIMEDOUT;
	reinit_completion(&data->wait_event);

	error = i2c_master_send(client, request, sizeof(request));
	if (error < 0) {
		mutex_unlock(&data->query_lock);
		return error;
	}

	wait_for_completion_interruptible_timeout(&data->wait_event, 1 * HZ);
	mutex_unlock(&data->query_lock);

	if (data->query_result < 0)
		return data->query_result;

	return sprintf(buf, "%s\n", data->fw_version);
}
static DEVICE_ATTR_RO(fw_version);

static ssize_t exc3000_get_model(struct exc3000_data *data)
{
	static const u8 request[68] = {
		0x67, 0x00, 0x42, 0x00, 0x03, 0x01, 'E', 0x00
	};
	struct i2c_client *client = data->client;
	int error;

	mutex_lock(&data->query_lock);
	data->query_result = -ETIMEDOUT;
	reinit_completion(&data->wait_event);

	error = i2c_master_send(client, request, sizeof(request));
	if (error < 0) {
		mutex_unlock(&data->query_lock);
		return error;
	}

	wait_for_completion_interruptible_timeout(&data->wait_event, 1 * HZ);
	mutex_unlock(&data->query_lock);

	return data->query_result;
}

static ssize_t model_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct exc3000_data *data = i2c_get_clientdata(client);
	int error;

	error = exc3000_get_model(data);
	if (error < 0)
		return error;

	return sprintf(buf, "%s\n", data->model);
}
static DEVICE_ATTR_RO(model);

static struct attribute *sysfs_attrs[] = {
	&dev_attr_fw_version.attr,
	&dev_attr_model.attr,
	NULL
};

static struct attribute_group exc3000_attribute_group = {
	.attrs = sysfs_attrs
};

static int exc3000_probe(struct i2c_client *client)
{
	struct exc3000_data *data;
	struct input_dev *input;
	int error, max_xy, retry;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	data->info = device_get_match_data(&client->dev);
	if (!data->info) {
		enum eeti_dev_id eeti_dev_id =
			i2c_match_id(exc3000_id, client)->driver_data;
		data->info = &exc3000_info[eeti_dev_id];
	}
	timer_setup(&data->timer, exc3000_timer, 0);
	init_completion(&data->wait_event);
	mutex_init(&data->query_lock);

	data->reset = devm_gpiod_get_optional(&client->dev, "reset",
					      GPIOD_OUT_HIGH);
	if (IS_ERR(data->reset))
		return PTR_ERR(data->reset);

	if (data->reset) {
		msleep(EXC3000_RESET_MS);
		gpiod_set_value_cansleep(data->reset, 0);
		msleep(EXC3000_READY_MS);
	}

	input = devm_input_allocate_device(&client->dev);
	if (!input)
		return -ENOMEM;

	data->input = input;
	input_set_drvdata(input, data);

	input->name = data->info->name;
	input->id.bustype = BUS_I2C;

	max_xy = data->info->max_xy;
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, max_xy, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, max_xy, 0, 0);

	touchscreen_parse_properties(input, true, &data->prop);

	error = input_mt_init_slots(input, EXC3000_NUM_SLOTS,
				    INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (error)
		return error;

	error = input_register_device(input);
	if (error)
		return error;

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, exc3000_interrupt, IRQF_ONESHOT,
					  client->name, data);
	if (error)
		return error;

	/*
	 * I²C does not have built-in recovery, so retry on failure. This
	 * ensures, that the device probe will not fail for temporary issues
	 * on the bus.  This is not needed for the sysfs calls (userspace
	 * will receive the error code and can start another query) and
	 * cannot be done for touch events (but that only means loosing one
	 * or two touch events anyways).
	 */
	for (retry = 0; retry < 3; retry++) {
		error = exc3000_get_model(data);
		if (!error)
			break;
		dev_warn(&client->dev, "Retry %d get EETI EXC3000 model: %d\n",
			 retry + 1, error);
	}

	if (error)
		return error;

	dev_dbg(&client->dev, "TS Model: %s", data->model);

	i2c_set_clientdata(client, data);

	error = devm_device_add_group(&client->dev, &exc3000_attribute_group);
	if (error)
		return error;

	return 0;
}

static const struct i2c_device_id exc3000_id[] = {
	{ "exc3000", EETI_EXC3000 },
	{ "exc80h60", EETI_EXC80H60 },
	{ "exc80h84", EETI_EXC80H84 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, exc3000_id);

#ifdef CONFIG_OF
static const struct of_device_id exc3000_of_match[] = {
	{ .compatible = "eeti,exc3000", .data = &exc3000_info[EETI_EXC3000] },
	{ .compatible = "eeti,exc80h60", .data = &exc3000_info[EETI_EXC80H60] },
	{ .compatible = "eeti,exc80h84", .data = &exc3000_info[EETI_EXC80H84] },
	{ }
};
MODULE_DEVICE_TABLE(of, exc3000_of_match);
#endif

static struct i2c_driver exc3000_driver = {
	.driver = {
		.name	= "exc3000",
		.of_match_table = of_match_ptr(exc3000_of_match),
	},
	.id_table	= exc3000_id,
	.probe_new	= exc3000_probe,
};

module_i2c_driver(exc3000_driver);

MODULE_AUTHOR("Ahmet Inan <inan@distec.de>");
MODULE_DESCRIPTION("I2C connected EETI EXC3000 multiple touch controller driver");
MODULE_LICENSE("GPL v2");
