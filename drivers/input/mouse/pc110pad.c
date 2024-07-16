// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Alan Cox	Robin O'Leary
 */

/*
 * IBM PC110 touchpad driver for Linux
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/irq.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("IBM PC110 touchpad driver");
MODULE_LICENSE("GPL");

#define PC110PAD_OFF	0x30
#define PC110PAD_ON	0x38

static int pc110pad_irq = 10;
static int pc110pad_io = 0x15e0;

static struct input_dev *pc110pad_dev;
static int pc110pad_data[3];
static int pc110pad_count;

static irqreturn_t pc110pad_interrupt(int irq, void *ptr)
{
	int value     = inb_p(pc110pad_io);
	int handshake = inb_p(pc110pad_io + 2);

	outb(handshake |  1, pc110pad_io + 2);
	udelay(2);
	outb(handshake & ~1, pc110pad_io + 2);
	udelay(2);
	inb_p(0x64);

	pc110pad_data[pc110pad_count++] = value;

	if (pc110pad_count < 3)
		return IRQ_HANDLED;

	input_report_key(pc110pad_dev, BTN_TOUCH,
		pc110pad_data[0] & 0x01);
	input_report_abs(pc110pad_dev, ABS_X,
		pc110pad_data[1] | ((pc110pad_data[0] << 3) & 0x80) | ((pc110pad_data[0] << 1) & 0x100));
	input_report_abs(pc110pad_dev, ABS_Y,
		pc110pad_data[2] | ((pc110pad_data[0] << 4) & 0x80));
	input_sync(pc110pad_dev);

	pc110pad_count = 0;
	return IRQ_HANDLED;
}

static void pc110pad_close(struct input_dev *dev)
{
	outb(PC110PAD_OFF, pc110pad_io + 2);
}

static int pc110pad_open(struct input_dev *dev)
{
	pc110pad_interrupt(0, NULL);
	pc110pad_interrupt(0, NULL);
	pc110pad_interrupt(0, NULL);
	outb(PC110PAD_ON, pc110pad_io + 2);
	pc110pad_count = 0;

	return 0;
}

/*
 * We try to avoid enabling the hardware if it's not
 * there, but we don't know how to test. But we do know
 * that the PC110 is not a PCI system. So if we find any
 * PCI devices in the machine, we don't have a PC110.
 */
static int __init pc110pad_init(void)
{
	int err;

	if (!no_pci_devices())
		return -ENODEV;

	if (!request_region(pc110pad_io, 4, "pc110pad")) {
		printk(KERN_ERR "pc110pad: I/O area %#x-%#x in use.\n",
				pc110pad_io, pc110pad_io + 4);
		return -EBUSY;
	}

	outb(PC110PAD_OFF, pc110pad_io + 2);

	if (request_irq(pc110pad_irq, pc110pad_interrupt, 0, "pc110pad", NULL)) {
		printk(KERN_ERR "pc110pad: Unable to get irq %d.\n", pc110pad_irq);
		err = -EBUSY;
		goto err_release_region;
	}

	pc110pad_dev = input_allocate_device();
	if (!pc110pad_dev) {
		printk(KERN_ERR "pc110pad: Not enough memory.\n");
		err = -ENOMEM;
		goto err_free_irq;
	}

	pc110pad_dev->name = "IBM PC110 TouchPad";
	pc110pad_dev->phys = "isa15e0/input0";
	pc110pad_dev->id.bustype = BUS_ISA;
	pc110pad_dev->id.vendor = 0x0003;
	pc110pad_dev->id.product = 0x0001;
	pc110pad_dev->id.version = 0x0100;

	pc110pad_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	pc110pad_dev->absbit[0] = BIT_MASK(ABS_X) | BIT_MASK(ABS_Y);
	pc110pad_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	input_abs_set_max(pc110pad_dev, ABS_X, 0x1ff);
	input_abs_set_max(pc110pad_dev, ABS_Y, 0x0ff);

	pc110pad_dev->open = pc110pad_open;
	pc110pad_dev->close = pc110pad_close;

	err = input_register_device(pc110pad_dev);
	if (err)
		goto err_free_dev;

	return 0;

 err_free_dev:
	input_free_device(pc110pad_dev);
 err_free_irq:
	free_irq(pc110pad_irq, NULL);
 err_release_region:
	release_region(pc110pad_io, 4);

	return err;
}

static void __exit pc110pad_exit(void)
{
	outb(PC110PAD_OFF, pc110pad_io + 2);
	free_irq(pc110pad_irq, NULL);
	input_unregister_device(pc110pad_dev);
	release_region(pc110pad_io, 4);
}

module_init(pc110pad_init);
module_exit(pc110pad_exit);
