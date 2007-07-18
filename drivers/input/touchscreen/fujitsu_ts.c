/*
 * Fujitsu serial touchscreen driver
 *
 * Copyright (c) Dmitry Torokhov <dtor@mail.ru>
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
#include <linux/init.h>

#define DRIVER_DESC	"Fujitsu serial touchscreen driver"

MODULE_AUTHOR("Dmitry Torokhov <dtor@mail.ru>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

#define FUJITSU_LENGTH 5

/*
 * Per-touchscreen data.
 */
struct fujitsu {
	struct input_dev *dev;
	struct serio *serio;
	int idx;
	unsigned char data[FUJITSU_LENGTH];
	char phys[32];
};

/*
 * Decode serial data (5 bytes per packet)
 * First byte
 * 1 C 0 0 R S S S
 * Where C is 1 while in calibration mode (which we don't use)
 * R is 1 when no coordinate corection was done.
 * S are button state
 */
static irqreturn_t fujitsu_interrupt(struct serio *serio,
				     unsigned char data, unsigned int flags)
{
	struct fujitsu *fujitsu = serio_get_drvdata(serio);
	struct input_dev *dev = fujitsu->dev;

	if (fujitsu->idx == 0) {
		/* resync skip until start of frame */
		if ((data & 0xf0) != 0x80)
			return IRQ_HANDLED;
	} else {
		/* resync skip garbage */
		if (data & 0x80) {
			fujitsu->idx = 0;
			return IRQ_HANDLED;
		}
	}

	fujitsu->data[fujitsu->idx++] = data;
	if (fujitsu->idx == FUJITSU_LENGTH) {
		input_report_abs(dev, ABS_X,
				 (fujitsu->data[2] << 7) | fujitsu->data[1]);
		input_report_abs(dev, ABS_Y,
				 (fujitsu->data[4] << 7) | fujitsu->data[3]);
		input_report_key(dev, BTN_TOUCH,
				 (fujitsu->data[0] & 0x03) != 2);
		input_sync(dev);
		fujitsu->idx = 0;
	}

	return IRQ_HANDLED;
}

/*
 * fujitsu_disconnect() is the opposite of fujitsu_connect()
 */
static void fujitsu_disconnect(struct serio *serio)
{
	struct fujitsu *fujitsu = serio_get_drvdata(serio);

	input_get_device(fujitsu->dev);
	input_unregister_device(fujitsu->dev);
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_put_device(fujitsu->dev);
	kfree(fujitsu);
}

/*
 * fujitsu_connect() is the routine that is called when someone adds a
 * new serio device that supports the Fujitsu protocol and registers it
 * as input device.
 */
static int fujitsu_connect(struct serio *serio, struct serio_driver *drv)
{
	struct fujitsu *fujitsu;
	struct input_dev *input_dev;
	int err;

	fujitsu = kzalloc(sizeof(struct fujitsu), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!fujitsu || !input_dev) {
		err = -ENOMEM;
		goto fail1;
	}

	fujitsu->serio = serio;
	fujitsu->dev = input_dev;
	snprintf(fujitsu->phys, sizeof(fujitsu->phys),
		 "%s/input0", serio->phys);

	input_dev->name = "Fujitsu Serial Touchscreen";
	input_dev->phys = fujitsu->phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = SERIO_FUJITSU;
	input_dev->id.product = 0;
	input_dev->id.version = 0x0100;
	input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
	input_dev->keybit[LONG(BTN_TOUCH)] = BIT(BTN_TOUCH);

	input_set_abs_params(input_dev, ABS_X, 0, 4096, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, 4096, 0, 0);
	serio_set_drvdata(serio, fujitsu);

	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	err = input_register_device(fujitsu->dev);
	if (err)
		goto fail3;

	return 0;

 fail3:
	serio_close(serio);
 fail2:
	serio_set_drvdata(serio, NULL);
 fail1:
	input_free_device(input_dev);
	kfree(fujitsu);
	return err;
}

/*
 * The serio driver structure.
 */
static struct serio_device_id fujitsu_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_FUJITSU,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, fujitsu_serio_ids);

static struct serio_driver fujitsu_drv = {
	.driver		= {
		.name	= "fujitsu_ts",
	},
	.description	= DRIVER_DESC,
	.id_table	= fujitsu_serio_ids,
	.interrupt	= fujitsu_interrupt,
	.connect	= fujitsu_connect,
	.disconnect	= fujitsu_disconnect,
};

static int __init fujitsu_init(void)
{
	return serio_register_driver(&fujitsu_drv);
}

static void __exit fujitsu_exit(void)
{
	serio_unregister_driver(&fujitsu_drv);
}

module_init(fujitsu_init);
module_exit(fujitsu_exit);
