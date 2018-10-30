/*
    Driver for STV0297 demodulator

    Copyright (C) 2003-2004 Dennis Noermann <dennis.noermann@noernet.de>

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

#ifndef STV0297_H
#define STV0297_H

#include <linux/dvb/frontend.h>
#include <media/dvb_frontend.h>

struct stv0297_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* inittab - array of pairs of values.
	* First of each pair is the register, second is the value.
	* List should be terminated with an 0xff, 0xff pair.
	*/
	u8* inittab;

	/* does the "inversion" need inverted? */
	u8 invert:1;

	/* set to 1 if the device requires an i2c STOP during reading */
	u8 stop_during_read:1;
};

#if IS_REACHABLE(CONFIG_DVB_STV0297)
extern struct dvb_frontend* stv0297_attach(const struct stv0297_config* config,
					   struct i2c_adapter* i2c);
#else
static inline struct dvb_frontend* stv0297_attach(const struct stv0297_config* config,
					   struct i2c_adapter* i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_DVB_STV0297

#endif // STV0297_H
