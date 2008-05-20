/*
 *  Copyright (c) 2001 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Toby Deshane
 */

/*
 * InterAct digital gamepad/joystick driver for Linux
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

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/gameport.h>
#include <linux/input.h>
#include <linux/jiffies.h>

#define DRIVER_DESC	"InterAct digital joystick driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

#define INTERACT_MAX_START	600	/* 400 us */
#define INTERACT_MAX_STROBE	60	/* 40 us */
#define INTERACT_MAX_LENGTH	32	/* 32 bits */

#define INTERACT_TYPE_HHFX	0	/* HammerHead/FX */
#define INTERACT_TYPE_PP8D	1	/* ProPad 8 */

struct interact {
	struct gameport *gameport;
	struct input_dev *dev;
	int bads;
	int reads;
	unsigned char type;
	unsigned char length;
	char phys[32];
};

static short interact_abs_hhfx[] =
	{ ABS_RX, ABS_RY, ABS_X, ABS_Y, ABS_HAT0X, ABS_HAT0Y, -1 };
static short interact_abs_pp8d[] =
	{ ABS_X, ABS_Y, -1 };

static short interact_btn_hhfx[] =
	{ BTN_TR, BTN_X, BTN_Y, BTN_Z, BTN_A, BTN_B, BTN_C, BTN_TL, BTN_TL2, BTN_TR2, BTN_MODE, BTN_SELECT, -1 };
static short interact_btn_pp8d[] =
	{ BTN_C, BTN_TL, BTN_TR, BTN_A, BTN_B, BTN_Y, BTN_Z, BTN_X, -1 };

struct interact_type {
	int id;
	short *abs;
	short *btn;
	char *name;
	unsigned char length;
	unsigned char b8;
};

static struct interact_type interact_type[] = {
	{ 0x6202, interact_abs_hhfx, interact_btn_hhfx, "InterAct HammerHead/FX",    32, 4 },
	{ 0x53f8, interact_abs_pp8d, interact_btn_pp8d, "InterAct ProPad 8 Digital", 16, 0 },
	{ 0 }};

/*
 * interact_read_packet() reads and InterAct joystick data.
 */

static int interact_read_packet(struct gameport *gameport, int length, u32 *data)
{
	unsigned long flags;
	unsigned char u, v;
	unsigned int t, s;
	int i;

	i = 0;
	data[0] = data[1] = data[2] = 0;
	t = gameport_time(gameport, INTERACT_MAX_START);
	s = gameport_time(gameport, INTERACT_MAX_STROBE);

	local_irq_save(flags);
	gameport_trigger(gameport);
	v = gameport_read(gameport);

	while (t > 0 && i < length) {
		t--;
		u = v; v = gameport_read(gameport);
		if (v & ~u & 0x40) {
			data[0] = (data[0] << 1) | ((v >> 4) & 1);
			data[1] = (data[1] << 1) | ((v >> 5) & 1);
			data[2] = (data[2] << 1) | ((v >> 7) & 1);
			i++;
			t = s;
		}
	}

	local_irq_restore(flags);

	return i;
}

/*
 * interact_poll() reads and analyzes InterAct joystick data.
 */

static void interact_poll(struct gameport *gameport)
{
	struct interact *interact = gameport_get_drvdata(gameport);
	struct input_dev *dev = interact->dev;
	u32 data[3];
	int i;

	interact->reads++;

	if (interact_read_packet(interact->gameport, interact->length, data) < interact->length) {
		interact->bads++;
	} else {

		for (i = 0; i < 3; i++)
			data[i] <<= INTERACT_MAX_LENGTH - interact->length;

		switch (interact->type) {

			case INTERACT_TYPE_HHFX:

				for (i = 0; i < 4; i++)
					input_report_abs(dev, interact_abs_hhfx[i], (data[i & 1] >> ((i >> 1) << 3)) & 0xff);

				for (i = 0; i < 2; i++)
					input_report_abs(dev, ABS_HAT0Y - i,
						((data[1] >> ((i << 1) + 17)) & 1)  - ((data[1] >> ((i << 1) + 16)) & 1));

				for (i = 0; i < 8; i++)
					input_report_key(dev, interact_btn_hhfx[i], (data[0] >> (i + 16)) & 1);

				for (i = 0; i < 4; i++)
					input_report_key(dev, interact_btn_hhfx[i + 8], (data[1] >> (i + 20)) & 1);

				break;

			case INTERACT_TYPE_PP8D:

				for (i = 0; i < 2; i++)
					input_report_abs(dev, interact_abs_pp8d[i],
						((data[0] >> ((i << 1) + 20)) & 1)  - ((data[0] >> ((i << 1) + 21)) & 1));

				for (i = 0; i < 8; i++)
					input_report_key(dev, interact_btn_pp8d[i], (data[1] >> (i + 16)) & 1);

				break;
		}
	}

	input_sync(dev);
}

/*
 * interact_open() is a callback from the input open routine.
 */

static int interact_open(struct input_dev *dev)
{
	struct interact *interact = input_get_drvdata(dev);

	gameport_start_polling(interact->gameport);
	return 0;
}

/*
 * interact_close() is a callback from the input close routine.
 */

static void interact_close(struct input_dev *dev)
{
	struct interact *interact = input_get_drvdata(dev);

	gameport_stop_polling(interact->gameport);
}

/*
 * interact_connect() probes for InterAct joysticks.
 */

static int interact_connect(struct gameport *gameport, struct gameport_driver *drv)
{
	struct interact *interact;
	struct input_dev *input_dev;
	__u32 data[3];
	int i, t;
	int err;

	interact = kzalloc(sizeof(struct interact), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!interact || !input_dev) {
		err = -ENOMEM;
		goto fail1;
	}

	interact->gameport = gameport;
	interact->dev = input_dev;

	gameport_set_drvdata(gameport, interact);

	err = gameport_open(gameport, drv, GAMEPORT_MODE_RAW);
	if (err)
		goto fail1;

	i = interact_read_packet(gameport, INTERACT_MAX_LENGTH * 2, data);

	if (i != 32 || (data[0] >> 24) != 0x0c || (data[1] >> 24) != 0x02) {
		err = -ENODEV;
		goto fail2;
	}

	for (i = 0; interact_type[i].length; i++)
		if (interact_type[i].id == (data[2] >> 16))
			break;

	if (!interact_type[i].length) {
		printk(KERN_WARNING "interact.c: Unknown joystick on %s. [len %d d0 %08x d1 %08x i2 %08x]\n",
			gameport->phys, i, data[0], data[1], data[2]);
		err = -ENODEV;
		goto fail2;
	}

	gameport_set_poll_handler(gameport, interact_poll);
	gameport_set_poll_interval(gameport, 20);

	snprintf(interact->phys, sizeof(interact->phys), "%s/input0", gameport->phys);

	interact->type = i;
	interact->length = interact_type[i].length;

	input_dev->name = interact_type[i].name;
	input_dev->phys = interact->phys;
	input_dev->id.bustype = BUS_GAMEPORT;
	input_dev->id.vendor = GAMEPORT_ID_VENDOR_INTERACT;
	input_dev->id.product = interact_type[i].id;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &gameport->dev;

	input_set_drvdata(input_dev, interact);

	input_dev->open = interact_open;
	input_dev->close = interact_close;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

	for (i = 0; (t = interact_type[interact->type].abs[i]) >= 0; i++) {
		set_bit(t, input_dev->absbit);
		if (i < interact_type[interact->type].b8) {
			input_dev->absmin[t] = 0;
			input_dev->absmax[t] = 255;
		} else {
			input_dev->absmin[t] = -1;
			input_dev->absmax[t] = 1;
		}
	}

	for (i = 0; (t = interact_type[interact->type].btn[i]) >= 0; i++)
		set_bit(t, input_dev->keybit);

	err = input_register_device(interact->dev);
	if (err)
		goto fail2;

	return 0;

fail2:	gameport_close(gameport);
fail1:  gameport_set_drvdata(gameport, NULL);
	input_free_device(input_dev);
	kfree(interact);
	return err;
}

static void interact_disconnect(struct gameport *gameport)
{
	struct interact *interact = gameport_get_drvdata(gameport);

	input_unregister_device(interact->dev);
	gameport_close(gameport);
	gameport_set_drvdata(gameport, NULL);
	kfree(interact);
}

static struct gameport_driver interact_drv = {
	.driver		= {
		.name	= "interact",
	},
	.description	= DRIVER_DESC,
	.connect	= interact_connect,
	.disconnect	= interact_disconnect,
};

static int __init interact_init(void)
{
	gameport_register_driver(&interact_drv);
	return 0;
}

static void __exit interact_exit(void)
{
	gameport_unregister_driver(&interact_drv);
}

module_init(interact_init);
module_exit(interact_exit);
