/*
    Auvitek AU8522 QAM/8VSB demodulator driver

    Copyright (C) 2008 Steven Toth <stoth@hauppauge.com>

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
#include "dvb-pll.h"
#include "au8522.h"

struct au8522_state {

	struct i2c_adapter* i2c;

	/* configuration settings */
	const struct au8522_config* config;

	struct dvb_frontend frontend;

	u32 current_frequency;
	fe_modulation_t current_modulation;

};

static int debug = 0;
#define dprintk	if (debug) printk

/* 16 bit registers, 8 bit values */
static int au8522_writereg(struct au8522_state* state, u16 reg, u8 data)
{
	int ret;
	u8 buf [] = { reg >> 8, reg & 0xff, data };

	struct i2c_msg msg = { .addr = state->config->demod_address,
			       .flags = 0, .buf = buf, .len = 3 };

	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1)
		printk("%s: writereg error (reg == 0x%02x, val == 0x%04x, "
		       "ret == %i)\n", __FUNCTION__, reg, data, ret);

	return (ret != 1) ? -1 : 0;
}

static u8 au8522_readreg(struct au8522_state* state, u16 reg)
{
	int ret;
	u8 b0 [] = { reg >> 8, reg & 0xff };
	u8 b1 [] = { 0 };

	struct i2c_msg msg [] = {
		{ .addr = state->config->demod_address, .flags = 0,
		  .buf = b0, .len = 2 },
		{ .addr = state->config->demod_address, .flags = I2C_M_RD,
		  .buf = b1, .len = 1 } };

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2)
		printk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);
	return b1[0];
}

static int au8522_i2c_gate_ctrl(struct dvb_frontend* fe, int enable)
{
	struct au8522_state* state = fe->demodulator_priv;

	dprintk("%s(%d)\n", __FUNCTION__, enable);

	if (enable)
		return au8522_writereg(state, 0x106, 1);
	else
		return au8522_writereg(state, 0x106, 0);
}

static int au8522_enable_modulation(struct dvb_frontend* fe,
	fe_modulation_t m)
{
	struct au8522_state* state = fe->demodulator_priv;

	dprintk("%s(0x%08x)\n", __FUNCTION__, m);

	switch(m) {
	case VSB_8:
		dprintk("%s() VSB_8\n", __FUNCTION__);

		//au8522_writereg(state, 0x410b, 0x84); // Serial

		//au8522_writereg(state, 0x8090, 0x82);
		au8522_writereg(state, 0x8090, 0x84);
		au8522_writereg(state, 0x4092, 0x11);
		au8522_writereg(state, 0x2005, 0x00);
		au8522_writereg(state, 0x8091, 0x80);

		au8522_writereg(state, 0x80a3, 0x0c);
		au8522_writereg(state, 0x80a4, 0xe8);
		au8522_writereg(state, 0x8081, 0xc4);
		au8522_writereg(state, 0x80a5, 0x40);
		au8522_writereg(state, 0x80a7, 0x40);
		au8522_writereg(state, 0x80a6, 0x67);
		au8522_writereg(state, 0x8262, 0x20);
		au8522_writereg(state, 0x821c, 0x30);
		au8522_writereg(state, 0x80d8, 0x1a);
		au8522_writereg(state, 0x8227, 0xa0);
		au8522_writereg(state, 0x8121, 0xff);
		au8522_writereg(state, 0x80a8, 0xf0);
		au8522_writereg(state, 0x80a9, 0x05);
		au8522_writereg(state, 0x80aa, 0x77);
		au8522_writereg(state, 0x80ab, 0xf0);
		au8522_writereg(state, 0x80ac, 0x05);
		au8522_writereg(state, 0x80ad, 0x77);
		au8522_writereg(state, 0x80ae, 0x41);
		au8522_writereg(state, 0x80af, 0x66);
		au8522_writereg(state, 0x821b, 0xcc);
		au8522_writereg(state, 0x821d, 0x80);
		au8522_writereg(state, 0x80b5, 0xfb);
		au8522_writereg(state, 0x80b6, 0x8e);
		au8522_writereg(state, 0x80b7, 0x39);
		au8522_writereg(state, 0x80a4, 0xe8);
		au8522_writereg(state, 0x8231, 0x13);
		break;
	case QAM_64:
	case QAM_256:
		au8522_writereg(state, 0x80a3, 0x09);
		au8522_writereg(state, 0x80a4, 0x00);
		au8522_writereg(state, 0x8081, 0xc4);
		au8522_writereg(state, 0x80a5, 0x40);
		au8522_writereg(state, 0x80b5, 0xfb);
		au8522_writereg(state, 0x80b6, 0x8e);
		au8522_writereg(state, 0x80b7, 0x39);
		au8522_writereg(state, 0x80aa, 0x77);
		au8522_writereg(state, 0x80ad, 0x77);
		au8522_writereg(state, 0x80a6, 0x67);
		au8522_writereg(state, 0x8262, 0x20);
		au8522_writereg(state, 0x821c, 0x30);
		au8522_writereg(state, 0x80b8, 0x3e);
		au8522_writereg(state, 0x80b9, 0xf0);
		au8522_writereg(state, 0x80ba, 0x01);
		au8522_writereg(state, 0x80bb, 0x18);
		au8522_writereg(state, 0x80bc, 0x50);
		au8522_writereg(state, 0x80bd, 0x00);
		au8522_writereg(state, 0x80be, 0xea);
		au8522_writereg(state, 0x80bf, 0xef);
		au8522_writereg(state, 0x80c0, 0xfc);
		au8522_writereg(state, 0x80c1, 0xbd);
		au8522_writereg(state, 0x80c2, 0x1f);
		au8522_writereg(state, 0x80c3, 0xfc);
		au8522_writereg(state, 0x80c4, 0xdd);
		au8522_writereg(state, 0x80c5, 0xaf);
		au8522_writereg(state, 0x80c6, 0x00);
		au8522_writereg(state, 0x80c7, 0x38);
		au8522_writereg(state, 0x80c8, 0x30);
		au8522_writereg(state, 0x80c9, 0x05);
		au8522_writereg(state, 0x80ca, 0x4a);
		au8522_writereg(state, 0x80cb, 0xd0);
		au8522_writereg(state, 0x80cc, 0x01);
		au8522_writereg(state, 0x80cd, 0xd9);
		au8522_writereg(state, 0x80ce, 0x6f);
		au8522_writereg(state, 0x80cf, 0xf9);
		au8522_writereg(state, 0x80d0, 0x70);
		au8522_writereg(state, 0x80d1, 0xdf);
		au8522_writereg(state, 0x80d2, 0xf7);
		au8522_writereg(state, 0x80d3, 0xc2);
		au8522_writereg(state, 0x80d4, 0xdf);
		au8522_writereg(state, 0x80d5, 0x02);
		au8522_writereg(state, 0x80d6, 0x9a);
		au8522_writereg(state, 0x80d7, 0xd0);
		au8522_writereg(state, 0x8250, 0x0d);
		au8522_writereg(state, 0x8251, 0xcd);
		au8522_writereg(state, 0x8252, 0xe0);
		au8522_writereg(state, 0x8253, 0x05);
		au8522_writereg(state, 0x8254, 0xa7);
		au8522_writereg(state, 0x8255, 0xff);
		au8522_writereg(state, 0x8256, 0xed);
		au8522_writereg(state, 0x8257, 0x5b);
		au8522_writereg(state, 0x8258, 0xae);
		au8522_writereg(state, 0x8259, 0xe6);
		au8522_writereg(state, 0x825a, 0x3d);
		au8522_writereg(state, 0x825b, 0x0f);
		au8522_writereg(state, 0x825c, 0x0d);
		au8522_writereg(state, 0x825d, 0xea);
		au8522_writereg(state, 0x825e, 0xf2);
		au8522_writereg(state, 0x825f, 0x51);
		au8522_writereg(state, 0x8260, 0xf5);
		au8522_writereg(state, 0x8261, 0x06);
		au8522_writereg(state, 0x821a, 0x00);
		au8522_writereg(state, 0x8546, 0x40);
		au8522_writereg(state, 0x8210, 0x26);
		au8522_writereg(state, 0x8211, 0xf6);
		au8522_writereg(state, 0x8212, 0x84);
		au8522_writereg(state, 0x8213, 0x02);
		au8522_writereg(state, 0x8502, 0x01);
		au8522_writereg(state, 0x8121, 0x04);
		au8522_writereg(state, 0x8122, 0x04);
		au8522_writereg(state, 0x852e, 0x10);
		au8522_writereg(state, 0x80a4, 0xca);
		au8522_writereg(state, 0x80a7, 0x40);
		au8522_writereg(state, 0x8526, 0x01);
		break;
	default:
		dprintk("%s() Invalid modulation\n", __FUNCTION__);
		return -EINVAL;
	}

	state->current_modulation = m;

	return 0;
}

/* Talk to the demod, set the FEC, GUARD, QAM settings etc */
static int au8522_set_frontend (struct dvb_frontend* fe,
	struct dvb_frontend_parameters *p)
{
	struct au8522_state* state = fe->demodulator_priv;

	dprintk("%s(frequency=%d)\n", __FUNCTION__, p->frequency);

	state->current_frequency = p->frequency;

	au8522_enable_modulation(fe, p->u.vsb.modulation);

	/* Allow the demod to settle */
	msleep(100);

	if (fe->ops.tuner_ops.set_params) {
		if (fe->ops.i2c_gate_ctrl) fe->ops.i2c_gate_ctrl(fe, 1);
		fe->ops.tuner_ops.set_params(fe, p);
		if (fe->ops.i2c_gate_ctrl) fe->ops.i2c_gate_ctrl(fe, 0);
	}

	return 0;
}

/* Reset the demod hardware and reset all of the configuration registers
   to a default state. */
static int au8522_init(struct dvb_frontend* fe)
{
	struct au8522_state* state = fe->demodulator_priv;
	dprintk("%s()\n", __FUNCTION__);

	au8522_writereg(state, 0xa4, 1 << 5);

	au8522_i2c_gate_ctrl(fe, 1);

	return 0;
}

static int au8522_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct au8522_state* state = fe->demodulator_priv;
	u8 reg;
	u32 tuner_status = 0;

	*status = 0;

	if (state->current_modulation == VSB_8) {
		dprintk("%s() Checking VSB_8\n", __FUNCTION__);
		//au8522_writereg(state, 0x80a4, 0x20);
		reg = au8522_readreg(state, 0x4088);
		if(reg & 0x01)
			*status |= FE_HAS_VITERBI;
		if(reg & 0x02)
			*status |= FE_HAS_LOCK | FE_HAS_SYNC;
	} else {
		dprintk("%s() Checking QAM\n", __FUNCTION__);
		reg = au8522_readreg(state, 0x4541);
		if(reg & 0x80)
			*status |= FE_HAS_VITERBI;
		if(reg & 0x20)
			*status |= FE_HAS_LOCK | FE_HAS_SYNC;
	}

	switch(state->config->status_mode) {
	case AU8522_DEMODLOCKING:
		dprintk("%s() DEMODLOCKING\n", __FUNCTION__);
		if (*status & FE_HAS_VITERBI)
			*status |= FE_HAS_CARRIER | FE_HAS_SIGNAL;
		break;
	case AU8522_TUNERLOCKING:
		/* Get the tuner status */
		dprintk("%s() TUNERLOCKING\n", __FUNCTION__);
		if (fe->ops.tuner_ops.get_status) {
			if (fe->ops.i2c_gate_ctrl)
				fe->ops.i2c_gate_ctrl(fe, 1);

			fe->ops.tuner_ops.get_status(fe, &tuner_status);

			if (fe->ops.i2c_gate_ctrl)
				fe->ops.i2c_gate_ctrl(fe, 0);
		}
		if (tuner_status)
			*status |= FE_HAS_CARRIER | FE_HAS_SIGNAL;
		break;
	}

	dprintk("%s() status 0x%08x\n", __FUNCTION__, *status);

	return 0;
}

static int au8522_read_snr(struct dvb_frontend* fe, u16* snr)
{
	dprintk("%s()\n", __FUNCTION__);

	*snr = 0;

	return 0;
}

static int au8522_read_signal_strength(struct dvb_frontend* fe,
					u16* signal_strength)
{
	return au8522_read_snr(fe, signal_strength);
}

static int au8522_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct au8522_state* state = fe->demodulator_priv;

	*ucblocks = au8522_readreg(state, 0x4087);

	return 0;
}

static int au8522_read_ber(struct dvb_frontend* fe, u32* ber)
{
	return au8522_read_ucblocks(fe, ber);
}

static int au8522_get_frontend(struct dvb_frontend* fe,
				struct dvb_frontend_parameters *p)
{
	struct au8522_state* state = fe->demodulator_priv;

	p->frequency = state->current_frequency;
	p->u.vsb.modulation = state->current_modulation;

	return 0;
}

static int au8522_get_tune_settings(struct dvb_frontend* fe,
				     struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 1000;
	return 0;
}

static void au8522_release(struct dvb_frontend* fe)
{
	struct au8522_state* state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops au8522_ops;

struct dvb_frontend* au8522_attach(const struct au8522_config* config,
				    struct i2c_adapter* i2c)
{
	struct au8522_state* state = NULL;

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct au8522_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &au8522_ops,
	       sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;

	if (au8522_init(&state->frontend) != 0) {
		printk(KERN_ERR "%s: Failed to initialize correctly\n",
			__FUNCTION__);
		goto error;
	}

	/* Note: Leaving the I2C gate open here. */
	au8522_i2c_gate_ctrl(&state->frontend, 1);

	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops au8522_ops = {

	.info = {
		.name			= "Auvitek AU8522 QAM/8VSB Frontend",
		.type			= FE_ATSC,
		.frequency_min		= 54000000,
		.frequency_max		= 858000000,
		.frequency_stepsize	= 62500,
		.caps = FE_CAN_QAM_64 | FE_CAN_QAM_256 | FE_CAN_8VSB
	},

	.init                 = au8522_init,
	.i2c_gate_ctrl        = au8522_i2c_gate_ctrl,
	.set_frontend         = au8522_set_frontend,
	.get_frontend         = au8522_get_frontend,
	.get_tune_settings    = au8522_get_tune_settings,
	.read_status          = au8522_read_status,
	.read_ber             = au8522_read_ber,
	.read_signal_strength = au8522_read_signal_strength,
	.read_snr             = au8522_read_snr,
	.read_ucblocks        = au8522_read_ucblocks,
	.release              = au8522_release,
};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Enable verbose debug messages");

MODULE_DESCRIPTION("Auvitek AU8522 QAM-B/ATSC Demodulator driver");
MODULE_AUTHOR("Steven Toth");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(au8522_attach);

/*
 * Local variables:
 * c-basic-offset: 8
 */
