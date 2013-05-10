/*
 * MicroTouch (3M) serial touchscreen driver
 *
 * Copyright (c) 2004 Vojtech Pavlik
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

/*
 * 2005/02/19 Dan Streetman <ddstreet@ieee.org>
 *   Copied elo.c and edited for MicroTouch protocol
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/init.h>

#define DRIVER_DESC	"MicroTouch serial touchscreen driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Definitions & global arrays.
 */

#define MTOUCH_FORMAT_TABLET_STATUS_BIT 0x80
#define MTOUCH_FORMAT_TABLET_TOUCH_BIT 0x40
#define MTOUCH_FORMAT_TABLET_LENGTH 5
#define MTOUCH_RESPONSE_BEGIN_BYTE 0x01
#define MTOUCH_RESPONSE_END_BYTE 0x0d

/* todo: check specs for max length of all responses */
#define MTOUCH_MAX_LENGTH 16

#define MTOUCH_MIN_XC 0
#define MTOUCH_MAX_XC 0x3fff
#define MTOUCH_MIN_YC 0
#define MTOUCH_MAX_YC 0x3fff

#define MTOUCH_GET_XC(data) (((data[2])<<7) | data[1])
#define MTOUCH_GET_YC(data) (((data[4])<<7) | data[3])
#define MTOUCH_GET_TOUCHED(data) (MTOUCH_FORMAT_TABLET_TOUCH_BIT & data[0])

/*
 * Per-touchscreen data.
 */

struct mtouch {
	struct input_dev *dev;
	struct serio *serio;
	int idx;
	unsigned char data[MTOUCH_MAX_LENGTH];
	char phys[32];
};

static void mtouch_process_format_tablet(struct mtouch *mtouch)
{
	struct input_dev *dev = mtouch->dev;

	if (MTOUCH_FORMAT_TABLET_LENGTH == ++mtouch->idx) {
		input_report_abs(dev, ABS_X, MTOUCH_GET_XC(mtouch->data));
		input_report_abs(dev, ABS_Y, MTOUCH_MAX_YC - MTOUCH_GET_YC(mtouch->data));
		input_report_key(dev, BTN_TOUCH, MTOUCH_GET_TOUCHED(mtouch->data));
		input_sync(dev);

		mtouch->idx = 0;
	}
}

static void mtouch_process_response(struct mtouch *mtouch)
{
	if (MTOUCH_RESPONSE_END_BYTE == mtouch->data[mtouch->idx++]) {
		/* FIXME - process response */
		mtouch->idx = 0;
	} else if (MTOUCH_MAX_LENGTH == mtouch->idx) {
		printk(KERN_ERR "mtouch.c: too many response bytes\n");
		mtouch->idx = 0;
	}
}

static irqreturn_t mtouch_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags)
{
	struct mtouch* mtouch = serio_get_drvdata(serio);

	mtouch->data[mtouch->idx] = data;

	if (MTOUCH_FORMAT_TABLET_STATUS_BIT & mtouch->data[0])
		mtouch_process_format_tablet(mtouch);
	else if (MTOUCH_RESPONSE_BEGIN_BYTE == mtouch->data[0])
		mtouch_process_response(mtouch);
	else
		printk(KERN_DEBUG "mtouch.c: unknown/unsynchronized data from device, byte %x\n",mtouch->data[0]);

	return IRQ_HANDLED;
}

/*
 * mtouch_disconnect() is the opposite of mtouch_connect()
 */

static void mtouch_disconnect(struct serio *serio)
{
	struct mtouch* mtouch = serio_get_drvdata(serio);

	input_get_device(mtouch->dev);
	input_unregister_device(mtouch->dev);
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_put_device(mtouch->dev);
	kfree(mtouch);
}

/*
 * mtouch_connect() is the routine that is called when someone adds a
 * new serio device that supports MicroTouch (Format Tablet) protocol and registers it as
 * an input device.
 */

static int mtouch_connect(struct serio *serio, struct serio_driver *drv)
{
	struct mtouch *mtouch;
	struct input_dev *input_dev;
	int err;

	mtouch = kzalloc(sizeof(struct mtouch), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!mtouch || !input_dev) {
		err = -ENOMEM;
		goto fail1;
	}

	mtouch->serio = serio;
	mtouch->dev = input_dev;
	snprintf(mtouch->phys, sizeof(mtouch->phys), "%s/input0", serio->phys);

	input_dev->name = "MicroTouch Serial TouchScreen";
	input_dev->phys = mtouch->phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = SERIO_MICROTOUCH;
	input_dev->id.product = 0;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &serio->dev;
	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_set_abs_params(mtouch->dev, ABS_X, MTOUCH_MIN_XC, MTOUCH_MAX_XC, 0, 0);
	input_set_abs_params(mtouch->dev, ABS_Y, MTOUCH_MIN_YC, MTOUCH_MAX_YC, 0, 0);

	serio_set_drvdata(serio, mtouch);

	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	err = input_register_device(mtouch->dev);
	if (err)
		goto fail3;

	return 0;

 fail3:	serio_close(serio);
 fail2:	serio_set_drvdata(serio, NULL);
 fail1:	input_free_device(input_dev);
	kfree(mtouch);
	return err;
}

/*
 * The serio driver structure.
 */

static struct serio_device_id mtouch_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_MICROTOUCH,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, mtouch_serio_ids);

static struct serio_driver mtouch_drv = {
	.driver		= {
		.name	= "mtouch",
	},
	.description	= DRIVER_DESC,
	.id_table	= mtouch_serio_ids,
	.interrupt	= mtouch_interrupt,
	.connect	= mtouch_connect,
	.disconnect	= mtouch_disconnect,
};

/*
 * The functions for inserting/removing us as a module.
 */

static int __init mtouch_init(void)
{
	return serio_register_driver(&mtouch_drv);
}

static void __exit mtouch_exit(void)
{
	serio_unregister_driver(&mtouch_drv);
}

module_init(mtouch_init);
module_exit(mtouch_exit);
