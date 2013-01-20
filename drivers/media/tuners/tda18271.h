/*
    tda18271.h - header for the Philips / NXP TDA18271 silicon tuner

    Copyright (C) 2007, 2008 Michael Krufky <mkrufky@linuxtv.org>

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

#ifndef __TDA18271_H__
#define __TDA18271_H__

#include <linux/i2c.h>
#include "dvb_frontend.h"

struct tda18271_std_map_item {
	u16 if_freq;

	/* EP3[4:3] */
	unsigned int agc_mode:2;
	/* EP3[2:0] */
	unsigned int std:3;
	/* EP4[7] */
	unsigned int fm_rfn:1;
	/* EP4[4:2] */
	unsigned int if_lvl:3;
	/* EB22[6:0] */
	unsigned int rfagc_top:7;
};

struct tda18271_std_map {
	struct tda18271_std_map_item fm_radio;
	struct tda18271_std_map_item atv_b;
	struct tda18271_std_map_item atv_dk;
	struct tda18271_std_map_item atv_gh;
	struct tda18271_std_map_item atv_i;
	struct tda18271_std_map_item atv_l;
	struct tda18271_std_map_item atv_lc;
	struct tda18271_std_map_item atv_mn;
	struct tda18271_std_map_item atsc_6;
	struct tda18271_std_map_item dvbt_6;
	struct tda18271_std_map_item dvbt_7;
	struct tda18271_std_map_item dvbt_8;
	struct tda18271_std_map_item qam_6;
	struct tda18271_std_map_item qam_7;
	struct tda18271_std_map_item qam_8;
};

enum tda18271_role {
	TDA18271_MASTER = 0,
	TDA18271_SLAVE,
};

enum tda18271_i2c_gate {
	TDA18271_GATE_AUTO = 0,
	TDA18271_GATE_ANALOG,
	TDA18271_GATE_DIGITAL,
};

enum tda18271_output_options {
	/* slave tuner output & loop thru & xtal oscillator always on */
	TDA18271_OUTPUT_LT_XT_ON = 0,

	/* slave tuner output loop thru off */
	TDA18271_OUTPUT_LT_OFF = 1,

	/* xtal oscillator off */
	TDA18271_OUTPUT_XT_OFF = 2,
};

enum tda18271_small_i2c {
	TDA18271_39_BYTE_CHUNK_INIT = 0,
	TDA18271_16_BYTE_CHUNK_INIT = 16,
	TDA18271_08_BYTE_CHUNK_INIT = 8,
	TDA18271_03_BYTE_CHUNK_INIT = 3,
};

struct tda18271_config {
	/* override default if freq / std settings (optional) */
	struct tda18271_std_map *std_map;

	/* master / slave tuner: master uses main pll, slave uses cal pll */
	enum tda18271_role role;

	/* use i2c gate provided by analog or digital demod */
	enum tda18271_i2c_gate gate;

	/* output options that can be disabled */
	enum tda18271_output_options output_opt;

	/* some i2c providers can't write all 39 registers at once */
	enum tda18271_small_i2c small_i2c;

	/* force rf tracking filter calibration on startup */
	unsigned int rf_cal_on_startup:1;

	/* prevent any register access during attach(),
	 * delaying both IR & RF calibration until init()
	 * module option 'cal' overrides this delay */
	unsigned int delay_cal:1;

	/* interface to saa713x / tda829x */
	unsigned int config;
};

#define TDA18271_CALLBACK_CMD_AGC_ENABLE 0

enum tda18271_mode {
	TDA18271_ANALOG = 0,
	TDA18271_DIGITAL,
};

#if IS_ENABLED(CONFIG_MEDIA_TUNER_TDA18271)
extern struct dvb_frontend *tda18271_attach(struct dvb_frontend *fe, u8 addr,
					    struct i2c_adapter *i2c,
					    struct tda18271_config *cfg);
#else
static inline struct dvb_frontend *tda18271_attach(struct dvb_frontend *fe,
						   u8 addr,
						   struct i2c_adapter *i2c,
						   struct tda18271_config *cfg)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif /* __TDA18271_H__ */
