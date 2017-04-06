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
 */

#ifndef AU6610_H
#define AU6610_H
#include "dvb_usb.h"

#define AU6610_REQ_I2C_WRITE	0x14
#define AU6610_REQ_I2C_READ	0x13
#define AU6610_REQ_USB_WRITE	0x16
#define AU6610_REQ_USB_READ	0x15

#define AU6610_USB_TIMEOUT 1000

#endif
