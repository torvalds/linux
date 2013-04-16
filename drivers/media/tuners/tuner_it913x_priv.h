/*
 * ITE Tech IT9137 silicon tuner driver
 *
 *  Copyright (C) 2011 Malcolm Priestley (tvboxspy@gmail.com)
 *  IT9137 Copyright (C) ITE Tech Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.=
 */

#ifndef IT913X_PRIV_H
#define IT913X_PRIV_H

#include "tuner_it913x.h"
#include "af9033.h"

#define PRO_LINK		0x0
#define PRO_DMOD		0x1
#define TRIGGER_OFSM		0x0000

struct it913xset {	u32 pro;
			u32 address;
			u8 reg[15];
			u8 count;
};

/* Tuner setting scripts (still keeping it9137) */
static struct it913xset it9137_tuner_off[] = {
	{PRO_DMOD, 0xfba8, {0x01}, 0x01}, /* Tuner Clock Off  */
	{PRO_DMOD, 0xec40, {0x00}, 0x01}, /* Power Down Tuner */
	{PRO_DMOD, 0xec02, {0x3f, 0x1f, 0x3f, 0x3f}, 0x04},
	{PRO_DMOD, 0xec06, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00}, 0x0c},
	{PRO_DMOD, 0xec12, {0x00, 0x00, 0x00, 0x00}, 0x04},
	{PRO_DMOD, 0xec17, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00}, 0x09},
	{PRO_DMOD, 0xec22, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00}, 0x0a},
	{PRO_DMOD, 0xec20, {0x00}, 0x01},
	{PRO_DMOD, 0xec3f, {0x01}, 0x01},
	{0xff, 0x0000, {0x00}, 0x00}, /* Terminating Entry */
};

static struct it913xset set_it9135_template[] = {
	{PRO_DMOD, 0xee06, {0x00}, 0x01},
	{PRO_DMOD, 0xec56, {0x00}, 0x01},
	{PRO_DMOD, 0xec4c, {0x00}, 0x01},
	{PRO_DMOD, 0xec4d, {0x00}, 0x01},
	{PRO_DMOD, 0xec4e, {0x00}, 0x01},
	{PRO_DMOD, 0x011e, {0x00}, 0x01}, /* Older Devices */
	{PRO_DMOD, 0x011f, {0x00}, 0x01},
	{0xff, 0x0000, {0x00}, 0x00}, /* Terminating Entry */
};

static struct it913xset set_it9137_template[] = {
	{PRO_DMOD, 0xee06, {0x00}, 0x01},
	{PRO_DMOD, 0xec56, {0x00}, 0x01},
	{PRO_DMOD, 0xec4c, {0x00}, 0x01},
	{PRO_DMOD, 0xec4d, {0x00}, 0x01},
	{PRO_DMOD, 0xec4e, {0x00}, 0x01},
	{PRO_DMOD, 0xec4f, {0x00}, 0x01},
	{PRO_DMOD, 0xec50, {0x00}, 0x01},
	{0xff, 0x0000, {0x00}, 0x00}, /* Terminating Entry */
};

#endif
