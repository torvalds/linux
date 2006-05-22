/*
    TDA10021  - Single Chip Cable Channel Receiver driver module
	       used on the the Siemens DVB-C cards

    Copyright (C) 1999 Convergence Integrated Media GmbH <ralph@convergence.de>
    Copyright (C) 2004 Markus Schulz <msc@antzsystem.de>
		   Support for TDA10021

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

#ifndef TDA10021_H
#define TDA10021_H

#include <linux/dvb/frontend.h>

struct tda10021_config
{
	/* the demodulator's i2c address */
	u8 demod_address;
};

extern struct dvb_frontend* tda10021_attach(const struct tda10021_config* config,
					    struct i2c_adapter* i2c, u8 pwm);

extern int tda10021_write_byte(struct dvb_frontend* fe, int reg, int data);

#endif // TDA10021_H
