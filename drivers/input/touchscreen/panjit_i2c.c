/*
 * drivers/input/touchscreen/panjit_i2c.c
 *
 * Touchscreen class input driver for Panjit touch panel using I2C bus
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/slab.h>

/* touch controller registers */
#define CSR				0x00 /* Control and Status register */
#define C_FLAG				0x01 /* Interrupt Clear flag */
#define X1_H				0x03 /* High Byte of X1 Position */
#define X1_L				0x04 /* Low Byte of X1 Position */
#define Y1_H				0x05 /* High Byte of Y1 Position */
#define Y1_L				0x06 /* Low Byte of Y1 Position */
#define X2_H				0x07 /* High Byte of X2 Position */
#define X2_L				0x08 /* Low Byte of X2 Position */
#define Y2_H				0x09 /* High Byte of Y2 Position */
#define Y2_L				0x0A /* Low Byte of Y2 Position */
#define FINGERS				0x0B /* Detected finger number */
#define GESTURE				0x0C /* Interpreted gesture */

/* Control Status Register bit masks */
#define CSR_SLEEP_EN			(1 << 7)
#define CSR_SCAN_EN			(1 << 3)
#define SLEEP_ENABLE			1

/* Interrupt Clear register bit masks */
#define C_FLAG_CLEAR			0x08
#define INT_ASSERTED			(1 << C_FLAG_CLEAR)

#define DRIVER_NAME			"panjit_touch"

struct pj_data {
	struct input_dev	*input_dev;
	struct i2c_client	*client;
	int			chipid;
};

struct pj_event {
	__be16	x0;
	__be16	y0;
	__be16	x1;
	__be16	y1;
	__u8	fingers;
	__u8	gesture;
};

union pj_buff {
	struct pj_event	data;
	unsigned char	buff[sizeof(struct pj_data)];
};

static irqreturn_t pj_irq(int irq, void *dev_id)
{
	struct pj_data *touch = dev_id;
	struct i2c_client *client = touch->client;
	union pj_buff event;
	unsigned short x, y;
	int offs;
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, X1_H,
					    sizeof(event.buff), event.buff);
	if (WARN_ON(ret < 0)) {
		dev_err(&client->dev, "error %d reading event data\n", ret);
		return IRQ_HANDLED;
	}
	ret = i2c_smbus_write_byte_data(client, C_FLAG, 0);
	if (WARN_ON(ret < 0)) {
		dev_err(&client->dev, "error %d clearing interrupt\n", ret);
		return IRQ_HANDLED;
	}

	input_report_key(touch->input_dev, BTN_TOUCH,
			 (event.data.fingers == 1 || event.data.fingers == 2));
	input_report_key(touch->input_dev, BTN_2, (event.data.fingers == 2));

	if (!event.data.fingers || (event.data.fingers > 2))
		goto out;

	offs = (event.data.fingers == 2) ? ABS_HAT0X : ABS_X;

	x = __be16_to_cpu(event.data.x0);
	y = __be16_to_cpu(event.data.y0);

	dev_dbg(&client->dev, "f[0] x=%u, y=%u\n", x, y);
	input_report_abs(touch->input_dev, offs, x);
	input_report_abs(touch->input_dev, offs + 1, y);

	if (event.data.fingers == 1)
		goto out;

	x = __be16_to_cpu(event.data.x1);
	y = __be16_to_cpu(event.data.y1);
	dev_dbg(&client->dev, "f[1] x=%u, y=%u\n", x, y);
	input_report_abs(touch->input_dev, ABS_HAT1X, x);
	input_report_abs(touch->input_dev, ABS_HAT1Y, y);

out:
	input_sync(touch->input_dev);
	return IRQ_HANDLED;
}

static int pj_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct pj_data *touch = NULL;
	struct input_dev *input_dev = NULL;
	int ret = 0;

	touch = kzalloc(sizeof(struct pj_data), GFP_KERNEL);
	if (!touch) {
		dev_err(&client->dev, "%s: no memory\n", __func__);
		return -ENOMEM;
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(&client->dev, "%s: no memory\n", __func__);
		kfree(touch);
		return -ENOMEM;
	}

	touch->client = client;
	i2c_set_clientdata(client, touch);

	/* clear interrupt */
	ret = i2c_smbus_write_byte_data(touch->client, C_FLAG, 0);
	if (ret < 0) {
		dev_err(&client->dev, "%s: clear interrupt failed\n",
			__func__);
		goto fail_i2c_or_register;
	}

	/* enable scanning */
	ret = i2c_smbus_write_byte_data(touch->client, CSR, CSR_SCAN_EN);
	if (ret < 0) {
		dev_err(&client->dev, "%s: enable interrupt failed\n",
			__func__);
		goto fail_i2c_or_register;
	}

	touch->input_dev = input_dev;
	touch->input_dev->name = DRIVER_NAME;

	set_bit(EV_SYN, touch->input_dev->evbit);
	set_bit(EV_KEY, touch->input_dev->evbit);
	set_bit(EV_ABS, touch->input_dev->evbit);
	set_bit(BTN_TOUCH, touch->input_dev->keybit);
	set_bit(BTN_2, touch->input_dev->keybit);

	/* expose multi-touch capabilities */
	set_bit(ABS_MT_POSITION_X, touch->input_dev->keybit);
	set_bit(ABS_MT_POSITION_Y, touch->input_dev->keybit);
	set_bit(ABS_X, touch->input_dev->keybit);
	set_bit(ABS_Y, touch->input_dev->keybit);

	/* all coordinates are reported in 0..4095 */
	input_set_abs_params(touch->input_dev, ABS_X, 0, 4095, 0, 0);
	input_set_abs_params(touch->input_dev, ABS_Y, 0, 4095, 0, 0);
	input_set_abs_params(touch->input_dev, ABS_HAT0X, 0, 4095, 0, 0);
	input_set_abs_params(touch->input_dev, ABS_HAT0Y, 0, 4095, 0, 0);
	input_set_abs_params(touch->input_dev, ABS_HAT1X, 0, 4095, 0, 0);
	input_set_abs_params(touch->input_dev, ABS_HAT1Y, 0, 4095, 0, 0);

	input_set_abs_params(touch->input_dev, ABS_MT_POSITION_X, 0, 4095, 0, 0);
	input_set_abs_params(touch->input_dev, ABS_MT_POSITION_Y, 0, 4095, 0, 0);

	ret = input_register_device(touch->input_dev);
	if (ret) {
		dev_err(&client->dev, "%s: input_register_device failed\n",
			__func__);
		goto fail_i2c_or_register;
	}

	/* get the irq */
	ret = request_threaded_irq(touch->client->irq, NULL, pj_irq,
				   IRQF_ONESHOT, DRIVER_NAME, touch);
	if (ret) {
		dev_err(&client->dev, "%s: request_irq(%d) failed\n",
			__func__, touch->client->irq);
		goto fail_irq;
	}

	dev_info(&client->dev, "%s: initialized\n", __func__);
	return 0;

fail_irq:
	input_unregister_device(touch->input_dev);

fail_i2c_or_register:
	input_free_device(input_dev);
	kfree(touch);
	return ret;
}

static int pj_suspend(struct i2c_client *client, pm_message_t state)
{
	struct pj_data *touch = i2c_get_clientdata(client);
	int ret;

	if (!touch) {
		WARN_ON(1);
		return -EINVAL;
	}

	disable_irq(touch->client->irq);

	/* disable scanning and enable deep sleep */
	ret = i2c_smbus_write_byte_data(client, CSR, CSR_SLEEP_EN);
	if (ret < 0) {
		dev_err(&client->dev, "%s: sleep enable fail\n", __func__);
		return ret;
	}
	return 0;
}

static int pj_resume(struct i2c_client *client)
{
	struct pj_data *touch = i2c_get_clientdata(client);
	int ret = 0;

	if (!touch) {
		WARN_ON(1);
		return -EINVAL;
	}
	/* enable scanning and disable deep sleep */
	ret = i2c_smbus_write_byte_data(client, CSR, CSR_SCAN_EN);
	if (ret < 0) {
		dev_err(&client->dev, "%s: interrupt enable fail\n", __func__);
		return ret;
	}

	enable_irq(touch->client->irq);
}

static int pj_remove(struct i2c_client *client)
{
	struct pj_data *touch = i2c_get_clientdata(client);

	if (!touch)
		return -EINVAL;

	free_irq(touch->client->irq, touch);
	input_unregister_device(touch->input_dev);
	input_free_device(touch->input_dev);
	kfree(touch);
	return 0;
}

static const struct i2c_device_id panjit_ts_id[] = {
	{ DRIVER_NAME, 0 },
	{ }
};

static struct i2c_driver panjit_driver = {
	.probe		= pj_probe,
	.remove		= pj_remove,
	.suspend	= pj_suspend,
	.resume		= pj_resume,
	.id_table	= panjit_ts_id,
	.driver		= {
		.name	= DRIVER_NAME,
	},
};

static int __devinit panjit_init(void)
{
	int e;

	e = i2c_add_driver(&panjit_driver);
	if (e != 0) {
		pr_err("%s: failed to register with I2C bus with "
		       "error: 0x%x\n", __func__, e);
	}
	return e;
}

static void __exit panjit_exit(void)
{
	i2c_del_driver(&panjit_driver);
}

module_init(panjit_init);
module_exit(panjit_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Panjit I2C touch driver");
