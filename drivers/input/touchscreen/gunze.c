/*
 * $Id: gunze.c,v 1.12 2001/09/25 10:12:07 vojtech Exp $
 *
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 */

/*
 * Gunze AHL-51S touchscreen driver for Linux
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

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/init.h>

#define DRIVER_DESC	"Gunze AHL-51S touchscreen driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Definitions & global arrays.
 */

#define	GUNZE_MAX_LENGTH	10

static char *gunze_name = "Gunze AHL-51S TouchScreen";

/*
 * Per-touchscreen data.
 */

struct gunze {
	struct input_dev dev;
	struct serio *serio;
	int idx;
	unsigned char data[GUNZE_MAX_LENGTH];
	char phys[32];
};

static void gunze_process_packet(struct gunze* gunze, struct pt_regs *regs)
{
	struct input_dev *dev = &gunze->dev;

	if (gunze->idx != GUNZE_MAX_LENGTH || gunze->data[5] != ',' ||
		(gunze->data[0] != 'T' && gunze->data[0] != 'R')) {
		printk(KERN_WARNING "gunze.c: bad packet: >%.*s<\n", GUNZE_MAX_LENGTH, gunze->data);
		return;
	}

	input_regs(dev, regs);
	input_report_abs(dev, ABS_X, simple_strtoul(gunze->data + 1, NULL, 10));
	input_report_abs(dev, ABS_Y, 1024 - simple_strtoul(gunze->data + 6, NULL, 10));
	input_report_key(dev, BTN_TOUCH, gunze->data[0] == 'T');
	input_sync(dev);
}

static irqreturn_t gunze_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags, struct pt_regs *regs)
{
	struct gunze* gunze = serio_get_drvdata(serio);

	if (data == '\r') {
		gunze_process_packet(gunze, regs);
		gunze->idx = 0;
	} else {
		if (gunze->idx < GUNZE_MAX_LENGTH)
			gunze->data[gunze->idx++] = data;
	}
	return IRQ_HANDLED;
}

/*
 * gunze_disconnect() is the opposite of gunze_connect()
 */

static void gunze_disconnect(struct serio *serio)
{
	struct gunze* gunze = serio_get_drvdata(serio);

	input_unregister_device(&gunze->dev);
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	kfree(gunze);
}

/*
 * gunze_connect() is the routine that is called when someone adds a
 * new serio device that supports Gunze protocol and registers it as
 * an input device.
 */

static int gunze_connect(struct serio *serio, struct serio_driver *drv)
{
	struct gunze *gunze;
	int err;

	if (!(gunze = kmalloc(sizeof(struct gunze), GFP_KERNEL)))
		return -ENOMEM;

	memset(gunze, 0, sizeof(struct gunze));

	init_input_dev(&gunze->dev);
	gunze->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
	gunze->dev.keybit[LONG(BTN_TOUCH)] = BIT(BTN_TOUCH);
	input_set_abs_params(&gunze->dev, ABS_X, 24, 1000, 0, 0);
	input_set_abs_params(&gunze->dev, ABS_Y, 24, 1000, 0, 0);

	gunze->serio = serio;

	sprintf(gunze->phys, "%s/input0", serio->phys);

	gunze->dev.private = gunze;
	gunze->dev.name = gunze_name;
	gunze->dev.phys = gunze->phys;
	gunze->dev.id.bustype = BUS_RS232;
	gunze->dev.id.vendor = SERIO_GUNZE;
	gunze->dev.id.product = 0x0051;
	gunze->dev.id.version = 0x0100;

	serio_set_drvdata(serio, gunze);

	err = serio_open(serio, drv);
	if (err) {
		serio_set_drvdata(serio, NULL);
		kfree(gunze);
		return err;
	}

	input_register_device(&gunze->dev);

	printk(KERN_INFO "input: %s on %s\n", gunze_name, serio->phys);

	return 0;
}

/*
 * The serio driver structure.
 */

static struct serio_device_id gunze_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_GUNZE,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, gunze_serio_ids);

static struct serio_driver gunze_drv = {
	.driver		= {
		.name	= "gunze",
	},
	.description	= DRIVER_DESC,
	.id_table	= gunze_serio_ids,
	.interrupt	= gunze_interrupt,
	.connect	= gunze_connect,
	.disconnect	= gunze_disconnect,
};

/*
 * The functions for inserting/removing us as a module.
 */

static int __init gunze_init(void)
{
	serio_register_driver(&gunze_drv);
	return 0;
}

static void __exit gunze_exit(void)
{
	serio_unregister_driver(&gunze_drv);
}

module_init(gunze_init);
module_exit(gunze_exit);
