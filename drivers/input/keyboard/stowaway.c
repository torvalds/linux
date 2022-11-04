// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Stowaway keyboard driver for Linux
 */

/*
 *  Copyright (c) 2006 Marek Vasut
 *
 *  Based on Newton keyboard driver for Linux
 *  by Justin Cormack
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/serio.h>

#define DRIVER_DESC	"Stowaway keyboard driver"

MODULE_AUTHOR("Marek Vasut <marek.vasut@gmail.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

#define SKBD_KEY_MASK	0x7f
#define SKBD_RELEASE	0x80

static unsigned char skbd_keycode[128] = {
	KEY_1, KEY_2, KEY_3, KEY_Z, KEY_4, KEY_5, KEY_6, KEY_7,
	0, KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_GRAVE,
	KEY_X, KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_SPACE,
	KEY_CAPSLOCK, KEY_TAB, KEY_LEFTCTRL, 0, 0, 0, 0, 0,
	0, 0, 0, KEY_LEFTALT, 0, 0, 0, 0,
	0, 0, 0, 0, KEY_C, KEY_V, KEY_B, KEY_N,
	KEY_MINUS, KEY_EQUAL, KEY_BACKSPACE, KEY_HOME, KEY_8, KEY_9, KEY_0, KEY_ESC,
	KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_BACKSLASH, KEY_END, KEY_U, KEY_I, KEY_O, KEY_P,
	KEY_APOSTROPHE, KEY_ENTER, KEY_PAGEUP,0, KEY_J, KEY_K, KEY_L, KEY_SEMICOLON,
	KEY_SLASH, KEY_UP, KEY_PAGEDOWN, 0,KEY_M, KEY_COMMA, KEY_DOT, KEY_INSERT,
	KEY_DELETE, KEY_LEFT, KEY_DOWN, KEY_RIGHT,  0, 0, 0,
	KEY_LEFTSHIFT, KEY_RIGHTSHIFT, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7,
	KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12, 0, 0, 0
};

struct skbd {
	unsigned char keycode[128];
	struct input_dev *dev;
	struct serio *serio;
	char phys[32];
};

static irqreturn_t skbd_interrupt(struct serio *serio, unsigned char data,
				  unsigned int flags)
{
	struct skbd *skbd = serio_get_drvdata(serio);
	struct input_dev *dev = skbd->dev;

	if (skbd->keycode[data & SKBD_KEY_MASK]) {
		input_report_key(dev, skbd->keycode[data & SKBD_KEY_MASK],
				 !(data & SKBD_RELEASE));
		input_sync(dev);
	}

	return IRQ_HANDLED;
}

static int skbd_connect(struct serio *serio, struct serio_driver *drv)
{
	struct skbd *skbd;
	struct input_dev *input_dev;
	int err = -ENOMEM;
	int i;

	skbd = kzalloc(sizeof(struct skbd), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!skbd || !input_dev)
		goto fail1;

	skbd->serio = serio;
	skbd->dev = input_dev;
	snprintf(skbd->phys, sizeof(skbd->phys), "%s/input0", serio->phys);
	memcpy(skbd->keycode, skbd_keycode, sizeof(skbd->keycode));

	input_dev->name = "Stowaway Keyboard";
	input_dev->phys = skbd->phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = SERIO_STOWAWAY;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &serio->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);
	input_dev->keycode = skbd->keycode;
	input_dev->keycodesize = sizeof(unsigned char);
	input_dev->keycodemax = ARRAY_SIZE(skbd_keycode);
	for (i = 0; i < ARRAY_SIZE(skbd_keycode); i++)
		set_bit(skbd_keycode[i], input_dev->keybit);
	clear_bit(0, input_dev->keybit);

	serio_set_drvdata(serio, skbd);

	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	err = input_register_device(skbd->dev);
	if (err)
		goto fail3;

	return 0;

 fail3: serio_close(serio);
 fail2:	serio_set_drvdata(serio, NULL);
 fail1:	input_free_device(input_dev);
	kfree(skbd);
	return err;
}

static void skbd_disconnect(struct serio *serio)
{
	struct skbd *skbd = serio_get_drvdata(serio);

	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_unregister_device(skbd->dev);
	kfree(skbd);
}

static const struct serio_device_id skbd_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_STOWAWAY,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, skbd_serio_ids);

static struct serio_driver skbd_drv = {
	.driver		= {
		.name	= "stowaway",
	},
	.description	= DRIVER_DESC,
	.id_table	= skbd_serio_ids,
	.interrupt	= skbd_interrupt,
	.connect	= skbd_connect,
	.disconnect	= skbd_disconnect,
};

module_serio_driver(skbd_drv);
