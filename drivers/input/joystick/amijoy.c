// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 1998-2001 Vojtech Pavlik
 */

/*
 * Driver for Amiga joysticks for Linux/m68k
 */

/*
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>

#include <asm/amigahw.h>
#include <asm/amigaints.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Driver for Amiga joysticks");
MODULE_LICENSE("GPL");

static int amijoy[2] = { 0, 1 };
module_param_array_named(map, amijoy, uint, NULL, 0);
MODULE_PARM_DESC(map, "Map of attached joysticks in form of <a>,<b> (default is 0,1)");

static int amijoy_used;
static DEFINE_MUTEX(amijoy_mutex);
static struct input_dev *amijoy_dev[2];
static char *amijoy_phys[2] = { "amijoy/input0", "amijoy/input1" };

static irqreturn_t amijoy_interrupt(int irq, void *dummy)
{
	int i, data = 0, button = 0;

	for (i = 0; i < 2; i++)
		if (amijoy[i]) {

			switch (i) {
				case 0: data = ~amiga_custom.joy0dat; button = (~ciaa.pra >> 6) & 1; break;
				case 1: data = ~amiga_custom.joy1dat; button = (~ciaa.pra >> 7) & 1; break;
			}

			input_report_key(amijoy_dev[i], BTN_TRIGGER, button);

			input_report_abs(amijoy_dev[i], ABS_X, ((data >> 1) & 1) - ((data >> 9) & 1));
			data = ~(data ^ (data << 1));
			input_report_abs(amijoy_dev[i], ABS_Y, ((data >> 1) & 1) - ((data >> 9) & 1));

			input_sync(amijoy_dev[i]);
		}
	return IRQ_HANDLED;
}

static int amijoy_open(struct input_dev *dev)
{
	int err;

	err = mutex_lock_interruptible(&amijoy_mutex);
	if (err)
		return err;

	if (!amijoy_used && request_irq(IRQ_AMIGA_VERTB, amijoy_interrupt, 0, "amijoy", amijoy_interrupt)) {
		printk(KERN_ERR "amijoy.c: Can't allocate irq %d\n", IRQ_AMIGA_VERTB);
		err = -EBUSY;
		goto out;
	}

	amijoy_used++;
out:
	mutex_unlock(&amijoy_mutex);
	return err;
}

static void amijoy_close(struct input_dev *dev)
{
	mutex_lock(&amijoy_mutex);
	if (!--amijoy_used)
		free_irq(IRQ_AMIGA_VERTB, amijoy_interrupt);
	mutex_unlock(&amijoy_mutex);
}

static int __init amijoy_init(void)
{
	int i, j;
	int err;

	if (!MACH_IS_AMIGA)
		return -ENODEV;

	for (i = 0; i < 2; i++) {
		if (!amijoy[i])
			continue;

		amijoy_dev[i] = input_allocate_device();
		if (!amijoy_dev[i]) {
			err = -ENOMEM;
			goto fail;
		}

		if (!request_mem_region(CUSTOM_PHYSADDR + 10 + i * 2, 2, "amijoy [Denise]")) {
			input_free_device(amijoy_dev[i]);
			err = -EBUSY;
			goto fail;
		}

		amijoy_dev[i]->name = "Amiga joystick";
		amijoy_dev[i]->phys = amijoy_phys[i];
		amijoy_dev[i]->id.bustype = BUS_AMIGA;
		amijoy_dev[i]->id.vendor = 0x0001;
		amijoy_dev[i]->id.product = 0x0003;
		amijoy_dev[i]->id.version = 0x0100;

		amijoy_dev[i]->open = amijoy_open;
		amijoy_dev[i]->close = amijoy_close;

		amijoy_dev[i]->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
		amijoy_dev[i]->absbit[0] = BIT_MASK(ABS_X) | BIT_MASK(ABS_Y);
		amijoy_dev[i]->keybit[BIT_WORD(BTN_LEFT)] = BIT_MASK(BTN_LEFT) |
			BIT_MASK(BTN_MIDDLE) | BIT_MASK(BTN_RIGHT);
		for (j = 0; j < 2; j++) {
			input_set_abs_params(amijoy_dev[i], ABS_X + j,
					     -1, 1, 0, 0);
		}

		err = input_register_device(amijoy_dev[i]);
		if (err) {
			input_free_device(amijoy_dev[i]);
			goto fail;
		}
	}
	return 0;

 fail:	while (--i >= 0)
		if (amijoy[i]) {
			input_unregister_device(amijoy_dev[i]);
			release_mem_region(CUSTOM_PHYSADDR + 10 + i * 2, 2);
		}
	return err;
}

static void __exit amijoy_exit(void)
{
	int i;

	for (i = 0; i < 2; i++)
		if (amijoy[i]) {
			input_unregister_device(amijoy_dev[i]);
			release_mem_region(CUSTOM_PHYSADDR + 10 + i * 2, 2);
		}
}

module_init(amijoy_init);
module_exit(amijoy_exit);
