/*
 * stv0900_priv.h
 *
 * Driver for ST STV0900 satellite demodulator IC.
 *
 * Copyright (C) ST Microelectronics.
 * Copyright (C) 2009 NetUP Inc.
 * Copyright (C) 2009 Igor M. Liplianin <liplianin@netup.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 * GNU General Public License for more details.
 */

#ifndef STV0900_PRIV_H
#define STV0900_PRIV_H

#include <linux/i2c.h>

#define INRANGE(X, Y, Z) ((((X) <= (Y)) && ((Y) <= (Z))) \
		|| (((Z) <= (Y)) && ((Y) <= (X))) ? 1 : 0)

#ifndef MAKEWORD
#define MAKEWORD(X, Y) (((X) << 8) + (Y))
#endif

#define LSB(X) (((X) & 0xFF))
#define MSB(Y) (((Y) >> 8) & 0xFF)

#ifndef TRUE
#define TRUE (1 == 1)
#endif
#ifndef FALSE
#define FALSE (!TRUE)
#endif

#define dprintk(args...) \
	do { \
		if (stvdebug) \
			printk(KERN_DEBUG args); \
	} while (0)

#define STV0900_MAXLOOKUPSIZE 500
#define STV0900_BLIND_SEARCH_AGC2_TH 700
#define STV0900_BLIND_SEARCH_AGC2_TH_CUT30 1400
#define IQPOWER_THRESHOLD  30

/* One point of the lookup table */
struct stv000_lookpoint {
	s32 realval;/* real value */
	s32 regval;/* binary value */
};

/* Lookup table definition */
struct stv0900_table{
	s32 size;/* Size of the lookup table */
	struct stv000_lookpoint table[STV0900_MAXLOOKUPSIZE];/* Lookup table */
};

enum fe_stv0900_error {
	STV0900_NO_ERROR = 0,
	STV0900_INVALID_HANDLE,
	STV0900_BAD_PARAMETER,
	STV0900_I2C_ERROR,
	STV0900_SEARCH_FAILED,
};

enum fe_stv0900_clock_type {
	STV0900_USE_REGISTERS_DEFAULT,
	STV0900_SERIAL_PUNCT_CLOCK,/*Serial punctured clock */
	STV0900_SERIAL_CONT_CLOCK,/*Serial continues clock */
	STV0900_PARALLEL_PUNCT_CLOCK,/*Parallel punctured clock */
	STV0900_DVBCI_CLOCK/*Parallel continues clock : DVBCI */
};

enum fe_stv0900_search_state {
	STV0900_SEARCH = 0,
	STV0900_PLH_DETECTED,
	STV0900_DVBS2_FOUND,
	STV0900_DVBS_FOUND

};

enum fe_stv0900_ldpc_state {
	STV0900_PATH1_OFF_PATH2_OFF = 0,
	STV0900_PATH1_ON_PATH2_OFF = 1,
	STV0900_PATH1_OFF_PATH2_ON = 2,
	STV0900_PATH1_ON_PATH2_ON = 3
};

enum fe_stv0900_signal_type {
	STV0900_NOAGC1 = 0,
	STV0900_AGC1OK,
	STV0900_NOTIMING,
	STV0900_ANALOGCARRIER,
	STV0900_TIMINGOK,
	STV0900_NOAGC2,
	STV0900_AGC2OK,
	STV0900_NOCARRIER,
	STV0900_CARRIEROK,
	STV0900_NODATA,
	STV0900_DATAOK,
	STV0900_OUTOFRANGE,
	STV0900_RANGEOK
};

enum fe_stv0900_demod_num {
	STV0900_DEMOD_1,
	STV0900_DEMOD_2
};

enum fe_stv0900_tracking_standard {
	STV0900_DVBS1_STANDARD,/* Found Standard*/
	STV0900_DVBS2_STANDARD,
	STV0900_DSS_STANDARD,
	STV0900_TURBOCODE_STANDARD,
	STV0900_UNKNOWN_STANDARD
};

enum fe_stv0900_search_standard {
	STV0900_AUTO_SEARCH,
	STV0900_SEARCH_DVBS1,/* Search Standard*/
	STV0900_SEARCH_DVBS2,
	STV0900_SEARCH_DSS,
	STV0900_SEARCH_TURBOCODE
};

enum fe_stv0900_search_algo {
	STV0900_BLIND_SEARCH,/* offset freq and SR are Unknown */
	STV0900_COLD_START,/* only the SR is known */
	STV0900_WARM_START/* offset freq and SR are known */
};

enum fe_stv0900_modulation {
	STV0900_QPSK,
	STV0900_8PSK,
	STV0900_16APSK,
	STV0900_32APSK,
	STV0900_UNKNOWN
};

enum fe_stv0900_modcode {
	STV0900_DUMMY_PLF,
	STV0900_QPSK_14,
	STV0900_QPSK_13,
	STV0900_QPSK_25,
	STV0900_QPSK_12,
	STV0900_QPSK_35,
	STV0900_QPSK_23,
	STV0900_QPSK_34,
	STV0900_QPSK_45,
	STV0900_QPSK_56,
	STV0900_QPSK_89,
	STV0900_QPSK_910,
	STV0900_8PSK_35,
	STV0900_8PSK_23,
	STV0900_8PSK_34,
	STV0900_8PSK_56,
	STV0900_8PSK_89,
	STV0900_8PSK_910,
	STV0900_16APSK_23,
	STV0900_16APSK_34,
	STV0900_16APSK_45,
	STV0900_16APSK_56,
	STV0900_16APSK_89,
	STV0900_16APSK_910,
	STV0900_32APSK_34,
	STV0900_32APSK_45,
	STV0900_32APSK_56,
	STV0900_32APSK_89,
	STV0900_32APSK_910,
	STV0900_MODCODE_UNKNOWN
};

enum fe_stv0900_fec {/*DVBS1, DSS and turbo code puncture rate*/
	STV0900_FEC_1_2 = 0,
	STV0900_FEC_2_3,
	STV0900_FEC_3_4,
	STV0900_FEC_4_5,/*for turbo code only*/
	STV0900_FEC_5_6,
	STV0900_FEC_6_7,/*for DSS only */
	STV0900_FEC_7_8,
	STV0900_FEC_8_9,/*for turbo code only*/
	STV0900_FEC_UNKNOWN
};

enum fe_stv0900_frame_length {
	STV0900_LONG_FRAME,
	STV0900_SHORT_FRAME
};

enum fe_stv0900_pilot {
	STV0900_PILOTS_OFF,
	STV0900_PILOTS_ON
};

enum fe_stv0900_rolloff {
	STV0900_35,
	STV0900_25,
	STV0900_20
};

enum fe_stv0900_search_iq {
	STV0900_IQ_AUTO,
	STV0900_IQ_AUTO_NORMAL_FIRST,
	STV0900_IQ_FORCE_NORMAL,
	STV0900_IQ_FORCE_SWAPPED
};

enum stv0900_iq_inversion {
	STV0900_IQ_NORMAL,
	STV0900_IQ_SWAPPED
};

enum fe_stv0900_diseqc_mode {
	STV0900_22KHZ_Continues = 0,
	STV0900_DISEQC_2_3_PWM = 2,
	STV0900_DISEQC_3_3_PWM = 3,
	STV0900_DISEQC_2_3_ENVELOP = 4,
	STV0900_DISEQC_3_3_ENVELOP = 5
};

enum fe_stv0900_demod_mode {
	STV0900_SINGLE = 0,
	STV0900_DUAL
};

struct stv0900_init_params{
	u32	dmd_ref_clk;/* Reference,Input clock for the demod in Hz */

	/* Demodulator Type (single demod or dual demod) */
	enum fe_stv0900_demod_mode	demod_mode;
	enum fe_stv0900_rolloff		rolloff;
	enum fe_stv0900_clock_type	path1_ts_clock;

	u8	tun1_maddress;
	int	tuner1_adc;
	int	tuner1_type;

	/* IQ from the tuner1 to the demod */
	enum stv0900_iq_inversion	tun1_iq_inv;
	enum fe_stv0900_clock_type	path2_ts_clock;

	u8	tun2_maddress;
	int	tuner2_adc;
	int	tuner2_type;

	/* IQ from the tuner2 to the demod */
	enum stv0900_iq_inversion	tun2_iq_inv;
	struct stv0900_reg		*ts_config;
};

struct stv0900_search_params {
	enum fe_stv0900_demod_num	path;/* Path Used demod1 or 2 */

	u32	frequency;/* Transponder frequency (in KHz) */
	u32	symbol_rate;/* Transponder symbol rate  (in bds)*/
	u32	search_range;/* Range of the search (in Hz) */

	enum fe_stv0900_search_standard	standard;
	enum fe_stv0900_modulation	modulation;
	enum fe_stv0900_fec		fec;
	enum fe_stv0900_modcode		modcode;
	enum fe_stv0900_search_iq	iq_inversion;
	enum fe_stv0900_search_algo	search_algo;

};

struct stv0900_signal_info {
	int	locked;/* Transponder locked */
	u32	frequency;/* Transponder frequency (in KHz) */
	u32	symbol_rate;/* Transponder symbol rate  (in Mbds) */

	enum fe_stv0900_tracking_standard	standard;
	enum fe_stv0900_fec			fec;
	enum fe_stv0900_modcode			modcode;
	enum fe_stv0900_modulation		modulation;
	enum fe_stv0900_pilot			pilot;
	enum fe_stv0900_frame_length		frame_len;
	enum stv0900_iq_inversion		spectrum;
	enum fe_stv0900_rolloff			rolloff;

	s32 Power;/* Power of the RF signal (dBm) */
	s32 C_N;/* Carrier to noise ratio (dB x10)*/
	u32 BER;/* Bit error rate (x10^7) */

};

struct stv0900_internal{
	s32	quartz;
	s32	mclk;
	/* manual RollOff for DVBS1/DSS only */
	enum fe_stv0900_rolloff		rolloff;
	/* Demodulator use for single demod or for dual demod) */
	enum fe_stv0900_demod_mode	demod_mode;

	/*Demods */
	s32	freq[2];
	s32	bw[2];
	s32	symbol_rate[2];
	s32	srch_range[2];
	/* for software/auto tuner */
	int	tuner_type[2];

	/* algorithm for search Blind, Cold or Warm*/
	enum fe_stv0900_search_algo	srch_algo[2];
	/* search standard: Auto, DVBS1/DSS only or DVBS2 only*/
	enum fe_stv0900_search_standard	srch_standard[2];
	/* inversion search : auto, auto norma first, normal or inverted */
	enum fe_stv0900_search_iq	srch_iq_inv[2];
	enum fe_stv0900_modcode		modcode[2];
	enum fe_stv0900_modulation	modulation[2];
	enum fe_stv0900_fec		fec[2];

	struct stv0900_signal_info	result[2];
	enum fe_stv0900_error		err[2];


	struct i2c_adapter	*i2c_adap;
	u8			i2c_addr;
	u8			clkmode;/* 0 for CLKI, 2 for XTALI */
	u8			chip_id;
	struct stv0900_reg	*ts_config;
	enum fe_stv0900_error	errs;
	int dmds_used;
};

/* state for each demod */
struct stv0900_state {
	/* pointer for internal params, one for each pair of demods */
	struct stv0900_internal		*internal;
	struct i2c_adapter		*i2c_adap;
	const struct stv0900_config	*config;
	struct dvb_frontend		frontend;
	int demod;
};

extern int stvdebug;

extern s32 ge2comp(s32 a, s32 width);

extern void stv0900_write_reg(struct stv0900_internal *i_params,
				u16 reg_addr, u8 reg_data);

extern u8 stv0900_read_reg(struct stv0900_internal *i_params,
				u16 reg_addr);

extern void stv0900_write_bits(struct stv0900_internal *i_params,
				u32 label, u8 val);

extern u8 stv0900_get_bits(struct stv0900_internal *i_params,
				u32 label);

extern int stv0900_get_demod_lock(struct stv0900_internal *i_params,
				enum fe_stv0900_demod_num demod, s32 time_out);
extern int stv0900_check_signal_presence(struct stv0900_internal *i_params,
				enum fe_stv0900_demod_num demod);

extern enum fe_stv0900_signal_type stv0900_algo(struct dvb_frontend *fe);

extern void stv0900_set_tuner(struct dvb_frontend *fe, u32 frequency,
				u32 bandwidth);
extern void stv0900_set_bandwidth(struct dvb_frontend *fe, u32 bandwidth);

extern void stv0900_start_search(struct stv0900_internal *i_params,
				enum fe_stv0900_demod_num demod);

extern u8 stv0900_get_optim_carr_loop(s32 srate,
				enum fe_stv0900_modcode modcode,
				s32 pilot, u8 chip_id);

extern u8 stv0900_get_optim_short_carr_loop(s32 srate,
				enum fe_stv0900_modulation modulation,
				u8 chip_id);

extern void stv0900_stop_all_s2_modcod(struct stv0900_internal *i_params,
				enum fe_stv0900_demod_num demod);

extern void stv0900_activate_s2_modcod(struct stv0900_internal *i_params,
				enum fe_stv0900_demod_num demod);

extern void stv0900_activate_s2_modcod_single(struct stv0900_internal *i_params,
				enum fe_stv0900_demod_num demod);

extern enum
fe_stv0900_tracking_standard stv0900_get_standard(struct dvb_frontend *fe,
				enum fe_stv0900_demod_num demod);

extern u32
stv0900_get_freq_auto(struct stv0900_internal *intp, int demod);

extern void
stv0900_set_tuner_auto(struct stv0900_internal *intp, u32 Frequency,
						u32 Bandwidth, int demod);

#endif
