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
#include <linux/platform_device.h>
#include <asm/machdep.h>
#include <asm/io.h>

MODULE_AUTHOR("Richard Zidlicky <rz@linux-m68k.org>");
MODULE_DESCRIPTION("m68k beeper driver");
MODULE_LICENSE("GPL");

static struct platform_device *m68kspkr_platform_device;

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

static int m68kspkr_probe(struct platform_device *dev)
{
	struct input_dev *input_dev;
	int err;

	input_dev = input_allocate_device();
	if (!input_dev)
		return -ENOMEM;

	input_dev->name = "m68k beeper";
	input_dev->phys = "m68k/generic";
	input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor  = 0x001f;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &dev->dev;

	input_dev->evbit[0] = BIT_MASK(EV_SND);
	input_dev->sndbit[0] = BIT_MASK(SND_BELL) | BIT_MASK(SND_TONE);
	input_dev->event = m68kspkr_event;

	err = input_register_device(input_dev);
	if (err) {
		input_free_device(input_dev);
		return err;
	}

	platform_set_drvdata(dev, input_dev);

	return 0;
}

static int m68kspkr_remove(struct platform_device *dev)
{
	struct input_dev *input_dev = platform_get_drvdata(dev);

	input_unregister_device(input_dev);
	/* turn off the speaker */
	m68kspkr_event(NULL, EV_SND, SND_BELL, 0);

	return 0;
}

static void m68kspkr_shutdown(struct platform_device *dev)
{
	/* turn off the speaker */
	m68kspkr_event(NULL, EV_SND, SND_BELL, 0);
}

static struct platform_driver m68kspkr_platform_driver = {
	.driver		= {
		.name	= "m68kspkr",
	},
	.probe		= m68kspkr_probe,
	.remove		= m68kspkr_remove,
	.shutdown	= m68kspkr_shutdown,
};

static int __init m68kspkr_init(void)
{
	int err;

	if (!mach_beep) {
		printk(KERN_INFO "m68kspkr: no lowlevel beep support\n");
		return -ENODEV;
        }

	err = platform_driver_register(&m68kspkr_platform_driver);
	if (err)
		return err;

	m68kspkr_platform_device = platform_device_alloc("m68kspkr", -1);
	if (!m68kspkr_platform_device) {
		err = -ENOMEM;
		goto err_unregister_driver;
	}

	err = platform_device_add(m68kspkr_platform_device);
	if (err)
		goto err_free_device;

	return 0;

 err_free_device:
	platform_device_put(m68kspkr_platform_device);
 err_unregister_driver:
	platform_driver_unregister(&m68kspkr_platform_driver);

	return err;
}

static void __exit m68kspkr_exit(void)
{
	platform_device_unregister(m68kspkr_platform_device);
	platform_driver_unregister(&m68kspkr_platform_driver);
}

module_init(m68kspkr_init);
module_exit(m68kspkr_exit);
