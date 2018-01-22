/*
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 *
 *  Based on the work of:
 *	David Thompson
 *	Joseph Krahn
 */

/*
 * SpaceTec SpaceBall 2003/3003/4000 FLX driver for Linux
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
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/serio.h>

#define DRIVER_DESC	"SpaceTec SpaceBall 2003/3003/4000 FLX driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Constants.
 */

#define SPACEBALL_MAX_LENGTH	128
#define SPACEBALL_MAX_ID	9

#define SPACEBALL_1003      1
#define SPACEBALL_2003B     3
#define SPACEBALL_2003C     4
#define SPACEBALL_3003C     7
#define SPACEBALL_4000FLX   8
#define SPACEBALL_4000FLX_L 9

static int spaceball_axes[] = { ABS_X, ABS_Z, ABS_Y, ABS_RX, ABS_RZ, ABS_RY };
static char *spaceball_names[] = {
	"?", "SpaceTec SpaceBall 1003", "SpaceTec SpaceBall 2003", "SpaceTec SpaceBall 2003B",
	"SpaceTec SpaceBall 2003C", "SpaceTec SpaceBall 3003", "SpaceTec SpaceBall SpaceController",
	"SpaceTec SpaceBall 3003C", "SpaceTec SpaceBall 4000FLX", "SpaceTec SpaceBall 4000FLX Lefty" };

/*
 * Per-Ball data.
 */

struct spaceball {
	struct input_dev *dev;
	int idx;
	int escape;
	unsigned char data[SPACEBALL_MAX_LENGTH];
	char phys[32];
};

/*
 * spaceball_process_packet() decodes packets the driver receives from the
 * SpaceBall.
 */

static void spaceball_process_packet(struct spaceball* spaceball)
{
	struct input_dev *dev = spaceball->dev;
	unsigned char *data = spaceball->data;
	int i;

	if (spaceball->idx < 2) return;

	switch (spaceball->data[0]) {

		case 'D':					/* Ball data */
			if (spaceball->idx != 15) return;
			for (i = 0; i < 6; i++)
				input_report_abs(dev, spaceball_axes[i],
					(__s16)((data[2 * i + 3] << 8) | data[2 * i + 2]));
			break;

		case 'K':					/* Button data */
			if (spaceball->idx != 3) return;
			input_report_key(dev, BTN_1, (data[2] & 0x01) || (data[2] & 0x20));
			input_report_key(dev, BTN_2, data[2] & 0x02);
			input_report_key(dev, BTN_3, data[2] & 0x04);
			input_report_key(dev, BTN_4, data[2] & 0x08);
			input_report_key(dev, BTN_5, data[1] & 0x01);
			input_report_key(dev, BTN_6, data[1] & 0x02);
			input_report_key(dev, BTN_7, data[1] & 0x04);
			input_report_key(dev, BTN_8, data[1] & 0x10);
			break;

		case '.':					/* Advanced button data */
			if (spaceball->idx != 3) return;
			input_report_key(dev, BTN_1, data[2] & 0x01);
			input_report_key(dev, BTN_2, data[2] & 0x02);
			input_report_key(dev, BTN_3, data[2] & 0x04);
			input_report_key(dev, BTN_4, data[2] & 0x08);
			input_report_key(dev, BTN_5, data[2] & 0x10);
			input_report_key(dev, BTN_6, data[2] & 0x20);
			input_report_key(dev, BTN_7, data[2] & 0x80);
			input_report_key(dev, BTN_8, data[1] & 0x01);
			input_report_key(dev, BTN_9, data[1] & 0x02);
			input_report_key(dev, BTN_A, data[1] & 0x04);
			input_report_key(dev, BTN_B, data[1] & 0x08);
			input_report_key(dev, BTN_C, data[1] & 0x10);
			input_report_key(dev, BTN_MODE, data[1] & 0x20);
			break;

		case 'E':					/* Device error */
			spaceball->data[spaceball->idx - 1] = 0;
			printk(KERN_ERR "spaceball: Device error. [%s]\n", spaceball->data + 1);
			break;

		case '?':					/* Bad command packet */
			spaceball->data[spaceball->idx - 1] = 0;
			printk(KERN_ERR "spaceball: Bad command. [%s]\n", spaceball->data + 1);
			break;
	}

	input_sync(dev);
}

/*
 * Spaceball 4000 FLX packets all start with a one letter packet-type decriptor,
 * and end in 0x0d. It uses '^' as an escape for CR, XOFF and XON characters which
 * can occur in the axis values.
 */

static irqreturn_t spaceball_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags)
{
	struct spaceball *spaceball = serio_get_drvdata(serio);

	switch (data) {
		case 0xd:
			spaceball_process_packet(spaceball);
			spaceball->idx = 0;
			spaceball->escape = 0;
			break;
		case '^':
			if (!spaceball->escape) {
				spaceball->escape = 1;
				break;
			}
			spaceball->escape = 0;
			/* fall through */
		case 'M':
		case 'Q':
		case 'S':
			if (spaceball->escape) {
				spaceball->escape = 0;
				data &= 0x1f;
			}
			/* fall through */
		default:
			if (spaceball->escape)
				spaceball->escape = 0;
			if (spaceball->idx < SPACEBALL_MAX_LENGTH)
				spaceball->data[spaceball->idx++] = data;
			break;
	}
	return IRQ_HANDLED;
}

/*
 * spaceball_disconnect() is the opposite of spaceball_connect()
 */

static void spaceball_disconnect(struct serio *serio)
{
	struct spaceball* spaceball = serio_get_drvdata(serio);

	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_unregister_device(spaceball->dev);
	kfree(spaceball);
}

/*
 * spaceball_connect() is the routine that is called when someone adds a
 * new serio device that supports Spaceball protocol and registers it as
 * an input device.
 */

static int spaceball_connect(struct serio *serio, struct serio_driver *drv)
{
	struct spaceball *spaceball;
	struct input_dev *input_dev;
	int err = -ENOMEM;
	int i, id;

	if ((id = serio->id.id) > SPACEBALL_MAX_ID)
		return -ENODEV;

	spaceball = kmalloc(sizeof(struct spaceball), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!spaceball || !input_dev)
		goto fail1;

	spaceball->dev = input_dev;
	snprintf(spaceball->phys, sizeof(spaceball->phys), "%s/input0", serio->phys);

	input_dev->name = spaceball_names[id];
	input_dev->phys = spaceball->phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = SERIO_SPACEBALL;
	input_dev->id.product = id;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &serio->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

	switch (id) {
		case SPACEBALL_4000FLX:
		case SPACEBALL_4000FLX_L:
			input_dev->keybit[BIT_WORD(BTN_0)] |= BIT_MASK(BTN_9);
			input_dev->keybit[BIT_WORD(BTN_A)] |= BIT_MASK(BTN_A) |
				BIT_MASK(BTN_B) | BIT_MASK(BTN_C) |
				BIT_MASK(BTN_MODE);
			/* fall through */
		default:
			input_dev->keybit[BIT_WORD(BTN_0)] |= BIT_MASK(BTN_2) |
				BIT_MASK(BTN_3) | BIT_MASK(BTN_4) |
				BIT_MASK(BTN_5) | BIT_MASK(BTN_6) |
				BIT_MASK(BTN_7) | BIT_MASK(BTN_8);
			/* fall through */
		case SPACEBALL_3003C:
			input_dev->keybit[BIT_WORD(BTN_0)] |= BIT_MASK(BTN_1) |
				BIT_MASK(BTN_8);
	}

	for (i = 0; i < 3; i++) {
		input_set_abs_params(input_dev, ABS_X + i, -8000, 8000, 8, 40);
		input_set_abs_params(input_dev, ABS_RX + i, -1600, 1600, 2, 8);
	}

	serio_set_drvdata(serio, spaceball);

	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	err = input_register_device(spaceball->dev);
	if (err)
		goto fail3;

	return 0;

 fail3:	serio_close(serio);
 fail2:	serio_set_drvdata(serio, NULL);
 fail1:	input_free_device(input_dev);
	kfree(spaceball);
	return err;
}

/*
 * The serio driver structure.
 */

static const struct serio_device_id spaceball_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_SPACEBALL,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, spaceball_serio_ids);

static struct serio_driver spaceball_drv = {
	.driver		= {
		.name	= "spaceball",
	},
	.description	= DRIVER_DESC,
	.id_table	= spaceball_serio_ids,
	.interrupt	= spaceball_interrupt,
	.connect	= spaceball_connect,
	.disconnect	= spaceball_disconnect,
};

module_serio_driver(spaceball_drv);
