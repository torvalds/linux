/**
 * Driver for Sharp IX2505V (marked B0017) DVB-S silicon tuner
 *
 * Copyright (C) 2010 Malcolm Priestley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef DVB_IX2505V_H
#define DVB_IX2505V_H

#include <linux/i2c.h>
#include "dvb_frontend.h"

/**
 * Attach a ix2505v tuner to the supplied frontend structure.
 *
 * @param fe Frontend to attach to.
 * @param config ix2505v_config structure
 * @return FE pointer on success, NULL on failure.
 */

struct ix2505v_config {
	u8 tuner_address;

	/*Baseband AMP gain control 0/1=0dB(default) 2=-2bB 3=-4dB */
	u8 tuner_gain;

	/*Charge pump output +/- 0=120 1=260 2=555 3=1200(default) */
	u8 tuner_chargepump;

	/* delay after tune */
	int min_delay_ms;

	/* disables reads*/
	u8 tuner_write_only;

};

#if IS_REACHABLE(CONFIG_DVB_IX2505V)
extern struct dvb_frontend *ix2505v_attach(struct dvb_frontend *fe,
	const struct ix2505v_config *config, struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *ix2505v_attach(struct dvb_frontend *fe,
	const struct ix2505v_config *config, struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* DVB_IX2505V_H */
