/* SPDX-License-Identifier: GPL-2.0-or-later */
  /*
     Driver for Philips tda10086 DVBS Frontend

     (c) 2006 Andrew de Quincey


   */

#ifndef TDA10086_H
#define TDA10086_H

#include <linux/dvb/frontend.h>
#include <linux/firmware.h>

enum tda10086_xtal {
	TDA10086_XTAL_16M,
	TDA10086_XTAL_4M
};

struct tda10086_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* does the "inversion" need inverted? */
	u8 invert;

	/* do we need the diseqc signal with carrier? */
	u8 diseqc_tone;

	/* frequency of the reference xtal */
	enum tda10086_xtal xtal_freq;
};

#if IS_REACHABLE(CONFIG_DVB_TDA10086)
extern struct dvb_frontend* tda10086_attach(const struct tda10086_config* config,
					    struct i2c_adapter* i2c);
#else
static inline struct dvb_frontend* tda10086_attach(const struct tda10086_config* config,
						   struct i2c_adapter* i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_TDA10086 */

#endif /* TDA10086_H */
