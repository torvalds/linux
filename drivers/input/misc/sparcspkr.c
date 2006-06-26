/*
 *  Driver for PC-speaker like devices found on various Sparc systems.
 *
 *  Copyright (c) 2002 Vojtech Pavlik
 *  Copyright (c) 2002, 2006 David S. Miller (davem@davemloft.net)
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/ebus.h>
#include <asm/isa.h>

MODULE_AUTHOR("David S. Miller <davem@davemloft.net>");
MODULE_DESCRIPTION("Sparc Speaker beeper driver");
MODULE_LICENSE("GPL");

struct sparcspkr_state {
	const char		*name;
	unsigned long		iobase;
	int (*event)(struct input_dev *dev, unsigned int type, unsigned int code, int value);
	spinlock_t		lock;
	struct input_dev	*input_dev;
};

static int ebus_spkr_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct sparcspkr_state *state = dev_get_drvdata(dev->cdev.dev);
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

	spin_lock_irqsave(&state->lock, flags);

	/* EBUS speaker only has on/off state, the frequency does not
	 * appear to be programmable.
	 */
	if (state->iobase & 0x2UL)
		outb(!!count, state->iobase);
	else
		outl(!!count, state->iobase);

	spin_unlock_irqrestore(&state->lock, flags);

	return 0;
}

static int isa_spkr_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct sparcspkr_state *state = dev_get_drvdata(dev->cdev.dev);
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

	spin_lock_irqsave(&state->lock, flags);

	if (count) {
		/* enable counter 2 */
		outb(inb(state->iobase + 0x61) | 3, state->iobase + 0x61);
		/* set command for counter 2, 2 byte write */
		outb(0xB6, state->iobase + 0x43);
		/* select desired HZ */
		outb(count & 0xff, state->iobase + 0x42);
		outb((count >> 8) & 0xff, state->iobase + 0x42);
	} else {
		/* disable counter 2 */
		outb(inb_p(state->iobase + 0x61) & 0xFC, state->iobase + 0x61);
	}

	spin_unlock_irqrestore(&state->lock, flags);

	return 0;
}

static int __devinit sparcspkr_probe(struct device *dev)
{
	struct sparcspkr_state *state = dev_get_drvdata(dev);
	struct input_dev *input_dev;
	int error;

	input_dev = input_allocate_device();
	if (!input_dev)
		return -ENOMEM;

	input_dev->name = state->name;
	input_dev->phys = "sparc/input0";
	input_dev->id.bustype = BUS_ISA;
	input_dev->id.vendor = 0x001f;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;
	input_dev->cdev.dev = dev;

	input_dev->evbit[0] = BIT(EV_SND);
	input_dev->sndbit[0] = BIT(SND_BELL) | BIT(SND_TONE);

	input_dev->event = state->event;

	error = input_register_device(input_dev);
	if (error) {
		input_free_device(input_dev);
		return error;
	}

	state->input_dev = input_dev;

	return 0;
}

static int __devexit sparcspkr_remove(struct of_device *dev)
{
	struct sparcspkr_state *state = dev_get_drvdata(&dev->dev);
	struct input_dev *input_dev = state->input_dev;

	/* turn off the speaker */
	state->event(input_dev, EV_SND, SND_BELL, 0);

	input_unregister_device(input_dev);

	dev_set_drvdata(&dev->dev, NULL);
	kfree(state);

	return 0;
}

static int sparcspkr_shutdown(struct of_device *dev)
{
	struct sparcspkr_state *state = dev_get_drvdata(&dev->dev);
	struct input_dev *input_dev = state->input_dev;

	/* turn off the speaker */
	state->event(input_dev, EV_SND, SND_BELL, 0);

	return 0;
}

static int __devinit ebus_beep_probe(struct of_device *dev, const struct of_device_id *match)
{
	struct linux_ebus_device *edev = to_ebus_device(&dev->dev);
	struct sparcspkr_state *state;
	int err;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->name = "Sparc EBUS Speaker";
	state->iobase = edev->resource[0].start;
	state->event = ebus_spkr_event;
	spin_lock_init(&state->lock);

	dev_set_drvdata(&dev->dev, state);

	err = sparcspkr_probe(&dev->dev);
	if (err) {
		dev_set_drvdata(&dev->dev, NULL);
		kfree(state);
	}

	return 0;
}

static struct of_device_id ebus_beep_match[] = {
	{
		.name = "beep",
	},
	{},
};

static struct of_platform_driver ebus_beep_driver = {
	.name		= "beep",
	.match_table	= ebus_beep_match,
	.probe		= ebus_beep_probe,
	.remove		= sparcspkr_remove,
	.shutdown	= sparcspkr_shutdown,
};

static int __devinit isa_beep_probe(struct of_device *dev, const struct of_device_id *match)
{
	struct sparc_isa_device *idev = to_isa_device(&dev->dev);
	struct sparcspkr_state *state;
	int err;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->name = "Sparc ISA Speaker";
	state->iobase = idev->resource.start;
	state->event = isa_spkr_event;
	spin_lock_init(&state->lock);

	dev_set_drvdata(&dev->dev, state);

	err = sparcspkr_probe(&dev->dev);
	if (err) {
		dev_set_drvdata(&dev->dev, NULL);
		kfree(state);
	}

	return 0;
}

static struct of_device_id isa_beep_match[] = {
	{
		.name = "dma",
	},
	{},
};

static struct of_platform_driver isa_beep_driver = {
	.name		= "beep",
	.match_table	= isa_beep_match,
	.probe		= isa_beep_probe,
	.remove		= sparcspkr_remove,
	.shutdown	= sparcspkr_shutdown,
};

static int __init sparcspkr_init(void)
{
	int err = of_register_driver(&ebus_beep_driver, &ebus_bus_type);

	if (!err) {
		err = of_register_driver(&isa_beep_driver, &isa_bus_type);
		if (err)
			of_unregister_driver(&ebus_beep_driver);
	}

	return err;
}

static void __exit sparcspkr_exit(void)
{
	of_unregister_driver(&ebus_beep_driver);
	of_unregister_driver(&isa_beep_driver);
}

module_init(sparcspkr_init);
module_exit(sparcspkr_exit);
