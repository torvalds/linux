/*
    Conexant cx22700 DVB OFDM demodulator driver

    Copyright (C) 2001-2002 Convergence Integrated Media GmbH
	Holger Waechtler <holger@convergence.de>

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
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/slab.h>
#include "dvb_frontend.h"
#include "cx22700.h"


struct cx22700_state {

	struct i2c_adapter* i2c;

	const struct cx22700_config* config;

	struct dvb_frontend frontend;
};


static int debug;
#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG "cx22700: " args); \
	} while (0)

static u8 init_tab [] = {
	0x04, 0x10,
	0x05, 0x09,
	0x06, 0x00,
	0x08, 0x04,
	0x09, 0x00,
	0x0a, 0x01,
	0x15, 0x40,
	0x16, 0x10,
	0x17, 0x87,
	0x18, 0x17,
	0x1a, 0x10,
	0x25, 0x04,
	0x2e, 0x00,
	0x39, 0x00,
	0x3a, 0x04,
	0x45, 0x08,
	0x46, 0x02,
	0x47, 0x05,
};


static int cx22700_writereg (struct cx22700_state* state, u8 reg, u8 data)
{
	int ret;
	u8 buf [] = { reg, data };
	struct i2c_msg msg = { .addr = state->config->demod_address, .flags = 0, .buf = buf, .len = 2 };

	dprintk ("%s\n", __FUNCTION__);

	ret = i2c_transfer (state->i2c, &msg, 1);

	if (ret != 1)
		printk("%s: writereg error (reg == 0x%02x, val == 0x%02x, ret == %i)\n",
			__FUNCTION__, reg, data, ret);

	return (ret != 1) ? -1 : 0;
}

static int cx22700_readreg (struct cx22700_state* state, u8 reg)
{
	int ret;
	u8 b0 [] = { reg };
	u8 b1 [] = { 0 };
	struct i2c_msg msg [] = { { .addr = state->config->demod_address, .flags = 0, .buf = b0, .len = 1 },
			   { .addr = state->config->demod_address, .flags = I2C_M_RD, .buf = b1, .len = 1 } };

	dprintk ("%s\n", __FUNCTION__);

	ret = i2c_transfer (state->i2c, msg, 2);

	if (ret != 2) return -EIO;

	return b1[0];
}

static int cx22700_set_inversion (struct cx22700_state* state, int inversion)
{
	u8 val;

	dprintk ("%s\n", __FUNCTION__);

	switch (inversion) {
	case INVERSION_AUTO:
		return -EOPNOTSUPP;
	case INVERSION_ON:
		val = cx22700_readreg (state, 0x09);
		return cx22700_writereg (state, 0x09, val | 0x01);
	case INVERSION_OFF:
		val = cx22700_readreg (state, 0x09);
		return cx22700_writereg (state, 0x09, val & 0xfe);
	default:
		return -EINVAL;
	}
}

static int cx22700_set_tps (struct cx22700_state *state, struct dvb_ofdm_parameters *p)
{
	static const u8 qam_tab [4] = { 0, 1, 0, 2 };
	static const u8 fec_tab [6] = { 0, 1, 2, 0, 3, 4 };
	u8 val;

	dprintk ("%s\n", __FUNCTION__);

	if (p->code_rate_HP < FEC_1_2 || p->code_rate_HP > FEC_7_8)
		return -EINVAL;

	if (p->code_rate_LP < FEC_1_2 || p->code_rate_LP > FEC_7_8)
		return -EINVAL;

	if (p->code_rate_HP == FEC_4_5 || p->code_rate_LP == FEC_4_5)
		return -EINVAL;

	if (p->guard_interval < GUARD_INTERVAL_1_32 ||
	    p->guard_interval > GUARD_INTERVAL_1_4)
		return -EINVAL;

	if (p->transmission_mode != TRANSMISSION_MODE_2K &&
	    p->transmission_mode != TRANSMISSION_MODE_8K)
		return -EINVAL;

	if (p->constellation != QPSK &&
	    p->constellation != QAM_16 &&
	    p->constellation != QAM_64)
		return -EINVAL;

	if (p->hierarchy_information < HIERARCHY_NONE ||
	    p->hierarchy_information > HIERARCHY_4)
		return -EINVAL;

	if (p->bandwidth < BANDWIDTH_8_MHZ && p->bandwidth > BANDWIDTH_6_MHZ)
		return -EINVAL;

	if (p->bandwidth == BANDWIDTH_7_MHZ)
		cx22700_writereg (state, 0x09, cx22700_readreg (state, 0x09 | 0x10));
	else
		cx22700_writereg (state, 0x09, cx22700_readreg (state, 0x09 & ~0x10));

	val = qam_tab[p->constellation - QPSK];
	val |= p->hierarchy_information - HIERARCHY_NONE;

	cx22700_writereg (state, 0x04, val);

	val = fec_tab[p->code_rate_HP - FEC_1_2] << 3;
	val |= fec_tab[p->code_rate_LP - FEC_1_2];

	cx22700_writereg (state, 0x05, val);

	val = (p->guard_interval - GUARD_INTERVAL_1_32) << 2;
	val |= p->transmission_mode - TRANSMISSION_MODE_2K;

	cx22700_writereg (state, 0x06, val);

	cx22700_writereg (state, 0x08, 0x04 | 0x02);  /* use user tps parameters */
	cx22700_writereg (state, 0x08, 0x04);         /* restart aquisition */

	return 0;
}

static int cx22700_get_tps (struct cx22700_state* state, struct dvb_ofdm_parameters *p)
{
	static const fe_modulation_t qam_tab [3] = { QPSK, QAM_16, QAM_64 };
	static const fe_code_rate_t fec_tab [5] = { FEC_1_2, FEC_2_3, FEC_3_4,
						    FEC_5_6, FEC_7_8 };
	u8 val;

	dprintk ("%s\n", __FUNCTION__);

	if (!(cx22700_readreg(state, 0x07) & 0x20))  /*  tps valid? */
		return -EAGAIN;

	val = cx22700_readreg (state, 0x01);

	if ((val & 0x7) > 4)
		p->hierarchy_information = HIERARCHY_AUTO;
	else
		p->hierarchy_information = HIERARCHY_NONE + (val & 0x7);

	if (((val >> 3) & 0x3) > 2)
		p->constellation = QAM_AUTO;
	else
		p->constellation = qam_tab[(val >> 3) & 0x3];

	val = cx22700_readreg (state, 0x02);

	if (((val >> 3) & 0x07) > 4)
		p->code_rate_HP = FEC_AUTO;
	else
		p->code_rate_HP = fec_tab[(val >> 3) & 0x07];

	if ((val & 0x07) > 4)
		p->code_rate_LP = FEC_AUTO;
	else
		p->code_rate_LP = fec_tab[val & 0x07];

	val = cx22700_readreg (state, 0x03);

	p->guard_interval = GUARD_INTERVAL_1_32 + ((val >> 6) & 0x3);
	p->transmission_mode = TRANSMISSION_MODE_2K + ((val >> 5) & 0x1);

	return 0;
}

static int cx22700_init (struct dvb_frontend* fe)

{	struct cx22700_state* state = fe->demodulator_priv;
	int i;

	dprintk("cx22700_init: init chip\n");

	cx22700_writereg (state, 0x00, 0x02);   /*  soft reset */
	cx22700_writereg (state, 0x00, 0x00);

	msleep(10);

	for (i=0; i<sizeof(init_tab); i+=2)
		cx22700_writereg (state, init_tab[i], init_tab[i+1]);

	cx22700_writereg (state, 0x00, 0x01);

	return 0;
}

static int cx22700_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct cx22700_state* state = fe->demodulator_priv;

	u16 rs_ber = (cx22700_readreg (state, 0x0d) << 9)
		   | (cx22700_readreg (state, 0x0e) << 1);
	u8 sync = cx22700_readreg (state, 0x07);

	*status = 0;

	if (rs_ber < 0xff00)
		*status |= FE_HAS_SIGNAL;

	if (sync & 0x20)
		*status |= FE_HAS_CARRIER;

	if (sync & 0x10)
		*status |= FE_HAS_VITERBI;

	if (sync & 0x10)
		*status |= FE_HAS_SYNC;

	if (*status == 0x0f)
		*status |= FE_HAS_LOCK;

	return 0;
}

static int cx22700_read_ber(struct dvb_frontend* fe, u32* ber)
{
	struct cx22700_state* state = fe->demodulator_priv;

	*ber = cx22700_readreg (state, 0x0c) & 0x7f;
	cx22700_writereg (state, 0x0c, 0x00);

	return 0;
}

static int cx22700_read_signal_strength(struct dvb_frontend* fe, u16* signal_strength)
{
	struct cx22700_state* state = fe->demodulator_priv;

	u16 rs_ber = (cx22700_readreg (state, 0x0d) << 9)
		   | (cx22700_readreg (state, 0x0e) << 1);
	*signal_strength = ~rs_ber;

	return 0;
}

static int cx22700_read_snr(struct dvb_frontend* fe, u16* snr)
{
	struct cx22700_state* state = fe->demodulator_priv;

	u16 rs_ber = (cx22700_readreg (state, 0x0d) << 9)
		   | (cx22700_readreg (state, 0x0e) << 1);
	*snr = ~rs_ber;

	return 0;
}

static int cx22700_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct cx22700_state* state = fe->demodulator_priv;

	*ucblocks = cx22700_readreg (state, 0x0f);
	cx22700_writereg (state, 0x0f, 0x00);

	return 0;
}

static int cx22700_set_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters *p)
{
	struct cx22700_state* state = fe->demodulator_priv;

	cx22700_writereg (state, 0x00, 0x02); /* XXX CHECKME: soft reset*/
	cx22700_writereg (state, 0x00, 0x00);

	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe, p);
		if (fe->ops.i2c_gate_ctrl) fe->ops.i2c_gate_ctrl(fe, 0);
	}

	cx22700_set_inversion (state, p->inversion);
	cx22700_set_tps (state, &p->u.ofdm);
	cx22700_writereg (state, 0x37, 0x01);  /* PAL loop filter off */
	cx22700_writereg (state, 0x00, 0x01);  /* restart acquire */

	return 0;
}

static int cx22700_get_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters *p)
{
	struct cx22700_state* state = fe->demodulator_priv;
	u8 reg09 = cx22700_readreg (state, 0x09);

	p->inversion = reg09 & 0x1 ? INVERSION_ON : INVERSION_OFF;
	return cx22700_get_tps (state, &p->u.ofdm);
}

static int cx22700_i2c_gate_ctrl(struct dvb_frontend* fe, int enable)
{
	struct cx22700_state* state = fe->demodulator_priv;

	if (enable) {
		return cx22700_writereg(state, 0x0a, 0x00);
	} else {
		return cx22700_writereg(state, 0x0a, 0x01);
	}
}

static int cx22700_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings* fesettings)
{
	fesettings->min_delay_ms = 150;
	fesettings->step_size = 166667;
	fesettings->max_drift = 166667*2;
	return 0;
}

static void cx22700_release(struct dvb_frontend* fe)
{
	struct cx22700_state* state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops cx22700_ops;

struct dvb_frontend* cx22700_attach(const struct cx22700_config* config,
				    struct i2c_adapter* i2c)
{
	struct cx22700_state* state = NULL;

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct cx22700_state), GFP_KERNEL);
	if (state == NULL) goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;

	/* check if the demod is there */
	if (cx22700_readreg(state, 0x07) < 0) goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &cx22700_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops cx22700_ops = {

	.info = {
		.name			= "Conexant CX22700 DVB-T",
		.type			= FE_OFDM,
		.frequency_min		= 470000000,
		.frequency_max		= 860000000,
		.frequency_stepsize	= 166667,
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		      FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
		      FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 |
		      FE_CAN_RECOVER
	},

	.release = cx22700_release,

	.init = cx22700_init,
	.i2c_gate_ctrl = cx22700_i2c_gate_ctrl,

	.set_frontend = cx22700_set_frontend,
	.get_frontend = cx22700_get_frontend,
	.get_tune_settings = cx22700_get_tune_settings,

	.read_status = cx22700_read_status,
	.read_ber = cx22700_read_ber,
	.read_signal_strength = cx22700_read_signal_strength,
	.read_snr = cx22700_read_snr,
	.read_ucblocks = cx22700_read_ucblocks,
};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("Conexant CX22700 DVB-T Demodulator driver");
MODULE_AUTHOR("Holger Waechtler");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(cx22700_attach);
