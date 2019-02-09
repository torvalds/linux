/*
 * dib3000mb_priv.h
 *
 * Copyright (C) 2004 Patrick Boettcher (patrick.boettcher@posteo.de)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 * for more information see dib3000mb.c .
 */

#ifndef __DIB3000MB_PRIV_H_INCLUDED__
#define __DIB3000MB_PRIV_H_INCLUDED__

/* handy shortcuts */
#define rd(reg) dib3000_read_reg(state,reg)

#define wr(reg,val) if (dib3000_write_reg(state,reg,val)) \
	{ pr_err("while sending 0x%04x to 0x%04x.", val, reg); return -EREMOTEIO; }

#define wr_foreach(a,v) { int i; \
	if (sizeof(a) != sizeof(v)) \
		pr_err("sizeof: %zu %zu is different", sizeof(a), sizeof(v));\
	for (i=0; i < sizeof(a)/sizeof(u16); i++) \
		wr(a[i],v[i]); \
	}

#define set_or(reg,val) wr(reg,rd(reg) | val)

#define set_and(reg,val) wr(reg,rd(reg) & val)

/* debug */

#define dprintk(level, fmt, arg...) do {				\
	if (debug & level)						\
		printk(KERN_DEBUG pr_fmt("%s: " fmt),			\
		       __func__, ##arg);				\
} while (0)

/* mask for enabling a specific pid for the pid_filter */
#define DIB3000_ACTIVATE_PID_FILTERING	(0x2000)

/* common values for tuning */
#define DIB3000_ALPHA_0					(     0)
#define DIB3000_ALPHA_1					(     1)
#define DIB3000_ALPHA_2					(     2)
#define DIB3000_ALPHA_4					(     4)

#define DIB3000_CONSTELLATION_QPSK		(     0)
#define DIB3000_CONSTELLATION_16QAM		(     1)
#define DIB3000_CONSTELLATION_64QAM		(     2)

#define DIB3000_GUARD_TIME_1_32			(     0)
#define DIB3000_GUARD_TIME_1_16			(     1)
#define DIB3000_GUARD_TIME_1_8			(     2)
#define DIB3000_GUARD_TIME_1_4			(     3)

#define DIB3000_TRANSMISSION_MODE_2K	(     0)
#define DIB3000_TRANSMISSION_MODE_8K	(     1)

#define DIB3000_SELECT_LP				(     0)
#define DIB3000_SELECT_HP				(     1)

#define DIB3000_FEC_1_2					(     1)
#define DIB3000_FEC_2_3					(     2)
#define DIB3000_FEC_3_4					(     3)
#define DIB3000_FEC_5_6					(     5)
#define DIB3000_FEC_7_8					(     7)

#define DIB3000_HRCH_OFF				(     0)
#define DIB3000_HRCH_ON					(     1)

#define DIB3000_DDS_INVERSION_OFF		(     0)
#define DIB3000_DDS_INVERSION_ON		(     1)

#define DIB3000_TUNER_WRITE_ENABLE(a)	(0xffff & (a << 8))
#define DIB3000_TUNER_WRITE_DISABLE(a)	(0xffff & ((a << 8) | (1 << 7)))

#define DIB3000_REG_MANUFACTOR_ID		(  1025)
#define DIB3000_I2C_ID_DIBCOM			(0x01b3)

#define DIB3000_REG_DEVICE_ID			(  1026)
#define DIB3000MB_DEVICE_ID				(0x3000)
#define DIB3000MC_DEVICE_ID				(0x3001)
#define DIB3000P_DEVICE_ID				(0x3002)

/* frontend state */
struct dib3000_state {
	struct i2c_adapter* i2c;

/* configuration settings */
	struct dib3000_config config;

	struct dvb_frontend frontend;
	int timing_offset;
	int timing_offset_comp_done;

	u32 last_tuned_bw;
	u32 last_tuned_freq;
};

/* register addresses and some of their default values */

/* restart subsystems */
#define DIB3000MB_REG_RESTART			(     0)

#define DIB3000MB_RESTART_OFF			(     0)
#define DIB3000MB_RESTART_AUTO_SEARCH		(1 << 1)
#define DIB3000MB_RESTART_CTRL				(1 << 2)
#define DIB3000MB_RESTART_AGC				(1 << 3)

/* FFT size */
#define DIB3000MB_REG_FFT				(     1)

/* Guard time */
#define DIB3000MB_REG_GUARD_TIME		(     2)

/* QAM */
#define DIB3000MB_REG_QAM				(     3)

/* Alpha coefficient high priority Viterbi algorithm */
#define DIB3000MB_REG_VIT_ALPHA			(     4)

/* spectrum inversion */
#define DIB3000MB_REG_DDS_INV			(     5)

/* DDS frequency value (IF position) ad ? values don't match reg_3000mb.txt */
#define DIB3000MB_REG_DDS_FREQ_MSB		(     6)
#define DIB3000MB_REG_DDS_FREQ_LSB		(     7)
#define DIB3000MB_DDS_FREQ_MSB				(   178)
#define DIB3000MB_DDS_FREQ_LSB				(  8990)

/* timing frequency (carrier spacing) */
static u16 dib3000mb_reg_timing_freq[] = { 8,9 };
static u16 dib3000mb_timing_freq[][2] = {
	{ 126 , 48873 }, /* 6 MHz */
	{ 147 , 57019 }, /* 7 MHz */
	{ 168 , 65164 }, /* 8 MHz */
};

/* impulse noise parameter */
/* 36 ??? */

static u16 dib3000mb_reg_impulse_noise[] = { 10,11,12,15,36 };

enum dib3000mb_impulse_noise_type {
	DIB3000MB_IMPNOISE_OFF,
	DIB3000MB_IMPNOISE_MOBILE,
	DIB3000MB_IMPNOISE_FIXED,
	DIB3000MB_IMPNOISE_DEFAULT
};

static u16 dib3000mb_impulse_noise_values[][5] = {
	{ 0x0000, 0x0004, 0x0014, 0x01ff, 0x0399 }, /* off */
	{ 0x0001, 0x0004, 0x0014, 0x01ff, 0x037b }, /* mobile */
	{ 0x0001, 0x0004, 0x0020, 0x01bd, 0x0399 }, /* fixed */
	{ 0x0000, 0x0002, 0x000a, 0x01ff, 0x0399 }, /* default */
};

/*
 * Dual Automatic-Gain-Control
 * - gains RF in tuner (AGC1)
 * - gains IF after filtering (AGC2)
 */

/* also from 16 to 18 */
static u16 dib3000mb_reg_agc_gain[] = {
	19,20,21,22,23,24,25,26,27,28,29,30,31,32
};

static u16 dib3000mb_default_agc_gain[] =
	{ 0x0001, 52429,   623, 128, 166, 195, 61,   /* RF ??? */
	  0x0001, 53766, 38011,   0,  90,  33, 23 }; /* IF ??? */

/* phase noise */
/* 36 is set when setting the impulse noise */
static u16 dib3000mb_reg_phase_noise[] = { 33,34,35,37,38 };

static u16 dib3000mb_default_noise_phase[] = { 2, 544, 0, 5, 4 };

/* lock duration */
static u16 dib3000mb_reg_lock_duration[] = { 39,40 };
static u16 dib3000mb_default_lock_duration[] = { 135, 135 };

/* AGC loop bandwidth */
static u16 dib3000mb_reg_agc_bandwidth[] = { 43,44,45,46,47,48,49,50 };

static u16 dib3000mb_agc_bandwidth_low[]  =
	{ 2088, 10, 2088, 10, 3448, 5, 3448, 5 };
static u16 dib3000mb_agc_bandwidth_high[] =
	{ 2349,  5, 2349,  5, 2586, 2, 2586, 2 };

/*
 * lock0 definition (coff_lock)
 */
#define DIB3000MB_REG_LOCK0_MASK		(    51)
#define DIB3000MB_LOCK0_DEFAULT				(     4)

/*
 * lock1 definition (cpil_lock)
 * for auto search
 * which values hide behind the lock masks
 */
#define DIB3000MB_REG_LOCK1_MASK		(    52)
#define DIB3000MB_LOCK1_SEARCH_4			(0x0004)
#define DIB3000MB_LOCK1_SEARCH_2048			(0x0800)
#define DIB3000MB_LOCK1_DEFAULT				(0x0001)

/*
 * lock2 definition (fec_lock) */
#define DIB3000MB_REG_LOCK2_MASK		(    53)
#define DIB3000MB_LOCK2_DEFAULT				(0x0080)

/*
 * SEQ ? what was that again ... :)
 * changes when, inversion, guard time and fft is
 * either automatically detected or not
 */
#define DIB3000MB_REG_SEQ				(    54)

/* bandwidth */
static u16 dib3000mb_reg_bandwidth[] = { 55,56,57,58,59,60,61,62,63,64,65,66,67 };
static u16 dib3000mb_bandwidth_6mhz[] =
	{ 0, 33, 53312, 112, 46635, 563, 36565, 0, 1000, 0, 1010, 1, 45264 };

static u16 dib3000mb_bandwidth_7mhz[] =
	{ 0, 28, 64421,  96, 39973, 483,  3255, 0, 1000, 0, 1010, 1, 45264 };

static u16 dib3000mb_bandwidth_8mhz[] =
	{ 0, 25, 23600,  84, 34976, 422, 43808, 0, 1000, 0, 1010, 1, 45264 };

#define DIB3000MB_REG_UNK_68				(    68)
#define DIB3000MB_UNK_68						(     0)

#define DIB3000MB_REG_UNK_69				(    69)
#define DIB3000MB_UNK_69						(     0)

#define DIB3000MB_REG_UNK_71				(    71)
#define DIB3000MB_UNK_71						(     0)

#define DIB3000MB_REG_UNK_77				(    77)
#define DIB3000MB_UNK_77						(     6)

#define DIB3000MB_REG_UNK_78				(    78)
#define DIB3000MB_UNK_78						(0x0080)

/* isi */
#define DIB3000MB_REG_ISI				(    79)
#define DIB3000MB_ISI_ACTIVATE				(     0)
#define DIB3000MB_ISI_INHIBIT				(     1)

/* sync impovement */
#define DIB3000MB_REG_SYNC_IMPROVEMENT	(    84)
#define DIB3000MB_SYNC_IMPROVE_2K_1_8		(     3)
#define DIB3000MB_SYNC_IMPROVE_DEFAULT		(     0)

/* phase noise compensation inhibition */
#define DIB3000MB_REG_PHASE_NOISE		(    87)
#define DIB3000MB_PHASE_NOISE_DEFAULT	(     0)

#define DIB3000MB_REG_UNK_92				(    92)
#define DIB3000MB_UNK_92						(0x0080)

#define DIB3000MB_REG_UNK_96				(    96)
#define DIB3000MB_UNK_96						(0x0010)

#define DIB3000MB_REG_UNK_97				(    97)
#define DIB3000MB_UNK_97						(0x0009)

/* mobile mode ??? */
#define DIB3000MB_REG_MOBILE_MODE		(   101)
#define DIB3000MB_MOBILE_MODE_ON			(     1)
#define DIB3000MB_MOBILE_MODE_OFF			(     0)

#define DIB3000MB_REG_UNK_106			(   106)
#define DIB3000MB_UNK_106					(0x0080)

#define DIB3000MB_REG_UNK_107			(   107)
#define DIB3000MB_UNK_107					(0x0080)

#define DIB3000MB_REG_UNK_108			(   108)
#define DIB3000MB_UNK_108					(0x0080)

/* fft */
#define DIB3000MB_REG_UNK_121			(   121)
#define DIB3000MB_UNK_121_2K				(     7)
#define DIB3000MB_UNK_121_DEFAULT			(     5)

#define DIB3000MB_REG_UNK_122			(   122)
#define DIB3000MB_UNK_122					(  2867)

/* QAM for mobile mode */
#define DIB3000MB_REG_MOBILE_MODE_QAM	(   126)
#define DIB3000MB_MOBILE_MODE_QAM_64		(     3)
#define DIB3000MB_MOBILE_MODE_QAM_QPSK_16	(     1)
#define DIB3000MB_MOBILE_MODE_QAM_OFF		(     0)

/*
 * data diversity when having more than one chip on-board
 * see also DIB3000MB_OUTPUT_MODE_DATA_DIVERSITY
 */
#define DIB3000MB_REG_DATA_IN_DIVERSITY		(   127)
#define DIB3000MB_DATA_DIVERSITY_IN_OFF			(     0)
#define DIB3000MB_DATA_DIVERSITY_IN_ON			(     2)

/* vit hrch */
#define DIB3000MB_REG_VIT_HRCH			(   128)

/* vit code rate */
#define DIB3000MB_REG_VIT_CODE_RATE		(   129)

/* vit select hp */
#define DIB3000MB_REG_VIT_HP			(   130)

/* time frame for Bit-Error-Rate calculation */
#define DIB3000MB_REG_BERLEN			(   135)
#define DIB3000MB_BERLEN_LONG				(     0)
#define DIB3000MB_BERLEN_DEFAULT			(     1)
#define DIB3000MB_BERLEN_MEDIUM				(     2)
#define DIB3000MB_BERLEN_SHORT				(     3)

/* 142 - 152 FIFO parameters
 * which is what ?
 */

#define DIB3000MB_REG_FIFO_142			(   142)
#define DIB3000MB_FIFO_142					(     0)

/* MPEG2 TS output mode */
#define DIB3000MB_REG_MPEG2_OUT_MODE	(   143)
#define DIB3000MB_MPEG2_OUT_MODE_204		(     0)
#define DIB3000MB_MPEG2_OUT_MODE_188		(     1)

#define DIB3000MB_REG_PID_PARSE			(   144)
#define DIB3000MB_PID_PARSE_INHIBIT		(     0)
#define DIB3000MB_PID_PARSE_ACTIVATE	(     1)

#define DIB3000MB_REG_FIFO				(   145)
#define DIB3000MB_FIFO_INHIBIT				(     1)
#define DIB3000MB_FIFO_ACTIVATE				(     0)

#define DIB3000MB_REG_FIFO_146			(   146)
#define DIB3000MB_FIFO_146					(     3)

#define DIB3000MB_REG_FIFO_147			(   147)
#define DIB3000MB_FIFO_147					(0x0100)

/*
 * pidfilter
 * it is not a hardware pidfilter but a filter which drops all pids
 * except the ones set. Necessary because of the limited USB1.1 bandwidth.
 * regs 153-168
 */

#define DIB3000MB_REG_FIRST_PID			(   153)
#define DIB3000MB_NUM_PIDS				(    16)

/*
 * output mode
 * USB devices have to use 'slave'-mode
 * see also DIB3000MB_REG_ELECT_OUT_MODE
 */
#define DIB3000MB_REG_OUTPUT_MODE		(   169)
#define DIB3000MB_OUTPUT_MODE_GATED_CLK		(     0)
#define DIB3000MB_OUTPUT_MODE_CONT_CLK		(     1)
#define DIB3000MB_OUTPUT_MODE_SERIAL		(     2)
#define DIB3000MB_OUTPUT_MODE_DATA_DIVERSITY	(     5)
#define DIB3000MB_OUTPUT_MODE_SLAVE			(     6)

/* irq event mask */
#define DIB3000MB_REG_IRQ_EVENT_MASK		(   170)
#define DIB3000MB_IRQ_EVENT_MASK				(     0)

/* filter coefficients */
static u16 dib3000mb_reg_filter_coeffs[] = {
	171, 172, 173, 174, 175, 176, 177, 178,
	179, 180, 181, 182, 183, 184, 185, 186,
	188, 189, 190, 191, 192, 194
};

static u16 dib3000mb_filter_coeffs[] = {
	 226,  160,   29,
	 979,  998,   19,
	  22, 1019, 1006,
	1022,   12,    6,
	1017, 1017,    3,
	   6,       1019,
	1021,    2,    3,
	   1,          0,
};

/*
 * mobile algorithm (when you are moving with your device)
 * but not faster than 90 km/h
 */
#define DIB3000MB_REG_MOBILE_ALGO		(   195)
#define DIB3000MB_MOBILE_ALGO_ON			(     0)
#define DIB3000MB_MOBILE_ALGO_OFF			(     1)

/* multiple demodulators algorithm */
#define DIB3000MB_REG_MULTI_DEMOD_MSB	(   206)
#define DIB3000MB_REG_MULTI_DEMOD_LSB	(   207)

/* terminator, no more demods */
#define DIB3000MB_MULTI_DEMOD_MSB			( 32767)
#define DIB3000MB_MULTI_DEMOD_LSB			(  4095)

/* bring the device into a known  */
#define DIB3000MB_REG_RESET_DEVICE		(  1024)
#define DIB3000MB_RESET_DEVICE				(0x812c)
#define DIB3000MB_RESET_DEVICE_RST			(     0)

/* hardware clock configuration */
#define DIB3000MB_REG_CLOCK				(  1027)
#define DIB3000MB_CLOCK_DEFAULT				(0x9000)
#define DIB3000MB_CLOCK_DIVERSITY			(0x92b0)

/* power down config */
#define DIB3000MB_REG_POWER_CONTROL		(  1028)
#define DIB3000MB_POWER_DOWN				(     1)
#define DIB3000MB_POWER_UP					(     0)

/* electrical output mode */
#define DIB3000MB_REG_ELECT_OUT_MODE	(  1029)
#define DIB3000MB_ELECT_OUT_MODE_OFF		(     0)
#define DIB3000MB_ELECT_OUT_MODE_ON			(     1)

/* set the tuner i2c address */
#define DIB3000MB_REG_TUNER				(  1089)

/* monitoring registers (read only) */

/* agc loop locked (size: 1) */
#define DIB3000MB_REG_AGC_LOCK			(   324)

/* agc power (size: 16) */
#define DIB3000MB_REG_AGC_POWER			(   325)

/* agc1 value (16) */
#define DIB3000MB_REG_AGC1_VALUE		(   326)

/* agc2 value (16) */
#define DIB3000MB_REG_AGC2_VALUE		(   327)

/* total RF power (16), can be used for signal strength */
#define DIB3000MB_REG_RF_POWER			(   328)

/* dds_frequency with offset (24) */
#define DIB3000MB_REG_DDS_VALUE_MSB		(   339)
#define DIB3000MB_REG_DDS_VALUE_LSB		(   340)

/* timing offset signed (24) */
#define DIB3000MB_REG_TIMING_OFFSET_MSB	(   341)
#define DIB3000MB_REG_TIMING_OFFSET_LSB	(   342)

/* fft start position (13) */
#define DIB3000MB_REG_FFT_WINDOW_POS	(   353)

/* carriers locked (1) */
#define DIB3000MB_REG_CARRIER_LOCK		(   355)

/* noise power (24) */
#define DIB3000MB_REG_NOISE_POWER_MSB	(   372)
#define DIB3000MB_REG_NOISE_POWER_LSB	(   373)

#define DIB3000MB_REG_MOBILE_NOISE_MSB	(   374)
#define DIB3000MB_REG_MOBILE_NOISE_LSB	(   375)

/*
 * signal power (16), this and the above can be
 * used to calculate the signal/noise - ratio
 */
#define DIB3000MB_REG_SIGNAL_POWER		(   380)

/* mer (24) */
#define DIB3000MB_REG_MER_MSB			(   381)
#define DIB3000MB_REG_MER_LSB			(   382)

/*
 * Transmission Parameter Signalling (TPS)
 * the following registers can be used to get TPS-information.
 * The values are according to the DVB-T standard.
 */

/* TPS locked (1) */
#define DIB3000MB_REG_TPS_LOCK			(   394)

/* QAM from TPS (2) (values according to DIB3000MB_REG_QAM) */
#define DIB3000MB_REG_TPS_QAM			(   398)

/* hierarchy from TPS (1) */
#define DIB3000MB_REG_TPS_HRCH			(   399)

/* alpha from TPS (3) (values according to DIB3000MB_REG_VIT_ALPHA) */
#define DIB3000MB_REG_TPS_VIT_ALPHA		(   400)

/* code rate high priority from TPS (3) (values according to DIB3000MB_FEC_*) */
#define DIB3000MB_REG_TPS_CODE_RATE_HP	(   401)

/* code rate low priority from TPS (3) if DIB3000MB_REG_TPS_VIT_ALPHA */
#define DIB3000MB_REG_TPS_CODE_RATE_LP	(   402)

/* guard time from TPS (2) (values according to DIB3000MB_REG_GUARD_TIME */
#define DIB3000MB_REG_TPS_GUARD_TIME	(   403)

/* fft size from TPS (2) (values according to DIB3000MB_REG_FFT) */
#define DIB3000MB_REG_TPS_FFT			(   404)

/* cell id from TPS (16) */
#define DIB3000MB_REG_TPS_CELL_ID		(   406)

/* TPS (68) */
#define DIB3000MB_REG_TPS_1				(   408)
#define DIB3000MB_REG_TPS_2				(   409)
#define DIB3000MB_REG_TPS_3				(   410)
#define DIB3000MB_REG_TPS_4				(   411)
#define DIB3000MB_REG_TPS_5				(   412)

/* bit error rate (before RS correction) (21) */
#define DIB3000MB_REG_BER_MSB			(   414)
#define DIB3000MB_REG_BER_LSB			(   415)

/* packet error rate (uncorrected TS packets) (16) */
#define DIB3000MB_REG_PACKET_ERROR_RATE	(   417)

/* uncorrected packet count (16) */
#define DIB3000MB_REG_UNC				(   420)

/* viterbi locked (1) */
#define DIB3000MB_REG_VIT_LCK			(   421)

/* viterbi inidcator (16) */
#define DIB3000MB_REG_VIT_INDICATOR		(   422)

/* transport stream sync lock (1) */
#define DIB3000MB_REG_TS_SYNC_LOCK		(   423)

/* transport stream RS lock (1) */
#define DIB3000MB_REG_TS_RS_LOCK		(   424)

/* lock mask 0 value (1) */
#define DIB3000MB_REG_LOCK0_VALUE		(   425)

/* lock mask 1 value (1) */
#define DIB3000MB_REG_LOCK1_VALUE		(   426)

/* lock mask 2 value (1) */
#define DIB3000MB_REG_LOCK2_VALUE		(   427)

/* interrupt pending for auto search */
#define DIB3000MB_REG_AS_IRQ_PENDING	(   434)

#endif
