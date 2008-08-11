/*
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 */

/*
 * Creative Labs Blaster GamePad Cobra driver for Linux
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

#define DRIVER_DESC	"Creative Labs Blaster GamePad Cobra driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

#define COBRA_MAX_STROBE	45	/* 45 us max wait for first strobe */
#define COBRA_LENGTH		36

static int cobra_btn[] = { BTN_START, BTN_SELECT, BTN_TL, BTN_TR, BTN_X, BTN_Y, BTN_Z, BTN_A, BTN_B, BTN_C, BTN_TL2, BTN_TR2, 0 };

struct cobra {
	struct gameport *gameport;
	struct input_dev *dev[2];
	int reads;
	int bads;
	unsigned char exists;
	char phys[2][32];
};

static unsigned char cobra_read_packet(struct gameport *gameport, unsigned int *data)
{
	unsigned long flags;
	unsigned char u, v, w;
	__u64 buf[2];
	int r[2], t[2];
	int i, j, ret;

	int strobe = gameport_time(gameport, COBRA_MAX_STROBE);

	for (i = 0; i < 2; i++) {
		r[i] = buf[i] = 0;
		t[i] = COBRA_MAX_STROBE;
	}

	local_irq_save(flags);

	u = gameport_read(gameport);

	do {
		t[0]--; t[1]--;
		v = gameport_read(gameport);
		for (i = 0, w = u ^ v; i < 2 && w; i++, w >>= 2)
			if (w & 0x30) {
				if ((w & 0x30) < 0x30 && r[i] < COBRA_LENGTH && t[i] > 0) {
					buf[i] |= (__u64)((w >> 5) & 1) << r[i]++;
					t[i] = strobe;
					u = v;
				} else t[i] = 0;
			}
	} while (t[0] > 0 || t[1] > 0);

	local_irq_restore(flags);

	ret = 0;

	for (i = 0; i < 2; i++) {

		if (r[i] != COBRA_LENGTH) continue;

		for (j = 0; j < COBRA_LENGTH && (buf[i] & 0x04104107f) ^ 0x041041040; j++)
			buf[i] = (buf[i] >> 1) | ((__u64)(buf[i] & 1) << (COBRA_LENGTH - 1));

		if (j < COBRA_LENGTH) ret |= (1 << i);

		data[i] = ((buf[i] >>  7) & 0x000001f) | ((buf[i] >>  8) & 0x00003e0)
			| ((buf[i] >>  9) & 0x0007c00) | ((buf[i] >> 10) & 0x00f8000)
			| ((buf[i] >> 11) & 0x1f00000);

	}

	return ret;
}

static void cobra_poll(struct gameport *gameport)
{
	struct cobra *cobra = gameport_get_drvdata(gameport);
	struct input_dev *dev;
	unsigned int data[2];
	int i, j, r;

	cobra->reads++;

	if ((r = cobra_read_packet(gameport, data)) != cobra->exists) {
		cobra->bads++;
		return;
	}

	for (i = 0; i < 2; i++)
		if (cobra->exists & r & (1 << i)) {

			dev = cobra->dev[i];

			input_report_abs(dev, ABS_X, ((data[i] >> 4) & 1) - ((data[i] >> 3) & 1));
			input_report_abs(dev, ABS_Y, ((data[i] >> 2) & 1) - ((data[i] >> 1) & 1));

			for (j = 0; cobra_btn[j]; j++)
				input_report_key(dev, cobra_btn[j], data[i] & (0x20 << j));

			input_sync(dev);

		}
}

static int cobra_open(struct input_dev *dev)
{
	struct cobra *cobra = input_get_drvdata(dev);

	gameport_start_polling(cobra->gameport);
	return 0;
}

static void cobra_close(struct input_dev *dev)
{
	struct cobra *cobra = input_get_drvdata(dev);

	gameport_stop_polling(cobra->gameport);
}

static int cobra_connect(struct gameport *gameport, struct gameport_driver *drv)
{
	struct cobra *cobra;
	struct input_dev *input_dev;
	unsigned int data[2];
	int i, j;
	int err;

	cobra = kzalloc(sizeof(struct cobra), GFP_KERNEL);
	if (!cobra)
		return -ENOMEM;

	cobra->gameport = gameport;

	gameport_set_drvdata(gameport, cobra);

	err = gameport_open(gameport, drv, GAMEPORT_MODE_RAW);
	if (err)
		goto fail1;

	cobra->exists = cobra_read_packet(gameport, data);

	for (i = 0; i < 2; i++)
		if ((cobra->exists >> i) & data[i] & 1) {
			printk(KERN_WARNING "cobra.c: Device %d on %s has the Ext bit set. ID is: %d"
				" Contact vojtech@ucw.cz\n", i, gameport->phys, (data[i] >> 2) & 7);
			cobra->exists &= ~(1 << i);
		}

	if (!cobra->exists) {
		err = -ENODEV;
		goto fail2;
	}

	gameport_set_poll_handler(gameport, cobra_poll);
	gameport_set_poll_interval(gameport, 20);

	for (i = 0; i < 2; i++) {
		if (~(cobra->exists >> i) & 1)
			continue;

		cobra->dev[i] = input_dev = input_allocate_device();
		if (!input_dev) {
			err = -ENOMEM;
			goto fail3;
		}

		snprintf(cobra->phys[i], sizeof(cobra->phys[i]),
			 "%s/input%d", gameport->phys, i);

		input_dev->name = "Creative Labs Blaster GamePad Cobra";
		input_dev->phys = cobra->phys[i];
		input_dev->id.bustype = BUS_GAMEPORT;
		input_dev->id.vendor = GAMEPORT_ID_VENDOR_CREATIVE;
		input_dev->id.product = 0x0008;
		input_dev->id.version = 0x0100;
		input_dev->dev.parent = &gameport->dev;

		input_set_drvdata(input_dev, cobra);

		input_dev->open = cobra_open;
		input_dev->close = cobra_close;

		input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
		input_set_abs_params(input_dev, ABS_X, -1, 1, 0, 0);
		input_set_abs_params(input_dev, ABS_Y, -1, 1, 0, 0);
		for (j = 0; cobra_btn[j]; j++)
			set_bit(cobra_btn[j], input_dev->keybit);

		err = input_register_device(cobra->dev[i]);
		if (err)
			goto fail4;
	}

	return 0;

 fail4:	input_free_device(cobra->dev[i]);
 fail3:	while (--i >= 0)
		if (cobra->dev[i])
			input_unregister_device(cobra->dev[i]);
 fail2:	gameport_close(gameport);
 fail1:	gameport_set_drvdata(gameport, NULL);
	kfree(cobra);
	return err;
}

static void cobra_disconnect(struct gameport *gameport)
{
	struct cobra *cobra = gameport_get_drvdata(gameport);
	int i;

	for (i = 0; i < 2; i++)
		if ((cobra->exists >> i) & 1)
			input_unregister_device(cobra->dev[i]);
	gameport_close(gameport);
	gameport_set_drvdata(gameport, NULL);
	kfree(cobra);
}

static struct gameport_driver cobra_drv = {
	.driver		= {
		.name	= "cobra",
	},
	.description	= DRIVER_DESC,
	.connect	= cobra_connect,
	.disconnect	= cobra_disconnect,
};

static int __init cobra_init(void)
{
	gameport_register_driver(&cobra_drv);
	return 0;
}

static void __exit cobra_exit(void)
{
	gameport_unregister_driver(&cobra_drv);
}

module_init(cobra_init);
module_exit(cobra_exit);
