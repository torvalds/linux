/*
 *    Support for LGDT3306A - 8VSB/QAM-B
 *
 *    Copyright (C) 2013,2014 Fred Richter <frichter@hauppauge.com>
 *      based on lgdt3305.[ch] by Michael Krufky
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
 */

#ifndef _LGDT3306A_H_
#define _LGDT3306A_H_

#include <linux/i2c.h>
#include "dvb_frontend.h"


enum lgdt3306a_mpeg_mode {
	LGDT3306A_MPEG_PARALLEL = 0,
	LGDT3306A_MPEG_SERIAL = 1,
};

enum lgdt3306a_tp_clock_edge {
	LGDT3306A_TPCLK_RISING_EDGE = 0,
	LGDT3306A_TPCLK_FALLING_EDGE = 1,
};

enum lgdt3306a_tp_valid_polarity {
	LGDT3306A_TP_VALID_LOW = 0,
	LGDT3306A_TP_VALID_HIGH = 1,
};

struct lgdt3306a_config {
	u8 i2c_addr;

	/* user defined IF frequency in KHz */
	u16 qam_if_khz;
	u16 vsb_if_khz;

	/* disable i2c repeater - 0:repeater enabled 1:repeater disabled */
	unsigned int deny_i2c_rptr:1;

	/* spectral inversion - 0:disabled 1:enabled */
	unsigned int spectral_inversion:1;

	enum lgdt3306a_mpeg_mode mpeg_mode;
	enum lgdt3306a_tp_clock_edge tpclk_edge;
	enum lgdt3306a_tp_valid_polarity tpvalid_polarity;

	/* demod clock freq in MHz; 24 or 25 supported */
	int  xtalMHz;

	/* returned by driver if using i2c bus multiplexing */
	struct dvb_frontend **fe;
	struct i2c_adapter **i2c_adapter;
};

#if IS_REACHABLE(CONFIG_DVB_LGDT3306A)
struct dvb_frontend *lgdt3306a_attach(const struct lgdt3306a_config *config,
				      struct i2c_adapter *i2c_adap);
#else
static inline
struct dvb_frontend *lgdt3306a_attach(const struct lgdt3306a_config *config,
				      struct i2c_adapter *i2c_adap)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_LGDT3306A */

#endif /* _LGDT3306A_H_ */
