/*
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 */

/*
 * Logitech WingMan Warrior joystick driver for Linux
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
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>

#define DRIVER_DESC	"Logitech WingMan Warrior joystick driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Constants.
 */

#define WARRIOR_MAX_LENGTH	16
static char warrior_lengths[] = { 0, 4, 12, 3, 4, 4, 0, 0 };

/*
 * Per-Warrior data.
 */

struct warrior {
	struct input_dev *dev;
	int idx, len;
	unsigned char data[WARRIOR_MAX_LENGTH];
	char phys[32];
};

/*
 * warrior_process_packet() decodes packets the driver receives from the
 * Warrior. It updates the data accordingly.
 */

static void warrior_process_packet(struct warrior *warrior)
{
	struct input_dev *dev = warrior->dev;
	unsigned char *data = warrior->data;

	if (!warrior->idx) return;

	switch ((data[0] >> 4) & 7) {
		case 1:					/* Button data */
			input_report_key(dev, BTN_TRIGGER,  data[3]       & 1);
			input_report_key(dev, BTN_THUMB,   (data[3] >> 1) & 1);
			input_report_key(dev, BTN_TOP,     (data[3] >> 2) & 1);
			input_report_key(dev, BTN_TOP2,    (data[3] >> 3) & 1);
			break;
		case 3:					/* XY-axis info->data */
			input_report_abs(dev, ABS_X, ((data[0] & 8) << 5) - (data[2] | ((data[0] & 4) << 5)));
			input_report_abs(dev, ABS_Y, (data[1] | ((data[0] & 1) << 7)) - ((data[0] & 2) << 7));
			break;
		case 5:					/* Throttle, spinner, hat info->data */
			input_report_abs(dev, ABS_THROTTLE, (data[1] | ((data[0] & 1) << 7)) - ((data[0] & 2) << 7));
			input_report_abs(dev, ABS_HAT0X, (data[3] & 2 ? 1 : 0) - (data[3] & 1 ? 1 : 0));
			input_report_abs(dev, ABS_HAT0Y, (data[3] & 8 ? 1 : 0) - (data[3] & 4 ? 1 : 0));
			input_report_rel(dev, REL_DIAL,  (data[2] | ((data[0] & 4) << 5)) - ((data[0] & 8) << 5));
			break;
	}
	input_sync(dev);
}

/*
 * warrior_interrupt() is called by the low level driver when characters
 * are ready for us. We then buffer them for further processing, or call the
 * packet processing routine.
 */

static irqreturn_t warrior_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags)
{
	struct warrior *warrior = serio_get_drvdata(serio);

	if (data & 0x80) {
		if (warrior->idx) warrior_process_packet(warrior);
		warrior->idx = 0;
		warrior->len = warrior_lengths[(data >> 4) & 7];
	}

	if (warrior->idx < warrior->len)
		warrior->data[warrior->idx++] = data;

	if (warrior->idx == warrior->len) {
		if (warrior->idx) warrior_process_packet(warrior);
		warrior->idx = 0;
		warrior->len = 0;
	}
	return IRQ_HANDLED;
}

/*
 * warrior_disconnect() is the opposite of warrior_connect()
 */

static void warrior_disconnect(struct serio *serio)
{
	struct warrior *warrior = serio_get_drvdata(serio);

	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_unregister_device(warrior->dev);
	kfree(warrior);
}

/*
 * warrior_connect() is the routine that is called when someone adds a
 * new serio device. It looks for the Warrior, and if found, registers
 * it as an input device.
 */

static int warrior_connect(struct serio *serio, struct serio_driver *drv)
{
	struct warrior *warrior;
	struct input_dev *input_dev;
	int err = -ENOMEM;

	warrior = kzalloc(sizeof(struct warrior), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!warrior || !input_dev)
		goto fail1;

	warrior->dev = input_dev;
	snprintf(warrior->phys, sizeof(warrior->phys), "%s/input0", serio->phys);

	input_dev->name = "Logitech WingMan Warrior";
	input_dev->phys = warrior->phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = SERIO_WARRIOR;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &serio->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL) |
		BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TRIGGER)] = BIT_MASK(BTN_TRIGGER) |
		BIT_MASK(BTN_THUMB) | BIT_MASK(BTN_TOP) | BIT_MASK(BTN_TOP2);
	input_dev->relbit[0] = BIT_MASK(REL_DIAL);
	input_set_abs_params(input_dev, ABS_X, -64, 64, 0, 8);
	input_set_abs_params(input_dev, ABS_Y, -64, 64, 0, 8);
	input_set_abs_params(input_dev, ABS_THROTTLE, -112, 112, 0, 0);
	input_set_abs_params(input_dev, ABS_HAT0X, -1, 1, 0, 0);
	input_set_abs_params(input_dev, ABS_HAT0Y, -1, 1, 0, 0);

	serio_set_drvdata(serio, warrior);

	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	err = input_register_device(warrior->dev);
	if (err)
		goto fail3;

	return 0;

 fail3:	serio_close(serio);
 fail2:	serio_set_drvdata(serio, NULL);
 fail1:	input_free_device(input_dev);
	kfree(warrior);
	return err;
}

/*
 * The serio driver structure.
 */

static const struct serio_device_id warrior_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_WARRIOR,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, warrior_serio_ids);

static struct serio_driver warrior_drv = {
	.driver		= {
		.name	= "warrior",
	},
	.description	= DRIVER_DESC,
	.id_table	= warrior_serio_ids,
	.interrupt	= warrior_interrupt,
	.connect	= warrior_connect,
	.disconnect	= warrior_disconnect,
};

module_serio_driver(warrior_drv);
