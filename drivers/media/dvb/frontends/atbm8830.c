/*
 *    Support for AltoBeam GB20600 (a.k.a DMB-TH) demodulator
 *    ATBM8830, ATBM8831
 *
 *    Copyright (C) 2009 David T.L. Wong <davidtlwong@gmail.com>
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
 */

#include <asm/div64.h>
#include "dvb_frontend.h"

#include "atbm8830.h"
#include "atbm8830_priv.h"

#define dprintk(args...) \
	do { \
		if (debug) \
			printk(KERN_DEBUG "atbm8830: " args); \
	} while (0)

static int debug;

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

static int atbm8830_write_reg(struct atbm_state *priv, u16 reg, u8 data)
{
	int ret = 0;
	u8 dev_addr;
	u8 buf1[] = { reg >> 8, reg & 0xFF };
	u8 buf2[] = { data };
	struct i2c_msg msg1 = { .flags = 0, .buf = buf1, .len = 2 };
	struct i2c_msg msg2 = { .flags = 0, .buf = buf2, .len = 1 };

	dev_addr = priv->config->demod_address;
	msg1.addr = dev_addr;
	msg2.addr = dev_addr;

	if (debug >= 2)
		dprintk("%s: reg=0x%04X, data=0x%02X\n", __func__, reg, data);

	ret = i2c_transfer(priv->i2c, &msg1, 1);
	if (ret != 1)
		return -EIO;

	ret = i2c_transfer(priv->i2c, &msg2, 1);
	return (ret != 1) ? -EIO : 0;
}

static int atbm8830_read_reg(struct atbm_state *priv, u16 reg, u8 *p_data)
{
	int ret;
	u8 dev_addr;

	u8 buf1[] = { reg >> 8, reg & 0xFF };
	u8 buf2[] = { 0 };
	struct i2c_msg msg1 = { .flags = 0, .buf = buf1, .len = 2 };
	struct i2c_msg msg2 = { .flags = I2C_M_RD, .buf = buf2, .len = 1 };

	dev_addr = priv->config->demod_address;
	msg1.addr = dev_addr;
	msg2.addr = dev_addr;

	ret = i2c_transfer(priv->i2c, &msg1, 1);
	if (ret != 1) {
		dprintk("%s: error reg=0x%04x, ret=%i\n", __func__, reg, ret);
		return -EIO;
	}

	ret = i2c_transfer(priv->i2c, &msg2, 1);
	if (ret != 1)
		return -EIO;

	*p_data = buf2[0];
	if (debug >= 2)
		dprintk("%s: reg=0x%04X, data=0x%02X\n",
			__func__, reg, buf2[0]);

	return 0;
}

/* Lock register latch so that multi-register read is atomic */
static inline int atbm8830_reglatch_lock(struct atbm_state *priv, int lock)
{
	return atbm8830_write_reg(priv, REG_READ_LATCH, lock ? 1 : 0);
}

static int set_osc_freq(struct atbm_state *priv, u32 freq /*in kHz*/)
{
	u32 val;
	u64 t;

	/* 0x100000 * freq / 30.4MHz */
	t = (u64)0x100000 * freq;
	do_div(t, 30400);
	val = t;

	atbm8830_write_reg(priv, REG_OSC_CLK, val);
	atbm8830_write_reg(priv, REG_OSC_CLK + 1, val >> 8);
	atbm8830_write_reg(priv, REG_OSC_CLK + 2, val >> 16);

	return 0;
}

static int set_if_freq(struct atbm_state *priv, u32 freq /*in kHz*/)
{

	u32 fs = priv->config->osc_clk_freq;
	u64 t;
	u32 val;
	u8 dat;

	if (freq != 0) {
		/* 2 * PI * (freq - fs) / fs * (2 ^ 22) */
		t = (u64) 2 * 31416 * (freq - fs);
		t <<= 22;
		do_div(t, fs);
		do_div(t, 1000);
		val = t;

		atbm8830_write_reg(priv, REG_TUNER_BASEBAND, 1);
		atbm8830_write_reg(priv, REG_IF_FREQ, val);
		atbm8830_write_reg(priv, REG_IF_FREQ+1, val >> 8);
		atbm8830_write_reg(priv, REG_IF_FREQ+2, val >> 16);

		atbm8830_read_reg(priv, REG_ADC_CONFIG, &dat);
		dat &= 0xFC;
		atbm8830_write_reg(priv, REG_ADC_CONFIG, dat);
	} else {
		/* Zero IF */
		atbm8830_write_reg(priv, REG_TUNER_BASEBAND, 0);

		atbm8830_read_reg(priv, REG_ADC_CONFIG, &dat);
		dat &= 0xFC;
		dat |= 0x02;
		atbm8830_write_reg(priv, REG_ADC_CONFIG, dat);

		if (priv->config->zif_swap_iq)
			atbm8830_write_reg(priv, REG_SWAP_I_Q, 0x03);
		else
			atbm8830_write_reg(priv, REG_SWAP_I_Q, 0x01);
	}

	return 0;
}

static int is_locked(struct atbm_state *priv, u8 *locked)
{
	u8 status;

	atbm8830_read_reg(priv, REG_LOCK_STATUS, &status);

	if (locked != NULL)
		*locked = (status == 1);
	return 0;
}

static int set_agc_config(struct atbm_state *priv,
	u8 min, u8 max, u8 hold_loop)
{
	/* no effect if both min and max are zero */
	if (!min && !max)
	    return 0;

	atbm8830_write_reg(priv, REG_AGC_MIN, min);
	atbm8830_write_reg(priv, REG_AGC_MAX, max);
	atbm8830_write_reg(priv, REG_AGC_HOLD_LOOP, hold_loop);

	return 0;
}

static int set_static_channel_mode(struct atbm_state *priv)
{
	int i;

	for (i = 0; i < 5; i++)
		atbm8830_write_reg(priv, 0x099B + i, 0x08);

	atbm8830_write_reg(priv, 0x095B, 0x7F);
	atbm8830_write_reg(priv, 0x09CB, 0x01);
	atbm8830_write_reg(priv, 0x09CC, 0x7F);
	atbm8830_write_reg(priv, 0x09CD, 0x7F);
	atbm8830_write_reg(priv, 0x0E01, 0x20);

	/* For single carrier */
	atbm8830_write_reg(priv, 0x0B03, 0x0A);
	atbm8830_write_reg(priv, 0x0935, 0x10);
	atbm8830_write_reg(priv, 0x0936, 0x08);
	atbm8830_write_reg(priv, 0x093E, 0x08);
	atbm8830_write_reg(priv, 0x096E, 0x06);

	/* frame_count_max0 */
	atbm8830_write_reg(priv, 0x0B09, 0x00);
	/* frame_count_max1 */
	atbm8830_write_reg(priv, 0x0B0A, 0x08);

	return 0;
}

static int set_ts_config(struct atbm_state *priv)
{
	const struct atbm8830_config *cfg = priv->config;

	/*Set parallel/serial ts mode*/
	atbm8830_write_reg(priv, REG_TS_SERIAL, cfg->serial_ts ? 1 : 0);
	atbm8830_write_reg(priv, REG_TS_CLK_MODE, cfg->serial_ts ? 1 : 0);
	/*Set ts sampling edge*/
	atbm8830_write_reg(priv, REG_TS_SAMPLE_EDGE,
		cfg->ts_sampling_edge ? 1 : 0);
	/*Set ts clock freerun*/
	atbm8830_write_reg(priv, REG_TS_CLK_FREERUN,
		cfg->ts_clk_gated ? 0 : 1);

	return 0;
}

static int atbm8830_init(struct dvb_frontend *fe)
{
	struct atbm_state *priv = fe->demodulator_priv;
	const struct atbm8830_config *cfg = priv->config;

	/*Set oscillator frequency*/
	set_osc_freq(priv, cfg->osc_clk_freq);

	/*Set IF frequency*/
	set_if_freq(priv, cfg->if_freq);

	/*Set AGC Config*/
	set_agc_config(priv, cfg->agc_min, cfg->agc_max,
		cfg->agc_hold_loop);

	/*Set static channel mode*/
	set_static_channel_mode(priv);

	set_ts_config(priv);
	/*Turn off DSP reset*/
	atbm8830_write_reg(priv, 0x000A, 0);

	/*SW version test*/
	atbm8830_write_reg(priv, 0x020C, 11);

	/* Run */
	atbm8830_write_reg(priv, REG_DEMOD_RUN, 1);

	return 0;
}


static void atbm8830_release(struct dvb_frontend *fe)
{
	struct atbm_state *state = fe->demodulator_priv;
	dprintk("%s\n", __func__);

	kfree(state);
}

static int atbm8830_set_fe(struct dvb_frontend *fe)
{
	struct atbm_state *priv = fe->demodulator_priv;
	int i;
	u8 locked = 0;
	dprintk("%s\n", __func__);

	/* set frequency */
	if (fe->ops.tuner_ops.set_params) {
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);
		fe->ops.tuner_ops.set_params(fe);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	/* start auto lock */
	for (i = 0; i < 10; i++) {
		mdelay(100);
		dprintk("Try %d\n", i);
		is_locked(priv, &locked);
		if (locked != 0) {
			dprintk("ATBM8830 locked!\n");
			break;
		}
	}

	return 0;
}

static int atbm8830_get_fe(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	dprintk("%s\n", __func__);

	/* TODO: get real readings from device */
	/* inversion status */
	c->inversion = INVERSION_OFF;

	/* bandwidth */
	c->bandwidth_hz = 8000000;

	c->code_rate_HP = FEC_AUTO;
	c->code_rate_LP = FEC_AUTO;

	c->modulation = QAM_AUTO;

	/* transmission mode */
	c->transmission_mode = TRANSMISSION_MODE_AUTO;

	/* guard interval */
	c->guard_interval = GUARD_INTERVAL_AUTO;

	/* hierarchy */
	c->hierarchy = HIERARCHY_NONE;

	return 0;
}

static int atbm8830_get_tune_settings(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *fesettings)
{
	fesettings->min_delay_ms = 0;
	fesettings->step_size = 0;
	fesettings->max_drift = 0;
	return 0;
}

static int atbm8830_read_status(struct dvb_frontend *fe, fe_status_t *fe_status)
{
	struct atbm_state *priv = fe->demodulator_priv;
	u8 locked = 0;
	u8 agc_locked = 0;

	dprintk("%s\n", __func__);
	*fe_status = 0;

	is_locked(priv, &locked);
	if (locked) {
		*fe_status |= FE_HAS_SIGNAL | FE_HAS_CARRIER |
			FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
	}
	dprintk("%s: fe_status=0x%x\n", __func__, *fe_status);

	atbm8830_read_reg(priv, REG_AGC_LOCK, &agc_locked);
	dprintk("AGC Lock: %d\n", agc_locked);

	return 0;
}

static int atbm8830_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct atbm_state *priv = fe->demodulator_priv;
	u32 frame_err;
	u8 t;

	dprintk("%s\n", __func__);

	atbm8830_reglatch_lock(priv, 1);

	atbm8830_read_reg(priv, REG_FRAME_ERR_CNT + 1, &t);
	frame_err = t & 0x7F;
	frame_err <<= 8;
	atbm8830_read_reg(priv, REG_FRAME_ERR_CNT, &t);
	frame_err |= t;

	atbm8830_reglatch_lock(priv, 0);

	*ber = frame_err * 100 / 32767;

	dprintk("%s: ber=0x%x\n", __func__, *ber);
	return 0;
}

static int atbm8830_read_signal_strength(struct dvb_frontend *fe, u16 *signal)
{
	struct atbm_state *priv = fe->demodulator_priv;
	u32 pwm;
	u8 t;

	dprintk("%s\n", __func__);
	atbm8830_reglatch_lock(priv, 1);

	atbm8830_read_reg(priv, REG_AGC_PWM_VAL + 1, &t);
	pwm = t & 0x03;
	pwm <<= 8;
	atbm8830_read_reg(priv, REG_AGC_PWM_VAL, &t);
	pwm |= t;

	atbm8830_reglatch_lock(priv, 0);

	dprintk("AGC PWM = 0x%02X\n", pwm);
	pwm = 0x400 - pwm;

	*signal = pwm * 0x10000 / 0x400;

	return 0;
}

static int atbm8830_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	dprintk("%s\n", __func__);
	*snr = 0;
	return 0;
}

static int atbm8830_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	dprintk("%s\n", __func__);
	*ucblocks = 0;
	return 0;
}

static int atbm8830_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct atbm_state *priv = fe->demodulator_priv;

	return atbm8830_write_reg(priv, REG_I2C_GATE, enable ? 1 : 0);
}

static struct dvb_frontend_ops atbm8830_ops = {
	.delsys = { SYS_DTMB },
	.info = {
		.name = "AltoBeam ATBM8830/8831 DMB-TH",
		.frequency_min = 474000000,
		.frequency_max = 858000000,
		.frequency_stepsize = 10000,
		.caps =
			FE_CAN_FEC_AUTO |
			FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO
	},

	.release = atbm8830_release,

	.init = atbm8830_init,
	.sleep = NULL,
	.write = NULL,
	.i2c_gate_ctrl = atbm8830_i2c_gate_ctrl,

	.set_frontend = atbm8830_set_fe,
	.get_frontend = atbm8830_get_fe,
	.get_tune_settings = atbm8830_get_tune_settings,

	.read_status = atbm8830_read_status,
	.read_ber = atbm8830_read_ber,
	.read_signal_strength = atbm8830_read_signal_strength,
	.read_snr = atbm8830_read_snr,
	.read_ucblocks = atbm8830_read_ucblocks,
};

struct dvb_frontend *atbm8830_attach(const struct atbm8830_config *config,
	struct i2c_adapter *i2c)
{
	struct atbm_state *priv = NULL;
	u8 data = 0;

	dprintk("%s()\n", __func__);

	if (config == NULL || i2c == NULL)
		return NULL;

	priv = kzalloc(sizeof(struct atbm_state), GFP_KERNEL);
	if (priv == NULL)
		goto error_out;

	priv->config = config;
	priv->i2c = i2c;

	/* check if the demod is there */
	if (atbm8830_read_reg(priv, REG_CHIP_ID, &data) != 0) {
		dprintk("%s atbm8830/8831 not found at i2c addr 0x%02X\n",
			__func__, priv->config->demod_address);
		goto error_out;
	}
	dprintk("atbm8830 chip id: 0x%02X\n", data);

	memcpy(&priv->frontend.ops, &atbm8830_ops,
	       sizeof(struct dvb_frontend_ops));
	priv->frontend.demodulator_priv = priv;

	atbm8830_init(&priv->frontend);

	atbm8830_i2c_gate_ctrl(&priv->frontend, 1);

	return &priv->frontend;

error_out:
	dprintk("%s() error_out\n", __func__);
	kfree(priv);
	return NULL;

}
EXPORT_SYMBOL(atbm8830_attach);

MODULE_DESCRIPTION("AltoBeam ATBM8830/8831 GB20600 demodulator driver");
MODULE_AUTHOR("David T. L. Wong <davidtlwong@gmail.com>");
MODULE_LICENSE("GPL");
