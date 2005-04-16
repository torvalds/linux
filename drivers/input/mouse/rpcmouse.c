/*
 *  Acorn RiscPC mouse driver for Linux/ARM
 *
 *  Copyright (c) 2000-2002 Vojtech Pavlik
 *  Copyright (C) 1996-2002 Russell King
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This handles the Acorn RiscPCs mouse.  We basically have a couple of
 * hardware registers that track the sensor count for the X-Y movement and
 * another register holding the button state.  On every VSYNC interrupt we read
 * the complete state and then work out if something has changed.
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/input.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/hardware/iomd.h>

MODULE_AUTHOR("Vojtech Pavlik, Russell King");
MODULE_DESCRIPTION("Acorn RiscPC mouse driver");
MODULE_LICENSE("GPL");

static short rpcmouse_lastx, rpcmouse_lasty;

static struct input_dev rpcmouse_dev = {
	.evbit	= { BIT(EV_KEY) | BIT(EV_REL) },
	.keybit = { [LONG(BTN_LEFT)] = BIT(BTN_LEFT) | BIT(BTN_MIDDLE) | BIT(BTN_RIGHT) },
	.relbit	= { BIT(REL_X) | BIT(REL_Y) },
	.name	= "Acorn RiscPC Mouse",
	.phys	= "rpcmouse/input0",
	.id	= {
		.bustype = BUS_HOST,
		.vendor  = 0x0005,
		.product = 0x0001,
		.version = 0x0100,
	},
};

static irqreturn_t rpcmouse_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	struct input_dev *dev = dev_id;
	short x, y, dx, dy, b;

	x = (short) iomd_readl(IOMD_MOUSEX);
	y = (short) iomd_readl(IOMD_MOUSEY);
	b = (short) (__raw_readl(0xe0310000) ^ 0x70);

	dx = x - rpcmouse_lastx;
	dy = y - rpcmouse_lasty; 

	rpcmouse_lastx = x;
	rpcmouse_lasty = y;

	input_regs(dev, regs);

	input_report_rel(dev, REL_X, dx);
	input_report_rel(dev, REL_Y, -dy);

	input_report_key(dev, BTN_LEFT,   b & 0x40);
	input_report_key(dev, BTN_MIDDLE, b & 0x20);
	input_report_key(dev, BTN_RIGHT,  b & 0x10);

	input_sync(dev);

	return IRQ_HANDLED;
}

static int __init rpcmouse_init(void)
{
	init_input_dev(&rpcmouse_dev);

	rpcmouse_lastx = (short) iomd_readl(IOMD_MOUSEX);
	rpcmouse_lasty = (short) iomd_readl(IOMD_MOUSEY);

	if (request_irq(IRQ_VSYNCPULSE, rpcmouse_irq, SA_SHIRQ, "rpcmouse", &rpcmouse_dev)) {
		printk(KERN_ERR "rpcmouse: unable to allocate VSYNC interrupt\n");
		return -1;
	}

	input_register_device(&rpcmouse_dev);

	printk(KERN_INFO "input: Acorn RiscPC mouse\n");

	return 0;
}

static void __exit rpcmouse_exit(void)
{
	input_unregister_device(&rpcmouse_dev);
	free_irq(IRQ_VSYNCPULSE, &rpcmouse_dev);
}

module_init(rpcmouse_init);
module_exit(rpcmouse_exit);
