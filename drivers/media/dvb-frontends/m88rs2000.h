/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
	Driver for M88RS2000 demodulator


*/

#ifndef M88RS2000_H
#define M88RS2000_H

#include <linux/dvb/frontend.h>
#include <media/dvb_frontend.h>

struct m88rs2000_config {
	/* Demodulator i2c address */
	u8 demod_addr;

	u8 *inittab;

	/* minimum delay before retuning */
	int min_delay_ms;

	int (*set_ts_params)(struct dvb_frontend *, int);
};

enum {
	CALL_IS_SET_FRONTEND = 0x0,
	CALL_IS_READ,
};

#if IS_REACHABLE(CONFIG_DVB_M88RS2000)
extern struct dvb_frontend *m88rs2000_attach(
	const struct m88rs2000_config *config, struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *m88rs2000_attach(
	const struct m88rs2000_config *config, struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_M88RS2000 */

#define RS2000_FE_CRYSTAL_KHZ 27000

enum {
	DEMOD_WRITE = 0x1,
	WRITE_DELAY = 0x10,
};
#endif /* M88RS2000_H */
