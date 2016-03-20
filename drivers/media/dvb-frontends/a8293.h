/*
 * Allegro A8293 SEC driver
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
 */

#ifndef A8293_H
#define A8293_H

#include "dvb_frontend.h"

/*
 * I2C address
 * 0x08, 0x09, 0x0a, 0x0b
 */

/**
 * struct a8293_platform_data - Platform data for the a8293 driver
 * @dvb_frontend: DVB frontend.
 */
struct a8293_platform_data {
	struct dvb_frontend *dvb_frontend;
};

#endif /* A8293_H */
