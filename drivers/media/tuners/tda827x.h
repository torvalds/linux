  /*
     DVB Driver for Philips tda827x / tda827xa Silicon tuners

     (c) 2005 Hartmut Hackmann
     (c) 2007 Michael Krufky

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

#ifndef __DVB_TDA827X_H__
#define __DVB_TDA827X_H__

#include <linux/i2c.h>
#include "dvb_frontend.h"

struct tda827x_config
{
	/* saa7134 - provided callbacks */
	int (*init) (struct dvb_frontend *fe);
	int (*sleep) (struct dvb_frontend *fe);

	/* interface to tda829x driver */
	unsigned int config;
	int 	     switch_addr;

	void (*agcf)(struct dvb_frontend *fe);
};


/**
 * Attach a tda827x tuner to the supplied frontend structure.
 *
 * @param fe Frontend to attach to.
 * @param addr i2c address of the tuner.
 * @param i2c i2c adapter to use.
 * @param cfg optional callback function pointers.
 * @return FE pointer on success, NULL on failure.
 */
#if IS_ENABLED(CONFIG_MEDIA_TUNER_TDA827X)
extern struct dvb_frontend* tda827x_attach(struct dvb_frontend *fe, int addr,
					   struct i2c_adapter *i2c,
					   struct tda827x_config *cfg);
#else
static inline struct dvb_frontend* tda827x_attach(struct dvb_frontend *fe,
						  int addr,
						  struct i2c_adapter *i2c,
						  struct tda827x_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_MEDIA_TUNER_TDA827X

#endif // __DVB_TDA827X_H__
