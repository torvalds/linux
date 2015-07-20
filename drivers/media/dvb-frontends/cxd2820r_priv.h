/*
 * Sony CXD2820R demodulator driver
 *
 * Copyright (C) 2010 Antti Palosaari <crope@iki.fi>
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


#ifndef CXD2820R_PRIV_H
#define CXD2820R_PRIV_H

#include <linux/dvb/version.h>
#include "dvb_frontend.h"
#include "dvb_math.h"
#include "cxd2820r.h"
#include <linux/gpio.h>

struct reg_val_mask {
	u32 reg;
	u8  val;
	u8  mask;
};

struct cxd2820r_priv {
	struct i2c_adapter *i2c;
	struct dvb_frontend fe;
	struct cxd2820r_config cfg;

	bool ber_running;

	u8 bank[2];
#define GPIO_COUNT 3
	u8 gpio[GPIO_COUNT];
#ifdef CONFIG_GPIOLIB
	struct gpio_chip gpio_chip;
#endif

	enum fe_delivery_system delivery_system;
	bool last_tune_failed; /* for switch between T and T2 tune */
};

/* cxd2820r_core.c */

extern int cxd2820r_debug;

int cxd2820r_gpio(struct dvb_frontend *fe, u8 *gpio);

int cxd2820r_wr_reg_mask(struct cxd2820r_priv *priv, u32 reg, u8 val,
	u8 mask);

int cxd2820r_wr_regs(struct cxd2820r_priv *priv, u32 reginfo, u8 *val,
	int len);

int cxd2820r_wr_regs(struct cxd2820r_priv *priv, u32 reginfo, u8 *val,
	int len);

int cxd2820r_rd_regs(struct cxd2820r_priv *priv, u32 reginfo, u8 *val,
	int len);

int cxd2820r_wr_reg(struct cxd2820r_priv *priv, u32 reg, u8 val);

int cxd2820r_rd_reg(struct cxd2820r_priv *priv, u32 reg, u8 *val);

/* cxd2820r_c.c */

int cxd2820r_get_frontend_c(struct dvb_frontend *fe);

int cxd2820r_set_frontend_c(struct dvb_frontend *fe);

int cxd2820r_read_status_c(struct dvb_frontend *fe, enum fe_status *status);

int cxd2820r_read_ber_c(struct dvb_frontend *fe, u32 *ber);

int cxd2820r_read_signal_strength_c(struct dvb_frontend *fe, u16 *strength);

int cxd2820r_read_snr_c(struct dvb_frontend *fe, u16 *snr);

int cxd2820r_read_ucblocks_c(struct dvb_frontend *fe, u32 *ucblocks);

int cxd2820r_init_c(struct dvb_frontend *fe);

int cxd2820r_sleep_c(struct dvb_frontend *fe);

int cxd2820r_get_tune_settings_c(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s);

/* cxd2820r_t.c */

int cxd2820r_get_frontend_t(struct dvb_frontend *fe);

int cxd2820r_set_frontend_t(struct dvb_frontend *fe);

int cxd2820r_read_status_t(struct dvb_frontend *fe, enum fe_status *status);

int cxd2820r_read_ber_t(struct dvb_frontend *fe, u32 *ber);

int cxd2820r_read_signal_strength_t(struct dvb_frontend *fe, u16 *strength);

int cxd2820r_read_snr_t(struct dvb_frontend *fe, u16 *snr);

int cxd2820r_read_ucblocks_t(struct dvb_frontend *fe, u32 *ucblocks);

int cxd2820r_init_t(struct dvb_frontend *fe);

int cxd2820r_sleep_t(struct dvb_frontend *fe);

int cxd2820r_get_tune_settings_t(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s);

/* cxd2820r_t2.c */

int cxd2820r_get_frontend_t2(struct dvb_frontend *fe);

int cxd2820r_set_frontend_t2(struct dvb_frontend *fe);

int cxd2820r_read_status_t2(struct dvb_frontend *fe, enum fe_status *status);

int cxd2820r_read_ber_t2(struct dvb_frontend *fe, u32 *ber);

int cxd2820r_read_signal_strength_t2(struct dvb_frontend *fe, u16 *strength);

int cxd2820r_read_snr_t2(struct dvb_frontend *fe, u16 *snr);

int cxd2820r_read_ucblocks_t2(struct dvb_frontend *fe, u32 *ucblocks);

int cxd2820r_init_t2(struct dvb_frontend *fe);

int cxd2820r_sleep_t2(struct dvb_frontend *fe);

int cxd2820r_get_tune_settings_t2(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s);

#endif /* CXD2820R_PRIV_H */
