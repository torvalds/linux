/*
    Driver for Philips TDA8083 based QPSK Demodulator

    Copyright (C) 2001 Convergence Integrated Media GmbH

    written by Ralph Metzler <ralph@convergence.de>

    adoption to the new DVB frontend API and diagnostic ioctl's
    by Holger Waechtler <holger@convergence.de>

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
#include "dvb_frontend.h"
#include "tda8083.h"


struct tda8083_state {
	struct i2c_adapter* i2c;
	/* configuration settings */
	const struct tda8083_config* config;
	struct dvb_frontend frontend;
};

static int debug;
#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG "tda8083: " args); \
	} while (0)


static u8 tda8083_init_tab [] = {
	0x04, 0x00, 0x4a, 0x79, 0x04, 0x00, 0xff, 0xea,
	0x48, 0x42, 0x79, 0x60, 0x70, 0x52, 0x9a, 0x10,
	0x0e, 0x10, 0xf2, 0xa7, 0x93, 0x0b, 0x05, 0xc8,
	0x9d, 0x00, 0x42, 0x80, 0x00, 0x60, 0x40, 0x00,
	0x00, 0x75, 0x00, 0xe0, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00
};


static int tda8083_writereg (struct tda8083_state* state, u8 reg, u8 data)
{
	int ret;
	u8 buf [] = { reg, data };
	struct i2c_msg msg = { .addr = state->config->demod_address, .flags = 0, .buf = buf, .len = 2 };

	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1)
		dprintk ("%s: writereg error (reg %02x, ret == %i)\n",
			__func__, reg, ret);

	return (ret != 1) ? -1 : 0;
}

static int tda8083_readregs (struct tda8083_state* state, u8 reg1, u8 *b, u8 len)
{
	int ret;
	struct i2c_msg msg [] = { { .addr = state->config->demod_address, .flags = 0, .buf = &reg1, .len = 1 },
			   { .addr = state->config->demod_address, .flags = I2C_M_RD, .buf = b, .len = len } };

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2)
		dprintk ("%s: readreg error (reg %02x, ret == %i)\n",
			__func__, reg1, ret);

	return ret == 2 ? 0 : -1;
}

static inline u8 tda8083_readreg (struct tda8083_state* state, u8 reg)
{
	u8 val;

	tda8083_readregs (state, reg, &val, 1);

	return val;
}

static int tda8083_set_inversion (struct tda8083_state* state, fe_spectral_inversion_t inversion)
{
	/*  XXX FIXME: implement other modes than FEC_AUTO */
	if (inversion == INVERSION_AUTO)
		return 0;

	return -EINVAL;
}

static int tda8083_set_fec (struct tda8083_state* state, fe_code_rate_t fec)
{
	if (fec == FEC_AUTO)
		return tda8083_writereg (state, 0x07, 0xff);

	if (fec >= FEC_1_2 && fec <= FEC_8_9)
		return tda8083_writereg (state, 0x07, 1 << (FEC_8_9 - fec));

	return -EINVAL;
}

static fe_code_rate_t tda8083_get_fec (struct tda8083_state* state)
{
	u8 index;
	static fe_code_rate_t fec_tab [] = { FEC_8_9, FEC_1_2, FEC_2_3, FEC_3_4,
				       FEC_4_5, FEC_5_6, FEC_6_7, FEC_7_8 };

	index = tda8083_readreg(state, 0x0e) & 0x07;

	return fec_tab [index];
}

static int tda8083_set_symbolrate (struct tda8083_state* state, u32 srate)
{
	u32 ratio;
	u32 tmp;
	u8 filter;

	if (srate > 32000000)
		srate = 32000000;
	if (srate < 500000)
		srate = 500000;

	filter = 0;
	if (srate < 24000000)
		filter = 2;
	if (srate < 16000000)
		filter = 3;

	tmp = 31250 << 16;
	ratio = tmp / srate;

	tmp = (tmp % srate) << 8;
	ratio = (ratio << 8) + tmp / srate;

	tmp = (tmp % srate) << 8;
	ratio = (ratio << 8) + tmp / srate;

	dprintk("tda8083: ratio == %08x\n", (unsigned int) ratio);

	tda8083_writereg (state, 0x05, filter);
	tda8083_writereg (state, 0x02, (ratio >> 16) & 0xff);
	tda8083_writereg (state, 0x03, (ratio >>  8) & 0xff);
	tda8083_writereg (state, 0x04, (ratio      ) & 0xff);

	tda8083_writereg (state, 0x00, 0x3c);
	tda8083_writereg (state, 0x00, 0x04);

	return 1;
}

static void tda8083_wait_diseqc_fifo (struct tda8083_state* state, int timeout)
{
	unsigned long start = jiffies;

	while (jiffies - start < timeout &&
	       !(tda8083_readreg(state, 0x02) & 0x80))
	{
		msleep(50);
	}
}

static int tda8083_set_tone (struct tda8083_state* state, fe_sec_tone_mode_t tone)
{
	tda8083_writereg (state, 0x26, 0xf1);

	switch (tone) {
	case SEC_TONE_OFF:
		return tda8083_writereg (state, 0x29, 0x00);
	case SEC_TONE_ON:
		return tda8083_writereg (state, 0x29, 0x80);
	default:
		return -EINVAL;
	}
}

static int tda8083_set_voltage (struct tda8083_state* state, fe_sec_voltage_t voltage)
{
	switch (voltage) {
	case SEC_VOLTAGE_13:
		return tda8083_writereg (state, 0x20, 0x00);
	case SEC_VOLTAGE_18:
		return tda8083_writereg (state, 0x20, 0x11);
	default:
		return -EINVAL;
	}
}

static int tda8083_send_diseqc_burst (struct tda8083_state* state, fe_sec_mini_cmd_t burst)
{
	switch (burst) {
	case SEC_MINI_A:
		tda8083_writereg (state, 0x29, (5 << 2));  /* send burst A */
		break;
	case SEC_MINI_B:
		tda8083_writereg (state, 0x29, (7 << 2));  /* send B */
		break;
	default:
		return -EINVAL;
	}

	tda8083_wait_diseqc_fifo (state, 100);

	return 0;
}

static int tda8083_send_diseqc_msg (struct dvb_frontend* fe,
				    struct dvb_diseqc_master_cmd *m)
{
	struct tda8083_state* state = fe->demodulator_priv;
	int i;

	tda8083_writereg (state, 0x29, (m->msg_len - 3) | (1 << 2)); /* enable */

	for (i=0; i<m->msg_len; i++)
		tda8083_writereg (state, 0x23 + i, m->msg[i]);

	tda8083_writereg (state, 0x29, (m->msg_len - 3) | (3 << 2)); /* send!! */

	tda8083_wait_diseqc_fifo (state, 100);

	return 0;
}

static int tda8083_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct tda8083_state* state = fe->demodulator_priv;

	u8 signal = ~tda8083_readreg (state, 0x01);
	u8 sync = tda8083_readreg (state, 0x02);

	*status = 0;

	if (signal > 10)
		*status |= FE_HAS_SIGNAL;

	if (sync & 0x01)
		*status |= FE_HAS_CARRIER;

	if (sync & 0x02)
		*status |= FE_HAS_VITERBI;

	if (sync & 0x10)
		*status |= FE_HAS_SYNC;

	if (sync & 0x20) /* frontend can not lock */
		*status |= FE_TIMEDOUT;

	if ((sync & 0x1f) == 0x1f)
		*status |= FE_HAS_LOCK;

	return 0;
}

static int tda8083_read_ber(struct dvb_frontend* fe, u32* ber)
{
	struct tda8083_state* state = fe->demodulator_priv;
	int ret;
	u8 buf[3];

	if ((ret = tda8083_readregs(state, 0x0b, buf, sizeof(buf))))
		return ret;

	*ber = ((buf[0] & 0x1f) << 16) | (buf[1] << 8) | buf[2];

	return 0;
}

static int tda8083_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	struct tda8083_state* state = fe->demodulator_priv;

	u8 signal = ~tda8083_readreg (state, 0x01);
	*strength = (signal << 8) | signal;

	return 0;
}

static int tda8083_read_snr(struct dvb_frontend* fe, u16* snr)
{
	struct tda8083_state* state = fe->demodulator_priv;

	u8 _snr = tda8083_readreg (state, 0x08);
	*snr = (_snr << 8) | _snr;

	return 0;
}

static int tda8083_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct tda8083_state* state = fe->demodulator_priv;

	*ucblocks = tda8083_readreg(state, 0x0f);
	if (*ucblocks == 0xff)
		*ucblocks = 0xffffffff;

	return 0;
}

static int tda8083_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct tda8083_state* state = fe->demodulator_priv;

	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe);
		if (fe->ops.i2c_gate_ctrl) fe->ops.i2c_gate_ctrl(fe, 0);
	}

	tda8083_set_inversion (state, p->inversion);
	tda8083_set_fec(state, p->fec_inner);
	tda8083_set_symbolrate(state, p->symbol_rate);

	tda8083_writereg (state, 0x00, 0x3c);
	tda8083_writereg (state, 0x00, 0x04);

	return 0;
}

static int tda8083_get_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct tda8083_state* state = fe->demodulator_priv;

	/*  FIXME: get symbolrate & frequency offset...*/
	/*p->frequency = ???;*/
	p->inversion = (tda8083_readreg (state, 0x0e) & 0x80) ?
			INVERSION_ON : INVERSION_OFF;
	p->fec_inner = tda8083_get_fec(state);
	/*p->symbol_rate = tda8083_get_symbolrate (state);*/

	return 0;
}

static int tda8083_sleep(struct dvb_frontend* fe)
{
	struct tda8083_state* state = fe->demodulator_priv;

	tda8083_writereg (state, 0x00, 0x02);
	return 0;
}

static int tda8083_init(struct dvb_frontend* fe)
{
	struct tda8083_state* state = fe->demodulator_priv;
	int i;

	for (i=0; i<44; i++)
		tda8083_writereg (state, i, tda8083_init_tab[i]);

	tda8083_writereg (state, 0x00, 0x3c);
	tda8083_writereg (state, 0x00, 0x04);

	return 0;
}

static int tda8083_diseqc_send_burst(struct dvb_frontend* fe, fe_sec_mini_cmd_t burst)
{
	struct tda8083_state* state = fe->demodulator_priv;

	tda8083_send_diseqc_burst (state, burst);
	tda8083_writereg (state, 0x00, 0x3c);
	tda8083_writereg (state, 0x00, 0x04);

	return 0;
}

static int tda8083_diseqc_set_tone(struct dvb_frontend* fe, fe_sec_tone_mode_t tone)
{
	struct tda8083_state* state = fe->demodulator_priv;

	tda8083_set_tone (state, tone);
	tda8083_writereg (state, 0x00, 0x3c);
	tda8083_writereg (state, 0x00, 0x04);

	return 0;
}

static int tda8083_diseqc_set_voltage(struct dvb_frontend* fe, fe_sec_voltage_t voltage)
{
	struct tda8083_state* state = fe->demodulator_priv;

	tda8083_set_voltage (state, voltage);
	tda8083_writereg (state, 0x00, 0x3c);
	tda8083_writereg (state, 0x00, 0x04);

	return 0;
}

static void tda8083_release(struct dvb_frontend* fe)
{
	struct tda8083_state* state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops tda8083_ops;

struct dvb_frontend* tda8083_attach(const struct tda8083_config* config,
				    struct i2c_adapter* i2c)
{
	struct tda8083_state* state = NULL;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct tda8083_state), GFP_KERNEL);
	if (state == NULL) goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;

	/* check if the demod is there */
	if ((tda8083_readreg(state, 0x00)) != 0x05) goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &tda8083_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops tda8083_ops = {
	.delsys = { SYS_DVBS },
	.info = {
		.name			= "Philips TDA8083 DVB-S",
		.frequency_min		= 920000,     /* TDA8060 */
		.frequency_max		= 2200000,    /* TDA8060 */
		.frequency_stepsize	= 125,   /* kHz for QPSK frontends */
	/*      .frequency_tolerance	= ???,*/
		.symbol_rate_min	= 12000000,
		.symbol_rate_max	= 30000000,
	/*      .symbol_rate_tolerance	= ???,*/
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_4_5 | FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 |
			FE_CAN_FEC_7_8 | FE_CAN_FEC_8_9 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK | FE_CAN_MUTE_TS
	},

	.release = tda8083_release,

	.init = tda8083_init,
	.sleep = tda8083_sleep,

	.set_frontend = tda8083_set_frontend,
	.get_frontend = tda8083_get_frontend,

	.read_status = tda8083_read_status,
	.read_signal_strength = tda8083_read_signal_strength,
	.read_snr = tda8083_read_snr,
	.read_ber = tda8083_read_ber,
	.read_ucblocks = tda8083_read_ucblocks,

	.diseqc_send_master_cmd = tda8083_send_diseqc_msg,
	.diseqc_send_burst = tda8083_diseqc_send_burst,
	.set_tone = tda8083_diseqc_set_tone,
	.set_voltage = tda8083_diseqc_set_voltage,
};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("Philips TDA8083 DVB-S Demodulator");
MODULE_AUTHOR("Ralph Metzler, Holger Waechtler");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(tda8083_attach);
