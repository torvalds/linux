/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Silicon Labs Si2146/2147/2148/2157/2158 silicon tuner driver
 *
 * Copyright (C) 2014 Antti Palosaari <crope@iki.fi>
 */

#ifndef SI2157_H
#define SI2157_H

#include <media/media-device.h>
#include <media/dvb_frontend.h>

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
