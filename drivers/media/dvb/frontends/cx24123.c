/*
    Conexant cx24123/cx24109 - DVB QPSK Satellite demod/tuner driver

    Copyright (C) 2005 Steven Toth <stoth@hauppauge.com>

    Support for KWorld DVB-S 100 by Vadim Catana <skystar@moldova.cc>

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

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include "dvb_frontend.h"
#include "cx24123.h"

static int debug;
#define dprintk(args...) \
	do { \
		if (debug) printk (KERN_DEBUG "cx24123: " args); \
	} while (0)

struct cx24123_state
{
	struct i2c_adapter* i2c;
	struct dvb_frontend_ops ops;
	const struct cx24123_config* config;

	struct dvb_frontend frontend;

	u32 lastber;
	u16 snr;
	u8  lnbreg;

	/* Some PLL specifics for tuning */
	u32 VCAarg;
	u32 VGAarg;
	u32 bandselectarg;
	u32 pllarg;

	/* The Demod/Tuner can't easily provide these, we cache them */
	u32 currentfreq;
	u32 currentsymbolrate;
};

/* Various tuner defaults need to be established for a given symbol rate Sps */
static struct
{
	u32 symbolrate_low;
	u32 symbolrate_high;
	u32 VCAslope;
	u32 VCAoffset;
	u32 VGA1offset;
	u32 VGA2offset;
	u32 VCAprogdata;
	u32 VGAprogdata;
} cx24123_AGC_vals[] =
{
	{
		.symbolrate_low		= 1000000,
		.symbolrate_high	= 4999999,
		.VCAslope		= 0x07,
		.VCAoffset		= 0x0f,
		.VGA1offset		= 0x1f8,
		.VGA2offset		= 0x1f8,
		.VGAprogdata		= (2 << 18) | (0x1f8 << 9) | 0x1f8,
		.VCAprogdata		= (4 << 18) | (0x07 << 9) | 0x07,
	},
	{
		.symbolrate_low		=  5000000,
		.symbolrate_high	= 14999999,
		.VCAslope		= 0x1f,
		.VCAoffset		= 0x1f,
		.VGA1offset		= 0x1e0,
		.VGA2offset		= 0x180,
		.VGAprogdata		= (2 << 18) | (0x180 << 9) | 0x1e0,
		.VCAprogdata		= (4 << 18) | (0x07 << 9) | 0x1f,
	},
	{
		.symbolrate_low		= 15000000,
		.symbolrate_high	= 45000000,
		.VCAslope		= 0x3f,
		.VCAoffset		= 0x3f,
		.VGA1offset		= 0x180,
		.VGA2offset		= 0x100,
		.VGAprogdata		= (2 << 18) | (0x100 << 9) | 0x180,
		.VCAprogdata		= (4 << 18) | (0x07 << 9) | 0x3f,
	},
};

/*
 * Various tuner defaults need to be established for a given frequency kHz.
 * fixme: The bounds on the bands do not match the doc in real life.
 * fixme: Some of them have been moved, other might need adjustment.
 */
static struct
{
	u32 freq_low;
	u32 freq_high;
	u32 bandselect;
	u32 VCOdivider;
	u32 VCOnumber;
	u32 progdata;
} cx24123_bandselect_vals[] =
{
	{
		.freq_low	= 950000,
		.freq_high	= 1018999,
		.bandselect	= 0x40,
		.VCOdivider	= 4,
		.VCOnumber	= 7,
		.progdata	= (0 << 18) | (0 << 9) | 0x40,
	},
	{
		.freq_low	= 1019000,
		.freq_high	= 1074999,
		.bandselect	= 0x80,
		.VCOdivider	= 4,
		.VCOnumber	= 8,
		.progdata	= (0 << 18) | (0 << 9) | 0x80,
	},
	{
		.freq_low	= 1075000,
		.freq_high	= 1227999,
		.bandselect	= 0x01,
		.VCOdivider	= 2,
		.VCOnumber	= 1,
		.progdata	= (0 << 18) | (1 << 9) | 0x01,
	},
	{
		.freq_low	= 1228000,
		.freq_high	= 1349999,
		.bandselect	= 0x02,
		.VCOdivider	= 2,
		.VCOnumber	= 2,
		.progdata	= (0 << 18) | (1 << 9) | 0x02,
	},
	{
		.freq_low	= 1350000,
		.freq_high	= 1481999,
		.bandselect	= 0x04,
		.VCOdivider	= 2,
		.VCOnumber	= 3,
		.progdata	= (0 << 18) | (1 << 9) | 0x04,
	},
	{
		.freq_low	= 1482000,
		.freq_high	= 1595999,
		.bandselect	= 0x08,
		.VCOdivider	= 2,
		.VCOnumber	= 4,
		.progdata	= (0 << 18) | (1 << 9) | 0x08,
	},
	{
		.freq_low	= 1596000,
		.freq_high	= 1717999,
		.bandselect	= 0x10,
		.VCOdivider	= 2,
		.VCOnumber	= 5,
		.progdata	= (0 << 18) | (1 << 9) | 0x10,
	},
	{
		.freq_low	= 1718000,
		.freq_high	= 1855999,
		.bandselect	= 0x20,
		.VCOdivider	= 2,
		.VCOnumber	= 6,
		.progdata	= (0 << 18) | (1 << 9) | 0x20,
	},
	{
		.freq_low	= 1856000,
		.freq_high	= 2035999,
		.bandselect	= 0x40,
		.VCOdivider	= 2,
		.VCOnumber	= 7,
		.progdata	= (0 << 18) | (1 << 9) | 0x40,
	},
	{
		.freq_low	= 2036000,
		.freq_high	= 2149999,
		.bandselect	= 0x80,
		.VCOdivider	= 2,
		.VCOnumber	= 8,
		.progdata	= (0 << 18) | (1 << 9) | 0x80,
	},
};

static struct {
	u8 reg;
	u8 data;
} cx24123_regdata[] =
{
	{0x00, 0x03}, /* Reset system */
	{0x00, 0x00}, /* Clear reset */
	{0x01, 0x3b}, /* Apply sensible defaults, from an i2c sniffer */
	{0x03, 0x07},
	{0x04, 0x10},
	{0x05, 0x04},
	{0x06, 0x31},
	{0x0d, 0x02},
	{0x0e, 0x03},
	{0x0f, 0xfe},
	{0x10, 0x01},
	{0x14, 0x01},
	{0x15, 0x98},
	{0x16, 0x00},
	{0x17, 0x01},
	{0x1b, 0x05},
	{0x1c, 0x80},
	{0x1d, 0x00},
	{0x1e, 0x00},
	{0x20, 0x41},
	{0x21, 0x15},
	{0x27, 0x14},
	{0x28, 0x46},
	{0x29, 0x00},
	{0x2a, 0xb0},
	{0x2b, 0x73},
	{0x2c, 0x00},
	{0x2d, 0x00},
	{0x2e, 0x00},
	{0x2f, 0x00},
	{0x30, 0x00},
	{0x31, 0x00},
	{0x32, 0x8c},
	{0x33, 0x00},
	{0x34, 0x00},
	{0x35, 0x03},
	{0x36, 0x02},
	{0x37, 0x3a},
	{0x3a, 0x00},	/* Enable AGC accumulator */
	{0x44, 0x00},
	{0x45, 0x00},
	{0x46, 0x05},
	{0x56, 0x41},
	{0x57, 0xff},
	{0x67, 0x83},
};

static int cx24123_writereg(struct cx24123_state* state, int reg, int data)
{
	u8 buf[] = { reg, data };
	struct i2c_msg msg = { .addr = state->config->demod_address, .flags = 0, .buf = buf, .len = 2 };
	int err;

	if ((err = i2c_transfer(state->i2c, &msg, 1)) != 1) {
		printk("%s: writereg error(err == %i, reg == 0x%02x,"
			 " data == 0x%02x)\n", __FUNCTION__, err, reg, data);
		return -EREMOTEIO;
	}

	return 0;
}

static int cx24123_writelnbreg(struct cx24123_state* state, int reg, int data)
{
	u8 buf[] = { reg, data };
	/* fixme: put the intersil addr int the config */
	struct i2c_msg msg = { .addr = 0x08, .flags = 0, .buf = buf, .len = 2 };
	int err;

	if ((err = i2c_transfer(state->i2c, &msg, 1)) != 1) {
		printk("%s: writelnbreg error (err == %i, reg == 0x%02x,"
			 " data == 0x%02x)\n", __FUNCTION__, err, reg, data);
		return -EREMOTEIO;
	}

	/* cache the write, no way to read back */
	state->lnbreg = data;

	return 0;
}

static int cx24123_readreg(struct cx24123_state* state, u8 reg)
{
	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = {
		{ .addr = state->config->demod_address, .flags = 0, .buf = b0, .len = 1 },
		{ .addr = state->config->demod_address, .flags = I2C_M_RD, .buf = b1, .len = 1 }
	};

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2) {
		printk("%s: reg=0x%x (error=%d)\n", __FUNCTION__, reg, ret);
		return ret;
	}

	return b1[0];
}

static int cx24123_readlnbreg(struct cx24123_state* state, u8 reg)
{
	return state->lnbreg;
}

static int cx24123_set_inversion(struct cx24123_state* state, fe_spectral_inversion_t inversion)
{
	switch (inversion) {
	case INVERSION_OFF:
		cx24123_writereg(state, 0x0e, cx24123_readreg(state, 0x0e) & 0x7f);
		cx24123_writereg(state, 0x10, cx24123_readreg(state, 0x10) | 0x80);
		break;
	case INVERSION_ON:
		cx24123_writereg(state, 0x0e, cx24123_readreg(state, 0x0e) | 0x80);
		cx24123_writereg(state, 0x10, cx24123_readreg(state, 0x10) | 0x80);
		break;
	case INVERSION_AUTO:
		cx24123_writereg(state, 0x10, cx24123_readreg(state, 0x10) & 0x7f);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cx24123_get_inversion(struct cx24123_state* state, fe_spectral_inversion_t *inversion)
{
	u8 val;

	val = cx24123_readreg(state, 0x1b) >> 7;

	if (val == 0)
		*inversion = INVERSION_OFF;
	else
		*inversion = INVERSION_ON;

	return 0;
}

static int cx24123_set_fec(struct cx24123_state* state, fe_code_rate_t fec)
{
	if ( (fec < FEC_NONE) || (fec > FEC_AUTO) )
		fec = FEC_AUTO;

	/* Hardware has 5/11 and 3/5 but are never unused */
	switch (fec) {
	case FEC_NONE:
		return cx24123_writereg(state, 0x0f, 0x01);
	case FEC_1_2:
		return cx24123_writereg(state, 0x0f, 0x02);
	case FEC_2_3:
		return cx24123_writereg(state, 0x0f, 0x04);
	case FEC_3_4:
		return cx24123_writereg(state, 0x0f, 0x08);
	case FEC_5_6:
		return cx24123_writereg(state, 0x0f, 0x20);
	case FEC_7_8:
		return cx24123_writereg(state, 0x0f, 0x80);
	case FEC_AUTO:
		return cx24123_writereg(state, 0x0f, 0xae);
	default:
		return -EOPNOTSUPP;
	}
}

static int cx24123_get_fec(struct cx24123_state* state, fe_code_rate_t *fec)
{
	int ret;
	u8 val;

	ret = cx24123_readreg (state, 0x1b);
	if (ret < 0)
		return ret;
	val = ret & 0x07;
	switch (val) {
	case 1:
		*fec = FEC_1_2;
		break;
	case 3:
		*fec = FEC_2_3;
		break;
	case 4:
		*fec = FEC_3_4;
		break;
	case 5:
		*fec = FEC_4_5;
		break;
	case 6:
		*fec = FEC_5_6;
		break;
	case 7:
		*fec = FEC_7_8;
		break;
	case 2:	/* *fec = FEC_3_5; break; */
	case 0:	/* *fec = FEC_5_11; break; */
		*fec = FEC_AUTO;
		break;
	default:
		*fec = FEC_NONE; // can't happen
	}

	return 0;
}

/* fixme: Symbol rates < 3MSps may not work because of precision loss */
static int cx24123_set_symbolrate(struct cx24123_state* state, u32 srate)
{
	u32 val;

	val = (srate / 1185) * 100;

	/* Compensate for scaling up, by removing 17 symbols per 1Msps */
	val = val - (17 * (srate / 1000000));

	cx24123_writereg(state, 0x08, (val >> 16) & 0xff );
	cx24123_writereg(state, 0x09, (val >>  8) & 0xff );
	cx24123_writereg(state, 0x0a, (val      ) & 0xff );

	return 0;
}

/*
 * Based on the required frequency and symbolrate, the tuner AGC has to be configured
 * and the correct band selected. Calculate those values
 */
static int cx24123_pll_calculate(struct dvb_frontend* fe, struct dvb_frontend_parameters *p)
{
	struct cx24123_state *state = fe->demodulator_priv;
	u32 ndiv = 0, adiv = 0, vco_div = 0;
	int i = 0;

	/* Defaults for low freq, low rate */
	state->VCAarg = cx24123_AGC_vals[0].VCAprogdata;
	state->VGAarg = cx24123_AGC_vals[0].VGAprogdata;
	state->bandselectarg = cx24123_bandselect_vals[0].progdata;
	vco_div = cx24123_bandselect_vals[0].VCOdivider;

	/* For the given symbolerate, determine the VCA and VGA programming bits */
	for (i = 0; i < sizeof(cx24123_AGC_vals) / sizeof(cx24123_AGC_vals[0]); i++)
	{
		if ((cx24123_AGC_vals[i].symbolrate_low <= p->u.qpsk.symbol_rate) &&
				(cx24123_AGC_vals[i].symbolrate_high >= p->u.qpsk.symbol_rate) ) {
			state->VCAarg = cx24123_AGC_vals[i].VCAprogdata;
			state->VGAarg = cx24123_AGC_vals[i].VGAprogdata;
		}
	}

	/* For the given frequency, determine the bandselect programming bits */
	for (i = 0; i < sizeof(cx24123_bandselect_vals) / sizeof(cx24123_bandselect_vals[0]); i++)
	{
		if ((cx24123_bandselect_vals[i].freq_low <= p->frequency) &&
				(cx24123_bandselect_vals[i].freq_high >= p->frequency) ) {
			state->bandselectarg = cx24123_bandselect_vals[i].progdata;
			vco_div = cx24123_bandselect_vals[i].VCOdivider;
		}
	}

	/* Determine the N/A dividers for the requested lband freq (in kHz). */
	/* Note: 10111 (kHz) is the Crystal Freq and divider of 10. */
	ndiv = ( ((p->frequency * vco_div) / (10111 / 10) / 2) / 32) & 0x1ff;
	adiv = ( ((p->frequency * vco_div) / (10111 / 10) / 2) % 32) & 0x1f;

	if (adiv == 0)
		adiv++;

	/* determine the correct pll frequency values. */
	/* Command 11, refdiv 11, cpump polarity 1, cpump current 3mA 10. */
	state->pllarg = (3 << 19) | (3 << 17) | (1 << 16) | (2 << 14);
	state->pllarg |= (ndiv << 5) | adiv;

	return 0;
}

/*
 * Tuner data is 21 bits long, must be left-aligned in data.
 * Tuner cx24109 is written through a dedicated 3wire interface on the demod chip.
 */
static int cx24123_pll_writereg(struct dvb_frontend* fe, struct dvb_frontend_parameters *p, u32 data)
{
	struct cx24123_state *state = fe->demodulator_priv;
	unsigned long timeout;

	/* align the 21 bytes into to bit23 boundary */
	data = data << 3;

	/* Reset the demod pll word length to 0x15 bits */
	cx24123_writereg(state, 0x21, 0x15);

	/* write the msb 8 bits, wait for the send to be completed */
	timeout = jiffies + msecs_to_jiffies(40);
	cx24123_writereg(state, 0x22, (data >> 16) & 0xff);
	while ((cx24123_readreg(state, 0x20) & 0x40) == 0) {
		if (time_after(jiffies, timeout)) {
			printk("%s:  demodulator is not responding, possibly hung, aborting.\n", __FUNCTION__);
			return -EREMOTEIO;
		}
		msleep(10);
	}

	/* send another 8 bytes, wait for the send to be completed */
	timeout = jiffies + msecs_to_jiffies(40);
	cx24123_writereg(state, 0x22, (data>>8) & 0xff );
	while ((cx24123_readreg(state, 0x20) & 0x40) == 0) {
		if (time_after(jiffies, timeout)) {
			printk("%s:  demodulator is not responding, possibly hung, aborting.\n", __FUNCTION__);
			return -EREMOTEIO;
		}
		msleep(10);
	}

	/* send the lower 5 bits of this byte, padded with 3 LBB, wait for the send to be completed */
	timeout = jiffies + msecs_to_jiffies(40);
	cx24123_writereg(state, 0x22, (data) & 0xff );
	while ((cx24123_readreg(state, 0x20) & 0x80)) {
		if (time_after(jiffies, timeout)) {
			printk("%s:  demodulator is not responding, possibly hung, aborting.\n", __FUNCTION__);
			return -EREMOTEIO;
		}
		msleep(10);
	}

	/* Trigger the demod to configure the tuner */
	cx24123_writereg(state, 0x20, cx24123_readreg(state, 0x20) | 2);
	cx24123_writereg(state, 0x20, cx24123_readreg(state, 0x20) & 0xfd);

	return 0;
}

static int cx24123_pll_tune(struct dvb_frontend* fe, struct dvb_frontend_parameters *p)
{
	struct cx24123_state *state = fe->demodulator_priv;

	if (cx24123_pll_calculate(fe, p) != 0) {
		printk("%s: cx24123_pll_calcutate failed\n",__FUNCTION__);
		return -EINVAL;
	}

	/* Write the new VCO/VGA */
	cx24123_pll_writereg(fe, p, state->VCAarg);
	cx24123_pll_writereg(fe, p, state->VGAarg);

	/* Write the new bandselect and pll args */
	cx24123_pll_writereg(fe, p, state->bandselectarg);
	cx24123_pll_writereg(fe, p, state->pllarg);

	return 0;
}

static int cx24123_initfe(struct dvb_frontend* fe)
{
	struct cx24123_state *state = fe->demodulator_priv;
	int i;

	/* Configure the demod to a good set of defaults */
	for (i = 0; i < sizeof(cx24123_regdata) / sizeof(cx24123_regdata[0]); i++)
		cx24123_writereg(state, cx24123_regdata[i].reg, cx24123_regdata[i].data);

	if (state->config->pll_init)
		state->config->pll_init(fe);

	/* Configure the LNB for 14V */
	if (state->config->use_isl6421)
		cx24123_writelnbreg(state, 0x0, 0x2a);

	return 0;
}

static int cx24123_set_voltage(struct dvb_frontend* fe, fe_sec_voltage_t voltage)
{
	struct cx24123_state *state = fe->demodulator_priv;
	u8 val;

	switch (state->config->use_isl6421) {

	case 1:

		val = cx24123_readlnbreg(state, 0x0);

		switch (voltage) {
		case SEC_VOLTAGE_13:
			return cx24123_writelnbreg(state, 0x0, val & 0x32); /* V 13v */
		case SEC_VOLTAGE_18:
			return cx24123_writelnbreg(state, 0x0, val | 0x04); /* H 18v */
		case SEC_VOLTAGE_OFF:
			return cx24123_writelnbreg(state, 0x0, val & 0x30);
		default:
			return -EINVAL;
		};

	case 0:

		val = cx24123_readreg(state, 0x29);

		switch (voltage) {
		case SEC_VOLTAGE_13:
			dprintk("%s: setting voltage 13V\n", __FUNCTION__);
			if (state->config->enable_lnb_voltage)
				state->config->enable_lnb_voltage(fe, 1);
			return cx24123_writereg(state, 0x29, val | 0x80);
		case SEC_VOLTAGE_18:
			dprintk("%s: setting voltage 18V\n", __FUNCTION__);
			if (state->config->enable_lnb_voltage)
				state->config->enable_lnb_voltage(fe, 1);
			return cx24123_writereg(state, 0x29, val & 0x7f);
		case SEC_VOLTAGE_OFF:
			dprintk("%s: setting voltage off\n", __FUNCTION__);
			if (state->config->enable_lnb_voltage)
				state->config->enable_lnb_voltage(fe, 0);
			return 0;
		default:
			return -EINVAL;
		};
	}

	return 0;
}

static int cx24123_send_diseqc_msg(struct dvb_frontend* fe,
				   struct dvb_diseqc_master_cmd *cmd)
{
	/* fixme: Implement diseqc */
	printk("%s: No support yet\n",__FUNCTION__);

	return -ENOTSUPP;
}

static int cx24123_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct cx24123_state *state = fe->demodulator_priv;

	int sync = cx24123_readreg(state, 0x14);
	int lock = cx24123_readreg(state, 0x20);

	*status = 0;
	if (lock & 0x01)
		*status |= FE_HAS_CARRIER | FE_HAS_SIGNAL;
	if (sync & 0x04)
		*status |= FE_HAS_VITERBI;
	if (sync & 0x08)
		*status |= FE_HAS_CARRIER;
	if (sync & 0x80)
		*status |= FE_HAS_SYNC | FE_HAS_LOCK;

	return 0;
}

/*
 * Configured to return the measurement of errors in blocks, because no UCBLOCKS value
 * is available, so this value doubles up to satisfy both measurements
 */
static int cx24123_read_ber(struct dvb_frontend* fe, u32* ber)
{
	struct cx24123_state *state = fe->demodulator_priv;

	state->lastber =
		((cx24123_readreg(state, 0x1c) & 0x3f) << 16) |
		(cx24123_readreg(state, 0x1d) << 8 |
		cx24123_readreg(state, 0x1e));

	/* Do the signal quality processing here, it's derived from the BER. */
	/* Scale the BER from a 24bit to a SNR 16 bit where higher = better */
	if (state->lastber < 5000)
		state->snr = 655*100;
	else if ( (state->lastber >=   5000) && (state->lastber <  55000) )
		state->snr = 655*90;
	else if ( (state->lastber >=  55000) && (state->lastber < 150000) )
		state->snr = 655*80;
	else if ( (state->lastber >= 150000) && (state->lastber < 250000) )
		state->snr = 655*70;
	else if ( (state->lastber >= 250000) && (state->lastber < 450000) )
		state->snr = 655*65;
	else
		state->snr = 0;

	*ber = state->lastber;

	return 0;
}

static int cx24123_read_signal_strength(struct dvb_frontend* fe, u16* signal_strength)
{
	struct cx24123_state *state = fe->demodulator_priv;
	*signal_strength = cx24123_readreg(state, 0x3b) << 8; /* larger = better */

	return 0;
}

static int cx24123_read_snr(struct dvb_frontend* fe, u16* snr)
{
	struct cx24123_state *state = fe->demodulator_priv;
	*snr = state->snr;

	return 0;
}

static int cx24123_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct cx24123_state *state = fe->demodulator_priv;
	*ucblocks = state->lastber;

	return 0;
}

static int cx24123_set_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters *p)
{
	struct cx24123_state *state = fe->demodulator_priv;

	if (state->config->set_ts_params)
		state->config->set_ts_params(fe, 0);

	state->currentfreq=p->frequency;
	state->currentsymbolrate = p->u.qpsk.symbol_rate;

	cx24123_set_inversion(state, p->inversion);
	cx24123_set_fec(state, p->u.qpsk.fec_inner);
	cx24123_set_symbolrate(state, p->u.qpsk.symbol_rate);
	cx24123_pll_tune(fe, p);

	/* Enable automatic aquisition and reset cycle */
	cx24123_writereg(state, 0x03, (cx24123_readreg(state, 0x03) | 0x07));
	cx24123_writereg(state, 0x00, 0x10);
	cx24123_writereg(state, 0x00, 0);

	return 0;
}

static int cx24123_get_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters *p)
{
	struct cx24123_state *state = fe->demodulator_priv;

	if (cx24123_get_inversion(state, &p->inversion) != 0) {
		printk("%s: Failed to get inversion status\n",__FUNCTION__);
		return -EREMOTEIO;
	}
	if (cx24123_get_fec(state, &p->u.qpsk.fec_inner) != 0) {
		printk("%s: Failed to get fec status\n",__FUNCTION__);
		return -EREMOTEIO;
	}
	p->frequency = state->currentfreq;
	p->u.qpsk.symbol_rate = state->currentsymbolrate;

	return 0;
}

static int cx24123_set_tone(struct dvb_frontend* fe, fe_sec_tone_mode_t tone)
{
	struct cx24123_state *state = fe->demodulator_priv;
	u8 val;

	switch (state->config->use_isl6421) {
	case 1:

		val = cx24123_readlnbreg(state, 0x0);

		switch (tone) {
		case SEC_TONE_ON:
			return cx24123_writelnbreg(state, 0x0, val | 0x10);
		case SEC_TONE_OFF:
			return cx24123_writelnbreg(state, 0x0, val & 0x2f);
		default:
			printk("%s: CASE reached default with tone=%d\n", __FUNCTION__, tone);
			return -EINVAL;
		}

	case 0:

		val = cx24123_readreg(state, 0x29);

		switch (tone) {
		case SEC_TONE_ON:
			dprintk("%s: setting tone on\n", __FUNCTION__);
			return cx24123_writereg(state, 0x29, val | 0x10);
		case SEC_TONE_OFF:
			dprintk("%s: setting tone off\n",__FUNCTION__);
			return cx24123_writereg(state, 0x29, val & 0xef);
		default:
			printk("%s: CASE reached default with tone=%d\n", __FUNCTION__, tone);
			return -EINVAL;
		}
	}

	return 0;
}

static void cx24123_release(struct dvb_frontend* fe)
{
	struct cx24123_state* state = fe->demodulator_priv;
	dprintk("%s\n",__FUNCTION__);
	kfree(state);
}

static struct dvb_frontend_ops cx24123_ops;

struct dvb_frontend* cx24123_attach(const struct cx24123_config* config,
				    struct i2c_adapter* i2c)
{
	struct cx24123_state* state = NULL;
	int ret;

	dprintk("%s\n",__FUNCTION__);

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct cx24123_state), GFP_KERNEL);
	if (state == NULL) {
		printk("Unable to kmalloc\n");
		goto error;
	}

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	memcpy(&state->ops, &cx24123_ops, sizeof(struct dvb_frontend_ops));
	state->lastber = 0;
	state->snr = 0;
	state->lnbreg = 0;
	state->VCAarg = 0;
	state->VGAarg = 0;
	state->bandselectarg = 0;
	state->pllarg = 0;
	state->currentfreq = 0;
	state->currentsymbolrate = 0;

	/* check if the demod is there */
	ret = cx24123_readreg(state, 0x00);
	if ((ret != 0xd1) && (ret != 0xe1)) {
		printk("Version != d1 or e1\n");
		goto error;
	}

	/* create dvb_frontend */
	state->frontend.ops = &state->ops;
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);

	return NULL;
}

static struct dvb_frontend_ops cx24123_ops = {

	.info = {
		.name = "Conexant CX24123/CX24109",
		.type = FE_QPSK,
		.frequency_min = 950000,
		.frequency_max = 2150000,
		.frequency_stepsize = 1011, /* kHz for QPSK frontends */
		.frequency_tolerance = 29500,
		.symbol_rate_min = 1000000,
		.symbol_rate_max = 45000000,
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK | FE_CAN_RECOVER
	},

	.release = cx24123_release,

	.init = cx24123_initfe,
	.set_frontend = cx24123_set_frontend,
	.get_frontend = cx24123_get_frontend,
	.read_status = cx24123_read_status,
	.read_ber = cx24123_read_ber,
	.read_signal_strength = cx24123_read_signal_strength,
	.read_snr = cx24123_read_snr,
	.read_ucblocks = cx24123_read_ucblocks,
	.diseqc_send_master_cmd = cx24123_send_diseqc_msg,
	.set_tone = cx24123_set_tone,
	.set_voltage = cx24123_set_voltage,
};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("DVB Frontend module for Conexant cx24123/cx24109 hardware");
MODULE_AUTHOR("Steven Toth");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(cx24123_attach);
