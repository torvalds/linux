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

#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Amiga mouse driver");
MODULE_LICENSE("GPL");

static int amimouse_used = 0;
static int amimouse_lastx, amimouse_lasty;
static struct input_dev amimouse_dev;

static char *amimouse_name = "Amiga mouse";
static char *amimouse_phys = "amimouse/input0";

static irqreturn_t amimouse_interrupt(int irq, void *dummy, struct pt_regs *fp)
{
	unsigned short joy0dat, potgor;
	int nx, ny, dx, dy;

	joy0dat = custom.joy0dat;

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

	potgor = custom.potgor;

	input_regs(&amimouse_dev, fp);

	input_report_rel(&amimouse_dev, REL_X, dx);
	input_report_rel(&amimouse_dev, REL_Y, dy);

	input_report_key(&amimouse_dev, BTN_LEFT,   ciaa.pra & 0x40);
	input_report_key(&amimouse_dev, BTN_MIDDLE, potgor & 0x0100);
	input_report_key(&amimouse_dev, BTN_RIGHT,  potgor & 0x0400);

	input_sync(&amimouse_dev);

	return IRQ_HANDLED;
}

static int amimouse_open(struct input_dev *dev)
{
	unsigned short joy0dat;

        if (amimouse_used++)
                return 0;

	joy0dat = custom.joy0dat;

	amimouse_lastx = joy0dat & 0xff;
	amimouse_lasty = joy0dat >> 8;

	if (request_irq(IRQ_AMIGA_VERTB, amimouse_interrupt, 0, "amimouse", amimouse_interrupt)) {
                amimouse_used--;
                printk(KERN_ERR "amimouse.c: Can't allocate irq %d\n", IRQ_AMIGA_VERTB);
                return -EBUSY;
        }

        return 0;
}

static void amimouse_close(struct input_dev *dev)
{
        if (!--amimouse_used)
		free_irq(IRQ_AMIGA_VERTB, amimouse_interrupt);
}

static int __init amimouse_init(void)
{
	if (!MACH_IS_AMIGA || !AMIGAHW_PRESENT(AMI_MOUSE))
		return -ENODEV;

	amimouse_dev.evbit[0] = BIT(EV_KEY) | BIT(EV_REL);
	amimouse_dev.relbit[0] = BIT(REL_X) | BIT(REL_Y);
	amimouse_dev.keybit[LONG(BTN_LEFT)] = BIT(BTN_LEFT) | BIT(BTN_MIDDLE) | BIT(BTN_RIGHT);
	amimouse_dev.open = amimouse_open;
	amimouse_dev.close = amimouse_close;

	amimouse_dev.name = amimouse_name;
	amimouse_dev.phys = amimouse_phys;
	amimouse_dev.id.bustype = BUS_AMIGA;
	amimouse_dev.id.vendor = 0x0001;
	amimouse_dev.id.product = 0x0002;
	amimouse_dev.id.version = 0x0100;

	input_register_device(&amimouse_dev);

        printk(KERN_INFO "input: %s at joy0dat\n", amimouse_name);
	return 0;
}

static void __exit amimouse_exit(void)
{
        input_unregister_device(&amimouse_dev);
}

module_init(amimouse_init);
module_exit(amimouse_exit);
