/*
    Conexant 22702 DVB OFDM demodulator driver

    based on:
	Alps TDMB7 DVB OFDM demodulator driver

    Copyright (C) 2001-2002 Convergence Integrated Media GmbH
	  Holger Waechtler <holger@convergence.de>

    Copyright (C) 2004 Steven Toth <stoth@linuxtv.org>

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
#include "cx22702.h"

struct cx22702_state {

	struct i2c_adapter *i2c;

	/* configuration settings */
	const struct cx22702_config *config;

	struct dvb_frontend frontend;

	/* previous uncorrected block counter */
	u8 prevUCBlocks;
};

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Enable verbose debug messages");

#define dprintk	if (debug) printk

/* Register values to initialise the demod */
static const u8 init_tab[] = {
	0x00, 0x00, /* Stop acquisition */
	0x0B, 0x06,
	0x09, 0x01,
	0x0D, 0x41,
	0x16, 0x32,
	0x20, 0x0A,
	0x21, 0x17,
	0x24, 0x3e,
	0x26, 0xff,
	0x27, 0x10,
	0x28, 0x00,
	0x29, 0x00,
	0x2a, 0x10,
	0x2b, 0x00,
	0x2c, 0x10,
	0x2d, 0x00,
	0x48, 0xd4,
	0x49, 0x56,
	0x6b, 0x1e,
	0xc8, 0x02,
	0xf9, 0x00,
	0xfa, 0x00,
	0xfb, 0x00,
	0xfc, 0x00,
	0xfd, 0x00,
};

static int cx22702_writereg(struct cx22702_state *state, u8 reg, u8 data)
{
	int ret;
	u8 buf[] = { reg, data };
	struct i2c_msg msg = {
		.addr = state->config->demod_address, .flags = 0,
			.buf = buf, .len = 2 };

	ret = i2c_transfer(state->i2c, &msg, 1);

	if (unlikely(ret != 1)) {
		printk(KERN_ERR
			"%s: error (reg == 0x%02x, val == 0x%02x, ret == %i)\n",
			__func__, reg, data, ret);
		return -1;
	}

	return 0;
}

static u8 cx22702_readreg(struct cx22702_state *state, u8 reg)
{
	int ret;
	u8 data;

	struct i2c_msg msg[] = {
		{ .addr = state->config->demod_address, .flags = 0,
			.buf = &reg, .len = 1 },
		{ .addr = state->config->demod_address, .flags = I2C_M_RD,
			.buf = &data, .len = 1 } };

	ret = i2c_transfer(state->i2c, msg, 2);

	if (unlikely(ret != 2)) {
		printk(KERN_ERR "%s: error (reg == 0x%02x, ret == %i)\n",
			__func__, reg, ret);
		return 0;
	}

	return data;
}

static int cx22702_set_inversion(struct cx22702_state *state, int inversion)
{
	u8 val;

	val = cx22702_readreg(state, 0x0C);
	switch (inversion) {
	case INVERSION_AUTO:
		return -EOPNOTSUPP;
	case INVERSION_ON:
		val |= 0x01;
		break;
	case INVERSION_OFF:
		val &= 0xfe;
		break;
	default:
		return -EINVAL;
	}
	return cx22702_writereg(state, 0x0C, val);
}

/* Retrieve the demod settings */
static int cx22702_get_tps(struct cx22702_state *state,
	struct dvb_ofdm_parameters *p)
{
	u8 val;

	/* Make sure the TPS regs are valid */
	if (!(cx22702_readreg(state, 0x0A) & 0x20))
		return -EAGAIN;

	val = cx22702_readreg(state, 0x01);
	switch ((val & 0x18) >> 3) {
	case 0:
		p->constellation = QPSK;
		break;
	case 1:
		p->constellation = QAM_16;
		break;
	case 2:
		p->constellation = QAM_64;
		break;
	}
	switch (val & 0x07) {
	case 0:
		p->hierarchy_information = HIERARCHY_NONE;
		break;
	case 1:
		p->hierarchy_information = HIERARCHY_1;
		break;
	case 2:
		p->hierarchy_information = HIERARCHY_2;
		break;
	case 3:
		p->hierarchy_information = HIERARCHY_4;
		break;
	}


	val = cx22702_readreg(state, 0x02);
	switch ((val & 0x38) >> 3) {
	case 0:
		p->code_rate_HP = FEC_1_2;
		break;
	case 1:
		p->code_rate_HP = FEC_2_3;
		break;
	case 2:
		p->code_rate_HP = FEC_3_4;
		break;
	case 3:
		p->code_rate_HP = FEC_5_6;
		break;
	case 4:
		p->code_rate_HP = FEC_7_8;
		break;
	}
	switch (val & 0x07) {
	case 0:
		p->code_rate_LP = FEC_1_2;
		break;
	case 1:
		p->code_rate_LP = FEC_2_3;
		break;
	case 2:
		p->code_rate_LP = FEC_3_4;
		break;
	case 3:
		p->code_rate_LP = FEC_5_6;
		break;
	case 4:
		p->code_rate_LP = FEC_7_8;
		break;
	}

	val = cx22702_readreg(state, 0x03);
	switch ((val & 0x0c) >> 2) {
	case 0:
		p->guard_interval = GUARD_INTERVAL_1_32;
		break;
	case 1:
		p->guard_interval = GUARD_INTERVAL_1_16;
		break;
	case 2:
		p->guard_interval = GUARD_INTERVAL_1_8;
		break;
	case 3:
		p->guard_interval = GUARD_INTERVAL_1_4;
		break;
	}
	switch (val & 0x03) {
	case 0:
		p->transmission_mode = TRANSMISSION_MODE_2K;
		break;
	case 1:
		p->transmission_mode = TRANSMISSION_MODE_8K;
		break;
	}

	return 0;
}

static int cx22702_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct cx22702_state *state = fe->demodulator_priv;
	u8 val;

	dprintk("%s(%d)\n", __func__, enable);
	val = cx22702_readreg(state, 0x0D);
	if (enable)
		val &= 0xfe;
	else
		val |= 0x01;
	return cx22702_writereg(state, 0x0D, val);
}

/* Talk to the demod, set the FEC, GUARD, QAM settings etc */
static int cx22702_set_tps(struct dvb_frontend *fe,
	struct dvb_frontend_parameters *p)
{
	u8 val;
	struct cx22702_state *state = fe->demodulator_priv;

	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe, p);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	/* set inversion */
	cx22702_set_inversion(state, p->inversion);

	/* set bandwidth */
	val = cx22702_readreg(state, 0x0C) & 0xcf;
	switch (p->u.ofdm.bandwidth) {
	case BANDWIDTH_6_MHZ:
		val |= 0x20;
		break;
	case BANDWIDTH_7_MHZ:
		val |= 0x10;
		break;
	case BANDWIDTH_8_MHZ:
		break;
	default:
		dprintk("%s: invalid bandwidth\n", __func__);
		return -EINVAL;
	}
	cx22702_writereg(state, 0x0C, val);

	p->u.ofdm.code_rate_LP = FEC_AUTO; /* temp hack as manual not working */

	/* use auto configuration? */
	if ((p->u.ofdm.hierarchy_information == HIERARCHY_AUTO) ||
	   (p->u.ofdm.constellation == QAM_AUTO) ||
	   (p->u.ofdm.code_rate_HP == FEC_AUTO) ||
	   (p->u.ofdm.code_rate_LP == FEC_AUTO) ||
	   (p->u.ofdm.guard_interval == GUARD_INTERVAL_AUTO) ||
	   (p->u.ofdm.transmission_mode == TRANSMISSION_MODE_AUTO)) {

		/* TPS Source - use hardware driven values */
		cx22702_writereg(state, 0x06, 0x10);
		cx22702_writereg(state, 0x07, 0x9);
		cx22702_writereg(state, 0x08, 0xC1);
		cx22702_writereg(state, 0x0B, cx22702_readreg(state, 0x0B)
			& 0xfc);
		cx22702_writereg(state, 0x0C,
			(cx22702_readreg(state, 0x0C) & 0xBF) | 0x40);
		cx22702_writereg(state, 0x00, 0x01); /* Begin acquisition */
		dprintk("%s: Autodetecting\n", __func__);
		return 0;
	}

	/* manually programmed values */
	switch (p->u.ofdm.constellation) {		/* mask 0x18 */
	case QPSK:
		val = 0x00;
		break;
	case QAM_16:
		val = 0x08;
		break;
	case QAM_64:
		val = 0x10;
		break;
	default:
		dprintk("%s: invalid constellation\n", __func__);
		return -EINVAL;
	}
	switch (p->u.ofdm.hierarchy_information) {	/* mask 0x07 */
	case HIERARCHY_NONE:
		break;
	case HIERARCHY_1:
		val |= 0x01;
		break;
	case HIERARCHY_2:
		val |= 0x02;
		break;
	case HIERARCHY_4:
		val |= 0x03;
		break;
	default:
		dprintk("%s: invalid hierarchy\n", __func__);
		return -EINVAL;
	}
	cx22702_writereg(state, 0x06, val);

	switch (p->u.ofdm.code_rate_HP) {		/* mask 0x38 */
	case FEC_NONE:
	case FEC_1_2:
		val = 0x00;
		break;
	case FEC_2_3:
		val = 0x08;
		break;
	case FEC_3_4:
		val = 0x10;
		break;
	case FEC_5_6:
		val = 0x18;
		break;
	case FEC_7_8:
		val = 0x20;
		break;
	default:
		dprintk("%s: invalid code_rate_HP\n", __func__);
		return -EINVAL;
	}
	switch (p->u.ofdm.code_rate_LP) {		/* mask 0x07 */
	case FEC_NONE:
	case FEC_1_2:
		break;
	case FEC_2_3:
		val |= 0x01;
		break;
	case FEC_3_4:
		val |= 0x02;
		break;
	case FEC_5_6:
		val |= 0x03;
		break;
	case FEC_7_8:
		val |= 0x04;
		break;
	default:
		dprintk("%s: invalid code_rate_LP\n", __func__);
		return -EINVAL;
	}
	cx22702_writereg(state, 0x07, val);

	switch (p->u.ofdm.guard_interval) {		/* mask 0x0c */
	case GUARD_INTERVAL_1_32:
		val = 0x00;
		break;
	case GUARD_INTERVAL_1_16:
		val = 0x04;
		break;
	case GUARD_INTERVAL_1_8:
		val = 0x08;
		break;
	case GUARD_INTERVAL_1_4:
		val = 0x0c;
		break;
	default:
		dprintk("%s: invalid guard_interval\n", __func__);
		return -EINVAL;
	}
	switch (p->u.ofdm.transmission_mode) {		/* mask 0x03 */
	case TRANSMISSION_MODE_2K:
		break;
	case TRANSMISSION_MODE_8K:
		val |= 0x1;
		break;
	default:
		dprintk("%s: invalid transmission_mode\n", __func__);
		return -EINVAL;
	}
	cx22702_writereg(state, 0x08, val);
	cx22702_writereg(state, 0x0B,
		(cx22702_readreg(state, 0x0B) & 0xfc) | 0x02);
	cx22702_writereg(state, 0x0C,
		(cx22702_readreg(state, 0x0C) & 0xBF) | 0x40);

	/* Begin channel acquisition */
	cx22702_writereg(state, 0x00, 0x01);

	return 0;
}

/* Reset the demod hardware and reset all of the configuration registers
   to a default state. */
static int cx22702_init(struct dvb_frontend *fe)
{
	int i;
	struct cx22702_state *state = fe->demodulator_priv;

	cx22702_writereg(state, 0x00, 0x02);

	msleep(10);

	for (i = 0; i < ARRAY_SIZE(init_tab); i += 2)
		cx22702_writereg(state, init_tab[i], init_tab[i + 1]);

	cx22702_writereg(state, 0xf8, (state->config->output_mode << 1)
		& 0x02);

	cx22702_i2c_gate_ctrl(fe, 0);

	return 0;
}

static int cx22702_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct cx22702_state *state = fe->demodulator_priv;
	u8 reg0A;
	u8 reg23;

	*status = 0;

	reg0A = cx22702_readreg(state, 0x0A);
	reg23 = cx22702_readreg(state, 0x23);

	dprintk("%s: status demod=0x%02x agc=0x%02x\n"
		, __func__, reg0A, reg23);

	if (reg0A & 0x10) {
		*status |= FE_HAS_LOCK;
		*status |= FE_HAS_VITERBI;
		*status |= FE_HAS_SYNC;
	}

	if (reg0A & 0x20)
		*status |= FE_HAS_CARRIER;

	if (reg23 < 0xf0)
		*status |= FE_HAS_SIGNAL;

	return 0;
}

static int cx22702_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct cx22702_state *state = fe->demodulator_priv;

	if (cx22702_readreg(state, 0xE4) & 0x02) {
		/* Realtime statistics */
		*ber = (cx22702_readreg(state, 0xDE) & 0x7F) << 7
			| (cx22702_readreg(state, 0xDF) & 0x7F);
	} else {
		/* Averagtine statistics */
		*ber = (cx22702_readreg(state, 0xDE) & 0x7F) << 7
			| cx22702_readreg(state, 0xDF);
	}

	return 0;
}

static int cx22702_read_signal_strength(struct dvb_frontend *fe,
	u16 *signal_strength)
{
	struct cx22702_state *state = fe->demodulator_priv;

	u16 rs_ber;
	rs_ber = cx22702_readreg(state, 0x23);
	*signal_strength = (rs_ber << 8) | rs_ber;

	return 0;
}

static int cx22702_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct cx22702_state *state = fe->demodulator_priv;

	u16 rs_ber;
	if (cx22702_readreg(state, 0xE4) & 0x02) {
		/* Realtime statistics */
		rs_ber = (cx22702_readreg(state, 0xDE) & 0x7F) << 7
			| (cx22702_readreg(state, 0xDF) & 0x7F);
	} else {
		/* Averagine statistics */
		rs_ber = (cx22702_readreg(state, 0xDE) & 0x7F) << 8
			| cx22702_readreg(state, 0xDF);
	}
	*snr = ~rs_ber;

	return 0;
}

static int cx22702_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct cx22702_state *state = fe->demodulator_priv;

	u8 _ucblocks;

	/* RS Uncorrectable Packet Count then reset */
	_ucblocks = cx22702_readreg(state, 0xE3);
	if (state->prevUCBlocks < _ucblocks)
		*ucblocks = (_ucblocks - state->prevUCBlocks);
	else
		*ucblocks = state->prevUCBlocks - _ucblocks;
	state->prevUCBlocks = _ucblocks;

	return 0;
}

static int cx22702_get_frontend(struct dvb_frontend *fe,
	struct dvb_frontend_parameters *p)
{
	struct cx22702_state *state = fe->demodulator_priv;

	u8 reg0C = cx22702_readreg(state, 0x0C);

	p->inversion = reg0C & 0x1 ? INVERSION_ON : INVERSION_OFF;
	return cx22702_get_tps(state, &p->u.ofdm);
}

static int cx22702_get_tune_settings(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 1000;
	return 0;
}

static void cx22702_release(struct dvb_frontend *fe)
{
	struct cx22702_state *state = fe->demodulator_priv;
	kfree(state);
}

static const struct dvb_frontend_ops cx22702_ops;

struct dvb_frontend *cx22702_attach(const struct cx22702_config *config,
	struct i2c_adapter *i2c)
{
	struct cx22702_state *state = NULL;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct cx22702_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;

	/* check if the demod is there */
	if (cx22702_readreg(state, 0x1f) != 0x3)
		goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &cx22702_ops,
		sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(cx22702_attach);

static const struct dvb_frontend_ops cx22702_ops = {

	.info = {
		.name			= "Conexant CX22702 DVB-T",
		.type			= FE_OFDM,
		.frequency_min		= 177000000,
		.frequency_max		= 858000000,
		.frequency_stepsize	= 166666,
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
		FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
		FE_CAN_HIERARCHY_AUTO | FE_CAN_GUARD_INTERVAL_AUTO |
		FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_RECOVER
	},

	.release = cx22702_release,

	.init = cx22702_init,
	.i2c_gate_ctrl = cx22702_i2c_gate_ctrl,

	.set_frontend = cx22702_set_tps,
	.get_frontend = cx22702_get_frontend,
	.get_tune_settings = cx22702_get_tune_settings,

	.read_status = cx22702_read_status,
	.read_ber = cx22702_read_ber,
	.read_signal_strength = cx22702_read_signal_strength,
	.read_snr = cx22702_read_snr,
	.read_ucblocks = cx22702_read_ucblocks,
};

MODULE_DESCRIPTION("Conexant CX22702 DVB-T Demodulator driver");
MODULE_AUTHOR("Steven Toth");
MODULE_LICENSE("GPL");
