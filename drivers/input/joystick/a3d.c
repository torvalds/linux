/*
 * $Id: a3d.c,v 1.21 2002/01/22 20:11:50 vojtech Exp $
 *
 *  Copyright (c) 1998-2001 Vojtech Pavlik
 */

/*
 * FP-Gaming Assasin 3D joystick driver for Linux
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
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/gameport.h>
#include <linux/input.h>
#include <linux/jiffies.h>

#define DRIVER_DESC	"FP-Gaming Assasin 3D joystick driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

#define A3D_MAX_START		600	/* 600 us */
#define A3D_MAX_STROBE		80	/* 80 us */
#define A3D_MAX_LENGTH		40	/* 40*3 bits */

#define A3D_MODE_A3D		1	/* Assassin 3D */
#define A3D_MODE_PAN		2	/* Panther */
#define A3D_MODE_OEM		3	/* Panther OEM version */
#define A3D_MODE_PXL		4	/* Panther XL */

static char *a3d_names[] = { NULL, "FP-Gaming Assassin 3D", "MadCatz Panther", "OEM Panther",
			"MadCatz Panther XL", "MadCatz Panther XL w/ rudder" };

struct a3d {
	struct gameport *gameport;
	struct gameport *adc;
	struct input_dev *dev;
	int axes[4];
	int buttons;
	int mode;
	int length;
	int reads;
	int bads;
	char phys[32];
};

/*
 * a3d_read_packet() reads an Assassin 3D packet.
 */

static int a3d_read_packet(struct gameport *gameport, int length, char *data)
{
	unsigned long flags;
	unsigned char u, v;
	unsigned int t, s;
	int i;

	i = 0;
	t = gameport_time(gameport, A3D_MAX_START);
	s = gameport_time(gameport, A3D_MAX_STROBE);

	local_irq_save(flags);
	gameport_trigger(gameport);
	v = gameport_read(gameport);

	while (t > 0 && i < length) {
		t--;
		u = v; v = gameport_read(gameport);
		if (~v & u & 0x10) {
			data[i++] = v >> 5;
			t = s;
		}
	}

	local_irq_restore(flags);

	return i;
}

/*
 * a3d_csum() computes checksum of triplet packet
 */

static int a3d_csum(char *data, int count)
{
	int i, csum = 0;

	for (i = 0; i < count - 2; i++)
		csum += data[i];
	return (csum & 0x3f) != ((data[count - 2] << 3) | data[count - 1]);
}

static void a3d_read(struct a3d *a3d, unsigned char *data)
{
	struct input_dev *dev = a3d->dev;

	switch (a3d->mode) {

		case A3D_MODE_A3D:
		case A3D_MODE_OEM:
		case A3D_MODE_PAN:

			input_report_rel(dev, REL_X, ((data[5] << 6) | (data[6] << 3) | data[ 7]) - ((data[5] & 4) << 7));
			input_report_rel(dev, REL_Y, ((data[8] << 6) | (data[9] << 3) | data[10]) - ((data[8] & 4) << 7));

			input_report_key(dev, BTN_RIGHT,  data[2] & 1);
			input_report_key(dev, BTN_LEFT,   data[3] & 2);
			input_report_key(dev, BTN_MIDDLE, data[3] & 4);

			input_sync(dev);

			a3d->axes[0] = ((signed char)((data[11] << 6) | (data[12] << 3) | (data[13]))) + 128;
			a3d->axes[1] = ((signed char)((data[14] << 6) | (data[15] << 3) | (data[16]))) + 128;
			a3d->axes[2] = ((signed char)((data[17] << 6) | (data[18] << 3) | (data[19]))) + 128;
			a3d->axes[3] = ((signed char)((data[20] << 6) | (data[21] << 3) | (data[22]))) + 128;

			a3d->buttons = ((data[3] << 3) | data[4]) & 0xf;

			break;

		case A3D_MODE_PXL:

			input_report_rel(dev, REL_X, ((data[ 9] << 6) | (data[10] << 3) | data[11]) - ((data[ 9] & 4) << 7));
			input_report_rel(dev, REL_Y, ((data[12] << 6) | (data[13] << 3) | data[14]) - ((data[12] & 4) << 7));

			input_report_key(dev, BTN_RIGHT,  data[2] & 1);
			input_report_key(dev, BTN_LEFT,   data[3] & 2);
			input_report_key(dev, BTN_MIDDLE, data[3] & 4);
			input_report_key(dev, BTN_SIDE,   data[7] & 2);
			input_report_key(dev, BTN_EXTRA,  data[7] & 4);

			input_report_abs(dev, ABS_X,        ((signed char)((data[15] << 6) | (data[16] << 3) | (data[17]))) + 128);
			input_report_abs(dev, ABS_Y,        ((signed char)((data[18] << 6) | (data[19] << 3) | (data[20]))) + 128);
			input_report_abs(dev, ABS_RUDDER,   ((signed char)((data[21] << 6) | (data[22] << 3) | (data[23]))) + 128);
			input_report_abs(dev, ABS_THROTTLE, ((signed char)((data[24] << 6) | (data[25] << 3) | (data[26]))) + 128);

			input_report_abs(dev, ABS_HAT0X, ( data[5]       & 1) - ((data[5] >> 2) & 1));
			input_report_abs(dev, ABS_HAT0Y, ((data[5] >> 1) & 1) - ((data[6] >> 2) & 1));
			input_report_abs(dev, ABS_HAT1X, ((data[4] >> 1) & 1) - ( data[3]       & 1));
			input_report_abs(dev, ABS_HAT1Y, ((data[4] >> 2) & 1) - ( data[4]       & 1));

			input_report_key(dev, BTN_TRIGGER, data[8] & 1);
			input_report_key(dev, BTN_THUMB,   data[8] & 2);
			input_report_key(dev, BTN_TOP,     data[8] & 4);
			input_report_key(dev, BTN_PINKIE,  data[7] & 1);

			input_sync(dev);

			break;
	}
}


/*
 * a3d_poll() reads and analyzes A3D joystick data.
 */

static void a3d_poll(struct gameport *gameport)
{
	struct a3d *a3d = gameport_get_drvdata(gameport);
	unsigned char data[A3D_MAX_LENGTH];

	a3d->reads++;
	if (a3d_read_packet(a3d->gameport, a3d->length, data) != a3d->length ||
	    data[0] != a3d->mode || a3d_csum(data, a3d->length))
		a3d->bads++;
	else
		a3d_read(a3d, data);
}

/*
 * a3d_adc_cooked_read() copies the acis and button data to the
 * callers arrays. It could do the read itself, but the caller could
 * call this more than 50 times a second, which would use too much CPU.
 */

static int a3d_adc_cooked_read(struct gameport *gameport, int *axes, int *buttons)
{
	struct a3d *a3d = gameport->port_data;
	int i;

	for (i = 0; i < 4; i++)
		axes[i] = (a3d->axes[i] < 254) ? a3d->axes[i] : -1;
	*buttons = a3d->buttons;
	return 0;
}

/*
 * a3d_adc_open() is the gameport open routine. It refuses to serve
 * any but cooked data.
 */

static int a3d_adc_open(struct gameport *gameport, int mode)
{
	struct a3d *a3d = gameport->port_data;

	if (mode != GAMEPORT_MODE_COOKED)
		return -1;

	gameport_start_polling(a3d->gameport);
	return 0;
}

/*
 * a3d_adc_close() is a callback from the input close routine.
 */

static void a3d_adc_close(struct gameport *gameport)
{
	struct a3d *a3d = gameport->port_data;

	gameport_stop_polling(a3d->gameport);
}

/*
 * a3d_open() is a callback from the input open routine.
 */

static int a3d_open(struct input_dev *dev)
{
	struct a3d *a3d = dev->private;

	gameport_start_polling(a3d->gameport);
	return 0;
}

/*
 * a3d_close() is a callback from the input close routine.
 */

static void a3d_close(struct input_dev *dev)
{
	struct a3d *a3d = dev->private;

	gameport_stop_polling(a3d->gameport);
}

/*
 * a3d_connect() probes for A3D joysticks.
 */

static int a3d_connect(struct gameport *gameport, struct gameport_driver *drv)
{
	struct a3d *a3d;
	struct input_dev *input_dev;
	struct gameport *adc;
	unsigned char data[A3D_MAX_LENGTH];
	int i;
	int err;

	a3d = kzalloc(sizeof(struct a3d), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!a3d || !input_dev) {
		err = -ENOMEM;
		goto fail1;
	}

	a3d->dev = input_dev;
	a3d->gameport = gameport;

	gameport_set_drvdata(gameport, a3d);

	err = gameport_open(gameport, drv, GAMEPORT_MODE_RAW);
	if (err)
		goto fail1;

	i = a3d_read_packet(gameport, A3D_MAX_LENGTH, data);

	if (!i || a3d_csum(data, i)) {
		err = -ENODEV;
		goto fail2;
	}

	a3d->mode = data[0];

	if (!a3d->mode || a3d->mode > 5) {
		printk(KERN_WARNING "a3d.c: Unknown A3D device detected "
			"(%s, id=%d), contact <vojtech@ucw.cz>\n", gameport->phys, a3d->mode);
		err = -ENODEV;
		goto fail2;
	}

	gameport_set_poll_handler(gameport, a3d_poll);
	gameport_set_poll_interval(gameport, 20);

	sprintf(a3d->phys, "%s/input0", gameport->phys);

	input_dev->name = a3d_names[a3d->mode];
	input_dev->phys = a3d->phys;
	input_dev->id.bustype = BUS_GAMEPORT;
	input_dev->id.vendor = GAMEPORT_ID_VENDOR_MADCATZ;
	input_dev->id.product = a3d->mode;
	input_dev->id.version = 0x0100;
	input_dev->cdev.dev = &gameport->dev;
	input_dev->private = a3d;
	input_dev->open = a3d_open;
	input_dev->close = a3d_close;

	if (a3d->mode == A3D_MODE_PXL) {

		int axes[] = { ABS_X, ABS_Y, ABS_THROTTLE, ABS_RUDDER };

		a3d->length = 33;

		input_dev->evbit[0] |= BIT(EV_ABS) | BIT(EV_KEY) | BIT(EV_REL);
		input_dev->relbit[0] |= BIT(REL_X) | BIT(REL_Y);
		input_dev->absbit[0] |= BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_THROTTLE) | BIT(ABS_RUDDER)
					| BIT(ABS_HAT0X) | BIT(ABS_HAT0Y) | BIT(ABS_HAT1X) | BIT(ABS_HAT1Y);
		input_dev->keybit[LONG(BTN_MOUSE)] |= BIT(BTN_RIGHT) | BIT(BTN_LEFT) | BIT(BTN_MIDDLE)
							| BIT(BTN_SIDE) | BIT(BTN_EXTRA);
		input_dev->keybit[LONG(BTN_JOYSTICK)] |= BIT(BTN_TRIGGER) | BIT(BTN_THUMB) | BIT(BTN_TOP)
							| BIT(BTN_PINKIE);

		a3d_read(a3d, data);

		for (i = 0; i < 4; i++) {
			if (i < 2)
				input_set_abs_params(input_dev, axes[i], 48, input_dev->abs[axes[i]] * 2 - 48, 0, 8);
			else
				input_set_abs_params(input_dev, axes[i], 2, 253, 0, 0);
			input_set_abs_params(input_dev, ABS_HAT0X + i, -1, 1, 0, 0);
		}

	} else {
		a3d->length = 29;

		input_dev->evbit[0] |= BIT(EV_KEY) | BIT(EV_REL);
		input_dev->relbit[0] |= BIT(REL_X) | BIT(REL_Y);
		input_dev->keybit[LONG(BTN_MOUSE)] |= BIT(BTN_RIGHT) | BIT(BTN_LEFT) | BIT(BTN_MIDDLE);

		a3d_read(a3d, data);

		if (!(a3d->adc = adc = gameport_allocate_port()))
			printk(KERN_ERR "a3d: Not enough memory for ADC port\n");
		else {
			adc->port_data = a3d;
			adc->open = a3d_adc_open;
			adc->close = a3d_adc_close;
			adc->cooked_read = a3d_adc_cooked_read;
			adc->fuzz = 1;

			gameport_set_name(adc, a3d_names[a3d->mode]);
			gameport_set_phys(adc, "%s/gameport0", gameport->phys);
			adc->dev.parent = &gameport->dev;

			gameport_register_port(adc);
		}
	}

	err = input_register_device(a3d->dev);
	if (err)
		goto fail3;

	return 0;

 fail3:	if (a3d->adc)
		gameport_unregister_port(a3d->adc);
 fail2:	gameport_close(gameport);
 fail1:	gameport_set_drvdata(gameport, NULL);
	input_free_device(input_dev);
	kfree(a3d);
	return err;
}

static void a3d_disconnect(struct gameport *gameport)
{
	struct a3d *a3d = gameport_get_drvdata(gameport);

	input_unregister_device(a3d->dev);
	if (a3d->adc)
		gameport_unregister_port(a3d->adc);
	gameport_close(gameport);
	gameport_set_drvdata(gameport, NULL);
	kfree(a3d);
}

static struct gameport_driver a3d_drv = {
	.driver		= {
		.name	= "adc",
		.owner	= THIS_MODULE,
	},
	.description	= DRIVER_DESC,
	.connect	= a3d_connect,
	.disconnect	= a3d_disconnect,
};

static int __init a3d_init(void)
{
	gameport_register_driver(&a3d_drv);
	return 0;
}

static void __exit a3d_exit(void)
{
	gameport_unregister_driver(&a3d_drv);
}

module_init(a3d_init);
module_exit(a3d_exit);
