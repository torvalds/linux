/*
 *  Copyright (c) 2000 Justin Cormack
 */

/*
 * Newton keyboard driver for Linux
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <j.cormack@doc.ic.ac.uk>, or by paper mail:
 * Justin Cormack, 68 Dartmouth Park Road, London NW5 1SN, UK.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/serio.h>

#define DRIVER_DESC	"Newton keyboard driver"

MODULE_AUTHOR("Justin Cormack <j.cormack@doc.ic.ac.uk>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

#define NKBD_KEY	0x7f
#define NKBD_PRESS	0x80

static unsigned char nkbd_keycode[128] = {
	KEY_A, KEY_S, KEY_D, KEY_F, KEY_H, KEY_G, KEY_Z, KEY_X,
	KEY_C, KEY_V, 0, KEY_B, KEY_Q, KEY_W, KEY_E, KEY_R,
	KEY_Y, KEY_T, KEY_1, KEY_2, KEY_3, KEY_4, KEY_6, KEY_5,
	KEY_EQUAL, KEY_9, KEY_7, KEY_MINUS, KEY_8, KEY_0, KEY_RIGHTBRACE, KEY_O,
	KEY_U, KEY_LEFTBRACE, KEY_I, KEY_P, KEY_ENTER, KEY_L, KEY_J, KEY_APOSTROPHE,
	KEY_K, KEY_SEMICOLON, KEY_BACKSLASH, KEY_COMMA, KEY_SLASH, KEY_N, KEY_M, KEY_DOT,
	KEY_TAB, KEY_SPACE, KEY_GRAVE, KEY_DELETE, 0, 0, 0, KEY_LEFTMETA,
	KEY_LEFTSHIFT, KEY_CAPSLOCK, KEY_LEFTALT, KEY_LEFTCTRL, KEY_RIGHTSHIFT, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	KEY_LEFT, KEY_RIGHT, KEY_DOWN, KEY_UP, 0
};

struct nkbd {
	unsigned char keycode[128];
	struct input_dev *dev;
	struct serio *serio;
	char phys[32];
};

static irqreturn_t nkbd_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags)
{
	struct nkbd *nkbd = serio_get_drvdata(serio);

	/* invalid scan codes are probably the init sequence, so we ignore them */
	if (nkbd->keycode[data & NKBD_KEY]) {
		input_report_key(nkbd->dev, nkbd->keycode[data & NKBD_KEY], data & NKBD_PRESS);
		input_sync(nkbd->dev);
	}

	else if (data == 0xe7) /* end of init sequence */
		printk(KERN_INFO "input: %s on %s\n", nkbd->dev->name, serio->phys);
	return IRQ_HANDLED;

}

static int nkbd_connect(struct serio *serio, struct serio_driver *drv)
{
	struct nkbd *nkbd;
	struct input_dev *input_dev;
	int err = -ENOMEM;
	int i;

	nkbd = kzalloc(sizeof(struct nkbd), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!nkbd || !input_dev)
		goto fail1;

	nkbd->serio = serio;
	nkbd->dev = input_dev;
	snprintf(nkbd->phys, sizeof(nkbd->phys), "%s/input0", serio->phys);
	memcpy(nkbd->keycode, nkbd_keycode, sizeof(nkbd->keycode));

	input_dev->name = "Newton Keyboard";
	input_dev->phys = nkbd->phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = SERIO_NEWTON;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &serio->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);
	input_dev->keycode = nkbd->keycode;
	input_dev->keycodesize = sizeof(unsigned char);
	input_dev->keycodemax = ARRAY_SIZE(nkbd_keycode);
	for (i = 0; i < 128; i++)
		set_bit(nkbd->keycode[i], input_dev->keybit);
	clear_bit(0, input_dev->keybit);

	serio_set_drvdata(serio, nkbd);

	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	err = input_register_device(nkbd->dev);
	if (err)
		goto fail3;

	return 0;

 fail3:	serio_close(serio);
 fail2:	serio_set_drvdata(serio, NULL);
 fail1:	input_free_device(input_dev);
	kfree(nkbd);
	return err;
}

static void nkbd_disconnect(struct serio *serio)
{
	struct nkbd *nkbd = serio_get_drvdata(serio);

	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_unregister_device(nkbd->dev);
	kfree(nkbd);
}

static struct serio_device_id nkbd_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_NEWTON,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, nkbd_serio_ids);

static struct serio_driver nkbd_drv = {
	.driver		= {
		.name	= "newtonkbd",
	},
	.description	= DRIVER_DESC,
	.id_table	= nkbd_serio_ids,
	.interrupt	= nkbd_interrupt,
	.connect	= nkbd_connect,
	.disconnect	= nkbd_disconnect,
};

module_serio_driver(nkbd_drv);
