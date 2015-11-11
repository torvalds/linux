/*
 * ITE Tech IT9137 silicon tuner driver
 *
 *  Copyright (C) 2011 Malcolm Priestley (tvboxspy@gmail.com)
 *  IT9137 Copyright (C) ITE Tech Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.=
 */

#ifndef IT913X_H
#define IT913X_H

#include "dvb_frontend.h"

/*
 * I2C address
 * 0x38, 0x3a, 0x3c, 0x3e
 */
struct it913x_config {
	/*
	 * pointer to DVB frontend
	 */
	struct dvb_frontend *fe;

	/*
	 * chip version
	 * 1 = IT9135 AX
	 * 2 = IT9135 BX
	 */
	unsigned int chip_ver:2;

	/*
	 * tuner role
	 */
#define IT913X_ROLE_SINGLE         0
#define IT913X_ROLE_DUAL_MASTER    1
#define IT913X_ROLE_DUAL_SLAVE     2
	unsigned int role:2;
};

#endif
