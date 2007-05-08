/*
 * $Id: grip.c,v 1.21 2002/01/22 20:27:57 vojtech Exp $
 *
 *  Copyright (c) 1998-2001 Vojtech Pavlik
 */

/*
 * Gravis/Kensington GrIP protocol joystick and gamepad driver for Linux
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
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/gameport.h>
#include <linux/input.h>
#include <linux/jiffies.h>

#define DRIVER_DESC	"Gravis GrIP protocol joystick driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

#define GRIP_MODE_GPP		1
#define GRIP_MODE_BD		2
#define GRIP_MODE_XT		3
#define GRIP_MODE_DC		4

#define GRIP_LENGTH_GPP		24
#define GRIP_STROBE_GPP		200	/* 200 us */
#define GRIP_LENGTH_XT		4
#define GRIP_STROBE_XT		64	/* 64 us */
#define GRIP_MAX_CHUNKS_XT	10
#define GRIP_MAX_BITS_XT	30

struct grip {
	struct gameport *gameport;
	struct input_dev *dev[2];
	unsigned char mode[2];
	int reads;
	int bads;
	char phys[2][32];
};

static int grip_btn_gpp[] = { BTN_START, BTN_SELECT, BTN_TR2, BTN_Y, 0, BTN_TL2, BTN_A, BTN_B, BTN_X, 0, BTN_TL, BTN_TR, -1 };
static int grip_btn_bd[] = { BTN_THUMB, BTN_THUMB2, BTN_TRIGGER, BTN_TOP, BTN_BASE, -1 };
static int grip_btn_xt[] = { BTN_TRIGGER, BTN_THUMB, BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z, BTN_SELECT, BTN_START, BTN_MODE, -1 };
static int grip_btn_dc[] = { BTN_TRIGGER, BTN_THUMB, BTN_TOP, BTN_TOP2, BTN_BASE, BTN_BASE2, BTN_BASE3, BTN_BASE4, BTN_BASE5, -1 };

static int grip_abs_gpp[] = { ABS_X, ABS_Y, -1 };
static int grip_abs_bd[] = { ABS_X, ABS_Y, ABS_THROTTLE, ABS_HAT0X, ABS_HAT0Y, -1 };
static int grip_abs_xt[] = { ABS_X, ABS_Y, ABS_BRAKE, ABS_GAS, ABS_THROTTLE, ABS_HAT0X, ABS_HAT0Y, ABS_HAT1X, ABS_HAT1Y, -1 };
static int grip_abs_dc[] = { ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_THROTTLE, ABS_HAT0X, ABS_HAT0Y, -1 };

static char *grip_name[] = { NULL, "Gravis GamePad Pro", "Gravis Blackhawk Digital",
				"Gravis Xterminator Digital", "Gravis Xterminator DualControl" };
static int *grip_abs[] = { NULL, grip_abs_gpp, grip_abs_bd, grip_abs_xt, grip_abs_dc };
static int *grip_btn[] = { NULL, grip_btn_gpp, grip_btn_bd, grip_btn_xt, grip_btn_dc };
static char grip_anx[] = { 0, 0, 3, 5, 5 };
static char grip_cen[] = { 0, 0, 2, 2, 4 };

/*
 * grip_gpp_read_packet() reads a Gravis GamePad Pro packet.
 */

static int grip_gpp_read_packet(struct gameport *gameport, int shift, unsigned int *data)
{
	unsigned long flags;
	unsigned char u, v;
	unsigned int t;
	int i;

	int strobe = gameport_time(gameport, GRIP_STROBE_GPP);

	data[0] = 0;
	t = strobe;
	i = 0;

	local_irq_save(flags);

	v = gameport_read(gameport) >> shift;

	do {
		t--;
		u = v; v = (gameport_read(gameport) >> shift) & 3;
		if (~v & u & 1) {
			data[0] |= (v >> 1) << i++;
			t = strobe;
		}
	} while (i < GRIP_LENGTH_GPP && t > 0);

	local_irq_restore(flags);

	if (i < GRIP_LENGTH_GPP) return -1;

	for (i = 0; i < GRIP_LENGTH_GPP && (data[0] & 0xfe4210) ^ 0x7c0000; i++)
		data[0] = data[0] >> 1 | (data[0] & 1) << (GRIP_LENGTH_GPP - 1);

	return -(i == GRIP_LENGTH_GPP);
}

/*
 * grip_xt_read_packet() reads a Gravis Xterminator packet.
 */

static int grip_xt_read_packet(struct gameport *gameport, int shift, unsigned int *data)
{
	unsigned int i, j, buf, crc;
	unsigned char u, v, w;
	unsigned long flags;
	unsigned int t;
	char status;

	int strobe = gameport_time(gameport, GRIP_STROBE_XT);

	data[0] = data[1] = data[2] = data[3] = 0;
	status = buf = i = j = 0;
	t = strobe;

	local_irq_save(flags);

	v = w = (gameport_read(gameport) >> shift) & 3;

	do {
		t--;
		u = (gameport_read(gameport) >> shift) & 3;

		if (u ^ v) {

			if ((u ^ v) & 1) {
				buf = (buf << 1) | (u >> 1);
				t = strobe;
				i++;
			} else

			if ((((u ^ v) & (v ^ w)) >> 1) & ~(u | v | w) & 1) {
				if (i == 20) {
					crc = buf ^ (buf >> 7) ^ (buf >> 14);
					if (!((crc ^ (0x25cb9e70 >> ((crc >> 2) & 0x1c))) & 0xf)) {
						data[buf >> 18] = buf >> 4;
						status |= 1 << (buf >> 18);
					}
					j++;
				}
				t = strobe;
				buf = 0;
				i = 0;
			}
			w = v;
			v = u;
		}

	} while (status != 0xf && i < GRIP_MAX_BITS_XT && j < GRIP_MAX_CHUNKS_XT && t > 0);

	local_irq_restore(flags);

	return -(status != 0xf);
}

/*
 * grip_timer() repeatedly polls the joysticks and generates events.
 */

static void grip_poll(struct gameport *gameport)
{
	struct grip *grip = gameport_get_drvdata(gameport);
	unsigned int data[GRIP_LENGTH_XT];
	struct input_dev *dev;
	int i, j;

	for (i = 0; i < 2; i++) {

		dev = grip->dev[i];
		if (!dev)
			continue;

		grip->reads++;

		switch (grip->mode[i]) {

			case GRIP_MODE_GPP:

				if (grip_gpp_read_packet(grip->gameport, (i << 1) + 4, data)) {
					grip->bads++;
					break;
				}

				input_report_abs(dev, ABS_X, ((*data >> 15) & 1) - ((*data >> 16) & 1));
				input_report_abs(dev, ABS_Y, ((*data >> 13) & 1) - ((*data >> 12) & 1));

				for (j = 0; j < 12; j++)
					if (grip_btn_gpp[j])
						input_report_key(dev, grip_btn_gpp[j], (*data >> j) & 1);

				break;

			case GRIP_MODE_BD:

				if (grip_xt_read_packet(grip->gameport, (i << 1) + 4, data)) {
					grip->bads++;
					break;
				}

				input_report_abs(dev, ABS_X,        (data[0] >> 2) & 0x3f);
				input_report_abs(dev, ABS_Y,  63 - ((data[0] >> 8) & 0x3f));
				input_report_abs(dev, ABS_THROTTLE, (data[2] >> 8) & 0x3f);

				input_report_abs(dev, ABS_HAT0X, ((data[2] >> 1) & 1) - ( data[2]       & 1));
				input_report_abs(dev, ABS_HAT0Y, ((data[2] >> 2) & 1) - ((data[2] >> 3) & 1));

				for (j = 0; j < 5; j++)
					input_report_key(dev, grip_btn_bd[j], (data[3] >> (j + 4)) & 1);

				break;

			case GRIP_MODE_XT:

				if (grip_xt_read_packet(grip->gameport, (i << 1) + 4, data)) {
					grip->bads++;
					break;
				}

				input_report_abs(dev, ABS_X,        (data[0] >> 2) & 0x3f);
				input_report_abs(dev, ABS_Y,  63 - ((data[0] >> 8) & 0x3f));
				input_report_abs(dev, ABS_BRAKE,    (data[1] >> 2) & 0x3f);
				input_report_abs(dev, ABS_GAS,	    (data[1] >> 8) & 0x3f);
				input_report_abs(dev, ABS_THROTTLE, (data[2] >> 8) & 0x3f);

				input_report_abs(dev, ABS_HAT0X, ((data[2] >> 1) & 1) - ( data[2]       & 1));
				input_report_abs(dev, ABS_HAT0Y, ((data[2] >> 2) & 1) - ((data[2] >> 3) & 1));
				input_report_abs(dev, ABS_HAT1X, ((data[2] >> 5) & 1) - ((data[2] >> 4) & 1));
				input_report_abs(dev, ABS_HAT1Y, ((data[2] >> 6) & 1) - ((data[2] >> 7) & 1));

				for (j = 0; j < 11; j++)
					input_report_key(dev, grip_btn_xt[j], (data[3] >> (j + 3)) & 1);
				break;

			case GRIP_MODE_DC:

				if (grip_xt_read_packet(grip->gameport, (i << 1) + 4, data)) {
					grip->bads++;
					break;
				}

				input_report_abs(dev, ABS_X,        (data[0] >> 2) & 0x3f);
				input_report_abs(dev, ABS_Y,        (data[0] >> 8) & 0x3f);
				input_report_abs(dev, ABS_RX,       (data[1] >> 2) & 0x3f);
				input_report_abs(dev, ABS_RY,	    (data[1] >> 8) & 0x3f);
				input_report_abs(dev, ABS_THROTTLE, (data[2] >> 8) & 0x3f);

				input_report_abs(dev, ABS_HAT0X, ((data[2] >> 1) & 1) - ( data[2]       & 1));
				input_report_abs(dev, ABS_HAT0Y, ((data[2] >> 2) & 1) - ((data[2] >> 3) & 1));

				for (j = 0; j < 9; j++)
					input_report_key(dev, grip_btn_dc[j], (data[3] >> (j + 3)) & 1);
				break;


		}

		input_sync(dev);
	}
}

static int grip_open(struct input_dev *dev)
{
	struct grip *grip = input_get_drvdata(dev);

	gameport_start_polling(grip->gameport);
	return 0;
}

static void grip_close(struct input_dev *dev)
{
	struct grip *grip = input_get_drvdata(dev);

	gameport_stop_polling(grip->gameport);
}

static int grip_connect(struct gameport *gameport, struct gameport_driver *drv)
{
	struct grip *grip;
	struct input_dev *input_dev;
	unsigned int data[GRIP_LENGTH_XT];
	int i, j, t;
	int err;

	if (!(grip = kzalloc(sizeof(struct grip), GFP_KERNEL)))
		return -ENOMEM;

	grip->gameport = gameport;

	gameport_set_drvdata(gameport, grip);

	err = gameport_open(gameport, drv, GAMEPORT_MODE_RAW);
	if (err)
		goto fail1;

	for (i = 0; i < 2; i++) {
		if (!grip_gpp_read_packet(gameport, (i << 1) + 4, data)) {
			grip->mode[i] = GRIP_MODE_GPP;
			continue;
		}
		if (!grip_xt_read_packet(gameport, (i << 1) + 4, data)) {
			if (!(data[3] & 7)) {
				grip->mode[i] = GRIP_MODE_BD;
				continue;
			}
			if (!(data[2] & 0xf0)) {
				grip->mode[i] = GRIP_MODE_XT;
				continue;
			}
			grip->mode[i] = GRIP_MODE_DC;
			continue;
		}
	}

	if (!grip->mode[0] && !grip->mode[1]) {
		err = -ENODEV;
		goto fail2;
	}

	gameport_set_poll_handler(gameport, grip_poll);
	gameport_set_poll_interval(gameport, 20);

	for (i = 0; i < 2; i++) {
		if (!grip->mode[i])
			continue;

		grip->dev[i] = input_dev = input_allocate_device();
		if (!input_dev) {
			err = -ENOMEM;
			goto fail3;
		}

		snprintf(grip->phys[i], sizeof(grip->phys[i]),
			 "%s/input%d", gameport->phys, i);

		input_dev->name = grip_name[grip->mode[i]];
		input_dev->phys = grip->phys[i];
		input_dev->id.bustype = BUS_GAMEPORT;
		input_dev->id.vendor = GAMEPORT_ID_VENDOR_GRAVIS;
		input_dev->id.product = grip->mode[i];
		input_dev->id.version = 0x0100;
		input_dev->dev.parent = &gameport->dev;

		input_set_drvdata(input_dev, grip);

		input_dev->open = grip_open;
		input_dev->close = grip_close;

		input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);

		for (j = 0; (t = grip_abs[grip->mode[i]][j]) >= 0; j++) {

			if (j < grip_cen[grip->mode[i]])
				input_set_abs_params(input_dev, t, 14, 52, 1, 2);
			else if (j < grip_anx[grip->mode[i]])
				input_set_abs_params(input_dev, t, 3, 57, 1, 0);
			else
				input_set_abs_params(input_dev, t, -1, 1, 0, 0);
		}

		for (j = 0; (t = grip_btn[grip->mode[i]][j]) >= 0; j++)
			if (t > 0)
				set_bit(t, input_dev->keybit);

		err = input_register_device(grip->dev[i]);
		if (err)
			goto fail4;
	}

	return 0;

 fail4:	input_free_device(grip->dev[i]);
 fail3:	while (--i >= 0)
		if (grip->dev[i])
			input_unregister_device(grip->dev[i]);
 fail2:	gameport_close(gameport);
 fail1:	gameport_set_drvdata(gameport, NULL);
	kfree(grip);
	return err;
}

static void grip_disconnect(struct gameport *gameport)
{
	struct grip *grip = gameport_get_drvdata(gameport);
	int i;

	for (i = 0; i < 2; i++)
		if (grip->dev[i])
			input_unregister_device(grip->dev[i]);
	gameport_close(gameport);
	gameport_set_drvdata(gameport, NULL);
	kfree(grip);
}

static struct gameport_driver grip_drv = {
	.driver		= {
		.name	= "grip",
		.owner	= THIS_MODULE,
	},
	.description	= DRIVER_DESC,
	.connect	= grip_connect,
	.disconnect	= grip_disconnect,
};

static int __init grip_init(void)
{
	gameport_register_driver(&grip_drv);
	return 0;
}

static void __exit grip_exit(void)
{
	gameport_unregister_driver(&grip_drv);
}

module_init(grip_init);
module_exit(grip_exit);
