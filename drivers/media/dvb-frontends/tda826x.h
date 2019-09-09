/* SPDX-License-Identifier: GPL-2.0-or-later */
  /*
     Driver for Philips tda8262/tda8263 DVBS Silicon tuners

     (c) 2006 Andrew de Quincey


  */

#ifndef __DVB_TDA826X_H__
#define __DVB_TDA826X_H__

#include <linux/i2c.h>
#include <media/dvb_frontend.h>

/**
 * Attach a tda826x tuner to the supplied frontend structure.
 *
 * @fe: Frontend to attach to.
 * @addr: i2c address of the tuner.
 * @i2c: i2c adapter to use.
 * @has_loopthrough: Set to 1 if the card has a loopthrough RF connector.
 *
 * return: FE pointer on success, NULL on failure.
 */
#if IS_REACHABLE(CONFIG_DVB_TDA826X)
extern struct dvb_frontend* tda826x_attach(struct dvb_frontend *fe, int addr,
					   struct i2c_adapter *i2c,
					   int has_loopthrough);
#else
static inline struct dvb_frontend* tda826x_attach(struct dvb_frontend *fe,
						  int addr,
						  struct i2c_adapter *i2c,
						  int has_loopthrough)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_DVB_TDA826X

#endif // __DVB_TDA826X_H__
