/*
 * DVB USB Linux driver for Alcor Micro AU6610 DVB-T USB2.0.
 *
 * Copyright (C) 2006 Antti Palosaari <crope@iki.fi>
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
 */

#ifndef _DVB_USB_AU6610_H_
#define _DVB_USB_AU6610_H_

#define DVB_USB_LOG_PREFIX "au6610"
#include "dvb-usb.h"

#define deb_info(args...)   dprintk(dvb_usb_au6610_debug, 0x01, args)

#define AU6610_REQ_I2C_WRITE	0x14
#define AU6610_REQ_I2C_READ	0x13
#define AU6610_REQ_USB_WRITE	0x16
#define AU6610_REQ_USB_READ	0x15

#define AU6610_USB_TIMEOUT 1000

#define AU6610_ALTSETTING_COUNT 6
#define AU6610_ALTSETTING       5

#endif
