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

#ifndef __STB0899_PRIV_H
#define __STB0899_PRIV_H

#include <media/dvb_frontend.h>
#include "stb0899_drv.h"

#define FE_ERROR				0
#define FE_NOTICE				1
#define FE_INFO					2
#define FE_DEBUG				3
#define FE_DEBUGREG				4

#define dprintk(x, y, z, format, arg...) do {						\
	if (z) {									\
		if	((*x > FE_ERROR) && (*x > y))					\
			printk(KERN_ERR "%s: " format "\n", __func__ , ##arg);		\
		else if	((*x > FE_NOTICE) && (*x > y))					\
			printk(KERN_NOTICE "%s: " format "\n", __func__ , ##arg);	\
		else if ((*x > FE_INFO) && (*x > y))					\
			printk(KERN_INFO "%s: " format "\n", __func__ , ##arg);		\
		else if ((*x > FE_DEBUG) && (*x > y))					\
			printk(KERN_DEBUG "%s: " format "\n", __func__ , ##arg);	\
	} else {									\
		if (*x > y)								\
			printk(format, ##arg);						\
	}										\
} while(0)

#define INRANGE(val, x, y)			(((x <= val) && (val <= y)) ||		\
						 ((y <= val) && (val <= x)) ? 1 : 0)

#define BYTE0					0
#define BYTE1					8
#define BYTE2					16
#define BYTE3					24

#define GETBYTE(x, y)				(((x) >> (y)) & 0xff)
#define MAKEWORD32(a, b, c, d)			(((a) << 24) | ((b) << 16) | ((c) << 8) | (d))
#define MAKEWORD16(a, b)			(((a) << 8) | (b))

#define LSB(x)					((x & 0xff))
#define MSB(y)					((y >> 8) & 0xff)


#define STB0899_GETFIELD(bitf, val)		((val >> STB0899_OFFST_##bitf) & ((1 << STB0899_WIDTH_##bitf) - 1))


#define STB0899_SETFIELD(mask, val, width, offset)      (mask & (~(((1 << width) - 1) <<	\
							 offset))) | ((val &			\
							 ((1 << width) - 1)) << offset)

#define STB0899_SETFIELD_VAL(bitf, mask, val)	(mask = (mask & (~(((1 << STB0899_WIDTH_##bitf) - 1) <<\
							 STB0899_OFFST_##bitf))) | \
							 (val << STB0899_OFFST_##bitf))


enum stb0899_status {
	NOAGC1	= 0,
	AGC1OK,
	NOTIMING,
	ANALOGCARRIER,
	TIMINGOK,
	NOAGC2,
	AGC2OK,
	NOCARRIER,
	CARRIEROK,
	NODATA,
	FALSELOCK,
	DATAOK,
	OUTOFRANGE,
	RANGEOK,
	DVBS2_DEMOD_LOCK,
	DVBS2_DEMOD_NOLOCK,
	DVBS2_FEC_LOCK,
	DVBS2_FEC_NOLOCK
};

enum stb0899_modcod {
	STB0899_DUMMY_PLF,
	STB0899_QPSK_14,
	STB0899_QPSK_13,
	STB0899_QPSK_25,
	STB0899_QPSK_12,
	STB0899_QPSK_35,
	STB0899_QPSK_23,
	STB0899_QPSK_34,
	STB0899_QPSK_45,
	STB0899_QPSK_56,
	STB0899_QPSK_89,
	STB0899_QPSK_910,
	STB0899_8PSK_35,
	STB0899_8PSK_23,
	STB0899_8PSK_34,
	STB0899_8PSK_56,
	STB0899_8PSK_89,
	STB0899_8PSK_910,
	STB0899_16APSK_23,
	STB0899_16APSK_34,
	STB0899_16APSK_45,
	STB0899_16APSK_56,
	STB0899_16APSK_89,
	STB0899_16APSK_910,
	STB0899_32APSK_34,
	STB0899_32APSK_45,
	STB0899_32APSK_56,
	STB0899_32APSK_89,
	STB0899_32APSK_910
};

enum stb0899_frame {
	STB0899_LONG_FRAME,
	STB0899_SHORT_FRAME
};

enum stb0899_alpha {
	RRC_20,
	RRC_25,
	RRC_35
};

struct stb0899_tab {
	s32 real;
	s32 read;
};

enum stb0899_fec {
	STB0899_FEC_1_2			= 13,
	STB0899_FEC_2_3			= 18,
	STB0899_FEC_3_4			= 21,
	STB0899_FEC_5_6			= 24,
	STB0899_FEC_6_7			= 25,
	STB0899_FEC_7_8			= 26
};

struct stb0899_params {
	u32	freq;					/* Frequency	*/
	u32	srate;					/* Symbol rate	*/
	enum fe_code_rate fecrate;
};

struct stb0899_internal {
	u32			master_clk;
	u32			freq;			/* Demod internal Frequency		*/
	u32			srate;			/* Demod internal Symbol rate		*/
	enum stb0899_fec	fecrate;		/* Demod internal FEC rate		*/
	s32			srch_range;		/* Demod internal Search Range		*/
	s32			sub_range;		/* Demod current sub range (Hz)		*/
	s32			tuner_step;		/* Tuner step (Hz)			*/
	s32			tuner_offst;		/* Relative offset to carrier (Hz)	*/
	u32			tuner_bw;		/* Current bandwidth of the tuner (Hz)	*/

	s32			mclk;			/* Masterclock Divider factor (binary)	*/
	s32			rolloff;		/* Current RollOff of the filter (x100)	*/

	s16			derot_freq;		/* Current derotator frequency (Hz)	*/
	s16			derot_percent;

	s16			direction;		/* Current derotator search direction	*/
	s16			derot_step;		/* Derotator step (binary value)	*/
	s16			t_derot;		/* Derotator time constant (ms)		*/
	s16			t_data;			/* Data recovery time constant (ms)	*/
	s16			sub_dir;		/* Direction of the next sub range	*/

	s16			t_agc1;			/* Agc1 time constant (ms)		*/
	s16			t_agc2;			/* Agc2 time constant (ms)		*/

	u32			lock;			/* Demod internal lock state		*/
	enum stb0899_status	status;			/* Demod internal status		*/

	/* DVB-S2 */
	s32			agc_gain;		/* RF AGC Gain				*/
	s32			center_freq;		/* Nominal carrier frequency		*/
	s32			av_frame_coarse;	/* Coarse carrier freq search frames	*/
	s32			av_frame_fine;		/* Fine carrier freq search frames	*/

	s16			step_size;		/* Carrier frequency search step size	*/

	enum stb0899_alpha	rrc_alpha;
	enum stb0899_inversion	inversion;
	enum stb0899_modcod	modcod;
	u8			pilots;			/* Pilots found				*/

	enum stb0899_frame	frame_length;
	u8			v_status;		/* VSTATUS				*/
	u8			err_ctrl;		/* ERRCTRLn				*/
};

struct stb0899_state {
	struct i2c_adapter		*i2c;
	struct stb0899_config		*config;
	struct dvb_frontend		frontend;

	u32				*verbose;	/* Cached module verbosity level	*/

	struct stb0899_internal		internal;	/* Device internal parameters		*/

	/*	cached params from API	*/
	enum fe_delivery_system		delsys;
	struct stb0899_params		params;

	u32				rx_freq;	/* DiSEqC 2.0 receiver freq		*/
	struct mutex			search_lock;
};
/* stb0899.c		*/
extern int stb0899_read_reg(struct stb0899_state *state,
			    unsigned int reg);

extern u32 _stb0899_read_s2reg(struct stb0899_state *state,
			       u32 stb0899_i2cdev,
			       u32 stb0899_base_addr,
			       u16 stb0899_reg_offset);

extern int stb0899_read_regs(struct stb0899_state *state,
			     unsigned int reg, u8 *buf,
			     u32 count);

extern int stb0899_write_regs(struct stb0899_state *state,
			      unsigned int reg, u8 *data,
			      u32 count);

extern int stb0899_write_reg(struct stb0899_state *state,
			     unsigned int reg,
			     u8 data);

extern int stb0899_write_s2reg(struct stb0899_state *state,
			       u32 stb0899_i2cdev,
			       u32 stb0899_base_addr,
			       u16 stb0899_reg_offset,
			       u32 stb0899_data);

extern int stb0899_i2c_gate_ctrl(struct dvb_frontend *fe, int enable);


#define STB0899_READ_S2REG(DEVICE, REG)		(_stb0899_read_s2reg(state, DEVICE, STB0899_BASE_##REG, STB0899_OFF0_##REG))
//#define STB0899_WRITE_S2REG(DEVICE, REG, DATA)	(_stb0899_write_s2reg(state, DEVICE, STB0899_BASE_##REG, STB0899_OFF0_##REG, DATA))

/* stb0899_algo.c	*/
extern enum stb0899_status stb0899_dvbs_algo(struct stb0899_state *state);
extern enum stb0899_status stb0899_dvbs2_algo(struct stb0899_state *state);
extern long stb0899_carr_width(struct stb0899_state *state);

#endif //__STB0899_PRIV_H
