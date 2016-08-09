/*
 *    Support for LG Electronics LGDT3304 and LGDT3305 - VSB/QAM
 *
 *    Copyright (C) 2008, 2009, 2010 Michael Krufky <mkrufky@linuxtv.org>
 *
 *    LGDT3304 support by Jarod Wilson <jarod@redhat.com>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <asm/div64.h>
#include <linux/dvb/frontend.h>
#include <linux/slab.h>
#include "dvb_math.h"
#include "lgdt3305.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "set debug level (info=1, reg=2 (or-able))");

#define DBG_INFO 1
#define DBG_REG  2

#define lg_printk(kern, fmt, arg...)					\
	printk(kern "%s: " fmt, __func__, ##arg)

#define lg_info(fmt, arg...)	printk(KERN_INFO "lgdt3305: " fmt, ##arg)
#define lg_warn(fmt, arg...)	lg_printk(KERN_WARNING,       fmt, ##arg)
#define lg_err(fmt, arg...)	lg_printk(KERN_ERR,           fmt, ##arg)
#define lg_dbg(fmt, arg...) if (debug & DBG_INFO)			\
				lg_printk(KERN_DEBUG,         fmt, ##arg)
#define lg_reg(fmt, arg...) if (debug & DBG_REG)			\
				lg_printk(KERN_DEBUG,         fmt, ##arg)

#define lg_fail(ret)							\
({									\
	int __ret;							\
	__ret = (ret < 0);						\
	if (__ret)							\
		lg_err("error %d on line %d\n",	ret, __LINE__);		\
	__ret;								\
})

struct lgdt3305_state {
	struct i2c_adapter *i2c_adap;
	const struct lgdt3305_config *cfg;

	struct dvb_frontend frontend;

	enum fe_modulation current_modulation;
	u32 current_frequency;
	u32 snr;
};

/* ------------------------------------------------------------------------ */

/* FIXME: verify & document the LGDT3304 registers */

#define LGDT3305_GEN_CTRL_1                   0x0000
#define LGDT3305_GEN_CTRL_2                   0x0001
#define LGDT3305_GEN_CTRL_3                   0x0002
#define LGDT3305_GEN_STATUS                   0x0003
#define LGDT3305_GEN_CONTROL                  0x0007
#define LGDT3305_GEN_CTRL_4                   0x000a
#define LGDT3305_DGTL_AGC_REF_1               0x0012
#define LGDT3305_DGTL_AGC_REF_2               0x0013
#define LGDT3305_CR_CTR_FREQ_1                0x0106
#define LGDT3305_CR_CTR_FREQ_2                0x0107
#define LGDT3305_CR_CTR_FREQ_3                0x0108
#define LGDT3305_CR_CTR_FREQ_4                0x0109
#define LGDT3305_CR_MSE_1                     0x011b
#define LGDT3305_CR_MSE_2                     0x011c
#define LGDT3305_CR_LOCK_STATUS               0x011d
#define LGDT3305_CR_CTRL_7                    0x0126
#define LGDT3305_AGC_POWER_REF_1              0x0300
#define LGDT3305_AGC_POWER_REF_2              0x0301
#define LGDT3305_AGC_DELAY_PT_1               0x0302
#define LGDT3305_AGC_DELAY_PT_2               0x0303
#define LGDT3305_RFAGC_LOOP_FLTR_BW_1         0x0306
#define LGDT3305_RFAGC_LOOP_FLTR_BW_2         0x0307
#define LGDT3305_IFBW_1                       0x0308
#define LGDT3305_IFBW_2                       0x0309
#define LGDT3305_AGC_CTRL_1                   0x030c
#define LGDT3305_AGC_CTRL_4                   0x0314
#define LGDT3305_EQ_MSE_1                     0x0413
#define LGDT3305_EQ_MSE_2                     0x0414
#define LGDT3305_EQ_MSE_3                     0x0415
#define LGDT3305_PT_MSE_1                     0x0417
#define LGDT3305_PT_MSE_2                     0x0418
#define LGDT3305_PT_MSE_3                     0x0419
#define LGDT3305_FEC_BLOCK_CTRL               0x0504
#define LGDT3305_FEC_LOCK_STATUS              0x050a
#define LGDT3305_FEC_PKT_ERR_1                0x050c
#define LGDT3305_FEC_PKT_ERR_2                0x050d
#define LGDT3305_TP_CTRL_1                    0x050e
#define LGDT3305_BERT_PERIOD                  0x0801
#define LGDT3305_BERT_ERROR_COUNT_1           0x080a
#define LGDT3305_BERT_ERROR_COUNT_2           0x080b
#define LGDT3305_BERT_ERROR_COUNT_3           0x080c
#define LGDT3305_BERT_ERROR_COUNT_4           0x080d

static int lgdt3305_write_reg(struct lgdt3305_state *state, u16 reg, u8 val)
{
	int ret;
	u8 buf[] = { reg >> 8, reg & 0xff, val };
	struct i2c_msg msg = {
		.addr = state->cfg->i2c_addr, .flags = 0,
		.buf = buf, .len = 3,
	};

	lg_reg("reg: 0x%04x, val: 0x%02x\n", reg, val);

	ret = i2c_transfer(state->i2c_adap, &msg, 1);

	if (ret != 1) {
		lg_err("error (addr %02x %02x <- %02x, err = %i)\n",
		       msg.buf[0], msg.buf[1], msg.buf[2], ret);
		if (ret < 0)
			return ret;
		else
			return -EREMOTEIO;
	}
	return 0;
}

static int lgdt3305_read_reg(struct lgdt3305_state *state, u16 reg, u8 *val)
{
	int ret;
	u8 reg_buf[] = { reg >> 8, reg & 0xff };
	struct i2c_msg msg[] = {
		{ .addr = state->cfg->i2c_addr,
		  .flags = 0, .buf = reg_buf, .len = 2 },
		{ .addr = state->cfg->i2c_addr,
		  .flags = I2C_M_RD, .buf = val, .len = 1 },
	};

	lg_reg("reg: 0x%04x\n", reg);

	ret = i2c_transfer(state->i2c_adap, msg, 2);

	if (ret != 2) {
		lg_err("error (addr %02x reg %04x error (ret == %i)\n",
		       state->cfg->i2c_addr, reg, ret);
		if (ret < 0)
			return ret;
		else
			return -EREMOTEIO;
	}
	return 0;
}

#define read_reg(state, reg)						\
({									\
	u8 __val;							\
	int ret = lgdt3305_read_reg(state, reg, &__val);		\
	if (lg_fail(ret))						\
		__val = 0;						\
	__val;								\
})

static int lgdt3305_set_reg_bit(struct lgdt3305_state *state,
				u16 reg, int bit, int onoff)
{
	u8 val;
	int ret;

	lg_reg("reg: 0x%04x, bit: %d, level: %d\n", reg, bit, onoff);

	ret = lgdt3305_read_reg(state, reg, &val);
	if (lg_fail(ret))
		goto fail;

	val &= ~(1 << bit);
	val |= (onoff & 1) << bit;

	ret = lgdt3305_write_reg(state, reg, val);
fail:
	return ret;
}

struct lgdt3305_reg {
	u16 reg;
	u8 val;
};

static int lgdt3305_write_regs(struct lgdt3305_state *state,
			       struct lgdt3305_reg *regs, int len)
{
	int i, ret;

	lg_reg("writing %d registers...\n", len);

	for (i = 0; i < len - 1; i++) {
		ret = lgdt3305_write_reg(state, regs[i].reg, regs[i].val);
		if (lg_fail(ret))
			return ret;
	}
	return 0;
}

/* ------------------------------------------------------------------------ */

static int lgdt3305_soft_reset(struct lgdt3305_state *state)
{
	int ret;

	lg_dbg("\n");

	ret = lgdt3305_set_reg_bit(state, LGDT3305_GEN_CTRL_3, 0, 0);
	if (lg_fail(ret))
		goto fail;

	msleep(20);
	ret = lgdt3305_set_reg_bit(state, LGDT3305_GEN_CTRL_3, 0, 1);
fail:
	return ret;
}

static inline int lgdt3305_mpeg_mode(struct lgdt3305_state *state,
				     enum lgdt3305_mpeg_mode mode)
{
	lg_dbg("(%d)\n", mode);
	return lgdt3305_set_reg_bit(state, LGDT3305_TP_CTRL_1, 5, mode);
}

static int lgdt3305_mpeg_mode_polarity(struct lgdt3305_state *state)
{
	u8 val;
	int ret;
	enum lgdt3305_tp_clock_edge edge = state->cfg->tpclk_edge;
	enum lgdt3305_tp_clock_mode mode = state->cfg->tpclk_mode;
	enum lgdt3305_tp_valid_polarity valid = state->cfg->tpvalid_polarity;

	lg_dbg("edge = %d, valid = %d\n", edge, valid);

	ret = lgdt3305_read_reg(state, LGDT3305_TP_CTRL_1, &val);
	if (lg_fail(ret))
		goto fail;

	val &= ~0x09;

	if (edge)
		val |= 0x08;
	if (mode)
		val |= 0x40;
	if (valid)
		val |= 0x01;

	ret = lgdt3305_write_reg(state, LGDT3305_TP_CTRL_1, val);
	if (lg_fail(ret))
		goto fail;

	ret = lgdt3305_soft_reset(state);
fail:
	return ret;
}

static int lgdt3305_set_modulation(struct lgdt3305_state *state,
				   struct dtv_frontend_properties *p)
{
	u8 opermode;
	int ret;

	lg_dbg("\n");

	ret = lgdt3305_read_reg(state, LGDT3305_GEN_CTRL_1, &opermode);
	if (lg_fail(ret))
		goto fail;

	opermode &= ~0x03;

	switch (p->modulation) {
	case VSB_8:
		opermode |= 0x03;
		break;
	case QAM_64:
		opermode |= 0x00;
		break;
	case QAM_256:
		opermode |= 0x01;
		break;
	default:
		return -EINVAL;
	}
	ret = lgdt3305_write_reg(state, LGDT3305_GEN_CTRL_1, opermode);
fail:
	return ret;
}

static int lgdt3305_set_filter_extension(struct lgdt3305_state *state,
					 struct dtv_frontend_properties *p)
{
	int val;

	switch (p->modulation) {
	case VSB_8:
		val = 0;
		break;
	case QAM_64:
	case QAM_256:
		val = 1;
		break;
	default:
		return -EINVAL;
	}
	lg_dbg("val = %d\n", val);

	return lgdt3305_set_reg_bit(state, 0x043f, 2, val);
}

/* ------------------------------------------------------------------------ */

static int lgdt3305_passband_digital_agc(struct lgdt3305_state *state,
					 struct dtv_frontend_properties *p)
{
	u16 agc_ref;

	switch (p->modulation) {
	case VSB_8:
		agc_ref = 0x32c4;
		break;
	case QAM_64:
		agc_ref = 0x2a00;
		break;
	case QAM_256:
		agc_ref = 0x2a80;
		break;
	default:
		return -EINVAL;
	}

	lg_dbg("agc ref: 0x%04x\n", agc_ref);

	lgdt3305_write_reg(state, LGDT3305_DGTL_AGC_REF_1, agc_ref >> 8);
	lgdt3305_write_reg(state, LGDT3305_DGTL_AGC_REF_2, agc_ref & 0xff);

	return 0;
}

static int lgdt3305_rfagc_loop(struct lgdt3305_state *state,
			       struct dtv_frontend_properties *p)
{
	u16 ifbw, rfbw, agcdelay;

	switch (p->modulation) {
	case VSB_8:
		agcdelay = 0x04c0;
		rfbw     = 0x8000;
		ifbw     = 0x8000;
		break;
	case QAM_64:
	case QAM_256:
		agcdelay = 0x046b;
		rfbw     = 0x8889;
		/* FIXME: investigate optimal ifbw & rfbw values for the
		 *        DT3304 and re-write this switch..case block */
		if (state->cfg->demod_chip == LGDT3304)
			ifbw = 0x6666;
		else /* (state->cfg->demod_chip == LGDT3305) */
			ifbw = 0x8888;
		break;
	default:
		return -EINVAL;
	}

	if (state->cfg->rf_agc_loop) {
		lg_dbg("agcdelay: 0x%04x, rfbw: 0x%04x\n", agcdelay, rfbw);

		/* rf agc loop filter bandwidth */
		lgdt3305_write_reg(state, LGDT3305_AGC_DELAY_PT_1,
				   agcdelay >> 8);
		lgdt3305_write_reg(state, LGDT3305_AGC_DELAY_PT_2,
				   agcdelay & 0xff);

		lgdt3305_write_reg(state, LGDT3305_RFAGC_LOOP_FLTR_BW_1,
				   rfbw >> 8);
		lgdt3305_write_reg(state, LGDT3305_RFAGC_LOOP_FLTR_BW_2,
				   rfbw & 0xff);
	} else {
		lg_dbg("ifbw: 0x%04x\n", ifbw);

		/* if agc loop filter bandwidth */
		lgdt3305_write_reg(state, LGDT3305_IFBW_1, ifbw >> 8);
		lgdt3305_write_reg(state, LGDT3305_IFBW_2, ifbw & 0xff);
	}

	return 0;
}

static int lgdt3305_agc_setup(struct lgdt3305_state *state,
			      struct dtv_frontend_properties *p)
{
	int lockdten, acqen;

	switch (p->modulation) {
	case VSB_8:
		lockdten = 0;
		acqen = 0;
		break;
	case QAM_64:
	case QAM_256:
		lockdten = 1;
		acqen = 1;
		break;
	default:
		return -EINVAL;
	}

	lg_dbg("lockdten = %d, acqen = %d\n", lockdten, acqen);

	/* control agc function */
	switch (state->cfg->demod_chip) {
	case LGDT3304:
		lgdt3305_write_reg(state, 0x0314, 0xe1 | lockdten << 1);
		lgdt3305_set_reg_bit(state, 0x030e, 2, acqen);
		break;
	case LGDT3305:
		lgdt3305_write_reg(state, LGDT3305_AGC_CTRL_4, 0xe1 | lockdten << 1);
		lgdt3305_set_reg_bit(state, LGDT3305_AGC_CTRL_1, 2, acqen);
		break;
	default:
		return -EINVAL;
	}

	return lgdt3305_rfagc_loop(state, p);
}

static int lgdt3305_set_agc_power_ref(struct lgdt3305_state *state,
				      struct dtv_frontend_properties *p)
{
	u16 usref = 0;

	switch (p->modulation) {
	case VSB_8:
		if (state->cfg->usref_8vsb)
			usref = state->cfg->usref_8vsb;
		break;
	case QAM_64:
		if (state->cfg->usref_qam64)
			usref = state->cfg->usref_qam64;
		break;
	case QAM_256:
		if (state->cfg->usref_qam256)
			usref = state->cfg->usref_qam256;
		break;
	default:
		return -EINVAL;
	}

	if (usref) {
		lg_dbg("set manual mode: 0x%04x\n", usref);

		lgdt3305_set_reg_bit(state, LGDT3305_AGC_CTRL_1, 3, 1);

		lgdt3305_write_reg(state, LGDT3305_AGC_POWER_REF_1,
				   0xff & (usref >> 8));
		lgdt3305_write_reg(state, LGDT3305_AGC_POWER_REF_2,
				   0xff & (usref >> 0));
	}
	return 0;
}

/* ------------------------------------------------------------------------ */

static int lgdt3305_spectral_inversion(struct lgdt3305_state *state,
				       struct dtv_frontend_properties *p,
				       int inversion)
{
	int ret;

	lg_dbg("(%d)\n", inversion);

	switch (p->modulation) {
	case VSB_8:
		ret = lgdt3305_write_reg(state, LGDT3305_CR_CTRL_7,
					 inversion ? 0xf9 : 0x79);
		break;
	case QAM_64:
	case QAM_256:
		ret = lgdt3305_write_reg(state, LGDT3305_FEC_BLOCK_CTRL,
					 inversion ? 0xfd : 0xff);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int lgdt3305_set_if(struct lgdt3305_state *state,
			   struct dtv_frontend_properties *p)
{
	u16 if_freq_khz;
	u8 nco1, nco2, nco3, nco4;
	u64 nco;

	switch (p->modulation) {
	case VSB_8:
		if_freq_khz = state->cfg->vsb_if_khz;
		break;
	case QAM_64:
	case QAM_256:
		if_freq_khz = state->cfg->qam_if_khz;
		break;
	default:
		return -EINVAL;
	}

	nco = if_freq_khz / 10;

	switch (p->modulation) {
	case VSB_8:
		nco <<= 24;
		do_div(nco, 625);
		break;
	case QAM_64:
	case QAM_256:
		nco <<= 28;
		do_div(nco, 625);
		break;
	default:
		return -EINVAL;
	}

	nco1 = (nco >> 24) & 0x3f;
	nco1 |= 0x40;
	nco2 = (nco >> 16) & 0xff;
	nco3 = (nco >> 8) & 0xff;
	nco4 = nco & 0xff;

	lgdt3305_write_reg(state, LGDT3305_CR_CTR_FREQ_1, nco1);
	lgdt3305_write_reg(state, LGDT3305_CR_CTR_FREQ_2, nco2);
	lgdt3305_write_reg(state, LGDT3305_CR_CTR_FREQ_3, nco3);
	lgdt3305_write_reg(state, LGDT3305_CR_CTR_FREQ_4, nco4);

	lg_dbg("%d KHz -> [%02x%02x%02x%02x]\n",
	       if_freq_khz, nco1, nco2, nco3, nco4);

	return 0;
}

/* ------------------------------------------------------------------------ */

static int lgdt3305_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct lgdt3305_state *state = fe->demodulator_priv;

	if (state->cfg->deny_i2c_rptr)
		return 0;

	lg_dbg("(%d)\n", enable);

	return lgdt3305_set_reg_bit(state, LGDT3305_GEN_CTRL_2, 5,
				    enable ? 0 : 1);
}

static int lgdt3305_sleep(struct dvb_frontend *fe)
{
	struct lgdt3305_state *state = fe->demodulator_priv;
	u8 gen_ctrl_3, gen_ctrl_4;

	lg_dbg("\n");

	gen_ctrl_3 = read_reg(state, LGDT3305_GEN_CTRL_3);
	gen_ctrl_4 = read_reg(state, LGDT3305_GEN_CTRL_4);

	/* hold in software reset while sleeping */
	gen_ctrl_3 &= ~0x01;
	/* tristate the IF-AGC pin */
	gen_ctrl_3 |=  0x02;
	/* tristate the RF-AGC pin */
	gen_ctrl_3 |=  0x04;

	/* disable vsb/qam module */
	gen_ctrl_4 &= ~0x01;
	/* disable adc module */
	gen_ctrl_4 &= ~0x02;

	lgdt3305_write_reg(state, LGDT3305_GEN_CTRL_3, gen_ctrl_3);
	lgdt3305_write_reg(state, LGDT3305_GEN_CTRL_4, gen_ctrl_4);

	return 0;
}

static int lgdt3305_init(struct dvb_frontend *fe)
{
	struct lgdt3305_state *state = fe->demodulator_priv;
	int ret;

	static struct lgdt3305_reg lgdt3304_init_data[] = {
		{ .reg = LGDT3305_GEN_CTRL_1,           .val = 0x03, },
		{ .reg = 0x000d,                        .val = 0x02, },
		{ .reg = 0x000e,                        .val = 0x02, },
		{ .reg = LGDT3305_DGTL_AGC_REF_1,       .val = 0x32, },
		{ .reg = LGDT3305_DGTL_AGC_REF_2,       .val = 0xc4, },
		{ .reg = LGDT3305_CR_CTR_FREQ_1,        .val = 0x00, },
		{ .reg = LGDT3305_CR_CTR_FREQ_2,        .val = 0x00, },
		{ .reg = LGDT3305_CR_CTR_FREQ_3,        .val = 0x00, },
		{ .reg = LGDT3305_CR_CTR_FREQ_4,        .val = 0x00, },
		{ .reg = LGDT3305_CR_CTRL_7,            .val = 0xf9, },
		{ .reg = 0x0112,                        .val = 0x17, },
		{ .reg = 0x0113,                        .val = 0x15, },
		{ .reg = 0x0114,                        .val = 0x18, },
		{ .reg = 0x0115,                        .val = 0xff, },
		{ .reg = 0x0116,                        .val = 0x3c, },
		{ .reg = 0x0214,                        .val = 0x67, },
		{ .reg = 0x0424,                        .val = 0x8d, },
		{ .reg = 0x0427,                        .val = 0x12, },
		{ .reg = 0x0428,                        .val = 0x4f, },
		{ .reg = LGDT3305_IFBW_1,               .val = 0x80, },
		{ .reg = LGDT3305_IFBW_2,               .val = 0x00, },
		{ .reg = 0x030a,                        .val = 0x08, },
		{ .reg = 0x030b,                        .val = 0x9b, },
		{ .reg = 0x030d,                        .val = 0x00, },
		{ .reg = 0x030e,                        .val = 0x1c, },
		{ .reg = 0x0314,                        .val = 0xe1, },
		{ .reg = 0x000d,                        .val = 0x82, },
		{ .reg = LGDT3305_TP_CTRL_1,            .val = 0x5b, },
		{ .reg = LGDT3305_TP_CTRL_1,            .val = 0x5b, },
	};

	static struct lgdt3305_reg lgdt3305_init_data[] = {
		{ .reg = LGDT3305_GEN_CTRL_1,           .val = 0x03, },
		{ .reg = LGDT3305_GEN_CTRL_2,           .val = 0xb0, },
		{ .reg = LGDT3305_GEN_CTRL_3,           .val = 0x01, },
		{ .reg = LGDT3305_GEN_CONTROL,          .val = 0x6f, },
		{ .reg = LGDT3305_GEN_CTRL_4,           .val = 0x03, },
		{ .reg = LGDT3305_DGTL_AGC_REF_1,       .val = 0x32, },
		{ .reg = LGDT3305_DGTL_AGC_REF_2,       .val = 0xc4, },
		{ .reg = LGDT3305_CR_CTR_FREQ_1,        .val = 0x00, },
		{ .reg = LGDT3305_CR_CTR_FREQ_2,        .val = 0x00, },
		{ .reg = LGDT3305_CR_CTR_FREQ_3,        .val = 0x00, },
		{ .reg = LGDT3305_CR_CTR_FREQ_4,        .val = 0x00, },
		{ .reg = LGDT3305_CR_CTRL_7,            .val = 0x79, },
		{ .reg = LGDT3305_AGC_POWER_REF_1,      .val = 0x32, },
		{ .reg = LGDT3305_AGC_POWER_REF_2,      .val = 0xc4, },
		{ .reg = LGDT3305_AGC_DELAY_PT_1,       .val = 0x0d, },
		{ .reg = LGDT3305_AGC_DELAY_PT_2,       .val = 0x30, },
		{ .reg = LGDT3305_RFAGC_LOOP_FLTR_BW_1, .val = 0x80, },
		{ .reg = LGDT3305_RFAGC_LOOP_FLTR_BW_2, .val = 0x00, },
		{ .reg = LGDT3305_IFBW_1,               .val = 0x80, },
		{ .reg = LGDT3305_IFBW_2,               .val = 0x00, },
		{ .reg = LGDT3305_AGC_CTRL_1,           .val = 0x30, },
		{ .reg = LGDT3305_AGC_CTRL_4,           .val = 0x61, },
		{ .reg = LGDT3305_FEC_BLOCK_CTRL,       .val = 0xff, },
		{ .reg = LGDT3305_TP_CTRL_1,            .val = 0x1b, },
	};

	lg_dbg("\n");

	switch (state->cfg->demod_chip) {
	case LGDT3304:
		ret = lgdt3305_write_regs(state, lgdt3304_init_data,
					  ARRAY_SIZE(lgdt3304_init_data));
		break;
	case LGDT3305:
		ret = lgdt3305_write_regs(state, lgdt3305_init_data,
					  ARRAY_SIZE(lgdt3305_init_data));
		break;
	default:
		ret = -EINVAL;
	}
	if (lg_fail(ret))
		goto fail;

	ret = lgdt3305_soft_reset(state);
fail:
	return ret;
}

static int lgdt3304_set_parameters(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct lgdt3305_state *state = fe->demodulator_priv;
	int ret;

	lg_dbg("(%d, %d)\n", p->frequency, p->modulation);

	if (fe->ops.tuner_ops.set_params) {
		ret = fe->ops.tuner_ops.set_params(fe);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
		if (lg_fail(ret))
			goto fail;
		state->current_frequency = p->frequency;
	}

	ret = lgdt3305_set_modulation(state, p);
	if (lg_fail(ret))
		goto fail;

	ret = lgdt3305_passband_digital_agc(state, p);
	if (lg_fail(ret))
		goto fail;

	ret = lgdt3305_agc_setup(state, p);
	if (lg_fail(ret))
		goto fail;

	/* reg 0x030d is 3304-only... seen in vsb and qam usbsnoops... */
	switch (p->modulation) {
	case VSB_8:
		lgdt3305_write_reg(state, 0x030d, 0x00);
		lgdt3305_write_reg(state, LGDT3305_CR_CTR_FREQ_1, 0x4f);
		lgdt3305_write_reg(state, LGDT3305_CR_CTR_FREQ_2, 0x0c);
		lgdt3305_write_reg(state, LGDT3305_CR_CTR_FREQ_3, 0xac);
		lgdt3305_write_reg(state, LGDT3305_CR_CTR_FREQ_4, 0xba);
		break;
	case QAM_64:
	case QAM_256:
		lgdt3305_write_reg(state, 0x030d, 0x14);
		ret = lgdt3305_set_if(state, p);
		if (lg_fail(ret))
			goto fail;
		break;
	default:
		return -EINVAL;
	}


	ret = lgdt3305_spectral_inversion(state, p,
					  state->cfg->spectral_inversion
					  ? 1 : 0);
	if (lg_fail(ret))
		goto fail;

	state->current_modulation = p->modulation;

	ret = lgdt3305_mpeg_mode(state, state->cfg->mpeg_mode);
	if (lg_fail(ret))
		goto fail;

	/* lgdt3305_mpeg_mode_polarity calls lgdt3305_soft_reset */
	ret = lgdt3305_mpeg_mode_polarity(state);
fail:
	return ret;
}

static int lgdt3305_set_parameters(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct lgdt3305_state *state = fe->demodulator_priv;
	int ret;

	lg_dbg("(%d, %d)\n", p->frequency, p->modulation);

	if (fe->ops.tuner_ops.set_params) {
		ret = fe->ops.tuner_ops.set_params(fe);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
		if (lg_fail(ret))
			goto fail;
		state->current_frequency = p->frequency;
	}

	ret = lgdt3305_set_modulation(state, p);
	if (lg_fail(ret))
		goto fail;

	ret = lgdt3305_passband_digital_agc(state, p);
	if (lg_fail(ret))
		goto fail;
	ret = lgdt3305_set_agc_power_ref(state, p);
	if (lg_fail(ret))
		goto fail;
	ret = lgdt3305_agc_setup(state, p);
	if (lg_fail(ret))
		goto fail;

	/* low if */
	ret = lgdt3305_write_reg(state, LGDT3305_GEN_CONTROL, 0x2f);
	if (lg_fail(ret))
		goto fail;
	ret = lgdt3305_set_reg_bit(state, LGDT3305_CR_CTR_FREQ_1, 6, 1);
	if (lg_fail(ret))
		goto fail;

	ret = lgdt3305_set_if(state, p);
	if (lg_fail(ret))
		goto fail;
	ret = lgdt3305_spectral_inversion(state, p,
					  state->cfg->spectral_inversion
					  ? 1 : 0);
	if (lg_fail(ret))
		goto fail;

	ret = lgdt3305_set_filter_extension(state, p);
	if (lg_fail(ret))
		goto fail;

	state->current_modulation = p->modulation;

	ret = lgdt3305_mpeg_mode(state, state->cfg->mpeg_mode);
	if (lg_fail(ret))
		goto fail;

	/* lgdt3305_mpeg_mode_polarity calls lgdt3305_soft_reset */
	ret = lgdt3305_mpeg_mode_polarity(state);
fail:
	return ret;
}

static int lgdt3305_get_frontend(struct dvb_frontend *fe,
				 struct dtv_frontend_properties *p)
{
	struct lgdt3305_state *state = fe->demodulator_priv;

	lg_dbg("\n");

	p->modulation = state->current_modulation;
	p->frequency = state->current_frequency;
	return 0;
}

/* ------------------------------------------------------------------------ */

static int lgdt3305_read_cr_lock_status(struct lgdt3305_state *state,
					int *locked)
{
	u8 val;
	int ret;
	char *cr_lock_state = "";

	*locked = 0;

	ret = lgdt3305_read_reg(state, LGDT3305_CR_LOCK_STATUS, &val);
	if (lg_fail(ret))
		goto fail;

	switch (state->current_modulation) {
	case QAM_256:
	case QAM_64:
		if (val & (1 << 1))
			*locked = 1;

		switch (val & 0x07) {
		case 0:
			cr_lock_state = "QAM UNLOCK";
			break;
		case 4:
			cr_lock_state = "QAM 1stLock";
			break;
		case 6:
			cr_lock_state = "QAM 2ndLock";
			break;
		case 7:
			cr_lock_state = "QAM FinalLock";
			break;
		default:
			cr_lock_state = "CLOCKQAM-INVALID!";
			break;
		}
		break;
	case VSB_8:
		if (val & (1 << 7)) {
			*locked = 1;
			cr_lock_state = "CLOCKVSB";
		}
		break;
	default:
		ret = -EINVAL;
	}
	lg_dbg("(%d) %s\n", *locked, cr_lock_state);
fail:
	return ret;
}

static int lgdt3305_read_fec_lock_status(struct lgdt3305_state *state,
					 int *locked)
{
	u8 val;
	int ret, mpeg_lock, fec_lock, viterbi_lock;

	*locked = 0;

	switch (state->current_modulation) {
	case QAM_256:
	case QAM_64:
		ret = lgdt3305_read_reg(state,
					LGDT3305_FEC_LOCK_STATUS, &val);
		if (lg_fail(ret))
			goto fail;

		mpeg_lock    = (val & (1 << 0)) ? 1 : 0;
		fec_lock     = (val & (1 << 2)) ? 1 : 0;
		viterbi_lock = (val & (1 << 3)) ? 1 : 0;

		*locked = mpeg_lock && fec_lock && viterbi_lock;

		lg_dbg("(%d) %s%s%s\n", *locked,
		       mpeg_lock    ? "mpeg lock  "  : "",
		       fec_lock     ? "fec lock  "   : "",
		       viterbi_lock ? "viterbi lock" : "");
		break;
	case VSB_8:
	default:
		ret = -EINVAL;
	}
fail:
	return ret;
}

static int lgdt3305_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct lgdt3305_state *state = fe->demodulator_priv;
	u8 val;
	int ret, signal, inlock, nofecerr, snrgood,
		cr_lock, fec_lock, sync_lock;

	*status = 0;

	ret = lgdt3305_read_reg(state, LGDT3305_GEN_STATUS, &val);
	if (lg_fail(ret))
		goto fail;

	signal    = (val & (1 << 4)) ? 1 : 0;
	inlock    = (val & (1 << 3)) ? 0 : 1;
	sync_lock = (val & (1 << 2)) ? 1 : 0;
	nofecerr  = (val & (1 << 1)) ? 1 : 0;
	snrgood   = (val & (1 << 0)) ? 1 : 0;

	lg_dbg("%s%s%s%s%s\n",
	       signal    ? "SIGNALEXIST " : "",
	       inlock    ? "INLOCK "      : "",
	       sync_lock ? "SYNCLOCK "    : "",
	       nofecerr  ? "NOFECERR "    : "",
	       snrgood   ? "SNRGOOD "     : "");

	ret = lgdt3305_read_cr_lock_status(state, &cr_lock);
	if (lg_fail(ret))
		goto fail;

	if (signal)
		*status |= FE_HAS_SIGNAL;
	if (cr_lock)
		*status |= FE_HAS_CARRIER;
	if (nofecerr)
		*status |= FE_HAS_VITERBI;
	if (sync_lock)
		*status |= FE_HAS_SYNC;

	switch (state->current_modulation) {
	case QAM_256:
	case QAM_64:
		/* signal bit is unreliable on the DT3304 in QAM mode */
		if (((LGDT3304 == state->cfg->demod_chip)) && (cr_lock))
			*status |= FE_HAS_SIGNAL;

		ret = lgdt3305_read_fec_lock_status(state, &fec_lock);
		if (lg_fail(ret))
			goto fail;

		if (fec_lock)
			*status |= FE_HAS_LOCK;
		break;
	case VSB_8:
		if (inlock)
			*status |= FE_HAS_LOCK;
		break;
	default:
		ret = -EINVAL;
	}
fail:
	return ret;
}

/* ------------------------------------------------------------------------ */

/* borrowed from lgdt330x.c */
static u32 calculate_snr(u32 mse, u32 c)
{
	if (mse == 0) /* no signal */
		return 0;

	mse = intlog10(mse);
	if (mse > c) {
		/* Negative SNR, which is possible, but realisticly the
		demod will lose lock before the signal gets this bad.  The
		API only allows for unsigned values, so just return 0 */
		return 0;
	}
	return 10*(c - mse);
}

static int lgdt3305_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct lgdt3305_state *state = fe->demodulator_priv;
	u32 noise;	/* noise value */
	u32 c;		/* per-modulation SNR calculation constant */

	switch (state->current_modulation) {
	case VSB_8:
#ifdef USE_PTMSE
		/* Use Phase Tracker Mean-Square Error Register */
		/* SNR for ranges from -13.11 to +44.08 */
		noise =	((read_reg(state, LGDT3305_PT_MSE_1) & 0x07) << 16) |
			(read_reg(state, LGDT3305_PT_MSE_2) << 8) |
			(read_reg(state, LGDT3305_PT_MSE_3) & 0xff);
		c = 73957994; /* log10(25*32^2)*2^24 */
#else
		/* Use Equalizer Mean-Square Error Register */
		/* SNR for ranges from -16.12 to +44.08 */
		noise =	((read_reg(state, LGDT3305_EQ_MSE_1) & 0x0f) << 16) |
			(read_reg(state, LGDT3305_EQ_MSE_2) << 8) |
			(read_reg(state, LGDT3305_EQ_MSE_3) & 0xff);
		c = 73957994; /* log10(25*32^2)*2^24 */
#endif
		break;
	case QAM_64:
	case QAM_256:
		noise = (read_reg(state, LGDT3305_CR_MSE_1) << 8) |
			(read_reg(state, LGDT3305_CR_MSE_2) & 0xff);

		c = (state->current_modulation == QAM_64) ?
			97939837 : 98026066;
		/* log10(688128)*2^24 and log10(696320)*2^24 */
		break;
	default:
		return -EINVAL;
	}
	state->snr = calculate_snr(noise, c);
	/* report SNR in dB * 10 */
	*snr = (state->snr / ((1 << 24) / 10));
	lg_dbg("noise = 0x%08x, snr = %d.%02d dB\n", noise,
	       state->snr >> 24, (((state->snr >> 8) & 0xffff) * 100) >> 16);

	return 0;
}

static int lgdt3305_read_signal_strength(struct dvb_frontend *fe,
					 u16 *strength)
{
	/* borrowed from lgdt330x.c
	 *
	 * Calculate strength from SNR up to 35dB
	 * Even though the SNR can go higher than 35dB,
	 * there is some comfort factor in having a range of
	 * strong signals that can show at 100%
	 */
	struct lgdt3305_state *state = fe->demodulator_priv;
	u16 snr;
	int ret;

	*strength = 0;

	ret = fe->ops.read_snr(fe, &snr);
	if (lg_fail(ret))
		goto fail;
	/* Rather than use the 8.8 value snr, use state->snr which is 8.24 */
	/* scale the range 0 - 35*2^24 into 0 - 65535 */
	if (state->snr >= 8960 * 0x10000)
		*strength = 0xffff;
	else
		*strength = state->snr / 8960;
fail:
	return ret;
}

/* ------------------------------------------------------------------------ */

static int lgdt3305_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	*ber = 0;
	return 0;
}

static int lgdt3305_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct lgdt3305_state *state = fe->demodulator_priv;

	*ucblocks =
		(read_reg(state, LGDT3305_FEC_PKT_ERR_1) << 8) |
		(read_reg(state, LGDT3305_FEC_PKT_ERR_2) & 0xff);

	return 0;
}

static int lgdt3305_get_tune_settings(struct dvb_frontend *fe,
				      struct dvb_frontend_tune_settings
					*fe_tune_settings)
{
	fe_tune_settings->min_delay_ms = 500;
	lg_dbg("\n");
	return 0;
}

static void lgdt3305_release(struct dvb_frontend *fe)
{
	struct lgdt3305_state *state = fe->demodulator_priv;
	lg_dbg("\n");
	kfree(state);
}

static const struct dvb_frontend_ops lgdt3304_ops;
static const struct dvb_frontend_ops lgdt3305_ops;

struct dvb_frontend *lgdt3305_attach(const struct lgdt3305_config *config,
				     struct i2c_adapter *i2c_adap)
{
	struct lgdt3305_state *state = NULL;
	int ret;
	u8 val;

	lg_dbg("(%d-%04x)\n",
	       i2c_adap ? i2c_adapter_id(i2c_adap) : 0,
	       config ? config->i2c_addr : 0);

	state = kzalloc(sizeof(struct lgdt3305_state), GFP_KERNEL);
	if (state == NULL)
		goto fail;

	state->cfg = config;
	state->i2c_adap = i2c_adap;

	switch (config->demod_chip) {
	case LGDT3304:
		memcpy(&state->frontend.ops, &lgdt3304_ops,
		       sizeof(struct dvb_frontend_ops));
		break;
	case LGDT3305:
		memcpy(&state->frontend.ops, &lgdt3305_ops,
		       sizeof(struct dvb_frontend_ops));
		break;
	default:
		goto fail;
	}
	state->frontend.demodulator_priv = state;

	/* verify that we're talking to a lg dt3304/5 */
	ret = lgdt3305_read_reg(state, LGDT3305_GEN_CTRL_2, &val);
	if ((lg_fail(ret)) | (val == 0))
		goto fail;
	ret = lgdt3305_write_reg(state, 0x0808, 0x80);
	if (lg_fail(ret))
		goto fail;
	ret = lgdt3305_read_reg(state, 0x0808, &val);
	if ((lg_fail(ret)) | (val != 0x80))
		goto fail;
	ret = lgdt3305_write_reg(state, 0x0808, 0x00);
	if (lg_fail(ret))
		goto fail;

	state->current_frequency = -1;
	state->current_modulation = -1;

	return &state->frontend;
fail:
	lg_warn("unable to detect %s hardware\n",
		config->demod_chip ? "LGDT3304" : "LGDT3305");
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(lgdt3305_attach);

static const struct dvb_frontend_ops lgdt3304_ops = {
	.delsys = { SYS_ATSC, SYS_DVBC_ANNEX_B },
	.info = {
		.name = "LG Electronics LGDT3304 VSB/QAM Frontend",
		.frequency_min      = 54000000,
		.frequency_max      = 858000000,
		.frequency_stepsize = 62500,
		.caps = FE_CAN_QAM_64 | FE_CAN_QAM_256 | FE_CAN_8VSB
	},
	.i2c_gate_ctrl        = lgdt3305_i2c_gate_ctrl,
	.init                 = lgdt3305_init,
	.sleep                = lgdt3305_sleep,
	.set_frontend         = lgdt3304_set_parameters,
	.get_frontend         = lgdt3305_get_frontend,
	.get_tune_settings    = lgdt3305_get_tune_settings,
	.read_status          = lgdt3305_read_status,
	.read_ber             = lgdt3305_read_ber,
	.read_signal_strength = lgdt3305_read_signal_strength,
	.read_snr             = lgdt3305_read_snr,
	.read_ucblocks        = lgdt3305_read_ucblocks,
	.release              = lgdt3305_release,
};

static const struct dvb_frontend_ops lgdt3305_ops = {
	.delsys = { SYS_ATSC, SYS_DVBC_ANNEX_B },
	.info = {
		.name = "LG Electronics LGDT3305 VSB/QAM Frontend",
		.frequency_min      = 54000000,
		.frequency_max      = 858000000,
		.frequency_stepsize = 62500,
		.caps = FE_CAN_QAM_64 | FE_CAN_QAM_256 | FE_CAN_8VSB
	},
	.i2c_gate_ctrl        = lgdt3305_i2c_gate_ctrl,
	.init                 = lgdt3305_init,
	.sleep                = lgdt3305_sleep,
	.set_frontend         = lgdt3305_set_parameters,
	.get_frontend         = lgdt3305_get_frontend,
	.get_tune_settings    = lgdt3305_get_tune_settings,
	.read_status          = lgdt3305_read_status,
	.read_ber             = lgdt3305_read_ber,
	.read_signal_strength = lgdt3305_read_signal_strength,
	.read_snr             = lgdt3305_read_snr,
	.read_ucblocks        = lgdt3305_read_ucblocks,
	.release              = lgdt3305_release,
};

MODULE_DESCRIPTION("LG Electronics LGDT3304/5 ATSC/QAM-B Demodulator Driver");
MODULE_AUTHOR("Michael Krufky <mkrufky@linuxtv.org>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.2");
