/*
 * dib3000mc_priv.h
 *
 * Copyright (C) 2004 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 * for more information see dib3000mc.c .
 */

#ifndef __DIB3000MC_PRIV_H__
#define __DIB3000MC_PRIV_H__

/*
 * Demodulator parameters
 * reg: 0  1 1  1 11 11 111
 *         | |  |  |  |  |
 *         | |  |  |  |  +-- alpha (000=0, 001=1, 010=2, 100=4)
 *         | |  |  |  +----- constellation (00=QPSK, 01=16QAM, 10=64QAM)
 *         | |  |  +-------- guard (00=1/32, 01=1/16, 10=1/8, 11=1/4)
 *         | |  +----------- transmission mode (0=2k, 1=8k)
 *         | |
 *         | +-------------- restart autosearch for parameters
 *         +---------------- restart the demodulator
 * reg: 181      1 111 1
 *               |  |  |
 *               |  |  +- FEC applies for HP or LP (0=LP, 1=HP)
 *               |  +---- FEC rate (001=1/2, 010=2/3, 011=3/4, 101=5/6, 111=7/8)
 *               +------- hierarchy on (0=no, 1=yes)
 */

/* demodulator tuning parameter and restart options */
#define DIB3000MC_REG_DEMOD_PARM		(     0)
#define DIB3000MC_DEMOD_PARM(a,c,g,t)	( \
		 (0x7 & a) | \
		((0x3 & c) << 3) | \
		((0x3 & g) << 5) | \
		((0x1 & t) << 7) )
#define DIB3000MC_DEMOD_RST_AUTO_SRCH_ON	(1 << 8)
#define DIB3000MC_DEMOD_RST_AUTO_SRCH_OFF	(0 << 8)
#define DIB3000MC_DEMOD_RST_DEMOD_ON		(1 << 9)
#define DIB3000MC_DEMOD_RST_DEMOD_OFF		(0 << 9)

/* register for hierarchy parameters */
#define DIB3000MC_REG_HRCH_PARM			(   181)
#define DIB3000MC_HRCH_PARM(s,f,h)		( \
		 (0x1 & s) | \
		((0x7 & f) << 1) | \
		((0x1 & h) << 4) )

/* timeout ??? */
#define DIB3000MC_REG_UNK_1				(     1)
#define DIB3000MC_UNK_1					(  0x04)

/* timeout ??? */
#define DIB3000MC_REG_UNK_2				(     2)
#define DIB3000MC_UNK_2					(  0x04)

/* timeout ??? */
#define DIB3000MC_REG_UNK_3				(     3)
#define DIB3000MC_UNK_3					(0x1000)

#define DIB3000MC_REG_UNK_4				(     4)
#define DIB3000MC_UNK_4					(0x0814)

/* timeout ??? */
#define DIB3000MC_REG_SEQ_TPS			(     5)
#define DIB3000MC_SEQ_TPS_DEFAULT		(     1)
#define DIB3000MC_SEQ_TPS(s,t)			( \
		((s & 0x0f) << 4) | \
		((t & 0x01) << 8) )
#define DIB3000MC_IS_TPS(v)				((v << 8) & 0x1)
#define DIB3000MC_IS_AS(v)				((v >> 4) & 0xf)

/* parameters for the bandwidth */
#define DIB3000MC_REG_BW_TIMOUT_MSB		(     6)
#define DIB3000MC_REG_BW_TIMOUT_LSB		(     7)

static u16 dib3000mc_reg_bandwidth[] = { 6,7,8,9,10,11,16,17 };

/*static u16 dib3000mc_bandwidth_5mhz[] =
	{ 0x28, 0x9380, 0x87, 0x4100, 0x2a4, 0x4500, 0x1, 0xb0d0 };*/

static u16 dib3000mc_bandwidth_6mhz[] =
	{ 0x21, 0xd040, 0x70, 0xb62b, 0x233, 0x8ed5, 0x1, 0xb0d0 };

static u16 dib3000mc_bandwidth_7mhz[] =
	{ 0x1c, 0xfba5, 0x60, 0x9c25, 0x1e3, 0x0cb7, 0x1, 0xb0d0 };

static u16 dib3000mc_bandwidth_8mhz[] =
	{ 0x19, 0x5c30, 0x54, 0x88a0, 0x1a6, 0xab20, 0x1, 0xb0d0 };

static u16 dib3000mc_reg_bandwidth_general[] = { 12,13,14,15 };
static u16 dib3000mc_bandwidth_general[] = { 0x0000, 0x03e8, 0x0000, 0x03f2 };

/* lock mask */
#define DIB3000MC_REG_LOCK_MASK			(    15)
#define DIB3000MC_ACTIVATE_LOCK_MASK	(0x0800)

/* reset the uncorrected packet count (??? do it 5 times) */
#define DIB3000MC_REG_RST_UNC			(    18)
#define DIB3000MC_RST_UNC_ON			(     1)
#define DIB3000MC_RST_UNC_OFF			(     0)

#define DIB3000MC_REG_UNK_19			(    19)
#define DIB3000MC_UNK_19				(     0)

/* DDS frequency value (IF position) and inversion bit */
#define DIB3000MC_REG_INVERSION			(    21)
#define DIB3000MC_REG_SET_DDS_FREQ_MSB	(    21)
#define DIB3000MC_DDS_FREQ_MSB_INV_OFF	(0x0164)
#define DIB3000MC_DDS_FREQ_MSB_INV_ON	(0x0364)

#define DIB3000MC_REG_SET_DDS_FREQ_LSB	(    22)
#define DIB3000MC_DDS_FREQ_LSB			(0x463d)

/* timing frequencies setting */
#define DIB3000MC_REG_TIMING_FREQ_MSB	(    23)
#define DIB3000MC_REG_TIMING_FREQ_LSB	(    24)
#define DIB3000MC_CLOCK_REF				(0x151fd1)

//static u16 dib3000mc_reg_timing_freq[] = { 23,24 };

//static u16 dib3000mc_timing_freq[][2] = {
//	{ 0x69, 0x9f18 }, /* 5 MHz */
//	{ 0x7e ,0xbee9 }, /* 6 MHz */
//	{ 0x93 ,0xdebb }, /* 7 MHz */
//	{ 0xa8 ,0xfe8c }, /* 8 MHz */
//};

/* timeout ??? */
static u16 dib3000mc_reg_offset[] = { 26,33 };

static u16 dib3000mc_offset[][2] = {
	{ 26240, 5 }, /* default */
	{ 30336, 6 }, /* 8K */
	{ 38528, 8 }, /* 2K */
};

#define DIB3000MC_REG_ISI				(    29)
#define DIB3000MC_ISI_DEFAULT			(0x1073)
#define DIB3000MC_ISI_ACTIVATE			(0x0000)
#define DIB3000MC_ISI_INHIBIT			(0x0200)

/* impulse noise control */
static u16 dib3000mc_reg_imp_noise_ctl[] = { 34,35 };

static u16 dib3000mc_imp_noise_ctl[][2] = {
	{ 0x1294, 0x1ff8 }, /* mode 0 */
	{ 0x1294, 0x1ff8 }, /* mode 1 */
	{ 0x1294, 0x1ff8 }, /* mode 2 */
	{ 0x1294, 0x1ff8 }, /* mode 3 */
	{ 0x1294, 0x1ff8 }, /* mode 4 */
};

/* AGC registers */
static u16 dib3000mc_reg_agc[] = {
	36,37,38,39,42,43,44,45,46,47,48,49
};

static u16 dib3000mc_agc_tuner[][12] = {
	{	0x0051, 0x301d, 0x0000, 0x1cc7, 0xcf5c, 0x6666,
		0xbae1, 0xa148, 0x3b5e, 0x3c1c, 0x001a, 0x2019
	}, /* TUNER_PANASONIC_ENV77H04D5, */

	{	0x0051, 0x301d, 0x0000, 0x1cc7, 0xdc29, 0x570a,
		0xbae1, 0x8ccd, 0x3b6d, 0x551d, 0x000a, 0x951e
	}, /* TUNER_PANASONIC_ENV57H13D5, TUNER_PANASONIC_ENV57H12D5 */

	{	0x0051, 0x301d, 0x0000, 0x1cc7, 0xffff, 0xffff,
		0xffff, 0x0000, 0xfdfd, 0x4040, 0x00fd, 0x4040
	}, /* TUNER_SAMSUNG_DTOS333IH102, TUNER_RFAGCIN_UNKNOWN */

	{	0x0196, 0x301d, 0x0000, 0x1cc7, 0xbd71, 0x5c29,
		0xb5c3, 0x6148, 0x6569, 0x5127, 0x0033, 0x3537
	}, /* TUNER_PROVIDER_X */
	/* TODO TUNER_PANASONIC_ENV57H10D8, TUNER_PANASONIC_ENV57H11D8 */
};

/* AGC loop bandwidth */
static u16 dib3000mc_reg_agc_bandwidth[] = { 40,41 };
static u16 dib3000mc_agc_bandwidth[]  = { 0x119,0x330 };

static u16 dib3000mc_reg_agc_bandwidth_general[] = { 50,51,52,53,54 };
static u16 dib3000mc_agc_bandwidth_general[] =
	{ 0x8000, 0x91ca, 0x01ba, 0x0087, 0x0087 };

#define DIB3000MC_REG_IMP_NOISE_55		(    55)
#define DIB3000MC_IMP_NEW_ALGO(w)		(w | (1<<10))

/* Impulse noise params */
static u16 dib3000mc_reg_impulse_noise[] = { 55,56,57 };
static u16 dib3000mc_impluse_noise[][3] = {
	{ 0x489, 0x89, 0x72 }, /* 5 MHz */
	{ 0x4a5, 0xa5, 0x89 }, /* 6 MHz */
	{ 0x4c0, 0xc0, 0xa0 }, /* 7 MHz */
	{ 0x4db, 0xdb, 0xb7 }, /* 8 Mhz */
};

static u16 dib3000mc_reg_fft[] = {
	58,59,60,61,62,63,64,65,66,67,68,69,
	70,71,72,73,74,75,76,77,78,79,80,81,
	82,83,84,85,86
};

static u16 dib3000mc_fft_modes[][29] = {
	{	0x38, 0x6d9, 0x3f28, 0x7a7, 0x3a74, 0x196, 0x32a, 0x48c,
		0x3ffe, 0x7f3, 0x2d94, 0x76, 0x53d,
		0x3ff8, 0x7e3, 0x3320, 0x76, 0x5b3,
		0x3feb, 0x7d2, 0x365e, 0x76, 0x48c,
		0x3ffe, 0x5b3, 0x3feb, 0x76,   0x0, 0xd
	}, /* fft mode 0 */
	{	0x3b, 0x6d9, 0x3f28, 0x7a7, 0x3a74, 0x196, 0x32a, 0x48c,
		0x3ffe, 0x7f3, 0x2d94, 0x76, 0x53d,
		0x3ff8, 0x7e3, 0x3320, 0x76, 0x5b3,
		0x3feb, 0x7d2, 0x365e, 0x76, 0x48c,
		0x3ffe, 0x5b3, 0x3feb, 0x0,  0x8200, 0xd
	}, /* fft mode 1 */
};

#define DIB3000MC_REG_UNK_88			(    88)
#define DIB3000MC_UNK_88				(0x0410)

static u16 dib3000mc_reg_bw[] = { 93,94,95,96,97,98 };
static u16 dib3000mc_bw[][6] = {
	{ 0,0,0,0,0,0 }, /* 5 MHz */
	{ 0,0,0,0,0,0 }, /* 6 MHz */
	{ 0,0,0,0,0,0 }, /* 7 MHz */
	{ 0x20, 0x21, 0x20, 0x23, 0x20, 0x27 }, /* 8 MHz */
};


/* phase noise control */
#define DIB3000MC_REG_UNK_99			(    99)
#define DIB3000MC_UNK_99				(0x0220)

#define DIB3000MC_REG_SCAN_BOOST		(   100)
#define DIB3000MC_SCAN_BOOST_ON			((11 << 6) + 6)
#define DIB3000MC_SCAN_BOOST_OFF		((16 << 6) + 9)

/* timeout ??? */
#define DIB3000MC_REG_UNK_110			(   110)
#define DIB3000MC_UNK_110				(  3277)

#define DIB3000MC_REG_UNK_111			(   111)
#define DIB3000MC_UNK_111_PH_N_MODE_0	(     0)
#define DIB3000MC_UNK_111_PH_N_MODE_1	(1 << 1)

/* superious rm config */
#define DIB3000MC_REG_UNK_120			(   120)
#define DIB3000MC_UNK_120				(  8207)

#define DIB3000MC_REG_UNK_133			(   133)
#define DIB3000MC_UNK_133				( 15564)

#define DIB3000MC_REG_UNK_134			(   134)
#define DIB3000MC_UNK_134				(     0)

/* adapter config for constellation */
static u16 dib3000mc_reg_adp_cfg[] = { 129, 130, 131, 132 };

static u16 dib3000mc_adp_cfg[][4] = {
	{ 0x99a, 0x7fae, 0x333, 0x7ff0 }, /* QPSK  */
	{ 0x23d, 0x7fdf, 0x0a4, 0x7ff0 }, /* 16-QAM */
	{ 0x148, 0x7ff0, 0x0a4, 0x7ff8 }, /* 64-QAM */
};

static u16 dib3000mc_reg_mobile_mode[] = { 139, 140, 141, 175, 1032 };

static u16 dib3000mc_mobile_mode[][5] = {
	{ 0x01, 0x0, 0x0, 0x00, 0x12c }, /* fixed */
	{ 0x01, 0x0, 0x0, 0x00, 0x12c }, /* portable */
	{ 0x00, 0x0, 0x0, 0x02, 0x000 }, /* mobile */
	{ 0x00, 0x0, 0x0, 0x02, 0x000 }, /* auto */
};

#define DIB3000MC_REG_DIVERSITY1		(   177)
#define DIB3000MC_DIVERSITY1_DEFAULT	(     1)

#define DIB3000MC_REG_DIVERSITY2		(   178)
#define DIB3000MC_DIVERSITY2_DEFAULT	(     1)

#define DIB3000MC_REG_DIVERSITY3		(   180)
#define DIB3000MC_DIVERSITY3_IN_OFF		(0xfff0)
#define DIB3000MC_DIVERSITY3_IN_ON		(0xfff6)

#define DIB3000MC_REG_FEC_CFG			(   195)
#define DIB3000MC_FEC_CFG				(  0x10)

/*
 * reg 206, output mode
 *              1111 1111
 *              |||| ||||
 *              |||| |||+- unk
 *              |||| ||+-- unk
 *              |||| |+--- unk (on by default)
 *              |||| +---- fifo_ctrl (1 = inhibit (flushed), 0 = active (unflushed))
 *              |||+------ pid_parse (1 = enabled, 0 = disabled)
 *              ||+------- outp_188  (1 = TS packet size 188, 0 = packet size 204)
 *              |+-------- unk
 *              +--------- unk
 */

#define DIB3000MC_REG_SMO_MODE			(   206)
#define DIB3000MC_SMO_MODE_DEFAULT		(1 << 2)
#define DIB3000MC_SMO_MODE_FIFO_FLUSH	(1 << 3)
#define DIB3000MC_SMO_MODE_FIFO_UNFLUSH	(0xfff7)
#define DIB3000MC_SMO_MODE_PID_PARSE	(1 << 4)
#define DIB3000MC_SMO_MODE_NO_PID_PARSE	(0xffef)
#define DIB3000MC_SMO_MODE_188			(1 << 5)
#define DIB3000MC_SMO_MODE_SLAVE		(DIB3000MC_SMO_MODE_DEFAULT | \
			DIB3000MC_SMO_MODE_188 | DIB3000MC_SMO_MODE_PID_PARSE | (1<<1))

#define DIB3000MC_REG_FIFO_THRESHOLD	(   207)
#define DIB3000MC_FIFO_THRESHOLD_DEFAULT	(  1792)
#define DIB3000MC_FIFO_THRESHOLD_SLAVE	(   512)
/*
 * pidfilter
 * it is not a hardware pidfilter but a filter which drops all pids
 * except the ones set. When connected to USB1.1 bandwidth this is important.
 * DiB3000P/M-C can filter up to 32 PIDs
 */
#define DIB3000MC_REG_FIRST_PID			(   212)
#define DIB3000MC_NUM_PIDS				(    32)

#define DIB3000MC_REG_OUTMODE			(   244)
#define DIB3000MC_OM_PARALLEL_GATED_CLK	(     0)
#define DIB3000MC_OM_PAR_CONT_CLK		(1 << 11)
#define DIB3000MC_OM_SERIAL				(2 << 11)
#define DIB3000MC_OM_DIVOUT_ON			(4 << 11)
#define DIB3000MC_OM_SLAVE				(DIB3000MC_OM_DIVOUT_ON | DIB3000MC_OM_PAR_CONT_CLK)

#define DIB3000MC_REG_RF_POWER			(   392)

#define DIB3000MC_REG_FFT_POSITION		(   407)

#define DIB3000MC_REG_DDS_FREQ_MSB		(   414)
#define DIB3000MC_REG_DDS_FREQ_LSB		(   415)

#define DIB3000MC_REG_TIMING_OFFS_MSB	(   416)
#define DIB3000MC_REG_TIMING_OFFS_LSB	(   417)

#define DIB3000MC_REG_TUNING_PARM		(   458)
#define DIB3000MC_TP_QAM(v)				((v >> 13) & 0x03)
#define DIB3000MC_TP_HRCH(v)			((v >> 12) & 0x01)
#define DIB3000MC_TP_ALPHA(v)			((v >> 9) & 0x07)
#define DIB3000MC_TP_FFT(v)				((v >> 8) & 0x01)
#define DIB3000MC_TP_FEC_CR_HP(v)		((v >> 5) & 0x07)
#define DIB3000MC_TP_FEC_CR_LP(v)		((v >> 2) & 0x07)
#define DIB3000MC_TP_GUARD(v)			(v & 0x03)

#define DIB3000MC_REG_SIGNAL_NOISE_MSB	(   483)
#define DIB3000MC_REG_SIGNAL_NOISE_LSB	(   484)

#define DIB3000MC_REG_MER				(   485)

#define DIB3000MC_REG_BER_MSB			(   500)
#define DIB3000MC_REG_BER_LSB			(   501)

#define DIB3000MC_REG_PACKET_ERRORS		(   503)

#define DIB3000MC_REG_PACKET_ERROR_COUNT	(   506)

#define DIB3000MC_REG_LOCK_507			(   507)
#define DIB3000MC_LOCK_507				(0x0002) // ? name correct ?

#define DIB3000MC_REG_LOCKING			(   509)
#define DIB3000MC_AGC_LOCK(v)			(v & 0x8000)
#define DIB3000MC_CARRIER_LOCK(v)		(v & 0x2000)
#define DIB3000MC_MPEG_SYNC_LOCK(v)		(v & 0x0080)
#define DIB3000MC_MPEG_DATA_LOCK(v)		(v & 0x0040)
#define DIB3000MC_TPS_LOCK(v)			(v & 0x0004)

#define DIB3000MC_REG_AS_IRQ			(   511)
#define DIB3000MC_AS_IRQ_SUCCESS		(1 << 1)
#define DIB3000MC_AS_IRQ_FAIL			(     1)

#define DIB3000MC_REG_TUNER				(   769)

#define DIB3000MC_REG_RST_I2C_ADDR		(  1024)
#define DIB3000MC_DEMOD_ADDR_ON			(     1)
#define DIB3000MC_DEMOD_ADDR(a)			((a << 4) & 0x03F0)

#define DIB3000MC_REG_RESTART			(  1027)
#define DIB3000MC_RESTART_OFF			(0x0000)
#define DIB3000MC_RESTART_AGC			(0x0800)
#define DIB3000MC_RESTART_CONFIG		(0x8000)

#define DIB3000MC_REG_RESTART_VIT		(  1028)
#define DIB3000MC_RESTART_VIT_OFF		(     0)
#define DIB3000MC_RESTART_VIT_ON		(     1)

#define DIB3000MC_REG_CLK_CFG_1			(  1031)
#define DIB3000MC_CLK_CFG_1_POWER_UP	(     0)
#define DIB3000MC_CLK_CFG_1_POWER_DOWN	(0xffff)

#define DIB3000MC_REG_CLK_CFG_2			(  1032)
#define DIB3000MC_CLK_CFG_2_PUP_FIXED	(0x012c)
#define DIB3000MC_CLK_CFG_2_PUP_PORT	(0x0104)
#define DIB3000MC_CLK_CFG_2_PUP_MOBILE  (0x0000)
#define DIB3000MC_CLK_CFG_2_POWER_DOWN	(0xffff)

#define DIB3000MC_REG_CLK_CFG_3			(  1033)
#define DIB3000MC_CLK_CFG_3_POWER_UP	(     0)
#define DIB3000MC_CLK_CFG_3_POWER_DOWN	(0xfff5)

#define DIB3000MC_REG_CLK_CFG_7			(  1037)
#define DIB3000MC_CLK_CFG_7_INIT		( 12592)
#define DIB3000MC_CLK_CFG_7_POWER_UP	(~0x0003)
#define DIB3000MC_CLK_CFG_7_PWR_DOWN	(0x0003)
#define DIB3000MC_CLK_CFG_7_DIV_IN_OFF	(1 << 8)

/* was commented out ??? */
#define DIB3000MC_REG_CLK_CFG_8			(  1038)
#define DIB3000MC_CLK_CFG_8_POWER_UP	(0x160c)

#define DIB3000MC_REG_CLK_CFG_9			(  1039)
#define DIB3000MC_CLK_CFG_9_POWER_UP	(     0)

/* also clock ??? */
#define DIB3000MC_REG_ELEC_OUT			(  1040)
#define DIB3000MC_ELEC_OUT_HIGH_Z		(     0)
#define DIB3000MC_ELEC_OUT_DIV_OUT_ON	(     1)
#define DIB3000MC_ELEC_OUT_SLAVE		(     3)

#endif
