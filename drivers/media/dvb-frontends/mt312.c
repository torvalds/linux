/*
    Driver for Zarlink VP310/MT312/ZL10313 Satellite Channel Decoder

    Copyright (C) 2003 Andreas Oberritter <obi@linuxtv.org>
    Copyright (C) 2008 Matthias Schwarzott <zzam@gentoo.org>

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

    References:
    http://products.zarlink.com/product_profiles/MT312.htm
    http://products.zarlink.com/product_profiles/SL1935.htm
*/

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "dvb_frontend.h"
#include "mt312_priv.h"
#include "mt312.h"

/* Max transfer size done by I2C transfer functions */
#define MAX_XFER_SIZE  64

struct mt312_state {
	struct i2c_adapter *i2c;
	/* configuration settings */
	const struct mt312_config *config;
	struct dvb_frontend frontend;

	u8 id;
	unsigned long xtal;
	u8 freq_mult;
};

static int debug;
#define dprintk(args...) \
	do { \
		if (debug) \
			printk(KERN_DEBUG "mt312: " args); \
	} while (0)

#define MT312_PLL_CLK		10000000UL	/* 10 MHz */
#define MT312_PLL_CLK_10_111	10111000UL	/* 10.111 MHz */

static int mt312_read(struct mt312_state *state, const enum mt312_reg_addr reg,
		      u8 *buf, const size_t count)
{
	int ret;
	struct i2c_msg msg[2];
	u8 regbuf[1] = { reg };

	msg[0].addr = state->config->demod_address;
	msg[0].flags = 0;
	msg[0].buf = regbuf;
	msg[0].len = 1;
	msg[1].addr = state->config->demod_address;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = count;

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2) {
		printk(KERN_DEBUG "%s: ret == %d\n", __func__, ret);
		return -EREMOTEIO;
	}

	if (debug) {
		int i;
		dprintk("R(%d):", reg & 0x7f);
		for (i = 0; i < count; i++)
			printk(KERN_CONT " %02x", buf[i]);
		printk("\n");
	}

	return 0;
}

static int mt312_write(struct mt312_state *state, const enum mt312_reg_addr reg,
		       const u8 *src, const size_t count)
{
	int ret;
	u8 buf[MAX_XFER_SIZE];
	struct i2c_msg msg;

	if (1 + count > sizeof(buf)) {
		printk(KERN_WARNING
		       "mt312: write: len=%zd is too big!\n", count);
		return -EINVAL;
	}

	if (debug) {
		int i;
		dprintk("W(%d):", reg & 0x7f);
		for (i = 0; i < count; i++)
			printk(KERN_CONT " %02x", src[i]);
		printk("\n");
	}

	buf[0] = reg;
	memcpy(&buf[1], src, count);

	msg.addr = state->config->demod_address;
	msg.flags = 0;
	msg.buf = buf;
	msg.len = count + 1;

	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1) {
		dprintk("%s: ret == %d\n", __func__, ret);
		return -EREMOTEIO;
	}

	return 0;
}

static inline int mt312_readreg(struct mt312_state *state,
				const enum mt312_reg_addr reg, u8 *val)
{
	return mt312_read(state, reg, val, 1);
}

static inline int mt312_writereg(struct mt312_state *state,
				 const enum mt312_reg_addr reg, const u8 val)
{
	return mt312_write(state, reg, &val, 1);
}

static inline u32 mt312_div(u32 a, u32 b)
{
	return (a + (b / 2)) / b;
}

static int mt312_reset(struct mt312_state *state, const u8 full)
{
	return mt312_writereg(state, RESET, full ? 0x80 : 0x40);
}

static int mt312_get_inversion(struct mt312_state *state,
			       fe_spectral_inversion_t *i)
{
	int ret;
	u8 vit_mode;

	ret = mt312_readreg(state, VIT_MODE, &vit_mode);
	if (ret < 0)
		return ret;

	if (vit_mode & 0x80)	/* auto inversion was used */
		*i = (vit_mode & 0x40) ? INVERSION_ON : INVERSION_OFF;

	return 0;
}

static int mt312_get_symbol_rate(struct mt312_state *state, u32 *sr)
{
	int ret;
	u8 sym_rate_h;
	u8 dec_ratio;
	u16 sym_rat_op;
	u16 monitor;
	u8 buf[2];

	ret = mt312_readreg(state, SYM_RATE_H, &sym_rate_h);
	if (ret < 0)
		return ret;

	if (sym_rate_h & 0x80) {
		/* symbol rate search was used */
		ret = mt312_writereg(state, MON_CTRL, 0x03);
		if (ret < 0)
			return ret;

		ret = mt312_read(state, MONITOR_H, buf, sizeof(buf));
		if (ret < 0)
			return ret;

		monitor = (buf[0] << 8) | buf[1];

		dprintk("sr(auto) = %u\n",
		       mt312_div(monitor * 15625, 4));
	} else {
		ret = mt312_writereg(state, MON_CTRL, 0x05);
		if (ret < 0)
			return ret;

		ret = mt312_read(state, MONITOR_H, buf, sizeof(buf));
		if (ret < 0)
			return ret;

		dec_ratio = ((buf[0] >> 5) & 0x07) * 32;

		ret = mt312_read(state, SYM_RAT_OP_H, buf, sizeof(buf));
		if (ret < 0)
			return ret;

		sym_rat_op = (buf[0] << 8) | buf[1];

		dprintk("sym_rat_op=%d dec_ratio=%d\n",
		       sym_rat_op, dec_ratio);
		dprintk("*sr(manual) = %lu\n",
		       (((state->xtal * 8192) / (sym_rat_op + 8192)) *
			2) - dec_ratio);
	}

	return 0;
}

static int mt312_get_code_rate(struct mt312_state *state, fe_code_rate_t *cr)
{
	const fe_code_rate_t fec_tab[8] =
	    { FEC_1_2, FEC_2_3, FEC_3_4, FEC_5_6, FEC_6_7, FEC_7_8,
		FEC_AUTO, FEC_AUTO };

	int ret;
	u8 fec_status;

	ret = mt312_readreg(state, FEC_STATUS, &fec_status);
	if (ret < 0)
		return ret;

	*cr = fec_tab[(fec_status >> 4) & 0x07];

	return 0;
}

static int mt312_initfe(struct dvb_frontend *fe)
{
	struct mt312_state *state = fe->demodulator_priv;
	int ret;
	u8 buf[2];

	/* wake up */
	ret = mt312_writereg(state, CONFIG,
			(state->freq_mult == 6 ? 0x88 : 0x8c));
	if (ret < 0)
		return ret;

	/* wait at least 150 usec */
	udelay(150);

	/* full reset */
	ret = mt312_reset(state, 1);
	if (ret < 0)
		return ret;

/* Per datasheet, write correct values. 09/28/03 ACCJr.
 * If we don't do this, we won't get FE_HAS_VITERBI in the VP310. */
	{
		u8 buf_def[8] = { 0x14, 0x12, 0x03, 0x02,
				  0x01, 0x00, 0x00, 0x00 };

		ret = mt312_write(state, VIT_SETUP, buf_def, sizeof(buf_def));
		if (ret < 0)
			return ret;
	}

	switch (state->id) {
	case ID_ZL10313:
		/* enable ADC */
		ret = mt312_writereg(state, GPP_CTRL, 0x80);
		if (ret < 0)
			return ret;

		/* configure ZL10313 for optimal ADC performance */
		buf[0] = 0x80;
		buf[1] = 0xB0;
		ret = mt312_write(state, HW_CTRL, buf, 2);
		if (ret < 0)
			return ret;

		/* enable MPEG output and ADCs */
		ret = mt312_writereg(state, HW_CTRL, 0x00);
		if (ret < 0)
			return ret;

		ret = mt312_writereg(state, MPEG_CTRL, 0x00);
		if (ret < 0)
			return ret;

		break;
	}

	/* SYS_CLK */
	buf[0] = mt312_div(state->xtal * state->freq_mult * 2, 1000000);

	/* DISEQC_RATIO */
	buf[1] = mt312_div(state->xtal, 22000 * 4);

	ret = mt312_write(state, SYS_CLK, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	ret = mt312_writereg(state, SNR_THS_HIGH, 0x32);
	if (ret < 0)
		return ret;

	/* different MOCLK polarity */
	switch (state->id) {
	case ID_ZL10313:
		buf[0] = 0x33;
		break;
	default:
		buf[0] = 0x53;
		break;
	}

	ret = mt312_writereg(state, OP_CTRL, buf[0]);
	if (ret < 0)
		return ret;

	/* TS_SW_LIM */
	buf[0] = 0x8c;
	buf[1] = 0x98;

	ret = mt312_write(state, TS_SW_LIM_L, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	ret = mt312_writereg(state, CS_SW_LIM, 0x69);
	if (ret < 0)
		return ret;

	return 0;
}

static int mt312_send_master_cmd(struct dvb_frontend *fe,
				 struct dvb_diseqc_master_cmd *c)
{
	struct mt312_state *state = fe->demodulator_priv;
	int ret;
	u8 diseqc_mode;

	if ((c->msg_len == 0) || (c->msg_len > sizeof(c->msg)))
		return -EINVAL;

	ret = mt312_readreg(state, DISEQC_MODE, &diseqc_mode);
	if (ret < 0)
		return ret;

	ret = mt312_write(state, (0x80 | DISEQC_INSTR), c->msg, c->msg_len);
	if (ret < 0)
		return ret;

	ret = mt312_writereg(state, DISEQC_MODE,
			     (diseqc_mode & 0x40) | ((c->msg_len - 1) << 3)
			     | 0x04);
	if (ret < 0)
		return ret;

	/* is there a better way to wait for message to be transmitted */
	msleep(100);

	/* set DISEQC_MODE[2:0] to zero if a return message is expected */
	if (c->msg[0] & 0x02) {
		ret = mt312_writereg(state, DISEQC_MODE, (diseqc_mode & 0x40));
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int mt312_send_burst(struct dvb_frontend *fe, const fe_sec_mini_cmd_t c)
{
	struct mt312_state *state = fe->demodulator_priv;
	const u8 mini_tab[2] = { 0x02, 0x03 };

	int ret;
	u8 diseqc_mode;

	if (c > SEC_MINI_B)
		return -EINVAL;

	ret = mt312_readreg(state, DISEQC_MODE, &diseqc_mode);
	if (ret < 0)
		return ret;

	ret = mt312_writereg(state, DISEQC_MODE,
			     (diseqc_mode & 0x40) | mini_tab[c]);
	if (ret < 0)
		return ret;

	return 0;
}

static int mt312_set_tone(struct dvb_frontend *fe, const fe_sec_tone_mode_t t)
{
	struct mt312_state *state = fe->demodulator_priv;
	const u8 tone_tab[2] = { 0x01, 0x00 };

	int ret;
	u8 diseqc_mode;

	if (t > SEC_TONE_OFF)
		return -EINVAL;

	ret = mt312_readreg(state, DISEQC_MODE, &diseqc_mode);
	if (ret < 0)
		return ret;

	ret = mt312_writereg(state, DISEQC_MODE,
			     (diseqc_mode & 0x40) | tone_tab[t]);
	if (ret < 0)
		return ret;

	return 0;
}

static int mt312_set_voltage(struct dvb_frontend *fe, const fe_sec_voltage_t v)
{
	struct mt312_state *state = fe->demodulator_priv;
	const u8 volt_tab[3] = { 0x00, 0x40, 0x00 };
	u8 val;

	if (v > SEC_VOLTAGE_OFF)
		return -EINVAL;

	val = volt_tab[v];
	if (state->config->voltage_inverted)
		val ^= 0x40;

	return mt312_writereg(state, DISEQC_MODE, val);
}

static int mt312_read_status(struct dvb_frontend *fe, fe_status_t *s)
{
	struct mt312_state *state = fe->demodulator_priv;
	int ret;
	u8 status[3];

	*s = 0;

	ret = mt312_read(state, QPSK_STAT_H, status, sizeof(status));
	if (ret < 0)
		return ret;

	dprintk("QPSK_STAT_H: 0x%02x, QPSK_STAT_L: 0x%02x,"
		" FEC_STATUS: 0x%02x\n", status[0], status[1], status[2]);

	if (status[0] & 0xc0)
		*s |= FE_HAS_SIGNAL;	/* signal noise ratio */
	if (status[0] & 0x04)
		*s |= FE_HAS_CARRIER;	/* qpsk carrier lock */
	if (status[2] & 0x02)
		*s |= FE_HAS_VITERBI;	/* viterbi lock */
	if (status[2] & 0x04)
		*s |= FE_HAS_SYNC;	/* byte align lock */
	if (status[0] & 0x01)
		*s |= FE_HAS_LOCK;	/* qpsk lock */

	return 0;
}

static int mt312_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct mt312_state *state = fe->demodulator_priv;
	int ret;
	u8 buf[3];

	ret = mt312_read(state, RS_BERCNT_H, buf, 3);
	if (ret < 0)
		return ret;

	*ber = ((buf[0] << 16) | (buf[1] << 8) | buf[2]) * 64;

	return 0;
}

static int mt312_read_signal_strength(struct dvb_frontend *fe,
				      u16 *signal_strength)
{
	struct mt312_state *state = fe->demodulator_priv;
	int ret;
	u8 buf[3];
	u16 agc;
	s16 err_db;

	ret = mt312_read(state, AGC_H, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	agc = (buf[0] << 6) | (buf[1] >> 2);
	err_db = (s16) (((buf[1] & 0x03) << 14) | buf[2] << 6) >> 6;

	*signal_strength = agc;

	dprintk("agc=%08x err_db=%hd\n", agc, err_db);

	return 0;
}

static int mt312_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct mt312_state *state = fe->demodulator_priv;
	int ret;
	u8 buf[2];

	ret = mt312_read(state, M_SNR_H, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	*snr = 0xFFFF - ((((buf[0] & 0x7f) << 8) | buf[1]) << 1);

	return 0;
}

static int mt312_read_ucblocks(struct dvb_frontend *fe, u32 *ubc)
{
	struct mt312_state *state = fe->demodulator_priv;
	int ret;
	u8 buf[2];

	ret = mt312_read(state, RS_UBC_H, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	*ubc = (buf[0] << 8) | buf[1];

	return 0;
}

static int mt312_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct mt312_state *state = fe->demodulator_priv;
	int ret;
	u8 buf[5], config_val;
	u16 sr;

	const u8 fec_tab[10] =
	    { 0x00, 0x01, 0x02, 0x04, 0x3f, 0x08, 0x10, 0x20, 0x3f, 0x3f };
	const u8 inv_tab[3] = { 0x00, 0x40, 0x80 };

	dprintk("%s: Freq %d\n", __func__, p->frequency);

	if ((p->frequency < fe->ops.info.frequency_min)
	    || (p->frequency > fe->ops.info.frequency_max))
		return -EINVAL;

	if (((int)p->inversion < INVERSION_OFF)
	    || (p->inversion > INVERSION_ON))
		return -EINVAL;

	if ((p->symbol_rate < fe->ops.info.symbol_rate_min)
	    || (p->symbol_rate > fe->ops.info.symbol_rate_max))
		return -EINVAL;

	if (((int)p->fec_inner < FEC_NONE)
	    || (p->fec_inner > FEC_AUTO))
		return -EINVAL;

	if ((p->fec_inner == FEC_4_5)
	    || (p->fec_inner == FEC_8_9))
		return -EINVAL;

	switch (state->id) {
	case ID_VP310:
	/* For now we will do this only for the VP310.
	 * It should be better for the mt312 as well,
	 * but tuning will be slower. ACCJr 09/29/03
	 */
		ret = mt312_readreg(state, CONFIG, &config_val);
		if (ret < 0)
			return ret;
		if (p->symbol_rate >= 30000000) {
			/* Note that 30MS/s should use 90MHz */
			if (state->freq_mult == 6) {
				/* We are running 60MHz */
				state->freq_mult = 9;
				ret = mt312_initfe(fe);
				if (ret < 0)
					return ret;
			}
		} else {
			if (state->freq_mult == 9) {
				/* We are running 90MHz */
				state->freq_mult = 6;
				ret = mt312_initfe(fe);
				if (ret < 0)
					return ret;
			}
		}
		break;

	case ID_MT312:
	case ID_ZL10313:
		break;

	default:
		return -EINVAL;
	}

	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	/* sr = (u16)(sr * 256.0 / 1000000.0) */
	sr = mt312_div(p->symbol_rate * 4, 15625);

	/* SYM_RATE */
	buf[0] = (sr >> 8) & 0x3f;
	buf[1] = (sr >> 0) & 0xff;

	/* VIT_MODE */
	buf[2] = inv_tab[p->inversion] | fec_tab[p->fec_inner];

	/* QPSK_CTRL */
	buf[3] = 0x40;		/* swap I and Q before QPSK demodulation */

	if (p->symbol_rate < 10000000)
		buf[3] |= 0x04;	/* use afc mode */

	/* GO */
	buf[4] = 0x01;

	ret = mt312_write(state, SYM_RATE_H, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	mt312_reset(state, 0);

	return 0;
}

static int mt312_get_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct mt312_state *state = fe->demodulator_priv;
	int ret;

	ret = mt312_get_inversion(state, &p->inversion);
	if (ret < 0)
		return ret;

	ret = mt312_get_symbol_rate(state, &p->symbol_rate);
	if (ret < 0)
		return ret;

	ret = mt312_get_code_rate(state, &p->fec_inner);
	if (ret < 0)
		return ret;

	return 0;
}

static int mt312_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct mt312_state *state = fe->demodulator_priv;

	u8 val = 0x00;
	int ret;

	switch (state->id) {
	case ID_ZL10313:
		ret = mt312_readreg(state, GPP_CTRL, &val);
		if (ret < 0)
			goto error;

		/* preserve this bit to not accidentally shutdown ADC */
		val &= 0x80;
		break;
	}

	if (enable)
		val |= 0x40;
	else
		val &= ~0x40;

	ret = mt312_writereg(state, GPP_CTRL, val);

error:
	return ret;
}

static int mt312_sleep(struct dvb_frontend *fe)
{
	struct mt312_state *state = fe->demodulator_priv;
	int ret;
	u8 config;

	/* reset all registers to defaults */
	ret = mt312_reset(state, 1);
	if (ret < 0)
		return ret;

	if (state->id == ID_ZL10313) {
		/* reset ADC */
		ret = mt312_writereg(state, GPP_CTRL, 0x00);
		if (ret < 0)
			return ret;

		/* full shutdown of ADCs, mpeg bus tristated */
		ret = mt312_writereg(state, HW_CTRL, 0x0d);
		if (ret < 0)
			return ret;
	}

	ret = mt312_readreg(state, CONFIG, &config);
	if (ret < 0)
		return ret;

	/* enter standby */
	ret = mt312_writereg(state, CONFIG, config & 0x7f);
	if (ret < 0)
		return ret;

	return 0;
}

static int mt312_get_tune_settings(struct dvb_frontend *fe,
		struct dvb_frontend_tune_settings *fesettings)
{
	fesettings->min_delay_ms = 50;
	fesettings->step_size = 0;
	fesettings->max_drift = 0;
	return 0;
}

static void mt312_release(struct dvb_frontend *fe)
{
	struct mt312_state *state = fe->demodulator_priv;
	kfree(state);
}

#define MT312_SYS_CLK		90000000UL	/* 90 MHz */
static struct dvb_frontend_ops mt312_ops = {
	.delsys = { SYS_DVBS },
	.info = {
		.name = "Zarlink ???? DVB-S",
		.frequency_min = 950000,
		.frequency_max = 2150000,
		/* FIXME: adjust freq to real used xtal */
		.frequency_stepsize = (MT312_PLL_CLK / 1000) / 128,
		.symbol_rate_min = MT312_SYS_CLK / 128, /* FIXME as above */
		.symbol_rate_max = MT312_SYS_CLK / 2,
		.caps =
		    FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 |
		    FE_CAN_FEC_3_4 | FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 |
		    FE_CAN_FEC_AUTO | FE_CAN_QPSK | FE_CAN_MUTE_TS |
		    FE_CAN_RECOVER
	},

	.release = mt312_release,

	.init = mt312_initfe,
	.sleep = mt312_sleep,
	.i2c_gate_ctrl = mt312_i2c_gate_ctrl,

	.set_frontend = mt312_set_frontend,
	.get_frontend = mt312_get_frontend,
	.get_tune_settings = mt312_get_tune_settings,

	.read_status = mt312_read_status,
	.read_ber = mt312_read_ber,
	.read_signal_strength = mt312_read_signal_strength,
	.read_snr = mt312_read_snr,
	.read_ucblocks = mt312_read_ucblocks,

	.diseqc_send_master_cmd = mt312_send_master_cmd,
	.diseqc_send_burst = mt312_send_burst,
	.set_tone = mt312_set_tone,
	.set_voltage = mt312_set_voltage,
};

struct dvb_frontend *mt312_attach(const struct mt312_config *config,
					struct i2c_adapter *i2c)
{
	struct mt312_state *state = NULL;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct mt312_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;

	/* check if the demod is there */
	if (mt312_readreg(state, ID, &state->id) < 0)
		goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &mt312_ops,
		sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;

	switch (state->id) {
	case ID_VP310:
		strcpy(state->frontend.ops.info.name, "Zarlink VP310 DVB-S");
		state->xtal = MT312_PLL_CLK;
		state->freq_mult = 9;
		break;
	case ID_MT312:
		strcpy(state->frontend.ops.info.name, "Zarlink MT312 DVB-S");
		state->xtal = MT312_PLL_CLK;
		state->freq_mult = 6;
		break;
	case ID_ZL10313:
		strcpy(state->frontend.ops.info.name, "Zarlink ZL10313 DVB-S");
		state->xtal = MT312_PLL_CLK_10_111;
		state->freq_mult = 9;
		break;
	default:
		printk(KERN_WARNING "Only Zarlink VP310/MT312/ZL10313"
			" are supported chips.\n");
		goto error;
	}

	return &state->frontend;

error:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(mt312_attach);

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("Zarlink VP310/MT312/ZL10313 DVB-S Demodulator driver");
MODULE_AUTHOR("Andreas Oberritter <obi@linuxtv.org>");
MODULE_AUTHOR("Matthias Schwarzott <zzam@gentoo.org>");
MODULE_LICENSE("GPL");

