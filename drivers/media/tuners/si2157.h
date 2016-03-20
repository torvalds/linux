/*
 * Silicon Labs Si2146/2147/2148/2157/2158 silicon tuner driver
 *
 * Copyright (C) 2014 Antti Palosaari <crope@iki.fi>
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

#ifndef SI2157_H
#define SI2157_H

#include <linux/kconfig.h>
#include <media/media-device.h>
#include "dvb_frontend.h"

/*
 * I2C address
 * 0x60
 */
struct si2157_config {
	/*
	 * frontend
	 */
	struct dvb_frontend *fe;

#if defined(CONFIG_MEDIA_CONTROLLER)
	struct media_device *mdev;
#endif

	/*
	 * Spectral Inversion
	 */
	bool inversion;

	/*
	 * Port selection
	 * Select the RF interface to use (pins 9+11 or 12+13)
	 */
	u8 if_port;
};

#endif
