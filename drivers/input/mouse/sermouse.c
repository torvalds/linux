/*
 * $Id: sermouse.c,v 1.17 2002/03/13 10:03:43 vojtech Exp $
 *
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 */

/*
 *  Serial mouse driver for Linux
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
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/config.h>
#include <linux/serio.h>
#include <linux/init.h>

#define DRIVER_DESC	"Serial mouse driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

static char *sermouse_protocols[] = { "None", "Mouse Systems Mouse", "Sun Mouse", "Microsoft Mouse",
					"Logitech M+ Mouse", "Microsoft MZ Mouse", "Logitech MZ+ Mouse",
					"Logitech MZ++ Mouse"};

struct sermouse {
	struct input_dev dev;
	signed char buf[8];
	unsigned char count;
	unsigned char type;
	unsigned long last;
	char phys[32];
};

/*
 * sermouse_process_msc() analyzes the incoming MSC/Sun bytestream and
 * applies some prediction to the data, resulting in 96 updates per
 * second, which is as good as a PS/2 or USB mouse.
 */

static void sermouse_process_msc(struct sermouse *sermouse, signed char data, struct pt_regs *regs)
{
	struct input_dev *dev = &sermouse->dev;
	signed char *buf = sermouse->buf;

	input_regs(dev, regs);

	switch (sermouse->count) {

		case 0:
			if ((data & 0xf8) != 0x80) return;
			input_report_key(dev, BTN_LEFT,   !(data & 4));
			input_report_key(dev, BTN_RIGHT,  !(data & 1));
			input_report_key(dev, BTN_MIDDLE, !(data & 2));
			break;

		case 1:
		case 3:
			input_report_rel(dev, REL_X, data / 2);
			input_report_rel(dev, REL_Y, -buf[1]);
			buf[0] = data - data / 2;
			break;

		case 2:
		case 4:
			input_report_rel(dev, REL_X, buf[0]);
			input_report_rel(dev, REL_Y, buf[1] - data);
			buf[1] = data / 2;
			break;
	}

	input_sync(dev);

	if (++sermouse->count == (5 - ((sermouse->type == SERIO_SUN) << 1)))
		sermouse->count = 0;
}

/*
 * sermouse_process_ms() anlyzes the incoming MS(Z/+/++) bytestream and
 * generates events. With prediction it gets 80 updates/sec, assuming
 * standard 3-byte packets and 1200 bps.
 */

static void sermouse_process_ms(struct sermouse *sermouse, signed char data, struct pt_regs *regs)
{
	struct input_dev *dev = &sermouse->dev;
	signed char *buf = sermouse->buf;

	if (data & 0x40) sermouse->count = 0;

	input_regs(dev, regs);

	switch (sermouse->count) {

		case 0:
			buf[1] = data;
			input_report_key(dev, BTN_LEFT,   (data >> 5) & 1);
			input_report_key(dev, BTN_RIGHT,  (data >> 4) & 1);
			break;

		case 1:
			buf[2] = data;
			data = (signed char) (((buf[1] << 6) & 0xc0) | (data & 0x3f));
			input_report_rel(dev, REL_X, data / 2);
			input_report_rel(dev, REL_Y, buf[4]);
			buf[3] = data - data / 2;
			break;

		case 2:
			/* Guessing the state of the middle button on 3-button MS-protocol mice - ugly. */
			if ((sermouse->type == SERIO_MS) && !data && !buf[2] && !((buf[0] & 0xf0) ^ buf[1]))
				input_report_key(dev, BTN_MIDDLE, !test_bit(BTN_MIDDLE, dev->key));
			buf[0] = buf[1];

			data = (signed char) (((buf[1] << 4) & 0xc0) | (data & 0x3f));
			input_report_rel(dev, REL_X, buf[3]);
			input_report_rel(dev, REL_Y, data - buf[4]);
			buf[4] = data / 2;
			break;

		case 3:

			switch (sermouse->type) {

				case SERIO_MS:
					 sermouse->type = SERIO_MP;

				case SERIO_MP:
					if ((data >> 2) & 3) break;	/* M++ Wireless Extension packet. */
					input_report_key(dev, BTN_MIDDLE, (data >> 5) & 1);
					input_report_key(dev, BTN_SIDE,   (data >> 4) & 1);
					break;

				case SERIO_MZP:
				case SERIO_MZPP:
					input_report_key(dev, BTN_SIDE,   (data >> 5) & 1);

				case SERIO_MZ:
					input_report_key(dev, BTN_MIDDLE, (data >> 4) & 1);
					input_report_rel(dev, REL_WHEEL,  (data & 8) - (data & 7));
					break;
			}

			break;

		case 4:
		case 6:	/* MZ++ packet type. We can get these bytes for M++ too but we ignore them later. */
			buf[1] = (data >> 2) & 0x0f;
			break;

		case 5:
		case 7: /* Ignore anything besides MZ++ */
			if (sermouse->type != SERIO_MZPP) break;

			switch (buf[1]) {

				case 1: /* Extra mouse info */

					input_report_key(dev, BTN_SIDE, (data >> 4) & 1);
					input_report_key(dev, BTN_EXTRA, (data >> 5) & 1);
					input_report_rel(dev, data & 0x80 ? REL_HWHEEL : REL_WHEEL, (data & 7) - (data & 8));

					break;

				default: /* We don't decode anything else yet. */

					printk(KERN_WARNING
						"sermouse.c: Received MZ++ packet %x, don't know how to handle.\n", buf[1]);
					break;
			}

			break;
	}

	input_sync(dev);

	sermouse->count++;
}

/*
 * sermouse_interrupt() handles incoming characters, either gathering them into
 * packets or passing them to the command routine as command output.
 */

static irqreturn_t sermouse_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags, struct pt_regs *regs)
{
	struct sermouse *sermouse = serio_get_drvdata(serio);

	if (time_after(jiffies, sermouse->last + HZ/10)) sermouse->count = 0;
	sermouse->last = jiffies;

	if (sermouse->type > SERIO_SUN)
		sermouse_process_ms(sermouse, data, regs);
	else
		sermouse_process_msc(sermouse, data, regs);
	return IRQ_HANDLED;
}

/*
 * sermouse_disconnect() cleans up after we don't want talk
 * to the mouse anymore.
 */

static void sermouse_disconnect(struct serio *serio)
{
	struct sermouse *sermouse = serio_get_drvdata(serio);

	input_unregister_device(&sermouse->dev);
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	kfree(sermouse);
}

/*
 * sermouse_connect() is a callback form the serio module when
 * an unhandled serio port is found.
 */

static int sermouse_connect(struct serio *serio, struct serio_driver *drv)
{
	struct sermouse *sermouse;
	unsigned char c;
	int err;

	if (!serio->id.proto || serio->id.proto > SERIO_MZPP)
		return -ENODEV;

	if (!(sermouse = kmalloc(sizeof(struct sermouse), GFP_KERNEL)))
		return -ENOMEM;

	memset(sermouse, 0, sizeof(struct sermouse));

	init_input_dev(&sermouse->dev);
	sermouse->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_REL);
	sermouse->dev.keybit[LONG(BTN_MOUSE)] = BIT(BTN_LEFT) | BIT(BTN_RIGHT);
	sermouse->dev.relbit[0] = BIT(REL_X) | BIT(REL_Y);
	sermouse->dev.private = sermouse;

	sermouse->type = serio->id.proto;
	c = serio->id.extra;

	if (c & 0x01) set_bit(BTN_MIDDLE, sermouse->dev.keybit);
	if (c & 0x02) set_bit(BTN_SIDE, sermouse->dev.keybit);
	if (c & 0x04) set_bit(BTN_EXTRA, sermouse->dev.keybit);
	if (c & 0x10) set_bit(REL_WHEEL, sermouse->dev.relbit);
	if (c & 0x20) set_bit(REL_HWHEEL, sermouse->dev.relbit);

	sprintf(sermouse->phys, "%s/input0", serio->phys);

	sermouse->dev.name = sermouse_protocols[sermouse->type];
	sermouse->dev.phys = sermouse->phys;
	sermouse->dev.id.bustype = BUS_RS232;
	sermouse->dev.id.vendor = sermouse->type;
	sermouse->dev.id.product = c;
	sermouse->dev.id.version = 0x0100;
	sermouse->dev.dev = &serio->dev;

	serio_set_drvdata(serio, sermouse);

	err = serio_open(serio, drv);
	if (err) {
		serio_set_drvdata(serio, NULL);
		kfree(sermouse);
		return err;
	}

	input_register_device(&sermouse->dev);

	printk(KERN_INFO "input: %s on %s\n", sermouse_protocols[sermouse->type], serio->phys);

	return 0;
}

static struct serio_device_id sermouse_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_MSC,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_SUN,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_MS,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_MP,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_MZ,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_MZP,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_MZPP,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, sermouse_serio_ids);

static struct serio_driver sermouse_drv = {
	.driver		= {
		.name	= "sermouse",
	},
	.description	= DRIVER_DESC,
	.id_table	= sermouse_serio_ids,
	.interrupt	= sermouse_interrupt,
	.connect	= sermouse_connect,
	.disconnect	= sermouse_disconnect,
};

static int __init sermouse_init(void)
{
	serio_register_driver(&sermouse_drv);
	return 0;
}

static void __exit sermouse_exit(void)
{
	serio_unregister_driver(&sermouse_drv);
}

module_init(sermouse_init);
module_exit(sermouse_exit);
