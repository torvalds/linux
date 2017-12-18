/* SPDX-License-Identifier: GPL-2.0 */
#ifndef SI21XX_H
#define SI21XX_H

#include <linux/dvb/frontend.h>
#include "dvb_frontend.h"

struct si21xx_config {
	/* the demodulator's i2c address */
	u8 demod_address;

	/* minimum delay before retuning */
	int min_delay_ms;
};

#if IS_REACHABLE(CONFIG_DVB_SI21XX)
extern struct dvb_frontend *si21xx_attach(const struct si21xx_config *config,
						struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *si21xx_attach(
		const struct si21xx_config *config, struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

static inline int si21xx_writeregister(struct dvb_frontend *fe, u8 reg, u8 val)
{
	int r = 0;
	u8 buf[] = {reg, val};
	if (fe->ops.write)
		r = fe->ops.write(fe, buf, 2);
	return r;
}

#endif
