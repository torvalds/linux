/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * NXP TDA10071 + Conexant CX24118A DVB-S/S2 demodulator + tuner driver
 *
 * Copyright (C) 2011 Antti Palosaari <crope@iki.fi>
 */

#ifndef TDA10071_H
#define TDA10071_H

#include <linux/dvb/frontend.h>

/*
 * I2C address
 * 0x05, 0x55,
 */

/**
 * struct tda10071_platform_data - Platform data for the tda10071 driver
 * @clk: Clock frequency.
 * @i2c_wr_max: Max bytes I2C adapter can write at once.
 * @ts_mode: TS mode.
 * @spec_inv: Input spectrum inversion.
 * @pll_multiplier: PLL multiplier.
 * @tuner_i2c_addr: CX24118A tuner I2C address (0x14, 0x54, ...).
 * @get_dvb_frontend: Get DVB frontend.
 */
struct tda10071_platform_data {
	u32 clk;
	u16 i2c_wr_max;
#define TDA10071_TS_SERIAL        0
#define TDA10071_TS_PARALLEL      1
	u8 ts_mode;
	bool spec_inv;
	u8 pll_multiplier;
	u8 tuner_i2c_addr;

	struct dvb_frontend* (*get_dvb_frontend)(struct i2c_client *);
};

#endif /* TDA10071_H */
