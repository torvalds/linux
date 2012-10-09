/*
 * LM8333 keypad driver
 * Copyright (C) 2012 Wolfram Sang, Pengutronix <w.sang@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/input/matrix_keypad.h>
#include <linux/input/lm8333.h>

#define LM8333_FIFO_READ		0x20
#define LM8333_DEBOUNCE			0x22
#define LM8333_READ_INT			0xD0
#define LM8333_ACTIVE			0xE4
#define LM8333_READ_ERROR		0xF0

#define LM8333_KEYPAD_IRQ		(1 << 0)
#define LM8333_ERROR_IRQ		(1 << 3)

#define LM8333_ERROR_KEYOVR		0x04
#define LM8333_ERROR_FIFOOVR		0x40

#define LM8333_FIFO_TRANSFER_SIZE	16

#define LM8333_NUM_ROWS		8
#define LM8333_NUM_COLS		16
#define LM8333_ROW_SHIFT	4

struct lm8333 {
	struct i2c_client *client;
	struct input_dev *input;
	unsigned short keycodes[LM8333_NUM_ROWS << LM8333_ROW_SHIFT];
};

/* The accessors try twice because the first access may be needed for wakeup */
#define LM8333_READ_RETRIES 2

int lm8333_read8(struct lm8333 *lm8333, u8 cmd)
{
	int retries = 0, ret;

	do {
		ret = i2c_smbus_read_byte_data(lm8333->client, cmd);
	} while (ret < 0 && retries++ < LM8333_READ_RETRIES);

	return ret;
}

int lm8333_write8(struct lm8333 *lm8333, u8 cmd, u8 val)
{
	int retries = 0, ret;

	do {
		ret = i2c_smbus_write_byte_data(lm8333->client, cmd, val);
	} while (ret < 0 && retries++ < LM8333_READ_RETRIES);

	return ret;
}

int lm8333_read_block(struct lm8333 *lm8333, u8 cmd, u8 len, u8 *buf)
{
	int retries = 0, ret;

	do {
		ret = i2c_smbus_read_i2c_block_data(lm8333->client,
						    cmd, len, buf);
	} while (ret < 0 && retries++ < LM8333_READ_RETRIES);

	return ret;
}

static void lm8333_key_handler(struct lm8333 *lm8333)
{
	struct input_dev *input = lm8333->input;
	u8 keys[LM8333_FIFO_TRANSFER_SIZE];
	u8 code, pressed;
	int i, ret;

	ret = lm8333_read_block(lm8333, LM8333_FIFO_READ,
				LM8333_FIFO_TRANSFER_SIZE, keys);
	if (ret != LM8333_FIFO_TRANSFER_SIZE) {
		dev_err(&lm8333->client->dev,
			"Error %d while reading FIFO\n", ret);
		return;
	}

	for (i = 0; i < LM8333_FIFO_TRANSFER_SIZE && keys[i]; i++) {
		pressed = keys[i] & 0x80;
		code = keys[i] & 0x7f;

		input_event(input, EV_MSC, MSC_SCAN, code);
		input_report_key(input, lm8333->keycodes[code], pressed);
	}

	input_sync(input);
}

static irqreturn_t lm8333_irq_thread(int irq, void *data)
{
	struct lm8333 *lm8333 = data;
	u8 status = lm8333_read8(lm8333, LM8333_READ_INT);

	if (!status)
		return IRQ_NONE;

	if (status & LM8333_ERROR_IRQ) {
		u8 err = lm8333_read8(lm8333, LM8333_READ_ERROR);

		if (err & (LM8333_ERROR_KEYOVR | LM8333_ERROR_FIFOOVR)) {
			u8 dummy[LM8333_FIFO_TRANSFER_SIZE];

			lm8333_read_block(lm8333, LM8333_FIFO_READ,
					LM8333_FIFO_TRANSFER_SIZE, dummy);
		}
		dev_err(&lm8333->client->dev, "Got error %02x\n", err);
	}

	if (status & LM8333_KEYPAD_IRQ)
		lm8333_key_handler(lm8333);

	return IRQ_HANDLED;
}

static int __devinit lm8333_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	const struct lm8333_platform_data *pdata = client->dev.platform_data;
	struct lm8333 *lm8333;
	struct input_dev *input;
	int err, active_time;

	if (!pdata)
		return -EINVAL;

	active_time = pdata->active_time ?: 500;
	if (active_time / 3 <= pdata->debounce_time / 3) {
		dev_err(&client->dev, "Active time not big enough!\n");
		return -EINVAL;
	}

	lm8333 = kzalloc(sizeof(*lm8333), GFP_KERNEL);
	input = input_allocate_device();
	if (!lm8333 || !input) {
		err = -ENOMEM;
		goto free_mem;
	}

	lm8333->client = client;
	lm8333->input = input;

	input->name = client->name;
	input->dev.parent = &client->dev;
	input->id.bustype = BUS_I2C;

	input_set_capability(input, EV_MSC, MSC_SCAN);

	err = matrix_keypad_build_keymap(pdata->matrix_data, NULL,
					 LM8333_NUM_ROWS, LM8333_NUM_COLS,
					 lm8333->keycodes, input);
	if (err)
		goto free_mem;

	if (pdata->debounce_time) {
		err = lm8333_write8(lm8333, LM8333_DEBOUNCE,
				    pdata->debounce_time / 3);
		if (err)
			dev_warn(&client->dev, "Unable to set debounce time\n");
	}

	if (pdata->active_time) {
		err = lm8333_write8(lm8333, LM8333_ACTIVE,
				    pdata->active_time / 3);
		if (err)
			dev_warn(&client->dev, "Unable to set active time\n");
	}

	err = request_threaded_irq(client->irq, NULL, lm8333_irq_thread,
				   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				   "lm8333", lm8333);
	if (err)
		goto free_mem;

	err = input_register_device(input);
	if (err)
		goto free_irq;

	i2c_set_clientdata(client, lm8333);
	return 0;

 free_irq:
	free_irq(client->irq, lm8333);
 free_mem:
	input_free_device(input);
	kfree(lm8333);
	return err;
}

static int __devexit lm8333_remove(struct i2c_client *client)
{
	struct lm8333 *lm8333 = i2c_get_clientdata(client);

	free_irq(client->irq, lm8333);
	input_unregister_device(lm8333->input);
	kfree(lm8333);

	return 0;
}

static const struct i2c_device_id lm8333_id[] = {
	{ "lm8333", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm8333_id);

static struct i2c_driver lm8333_driver = {
	.driver = {
		.name		= "lm8333",
		.owner		= THIS_MODULE,
	},
	.probe		= lm8333_probe,
	.remove		= __devexit_p(lm8333_remove),
	.id_table	= lm8333_id,
};
module_i2c_driver(lm8333_driver);

MODULE_AUTHOR("Wolfram Sang <w.sang@pengutronix.de>");
MODULE_DESCRIPTION("LM8333 keyboard driver");
MODULE_LICENSE("GPL v2");
