/*
 * $Id: db9.c,v 1.13 2002/04/07 20:13:37 vojtech Exp $
 *
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Andree Borrmann		Mats Sjövall
 */

/*
 * Atari, Amstrad, Commodore, Amiga, Sega, etc. joystick driver for Linux
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
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/parport.h>
#include <linux/input.h>
#include <linux/mutex.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Atari, Amstrad, Commodore, Amiga, Sega, etc. joystick driver");
MODULE_LICENSE("GPL");

struct db9_config {
	int args[2];
	int nargs;
};

#define DB9_MAX_PORTS		3
static struct db9_config db9[DB9_MAX_PORTS] __initdata;

module_param_array_named(dev, db9[0].args, int, &db9[0].nargs, 0);
MODULE_PARM_DESC(dev, "Describes first attached device (<parport#>,<type>)");
module_param_array_named(dev2, db9[1].args, int, &db9[0].nargs, 0);
MODULE_PARM_DESC(dev2, "Describes second attached device (<parport#>,<type>)");
module_param_array_named(dev3, db9[2].args, int, &db9[2].nargs, 0);
MODULE_PARM_DESC(dev3, "Describes third attached device (<parport#>,<type>)");

#define DB9_ARG_PARPORT		0
#define DB9_ARG_MODE		1

#define DB9_MULTI_STICK		0x01
#define DB9_MULTI2_STICK	0x02
#define DB9_GENESIS_PAD		0x03
#define DB9_GENESIS5_PAD	0x05
#define DB9_GENESIS6_PAD	0x06
#define DB9_SATURN_PAD		0x07
#define DB9_MULTI_0802		0x08
#define DB9_MULTI_0802_2	0x09
#define DB9_CD32_PAD		0x0A
#define DB9_SATURN_DPP		0x0B
#define DB9_SATURN_DPP_2	0x0C
#define DB9_MAX_PAD		0x0D

#define DB9_UP			0x01
#define DB9_DOWN		0x02
#define DB9_LEFT		0x04
#define DB9_RIGHT		0x08
#define DB9_FIRE1		0x10
#define DB9_FIRE2		0x20
#define DB9_FIRE3		0x40
#define DB9_FIRE4		0x80

#define DB9_NORMAL		0x0a
#define DB9_NOSELECT		0x08

#define DB9_GENESIS6_DELAY	14
#define DB9_REFRESH_TIME	HZ/100

#define DB9_MAX_DEVICES		2

struct db9_mode_data {
	const char *name;
	const short *buttons;
	int n_buttons;
	int n_pads;
	int n_axis;
	int bidirectional;
	int reverse;
};

struct db9 {
	struct input_dev *dev[DB9_MAX_DEVICES];
	struct timer_list timer;
	struct pardevice *pd;
	int mode;
	int used;
	struct mutex mutex;
	char phys[DB9_MAX_DEVICES][32];
};

static struct db9 *db9_base[3];

static const short db9_multi_btn[] = { BTN_TRIGGER, BTN_THUMB };
static const short db9_genesis_btn[] = { BTN_START, BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z, BTN_MODE };
static const short db9_cd32_btn[] = { BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z, BTN_TL, BTN_TR, BTN_START };
static const short db9_abs[] = { ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_RZ, ABS_Z, ABS_HAT0X, ABS_HAT0Y, ABS_HAT1X, ABS_HAT1Y };

static const struct db9_mode_data db9_modes[] = {
	{ NULL,					 NULL,		  0,  0,  0,  0,  0 },
	{ "Multisystem joystick",		 db9_multi_btn,	  1,  1,  2,  1,  1 },
	{ "Multisystem joystick (2 fire)",	 db9_multi_btn,	  2,  1,  2,  1,  1 },
	{ "Genesis pad",			 db9_genesis_btn, 4,  1,  2,  1,  1 },
	{ NULL,					 NULL,		  0,  0,  0,  0,  0 },
	{ "Genesis 5 pad",			 db9_genesis_btn, 6,  1,  2,  1,  1 },
	{ "Genesis 6 pad",			 db9_genesis_btn, 8,  1,  2,  1,  1 },
	{ "Saturn pad",				 db9_cd32_btn,	  9,  6,  7,  0,  1 },
	{ "Multisystem (0.8.0.2) joystick",	 db9_multi_btn,	  1,  1,  2,  1,  1 },
	{ "Multisystem (0.8.0.2-dual) joystick", db9_multi_btn,	  1,  2,  2,  1,  1 },
	{ "Amiga CD-32 pad",			 db9_cd32_btn,	  7,  1,  2,  1,  1 },
	{ "Saturn dpp",				 db9_cd32_btn,	  9,  6,  7,  0,  0 },
	{ "Saturn dpp dual",			 db9_cd32_btn,	  9,  12, 7,  0,  0 },
};

/*
 * Saturn controllers
 */
#define DB9_SATURN_DELAY 300
static const int db9_saturn_byte[] = { 1, 1, 1, 2, 2, 2, 2, 2, 1 };
static const unsigned char db9_saturn_mask[] = { 0x04, 0x01, 0x02, 0x40, 0x20, 0x10, 0x08, 0x80, 0x08 };

/*
 * db9_saturn_write_sub() writes 2 bit data.
 */
static void db9_saturn_write_sub(struct parport *port, int type, unsigned char data, int powered, int pwr_sub)
{
	unsigned char c;

	switch (type) {
	case 1: /* DPP1 */
		c = 0x80 | 0x30 | (powered ? 0x08 : 0) | (pwr_sub ? 0x04 : 0) | data;
		parport_write_data(port, c);
		break;
	case 2: /* DPP2 */
		c = 0x40 | data << 4 | (powered ? 0x08 : 0) | (pwr_sub ? 0x04 : 0) | 0x03;
		parport_write_data(port, c);
		break;
	case 0:	/* DB9 */
		c = ((((data & 2) ? 2 : 0) | ((data & 1) ? 4 : 0)) ^ 0x02) | !powered;
		parport_write_control(port, c);
		break;
	}
}

/*
 * gc_saturn_read_sub() reads 4 bit data.
 */
static unsigned char db9_saturn_read_sub(struct parport *port, int type)
{
	unsigned char data;

	if (type) {
		/* DPP */
		data = parport_read_status(port) ^ 0x80;
		return (data & 0x80 ? 1 : 0) | (data & 0x40 ? 2 : 0)
		     | (data & 0x20 ? 4 : 0) | (data & 0x10 ? 8 : 0);
	} else {
		/* DB9 */
		data = parport_read_data(port) & 0x0f;
		return (data & 0x8 ? 1 : 0) | (data & 0x4 ? 2 : 0)
		     | (data & 0x2 ? 4 : 0) | (data & 0x1 ? 8 : 0);
	}
}

/*
 * db9_saturn_read_analog() sends clock and reads 8 bit data.
 */
static unsigned char db9_saturn_read_analog(struct parport *port, int type, int powered)
{
	unsigned char data;

	db9_saturn_write_sub(port, type, 0, powered, 0);
	udelay(DB9_SATURN_DELAY);
	data = db9_saturn_read_sub(port, type) << 4;
	db9_saturn_write_sub(port, type, 2, powered, 0);
	udelay(DB9_SATURN_DELAY);
	data |= db9_saturn_read_sub(port, type);
	return data;
}

/*
 * db9_saturn_read_packet() reads whole saturn packet at connector
 * and returns device identifier code.
 */
static unsigned char db9_saturn_read_packet(struct parport *port, unsigned char *data, int type, int powered)
{
	int i, j;
	unsigned char tmp;

	db9_saturn_write_sub(port, type, 3, powered, 0);
	data[0] = db9_saturn_read_sub(port, type);
	switch (data[0] & 0x0f) {
	case 0xf:
		/* 1111  no pad */
		return data[0] = 0xff;
	case 0x4: case 0x4 | 0x8:
		/* ?100 : digital controller */
		db9_saturn_write_sub(port, type, 0, powered, 1);
		data[2] = db9_saturn_read_sub(port, type) << 4;
		db9_saturn_write_sub(port, type, 2, powered, 1);
		data[1] = db9_saturn_read_sub(port, type) << 4;
		db9_saturn_write_sub(port, type, 1, powered, 1);
		data[1] |= db9_saturn_read_sub(port, type);
		db9_saturn_write_sub(port, type, 3, powered, 1);
		/* data[2] |= db9_saturn_read_sub(port, type); */
		data[2] |= data[0];
		return data[0] = 0x02;
	case 0x1:
		/* 0001 : analog controller or multitap */
		db9_saturn_write_sub(port, type, 2, powered, 0);
		udelay(DB9_SATURN_DELAY);
		data[0] = db9_saturn_read_analog(port, type, powered);
		if (data[0] != 0x41) {
			/* read analog controller */
			for (i = 0; i < (data[0] & 0x0f); i++)
				data[i + 1] = db9_saturn_read_analog(port, type, powered);
			db9_saturn_write_sub(port, type, 3, powered, 0);
			return data[0];
		} else {
			/* read multitap */
			if (db9_saturn_read_analog(port, type, powered) != 0x60)
				return data[0] = 0xff;
			for (i = 0; i < 60; i += 10) {
				data[i] = db9_saturn_read_analog(port, type, powered);
				if (data[i] != 0xff)
					/* read each pad */
					for (j = 0; j < (data[i] & 0x0f); j++)
						data[i + j + 1] = db9_saturn_read_analog(port, type, powered);
			}
			db9_saturn_write_sub(port, type, 3, powered, 0);
			return 0x41;
		}
	case 0x0:
		/* 0000 : mouse */
		db9_saturn_write_sub(port, type, 2, powered, 0);
		udelay(DB9_SATURN_DELAY);
		tmp = db9_saturn_read_analog(port, type, powered);
		if (tmp == 0xff) {
			for (i = 0; i < 3; i++)
				data[i + 1] = db9_saturn_read_analog(port, type, powered);
			db9_saturn_write_sub(port, type, 3, powered, 0);
			return data[0] = 0xe3;
		}
	default:
		return data[0];
	}
}

/*
 * db9_saturn_report() analyzes packet and reports.
 */
static int db9_saturn_report(unsigned char id, unsigned char data[60], struct input_dev *devs[], int n, int max_pads)
{
	struct input_dev *dev;
	int tmp, i, j;

	tmp = (id == 0x41) ? 60 : 10;
	for (j = 0; j < tmp && n < max_pads; j += 10, n++) {
		dev = devs[n];
		switch (data[j]) {
		case 0x16: /* multi controller (analog 4 axis) */
			input_report_abs(dev, db9_abs[5], data[j + 6]);
		case 0x15: /* mission stick (analog 3 axis) */
			input_report_abs(dev, db9_abs[3], data[j + 4]);
			input_report_abs(dev, db9_abs[4], data[j + 5]);
		case 0x13: /* racing controller (analog 1 axis) */
			input_report_abs(dev, db9_abs[2], data[j + 3]);
		case 0x34: /* saturn keyboard (udlr ZXC ASD QE Esc) */
		case 0x02: /* digital pad (digital 2 axis + buttons) */
			input_report_abs(dev, db9_abs[0], !(data[j + 1] & 128) - !(data[j + 1] & 64));
			input_report_abs(dev, db9_abs[1], !(data[j + 1] & 32) - !(data[j + 1] & 16));
			for (i = 0; i < 9; i++)
				input_report_key(dev, db9_cd32_btn[i], ~data[j + db9_saturn_byte[i]] & db9_saturn_mask[i]);
			break;
		case 0x19: /* mission stick x2 (analog 6 axis + buttons) */
			input_report_abs(dev, db9_abs[0], !(data[j + 1] & 128) - !(data[j + 1] & 64));
			input_report_abs(dev, db9_abs[1], !(data[j + 1] & 32) - !(data[j + 1] & 16));
			for (i = 0; i < 9; i++)
				input_report_key(dev, db9_cd32_btn[i], ~data[j + db9_saturn_byte[i]] & db9_saturn_mask[i]);
			input_report_abs(dev, db9_abs[2], data[j + 3]);
			input_report_abs(dev, db9_abs[3], data[j + 4]);
			input_report_abs(dev, db9_abs[4], data[j + 5]);
			/*
			input_report_abs(dev, db9_abs[8], (data[j + 6] & 128 ? 0 : 1) - (data[j + 6] & 64 ? 0 : 1));
			input_report_abs(dev, db9_abs[9], (data[j + 6] & 32 ? 0 : 1) - (data[j + 6] & 16 ? 0 : 1));
			*/
			input_report_abs(dev, db9_abs[6], data[j + 7]);
			input_report_abs(dev, db9_abs[7], data[j + 8]);
			input_report_abs(dev, db9_abs[5], data[j + 9]);
			break;
		case 0xd3: /* sankyo ff (analog 1 axis + stop btn) */
			input_report_key(dev, BTN_A, data[j + 3] & 0x80);
			input_report_abs(dev, db9_abs[2], data[j + 3] & 0x7f);
			break;
		case 0xe3: /* shuttle mouse (analog 2 axis + buttons. signed value) */
			input_report_key(dev, BTN_START, data[j + 1] & 0x08);
			input_report_key(dev, BTN_A, data[j + 1] & 0x04);
			input_report_key(dev, BTN_C, data[j + 1] & 0x02);
			input_report_key(dev, BTN_B, data[j + 1] & 0x01);
			input_report_abs(dev, db9_abs[2], data[j + 2] ^ 0x80);
			input_report_abs(dev, db9_abs[3], (0xff-(data[j + 3] ^ 0x80))+1); /* */
			break;
		case 0xff:
		default: /* no pad */
			input_report_abs(dev, db9_abs[0], 0);
			input_report_abs(dev, db9_abs[1], 0);
			for (i = 0; i < 9; i++)
				input_report_key(dev, db9_cd32_btn[i], 0);
			break;
		}
	}
	return n;
}

static int db9_saturn(int mode, struct parport *port, struct input_dev *devs[])
{
	unsigned char id, data[60];
	int type, n, max_pads;
	int tmp, i;

	switch (mode) {
	case DB9_SATURN_PAD:
		type = 0;
		n = 1;
		break;
	case DB9_SATURN_DPP:
		type = 1;
		n = 1;
		break;
	case DB9_SATURN_DPP_2:
		type = 1;
		n = 2;
		break;
	default:
		return -1;
	}
	max_pads = min(db9_modes[mode].n_pads, DB9_MAX_DEVICES);
	for (tmp = 0, i = 0; i < n; i++) {
		id = db9_saturn_read_packet(port, data, type + i, 1);
		tmp = db9_saturn_report(id, data, devs, tmp, max_pads);
	}
	return 0;
}

static void db9_timer(unsigned long private)
{
	struct db9 *db9 = (void *) private;
	struct parport *port = db9->pd->port;
	struct input_dev *dev = db9->dev[0];
	struct input_dev *dev2 = db9->dev[1];
	int data, i;

	switch (db9->mode) {
		case DB9_MULTI_0802_2:

			data = parport_read_data(port) >> 3;

			input_report_abs(dev2, ABS_X, (data & DB9_RIGHT ? 0 : 1) - (data & DB9_LEFT ? 0 : 1));
			input_report_abs(dev2, ABS_Y, (data & DB9_DOWN  ? 0 : 1) - (data & DB9_UP   ? 0 : 1));
			input_report_key(dev2, BTN_TRIGGER, ~data & DB9_FIRE1);

		case DB9_MULTI_0802:

			data = parport_read_status(port) >> 3;

			input_report_abs(dev, ABS_X, (data & DB9_RIGHT ? 0 : 1) - (data & DB9_LEFT ? 0 : 1));
			input_report_abs(dev, ABS_Y, (data & DB9_DOWN  ? 0 : 1) - (data & DB9_UP   ? 0 : 1));
			input_report_key(dev, BTN_TRIGGER, data & DB9_FIRE1);
			break;

		case DB9_MULTI_STICK:

			data = parport_read_data(port);

			input_report_abs(dev, ABS_X, (data & DB9_RIGHT ? 0 : 1) - (data & DB9_LEFT ? 0 : 1));
			input_report_abs(dev, ABS_Y, (data & DB9_DOWN  ? 0 : 1) - (data & DB9_UP   ? 0 : 1));
			input_report_key(dev, BTN_TRIGGER, ~data & DB9_FIRE1);
			break;

		case DB9_MULTI2_STICK:

			data = parport_read_data(port);

			input_report_abs(dev, ABS_X, (data & DB9_RIGHT ? 0 : 1) - (data & DB9_LEFT ? 0 : 1));
			input_report_abs(dev, ABS_Y, (data & DB9_DOWN  ? 0 : 1) - (data & DB9_UP   ? 0 : 1));
			input_report_key(dev, BTN_TRIGGER, ~data & DB9_FIRE1);
			input_report_key(dev, BTN_THUMB,   ~data & DB9_FIRE2);
			break;

		case DB9_GENESIS_PAD:

			parport_write_control(port, DB9_NOSELECT);
			data = parport_read_data(port);

			input_report_abs(dev, ABS_X, (data & DB9_RIGHT ? 0 : 1) - (data & DB9_LEFT ? 0 : 1));
			input_report_abs(dev, ABS_Y, (data & DB9_DOWN  ? 0 : 1) - (data & DB9_UP   ? 0 : 1));
			input_report_key(dev, BTN_B, ~data & DB9_FIRE1);
			input_report_key(dev, BTN_C, ~data & DB9_FIRE2);

			parport_write_control(port, DB9_NORMAL);
			data = parport_read_data(port);

			input_report_key(dev, BTN_A,     ~data & DB9_FIRE1);
			input_report_key(dev, BTN_START, ~data & DB9_FIRE2);
			break;

		case DB9_GENESIS5_PAD:

			parport_write_control(port, DB9_NOSELECT);
			data = parport_read_data(port);

			input_report_abs(dev, ABS_X, (data & DB9_RIGHT ? 0 : 1) - (data & DB9_LEFT ? 0 : 1));
			input_report_abs(dev, ABS_Y, (data & DB9_DOWN  ? 0 : 1) - (data & DB9_UP   ? 0 : 1));
			input_report_key(dev, BTN_B, ~data & DB9_FIRE1);
			input_report_key(dev, BTN_C, ~data & DB9_FIRE2);

			parport_write_control(port, DB9_NORMAL);
			data = parport_read_data(port);

			input_report_key(dev, BTN_A,     ~data & DB9_FIRE1);
			input_report_key(dev, BTN_X,     ~data & DB9_FIRE2);
			input_report_key(dev, BTN_Y,     ~data & DB9_LEFT);
			input_report_key(dev, BTN_START, ~data & DB9_RIGHT);
			break;

		case DB9_GENESIS6_PAD:

			parport_write_control(port, DB9_NOSELECT); /* 1 */
			udelay(DB9_GENESIS6_DELAY);
			data = parport_read_data(port);

			input_report_abs(dev, ABS_X, (data & DB9_RIGHT ? 0 : 1) - (data & DB9_LEFT ? 0 : 1));
			input_report_abs(dev, ABS_Y, (data & DB9_DOWN  ? 0 : 1) - (data & DB9_UP   ? 0 : 1));
			input_report_key(dev, BTN_B, ~data & DB9_FIRE1);
			input_report_key(dev, BTN_C, ~data & DB9_FIRE2);

			parport_write_control(port, DB9_NORMAL);
			udelay(DB9_GENESIS6_DELAY);
			data = parport_read_data(port);

			input_report_key(dev, BTN_A, ~data & DB9_FIRE1);
			input_report_key(dev, BTN_START, ~data & DB9_FIRE2);

			parport_write_control(port, DB9_NOSELECT); /* 2 */
			udelay(DB9_GENESIS6_DELAY);
			parport_write_control(port, DB9_NORMAL);
			udelay(DB9_GENESIS6_DELAY);
			parport_write_control(port, DB9_NOSELECT); /* 3 */
			udelay(DB9_GENESIS6_DELAY);
			data=parport_read_data(port);

			input_report_key(dev, BTN_X,    ~data & DB9_LEFT);
			input_report_key(dev, BTN_Y,    ~data & DB9_DOWN);
			input_report_key(dev, BTN_Z,    ~data & DB9_UP);
			input_report_key(dev, BTN_MODE, ~data & DB9_RIGHT);

			parport_write_control(port, DB9_NORMAL);
			udelay(DB9_GENESIS6_DELAY);
			parport_write_control(port, DB9_NOSELECT); /* 4 */
			udelay(DB9_GENESIS6_DELAY);
			parport_write_control(port, DB9_NORMAL);
			break;

		case DB9_SATURN_PAD:
		case DB9_SATURN_DPP:
		case DB9_SATURN_DPP_2:

			db9_saturn(db9->mode, port, db9->dev);
			break;

		case DB9_CD32_PAD:

			data = parport_read_data(port);

			input_report_abs(dev, ABS_X, (data & DB9_RIGHT ? 0 : 1) - (data & DB9_LEFT ? 0 : 1));
			input_report_abs(dev, ABS_Y, (data & DB9_DOWN  ? 0 : 1) - (data & DB9_UP   ? 0 : 1));

			parport_write_control(port, 0x0a);

			for (i = 0; i < 7; i++) {
				data = parport_read_data(port);
				parport_write_control(port, 0x02);
				parport_write_control(port, 0x0a);
				input_report_key(dev, db9_cd32_btn[i], ~data & DB9_FIRE2);
			}

			parport_write_control(port, 0x00);
			break;
		}

	input_sync(dev);

	mod_timer(&db9->timer, jiffies + DB9_REFRESH_TIME);
}

static int db9_open(struct input_dev *dev)
{
	struct db9 *db9 = input_get_drvdata(dev);
	struct parport *port = db9->pd->port;
	int err;

	err = mutex_lock_interruptible(&db9->mutex);
	if (err)
		return err;

	if (!db9->used++) {
		parport_claim(db9->pd);
		parport_write_data(port, 0xff);
		if (db9_modes[db9->mode].reverse) {
			parport_data_reverse(port);
			parport_write_control(port, DB9_NORMAL);
		}
		mod_timer(&db9->timer, jiffies + DB9_REFRESH_TIME);
	}

	mutex_unlock(&db9->mutex);
	return 0;
}

static void db9_close(struct input_dev *dev)
{
	struct db9 *db9 = input_get_drvdata(dev);
	struct parport *port = db9->pd->port;

	mutex_lock(&db9->mutex);
	if (!--db9->used) {
		del_timer_sync(&db9->timer);
		parport_write_control(port, 0x00);
		parport_data_forward(port);
		parport_release(db9->pd);
	}
	mutex_unlock(&db9->mutex);
}

static struct db9 __init *db9_probe(int parport, int mode)
{
	struct db9 *db9;
	const struct db9_mode_data *db9_mode;
	struct parport *pp;
	struct pardevice *pd;
	struct input_dev *input_dev;
	int i, j;
	int err;

	if (mode < 1 || mode >= DB9_MAX_PAD || !db9_modes[mode].n_buttons) {
		printk(KERN_ERR "db9.c: Bad device type %d\n", mode);
		err = -EINVAL;
		goto err_out;
	}

	db9_mode = &db9_modes[mode];

	pp = parport_find_number(parport);
	if (!pp) {
		printk(KERN_ERR "db9.c: no such parport\n");
		err = -ENODEV;
		goto err_out;
	}

	if (db9_mode->bidirectional && !(pp->modes & PARPORT_MODE_TRISTATE)) {
		printk(KERN_ERR "db9.c: specified parport is not bidirectional\n");
		err = -EINVAL;
		goto err_put_pp;
	}

	pd = parport_register_device(pp, "db9", NULL, NULL, NULL, PARPORT_DEV_EXCL, NULL);
	if (!pd) {
		printk(KERN_ERR "db9.c: parport busy already - lp.o loaded?\n");
		err = -EBUSY;
		goto err_put_pp;
	}

	db9 = kzalloc(sizeof(struct db9), GFP_KERNEL);
	if (!db9) {
		printk(KERN_ERR "db9.c: Not enough memory\n");
		err = -ENOMEM;
		goto err_unreg_pardev;
	}

	mutex_init(&db9->mutex);
	db9->pd = pd;
	db9->mode = mode;
	init_timer(&db9->timer);
	db9->timer.data = (long) db9;
	db9->timer.function = db9_timer;

	for (i = 0; i < (min(db9_mode->n_pads, DB9_MAX_DEVICES)); i++) {

		db9->dev[i] = input_dev = input_allocate_device();
		if (!input_dev) {
			printk(KERN_ERR "db9.c: Not enough memory for input device\n");
			err = -ENOMEM;
			goto err_unreg_devs;
		}

		snprintf(db9->phys[i], sizeof(db9->phys[i]),
			 "%s/input%d", db9->pd->port->name, i);

		input_dev->name = db9_mode->name;
		input_dev->phys = db9->phys[i];
		input_dev->id.bustype = BUS_PARPORT;
		input_dev->id.vendor = 0x0002;
		input_dev->id.product = mode;
		input_dev->id.version = 0x0100;

		input_set_drvdata(input_dev, db9);

		input_dev->open = db9_open;
		input_dev->close = db9_close;

		input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
		for (j = 0; j < db9_mode->n_buttons; j++)
			set_bit(db9_mode->buttons[j], input_dev->keybit);
		for (j = 0; j < db9_mode->n_axis; j++) {
			if (j < 2)
				input_set_abs_params(input_dev, db9_abs[j], -1, 1, 0, 0);
			else
				input_set_abs_params(input_dev, db9_abs[j], 1, 255, 0, 0);
		}

		err = input_register_device(input_dev);
		if (err)
			goto err_free_dev;
	}

	parport_put_port(pp);
	return db9;

 err_free_dev:
	input_free_device(db9->dev[i]);
 err_unreg_devs:
	while (--i >= 0)
		input_unregister_device(db9->dev[i]);
	kfree(db9);
 err_unreg_pardev:
	parport_unregister_device(pd);
 err_put_pp:
	parport_put_port(pp);
 err_out:
	return ERR_PTR(err);
}

static void db9_remove(struct db9 *db9)
{
	int i;

	for (i = 0; i < min(db9_modes[db9->mode].n_pads, DB9_MAX_DEVICES); i++)
		input_unregister_device(db9->dev[i]);
	parport_unregister_device(db9->pd);
	kfree(db9);
}

static int __init db9_init(void)
{
	int i;
	int have_dev = 0;
	int err = 0;

	for (i = 0; i < DB9_MAX_PORTS; i++) {
		if (db9[i].nargs == 0 || db9[i].args[DB9_ARG_PARPORT] < 0)
			continue;

		if (db9[i].nargs < 2) {
			printk(KERN_ERR "db9.c: Device type must be specified.\n");
			err = -EINVAL;
			break;
		}

		db9_base[i] = db9_probe(db9[i].args[DB9_ARG_PARPORT],
					db9[i].args[DB9_ARG_MODE]);
		if (IS_ERR(db9_base[i])) {
			err = PTR_ERR(db9_base[i]);
			break;
		}

		have_dev = 1;
	}

	if (err) {
		while (--i >= 0)
			if (db9_base[i])
				db9_remove(db9_base[i]);
		return err;
	}

	return have_dev ? 0 : -ENODEV;
}

static void __exit db9_exit(void)
{
	int i;

	for (i = 0; i < DB9_MAX_PORTS; i++)
		if (db9_base[i])
			db9_remove(db9_base[i]);
}

module_init(db9_init);
module_exit(db9_exit);
