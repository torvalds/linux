/*
    Driver for ST STV0299 demodulator

    Copyright (C) 2001-2002 Convergence Integrated Media GmbH
	<ralph@convergence.de>,
	<holger@convergence.de>,
	<js@convergence.de>


    Philips SU1278/SH

    Copyright (C) 2002 by Peter Schildmann <peter.schildmann@web.de>


    LG TDQF-S001F

    Copyright (C) 2002 Felix Domke <tmbinc@elitedvb.net>
		     & Andreas Oberritter <obi@linuxtv.org>


    Support for Samsung TBMU24112IMB used on Technisat SkyStar2 rev. 2.6B

    Copyright (C) 2003 Vadim Catana <skystar@moldova.cc>:

    Support for Philips SU1278 on Technotrend hardware

    Copyright (C) 2004 Andrew de Quincey <adq_dvb@lidskialf.net>

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

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <asm/div64.h>

#include "dvb_frontend.h"
#include "stv0299.h"

struct stv0299_state {
	struct i2c_adapter* i2c;
	const struct stv0299_config* config;
	struct dvb_frontend frontend;

	u8 initialised:1;
	u32 tuner_frequency;
	u32 symbol_rate;
	fe_code_rate_t fec_inner;
	int errmode;
};

#define STATUS_BER 0
#define STATUS_UCBLOCKS 1

static int debug;
static int debug_legacy_dish_switch;
#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG "stv0299: " args); \
	} while (0)


static int stv0299_writeregI (struct stv0299_state* state, u8 reg, u8 data)
{
	int ret;
	u8 buf [] = { reg, data };
	struct i2c_msg msg = { .addr = state->config->demod_address, .flags = 0, .buf = buf, .len = 2 };

	ret = i2c_transfer (state->i2c, &msg, 1);

	if (ret != 1)
		dprintk("%s: writereg error (reg == 0x%02x, val == 0x%02x, "
			"ret == %i)\n", __FUNCTION__, reg, data, ret);

	return (ret != 1) ? -EREMOTEIO : 0;
}

static int stv0299_write(struct dvb_frontend* fe, u8 *buf, int len)
{
	struct stv0299_state* state = fe->demodulator_priv;

	if (len != 2)
		return -EINVAL;

	return stv0299_writeregI(state, buf[0], buf[1]);
}

static u8 stv0299_readreg (struct stv0299_state* state, u8 reg)
{
	int ret;
	u8 b0 [] = { reg };
	u8 b1 [] = { 0 };
	struct i2c_msg msg [] = { { .addr = state->config->demod_address, .flags = 0, .buf = b0, .len = 1 },
			   { .addr = state->config->demod_address, .flags = I2C_M_RD, .buf = b1, .len = 1 } };

	ret = i2c_transfer (state->i2c, msg, 2);

	if (ret != 2)
		dprintk("%s: readreg error (reg == 0x%02x, ret == %i)\n",
				__FUNCTION__, reg, ret);

	return b1[0];
}

static int stv0299_readregs (struct stv0299_state* state, u8 reg1, u8 *b, u8 len)
{
	int ret;
	struct i2c_msg msg [] = { { .addr = state->config->demod_address, .flags = 0, .buf = &reg1, .len = 1 },
			   { .addr = state->config->demod_address, .flags = I2C_M_RD, .buf = b, .len = len } };

	ret = i2c_transfer (state->i2c, msg, 2);

	if (ret != 2)
		dprintk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);

	return ret == 2 ? 0 : ret;
}

static int stv0299_set_FEC (struct stv0299_state* state, fe_code_rate_t fec)
{
	dprintk ("%s\n", __FUNCTION__);

	switch (fec) {
	case FEC_AUTO:
	{
		return stv0299_writeregI (state, 0x31, 0x1f);
	}
	case FEC_1_2:
	{
		return stv0299_writeregI (state, 0x31, 0x01);
	}
	case FEC_2_3:
	{
		return stv0299_writeregI (state, 0x31, 0x02);
	}
	case FEC_3_4:
	{
		return stv0299_writeregI (state, 0x31, 0x04);
	}
	case FEC_5_6:
	{
		return stv0299_writeregI (state, 0x31, 0x08);
	}
	case FEC_7_8:
	{
		return stv0299_writeregI (state, 0x31, 0x10);
	}
	default:
	{
		return -EINVAL;
	}
    }
}

static fe_code_rate_t stv0299_get_fec (struct stv0299_state* state)
{
	static fe_code_rate_t fec_tab [] = { FEC_2_3, FEC_3_4, FEC_5_6,
					     FEC_7_8, FEC_1_2 };
	u8 index;

	dprintk ("%s\n", __FUNCTION__);

	index = stv0299_readreg (state, 0x1b);
	index &= 0x7;

	if (index > 4)
		return FEC_AUTO;

	return fec_tab [index];
}

static int stv0299_wait_diseqc_fifo (struct stv0299_state* state, int timeout)
{
	unsigned long start = jiffies;

	dprintk ("%s\n", __FUNCTION__);

	while (stv0299_readreg(state, 0x0a) & 1) {
		if (jiffies - start > timeout) {
			dprintk ("%s: timeout!!\n", __FUNCTION__);
			return -ETIMEDOUT;
		}
		msleep(10);
	};

	return 0;
}

static int stv0299_wait_diseqc_idle (struct stv0299_state* state, int timeout)
{
	unsigned long start = jiffies;

	dprintk ("%s\n", __FUNCTION__);

	while ((stv0299_readreg(state, 0x0a) & 3) != 2 ) {
		if (jiffies - start > timeout) {
			dprintk ("%s: timeout!!\n", __FUNCTION__);
			return -ETIMEDOUT;
		}
		msleep(10);
	};

	return 0;
}

static int stv0299_set_symbolrate (struct dvb_frontend* fe, u32 srate)
{
	struct stv0299_state* state = fe->demodulator_priv;
	u64 big = srate;
	u32 ratio;

	// check rate is within limits
	if ((srate < 1000000) || (srate > 45000000)) return -EINVAL;

	// calculate value to program
	big = big << 20;
	big += (state->config->mclk-1); // round correctly
	do_div(big, state->config->mclk);
	ratio = big << 4;

	return state->config->set_symbol_rate(fe, srate, ratio);
}

static int stv0299_get_symbolrate (struct stv0299_state* state)
{
	u32 Mclk = state->config->mclk / 4096L;
	u32 srate;
	s32 offset;
	u8 sfr[3];
	s8 rtf;

	dprintk ("%s\n", __FUNCTION__);

	stv0299_readregs (state, 0x1f, sfr, 3);
	stv0299_readregs (state, 0x1a, (u8 *)&rtf, 1);

	srate = (sfr[0] << 8) | sfr[1];
	srate *= Mclk;
	srate /= 16;
	srate += (sfr[2] >> 4) * Mclk / 256;
	offset = (s32) rtf * (srate / 4096L);
	offset /= 128;

	dprintk ("%s : srate = %i\n", __FUNCTION__, srate);
	dprintk ("%s : ofset = %i\n", __FUNCTION__, offset);

	srate += offset;

	srate += 1000;
	srate /= 2000;
	srate *= 2000;

	return srate;
}

static int stv0299_send_diseqc_msg (struct dvb_frontend* fe,
				    struct dvb_diseqc_master_cmd *m)
{
	struct stv0299_state* state = fe->demodulator_priv;
	u8 val;
	int i;

	dprintk ("%s\n", __FUNCTION__);

	if (stv0299_wait_diseqc_idle (state, 100) < 0)
		return -ETIMEDOUT;

	val = stv0299_readreg (state, 0x08);

	if (stv0299_writeregI (state, 0x08, (val & ~0x7) | 0x6))  /* DiSEqC mode */
		return -EREMOTEIO;

	for (i=0; i<m->msg_len; i++) {
		if (stv0299_wait_diseqc_fifo (state, 100) < 0)
			return -ETIMEDOUT;

		if (stv0299_writeregI (state, 0x09, m->msg[i]))
			return -EREMOTEIO;
	}

	if (stv0299_wait_diseqc_idle (state, 100) < 0)
		return -ETIMEDOUT;

	return 0;
}

static int stv0299_send_diseqc_burst (struct dvb_frontend* fe, fe_sec_mini_cmd_t burst)
{
	struct stv0299_state* state = fe->demodulator_priv;
	u8 val;

	dprintk ("%s\n", __FUNCTION__);

	if (stv0299_wait_diseqc_idle (state, 100) < 0)
		return -ETIMEDOUT;

	val = stv0299_readreg (state, 0x08);

	if (stv0299_writeregI (state, 0x08, (val & ~0x7) | 0x2))	/* burst mode */
		return -EREMOTEIO;

	if (stv0299_writeregI (state, 0x09, burst == SEC_MINI_A ? 0x00 : 0xff))
		return -EREMOTEIO;

	if (stv0299_wait_diseqc_idle (state, 100) < 0)
		return -ETIMEDOUT;

	if (stv0299_writeregI (state, 0x08, val))
		return -EREMOTEIO;

	return 0;
}

static int stv0299_set_tone (struct dvb_frontend* fe, fe_sec_tone_mode_t tone)
{
	struct stv0299_state* state = fe->demodulator_priv;
	u8 val;

	if (stv0299_wait_diseqc_idle (state, 100) < 0)
		return -ETIMEDOUT;

	val = stv0299_readreg (state, 0x08);

	switch (tone) {
	case SEC_TONE_ON:
		return stv0299_writeregI (state, 0x08, val | 0x3);

	case SEC_TONE_OFF:
		return stv0299_writeregI (state, 0x08, (val & ~0x3) | 0x02);

	default:
		return -EINVAL;
	}
}

static int stv0299_set_voltage (struct dvb_frontend* fe, fe_sec_voltage_t voltage)
{
	struct stv0299_state* state = fe->demodulator_priv;
	u8 reg0x08;
	u8 reg0x0c;

	dprintk("%s: %s\n", __FUNCTION__,
		voltage == SEC_VOLTAGE_13 ? "SEC_VOLTAGE_13" :
		voltage == SEC_VOLTAGE_18 ? "SEC_VOLTAGE_18" : "??");

	reg0x08 = stv0299_readreg (state, 0x08);
	reg0x0c = stv0299_readreg (state, 0x0c);

	/**
	 *  H/V switching over OP0, OP1 and OP2 are LNB power enable bits
	 */
	reg0x0c &= 0x0f;

	if (voltage == SEC_VOLTAGE_OFF) {
		stv0299_writeregI (state, 0x0c, 0x00); /*	LNB power off! */
		return stv0299_writeregI (state, 0x08, 0x00); /*	LNB power off! */
	}

	stv0299_writeregI (state, 0x08, (reg0x08 & 0x3f) | (state->config->lock_output << 6));

	switch (voltage) {
	case SEC_VOLTAGE_13:
		if (state->config->volt13_op0_op1 == STV0299_VOLT13_OP0) reg0x0c |= 0x10;
		else reg0x0c |= 0x40;

		return stv0299_writeregI(state, 0x0c, reg0x0c);

	case SEC_VOLTAGE_18:
		return stv0299_writeregI(state, 0x0c, reg0x0c | 0x50);
	default:
		return -EINVAL;
	};
}

static int stv0299_send_legacy_dish_cmd (struct dvb_frontend* fe, unsigned long cmd)
{
	struct stv0299_state* state = fe->demodulator_priv;
	u8 reg0x08;
	u8 reg0x0c;
	u8 lv_mask = 0x40;
	u8 last = 1;
	int i;
	struct timeval nexttime;
	struct timeval tv[10];

	reg0x08 = stv0299_readreg (state, 0x08);
	reg0x0c = stv0299_readreg (state, 0x0c);
	reg0x0c &= 0x0f;
	stv0299_writeregI (state, 0x08, (reg0x08 & 0x3f) | (state->config->lock_output << 6));
	if (state->config->volt13_op0_op1 == STV0299_VOLT13_OP0)
		lv_mask = 0x10;

	cmd = cmd << 1;
	if (debug_legacy_dish_switch)
		printk ("%s switch command: 0x%04lx\n",__FUNCTION__, cmd);

	do_gettimeofday (&nexttime);
	if (debug_legacy_dish_switch)
		memcpy (&tv[0], &nexttime, sizeof (struct timeval));
	stv0299_writeregI (state, 0x0c, reg0x0c | 0x50); /* set LNB to 18V */

	dvb_frontend_sleep_until(&nexttime, 32000);

	for (i=0; i<9; i++) {
		if (debug_legacy_dish_switch)
			do_gettimeofday (&tv[i+1]);
		if((cmd & 0x01) != last) {
			/* set voltage to (last ? 13V : 18V) */
			stv0299_writeregI (state, 0x0c, reg0x0c | (last ? lv_mask : 0x50));
			last = (last) ? 0 : 1;
		}

		cmd = cmd >> 1;

		if (i != 8)
			dvb_frontend_sleep_until(&nexttime, 8000);
	}
	if (debug_legacy_dish_switch) {
		printk ("%s(%d): switch delay (should be 32k followed by all 8k\n",
			__FUNCTION__, fe->dvb->num);
		for (i = 1; i < 10; i++)
			printk ("%d: %d\n", i, timeval_usec_diff(tv[i-1] , tv[i]));
	}

	return 0;
}

static int stv0299_init (struct dvb_frontend* fe)
{
	struct stv0299_state* state = fe->demodulator_priv;
	int i;

	dprintk("stv0299: init chip\n");

	for (i=0; !(state->config->inittab[i] == 0xff && state->config->inittab[i+1] == 0xff); i+=2)
		stv0299_writeregI(state, state->config->inittab[i], state->config->inittab[i+1]);

	return 0;
}

static int stv0299_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct stv0299_state* state = fe->demodulator_priv;

	u8 signal = 0xff - stv0299_readreg (state, 0x18);
	u8 sync = stv0299_readreg (state, 0x1b);

	dprintk ("%s : FE_READ_STATUS : VSTATUS: 0x%02x\n", __FUNCTION__, sync);
	*status = 0;

	if (signal > 10)
		*status |= FE_HAS_SIGNAL;

	if (sync & 0x80)
		*status |= FE_HAS_CARRIER;

	if (sync & 0x10)
		*status |= FE_HAS_VITERBI;

	if (sync & 0x08)
		*status |= FE_HAS_SYNC;

	if ((sync & 0x98) == 0x98)
		*status |= FE_HAS_LOCK;

	return 0;
}

static int stv0299_read_ber(struct dvb_frontend* fe, u32* ber)
{
	struct stv0299_state* state = fe->demodulator_priv;

	if (state->errmode != STATUS_BER) return 0;
	*ber = (stv0299_readreg (state, 0x1d) << 8) | stv0299_readreg (state, 0x1e);

	return 0;
}

static int stv0299_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	struct stv0299_state* state = fe->demodulator_priv;

	s32 signal =  0xffff - ((stv0299_readreg (state, 0x18) << 8)
			       | stv0299_readreg (state, 0x19));

	dprintk ("%s : FE_READ_SIGNAL_STRENGTH : AGC2I: 0x%02x%02x, signal=0x%04x\n", __FUNCTION__,
		 stv0299_readreg (state, 0x18),
		 stv0299_readreg (state, 0x19), (int) signal);

	signal = signal * 5 / 4;
	*strength = (signal > 0xffff) ? 0xffff : (signal < 0) ? 0 : signal;

	return 0;
}

static int stv0299_read_snr(struct dvb_frontend* fe, u16* snr)
{
	struct stv0299_state* state = fe->demodulator_priv;

	s32 xsnr = 0xffff - ((stv0299_readreg (state, 0x24) << 8)
			   | stv0299_readreg (state, 0x25));
	xsnr = 3 * (xsnr - 0xa100);
	*snr = (xsnr > 0xffff) ? 0xffff : (xsnr < 0) ? 0 : xsnr;

	return 0;
}

static int stv0299_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct stv0299_state* state = fe->demodulator_priv;

	if (state->errmode != STATUS_UCBLOCKS) *ucblocks = 0;
	else *ucblocks = (stv0299_readreg (state, 0x1d) << 8) | stv0299_readreg (state, 0x1e);

	return 0;
}

static int stv0299_set_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters * p)
{
	struct stv0299_state* state = fe->demodulator_priv;
	int invval = 0;

	dprintk ("%s : FE_SET_FRONTEND\n", __FUNCTION__);

	// set the inversion
	if (p->inversion == INVERSION_OFF) invval = 0;
	else if (p->inversion == INVERSION_ON) invval = 1;
	else {
		printk("stv0299 does not support auto-inversion\n");
		return -EINVAL;
	}
	if (state->config->invert) invval = (~invval) & 1;
	stv0299_writeregI(state, 0x0c, (stv0299_readreg(state, 0x0c) & 0xfe) | invval);

	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe, p);
		if (fe->ops.i2c_gate_ctrl) fe->ops.i2c_gate_ctrl(fe, 0);
	}

	stv0299_set_FEC (state, p->u.qpsk.fec_inner);
	stv0299_set_symbolrate (fe, p->u.qpsk.symbol_rate);
	stv0299_writeregI(state, 0x22, 0x00);
	stv0299_writeregI(state, 0x23, 0x00);

	state->tuner_frequency = p->frequency;
	state->fec_inner = p->u.qpsk.fec_inner;
	state->symbol_rate = p->u.qpsk.symbol_rate;

	return 0;
}

static int stv0299_get_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters * p)
{
	struct stv0299_state* state = fe->demodulator_priv;
	s32 derot_freq;
	int invval;

	derot_freq = (s32)(s16) ((stv0299_readreg (state, 0x22) << 8)
				| stv0299_readreg (state, 0x23));

	derot_freq *= (state->config->mclk >> 16);
	derot_freq += 500;
	derot_freq /= 1000;

	p->frequency += derot_freq;

	invval = stv0299_readreg (state, 0x0c) & 1;
	if (state->config->invert) invval = (~invval) & 1;
	p->inversion = invval ? INVERSION_ON : INVERSION_OFF;

	p->u.qpsk.fec_inner = stv0299_get_fec (state);
	p->u.qpsk.symbol_rate = stv0299_get_symbolrate (state);

	return 0;
}

static int stv0299_sleep(struct dvb_frontend* fe)
{
	struct stv0299_state* state = fe->demodulator_priv;

	stv0299_writeregI(state, 0x02, 0x80);
	state->initialised = 0;

	return 0;
}

static int stv0299_i2c_gate_ctrl(struct dvb_frontend* fe, int enable)
{
	struct stv0299_state* state = fe->demodulator_priv;

	if (enable) {
		stv0299_writeregI(state, 0x05, 0xb5);
	} else {
		stv0299_writeregI(state, 0x05, 0x35);
	}
	udelay(1);
	return 0;
}

static int stv0299_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings* fesettings)
{
	struct stv0299_state* state = fe->demodulator_priv;

	fesettings->min_delay_ms = state->config->min_delay_ms;
	if (fesettings->parameters.u.qpsk.symbol_rate < 10000000) {
		fesettings->step_size = fesettings->parameters.u.qpsk.symbol_rate / 32000;
		fesettings->max_drift = 5000;
	} else {
		fesettings->step_size = fesettings->parameters.u.qpsk.symbol_rate / 16000;
		fesettings->max_drift = fesettings->parameters.u.qpsk.symbol_rate / 2000;
	}
	return 0;
}

static void stv0299_release(struct dvb_frontend* fe)
{
	struct stv0299_state* state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops stv0299_ops;

struct dvb_frontend* stv0299_attach(const struct stv0299_config* config,
				    struct i2c_adapter* i2c)
{
	struct stv0299_state* state = NULL;
	int id;

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct stv0299_state), GFP_KERNEL);
	if (state == NULL) goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	state->initialised = 0;
	state->tuner_frequency = 0;
	state->symbol_rate = 0;
	state->fec_inner = 0;
	state->errmode = STATUS_BER;

	/* check if the demod is there */
	stv0299_writeregI(state, 0x02, 0x34); /* standby off */
	msleep(200);
	id = stv0299_readreg(state, 0x00);

	/* register 0x00 contains 0xa1 for STV0299 and STV0299B */
	/* register 0x00 might contain 0x80 when returning from standby */
	if (id != 0xa1 && id != 0x80) goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &stv0299_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops stv0299_ops = {

	.info = {
		.name			= "ST STV0299 DVB-S",
		.type			= FE_QPSK,
		.frequency_min		= 950000,
		.frequency_max		= 2150000,
		.frequency_stepsize	= 125,	 /* kHz for QPSK frontends */
		.frequency_tolerance	= 0,
		.symbol_rate_min	= 1000000,
		.symbol_rate_max	= 45000000,
		.symbol_rate_tolerance	= 500,	/* ppm */
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		      FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 |
		      FE_CAN_QPSK |
		      FE_CAN_FEC_AUTO
	},

	.release = stv0299_release,

	.init = stv0299_init,
	.sleep = stv0299_sleep,
	.write = stv0299_write,
	.i2c_gate_ctrl = stv0299_i2c_gate_ctrl,

	.set_frontend = stv0299_set_frontend,
	.get_frontend = stv0299_get_frontend,
	.get_tune_settings = stv0299_get_tune_settings,

	.read_status = stv0299_read_status,
	.read_ber = stv0299_read_ber,
	.read_signal_strength = stv0299_read_signal_strength,
	.read_snr = stv0299_read_snr,
	.read_ucblocks = stv0299_read_ucblocks,

	.diseqc_send_master_cmd = stv0299_send_diseqc_msg,
	.diseqc_send_burst = stv0299_send_diseqc_burst,
	.set_tone = stv0299_set_tone,
	.set_voltage = stv0299_set_voltage,
	.dishnetwork_send_legacy_command = stv0299_send_legacy_dish_cmd,
};

module_param(debug_legacy_dish_switch, int, 0444);
MODULE_PARM_DESC(debug_legacy_dish_switch, "Enable timing analysis for Dish Network legacy switches");

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("ST STV0299 DVB Demodulator driver");
MODULE_AUTHOR("Ralph Metzler, Holger Waechtler, Peter Schildmann, Felix Domke, "
	      "Andreas Oberritter, Andrew de Quincey, Kenneth Aafly");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(stv0299_attach);
