/*
 * Driver for
 *    Samsung S5H1420 and
 *    PnpNetwork PN1010 QPSK Demodulator
 *
 * Copyright (C) 2005 Andrew de Quincey <adq_dvb@lidskialf.net>
 * Copyright (C) 2005-8 Patrick Boettcher <pb@linuxtv.org>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <asm/div64.h>

#include <linux/i2c.h>


#include "dvb_frontend.h"
#include "s5h1420.h"
#include "s5h1420_priv.h"

#define TONE_FREQ 22000

struct s5h1420_state {
	struct i2c_adapter* i2c;
	const struct s5h1420_config* config;

	struct dvb_frontend frontend;
	struct i2c_adapter tuner_i2c_adapter;

	u8 CON_1_val;

	u8 postlocked:1;
	u32 fclk;
	u32 tunedfreq;
	fe_code_rate_t fec_inner;
	u32 symbol_rate;

	/* FIXME: ugly workaround for flexcop's incapable i2c-controller
	 * it does not support repeated-start, workaround: write addr-1
	 * and then read
	 */
	u8 shadow[256];
};

static u32 s5h1420_getsymbolrate(struct s5h1420_state* state);
static int s5h1420_get_tune_settings(struct dvb_frontend* fe,
				     struct dvb_frontend_tune_settings* fesettings);


static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable debugging");

#define dprintk(x...) do { \
	if (debug) \
		printk(KERN_DEBUG "S5H1420: " x); \
} while (0)

static u8 s5h1420_readreg(struct s5h1420_state *state, u8 reg)
{
	int ret;
	u8 b[2];
	struct i2c_msg msg[] = {
		{ .addr = state->config->demod_address, .flags = 0, .buf = b, .len = 2 },
		{ .addr = state->config->demod_address, .flags = 0, .buf = &reg, .len = 1 },
		{ .addr = state->config->demod_address, .flags = I2C_M_RD, .buf = b, .len = 1 },
	};

	b[0] = (reg - 1) & 0xff;
	b[1] = state->shadow[(reg - 1) & 0xff];

	if (state->config->repeated_start_workaround) {
		ret = i2c_transfer(state->i2c, msg, 3);
		if (ret != 3)
			return ret;
	} else {
		ret = i2c_transfer(state->i2c, &msg[1], 1);
		if (ret != 1)
			return ret;
		ret = i2c_transfer(state->i2c, &msg[2], 1);
		if (ret != 1)
			return ret;
	}

	/* dprintk("rd(%02x): %02x %02x\n", state->config->demod_address, reg, b[0]); */

	return b[0];
}

static int s5h1420_writereg (struct s5h1420_state* state, u8 reg, u8 data)
{
	u8 buf[] = { reg, data };
	struct i2c_msg msg = { .addr = state->config->demod_address, .flags = 0, .buf = buf, .len = 2 };
	int err;

	/* dprintk("wr(%02x): %02x %02x\n", state->config->demod_address, reg, data); */
	err = i2c_transfer(state->i2c, &msg, 1);
	if (err != 1) {
		dprintk("%s: writereg error (err == %i, reg == 0x%02x, data == 0x%02x)\n", __func__, err, reg, data);
		return -EREMOTEIO;
	}
	state->shadow[reg] = data;

	return 0;
}

static int s5h1420_set_voltage (struct dvb_frontend* fe, fe_sec_voltage_t voltage)
{
	struct s5h1420_state* state = fe->demodulator_priv;

	dprintk("enter %s\n", __func__);

	switch(voltage) {
	case SEC_VOLTAGE_13:
		s5h1420_writereg(state, 0x3c,
				 (s5h1420_readreg(state, 0x3c) & 0xfe) | 0x02);
		break;

	case SEC_VOLTAGE_18:
		s5h1420_writereg(state, 0x3c, s5h1420_readreg(state, 0x3c) | 0x03);
		break;

	case SEC_VOLTAGE_OFF:
		s5h1420_writereg(state, 0x3c, s5h1420_readreg(state, 0x3c) & 0xfd);
		break;
	}

	dprintk("leave %s\n", __func__);
	return 0;
}

static int s5h1420_set_tone (struct dvb_frontend* fe, fe_sec_tone_mode_t tone)
{
	struct s5h1420_state* state = fe->demodulator_priv;

	dprintk("enter %s\n", __func__);
	switch(tone) {
	case SEC_TONE_ON:
		s5h1420_writereg(state, 0x3b,
				 (s5h1420_readreg(state, 0x3b) & 0x74) | 0x08);
		break;

	case SEC_TONE_OFF:
		s5h1420_writereg(state, 0x3b,
				 (s5h1420_readreg(state, 0x3b) & 0x74) | 0x01);
		break;
	}
	dprintk("leave %s\n", __func__);

	return 0;
}

static int s5h1420_send_master_cmd (struct dvb_frontend* fe,
				    struct dvb_diseqc_master_cmd* cmd)
{
	struct s5h1420_state* state = fe->demodulator_priv;
	u8 val;
	int i;
	unsigned long timeout;
	int result = 0;

	dprintk("enter %s\n", __func__);
	if (cmd->msg_len > 8)
		return -EINVAL;

	/* setup for DISEQC */
	val = s5h1420_readreg(state, 0x3b);
	s5h1420_writereg(state, 0x3b, 0x02);
	msleep(15);

	/* write the DISEQC command bytes */
	for(i=0; i< cmd->msg_len; i++) {
		s5h1420_writereg(state, 0x3d + i, cmd->msg[i]);
	}

	/* kick off transmission */
	s5h1420_writereg(state, 0x3b, s5h1420_readreg(state, 0x3b) |
				      ((cmd->msg_len-1) << 4) | 0x08);

	/* wait for transmission to complete */
	timeout = jiffies + ((100*HZ) / 1000);
	while(time_before(jiffies, timeout)) {
		if (!(s5h1420_readreg(state, 0x3b) & 0x08))
			break;

		msleep(5);
	}
	if (time_after(jiffies, timeout))
		result = -ETIMEDOUT;

	/* restore original settings */
	s5h1420_writereg(state, 0x3b, val);
	msleep(15);
	dprintk("leave %s\n", __func__);
	return result;
}

static int s5h1420_recv_slave_reply (struct dvb_frontend* fe,
				     struct dvb_diseqc_slave_reply* reply)
{
	struct s5h1420_state* state = fe->demodulator_priv;
	u8 val;
	int i;
	int length;
	unsigned long timeout;
	int result = 0;

	/* setup for DISEQC recieve */
	val = s5h1420_readreg(state, 0x3b);
	s5h1420_writereg(state, 0x3b, 0x82); /* FIXME: guess - do we need to set DIS_RDY(0x08) in receive mode? */
	msleep(15);

	/* wait for reception to complete */
	timeout = jiffies + ((reply->timeout*HZ) / 1000);
	while(time_before(jiffies, timeout)) {
		if (!(s5h1420_readreg(state, 0x3b) & 0x80)) /* FIXME: do we test DIS_RDY(0x08) or RCV_EN(0x80)? */
			break;

		msleep(5);
	}
	if (time_after(jiffies, timeout)) {
		result = -ETIMEDOUT;
		goto exit;
	}

	/* check error flag - FIXME: not sure what this does - docs do not describe
	 * beyond "error flag for diseqc receive data :( */
	if (s5h1420_readreg(state, 0x49)) {
		result = -EIO;
		goto exit;
	}

	/* check length */
	length = (s5h1420_readreg(state, 0x3b) & 0x70) >> 4;
	if (length > sizeof(reply->msg)) {
		result = -EOVERFLOW;
		goto exit;
	}
	reply->msg_len = length;

	/* extract data */
	for(i=0; i< length; i++) {
		reply->msg[i] = s5h1420_readreg(state, 0x3d + i);
	}

exit:
	/* restore original settings */
	s5h1420_writereg(state, 0x3b, val);
	msleep(15);
	return result;
}

static int s5h1420_send_burst (struct dvb_frontend* fe, fe_sec_mini_cmd_t minicmd)
{
	struct s5h1420_state* state = fe->demodulator_priv;
	u8 val;
	int result = 0;
	unsigned long timeout;

	/* setup for tone burst */
	val = s5h1420_readreg(state, 0x3b);
	s5h1420_writereg(state, 0x3b, (s5h1420_readreg(state, 0x3b) & 0x70) | 0x01);

	/* set value for B position if requested */
	if (minicmd == SEC_MINI_B) {
		s5h1420_writereg(state, 0x3b, s5h1420_readreg(state, 0x3b) | 0x04);
	}
	msleep(15);

	/* start transmission */
	s5h1420_writereg(state, 0x3b, s5h1420_readreg(state, 0x3b) | 0x08);

	/* wait for transmission to complete */
	timeout = jiffies + ((100*HZ) / 1000);
	while(time_before(jiffies, timeout)) {
		if (!(s5h1420_readreg(state, 0x3b) & 0x08))
			break;

		msleep(5);
	}
	if (time_after(jiffies, timeout))
		result = -ETIMEDOUT;

	/* restore original settings */
	s5h1420_writereg(state, 0x3b, val);
	msleep(15);
	return result;
}

static fe_status_t s5h1420_get_status_bits(struct s5h1420_state* state)
{
	u8 val;
	fe_status_t status = 0;

	val = s5h1420_readreg(state, 0x14);
	if (val & 0x02)
		status |=  FE_HAS_SIGNAL;
	if (val & 0x01)
		status |=  FE_HAS_CARRIER;
	val = s5h1420_readreg(state, 0x36);
	if (val & 0x01)
		status |=  FE_HAS_VITERBI;
	if (val & 0x20)
		status |=  FE_HAS_SYNC;
	if (status == (FE_HAS_SIGNAL|FE_HAS_CARRIER|FE_HAS_VITERBI|FE_HAS_SYNC))
		status |=  FE_HAS_LOCK;

	return status;
}

static int s5h1420_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct s5h1420_state* state = fe->demodulator_priv;
	u8 val;

	dprintk("enter %s\n", __func__);

	if (status == NULL)
		return -EINVAL;

	/* determine lock state */
	*status = s5h1420_get_status_bits(state);

	/* fix for FEC 5/6 inversion issue - if it doesn't quite lock, invert
	the inversion, wait a bit and check again */
	if (*status == (FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI)) {
		val = s5h1420_readreg(state, Vit10);
		if ((val & 0x07) == 0x03) {
			if (val & 0x08)
				s5h1420_writereg(state, Vit09, 0x13);
			else
				s5h1420_writereg(state, Vit09, 0x1b);

			/* wait a bit then update lock status */
			mdelay(200);
			*status = s5h1420_get_status_bits(state);
		}
	}

	/* perform post lock setup */
	if ((*status & FE_HAS_LOCK) && !state->postlocked) {

		/* calculate the data rate */
		u32 tmp = s5h1420_getsymbolrate(state);
		switch (s5h1420_readreg(state, Vit10) & 0x07) {
		case 0: tmp = (tmp * 2 * 1) / 2; break;
		case 1: tmp = (tmp * 2 * 2) / 3; break;
		case 2: tmp = (tmp * 2 * 3) / 4; break;
		case 3: tmp = (tmp * 2 * 5) / 6; break;
		case 4: tmp = (tmp * 2 * 6) / 7; break;
		case 5: tmp = (tmp * 2 * 7) / 8; break;
		}

		if (tmp == 0) {
			printk(KERN_ERR "s5h1420: avoided division by 0\n");
			tmp = 1;
		}
		tmp = state->fclk / tmp;


		/* set the MPEG_CLK_INTL for the calculated data rate */
		if (tmp < 2)
			val = 0x00;
		else if (tmp < 5)
			val = 0x01;
		else if (tmp < 9)
			val = 0x02;
		else if (tmp < 13)
			val = 0x03;
		else if (tmp < 17)
			val = 0x04;
		else if (tmp < 25)
			val = 0x05;
		else if (tmp < 33)
			val = 0x06;
		else
			val = 0x07;
		dprintk("for MPEG_CLK_INTL %d %x\n", tmp, val);

		s5h1420_writereg(state, FEC01, 0x18);
		s5h1420_writereg(state, FEC01, 0x10);
		s5h1420_writereg(state, FEC01, val);

		/* Enable "MPEG_Out" */
		val = s5h1420_readreg(state, Mpeg02);
		s5h1420_writereg(state, Mpeg02, val | (1 << 6));

		/* kicker disable */
		val = s5h1420_readreg(state, QPSK01) & 0x7f;
		s5h1420_writereg(state, QPSK01, val);

		/* DC freeze TODO it was never activated by default or it can stay activated */

		if (s5h1420_getsymbolrate(state) >= 20000000) {
			s5h1420_writereg(state, Loop04, 0x8a);
			s5h1420_writereg(state, Loop05, 0x6a);
		} else {
			s5h1420_writereg(state, Loop04, 0x58);
			s5h1420_writereg(state, Loop05, 0x27);
		}

		/* post-lock processing has been done! */
		state->postlocked = 1;
	}

	dprintk("leave %s\n", __func__);

	return 0;
}

static int s5h1420_read_ber(struct dvb_frontend* fe, u32* ber)
{
	struct s5h1420_state* state = fe->demodulator_priv;

	s5h1420_writereg(state, 0x46, 0x1d);
	mdelay(25);

	*ber = (s5h1420_readreg(state, 0x48) << 8) | s5h1420_readreg(state, 0x47);

	return 0;
}

static int s5h1420_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	struct s5h1420_state* state = fe->demodulator_priv;

	u8 val = s5h1420_readreg(state, 0x15);

	*strength =  (u16) ((val << 8) | val);

	return 0;
}

static int s5h1420_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct s5h1420_state* state = fe->demodulator_priv;

	s5h1420_writereg(state, 0x46, 0x1f);
	mdelay(25);

	*ucblocks = (s5h1420_readreg(state, 0x48) << 8) | s5h1420_readreg(state, 0x47);

	return 0;
}

static void s5h1420_reset(struct s5h1420_state* state)
{
	dprintk("%s\n", __func__);
	s5h1420_writereg (state, 0x01, 0x08);
	s5h1420_writereg (state, 0x01, 0x00);
	udelay(10);
}

static void s5h1420_setsymbolrate(struct s5h1420_state* state,
				  struct dvb_frontend_parameters *p)
{
	u8 v;
	u64 val;

	dprintk("enter %s\n", __func__);

	val = ((u64) p->u.qpsk.symbol_rate / 1000ULL) * (1ULL<<24);
	if (p->u.qpsk.symbol_rate < 29000000)
		val *= 2;
	do_div(val, (state->fclk / 1000));

	dprintk("symbol rate register: %06llx\n", (unsigned long long)val);

	v = s5h1420_readreg(state, Loop01);
	s5h1420_writereg(state, Loop01, v & 0x7f);
	s5h1420_writereg(state, Tnco01, val >> 16);
	s5h1420_writereg(state, Tnco02, val >> 8);
	s5h1420_writereg(state, Tnco03, val & 0xff);
	s5h1420_writereg(state, Loop01,  v | 0x80);
	dprintk("leave %s\n", __func__);
}

static u32 s5h1420_getsymbolrate(struct s5h1420_state* state)
{
	return state->symbol_rate;
}

static void s5h1420_setfreqoffset(struct s5h1420_state* state, int freqoffset)
{
	int val;
	u8 v;

	dprintk("enter %s\n", __func__);

	/* remember freqoffset is in kHz, but the chip wants the offset in Hz, so
	 * divide fclk by 1000000 to get the correct value. */
	val = -(int) ((freqoffset * (1<<24)) / (state->fclk / 1000000));

	dprintk("phase rotator/freqoffset: %d %06x\n", freqoffset, val);

	v = s5h1420_readreg(state, Loop01);
	s5h1420_writereg(state, Loop01, v & 0xbf);
	s5h1420_writereg(state, Pnco01, val >> 16);
	s5h1420_writereg(state, Pnco02, val >> 8);
	s5h1420_writereg(state, Pnco03, val & 0xff);
	s5h1420_writereg(state, Loop01, v | 0x40);
	dprintk("leave %s\n", __func__);
}

static int s5h1420_getfreqoffset(struct s5h1420_state* state)
{
	int val;

	s5h1420_writereg(state, 0x06, s5h1420_readreg(state, 0x06) | 0x08);
	val  = s5h1420_readreg(state, 0x0e) << 16;
	val |= s5h1420_readreg(state, 0x0f) << 8;
	val |= s5h1420_readreg(state, 0x10);
	s5h1420_writereg(state, 0x06, s5h1420_readreg(state, 0x06) & 0xf7);

	if (val & 0x800000)
		val |= 0xff000000;

	/* remember freqoffset is in kHz, but the chip wants the offset in Hz, so
	 * divide fclk by 1000000 to get the correct value. */
	val = (((-val) * (state->fclk/1000000)) / (1<<24));

	return val;
}

static void s5h1420_setfec_inversion(struct s5h1420_state* state,
				     struct dvb_frontend_parameters *p)
{
	u8 inversion = 0;
	u8 vit08, vit09;

	dprintk("enter %s\n", __func__);

	if (p->inversion == INVERSION_OFF)
		inversion = state->config->invert ? 0x08 : 0;
	else if (p->inversion == INVERSION_ON)
		inversion = state->config->invert ? 0 : 0x08;

	if ((p->u.qpsk.fec_inner == FEC_AUTO) || (p->inversion == INVERSION_AUTO)) {
		vit08 = 0x3f;
		vit09 = 0;
	} else {
		switch(p->u.qpsk.fec_inner) {
		case FEC_1_2:
			vit08 = 0x01; vit09 = 0x10;
			break;

		case FEC_2_3:
			vit08 = 0x02; vit09 = 0x11;
			break;

		case FEC_3_4:
			vit08 = 0x04; vit09 = 0x12;
			break;

		case FEC_5_6:
			vit08 = 0x08; vit09 = 0x13;
			break;

		case FEC_6_7:
			vit08 = 0x10; vit09 = 0x14;
			break;

		case FEC_7_8:
			vit08 = 0x20; vit09 = 0x15;
			break;

		default:
			return;
		}
	}
	vit09 |= inversion;
	dprintk("fec: %02x %02x\n", vit08, vit09);
	s5h1420_writereg(state, Vit08, vit08);
	s5h1420_writereg(state, Vit09, vit09);
	dprintk("leave %s\n", __func__);
}

static fe_code_rate_t s5h1420_getfec(struct s5h1420_state* state)
{
	switch(s5h1420_readreg(state, 0x32) & 0x07) {
	case 0:
		return FEC_1_2;

	case 1:
		return FEC_2_3;

	case 2:
		return FEC_3_4;

	case 3:
		return FEC_5_6;

	case 4:
		return FEC_6_7;

	case 5:
		return FEC_7_8;
	}

	return FEC_NONE;
}

static fe_spectral_inversion_t s5h1420_getinversion(struct s5h1420_state* state)
{
	if (s5h1420_readreg(state, 0x32) & 0x08)
		return INVERSION_ON;

	return INVERSION_OFF;
}

static int s5h1420_set_frontend(struct dvb_frontend* fe,
				struct dvb_frontend_parameters *p)
{
	struct s5h1420_state* state = fe->demodulator_priv;
	int frequency_delta;
	struct dvb_frontend_tune_settings fesettings;
	uint8_t clock_settting;

	dprintk("enter %s\n", __func__);

	/* check if we should do a fast-tune */
	memcpy(&fesettings.parameters, p, sizeof(struct dvb_frontend_parameters));
	s5h1420_get_tune_settings(fe, &fesettings);
	frequency_delta = p->frequency - state->tunedfreq;
	if ((frequency_delta > -fesettings.max_drift) &&
			(frequency_delta < fesettings.max_drift) &&
			(frequency_delta != 0) &&
			(state->fec_inner == p->u.qpsk.fec_inner) &&
			(state->symbol_rate == p->u.qpsk.symbol_rate)) {

		if (fe->ops.tuner_ops.set_params) {
			fe->ops.tuner_ops.set_params(fe, p);
			if (fe->ops.i2c_gate_ctrl) fe->ops.i2c_gate_ctrl(fe, 0);
		}
		if (fe->ops.tuner_ops.get_frequency) {
			u32 tmp;
			fe->ops.tuner_ops.get_frequency(fe, &tmp);
			if (fe->ops.i2c_gate_ctrl) fe->ops.i2c_gate_ctrl(fe, 0);
			s5h1420_setfreqoffset(state, p->frequency - tmp);
		} else {
			s5h1420_setfreqoffset(state, 0);
		}
		dprintk("simple tune\n");
		return 0;
	}
	dprintk("tuning demod\n");

	/* first of all, software reset */
	s5h1420_reset(state);

	/* set s5h1420 fclk PLL according to desired symbol rate */
	if (p->u.qpsk.symbol_rate > 33000000)
		state->fclk = 80000000;
	else if (p->u.qpsk.symbol_rate > 28500000)
		state->fclk = 59000000;
	else if (p->u.qpsk.symbol_rate > 25000000)
		state->fclk = 86000000;
	else if (p->u.qpsk.symbol_rate > 1900000)
		state->fclk = 88000000;
	else
		state->fclk = 44000000;

	/* Clock */
	switch (state->fclk) {
	default:
	case 88000000:
		clock_settting = 80;
		break;
	case 86000000:
		clock_settting = 78;
		break;
	case 80000000:
		clock_settting = 72;
		break;
	case 59000000:
		clock_settting = 51;
		break;
	case 44000000:
		clock_settting = 36;
		break;
	}
	dprintk("pll01: %d, ToneFreq: %d\n", state->fclk/1000000 - 8, (state->fclk + (TONE_FREQ * 32) - 1) / (TONE_FREQ * 32));
	s5h1420_writereg(state, PLL01, state->fclk/1000000 - 8);
	s5h1420_writereg(state, PLL02, 0x40);
	s5h1420_writereg(state, DiS01, (state->fclk + (TONE_FREQ * 32) - 1) / (TONE_FREQ * 32));

	/* TODO DC offset removal, config parameter ? */
	if (p->u.qpsk.symbol_rate > 29000000)
		s5h1420_writereg(state, QPSK01, 0xae | 0x10);
	else
		s5h1420_writereg(state, QPSK01, 0xac | 0x10);

	/* set misc registers */
	s5h1420_writereg(state, CON_1, 0x00);
	s5h1420_writereg(state, QPSK02, 0x00);
	s5h1420_writereg(state, Pre01, 0xb0);

	s5h1420_writereg(state, Loop01, 0xF0);
	s5h1420_writereg(state, Loop02, 0x2a); /* e7 for s5h1420 */
	s5h1420_writereg(state, Loop03, 0x79); /* 78 for s5h1420 */
	if (p->u.qpsk.symbol_rate > 20000000)
		s5h1420_writereg(state, Loop04, 0x79);
	else
		s5h1420_writereg(state, Loop04, 0x58);
	s5h1420_writereg(state, Loop05, 0x6b);

	if (p->u.qpsk.symbol_rate >= 8000000)
		s5h1420_writereg(state, Post01, (0 << 6) | 0x10);
	else if (p->u.qpsk.symbol_rate >= 4000000)
		s5h1420_writereg(state, Post01, (1 << 6) | 0x10);
	else
		s5h1420_writereg(state, Post01, (3 << 6) | 0x10);

	s5h1420_writereg(state, Monitor12, 0x00); /* unfreeze DC compensation */

	s5h1420_writereg(state, Sync01, 0x33);
	s5h1420_writereg(state, Mpeg01, state->config->cdclk_polarity);
	s5h1420_writereg(state, Mpeg02, 0x3d); /* Parallel output more, disabled -> enabled later */
	s5h1420_writereg(state, Err01, 0x03); /* 0x1d for s5h1420 */

	s5h1420_writereg(state, Vit06, 0x6e); /* 0x8e for s5h1420 */
	s5h1420_writereg(state, DiS03, 0x00);
	s5h1420_writereg(state, Rf01, 0x61); /* Tuner i2c address - for the gate controller */

	/* set tuner PLL */
	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe, p);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
		s5h1420_setfreqoffset(state, 0);
	}

	/* set the reset of the parameters */
	s5h1420_setsymbolrate(state, p);
	s5h1420_setfec_inversion(state, p);

	/* start QPSK */
	s5h1420_writereg(state, QPSK01, s5h1420_readreg(state, QPSK01) | 1);

	state->fec_inner = p->u.qpsk.fec_inner;
	state->symbol_rate = p->u.qpsk.symbol_rate;
	state->postlocked = 0;
	state->tunedfreq = p->frequency;

	dprintk("leave %s\n", __func__);
	return 0;
}

static int s5h1420_get_frontend(struct dvb_frontend* fe,
				struct dvb_frontend_parameters *p)
{
	struct s5h1420_state* state = fe->demodulator_priv;

	p->frequency = state->tunedfreq + s5h1420_getfreqoffset(state);
	p->inversion = s5h1420_getinversion(state);
	p->u.qpsk.symbol_rate = s5h1420_getsymbolrate(state);
	p->u.qpsk.fec_inner = s5h1420_getfec(state);

	return 0;
}

static int s5h1420_get_tune_settings(struct dvb_frontend* fe,
				     struct dvb_frontend_tune_settings* fesettings)
{
	if (fesettings->parameters.u.qpsk.symbol_rate > 20000000) {
		fesettings->min_delay_ms = 50;
		fesettings->step_size = 2000;
		fesettings->max_drift = 8000;
	} else if (fesettings->parameters.u.qpsk.symbol_rate > 12000000) {
		fesettings->min_delay_ms = 100;
		fesettings->step_size = 1500;
		fesettings->max_drift = 9000;
	} else if (fesettings->parameters.u.qpsk.symbol_rate > 8000000) {
		fesettings->min_delay_ms = 100;
		fesettings->step_size = 1000;
		fesettings->max_drift = 8000;
	} else if (fesettings->parameters.u.qpsk.symbol_rate > 4000000) {
		fesettings->min_delay_ms = 100;
		fesettings->step_size = 500;
		fesettings->max_drift = 7000;
	} else if (fesettings->parameters.u.qpsk.symbol_rate > 2000000) {
		fesettings->min_delay_ms = 200;
		fesettings->step_size = (fesettings->parameters.u.qpsk.symbol_rate / 8000);
		fesettings->max_drift = 14 * fesettings->step_size;
	} else {
		fesettings->min_delay_ms = 200;
		fesettings->step_size = (fesettings->parameters.u.qpsk.symbol_rate / 8000);
		fesettings->max_drift = 18 * fesettings->step_size;
	}

	return 0;
}

static int s5h1420_i2c_gate_ctrl(struct dvb_frontend* fe, int enable)
{
	struct s5h1420_state* state = fe->demodulator_priv;

	if (enable)
		return s5h1420_writereg(state, 0x02, state->CON_1_val | 1);
	else
		return s5h1420_writereg(state, 0x02, state->CON_1_val & 0xfe);
}

static int s5h1420_init (struct dvb_frontend* fe)
{
	struct s5h1420_state* state = fe->demodulator_priv;

	/* disable power down and do reset */
	state->CON_1_val = state->config->serial_mpeg << 4;
	s5h1420_writereg(state, 0x02, state->CON_1_val);
	msleep(10);
	s5h1420_reset(state);

	return 0;
}

static int s5h1420_sleep(struct dvb_frontend* fe)
{
	struct s5h1420_state* state = fe->demodulator_priv;
	state->CON_1_val = 0x12;
	return s5h1420_writereg(state, 0x02, state->CON_1_val);
}

static void s5h1420_release(struct dvb_frontend* fe)
{
	struct s5h1420_state* state = fe->demodulator_priv;
	i2c_del_adapter(&state->tuner_i2c_adapter);
	kfree(state);
}

static u32 s5h1420_tuner_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static int s5h1420_tuner_i2c_tuner_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg msg[], int num)
{
	struct s5h1420_state *state = i2c_get_adapdata(i2c_adap);
	struct i2c_msg m[1 + num];
	u8 tx_open[2] = { CON_1, state->CON_1_val | 1 }; /* repeater stops once there was a stop condition */

	memset(m, 0, sizeof(struct i2c_msg) * (1 + num));

	m[0].addr = state->config->demod_address;
	m[0].buf  = tx_open;
	m[0].len  = 2;

	memcpy(&m[1], msg, sizeof(struct i2c_msg) * num);

	return i2c_transfer(state->i2c, m, 1+num) == 1 + num ? num : -EIO;
}

static struct i2c_algorithm s5h1420_tuner_i2c_algo = {
	.master_xfer   = s5h1420_tuner_i2c_tuner_xfer,
	.functionality = s5h1420_tuner_i2c_func,
};

struct i2c_adapter *s5h1420_get_tuner_i2c_adapter(struct dvb_frontend *fe)
{
	struct s5h1420_state *state = fe->demodulator_priv;
	return &state->tuner_i2c_adapter;
}
EXPORT_SYMBOL(s5h1420_get_tuner_i2c_adapter);

static struct dvb_frontend_ops s5h1420_ops;

struct dvb_frontend *s5h1420_attach(const struct s5h1420_config *config,
				    struct i2c_adapter *i2c)
{
	/* allocate memory for the internal state */
	struct s5h1420_state *state = kzalloc(sizeof(struct s5h1420_state), GFP_KERNEL);
	u8 i;

	if (state == NULL)
		goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	state->postlocked = 0;
	state->fclk = 88000000;
	state->tunedfreq = 0;
	state->fec_inner = FEC_NONE;
	state->symbol_rate = 0;

	/* check if the demod is there + identify it */
	i = s5h1420_readreg(state, ID01);
	if (i != 0x03)
		goto error;

	memset(state->shadow, 0xff, sizeof(state->shadow));

	for (i = 0; i < 0x50; i++)
		state->shadow[i] = s5h1420_readreg(state, i);

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &s5h1420_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;

	/* create tuner i2c adapter */
	strlcpy(state->tuner_i2c_adapter.name, "S5H1420-PN1010 tuner I2C bus",
		sizeof(state->tuner_i2c_adapter.name));
	state->tuner_i2c_adapter.algo      = &s5h1420_tuner_i2c_algo;
	state->tuner_i2c_adapter.algo_data = NULL;
	i2c_set_adapdata(&state->tuner_i2c_adapter, state);
	if (i2c_add_adapter(&state->tuner_i2c_adapter) < 0) {
		printk(KERN_ERR "S5H1420/PN1010: tuner i2c bus could not be initialized\n");
		goto error;
	}

	return &state->frontend;

error:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(s5h1420_attach);

static struct dvb_frontend_ops s5h1420_ops = {

	.info = {
		.name     = "Samsung S5H1420/PnpNetwork PN1010 DVB-S",
		.type     = FE_QPSK,
		.frequency_min    = 950000,
		.frequency_max    = 2150000,
		.frequency_stepsize = 125,     /* kHz for QPSK frontends */
		.frequency_tolerance  = 29500,
		.symbol_rate_min  = 1000000,
		.symbol_rate_max  = 45000000,
		/*  .symbol_rate_tolerance  = ???,*/
		.caps = FE_CAN_INVERSION_AUTO |
		FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
		FE_CAN_QPSK
	},

	.release = s5h1420_release,

	.init = s5h1420_init,
	.sleep = s5h1420_sleep,
	.i2c_gate_ctrl = s5h1420_i2c_gate_ctrl,

	.set_frontend = s5h1420_set_frontend,
	.get_frontend = s5h1420_get_frontend,
	.get_tune_settings = s5h1420_get_tune_settings,

	.read_status = s5h1420_read_status,
	.read_ber = s5h1420_read_ber,
	.read_signal_strength = s5h1420_read_signal_strength,
	.read_ucblocks = s5h1420_read_ucblocks,

	.diseqc_send_master_cmd = s5h1420_send_master_cmd,
	.diseqc_recv_slave_reply = s5h1420_recv_slave_reply,
	.diseqc_send_burst = s5h1420_send_burst,
	.set_tone = s5h1420_set_tone,
	.set_voltage = s5h1420_set_voltage,
};

MODULE_DESCRIPTION("Samsung S5H1420/PnpNetwork PN1010 DVB-S Demodulator driver");
MODULE_AUTHOR("Andrew de Quincey, Patrick Boettcher");
MODULE_LICENSE("GPL");
