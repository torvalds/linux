/*
 * NXP TDA10071 + Conexant CX24118A DVB-S/S2 demodulator + tuner driver
 *
 * Copyright (C) 2011 Antti Palosaari <crope@iki.fi>
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
 *    You should have received a copy of the GNU General Public License along
 *    with this program; if not, write to the Free Software Foundation, Inc.,
 *    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
