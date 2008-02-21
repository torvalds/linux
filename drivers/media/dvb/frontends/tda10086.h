  /*
     Driver for Philips tda10086 DVBS Frontend

     (c) 2006 Andrew de Quincey

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

#ifndef TDA10086_H
#define TDA10086_H

#include <linux/dvb/frontend.h>
#include <linux/firmware.h>

struct tda10086_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* does the "inversion" need inverted? */
	u8 invert;

	/* do we need the diseqc signal with carrier? */
	u8 diseqc_tone;
};

#if defined(CONFIG_DVB_TDA10086) || (defined(CONFIG_DVB_TDA10086_MODULE) && defined(MODULE))
extern struct dvb_frontend* tda10086_attach(const struct tda10086_config* config,
					    struct i2c_adapter* i2c);
#else
static inline struct dvb_frontend* tda10086_attach(const struct tda10086_config* config,
						   struct i2c_adapter* i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __FUNCTION__);
	return NULL;
}
#endif // CONFIG_DVB_TDA10086

#endif // TDA10086_H
