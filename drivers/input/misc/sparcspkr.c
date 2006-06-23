/*
 *  Driver for PC-speaker like devices found on various Sparc systems.
 *
 *  Copyright (c) 2002 Vojtech Pavlik
 *  Copyright (c) 2002 David S. Miller (davem@redhat.com)
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/ebus.h>
#ifdef CONFIG_SPARC64
#include <asm/isa.h>
#endif

MODULE_AUTHOR("David S. Miller <davem@redhat.com>");
MODULE_DESCRIPTION("Sparc Speaker beeper driver");
MODULE_LICENSE("GPL");

const char *beep_name;
static unsigned long beep_iobase;
static int (*beep_event)(struct input_dev *dev, unsigned int type, unsigned int code, int value);
static DEFINE_SPINLOCK(beep_lock);

static int ebus_spkr_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
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
		count = 1193182 / value;

	spin_lock_irqsave(&beep_lock, flags);

	/* EBUS speaker only has on/off state, the frequency does not
	 * appear to be programmable.
	 */
	if (beep_iobase & 0x2UL)
		outb(!!count, beep_iobase);
	else
		outl(!!count, beep_iobase);

	spin_unlock_irqrestore(&beep_lock, flags);

	return 0;
}

#ifdef CONFIG_SPARC64
static int isa_spkr_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
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
		count = 1193182 / value;

	spin_lock_irqsave(&beep_lock, flags);

	if (count) {
		/* enable counter 2 */
		outb(inb(beep_iobase + 0x61) | 3, beep_iobase + 0x61);
		/* set command for counter 2, 2 byte write */
		outb(0xB6, beep_iobase + 0x43);
		/* select desired HZ */
		outb(count & 0xff, beep_iobase + 0x42);
		outb((count >> 8) & 0xff, beep_iobase + 0x42);
	} else {
		/* disable counter 2 */
		outb(inb_p(beep_iobase + 0x61) & 0xFC, beep_iobase + 0x61);
	}

	spin_unlock_irqrestore(&beep_lock, flags);

	return 0;
}
#endif

static int __devinit sparcspkr_probe(struct platform_device *dev)
{
	struct input_dev *input_dev;
	int error;

	input_dev = input_allocate_device();
	if (!input_dev)
		return -ENOMEM;

	input_dev->name = beep_name;
	input_dev->phys = "sparc/input0";
	input_dev->id.bustype = BUS_ISA;
	input_dev->id.vendor = 0x001f;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;
	input_dev->cdev.dev = &dev->dev;

	input_dev->evbit[0] = BIT(EV_SND);
	input_dev->sndbit[0] = BIT(SND_BELL) | BIT(SND_TONE);

	input_dev->event = beep_event;

	error = input_register_device(input_dev);
	if (error) {
		input_free_device(input_dev);
		return error;
	}

	platform_set_drvdata(dev, input_dev);

	return 0;
}

static int __devexit sparcspkr_remove(struct platform_device *dev)
{
	struct input_dev *input_dev = platform_get_drvdata(dev);

	input_unregister_device(input_dev);
	platform_set_drvdata(dev, NULL);
	/* turn off the speaker */
	beep_event(NULL, EV_SND, SND_BELL, 0);

	return 0;
}

static void sparcspkr_shutdown(struct platform_device *dev)
{
	/* turn off the speaker */
	beep_event(NULL, EV_SND, SND_BELL, 0);
}

static struct platform_driver sparcspkr_platform_driver = {
	.driver		= {
		.name	= "sparcspkr",
		.owner	= THIS_MODULE,
	},
	.probe		= sparcspkr_probe,
	.remove		= __devexit_p(sparcspkr_remove),
	.shutdown	= sparcspkr_shutdown,
};

static struct platform_device *sparcspkr_platform_device;

static int __init sparcspkr_drv_init(void)
{
	int error;

	error = platform_driver_register(&sparcspkr_platform_driver);
	if (error)
		return error;

	sparcspkr_platform_device = platform_device_alloc("sparcspkr", -1);
	if (!sparcspkr_platform_device) {
		error = -ENOMEM;
		goto err_unregister_driver;
	}

	error = platform_device_add(sparcspkr_platform_device);
	if (error)
		goto err_free_device;

	return 0;

 err_free_device:
	platform_device_put(sparcspkr_platform_device);
 err_unregister_driver:
	platform_driver_unregister(&sparcspkr_platform_driver);

	return error;
}

static int __init sparcspkr_init(void)
{
	struct linux_ebus *ebus;
	struct linux_ebus_device *edev;
#ifdef CONFIG_SPARC64
	struct sparc_isa_bridge *isa_br;
	struct sparc_isa_device *isa_dev;
#endif

	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			if (!strcmp(edev->prom_node->name, "beep")) {
				beep_name = "Sparc EBUS Speaker";
				beep_event = ebus_spkr_event;
				beep_iobase = edev->resource[0].start;
				return sparcspkr_drv_init();
			}
		}
	}
#ifdef CONFIG_SPARC64
	for_each_isa(isa_br) {
		for_each_isadev(isa_dev, isa_br) {
			/* A hack, the beep device's base lives in
			 * the DMA isa node.
			 */
			if (!strcmp(isa_dev->prom_node->name, "dma")) {
				beep_name = "Sparc ISA Speaker";
				beep_event = isa_spkr_event,
				beep_iobase = isa_dev->resource.start;
				return sparcspkr_drv_init();
			}
		}
	}
#endif

	return -ENODEV;
}

static void __exit sparcspkr_exit(void)
{
	platform_device_unregister(sparcspkr_platform_device);
	platform_driver_unregister(&sparcspkr_platform_driver);
}

module_init(sparcspkr_init);
module_exit(sparcspkr_exit);
