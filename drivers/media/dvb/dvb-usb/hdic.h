/*
 * DVB USB Linux driver for the HDIC receiver
 *
 * Copyright (C) 2011 Metropolia University of Applied Sciences, Electria R&D
 *
 * Author: Antti Palosaari <crope@iki.fi>
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

#ifndef HDIC_H
#define HDIC_H

#define DVB_USB_LOG_PREFIX "hdic"
#include "dvb-usb.h"

#define deb_info(args...) dprintk(dvb_usb_hdic_debug, 0x01, args)
#define deb_rc(args...)   dprintk(dvb_usb_hdic_debug, 0x02, args)
#define deb_xfer(args...) dprintk(dvb_usb_hdic_debug, 0x04, args)
#define deb_reg(args...)  dprintk(dvb_usb_hdic_debug, 0x08, args)
#define deb_i2c(args...)  dprintk(dvb_usb_hdic_debug, 0x10, args)
#define deb_fw(args...)   dprintk(dvb_usb_hdic_debug, 0x20, args)

enum hdic_cmd {
	HDIC_CMD_I2C                     = 0x00,
	HDIC_CMD_CONTROL_STREAM_TRANSFER = 0x03,
	HDIC_CMD_SLEEP_MODE              = 0x09,
	HDIC_CMD_GET_FIRMWARE_VERSION    = 0x0a,
	HDIC_CMD_DEMOD_RESET             = 0x0b,
};


#endif
