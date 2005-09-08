/*
 * $Id: tmdc.c,v 1.31 2002/01/22 20:29:52 vojtech Exp $
 *
 *  Copyright (c) 1998-2001 Vojtech Pavlik
 *
 *   Based on the work of:
 *	Trystan Larey-Williams
 */

/*
 * ThrustMaster DirectConnect (BSP) joystick family driver for Linux
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
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/gameport.h>
#include <linux/input.h>

#define DRIVER_DESC	"ThrustMaster DirectConnect joystick driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

#define TMDC_MAX_START		600	/* 600 us */
#define TMDC_MAX_STROBE		60	/* 60 us */
#define TMDC_MAX_LENGTH		13

#define TMDC_MODE_M3DI		1
#define TMDC_MODE_3DRP		3
#define TMDC_MODE_AT		4
#define TMDC_MODE_FM		8
#define TMDC_MODE_FGP		163

#define TMDC_BYTE_ID		10
#define TMDC_BYTE_REV		11
#define TMDC_BYTE_DEF		12

#define TMDC_ABS		7
#define TMDC_ABS_HAT		4
#define TMDC_BTN		16

static unsigned char tmdc_byte_a[16] = { 0, 1, 3, 4, 6, 7 };
static unsigned char tmdc_byte_d[16] = { 2, 5, 8, 9 };

static signed char tmdc_abs[TMDC_ABS] =
	{ ABS_X, ABS_Y, ABS_RUDDER, ABS_THROTTLE, ABS_RX, ABS_RY, ABS_RZ };
static signed char tmdc_abs_hat[TMDC_ABS_HAT] =
	{ ABS_HAT0X, ABS_HAT0Y, ABS_HAT1X, ABS_HAT1Y };
static signed char tmdc_abs_at[TMDC_ABS] =
	{ ABS_X, ABS_Y, ABS_RUDDER, -1, ABS_THROTTLE };
static signed char tmdc_abs_fm[TMDC_ABS] =
	{ ABS_RX, ABS_RY, ABS_X, ABS_Y };

static short tmdc_btn_pad[TMDC_BTN] =
	{ BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z, BTN_START, BTN_SELECT, BTN_TL, BTN_TR };
static short tmdc_btn_joy[TMDC_BTN] =
	{ BTN_TRIGGER, BTN_THUMB, BTN_TOP, BTN_TOP2, BTN_BASE, BTN_BASE2, BTN_THUMB2, BTN_PINKIE,
	  BTN_BASE3, BTN_BASE4, BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z };
static short tmdc_btn_fm[TMDC_BTN] =
        { BTN_TRIGGER, BTN_C, BTN_B, BTN_A, BTN_THUMB, BTN_X, BTN_Y, BTN_Z, BTN_TOP, BTN_TOP2 };
static short tmdc_btn_at[TMDC_BTN] =
        { BTN_TRIGGER, BTN_THUMB2, BTN_PINKIE, BTN_THUMB, BTN_BASE6, BTN_BASE5, BTN_BASE4,
          BTN_BASE3, BTN_BASE2, BTN_BASE };

static struct {
        int x;
        int y;
} tmdc_hat_to_axis[] = {{ 0, 0}, { 1, 0}, { 0,-1}, {-1, 0}, { 0, 1}};

struct tmdc {
	struct gameport *gameport;
	struct input_dev dev[2];
	char name[2][64];
	char phys[2][32];
	int mode[2];
	signed char *abs[2];
	short *btn[2];
	unsigned char absc[2];
	unsigned char btnc[2][4];
	unsigned char btno[2][4];
	int reads;
	int bads;
	unsigned char exists;
};

/*
 * tmdc_read_packet() reads a ThrustMaster packet.
 */

static int tmdc_read_packet(struct gameport *gameport, unsigned char data[2][TMDC_MAX_LENGTH])
{
	unsigned char u, v, w, x;
	unsigned long flags;
	int i[2], j[2], t[2], p, k;

	p = gameport_time(gameport, TMDC_MAX_STROBE);

	for (k = 0; k < 2; k++) {
		t[k] = gameport_time(gameport, TMDC_MAX_START);
		i[k] = j[k] = 0;
	}

	local_irq_save(flags);
	gameport_trigger(gameport);

	w = gameport_read(gameport) >> 4;

	do {
		x = w;
		w = gameport_read(gameport) >> 4;

		for (k = 0, v = w, u = x; k < 2; k++, v >>= 2, u >>= 2) {
			if (~v & u & 2) {
				if (t[k] <= 0 || i[k] >= TMDC_MAX_LENGTH) continue;
				t[k] = p;
				if (j[k] == 0) {				 /* Start bit */
					if (~v & 1) t[k] = 0;
					data[k][i[k]] = 0; j[k]++; continue;
				}
				if (j[k] == 9) {				/* Stop bit */
					if (v & 1) t[k] = 0;
					j[k] = 0; i[k]++; continue;
				}
				data[k][i[k]] |= (~v & 1) << (j[k]++ - 1);	/* Data bit */
			}
			t[k]--;
		}
	} while (t[0] > 0 || t[1] > 0);

	local_irq_restore(flags);

	return (i[0] == TMDC_MAX_LENGTH) | ((i[1] == TMDC_MAX_LENGTH) << 1);
}

/*
 * tmdc_poll() reads and analyzes ThrustMaster joystick data.
 */

static void tmdc_poll(struct gameport *gameport)
{
	unsigned char data[2][TMDC_MAX_LENGTH];
	struct tmdc *tmdc = gameport_get_drvdata(gameport);
	struct input_dev *dev;
	unsigned char r, bad = 0;
	int i, j, k, l;

	tmdc->reads++;

	if ((r = tmdc_read_packet(tmdc->gameport, data)) != tmdc->exists)
		bad = 1;
	else

	for (j = 0; j < 2; j++)
		if (r & (1 << j) & tmdc->exists) {

			if (data[j][TMDC_BYTE_ID] != tmdc->mode[j]) {
				bad = 1;
				continue;
			}

			dev = tmdc->dev + j;

			for (i = 0; i < tmdc->absc[j]; i++) {
				if (tmdc->abs[j][i] < 0) continue;
				input_report_abs(dev, tmdc->abs[j][i], data[j][tmdc_byte_a[i]]);
			}

			switch (tmdc->mode[j]) {

				case TMDC_MODE_M3DI:

					i = tmdc_byte_d[0];
					input_report_abs(dev, ABS_HAT0X, ((data[j][i] >> 3) & 1) - ((data[j][i] >> 1) & 1));
					input_report_abs(dev, ABS_HAT0Y, ((data[j][i] >> 2) & 1) - ( data[j][i]       & 1));
					break;

				case TMDC_MODE_AT:

					i = tmdc_byte_a[3];
					input_report_abs(dev, ABS_HAT0X, tmdc_hat_to_axis[(data[j][i] - 141) / 25].x);
					input_report_abs(dev, ABS_HAT0Y, tmdc_hat_to_axis[(data[j][i] - 141) / 25].y);
					break;

			}

			for (k = l = 0; k < 4; k++) {
				for (i = 0; i < tmdc->btnc[j][k]; i++)
					input_report_key(dev, tmdc->btn[j][i + l],
						((data[j][tmdc_byte_d[k]] >> (i + tmdc->btno[j][k])) & 1));
				l += tmdc->btnc[j][k];
			}

			input_sync(dev);
	}

	tmdc->bads += bad;
}

static int tmdc_open(struct input_dev *dev)
{
	struct tmdc *tmdc = dev->private;

	gameport_start_polling(tmdc->gameport);
	return 0;
}

static void tmdc_close(struct input_dev *dev)
{
	struct tmdc *tmdc = dev->private;

	gameport_stop_polling(tmdc->gameport);
}

/*
 * tmdc_probe() probes for ThrustMaster type joysticks.
 */

static int tmdc_connect(struct gameport *gameport, struct gameport_driver *drv)
{
	static struct models {
		unsigned char id;
		char *name;
		char abs;
		char hats;
		char btnc[4];
		char btno[4];
		signed char *axes;
		short *buttons;
	} models[] = {	{   1, "ThrustMaster Millenium 3D Inceptor",	  6, 2, { 4, 2 }, { 4, 6 }, tmdc_abs, tmdc_btn_joy },
			{   3, "ThrustMaster Rage 3D Gamepad",		  2, 0, { 8, 2 }, { 0, 0 }, tmdc_abs, tmdc_btn_pad },
			{   4, "ThrustMaster Attack Throttle",		  5, 2, { 4, 6 }, { 4, 2 }, tmdc_abs_at, tmdc_btn_at },
			{   8, "ThrustMaster FragMaster",		  4, 0, { 8, 2 }, { 0, 0 }, tmdc_abs_fm, tmdc_btn_fm },
			{ 163, "Thrustmaster Fusion GamePad",		  2, 0, { 8, 2 }, { 0, 0 }, tmdc_abs, tmdc_btn_pad },
			{   0, "Unknown %d-axis, %d-button TM device %d", 0, 0, { 0, 0 }, { 0, 0 }, tmdc_abs, tmdc_btn_joy }};

	unsigned char data[2][TMDC_MAX_LENGTH];
	struct tmdc *tmdc;
	int i, j, k, l, m;
	int err;

	if (!(tmdc = kzalloc(sizeof(struct tmdc), GFP_KERNEL)))
		return -ENOMEM;

	tmdc->gameport = gameport;

	gameport_set_drvdata(gameport, tmdc);

	err = gameport_open(gameport, drv, GAMEPORT_MODE_RAW);
	if (err)
		goto fail1;

	if (!(tmdc->exists = tmdc_read_packet(gameport, data))) {
		err = -ENODEV;
		goto fail2;
	}

	gameport_set_poll_handler(gameport, tmdc_poll);
	gameport_set_poll_interval(gameport, 20);

	for (j = 0; j < 2; j++)
		if (tmdc->exists & (1 << j)) {

			tmdc->mode[j] = data[j][TMDC_BYTE_ID];

			for (m = 0; models[m].id && models[m].id != tmdc->mode[j]; m++);

			tmdc->abs[j] = models[m].axes;
			tmdc->btn[j] = models[m].buttons;

			if (!models[m].id) {
				models[m].abs = data[j][TMDC_BYTE_DEF] >> 4;
				for (k = 0; k < 4; k++)
					models[m].btnc[k] = k < (data[j][TMDC_BYTE_DEF] & 0xf) ? 8 : 0;
			}

			tmdc->absc[j] = models[m].abs;
			for (k = 0; k < 4; k++) {
				tmdc->btnc[j][k] = models[m].btnc[k];
				tmdc->btno[j][k] = models[m].btno[k];
			}

			sprintf(tmdc->name[j], models[m].name, models[m].abs,
				(data[j][TMDC_BYTE_DEF] & 0xf) << 3, tmdc->mode[j]);

			sprintf(tmdc->phys[j], "%s/input%d", gameport->phys, j);

			tmdc->dev[j].private = tmdc;
			tmdc->dev[j].open = tmdc_open;
			tmdc->dev[j].close = tmdc_close;

			tmdc->dev[j].name = tmdc->name[j];
			tmdc->dev[j].phys = tmdc->phys[j];
			tmdc->dev[j].id.bustype = BUS_GAMEPORT;
			tmdc->dev[j].id.vendor = GAMEPORT_ID_VENDOR_THRUSTMASTER;
			tmdc->dev[j].id.product = models[m].id;
			tmdc->dev[j].id.version = 0x0100;

			tmdc->dev[j].evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);

			for (i = 0; i < models[m].abs && i < TMDC_ABS; i++)
				if (tmdc->abs[j][i] >= 0)
					input_set_abs_params(&tmdc->dev[j], tmdc->abs[j][i], 8, 248, 2, 4);

			for (i = 0; i < models[m].hats && i < TMDC_ABS_HAT; i++)
				input_set_abs_params(&tmdc->dev[j], tmdc_abs_hat[i], -1, 1, 0, 0);


			for (k = l = 0; k < 4; k++) {
				for (i = 0; i < models[m].btnc[k] && i < TMDC_BTN; i++)
					set_bit(tmdc->btn[j][i + l], tmdc->dev[j].keybit);
				l += models[m].btnc[k];
			}

			input_register_device(tmdc->dev + j);
			printk(KERN_INFO "input: %s on %s\n", tmdc->name[j], gameport->phys);
		}

	return 0;

fail2:	gameport_close(gameport);
fail1:	gameport_set_drvdata(gameport, NULL);
	kfree(tmdc);
	return err;
}

static void tmdc_disconnect(struct gameport *gameport)
{
	struct tmdc *tmdc = gameport_get_drvdata(gameport);
	int i;

	for (i = 0; i < 2; i++)
		if (tmdc->exists & (1 << i))
			input_unregister_device(tmdc->dev + i);
	gameport_close(gameport);
	gameport_set_drvdata(gameport, NULL);
	kfree(tmdc);
}

static struct gameport_driver tmdc_drv = {
	.driver		= {
		.name	= "tmdc",
	},
	.description	= DRIVER_DESC,
	.connect	= tmdc_connect,
	.disconnect	= tmdc_disconnect,
};

static int __init tmdc_init(void)
{
	gameport_register_driver(&tmdc_drv);
	return 0;
}

static void __exit tmdc_exit(void)
{
	gameport_unregister_driver(&tmdc_drv);
}

module_init(tmdc_init);
module_exit(tmdc_exit);
