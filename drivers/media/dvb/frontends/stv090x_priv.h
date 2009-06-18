/*
	STV0900/0903 Multistandard Broadcast Frontend driver
	Copyright (C) Manu Abraham <abraham.manu@gmail.com>

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

#ifndef __STV090x_PRIV_H
#define __STV090x_PRIV_H

#include "dvb_frontend.h"

#define FE_ERROR				0
#define FE_NOTICE				1
#define FE_INFO					2
#define FE_DEBUG				3
#define FE_DEBUGREG				4

#define dprintk(__y, __z, format, arg...) do {						\
	if (__z) {									\
		if	((verbose > FE_ERROR) && (verbose > __y))			\
			printk(KERN_ERR "%s: " format "\n", __func__ , ##arg);		\
		else if	((verbose > FE_NOTICE) && (verbose > __y))			\
			printk(KERN_NOTICE "%s: " format "\n", __func__ , ##arg);	\
		else if ((verbose > FE_INFO) && (verbose > __y))			\
			printk(KERN_INFO "%s: " format "\n", __func__ , ##arg);		\
		else if ((verbose > FE_DEBUG) && (verbose > __y))			\
			printk(KERN_DEBUG "%s: " format "\n", __func__ , ##arg);	\
	} else {									\
		if (verbose > __y)							\
			printk(format, ##arg);						\
	}										\
} while (0)

#define STV090x_READ_DEMOD(__state, __reg) ((			\
	(__state)->demod == STV090x_DEMODULATOR_1)	?	\
	stv090x_read_reg(__state, STV090x_P2_##__reg) :		\
	stv090x_read_reg(__state, STV090x_P1_##__reg))

#define STV090x_WRITE_DEMOD(__state, __reg, __data) ((		\
	(__state)->demod == STV090x_DEMODULATOR_1)	?	\
	stv090x_write_reg(__state, STV090x_P2_##__reg, __data) :\
	stv090x_write_reg(__state, STV090x_P1_##__reg, __data))

#define STV090x_ADDR_OFFST(__state, __x) ((			\
	(__state->demod) == STV090x_DEMODULATOR_1)	?	\
		STV090x_P1_##__x :				\
		STV090x_P2_##__x)


#define STV090x_SETFIELD(mask, bitf, val)	(mask = (mask & (~(((1 << STV090x_WIDTH_##bitf) - 1) <<\
							 STV090x_OFFST_##bitf))) | \
							 (val << STV090x_OFFST_##bitf))

#define STV090x_GETFIELD(val, bitf)		((val >> STV090x_OFFST_##bitf) & ((1 << STV090x_WIDTH_##bitf) - 1))


#define STV090x_SETFIELD_Px(mask, bitf, val)	(mask = (mask & (~(((1 << STV090x_WIDTH_Px_##bitf) - 1) <<\
							 STV090x_OFFST_Px_##bitf))) | \
							 (val << STV090x_OFFST_Px_##bitf))

#define STV090x_GETFIELD_Px(val, bitf)		((val >> STV090x_OFFST_Px_##bitf) & ((1 << STV090x_WIDTH_Px_##bitf) - 1))

#define MAKEWORD16(__a, __b)			(((__a) << 8) | (__b))

#define MSB(__x)				((__x >> 8) & 0xff)
#define LSB(__x)				(__x & 0xff)


#define STV090x_IQPOWER_THRESHOLD	  30
#define STV090x_SEARCH_AGC2_TH_CUT20	 700
#define STV090x_SEARCH_AGC2_TH_CUT30	1200

#define STV090x_SEARCH_AGC2_TH(__ver)	\
	((__ver <= 0x20) ?		\
	STV090x_SEARCH_AGC2_TH_CUT20 :	\
	STV090x_SEARCH_AGC2_TH_CUT30)

enum stv090x_signal_state {
	STV090x_NOCARRIER,
	STV090x_NODATA,
	STV090x_DATAOK,
	STV090x_RANGEOK,
	STV090x_OUTOFRANGE
};

enum stv090x_fec {
	STV090x_PR12 = 0,
	STV090x_PR23,
	STV090x_PR34,
	STV090x_PR45,
	STV090x_PR56,
	STV090x_PR67,
	STV090x_PR78,
	STV090x_PR89,
	STV090x_PR910,
	STV090x_PRERR
};

enum stv090x_modulation {
	STV090x_QPSK,
	STV090x_8PSK,
	STV090x_16APSK,
	STV090x_32APSK,
	STV090x_UNKNOWN
};

enum stv090x_frame {
	STV090x_LONG_FRAME,
	STV090x_SHORT_FRAME
};

enum stv090x_pilot {
	STV090x_PILOTS_OFF,
	STV090x_PILOTS_ON
};

enum stv090x_rolloff {
	STV090x_RO_35,
	STV090x_RO_25,
	STV090x_RO_20
};

enum stv090x_inversion {
	STV090x_IQ_AUTO,
	STV090x_IQ_NORMAL,
	STV090x_IQ_SWAP
};

enum stv090x_modcod {
	STV090x_DUMMY_PLF = 0,
	STV090x_QPSK_14,
	STV090x_QPSK_13,
	STV090x_QPSK_25,
	STV090x_QPSK_12,
	STV090x_QPSK_35,
	STV090x_QPSK_23,
	STV090x_QPSK_34,
	STV090x_QPSK_45,
	STV090x_QPSK_56,
	STV090x_QPSK_89,
	STV090x_QPSK_910,
	STV090x_8PSK_35,
	STV090x_8PSK_23,
	STV090x_8PSK_34,
	STV090x_8PSK_56,
	STV090x_8PSK_89,
	STV090x_8PSK_910,
	STV090x_16APSK_23,
	STV090x_16APSK_34,
	STV090x_16APSK_45,
	STV090x_16APSK_56,
	STV090x_16APSK_89,
	STV090x_16APSK_910,
	STV090x_32APSK_34,
	STV090x_32APSK_45,
	STV090x_32APSK_56,
	STV090x_32APSK_89,
	STV090x_32APSK_910,
	STV090x_MODCODE_UNKNOWN
};

enum stv090x_search {
	STV090x_SEARCH_DSS = 0,
	STV090x_SEARCH_DVBS1,
	STV090x_SEARCH_DVBS2,
	STV090x_SEARCH_AUTO
};

enum stv090x_algo {
	STV090x_BLIND_SEARCH,
	STV090x_COLD_SEARCH,
	STV090x_WARM_SEARCH
};

enum stv090x_delsys {
	STV090x_ERROR = 0,
	STV090x_DVBS1 = 1,
	STV090x_DVBS2,
	STV090x_DSS
};

struct stv090x_long_frame_crloop {
	enum stv090x_modcod	modcod;

	u8 crl_pilots_on_2;
	u8 crl_pilots_off_2;
	u8 crl_pilots_on_5;
	u8 crl_pilots_off_5;
	u8 crl_pilots_on_10;
	u8 crl_pilots_off_10;
	u8 crl_pilots_on_20;
	u8 crl_pilots_off_20;
	u8 crl_pilots_on_30;
	u8 crl_pilots_off_30;
};

struct stv090x_short_frame_crloop {
	enum stv090x_modulation	modulation;

	u8 crl_2;  /*      SR <   3M */
	u8 crl_5;  /*  3 < SR <=  7M */
	u8 crl_10; /*  7 < SR <= 15M */
	u8 crl_20; /* 10 < SR <= 25M */
	u8 crl_30; /* 10 < SR <= 45M */
};

struct stv090x_reg {
	u16 addr;
	u8  data;
};

struct stv090x_tab {
	s32 real;
	s32 read;
};

struct stv090x_state {
	enum stv090x_device		device;
	enum stv090x_demodulator	demod;
	enum stv090x_mode		demod_mode;
	u32				dev_ver;

	struct i2c_adapter		*i2c;
	const struct stv090x_config	*config;
	struct dvb_frontend		frontend;

	u32				*verbose; /* Cached module verbosity */

	enum stv090x_delsys		delsys;
	enum stv090x_fec		fec;
	enum stv090x_modulation		modulation;
	enum stv090x_modcod		modcod;
	enum stv090x_search		search_mode;
	enum stv090x_frame		frame_len;
	enum stv090x_pilot		pilots;
	enum stv090x_rolloff		rolloff;
	enum stv090x_inversion		inversion;
	enum stv090x_algo		algo;

	u32				frequency;
	u32				srate;

	s32				mclk; /* Masterclock Divider factor */
	s32				tuner_bw;

	u32				tuner_refclk;

	s32				search_range;

	s32				DemodTimeout;
	s32				FecTimeout;
};

#endif /* __STV090x_PRIV_H */
