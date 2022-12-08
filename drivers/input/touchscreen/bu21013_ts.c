// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Naveen Kumar G <naveen.gaddipati@stericsson.com> for ST-Ericsson
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>

#define MAX_FINGERS	2
#define RESET_DELAY	30
#define PENUP_TIMEOUT	(10)
#define DELTA_MIN	16
#define MASK_BITS	0x03
#define SHIFT_8		8
#define SHIFT_2		2
#define LENGTH_OF_BUFFER	11
#define I2C_RETRY_COUNT	5

#define BU21013_SENSORS_BTN_0_7_REG	0x70
#define BU21013_SENSORS_BTN_8_15_REG	0x71
#define BU21013_SENSORS_BTN_16_23_REG	0x72
#define BU21013_X1_POS_MSB_REG		0x73
#define BU21013_X1_POS_LSB_REG		0x74
#define BU21013_Y1_POS_MSB_REG		0x75
#define BU21013_Y1_POS_LSB_REG		0x76
#define BU21013_X2_POS_MSB_REG		0x77
#define BU21013_X2_POS_LSB_REG		0x78
#define BU21013_Y2_POS_MSB_REG		0x79
#define BU21013_Y2_POS_LSB_REG		0x7A
#define BU21013_INT_CLR_REG		0xE8
#define BU21013_INT_MODE_REG		0xE9
#define BU21013_GAIN_REG		0xEA
#define BU21013_OFFSET_MODE_REG		0xEB
#define BU21013_XY_EDGE_REG		0xEC
#define BU21013_RESET_REG		0xED
#define BU21013_CALIB_REG		0xEE
#define BU21013_DONE_REG		0xEF
#define BU21013_SENSOR_0_7_REG		0xF0
#define BU21013_SENSOR_8_15_REG		0xF1
#define BU21013_SENSOR_16_23_REG	0xF2
#define BU21013_POS_MODE1_REG		0xF3
#define BU21013_POS_MODE2_REG		0xF4
#define BU21013_CLK_MODE_REG		0xF5
#define BU21013_IDLE_REG		0xFA
#define BU21013_FILTER_REG		0xFB
#define BU21013_TH_ON_REG		0xFC
#define BU21013_TH_OFF_REG		0xFD


#define BU21013_RESET_ENABLE		0x01

#define BU21013_SENSORS_EN_0_7		0x3F
#define BU21013_SENSORS_EN_8_15		0xFC
#define BU21013_SENSORS_EN_16_23	0x1F

#define BU21013_POS_MODE1_0		0x02
#define BU21013_POS_MODE1_1		0x04
#define BU21013_POS_MODE1_2		0x08

#define BU21013_POS_MODE2_ZERO		0x01
#define BU21013_POS_MODE2_AVG1		0x02
#define BU21013_POS_MODE2_AVG2		0x04
#define BU21013_POS_MODE2_EN_XY		0x08
#define BU21013_POS_MODE2_EN_RAW	0x10
#define BU21013_POS_MODE2_MULTI		0x80

#define BU21013_CLK_MODE_DIV		0x01
#define BU21013_CLK_MODE_EXT		0x02
#define BU21013_CLK_MODE_CALIB		0x80

#define BU21013_IDLET_0			0x01
#define BU21013_IDLET_1			0x02
#define BU21013_IDLET_2			0x04
#define BU21013_IDLET_3			0x08
#define BU21013_IDLE_INTERMIT_EN	0x10

#define BU21013_DELTA_0_6	0x7F
#define BU21013_FILTER_EN	0x80

#define BU21013_INT_MODE_LEVEL	0x00
#define BU21013_INT_MODE_EDGE	0x01

#define BU21013_GAIN_0		0x01
#define BU21013_GAIN_1		0x02
#define BU21013_GAIN_2		0x04

#define BU21013_OFFSET_MODE_DEFAULT	0x00
#define BU21013_OFFSET_MODE_MOVE	0x01
#define BU21013_OFFSET_MODE_DISABLE	0x02

#define BU21013_TH_ON_0		0x01
#define BU21013_TH_ON_1		0x02
#define BU21013_TH_ON_2		0x04
#define BU21013_TH_ON_3		0x08
#define BU21013_TH_ON_4		0x10
#define BU21013_TH_ON_5		0x20
#define BU21013_TH_ON_6		0x40
#define BU21013_TH_ON_7		0x80
#define BU21013_TH_ON_MAX	0xFF

#define BU21013_TH_OFF_0	0x01
#define BU21013_TH_OFF_1	0x02
#define BU21013_TH_OFF_2	0x04
#define BU21013_TH_OFF_3	0x08
#define BU21013_TH_OFF_4	0x10
#define BU21013_TH_OFF_5	0x20
#define BU21013_TH_OFF_6	0x40
#define BU21013_TH_OFF_7	0x80
#define BU21013_TH_OFF_MAX	0xFF

#define BU21013_X_EDGE_0	0x01
#define BU21013_X_EDGE_1	0x02
#define BU21013_X_EDGE_2	0x04
#define BU21013_X_EDGE_3	0x08
#define BU21013_Y_EDGE_0	0x10
#define BU21013_Y_EDGE_1	0x20
#define BU21013_Y_EDGE_2	0x40
#define BU21013_Y_EDGE_3	0x80

#define BU21013_DONE	0x01
#define BU21013_NUMBER_OF_X_SENSORS	(6)
#define BU21013_NUMBER_OF_Y_SENSORS	(11)

#define DRIVER_TP	"bu21013_tp"

/**
 * struct bu21013_ts - touch panel data structure
 * @client: pointer to the i2c client
 * @in_dev: pointer to the input device structure
 * @props: the device coordinate transformation properties
 * @regulator: pointer to the Regulator used for touch screen
 * @cs_gpiod: chip select GPIO line
 * @int_gpiod: touch interrupt GPIO line
 * @touch_x_max: maximum X coordinate reported by the device
 * @touch_y_max: maximum Y coordinate reported by the device
 * @x_flip: indicates that the driver should invert X coordinate before
 *	reporting
 * @y_flip: indicates that the driver should invert Y coordinate before
 *	reporting
 * @touch_stopped: touch stop flag
 *
 * Touch panel device data structure
 */
struct bu21013_ts {
	struct i2c_client *client;
	struct input_dev *in_dev;
	struct touchscreen_properties props;
	struct regulator *regulator;
	struct gpio_desc *cs_gpiod;
	struct gpio_desc *int_gpiod;
	u32 touch_x_max;
	u32 touch_y_max;
	bool x_flip;
	bool y_flip;
	bool touch_stopped;
};

static int bu21013_read_block_data(struct bu21013_ts *ts, u8 *buf)
{
	int ret, i;

	for (i = 0; i < I2C_RETRY_COUNT; i++) {
		ret = i2c_smbus_read_i2c_block_data(ts->client,
						    BU21013_SENSORS_BTN_0_7_REG,
						    LENGTH_OF_BUFFER, buf);
		if (ret == LENGTH_OF_BUFFER)
			return 0;
	}

	return -EINVAL;
}

static int bu21013_do_touch_report(struct bu21013_ts *ts)
{
	struct input_dev *input = ts->in_dev;
	struct input_mt_pos pos[MAX_FINGERS];
	int slots[MAX_FINGERS];
	u8 buf[LENGTH_OF_BUFFER];
	bool has_x_sensors, has_y_sensors;
	int finger_down_count = 0;
	int i;

	if (bu21013_read_block_data(ts, buf) < 0)
		return -EINVAL;

	has_x_sensors = hweight32(buf[0] & BU21013_SENSORS_EN_0_7);
	has_y_sensors = hweight32(((buf[1] & BU21013_SENSORS_EN_8_15) |
		((buf[2] & BU21013_SENSORS_EN_16_23) << SHIFT_8)) >> SHIFT_2);
	if (!has_x_sensors || !has_y_sensors)
		return 0;

	for (i = 0; i < MAX_FINGERS; i++) {
		const u8 *data = &buf[4 * i + 3];
		unsigned int x, y;

		x = data[0] << SHIFT_2 | (data[1] & MASK_BITS);
		y = data[2] << SHIFT_2 | (data[3] & MASK_BITS);
		if (x != 0 && y != 0)
			touchscreen_set_mt_pos(&pos[finger_down_count++],
					       &ts->props, x, y);
	}

	if (finger_down_count == 2 &&
	    (abs(pos[0].x - pos[1].x) < DELTA_MIN ||
	     abs(pos[0].y - pos[1].y) < DELTA_MIN)) {
		return 0;
	}

	input_mt_assign_slots(input, slots, pos, finger_down_count, DELTA_MIN);
	for (i = 0; i < finger_down_count; i++) {
		input_mt_slot(input, slots[i]);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, true);
		input_report_abs(input, ABS_MT_POSITION_X, pos[i].x);
		input_report_abs(input, ABS_MT_POSITION_Y, pos[i].y);
	}

	input_mt_sync_frame(input);
	input_sync(input);

	return 0;
}

static irqreturn_t bu21013_gpio_irq(int irq, void *device_data)
{
	struct bu21013_ts *ts = device_data;
	int keep_polling;
	int error;

	do {
		error = bu21013_do_touch_report(ts);
		if (error) {
			dev_err(&ts->client->dev, "%s failed\n", __func__);
			break;
		}

		if (unlikely(ts->touch_stopped))
			break;

		keep_polling = ts->int_gpiod ?
			gpiod_get_value(ts->int_gpiod) : false;
		if (keep_polling)
			usleep_range(2000, 2500);
	} while (keep_polling);

	return IRQ_HANDLED;
}

static int bu21013_init_chip(struct bu21013_ts *ts)
{
	struct i2c_client *client = ts->client;
	int error;

	error = i2c_smbus_write_byte_data(client, BU21013_RESET_REG,
					  BU21013_RESET_ENABLE);
	if (error) {
		dev_err(&client->dev, "BU21013_RESET reg write failed\n");
		return error;
	}
	msleep(RESET_DELAY);

	error = i2c_smbus_write_byte_data(client, BU21013_SENSOR_0_7_REG,
					  BU21013_SENSORS_EN_0_7);
	if (error) {
		dev_err(&client->dev, "BU21013_SENSOR_0_7 reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_SENSOR_8_15_REG,
					  BU21013_SENSORS_EN_8_15);
	if (error) {
		dev_err(&client->dev, "BU21013_SENSOR_8_15 reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_SENSOR_16_23_REG,
					  BU21013_SENSORS_EN_16_23);
	if (error) {
		dev_err(&client->dev, "BU21013_SENSOR_16_23 reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_POS_MODE1_REG,
					  BU21013_POS_MODE1_0 |
						BU21013_POS_MODE1_1);
	if (error) {
		dev_err(&client->dev, "BU21013_POS_MODE1 reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_POS_MODE2_REG,
					  BU21013_POS_MODE2_ZERO |
						BU21013_POS_MODE2_AVG1 |
						BU21013_POS_MODE2_AVG2 |
						BU21013_POS_MODE2_EN_RAW |
						BU21013_POS_MODE2_MULTI);
	if (error) {
		dev_err(&client->dev, "BU21013_POS_MODE2 reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_CLK_MODE_REG,
					  BU21013_CLK_MODE_DIV |
						BU21013_CLK_MODE_CALIB);
	if (error) {
		dev_err(&client->dev, "BU21013_CLK_MODE reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_IDLE_REG,
					  BU21013_IDLET_0 |
						BU21013_IDLE_INTERMIT_EN);
	if (error) {
		dev_err(&client->dev, "BU21013_IDLE reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_INT_MODE_REG,
					  BU21013_INT_MODE_LEVEL);
	if (error) {
		dev_err(&client->dev, "BU21013_INT_MODE reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_FILTER_REG,
					  BU21013_DELTA_0_6 |
						BU21013_FILTER_EN);
	if (error) {
		dev_err(&client->dev, "BU21013_FILTER reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_TH_ON_REG,
					  BU21013_TH_ON_5);
	if (error) {
		dev_err(&client->dev, "BU21013_TH_ON reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_TH_OFF_REG,
					  BU21013_TH_OFF_4 | BU21013_TH_OFF_3);
	if (error) {
		dev_err(&client->dev, "BU21013_TH_OFF reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_GAIN_REG,
					  BU21013_GAIN_0 | BU21013_GAIN_1);
	if (error) {
		dev_err(&client->dev, "BU21013_GAIN reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_OFFSET_MODE_REG,
					  BU21013_OFFSET_MODE_DEFAULT);
	if (error) {
		dev_err(&client->dev, "BU21013_OFFSET_MODE reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_XY_EDGE_REG,
					  BU21013_X_EDGE_0 |
						BU21013_X_EDGE_2 |
						BU21013_Y_EDGE_1 |
						BU21013_Y_EDGE_3);
	if (error) {
		dev_err(&client->dev, "BU21013_XY_EDGE reg write failed\n");
		return error;
	}

	error = i2c_smbus_write_byte_data(client, BU21013_DONE_REG,
					  BU21013_DONE);
	if (error) {
		dev_err(&client->dev, "BU21013_REG_DONE reg write failed\n");
		return error;
	}

	return 0;
}

static void bu21013_power_off(void *_ts)
{
	struct bu21013_ts *ts = _ts;

	regulator_disable(ts->regulator);
}

static void bu21013_disable_chip(void *_ts)
{
	struct bu21013_ts *ts = _ts;

	gpiod_set_value(ts->cs_gpiod, 0);
}

static int bu21013_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct bu21013_ts *ts;
	struct input_dev *in_dev;
	struct input_absinfo *info;
	u32 max_x = 0, max_y = 0;
	int error;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "i2c smbus byte data not supported\n");
		return -EIO;
	}

	if (!client->irq) {
		dev_err(&client->dev, "No IRQ set up\n");
		return -EINVAL;
	}

	ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->client = client;

	ts->x_flip = device_property_read_bool(&client->dev, "rohm,flip-x");
	ts->y_flip = device_property_read_bool(&client->dev, "rohm,flip-y");

	in_dev = devm_input_allocate_device(&client->dev);
	if (!in_dev) {
		dev_err(&client->dev, "device memory alloc failed\n");
		return -ENOMEM;
	}
	ts->in_dev = in_dev;
	input_set_drvdata(in_dev, ts);

	/* register the device to input subsystem */
	in_dev->name = DRIVER_TP;
	in_dev->id.bustype = BUS_I2C;

	device_property_read_u32(&client->dev, "rohm,touch-max-x", &max_x);
	device_property_read_u32(&client->dev, "rohm,touch-max-y", &max_y);

	input_set_abs_params(in_dev, ABS_MT_POSITION_X, 0, max_x, 0, 0);
	input_set_abs_params(in_dev, ABS_MT_POSITION_Y, 0, max_y, 0, 0);

	touchscreen_parse_properties(in_dev, true, &ts->props);

	/* Adjust for the legacy "flip" properties, if present */
	if (!ts->props.invert_x &&
	    device_property_read_bool(&client->dev, "rohm,flip-x")) {
		info = &in_dev->absinfo[ABS_MT_POSITION_X];
		info->maximum -= info->minimum;
		info->minimum = 0;
	}

	if (!ts->props.invert_y &&
	    device_property_read_bool(&client->dev, "rohm,flip-y")) {
		info = &in_dev->absinfo[ABS_MT_POSITION_Y];
		info->maximum -= info->minimum;
		info->minimum = 0;
	}

	error = input_mt_init_slots(in_dev, MAX_FINGERS,
				    INPUT_MT_DIRECT | INPUT_MT_TRACK |
					INPUT_MT_DROP_UNUSED);
	if (error) {
		dev_err(&client->dev, "failed to initialize MT slots");
		return error;
	}

	ts->regulator = devm_regulator_get(&client->dev, "avdd");
	if (IS_ERR(ts->regulator)) {
		dev_err(&client->dev, "regulator_get failed\n");
		return PTR_ERR(ts->regulator);
	}

	error = regulator_enable(ts->regulator);
	if (error) {
		dev_err(&client->dev, "regulator enable failed\n");
		return error;
	}

	error = devm_add_action_or_reset(&client->dev, bu21013_power_off, ts);
	if (error) {
		dev_err(&client->dev, "failed to install power off handler\n");
		return error;
	}

	/* Named "CS" on the chip, DT binding is "reset" */
	ts->cs_gpiod = devm_gpiod_get(&client->dev, "reset", GPIOD_OUT_HIGH);
	error = PTR_ERR_OR_ZERO(ts->cs_gpiod);
	if (error) {
		if (error != -EPROBE_DEFER)
			dev_err(&client->dev, "failed to get CS GPIO\n");
		return error;
	}
	gpiod_set_consumer_name(ts->cs_gpiod, "BU21013 CS");

	error = devm_add_action_or_reset(&client->dev,
					 bu21013_disable_chip, ts);
	if (error) {
		dev_err(&client->dev,
			"failed to install chip disable handler\n");
		return error;
	}

	/* Named "INT" on the chip, DT binding is "touch" */
	ts->int_gpiod = devm_gpiod_get_optional(&client->dev,
						"touch", GPIOD_IN);
	error = PTR_ERR_OR_ZERO(ts->int_gpiod);
	if (error) {
		if (error != -EPROBE_DEFER)
			dev_err(&client->dev, "failed to get INT GPIO\n");
		return error;
	}

	if (ts->int_gpiod)
		gpiod_set_consumer_name(ts->int_gpiod, "BU21013 INT");

	/* configure the touch panel controller */
	error = bu21013_init_chip(ts);
	if (error) {
		dev_err(&client->dev, "error in bu21013 config\n");
		return error;
	}

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, bu21013_gpio_irq,
					  IRQF_ONESHOT, DRIVER_TP, ts);
	if (error) {
		dev_err(&client->dev, "request irq %d failed\n",
			client->irq);
		return error;
	}

	error = input_register_device(in_dev);
	if (error) {
		dev_err(&client->dev, "failed to register input device\n");
		return error;
	}

	i2c_set_clientdata(client, ts);

	return 0;
}

static void bu21013_remove(struct i2c_client *client)
{
	struct bu21013_ts *ts = i2c_get_clientdata(client);

	/* Make sure IRQ will exit quickly even if there is contact */
	ts->touch_stopped = true;
	/* The resources will be freed by devm */
}

static int __maybe_unused bu21013_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bu21013_ts *ts = i2c_get_clientdata(client);

	ts->touch_stopped = true;
	mb();
	disable_irq(client->irq);

	if (!device_may_wakeup(&client->dev))
		regulator_disable(ts->regulator);

	return 0;
}

static int __maybe_unused bu21013_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct bu21013_ts *ts = i2c_get_clientdata(client);
	int error;

	if (!device_may_wakeup(&client->dev)) {
		error = regulator_enable(ts->regulator);
		if (error) {
			dev_err(&client->dev,
				"failed to re-enable regulator when resuming\n");
			return error;
		}

		error = bu21013_init_chip(ts);
		if (error) {
			dev_err(&client->dev,
				"failed to reinitialize chip when resuming\n");
			return error;
		}
	}

	ts->touch_stopped = false;
	mb();
	enable_irq(client->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(bu21013_dev_pm_ops, bu21013_suspend, bu21013_resume);

static const struct i2c_device_id bu21013_id[] = {
	{ DRIVER_TP, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bu21013_id);

static struct i2c_driver bu21013_driver = {
	.driver	= {
		.name	=	DRIVER_TP,
		.pm	=	&bu21013_dev_pm_ops,
	},
	.probe		=	bu21013_probe,
	.remove		=	bu21013_remove,
	.id_table	=	bu21013_id,
};

module_i2c_driver(bu21013_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Naveen Kumar G <naveen.gaddipati@stericsson.com>");
MODULE_DESCRIPTION("bu21013 touch screen controller driver");
