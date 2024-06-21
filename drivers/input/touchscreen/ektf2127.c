// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for ELAN eKTF2127 i2c touchscreen controller
 *
 * For this driver the layout of the Chipone icn8318 i2c
 * touchscreencontroller is used.
 *
 * Author:
 * Michel Verlaan <michel.verl@gmail.com>
 * Siebren Vroegindeweij <siebren.vroegindeweij@hotmail.com>
 *
 * Original chipone_icn8318 driver:
 * Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/bits.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/delay.h>

/* Packet header defines (first byte of data send / received) */
#define EKTF2127_NOISE			0x40
#define EKTF2127_RESPONSE		0x52
#define EKTF2127_REQUEST		0x53
#define EKTF2127_HELLO			0x55
#define EKTF2127_REPORT2		0x5a
#define EKTF2127_REPORT			0x5d
#define EKTF2127_CALIB_DONE		0x66

/* Register defines (second byte of data send / received) */
#define EKTF2127_ENV_NOISY		0x41
#define EKTF2127_HEIGHT			0x60
#define EKTF2127_WIDTH			0x63

/* 2 bytes header + 5 * 3 bytes coordinates + 3 bytes pressure info + footer */
#define EKTF2127_TOUCH_REPORT_SIZE	21
#define EKTF2127_MAX_TOUCHES		5

struct ektf2127_ts {
	struct i2c_client *client;
	struct input_dev *input;
	struct gpio_desc *power_gpios;
	struct touchscreen_properties prop;
	int status_shift;
};

struct ektf2127_i2c_chip_data {
	int status_shift;
};

static void ektf2127_parse_coordinates(const u8 *buf, unsigned int touch_count,
				       struct input_mt_pos *touches)
{
	int index = 0;
	int i;

	for (i = 0; i < touch_count; i++) {
		index = 2 + i * 3;

		touches[i].x = (buf[index] & 0x0f);
		touches[i].x <<= 8;
		touches[i].x |= buf[index + 2];

		touches[i].y = (buf[index] & 0xf0);
		touches[i].y <<= 4;
		touches[i].y |= buf[index + 1];
	}
}

static void ektf2127_report_event(struct ektf2127_ts *ts, const u8 *buf)
{
	struct input_mt_pos touches[EKTF2127_MAX_TOUCHES];
	int slots[EKTF2127_MAX_TOUCHES];
	unsigned int touch_count, i;

	touch_count = buf[1] & 0x07;
	if (touch_count > EKTF2127_MAX_TOUCHES) {
		dev_err(&ts->client->dev,
			"Too many touches %d > %d\n",
			touch_count, EKTF2127_MAX_TOUCHES);
		touch_count = EKTF2127_MAX_TOUCHES;
	}

	ektf2127_parse_coordinates(buf, touch_count, touches);
	input_mt_assign_slots(ts->input, slots, touches,
			      touch_count, 0);

	for (i = 0; i < touch_count; i++) {
		input_mt_slot(ts->input, slots[i]);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, true);
		touchscreen_report_pos(ts->input, &ts->prop,
				       touches[i].x, touches[i].y, true);
	}

	input_mt_sync_frame(ts->input);
	input_sync(ts->input);
}

static void ektf2127_report2_contact(struct ektf2127_ts *ts, int slot,
				     const u8 *buf, bool active)
{
	input_mt_slot(ts->input, slot);
	input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, active);

	if (active) {
		int x = (buf[0] & 0xf0) << 4 | buf[1];
		int y = (buf[0] & 0x0f) << 8 | buf[2];

		touchscreen_report_pos(ts->input, &ts->prop, x, y, true);
	}
}

static void ektf2127_report2_event(struct ektf2127_ts *ts, const u8 *buf)
{
	ektf2127_report2_contact(ts, 0, &buf[1], !!(buf[7] & BIT(ts->status_shift)));
	ektf2127_report2_contact(ts, 1, &buf[4], !!(buf[7] & BIT(ts->status_shift + 1)));

	input_mt_sync_frame(ts->input);
	input_sync(ts->input);
}

static irqreturn_t ektf2127_irq(int irq, void *dev_id)
{
	struct ektf2127_ts *ts = dev_id;
	struct device *dev = &ts->client->dev;
	char buf[EKTF2127_TOUCH_REPORT_SIZE];
	int ret;

	ret = i2c_master_recv(ts->client, buf, EKTF2127_TOUCH_REPORT_SIZE);
	if (ret != EKTF2127_TOUCH_REPORT_SIZE) {
		dev_err(dev, "Error reading touch data: %d\n", ret);
		goto out;
	}

	switch (buf[0]) {
	case EKTF2127_REPORT:
		ektf2127_report_event(ts, buf);
		break;

	case EKTF2127_REPORT2:
		ektf2127_report2_event(ts, buf);
		break;

	case EKTF2127_NOISE:
		if (buf[1] == EKTF2127_ENV_NOISY)
			dev_dbg(dev, "Environment is electrically noisy\n");
		break;

	case EKTF2127_HELLO:
	case EKTF2127_CALIB_DONE:
		break;

	default:
		dev_err(dev, "Unexpected packet header byte %#02x\n", buf[0]);
		break;
	}

out:
	return IRQ_HANDLED;
}

static int ektf2127_start(struct input_dev *dev)
{
	struct ektf2127_ts *ts = input_get_drvdata(dev);

	enable_irq(ts->client->irq);
	gpiod_set_value_cansleep(ts->power_gpios, 1);

	return 0;
}

static void ektf2127_stop(struct input_dev *dev)
{
	struct ektf2127_ts *ts = input_get_drvdata(dev);

	disable_irq(ts->client->irq);
	gpiod_set_value_cansleep(ts->power_gpios, 0);
}

static int ektf2127_suspend(struct device *dev)
{
	struct ektf2127_ts *ts = i2c_get_clientdata(to_i2c_client(dev));

	mutex_lock(&ts->input->mutex);
	if (input_device_enabled(ts->input))
		ektf2127_stop(ts->input);
	mutex_unlock(&ts->input->mutex);

	return 0;
}

static int ektf2127_resume(struct device *dev)
{
	struct ektf2127_ts *ts = i2c_get_clientdata(to_i2c_client(dev));

	mutex_lock(&ts->input->mutex);
	if (input_device_enabled(ts->input))
		ektf2127_start(ts->input);
	mutex_unlock(&ts->input->mutex);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(ektf2127_pm_ops, ektf2127_suspend,
				ektf2127_resume);

static int ektf2127_query_dimension(struct i2c_client *client, bool width)
{
	struct device *dev = &client->dev;
	const char *what = width ? "width" : "height";
	u8 what_code = width ? EKTF2127_WIDTH : EKTF2127_HEIGHT;
	u8 buf[4];
	int ret;
	int error;

	/* Request dimension */
	buf[0] = EKTF2127_REQUEST;
	buf[1] = width ? EKTF2127_WIDTH : EKTF2127_HEIGHT;
	buf[2] = 0x00;
	buf[3] = 0x00;
	ret = i2c_master_send(client, buf, sizeof(buf));
	if (ret != sizeof(buf)) {
		error = ret < 0 ? ret : -EIO;
		dev_err(dev, "Failed to request %s: %d\n", what, error);
		return error;
	}

	msleep(20);

	/* Read response */
	ret = i2c_master_recv(client, buf, sizeof(buf));
	if (ret != sizeof(buf)) {
		error = ret < 0 ? ret : -EIO;
		dev_err(dev, "Failed to receive %s data: %d\n", what, error);
		return error;
	}

	if (buf[0] != EKTF2127_RESPONSE || buf[1] != what_code) {
		dev_err(dev, "Unexpected %s data: %#02x %#02x\n",
			what, buf[0], buf[1]);
		return -EIO;
	}

	return (((buf[3] & 0xf0) << 4) | buf[2]) - 1;
}

static int ektf2127_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	const struct ektf2127_i2c_chip_data *chip_data;
	struct ektf2127_ts *ts;
	struct input_dev *input;
	u8 buf[4];
	int max_x, max_y;
	int error;

	if (!client->irq) {
		dev_err(dev, "Error no irq specified\n");
		return -EINVAL;
	}

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	/* This requests the gpio *and* turns on the touchscreen controller */
	ts->power_gpios = devm_gpiod_get(dev, "power", GPIOD_OUT_HIGH);
	if (IS_ERR(ts->power_gpios))
		return dev_err_probe(dev, PTR_ERR(ts->power_gpios), "Error getting power gpio\n");

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	input->name = client->name;
	input->id.bustype = BUS_I2C;
	input->open = ektf2127_start;
	input->close = ektf2127_stop;

	ts->client = client;

	/* Read hello (ignore result, depends on initial power state) */
	msleep(20);
	i2c_master_recv(ts->client, buf, sizeof(buf));

	/* Read resolution from chip */
	max_x = ektf2127_query_dimension(client, true);
	if (max_x < 0)
		return max_x;

	max_y = ektf2127_query_dimension(client, false);
	if (max_y < 0)
		return max_y;

	input_set_abs_params(input, ABS_MT_POSITION_X, 0, max_x, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, max_y, 0, 0);
	touchscreen_parse_properties(input, true, &ts->prop);

	error = input_mt_init_slots(input, EKTF2127_MAX_TOUCHES,
				    INPUT_MT_DIRECT |
					INPUT_MT_DROP_UNUSED |
					INPUT_MT_TRACK);
	if (error)
		return error;

	ts->input = input;

	chip_data = i2c_get_match_data(client);
	if (!chip_data)
		return dev_err_probe(&client->dev, -EINVAL, "missing chip data\n");

	ts->status_shift = chip_data->status_shift;

	input_set_drvdata(input, ts);

	error = devm_request_threaded_irq(dev, client->irq,
					  NULL, ektf2127_irq,
					  IRQF_ONESHOT, client->name, ts);
	if (error) {
		dev_err(dev, "Error requesting irq: %d\n", error);
		return error;
	}

	/* Stop device till opened */
	ektf2127_stop(ts->input);

	error = input_register_device(input);
	if (error)
		return error;

	i2c_set_clientdata(client, ts);

	return 0;
}

static const struct ektf2127_i2c_chip_data ektf2127_data = {
	.status_shift = 1,
};

static const struct ektf2127_i2c_chip_data ektf2232_data = {
	.status_shift = 0,
};

#ifdef CONFIG_OF
static const struct of_device_id ektf2127_of_match[] = {
	{ .compatible = "elan,ektf2127", .data = &ektf2127_data},
	{ .compatible = "elan,ektf2132", .data = &ektf2127_data},
	{ .compatible = "elan,ektf2232", .data = &ektf2232_data},
	{}
};
MODULE_DEVICE_TABLE(of, ektf2127_of_match);
#endif

static const struct i2c_device_id ektf2127_i2c_id[] = {
	{ .name = "ektf2127", .driver_data = (long)&ektf2127_data },
	{ .name = "ektf2132", .driver_data = (long)&ektf2127_data },
	{ .name = "ektf2232", .driver_data = (long)&ektf2232_data },
	{}
};
MODULE_DEVICE_TABLE(i2c, ektf2127_i2c_id);

static struct i2c_driver ektf2127_driver = {
	.driver = {
		.name	= "elan_ektf2127",
		.pm	= pm_sleep_ptr(&ektf2127_pm_ops),
		.of_match_table = of_match_ptr(ektf2127_of_match),
	},
	.probe = ektf2127_probe,
	.id_table = ektf2127_i2c_id,
};
module_i2c_driver(ektf2127_driver);

MODULE_DESCRIPTION("ELAN eKTF2127/eKTF2132 I2C Touchscreen Driver");
MODULE_AUTHOR("Michel Verlaan, Siebren Vroegindeweij");
MODULE_LICENSE("GPL");
