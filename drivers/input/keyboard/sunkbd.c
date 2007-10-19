/*
 * $Id: sunkbd.c,v 1.14 2001/09/25 10:12:07 vojtech Exp $
 *
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 */

/*
 * Sun keyboard driver for Linux
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

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/workqueue.h>

#define DRIVER_DESC	"Sun keyboard driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

static unsigned char sunkbd_keycode[128] = {
	  0,128,114,129,115, 59, 60, 68, 61, 87, 62, 88, 63,100, 64,112,
	 65, 66, 67, 56,103,119, 99, 70,105,130,131,108,106,  1,  2,  3,
	  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 41, 14,110,113, 98, 55,
	116,132, 83,133,102, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
	 26, 27,111,127, 71, 72, 73, 74,134,135,107,  0, 29, 30, 31, 32,
	 33, 34, 35, 36, 37, 38, 39, 40, 43, 28, 96, 75, 76, 77, 82,136,
	104,137, 69, 42, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54,101,
	 79, 80, 81,  0,  0,  0,138, 58,125, 57,126,109, 86, 78
};

#define SUNKBD_CMD_RESET	0x1
#define SUNKBD_CMD_BELLON	0x2
#define SUNKBD_CMD_BELLOFF	0x3
#define SUNKBD_CMD_CLICK	0xa
#define SUNKBD_CMD_NOCLICK	0xb
#define SUNKBD_CMD_SETLED	0xe
#define SUNKBD_CMD_LAYOUT	0xf

#define SUNKBD_RET_RESET	0xff
#define SUNKBD_RET_ALLUP	0x7f
#define SUNKBD_RET_LAYOUT	0xfe

#define SUNKBD_LAYOUT_5_MASK	0x20
#define SUNKBD_RELEASE		0x80
#define SUNKBD_KEY		0x7f

/*
 * Per-keyboard data.
 */

struct sunkbd {
	unsigned char keycode[128];
	struct input_dev *dev;
	struct serio *serio;
	struct work_struct tq;
	wait_queue_head_t wait;
	char name[64];
	char phys[32];
	char type;
	unsigned char enabled;
	volatile s8 reset;
	volatile s8 layout;
};

/*
 * sunkbd_interrupt() is called by the low level driver when a character
 * is received.
 */

static irqreturn_t sunkbd_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags)
{
	struct sunkbd* sunkbd = serio_get_drvdata(serio);

	if (sunkbd->reset <= -1) {		/* If cp[i] is 0xff, sunkbd->reset will stay -1. */
		sunkbd->reset = data;		/* The keyboard sends 0xff 0xff 0xID on powerup */
		wake_up_interruptible(&sunkbd->wait);
		goto out;
	}

	if (sunkbd->layout == -1) {
		sunkbd->layout = data;
		wake_up_interruptible(&sunkbd->wait);
		goto out;
	}

	switch (data) {

		case SUNKBD_RET_RESET:
			schedule_work(&sunkbd->tq);
			sunkbd->reset = -1;
			break;

		case SUNKBD_RET_LAYOUT:
			sunkbd->layout = -1;
			break;

		case SUNKBD_RET_ALLUP: /* All keys released */
			break;

		default:
			if (!sunkbd->enabled)
				break;

			if (sunkbd->keycode[data & SUNKBD_KEY]) {
                                input_report_key(sunkbd->dev, sunkbd->keycode[data & SUNKBD_KEY], !(data & SUNKBD_RELEASE));
				input_sync(sunkbd->dev);
                        } else {
                                printk(KERN_WARNING "sunkbd.c: Unknown key (scancode %#x) %s.\n",
                                        data & SUNKBD_KEY, data & SUNKBD_RELEASE ? "released" : "pressed");
                        }
	}
out:
	return IRQ_HANDLED;
}

/*
 * sunkbd_event() handles events from the input module.
 */

static int sunkbd_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct sunkbd *sunkbd = input_get_drvdata(dev);

	switch (type) {

		case EV_LED:

			sunkbd->serio->write(sunkbd->serio, SUNKBD_CMD_SETLED);
			sunkbd->serio->write(sunkbd->serio,
				(!!test_bit(LED_CAPSL, dev->led) << 3) | (!!test_bit(LED_SCROLLL, dev->led) << 2) |
				(!!test_bit(LED_COMPOSE, dev->led) << 1) | !!test_bit(LED_NUML, dev->led));
			return 0;

		case EV_SND:

			switch (code) {

				case SND_CLICK:
					sunkbd->serio->write(sunkbd->serio, SUNKBD_CMD_NOCLICK - value);
					return 0;

				case SND_BELL:
					sunkbd->serio->write(sunkbd->serio, SUNKBD_CMD_BELLOFF - value);
					return 0;
			}

			break;
	}

	return -1;
}

/*
 * sunkbd_initialize() checks for a Sun keyboard attached, and determines
 * its type.
 */

static int sunkbd_initialize(struct sunkbd *sunkbd)
{
	sunkbd->reset = -2;
	sunkbd->serio->write(sunkbd->serio, SUNKBD_CMD_RESET);
	wait_event_interruptible_timeout(sunkbd->wait, sunkbd->reset >= 0, HZ);
	if (sunkbd->reset < 0)
		return -1;

	sunkbd->type = sunkbd->reset;

	if (sunkbd->type == 4) {	/* Type 4 keyboard */
		sunkbd->layout = -2;
		sunkbd->serio->write(sunkbd->serio, SUNKBD_CMD_LAYOUT);
		wait_event_interruptible_timeout(sunkbd->wait, sunkbd->layout >= 0, HZ/4);
		if (sunkbd->layout < 0) return -1;
		if (sunkbd->layout & SUNKBD_LAYOUT_5_MASK) sunkbd->type = 5;
	}

	return 0;
}

/*
 * sunkbd_reinit() sets leds and beeps to a state the computer remembers they
 * were in.
 */

static void sunkbd_reinit(struct work_struct *work)
{
	struct sunkbd *sunkbd = container_of(work, struct sunkbd, tq);

	wait_event_interruptible_timeout(sunkbd->wait, sunkbd->reset >= 0, HZ);

	sunkbd->serio->write(sunkbd->serio, SUNKBD_CMD_SETLED);
	sunkbd->serio->write(sunkbd->serio,
		(!!test_bit(LED_CAPSL, sunkbd->dev->led) << 3) | (!!test_bit(LED_SCROLLL, sunkbd->dev->led) << 2) |
		(!!test_bit(LED_COMPOSE, sunkbd->dev->led) << 1) | !!test_bit(LED_NUML, sunkbd->dev->led));
	sunkbd->serio->write(sunkbd->serio, SUNKBD_CMD_NOCLICK - !!test_bit(SND_CLICK, sunkbd->dev->snd));
	sunkbd->serio->write(sunkbd->serio, SUNKBD_CMD_BELLOFF - !!test_bit(SND_BELL, sunkbd->dev->snd));
}

static void sunkbd_enable(struct sunkbd *sunkbd, int enable)
{
	serio_pause_rx(sunkbd->serio);
	sunkbd->enabled = enable;
	serio_continue_rx(sunkbd->serio);
}

/*
 * sunkbd_connect() probes for a Sun keyboard and fills the necessary structures.
 */

static int sunkbd_connect(struct serio *serio, struct serio_driver *drv)
{
	struct sunkbd *sunkbd;
	struct input_dev *input_dev;
	int err = -ENOMEM;
	int i;

	sunkbd = kzalloc(sizeof(struct sunkbd), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!sunkbd || !input_dev)
		goto fail1;

	sunkbd->serio = serio;
	sunkbd->dev = input_dev;
	init_waitqueue_head(&sunkbd->wait);
	INIT_WORK(&sunkbd->tq, sunkbd_reinit);
	snprintf(sunkbd->phys, sizeof(sunkbd->phys), "%s/input0", serio->phys);

	serio_set_drvdata(serio, sunkbd);

	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	if (sunkbd_initialize(sunkbd) < 0) {
		err = -ENODEV;
		goto fail3;
	}

	snprintf(sunkbd->name, sizeof(sunkbd->name), "Sun Type %d keyboard", sunkbd->type);
	memcpy(sunkbd->keycode, sunkbd_keycode, sizeof(sunkbd->keycode));

	input_dev->name = sunkbd->name;
	input_dev->phys = sunkbd->phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor  = SERIO_SUNKBD;
	input_dev->id.product = sunkbd->type;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &serio->dev;

	input_set_drvdata(input_dev, sunkbd);

	input_dev->event = sunkbd_event;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_LED) |
		BIT_MASK(EV_SND) | BIT_MASK(EV_REP);
	input_dev->ledbit[0] = BIT_MASK(LED_CAPSL) | BIT_MASK(LED_COMPOSE) |
		BIT_MASK(LED_SCROLLL) | BIT_MASK(LED_NUML);
	input_dev->sndbit[0] = BIT_MASK(SND_CLICK) | BIT_MASK(SND_BELL);

	input_dev->keycode = sunkbd->keycode;
	input_dev->keycodesize = sizeof(unsigned char);
	input_dev->keycodemax = ARRAY_SIZE(sunkbd_keycode);
	for (i = 0; i < 128; i++)
		set_bit(sunkbd->keycode[i], input_dev->keybit);
	clear_bit(0, input_dev->keybit);

	sunkbd_enable(sunkbd, 1);

	err = input_register_device(sunkbd->dev);
	if (err)
		goto fail4;

	return 0;

 fail4:	sunkbd_enable(sunkbd, 0);
 fail3:	serio_close(serio);
 fail2:	serio_set_drvdata(serio, NULL);
 fail1:	input_free_device(input_dev);
	kfree(sunkbd);
	return err;
}

/*
 * sunkbd_disconnect() unregisters and closes behind us.
 */

static void sunkbd_disconnect(struct serio *serio)
{
	struct sunkbd *sunkbd = serio_get_drvdata(serio);

	sunkbd_enable(sunkbd, 0);
	input_unregister_device(sunkbd->dev);
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	kfree(sunkbd);
}

static struct serio_device_id sunkbd_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_SUNKBD,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_UNKNOWN, /* sunkbd does probe */
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, sunkbd_serio_ids);

static struct serio_driver sunkbd_drv = {
	.driver		= {
		.name	= "sunkbd",
	},
	.description	= DRIVER_DESC,
	.id_table	= sunkbd_serio_ids,
	.interrupt	= sunkbd_interrupt,
	.connect	= sunkbd_connect,
	.disconnect	= sunkbd_disconnect,
};

/*
 * The functions for insering/removing us as a module.
 */

static int __init sunkbd_init(void)
{
	return serio_register_driver(&sunkbd_drv);
}

static void __exit sunkbd_exit(void)
{
	serio_unregister_driver(&sunkbd_drv);
}

module_init(sunkbd_init);
module_exit(sunkbd_exit);
