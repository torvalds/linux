/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * E3C EC168 DVB USB driver
 *
 * Copyright (C) 2009 Antti Palosaari <crope@iki.fi>
 */

#ifndef EC168_H
#define EC168_H

#include "dvb_usb.h"

#define EC168_USB_TIMEOUT 1000
#define EC168_FIRMWARE "dvb-usb-ec168.fw"

struct ec168_req {
	u8  cmd;       /* [1] */
	u16 value;     /* [2|3] */
	u16 index;     /* [4|5] */
	u16 size;      /* [6|7] */
	u8  *data;
};

enum ec168_cmd {
	DOWNLOAD_FIRMWARE    = 0x00,
	CONFIG               = 0x01,
	DEMOD_RW             = 0x03,
	GPIO                 = 0x04,
	STREAMING_CTRL       = 0x10,
	READ_I2C             = 0x20,
	WRITE_I2C            = 0x21,
	HID_DOWNLOAD         = 0x30,
	GET_CONFIG,
	SET_CONFIG,
	READ_DEMOD,
	WRITE_DEMOD,
};

#endif
