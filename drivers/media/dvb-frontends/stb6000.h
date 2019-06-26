/* SPDX-License-Identifier: GPL-2.0-or-later */
  /*
     Driver for ST stb6000 DVBS Silicon tuner

     Copyright (C) 2008 Igor M. Liplianin (liplianin@me.by)


  */

#ifndef __DVB_STB6000_H__
#define __DVB_STB6000_H__

#include <linux/i2c.h>
#include <media/dvb_frontend.h>

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
