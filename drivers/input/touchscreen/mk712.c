/*
 * ICS MK712 touchscreen controller driver
 *
 * Copyright (c) 1999-2002 Transmeta Corporation
 * Copyright (c) 2005 Rick Koch <n1gp@hotmail.com>
 * Copyright (c) 2005 Vojtech Pavlik <vojtech@suse.cz>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

/*
 * This driver supports the ICS MicroClock MK712 TouchScreen controller,
 * found in Gateway AOL Connected Touchpad computers.
 *
 * Documentation for ICS MK712 can be found at:
 *	http://www.idt.com/products/getDoc.cfm?docID=18713923
 */

/*
 * 1999-12-18: original version, Daniel Quinlan
 * 1999-12-19: added anti-jitter code, report pen-up events, fixed mk712_poll
 *             to use queue_empty, Nathan Laredo
 * 1999-12-20: improved random point rejection, Nathan Laredo
 * 2000-01-05: checked in new anti-jitter code, changed mouse protocol, fixed
 *             queue code, added module options, other fixes, Daniel Quinlan
 * 2002-03-15: Clean up for kernel merge <alan@redhat.com>
 *             Fixed multi open race, fixed memory checks, fixed resource
 *             allocation, fixed close/powerdown bug, switched to new init
 * 2005-01-18: Ported to 2.6 from 2.4.28, Rick Koch
 * 2005-02-05: Rewritten for the input layer, Vojtech Pavlik
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <asm/io.h>

MODULE_AUTHOR("Daniel Quinlan <quinlan@pathname.com>, Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("ICS MicroClock MK712 TouchScreen driver");
MODULE_LICENSE("GPL");

static unsigned int mk712_io = 0x260;	/* Also 0x200, 0x208, 0x300 */
module_param_named(io, mk712_io, uint, 0);
MODULE_PARM_DESC(io, "I/O base address of MK712 touchscreen controller");

static unsigned int mk712_irq = 10;	/* Also 12, 14, 15 */
module_param_named(irq, mk712_irq, uint, 0);
MODULE_PARM_DESC(irq, "IRQ of MK712 touchscreen controller");

/* eight 8-bit registers */
#define MK712_STATUS		0
#define MK712_X			2
#define MK712_Y			4
#define MK712_CONTROL		6
#define MK712_RATE		7

/* status */
#define	MK712_STATUS_TOUCH			0x10
#define	MK712_CONVERSION_COMPLETE		0x80

/* control */
#define MK712_ENABLE_INT			0x01
#define MK712_INT_ON_CONVERSION_COMPLETE	0x02
#define MK712_INT_ON_CHANGE_IN_TOUCH_STATUS	0x04
#define MK712_ENABLE_PERIODIC_CONVERSIONS	0x10
#define MK712_READ_ONE_POINT			0x20
#define MK712_POWERUP				0x40

static struct input_dev *mk712_dev;
static DEFINE_SPINLOCK(mk712_lock);

static irqreturn_t mk712_interrupt(int irq, void *dev_id)
{
	unsigned char status;
	static int debounce = 1;
	static unsigned short last_x;
	static unsigned short last_y;

	spin_lock(&mk712_lock);

	status = inb(mk712_io + MK712_STATUS);

	if (~status & MK712_CONVERSION_COMPLETE) {
		debounce = 1;
		goto end;
	}

	if (~status & MK712_STATUS_TOUCH) {
		debounce = 1;
		input_report_key(mk712_dev, BTN_TOUCH, 0);
		goto end;
	}

	if (debounce) {
		debounce = 0;
		goto end;
	}

	input_report_key(mk712_dev, BTN_TOUCH, 1);
	input_report_abs(mk712_dev, ABS_X, last_x);
	input_report_abs(mk712_dev, ABS_Y, last_y);

 end:
	last_x = inw(mk712_io + MK712_X) & 0x0fff;
	last_y = inw(mk712_io + MK712_Y) & 0x0fff;
	input_sync(mk712_dev);
	spin_unlock(&mk712_lock);
	return IRQ_HANDLED;
}

static int mk712_open(struct input_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&mk712_lock, flags);

	outb(0, mk712_io + MK712_CONTROL); /* Reset */

	outb(MK712_ENABLE_INT | MK712_INT_ON_CONVERSION_COMPLETE |
		MK712_INT_ON_CHANGE_IN_TOUCH_STATUS |
		MK712_ENABLE_PERIODIC_CONVERSIONS |
		MK712_POWERUP, mk712_io + MK712_CONTROL);

	outb(10, mk712_io + MK712_RATE); /* 187 points per second */

	spin_unlock_irqrestore(&mk712_lock, flags);

	return 0;
}

static void mk712_close(struct input_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&mk712_lock, flags);

	outb(0, mk712_io + MK712_CONTROL);

	spin_unlock_irqrestore(&mk712_lock, flags);
}

static int __init mk712_init(void)
{
	int err;

	if (!request_region(mk712_io, 8, "mk712")) {
		printk(KERN_WARNING "mk712: unable to get IO region\n");
		return -ENODEV;
	}

	outb(0, mk712_io + MK712_CONTROL);

	if ((inw(mk712_io + MK712_X) & 0xf000) ||	/* Sanity check */
	    (inw(mk712_io + MK712_Y) & 0xf000) ||
	    (inw(mk712_io + MK712_STATUS) & 0xf333)) {
		printk(KERN_WARNING "mk712: device not present\n");
		err = -ENODEV;
		goto fail1;
	}

	mk712_dev = input_allocate_device();
	if (!mk712_dev) {
		printk(KERN_ERR "mk712: not enough memory\n");
		err = -ENOMEM;
		goto fail1;
	}

	mk712_dev->name = "ICS MicroClock MK712 TouchScreen";
	mk712_dev->phys = "isa0260/input0";
	mk712_dev->id.bustype = BUS_ISA;
	mk712_dev->id.vendor  = 0x0005;
	mk712_dev->id.product = 0x0001;
	mk712_dev->id.version = 0x0100;

	mk712_dev->open    = mk712_open;
	mk712_dev->close   = mk712_close;

	mk712_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	mk712_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_set_abs_params(mk712_dev, ABS_X, 0, 0xfff, 88, 0);
	input_set_abs_params(mk712_dev, ABS_Y, 0, 0xfff, 88, 0);

	if (request_irq(mk712_irq, mk712_interrupt, 0, "mk712", mk712_dev)) {
		printk(KERN_WARNING "mk712: unable to get IRQ\n");
		err = -EBUSY;
		goto fail1;
	}

	err = input_register_device(mk712_dev);
	if (err)
		goto fail2;

	return 0;

 fail2:	free_irq(mk712_irq, mk712_dev);
 fail1:	input_free_device(mk712_dev);
	release_region(mk712_io, 8);
	return err;
}

static void __exit mk712_exit(void)
{
	input_unregister_device(mk712_dev);
	free_irq(mk712_irq, mk712_dev);
	release_region(mk712_io, 8);
}

module_init(mk712_init);
module_exit(mk712_exit);
