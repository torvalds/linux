/*
 *  PC Speaker beeper driver for Linux
 *
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
#include <asm/io.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("PC Speaker beeper driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pcspkr");

#ifdef CONFIG_X86
/* Use the global PIT lock ! */
#include <asm/i8253.h>
#else
#include <asm/8253pit.h>
static DEFINE_SPINLOCK(i8253_lock);
#endif

static int pcspkr_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	unsigned int count = 0;
	unsigned long flags;

	if (type != EV_SND)
		return -1;

	switch (code) {
		case SND_BELL: if (value) value = 1000;
		case SND_TONE: break;
		default: return -1;
	}

	if (value > 20 && value < 32767)
		count = PIT_TICK_RATE / value;

	spin_lock_irqsave(&i8253_lock, flags);

	if (count) {
		/* enable counter 2 */
		outb_p(inb_p(0x61) | 3, 0x61);
		/* set command for counter 2, 2 byte write */
		outb_p(0xB6, 0x43);
		/* select desired HZ */
		outb_p(count & 0xff, 0x42);
		outb((count >> 8) & 0xff, 0x42);
	} else {
		/* disable counter 2 */
		outb(inb_p(0x61) & 0xFC, 0x61);
	}

	spin_unlock_irqrestore(&i8253_lock, flags);

	return 0;
}

static int __devinit pcspkr_probe(struct platform_device *dev)
{
	struct input_dev *pcspkr_dev;
	int err;

	pcspkr_dev = input_allocate_device();
	if (!pcspkr_dev)
		return -ENOMEM;

	pcspkr_dev->name = "PC Speaker";
	pcspkr_dev->phys = "isa0061/input0";
	pcspkr_dev->id.bustype = BUS_ISA;
	pcspkr_dev->id.vendor = 0x001f;
	pcspkr_dev->id.product = 0x0001;
	pcspkr_dev->id.version = 0x0100;
	pcspkr_dev->dev.parent = &dev->dev;

	pcspkr_dev->evbit[0] = BIT(EV_SND);
	pcspkr_dev->sndbit[0] = BIT(SND_BELL) | BIT(SND_TONE);
	pcspkr_dev->event = pcspkr_event;

	err = input_register_device(pcspkr_dev);
	if (err) {
		input_free_device(pcspkr_dev);
		return err;
	}

	platform_set_drvdata(dev, pcspkr_dev);

	return 0;
}

static int __devexit pcspkr_remove(struct platform_device *dev)
{
	struct input_dev *pcspkr_dev = platform_get_drvdata(dev);

	input_unregister_device(pcspkr_dev);
	platform_set_drvdata(dev, NULL);
	/* turn off the speaker */
	pcspkr_event(NULL, EV_SND, SND_BELL, 0);

	return 0;
}

static int pcspkr_suspend(struct platform_device *dev, pm_message_t state)
{
	pcspkr_event(NULL, EV_SND, SND_BELL, 0);

	return 0;
}

static void pcspkr_shutdown(struct platform_device *dev)
{
	/* turn off the speaker */
	pcspkr_event(NULL, EV_SND, SND_BELL, 0);
}

static struct platform_driver pcspkr_platform_driver = {
	.driver		= {
		.name	= "pcspkr",
		.owner	= THIS_MODULE,
	},
	.probe		= pcspkr_probe,
	.remove		= __devexit_p(pcspkr_remove),
	.suspend	= pcspkr_suspend,
	.shutdown	= pcspkr_shutdown,
};


static int __init pcspkr_init(void)
{
	return platform_driver_register(&pcspkr_platform_driver);
}

static void __exit pcspkr_exit(void)
{
	platform_driver_unregister(&pcspkr_platform_driver);
}

module_init(pcspkr_init);
module_exit(pcspkr_exit);
