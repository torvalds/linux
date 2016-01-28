/*
 * BYD TouchPad PS/2 mouse driver
 *
 * Copyright (C) 2015 Chris Diamand <chris@diamand.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/input.h>
#include <linux/libps2.h>
#include <linux/serio.h>

#include "psmouse.h"
#include "byd.h"

#define PS2_Y_OVERFLOW	BIT_MASK(7)
#define PS2_X_OVERFLOW	BIT_MASK(6)
#define PS2_Y_SIGN	BIT_MASK(5)
#define PS2_X_SIGN	BIT_MASK(4)
#define PS2_ALWAYS_1	BIT_MASK(3)
#define PS2_MIDDLE	BIT_MASK(2)
#define PS2_RIGHT	BIT_MASK(1)
#define PS2_LEFT	BIT_MASK(0)

/*
 * The touchpad reports gestures in the last byte of each packet. It can take
 * any of the following values:
 */

/* One-finger scrolling in one of the edge scroll zones. */
#define BYD_SCROLLUP		0xCA
#define BYD_SCROLLDOWN		0x36
#define BYD_SCROLLLEFT		0xCB
#define BYD_SCROLLRIGHT		0x35
/* Two-finger scrolling. */
#define BYD_2DOWN		0x2B
#define BYD_2UP			0xD5
#define BYD_2LEFT		0xD6
#define BYD_2RIGHT		0x2A
/* Pinching in or out. */
#define BYD_ZOOMOUT		0xD8
#define BYD_ZOOMIN		0x28
/* Three-finger swipe. */
#define BYD_3UP			0xD3
#define BYD_3DOWN		0x2D
#define BYD_3LEFT		0xD4
#define BYD_3RIGHT		0x2C
/* Four-finger swipe. */
#define BYD_4UP			0xCD
#define BYD_4DOWN		0x33

int byd_detect(struct psmouse *psmouse, bool set_properties)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[4];

	param[0] = 0x03;
	param[1] = 0x00;
	param[2] = 0x00;
	param[3] = 0x00;

	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES))
		return -1;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES))
		return -1;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES))
		return -1;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES))
		return -1;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO))
		return -1;

	if (param[1] != 0x03 || param[2] != 0x64)
		return -ENODEV;

	psmouse_dbg(psmouse, "BYD touchpad detected\n");

	if (set_properties) {
		psmouse->vendor = "BYD";
		psmouse->name = "TouchPad";
	}

	return 0;
}

static psmouse_ret_t byd_process_byte(struct psmouse *psmouse)
{
	struct input_dev *dev = psmouse->dev;
	u8 *pkt = psmouse->packet;

	if (psmouse->pktcnt > 0 && !(pkt[0] & PS2_ALWAYS_1)) {
		psmouse_warn(psmouse, "Always_1 bit not 1. pkt[0] = %02x\n",
			     pkt[0]);
		return PSMOUSE_BAD_DATA;
	}

	if (psmouse->pktcnt < psmouse->pktsize)
		return PSMOUSE_GOOD_DATA;

	/* Otherwise, a full packet has been received */
	switch (pkt[3]) {
	case 0: {
		/* Standard packet */
		/* Sign-extend if a sign bit is set. */
		unsigned int signx = pkt[0] & PS2_X_SIGN ? ~0xFF : 0;
		unsigned int signy = pkt[0] & PS2_Y_SIGN ? ~0xFF : 0;
		int dx = signx | (int) pkt[1];
		int dy = signy | (int) pkt[2];

		input_report_rel(psmouse->dev, REL_X, dx);
		input_report_rel(psmouse->dev, REL_Y, -dy);

		input_report_key(psmouse->dev, BTN_LEFT, pkt[0] & PS2_LEFT);
		input_report_key(psmouse->dev, BTN_RIGHT, pkt[0] & PS2_RIGHT);
		input_report_key(psmouse->dev, BTN_MIDDLE, pkt[0] & PS2_MIDDLE);
		break;
	}

	case BYD_SCROLLDOWN:
	case BYD_2DOWN:
		input_report_rel(dev, REL_WHEEL, -1);
		break;

	case BYD_SCROLLUP:
	case BYD_2UP:
		input_report_rel(dev, REL_WHEEL, 1);
		break;

	case BYD_SCROLLLEFT:
	case BYD_2LEFT:
		input_report_rel(dev, REL_HWHEEL, -1);
		break;

	case BYD_SCROLLRIGHT:
	case BYD_2RIGHT:
		input_report_rel(dev, REL_HWHEEL, 1);
		break;

	case BYD_ZOOMOUT:
	case BYD_ZOOMIN:
	case BYD_3UP:
	case BYD_3DOWN:
	case BYD_3LEFT:
	case BYD_3RIGHT:
	case BYD_4UP:
	case BYD_4DOWN:
		break;

	default:
		psmouse_warn(psmouse,
			     "Unrecognized Z: pkt = %02x %02x %02x %02x\n",
			     psmouse->packet[0], psmouse->packet[1],
			     psmouse->packet[2], psmouse->packet[3]);
		return PSMOUSE_BAD_DATA;
	}

	input_sync(dev);

	return PSMOUSE_FULL_PACKET;
}

/* Send a sequence of bytes, where each is ACKed before the next is sent. */
static int byd_send_sequence(struct psmouse *psmouse, const u8 *seq, size_t len)
{
	unsigned int i;

	for (i = 0; i < len; ++i) {
		if (ps2_command(&psmouse->ps2dev, NULL, seq[i]))
			return -1;
	}
	return 0;
}

/* Keep scrolling after fingers are removed. */
#define SCROLL_INERTIAL		0x01
#define SCROLL_NO_INERTIAL	0x02

/* Clicking can be done by tapping or pressing. */
#define CLICK_BOTH		0x01
/* Clicking can only be done by pressing. */
#define CLICK_PRESS_ONLY	0x02

static int byd_enable(struct psmouse *psmouse)
{
	const u8 seq1[] = { 0xE2, 0x00, 0xE0, 0x02, 0xE0 };
	const u8 seq2[] = {
		0xD3, 0x01,
		0xD0, 0x00,
		0xD0, 0x04,
		/* Whether clicking is done by tapping or pressing. */
		0xD4, CLICK_PRESS_ONLY,
		0xD5, 0x01,
		0xD7, 0x03,
		/* Vertical and horizontal one-finger scroll zone inertia. */
		0xD8, SCROLL_INERTIAL,
		0xDA, 0x05,
		0xDB, 0x02,
		0xE4, 0x05,
		0xD6, 0x01,
		0xDE, 0x04,
		0xE3, 0x01,
		0xCF, 0x00,
		0xD2, 0x03,
		/* Vertical and horizontal two-finger scrolling inertia. */
		0xE5, SCROLL_INERTIAL,
		0xD9, 0x02,
		0xD9, 0x07,
		0xDC, 0x03,
		0xDD, 0x03,
		0xDF, 0x03,
		0xE1, 0x03,
		0xD1, 0x00,
		0xCE, 0x00,
		0xCC, 0x00,
		0xE0, 0x00,
		0xE2, 0x01
	};
	u8 param[4];

	if (byd_send_sequence(psmouse, seq1, ARRAY_SIZE(seq1)))
		return -1;

	/* Send a 0x01 command, which should return 4 bytes. */
	if (ps2_command(&psmouse->ps2dev, param, 0x0401))
		return -1;

	if (byd_send_sequence(psmouse, seq2, ARRAY_SIZE(seq2)))
		return -1;

	return 0;
}

/*
 * Send the set of PS/2 commands required to make it identify as an
 * intellimouse with 4-byte instead of 3-byte packets.
 */
static int byd_send_intellimouse_sequence(struct psmouse *psmouse)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	u8 param[4];
	int i;
	const struct {
		u16 command;
		u8 arg;
	} seq[] = {
		{ PSMOUSE_CMD_RESET_BAT, 0 },
		{ PSMOUSE_CMD_RESET_BAT, 0 },
		{ PSMOUSE_CMD_GETID, 0 },
		{ PSMOUSE_CMD_SETSCALE11, 0 },
		{ PSMOUSE_CMD_SETSCALE11, 0 },
		{ PSMOUSE_CMD_SETSCALE11, 0 },
		{ PSMOUSE_CMD_GETINFO, 0 },
		{ PSMOUSE_CMD_SETRES, 0x03 },
		{ PSMOUSE_CMD_SETRATE, 0xC8 },
		{ PSMOUSE_CMD_SETRATE, 0x64 },
		{ PSMOUSE_CMD_SETRATE, 0x50 },
		{ PSMOUSE_CMD_GETID, 0 },
		{ PSMOUSE_CMD_SETRATE, 0xC8 },
		{ PSMOUSE_CMD_SETRATE, 0xC8 },
		{ PSMOUSE_CMD_SETRATE, 0x50 },
		{ PSMOUSE_CMD_GETID, 0 },
		{ PSMOUSE_CMD_SETRATE, 0x64 },
		{ PSMOUSE_CMD_SETRES, 0x03 },
		{ PSMOUSE_CMD_ENABLE, 0 }
	};

	memset(param, 0, sizeof(param));
	for (i = 0; i < ARRAY_SIZE(seq); ++i) {
		param[0] = seq[i].arg;
		if (ps2_command(ps2dev, param, seq[i].command))
			return -1;
	}

	return 0;
}

static int byd_reset_touchpad(struct psmouse *psmouse)
{
	if (byd_send_intellimouse_sequence(psmouse))
		return -EIO;

	if (byd_enable(psmouse))
		return -EIO;

	return 0;
}

static int byd_reconnect(struct psmouse *psmouse)
{
	int retry = 0, error = 0;

	psmouse_dbg(psmouse, "Reconnect\n");
	do {
		psmouse_reset(psmouse);
		if (retry)
			ssleep(1);
		error = byd_detect(psmouse, 0);
	} while (error && ++retry < 3);

	if (error)
		return error;

	psmouse_dbg(psmouse, "Reconnected after %d attempts\n", retry);

	error = byd_reset_touchpad(psmouse);
	if (error) {
		psmouse_err(psmouse, "Unable to initialize device\n");
		return error;
	}

	return 0;
}

int byd_init(struct psmouse *psmouse)
{
	struct input_dev *dev = psmouse->dev;

	if (psmouse_reset(psmouse))
		return -EIO;

	if (byd_reset_touchpad(psmouse))
		return -EIO;

	psmouse->reconnect = byd_reconnect;
	psmouse->protocol_handler = byd_process_byte;
	psmouse->pktsize = 4;
	psmouse->resync_time = 0;

	__set_bit(BTN_MIDDLE, dev->keybit);
	__set_bit(REL_WHEEL, dev->relbit);
	__set_bit(REL_HWHEEL, dev->relbit);

	return 0;
}
