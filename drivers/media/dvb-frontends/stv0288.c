/*
	Driver for ST STV0288 demodulator
	Copyright (C) 2006 Georg Acher, BayCom GmbH, acher (at) baycom (dot) de
		for Reel Multimedia
	Copyright (C) 2008 TurboSight.com, Bob Liu <bob@turbosight.com>
	Copyright (C) 2008 Igor M. Liplianin <liplianin@me.by>
		Removed stb6000 specific tuner code and revised some
		procedures.
	2010-09-01 Josef Pavlik <josef@pavlik.it>
		Fixed diseqc_msg, diseqc_burst and set_tone problems

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
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <asm/div64.h>

#include "dvb_frontend.h"
#include "stv0288.h"

struct stv0288_state {
	struct i2c_adapter *i2c;
	const struct stv0288_config *config;
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
		if (debug) \
			printk(KERN_DEBUG "stv0288: " args); \
	} while (0)


static int stv0288_writeregI(struct stv0288_state *state, u8 reg, u8 data)
{
	int ret;
	u8 buf[] = { reg, data };
	struct i2c_msg msg = {
		.addr = state->config->demod_address,
		.flags = 0,
		.buf = buf,
		.len = 2
	};

	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1)
		dprintk("%s: writereg error (reg == 0x%02x, val == 0x%02x, "
			"ret == %i)\n", __func__, reg, data, ret);

	return (ret != 1) ? -EREMOTEIO : 0;
}

static int stv0288_write(struct dvb_frontend *fe, const u8 buf[], int len)
{
	struct stv0288_state *state = fe->demodulator_priv;

	if (len != 2)
		return -EINVAL;

	return stv0288_writeregI(state, buf[0], buf[1]);
}

static u8 stv0288_readreg(struct stv0288_state *state, u8 reg)
{
	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = {
		{
			.addr = state->config->demod_address,
			.flags = 0,
			.buf = b0,
			.len = 1
		}, {
			.addr = state->config->demod_address,
			.flags = I2C_M_RD,
			.buf = b1,
			.len = 1
		}
	};

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2)
		dprintk("%s: readreg error (reg == 0x%02x, ret == %i)\n",
				__func__, reg, ret);

	return b1[0];
}

static int stv0288_set_symbolrate(struct dvb_frontend *fe, u32 srate)
{
	struct stv0288_state *state = fe->demodulator_priv;
	unsigned int temp;
	unsigned char b[3];

	if ((srate < 1000000) || (srate > 45000000))
		return -EINVAL;

	stv0288_writeregI(state, 0x22, 0);
	stv0288_writeregI(state, 0x23, 0);
	stv0288_writeregI(state, 0x2b, 0xff);
	stv0288_writeregI(state, 0x2c, 0xf7);

	temp = (unsigned int)srate / 1000;

		temp = temp * 32768;
		temp = temp / 25;
		temp = temp / 125;
		b[0] = (unsigned char)((temp >> 12) & 0xff);
		b[1] = (unsigned char)((temp >> 4) & 0xff);
		b[2] = (unsigned char)((temp << 4) & 0xf0);
		stv0288_writeregI(state, 0x28, 0x80); /* SFRH */
		stv0288_writeregI(state, 0x29, 0); /* SFRM */
		stv0288_writeregI(state, 0x2a, 0); /* SFRL */

		stv0288_writeregI(state, 0x28, b[0]);
		stv0288_writeregI(state, 0x29, b[1]);
		stv0288_writeregI(state, 0x2a, b[2]);
		dprintk("stv0288: stv0288_set_symbolrate\n");

	return 0;
}

static int stv0288_send_diseqc_msg(struct dvb_frontend *fe,
				    struct dvb_diseqc_master_cmd *m)
{
	struct stv0288_state *state = fe->demodulator_priv;

	int i;

	dprintk("%s\n", __func__);

	stv0288_writeregI(state, 0x09, 0);
	msleep(30);
	stv0288_writeregI(state, 0x05, 0x12);/* modulated mode, single shot */

	for (i = 0; i < m->msg_len; i++) {
		if (stv0288_writeregI(state, 0x06, m->msg[i]))
			return -EREMOTEIO;
	}
	msleep(m->msg_len*12);
	return 0;
}

static int stv0288_send_diseqc_burst(struct dvb_frontend *fe,
						fe_sec_mini_cmd_t burst)
{
	struct stv0288_state *state = fe->demodulator_priv;

	dprintk("%s\n", __func__);

	if (stv0288_writeregI(state, 0x05, 0x03))/* burst mode, single shot */
		return -EREMOTEIO;

	if (stv0288_writeregI(state, 0x06, burst == SEC_MINI_A ? 0x00 : 0xff))
		return -EREMOTEIO;

	msleep(15);
	if (stv0288_writeregI(state, 0x05, 0x12))
		return -EREMOTEIO;

	return 0;
}

static int stv0288_set_tone(struct dvb_frontend *fe, fe_sec_tone_mode_t tone)
{
	struct stv0288_state *state = fe->demodulator_priv;

	switch (tone) {
	case SEC_TONE_ON:
		if (stv0288_writeregI(state, 0x05, 0x10))/* cont carrier */
			return -EREMOTEIO;
	break;

	case SEC_TONE_OFF:
		if (stv0288_writeregI(state, 0x05, 0x12))/* burst mode off*/
			return -EREMOTEIO;
	break;

	default:
		return -EINVAL;
	}
	return 0;
}

static u8 stv0288_inittab[] = {
	0x01, 0x15,
	0x02, 0x20,
	0x09, 0x0,
	0x0a, 0x4,
	0x0b, 0x0,
	0x0c, 0x0,
	0x0d, 0x0,
	0x0e, 0xd4,
	0x0f, 0x30,
	0x11, 0x80,
	0x12, 0x03,
	0x13, 0x48,
	0x14, 0x84,
	0x15, 0x45,
	0x16, 0xb7,
	0x17, 0x9c,
	0x18, 0x0,
	0x19, 0xa6,
	0x1a, 0x88,
	0x1b, 0x8f,
	0x1c, 0xf0,
	0x20, 0x0b,
	0x21, 0x54,
	0x22, 0x0,
	0x23, 0x0,
	0x2b, 0xff,
	0x2c, 0xf7,
	0x30, 0x0,
	0x31, 0x1e,
	0x32, 0x14,
	0x33, 0x0f,
	0x34, 0x09,
	0x35, 0x0c,
	0x36, 0x05,
	0x37, 0x2f,
	0x38, 0x16,
	0x39, 0xbe,
	0x3a, 0x0,
	0x3b, 0x13,
	0x3c, 0x11,
	0x3d, 0x30,
	0x40, 0x63,
	0x41, 0x04,
	0x42, 0x20,
	0x43, 0x00,
	0x44, 0x00,
	0x45, 0x00,
	0x46, 0x00,
	0x47, 0x00,
	0x4a, 0x00,
	0x50, 0x10,
	0x51, 0x38,
	0x52, 0x21,
	0x58, 0x54,
	0x59, 0x86,
	0x5a, 0x0,
	0x5b, 0x9b,
	0x5c, 0x08,
	0x5d, 0x7f,
	0x5e, 0x0,
	0x5f, 0xff,
	0x70, 0x0,
	0x71, 0x0,
	0x72, 0x0,
	0x74, 0x0,
	0x75, 0x0,
	0x76, 0x0,
	0x81, 0x0,
	0x82, 0x3f,
	0x83, 0x3f,
	0x84, 0x0,
	0x85, 0x0,
	0x88, 0x0,
	0x89, 0x0,
	0x8a, 0x0,
	0x8b, 0x0,
	0x8c, 0x0,
	0x90, 0x0,
	0x91, 0x0,
	0x92, 0x0,
	0x93, 0x0,
	0x94, 0x1c,
	0x97, 0x0,
	0xa0, 0x48,
	0xa1, 0x0,
	0xb0, 0xb8,
	0xb1, 0x3a,
	0xb2, 0x10,
	0xb3, 0x82,
	0xb4, 0x80,
	0xb5, 0x82,
	0xb6, 0x82,
	0xb7, 0x82,
	0xb8, 0x20,
	0xb9, 0x0,
	0xf0, 0x0,
	0xf1, 0x0,
	0xf2, 0xc0,
	0x51, 0x36,
	0x52, 0x09,
	0x53, 0x94,
	0x54, 0x62,
	0x55, 0x29,
	0x56, 0x64,
	0x57, 0x2b,
	0xff, 0xff,
};

static int stv0288_set_voltage(struct dvb_frontend *fe, fe_sec_voltage_t volt)
{
	dprintk("%s: %s\n", __func__,
		volt == SEC_VOLTAGE_13 ? "SEC_VOLTAGE_13" :
		volt == SEC_VOLTAGE_18 ? "SEC_VOLTAGE_18" : "??");

	return 0;
}

static int stv0288_init(struct dvb_frontend *fe)
{
	struct stv0288_state *state = fe->demodulator_priv;
	int i;
	u8 reg;
	u8 val;

	dprintk("stv0288: init chip\n");
	stv0288_writeregI(state, 0x41, 0x04);
	msleep(50);

	/* we have default inittab */
	if (state->config->inittab == NULL) {
		for (i = 0; !(stv0288_inittab[i] == 0xff &&
				stv0288_inittab[i + 1] == 0xff); i += 2)
			stv0288_writeregI(state, stv0288_inittab[i],
					stv0288_inittab[i + 1]);
	} else {
		for (i = 0; ; i += 2)  {
			reg = state->config->inittab[i];
			val = state->config->inittab[i+1];
			if (reg == 0xff && val == 0xff)
				break;
			stv0288_writeregI(state, reg, val);
		}
	}
	return 0;
}

static int stv0288_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct stv0288_state *state = fe->demodulator_priv;

	u8 sync = stv0288_readreg(state, 0x24);
	if (sync == 255)
		sync = 0;

	dprintk("%s : FE_READ_STATUS : VSTATUS: 0x%02x\n", __func__, sync);

	*status = 0;
	if (sync & 0x80)
		*status |= FE_HAS_CARRIER | FE_HAS_SIGNAL;
	if (sync & 0x10)
		*status |= FE_HAS_VITERBI;
	if (sync & 0x08) {
		*status |= FE_HAS_LOCK;
		dprintk("stv0288 has locked\n");
	}

	return 0;
}

static int stv0288_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct stv0288_state *state = fe->demodulator_priv;

	if (state->errmode != STATUS_BER)
		return 0;
	*ber = (stv0288_readreg(state, 0x26) << 8) |
					stv0288_readreg(state, 0x27);
	dprintk("stv0288_read_ber %d\n", *ber);

	return 0;
}


static int stv0288_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct stv0288_state *state = fe->demodulator_priv;

	s32 signal =  0xffff - ((stv0288_readreg(state, 0x10) << 8));


	signal = signal * 5 / 4;
	*strength = (signal > 0xffff) ? 0xffff : (signal < 0) ? 0 : signal;
	dprintk("stv0288_read_signal_strength %d\n", *strength);

	return 0;
}
static int stv0288_sleep(struct dvb_frontend *fe)
{
	struct stv0288_state *state = fe->demodulator_priv;

	stv0288_writeregI(state, 0x41, 0x84);
	state->initialised = 0;

	return 0;
}
static int stv0288_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct stv0288_state *state = fe->demodulator_priv;

	s32 xsnr = 0xffff - ((stv0288_readreg(state, 0x2d) << 8)
			   | stv0288_readreg(state, 0x2e));
	xsnr = 3 * (xsnr - 0xa100);
	*snr = (xsnr > 0xffff) ? 0xffff : (xsnr < 0) ? 0 : xsnr;
	dprintk("stv0288_read_snr %d\n", *snr);

	return 0;
}

static int stv0288_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct stv0288_state *state = fe->demodulator_priv;

	if (state->errmode != STATUS_BER)
		return 0;
	*ucblocks = (stv0288_readreg(state, 0x26) << 8) |
					stv0288_readreg(state, 0x27);
	dprintk("stv0288_read_ber %d\n", *ucblocks);

	return 0;
}

static int stv0288_set_property(struct dvb_frontend *fe, struct dtv_property *p)
{
	dprintk("%s(..)\n", __func__);
	return 0;
}

static int stv0288_set_frontend(struct dvb_frontend *fe)
{
	struct stv0288_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	char tm;
	unsigned char tda[3];
	u8 reg, time_out = 0;

	dprintk("%s : FE_SET_FRONTEND\n", __func__);

	if (c->delivery_system != SYS_DVBS) {
			dprintk("%s: unsupported delivery "
				"system selected (%d)\n",
				__func__, c->delivery_system);
			return -EOPNOTSUPP;
	}

	if (state->config->set_ts_params)
		state->config->set_ts_params(fe, 0);

	/* only frequency & symbol_rate are used for tuner*/
	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	udelay(10);
	stv0288_set_symbolrate(fe, c->symbol_rate);
	/* Carrier lock control register */
	stv0288_writeregI(state, 0x15, 0xc5);

	tda[2] = 0x0; /* CFRL */
	for (tm = -9; tm < 7;) {
		/* Viterbi status */
		reg = stv0288_readreg(state, 0x24);
		if (reg & 0x8)
				break;
		if (reg & 0x80) {
			time_out++;
			if (time_out > 10)
				break;
			tda[2] += 40;
			if (tda[2] < 40)
				tm++;
		} else {
			tm++;
			tda[2] = 0;
			time_out = 0;
		}
		tda[1] = (unsigned char)tm;
		stv0288_writeregI(state, 0x2b, tda[1]);
		stv0288_writeregI(state, 0x2c, tda[2]);
		msleep(30);
	}
	state->tuner_frequency = c->frequency;
	state->fec_inner = FEC_AUTO;
	state->symbol_rate = c->symbol_rate;

	return 0;
}

static int stv0288_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct stv0288_state *state = fe->demodulator_priv;

	if (enable)
		stv0288_writeregI(state, 0x01, 0xb5);
	else
		stv0288_writeregI(state, 0x01, 0x35);

	udelay(1);

	return 0;
}

static void stv0288_release(struct dvb_frontend *fe)
{
	struct stv0288_state *state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops stv0288_ops = {
	.delsys = { SYS_DVBS },
	.info = {
		.name			= "ST STV0288 DVB-S",
		.frequency_min		= 950000,
		.frequency_max		= 2150000,
		.frequency_stepsize	= 1000,	 /* kHz for QPSK frontends */
		.frequency_tolerance	= 0,
		.symbol_rate_min	= 1000000,
		.symbol_rate_max	= 45000000,
		.symbol_rate_tolerance	= 500,	/* ppm */
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		      FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 |
		      FE_CAN_QPSK |
		      FE_CAN_FEC_AUTO
	},

	.release = stv0288_release,
	.init = stv0288_init,
	.sleep = stv0288_sleep,
	.write = stv0288_write,
	.i2c_gate_ctrl = stv0288_i2c_gate_ctrl,
	.read_status = stv0288_read_status,
	.read_ber = stv0288_read_ber,
	.read_signal_strength = stv0288_read_signal_strength,
	.read_snr = stv0288_read_snr,
	.read_ucblocks = stv0288_read_ucblocks,
	.diseqc_send_master_cmd = stv0288_send_diseqc_msg,
	.diseqc_send_burst = stv0288_send_diseqc_burst,
	.set_tone = stv0288_set_tone,
	.set_voltage = stv0288_set_voltage,

	.set_property = stv0288_set_property,
	.set_frontend = stv0288_set_frontend,
};

struct dvb_frontend *stv0288_attach(const struct stv0288_config *config,
				    struct i2c_adapter *i2c)
{
	struct stv0288_state *state = NULL;
	int id;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct stv0288_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	state->initialised = 0;
	state->tuner_frequency = 0;
	state->symbol_rate = 0;
	state->fec_inner = 0;
	state->errmode = STATUS_BER;

	stv0288_writeregI(state, 0x41, 0x04);
	msleep(200);
	id = stv0288_readreg(state, 0x00);
	dprintk("stv0288 id %x\n", id);

	/* register 0x00 contains 0x11 for STV0288  */
	if (id != 0x11)
		goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &stv0288_ops,
			sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);

	return NULL;
}
EXPORT_SYMBOL(stv0288_attach);

module_param(debug_legacy_dish_switch, int, 0444);
MODULE_PARM_DESC(debug_legacy_dish_switch,
		"Enable timing analysis for Dish Network legacy switches");

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("ST STV0288 DVB Demodulator driver");
MODULE_AUTHOR("Georg Acher, Bob Liu, Igor liplianin");
MODULE_LICENSE("GPL");

