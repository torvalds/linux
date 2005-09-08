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

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Atari, Amstrad, Commodore, Amiga, Sega, etc. joystick driver");
MODULE_LICENSE("GPL");

static int db9[] __initdata = { -1, 0 };
static int db9_nargs __initdata = 0;
module_param_array_named(dev, db9, int, &db9_nargs, 0);
MODULE_PARM_DESC(dev, "Describes first attached device (<parport#>,<type>)");

static int db9_2[] __initdata = { -1, 0 };
static int db9_nargs_2 __initdata = 0;
module_param_array_named(dev2, db9_2, int, &db9_nargs_2, 0);
MODULE_PARM_DESC(dev2, "Describes second attached device (<parport#>,<type>)");

static int db9_3[] __initdata = { -1, 0 };
static int db9_nargs_3 __initdata = 0;
module_param_array_named(dev3, db9_3, int, &db9_nargs_3, 0);
MODULE_PARM_DESC(dev3, "Describes third attached device (<parport#>,<type>)");

__obsolete_setup("db9=");
__obsolete_setup("db9_2=");
__obsolete_setup("db9_3=");

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

#define DB9_MAX_DEVICES		2

#define DB9_GENESIS6_DELAY	14
#define DB9_REFRESH_TIME	HZ/100

struct db9 {
	struct input_dev dev[DB9_MAX_DEVICES];
	struct timer_list timer;
	struct pardevice *pd;
	int mode;
	int used;
	struct semaphore sem;
	char phys[2][32];
};

static struct db9 *db9_base[3];

static short db9_multi_btn[] = { BTN_TRIGGER, BTN_THUMB };
static short db9_genesis_btn[] = { BTN_START, BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z, BTN_MODE };
static short db9_cd32_btn[] = { BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z, BTN_TL, BTN_TR, BTN_START };

static char db9_buttons[DB9_MAX_PAD] = { 0, 1, 2, 4, 0, 6, 8, 9, 1, 1, 7, 9, 9 };
static short *db9_btn[DB9_MAX_PAD] = { NULL, db9_multi_btn, db9_multi_btn, db9_genesis_btn, NULL, db9_genesis_btn,
					db9_genesis_btn, db9_cd32_btn, db9_multi_btn, db9_multi_btn, db9_cd32_btn,
					db9_cd32_btn, db9_cd32_btn };
static char *db9_name[DB9_MAX_PAD] = { NULL, "Multisystem joystick", "Multisystem joystick (2 fire)", "Genesis pad",
				      NULL, "Genesis 5 pad", "Genesis 6 pad", "Saturn pad", "Multisystem (0.8.0.2) joystick",
				     "Multisystem (0.8.0.2-dual) joystick", "Amiga CD-32 pad", "Saturn dpp", "Saturn dpp dual" };

static const int db9_max_pads[DB9_MAX_PAD] = { 0, 1, 1, 1, 0, 1, 1, 6, 1, 2, 1, 6, 12 };
static const int db9_num_axis[DB9_MAX_PAD] = { 0, 2, 2, 2, 0, 2, 2, 7, 2, 2, 2 ,7, 7 };
static const short db9_abs[] = { ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_RZ, ABS_Z, ABS_HAT0X, ABS_HAT0Y, ABS_HAT1X, ABS_HAT1Y };
static const int db9_bidirectional[DB9_MAX_PAD] = { 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0, 0 };
static const int db9_reverse[DB9_MAX_PAD] = { 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0 };

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
static int db9_saturn_report(unsigned char id, unsigned char data[60], struct input_dev *dev, int n, int max_pads)
{
	int tmp, i, j;

	tmp = (id == 0x41) ? 60 : 10;
	for (j = 0; (j < tmp) && (n < max_pads); j += 10, n++) {
		switch (data[j]) {
		case 0x16: /* multi controller (analog 4 axis) */
			input_report_abs(dev + n, db9_abs[5], data[j + 6]);
		case 0x15: /* mission stick (analog 3 axis) */
			input_report_abs(dev + n, db9_abs[3], data[j + 4]);
			input_report_abs(dev + n, db9_abs[4], data[j + 5]);
		case 0x13: /* racing controller (analog 1 axis) */
			input_report_abs(dev + n, db9_abs[2], data[j + 3]);
		case 0x34: /* saturn keyboard (udlr ZXC ASD QE Esc) */
		case 0x02: /* digital pad (digital 2 axis + buttons) */
			input_report_abs(dev + n, db9_abs[0], !(data[j + 1] & 128) - !(data[j + 1] & 64));
			input_report_abs(dev + n, db9_abs[1], !(data[j + 1] & 32) - !(data[j + 1] & 16));
			for (i = 0; i < 9; i++)
				input_report_key(dev + n, db9_cd32_btn[i], ~data[j + db9_saturn_byte[i]] & db9_saturn_mask[i]);
			break;
		case 0x19: /* mission stick x2 (analog 6 axis + buttons) */
			input_report_abs(dev + n, db9_abs[0], !(data[j + 1] & 128) - !(data[j + 1] & 64));
			input_report_abs(dev + n, db9_abs[1], !(data[j + 1] & 32) - !(data[j + 1] & 16));
			for (i = 0; i < 9; i++)
				input_report_key(dev + n, db9_cd32_btn[i], ~data[j + db9_saturn_byte[i]] & db9_saturn_mask[i]);
			input_report_abs(dev + n, db9_abs[2], data[j + 3]);
			input_report_abs(dev + n, db9_abs[3], data[j + 4]);
			input_report_abs(dev + n, db9_abs[4], data[j + 5]);
			/*
			input_report_abs(dev + n, db9_abs[8], (data[j + 6] & 128 ? 0 : 1) - (data[j + 6] & 64 ? 0 : 1));
			input_report_abs(dev + n, db9_abs[9], (data[j + 6] & 32 ? 0 : 1) - (data[j + 6] & 16 ? 0 : 1));
			*/
			input_report_abs(dev + n, db9_abs[6], data[j + 7]);
			input_report_abs(dev + n, db9_abs[7], data[j + 8]);
			input_report_abs(dev + n, db9_abs[5], data[j + 9]);
			break;
		case 0xd3: /* sankyo ff (analog 1 axis + stop btn) */
			input_report_key(dev + n, BTN_A, data[j + 3] & 0x80);
			input_report_abs(dev + n, db9_abs[2], data[j + 3] & 0x7f);
			break;
		case 0xe3: /* shuttle mouse (analog 2 axis + buttons. signed value) */
			input_report_key(dev + n, BTN_START, data[j + 1] & 0x08);
			input_report_key(dev + n, BTN_A, data[j + 1] & 0x04);
			input_report_key(dev + n, BTN_C, data[j + 1] & 0x02);
			input_report_key(dev + n, BTN_B, data[j + 1] & 0x01);
			input_report_abs(dev + n, db9_abs[2], data[j + 2] ^ 0x80);
			input_report_abs(dev + n, db9_abs[3], (0xff-(data[j + 3] ^ 0x80))+1); /* */
			break;
		case 0xff:
		default: /* no pad */
			input_report_abs(dev + n, db9_abs[0], 0);
			input_report_abs(dev + n, db9_abs[1], 0);
			for (i = 0; i < 9; i++)
				input_report_key(dev + n, db9_cd32_btn[i], 0);
			break;
		}
	}
	return n;
}

static int db9_saturn(int mode, struct parport *port, struct input_dev *dev)
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
	max_pads = min(db9_max_pads[mode], DB9_MAX_DEVICES);
	for (tmp = 0, i = 0; i < n; i++) {
		id = db9_saturn_read_packet(port, data, type + i, 1);
		tmp = db9_saturn_report(id, data, dev, tmp, max_pads);
	}
	return 0;
}

static void db9_timer(unsigned long private)
{
	struct db9 *db9 = (void *) private;
	struct parport *port = db9->pd->port;
	struct input_dev *dev = db9->dev;
	int data, i;

	switch(db9->mode) {
		case DB9_MULTI_0802_2:

			data = parport_read_data(port) >> 3;

			input_report_abs(dev + 1, ABS_X, (data & DB9_RIGHT ? 0 : 1) - (data & DB9_LEFT ? 0 : 1));
			input_report_abs(dev + 1, ABS_Y, (data & DB9_DOWN  ? 0 : 1) - (data & DB9_UP   ? 0 : 1));
			input_report_key(dev + 1, BTN_TRIGGER, ~data & DB9_FIRE1);

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
			data=parport_read_data(port);

			input_report_key(dev, BTN_A,     ~data & DB9_FIRE1);
			input_report_key(dev, BTN_START, ~data & DB9_FIRE2);
			break;

		case DB9_GENESIS5_PAD:

			parport_write_control(port, DB9_NOSELECT);
			data=parport_read_data(port);

			input_report_abs(dev, ABS_X, (data & DB9_RIGHT ? 0 : 1) - (data & DB9_LEFT ? 0 : 1));
			input_report_abs(dev, ABS_Y, (data & DB9_DOWN  ? 0 : 1) - (data & DB9_UP   ? 0 : 1));
			input_report_key(dev, BTN_B, ~data & DB9_FIRE1);
			input_report_key(dev, BTN_C, ~data & DB9_FIRE2);

			parport_write_control(port, DB9_NORMAL);
			data=parport_read_data(port);

			input_report_key(dev, BTN_A,     ~data & DB9_FIRE1);
			input_report_key(dev, BTN_X,     ~data & DB9_FIRE2);
			input_report_key(dev, BTN_Y,     ~data & DB9_LEFT);
			input_report_key(dev, BTN_START, ~data & DB9_RIGHT);
			break;

		case DB9_GENESIS6_PAD:

			parport_write_control(port, DB9_NOSELECT); /* 1 */
			udelay(DB9_GENESIS6_DELAY);
			data=parport_read_data(port);

			input_report_abs(dev, ABS_X, (data & DB9_RIGHT ? 0 : 1) - (data & DB9_LEFT ? 0 : 1));
			input_report_abs(dev, ABS_Y, (data & DB9_DOWN  ? 0 : 1) - (data & DB9_UP   ? 0 : 1));
			input_report_key(dev, BTN_B, ~data & DB9_FIRE1);
			input_report_key(dev, BTN_C, ~data & DB9_FIRE2);

			parport_write_control(port, DB9_NORMAL);
			udelay(DB9_GENESIS6_DELAY);
			data=parport_read_data(port);

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

			db9_saturn(db9->mode, port, dev);
			break;

		case DB9_CD32_PAD:

			data=parport_read_data(port);

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
	struct db9 *db9 = dev->private;
	struct parport *port = db9->pd->port;
	int err;

	err = down_interruptible(&db9->sem);
	if (err)
		return err;

	if (!db9->used++) {
		parport_claim(db9->pd);
		parport_write_data(port, 0xff);
		if (db9_reverse[db9->mode]) {
			parport_data_reverse(port);
			parport_write_control(port, DB9_NORMAL);
		}
		mod_timer(&db9->timer, jiffies + DB9_REFRESH_TIME);
	}

	up(&db9->sem);
	return 0;
}

static void db9_close(struct input_dev *dev)
{
	struct db9 *db9 = dev->private;
	struct parport *port = db9->pd->port;

	down(&db9->sem);
	if (!--db9->used) {
		del_timer_sync(&db9->timer);
		parport_write_control(port, 0x00);
		parport_data_forward(port);
		parport_release(db9->pd);
	}
	up(&db9->sem);
}

static struct db9 __init *db9_probe(int *config, int nargs)
{
	struct db9 *db9;
	struct parport *pp;
	int i, j;

	if (config[0] < 0)
		return NULL;

	if (nargs < 2) {
		printk(KERN_ERR "db9.c: Device type must be specified.\n");
		return NULL;
	}

	if (config[1] < 1 || config[1] >= DB9_MAX_PAD || !db9_buttons[config[1]]) {
		printk(KERN_ERR "db9.c: bad config\n");
		return NULL;
	}

	pp = parport_find_number(config[0]);
	if (!pp) {
		printk(KERN_ERR "db9.c: no such parport\n");
		return NULL;
	}

	if (db9_bidirectional[config[1]]) {
		if (!(pp->modes & PARPORT_MODE_TRISTATE)) {
			printk(KERN_ERR "db9.c: specified parport is not bidirectional\n");
			parport_put_port(pp);
			return NULL;
		}
	}

	if (!(db9 = kzalloc(sizeof(struct db9), GFP_KERNEL))) {
		parport_put_port(pp);
		return NULL;
	}

	init_MUTEX(&db9->sem);
	db9->mode = config[1];
	init_timer(&db9->timer);
	db9->timer.data = (long) db9;
	db9->timer.function = db9_timer;

	db9->pd = parport_register_device(pp, "db9", NULL, NULL, NULL, PARPORT_DEV_EXCL, NULL);
	parport_put_port(pp);

	if (!db9->pd) {
		printk(KERN_ERR "db9.c: parport busy already - lp.o loaded?\n");
		kfree(db9);
		return NULL;
	}

	for (i = 0; i < (min(db9_max_pads[db9->mode], DB9_MAX_DEVICES)); i++) {

		sprintf(db9->phys[i], "%s/input%d", db9->pd->port->name, i);

		db9->dev[i].private = db9;
		db9->dev[i].open = db9_open;
		db9->dev[i].close = db9_close;

		db9->dev[i].name = db9_name[db9->mode];
		db9->dev[i].phys = db9->phys[i];
		db9->dev[i].id.bustype = BUS_PARPORT;
		db9->dev[i].id.vendor = 0x0002;
		db9->dev[i].id.product = config[1];
		db9->dev[i].id.version = 0x0100;

		db9->dev[i].evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
		for (j = 0; j < db9_buttons[db9->mode]; j++)
			set_bit(db9_btn[db9->mode][j], db9->dev[i].keybit);
		for (j = 0; j < db9_num_axis[db9->mode]; j++) {
			set_bit(db9_abs[j], db9->dev[i].absbit);
			if (j < 2) {
				db9->dev[i].absmin[db9_abs[j]] = -1;
				db9->dev[i].absmax[db9_abs[j]] = 1;
			} else {
				db9->dev[i].absmin[db9_abs[j]] = 1;
				db9->dev[i].absmax[db9_abs[j]] = 255;
				db9->dev[i].absflat[db9_abs[j]] = 0;
			}
		}
		input_register_device(db9->dev + i);
		printk(KERN_INFO "input: %s on %s\n", db9->dev[i].name, db9->pd->port->name);
	}

	return db9;
}

static int __init db9_init(void)
{
	db9_base[0] = db9_probe(db9, db9_nargs);
	db9_base[1] = db9_probe(db9_2, db9_nargs_2);
	db9_base[2] = db9_probe(db9_3, db9_nargs_3);

	if (db9_base[0] || db9_base[1] || db9_base[2])
		return 0;

	return -ENODEV;
}

static void __exit db9_exit(void)
{
	int i, j;

	for (i = 0; i < 3; i++)
		if (db9_base[i]) {
			for (j = 0; j < min(db9_max_pads[db9_base[i]->mode], DB9_MAX_DEVICES); j++)
				input_unregister_device(db9_base[i]->dev + j);
		parport_unregister_device(db9_base[i]->pd);
	}
}

module_init(db9_init);
module_exit(db9_exit);
