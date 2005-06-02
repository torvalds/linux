/*
 * at76c651.c
 *
 * Atmel DVB-C Frontend Driver (at76c651/tua6010xs)
 *
 * Copyright (C) 2001 fnbrd <fnbrd@gmx.de>
 *             & 2002-2004 Andreas Oberritter <obi@linuxtv.org>
 *             & 2003 Wolfram Joost <dbox2@frokaschwei.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * AT76C651
 * http://www.nalanda.nitc.ac.in/industry/datasheets/atmel/acrobat/doc1293.pdf
 * http://www.atmel.com/atmel/acrobat/doc1320.pdf
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include "dvb_frontend.h"
#include "at76c651.h"


struct at76c651_state {

	struct i2c_adapter* i2c;

	struct dvb_frontend_ops ops;

	const struct at76c651_config* config;

	struct dvb_frontend frontend;

	/* revision of the chip */
	u8 revision;

	/* last QAM value set */
	u8 qam;
};

static int debug;
#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG "at76c651: " args); \
	} while (0)


#if ! defined(__powerpc__)
static __inline__ int __ilog2(unsigned long x)
{
	int i;

	if (x == 0)
		return -1;

	for (i = 0; x != 0; i++)
		x >>= 1;

	return i - 1;
}
#endif

static int at76c651_writereg(struct at76c651_state* state, u8 reg, u8 data)
{
	int ret;
	u8 buf[] = { reg, data };
	struct i2c_msg msg =
		{ .addr = state->config->demod_address, .flags = 0, .buf = buf, .len = 2 };

	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1)
		dprintk("%s: writereg error "
			"(reg == 0x%02x, val == 0x%02x, ret == %i)\n",
			__FUNCTION__, reg, data, ret);

	msleep(10);

	return (ret != 1) ? -EREMOTEIO : 0;
}

static u8 at76c651_readreg(struct at76c651_state* state, u8 reg)
{
	int ret;
	u8 val;
	struct i2c_msg msg[] = {
		{ .addr = state->config->demod_address, .flags = 0, .buf = &reg, .len = 1 },
		{ .addr = state->config->demod_address, .flags = I2C_M_RD, .buf = &val, .len = 1 }
	};

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2)
		dprintk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);

	return val;
}

static int at76c651_reset(struct at76c651_state* state)
{
	return at76c651_writereg(state, 0x07, 0x01);
}

static void at76c651_disable_interrupts(struct at76c651_state* state)
{
	at76c651_writereg(state, 0x0b, 0x00);
}

static int at76c651_set_auto_config(struct at76c651_state *state)
{
	/*
	 * Autoconfig
	 */

	at76c651_writereg(state, 0x06, 0x01);

	/*
	 * Performance optimizations, should be done after autoconfig
	 */

	at76c651_writereg(state, 0x10, 0x06);
	at76c651_writereg(state, 0x11, ((state->qam == 5) || (state->qam == 7)) ? 0x12 : 0x10);
	at76c651_writereg(state, 0x15, 0x28);
	at76c651_writereg(state, 0x20, 0x09);
	at76c651_writereg(state, 0x24, ((state->qam == 5) || (state->qam == 7)) ? 0xC0 : 0x90);
	at76c651_writereg(state, 0x30, 0x90);
	if (state->qam == 5)
		at76c651_writereg(state, 0x35, 0x2A);

	/*
	 * Initialize A/D-converter
	 */

	if (state->revision == 0x11) {
		at76c651_writereg(state, 0x2E, 0x38);
		at76c651_writereg(state, 0x2F, 0x13);
	}

	at76c651_disable_interrupts(state);

	/*
	 * Restart operation
	 */

	at76c651_reset(state);

	return 0;
}

static void at76c651_set_bbfreq(struct at76c651_state* state)
{
	at76c651_writereg(state, 0x04, 0x3f);
	at76c651_writereg(state, 0x05, 0xee);
}

static int at76c651_set_symbol_rate(struct at76c651_state* state, u32 symbol_rate)
{
	u8 exponent;
	u32 mantissa;

	if (symbol_rate > 9360000)
		return -EINVAL;

	/*
	 * FREF = 57800 kHz
	 * exponent = 10 + floor (log2(symbol_rate / FREF))
	 * mantissa = (symbol_rate / FREF) * (1 << (30 - exponent))
	 */

	exponent = __ilog2((symbol_rate << 4) / 903125);
	mantissa = ((symbol_rate / 3125) * (1 << (24 - exponent))) / 289;

	at76c651_writereg(state, 0x00, mantissa >> 13);
	at76c651_writereg(state, 0x01, mantissa >> 5);
	at76c651_writereg(state, 0x02, (mantissa << 3) | exponent);

	return 0;
}

static int at76c651_set_qam(struct at76c651_state *state, fe_modulation_t qam)
{
	switch (qam) {
	case QPSK:
		state->qam = 0x02;
		break;
	case QAM_16:
		state->qam = 0x04;
		break;
	case QAM_32:
		state->qam = 0x05;
		break;
	case QAM_64:
		state->qam = 0x06;
		break;
	case QAM_128:
		state->qam = 0x07;
		break;
	case QAM_256:
		state->qam = 0x08;
		break;
#if 0
	case QAM_512:
		state->qam = 0x09;
		break;
	case QAM_1024:
		state->qam = 0x0A;
		break;
#endif
	default:
		return -EINVAL;

	}

	return at76c651_writereg(state, 0x03, state->qam);
}

static int at76c651_set_inversion(struct at76c651_state* state, fe_spectral_inversion_t inversion)
{
	u8 feciqinv = at76c651_readreg(state, 0x60);

	switch (inversion) {
	case INVERSION_OFF:
		feciqinv |= 0x02;
		feciqinv &= 0xFE;
		break;

	case INVERSION_ON:
		feciqinv |= 0x03;
		break;

	case INVERSION_AUTO:
		feciqinv &= 0xFC;
		break;

	default:
		return -EINVAL;
	}

	return at76c651_writereg(state, 0x60, feciqinv);
}

static int at76c651_set_parameters(struct dvb_frontend* fe,
				   struct dvb_frontend_parameters *p)
{
	int ret;
	struct at76c651_state* state = fe->demodulator_priv;

	at76c651_writereg(state, 0x0c, 0xc3);
	state->config->pll_set(fe, p);
	at76c651_writereg(state, 0x0c, 0xc2);

	if ((ret = at76c651_set_symbol_rate(state, p->u.qam.symbol_rate)))
		return ret;

	if ((ret = at76c651_set_inversion(state, p->inversion)))
		return ret;

	return at76c651_set_auto_config(state);
}

static int at76c651_set_defaults(struct dvb_frontend* fe)
{
	struct at76c651_state* state = fe->demodulator_priv;

	at76c651_set_symbol_rate(state, 6900000);
	at76c651_set_qam(state, QAM_64);
	at76c651_set_bbfreq(state);
	at76c651_set_auto_config(state);

	if (state->config->pll_init) {
		at76c651_writereg(state, 0x0c, 0xc3);
		state->config->pll_init(fe);
		at76c651_writereg(state, 0x0c, 0xc2);
	}

	return 0;
}

static int at76c651_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct at76c651_state* state = fe->demodulator_priv;
	u8 sync;

	/*
	 * Bits: FEC, CAR, EQU, TIM, AGC2, AGC1, ADC, PLL (PLL=0)
	 */
	sync = at76c651_readreg(state, 0x80);
	*status = 0;

	if (sync & (0x04 | 0x10))	/* AGC1 || TIM */
		*status |= FE_HAS_SIGNAL;
	if (sync & 0x10)		/* TIM */
		*status |= FE_HAS_CARRIER;
	if (sync & 0x80)		/* FEC */
		*status |= FE_HAS_VITERBI;
	if (sync & 0x40)		/* CAR */
		*status |= FE_HAS_SYNC;
	if ((sync & 0xF0) == 0xF0)	/* TIM && EQU && CAR && FEC */
		*status |= FE_HAS_LOCK;

	return 0;
}

static int at76c651_read_ber(struct dvb_frontend* fe, u32* ber)
{
	struct at76c651_state* state = fe->demodulator_priv;

	*ber = (at76c651_readreg(state, 0x81) & 0x0F) << 16;
	*ber |= at76c651_readreg(state, 0x82) << 8;
	*ber |= at76c651_readreg(state, 0x83);
	*ber *= 10;

	return 0;
}

static int at76c651_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	struct at76c651_state* state = fe->demodulator_priv;

	u8 gain = ~at76c651_readreg(state, 0x91);
	*strength = (gain << 8) | gain;

	return 0;
}

static int at76c651_read_snr(struct dvb_frontend* fe, u16* snr)
{
	struct at76c651_state* state = fe->demodulator_priv;

	*snr = 0xFFFF -
	    ((at76c651_readreg(state, 0x8F) << 8) |
	     at76c651_readreg(state, 0x90));

	return 0;
}

static int at76c651_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct at76c651_state* state = fe->demodulator_priv;

	*ucblocks = at76c651_readreg(state, 0x82);

	return 0;
}

static int at76c651_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings *fesettings)
{
        fesettings->min_delay_ms = 50;
        fesettings->step_size = 0;
        fesettings->max_drift = 0;
	return 0;
}

static void at76c651_release(struct dvb_frontend* fe)
{
	struct at76c651_state* state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops at76c651_ops;

struct dvb_frontend* at76c651_attach(const struct at76c651_config* config,
				     struct i2c_adapter* i2c)
{
	struct at76c651_state* state = NULL;

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct at76c651_state), GFP_KERNEL);
	if (state == NULL) goto error;

	/* setup the state */
	state->config = config;
	state->qam = 0;

	/* check if the demod is there */
	if (at76c651_readreg(state, 0x0e) != 0x65) goto error;

	/* finalise state setup */
	state->i2c = i2c;
	state->revision = at76c651_readreg(state, 0x0f) & 0xfe;
	memcpy(&state->ops, &at76c651_ops, sizeof(struct dvb_frontend_ops));

	/* create dvb_frontend */
	state->frontend.ops = &state->ops;
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops at76c651_ops = {

	.info = {
		.name = "Atmel AT76C651B DVB-C",
		.type = FE_QAM,
		.frequency_min = 48250000,
		.frequency_max = 863250000,
		.frequency_stepsize = 62500,
		/*.frequency_tolerance = */	/* FIXME: 12% of SR */
		.symbol_rate_min = 0,		/* FIXME */
		.symbol_rate_max = 9360000,	/* FIXME */
		.symbol_rate_tolerance = 4000,
		.caps = FE_CAN_INVERSION_AUTO |
		    FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		    FE_CAN_FEC_4_5 | FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 |
		    FE_CAN_FEC_7_8 | FE_CAN_FEC_8_9 | FE_CAN_FEC_AUTO |
		    FE_CAN_QAM_16 | FE_CAN_QAM_32 | FE_CAN_QAM_64 | FE_CAN_QAM_128 |
		    FE_CAN_MUTE_TS | FE_CAN_QAM_256 | FE_CAN_RECOVER
	},

	.release = at76c651_release,

	.init = at76c651_set_defaults,

	.set_frontend = at76c651_set_parameters,
	.get_tune_settings = at76c651_get_tune_settings,

	.read_status = at76c651_read_status,
	.read_ber = at76c651_read_ber,
	.read_signal_strength = at76c651_read_signal_strength,
	.read_snr = at76c651_read_snr,
	.read_ucblocks = at76c651_read_ucblocks,
};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("Atmel AT76C651 DVB-C Demodulator Driver");
MODULE_AUTHOR("Andreas Oberritter <obi@linuxtv.org>");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(at76c651_attach);
