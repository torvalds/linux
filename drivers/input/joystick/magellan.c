/*
 * $Id: magellan.c,v 1.16 2002/01/22 20:28:39 vojtech Exp $
 *
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 */

/*
 * Magellan and Space Mouse 6dof controller driver for Linux
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
 *  Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/init.h>

#define DRIVER_DESC	"Magellan and SpaceMouse 6dof controller driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Definitions & global arrays.
 */

#define	MAGELLAN_MAX_LENGTH	32

static int magellan_buttons[] = { BTN_0, BTN_1, BTN_2, BTN_3, BTN_4, BTN_5, BTN_6, BTN_7, BTN_8 };
static int magellan_axes[] = { ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ };
static char *magellan_name = "LogiCad3D Magellan / SpaceMouse";

/*
 * Per-Magellan data.
 */

struct magellan {
	struct input_dev dev;
	int idx;
	unsigned char data[MAGELLAN_MAX_LENGTH];
	char phys[32];
};

/*
 * magellan_crunch_nibbles() verifies that the bytes sent from the Magellan
 * have correct upper nibbles for the lower ones, if not, the packet will
 * be thrown away. It also strips these upper halves to simplify further
 * processing.
 */

static int magellan_crunch_nibbles(unsigned char *data, int count)
{
	static unsigned char nibbles[16] = "0AB3D56GH9:K<MN?";

	do {
		if (data[count] == nibbles[data[count] & 0xf])
			data[count] = data[count] & 0xf;
		else
			return -1;
	} while (--count);

	return 0;
}

static void magellan_process_packet(struct magellan* magellan, struct pt_regs *regs)
{
	struct input_dev *dev = &magellan->dev;
	unsigned char *data = magellan->data;
	int i, t;

	if (!magellan->idx) return;

	input_regs(dev, regs);

	switch (magellan->data[0]) {

		case 'd':				/* Axis data */
			if (magellan->idx != 25) return;
			if (magellan_crunch_nibbles(data, 24)) return;
			for (i = 0; i < 6; i++)
				input_report_abs(dev, magellan_axes[i],
					(data[(i << 2) + 1] << 12 | data[(i << 2) + 2] << 8 |
					 data[(i << 2) + 3] <<  4 | data[(i << 2) + 4]) - 32768);
			break;

		case 'k':				/* Button data */
			if (magellan->idx != 4) return;
			if (magellan_crunch_nibbles(data, 3)) return;
			t = (data[1] << 1) | (data[2] << 5) | data[3];
			for (i = 0; i < 9; i++) input_report_key(dev, magellan_buttons[i], (t >> i) & 1);
			break;
	}

	input_sync(dev);
}

static irqreturn_t magellan_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags, struct pt_regs *regs)
{
	struct magellan* magellan = serio_get_drvdata(serio);

	if (data == '\r') {
		magellan_process_packet(magellan, regs);
		magellan->idx = 0;
	} else {
		if (magellan->idx < MAGELLAN_MAX_LENGTH)
			magellan->data[magellan->idx++] = data;
	}
	return IRQ_HANDLED;
}

/*
 * magellan_disconnect() is the opposite of magellan_connect()
 */

static void magellan_disconnect(struct serio *serio)
{
	struct magellan* magellan = serio_get_drvdata(serio);

	input_unregister_device(&magellan->dev);
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	kfree(magellan);
}

/*
 * magellan_connect() is the routine that is called when someone adds a
 * new serio device that supports Magellan protocol and registers it as
 * an input device.
 */

static int magellan_connect(struct serio *serio, struct serio_driver *drv)
{
	struct magellan *magellan;
	int i, t;
	int err;

	if (!(magellan = kmalloc(sizeof(struct magellan), GFP_KERNEL)))
		return -ENOMEM;

	memset(magellan, 0, sizeof(struct magellan));

	magellan->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);

	for (i = 0; i < 9; i++)
		set_bit(magellan_buttons[i], magellan->dev.keybit);

	for (i = 0; i < 6; i++) {
		t = magellan_axes[i];
		set_bit(t, magellan->dev.absbit);
		magellan->dev.absmin[t] = -360;
		magellan->dev.absmax[t] =  360;
	}

	sprintf(magellan->phys, "%s/input0", serio->phys);

	init_input_dev(&magellan->dev);
	magellan->dev.private = magellan;
	magellan->dev.name = magellan_name;
	magellan->dev.phys = magellan->phys;
	magellan->dev.id.bustype = BUS_RS232;
	magellan->dev.id.vendor = SERIO_MAGELLAN;
	magellan->dev.id.product = 0x0001;
	magellan->dev.id.version = 0x0100;
	magellan->dev.dev = &serio->dev;

	serio_set_drvdata(serio, magellan);

	err = serio_open(serio, drv);
	if (err) {
		serio_set_drvdata(serio, NULL);
		kfree(magellan);
		return err;
	}

	input_register_device(&magellan->dev);

	printk(KERN_INFO "input: %s on %s\n", magellan_name, serio->phys);

	return 0;
}

/*
 * The serio driver structure.
 */

static struct serio_device_id magellan_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_MAGELLAN,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, magellan_serio_ids);

static struct serio_driver magellan_drv = {
	.driver		= {
		.name	= "magellan",
	},
	.description	= DRIVER_DESC,
	.id_table	= magellan_serio_ids,
	.interrupt	= magellan_interrupt,
	.connect	= magellan_connect,
	.disconnect	= magellan_disconnect,
};

/*
 * The functions for inserting/removing us as a module.
 */

static int __init magellan_init(void)
{
	serio_register_driver(&magellan_drv);
	return 0;
}

static void __exit magellan_exit(void)
{
	serio_unregister_driver(&magellan_drv);
}

module_init(magellan_init);
module_exit(magellan_exit);
