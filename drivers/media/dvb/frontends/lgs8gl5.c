/*
    Legend Silicon LGS-8GL5 DMB-TH OFDM demodulator driver

    Copyright (C) 2008 Sirius International (Hong Kong) Limited
	Timothy Lee <timothy.lee@siriushk.com>

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
#include "dvb_frontend.h"
#include "lgs8gl5.h"


#define REG_RESET		0x02
#define REG_RESET_OFF			0x01
#define REG_03			0x03
#define REG_04			0x04
#define REG_07			0x07
#define REG_09			0x09
#define REG_0A			0x0a
#define REG_0B			0x0b
#define REG_0C			0x0c
#define REG_37			0x37
#define REG_STRENGTH		0x4b
#define REG_STRENGTH_MASK		0x7f
#define REG_STRENGTH_CARRIER		0x80
#define REG_INVERSION		0x7c
#define REG_INVERSION_ON		0x80
#define REG_7D			0x7d
#define REG_7E			0x7e
#define REG_A2			0xa2
#define REG_STATUS		0xa4
#define REG_STATUS_SYNC		0x04
#define REG_STATUS_LOCK		0x01


struct lgs8gl5_state {
	struct i2c_adapter *i2c;
	const struct lgs8gl5_config *config;
	struct dvb_frontend frontend;
};


static int debug;
#define dprintk(args...) \
	do { \
		if (debug) \
			printk(KERN_DEBUG "lgs8gl5: " args); \
	} while (0)


/* Writes into demod's register */
static int
lgs8gl5_write_reg(struct lgs8gl5_state *state, u8 reg, u8 data)
{
	int ret;
	u8 buf[] = {reg, data};
	struct i2c_msg msg = {
		.addr  = state->config->demod_address,
		.flags = 0,
		.buf   = buf,
		.len   = 2
	};

	ret = i2c_transfer(state->i2c, &msg, 1);
	if (ret != 1)
		dprintk("%s: error (reg=0x%02x, val=0x%02x, ret=%i)\n",
			__func__, reg, data, ret);
	return (ret != 1) ? -1 : 0;
}


/* Reads from demod's register */
static int
lgs8gl5_read_reg(struct lgs8gl5_state *state, u8 reg)
{
	int ret;
	u8 b0[] = {reg};
	u8 b1[] = {0};
	struct i2c_msg msg[2] = {
		{
			.addr  = state->config->demod_address,
			.flags = 0,
			.buf   = b0,
			.len   = 1
		},
		{
			.addr  = state->config->demod_address,
			.flags = I2C_M_RD,
			.buf   = b1,
			.len   = 1
		}
	};

	ret = i2c_transfer(state->i2c, msg, 2);
	if (ret != 2)
		return -EIO;

	return b1[0];
}


static int
lgs8gl5_update_reg(struct lgs8gl5_state *state, u8 reg, u8 data)
{
	lgs8gl5_read_reg(state, reg);
	lgs8gl5_write_reg(state, reg, data);
	return 0;
}


/* Writes into alternate device's register */
/* TODO:  Find out what that device is for! */
static int
lgs8gl5_update_alt_reg(struct lgs8gl5_state *state, u8 reg, u8 data)
{
	int ret;
	u8 b0[] = {reg};
	u8 b1[] = {0};
	u8 b2[] = {reg, data};
	struct i2c_msg msg[3] = {
		{
			.addr  = state->config->demod_address + 2,
			.flags = 0,
			.buf   = b0,
			.len   = 1
		},
		{
			.addr  = state->config->demod_address + 2,
			.flags = I2C_M_RD,
			.buf   = b1,
			.len   = 1
		},
		{
			.addr  = state->config->demod_address + 2,
			.flags = 0,
			.buf   = b2,
			.len   = 2
		},
	};

	ret = i2c_transfer(state->i2c, msg, 3);
	return (ret != 3) ? -1 : 0;
}


static void
lgs8gl5_soft_reset(struct lgs8gl5_state *state)
{
	u8 val;

	dprintk("%s\n", __func__);

	val = lgs8gl5_read_reg(state, REG_RESET);
	lgs8gl5_write_reg(state, REG_RESET, val & ~REG_RESET_OFF);
	lgs8gl5_write_reg(state, REG_RESET, val | REG_RESET_OFF);
	msleep(5);
}


/* Starts demodulation */
static void
lgs8gl5_start_demod(struct lgs8gl5_state *state)
{
	u8  val;
	int n;

	dprintk("%s\n", __func__);

	lgs8gl5_update_alt_reg(state, 0xc2, 0x28);
	lgs8gl5_soft_reset(state);
	lgs8gl5_update_reg(state, REG_07, 0x10);
	lgs8gl5_update_reg(state, REG_07, 0x10);
	lgs8gl5_write_reg(state, REG_09, 0x0e);
	lgs8gl5_write_reg(state, REG_0A, 0xe5);
	lgs8gl5_write_reg(state, REG_0B, 0x35);
	lgs8gl5_write_reg(state, REG_0C, 0x30);

	lgs8gl5_update_reg(state, REG_03, 0x00);
	lgs8gl5_update_reg(state, REG_7E, 0x01);
	lgs8gl5_update_alt_reg(state, 0xc5, 0x00);
	lgs8gl5_update_reg(state, REG_04, 0x02);
	lgs8gl5_update_reg(state, REG_37, 0x01);
	lgs8gl5_soft_reset(state);

	/* Wait for carrier */
	for (n = 0;  n < 10;  n++) {
		val = lgs8gl5_read_reg(state, REG_STRENGTH);
		dprintk("Wait for carrier[%d] 0x%02X\n", n, val);
		if (val & REG_STRENGTH_CARRIER)
			break;
		msleep(4);
	}
	if (!(val & REG_STRENGTH_CARRIER))
		return;

	/* Wait for lock */
	for (n = 0;  n < 20;  n++) {
		val = lgs8gl5_read_reg(state, REG_STATUS);
		dprintk("Wait for lock[%d] 0x%02X\n", n, val);
		if (val & REG_STATUS_LOCK)
			break;
		msleep(12);
	}
	if (!(val & REG_STATUS_LOCK))
		return;

	lgs8gl5_write_reg(state, REG_7D, lgs8gl5_read_reg(state, REG_A2));
	lgs8gl5_soft_reset(state);
}


static int
lgs8gl5_init(struct dvb_frontend *fe)
{
	struct lgs8gl5_state *state = fe->demodulator_priv;

	dprintk("%s\n", __func__);

	lgs8gl5_update_alt_reg(state, 0xc2, 0x28);
	lgs8gl5_soft_reset(state);
	lgs8gl5_update_reg(state, REG_07, 0x10);
	lgs8gl5_update_reg(state, REG_07, 0x10);
	lgs8gl5_write_reg(state, REG_09, 0x0e);
	lgs8gl5_write_reg(state, REG_0A, 0xe5);
	lgs8gl5_write_reg(state, REG_0B, 0x35);
	lgs8gl5_write_reg(state, REG_0C, 0x30);

	return 0;
}


static int
lgs8gl5_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct lgs8gl5_state *state = fe->demodulator_priv;
	u8 level = lgs8gl5_read_reg(state, REG_STRENGTH);
	u8 flags = lgs8gl5_read_reg(state, REG_STATUS);

	*status = 0;

	if ((level & REG_STRENGTH_MASK) > 0)
		*status |= FE_HAS_SIGNAL;
	if (level & REG_STRENGTH_CARRIER)
		*status |= FE_HAS_CARRIER;
	if (flags & REG_STATUS_SYNC)
		*status |= FE_HAS_SYNC;
	if (flags & REG_STATUS_LOCK)
		*status |= FE_HAS_LOCK;

	return 0;
}


static int
lgs8gl5_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	*ber = 0;

	return 0;
}


static int
lgs8gl5_read_signal_strength(struct dvb_frontend *fe, u16 *signal_strength)
{
	struct lgs8gl5_state *state = fe->demodulator_priv;
	u8 level = lgs8gl5_read_reg(state, REG_STRENGTH);
	*signal_strength = (level & REG_STRENGTH_MASK) << 8;

	return 0;
}


static int
lgs8gl5_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct lgs8gl5_state *state = fe->demodulator_priv;
	u8 level = lgs8gl5_read_reg(state, REG_STRENGTH);
	*snr = (level & REG_STRENGTH_MASK) << 8;

	return 0;
}


static int
lgs8gl5_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	*ucblocks = 0;

	return 0;
}


static int
lgs8gl5_set_frontend(struct dvb_frontend *fe,
		struct dvb_frontend_parameters *p)
{
	struct lgs8gl5_state *state = fe->demodulator_priv;

	dprintk("%s\n", __func__);

	if (p->u.ofdm.bandwidth != BANDWIDTH_8_MHZ)
		return -EINVAL;

	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe, p);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	/* lgs8gl5_set_inversion(state, p->inversion); */

	lgs8gl5_start_demod(state);

	return 0;
}


static int
lgs8gl5_get_frontend(struct dvb_frontend *fe,
		struct dvb_frontend_parameters *p)
{
	struct lgs8gl5_state *state = fe->demodulator_priv;
	u8 inv = lgs8gl5_read_reg(state, REG_INVERSION);
	struct dvb_ofdm_parameters *o = &p->u.ofdm;

	p->inversion = (inv & REG_INVERSION_ON) ? INVERSION_ON : INVERSION_OFF;

	o->code_rate_HP = FEC_1_2;
	o->code_rate_LP = FEC_7_8;
	o->guard_interval = GUARD_INTERVAL_1_32;
	o->transmission_mode = TRANSMISSION_MODE_2K;
	o->constellation = QAM_64;
	o->hierarchy_information = HIERARCHY_NONE;
	o->bandwidth = BANDWIDTH_8_MHZ;

	return 0;
}


static int
lgs8gl5_get_tune_settings(struct dvb_frontend *fe,
		struct dvb_frontend_tune_settings *fesettings)
{
	fesettings->min_delay_ms = 240;
	fesettings->step_size    = 0;
	fesettings->max_drift    = 0;
	return 0;
}


static void
lgs8gl5_release(struct dvb_frontend *fe)
{
	struct lgs8gl5_state *state = fe->demodulator_priv;
	kfree(state);
}


static struct dvb_frontend_ops lgs8gl5_ops;


struct dvb_frontend*
lgs8gl5_attach(const struct lgs8gl5_config *config, struct i2c_adapter *i2c)
{
	struct lgs8gl5_state *state = NULL;

	dprintk("%s\n", __func__);

	/* Allocate memory for the internal state */
	state = kmalloc(sizeof(struct lgs8gl5_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* Setup the state */
	state->config = config;
	state->i2c    = i2c;

	/* Check if the demod is there */
	if (lgs8gl5_read_reg(state, REG_RESET) < 0)
		goto error;

	/* Create dvb_frontend */
	memcpy(&state->frontend.ops, &lgs8gl5_ops,
		sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(lgs8gl5_attach);


static struct dvb_frontend_ops lgs8gl5_ops = {
	.info = {
		.name			= "Legend Silicon LGS-8GL5 DMB-TH",
		.type			= FE_OFDM,
		.frequency_min		= 474000000,
		.frequency_max		= 858000000,
		.frequency_stepsize	= 10000,
		.frequency_tolerance	= 0,
		.caps = FE_CAN_FEC_AUTO |
			FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_32 |
			FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_BANDWIDTH_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO |
			FE_CAN_RECOVER
	},

	.release = lgs8gl5_release,

	.init = lgs8gl5_init,

	.set_frontend = lgs8gl5_set_frontend,
	.get_frontend = lgs8gl5_get_frontend,
	.get_tune_settings = lgs8gl5_get_tune_settings,

	.read_status = lgs8gl5_read_status,
	.read_ber = lgs8gl5_read_ber,
	.read_signal_strength = lgs8gl5_read_signal_strength,
	.read_snr = lgs8gl5_read_snr,
	.read_ucblocks = lgs8gl5_read_ucblocks,
};


module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("Legend Silicon LGS-8GL5 DMB-TH Demodulator driver");
MODULE_AUTHOR("Timothy Lee");
MODULE_LICENSE("GPL");
