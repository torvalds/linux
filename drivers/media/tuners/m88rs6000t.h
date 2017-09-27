/*
 * Driver for the internal tuner of Montage M88RS6000
 *
 * Copyright (C) 2014 Max nibble <nibble.max@gmail.com>
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

#ifndef _M88RS6000T_H_
#define _M88RS6000T_H_

#include "dvb_frontend.h"

struct m88rs6000t_config {
	/*
	 * pointer to DVB frontend
	 */
	struct dvb_frontend *fe;
};

#endif
