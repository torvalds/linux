/*
 * Sahara TouchIT-213 serial touchscreen driver
 *
 * Copyright (c) 2007-2008 Claudio Nieder <private@claudio.ch>
 *
 * Based on Touchright driver (drivers/input/touchscreen/touchright.c)
 * Copyright (c) 2006 Rick Koch <n1gp@hotmail.com>
 * Copyright (c) 2004 Vojtech Pavlik
 * and Dan Streetman <ddstreet@ieee.org>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>

#define DRIVER_DESC	"Sahara TouchIT-213 serial touchscreen driver"

MODULE_AUTHOR("Claudio Nieder <private@claudio.ch>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Definitions & global arrays.
 */

/*
 * Data is received through COM1 at 9600bit/s,8bit,no parity in packets
 * of 5 byte each.
 *
 *   +--------+   +--------+   +--------+   +--------+   +--------+
 *   |1000000p|   |0xxxxxxx|   |0xxxxxxx|   |0yyyyyyy|   |0yyyyyyy|
 *   +--------+   +--------+   +--------+   +--------+   +--------+
 *                    MSB          LSB          MSB          LSB
 *
 * The value of p is 1 as long as the screen is touched and 0 when
 * reporting the location where touching stopped, e.g. where the pen was
 * lifted from the screen.
 *
 * When holding the screen in landscape mode as the BIOS text output is
 * presented, x is the horizontal axis with values growing from left to
 * right and y is the vertical axis with values growing from top to
 * bottom.
 *
 * When holding the screen in portrait mode with the Sahara logo in its
 * correct position, x ist the vertical axis with values growing from
 * top to bottom and y is the horizontal axis with values growing from
 * right to left.
 */

#define T213_FORMAT_TOUCH_BIT	0x01
#define T213_FORMAT_STATUS_BYTE	0x80
#define T213_FORMAT_STATUS_MASK	~T213_FORMAT_TOUCH_BIT

/*
 * On my Sahara Touch-IT 213 I have observed x values from 0 to 0x7f0
 * and y values from 0x1d to 0x7e9, so the actual measurement is
 * probably done with an 11 bit precision.
 */
#define T213_MIN_XC 0
#define T213_MAX_XC 0x07ff
#define T213_MIN_YC 0
#define T213_MAX_YC 0x07ff

/*
 * Per-touchscreen data.
 */

struct touchit213 {
	struct input_dev *dev;
	struct serio *serio;
	int idx;
	unsigned char csum;
	unsigned char data[5];
	char phys[32];
};

static irqreturn_t touchit213_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags)
{
	struct touchit213 *touchit213 = serio_get_drvdata(serio);
	struct input_dev *dev = touchit213->dev;

	touchit213->data[touchit213->idx] = data;

	switch (touchit213->idx++) {
	case 0:
		if ((touchit213->data[0] & T213_FORMAT_STATUS_MASK) !=
				T213_FORMAT_STATUS_BYTE) {
			pr_debug("unsynchronized data: 0x%02x\n", data);
			touchit213->idx = 0;
		}
		break;

	case 4:
		touchit213->idx = 0;
		input_report_abs(dev, ABS_X,
			(touchit213->data[1] << 7) | touchit213->data[2]);
		input_report_abs(dev, ABS_Y,
			(touchit213->data[3] << 7) | touchit213->data[4]);
		input_report_key(dev, BTN_TOUCH,
			touchit213->data[0] & T213_FORMAT_TOUCH_BIT);
		input_sync(dev);
		break;
	}

	return IRQ_HANDLED;
}

/*
 * touchit213_disconnect() is the opposite of touchit213_connect()
 */

static void touchit213_disconnect(struct serio *serio)
{
	struct touchit213 *touchit213 = serio_get_drvdata(serio);

	input_get_device(touchit213->dev);
	input_unregister_device(touchit213->dev);
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_put_device(touchit213->dev);
	kfree(touchit213);
}

/*
 * touchit213_connect() is the routine that is called when someone adds a
 * new serio device that supports the Touchright protocol and registers it as
 * an input device.
 */

static int touchit213_connect(struct serio *serio, struct serio_driver *drv)
{
	struct touchit213 *touchit213;
	struct input_dev *input_dev;
	int err;

	touchit213 = kzalloc(sizeof(struct touchit213), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!touchit213 || !input_dev) {
		err = -ENOMEM;
		goto fail1;
	}

	touchit213->serio = serio;
	touchit213->dev = input_dev;
	snprintf(touchit213->phys, sizeof(touchit213->phys),
		 "%s/input0", serio->phys);

	input_dev->name = "Sahara Touch-iT213 Serial TouchScreen";
	input_dev->phys = touchit213->phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = SERIO_TOUCHIT213;
	input_dev->id.product = 0;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &serio->dev;
	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_set_abs_params(touchit213->dev, ABS_X,
			     T213_MIN_XC, T213_MAX_XC, 0, 0);
	input_set_abs_params(touchit213->dev, ABS_Y,
			     T213_MIN_YC, T213_MAX_YC, 0, 0);

	serio_set_drvdata(serio, touchit213);

	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	err = input_register_device(touchit213->dev);
	if (err)
		goto fail3;

	return 0;

 fail3:	serio_close(serio);
 fail2:	serio_set_drvdata(serio, NULL);
 fail1:	input_free_device(input_dev);
	kfree(touchit213);
	return err;
}

/*
 * The serio driver structure.
 */

static struct serio_device_id touchit213_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_TOUCHIT213,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, touchit213_serio_ids);

static struct serio_driver touchit213_drv = {
	.driver		= {
		.name	= "touchit213",
	},
	.description	= DRIVER_DESC,
	.id_table	= touchit213_serio_ids,
	.interrupt	= touchit213_interrupt,
	.connect	= touchit213_connect,
	.disconnect	= touchit213_disconnect,
};

module_serio_driver(touchit213_drv);
