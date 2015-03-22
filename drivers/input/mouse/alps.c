/*
 * ALPS touchpad PS/2 mouse driver
 *
 * Copyright (c) 2003 Neil Brown <neilb@cse.unsw.edu.au>
 * Copyright (c) 2003-2005 Peter Osterlund <petero2@telia.com>
 * Copyright (c) 2004 Dmitry Torokhov <dtor@mail.ru>
 * Copyright (c) 2005 Vojtech Pavlik <vojtech@suse.cz>
 * Copyright (c) 2009 Sebastian Kapfer <sebastian_kapfer@gmx.net>
 *
 * ALPS detection, tap switching and status querying info is taken from
 * tpconfig utility (by C. Scott Ananian and Bruce Kall).
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/serio.h>
#include <linux/libps2.h>

#include "psmouse.h"
#include "alps.h"

/*
 * Definitions for ALPS version 3 and 4 command mode protocol
 */
#define ALPS_CMD_NIBBLE_10	0x01f2

#define ALPS_REG_BASE_RUSHMORE	0xc2c0
#define ALPS_REG_BASE_PINNACLE	0x0000

static const struct alps_nibble_commands alps_v3_nibble_commands[] = {
	{ PSMOUSE_CMD_SETPOLL,		0x00 }, /* 0 */
	{ PSMOUSE_CMD_RESET_DIS,	0x00 }, /* 1 */
	{ PSMOUSE_CMD_SETSCALE21,	0x00 }, /* 2 */
	{ PSMOUSE_CMD_SETRATE,		0x0a }, /* 3 */
	{ PSMOUSE_CMD_SETRATE,		0x14 }, /* 4 */
	{ PSMOUSE_CMD_SETRATE,		0x28 }, /* 5 */
	{ PSMOUSE_CMD_SETRATE,		0x3c }, /* 6 */
	{ PSMOUSE_CMD_SETRATE,		0x50 }, /* 7 */
	{ PSMOUSE_CMD_SETRATE,		0x64 }, /* 8 */
	{ PSMOUSE_CMD_SETRATE,		0xc8 }, /* 9 */
	{ ALPS_CMD_NIBBLE_10,		0x00 }, /* a */
	{ PSMOUSE_CMD_SETRES,		0x00 }, /* b */
	{ PSMOUSE_CMD_SETRES,		0x01 }, /* c */
	{ PSMOUSE_CMD_SETRES,		0x02 }, /* d */
	{ PSMOUSE_CMD_SETRES,		0x03 }, /* e */
	{ PSMOUSE_CMD_SETSCALE11,	0x00 }, /* f */
};

static const struct alps_nibble_commands alps_v4_nibble_commands[] = {
	{ PSMOUSE_CMD_ENABLE,		0x00 }, /* 0 */
	{ PSMOUSE_CMD_RESET_DIS,	0x00 }, /* 1 */
	{ PSMOUSE_CMD_SETSCALE21,	0x00 }, /* 2 */
	{ PSMOUSE_CMD_SETRATE,		0x0a }, /* 3 */
	{ PSMOUSE_CMD_SETRATE,		0x14 }, /* 4 */
	{ PSMOUSE_CMD_SETRATE,		0x28 }, /* 5 */
	{ PSMOUSE_CMD_SETRATE,		0x3c }, /* 6 */
	{ PSMOUSE_CMD_SETRATE,		0x50 }, /* 7 */
	{ PSMOUSE_CMD_SETRATE,		0x64 }, /* 8 */
	{ PSMOUSE_CMD_SETRATE,		0xc8 }, /* 9 */
	{ ALPS_CMD_NIBBLE_10,		0x00 }, /* a */
	{ PSMOUSE_CMD_SETRES,		0x00 }, /* b */
	{ PSMOUSE_CMD_SETRES,		0x01 }, /* c */
	{ PSMOUSE_CMD_SETRES,		0x02 }, /* d */
	{ PSMOUSE_CMD_SETRES,		0x03 }, /* e */
	{ PSMOUSE_CMD_SETSCALE11,	0x00 }, /* f */
};

static const struct alps_nibble_commands alps_v6_nibble_commands[] = {
	{ PSMOUSE_CMD_ENABLE,		0x00 }, /* 0 */
	{ PSMOUSE_CMD_SETRATE,		0x0a }, /* 1 */
	{ PSMOUSE_CMD_SETRATE,		0x14 }, /* 2 */
	{ PSMOUSE_CMD_SETRATE,		0x28 }, /* 3 */
	{ PSMOUSE_CMD_SETRATE,		0x3c }, /* 4 */
	{ PSMOUSE_CMD_SETRATE,		0x50 }, /* 5 */
	{ PSMOUSE_CMD_SETRATE,		0x64 }, /* 6 */
	{ PSMOUSE_CMD_SETRATE,		0xc8 }, /* 7 */
	{ PSMOUSE_CMD_GETID,		0x00 }, /* 8 */
	{ PSMOUSE_CMD_GETINFO,		0x00 }, /* 9 */
	{ PSMOUSE_CMD_SETRES,		0x00 }, /* a */
	{ PSMOUSE_CMD_SETRES,		0x01 }, /* b */
	{ PSMOUSE_CMD_SETRES,		0x02 }, /* c */
	{ PSMOUSE_CMD_SETRES,		0x03 }, /* d */
	{ PSMOUSE_CMD_SETSCALE21,	0x00 }, /* e */
	{ PSMOUSE_CMD_SETSCALE11,	0x00 }, /* f */
};


#define ALPS_DUALPOINT		0x02	/* touchpad has trackstick */
#define ALPS_PASS		0x04	/* device has a pass-through port */

#define ALPS_WHEEL		0x08	/* hardware wheel present */
#define ALPS_FW_BK_1		0x10	/* front & back buttons present */
#define ALPS_FW_BK_2		0x20	/* front & back buttons present */
#define ALPS_FOUR_BUTTONS	0x40	/* 4 direction button present */
#define ALPS_PS2_INTERLEAVED	0x80	/* 3-byte PS/2 packet interleaved with
					   6-byte ALPS packet */
#define ALPS_BUTTONPAD		0x200	/* device is a clickpad */

static const struct alps_model_info alps_model_data[] = {
	{ { 0x32, 0x02, 0x14 }, 0x00, { ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_PASS | ALPS_DUALPOINT } },	/* Toshiba Salellite Pro M10 */
	{ { 0x33, 0x02, 0x0a }, 0x00, { ALPS_PROTO_V1, 0x88, 0xf8, 0 } },				/* UMAX-530T */
	{ { 0x53, 0x02, 0x0a }, 0x00, { ALPS_PROTO_V2, 0xf8, 0xf8, 0 } },
	{ { 0x53, 0x02, 0x14 }, 0x00, { ALPS_PROTO_V2, 0xf8, 0xf8, 0 } },
	{ { 0x60, 0x03, 0xc8 }, 0x00, { ALPS_PROTO_V2, 0xf8, 0xf8, 0 } },				/* HP ze1115 */
	{ { 0x63, 0x02, 0x0a }, 0x00, { ALPS_PROTO_V2, 0xf8, 0xf8, 0 } },
	{ { 0x63, 0x02, 0x14 }, 0x00, { ALPS_PROTO_V2, 0xf8, 0xf8, 0 } },
	{ { 0x63, 0x02, 0x28 }, 0x00, { ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_FW_BK_2 } },		/* Fujitsu Siemens S6010 */
	{ { 0x63, 0x02, 0x3c }, 0x00, { ALPS_PROTO_V2, 0x8f, 0x8f, ALPS_WHEEL } },		/* Toshiba Satellite S2400-103 */
	{ { 0x63, 0x02, 0x50 }, 0x00, { ALPS_PROTO_V2, 0xef, 0xef, ALPS_FW_BK_1 } },		/* NEC Versa L320 */
	{ { 0x63, 0x02, 0x64 }, 0x00, { ALPS_PROTO_V2, 0xf8, 0xf8, 0 } },
	{ { 0x63, 0x03, 0xc8 }, 0x00, { ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_PASS | ALPS_DUALPOINT } },	/* Dell Latitude D800 */
	{ { 0x73, 0x00, 0x0a }, 0x00, { ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_DUALPOINT } },		/* ThinkPad R61 8918-5QG */
	{ { 0x73, 0x02, 0x0a }, 0x00, { ALPS_PROTO_V2, 0xf8, 0xf8, 0 } },
	{ { 0x73, 0x02, 0x14 }, 0x00, { ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_FW_BK_2 } },		/* Ahtec Laptop */

	/*
	 * XXX This entry is suspicious. First byte has zero lower nibble,
	 * which is what a normal mouse would report. Also, the value 0x0e
	 * isn't valid per PS/2 spec.
	 */
	{ { 0x20, 0x02, 0x0e }, 0x00, { ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_PASS | ALPS_DUALPOINT } },

	{ { 0x22, 0x02, 0x0a }, 0x00, { ALPS_PROTO_V2, 0xf8, 0xf8, ALPS_PASS | ALPS_DUALPOINT } },
	{ { 0x22, 0x02, 0x14 }, 0x00, { ALPS_PROTO_V2, 0xff, 0xff, ALPS_PASS | ALPS_DUALPOINT } },	/* Dell Latitude D600 */
	/* Dell Latitude E5500, E6400, E6500, Precision M4400 */
	{ { 0x62, 0x02, 0x14 }, 0x00, { ALPS_PROTO_V2, 0xcf, 0xcf,
		ALPS_PASS | ALPS_DUALPOINT | ALPS_PS2_INTERLEAVED } },
	{ { 0x73, 0x00, 0x14 }, 0x00, { ALPS_PROTO_V6, 0xff, 0xff, ALPS_DUALPOINT } },		/* Dell XT2 */
	{ { 0x73, 0x02, 0x50 }, 0x00, { ALPS_PROTO_V2, 0xcf, 0xcf, ALPS_FOUR_BUTTONS } },	/* Dell Vostro 1400 */
	{ { 0x52, 0x01, 0x14 }, 0x00, { ALPS_PROTO_V2, 0xff, 0xff,
		ALPS_PASS | ALPS_DUALPOINT | ALPS_PS2_INTERLEAVED } },				/* Toshiba Tecra A11-11L */
	{ { 0x73, 0x02, 0x64 }, 0x8a, { ALPS_PROTO_V4, 0x8f, 0x8f, 0 } },
};

static const struct alps_protocol_info alps_v3_protocol_data = {
	ALPS_PROTO_V3, 0x8f, 0x8f, ALPS_DUALPOINT
};

static const struct alps_protocol_info alps_v3_rushmore_data = {
	ALPS_PROTO_V3_RUSHMORE, 0x8f, 0x8f, ALPS_DUALPOINT
};

static const struct alps_protocol_info alps_v5_protocol_data = {
	ALPS_PROTO_V5, 0xc8, 0xd8, 0
};

static const struct alps_protocol_info alps_v7_protocol_data = {
	ALPS_PROTO_V7, 0x48, 0x48, ALPS_DUALPOINT
};

static void alps_set_abs_params_st(struct alps_data *priv,
				   struct input_dev *dev1);
static void alps_set_abs_params_mt(struct alps_data *priv,
				   struct input_dev *dev1);

/* Packet formats are described in Documentation/input/alps.txt */

static bool alps_is_valid_first_byte(struct alps_data *priv,
				     unsigned char data)
{
	return (data & priv->mask0) == priv->byte0;
}

static void alps_report_buttons(struct input_dev *dev1, struct input_dev *dev2,
				int left, int right, int middle)
{
	struct input_dev *dev;

	/*
	 * If shared button has already been reported on the
	 * other device (dev2) then this event should be also
	 * sent through that device.
	 */
	dev = (dev2 && test_bit(BTN_LEFT, dev2->key)) ? dev2 : dev1;
	input_report_key(dev, BTN_LEFT, left);

	dev = (dev2 && test_bit(BTN_RIGHT, dev2->key)) ? dev2 : dev1;
	input_report_key(dev, BTN_RIGHT, right);

	dev = (dev2 && test_bit(BTN_MIDDLE, dev2->key)) ? dev2 : dev1;
	input_report_key(dev, BTN_MIDDLE, middle);

	/*
	 * Sync the _other_ device now, we'll do the first
	 * device later once we report the rest of the events.
	 */
	if (dev2)
		input_sync(dev2);
}

static void alps_process_packet_v1_v2(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	unsigned char *packet = psmouse->packet;
	struct input_dev *dev = psmouse->dev;
	struct input_dev *dev2 = priv->dev2;
	int x, y, z, ges, fin, left, right, middle;
	int back = 0, forward = 0;

	if (priv->proto_version == ALPS_PROTO_V1) {
		left = packet[2] & 0x10;
		right = packet[2] & 0x08;
		middle = 0;
		x = packet[1] | ((packet[0] & 0x07) << 7);
		y = packet[4] | ((packet[3] & 0x07) << 7);
		z = packet[5];
	} else {
		left = packet[3] & 1;
		right = packet[3] & 2;
		middle = packet[3] & 4;
		x = packet[1] | ((packet[2] & 0x78) << (7 - 3));
		y = packet[4] | ((packet[3] & 0x70) << (7 - 4));
		z = packet[5];
	}

	if (priv->flags & ALPS_FW_BK_1) {
		back = packet[0] & 0x10;
		forward = packet[2] & 4;
	}

	if (priv->flags & ALPS_FW_BK_2) {
		back = packet[3] & 4;
		forward = packet[2] & 4;
		if ((middle = forward && back))
			forward = back = 0;
	}

	ges = packet[2] & 1;
	fin = packet[2] & 2;

	if ((priv->flags & ALPS_DUALPOINT) && z == 127) {
		input_report_rel(dev2, REL_X,  (x > 383 ? (x - 768) : x));
		input_report_rel(dev2, REL_Y, -(y > 255 ? (y - 512) : y));

		alps_report_buttons(dev2, dev, left, right, middle);

		input_sync(dev2);
		return;
	}

	alps_report_buttons(dev, dev2, left, right, middle);

	/* Convert hardware tap to a reasonable Z value */
	if (ges && !fin)
		z = 40;

	/*
	 * A "tap and drag" operation is reported by the hardware as a transition
	 * from (!fin && ges) to (fin && ges). This should be translated to the
	 * sequence Z>0, Z==0, Z>0, so the Z==0 event has to be generated manually.
	 */
	if (ges && fin && !priv->prev_fin) {
		input_report_abs(dev, ABS_X, x);
		input_report_abs(dev, ABS_Y, y);
		input_report_abs(dev, ABS_PRESSURE, 0);
		input_report_key(dev, BTN_TOOL_FINGER, 0);
		input_sync(dev);
	}
	priv->prev_fin = fin;

	if (z > 30)
		input_report_key(dev, BTN_TOUCH, 1);
	if (z < 25)
		input_report_key(dev, BTN_TOUCH, 0);

	if (z > 0) {
		input_report_abs(dev, ABS_X, x);
		input_report_abs(dev, ABS_Y, y);
	}

	input_report_abs(dev, ABS_PRESSURE, z);
	input_report_key(dev, BTN_TOOL_FINGER, z > 0);

	if (priv->flags & ALPS_WHEEL)
		input_report_rel(dev, REL_WHEEL, ((packet[2] << 1) & 0x08) - ((packet[0] >> 4) & 0x07));

	if (priv->flags & (ALPS_FW_BK_1 | ALPS_FW_BK_2)) {
		input_report_key(dev, BTN_FORWARD, forward);
		input_report_key(dev, BTN_BACK, back);
	}

	if (priv->flags & ALPS_FOUR_BUTTONS) {
		input_report_key(dev, BTN_0, packet[2] & 4);
		input_report_key(dev, BTN_1, packet[0] & 0x10);
		input_report_key(dev, BTN_2, packet[3] & 4);
		input_report_key(dev, BTN_3, packet[0] & 0x20);
	}

	input_sync(dev);
}

/*
 * Process bitmap data for V5 protocols. Return value is null.
 *
 * The bitmaps don't have enough data to track fingers, so this function
 * only generates points representing a bounding box of at most two contacts.
 * These two points are returned in fields->mt.
 */
static void alps_process_bitmap_dolphin(struct alps_data *priv,
					struct alps_fields *fields)
{
	int box_middle_x, box_middle_y;
	unsigned int x_map, y_map;
	unsigned char start_bit, end_bit;
	unsigned char x_msb, x_lsb, y_msb, y_lsb;

	x_map = fields->x_map;
	y_map = fields->y_map;

	if (!x_map || !y_map)
		return;

	/* Get Most-significant and Least-significant bit */
	x_msb = fls(x_map);
	x_lsb = ffs(x_map);
	y_msb = fls(y_map);
	y_lsb = ffs(y_map);

	/* Most-significant bit should never exceed max sensor line number */
	if (x_msb > priv->x_bits || y_msb > priv->y_bits)
		return;

	if (fields->fingers > 1) {
		start_bit = priv->x_bits - x_msb;
		end_bit = priv->x_bits - x_lsb;
		box_middle_x = (priv->x_max * (start_bit + end_bit)) /
				(2 * (priv->x_bits - 1));

		start_bit = y_lsb - 1;
		end_bit = y_msb - 1;
		box_middle_y = (priv->y_max * (start_bit + end_bit)) /
				(2 * (priv->y_bits - 1));
		fields->mt[0] = fields->st;
		fields->mt[1].x = 2 * box_middle_x - fields->mt[0].x;
		fields->mt[1].y = 2 * box_middle_y - fields->mt[0].y;
	}
}

static void alps_get_bitmap_points(unsigned int map,
				   struct alps_bitmap_point *low,
				   struct alps_bitmap_point *high,
				   int *fingers)
{
	struct alps_bitmap_point *point;
	int i, bit, prev_bit = 0;

	point = low;
	for (i = 0; map != 0; i++, map >>= 1) {
		bit = map & 1;
		if (bit) {
			if (!prev_bit) {
				point->start_bit = i;
				point->num_bits = 0;
				(*fingers)++;
			}
			point->num_bits++;
		} else {
			if (prev_bit)
				point = high;
		}
		prev_bit = bit;
	}
}

/*
 * Process bitmap data from v3 and v4 protocols. Returns the number of
 * fingers detected. A return value of 0 means at least one of the
 * bitmaps was empty.
 *
 * The bitmaps don't have enough data to track fingers, so this function
 * only generates points representing a bounding box of all contacts.
 * These points are returned in fields->mt when the return value
 * is greater than 0.
 */
static int alps_process_bitmap(struct alps_data *priv,
			       struct alps_fields *fields)
{
	int i, fingers_x = 0, fingers_y = 0, fingers;
	struct alps_bitmap_point x_low = {0,}, x_high = {0,};
	struct alps_bitmap_point y_low = {0,}, y_high = {0,};

	if (!fields->x_map || !fields->y_map)
		return 0;

	alps_get_bitmap_points(fields->x_map, &x_low, &x_high, &fingers_x);
	alps_get_bitmap_points(fields->y_map, &y_low, &y_high, &fingers_y);

	/*
	 * Fingers can overlap, so we use the maximum count of fingers
	 * on either axis as the finger count.
	 */
	fingers = max(fingers_x, fingers_y);

	/*
	 * If an axis reports only a single contact, we have overlapping or
	 * adjacent fingers. Divide the single contact between the two points.
	 */
	if (fingers_x == 1) {
		i = (x_low.num_bits - 1) / 2;
		x_low.num_bits = x_low.num_bits - i;
		x_high.start_bit = x_low.start_bit + i;
		x_high.num_bits = max(i, 1);
	}
	if (fingers_y == 1) {
		i = (y_low.num_bits - 1) / 2;
		y_low.num_bits = y_low.num_bits - i;
		y_high.start_bit = y_low.start_bit + i;
		y_high.num_bits = max(i, 1);
	}

	fields->mt[0].x =
		(priv->x_max * (2 * x_low.start_bit + x_low.num_bits - 1)) /
		(2 * (priv->x_bits - 1));
	fields->mt[0].y =
		(priv->y_max * (2 * y_low.start_bit + y_low.num_bits - 1)) /
		(2 * (priv->y_bits - 1));

	fields->mt[1].x =
		(priv->x_max * (2 * x_high.start_bit + x_high.num_bits - 1)) /
		(2 * (priv->x_bits - 1));
	fields->mt[1].y =
		(priv->y_max * (2 * y_high.start_bit + y_high.num_bits - 1)) /
		(2 * (priv->y_bits - 1));

	/* y-bitmap order is reversed, except on rushmore */
	if (priv->proto_version != ALPS_PROTO_V3_RUSHMORE) {
		fields->mt[0].y = priv->y_max - fields->mt[0].y;
		fields->mt[1].y = priv->y_max - fields->mt[1].y;
	}

	return fingers;
}

static void alps_set_slot(struct input_dev *dev, int slot, int x, int y)
{
	input_mt_slot(dev, slot);
	input_mt_report_slot_state(dev, MT_TOOL_FINGER, true);
	input_report_abs(dev, ABS_MT_POSITION_X, x);
	input_report_abs(dev, ABS_MT_POSITION_Y, y);
}

static void alps_report_mt_data(struct psmouse *psmouse, int n)
{
	struct alps_data *priv = psmouse->private;
	struct input_dev *dev = psmouse->dev;
	struct alps_fields *f = &priv->f;
	int i, slot[MAX_TOUCHES];

	input_mt_assign_slots(dev, slot, f->mt, n, 0);
	for (i = 0; i < n; i++)
		alps_set_slot(dev, slot[i], f->mt[i].x, f->mt[i].y);

	input_mt_sync_frame(dev);
}

static void alps_report_semi_mt_data(struct psmouse *psmouse, int fingers)
{
	struct alps_data *priv = psmouse->private;
	struct input_dev *dev = psmouse->dev;
	struct alps_fields *f = &priv->f;

	/* Use st data when we don't have mt data */
	if (fingers < 2) {
		f->mt[0].x = f->st.x;
		f->mt[0].y = f->st.y;
		fingers = f->pressure > 0 ? 1 : 0;
	}

	alps_report_mt_data(psmouse, (fingers <= 2) ? fingers : 2);

	input_mt_report_finger_count(dev, fingers);

	input_report_key(dev, BTN_LEFT, f->left);
	input_report_key(dev, BTN_RIGHT, f->right);
	input_report_key(dev, BTN_MIDDLE, f->middle);

	input_report_abs(dev, ABS_PRESSURE, f->pressure);

	input_sync(dev);
}

static void alps_process_trackstick_packet_v3(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	unsigned char *packet = psmouse->packet;
	struct input_dev *dev = priv->dev2;
	int x, y, z, left, right, middle;

	/* It should be a DualPoint when received trackstick packet */
	if (!(priv->flags & ALPS_DUALPOINT)) {
		psmouse_warn(psmouse,
			     "Rejected trackstick packet from non DualPoint device");
		return;
	}

	/* Sanity check packet */
	if (!(packet[0] & 0x40)) {
		psmouse_dbg(psmouse, "Bad trackstick packet, discarding\n");
		return;
	}

	/*
	 * There's a special packet that seems to indicate the end
	 * of a stream of trackstick data. Filter these out.
	 */
	if (packet[1] == 0x7f && packet[2] == 0x7f && packet[4] == 0x7f)
		return;

	x = (s8)(((packet[0] & 0x20) << 2) | (packet[1] & 0x7f));
	y = (s8)(((packet[0] & 0x10) << 3) | (packet[2] & 0x7f));
	z = (packet[4] & 0x7c) >> 2;

	/*
	 * The x and y values tend to be quite large, and when used
	 * alone the trackstick is difficult to use. Scale them down
	 * to compensate.
	 */
	x /= 8;
	y /= 8;

	input_report_rel(dev, REL_X, x);
	input_report_rel(dev, REL_Y, -y);

	/*
	 * Most ALPS models report the trackstick buttons in the touchpad
	 * packets, but a few report them here. No reliable way has been
	 * found to differentiate between the models upfront, so we enable
	 * the quirk in response to seeing a button press in the trackstick
	 * packet.
	 */
	left = packet[3] & 0x01;
	right = packet[3] & 0x02;
	middle = packet[3] & 0x04;

	if (!(priv->quirks & ALPS_QUIRK_TRACKSTICK_BUTTONS) &&
	    (left || right || middle))
		priv->quirks |= ALPS_QUIRK_TRACKSTICK_BUTTONS;

	if (priv->quirks & ALPS_QUIRK_TRACKSTICK_BUTTONS) {
		input_report_key(dev, BTN_LEFT, left);
		input_report_key(dev, BTN_RIGHT, right);
		input_report_key(dev, BTN_MIDDLE, middle);
	}

	input_sync(dev);
	return;
}

static void alps_decode_buttons_v3(struct alps_fields *f, unsigned char *p)
{
	f->left = !!(p[3] & 0x01);
	f->right = !!(p[3] & 0x02);
	f->middle = !!(p[3] & 0x04);

	f->ts_left = !!(p[3] & 0x10);
	f->ts_right = !!(p[3] & 0x20);
	f->ts_middle = !!(p[3] & 0x40);
}

static int alps_decode_pinnacle(struct alps_fields *f, unsigned char *p,
				 struct psmouse *psmouse)
{
	f->first_mp = !!(p[4] & 0x40);
	f->is_mp = !!(p[0] & 0x40);

	f->fingers = (p[5] & 0x3) + 1;
	f->x_map = ((p[4] & 0x7e) << 8) |
		   ((p[1] & 0x7f) << 2) |
		   ((p[0] & 0x30) >> 4);
	f->y_map = ((p[3] & 0x70) << 4) |
		   ((p[2] & 0x7f) << 1) |
		   (p[4] & 0x01);

	f->st.x = ((p[1] & 0x7f) << 4) | ((p[4] & 0x30) >> 2) |
	       ((p[0] & 0x30) >> 4);
	f->st.y = ((p[2] & 0x7f) << 4) | (p[4] & 0x0f);
	f->pressure = p[5] & 0x7f;

	alps_decode_buttons_v3(f, p);

	return 0;
}

static int alps_decode_rushmore(struct alps_fields *f, unsigned char *p,
				 struct psmouse *psmouse)
{
	alps_decode_pinnacle(f, p, psmouse);

	/* Rushmore's packet decode has a bit difference with Pinnacle's */
	f->is_mp = !!(p[5] & 0x40);
	f->fingers = max((p[5] & 0x3), ((p[5] >> 2) & 0x3)) + 1;
	f->x_map |= (p[5] & 0x10) << 11;
	f->y_map |= (p[5] & 0x20) << 6;

	return 0;
}

static int alps_decode_dolphin(struct alps_fields *f, unsigned char *p,
				struct psmouse *psmouse)
{
	u64 palm_data = 0;
	struct alps_data *priv = psmouse->private;

	f->first_mp = !!(p[0] & 0x02);
	f->is_mp = !!(p[0] & 0x20);

	if (!f->is_mp) {
		f->st.x = ((p[1] & 0x7f) | ((p[4] & 0x0f) << 7));
		f->st.y = ((p[2] & 0x7f) | ((p[4] & 0xf0) << 3));
		f->pressure = (p[0] & 4) ? 0 : p[5] & 0x7f;
		alps_decode_buttons_v3(f, p);
	} else {
		f->fingers = ((p[0] & 0x6) >> 1 |
		     (p[0] & 0x10) >> 2);

		palm_data = (p[1] & 0x7f) |
			    ((p[2] & 0x7f) << 7) |
			    ((p[4] & 0x7f) << 14) |
			    ((p[5] & 0x7f) << 21) |
			    ((p[3] & 0x07) << 28) |
			    (((u64)p[3] & 0x70) << 27) |
			    (((u64)p[0] & 0x01) << 34);

		/* Y-profile is stored in P(0) to p(n-1), n = y_bits; */
		f->y_map = palm_data & (BIT(priv->y_bits) - 1);

		/* X-profile is stored in p(n) to p(n+m-1), m = x_bits; */
		f->x_map = (palm_data >> priv->y_bits) &
			   (BIT(priv->x_bits) - 1);
	}

	return 0;
}

static void alps_process_touchpad_packet_v3_v5(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	unsigned char *packet = psmouse->packet;
	struct input_dev *dev2 = priv->dev2;
	struct alps_fields *f = &priv->f;
	int fingers = 0;

	memset(f, 0, sizeof(*f));

	priv->decode_fields(f, packet, psmouse);

	/*
	 * There's no single feature of touchpad position and bitmap packets
	 * that can be used to distinguish between them. We rely on the fact
	 * that a bitmap packet should always follow a position packet with
	 * bit 6 of packet[4] set.
	 */
	if (priv->multi_packet) {
		/*
		 * Sometimes a position packet will indicate a multi-packet
		 * sequence, but then what follows is another position
		 * packet. Check for this, and when it happens process the
		 * position packet as usual.
		 */
		if (f->is_mp) {
			fingers = f->fingers;
			if (priv->proto_version == ALPS_PROTO_V3 ||
			    priv->proto_version == ALPS_PROTO_V3_RUSHMORE) {
				if (alps_process_bitmap(priv, f) == 0)
					fingers = 0; /* Use st data */

				/* Now process position packet */
				priv->decode_fields(f, priv->multi_data,
						    psmouse);
			} else {
				/*
				 * Because Dolphin uses position packet's
				 * coordinate data as Pt1 and uses it to
				 * calculate Pt2, so we need to do position
				 * packet decode first.
				 */
				priv->decode_fields(f, priv->multi_data,
						    psmouse);

				/*
				 * Since Dolphin's finger number is reliable,
				 * there is no need to compare with bmap_fn.
				 */
				alps_process_bitmap_dolphin(priv, f);
			}
		} else {
			priv->multi_packet = 0;
		}
	}

	/*
	 * Bit 6 of byte 0 is not usually set in position packets. The only
	 * times it seems to be set is in situations where the data is
	 * suspect anyway, e.g. a palm resting flat on the touchpad. Given
	 * this combined with the fact that this bit is useful for filtering
	 * out misidentified bitmap packets, we reject anything with this
	 * bit set.
	 */
	if (f->is_mp)
		return;

	if (!priv->multi_packet && f->first_mp) {
		priv->multi_packet = 1;
		memcpy(priv->multi_data, packet, sizeof(priv->multi_data));
		return;
	}

	priv->multi_packet = 0;

	/*
	 * Sometimes the hardware sends a single packet with z = 0
	 * in the middle of a stream. Real releases generate packets
	 * with x, y, and z all zero, so these seem to be flukes.
	 * Ignore them.
	 */
	if (f->st.x && f->st.y && !f->pressure)
		return;

	alps_report_semi_mt_data(psmouse, fingers);

	if ((priv->flags & ALPS_DUALPOINT) &&
	    !(priv->quirks & ALPS_QUIRK_TRACKSTICK_BUTTONS)) {
		input_report_key(dev2, BTN_LEFT, f->ts_left);
		input_report_key(dev2, BTN_RIGHT, f->ts_right);
		input_report_key(dev2, BTN_MIDDLE, f->ts_middle);
		input_sync(dev2);
	}
}

static void alps_process_packet_v3(struct psmouse *psmouse)
{
	unsigned char *packet = psmouse->packet;

	/*
	 * v3 protocol packets come in three types, two representing
	 * touchpad data and one representing trackstick data.
	 * Trackstick packets seem to be distinguished by always
	 * having 0x3f in the last byte. This value has never been
	 * observed in the last byte of either of the other types
	 * of packets.
	 */
	if (packet[5] == 0x3f) {
		alps_process_trackstick_packet_v3(psmouse);
		return;
	}

	alps_process_touchpad_packet_v3_v5(psmouse);
}

static void alps_process_packet_v6(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	unsigned char *packet = psmouse->packet;
	struct input_dev *dev = psmouse->dev;
	struct input_dev *dev2 = priv->dev2;
	int x, y, z, left, right, middle;

	/*
	 * We can use Byte5 to distinguish if the packet is from Touchpad
	 * or Trackpoint.
	 * Touchpad:	0 - 0x7E
	 * Trackpoint:	0x7F
	 */
	if (packet[5] == 0x7F) {
		/* It should be a DualPoint when received Trackpoint packet */
		if (!(priv->flags & ALPS_DUALPOINT)) {
			psmouse_warn(psmouse,
				     "Rejected trackstick packet from non DualPoint device");
			return;
		}

		/* Trackpoint packet */
		x = packet[1] | ((packet[3] & 0x20) << 2);
		y = packet[2] | ((packet[3] & 0x40) << 1);
		z = packet[4];
		left = packet[3] & 0x01;
		right = packet[3] & 0x02;
		middle = packet[3] & 0x04;

		/* To prevent the cursor jump when finger lifted */
		if (x == 0x7F && y == 0x7F && z == 0x7F)
			x = y = z = 0;

		/* Divide 4 since trackpoint's speed is too fast */
		input_report_rel(dev2, REL_X, (char)x / 4);
		input_report_rel(dev2, REL_Y, -((char)y / 4));

		input_report_key(dev2, BTN_LEFT, left);
		input_report_key(dev2, BTN_RIGHT, right);
		input_report_key(dev2, BTN_MIDDLE, middle);

		input_sync(dev2);
		return;
	}

	/* Touchpad packet */
	x = packet[1] | ((packet[3] & 0x78) << 4);
	y = packet[2] | ((packet[4] & 0x78) << 4);
	z = packet[5];
	left = packet[3] & 0x01;
	right = packet[3] & 0x02;

	if (z > 30)
		input_report_key(dev, BTN_TOUCH, 1);
	if (z < 25)
		input_report_key(dev, BTN_TOUCH, 0);

	if (z > 0) {
		input_report_abs(dev, ABS_X, x);
		input_report_abs(dev, ABS_Y, y);
	}

	input_report_abs(dev, ABS_PRESSURE, z);
	input_report_key(dev, BTN_TOOL_FINGER, z > 0);

	/* v6 touchpad does not have middle button */
	input_report_key(dev, BTN_LEFT, left);
	input_report_key(dev, BTN_RIGHT, right);

	input_sync(dev);
}

static void alps_process_packet_v4(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	unsigned char *packet = psmouse->packet;
	struct alps_fields *f = &priv->f;
	int offset;

	/*
	 * v4 has a 6-byte encoding for bitmap data, but this data is
	 * broken up between 3 normal packets. Use priv->multi_packet to
	 * track our position in the bitmap packet.
	 */
	if (packet[6] & 0x40) {
		/* sync, reset position */
		priv->multi_packet = 0;
	}

	if (WARN_ON_ONCE(priv->multi_packet > 2))
		return;

	offset = 2 * priv->multi_packet;
	priv->multi_data[offset] = packet[6];
	priv->multi_data[offset + 1] = packet[7];

	if (++priv->multi_packet > 2) {
		priv->multi_packet = 0;

		f->x_map = ((priv->multi_data[2] & 0x1f) << 10) |
			   ((priv->multi_data[3] & 0x60) << 3) |
			   ((priv->multi_data[0] & 0x3f) << 2) |
			   ((priv->multi_data[1] & 0x60) >> 5);
		f->y_map = ((priv->multi_data[5] & 0x01) << 10) |
			   ((priv->multi_data[3] & 0x1f) << 5) |
			    (priv->multi_data[1] & 0x1f);

		f->fingers = alps_process_bitmap(priv, f);
	}

	f->left = !!(packet[4] & 0x01);
	f->right = !!(packet[4] & 0x02);

	f->st.x = ((packet[1] & 0x7f) << 4) | ((packet[3] & 0x30) >> 2) |
		  ((packet[0] & 0x30) >> 4);
	f->st.y = ((packet[2] & 0x7f) << 4) | (packet[3] & 0x0f);
	f->pressure = packet[5] & 0x7f;

	alps_report_semi_mt_data(psmouse, f->fingers);
}

static bool alps_is_valid_package_v7(struct psmouse *psmouse)
{
	switch (psmouse->pktcnt) {
	case 3:
		return (psmouse->packet[2] & 0x40) == 0x40;
	case 4:
		return (psmouse->packet[3] & 0x48) == 0x48;
	case 6:
		return (psmouse->packet[5] & 0x40) == 0x00;
	}
	return true;
}

static unsigned char alps_get_packet_id_v7(char *byte)
{
	unsigned char packet_id;

	if (byte[4] & 0x40)
		packet_id = V7_PACKET_ID_TWO;
	else if (byte[4] & 0x01)
		packet_id = V7_PACKET_ID_MULTI;
	else if ((byte[0] & 0x10) && !(byte[4] & 0x43))
		packet_id = V7_PACKET_ID_NEW;
	else if (byte[1] == 0x00 && byte[4] == 0x00)
		packet_id = V7_PACKET_ID_IDLE;
	else
		packet_id = V7_PACKET_ID_UNKNOWN;

	return packet_id;
}

static void alps_get_finger_coordinate_v7(struct input_mt_pos *mt,
					  unsigned char *pkt,
					  unsigned char pkt_id)
{
	mt[0].x = ((pkt[2] & 0x80) << 4);
	mt[0].x |= ((pkt[2] & 0x3F) << 5);
	mt[0].x |= ((pkt[3] & 0x30) >> 1);
	mt[0].x |= (pkt[3] & 0x07);
	mt[0].y = (pkt[1] << 3) | (pkt[0] & 0x07);

	mt[1].x = ((pkt[3] & 0x80) << 4);
	mt[1].x |= ((pkt[4] & 0x80) << 3);
	mt[1].x |= ((pkt[4] & 0x3F) << 4);
	mt[1].y = ((pkt[5] & 0x80) << 3);
	mt[1].y |= ((pkt[5] & 0x3F) << 4);

	switch (pkt_id) {
	case V7_PACKET_ID_TWO:
		mt[1].x &= ~0x000F;
		mt[1].y |= 0x000F;
		break;

	case V7_PACKET_ID_MULTI:
		mt[1].x &= ~0x003F;
		mt[1].y &= ~0x0020;
		mt[1].y |= ((pkt[4] & 0x02) << 4);
		mt[1].y |= 0x001F;
		break;

	case V7_PACKET_ID_NEW:
		mt[1].x &= ~0x003F;
		mt[1].x |= (pkt[0] & 0x20);
		mt[1].y |= 0x000F;
		break;
	}

	mt[0].y = 0x7FF - mt[0].y;
	mt[1].y = 0x7FF - mt[1].y;
}

static int alps_get_mt_count(struct input_mt_pos *mt)
{
	int i, fingers = 0;

	for (i = 0; i < MAX_TOUCHES; i++) {
		if (mt[i].x != 0 || mt[i].y != 0)
			fingers++;
	}

	return fingers;
}

static int alps_decode_packet_v7(struct alps_fields *f,
				  unsigned char *p,
				  struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	unsigned char pkt_id;

	pkt_id = alps_get_packet_id_v7(p);
	if (pkt_id == V7_PACKET_ID_IDLE)
		return 0;
	if (pkt_id == V7_PACKET_ID_UNKNOWN)
		return -1;
	/*
	 * NEW packets are send to indicate a discontinuity in the finger
	 * coordinate reporting. Specifically a finger may have moved from
	 * slot 0 to 1 or vice versa. INPUT_MT_TRACK takes care of this for
	 * us.
	 *
	 * NEW packets have 3 problems:
	 * 1) They do not contain middle / right button info (on non clickpads)
	 *    this can be worked around by preserving the old button state
	 * 2) They do not contain an accurate fingercount, and they are
	 *    typically send when the number of fingers changes. We cannot use
	 *    the old finger count as that may mismatch with the amount of
	 *    touch coordinates we've available in the NEW packet
	 * 3) Their x data for the second touch is inaccurate leading to
	 *    a possible jump of the x coordinate by 16 units when the first
	 *    non NEW packet comes in
	 * Since problems 2 & 3 cannot be worked around, just ignore them.
	 */
	if (pkt_id == V7_PACKET_ID_NEW)
		return 1;

	alps_get_finger_coordinate_v7(f->mt, p, pkt_id);

	if (pkt_id == V7_PACKET_ID_TWO)
		f->fingers = alps_get_mt_count(f->mt);
	else /* pkt_id == V7_PACKET_ID_MULTI */
		f->fingers = 3 + (p[5] & 0x03);

	f->left = (p[0] & 0x80) >> 7;
	if (priv->flags & ALPS_BUTTONPAD) {
		if (p[0] & 0x20)
			f->fingers++;
		if (p[0] & 0x10)
			f->fingers++;
	} else {
		f->right = (p[0] & 0x20) >> 5;
		f->middle = (p[0] & 0x10) >> 4;
	}

	/* Sometimes a single touch is reported in mt[1] rather then mt[0] */
	if (f->fingers == 1 && f->mt[0].x == 0 && f->mt[0].y == 0) {
		f->mt[0].x = f->mt[1].x;
		f->mt[0].y = f->mt[1].y;
		f->mt[1].x = 0;
		f->mt[1].y = 0;
	}

	return 0;
}

static void alps_process_trackstick_packet_v7(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	unsigned char *packet = psmouse->packet;
	struct input_dev *dev2 = priv->dev2;
	int x, y, z, left, right, middle;

	/* It should be a DualPoint when received trackstick packet */
	if (!(priv->flags & ALPS_DUALPOINT)) {
		psmouse_warn(psmouse,
			     "Rejected trackstick packet from non DualPoint device");
		return;
	}

	x = ((packet[2] & 0xbf)) | ((packet[3] & 0x10) << 2);
	y = (packet[3] & 0x07) | (packet[4] & 0xb8) |
	    ((packet[3] & 0x20) << 1);
	z = (packet[5] & 0x3f) | ((packet[3] & 0x80) >> 1);

	left = (packet[1] & 0x01);
	right = (packet[1] & 0x02) >> 1;
	middle = (packet[1] & 0x04) >> 2;

	/* Divide 2 since trackpoint's speed is too fast */
	input_report_rel(dev2, REL_X, (char)x / 2);
	input_report_rel(dev2, REL_Y, -((char)y / 2));

	input_report_key(dev2, BTN_LEFT, left);
	input_report_key(dev2, BTN_RIGHT, right);
	input_report_key(dev2, BTN_MIDDLE, middle);

	input_sync(dev2);
}

static void alps_process_touchpad_packet_v7(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	struct input_dev *dev = psmouse->dev;
	struct alps_fields *f = &priv->f;

	memset(f, 0, sizeof(*f));

	if (priv->decode_fields(f, psmouse->packet, psmouse))
		return;

	alps_report_mt_data(psmouse, alps_get_mt_count(f->mt));

	input_mt_report_finger_count(dev, f->fingers);

	input_report_key(dev, BTN_LEFT, f->left);
	input_report_key(dev, BTN_RIGHT, f->right);
	input_report_key(dev, BTN_MIDDLE, f->middle);

	input_sync(dev);
}

static void alps_process_packet_v7(struct psmouse *psmouse)
{
	unsigned char *packet = psmouse->packet;

	if (packet[0] == 0x48 && (packet[4] & 0x47) == 0x06)
		alps_process_trackstick_packet_v7(psmouse);
	else
		alps_process_touchpad_packet_v7(psmouse);
}

static DEFINE_MUTEX(alps_mutex);

static void alps_register_bare_ps2_mouse(struct work_struct *work)
{
	struct alps_data *priv =
		container_of(work, struct alps_data, dev3_register_work.work);
	struct psmouse *psmouse = priv->psmouse;
	struct input_dev *dev3;
	int error = 0;

	mutex_lock(&alps_mutex);

	if (priv->dev3)
		goto out;

	dev3 = input_allocate_device();
	if (!dev3) {
		psmouse_err(psmouse, "failed to allocate secondary device\n");
		error = -ENOMEM;
		goto out;
	}

	snprintf(priv->phys3, sizeof(priv->phys3), "%s/%s",
		 psmouse->ps2dev.serio->phys,
		 (priv->dev2 ? "input2" : "input1"));
	dev3->phys = priv->phys3;

	/*
	 * format of input device name is: "protocol vendor name"
	 * see function psmouse_switch_protocol() in psmouse-base.c
	 */
	dev3->name = "PS/2 ALPS Mouse";

	dev3->id.bustype = BUS_I8042;
	dev3->id.vendor  = 0x0002;
	dev3->id.product = PSMOUSE_PS2;
	dev3->id.version = 0x0000;
	dev3->dev.parent = &psmouse->ps2dev.serio->dev;

	input_set_capability(dev3, EV_REL, REL_X);
	input_set_capability(dev3, EV_REL, REL_Y);
	input_set_capability(dev3, EV_KEY, BTN_LEFT);
	input_set_capability(dev3, EV_KEY, BTN_RIGHT);
	input_set_capability(dev3, EV_KEY, BTN_MIDDLE);

	__set_bit(INPUT_PROP_POINTER, dev3->propbit);

	error = input_register_device(dev3);
	if (error) {
		psmouse_err(psmouse,
			    "failed to register secondary device: %d\n",
			    error);
		input_free_device(dev3);
		goto out;
	}

	priv->dev3 = dev3;

out:
	/*
	 * Save the error code so that we can detect that we
	 * already tried to create the device.
	 */
	if (error)
		priv->dev3 = ERR_PTR(error);

	mutex_unlock(&alps_mutex);
}

static void alps_report_bare_ps2_packet(struct input_dev *dev,
					unsigned char packet[],
					bool report_buttons)
{
	if (report_buttons)
		alps_report_buttons(dev, NULL,
				packet[0] & 1, packet[0] & 2, packet[0] & 4);

	input_report_rel(dev, REL_X,
		packet[1] ? packet[1] - ((packet[0] << 4) & 0x100) : 0);
	input_report_rel(dev, REL_Y,
		packet[2] ? ((packet[0] << 3) & 0x100) - packet[2] : 0);

	input_sync(dev);
}

static psmouse_ret_t alps_handle_interleaved_ps2(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;

	if (psmouse->pktcnt < 6)
		return PSMOUSE_GOOD_DATA;

	if (psmouse->pktcnt == 6) {
		/*
		 * Start a timer to flush the packet if it ends up last
		 * 6-byte packet in the stream. Timer needs to fire
		 * psmouse core times out itself. 20 ms should be enough
		 * to decide if we are getting more data or not.
		 */
		mod_timer(&priv->timer, jiffies + msecs_to_jiffies(20));
		return PSMOUSE_GOOD_DATA;
	}

	del_timer(&priv->timer);

	if (psmouse->packet[6] & 0x80) {

		/*
		 * Highest bit is set - that means we either had
		 * complete ALPS packet and this is start of the
		 * next packet or we got garbage.
		 */

		if (((psmouse->packet[3] |
		      psmouse->packet[4] |
		      psmouse->packet[5]) & 0x80) ||
		    (!alps_is_valid_first_byte(priv, psmouse->packet[6]))) {
			psmouse_dbg(psmouse,
				    "refusing packet %4ph (suspected interleaved ps/2)\n",
				    psmouse->packet + 3);
			return PSMOUSE_BAD_DATA;
		}

		priv->process_packet(psmouse);

		/* Continue with the next packet */
		psmouse->packet[0] = psmouse->packet[6];
		psmouse->pktcnt = 1;

	} else {

		/*
		 * High bit is 0 - that means that we indeed got a PS/2
		 * packet in the middle of ALPS packet.
		 *
		 * There is also possibility that we got 6-byte ALPS
		 * packet followed  by 3-byte packet from trackpoint. We
		 * can not distinguish between these 2 scenarios but
		 * because the latter is unlikely to happen in course of
		 * normal operation (user would need to press all
		 * buttons on the pad and start moving trackpoint
		 * without touching the pad surface) we assume former.
		 * Even if we are wrong the wost thing that would happen
		 * the cursor would jump but we should not get protocol
		 * de-synchronization.
		 */

		alps_report_bare_ps2_packet(priv->dev2,
					    &psmouse->packet[3], false);

		/*
		 * Continue with the standard ALPS protocol handling,
		 * but make sure we won't process it as an interleaved
		 * packet again, which may happen if all buttons are
		 * pressed. To avoid this let's reset the 4th bit which
		 * is normally 1.
		 */
		psmouse->packet[3] = psmouse->packet[6] & 0xf7;
		psmouse->pktcnt = 4;
	}

	return PSMOUSE_GOOD_DATA;
}

static void alps_flush_packet(unsigned long data)
{
	struct psmouse *psmouse = (struct psmouse *)data;
	struct alps_data *priv = psmouse->private;

	serio_pause_rx(psmouse->ps2dev.serio);

	if (psmouse->pktcnt == psmouse->pktsize) {

		/*
		 * We did not any more data in reasonable amount of time.
		 * Validate the last 3 bytes and process as a standard
		 * ALPS packet.
		 */
		if ((psmouse->packet[3] |
		     psmouse->packet[4] |
		     psmouse->packet[5]) & 0x80) {
			psmouse_dbg(psmouse,
				    "refusing packet %3ph (suspected interleaved ps/2)\n",
				    psmouse->packet + 3);
		} else {
			priv->process_packet(psmouse);
		}
		psmouse->pktcnt = 0;
	}

	serio_continue_rx(psmouse->ps2dev.serio);
}

static psmouse_ret_t alps_process_byte(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;

	/*
	 * Check if we are dealing with a bare PS/2 packet, presumably from
	 * a device connected to the external PS/2 port. Because bare PS/2
	 * protocol does not have enough constant bits to self-synchronize
	 * properly we only do this if the device is fully synchronized.
	 */
	if (!psmouse->out_of_sync_cnt && (psmouse->packet[0] & 0xc8) == 0x08) {

		/* Register dev3 mouse if we received PS/2 packet first time */
		if (unlikely(!priv->dev3))
			psmouse_queue_work(psmouse,
					   &priv->dev3_register_work, 0);

		if (psmouse->pktcnt == 3) {
			/* Once dev3 mouse device is registered report data */
			if (likely(!IS_ERR_OR_NULL(priv->dev3)))
				alps_report_bare_ps2_packet(priv->dev3,
							    psmouse->packet,
							    true);
			return PSMOUSE_FULL_PACKET;
		}
		return PSMOUSE_GOOD_DATA;
	}

	/* Check for PS/2 packet stuffed in the middle of ALPS packet. */

	if ((priv->flags & ALPS_PS2_INTERLEAVED) &&
	    psmouse->pktcnt >= 4 && (psmouse->packet[3] & 0x0f) == 0x0f) {
		return alps_handle_interleaved_ps2(psmouse);
	}

	if (!alps_is_valid_first_byte(priv, psmouse->packet[0])) {
		psmouse_dbg(psmouse,
			    "refusing packet[0] = %x (mask0 = %x, byte0 = %x)\n",
			    psmouse->packet[0], priv->mask0, priv->byte0);
		return PSMOUSE_BAD_DATA;
	}

	/* Bytes 2 - pktsize should have 0 in the highest bit */
	if (priv->proto_version < ALPS_PROTO_V5 &&
	    psmouse->pktcnt >= 2 && psmouse->pktcnt <= psmouse->pktsize &&
	    (psmouse->packet[psmouse->pktcnt - 1] & 0x80)) {
		psmouse_dbg(psmouse, "refusing packet[%i] = %x\n",
			    psmouse->pktcnt - 1,
			    psmouse->packet[psmouse->pktcnt - 1]);

		if (priv->proto_version == ALPS_PROTO_V3_RUSHMORE &&
		    psmouse->pktcnt == psmouse->pktsize) {
			/*
			 * Some Dell boxes, such as Latitude E6440 or E7440
			 * with closed lid, quite often smash last byte of
			 * otherwise valid packet with 0xff. Given that the
			 * next packet is very likely to be valid let's
			 * report PSMOUSE_FULL_PACKET but not process data,
			 * rather than reporting PSMOUSE_BAD_DATA and
			 * filling the logs.
			 */
			return PSMOUSE_FULL_PACKET;
		}

		return PSMOUSE_BAD_DATA;
	}

	if (priv->proto_version == ALPS_PROTO_V7 &&
	    !alps_is_valid_package_v7(psmouse)) {
		psmouse_dbg(psmouse, "refusing packet[%i] = %x\n",
			    psmouse->pktcnt - 1,
			    psmouse->packet[psmouse->pktcnt - 1]);
		return PSMOUSE_BAD_DATA;
	}

	if (psmouse->pktcnt == psmouse->pktsize) {
		priv->process_packet(psmouse);
		return PSMOUSE_FULL_PACKET;
	}

	return PSMOUSE_GOOD_DATA;
}

static int alps_command_mode_send_nibble(struct psmouse *psmouse, int nibble)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	struct alps_data *priv = psmouse->private;
	int command;
	unsigned char *param;
	unsigned char dummy[4];

	BUG_ON(nibble > 0xf);

	command = priv->nibble_commands[nibble].command;
	param = (command & 0x0f00) ?
		dummy : (unsigned char *)&priv->nibble_commands[nibble].data;

	if (ps2_command(ps2dev, param, command))
		return -1;

	return 0;
}

static int alps_command_mode_set_addr(struct psmouse *psmouse, int addr)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	struct alps_data *priv = psmouse->private;
	int i, nibble;

	if (ps2_command(ps2dev, NULL, priv->addr_command))
		return -1;

	for (i = 12; i >= 0; i -= 4) {
		nibble = (addr >> i) & 0xf;
		if (alps_command_mode_send_nibble(psmouse, nibble))
			return -1;
	}

	return 0;
}

static int __alps_command_mode_read_reg(struct psmouse *psmouse, int addr)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[4];

	if (ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO))
		return -1;

	/*
	 * The address being read is returned in the first two bytes
	 * of the result. Check that this address matches the expected
	 * address.
	 */
	if (addr != ((param[0] << 8) | param[1]))
		return -1;

	return param[2];
}

static int alps_command_mode_read_reg(struct psmouse *psmouse, int addr)
{
	if (alps_command_mode_set_addr(psmouse, addr))
		return -1;
	return __alps_command_mode_read_reg(psmouse, addr);
}

static int __alps_command_mode_write_reg(struct psmouse *psmouse, u8 value)
{
	if (alps_command_mode_send_nibble(psmouse, (value >> 4) & 0xf))
		return -1;
	if (alps_command_mode_send_nibble(psmouse, value & 0xf))
		return -1;
	return 0;
}

static int alps_command_mode_write_reg(struct psmouse *psmouse, int addr,
				       u8 value)
{
	if (alps_command_mode_set_addr(psmouse, addr))
		return -1;
	return __alps_command_mode_write_reg(psmouse, value);
}

static int alps_rpt_cmd(struct psmouse *psmouse, int init_command,
			int repeated_command, unsigned char *param)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;

	param[0] = 0;
	if (init_command && ps2_command(ps2dev, param, init_command))
		return -EIO;

	if (ps2_command(ps2dev,  NULL, repeated_command) ||
	    ps2_command(ps2dev,  NULL, repeated_command) ||
	    ps2_command(ps2dev,  NULL, repeated_command))
		return -EIO;

	param[0] = param[1] = param[2] = 0xff;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO))
		return -EIO;

	psmouse_dbg(psmouse, "%2.2X report: %3ph\n",
		    repeated_command, param);
	return 0;
}

static bool alps_check_valid_firmware_id(unsigned char id[])
{
	if (id[0] == 0x73)
		return true;

	if (id[0] == 0x88 &&
	    (id[1] == 0x07 ||
	     id[1] == 0x08 ||
	     (id[1] & 0xf0) == 0xb0 ||
	     (id[1] & 0xf0) == 0xc0)) {
		return true;
	}

	return false;
}

static int alps_enter_command_mode(struct psmouse *psmouse)
{
	unsigned char param[4];

	if (alps_rpt_cmd(psmouse, 0, PSMOUSE_CMD_RESET_WRAP, param)) {
		psmouse_err(psmouse, "failed to enter command mode\n");
		return -1;
	}

	if (!alps_check_valid_firmware_id(param)) {
		psmouse_dbg(psmouse,
			    "unknown response while entering command mode\n");
		return -1;
	}
	return 0;
}

static inline int alps_exit_command_mode(struct psmouse *psmouse)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	if (ps2_command(ps2dev, NULL, PSMOUSE_CMD_SETSTREAM))
		return -1;
	return 0;
}

/*
 * For DualPoint devices select the device that should respond to
 * subsequent commands. It looks like glidepad is behind stickpointer,
 * I'd thought it would be other way around...
 */
static int alps_passthrough_mode_v2(struct psmouse *psmouse, bool enable)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	int cmd = enable ? PSMOUSE_CMD_SETSCALE21 : PSMOUSE_CMD_SETSCALE11;

	if (ps2_command(ps2dev, NULL, cmd) ||
	    ps2_command(ps2dev, NULL, cmd) ||
	    ps2_command(ps2dev, NULL, cmd) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE))
		return -1;

	/* we may get 3 more bytes, just ignore them */
	ps2_drain(ps2dev, 3, 100);

	return 0;
}

static int alps_absolute_mode_v1_v2(struct psmouse *psmouse)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;

	/* Try ALPS magic knock - 4 disable before enable */
	if (ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_ENABLE))
		return -1;

	/*
	 * Switch mouse to poll (remote) mode so motion data will not
	 * get in our way
	 */
	return ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_SETPOLL);
}

static int alps_monitor_mode_send_word(struct psmouse *psmouse, u16 word)
{
	int i, nibble;

	/*
	 * b0-b11 are valid bits, send sequence is inverse.
	 * e.g. when word = 0x0123, nibble send sequence is 3, 2, 1
	 */
	for (i = 0; i <= 8; i += 4) {
		nibble = (word >> i) & 0xf;
		if (alps_command_mode_send_nibble(psmouse, nibble))
			return -1;
	}

	return 0;
}

static int alps_monitor_mode_write_reg(struct psmouse *psmouse,
				       u16 addr, u16 value)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;

	/* 0x0A0 is the command to write the word */
	if (ps2_command(ps2dev, NULL, PSMOUSE_CMD_ENABLE) ||
	    alps_monitor_mode_send_word(psmouse, 0x0A0) ||
	    alps_monitor_mode_send_word(psmouse, addr) ||
	    alps_monitor_mode_send_word(psmouse, value) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE))
		return -1;

	return 0;
}

static int alps_monitor_mode(struct psmouse *psmouse, bool enable)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;

	if (enable) {
		/* EC E9 F5 F5 E7 E6 E7 E9 to enter monitor mode */
		if (ps2_command(ps2dev, NULL, PSMOUSE_CMD_RESET_WRAP) ||
		    ps2_command(ps2dev, NULL, PSMOUSE_CMD_GETINFO) ||
		    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
		    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
		    ps2_command(ps2dev, NULL, PSMOUSE_CMD_SETSCALE21) ||
		    ps2_command(ps2dev, NULL, PSMOUSE_CMD_SETSCALE11) ||
		    ps2_command(ps2dev, NULL, PSMOUSE_CMD_SETSCALE21) ||
		    ps2_command(ps2dev, NULL, PSMOUSE_CMD_GETINFO))
			return -1;
	} else {
		/* EC to exit monitor mode */
		if (ps2_command(ps2dev, NULL, PSMOUSE_CMD_RESET_WRAP))
			return -1;
	}

	return 0;
}

static int alps_absolute_mode_v6(struct psmouse *psmouse)
{
	u16 reg_val = 0x181;
	int ret = -1;

	/* enter monitor mode, to write the register */
	if (alps_monitor_mode(psmouse, true))
		return -1;

	ret = alps_monitor_mode_write_reg(psmouse, 0x000, reg_val);

	if (alps_monitor_mode(psmouse, false))
		ret = -1;

	return ret;
}

static int alps_get_status(struct psmouse *psmouse, char *param)
{
	/* Get status: 0xF5 0xF5 0xF5 0xE9 */
	if (alps_rpt_cmd(psmouse, 0, PSMOUSE_CMD_DISABLE, param))
		return -1;

	return 0;
}

/*
 * Turn touchpad tapping on or off. The sequences are:
 * 0xE9 0xF5 0xF5 0xF3 0x0A to enable,
 * 0xE9 0xF5 0xF5 0xE8 0x00 to disable.
 * My guess that 0xE9 (GetInfo) is here as a sync point.
 * For models that also have stickpointer (DualPoints) its tapping
 * is controlled separately (0xE6 0xE6 0xE6 0xF3 0x14|0x0A) but
 * we don't fiddle with it.
 */
static int alps_tap_mode(struct psmouse *psmouse, int enable)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	int cmd = enable ? PSMOUSE_CMD_SETRATE : PSMOUSE_CMD_SETRES;
	unsigned char tap_arg = enable ? 0x0A : 0x00;
	unsigned char param[4];

	if (ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, &tap_arg, cmd))
		return -1;

	if (alps_get_status(psmouse, param))
		return -1;

	return 0;
}

/*
 * alps_poll() - poll the touchpad for current motion packet.
 * Used in resync.
 */
static int alps_poll(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	unsigned char buf[sizeof(psmouse->packet)];
	bool poll_failed;

	if (priv->flags & ALPS_PASS)
		alps_passthrough_mode_v2(psmouse, true);

	poll_failed = ps2_command(&psmouse->ps2dev, buf,
				  PSMOUSE_CMD_POLL | (psmouse->pktsize << 8)) < 0;

	if (priv->flags & ALPS_PASS)
		alps_passthrough_mode_v2(psmouse, false);

	if (poll_failed || (buf[0] & priv->mask0) != priv->byte0)
		return -1;

	if ((psmouse->badbyte & 0xc8) == 0x08) {
/*
 * Poll the track stick ...
 */
		if (ps2_command(&psmouse->ps2dev, buf, PSMOUSE_CMD_POLL | (3 << 8)))
			return -1;
	}

	memcpy(psmouse->packet, buf, sizeof(buf));
	return 0;
}

static int alps_hw_init_v1_v2(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;

	if ((priv->flags & ALPS_PASS) &&
	    alps_passthrough_mode_v2(psmouse, true)) {
		return -1;
	}

	if (alps_tap_mode(psmouse, true)) {
		psmouse_warn(psmouse, "Failed to enable hardware tapping\n");
		return -1;
	}

	if (alps_absolute_mode_v1_v2(psmouse)) {
		psmouse_err(psmouse, "Failed to enable absolute mode\n");
		return -1;
	}

	if ((priv->flags & ALPS_PASS) &&
	    alps_passthrough_mode_v2(psmouse, false)) {
		return -1;
	}

	/* ALPS needs stream mode, otherwise it won't report any data */
	if (ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_SETSTREAM)) {
		psmouse_err(psmouse, "Failed to enable stream mode\n");
		return -1;
	}

	return 0;
}

static int alps_hw_init_v6(struct psmouse *psmouse)
{
	unsigned char param[2] = {0xC8, 0x14};

	/* Enter passthrough mode to let trackpoint enter 6byte raw mode */
	if (alps_passthrough_mode_v2(psmouse, true))
		return -1;

	if (ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_SETSCALE11) ||
	    ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_SETSCALE11) ||
	    ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_SETSCALE11) ||
	    ps2_command(&psmouse->ps2dev, &param[0], PSMOUSE_CMD_SETRATE) ||
	    ps2_command(&psmouse->ps2dev, &param[1], PSMOUSE_CMD_SETRATE))
		return -1;

	if (alps_passthrough_mode_v2(psmouse, false))
		return -1;

	if (alps_absolute_mode_v6(psmouse)) {
		psmouse_err(psmouse, "Failed to enable absolute mode\n");
		return -1;
	}

	return 0;
}

/*
 * Enable or disable passthrough mode to the trackstick.
 */
static int alps_passthrough_mode_v3(struct psmouse *psmouse,
				    int reg_base, bool enable)
{
	int reg_val, ret = -1;

	if (alps_enter_command_mode(psmouse))
		return -1;

	reg_val = alps_command_mode_read_reg(psmouse, reg_base + 0x0008);
	if (reg_val == -1)
		goto error;

	if (enable)
		reg_val |= 0x01;
	else
		reg_val &= ~0x01;

	ret = __alps_command_mode_write_reg(psmouse, reg_val);

error:
	if (alps_exit_command_mode(psmouse))
		ret = -1;
	return ret;
}

/* Must be in command mode when calling this function */
static int alps_absolute_mode_v3(struct psmouse *psmouse)
{
	int reg_val;

	reg_val = alps_command_mode_read_reg(psmouse, 0x0004);
	if (reg_val == -1)
		return -1;

	reg_val |= 0x06;
	if (__alps_command_mode_write_reg(psmouse, reg_val))
		return -1;

	return 0;
}

static int alps_probe_trackstick_v3(struct psmouse *psmouse, int reg_base)
{
	int ret = -EIO, reg_val;

	if (alps_enter_command_mode(psmouse))
		goto error;

	reg_val = alps_command_mode_read_reg(psmouse, reg_base + 0x08);
	if (reg_val == -1)
		goto error;

	/* bit 7: trackstick is present */
	ret = reg_val & 0x80 ? 0 : -ENODEV;

error:
	alps_exit_command_mode(psmouse);
	return ret;
}

static int alps_setup_trackstick_v3(struct psmouse *psmouse, int reg_base)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	int ret = 0;
	unsigned char param[4];

	if (alps_passthrough_mode_v3(psmouse, reg_base, true))
		return -EIO;

	/*
	 * E7 report for the trackstick
	 *
	 * There have been reports of failures to seem to trace back
	 * to the above trackstick check failing. When these occur
	 * this E7 report fails, so when that happens we continue
	 * with the assumption that there isn't a trackstick after
	 * all.
	 */
	if (alps_rpt_cmd(psmouse, 0, PSMOUSE_CMD_SETSCALE21, param)) {
		psmouse_warn(psmouse, "Failed to initialize trackstick (E7 report failed)\n");
		ret = -ENODEV;
	} else {
		psmouse_dbg(psmouse, "trackstick E7 report: %3ph\n", param);

		/*
		 * Not sure what this does, but it is absolutely
		 * essential. Without it, the touchpad does not
		 * work at all and the trackstick just emits normal
		 * PS/2 packets.
		 */
		if (ps2_command(ps2dev, NULL, PSMOUSE_CMD_SETSCALE11) ||
		    ps2_command(ps2dev, NULL, PSMOUSE_CMD_SETSCALE11) ||
		    ps2_command(ps2dev, NULL, PSMOUSE_CMD_SETSCALE11) ||
		    alps_command_mode_send_nibble(psmouse, 0x9) ||
		    alps_command_mode_send_nibble(psmouse, 0x4)) {
			psmouse_err(psmouse,
				    "Error sending magic E6 sequence\n");
			ret = -EIO;
			goto error;
		}

		/*
		 * This ensures the trackstick packets are in the format
		 * supported by this driver. If bit 1 isn't set the packet
		 * format is different.
		 */
		if (alps_enter_command_mode(psmouse) ||
		    alps_command_mode_write_reg(psmouse,
						reg_base + 0x08, 0x82) ||
		    alps_exit_command_mode(psmouse))
			ret = -EIO;
	}

error:
	if (alps_passthrough_mode_v3(psmouse, reg_base, false))
		ret = -EIO;

	return ret;
}

static int alps_hw_init_v3(struct psmouse *psmouse)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	int reg_val;
	unsigned char param[4];

	reg_val = alps_probe_trackstick_v3(psmouse, ALPS_REG_BASE_PINNACLE);
	if (reg_val == -EIO)
		goto error;

	if (reg_val == 0 &&
	    alps_setup_trackstick_v3(psmouse, ALPS_REG_BASE_PINNACLE) == -EIO)
		goto error;

	if (alps_enter_command_mode(psmouse) ||
	    alps_absolute_mode_v3(psmouse)) {
		psmouse_err(psmouse, "Failed to enter absolute mode\n");
		goto error;
	}

	reg_val = alps_command_mode_read_reg(psmouse, 0x0006);
	if (reg_val == -1)
		goto error;
	if (__alps_command_mode_write_reg(psmouse, reg_val | 0x01))
		goto error;

	reg_val = alps_command_mode_read_reg(psmouse, 0x0007);
	if (reg_val == -1)
		goto error;
	if (__alps_command_mode_write_reg(psmouse, reg_val | 0x01))
		goto error;

	if (alps_command_mode_read_reg(psmouse, 0x0144) == -1)
		goto error;
	if (__alps_command_mode_write_reg(psmouse, 0x04))
		goto error;

	if (alps_command_mode_read_reg(psmouse, 0x0159) == -1)
		goto error;
	if (__alps_command_mode_write_reg(psmouse, 0x03))
		goto error;

	if (alps_command_mode_read_reg(psmouse, 0x0163) == -1)
		goto error;
	if (alps_command_mode_write_reg(psmouse, 0x0163, 0x03))
		goto error;

	if (alps_command_mode_read_reg(psmouse, 0x0162) == -1)
		goto error;
	if (alps_command_mode_write_reg(psmouse, 0x0162, 0x04))
		goto error;

	alps_exit_command_mode(psmouse);

	/* Set rate and enable data reporting */
	param[0] = 0x64;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_ENABLE)) {
		psmouse_err(psmouse, "Failed to enable data reporting\n");
		return -1;
	}

	return 0;

error:
	/*
	 * Leaving the touchpad in command mode will essentially render
	 * it unusable until the machine reboots, so exit it here just
	 * to be safe
	 */
	alps_exit_command_mode(psmouse);
	return -1;
}

static int alps_get_v3_v7_resolution(struct psmouse *psmouse, int reg_pitch)
{
	int reg, x_pitch, y_pitch, x_electrode, y_electrode, x_phys, y_phys;
	struct alps_data *priv = psmouse->private;

	reg = alps_command_mode_read_reg(psmouse, reg_pitch);
	if (reg < 0)
		return reg;

	x_pitch = (char)(reg << 4) >> 4; /* sign extend lower 4 bits */
	x_pitch = 50 + 2 * x_pitch; /* In 0.1 mm units */

	y_pitch = (char)reg >> 4; /* sign extend upper 4 bits */
	y_pitch = 36 + 2 * y_pitch; /* In 0.1 mm units */

	reg = alps_command_mode_read_reg(psmouse, reg_pitch + 1);
	if (reg < 0)
		return reg;

	x_electrode = (char)(reg << 4) >> 4; /* sign extend lower 4 bits */
	x_electrode = 17 + x_electrode;

	y_electrode = (char)reg >> 4; /* sign extend upper 4 bits */
	y_electrode = 13 + y_electrode;

	x_phys = x_pitch * (x_electrode - 1); /* In 0.1 mm units */
	y_phys = y_pitch * (y_electrode - 1); /* In 0.1 mm units */

	priv->x_res = priv->x_max * 10 / x_phys; /* units / mm */
	priv->y_res = priv->y_max * 10 / y_phys; /* units / mm */

	psmouse_dbg(psmouse,
		    "pitch %dx%d num-electrodes %dx%d physical size %dx%d mm res %dx%d\n",
		    x_pitch, y_pitch, x_electrode, y_electrode,
		    x_phys / 10, y_phys / 10, priv->x_res, priv->y_res);

	return 0;
}

static int alps_hw_init_rushmore_v3(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	int reg_val, ret = -1;

	if (priv->flags & ALPS_DUALPOINT) {
		reg_val = alps_setup_trackstick_v3(psmouse,
						   ALPS_REG_BASE_RUSHMORE);
		if (reg_val == -EIO)
			goto error;
	}

	if (alps_enter_command_mode(psmouse) ||
	    alps_command_mode_read_reg(psmouse, 0xc2d9) == -1 ||
	    alps_command_mode_write_reg(psmouse, 0xc2cb, 0x00))
		goto error;

	if (alps_get_v3_v7_resolution(psmouse, 0xc2da))
		goto error;

	reg_val = alps_command_mode_read_reg(psmouse, 0xc2c6);
	if (reg_val == -1)
		goto error;
	if (__alps_command_mode_write_reg(psmouse, reg_val & 0xfd))
		goto error;

	if (alps_command_mode_write_reg(psmouse, 0xc2c9, 0x64))
		goto error;

	/* enter absolute mode */
	reg_val = alps_command_mode_read_reg(psmouse, 0xc2c4);
	if (reg_val == -1)
		goto error;
	if (__alps_command_mode_write_reg(psmouse, reg_val | 0x02))
		goto error;

	alps_exit_command_mode(psmouse);
	return ps2_command(ps2dev, NULL, PSMOUSE_CMD_ENABLE);

error:
	alps_exit_command_mode(psmouse);
	return ret;
}

/* Must be in command mode when calling this function */
static int alps_absolute_mode_v4(struct psmouse *psmouse)
{
	int reg_val;

	reg_val = alps_command_mode_read_reg(psmouse, 0x0004);
	if (reg_val == -1)
		return -1;

	reg_val |= 0x02;
	if (__alps_command_mode_write_reg(psmouse, reg_val))
		return -1;

	return 0;
}

static int alps_hw_init_v4(struct psmouse *psmouse)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[4];

	if (alps_enter_command_mode(psmouse))
		goto error;

	if (alps_absolute_mode_v4(psmouse)) {
		psmouse_err(psmouse, "Failed to enter absolute mode\n");
		goto error;
	}

	if (alps_command_mode_write_reg(psmouse, 0x0007, 0x8c))
		goto error;

	if (alps_command_mode_write_reg(psmouse, 0x0149, 0x03))
		goto error;

	if (alps_command_mode_write_reg(psmouse, 0x0160, 0x03))
		goto error;

	if (alps_command_mode_write_reg(psmouse, 0x017f, 0x15))
		goto error;

	if (alps_command_mode_write_reg(psmouse, 0x0151, 0x01))
		goto error;

	if (alps_command_mode_write_reg(psmouse, 0x0168, 0x03))
		goto error;

	if (alps_command_mode_write_reg(psmouse, 0x014a, 0x03))
		goto error;

	if (alps_command_mode_write_reg(psmouse, 0x0161, 0x03))
		goto error;

	alps_exit_command_mode(psmouse);

	/*
	 * This sequence changes the output from a 9-byte to an
	 * 8-byte format. All the same data seems to be present,
	 * just in a more compact format.
	 */
	param[0] = 0xc8;
	param[1] = 0x64;
	param[2] = 0x50;
	if (ps2_command(ps2dev, &param[0], PSMOUSE_CMD_SETRATE) ||
	    ps2_command(ps2dev, &param[1], PSMOUSE_CMD_SETRATE) ||
	    ps2_command(ps2dev, &param[2], PSMOUSE_CMD_SETRATE) ||
	    ps2_command(ps2dev, param, PSMOUSE_CMD_GETID))
		return -1;

	/* Set rate and enable data reporting */
	param[0] = 0x64;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_ENABLE)) {
		psmouse_err(psmouse, "Failed to enable data reporting\n");
		return -1;
	}

	return 0;

error:
	/*
	 * Leaving the touchpad in command mode will essentially render
	 * it unusable until the machine reboots, so exit it here just
	 * to be safe
	 */
	alps_exit_command_mode(psmouse);
	return -1;
}

static int alps_dolphin_get_device_area(struct psmouse *psmouse,
					struct alps_data *priv)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[4] = {0};
	int num_x_electrode, num_y_electrode;

	if (alps_enter_command_mode(psmouse))
		return -1;

	param[0] = 0x0a;
	if (ps2_command(ps2dev, NULL, PSMOUSE_CMD_RESET_WRAP) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_SETPOLL) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_SETPOLL) ||
	    ps2_command(ps2dev, &param[0], PSMOUSE_CMD_SETRATE) ||
	    ps2_command(ps2dev, &param[0], PSMOUSE_CMD_SETRATE))
		return -1;

	if (ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO))
		return -1;

	/*
	 * Dolphin's sensor line number is not fixed. It can be calculated
	 * by adding the device's register value with DOLPHIN_PROFILE_X/YOFFSET.
	 * Further more, we can get device's x_max and y_max by multiplying
	 * sensor line number with DOLPHIN_COUNT_PER_ELECTRODE.
	 *
	 * e.g. When we get register's sensor_x = 11 & sensor_y = 8,
	 *	real sensor line number X = 11 + 8 = 19, and
	 *	real sensor line number Y = 8 + 1 = 9.
	 *	So, x_max = (19 - 1) * 64 = 1152, and
	 *	    y_max = (9 - 1) * 64 = 512.
	 */
	num_x_electrode = DOLPHIN_PROFILE_XOFFSET + (param[2] & 0x0F);
	num_y_electrode = DOLPHIN_PROFILE_YOFFSET + ((param[2] >> 4) & 0x0F);
	priv->x_bits = num_x_electrode;
	priv->y_bits = num_y_electrode;
	priv->x_max = (num_x_electrode - 1) * DOLPHIN_COUNT_PER_ELECTRODE;
	priv->y_max = (num_y_electrode - 1) * DOLPHIN_COUNT_PER_ELECTRODE;

	if (alps_exit_command_mode(psmouse))
		return -1;

	return 0;
}

static int alps_hw_init_dolphin_v1(struct psmouse *psmouse)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[2];

	/* This is dolphin "v1" as empirically defined by florin9doi */
	param[0] = 0x64;
	param[1] = 0x28;

	if (ps2_command(ps2dev, NULL, PSMOUSE_CMD_SETSTREAM) ||
	    ps2_command(ps2dev, &param[0], PSMOUSE_CMD_SETRATE) ||
	    ps2_command(ps2dev, &param[1], PSMOUSE_CMD_SETRATE))
		return -1;

	return 0;
}

static int alps_hw_init_v7(struct psmouse *psmouse)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	int reg_val, ret = -1;

	if (alps_enter_command_mode(psmouse) ||
	    alps_command_mode_read_reg(psmouse, 0xc2d9) == -1)
		goto error;

	if (alps_get_v3_v7_resolution(psmouse, 0xc397))
		goto error;

	if (alps_command_mode_write_reg(psmouse, 0xc2c9, 0x64))
		goto error;

	reg_val = alps_command_mode_read_reg(psmouse, 0xc2c4);
	if (reg_val == -1)
		goto error;
	if (__alps_command_mode_write_reg(psmouse, reg_val | 0x02))
		goto error;

	alps_exit_command_mode(psmouse);
	return ps2_command(ps2dev, NULL, PSMOUSE_CMD_ENABLE);

error:
	alps_exit_command_mode(psmouse);
	return ret;
}

static int alps_set_protocol(struct psmouse *psmouse,
			     struct alps_data *priv,
			     const struct alps_protocol_info *protocol)
{
	psmouse->private = priv;

	setup_timer(&priv->timer, alps_flush_packet, (unsigned long)psmouse);

	priv->proto_version = protocol->version;
	priv->byte0 = protocol->byte0;
	priv->mask0 = protocol->mask0;
	priv->flags = protocol->flags;

	priv->x_max = 2000;
	priv->y_max = 1400;
	priv->x_bits = 15;
	priv->y_bits = 11;

	switch (priv->proto_version) {
	case ALPS_PROTO_V1:
	case ALPS_PROTO_V2:
		priv->hw_init = alps_hw_init_v1_v2;
		priv->process_packet = alps_process_packet_v1_v2;
		priv->set_abs_params = alps_set_abs_params_st;
		priv->x_max = 1023;
		priv->y_max = 767;
		break;

	case ALPS_PROTO_V3:
		priv->hw_init = alps_hw_init_v3;
		priv->process_packet = alps_process_packet_v3;
		priv->set_abs_params = alps_set_abs_params_mt;
		priv->decode_fields = alps_decode_pinnacle;
		priv->nibble_commands = alps_v3_nibble_commands;
		priv->addr_command = PSMOUSE_CMD_RESET_WRAP;
		break;

	case ALPS_PROTO_V3_RUSHMORE:
		priv->hw_init = alps_hw_init_rushmore_v3;
		priv->process_packet = alps_process_packet_v3;
		priv->set_abs_params = alps_set_abs_params_mt;
		priv->decode_fields = alps_decode_rushmore;
		priv->nibble_commands = alps_v3_nibble_commands;
		priv->addr_command = PSMOUSE_CMD_RESET_WRAP;
		priv->x_bits = 16;
		priv->y_bits = 12;

		if (alps_probe_trackstick_v3(psmouse,
					     ALPS_REG_BASE_RUSHMORE) < 0)
			priv->flags &= ~ALPS_DUALPOINT;

		break;

	case ALPS_PROTO_V4:
		priv->hw_init = alps_hw_init_v4;
		priv->process_packet = alps_process_packet_v4;
		priv->set_abs_params = alps_set_abs_params_mt;
		priv->nibble_commands = alps_v4_nibble_commands;
		priv->addr_command = PSMOUSE_CMD_DISABLE;
		break;

	case ALPS_PROTO_V5:
		priv->hw_init = alps_hw_init_dolphin_v1;
		priv->process_packet = alps_process_touchpad_packet_v3_v5;
		priv->decode_fields = alps_decode_dolphin;
		priv->set_abs_params = alps_set_abs_params_mt;
		priv->nibble_commands = alps_v3_nibble_commands;
		priv->addr_command = PSMOUSE_CMD_RESET_WRAP;
		priv->x_bits = 23;
		priv->y_bits = 12;

		if (alps_dolphin_get_device_area(psmouse, priv))
			return -EIO;

		break;

	case ALPS_PROTO_V6:
		priv->hw_init = alps_hw_init_v6;
		priv->process_packet = alps_process_packet_v6;
		priv->set_abs_params = alps_set_abs_params_st;
		priv->nibble_commands = alps_v6_nibble_commands;
		priv->x_max = 2047;
		priv->y_max = 1535;
		break;

	case ALPS_PROTO_V7:
		priv->hw_init = alps_hw_init_v7;
		priv->process_packet = alps_process_packet_v7;
		priv->decode_fields = alps_decode_packet_v7;
		priv->set_abs_params = alps_set_abs_params_mt;
		priv->nibble_commands = alps_v3_nibble_commands;
		priv->addr_command = PSMOUSE_CMD_RESET_WRAP;
		priv->x_max = 0xfff;
		priv->y_max = 0x7ff;

		if (priv->fw_ver[1] != 0xba)
			priv->flags |= ALPS_BUTTONPAD;

		break;
	}

	return 0;
}

static const struct alps_protocol_info *alps_match_table(unsigned char *e7,
							 unsigned char *ec)
{
	const struct alps_model_info *model;
	int i;

	for (i = 0; i < ARRAY_SIZE(alps_model_data); i++) {
		model = &alps_model_data[i];

		if (!memcmp(e7, model->signature, sizeof(model->signature)) &&
		    (!model->command_mode_resp ||
		     model->command_mode_resp == ec[2])) {

			return &model->protocol_info;
		}
	}

	return NULL;
}

static int alps_identify(struct psmouse *psmouse, struct alps_data *priv)
{
	const struct alps_protocol_info *protocol;
	unsigned char e6[4], e7[4], ec[4];
	int error;

	/*
	 * First try "E6 report".
	 * ALPS should return 0,0,10 or 0,0,100 if no buttons are pressed.
	 * The bits 0-2 of the first byte will be 1s if some buttons are
	 * pressed.
	 */
	if (alps_rpt_cmd(psmouse, PSMOUSE_CMD_SETRES,
			 PSMOUSE_CMD_SETSCALE11, e6))
		return -EIO;

	if ((e6[0] & 0xf8) != 0 || e6[1] != 0 || (e6[2] != 10 && e6[2] != 100))
		return -EINVAL;

	/*
	 * Now get the "E7" and "EC" reports.  These will uniquely identify
	 * most ALPS touchpads.
	 */
	if (alps_rpt_cmd(psmouse, PSMOUSE_CMD_SETRES,
			 PSMOUSE_CMD_SETSCALE21, e7) ||
	    alps_rpt_cmd(psmouse, PSMOUSE_CMD_SETRES,
			 PSMOUSE_CMD_RESET_WRAP, ec) ||
	    alps_exit_command_mode(psmouse))
		return -EIO;

	protocol = alps_match_table(e7, ec);
	if (!protocol) {
		if (e7[0] == 0x73 && e7[1] == 0x03 && e7[2] == 0x50 &&
			   ec[0] == 0x73 && (ec[1] == 0x01 || ec[1] == 0x02)) {
			protocol = &alps_v5_protocol_data;
		} else if (ec[0] == 0x88 &&
			   ((ec[1] & 0xf0) == 0xb0 || (ec[1] & 0xf0) == 0xc0)) {
			protocol = &alps_v7_protocol_data;
		} else if (ec[0] == 0x88 && ec[1] == 0x08) {
			protocol = &alps_v3_rushmore_data;
		} else if (ec[0] == 0x88 && ec[1] == 0x07 &&
			   ec[2] >= 0x90 && ec[2] <= 0x9d) {
			protocol = &alps_v3_protocol_data;
		} else {
			psmouse_dbg(psmouse,
				    "Likely not an ALPS touchpad: E7=%3ph, EC=%3ph\n", e7, ec);
			return -EINVAL;
		}
	}

	if (priv) {
		/* Save the Firmware version */
		memcpy(priv->fw_ver, ec, 3);
		error = alps_set_protocol(psmouse, priv, protocol);
		if (error)
			return error;
	}

	return 0;
}

static int alps_reconnect(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;

	psmouse_reset(psmouse);

	if (alps_identify(psmouse, priv) < 0)
		return -1;

	return priv->hw_init(psmouse);
}

static void alps_disconnect(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;

	psmouse_reset(psmouse);
	del_timer_sync(&priv->timer);
	if (priv->dev2)
		input_unregister_device(priv->dev2);
	if (!IS_ERR_OR_NULL(priv->dev3))
		input_unregister_device(priv->dev3);
	kfree(priv);
}

static void alps_set_abs_params_st(struct alps_data *priv,
				   struct input_dev *dev1)
{
	input_set_abs_params(dev1, ABS_X, 0, priv->x_max, 0, 0);
	input_set_abs_params(dev1, ABS_Y, 0, priv->y_max, 0, 0);
}

static void alps_set_abs_params_mt(struct alps_data *priv,
				   struct input_dev *dev1)
{
	input_set_abs_params(dev1, ABS_MT_POSITION_X, 0, priv->x_max, 0, 0);
	input_set_abs_params(dev1, ABS_MT_POSITION_Y, 0, priv->y_max, 0, 0);

	input_abs_set_res(dev1, ABS_MT_POSITION_X, priv->x_res);
	input_abs_set_res(dev1, ABS_MT_POSITION_Y, priv->y_res);

	input_mt_init_slots(dev1, MAX_TOUCHES, INPUT_MT_POINTER |
		INPUT_MT_DROP_UNUSED | INPUT_MT_TRACK | INPUT_MT_SEMI_MT);

	set_bit(BTN_TOOL_TRIPLETAP, dev1->keybit);
	set_bit(BTN_TOOL_QUADTAP, dev1->keybit);

	/* V7 is real multi-touch */
	if (priv->proto_version == ALPS_PROTO_V7)
		clear_bit(INPUT_PROP_SEMI_MT, dev1->propbit);
}

int alps_init(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	struct input_dev *dev1 = psmouse->dev;
	int error;

	error = priv->hw_init(psmouse);
	if (error)
		goto init_fail;

	/*
	 * Undo part of setup done for us by psmouse core since touchpad
	 * is not a relative device.
	 */
	__clear_bit(EV_REL, dev1->evbit);
	__clear_bit(REL_X, dev1->relbit);
	__clear_bit(REL_Y, dev1->relbit);

	/*
	 * Now set up our capabilities.
	 */
	dev1->evbit[BIT_WORD(EV_KEY)] |= BIT_MASK(EV_KEY);
	dev1->keybit[BIT_WORD(BTN_TOUCH)] |= BIT_MASK(BTN_TOUCH);
	dev1->keybit[BIT_WORD(BTN_TOOL_FINGER)] |= BIT_MASK(BTN_TOOL_FINGER);
	dev1->keybit[BIT_WORD(BTN_LEFT)] |=
		BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_RIGHT);

	dev1->evbit[BIT_WORD(EV_ABS)] |= BIT_MASK(EV_ABS);

	priv->set_abs_params(priv, dev1);
	/* No pressure on V7 */
	if (priv->proto_version != ALPS_PROTO_V7)
		input_set_abs_params(dev1, ABS_PRESSURE, 0, 127, 0, 0);

	if (priv->flags & ALPS_WHEEL) {
		dev1->evbit[BIT_WORD(EV_REL)] |= BIT_MASK(EV_REL);
		dev1->relbit[BIT_WORD(REL_WHEEL)] |= BIT_MASK(REL_WHEEL);
	}

	if (priv->flags & (ALPS_FW_BK_1 | ALPS_FW_BK_2)) {
		dev1->keybit[BIT_WORD(BTN_FORWARD)] |= BIT_MASK(BTN_FORWARD);
		dev1->keybit[BIT_WORD(BTN_BACK)] |= BIT_MASK(BTN_BACK);
	}

	if (priv->flags & ALPS_FOUR_BUTTONS) {
		dev1->keybit[BIT_WORD(BTN_0)] |= BIT_MASK(BTN_0);
		dev1->keybit[BIT_WORD(BTN_1)] |= BIT_MASK(BTN_1);
		dev1->keybit[BIT_WORD(BTN_2)] |= BIT_MASK(BTN_2);
		dev1->keybit[BIT_WORD(BTN_3)] |= BIT_MASK(BTN_3);
	} else if (priv->flags & ALPS_BUTTONPAD) {
		set_bit(INPUT_PROP_BUTTONPAD, dev1->propbit);
		clear_bit(BTN_RIGHT, dev1->keybit);
	} else {
		dev1->keybit[BIT_WORD(BTN_MIDDLE)] |= BIT_MASK(BTN_MIDDLE);
	}

	if (priv->flags & ALPS_DUALPOINT) {
		struct input_dev *dev2;

		dev2 = input_allocate_device();
		if (!dev2) {
			psmouse_err(psmouse,
				    "failed to allocate trackstick device\n");
			error = -ENOMEM;
			goto init_fail;
		}

		snprintf(priv->phys2, sizeof(priv->phys2), "%s/input1",
			 psmouse->ps2dev.serio->phys);
		dev2->phys = priv->phys2;

		/*
		 * format of input device name is: "protocol vendor name"
		 * see function psmouse_switch_protocol() in psmouse-base.c
		 */
		dev2->name = "AlpsPS/2 ALPS DualPoint Stick";

		dev2->id.bustype = BUS_I8042;
		dev2->id.vendor  = 0x0002;
		dev2->id.product = PSMOUSE_ALPS;
		dev2->id.version = priv->proto_version;
		dev2->dev.parent = &psmouse->ps2dev.serio->dev;

		input_set_capability(dev2, EV_REL, REL_X);
		input_set_capability(dev2, EV_REL, REL_Y);
		input_set_capability(dev2, EV_KEY, BTN_LEFT);
		input_set_capability(dev2, EV_KEY, BTN_RIGHT);
		input_set_capability(dev2, EV_KEY, BTN_MIDDLE);

		__set_bit(INPUT_PROP_POINTER, dev2->propbit);
		__set_bit(INPUT_PROP_POINTING_STICK, dev2->propbit);

		error = input_register_device(dev2);
		if (error) {
			psmouse_err(psmouse,
				    "failed to register trackstick device: %d\n",
				    error);
			input_free_device(dev2);
			goto init_fail;
		}

		priv->dev2 = dev2;
	}

	priv->psmouse = psmouse;

	INIT_DELAYED_WORK(&priv->dev3_register_work,
			  alps_register_bare_ps2_mouse);

	psmouse->protocol_handler = alps_process_byte;
	psmouse->poll = alps_poll;
	psmouse->disconnect = alps_disconnect;
	psmouse->reconnect = alps_reconnect;
	psmouse->pktsize = priv->proto_version == ALPS_PROTO_V4 ? 8 : 6;

	/* We are having trouble resyncing ALPS touchpads so disable it for now */
	psmouse->resync_time = 0;

	/* Allow 2 invalid packets without resetting device */
	psmouse->resetafter = psmouse->pktsize * 2;

	return 0;

init_fail:
	psmouse_reset(psmouse);
	/*
	 * Even though we did not allocate psmouse->private we do free
	 * it here.
	 */
	kfree(psmouse->private);
	psmouse->private = NULL;
	return error;
}

int alps_detect(struct psmouse *psmouse, bool set_properties)
{
	struct alps_data *priv;
	int error;

	error = alps_identify(psmouse, NULL);
	if (error)
		return error;

	/*
	 * Reset the device to make sure it is fully operational:
	 * on some laptops, like certain Dell Latitudes, we may
	 * fail to properly detect presence of trackstick if device
	 * has not been reset.
	 */
	psmouse_reset(psmouse);

	priv = kzalloc(sizeof(struct alps_data), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	error = alps_identify(psmouse, priv);
	if (error) {
		kfree(priv);
		return error;
	}

	if (set_properties) {
		psmouse->vendor = "ALPS";
		psmouse->name = priv->flags & ALPS_DUALPOINT ?
				"DualPoint TouchPad" : "GlidePoint";
		psmouse->model = priv->proto_version;
	} else {
		/*
		 * Destroy alps_data structure we allocated earlier since
		 * this was just a "trial run". Otherwise we'll keep it
		 * to be used by alps_init() which has to be called if
		 * we succeed and set_properties is true.
		 */
		kfree(priv);
		psmouse->private = NULL;
	}

	return 0;
}

