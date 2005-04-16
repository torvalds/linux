/*
 *  m68k beeper driver for Linux
 *
 *  Copyright (c) 2002 Richard Zidlicky
 *  Copyright (c) 2002 Vojtech Pavlik
 *  Copyright (c) 1992 Orest Zborowski
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <asm/machdep.h>
#include <asm/io.h>

MODULE_AUTHOR("Richard Zidlicky <rz@linux-m68k.org>");
MODULE_DESCRIPTION("m68k beeper driver");
MODULE_LICENSE("GPL");

static char m68kspkr_name[] = "m68k beeper";
static char m68kspkr_phys[] = "m68k/generic";
static struct input_dev m68kspkr_dev;

static int m68kspkr_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	unsigned int count = 0;

	if (type != EV_SND)
		return -1;

	switch (code) {
		case SND_BELL: if (value) value = 1000;
		case SND_TONE: break;
		default: return -1;
	}

	if (value > 20 && value < 32767)
		count = 1193182 / value;

	mach_beep(count, -1);

	return 0;
}

static int __init m68kspkr_init(void)
{
        if (!mach_beep){
		printk("%s: no lowlevel beep support\n", m68kspkr_name);
		return -1;
        }

	m68kspkr_dev.evbit[0] = BIT(EV_SND);
	m68kspkr_dev.sndbit[0] = BIT(SND_BELL) | BIT(SND_TONE);
	m68kspkr_dev.event = m68kspkr_event;

	m68kspkr_dev.name = m68kspkr_name;
	m68kspkr_dev.phys = m68kspkr_phys;
	m68kspkr_dev.id.bustype = BUS_HOST;
	m68kspkr_dev.id.vendor = 0x001f;
	m68kspkr_dev.id.product = 0x0001;
	m68kspkr_dev.id.version = 0x0100;

	input_register_device(&m68kspkr_dev);

        printk(KERN_INFO "input: %s\n", m68kspkr_name);

	return 0;
}

static void __exit m68kspkr_exit(void)
{
        input_unregister_device(&m68kspkr_dev);
}

module_init(m68kspkr_init);
module_exit(m68kspkr_exit);
