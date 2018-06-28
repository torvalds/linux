/*
 *   Sharp VA3A5JZ921 One Seg Broadcast Module driver
 *   This device is labeled as just S. 921 at the top of the frontend can
 *
 *   Copyright (C) 2009-2010 Mauro Carvalho Chehab
 *   Copyright (C) 2009-2010 Douglas Landgraf <dougsland@redhat.com>
 *
 *   Developed for Leadership SBTVD 1seg device sold in Brazil
 *
 *   Frontend module based on cx24123 driver, getting some info from
 *	the old s921 driver.
 *
 *   FIXME: Need to port to DVB v5.2 API
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License as
 *   published by the Free Software Foundation version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 */

#include <linux/kernel.h>
#include <asm/div64.h>

#include <media/dvb_frontend.h>
#include "s921.h"

static int debug = 1;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Activates frontend debugging (default:0)");

#define rc(args...)  do {						\
	printk(KERN_ERR  "s921: " args);				\
} while (0)

#define dprintk(args...)						\
	do {								\
		if (debug) {						\
			printk(KERN_DEBUG "s921: %s: ", __func__);	\
			printk(args);					\
		}							\
	} while (0)

struct s921_state {
	struct i2c_adapter *i2c;
	const struct s921_config *config;

	struct dvb_frontend frontend;

	/* The Demod can't easily provide these, we cache them */
	u32 currentfreq;
};

/*
 * Various tuner defaults need to be established for a given frequency kHz.
 * fixme: The bounds on the bands do not match the doc in real life.
 * fixme: Some of them have been moved, other might need adjustment.
 */
static struct s921_bandselect_val {
	u32 freq_low;
	u8  band_reg;
} s921_bandselect[] = {
	{         0, 0x7b },
	{ 485140000, 0x5b },
	{ 515140000, 0x3b },
	{ 545140000, 0x1b },
	{ 599140000, 0xfb },
	{ 623140000, 0xdb },
	{ 659140000, 0xbb },
	{ 713140000, 0x9b },
};

struct regdata {
	u8 reg;
	u8 data;
};

static struct regdata s921_init[] = {
	{ 0x01, 0x80 },		/* Probably, a reset sequence */
	{ 0x01, 0x40 },
	{ 0x01, 0x80 },
	{ 0x01, 0x40 },

	{ 0x02, 0x00 },
	{ 0x03, 0x40 },
	{ 0x04, 0x01 },
	{ 0x05, 0x00 },
	{ 0x06, 0x00 },
	{ 0x07, 0x00 },
	{ 0x08, 0x00 },
	{ 0x09, 0x00 },
	{ 0x0a, 0x00 },
	{ 0x0b, 0x5a },
	{ 0x0c, 0x00 },
	{ 0x0d, 0x00 },
	{ 0x0f, 0x00 },
	{ 0x13, 0x1b },
	{ 0x14, 0x80 },
	{ 0x15, 0x40 },
	{ 0x17, 0x70 },
	{ 0x18, 0x01 },
	{ 0x19, 0x12 },
	{ 0x1a, 0x01 },
	{ 0x1b, 0x12 },
	{ 0x1c, 0xa0 },
	{ 0x1d, 0x00 },
	{ 0x1e, 0x0a },
	{ 0x1f, 0x08 },
	{ 0x20, 0x40 },
	{ 0x21, 0xff },
	{ 0x22, 0x4c },
	{ 0x23, 0x4e },
	{ 0x24, 0x4c },
	{ 0x25, 0x00 },
	{ 0x26, 0x00 },
	{ 0x27, 0xf4 },
	{ 0x28, 0x60 },
	{ 0x29, 0x88 },
	{ 0x2a, 0x40 },
	{ 0x2b, 0x40 },
	{ 0x2c, 0xff },
	{ 0x2d, 0x00 },
	{ 0x2e, 0xff },
	{ 0x2f, 0x00 },
	{ 0x30, 0x20 },
	{ 0x31, 0x06 },
	{ 0x32, 0x0c },
	{ 0x34, 0x0f },
	{ 0x37, 0xfe },
	{ 0x38, 0x00 },
	{ 0x39, 0x63 },
	{ 0x3a, 0x10 },
	{ 0x3b, 0x10 },
	{ 0x47, 0x00 },
	{ 0x49, 0xe5 },
	{ 0x4b, 0x00 },
	{ 0x50, 0xc0 },
	{ 0x52, 0x20 },
	{ 0x54, 0x5a },
	{ 0x55, 0x5b },
	{ 0x56, 0x40 },
	{ 0x57, 0x70 },
	{ 0x5c, 0x50 },
	{ 0x5d, 0x00 },
	{ 0x62, 0x17 },
	{ 0x63, 0x2f },
	{ 0x64, 0x6f },
	{ 0x68, 0x00 },
	{ 0x69, 0x89 },
	{ 0x6a, 0x00 },
	{ 0x6b, 0x00 },
	{ 0x6c, 0x00 },
	{ 0x6d, 0x00 },
	{ 0x6e, 0x00 },
	{ 0x70, 0x10 },
	{ 0x71, 0x00 },
	{ 0x75, 0x00 },
	{ 0x76, 0x30 },
	{ 0x77, 0x01 },
	{ 0xaf, 0x00 },
	{ 0xb0, 0xa0 },
	{ 0xb2, 0x3d },
	{ 0xb3, 0x25 },
	{ 0xb4, 0x8b },
	{ 0xb5, 0x4b },
	{ 0xb6, 0x3f },
	{ 0xb7, 0xff },
	{ 0xb8, 0xff },
	{ 0xb9, 0xfc },
	{ 0xba, 0x00 },
	{ 0xbb, 0x00 },
	{ 0xbc, 0x00 },
	{ 0xd0, 0x30 },
	{ 0xe4, 0x84 },
	{ 0xf0, 0x48 },
	{ 0xf1, 0x19 },
	{ 0xf2, 0x5a },
	{ 0xf3, 0x8e },
	{ 0xf4, 0x2d },
	{ 0xf5, 0x07 },
	{ 0xf6, 0x5a },
	{ 0xf7, 0xba },
	{ 0xf8, 0xd7 },
};

static struct regdata s921_prefreq[] = {
	{ 0x47, 0x60 },
	{ 0x68, 0x00 },
	{ 0x69, 0x89 },
	{ 0xf0, 0x48 },
	{ 0xf1, 0x19 },
};

static struct regdata s921_postfreq[] = {
	{ 0xf5, 0xae },
	{ 0xf6, 0xb7 },
	{ 0xf7, 0xba },
	{ 0xf8, 0xd7 },
	{ 0x68, 0x0a },
	{ 0x69, 0x09 },
};

static int s921_i2c_writereg(struct s921_state *state,
			     u8 i2c_addr, int reg, int data)
{
	u8 buf[] = { reg, data };
	struct i2c_msg msg = {
		.addr = i2c_addr, .flags = 0, .buf = buf, .len = 2
	};
	int rc;

	rc = i2c_transfer(state->i2c, &msg, 1);
	if (rc != 1) {
		printk("%s: writereg rcor(rc == %i, reg == 0x%02x, data == 0x%02x)\n",
		       __func__, rc, reg, data);
		return rc;
	}

	return 0;
}

static int s921_i2c_writeregdata(struct s921_state *state, u8 i2c_addr,
				 struct regdata *rd, int size)
{
	int i, rc;

	for (i = 0; i < size; i++) {
		rc = s921_i2c_writereg(state, i2c_addr, rd[i].reg, rd[i].data);
		if (rc < 0)
			return rc;
	}
	return 0;
}

static int s921_i2c_readreg(struct s921_state *state, u8 i2c_addr, u8 reg)
{
	u8 val;
	int rc;
	struct i2c_msg msg[] = {
		{ .addr = i2c_addr, .flags = 0, .buf = &reg, .len = 1 },
		{ .addr = i2c_addr, .flags = I2C_M_RD, .buf = &val, .len = 1 }
	};

	rc = i2c_transfer(state->i2c, msg, 2);

	if (rc != 2) {
		rc("%s: reg=0x%x (rcor=%d)\n", __func__, reg, rc);
		return rc;
	}

	return val;
}

#define s921_readreg(state, reg) \
	s921_i2c_readreg(state, state->config->demod_address, reg)
#define s921_writereg(state, reg, val) \
	s921_i2c_writereg(state, state->config->demod_address, reg, val)
#define s921_writeregdata(state, regdata) \
	s921_i2c_writeregdata(state, state->config->demod_address, \
	regdata, ARRAY_SIZE(regdata))

static int s921_pll_tune(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct s921_state *state = fe->demodulator_priv;
	int band, rc, i;
	unsigned long f_offset;
	u8 f_switch;
	u64 offset;

	dprintk("frequency=%i\n", p->frequency);

	for (band = 0; band < ARRAY_SIZE(s921_bandselect); band++)
		if (p->frequency < s921_bandselect[band].freq_low)
			break;
	band--;

	if (band < 0) {
		rc("%s: frequency out of range\n", __func__);
		return -EINVAL;
	}

	f_switch = s921_bandselect[band].band_reg;

	offset = ((u64)p->frequency) * 258;
	do_div(offset, 6000000);
	f_offset = ((unsigned long)offset) + 2321;

	rc = s921_writeregdata(state, s921_prefreq);
	if (rc < 0)
		return rc;

	rc = s921_writereg(state, 0xf2, (f_offset >> 8) & 0xff);
	if (rc < 0)
		return rc;

	rc = s921_writereg(state, 0xf3, f_offset & 0xff);
	if (rc < 0)
		return rc;

	rc = s921_writereg(state, 0xf4, f_switch);
	if (rc < 0)
		return rc;

	rc = s921_writeregdata(state, s921_postfreq);
	if (rc < 0)
		return rc;

	for (i = 0 ; i < 6; i++) {
		rc = s921_readreg(state, 0x80);
		dprintk("status 0x80: %02x\n", rc);
	}
	rc = s921_writereg(state, 0x01, 0x40);
	if (rc < 0)
		return rc;

	rc = s921_readreg(state, 0x01);
	dprintk("status 0x01: %02x\n", rc);

	rc = s921_readreg(state, 0x80);
	dprintk("status 0x80: %02x\n", rc);

	rc = s921_readreg(state, 0x80);
	dprintk("status 0x80: %02x\n", rc);

	rc = s921_readreg(state, 0x32);
	dprintk("status 0x32: %02x\n", rc);

	dprintk("pll tune band=%d, pll=%d\n", f_switch, (int)f_offset);

	return 0;
}

static int s921_initfe(struct dvb_frontend *fe)
{
	struct s921_state *state = fe->demodulator_priv;
	int rc;

	dprintk("\n");

	rc = s921_writeregdata(state, s921_init);
	if (rc < 0)
		return rc;

	return 0;
}

static int s921_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct s921_state *state = fe->demodulator_priv;
	int regstatus, rc;

	*status = 0;

	rc = s921_readreg(state, 0x81);
	if (rc < 0)
		return rc;

	regstatus = rc << 8;

	rc = s921_readreg(state, 0x82);
	if (rc < 0)
		return rc;

	regstatus |= rc;

	dprintk("status = %04x\n", regstatus);

	/* Full Sync - We don't know what each bit means on regs 0x81/0x82 */
	if ((regstatus & 0xff) == 0x40) {
		*status = FE_HAS_SIGNAL  |
			  FE_HAS_CARRIER |
			  FE_HAS_VITERBI |
			  FE_HAS_SYNC    |
			  FE_HAS_LOCK;
	} else if (regstatus & 0x40) {
		/* This is close to Full Sync, but not enough to get useful info */
		*status = FE_HAS_SIGNAL  |
			  FE_HAS_CARRIER |
			  FE_HAS_VITERBI |
			  FE_HAS_SYNC;
	}

	return 0;
}

static int s921_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	enum fe_status	status;
	struct s921_state *state = fe->demodulator_priv;
	int rc;

	/* FIXME: Use the proper register for it... 0x80? */
	rc = s921_read_status(fe, &status);
	if (rc < 0)
		return rc;

	*strength = (status & FE_HAS_LOCK) ? 0xffff : 0;

	dprintk("strength = 0x%04x\n", *strength);

	rc = s921_readreg(state, 0x01);
	dprintk("status 0x01: %02x\n", rc);

	rc = s921_readreg(state, 0x80);
	dprintk("status 0x80: %02x\n", rc);

	rc = s921_readreg(state, 0x32);
	dprintk("status 0x32: %02x\n", rc);

	return 0;
}

static int s921_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct s921_state *state = fe->demodulator_priv;
	int rc;

	dprintk("\n");

	/* FIXME: We don't know how to use non-auto mode */

	rc = s921_pll_tune(fe);
	if (rc < 0)
		return rc;

	state->currentfreq = p->frequency;

	return 0;
}

static int s921_get_frontend(struct dvb_frontend *fe,
			     struct dtv_frontend_properties *p)
{
	struct s921_state *state = fe->demodulator_priv;

	/* FIXME: Probably it is possible to get it from regs f1 and f2 */
	p->frequency = state->currentfreq;
	p->delivery_system = SYS_ISDBT;

	return 0;
}

static int s921_tune(struct dvb_frontend *fe,
			bool re_tune,
			unsigned int mode_flags,
			unsigned int *delay,
			enum fe_status *status)
{
	int rc = 0;

	dprintk("\n");

	if (re_tune)
		rc = s921_set_frontend(fe);

	if (!(mode_flags & FE_TUNE_MODE_ONESHOT))
		s921_read_status(fe, status);

	return rc;
}

static enum dvbfe_algo s921_get_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_HW;
}

static void s921_release(struct dvb_frontend *fe)
{
	struct s921_state *state = fe->demodulator_priv;

	dprintk("\n");
	kfree(state);
}

static const struct dvb_frontend_ops s921_ops;

struct dvb_frontend *s921_attach(const struct s921_config *config,
				    struct i2c_adapter *i2c)
{
	/* allocate memory for the internal state */
	struct s921_state *state =
		kzalloc(sizeof(struct s921_state), GFP_KERNEL);

	dprintk("\n");
	if (!state) {
		rc("Unable to kzalloc\n");
		return NULL;
	}

	/* setup the state */
	state->config = config;
	state->i2c = i2c;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &s921_ops,
		sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;

	return &state->frontend;
}
EXPORT_SYMBOL(s921_attach);

static const struct dvb_frontend_ops s921_ops = {
	.delsys = { SYS_ISDBT },
	/* Use dib8000 values per default */
	.info = {
		.name = "Sharp S921",
		.frequency_min = 470000000,
		/*
		 * Max should be 770MHz instead, according with Sharp docs,
		 * but Leadership doc says it works up to 806 MHz. This is
		 * required to get channel 69, used in Brazil
		 */
		.frequency_max = 806000000,
		.frequency_tolerance = 0,
		 .caps = FE_CAN_INVERSION_AUTO |
			 FE_CAN_FEC_1_2  | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			 FE_CAN_FEC_5_6  | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			 FE_CAN_QPSK     | FE_CAN_QAM_16 | FE_CAN_QAM_64 |
			 FE_CAN_QAM_AUTO | FE_CAN_TRANSMISSION_MODE_AUTO |
			 FE_CAN_GUARD_INTERVAL_AUTO | FE_CAN_RECOVER |
			 FE_CAN_HIERARCHY_AUTO,
	},

	.release = s921_release,

	.init = s921_initfe,
	.set_frontend = s921_set_frontend,
	.get_frontend = s921_get_frontend,
	.read_status = s921_read_status,
	.read_signal_strength = s921_read_signal_strength,
	.tune = s921_tune,
	.get_frontend_algo = s921_get_algo,
};

MODULE_DESCRIPTION("DVB Frontend module for Sharp S921 hardware");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_AUTHOR("Douglas Landgraf <dougsland@redhat.com>");
MODULE_LICENSE("GPL");
