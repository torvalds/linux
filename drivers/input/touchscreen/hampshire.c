/*
 * Hampshire serial touchscreen driver
 *
 * Copyright (c) 2010 Adam Bennett
 * Based on the dynapro driver (c) Tias Guns
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

/*
 * 2010/04/08 Adam Bennett <abennett72@gmail.com>
 *   Copied dynapro.c and edited for Hampshire 4-byte protocol
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>

#define DRIVER_DESC	"Hampshire serial touchscreen driver"

MODULE_AUTHOR("Adam Bennett <abennett72@gmail.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Definitions & global arrays.
 */

#define HAMPSHIRE_FORMAT_TOUCH_BIT 0x40
#define HAMPSHIRE_FORMAT_LENGTH 4
#define HAMPSHIRE_RESPONSE_BEGIN_BYTE 0x80

#define HAMPSHIRE_MIN_XC 0
#define HAMPSHIRE_MAX_XC 0x1000
#define HAMPSHIRE_MIN_YC 0
#define HAMPSHIRE_MAX_YC 0x1000

#define HAMPSHIRE_GET_XC(data) (((data[3] & 0x0c) >> 2) | (data[1] << 2) | ((data[0] & 0x38) << 6))
#define HAMPSHIRE_GET_YC(data) ((data[3] & 0x03) | (data[2] << 2) | ((data[0] & 0x07) << 9))
#define HAMPSHIRE_GET_TOUCHED(data) (HAMPSHIRE_FORMAT_TOUCH_BIT & data[0])

/*
 * Per-touchscreen data.
 */

struct hampshire {
	struct input_dev *dev;
	struct serio *serio;
	int idx;
	unsigned char data[HAMPSHIRE_FORMAT_LENGTH];
	char phys[32];
};

static void hampshire_process_data(struct hampshire *phampshire)
{
	struct input_dev *dev = phampshire->dev;

	if (HAMPSHIRE_FORMAT_LENGTH == ++phampshire->idx) {
		input_report_abs(dev, ABS_X, HAMPSHIRE_GET_XC(phampshire->data));
		input_report_abs(dev, ABS_Y, HAMPSHIRE_GET_YC(phampshire->data));
		input_report_key(dev, BTN_TOUCH,
				 HAMPSHIRE_GET_TOUCHED(phampshire->data));
		input_sync(dev);

		phampshire->idx = 0;
	}
}

static irqreturn_t hampshire_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags)
{
	struct hampshire *phampshire = serio_get_drvdata(serio);

	phampshire->data[phampshire->idx] = data;

	if (HAMPSHIRE_RESPONSE_BEGIN_BYTE & phampshire->data[0])
		hampshire_process_data(phampshire);
	else
		dev_dbg(&serio->dev, "unknown/unsynchronized data: %x\n",
			phampshire->data[0]);

	return IRQ_HANDLED;
}

static void hampshire_disconnect(struct serio *serio)
{
	struct hampshire *phampshire = serio_get_drvdata(serio);

	input_get_device(phampshire->dev);
	input_unregister_device(phampshire->dev);
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_put_device(phampshire->dev);
	kfree(phampshire);
}

/*
 * hampshire_connect() is the routine that is called when someone adds a
 * new serio device that supports hampshire protocol and registers it as
 * an input device. This is usually accomplished using inputattach.
 */

static int hampshire_connect(struct serio *serio, struct serio_driver *drv)
{
	struct hampshire *phampshire;
	struct input_dev *input_dev;
	int err;

	phampshire = kzalloc(sizeof(struct hampshire), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!phampshire || !input_dev) {
		err = -ENOMEM;
		goto fail1;
	}

	phampshire->serio = serio;
	phampshire->dev = input_dev;
	snprintf(phampshire->phys, sizeof(phampshire->phys),
		 "%s/input0", serio->phys);

	input_dev->name = "Hampshire Serial TouchScreen";
	input_dev->phys = phampshire->phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = SERIO_HAMPSHIRE;
	input_dev->id.product = 0;
	input_dev->id.version = 0x0001;
	input_dev->dev.parent = &serio->dev;
	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_set_abs_params(phampshire->dev, ABS_X,
			     HAMPSHIRE_MIN_XC, HAMPSHIRE_MAX_XC, 0, 0);
	input_set_abs_params(phampshire->dev, ABS_Y,
			     HAMPSHIRE_MIN_YC, HAMPSHIRE_MAX_YC, 0, 0);

	serio_set_drvdata(serio, phampshire);

	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	err = input_register_device(phampshire->dev);
	if (err)
		goto fail3;

	return 0;

 fail3:	serio_close(serio);
 fail2:	serio_set_drvdata(serio, NULL);
 fail1:	input_free_device(input_dev);
	kfree(phampshire);
	return err;
}

/*
 * The serio driver structure.
 */

static struct serio_device_id hampshire_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_HAMPSHIRE,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, hampshire_serio_ids);

static struct serio_driver hampshire_drv = {
	.driver		= {
		.name	= "hampshire",
	},
	.description	= DRIVER_DESC,
	.id_table	= hampshire_serio_ids,
	.interrupt	= hampshire_interrupt,
	.connect	= hampshire_connect,
	.disconnect	= hampshire_disconnect,
};

module_serio_driver(hampshire_drv);
