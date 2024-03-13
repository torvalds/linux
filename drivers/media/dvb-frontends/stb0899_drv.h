/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
	STB0899 Multistandard Frontend driver
	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

	Copyright (C) ST Microelectronics

*/

#ifndef __STB0899_DRV_H
#define __STB0899_DRV_H

#include <linux/kernel.h>
#include <linux/module.h>

#include <media/dvb_frontend.h>

#define STB0899_TSMODE_SERIAL		1
#define STB0899_CLKPOL_FALLING		2
#define STB0899_CLKNULL_PARITY		3
#define STB0899_SYNC_FORCED		4
#define STB0899_FECMODE_DSS		5

struct stb0899_s1_reg {
	u16	address;
	u8	data;
};

struct stb0899_s2_reg {
	u16	offset;
	u32	base_address;
	u32	data;
};

enum stb0899_inversion {
	IQ_SWAP_OFF	= +1, /* inversion affects the sign of e. g. */
	IQ_SWAP_ON	= -1, /* the derotator frequency register    */
};

#define STB0899_GPIO00				0xf140
#define STB0899_GPIO01				0xf141
#define STB0899_GPIO02				0xf142
#define STB0899_GPIO03				0xf143
#define STB0899_GPIO04				0xf144
#define STB0899_GPIO05				0xf145
#define STB0899_GPIO06				0xf146
#define STB0899_GPIO07				0xf147
#define STB0899_GPIO08				0xf148
#define STB0899_GPIO09				0xf149
#define STB0899_GPIO10				0xf14a
#define STB0899_GPIO11				0xf14b
#define STB0899_GPIO12				0xf14c
#define STB0899_GPIO13				0xf14d
#define STB0899_GPIO14				0xf14e
#define STB0899_GPIO15				0xf14f
#define STB0899_GPIO16				0xf150
#define STB0899_GPIO17				0xf151
#define STB0899_GPIO18				0xf152
#define STB0899_GPIO19				0xf153
#define STB0899_GPIO20				0xf154

#define STB0899_GPIOPULLUP			0x01 /* Output device is connected to Vdd */
#define STB0899_GPIOPULLDN			0x00 /* Output device is connected to Vss */

#define STB0899_POSTPROC_GPIO_POWER		0x00
#define STB0899_POSTPROC_GPIO_LOCK		0x01

/*
 * Post process output configuration control
 * 1. POWER ON/OFF		(index 0)
 * 2. FE_HAS_LOCK/LOCK_LOSS	(index 1)
 *
 * @gpio	= one of the above listed GPIO's
 * @level	= output state: pulled up or low
 */
struct stb0899_postproc {
	u16	gpio;
	u8	level;
};

struct stb0899_config {
	const struct stb0899_s1_reg	*init_dev;
	const struct stb0899_s2_reg	*init_s2_demod;
	const struct stb0899_s1_reg	*init_s1_demod;
	const struct stb0899_s2_reg	*init_s2_fec;
	const struct stb0899_s1_reg	*init_tst;

	const struct stb0899_postproc	*postproc;

	enum stb0899_inversion		inversion;

	u32	xtal_freq;

	u8	demod_address;
	u8	ts_output_mode;
	u8	block_sync_mode;
	u8	ts_pfbit_toggle;

	u8	clock_polarity;
	u8	data_clk_parity;
	u8	fec_mode;
	u8	data_output_ctl;
	u8	data_fifo_mode;
	u8	out_rate_comp;
	u8	i2c_repeater;
//	int	inversion;
	int	lo_clk;
	int	hi_clk;

	u32	esno_ave;
	u32	esno_quant;
	u32	avframes_coarse;
	u32	avframes_fine;
	u32	miss_threshold;
	u32	uwp_threshold_acq;
	u32	uwp_threshold_track;
	u32	uwp_threshold_sof;
	u32	sof_search_timeout;

	u32	btr_nco_bits;
	u32	btr_gain_shift_offset;
	u32	crl_nco_bits;
	u32	ldpc_max_iter;

	int (*tuner_set_frequency)(struct dvb_frontend *fe, u32 frequency);
	int (*tuner_get_frequency)(struct dvb_frontend *fe, u32 *frequency);
	int (*tuner_set_bandwidth)(struct dvb_frontend *fe, u32 bandwidth);
	int (*tuner_get_bandwidth)(struct dvb_frontend *fe, u32 *bandwidth);
	int (*tuner_set_rfsiggain)(struct dvb_frontend *fe, u32 rf_gain);
};

#if IS_REACHABLE(CONFIG_DVB_STB0899)

extern struct dvb_frontend *stb0899_attach(struct stb0899_config *config,
					   struct i2c_adapter *i2c);

#else

static inline struct dvb_frontend *stb0899_attach(struct stb0899_config *config,
						  struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: Driver disabled by Kconfig\n", __func__);
	return NULL;
}

#endif //CONFIG_DVB_STB0899


#endif
