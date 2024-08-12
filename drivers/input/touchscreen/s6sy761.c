// SPDX-License-Identifier: GPL-2.0
// Samsung S6SY761 Touchscreen device driver
//
// Copyright (c) 2017 Samsung Electronics Co., Ltd.
// Copyright (c) 2017 Andi Shyti <andi@etezian.org>

#include <asm/unaligned.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

/* commands */
#define S6SY761_SENSE_ON		0x10
#define S6SY761_SENSE_OFF		0x11
#define S6SY761_TOUCH_FUNCTION		0x30 /* R/W for get/set */
#define S6SY761_FIRMWARE_INTEGRITY	0x21
#define S6SY761_PANEL_INFO		0x23
#define S6SY761_DEVICE_ID		0x52
#define S6SY761_BOOT_STATUS		0x55
#define S6SY761_READ_ONE_EVENT		0x60
#define S6SY761_READ_ALL_EVENT		0x61
#define S6SY761_CLEAR_EVENT_STACK	0x62
#define S6SY761_APPLICATION_MODE	0xe4

/* events */
#define S6SY761_EVENT_INFO		0x02
#define S6SY761_EVENT_VENDOR_INFO	0x07

/* info */
#define S6SY761_INFO_BOOT_COMPLETE	0x00

/* firmware status */
#define S6SY761_FW_OK			0x80

/*
 * the functionalities are put as a reference
 * as in the device I am using none of them
 * works therefore not used in this driver yet.
 */
/* touchscreen functionalities */
#define S6SY761_MASK_TOUCH		BIT(0)
#define S6SY761_MASK_HOVER		BIT(1)
#define S6SY761_MASK_COVER		BIT(2)
#define S6SY761_MASK_GLOVE		BIT(3)
#define S6SY761_MASK_STYLUS		BIT(4)
#define S6SY761_MASK_PALM		BIT(5)
#define S6SY761_MASK_WET		BIT(6)
#define S6SY761_MASK_PROXIMITY		BIT(7)

/* boot status (BS) */
#define S6SY761_BS_BOOT_LOADER		0x10
#define S6SY761_BS_APPLICATION		0x20

/* event id */
#define S6SY761_EVENT_ID_COORDINATE	0x00
#define S6SY761_EVENT_ID_STATUS		0x01

/* event register masks */
#define S6SY761_MASK_TOUCH_STATE	0xc0 /* byte 0 */
#define S6SY761_MASK_TID		0x3c
#define S6SY761_MASK_EID		0x03
#define S6SY761_MASK_X			0xf0 /* byte 3 */
#define S6SY761_MASK_Y			0x0f
#define S6SY761_MASK_Z			0x3f /* byte 6 */
#define S6SY761_MASK_LEFT_EVENTS	0x3f /* byte 7 */
#define S6SY761_MASK_TOUCH_TYPE		0xc0 /* MSB in byte 6, LSB in byte 7 */

/* event touch state values */
#define S6SY761_TS_NONE			0x00
#define S6SY761_TS_PRESS		0x01
#define S6SY761_TS_MOVE			0x02
#define S6SY761_TS_RELEASE		0x03

/* application modes */
#define S6SY761_APP_NORMAL		0x0
#define S6SY761_APP_LOW_POWER		0x1
#define S6SY761_APP_TEST		0x2
#define S6SY761_APP_FLASH		0x3
#define S6SY761_APP_SLEEP		0x4

#define S6SY761_EVENT_SIZE		8
#define S6SY761_EVENT_COUNT		32
#define S6SY761_DEVID_SIZE		3
#define S6SY761_PANEL_ID_SIZE		11
#define S6SY761_TS_STATUS_SIZE		5
#define S6SY761_MAX_FINGERS		10

#define S6SY761_DEV_NAME	"s6sy761"

enum s6sy761_regulators {
	S6SY761_REGULATOR_VDD,
	S6SY761_REGULATOR_AVDD,
};

struct s6sy761_data {
	struct i2c_client *client;
	struct regulator_bulk_data regulators[2];
	struct input_dev *input;
	struct touchscreen_properties prop;

	u8 data[S6SY761_EVENT_SIZE * S6SY761_EVENT_COUNT];

	u16 devid;
	u8 tx_channel;
};

/*
 * We can't simply use i2c_smbus_read_i2c_block_data because we
 * need to read more than 255 bytes
 */
static int s6sy761_read_events(struct s6sy761_data *sdata, u16 n_events)
{
	u8 cmd = S6SY761_READ_ALL_EVENT;
	struct i2c_msg msgs[2] = {
		{
			.addr	= sdata->client->addr,
			.len	= 1,
			.buf	= &cmd,
		},
		{
			.addr	= sdata->client->addr,
			.flags	= I2C_M_RD,
			.len	= (n_events * S6SY761_EVENT_SIZE),
			.buf	= sdata->data + S6SY761_EVENT_SIZE,
		},
	};
	int ret;

	ret = i2c_transfer(sdata->client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0)
		return ret;

	return ret == ARRAY_SIZE(msgs) ? 0 : -EIO;
}

static void s6sy761_report_coordinates(struct s6sy761_data *sdata,
				       u8 *event, u8 tid)
{
	u8 major = event[4];
	u8 minor = event[5];
	u8 z = event[6] & S6SY761_MASK_Z;
	u16 x = (event[1] << 4) | ((event[3] & S6SY761_MASK_X) >> 4);
	u16 y = (event[2] << 4) | (event[3] & S6SY761_MASK_Y);

	input_mt_slot(sdata->input, tid);

	input_mt_report_slot_state(sdata->input, MT_TOOL_FINGER, true);
	input_report_abs(sdata->input, ABS_MT_POSITION_X, x);
	input_report_abs(sdata->input, ABS_MT_POSITION_Y, y);
	input_report_abs(sdata->input, ABS_MT_TOUCH_MAJOR, major);
	input_report_abs(sdata->input, ABS_MT_TOUCH_MINOR, minor);
	input_report_abs(sdata->input, ABS_MT_PRESSURE, z);

	input_sync(sdata->input);
}

static void s6sy761_report_release(struct s6sy761_data *sdata,
				   u8 *event, u8 tid)
{
	input_mt_slot(sdata->input, tid);
	input_mt_report_slot_state(sdata->input, MT_TOOL_FINGER, false);

	input_sync(sdata->input);
}

static void s6sy761_handle_coordinates(struct s6sy761_data *sdata, u8 *event)
{
	u8 tid;
	u8 touch_state;

	if (unlikely(!(event[0] & S6SY761_MASK_TID)))
		return;

	tid = ((event[0] & S6SY761_MASK_TID) >> 2) - 1;
	touch_state = (event[0] & S6SY761_MASK_TOUCH_STATE) >> 6;

	switch (touch_state) {

	case S6SY761_TS_NONE:
		break;
	case S6SY761_TS_RELEASE:
		s6sy761_report_release(sdata, event, tid);
		break;
	case S6SY761_TS_PRESS:
	case S6SY761_TS_MOVE:
		s6sy761_report_coordinates(sdata, event, tid);
		break;
	}
}

static void s6sy761_handle_events(struct s6sy761_data *sdata, u8 n_events)
{
	int i;

	for (i = 0; i < n_events; i++) {
		u8 *event = &sdata->data[i * S6SY761_EVENT_SIZE];
		u8 event_id = event[0] & S6SY761_MASK_EID;

		if (!event[0])
			return;

		switch (event_id) {

		case S6SY761_EVENT_ID_COORDINATE:
			s6sy761_handle_coordinates(sdata, event);
			break;

		case S6SY761_EVENT_ID_STATUS:
			break;

		default:
			break;
		}
	}
}

static irqreturn_t s6sy761_irq_handler(int irq, void *dev)
{
	struct s6sy761_data *sdata = dev;
	int ret;
	u8 n_events;

	ret = i2c_smbus_read_i2c_block_data(sdata->client,
					    S6SY761_READ_ONE_EVENT,
					    S6SY761_EVENT_SIZE,
					    sdata->data);
	if (ret < 0) {
		dev_err(&sdata->client->dev, "failed to read events\n");
		return IRQ_HANDLED;
	}

	if (!sdata->data[0])
		return IRQ_HANDLED;

	n_events = sdata->data[7] & S6SY761_MASK_LEFT_EVENTS;
	if (unlikely(n_events > S6SY761_EVENT_COUNT - 1))
		return IRQ_HANDLED;

	if (n_events) {
		ret = s6sy761_read_events(sdata, n_events);
		if (ret < 0) {
			dev_err(&sdata->client->dev, "failed to read events\n");
			return IRQ_HANDLED;
		}
	}

	s6sy761_handle_events(sdata, n_events +  1);

	return IRQ_HANDLED;
}

static int s6sy761_input_open(struct input_dev *dev)
{
	struct s6sy761_data *sdata = input_get_drvdata(dev);

	return i2c_smbus_write_byte(sdata->client, S6SY761_SENSE_ON);
}

static void s6sy761_input_close(struct input_dev *dev)
{
	struct s6sy761_data *sdata = input_get_drvdata(dev);
	int ret;

	ret = i2c_smbus_write_byte(sdata->client, S6SY761_SENSE_OFF);
	if (ret)
		dev_err(&sdata->client->dev, "failed to turn off sensing\n");
}

static ssize_t s6sy761_sysfs_devid(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct s6sy761_data *sdata = dev_get_drvdata(dev);

	return sprintf(buf, "%#x\n", sdata->devid);
}

static DEVICE_ATTR(devid, 0444, s6sy761_sysfs_devid, NULL);

static struct attribute *s6sy761_sysfs_attrs[] = {
	&dev_attr_devid.attr,
	NULL
};
ATTRIBUTE_GROUPS(s6sy761_sysfs);

static int s6sy761_power_on(struct s6sy761_data *sdata)
{
	u8 buffer[S6SY761_EVENT_SIZE];
	u8 event;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(sdata->regulators),
				    sdata->regulators);
	if (ret)
		return ret;

	msleep(140);

	/* double check whether the touch is functional */
	ret = i2c_smbus_read_i2c_block_data(sdata->client,
					    S6SY761_READ_ONE_EVENT,
					    S6SY761_EVENT_SIZE,
					    buffer);
	if (ret < 0)
		return ret;

	event = (buffer[0] >> 2) & 0xf;

	if ((event != S6SY761_EVENT_INFO &&
	     event != S6SY761_EVENT_VENDOR_INFO) ||
	    buffer[1] != S6SY761_INFO_BOOT_COMPLETE) {
		return -ENODEV;
	}

	ret = i2c_smbus_read_byte_data(sdata->client, S6SY761_BOOT_STATUS);
	if (ret < 0)
		return ret;

	/* for some reasons the device might be stuck in the bootloader */
	if (ret != S6SY761_BS_APPLICATION)
		return -ENODEV;

	/* enable touch functionality */
	ret = i2c_smbus_write_word_data(sdata->client,
					S6SY761_TOUCH_FUNCTION,
					S6SY761_MASK_TOUCH);
	if (ret)
		return ret;

	return 0;
}

static int s6sy761_hw_init(struct s6sy761_data *sdata,
			   unsigned int *max_x, unsigned int *max_y)
{
	u8 buffer[S6SY761_PANEL_ID_SIZE]; /* larger read size */
	int ret;

	ret = s6sy761_power_on(sdata);
	if (ret)
		return ret;

	ret = i2c_smbus_read_i2c_block_data(sdata->client,
					    S6SY761_DEVICE_ID,
					    S6SY761_DEVID_SIZE,
					    buffer);
	if (ret < 0)
		return ret;

	sdata->devid = get_unaligned_be16(buffer + 1);

	ret = i2c_smbus_read_i2c_block_data(sdata->client,
					    S6SY761_PANEL_INFO,
					    S6SY761_PANEL_ID_SIZE,
					    buffer);
	if (ret < 0)
		return ret;

	*max_x = get_unaligned_be16(buffer);
	*max_y = get_unaligned_be16(buffer + 2);

	/* if no tx channels defined, at least keep one */
	sdata->tx_channel = max_t(u8, buffer[8], 1);

	ret = i2c_smbus_read_byte_data(sdata->client,
				       S6SY761_FIRMWARE_INTEGRITY);
	if (ret < 0)
		return ret;
	else if (ret != S6SY761_FW_OK)
		return -ENODEV;

	return 0;
}

static void s6sy761_power_off(void *data)
{
	struct s6sy761_data *sdata = data;

	disable_irq(sdata->client->irq);
	regulator_bulk_disable(ARRAY_SIZE(sdata->regulators),
						sdata->regulators);
}

static int s6sy761_probe(struct i2c_client *client)
{
	struct s6sy761_data *sdata;
	unsigned int max_x, max_y;
	int err;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C |
						I2C_FUNC_SMBUS_BYTE_DATA |
						I2C_FUNC_SMBUS_I2C_BLOCK))
		return -ENODEV;

	sdata = devm_kzalloc(&client->dev, sizeof(*sdata), GFP_KERNEL);
	if (!sdata)
		return -ENOMEM;

	i2c_set_clientdata(client, sdata);
	sdata->client = client;

	sdata->regulators[S6SY761_REGULATOR_VDD].supply = "vdd";
	sdata->regulators[S6SY761_REGULATOR_AVDD].supply = "avdd";
	err = devm_regulator_bulk_get(&client->dev,
				      ARRAY_SIZE(sdata->regulators),
				      sdata->regulators);
	if (err)
		return err;

	err = devm_add_action_or_reset(&client->dev, s6sy761_power_off, sdata);
	if (err)
		return err;

	err = s6sy761_hw_init(sdata, &max_x, &max_y);
	if (err)
		return err;

	sdata->input = devm_input_allocate_device(&client->dev);
	if (!sdata->input)
		return -ENOMEM;

	sdata->input->name = S6SY761_DEV_NAME;
	sdata->input->id.bustype = BUS_I2C;
	sdata->input->open = s6sy761_input_open;
	sdata->input->close = s6sy761_input_close;

	input_set_abs_params(sdata->input, ABS_MT_POSITION_X, 0, max_x, 0, 0);
	input_set_abs_params(sdata->input, ABS_MT_POSITION_Y, 0, max_y, 0, 0);
	input_set_abs_params(sdata->input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(sdata->input, ABS_MT_TOUCH_MINOR, 0, 255, 0, 0);
	input_set_abs_params(sdata->input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(sdata->input, ABS_MT_TOUCH_MINOR, 0, 255, 0, 0);
	input_set_abs_params(sdata->input, ABS_MT_PRESSURE, 0, 255, 0, 0);

	touchscreen_parse_properties(sdata->input, true, &sdata->prop);

	if (!input_abs_get_max(sdata->input, ABS_X) ||
	    !input_abs_get_max(sdata->input, ABS_Y)) {
		dev_warn(&client->dev, "the axis have not been set\n");
	}

	err = input_mt_init_slots(sdata->input, sdata->tx_channel,
				  INPUT_MT_DIRECT);
	if (err)
		return err;

	input_set_drvdata(sdata->input, sdata);

	err = input_register_device(sdata->input);
	if (err)
		return err;

	err = devm_request_threaded_irq(&client->dev, client->irq, NULL,
					s6sy761_irq_handler,
					IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					"s6sy761_irq", sdata);
	if (err)
		return err;

	pm_runtime_enable(&client->dev);

	return 0;
}

static void s6sy761_remove(struct i2c_client *client)
{
	pm_runtime_disable(&client->dev);
}

static int s6sy761_runtime_suspend(struct device *dev)
{
	struct s6sy761_data *sdata = dev_get_drvdata(dev);

	return i2c_smbus_write_byte_data(sdata->client,
				S6SY761_APPLICATION_MODE, S6SY761_APP_SLEEP);
}

static int s6sy761_runtime_resume(struct device *dev)
{
	struct s6sy761_data *sdata = dev_get_drvdata(dev);

	return i2c_smbus_write_byte_data(sdata->client,
				S6SY761_APPLICATION_MODE, S6SY761_APP_NORMAL);
}

static int s6sy761_suspend(struct device *dev)
{
	struct s6sy761_data *sdata = dev_get_drvdata(dev);

	s6sy761_power_off(sdata);

	return 0;
}

static int s6sy761_resume(struct device *dev)
{
	struct s6sy761_data *sdata = dev_get_drvdata(dev);

	enable_irq(sdata->client->irq);

	return s6sy761_power_on(sdata);
}

static const struct dev_pm_ops s6sy761_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(s6sy761_suspend, s6sy761_resume)
	RUNTIME_PM_OPS(s6sy761_runtime_suspend, s6sy761_runtime_resume, NULL)
};

#ifdef CONFIG_OF
static const struct of_device_id s6sy761_of_match[] = {
	{ .compatible = "samsung,s6sy761", },
	{ },
};
MODULE_DEVICE_TABLE(of, s6sy761_of_match);
#endif

static const struct i2c_device_id s6sy761_id[] = {
	{ "s6sy761" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, s6sy761_id);

static struct i2c_driver s6sy761_driver = {
	.driver = {
		.name = S6SY761_DEV_NAME,
		.dev_groups = s6sy761_sysfs_groups,
		.of_match_table = of_match_ptr(s6sy761_of_match),
		.pm = pm_ptr(&s6sy761_pm_ops),
	},
	.probe = s6sy761_probe,
	.remove = s6sy761_remove,
	.id_table = s6sy761_id,
};

module_i2c_driver(s6sy761_driver);

MODULE_AUTHOR("Andi Shyti <andi.shyti@samsung.com>");
MODULE_DESCRIPTION("Samsung S6SY761 Touch Screen");
MODULE_LICENSE("GPL v2");
