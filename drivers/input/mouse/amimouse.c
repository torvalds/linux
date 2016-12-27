/*
 *  Amiga mouse driver for Linux/m68k
 *
 *  Copyright (c) 2000-2002 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Michael Rausch		James Banks
 *	Matther Dillon		David Giller
 *	Nathan Laredo		Linus Torvalds
 *	Johan Myreen		Jes Sorensen
 *	Russell King
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <asm/irq.h>
#include <asm/setup.h>
#include <linux/uaccess.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Amiga mouse driver");
MODULE_LICENSE("GPL");

static int amimouse_lastx, amimouse_lasty;

static irqreturn_t amimouse_interrupt(int irq, void *data)
{
	struct input_dev *dev = data;
	unsigned short joy0dat, potgor;
	int nx, ny, dx, dy;

	joy0dat = amiga_custom.joy0dat;

	nx = joy0dat & 0xff;
	ny = joy0dat >> 8;

	dx = nx - amimouse_lastx;
	dy = ny - amimouse_lasty;

	if (dx < -127) dx = (256 + nx) - amimouse_lastx;
	if (dx >  127) dx = (nx - 256) - amimouse_lastx;
	if (dy < -127) dy = (256 + ny) - amimouse_lasty;
	if (dy >  127) dy = (ny - 256) - amimouse_lasty;

	amimouse_lastx = nx;
	amimouse_lasty = ny;

	potgor = amiga_custom.potgor;

	input_report_rel(dev, REL_X, dx);
	input_report_rel(dev, REL_Y, dy);

	input_report_key(dev, BTN_LEFT,   ciaa.pra & 0x40);
	input_report_key(dev, BTN_MIDDLE, potgor & 0x0100);
	input_report_key(dev, BTN_RIGHT,  potgor & 0x0400);

	input_sync(dev);

	return IRQ_HANDLED;
}

static int amimouse_open(struct input_dev *dev)
{
	unsigned short joy0dat;
	int error;

	joy0dat = amiga_custom.joy0dat;

	amimouse_lastx = joy0dat & 0xff;
	amimouse_lasty = joy0dat >> 8;

	error = request_irq(IRQ_AMIGA_VERTB, amimouse_interrupt, 0, "amimouse",
			    dev);
	if (error)
		dev_err(&dev->dev, "Can't allocate irq %d\n", IRQ_AMIGA_VERTB);

	return error;
}

static void amimouse_close(struct input_dev *dev)
{
	free_irq(IRQ_AMIGA_VERTB, dev);
}

static int __init amimouse_probe(struct platform_device *pdev)
{
	int err;
	struct input_dev *dev;

	dev = input_allocate_device();
	if (!dev)
		return -ENOMEM;

	dev->name = pdev->name;
	dev->phys = "amimouse/input0";
	dev->id.bustype = BUS_AMIGA;
	dev->id.vendor = 0x0001;
	dev->id.product = 0x0002;
	dev->id.version = 0x0100;

	dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
	dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);
	dev->keybit[BIT_WORD(BTN_LEFT)] = BIT_MASK(BTN_LEFT) |
		BIT_MASK(BTN_MIDDLE) | BIT_MASK(BTN_RIGHT);
	dev->open = amimouse_open;
	dev->close = amimouse_close;
	dev->dev.parent = &pdev->dev;

	err = input_register_device(dev);
	if (err) {
		input_free_device(dev);
		return err;
	}

	platform_set_drvdata(pdev, dev);

	return 0;
}

static int __exit amimouse_remove(struct platform_device *pdev)
{
	struct input_dev *dev = platform_get_drvdata(pdev);

	input_unregister_device(dev);
	return 0;
}

static struct platform_driver amimouse_driver = {
	.remove = __exit_p(amimouse_remove),
	.driver   = {
		.name	= "amiga-mouse",
	},
};

module_platform_driver_probe(amimouse_driver, amimouse_probe);

MODULE_ALIAS("platform:amiga-mouse");
