/*
 *  derived from "twidjoy.c"
 *
 *  Copyright (c) 2008 Martin Kebert
 *  Copyright (c) 2001 Arndt Schoenewald
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *  Copyright (c) 2000 Mark Fletcher
 *
 */

/*
 * Driver to use 4CH RC transmitter using Zhen Hua 5-byte protocol (Walkera Lama,
 * EasyCopter etc.) as a joystick under Linux.
 *
 * RC transmitters using Zhen Hua 5-byte protocol are cheap four channels
 * transmitters for control a RC planes or RC helicopters with possibility to
 * connect on a serial port.
 * Data coming from transmitter is in this order:
 * 1. byte = synchronisation byte
 * 2. byte = X axis
 * 3. byte = Y axis
 * 4. byte = RZ axis
 * 5. byte = Z axis
 * (and this is repeated)
 *
 * For questions or feedback regarding this driver module please contact:
 * Martin Kebert <gkmarty@gmail.com> - but I am not a C-programmer nor kernel
 * coder :-(
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
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/init.h>

#define DRIVER_DESC	"RC transmitter with 5-byte Zhen Hua protocol joystick driver"

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Constants.
 */

#define ZHENHUA_MAX_LENGTH 5

/*
 * Zhen Hua data.
 */

struct zhenhua {
	struct input_dev *dev;
	int idx;
	unsigned char data[ZHENHUA_MAX_LENGTH];
	char phys[32];
};


/* bits in all incoming bytes needs to be "reversed" */
static int zhenhua_bitreverse(int x)
{
	x = ((x & 0xaa) >> 1) | ((x & 0x55) << 1);
	x = ((x & 0xcc) >> 2) | ((x & 0x33) << 2);
	x = ((x & 0xf0) >> 4) | ((x & 0x0f) << 4);
	return x;
}

/*
 * zhenhua_process_packet() decodes packets the driver receives from the
 * RC transmitter. It updates the data accordingly.
 */

static void zhenhua_process_packet(struct zhenhua *zhenhua)
{
	struct input_dev *dev = zhenhua->dev;
	unsigned char *data = zhenhua->data;

	input_report_abs(dev, ABS_Y, data[1]);
	input_report_abs(dev, ABS_X, data[2]);
	input_report_abs(dev, ABS_RZ, data[3]);
	input_report_abs(dev, ABS_Z, data[4]);

	input_sync(dev);
}

/*
 * zhenhua_interrupt() is called by the low level driver when characters
 * are ready for us. We then buffer them for further processing, or call the
 * packet processing routine.
 */

static irqreturn_t zhenhua_interrupt(struct serio *serio, unsigned char data, unsigned int flags)
{
	struct zhenhua *zhenhua = serio_get_drvdata(serio);

	/* All Zhen Hua packets are 5 bytes. The fact that the first byte
	 * is allways 0xf7 and all others are in range 0x32 - 0xc8 (50-200)
	 * can be used to check and regain sync. */

	if (data == 0xef)
		zhenhua->idx = 0;	/* this byte starts a new packet */
	else if (zhenhua->idx == 0)
		return IRQ_HANDLED;	/* wrong MSB -- ignore this byte */

	if (zhenhua->idx < ZHENHUA_MAX_LENGTH)
		zhenhua->data[zhenhua->idx++] = zhenhua_bitreverse(data);

	if (zhenhua->idx == ZHENHUA_MAX_LENGTH) {
		zhenhua_process_packet(zhenhua);
		zhenhua->idx = 0;
	}

	return IRQ_HANDLED;
}

/*
 * zhenhua_disconnect() is the opposite of zhenhua_connect()
 */

static void zhenhua_disconnect(struct serio *serio)
{
	struct zhenhua *zhenhua = serio_get_drvdata(serio);

	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_unregister_device(zhenhua->dev);
	kfree(zhenhua);
}

/*
 * zhenhua_connect() is the routine that is called when someone adds a
 * new serio device. It looks for the Twiddler, and if found, registers
 * it as an input device.
 */

static int zhenhua_connect(struct serio *serio, struct serio_driver *drv)
{
	struct zhenhua *zhenhua;
	struct input_dev *input_dev;
	int err = -ENOMEM;

	zhenhua = kzalloc(sizeof(struct zhenhua), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!zhenhua || !input_dev)
		goto fail1;

	zhenhua->dev = input_dev;
	snprintf(zhenhua->phys, sizeof(zhenhua->phys), "%s/input0", serio->phys);

	input_dev->name = "Zhen Hua 5-byte device";
	input_dev->phys = zhenhua->phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = SERIO_ZHENHUA;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &serio->dev;

	input_dev->evbit[0] = BIT(EV_ABS);
	input_set_abs_params(input_dev, ABS_X, 50, 200, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 50, 200, 0, 0);
	input_set_abs_params(input_dev, ABS_Z, 50, 200, 0, 0);
	input_set_abs_params(input_dev, ABS_RZ, 50, 200, 0, 0);

	serio_set_drvdata(serio, zhenhua);

	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	err = input_register_device(zhenhua->dev);
	if (err)
		goto fail3;

	return 0;

 fail3:	serio_close(serio);
 fail2:	serio_set_drvdata(serio, NULL);
 fail1:	input_free_device(input_dev);
	kfree(zhenhua);
	return err;
}

/*
 * The serio driver structure.
 */

static struct serio_device_id zhenhua_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_ZHENHUA,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, zhenhua_serio_ids);

static struct serio_driver zhenhua_drv = {
	.driver		= {
		.name	= "zhenhua",
	},
	.description	= DRIVER_DESC,
	.id_table	= zhenhua_serio_ids,
	.interrupt	= zhenhua_interrupt,
	.connect	= zhenhua_connect,
	.disconnect	= zhenhua_disconnect,
};

module_serio_driver(zhenhua_drv);
