/*
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *  Copyright (c) 2000 Mark Fletcher
 */

/*
 * Gravis Stinger gamepad driver for Linux
 */

/*
 * This program is free warftware; you can redistribute it and/or modify
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

#define DRIVER_DESC	"Gravis Stinger gamepad driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Constants.
 */

#define STINGER_MAX_LENGTH 8

/*
 * Per-Stinger data.
 */

struct stinger {
	struct input_dev *dev;
	int idx;
	unsigned char data[STINGER_MAX_LENGTH];
	char phys[32];
};

/*
 * stinger_process_packet() decodes packets the driver receives from the
 * Stinger. It updates the data accordingly.
 */

static void stinger_process_packet(struct stinger *stinger)
{
	struct input_dev *dev = stinger->dev;
	unsigned char *data = stinger->data;

	if (!stinger->idx) return;

	input_report_key(dev, BTN_A,	  ((data[0] & 0x20) >> 5));
	input_report_key(dev, BTN_B,	  ((data[0] & 0x10) >> 4));
	input_report_key(dev, BTN_C,	  ((data[0] & 0x08) >> 3));
	input_report_key(dev, BTN_X,	  ((data[0] & 0x04) >> 2));
	input_report_key(dev, BTN_Y,	  ((data[3] & 0x20) >> 5));
	input_report_key(dev, BTN_Z,	  ((data[3] & 0x10) >> 4));
	input_report_key(dev, BTN_TL,     ((data[3] & 0x08) >> 3));
	input_report_key(dev, BTN_TR,     ((data[3] & 0x04) >> 2));
	input_report_key(dev, BTN_SELECT, ((data[3] & 0x02) >> 1));
	input_report_key(dev, BTN_START,   (data[3] & 0x01));

	input_report_abs(dev, ABS_X, (data[1] & 0x3F) - ((data[0] & 0x01) << 6));
	input_report_abs(dev, ABS_Y, ((data[0] & 0x02) << 5) - (data[2] & 0x3F));

	input_sync(dev);

	return;
}

/*
 * stinger_interrupt() is called by the low level driver when characters
 * are ready for us. We then buffer them for further processing, or call the
 * packet processing routine.
 */

static irqreturn_t stinger_interrupt(struct serio *serio,
	unsigned char data, unsigned int flags)
{
	struct stinger *stinger = serio_get_drvdata(serio);

	/* All Stinger packets are 4 bytes */

	if (stinger->idx < STINGER_MAX_LENGTH)
		stinger->data[stinger->idx++] = data;

	if (stinger->idx == 4) {
		stinger_process_packet(stinger);
		stinger->idx = 0;
	}

	return IRQ_HANDLED;
}

/*
 * stinger_disconnect() is the opposite of stinger_connect()
 */

static void stinger_disconnect(struct serio *serio)
{
	struct stinger *stinger = serio_get_drvdata(serio);

	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_unregister_device(stinger->dev);
	kfree(stinger);
}

/*
 * stinger_connect() is the routine that is called when someone adds a
 * new serio device that supports Stinger protocol and registers it as
 * an input device.
 */

static int stinger_connect(struct serio *serio, struct serio_driver *drv)
{
	struct stinger *stinger;
	struct input_dev *input_dev;
	int err = -ENOMEM;

	stinger = kmalloc(sizeof(struct stinger), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!stinger || !input_dev)
		goto fail1;

	stinger->dev = input_dev;
	snprintf(stinger->phys, sizeof(stinger->phys), "%s/serio0", serio->phys);

	input_dev->name = "Gravis Stinger";
	input_dev->phys = stinger->phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = SERIO_STINGER;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &serio->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_A)] = BIT_MASK(BTN_A) | BIT_MASK(BTN_B) |
		BIT_MASK(BTN_C) | BIT_MASK(BTN_X) | BIT_MASK(BTN_Y) |
		BIT_MASK(BTN_Z) | BIT_MASK(BTN_TL) | BIT_MASK(BTN_TR) |
		BIT_MASK(BTN_START) | BIT_MASK(BTN_SELECT);
	input_set_abs_params(input_dev, ABS_X, -64, 64, 0, 4);
	input_set_abs_params(input_dev, ABS_Y, -64, 64, 0, 4);

	serio_set_drvdata(serio, stinger);

	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	err = input_register_device(stinger->dev);
	if (err)
		goto fail3;

	return 0;

 fail3:	serio_close(serio);
 fail2:	serio_set_drvdata(serio, NULL);
 fail1:	input_free_device(input_dev);
	kfree(stinger);
	return err;
}

/*
 * The serio driver structure.
 */

static struct serio_device_id stinger_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_STINGER,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, stinger_serio_ids);

static struct serio_driver stinger_drv = {
	.driver		= {
		.name	= "stinger",
	},
	.description	= DRIVER_DESC,
	.id_table	= stinger_serio_ids,
	.interrupt	= stinger_interrupt,
	.connect	= stinger_connect,
	.disconnect	= stinger_disconnect,
};

module_serio_driver(stinger_drv);
