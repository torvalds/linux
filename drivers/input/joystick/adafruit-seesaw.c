// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023 Anshul Dalal <anshulusr@gmail.com>
 *
 * Driver for Adafruit Mini I2C Gamepad
 *
 * Based on the work of:
 *	Oleh Kravchenko (Sparkfun Qwiic Joystick driver)
 *
 * Datasheet: https://cdn-learn.adafruit.com/downloads/pdf/gamepad-qt.pdf
 * Product page: https://www.adafruit.com/product/5743
 * Firmware and hardware sources: https://github.com/adafruit/Adafruit_Seesaw
 *
 * TODO:
 *	- Add interrupt support
 */

#include <linux/unaligned.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/sparse-keymap.h>
#include <linux/kernel.h>
#include <linux/module.h>

#define SEESAW_DEVICE_NAME		"seesaw-gamepad"

#define SEESAW_ADC_BASE			0x0900

#define SEESAW_GPIO_DIRCLR_BULK		0x0103
#define SEESAW_GPIO_BULK		0x0104
#define SEESAW_GPIO_BULK_SET		0x0105
#define SEESAW_GPIO_PULLENSET		0x010b

#define SEESAW_STATUS_HW_ID		0x0001
#define SEESAW_STATUS_SWRST		0x007f

#define SEESAW_ADC_OFFSET		0x07

#define SEESAW_BUTTON_A			0x05
#define SEESAW_BUTTON_B			0x01
#define SEESAW_BUTTON_X			0x06
#define SEESAW_BUTTON_Y			0x02
#define SEESAW_BUTTON_START		0x10
#define SEESAW_BUTTON_SELECT		0x00

#define SEESAW_ANALOG_X			0x0e
#define SEESAW_ANALOG_Y			0x0f

#define SEESAW_JOYSTICK_MAX_AXIS	1023
#define SEESAW_JOYSTICK_FUZZ		2
#define SEESAW_JOYSTICK_FLAT		4

#define SEESAW_GAMEPAD_POLL_INTERVAL_MS	16
#define SEESAW_GAMEPAD_POLL_MIN		8
#define SEESAW_GAMEPAD_POLL_MAX		32

static const u32 SEESAW_BUTTON_MASK =
	BIT(SEESAW_BUTTON_A) | BIT(SEESAW_BUTTON_B) | BIT(SEESAW_BUTTON_X) |
	BIT(SEESAW_BUTTON_Y) | BIT(SEESAW_BUTTON_START) |
	BIT(SEESAW_BUTTON_SELECT);

struct seesaw_gamepad {
	struct input_dev *input_dev;
	struct i2c_client *i2c_client;
	u32 button_state;
};

struct seesaw_data {
	u16 x;
	u16 y;
	u32 button_state;
};

static const struct key_entry seesaw_buttons_new[] = {
	{ KE_KEY, SEESAW_BUTTON_A, .keycode = BTN_SOUTH },
	{ KE_KEY, SEESAW_BUTTON_B, .keycode = BTN_EAST },
	{ KE_KEY, SEESAW_BUTTON_X, .keycode = BTN_NORTH },
	{ KE_KEY, SEESAW_BUTTON_Y, .keycode = BTN_WEST },
	{ KE_KEY, SEESAW_BUTTON_START, .keycode = BTN_START },
	{ KE_KEY, SEESAW_BUTTON_SELECT, .keycode = BTN_SELECT },
	{ KE_END, 0 }
};

static int seesaw_register_read(struct i2c_client *client, u16 reg, void *buf,
				int count)
{
	__be16 register_buf = cpu_to_be16(reg);
	struct i2c_msg message_buf[2] = {
		{
			.addr = client->addr,
			.flags = client->flags,
			.len = sizeof(register_buf),
			.buf = (u8 *)&register_buf,
		},
		{
			.addr = client->addr,
			.flags = client->flags | I2C_M_RD,
			.len = count,
			.buf = (u8 *)buf,
		},
	};
	int ret;

	ret = i2c_transfer(client->adapter, message_buf,
			   ARRAY_SIZE(message_buf));
	if (ret < 0)
		return ret;

	return 0;
}

static int seesaw_register_write_u8(struct i2c_client *client, u16 reg,
				    u8 value)
{
	u8 write_buf[sizeof(reg) + sizeof(value)];
	int ret;

	put_unaligned_be16(reg, write_buf);
	write_buf[sizeof(reg)] = value;

	ret = i2c_master_send(client, write_buf, sizeof(write_buf));
	if (ret < 0)
		return ret;

	return 0;
}

static int seesaw_register_write_u32(struct i2c_client *client, u16 reg,
				     u32 value)
{
	u8 write_buf[sizeof(reg) + sizeof(value)];
	int ret;

	put_unaligned_be16(reg, write_buf);
	put_unaligned_be32(value, write_buf + sizeof(reg));
	ret = i2c_master_send(client, write_buf, sizeof(write_buf));
	if (ret < 0)
		return ret;

	return 0;
}

static int seesaw_read_data(struct i2c_client *client, struct seesaw_data *data)
{
	__be16 adc_data;
	__be32 read_buf;
	int err;

	err = seesaw_register_read(client, SEESAW_GPIO_BULK,
				   &read_buf, sizeof(read_buf));
	if (err)
		return err;

	data->button_state = ~be32_to_cpu(read_buf);

	err = seesaw_register_read(client,
				   SEESAW_ADC_BASE |
					(SEESAW_ADC_OFFSET + SEESAW_ANALOG_X),
				   &adc_data, sizeof(adc_data));
	if (err)
		return err;
	/*
	 * ADC reads left as max and right as 0, must be reversed since kernel
	 * expects reports in opposite order.
	 */
	data->x = SEESAW_JOYSTICK_MAX_AXIS - be16_to_cpu(adc_data);

	err = seesaw_register_read(client,
				   SEESAW_ADC_BASE |
					(SEESAW_ADC_OFFSET + SEESAW_ANALOG_Y),
				   &adc_data, sizeof(adc_data));
	if (err)
		return err;

	data->y = be16_to_cpu(adc_data);

	return 0;
}

static int seesaw_open(struct input_dev *input)
{
	struct seesaw_gamepad *private = input_get_drvdata(input);

	private->button_state = 0;

	return 0;
}

static void seesaw_poll(struct input_dev *input)
{
	struct seesaw_gamepad *private = input_get_drvdata(input);
	struct seesaw_data data;
	unsigned long changed;
	int err, i;

	err = seesaw_read_data(private->i2c_client, &data);
	if (err) {
		dev_err_ratelimited(&input->dev,
				    "failed to read joystick state: %d\n", err);
		return;
	}

	input_report_abs(input, ABS_X, data.x);
	input_report_abs(input, ABS_Y, data.y);

	data.button_state &= SEESAW_BUTTON_MASK;
	changed = private->button_state ^ data.button_state;
	private->button_state = data.button_state;

	for_each_set_bit(i, &changed, fls(SEESAW_BUTTON_MASK)) {
		if (!sparse_keymap_report_event(input, i,
						data.button_state & BIT(i),
						false))
			dev_err_ratelimited(&input->dev,
					    "failed to report keymap event");
	}

	input_sync(input);
}

static int seesaw_probe(struct i2c_client *client)
{
	struct seesaw_gamepad *seesaw;
	u8 hardware_id;
	int err;

	err = seesaw_register_write_u8(client, SEESAW_STATUS_SWRST, 0xFF);
	if (err)
		return err;

	/* Wait for the registers to reset before proceeding */
	usleep_range(10000, 15000);

	seesaw = devm_kzalloc(&client->dev, sizeof(*seesaw), GFP_KERNEL);
	if (!seesaw)
		return -ENOMEM;

	err = seesaw_register_read(client, SEESAW_STATUS_HW_ID,
				   &hardware_id, sizeof(hardware_id));
	if (err)
		return err;

	dev_dbg(&client->dev, "Adafruit Seesaw Gamepad, Hardware ID: %02x\n",
		hardware_id);

	/* Set Pin Mode to input and enable pull-up resistors */
	err = seesaw_register_write_u32(client, SEESAW_GPIO_DIRCLR_BULK,
					SEESAW_BUTTON_MASK);
	if (err)
		return err;
	err = seesaw_register_write_u32(client, SEESAW_GPIO_PULLENSET,
					SEESAW_BUTTON_MASK);
	if (err)
		return err;
	err = seesaw_register_write_u32(client, SEESAW_GPIO_BULK_SET,
					SEESAW_BUTTON_MASK);
	if (err)
		return err;

	seesaw->i2c_client = client;
	seesaw->input_dev = devm_input_allocate_device(&client->dev);
	if (!seesaw->input_dev)
		return -ENOMEM;

	seesaw->input_dev->id.bustype = BUS_I2C;
	seesaw->input_dev->name = "Adafruit Seesaw Gamepad";
	seesaw->input_dev->phys = "i2c/" SEESAW_DEVICE_NAME;
	seesaw->input_dev->open = seesaw_open;
	input_set_drvdata(seesaw->input_dev, seesaw);
	input_set_abs_params(seesaw->input_dev, ABS_X,
			     0, SEESAW_JOYSTICK_MAX_AXIS,
			     SEESAW_JOYSTICK_FUZZ, SEESAW_JOYSTICK_FLAT);
	input_set_abs_params(seesaw->input_dev, ABS_Y,
			     0, SEESAW_JOYSTICK_MAX_AXIS,
			     SEESAW_JOYSTICK_FUZZ, SEESAW_JOYSTICK_FLAT);

	err = sparse_keymap_setup(seesaw->input_dev, seesaw_buttons_new, NULL);
	if (err) {
		dev_err(&client->dev,
			"failed to set up input device keymap: %d\n", err);
		return err;
	}

	err = input_setup_polling(seesaw->input_dev, seesaw_poll);
	if (err) {
		dev_err(&client->dev, "failed to set up polling: %d\n", err);
		return err;
	}

	input_set_poll_interval(seesaw->input_dev,
				SEESAW_GAMEPAD_POLL_INTERVAL_MS);
	input_set_max_poll_interval(seesaw->input_dev, SEESAW_GAMEPAD_POLL_MAX);
	input_set_min_poll_interval(seesaw->input_dev, SEESAW_GAMEPAD_POLL_MIN);

	err = input_register_device(seesaw->input_dev);
	if (err) {
		dev_err(&client->dev, "failed to register joystick: %d\n", err);
		return err;
	}

	return 0;
}

static const struct i2c_device_id seesaw_id_table[] = {
	{ SEESAW_DEVICE_NAME },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, seesaw_id_table);

static const struct of_device_id seesaw_of_table[] = {
	{ .compatible = "adafruit,seesaw-gamepad"},
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, seesaw_of_table);

static struct i2c_driver seesaw_driver = {
	.driver = {
		.name = SEESAW_DEVICE_NAME,
		.of_match_table = seesaw_of_table,
	},
	.id_table = seesaw_id_table,
	.probe = seesaw_probe,
};
module_i2c_driver(seesaw_driver);

MODULE_AUTHOR("Anshul Dalal <anshulusr@gmail.com>");
MODULE_DESCRIPTION("Adafruit Mini I2C Gamepad driver");
MODULE_LICENSE("GPL");
