/*
 * iNexio serial touchscreen driver
 *
 * Copyright (c) 2008 Richard Lemon
 * Based on the mtouch driver (c) Vojtech Pavlik and Dan Streetman
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

/*
 * 2008/06/19 Richard Lemon <richard@codelemon.com>
 *   Copied mtouch.c and edited for iNexio protocol
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/init.h>

#define DRIVER_DESC	"iNexio serial touchscreen driver"

MODULE_AUTHOR("Richard Lemon <richard@codelemon.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Definitions & global arrays.
 */

#define INEXIO_FORMAT_TOUCH_BIT 0x01
#define INEXIO_FORMAT_LENGTH 5
#define INEXIO_RESPONSE_BEGIN_BYTE 0x80

/* todo: check specs for max length of all responses */
#define INEXIO_MAX_LENGTH 16

#define INEXIO_MIN_XC 0
#define INEXIO_MAX_XC 0x3fff
#define INEXIO_MIN_YC 0
#define INEXIO_MAX_YC 0x3fff

#define INEXIO_GET_XC(data) (((data[1])<<7) | data[2])
#define INEXIO_GET_YC(data) (((data[3])<<7) | data[4])
#define INEXIO_GET_TOUCHED(data) (INEXIO_FORMAT_TOUCH_BIT & data[0])

/*
 * Per-touchscreen data.
 */

struct inexio {
	struct input_dev *dev;
	struct serio *serio;
	int idx;
	unsigned char data[INEXIO_MAX_LENGTH];
	char phys[32];
};

static void inexio_process_data(struct inexio *pinexio)
{
	struct input_dev *dev = pinexio->dev;

	if (INEXIO_FORMAT_LENGTH == ++pinexio->idx) {
		input_report_abs(dev, ABS_X, INEXIO_GET_XC(pinexio->data));
		input_report_abs(dev, ABS_Y, INEXIO_GET_YC(pinexio->data));
		input_report_key(dev, BTN_TOUCH, INEXIO_GET_TOUCHED(pinexio->data));
		input_sync(dev);

		pinexio->idx = 0;
	}
}

static irqreturn_t inexio_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags)
{
	struct inexio* pinexio = serio_get_drvdata(serio);

	pinexio->data[pinexio->idx] = data;

	if (INEXIO_RESPONSE_BEGIN_BYTE&pinexio->data[0])
		inexio_process_data(pinexio);
	else
		printk(KERN_DEBUG "inexio.c: unknown/unsynchronized data from device, byte %x\n",pinexio->data[0]);

	return IRQ_HANDLED;
}

/*
 * inexio_disconnect() is the opposite of inexio_connect()
 */

static void inexio_disconnect(struct serio *serio)
{
	struct inexio* pinexio = serio_get_drvdata(serio);

	input_get_device(pinexio->dev);
	input_unregister_device(pinexio->dev);
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_put_device(pinexio->dev);
	kfree(pinexio);
}

/*
 * inexio_connect() is the routine that is called when someone adds a
 * new serio device that supports iNexio protocol and registers it as
 * an input device. This is usually accomplished using inputattach.
 */

static int inexio_connect(struct serio *serio, struct serio_driver *drv)
{
	struct inexio *pinexio;
	struct input_dev *input_dev;
	int err;

	pinexio = kzalloc(sizeof(struct inexio), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!pinexio || !input_dev) {
		err = -ENOMEM;
		goto fail1;
	}

	pinexio->serio = serio;
	pinexio->dev = input_dev;
	snprintf(pinexio->phys, sizeof(pinexio->phys), "%s/input0", serio->phys);

	input_dev->name = "iNexio Serial TouchScreen";
	input_dev->phys = pinexio->phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = SERIO_INEXIO;
	input_dev->id.product = 0;
	input_dev->id.version = 0x0001;
	input_dev->dev.parent = &serio->dev;
	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_set_abs_params(pinexio->dev, ABS_X, INEXIO_MIN_XC, INEXIO_MAX_XC, 0, 0);
	input_set_abs_params(pinexio->dev, ABS_Y, INEXIO_MIN_YC, INEXIO_MAX_YC, 0, 0);

	serio_set_drvdata(serio, pinexio);

	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	err = input_register_device(pinexio->dev);
	if (err)
		goto fail3;

	return 0;

 fail3:	serio_close(serio);
 fail2:	serio_set_drvdata(serio, NULL);
 fail1:	input_free_device(input_dev);
	kfree(pinexio);
	return err;
}

/*
 * The serio driver structure.
 */

static struct serio_device_id inexio_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_INEXIO,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, inexio_serio_ids);

static struct serio_driver inexio_drv = {
	.driver		= {
		.name	= "inexio",
	},
	.description	= DRIVER_DESC,
	.id_table	= inexio_serio_ids,
	.interrupt	= inexio_interrupt,
	.connect	= inexio_connect,
	.disconnect	= inexio_disconnect,
};

/*
 * The functions for inserting/removing us as a module.
 */

static int __init inexio_init(void)
{
	return serio_register_driver(&inexio_drv);
}

static void __exit inexio_exit(void)
{
	serio_unregister_driver(&inexio_drv);
}

module_init(inexio_init);
module_exit(inexio_exit);
