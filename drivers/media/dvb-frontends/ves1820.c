/*
    VES1820  - Single Chip Cable Channel Receiver driver module

    Copyright (C) 1999 Convergence Integrated Media GmbH <ralph@convergence.de>

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

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <asm/div64.h>

#include "dvb_frontend.h"
#include "ves1820.h"



struct ves1820_state {
	struct i2c_adapter* i2c;
	/* configuration settings */
	const struct ves1820_config* config;
	struct dvb_frontend frontend;

	/* private demodulator data */
	u8 reg0;
	u8 pwm;
};


static int verbose;

static u8 ves1820_inittab[] = {
	0x69, 0x6A, 0x93, 0x1A, 0x12, 0x46, 0x26, 0x1A,
	0x43, 0x6A, 0xAA, 0xAA, 0x1E, 0x85, 0x43, 0x20,
	0xE0, 0x00, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x40
};

static int ves1820_writereg(struct ves1820_state *state, u8 reg, u8 data)
{
	u8 buf[] = { 0x00, reg, data };
	struct i2c_msg msg = {.addr = state->config->demod_address,.flags = 0,.buf = buf,.len = 3 };
	int ret;

	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1)
		printk("ves1820: %s(): writereg error (reg == 0x%02x, "
			"val == 0x%02x, ret == %i)\n", __func__, reg, data, ret);

	return (ret != 1) ? -EREMOTEIO : 0;
}

static u8 ves1820_readreg(struct ves1820_state *state, u8 reg)
{
	u8 b0[] = { 0x00, reg };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = {
		{.addr = state->config->demod_address,.flags = 0,.buf = b0,.len = 2},
		{.addr = state->config->demod_address,.flags = I2C_M_RD,.buf = b1,.len = 1}
	};
	int ret;

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2)
		printk("ves1820: %s(): readreg error (reg == 0x%02x, "
		"ret == %i)\n", __func__, reg, ret);

	return b1[0];
}

static int ves1820_setup_reg0(struct ves1820_state *state,
			      u8 reg0, enum fe_spectral_inversion inversion)
{
	reg0 |= state->reg0 & 0x62;

	if (INVERSION_ON == inversion) {
		if (!state->config->invert) reg0 |= 0x20;
		else reg0 &= ~0x20;
	} else if (INVERSION_OFF == inversion) {
		if (!state->config->invert) reg0 &= ~0x20;
		else reg0 |= 0x20;
	}

	ves1820_writereg(state, 0x00, reg0 & 0xfe);
	ves1820_writereg(state, 0x00, reg0 | 0x01);

	state->reg0 = reg0;

	return 0;
}

static int ves1820_set_symbolrate(struct ves1820_state *state, u32 symbolrate)
{
	s32 BDR;
	s32 BDRI;
	s16 SFIL = 0;
	u16 NDEC = 0;
	u32 ratio;
	u32 fin;
	u32 tmp;
	u64 fptmp;
	u64 fpxin;

	if (symbolrate > state->config->xin / 2)
		symbolrate = state->config->xin / 2;

	if (symbolrate < 500000)
		symbolrate = 500000;

	if (symbolrate < state->config->xin / 16)
		NDEC = 1;
	if (symbolrate < state->config->xin / 32)
		NDEC = 2;
	if (symbolrate < state->config->xin / 64)
		NDEC = 3;

	/* yeuch! */
	fpxin = state->config->xin * 10;
	fptmp = fpxin; do_div(fptmp, 123);
	if (symbolrate < fptmp)
		SFIL = 1;
	fptmp = fpxin; do_div(fptmp, 160);
	if (symbolrate < fptmp)
		SFIL = 0;
	fptmp = fpxin; do_div(fptmp, 246);
	if (symbolrate < fptmp)
		SFIL = 1;
	fptmp = fpxin; do_div(fptmp, 320);
	if (symbolrate < fptmp)
		SFIL = 0;
	fptmp = fpxin; do_div(fptmp, 492);
	if (symbolrate < fptmp)
		SFIL = 1;
	fptmp = fpxin; do_div(fptmp, 640);
	if (symbolrate < fptmp)
		SFIL = 0;
	fptmp = fpxin; do_div(fptmp, 984);
	if (symbolrate < fptmp)
		SFIL = 1;

	fin = state->config->xin >> 4;
	symbolrate <<= NDEC;
	ratio = (symbolrate << 4) / fin;
	tmp = ((symbolrate << 4) % fin) << 8;
	ratio = (ratio << 8) + tmp / fin;
	tmp = (tmp % fin) << 8;
	ratio = (ratio << 8) + DIV_ROUND_CLOSEST(tmp, fin);

	BDR = ratio;
	BDRI = (((state->config->xin << 5) / symbolrate) + 1) / 2;

	if (BDRI > 0xFF)
		BDRI = 0xFF;

	SFIL = (SFIL << 4) | ves1820_inittab[0x0E];

	NDEC = (NDEC << 6) | ves1820_inittab[0x03];

	ves1820_writereg(state, 0x03, NDEC);
	ves1820_writereg(state, 0x0a, BDR & 0xff);
	ves1820_writereg(state, 0x0b, (BDR >> 8) & 0xff);
	ves1820_writereg(state, 0x0c, (BDR >> 16) & 0x3f);

	ves1820_writereg(state, 0x0d, BDRI);
	ves1820_writereg(state, 0x0e, SFIL);

	return 0;
}

static int ves1820_init(struct dvb_frontend* fe)
{
	struct ves1820_state* state = fe->demodulator_priv;
	int i;

	ves1820_writereg(state, 0, 0);

	for (i = 0; i < sizeof(ves1820_inittab); i++)
		ves1820_writereg(state, i, ves1820_inittab[i]);
	if (state->config->selagc)
		ves1820_writereg(state, 2, ves1820_inittab[2] | 0x08);

	ves1820_writereg(state, 0x34, state->pwm);

	return 0;
}

static int ves1820_set_parameters(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct ves1820_state* state = fe->demodulator_priv;
	static const u8 reg0x00[] = { 0x00, 0x04, 0x08, 0x0c, 0x10 };
	static const u8 reg0x01[] = { 140, 140, 106, 100, 92 };
	static const u8 reg0x05[] = { 135, 100, 70, 54, 38 };
	static const u8 reg0x08[] = { 162, 116, 67, 52, 35 };
	static const u8 reg0x09[] = { 145, 150, 106, 126, 107 };
	int real_qam = p->modulation - QAM_16;

	if (real_qam < 0 || real_qam > 4)
		return -EINVAL;

	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe);
		if (fe->ops.i2c_gate_ctrl) fe->ops.i2c_gate_ctrl(fe, 0);
	}

	ves1820_set_symbolrate(state, p->symbol_rate);
	ves1820_writereg(state, 0x34, state->pwm);

	ves1820_writereg(state, 0x01, reg0x01[real_qam]);
	ves1820_writereg(state, 0x05, reg0x05[real_qam]);
	ves1820_writereg(state, 0x08, reg0x08[real_qam]);
	ves1820_writereg(state, 0x09, reg0x09[real_qam]);

	ves1820_setup_reg0(state, reg0x00[real_qam], p->inversion);
	ves1820_writereg(state, 2, ves1820_inittab[2] | (state->config->selagc ? 0x08 : 0));
	return 0;
}

static int ves1820_read_status(struct dvb_frontend *fe,
			       enum fe_status *status)
{
	struct ves1820_state* state = fe->demodulator_priv;
	int sync;

	*status = 0;
	sync = ves1820_readreg(state, 0x11);

	if (sync & 1)
		*status |= FE_HAS_SIGNAL;

	if (sync & 2)
		*status |= FE_HAS_CARRIER;

	if (sync & 2)	/* XXX FIXME! */
		*status |= FE_HAS_VITERBI;

	if (sync & 4)
		*status |= FE_HAS_SYNC;

	if (sync & 8)
		*status |= FE_HAS_LOCK;

	return 0;
}

static int ves1820_read_ber(struct dvb_frontend* fe, u32* ber)
{
	struct ves1820_state* state = fe->demodulator_priv;

	u32 _ber = ves1820_readreg(state, 0x14) |
			(ves1820_readreg(state, 0x15) << 8) |
			((ves1820_readreg(state, 0x16) & 0x0f) << 16);
	*ber = 10 * _ber;

	return 0;
}

static int ves1820_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	struct ves1820_state* state = fe->demodulator_priv;

	u8 gain = ves1820_readreg(state, 0x17);
	*strength = (gain << 8) | gain;

	return 0;
}

static int ves1820_read_snr(struct dvb_frontend* fe, u16* snr)
{
	struct ves1820_state* state = fe->demodulator_priv;

	u8 quality = ~ves1820_readreg(state, 0x18);
	*snr = (quality << 8) | quality;

	return 0;
}

static int ves1820_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct ves1820_state* state = fe->demodulator_priv;

	*ucblocks = ves1820_readreg(state, 0x13) & 0x7f;
	if (*ucblocks == 0x7f)
		*ucblocks = 0xffffffff;

	/* reset uncorrected block counter */
	ves1820_writereg(state, 0x10, ves1820_inittab[0x10] & 0xdf);
	ves1820_writereg(state, 0x10, ves1820_inittab[0x10]);

	return 0;
}

static int ves1820_get_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct ves1820_state* state = fe->demodulator_priv;
	int sync;
	s8 afc = 0;

	sync = ves1820_readreg(state, 0x11);
	afc = ves1820_readreg(state, 0x19);
	if (verbose) {
		/* AFC only valid when carrier has been recovered */
		printk(sync & 2 ? "ves1820: AFC (%d) %dHz\n" :
			"ves1820: [AFC (%d) %dHz]\n", afc, -((s32) p->symbol_rate * afc) >> 10);
	}

	if (!state->config->invert) {
		p->inversion = (state->reg0 & 0x20) ? INVERSION_ON : INVERSION_OFF;
	} else {
		p->inversion = (!(state->reg0 & 0x20)) ? INVERSION_ON : INVERSION_OFF;
	}

	p->modulation = ((state->reg0 >> 2) & 7) + QAM_16;

	p->fec_inner = FEC_NONE;

	p->frequency = ((p->frequency + 31250) / 62500) * 62500;
	if (sync & 2)
		p->frequency -= ((s32) p->symbol_rate * afc) >> 10;

	return 0;
}

static int ves1820_sleep(struct dvb_frontend* fe)
{
	struct ves1820_state* state = fe->demodulator_priv;

	ves1820_writereg(state, 0x1b, 0x02);	/* pdown ADC */
	ves1820_writereg(state, 0x00, 0x80);	/* standby */

	return 0;
}

static int ves1820_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings* fesettings)
{

	fesettings->min_delay_ms = 200;
	fesettings->step_size = 0;
	fesettings->max_drift = 0;
	return 0;
}

static void ves1820_release(struct dvb_frontend* fe)
{
	struct ves1820_state* state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops ves1820_ops;

struct dvb_frontend* ves1820_attach(const struct ves1820_config* config,
				    struct i2c_adapter* i2c,
				    u8 pwm)
{
	struct ves1820_state* state = NULL;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct ves1820_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* setup the state */
	state->reg0 = ves1820_inittab[0];
	state->config = config;
	state->i2c = i2c;
	state->pwm = pwm;

	/* check if the demod is there */
	if ((ves1820_readreg(state, 0x1a) & 0xf0) != 0x70)
		goto error;

	if (verbose)
		printk("ves1820: pwm=0x%02x\n", state->pwm);

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &ves1820_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.ops.info.symbol_rate_min = (state->config->xin / 2) / 64;      /* SACLK/64 == (XIN/2)/64 */
	state->frontend.ops.info.symbol_rate_max = (state->config->xin / 2) / 4;       /* SACLK/4 */
	state->frontend.demodulator_priv = state;

	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops ves1820_ops = {
	.delsys = { SYS_DVBC_ANNEX_A },
	.info = {
		.name = "VLSI VES1820 DVB-C",
		.frequency_stepsize = 62500,
		.frequency_min = 47000000,
		.frequency_max = 862000000,
		.caps = FE_CAN_QAM_16 |
			FE_CAN_QAM_32 |
			FE_CAN_QAM_64 |
			FE_CAN_QAM_128 |
			FE_CAN_QAM_256 |
			FE_CAN_FEC_AUTO
	},

	.release = ves1820_release,

	.init = ves1820_init,
	.sleep = ves1820_sleep,

	.set_frontend = ves1820_set_parameters,
	.get_frontend = ves1820_get_frontend,
	.get_tune_settings = ves1820_get_tune_settings,

	.read_status = ves1820_read_status,
	.read_ber = ves1820_read_ber,
	.read_signal_strength = ves1820_read_signal_strength,
	.read_snr = ves1820_read_snr,
	.read_ucblocks = ves1820_read_ucblocks,
};

module_param(verbose, int, 0644);
MODULE_PARM_DESC(verbose, "print AFC offset after tuning for debugging the PWM setting");

MODULE_DESCRIPTION("VLSI VES1820 DVB-C Demodulator driver");
MODULE_AUTHOR("Ralph Metzler, Holger Waechtler");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(ves1820_attach);
