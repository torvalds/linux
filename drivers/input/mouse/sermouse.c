// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 */

/*
 *  Serial mouse driver for Linux
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/serio.h>

#define DRIVER_DESC	"Serial mouse driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

static const char *sermouse_protocols[] = { "None", "Mouse Systems Mouse", "Sun Mouse", "Microsoft Mouse",
					"Logitech M+ Mouse", "Microsoft MZ Mouse", "Logitech MZ+ Mouse",
					"Logitech MZ++ Mouse"};

struct sermouse {
	struct input_dev *dev;
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

static void sermouse_process_msc(struct sermouse *sermouse, signed char data)
{
	struct input_dev *dev = sermouse->dev;
	signed char *buf = sermouse->buf;

	switch (sermouse->count) {

		case 0:
			if ((data & 0xf8) != 0x80)
				return;
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

	if (++sermouse->count == 5)
		sermouse->count = 0;
}

/*
 * sermouse_process_ms() anlyzes the incoming MS(Z/+/++) bytestream and
 * generates events. With prediction it gets 80 updates/sec, assuming
 * standard 3-byte packets and 1200 bps.
 */

static void sermouse_process_ms(struct sermouse *sermouse, signed char data)
{
	struct input_dev *dev = sermouse->dev;
	signed char *buf = sermouse->buf;

	if (data & 0x40)
		sermouse->count = 0;
	else if (sermouse->count == 0)
		return;

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
					fallthrough;

				case SERIO_MP:
					if ((data >> 2) & 3) break;	/* M++ Wireless Extension packet. */
					input_report_key(dev, BTN_MIDDLE, (data >> 5) & 1);
					input_report_key(dev, BTN_SIDE,   (data >> 4) & 1);
					break;

				case SERIO_MZP:
				case SERIO_MZPP:
					input_report_key(dev, BTN_SIDE,   (data >> 5) & 1);
					fallthrough;

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
			if (sermouse->type != SERIO_MZPP)
				break;

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
		unsigned char data, unsigned int flags)
{
	struct sermouse *sermouse = serio_get_drvdata(serio);

	if (time_after(jiffies, sermouse->last + HZ/10))
		sermouse->count = 0;

	sermouse->last = jiffies;

	if (sermouse->type > SERIO_SUN)
		sermouse_process_ms(sermouse, data);
	else
		sermouse_process_msc(sermouse, data);

	return IRQ_HANDLED;
}

/*
 * sermouse_disconnect() cleans up after we don't want talk
 * to the mouse anymore.
 */

static void sermouse_disconnect(struct serio *serio)
{
	struct sermouse *sermouse = serio_get_drvdata(serio);

	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_unregister_device(sermouse->dev);
	kfree(sermouse);
}

/*
 * sermouse_connect() is a callback form the serio module when
 * an unhandled serio port is found.
 */

static int sermouse_connect(struct serio *serio, struct serio_driver *drv)
{
	struct sermouse *sermouse;
	struct input_dev *input_dev;
	unsigned char c = serio->id.extra;
	int err = -ENOMEM;

	sermouse = kzalloc(sizeof(struct sermouse), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!sermouse || !input_dev)
		goto fail1;

	sermouse->dev = input_dev;
	snprintf(sermouse->phys, sizeof(sermouse->phys), "%s/input0", serio->phys);
	sermouse->type = serio->id.proto;

	input_dev->name = sermouse_protocols[sermouse->type];
	input_dev->phys = sermouse->phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor  = sermouse->type;
	input_dev->id.product = c;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &serio->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
	input_dev->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) |
		BIT_MASK(BTN_RIGHT);
	input_dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);

	if (c & 0x01) set_bit(BTN_MIDDLE, input_dev->keybit);
	if (c & 0x02) set_bit(BTN_SIDE, input_dev->keybit);
	if (c & 0x04) set_bit(BTN_EXTRA, input_dev->keybit);
	if (c & 0x10) set_bit(REL_WHEEL, input_dev->relbit);
	if (c & 0x20) set_bit(REL_HWHEEL, input_dev->relbit);

	serio_set_drvdata(serio, sermouse);

	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	err = input_register_device(sermouse->dev);
	if (err)
		goto fail3;

	return 0;

 fail3:	serio_close(serio);
 fail2:	serio_set_drvdata(serio, NULL);
 fail1:	input_free_device(input_dev);
	kfree(sermouse);
	return err;
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

module_serio_driver(sermouse_drv);
