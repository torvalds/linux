/*
	STB0899 Multistandard Frontend driver
	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

	Copyright (C) ST Microelectronics

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

#ifndef __STB0899_DRV_H
#define __STB0899_DRV_H

#include <linux/kernel.h>
#include <linux/module.h>

#include "dvb_frontend.h"

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
	IQ_SWAP_OFF	= 0,
	IQ_SWAP_ON,
	IQ_SWAP_AUTO
};

struct stb0899_config {
	const struct stb0899_s1_reg	*init_dev;
	const struct stb0899_s2_reg	*init_s2_demod;
	const struct stb0899_s1_reg	*init_s1_demod;
	const struct stb0899_s2_reg	*init_s2_fec;
	const struct stb0899_s1_reg	*init_tst;

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

#if defined(CONFIG_DVB_STB0899) || (defined(CONFIG_DVB_STB0899_MODULE) && defined(MODULE))

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
