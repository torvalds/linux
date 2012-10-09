/*
 * DVB USB framework
 *
 * Copyright (C) 2004-6 Patrick Boettcher <patrick.boettcher@desy.de>
 * Copyright (C) 2012 Antti Palosaari <crope@iki.fi>
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
 *    You should have received a copy of the GNU General Public License along
 *    with this program; if not, write to the Free Software Foundation, Inc.,
 *    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef DVB_USB_COMMON_H
#define DVB_USB_COMMON_H

#include "dvb_usb.h"

/* commonly used  methods */
extern int usb_urb_initv2(struct usb_data_stream *stream,
		const struct usb_data_stream_properties *props);
extern int usb_urb_exitv2(struct usb_data_stream *stream);
extern int usb_urb_submitv2(struct usb_data_stream *stream,
		struct usb_data_stream_properties *props);
extern int usb_urb_killv2(struct usb_data_stream *stream);

#endif
