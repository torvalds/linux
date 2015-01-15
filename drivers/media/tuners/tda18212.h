/*
 * NXP TDA18212HN silicon tuner driver
 *
 * Copyright (C) 2011 Antti Palosaari <crope@iki.fi>
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

#ifndef TDA18212_H
#define TDA18212_H

#include <linux/kconfig.h>
#include "dvb_frontend.h"

struct tda18212_config {
	u16 if_dvbt_6;
	u16 if_dvbt_7;
	u16 if_dvbt_8;
	u16 if_dvbt2_5;
	u16 if_dvbt2_6;
	u16 if_dvbt2_7;
	u16 if_dvbt2_8;
	u16 if_dvbc;
	u16 if_atsc_vsb;
	u16 if_atsc_qam;

	/*
	 * pointer to DVB frontend
	 */
	struct dvb_frontend *fe;
};

#endif
