/* ----------------------------------------------------------------------------
 * touchkit_ps2.c  --  Driver for eGalax TouchKit PS/2 Touchscreens
 *
 * Copyright (C) 2005 by Stefan Lucke
 * Copyright (C) 2004 by Daniel Ritz
 * Copyright (C) by Todd E. Johnson (mtouchusb.c)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Based upon touchkitusb.c
 *
 * Vendor documentation is available at:
 * http://home.eeti.com.tw/web20/drivers/Software%20Programming%20Guide_v2.0.pdf 
 */

#include <linux/kernel.h>

#include <linux/input.h>
#include <linux/serio.h>
#include <linux/libps2.h>

#include "psmouse.h"
#include "touchkit_ps2.h"

#define TOUCHKIT_MAX_XC			0x07ff
#define TOUCHKIT_MAX_YC			0x07ff

#define TOUCHKIT_CMD			0x0a
#define TOUCHKIT_CMD_LENGTH		1

#define TOUCHKIT_CMD_ACTIVE		'A'
#define TOUCHKIT_CMD_FIRMWARE_VERSION	'D'
#define TOUCHKIT_CMD_CONTROLLER_TYPE	'E'

#define TOUCHKIT_SEND_PARMS(s, r, c)	((s) << 12 | (r) << 8 | (c))

#define TOUCHKIT_GET_TOUCHED(packet)	(((packet)[0]) & 0x01)
#define TOUCHKIT_GET_X(packet)		(((packet)[1] << 7) | (packet)[2])
#define TOUCHKIT_GET_Y(packet)		(((packet)[3] << 7) | (packet)[4])

static psmouse_ret_t touchkit_ps2_process_byte(struct psmouse *psmouse)
{
	unsigned char *packet = psmouse->packet;
	struct input_dev *dev = psmouse->dev;

	if (psmouse->pktcnt != 5)
		return PSMOUSE_GOOD_DATA;

	input_report_abs(dev, ABS_X, TOUCHKIT_GET_X(packet));
	input_report_abs(dev, ABS_Y, TOUCHKIT_GET_Y(packet));
	input_report_key(dev, BTN_TOUCH, TOUCHKIT_GET_TOUCHED(packet));
	input_sync(dev);

	return PSMOUSE_FULL_PACKET;
}

int touchkit_ps2_detect(struct psmouse *psmouse, bool set_properties)
{
	struct input_dev *dev = psmouse->dev;
	unsigned char param[3];
	int command;

	param[0] = TOUCHKIT_CMD_LENGTH;
	param[1] = TOUCHKIT_CMD_ACTIVE;
	command = TOUCHKIT_SEND_PARMS(2, 3, TOUCHKIT_CMD);

	if (ps2_command(&psmouse->ps2dev, param, command))
		return -ENODEV;

	if (param[0] != TOUCHKIT_CMD || param[1] != 0x01 ||
	    param[2] != TOUCHKIT_CMD_ACTIVE)
		return -ENODEV;

	if (set_properties) {
		dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
		dev->keybit[BIT_WORD(BTN_MOUSE)] = 0;
		dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
		input_set_abs_params(dev, ABS_X, 0, TOUCHKIT_MAX_XC, 0, 0);
		input_set_abs_params(dev, ABS_Y, 0, TOUCHKIT_MAX_YC, 0, 0);

		psmouse->vendor = "eGalax";
		psmouse->name = "Touchscreen";
		psmouse->protocol_handler = touchkit_ps2_process_byte;
		psmouse->pktsize = 5;
	}

	return 0;
}
