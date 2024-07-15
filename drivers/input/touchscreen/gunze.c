// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 */

/*
 * Gunze AHL-51S touchscreen driver for Linux
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>

#define DRIVER_DESC	"Gunze AHL-51S touchscreen driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Definitions & global arrays.
 */

#define	GUNZE_MAX_LENGTH	10

/*
 * Per-touchscreen data.
 */

struct gunze {
	struct input_dev *dev;
	struct serio *serio;
	int idx;
	unsigned char data[GUNZE_MAX_LENGTH];
	char phys[32];
};

static void gunze_process_packet(struct gunze *gunze)
{
	struct input_dev *dev = gunze->dev;

	if (gunze->idx != GUNZE_MAX_LENGTH || gunze->data[5] != ',' ||
		(gunze->data[0] != 'T' && gunze->data[0] != 'R')) {
		printk(KERN_WARNING "gunze.c: bad packet: >%.*s<\n", GUNZE_MAX_LENGTH, gunze->data);
		return;
	}

	input_report_abs(dev, ABS_X, simple_strtoul(gunze->data + 1, NULL, 10));
	input_report_abs(dev, ABS_Y, 1024 - simple_strtoul(gunze->data + 6, NULL, 10));
	input_report_key(dev, BTN_TOUCH, gunze->data[0] == 'T');
	input_sync(dev);
}

static irqreturn_t gunze_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags)
{
	struct gunze *gunze = serio_get_drvdata(serio);

	if (data == '\r') {
		gunze_process_packet(gunze);
		gunze->idx = 0;
	} else {
		if (gunze->idx < GUNZE_MAX_LENGTH)
			gunze->data[gunze->idx++] = data;
	}
	return IRQ_HANDLED;
}

/*
 * gunze_disconnect() is the opposite of gunze_connect()
 */

static void gunze_disconnect(struct serio *serio)
{
	struct gunze *gunze = serio_get_drvdata(serio);

	input_get_device(gunze->dev);
	input_unregister_device(gunze->dev);
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_put_device(gunze->dev);
	kfree(gunze);
}

/*
 * gunze_connect() is the routine that is called when someone adds a
 * new serio device that supports Gunze protocol and registers it as
 * an input device.
 */

static int gunze_connect(struct serio *serio, struct serio_driver *drv)
{
	struct gunze *gunze;
	struct input_dev *input_dev;
	int err;

	gunze = kzalloc(sizeof(*gunze), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!gunze || !input_dev) {
		err = -ENOMEM;
		goto fail1;
	}

	gunze->serio = serio;
	gunze->dev = input_dev;
	snprintf(gunze->phys, sizeof(serio->phys), "%s/input0", serio->phys);

	input_dev->name = "Gunze AHL-51S TouchScreen";
	input_dev->phys = gunze->phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = SERIO_GUNZE;
	input_dev->id.product = 0x0051;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &serio->dev;
	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_set_abs_params(input_dev, ABS_X, 24, 1000, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 24, 1000, 0, 0);

	serio_set_drvdata(serio, gunze);

	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	err = input_register_device(gunze->dev);
	if (err)
		goto fail3;

	return 0;

 fail3:	serio_close(serio);
 fail2:	serio_set_drvdata(serio, NULL);
 fail1:	input_free_device(input_dev);
	kfree(gunze);
	return err;
}

/*
 * The serio driver structure.
 */

static const struct serio_device_id gunze_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_GUNZE,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, gunze_serio_ids);

static struct serio_driver gunze_drv = {
	.driver		= {
		.name	= "gunze",
	},
	.description	= DRIVER_DESC,
	.id_table	= gunze_serio_ids,
	.interrupt	= gunze_interrupt,
	.connect	= gunze_connect,
	.disconnect	= gunze_disconnect,
};

module_serio_driver(gunze_drv);
