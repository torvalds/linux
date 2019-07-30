/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Driver for Sharp IX2505V (marked B0017) DVB-S silicon tuner
 *
 * Copyright (C) 2010 Malcolm Priestley
 */

#ifndef DVB_IX2505V_H
#define DVB_IX2505V_H

#include <linux/i2c.h>
#include <media/dvb_frontend.h>

/**
 * struct ix2505v_config - ix2505 attachment configuration
 *
 * @tuner_address: tuner address
 * @tuner_gain: Baseband AMP gain control 0/1=0dB(default) 2=-2bB 3=-4dB
 * @tuner_chargepump: Charge pump output +/- 0=120 1=260 2=555 3=1200(default)
 * @min_delay_ms: delay after tune
 * @tuner_write_only: disables reads
 */
struct ix2505v_config {
	u8 tuner_address;
	u8 tuner_gain;
	u8 tuner_chargepump;
	int min_delay_ms;
	u8 tuner_write_only;

};

#if IS_REACHABLE(CONFIG_DVB_IX2505V)
/**
 * Attach a ix2505v tuner to the supplied frontend structure.
 *
 * @fe: Frontend to attach to.
 * @config: pointer to &struct ix2505v_config
 * @i2c: pointer to &struct i2c_adapter.
 *
 * return: FE pointer on success, NULL on failure.
 */
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
