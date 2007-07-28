/*
    Samsung S5H1409 VSB/QAM demodulator driver

    Copyright (C) 2006 Steven Toth <stoth@hauppauge.com>

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
#include "s5h1409.h"

struct s5h1409_state {

	struct i2c_adapter* i2c;

	/* configuration settings */
	const struct s5h1409_config* config;

	struct dvb_frontend frontend;

	/* previous uncorrected block counter */
	fe_modulation_t current_modulation;

	u32 current_frequency;
};

static int debug = 0;
#define dprintk	if (debug) printk

/* Register values to initialise the demod, this will set VSB by default */
static struct init_tab {
	u8	reg;
	u16	data;
} init_tab[] = {
	{ 0x00, 0x0071, },
	{ 0x01, 0x3213, },
	{ 0x09, 0x0025, },
	{ 0x1c, 0x001d, },
	{ 0x1f, 0x002d, },
	{ 0x20, 0x001d, },
	{ 0x22, 0x0022, },
	{ 0x23, 0x0020, },
	{ 0x29, 0x110f, },
	{ 0x2a, 0x10b4, },
	{ 0x2b, 0x10ae, },
	{ 0x2c, 0x0031, },
	{ 0x31, 0x010d, },
	{ 0x32, 0x0100, },
	{ 0x44, 0x0510, },
	{ 0x54, 0x0104, },
	{ 0x58, 0x2222, },
	{ 0x59, 0x1162, },
	{ 0x5a, 0x3211, },
	{ 0x5d, 0x0370, },
	{ 0x5e, 0x0296, },
	{ 0x61, 0x0010, },
	{ 0x63, 0x4a00, },
	{ 0x65, 0x0800, },
	{ 0x71, 0x0003, },
	{ 0x72, 0x0470, },
	{ 0x81, 0x0002, },
	{ 0x82, 0x0600, },
	{ 0x86, 0x0002, },
	{ 0x8a, 0x2c38, },
	{ 0x8b, 0x2a37, },
	{ 0x92, 0x302f, },
	{ 0x93, 0x3332, },
	{ 0x96, 0x000c, },
	{ 0x99, 0x0101, },
	{ 0x9c, 0x2e37, },
	{ 0x9d, 0x2c37, },
	{ 0x9e, 0x2c37, },
	{ 0xab, 0x0100, },
	{ 0xac, 0x1003, },
	{ 0xad, 0x103f, },
	{ 0xe2, 0x0100, },
	{ 0x28, 0x1010, },
	{ 0xb1, 0x000e, },
};

/* VSB SNR lookup table */
static struct vsb_snr_tab {
	u16	val;
	u16	data;
} vsb_snr_tab[] = {
	{ 1023, 770, },
	{  923, 300, },
	{  918, 295, },
	{  915, 290, },
	{  911, 285, },
	{  906, 280, },
	{  901, 275, },
	{  896, 270, },
	{  891, 265, },
	{  885, 260, },
	{  879, 255, },
	{  873, 250, },
	{  864, 245, },
	{  858, 240, },
	{  850, 235, },
	{  841, 230, },
	{  832, 225, },
	{  823, 220, },
	{  812, 215, },
	{  802, 210, },
	{  788, 205, },
	{  778, 200, },
	{  767, 195, },
	{  753, 190, },
	{  740, 185, },
	{  725, 180, },
	{  707, 175, },
	{  689, 170, },
	{  671, 165, },
	{  656, 160, },
	{  637, 155, },
	{  616, 150, },
	{  542, 145, },
	{  519, 140, },
	{  507, 135, },
	{  497, 130, },
	{  492, 125, },
	{  474, 120, },
	{  300, 111, },
	{    0,   0, },
};

/* QAM64 SNR lookup table */
static struct qam64_snr_tab {
	u16	val;
	u16	data;
} qam64_snr_tab[] = {
	{   12, 300, },
	{   15, 290, },
	{   18, 280, },
	{   22, 270, },
	{   23, 268, },
	{   24, 266, },
	{   25, 264, },
	{   27, 262, },
	{   28, 260, },
	{   29, 258, },
	{   30, 256, },
	{   32, 254, },
	{   33, 252, },
	{   34, 250, },
	{   35, 249, },
	{   36, 248, },
	{   37, 247, },
	{   38, 246, },
	{   39, 245, },
	{   40, 244, },
	{   41, 243, },
	{   42, 241, },
	{   43, 240, },
	{   44, 239, },
	{   45, 238, },
	{   46, 237, },
	{   47, 236, },
	{   48, 235, },
	{   49, 234, },
	{   50, 233, },
	{   51, 232, },
	{   52, 231, },
	{   53, 230, },
	{   55, 229, },
	{   56, 228, },
	{   57, 227, },
	{   58, 226, },
	{   59, 225, },
	{   60, 224, },
	{   62, 223, },
	{   63, 222, },
	{   65, 221, },
	{   66, 220, },
	{   68, 219, },
	{   69, 218, },
	{   70, 217, },
	{   72, 216, },
	{   73, 215, },
	{   75, 214, },
	{   76, 213, },
	{   78, 212, },
	{   80, 211, },
	{   81, 210, },
	{   83, 209, },
	{   84, 208, },
	{   85, 207, },
	{   87, 206, },
	{   89, 205, },
	{   91, 204, },
	{   93, 203, },
	{   95, 202, },
	{   96, 201, },
	{  104, 200, },
};

/* QAM256 SNR lookup table */
static struct qam256_snr_tab {
	u16	val;
	u16	data;
} qam256_snr_tab[] = {
	{   12, 400, },
	{   13, 390, },
	{   15, 380, },
	{   17, 360, },
	{   19, 350, },
	{   22, 348, },
	{   23, 346, },
	{   24, 344, },
	{   25, 342, },
	{   26, 340, },
	{   27, 336, },
	{   28, 334, },
	{   29, 332, },
	{   30, 330, },
	{   31, 328, },
	{   32, 326, },
	{   33, 325, },
	{   34, 322, },
	{   35, 320, },
	{   37, 318, },
	{   39, 316, },
	{   40, 314, },
	{   41, 312, },
	{   42, 310, },
	{   43, 308, },
	{   46, 306, },
	{   47, 304, },
	{   49, 302, },
	{   51, 300, },
	{   53, 298, },
	{   54, 297, },
	{   55, 296, },
	{   56, 295, },
	{   57, 294, },
	{   59, 293, },
	{   60, 292, },
	{   61, 291, },
	{   63, 290, },
	{   64, 289, },
	{   65, 288, },
	{   66, 287, },
	{   68, 286, },
	{   69, 285, },
	{   71, 284, },
	{   72, 283, },
	{   74, 282, },
	{   75, 281, },
	{   76, 280, },
	{   77, 279, },
	{   78, 278, },
	{   81, 277, },
	{   83, 276, },
	{   84, 275, },
	{   86, 274, },
	{   87, 273, },
	{   89, 272, },
	{   90, 271, },
	{   92, 270, },
	{   93, 269, },
	{   95, 268, },
	{   96, 267, },
	{   98, 266, },
	{  100, 265, },
	{  102, 264, },
	{  104, 263, },
	{  105, 262, },
	{  106, 261, },
	{  110, 260, },
};

/* 8 bit registers, 16 bit values */
static int s5h1409_writereg(struct s5h1409_state* state, u8 reg, u16 data)
{
	int ret;
	u8 buf [] = { reg, data >> 8,  data & 0xff };

	struct i2c_msg msg = { .addr = state->config->demod_address,
			       .flags = 0, .buf = buf, .len = 3 };

	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1)
		printk("%s: writereg error (reg == 0x%02x, val == 0x%04x, "
		       "ret == %i)\n", __FUNCTION__, reg, data, ret);

	return (ret != 1) ? -1 : 0;
}

static u16 s5h1409_readreg(struct s5h1409_state* state, u8 reg)
{
	int ret;
	u8 b0 [] = { reg };
	u8 b1 [] = { 0, 0 };

	struct i2c_msg msg [] = {
		{ .addr = state->config->demod_address, .flags = 0,
		  .buf = b0, .len = 1 },
		{ .addr = state->config->demod_address, .flags = I2C_M_RD,
		  .buf = b1, .len = 2 } };

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2)
		printk("%s: readreg error (ret == %i)\n", __FUNCTION__, ret);
	return (b1[0] << 8) | b1[1];
}

static int s5h1409_softreset(struct dvb_frontend* fe)
{
	struct s5h1409_state* state = fe->demodulator_priv;

	dprintk("%s()\n", __FUNCTION__);

	s5h1409_writereg(state, 0xf5, 0);
	s5h1409_writereg(state, 0xf5, 1);
	return 0;
}

static int s5h1409_set_if_freq(struct dvb_frontend* fe, int KHz)
{
	struct s5h1409_state* state = fe->demodulator_priv;
	int ret = 0;

	dprintk("%s(%d KHz)\n", __FUNCTION__, KHz);

	if( (KHz == 44000) || (KHz == 5380) ) {
		s5h1409_writereg(state, 0x87, 0x01be);
		s5h1409_writereg(state, 0x88, 0x0436);
		s5h1409_writereg(state, 0x89, 0x054d);
	} else {
		printk("%s() Invalid arg = %d KHz\n", __FUNCTION__, KHz);
		ret = -1;
	}

	return ret;
}

static int s5h1409_set_spectralinversion(struct dvb_frontend* fe, int inverted)
{
	struct s5h1409_state* state = fe->demodulator_priv;

	dprintk("%s()\n", __FUNCTION__);

	if(inverted == 1)
		return s5h1409_writereg(state, 0x1b, 0x1101); /* Inverted */
	else
		return s5h1409_writereg(state, 0x1b, 0x0110); /* Normal */
}

static int s5h1409_enable_modulation(struct dvb_frontend* fe,
				     fe_modulation_t m)
{
	struct s5h1409_state* state = fe->demodulator_priv;

	dprintk("%s(0x%08x)\n", __FUNCTION__, m);

	switch(m) {
	case VSB_8:
		dprintk("%s() VSB_8\n", __FUNCTION__);
		s5h1409_writereg(state, 0xf4, 0);
		break;
	case QAM_64:
		dprintk("%s() QAM_64\n", __FUNCTION__);
		s5h1409_writereg(state, 0xf4, 1);
		s5h1409_writereg(state, 0x85, 0x100);
		break;
	case QAM_256:
		dprintk("%s() QAM_256\n", __FUNCTION__);
		s5h1409_writereg(state, 0xf4, 1);
		s5h1409_writereg(state, 0x85, 0x101);
		break;
	default:
		dprintk("%s() Invalid modulation\n", __FUNCTION__);
		return -EINVAL;
	}

	state->current_modulation = m;
	s5h1409_softreset(fe);

	return 0;
}

static int s5h1409_i2c_gate_ctrl(struct dvb_frontend* fe, int enable)
{
	struct s5h1409_state* state = fe->demodulator_priv;

	dprintk("%s(%d)\n", __FUNCTION__, enable);

	if (enable)
		return s5h1409_writereg(state, 0xf3, 1);
	else
		return s5h1409_writereg(state, 0xf3, 0);
}

static int s5h1409_set_gpio(struct dvb_frontend* fe, int enable)
{
	struct s5h1409_state* state = fe->demodulator_priv;

	dprintk("%s(%d)\n", __FUNCTION__, enable);

	if (enable)
		return s5h1409_writereg(state, 0xe3, 0x1100);
	else
		return s5h1409_writereg(state, 0xe3, 0);
}

static int s5h1409_sleep(struct dvb_frontend* fe, int enable)
{
	struct s5h1409_state* state = fe->demodulator_priv;

	dprintk("%s(%d)\n", __FUNCTION__, enable);

	return s5h1409_writereg(state, 0xf2, enable);
}

static int s5h1409_register_reset(struct dvb_frontend* fe)
{
	struct s5h1409_state* state = fe->demodulator_priv;

	dprintk("%s()\n", __FUNCTION__);

	return s5h1409_writereg(state, 0xfa, 0);
}

/* Talk to the demod, set the FEC, GUARD, QAM settings etc */
static int s5h1409_set_frontend (struct dvb_frontend* fe,
				 struct dvb_frontend_parameters *p)
{
	struct s5h1409_state* state = fe->demodulator_priv;

	dprintk("%s(frequency=%d)\n", __FUNCTION__, p->frequency);

	s5h1409_softreset(fe);

	state->current_frequency = p->frequency;

	s5h1409_enable_modulation(fe, p->u.vsb.modulation);

	if (fe->ops.tuner_ops.set_params) {
		if (fe->ops.i2c_gate_ctrl) fe->ops.i2c_gate_ctrl(fe, 1);
		fe->ops.tuner_ops.set_params(fe, p);
		if (fe->ops.i2c_gate_ctrl) fe->ops.i2c_gate_ctrl(fe, 0);
	}

	return 0;
}

/* Reset the demod hardware and reset all of the configuration registers
   to a default state. */
static int s5h1409_init (struct dvb_frontend* fe)
{
	int i;

	struct s5h1409_state* state = fe->demodulator_priv;
	dprintk("%s()\n", __FUNCTION__);

	s5h1409_sleep(fe, 0);
	s5h1409_register_reset(fe);

	for (i=0; i < ARRAY_SIZE(init_tab); i++)
		s5h1409_writereg(state, init_tab[i].reg, init_tab[i].data);

	/* The datasheet says that after initialisation, VSB is default */
	state->current_modulation = VSB_8;

	if (state->config->output_mode == S5H1409_SERIAL_OUTPUT)
		s5h1409_writereg(state, 0xab, 0x100); /* Serial */
	else
		s5h1409_writereg(state, 0xab, 0x0); /* Parallel */

	s5h1409_set_spectralinversion(fe, state->config->inversion);
	s5h1409_set_if_freq(fe, state->config->if_freq);
	s5h1409_set_gpio(fe, state->config->gpio);
	s5h1409_softreset(fe);

	/* Note: Leaving the I2C gate open here. */
	s5h1409_i2c_gate_ctrl(fe, 1);

	return 0;
}

static int s5h1409_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct s5h1409_state* state = fe->demodulator_priv;
	u16 reg;
	u32 tuner_status = 0;

	*status = 0;

	/* Get the demodulator status */
	reg = s5h1409_readreg(state, 0xf1);
	if(reg & 0x1000)
		*status |= FE_HAS_VITERBI;
	if(reg & 0x8000)
		*status |= FE_HAS_LOCK | FE_HAS_SYNC;

	switch(state->config->status_mode) {
	case S5H1409_DEMODLOCKING:
		if (*status & FE_HAS_VITERBI)
			*status |= FE_HAS_CARRIER | FE_HAS_SIGNAL;
		break;
	case S5H1409_TUNERLOCKING:
		/* Get the tuner status */
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

static int s5h1409_qam256_lookup_snr(struct dvb_frontend* fe, u16* snr, u16 v)
{
	int i, ret = -EINVAL;
	dprintk("%s()\n", __FUNCTION__);

	for (i=0; i < ARRAY_SIZE(qam256_snr_tab); i++) {
		if (v < qam256_snr_tab[i].val) {
			*snr = qam256_snr_tab[i].data;
			ret = 0;
			break;
		}
	}
	return ret;
}

static int s5h1409_qam64_lookup_snr(struct dvb_frontend* fe, u16* snr, u16 v)
{
	int i, ret = -EINVAL;
	dprintk("%s()\n", __FUNCTION__);

	for (i=0; i < ARRAY_SIZE(qam64_snr_tab); i++) {
		if (v < qam64_snr_tab[i].val) {
			*snr = qam64_snr_tab[i].data;
			ret = 0;
			break;
		}
	}
	return ret;
}

static int s5h1409_vsb_lookup_snr(struct dvb_frontend* fe, u16* snr, u16 v)
{
	int i, ret = -EINVAL;
	dprintk("%s()\n", __FUNCTION__);

	for (i=0; i < ARRAY_SIZE(vsb_snr_tab); i++) {
		if (v > vsb_snr_tab[i].val) {
			*snr = vsb_snr_tab[i].data;
			ret = 0;
			break;
		}
	}
	dprintk("%s() snr=%d\n", __FUNCTION__, *snr);
	return ret;
}

static int s5h1409_read_snr(struct dvb_frontend* fe, u16* snr)
{
	struct s5h1409_state* state = fe->demodulator_priv;
	u16 reg;
	dprintk("%s()\n", __FUNCTION__);

	reg = s5h1409_readreg(state, 0xf1) & 0x1ff;

	switch(state->current_modulation) {
	case QAM_64:
		return s5h1409_qam64_lookup_snr(fe, snr, reg);
	case QAM_256:
		return s5h1409_qam256_lookup_snr(fe, snr, reg);
	case VSB_8:
		return s5h1409_vsb_lookup_snr(fe, snr, reg);
	default:
		break;
	}

	return -EINVAL;
}

static int s5h1409_read_signal_strength(struct dvb_frontend* fe,
					u16* signal_strength)
{
	return s5h1409_read_snr(fe, signal_strength);
}

static int s5h1409_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct s5h1409_state* state = fe->demodulator_priv;

	*ucblocks = s5h1409_readreg(state, 0xb5);

	return 0;
}

static int s5h1409_read_ber(struct dvb_frontend* fe, u32* ber)
{
	return s5h1409_read_ucblocks(fe, ber);
}

static int s5h1409_get_frontend(struct dvb_frontend* fe,
				struct dvb_frontend_parameters *p)
{
	struct s5h1409_state* state = fe->demodulator_priv;

	p->frequency = state->current_frequency;
	p->u.vsb.modulation = state->current_modulation;

	return 0;
}

static int s5h1409_get_tune_settings(struct dvb_frontend* fe,
				     struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 1000;
	return 0;
}

static void s5h1409_release(struct dvb_frontend* fe)
{
	struct s5h1409_state* state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops s5h1409_ops;

struct dvb_frontend* s5h1409_attach(const struct s5h1409_config* config,
				    struct i2c_adapter* i2c)
{
	struct s5h1409_state* state = NULL;

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct s5h1409_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	state->current_modulation = 0;

	/* check if the demod exists */
	if (s5h1409_readreg(state, 0x04) != 0x0066)
		goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &s5h1409_ops,
	       sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;

	/* Note: Leaving the I2C gate open here. */
	s5h1409_writereg(state, 0xf3, 1);

	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops s5h1409_ops = {

	.info = {
		.name			= "Samsung S5H1409 QAM/8VSB Frontend",
		.type			= FE_ATSC,
		.frequency_min		= 54000000,
		.frequency_max		= 858000000,
		.frequency_stepsize	= 62500,
		.caps = FE_CAN_QAM_64 | FE_CAN_QAM_256 | FE_CAN_8VSB
	},

	.init                 = s5h1409_init,
	.i2c_gate_ctrl        = s5h1409_i2c_gate_ctrl,
	.set_frontend         = s5h1409_set_frontend,
	.get_frontend         = s5h1409_get_frontend,
	.get_tune_settings    = s5h1409_get_tune_settings,
	.read_status          = s5h1409_read_status,
	.read_ber             = s5h1409_read_ber,
	.read_signal_strength = s5h1409_read_signal_strength,
	.read_snr             = s5h1409_read_snr,
	.read_ucblocks        = s5h1409_read_ucblocks,
	.release              = s5h1409_release,
};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Enable verbose debug messages");

MODULE_DESCRIPTION("Samsung S5H1409 QAM-B/ATSC Demodulator driver");
MODULE_AUTHOR("Steven Toth");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(s5h1409_attach);

/*
 * Local variables:
 * c-basic-offset: 8
 */
