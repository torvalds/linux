/*
 * tda80xx.c
 *
 * Philips TDA8044 / TDA8083 QPSK demodulator driver
 *
 * Copyright (C) 2001 Felix Domke <tmbinc@elitedvb.net>
 * Copyright (C) 2002-2004 Andreas Oberritter <obi@linuxtv.org>
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
 */

#include <linux/config.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/irq.h>
#include <asm/div64.h>

#include "dvb_frontend.h"
#include "tda80xx.h"

enum {
	ID_TDA8044 = 0x04,
	ID_TDA8083 = 0x05,
};


struct tda80xx_state {

	struct i2c_adapter* i2c;

	struct dvb_frontend_ops ops;

	/* configuration settings */
	const struct tda80xx_config* config;

	struct dvb_frontend frontend;

	u32 clk;
	int afc_loop;
	struct work_struct worklet;
	fe_code_rate_t code_rate;
	fe_spectral_inversion_t spectral_inversion;
	fe_status_t status;
	u8 id;
};

static int debug = 1;
#define dprintk	if (debug) printk

static u8 tda8044_inittab_pre[] = {
	0x02, 0x00, 0x6f, 0xb5, 0x86, 0x22, 0x00, 0xea,
	0x30, 0x42, 0x98, 0x68, 0x70, 0x42, 0x99, 0x58,
	0x95, 0x10, 0xf5, 0xe7, 0x93, 0x0b, 0x15, 0x68,
	0x9a, 0x90, 0x61, 0x80, 0x00, 0xe0, 0x40, 0x00,
	0x0f, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00
};

static u8 tda8044_inittab_post[] = {
	0x04, 0x00, 0x6f, 0xb5, 0x86, 0x22, 0x00, 0xea,
	0x30, 0x42, 0x98, 0x68, 0x70, 0x42, 0x99, 0x50,
	0x95, 0x10, 0xf5, 0xe7, 0x93, 0x0b, 0x15, 0x68,
	0x9a, 0x90, 0x61, 0x80, 0x00, 0xe0, 0x40, 0x6c,
	0x0f, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00
};

static u8 tda8083_inittab[] = {
	0x04, 0x00, 0x4a, 0x79, 0x04, 0x00, 0xff, 0xea,
	0x48, 0x42, 0x79, 0x60, 0x70, 0x52, 0x9a, 0x10,
	0x0e, 0x10, 0xf2, 0xa7, 0x93, 0x0b, 0x05, 0xc8,
	0x9d, 0x00, 0x42, 0x80, 0x00, 0x60, 0x40, 0x00,
	0x00, 0x75, 0x00, 0xe0, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00
};

static __inline__ u32 tda80xx_div(u32 a, u32 b)
{
	return (a + (b / 2)) / b;
}

static __inline__ u32 tda80xx_gcd(u32 a, u32 b)
{
	u32 r;

	while ((r = a % b)) {
		a = b;
		b = r;
	}

	return b;
}

static int tda80xx_read(struct tda80xx_state* state, u8 reg, u8 *buf, u8 len)
{
	int ret;
	struct i2c_msg msg[] = { { .addr = state->config->demod_address, .flags = 0, .buf = &reg, .len = 1 },
			  { .addr = state->config->demod_address, .flags = I2C_M_RD, .buf = buf, .len = len } };

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2)
		dprintk("%s: readreg error (reg %02x, ret == %i)\n",
				__FUNCTION__, reg, ret);

	mdelay(10);

	return (ret == 2) ? 0 : -EREMOTEIO;
}

static int tda80xx_write(struct tda80xx_state* state, u8 reg, const u8 *buf, u8 len)
{
	int ret;
	u8 wbuf[len + 1];
	struct i2c_msg msg = { .addr = state->config->demod_address, .flags = 0, .buf = wbuf, .len = len + 1 };

	wbuf[0] = reg;
	memcpy(&wbuf[1], buf, len);

	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1)
		dprintk("%s: i2c xfer error (ret == %i)\n", __FUNCTION__, ret);

	mdelay(10);

	return (ret == 1) ? 0 : -EREMOTEIO;
}

static __inline__ u8 tda80xx_readreg(struct tda80xx_state* state, u8 reg)
{
	u8 val;

	tda80xx_read(state, reg, &val, 1);

	return val;
}

static __inline__ int tda80xx_writereg(struct tda80xx_state* state, u8 reg, u8 data)
{
	return tda80xx_write(state, reg, &data, 1);
}

static int tda80xx_set_parameters(struct tda80xx_state* state,
				  fe_spectral_inversion_t inversion,
				  u32 symbol_rate,
				  fe_code_rate_t fec_inner)
{
	u8 buf[15];
	u64 ratio;
	u32 clk;
	u32 k;
	u32 sr = symbol_rate;
	u32 gcd;
	u8 scd;

	if (symbol_rate > (state->clk * 3) / 16)
		scd = 0;
	else if (symbol_rate > (state->clk * 3) / 32)
		scd = 1;
	else if (symbol_rate > (state->clk * 3) / 64)
		scd = 2;
	else
		scd = 3;

	clk = scd ? (state->clk / (scd * 2)) : state->clk;

	/*
	 * Viterbi decoder:
	 * Differential decoding off
	 * Spectral inversion unknown
	 * QPSK modulation
	 */
	if (inversion == INVERSION_ON)
		buf[0] = 0x60;
	else if (inversion == INVERSION_OFF)
		buf[0] = 0x20;
	else
		buf[0] = 0x00;

	/*
	 * CLK ratio:
	 * system clock frequency is up to 64 or 96 MHz
	 *
	 * formula:
	 * r = k * clk / symbol_rate
	 *
	 * k:	2^21 for caa 0..3,
	 *	2^20 for caa 4..5,
	 *	2^19 for caa 6..7
	 */
	if (symbol_rate <= (clk * 3) / 32)
		k = (1 << 19);
	else if (symbol_rate <= (clk * 3) / 16)
		k = (1 << 20);
	else
		k = (1 << 21);

	gcd = tda80xx_gcd(clk, sr);
	clk /= gcd;
	sr /= gcd;

	gcd = tda80xx_gcd(k, sr);
	k /= gcd;
	sr /= gcd;

	ratio = (u64)k * (u64)clk;
	do_div(ratio, sr);

	buf[1] = ratio >> 16;
	buf[2] = ratio >> 8;
	buf[3] = ratio;

	/* nyquist filter roll-off factor 35% */
	buf[4] = 0x20;

	clk = scd ? (state->clk / (scd * 2)) : state->clk;

	/* Anti Alias Filter */
	if (symbol_rate < (clk * 3) / 64)
		printk("tda80xx: unsupported symbol rate: %u\n", symbol_rate);
	else if (symbol_rate <= clk / 16)
		buf[4] |= 0x07;
	else if (symbol_rate <= (clk * 3) / 32)
		buf[4] |= 0x06;
	else if (symbol_rate <= clk / 8)
		buf[4] |= 0x05;
	else if (symbol_rate <= (clk * 3) / 16)
		buf[4] |= 0x04;
	else if (symbol_rate <= clk / 4)
		buf[4] |= 0x03;
	else if (symbol_rate <= (clk * 3) / 8)
		buf[4] |= 0x02;
	else if (symbol_rate <= clk / 2)
		buf[4] |= 0x01;
	else
		buf[4] |= 0x00;

	/* Sigma Delta converter */
	buf[5] = 0x00;

	/* FEC: Possible puncturing rates */
	if (fec_inner == FEC_NONE)
		buf[6] = 0x00;
	else if ((fec_inner >= FEC_1_2) && (fec_inner <= FEC_8_9))
		buf[6] = (1 << (8 - fec_inner));
	else if (fec_inner == FEC_AUTO)
		buf[6] = 0xff;
	else
		return -EINVAL;

	/* carrier lock detector threshold value */
	buf[7] = 0x30;
	/* AFC1: proportional part settings */
	buf[8] = 0x42;
	/* AFC1: integral part settings */
	buf[9] = 0x98;
	/* PD: Leaky integrator SCPC mode */
	buf[10] = 0x28;
	/* AFC2, AFC1 controls */
	buf[11] = 0x30;
	/* PD: proportional part settings */
	buf[12] = 0x42;
	/* PD: integral part settings */
	buf[13] = 0x99;
	/* AGC */
	buf[14] = 0x50 | scd;

	printk("symbol_rate=%u clk=%u\n", symbol_rate, clk);

	return tda80xx_write(state, 0x01, buf, sizeof(buf));
}

static int tda80xx_set_clk(struct tda80xx_state* state)
{
	u8 buf[2];

	/* CLK proportional part */
	buf[0] = (0x06 << 5) | 0x08;	/* CMP[2:0], CSP[4:0] */
	/* CLK integral part */
	buf[1] = (0x04 << 5) | 0x1a;	/* CMI[2:0], CSI[4:0] */

	return tda80xx_write(state, 0x17, buf, sizeof(buf));
}

#if 0
static int tda80xx_set_scpc_freq_offset(struct tda80xx_state* state)
{
	/* a constant value is nonsense here imho */
	return tda80xx_writereg(state, 0x22, 0xf9);
}
#endif

static int tda80xx_close_loop(struct tda80xx_state* state)
{
	u8 buf[2];

	/* PD: Loop closed, LD: lock detect enable, SCPC: Sweep mode - AFC1 loop closed */
	buf[0] = 0x68;
	/* AFC1: Loop closed, CAR Feedback: 8192 */
	buf[1] = 0x70;

	return tda80xx_write(state, 0x0b, buf, sizeof(buf));
}

static irqreturn_t tda80xx_irq(int irq, void *priv, struct pt_regs *pt)
{
	schedule_work(priv);

	return IRQ_HANDLED;
}

static void tda80xx_read_status_int(struct tda80xx_state* state)
{
	u8 val;

	static const fe_spectral_inversion_t inv_tab[] = {
		INVERSION_OFF, INVERSION_ON
	};

	static const fe_code_rate_t fec_tab[] = {
		FEC_8_9, FEC_1_2, FEC_2_3, FEC_3_4,
		FEC_4_5, FEC_5_6, FEC_6_7, FEC_7_8,
	};

	val = tda80xx_readreg(state, 0x02);

	state->status = 0;

	if (val & 0x01) /* demodulator lock */
		state->status |= FE_HAS_SIGNAL;
	if (val & 0x02) /* clock recovery lock */
		state->status |= FE_HAS_CARRIER;
	if (val & 0x04) /* viterbi lock */
		state->status |= FE_HAS_VITERBI;
	if (val & 0x08) /* deinterleaver lock (packet sync) */
		state->status |= FE_HAS_SYNC;
	if (val & 0x10) /* derandomizer lock (frame sync) */
		state->status |= FE_HAS_LOCK;
	if (val & 0x20) /* frontend can not lock */
		state->status |= FE_TIMEDOUT;

	if ((state->status & (FE_HAS_CARRIER)) && (state->afc_loop)) {
		printk("tda80xx: closing loop\n");
		tda80xx_close_loop(state);
		state->afc_loop = 0;
	}

	if (state->status & (FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK)) {
		val = tda80xx_readreg(state, 0x0e);
		state->code_rate = fec_tab[val & 0x07];
		if (state->status & (FE_HAS_SYNC | FE_HAS_LOCK))
			state->spectral_inversion = inv_tab[(val >> 7) & 0x01];
		else
			state->spectral_inversion = INVERSION_AUTO;
	}
	else {
		state->code_rate = FEC_AUTO;
	}
}

static void tda80xx_worklet(void *priv)
{
	struct tda80xx_state *state = priv;

	tda80xx_writereg(state, 0x00, 0x04);
	enable_irq(state->config->irq);

	tda80xx_read_status_int(state);
}

static void tda80xx_wait_diseqc_fifo(struct tda80xx_state* state)
{
	size_t i;

	for (i = 0; i < 100; i++) {
		if (tda80xx_readreg(state, 0x02) & 0x80)
			break;
		msleep(10);
	}
}

static int tda8044_init(struct dvb_frontend* fe)
{
	struct tda80xx_state* state = fe->demodulator_priv;
	int ret;

	/*
	 * this function is a mess...
	 */

	if ((ret = tda80xx_write(state, 0x00, tda8044_inittab_pre, sizeof(tda8044_inittab_pre))))
		return ret;

	tda80xx_writereg(state, 0x0f, 0x50);
#if 1
	tda80xx_writereg(state, 0x20, 0x8F);		/* FIXME */
	tda80xx_writereg(state, 0x20, state->config->volt18setting);	/* FIXME */
	//tda80xx_writereg(state, 0x00, 0x04);
	tda80xx_writereg(state, 0x00, 0x0C);
#endif
	//tda80xx_writereg(state, 0x00, 0x08); /* Reset AFC1 loop filter */

	tda80xx_write(state, 0x00, tda8044_inittab_post, sizeof(tda8044_inittab_post));

	if (state->config->pll_init) {
		tda80xx_writereg(state, 0x1c, 0x80);
		state->config->pll_init(fe);
		tda80xx_writereg(state, 0x1c, 0x00);
	}

	return 0;
}

static int tda8083_init(struct dvb_frontend* fe)
{
	struct tda80xx_state* state = fe->demodulator_priv;

	tda80xx_write(state, 0x00, tda8083_inittab, sizeof(tda8083_inittab));

	if (state->config->pll_init) {
		tda80xx_writereg(state, 0x1c, 0x80);
		state->config->pll_init(fe);
		tda80xx_writereg(state, 0x1c, 0x00);
	}

	return 0;
}

static int tda80xx_set_voltage(struct dvb_frontend* fe, fe_sec_voltage_t voltage)
{
	struct tda80xx_state* state = fe->demodulator_priv;

	switch (voltage) {
	case SEC_VOLTAGE_13:
		return tda80xx_writereg(state, 0x20, state->config->volt13setting);
	case SEC_VOLTAGE_18:
		return tda80xx_writereg(state, 0x20, state->config->volt18setting);
	case SEC_VOLTAGE_OFF:
		return tda80xx_writereg(state, 0x20, 0);
	default:
		return -EINVAL;
	}
}

static int tda80xx_set_tone(struct dvb_frontend* fe, fe_sec_tone_mode_t tone)
{
	struct tda80xx_state* state = fe->demodulator_priv;

	switch (tone) {
	case SEC_TONE_OFF:
		return tda80xx_writereg(state, 0x29, 0x00);
	case SEC_TONE_ON:
		return tda80xx_writereg(state, 0x29, 0x80);
	default:
		return -EINVAL;
	}
}

static int tda80xx_send_diseqc_msg(struct dvb_frontend* fe, struct dvb_diseqc_master_cmd *cmd)
{
	struct tda80xx_state* state = fe->demodulator_priv;

	if (cmd->msg_len > 6)
		return -EINVAL;

	tda80xx_writereg(state, 0x29, 0x08 | (cmd->msg_len - 3));
	tda80xx_write(state, 0x23, cmd->msg, cmd->msg_len);
	tda80xx_writereg(state, 0x29, 0x0c | (cmd->msg_len - 3));
	tda80xx_wait_diseqc_fifo(state);

	return 0;
}

static int tda80xx_send_diseqc_burst(struct dvb_frontend* fe, fe_sec_mini_cmd_t cmd)
{
	struct tda80xx_state* state = fe->demodulator_priv;

	switch (cmd) {
	case SEC_MINI_A:
		tda80xx_writereg(state, 0x29, 0x14);
		break;
	case SEC_MINI_B:
		tda80xx_writereg(state, 0x29, 0x1c);
		break;
	default:
		return -EINVAL;
	}

	tda80xx_wait_diseqc_fifo(state);

	return 0;
}

static int tda80xx_sleep(struct dvb_frontend* fe)
{
	struct tda80xx_state* state = fe->demodulator_priv;

	tda80xx_writereg(state, 0x00, 0x02);	/* enter standby */

	return 0;
}

static int tda80xx_set_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters *p)
{
	struct tda80xx_state* state = fe->demodulator_priv;

	tda80xx_writereg(state, 0x1c, 0x80);
	state->config->pll_set(fe, p);
	tda80xx_writereg(state, 0x1c, 0x00);

	tda80xx_set_parameters(state, p->inversion, p->u.qpsk.symbol_rate, p->u.qpsk.fec_inner);
	tda80xx_set_clk(state);
	//tda80xx_set_scpc_freq_offset(state);
	state->afc_loop = 1;

	return 0;
}

static int tda80xx_get_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters *p)
{
	struct tda80xx_state* state = fe->demodulator_priv;

	if (!state->config->irq)
		tda80xx_read_status_int(state);

	p->inversion = state->spectral_inversion;
	p->u.qpsk.fec_inner = state->code_rate;

	return 0;
}

static int tda80xx_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct tda80xx_state* state = fe->demodulator_priv;

	if (!state->config->irq)
		tda80xx_read_status_int(state);
	*status = state->status;

	return 0;
}

static int tda80xx_read_ber(struct dvb_frontend* fe, u32* ber)
{
	struct tda80xx_state* state = fe->demodulator_priv;
	int ret;
	u8 buf[3];

	if ((ret = tda80xx_read(state, 0x0b, buf, sizeof(buf))))
		return ret;

	*ber = ((buf[0] & 0x1f) << 16) | (buf[1] << 8) | buf[2];

	return 0;
}

static int tda80xx_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	struct tda80xx_state* state = fe->demodulator_priv;

	u8 gain = ~tda80xx_readreg(state, 0x01);
	*strength = (gain << 8) | gain;

	return 0;
}

static int tda80xx_read_snr(struct dvb_frontend* fe, u16* snr)
{
	struct tda80xx_state* state = fe->demodulator_priv;

	u8 quality = tda80xx_readreg(state, 0x08);
	*snr = (quality << 8) | quality;

	return 0;
}

static int tda80xx_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct tda80xx_state* state = fe->demodulator_priv;

	*ucblocks = tda80xx_readreg(state, 0x0f);
	if (*ucblocks == 0xff)
		*ucblocks = 0xffffffff;

	return 0;
}

static int tda80xx_init(struct dvb_frontend* fe)
{
	struct tda80xx_state* state = fe->demodulator_priv;

	switch(state->id) {
	case ID_TDA8044:
		return tda8044_init(fe);

	case ID_TDA8083:
		return tda8083_init(fe);
	}
	return 0;
}

static void tda80xx_release(struct dvb_frontend* fe)
{
	struct tda80xx_state* state = fe->demodulator_priv;

	if (state->config->irq)
		free_irq(state->config->irq, &state->worklet);

	kfree(state);
}

static struct dvb_frontend_ops tda80xx_ops;

struct dvb_frontend* tda80xx_attach(const struct tda80xx_config* config,
				    struct i2c_adapter* i2c)
{
	struct tda80xx_state* state = NULL;
	int ret;

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct tda80xx_state), GFP_KERNEL);
	if (state == NULL) goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	memcpy(&state->ops, &tda80xx_ops, sizeof(struct dvb_frontend_ops));
	state->spectral_inversion = INVERSION_AUTO;
	state->code_rate = FEC_AUTO;
	state->status = 0;
	state->afc_loop = 0;

	/* check if the demod is there */
	if (tda80xx_writereg(state, 0x89, 0x00) < 0) goto error;
	state->id = tda80xx_readreg(state, 0x00);

	switch (state->id) {
	case ID_TDA8044:
		state->clk = 96000000;
		printk("tda80xx: Detected tda8044\n");
		break;

	case ID_TDA8083:
		state->clk = 64000000;
		printk("tda80xx: Detected tda8083\n");
		break;

	default:
		goto error;
	}

	/* setup IRQ */
	if (state->config->irq) {
		INIT_WORK(&state->worklet, tda80xx_worklet, state);
		if ((ret = request_irq(state->config->irq, tda80xx_irq, SA_ONESHOT, "tda80xx", &state->worklet)) < 0) {
			printk(KERN_ERR "tda80xx: request_irq failed (%d)\n", ret);
			goto error;
		}
	}

	/* create dvb_frontend */
	state->frontend.ops = &state->ops;
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops tda80xx_ops = {

	.info = {
		.name = "Philips TDA80xx DVB-S",
		.type = FE_QPSK,
		.frequency_min = 500000,
		.frequency_max = 2700000,
		.frequency_stepsize = 125,
		.symbol_rate_min = 4500000,
		.symbol_rate_max = 45000000,
		.caps =	FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_4_5 | FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 |
			FE_CAN_FEC_7_8 | FE_CAN_FEC_8_9 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK |
			FE_CAN_MUTE_TS
	},

	.release = tda80xx_release,

	.init = tda80xx_init,
	.sleep = tda80xx_sleep,

	.set_frontend = tda80xx_set_frontend,
	.get_frontend = tda80xx_get_frontend,

	.read_status = tda80xx_read_status,
	.read_ber = tda80xx_read_ber,
	.read_signal_strength = tda80xx_read_signal_strength,
	.read_snr = tda80xx_read_snr,
	.read_ucblocks = tda80xx_read_ucblocks,

	.diseqc_send_master_cmd = tda80xx_send_diseqc_msg,
	.diseqc_send_burst = tda80xx_send_diseqc_burst,
	.set_tone = tda80xx_set_tone,
	.set_voltage = tda80xx_set_voltage,
};

module_param(debug, int, 0644);

MODULE_DESCRIPTION("Philips TDA8044 / TDA8083 DVB-S Demodulator driver");
MODULE_AUTHOR("Felix Domke, Andreas Oberritter");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(tda80xx_attach);
