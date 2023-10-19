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

/**
 * struct si2157_config - configuration parameters for si2157
 *
 * @fe:
 *	frontend returned by driver
 * @mdev:
 *	media device returned by driver
 * @inversion:
 *	spectral inversion
 * @dont_load_firmware:
 *	Instead of uploading a new firmware, use the existing one
 * @if_port:
 *	Port selection
 *	Select the RF interface to use (pins 9+11 or 12+13)
 *
 * Note:
 *	The I2C address of this demod is 0x60.
 */
struct si2157_config {
	struct dvb_frontend *fe;

#if defined(CONFIG_MEDIA_CONTROLLER)
	struct media_device *mdev;
#endif

	unsigned int inversion:1;
	unsigned int dont_load_firmware:1;

	u8 if_port;
};

#endif
