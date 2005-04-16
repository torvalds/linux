/*
    Frontend-driver for TwinHan DST Frontend

    Copyright (C) 2003 Jamie Honan

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the

    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef DST_H
#define DST_H

#include <linux/dvb/frontend.h>
#include <linux/device.h>
#include "bt878.h"

struct dst_config
{
	/* the demodulator's i2c address */
	u8 demod_address;
};

extern struct dvb_frontend* dst_attach(const struct dst_config* config,
				       struct i2c_adapter* i2c,
				       struct bt878 *bt);

#endif // DST_H
