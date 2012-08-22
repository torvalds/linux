/*
 * Intel CE6230 DVB USB driver
 *
 * Copyright (C) 2009 Antti Palosaari <crope@iki.fi>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef CE6230_H
#define CE6230_H

#include "dvb_usb.h"
#include "zl10353.h"
#include "mxl5005s.h"

#define CE6230_USB_TIMEOUT 1000

struct usb_req {
	u8  cmd;       /* [1] */
	u16 value;     /* [2|3] */
	u16 index;     /* [4|5] */
	u16 data_len;  /* [6|7] */
	u8  *data;
};

enum ce6230_cmd {
	CONFIG_READ          = 0xd0, /* rd 0 (unclear) */
	UNKNOWN_WRITE        = 0xc7, /* wr 7 (unclear) */
	I2C_READ             = 0xd9, /* rd 9 (unclear) */
	I2C_WRITE            = 0xca, /* wr a */
	DEMOD_READ           = 0xdb, /* rd b */
	DEMOD_WRITE          = 0xcc, /* wr c */
	REG_READ             = 0xde, /* rd e */
	REG_WRITE            = 0xcf, /* wr f */
};

#endif
