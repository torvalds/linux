/*
 * $Id: logibm.c,v 1.11 2001/09/25 10:12:07 vojtech Exp $
 *
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 *
 *  Based on the work of:
 *	James Banks		Matthew Dillon
 *	David Giller		Nathan Laredo
 *	Linus Torvalds		Johan Myreen
 *	Cliff Matthews		Philip Blundell
 *	Russell King
 */

/*
 * Logitech Bus Mouse Driver for Linux
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>

#include <asm/io.h>
#include <asm/irq.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Logitech busmouse driver");
MODULE_LICENSE("GPL");

#define	LOGIBM_BASE		0x23c
#define	LOGIBM_EXTENT		4

#define	LOGIBM_DATA_PORT	LOGIBM_BASE + 0
#define	LOGIBM_SIGNATURE_PORT	LOGIBM_BASE + 1
#define	LOGIBM_CONTROL_PORT	LOGIBM_BASE + 2
#define	LOGIBM_CONFIG_PORT	LOGIBM_BASE + 3

#define	LOGIBM_ENABLE_IRQ	0x00
#define	LOGIBM_DISABLE_IRQ	0x10
#define	LOGIBM_READ_X_LOW	0x80
#define	LOGIBM_READ_X_HIGH	0xa0
#define	LOGIBM_READ_Y_LOW	0xc0
#define	LOGIBM_READ_Y_HIGH	0xe0

#define LOGIBM_DEFAULT_MODE	0x90
#define LOGIBM_CONFIG_BYTE	0x91
#define LOGIBM_SIGNATURE_BYTE	0xa5

#define LOGIBM_IRQ		5

static int logibm_irq = LOGIBM_IRQ;
module_param_named(irq, logibm_irq, uint, 0);
MODULE_PARM_DESC(irq, "IRQ number (5=default)");

static struct input_dev *logibm_dev;

static irqreturn_t logibm_interrupt(int irq, void *dev_id)
{
	char dx, dy;
	unsigned char buttons;

	outb(LOGIBM_READ_X_LOW, LOGIBM_CONTROL_PORT);
	dx = (inb(LOGIBM_DATA_PORT) & 0xf);
	outb(LOGIBM_READ_X_HIGH, LOGIBM_CONTROL_PORT);
	dx |= (inb(LOGIBM_DATA_PORT) & 0xf) << 4;
	outb(LOGIBM_READ_Y_LOW, LOGIBM_CONTROL_PORT);
	dy = (inb(LOGIBM_DATA_PORT) & 0xf);
	outb(LOGIBM_READ_Y_HIGH, LOGIBM_CONTROL_PORT);
	buttons = inb(LOGIBM_DATA_PORT);
	dy |= (buttons & 0xf) << 4;
	buttons = ~buttons >> 5;

	input_report_rel(logibm_dev, REL_X, dx);
	input_report_rel(logibm_dev, REL_Y, dy);
	input_report_key(logibm_dev, BTN_RIGHT,  buttons & 1);
	input_report_key(logibm_dev, BTN_MIDDLE, buttons & 2);
	input_report_key(logibm_dev, BTN_LEFT,   buttons & 4);
	input_sync(logibm_dev);

	outb(LOGIBM_ENABLE_IRQ, LOGIBM_CONTROL_PORT);
	return IRQ_HANDLED;
}

static int logibm_open(struct input_dev *dev)
{
	if (request_irq(logibm_irq, logibm_interrupt, 0, "logibm", NULL)) {
		printk(KERN_ERR "logibm.c: Can't allocate irq %d\n", logibm_irq);
		return -EBUSY;
	}
	outb(LOGIBM_ENABLE_IRQ, LOGIBM_CONTROL_PORT);
	return 0;
}

static void logibm_close(struct input_dev *dev)
{
	outb(LOGIBM_DISABLE_IRQ, LOGIBM_CONTROL_PORT);
	free_irq(logibm_irq, NULL);
}

static int __init logibm_init(void)
{
	int err;

	if (!request_region(LOGIBM_BASE, LOGIBM_EXTENT, "logibm")) {
		printk(KERN_ERR "logibm.c: Can't allocate ports at %#x\n", LOGIBM_BASE);
		return -EBUSY;
	}

	outb(LOGIBM_CONFIG_BYTE, LOGIBM_CONFIG_PORT);
	outb(LOGIBM_SIGNATURE_BYTE, LOGIBM_SIGNATURE_PORT);
	udelay(100);

	if (inb(LOGIBM_SIGNATURE_PORT) != LOGIBM_SIGNATURE_BYTE) {
		printk(KERN_INFO "logibm.c: Didn't find Logitech busmouse at %#x\n", LOGIBM_BASE);
		err = -ENODEV;
		goto err_release_region;
	}

	outb(LOGIBM_DEFAULT_MODE, LOGIBM_CONFIG_PORT);
	outb(LOGIBM_DISABLE_IRQ, LOGIBM_CONTROL_PORT);

	logibm_dev = input_allocate_device();
	if (!logibm_dev) {
		printk(KERN_ERR "logibm.c: Not enough memory for input device\n");
		err = -ENOMEM;
		goto err_release_region;
	}

	logibm_dev->name = "Logitech bus mouse";
	logibm_dev->phys = "isa023c/input0";
	logibm_dev->id.bustype = BUS_ISA;
	logibm_dev->id.vendor  = 0x0003;
	logibm_dev->id.product = 0x0001;
	logibm_dev->id.version = 0x0100;

	logibm_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
	logibm_dev->keybit[BIT_WORD(BTN_LEFT)] = BIT_MASK(BTN_LEFT) |
		BIT_MASK(BTN_MIDDLE) | BIT_MASK(BTN_RIGHT);
	logibm_dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);

	logibm_dev->open  = logibm_open;
	logibm_dev->close = logibm_close;

	err = input_register_device(logibm_dev);
	if (err)
		goto err_free_dev;

	return 0;

 err_free_dev:
	input_free_device(logibm_dev);
 err_release_region:
	release_region(LOGIBM_BASE, LOGIBM_EXTENT);

	return err;
}

static void __exit logibm_exit(void)
{
	input_unregister_device(logibm_dev);
	release_region(LOGIBM_BASE, LOGIBM_EXTENT);
}

module_init(logibm_init);
module_exit(logibm_exit);
