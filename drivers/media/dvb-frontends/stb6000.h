  /*
     Driver for ST stb6000 DVBS Silicon tuner

     Copyright (C) 2008 Igor M. Liplianin (liplianin@me.by)

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

#ifndef __DVB_STB6000_H__
#define __DVB_STB6000_H__

#include <linux/i2c.h>
#include "dvb_frontend.h"

#if IS_REACHABLE(CONFIG_DVB_STB6000)
/**
 * Attach a stb6000 tuner to the supplied frontend structure.
 *
 * @fe: Frontend to attach to.
 * @addr: i2c address of the tuner.
 * @i2c: i2c adapter to use.
 *
 * return: FE pointer on success, NULL on failure.
 */
extern struct dvb_frontend *stb6000_attach(struct dvb_frontend *fe, int addr,
					   struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *stb6000_attach(struct dvb_frontend *fe,
						  int addr,
						  struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_STB6000 */

#endif /* __DVB_STB6000_H__ */
