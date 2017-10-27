/*
 *    Support for LGDT3302 and LGDT3303 - VSB/QAM
 *
 *    Copyright (C) 2005 Wilson Michaels <wilsonmichaels@earthlink.net>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 */

#ifndef LGDT330X_H
#define LGDT330X_H

#include <linux/dvb/frontend.h>

typedef enum lg_chip_t {
		UNDEFINED,
		LGDT3302,
		LGDT3303
}lg_chip_type;

struct lgdt330x_config
{
	/* The demodulator's i2c address */
	u8 demod_address;

	/* LG demodulator chip LGDT3302 or LGDT3303 */
	lg_chip_type demod_chip;

	/* MPEG hardware interface - 0:parallel 1:serial */
	int serial_mpeg;

	/* PLL interface */
	int (*pll_rf_set) (struct dvb_frontend* fe, int index);

	/* Need to set device param for start_dma */
	int (*set_ts_params)(struct dvb_frontend* fe, int is_punctured);

	/* Flip the polarity of the mpeg data transfer clock using alternate init data
	 * This option applies ONLY to LGDT3303 - 0:disabled (default) 1:enabled */
	int clock_polarity_flip;
};

#if IS_REACHABLE(CONFIG_DVB_LGDT330X)
extern struct dvb_frontend* lgdt330x_attach(const struct lgdt330x_config* config,
					    struct i2c_adapter* i2c);
#else
static inline struct dvb_frontend* lgdt330x_attach(const struct lgdt330x_config* config,
					    struct i2c_adapter* i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif // CONFIG_DVB_LGDT330X

#endif /* LGDT330X_H */
