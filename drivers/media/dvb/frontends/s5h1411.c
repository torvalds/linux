/*
    Samsung S5H1411 VSB/QAM demodulator driver

    Copyright (C) 2008 Steven Toth <stoth@linuxtv.org>

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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "dvb_frontend.h"
#include "s5h1411.h"

struct s5h1411_state {

	struct i2c_adapter *i2c;

	/* configuration settings */
	const struct s5h1411_config *config;

	struct dvb_frontend frontend;

	fe_modulation_t current_modulation;
	unsigned int first_tune:1;

	u32 current_frequency;
	int if_freq;

	u8 inversion;
};

static int debug;

#define dprintk(arg...) do {	\
	if (debug)		\
		printk(arg);	\
	} while (0)

/* Register values to initialise the demod, defaults to VSB */
static struct init_tab {
	u8	addr;
	u8	reg;
	u16	data;
} init_tab[] = {
	{ S5H1411_I2C_TOP_ADDR, 0x00, 0x0071, },
	{ S5H1411_I2C_TOP_ADDR, 0x08, 0x0047, },
	{ S5H1411_I2C_TOP_ADDR, 0x1c, 0x0400, },
	{ S5H1411_I2C_TOP_ADDR, 0x1e, 0x0370, },
	{ S5H1411_I2C_TOP_ADDR, 0x1f, 0x342c, },
	{ S5H1411_I2C_TOP_ADDR, 0x24, 0x0231, },
	{ S5H1411_I2C_TOP_ADDR, 0x25, 0x1011, },
	{ S5H1411_I2C_TOP_ADDR, 0x26, 0x0f07, },
	{ S5H1411_I2C_TOP_ADDR, 0x27, 0x0f04, },
	{ S5H1411_I2C_TOP_ADDR, 0x28, 0x070f, },
	{ S5H1411_I2C_TOP_ADDR, 0x29, 0x2820, },
	{ S5H1411_I2C_TOP_ADDR, 0x2a, 0x102e, },
	{ S5H1411_I2C_TOP_ADDR, 0x2b, 0x0220, },
	{ S5H1411_I2C_TOP_ADDR, 0x2e, 0x0d0e, },
	{ S5H1411_I2C_TOP_ADDR, 0x2f, 0x1013, },
	{ S5H1411_I2C_TOP_ADDR, 0x31, 0x171b, },
	{ S5H1411_I2C_TOP_ADDR, 0x32, 0x0e0f, },
	{ S5H1411_I2C_TOP_ADDR, 0x33, 0x0f10, },
	{ S5H1411_I2C_TOP_ADDR, 0x34, 0x170e, },
	{ S5H1411_I2C_TOP_ADDR, 0x35, 0x4b10, },
	{ S5H1411_I2C_TOP_ADDR, 0x36, 0x0f17, },
	{ S5H1411_I2C_TOP_ADDR, 0x3c, 0x1577, },
	{ S5H1411_I2C_TOP_ADDR, 0x3d, 0x081a, },
	{ S5H1411_I2C_TOP_ADDR, 0x3e, 0x77ee, },
	{ S5H1411_I2C_TOP_ADDR, 0x40, 0x1e09, },
	{ S5H1411_I2C_TOP_ADDR, 0x41, 0x0f0c, },
	{ S5H1411_I2C_TOP_ADDR, 0x42, 0x1f10, },
	{ S5H1411_I2C_TOP_ADDR, 0x4d, 0x0509, },
	{ S5H1411_I2C_TOP_ADDR, 0x4e, 0x0a00, },
	{ S5H1411_I2C_TOP_ADDR, 0x50, 0x0000, },
	{ S5H1411_I2C_TOP_ADDR, 0x5b, 0x0000, },
	{ S5H1411_I2C_TOP_ADDR, 0x5c, 0x0008, },
	{ S5H1411_I2C_TOP_ADDR, 0x57, 0x1101, },
	{ S5H1411_I2C_TOP_ADDR, 0x65, 0x007c, },
	{ S5H1411_I2C_TOP_ADDR, 0x68, 0x0512, },
	{ S5H1411_I2C_TOP_ADDR, 0x69, 0x0258, },
	{ S5H1411_I2C_TOP_ADDR, 0x70, 0x0004, },
	{ S5H1411_I2C_TOP_ADDR, 0x71, 0x0007, },
	{ S5H1411_I2C_TOP_ADDR, 0x76, 0x00a9, },
	{ S5H1411_I2C_TOP_ADDR, 0x78, 0x3141, },
	{ S5H1411_I2C_TOP_ADDR, 0x7a, 0x3141, },
	{ S5H1411_I2C_TOP_ADDR, 0xb3, 0x8003, },
	{ S5H1411_I2C_TOP_ADDR, 0xb5, 0xa6bb, },
	{ S5H1411_I2C_TOP_ADDR, 0xb6, 0x0609, },
	{ S5H1411_I2C_TOP_ADDR, 0xb7, 0x2f06, },
	{ S5H1411_I2C_TOP_ADDR, 0xb8, 0x003f, },
	{ S5H1411_I2C_TOP_ADDR, 0xb9, 0x2700, },
	{ S5H1411_I2C_TOP_ADDR, 0xba, 0xfac8, },
	{ S5H1411_I2C_TOP_ADDR, 0xbe, 0x1003, },
	{ S5H1411_I2C_TOP_ADDR, 0xbf, 0x103f, },
	{ S5H1411_I2C_TOP_ADDR, 0xce, 0x2000, },
	{ S5H1411_I2C_TOP_ADDR, 0xcf, 0x0800, },
	{ S5H1411_I2C_TOP_ADDR, 0xd0, 0x0800, },
	{ S5H1411_I2C_TOP_ADDR, 0xd1, 0x0400, },
	{ S5H1411_I2C_TOP_ADDR, 0xd2, 0x0800, },
	{ S5H1411_I2C_TOP_ADDR, 0xd3, 0x2000, },
	{ S5H1411_I2C_TOP_ADDR, 0xd4, 0x3000, },
	{ S5H1411_I2C_TOP_ADDR, 0xdb, 0x4a9b, },
	{ S5H1411_I2C_TOP_ADDR, 0xdc, 0x1000, },
	{ S5H1411_I2C_TOP_ADDR, 0xde, 0x0001, },
	{ S5H1411_I2C_TOP_ADDR, 0xdf, 0x0000, },
	{ S5H1411_I2C_TOP_ADDR, 0xe3, 0x0301, },
	{ S5H1411_I2C_QAM_ADDR, 0xf3, 0x0000, },
	{ S5H1411_I2C_QAM_ADDR, 0xf3, 0x0001, },
	{ S5H1411_I2C_QAM_ADDR, 0x08, 0x0600, },
	{ S5H1411_I2C_QAM_ADDR, 0x18, 0x4201, },
	{ S5H1411_I2C_QAM_ADDR, 0x1e, 0x6476, },
	{ S5H1411_I2C_QAM_ADDR, 0x21, 0x0830, },
	{ S5H1411_I2C_QAM_ADDR, 0x0c, 0x5679, },
	{ S5H1411_I2C_QAM_ADDR, 0x0d, 0x579b, },
	{ S5H1411_I2C_QAM_ADDR, 0x24, 0x0102, },
	{ S5H1411_I2C_QAM_ADDR, 0x31, 0x7488, },
	{ S5H1411_I2C_QAM_ADDR, 0x32, 0x0a08, },
	{ S5H1411_I2C_QAM_ADDR, 0x3d, 0x8689, },
	{ S5H1411_I2C_QAM_ADDR, 0x49, 0x0048, },
	{ S5H1411_I2C_QAM_ADDR, 0x57, 0x2012, },
	{ S5H1411_I2C_QAM_ADDR, 0x5d, 0x7676, },
	{ S5H1411_I2C_QAM_ADDR, 0x04, 0x0400, },
	{ S5H1411_I2C_QAM_ADDR, 0x58, 0x00c0, },
	{ S5H1411_I2C_QAM_ADDR, 0x5b, 0x0100, },
};

/* VSB SNR lookup table */
static struct vsb_snr_tab {
	u16	val;
	u16	data;
} vsb_snr_tab[] = {
	{  0x39f, 300, },
	{  0x39b, 295, },
	{  0x397, 290, },
	{  0x394, 285, },
	{  0x38f, 280, },
	{  0x38b, 275, },
	{  0x387, 270, },
	{  0x382, 265, },
	{  0x37d, 260, },
	{  0x377, 255, },
	{  0x370, 250, },
	{  0x36a, 245, },
	{  0x364, 240, },
	{  0x35b, 235, },
	{  0x353, 230, },
	{  0x349, 225, },
	{  0x340, 320, },
	{  0x337, 215, },
	{  0x327, 210, },
	{  0x31b, 205, },
	{  0x310, 200, },
	{  0x302, 195, },
	{  0x2f3, 190, },
	{  0x2e4, 185, },
	{  0x2d7, 180, },
	{  0x2cd, 175, },
	{  0x2bb, 170, },
	{  0x2a9, 165, },
	{  0x29e, 160, },
	{  0x284, 155, },
	{  0x27a, 150, },
	{  0x260, 145, },
	{  0x23a, 140, },
	{  0x224, 135, },
	{  0x213, 130, },
	{  0x204, 125, },
	{  0x1fe, 120, },
	{      0,   0, },
};

/* QAM64 SNR lookup table */
static struct qam64_snr_tab {
	u16	val;
	u16	data;
} qam64_snr_tab[] = {
	{  0x0001,   0, },
	{  0x0af0, 300, },
	{  0x0d80, 290, },
	{  0x10a0, 280, },
	{  0x14b5, 270, },
	{  0x1590, 268, },
	{  0x1680, 266, },
	{  0x17b0, 264, },
	{  0x18c0, 262, },
	{  0x19b0, 260, },
	{  0x1ad0, 258, },
	{  0x1d00, 256, },
	{  0x1da0, 254, },
	{  0x1ef0, 252, },
	{  0x2050, 250, },
	{  0x20f0, 249, },
	{  0x21d0, 248, },
	{  0x22b0, 247, },
	{  0x23a0, 246, },
	{  0x2470, 245, },
	{  0x24f0, 244, },
	{  0x25a0, 243, },
	{  0x26c0, 242, },
	{  0x27b0, 241, },
	{  0x28d0, 240, },
	{  0x29b0, 239, },
	{  0x2ad0, 238, },
	{  0x2ba0, 237, },
	{  0x2c80, 236, },
	{  0x2d20, 235, },
	{  0x2e00, 234, },
	{  0x2f10, 233, },
	{  0x3050, 232, },
	{  0x3190, 231, },
	{  0x3300, 230, },
	{  0x3340, 229, },
	{  0x3200, 228, },
	{  0x3550, 227, },
	{  0x3610, 226, },
	{  0x3600, 225, },
	{  0x3700, 224, },
	{  0x3800, 223, },
	{  0x3920, 222, },
	{  0x3a20, 221, },
	{  0x3b30, 220, },
	{  0x3d00, 219, },
	{  0x3e00, 218, },
	{  0x4000, 217, },
	{  0x4100, 216, },
	{  0x4300, 215, },
	{  0x4400, 214, },
	{  0x4600, 213, },
	{  0x4700, 212, },
	{  0x4800, 211, },
	{  0x4a00, 210, },
	{  0x4b00, 209, },
	{  0x4d00, 208, },
	{  0x4f00, 207, },
	{  0x5050, 206, },
	{  0x5200, 205, },
	{  0x53c0, 204, },
	{  0x5450, 203, },
	{  0x5650, 202, },
	{  0x5820, 201, },
	{  0x6000, 200, },
	{  0xffff,   0, },
};

/* QAM256 SNR lookup table */
static struct qam256_snr_tab {
	u16	val;
	u16	data;
} qam256_snr_tab[] = {
	{  0x0001,   0, },
	{  0x0970, 400, },
	{  0x0a90, 390, },
	{  0x0b90, 380, },
	{  0x0d90, 370, },
	{  0x0ff0, 360, },
	{  0x1240, 350, },
	{  0x1345, 348, },
	{  0x13c0, 346, },
	{  0x14c0, 344, },
	{  0x1500, 342, },
	{  0x1610, 340, },
	{  0x1700, 338, },
	{  0x1800, 336, },
	{  0x18b0, 334, },
	{  0x1900, 332, },
	{  0x1ab0, 330, },
	{  0x1bc0, 328, },
	{  0x1cb0, 326, },
	{  0x1db0, 324, },
	{  0x1eb0, 322, },
	{  0x2030, 320, },
	{  0x2200, 318, },
	{  0x2280, 316, },
	{  0x2410, 314, },
	{  0x25b0, 312, },
	{  0x27a0, 310, },
	{  0x2840, 308, },
	{  0x29d0, 306, },
	{  0x2b10, 304, },
	{  0x2d30, 302, },
	{  0x2f20, 300, },
	{  0x30c0, 298, },
	{  0x3260, 297, },
	{  0x32c0, 296, },
	{  0x3300, 295, },
	{  0x33b0, 294, },
	{  0x34b0, 293, },
	{  0x35a0, 292, },
	{  0x3650, 291, },
	{  0x3800, 290, },
	{  0x3900, 289, },
	{  0x3a50, 288, },
	{  0x3b30, 287, },
	{  0x3cb0, 286, },
	{  0x3e20, 285, },
	{  0x3fa0, 284, },
	{  0x40a0, 283, },
	{  0x41c0, 282, },
	{  0x42f0, 281, },
	{  0x44a0, 280, },
	{  0x4600, 279, },
	{  0x47b0, 278, },
	{  0x4900, 277, },
	{  0x4a00, 276, },
	{  0x4ba0, 275, },
	{  0x4d00, 274, },
	{  0x4f00, 273, },
	{  0x5000, 272, },
	{  0x51f0, 272, },
	{  0x53a0, 270, },
	{  0x5520, 269, },
	{  0x5700, 268, },
	{  0x5800, 267, },
	{  0x5a00, 266, },
	{  0x5c00, 265, },
	{  0x5d00, 264, },
	{  0x5f00, 263, },
	{  0x6000, 262, },
	{  0x6200, 261, },
	{  0x6400, 260, },
	{  0xffff,   0, },
};

/* 8 bit registers, 16 bit values */
static int s5h1411_writereg(struct s5h1411_state *state,
	u8 addr, u8 reg, u16 data)
{
	int ret;
	u8 buf[] = { reg, data >> 8,  data & 0xff };

	struct i2c_msg msg = { .addr = addr, .flags = 0, .buf = buf, .len = 3 };

	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1)
		printk(KERN_ERR "%s: writereg error 0x%02x 0x%02x 0x%04x, "
		       "ret == %i)\n", __func__, addr, reg, data, ret);

	return (ret != 1) ? -1 : 0;
}

static u16 s5h1411_readreg(struct s5h1411_state *state, u8 addr, u8 reg)
{
	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0, 0 };

	struct i2c_msg msg[] = {
		{ .addr = addr, .flags = 0, .buf = b0, .len = 1 },
		{ .addr = addr, .flags = I2C_M_RD, .buf = b1, .len = 2 } };

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2)
		printk(KERN_ERR "%s: readreg error (ret == %i)\n",
			__func__, ret);
	return (b1[0] << 8) | b1[1];
}

static int s5h1411_softreset(struct dvb_frontend *fe)
{
	struct s5h1411_state *state = fe->demodulator_priv;

	dprintk("%s()\n", __func__);

	s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0xf7, 0);
	s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0xf7, 1);
	return 0;
}

static int s5h1411_set_if_freq(struct dvb_frontend *fe, int KHz)
{
	struct s5h1411_state *state = fe->demodulator_priv;

	dprintk("%s(%d KHz)\n", __func__, KHz);

	switch (KHz) {
	case 3250:
		s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0x38, 0x10d5);
		s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0x39, 0x5342);
		s5h1411_writereg(state, S5H1411_I2C_QAM_ADDR, 0x2c, 0x10d9);
		break;
	case 3500:
		s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0x38, 0x1225);
		s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0x39, 0x1e96);
		s5h1411_writereg(state, S5H1411_I2C_QAM_ADDR, 0x2c, 0x1225);
		break;
	case 4000:
		s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0x38, 0x14bc);
		s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0x39, 0xb53e);
		s5h1411_writereg(state, S5H1411_I2C_QAM_ADDR, 0x2c, 0x14bd);
		break;
	default:
		dprintk("%s(%d KHz) Invalid, defaulting to 5380\n",
			__func__, KHz);
		/* no break, need to continue */
	case 5380:
	case 44000:
		s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0x38, 0x1be4);
		s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0x39, 0x3655);
		s5h1411_writereg(state, S5H1411_I2C_QAM_ADDR, 0x2c, 0x1be4);
		break;
	}

	state->if_freq = KHz;

	return 0;
}

static int s5h1411_set_mpeg_timing(struct dvb_frontend *fe, int mode)
{
	struct s5h1411_state *state = fe->demodulator_priv;
	u16 val;

	dprintk("%s(%d)\n", __func__, mode);

	val = s5h1411_readreg(state, S5H1411_I2C_TOP_ADDR, 0xbe) & 0xcfff;
	switch (mode) {
	case S5H1411_MPEGTIMING_CONTINOUS_INVERTING_CLOCK:
		val |= 0x0000;
		break;
	case S5H1411_MPEGTIMING_CONTINOUS_NONINVERTING_CLOCK:
		dprintk("%s(%d) Mode1 or Defaulting\n", __func__, mode);
		val |= 0x1000;
		break;
	case S5H1411_MPEGTIMING_NONCONTINOUS_INVERTING_CLOCK:
		val |= 0x2000;
		break;
	case S5H1411_MPEGTIMING_NONCONTINOUS_NONINVERTING_CLOCK:
		val |= 0x3000;
		break;
	default:
		return -EINVAL;
	}

	/* Configure MPEG Signal Timing charactistics */
	return s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0xbe, val);
}

static int s5h1411_set_spectralinversion(struct dvb_frontend *fe, int inversion)
{
	struct s5h1411_state *state = fe->demodulator_priv;
	u16 val;

	dprintk("%s(%d)\n", __func__, inversion);
	val = s5h1411_readreg(state, S5H1411_I2C_TOP_ADDR, 0x24) & ~0x1000;

	if (inversion == 1)
		val |= 0x1000; /* Inverted */

	state->inversion = inversion;
	return s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0x24, val);
}

static int s5h1411_set_serialmode(struct dvb_frontend *fe, int serial)
{
	struct s5h1411_state *state = fe->demodulator_priv;
	u16 val;

	dprintk("%s(%d)\n", __func__, serial);
	val = s5h1411_readreg(state, S5H1411_I2C_TOP_ADDR, 0xbd) & ~0x100;

	if (serial == 1)
		val |= 0x100;

	return s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0xbd, val);
}

static int s5h1411_enable_modulation(struct dvb_frontend *fe,
				     fe_modulation_t m)
{
	struct s5h1411_state *state = fe->demodulator_priv;

	dprintk("%s(0x%08x)\n", __func__, m);

	if ((state->first_tune == 0) && (m == state->current_modulation)) {
		dprintk("%s() Already at desired modulation.  Skipping...\n",
			__func__);
		return 0;
	}

	switch (m) {
	case VSB_8:
		dprintk("%s() VSB_8\n", __func__);
		s5h1411_set_if_freq(fe, state->config->vsb_if);
		s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0x00, 0x71);
		s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0xf6, 0x00);
		s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0xcd, 0xf1);
		break;
	case QAM_64:
	case QAM_256:
	case QAM_AUTO:
		dprintk("%s() QAM_AUTO (64/256)\n", __func__);
		s5h1411_set_if_freq(fe, state->config->qam_if);
		s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0x00, 0x0171);
		s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0xf6, 0x0001);
		s5h1411_writereg(state, S5H1411_I2C_QAM_ADDR, 0x16, 0x1101);
		s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0xcd, 0x00f0);
		break;
	default:
		dprintk("%s() Invalid modulation\n", __func__);
		return -EINVAL;
	}

	state->current_modulation = m;
	state->first_tune = 0;
	s5h1411_softreset(fe);

	return 0;
}

static int s5h1411_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct s5h1411_state *state = fe->demodulator_priv;

	dprintk("%s(%d)\n", __func__, enable);

	if (enable)
		return s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0xf5, 1);
	else
		return s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0xf5, 0);
}

static int s5h1411_set_gpio(struct dvb_frontend *fe, int enable)
{
	struct s5h1411_state *state = fe->demodulator_priv;
	u16 val;

	dprintk("%s(%d)\n", __func__, enable);

	val = s5h1411_readreg(state, S5H1411_I2C_TOP_ADDR, 0xe0) & ~0x02;

	if (enable)
		return s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0xe0,
				val | 0x02);
	else
		return s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0xe0, val);
}

static int s5h1411_set_powerstate(struct dvb_frontend *fe, int enable)
{
	struct s5h1411_state *state = fe->demodulator_priv;

	dprintk("%s(%d)\n", __func__, enable);

	if (enable)
		s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0xf4, 1);
	else {
		s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0xf4, 0);
		s5h1411_softreset(fe);
	}

	return 0;
}

static int s5h1411_sleep(struct dvb_frontend *fe)
{
	return s5h1411_set_powerstate(fe, 1);
}

static int s5h1411_register_reset(struct dvb_frontend *fe)
{
	struct s5h1411_state *state = fe->demodulator_priv;

	dprintk("%s()\n", __func__);

	return s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0xf3, 0);
}

/* Talk to the demod, set the FEC, GUARD, QAM settings etc */
static int s5h1411_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct s5h1411_state *state = fe->demodulator_priv;

	dprintk("%s(frequency=%d)\n", __func__, p->frequency);

	s5h1411_softreset(fe);

	state->current_frequency = p->frequency;

	s5h1411_enable_modulation(fe, p->modulation);

	if (fe->ops.tuner_ops.set_params) {
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);

		fe->ops.tuner_ops.set_params(fe);

		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	/* Issue a reset to the demod so it knows to resync against the
	   newly tuned frequency */
	s5h1411_softreset(fe);

	return 0;
}

/* Reset the demod hardware and reset all of the configuration registers
   to a default state. */
static int s5h1411_init(struct dvb_frontend *fe)
{
	struct s5h1411_state *state = fe->demodulator_priv;
	int i;

	dprintk("%s()\n", __func__);

	s5h1411_set_powerstate(fe, 0);
	s5h1411_register_reset(fe);

	for (i = 0; i < ARRAY_SIZE(init_tab); i++)
		s5h1411_writereg(state, init_tab[i].addr,
			init_tab[i].reg,
			init_tab[i].data);

	/* The datasheet says that after initialisation, VSB is default */
	state->current_modulation = VSB_8;

	/* Although the datasheet says it's in VSB, empirical evidence
	   shows problems getting lock on the first tuning request.  Make
	   sure we call enable_modulation the first time around */
	state->first_tune = 1;

	if (state->config->output_mode == S5H1411_SERIAL_OUTPUT)
		/* Serial */
		s5h1411_set_serialmode(fe, 1);
	else
		/* Parallel */
		s5h1411_set_serialmode(fe, 0);

	s5h1411_set_spectralinversion(fe, state->config->inversion);
	s5h1411_set_if_freq(fe, state->config->vsb_if);
	s5h1411_set_gpio(fe, state->config->gpio);
	s5h1411_set_mpeg_timing(fe, state->config->mpeg_timing);
	s5h1411_softreset(fe);

	/* Note: Leaving the I2C gate closed. */
	s5h1411_i2c_gate_ctrl(fe, 0);

	return 0;
}

static int s5h1411_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct s5h1411_state *state = fe->demodulator_priv;
	u16 reg;
	u32 tuner_status = 0;

	*status = 0;

	/* Register F2 bit 15 = Master Lock, removed */

	switch (state->current_modulation) {
	case QAM_64:
	case QAM_256:
		reg = s5h1411_readreg(state, S5H1411_I2C_TOP_ADDR, 0xf0);
		if (reg & 0x10) /* QAM FEC Lock */
			*status |= FE_HAS_SYNC | FE_HAS_LOCK;
		if (reg & 0x100) /* QAM EQ Lock */
			*status |= FE_HAS_VITERBI | FE_HAS_CARRIER | FE_HAS_SIGNAL;

		break;
	case VSB_8:
		reg = s5h1411_readreg(state, S5H1411_I2C_TOP_ADDR, 0xf2);
		if (reg & 0x1000) /* FEC Lock */
			*status |= FE_HAS_SYNC | FE_HAS_LOCK;
		if (reg & 0x2000) /* EQ Lock */
			*status |= FE_HAS_VITERBI | FE_HAS_CARRIER | FE_HAS_SIGNAL;

		reg = s5h1411_readreg(state, S5H1411_I2C_TOP_ADDR, 0x53);
		if (reg & 0x1) /* AFC Lock */
			*status |= FE_HAS_SIGNAL;

		break;
	default:
		return -EINVAL;
	}

	switch (state->config->status_mode) {
	case S5H1411_DEMODLOCKING:
		if (*status & FE_HAS_VITERBI)
			*status |= FE_HAS_CARRIER | FE_HAS_SIGNAL;
		break;
	case S5H1411_TUNERLOCKING:
		/* Get the tuner status */
		if (fe->ops.tuner_ops.get_status) {
			if (fe->ops.i2c_gate_ctrl)
				fe->ops.i2c_gate_ctrl(fe, 1);

			fe->ops.tuner_ops.get_status(fe, &tuner_status);

			if (fe->ops.i2c_gate_ctrl)
				fe->ops.i2c_gate_ctrl(fe, 0);
		}
		if (tuner_status)
			*status |= FE_HAS_CARRIER | FE_HAS_SIGNAL;
		break;
	}

	dprintk("%s() status 0x%08x\n", __func__, *status);

	return 0;
}

static int s5h1411_qam256_lookup_snr(struct dvb_frontend *fe, u16 *snr, u16 v)
{
	int i, ret = -EINVAL;
	dprintk("%s()\n", __func__);

	for (i = 0; i < ARRAY_SIZE(qam256_snr_tab); i++) {
		if (v < qam256_snr_tab[i].val) {
			*snr = qam256_snr_tab[i].data;
			ret = 0;
			break;
		}
	}
	return ret;
}

static int s5h1411_qam64_lookup_snr(struct dvb_frontend *fe, u16 *snr, u16 v)
{
	int i, ret = -EINVAL;
	dprintk("%s()\n", __func__);

	for (i = 0; i < ARRAY_SIZE(qam64_snr_tab); i++) {
		if (v < qam64_snr_tab[i].val) {
			*snr = qam64_snr_tab[i].data;
			ret = 0;
			break;
		}
	}
	return ret;
}

static int s5h1411_vsb_lookup_snr(struct dvb_frontend *fe, u16 *snr, u16 v)
{
	int i, ret = -EINVAL;
	dprintk("%s()\n", __func__);

	for (i = 0; i < ARRAY_SIZE(vsb_snr_tab); i++) {
		if (v > vsb_snr_tab[i].val) {
			*snr = vsb_snr_tab[i].data;
			ret = 0;
			break;
		}
	}
	dprintk("%s() snr=%d\n", __func__, *snr);
	return ret;
}

static int s5h1411_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct s5h1411_state *state = fe->demodulator_priv;
	u16 reg;
	dprintk("%s()\n", __func__);

	switch (state->current_modulation) {
	case QAM_64:
		reg = s5h1411_readreg(state, S5H1411_I2C_TOP_ADDR, 0xf1);
		return s5h1411_qam64_lookup_snr(fe, snr, reg);
	case QAM_256:
		reg = s5h1411_readreg(state, S5H1411_I2C_TOP_ADDR, 0xf1);
		return s5h1411_qam256_lookup_snr(fe, snr, reg);
	case VSB_8:
		reg = s5h1411_readreg(state, S5H1411_I2C_TOP_ADDR,
			0xf2) & 0x3ff;
		return s5h1411_vsb_lookup_snr(fe, snr, reg);
	default:
		break;
	}

	return -EINVAL;
}

static int s5h1411_read_signal_strength(struct dvb_frontend *fe,
	u16 *signal_strength)
{
	/* borrowed from lgdt330x.c
	 *
	 * Calculate strength from SNR up to 35dB
	 * Even though the SNR can go higher than 35dB,
	 * there is some comfort factor in having a range of
	 * strong signals that can show at 100%
	 */
	u16 snr;
	u32 tmp;
	int ret = s5h1411_read_snr(fe, &snr);

	*signal_strength = 0;

	if (0 == ret) {
		/* The following calculation method was chosen
		 * purely for the sake of code re-use from the
		 * other demod drivers that use this method */

		/* Convert from SNR in dB * 10 to 8.24 fixed-point */
		tmp = (snr * ((1 << 24) / 10));

		/* Convert from 8.24 fixed-point to
		 * scale the range 0 - 35*2^24 into 0 - 65535*/
		if (tmp >= 8960 * 0x10000)
			*signal_strength = 0xffff;
		else
			*signal_strength = tmp / 8960;
	}

	return ret;
}

static int s5h1411_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct s5h1411_state *state = fe->demodulator_priv;

	*ucblocks = s5h1411_readreg(state, S5H1411_I2C_TOP_ADDR, 0xc9);

	return 0;
}

static int s5h1411_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	return s5h1411_read_ucblocks(fe, ber);
}

static int s5h1411_get_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct s5h1411_state *state = fe->demodulator_priv;

	p->frequency = state->current_frequency;
	p->modulation = state->current_modulation;

	return 0;
}

static int s5h1411_get_tune_settings(struct dvb_frontend *fe,
				     struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 1000;
	return 0;
}

static void s5h1411_release(struct dvb_frontend *fe)
{
	struct s5h1411_state *state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops s5h1411_ops;

struct dvb_frontend *s5h1411_attach(const struct s5h1411_config *config,
				    struct i2c_adapter *i2c)
{
	struct s5h1411_state *state = NULL;
	u16 reg;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct s5h1411_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	state->current_modulation = VSB_8;
	state->inversion = state->config->inversion;

	/* check if the demod exists */
	reg = s5h1411_readreg(state, S5H1411_I2C_TOP_ADDR, 0x05);
	if (reg != 0x0066)
		goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &s5h1411_ops,
	       sizeof(struct dvb_frontend_ops));

	state->frontend.demodulator_priv = state;

	if (s5h1411_init(&state->frontend) != 0) {
		printk(KERN_ERR "%s: Failed to initialize correctly\n",
			__func__);
		goto error;
	}

	/* Note: Leaving the I2C gate open here. */
	s5h1411_writereg(state, S5H1411_I2C_TOP_ADDR, 0xf5, 1);

	/* Put the device into low-power mode until first use */
	s5h1411_set_powerstate(&state->frontend, 1);

	return &state->frontend;

error:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(s5h1411_attach);

static struct dvb_frontend_ops s5h1411_ops = {
	.delsys = { SYS_ATSC, SYS_DVBC_ANNEX_B },
	.info = {
		.name			= "Samsung S5H1411 QAM/8VSB Frontend",
		.frequency_min		= 54000000,
		.frequency_max		= 858000000,
		.frequency_stepsize	= 62500,
		.caps = FE_CAN_QAM_64 | FE_CAN_QAM_256 | FE_CAN_8VSB
	},

	.init                 = s5h1411_init,
	.sleep                = s5h1411_sleep,
	.i2c_gate_ctrl        = s5h1411_i2c_gate_ctrl,
	.set_frontend         = s5h1411_set_frontend,
	.get_frontend         = s5h1411_get_frontend,
	.get_tune_settings    = s5h1411_get_tune_settings,
	.read_status          = s5h1411_read_status,
	.read_ber             = s5h1411_read_ber,
	.read_signal_strength = s5h1411_read_signal_strength,
	.read_snr             = s5h1411_read_snr,
	.read_ucblocks        = s5h1411_read_ucblocks,
	.release              = s5h1411_release,
};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Enable verbose debug messages");

MODULE_DESCRIPTION("Samsung S5H1411 QAM-B/ATSC Demodulator driver");
MODULE_AUTHOR("Steven Toth");
MODULE_LICENSE("GPL");

/*
 * Local variables:
 * c-basic-offset: 8
 */
