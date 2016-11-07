/*
    Montage Technology TS2020 - Silicon Tuner driver
    Copyright (C) 2009-2012 Konstantin Dimitrov <kosio.dimitrov@gmail.com>

    Copyright (C) 2009-2012 TurboSight.com

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

#ifndef TS2020_H
#define TS2020_H

#include <linux/dvb/frontend.h>

struct ts2020_config {
	u8 tuner_address;
	u32 frequency_div;

	/*
	 * RF loop-through
	 */
	bool loop_through:1;

	/*
	 * clock output
	 */
#define TS2020_CLK_OUT_DISABLED        0
#define TS2020_CLK_OUT_ENABLED         1
#define TS2020_CLK_OUT_ENABLED_XTALOUT 2
	u8 clk_out:2;

	/*
	 * clock output divider
	 * 1 - 31
	 */
	u8 clk_out_div:5;

	/* Set to true to suppress stat polling */
	bool dont_poll:1;

	/*
	 * pointer to DVB frontend
	 */
	struct dvb_frontend *fe;

	/*
	 * driver private, do not set value
	 */
	u8 attach_in_use:1;

	/* Operation to be called by the ts2020 driver to get the value of the
	 * AGC PWM tuner input as theoretically output by the demodulator.
	 */
	int (*get_agc_pwm)(struct dvb_frontend *fe, u8 *_agc_pwm);
};

/* Do not add new ts2020_attach() users! Use I2C bindings instead. */
#if IS_REACHABLE(CONFIG_DVB_TS2020)
extern struct dvb_frontend *ts2020_attach(
	struct dvb_frontend *fe,
	const struct ts2020_config *config,
	struct i2c_adapter *i2c);
#else
static inline struct dvb_frontend *ts2020_attach(
	struct dvb_frontend *fe,
	const struct ts2020_config *config,
	struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* TS2020_H */
