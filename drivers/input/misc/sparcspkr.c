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

#include <asm/io.h>
#include <asm/ebus.h>
#ifdef CONFIG_SPARC64
#include <asm/isa.h>
#endif

MODULE_AUTHOR("David S. Miller <davem@redhat.com>");
MODULE_DESCRIPTION("PC Speaker beeper driver");
MODULE_LICENSE("GPL");

static unsigned long beep_iobase;

static char *sparcspkr_isa_name = "Sparc ISA Speaker";
static char *sparcspkr_ebus_name = "Sparc EBUS Speaker";
static char *sparcspkr_phys = "sparc/input0";
static struct input_dev sparcspkr_dev;

DEFINE_SPINLOCK(beep_lock);

static void __init init_sparcspkr_struct(void)
{
	sparcspkr_dev.evbit[0] = BIT(EV_SND);
	sparcspkr_dev.sndbit[0] = BIT(SND_BELL) | BIT(SND_TONE);

	sparcspkr_dev.phys = sparcspkr_phys;
	sparcspkr_dev.id.bustype = BUS_ISA;
	sparcspkr_dev.id.vendor = 0x001f;
	sparcspkr_dev.id.product = 0x0001;
	sparcspkr_dev.id.version = 0x0100;
}

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
	if (count) {
		if (beep_iobase & 0x2UL)
			outb(1, beep_iobase);
		else
			outl(1, beep_iobase);
	} else {
		if (beep_iobase & 0x2UL)
			outb(0, beep_iobase);
		else
			outl(0, beep_iobase);
	}

	spin_unlock_irqrestore(&beep_lock, flags);

	return 0;
}

static int __init init_ebus_beep(struct linux_ebus_device *edev)
{
	beep_iobase = edev->resource[0].start;

	init_sparcspkr_struct();

	sparcspkr_dev.name = sparcspkr_ebus_name;
	sparcspkr_dev.event = ebus_spkr_event;

	input_register_device(&sparcspkr_dev);

        printk(KERN_INFO "input: %s\n", sparcspkr_ebus_name);
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

static int __init init_isa_beep(struct sparc_isa_device *isa_dev)
{
	beep_iobase = isa_dev->resource.start;

	init_sparcspkr_struct();

	sparcspkr_dev.name = sparcspkr_isa_name;
	sparcspkr_dev.event = isa_spkr_event;
	sparcspkr_dev.id.bustype = BUS_ISA;

	input_register_device(&sparcspkr_dev);

        printk(KERN_INFO "input: %s\n", sparcspkr_isa_name);
	return 0;
}
#endif

static int __init sparcspkr_init(void)
{
	struct linux_ebus *ebus;
	struct linux_ebus_device *edev = NULL;
#ifdef CONFIG_SPARC64
	struct sparc_isa_bridge *isa_br;
	struct sparc_isa_device *isa_dev;
#endif

	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			if (!strcmp(edev->prom_name, "beep"))
				return init_ebus_beep(edev);
		}
	}
#ifdef CONFIG_SPARC64
	for_each_isa(isa_br) {
		for_each_isadev(isa_dev, isa_br) {
			/* A hack, the beep device's base lives in
			 * the DMA isa node.
			 */
			if (!strcmp(isa_dev->prom_name, "dma"))
				return init_isa_beep(isa_dev);
		}
	}
#endif

	return -ENODEV;
}

static void __exit sparcspkr_exit(void)
{
	input_unregister_device(&sparcspkr_dev);
}

module_init(sparcspkr_init);
module_exit(sparcspkr_exit);
