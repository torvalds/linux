/*
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

#ifndef __TDA8290_H__
#define __TDA8290_H__

#include <linux/i2c.h>
#include <media/dvb_frontend.h>
#include "tda18271.h"

enum tda8290_lna {
	TDA8290_LNA_OFF = 0,
	TDA8290_LNA_GP0_HIGH_ON = 1,
	TDA8290_LNA_GP0_HIGH_OFF = 2,
	TDA8290_LNA_ON_BRIDGE = 3,
};

struct tda829x_config {
	enum tda8290_lna lna_cfg;

	unsigned int probe_tuner:1;
#define TDA829X_PROBE_TUNER 0
#define TDA829X_DONT_PROBE  1
	unsigned int no_i2c_gate:1;
	struct tda18271_std_map *tda18271_std_map;
};

#if IS_REACHABLE(CONFIG_MEDIA_TUNER_TDA8290)
extern int tda829x_probe(struct i2c_adapter *i2c_adap, u8 i2c_addr);

extern struct dvb_frontend *tda829x_attach(struct dvb_frontend *fe,
					   struct i2c_adapter *i2c_adap,
					   u8 i2c_addr,
					   struct tda829x_config *cfg);
#else
static inline int tda829x_probe(struct i2c_adapter *i2c_adap, u8 i2c_addr)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return -EINVAL;
}

static inline struct dvb_frontend *tda829x_attach(struct dvb_frontend *fe,
						  struct i2c_adapter *i2c_adap,
						  u8 i2c_addr,
						  struct tda829x_config *cfg)
{
	printk(KERN_INFO "%s: not probed - driver disabled by Kconfig\n",
	       __func__);
	return NULL;
}
#endif

#endif /* __TDA8290_H__ */
