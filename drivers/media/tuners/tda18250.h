/*
 * NXP TDA18250BHN silicon tuner driver
 *
 * Copyright (C) 2017 Olli Salonen <olli.salonen@iki.fi>
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

#ifndef TDA18250_H
#define TDA18250_H

#include <linux/kconfig.h>
#include <media/media-device.h>
#include <media/dvb_frontend.h>

#define TDA18250_XTAL_FREQ_16MHZ 0
#define TDA18250_XTAL_FREQ_24MHZ 1
#define TDA18250_XTAL_FREQ_25MHZ 2
#define TDA18250_XTAL_FREQ_27MHZ 3
#define TDA18250_XTAL_FREQ_30MHZ 4
#define TDA18250_XTAL_FREQ_MAX 5

struct tda18250_config {
	u16 if_dvbt_6;
	u16 if_dvbt_7;
	u16 if_dvbt_8;
	u16 if_dvbc_6;
	u16 if_dvbc_8;
	u16 if_atsc;
	u8 xtal_freq;
	bool loopthrough;

	/*
	 * frontend
	 */
	struct dvb_frontend *fe;

#if defined(CONFIG_MEDIA_CONTROLLER)
	struct media_device *mdev;
#endif
};

#endif
