/*
 * Driver for Zarlink DVB-T ZL10353 demodulator
 *
 * Copyright (C) 2006 Christopher Pascoe <c.pascoe@itee.uq.edu.au>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.=
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "dvb_frontend.h"
#include "zl10353_priv.h"
#include "zl10353.h"

struct zl10353_state {
	struct i2c_adapter *i2c;
	struct dvb_frontend frontend;
	struct dvb_frontend_ops ops;

	struct zl10353_config config;
};

static int debug_regs = 0;

static int zl10353_single_write(struct dvb_frontend *fe, u8 reg, u8 val)
{
	struct zl10353_state *state = fe->demodulator_priv;
	u8 buf[2] = { reg, val };
	struct i2c_msg msg = { .addr = state->config.demod_address, .flags = 0,
			       .buf = buf, .len = 2 };
	int err = i2c_transfer(state->i2c, &msg, 1);
	if (err != 1) {
		printk("zl10353: write to reg %x failed (err = %d)!\n", reg, err);
		return err;
	}
	return 0;
}

int zl10353_write(struct dvb_frontend *fe, u8 *ibuf, int ilen)
{
	int err, i;
	for (i = 0; i < ilen - 1; i++)
		if ((err = zl10353_single_write(fe, ibuf[0] + i, ibuf[i + 1])))
			return err;

	return 0;
}

static int zl10353_read_register(struct zl10353_state *state, u8 reg)
{
	int ret;
	u8 b0[1] = { reg };
	u8 b1[1] = { 0 };
	struct i2c_msg msg[2] = { { .addr = state->config.demod_address,
				    .flags = 0,
				    .buf = b0, .len = 1 },
				  { .addr = state->config.demod_address,
				    .flags = I2C_M_RD,
				    .buf = b1, .len = 1 } };

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2) {
		printk("%s: readreg error (reg=%d, ret==%i)\n",
		       __FUNCTION__, reg, ret);
		return ret;
	}

	return b1[0];
}

static void zl10353_dump_regs(struct dvb_frontend *fe)
{
	struct zl10353_state *state = fe->demodulator_priv;
	char buf[52], buf2[4];
	int ret;
	u8 reg;

	/* Dump all registers. */
	for (reg = 0; ; reg++) {
		if (reg % 16 == 0) {
			if (reg)
				printk(KERN_DEBUG "%s\n", buf);
			sprintf(buf, "%02x: ", reg);
		}
		ret = zl10353_read_register(state, reg);
		if (ret >= 0)
			sprintf(buf2, "%02x ", (u8)ret);
		else
			strcpy(buf2, "-- ");
		strcat(buf, buf2);
		if (reg == 0xff)
			break;
	}
	printk(KERN_DEBUG "%s\n", buf);
}

static int zl10353_sleep(struct dvb_frontend *fe)
{
	static u8 zl10353_softdown[] = { 0x50, 0x0C, 0x44 };

	zl10353_write(fe, zl10353_softdown, sizeof(zl10353_softdown));
	return 0;
}

static int zl10353_set_parameters(struct dvb_frontend *fe,
				  struct dvb_frontend_parameters *param)
{
	struct zl10353_state *state = fe->demodulator_priv;

	u8 pllbuf[6] = { 0x67 };

	/* These settings set "auto-everything" and start the FSM. */
	zl10353_single_write(fe, 0x55, 0x80);
	udelay(200);
	zl10353_single_write(fe, 0xEA, 0x01);
	udelay(200);
	zl10353_single_write(fe, 0xEA, 0x00);

	zl10353_single_write(fe, 0x56, 0x28);
	zl10353_single_write(fe, 0x89, 0x20);
	zl10353_single_write(fe, 0x5E, 0x00);
	zl10353_single_write(fe, 0x65, 0x5A);
	zl10353_single_write(fe, 0x66, 0xE9);
	zl10353_single_write(fe, 0x62, 0x0A);

	// if there is no attached secondary tuner, we call set_params to program
	// a potential tuner attached somewhere else
	if (state->config.no_tuner) {
		if (fe->ops->tuner_ops.set_params) {
			fe->ops->tuner_ops.set_params(fe, param);
			if (fe->ops->i2c_gate_ctrl) fe->ops->i2c_gate_ctrl(fe, 0);
		}
	}

	// if pllbuf is defined, retrieve the settings
	if (fe->ops->tuner_ops.pllbuf) {
		fe->ops->tuner_ops.pllbuf(fe, param, pllbuf+1, 5);
		pllbuf[1] <<= 1;
	} else {
		// fake pllbuf settings
		pllbuf[1] = 0x61 << 1;
		pllbuf[2] = 0;
		pllbuf[3] = 0;
		pllbuf[3] = 0;
		pllbuf[4] = 0;
	}

	// there is no call to _just_ start decoding, so we send the pllbuf anyway
	// even if there isn't a PLL attached to the secondary bus
	zl10353_write(fe, pllbuf, sizeof(pllbuf));

	zl10353_single_write(fe, 0x70, 0x01);
	udelay(250);
	zl10353_single_write(fe, 0xE4, 0x00);
	zl10353_single_write(fe, 0xE5, 0x2A);
	zl10353_single_write(fe, 0xE9, 0x02);
	zl10353_single_write(fe, 0xE7, 0x40);
	zl10353_single_write(fe, 0xE8, 0x10);

	return 0;
}

static int zl10353_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct zl10353_state *state = fe->demodulator_priv;
	int s6, s7, s8;

	if ((s6 = zl10353_read_register(state, STATUS_6)) < 0)
		return -EREMOTEIO;
	if ((s7 = zl10353_read_register(state, STATUS_7)) < 0)
		return -EREMOTEIO;
	if ((s8 = zl10353_read_register(state, STATUS_8)) < 0)
		return -EREMOTEIO;

	*status = 0;
	if (s6 & (1 << 2))
		*status |= FE_HAS_CARRIER;
	if (s6 & (1 << 1))
		*status |= FE_HAS_VITERBI;
	if (s6 & (1 << 5))
		*status |= FE_HAS_LOCK;
	if (s7 & (1 << 4))
		*status |= FE_HAS_SYNC;
	if (s8 & (1 << 6))
		*status |= FE_HAS_SIGNAL;

	if ((*status & (FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC)) !=
	    (FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC))
		*status &= ~FE_HAS_LOCK;

	return 0;
}

static int zl10353_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct zl10353_state *state = fe->demodulator_priv;
	u8 _snr;

	if (debug_regs)
		zl10353_dump_regs(fe);

	_snr = zl10353_read_register(state, SNR);
	*snr = (_snr << 8) | _snr;

	return 0;
}

static int zl10353_get_tune_settings(struct dvb_frontend *fe,
				     struct dvb_frontend_tune_settings
					 *fe_tune_settings)
{
	fe_tune_settings->min_delay_ms = 1000;
	fe_tune_settings->step_size = 0;
	fe_tune_settings->max_drift = 0;

	return 0;
}

static int zl10353_init(struct dvb_frontend *fe)
{
	struct zl10353_state *state = fe->demodulator_priv;
	u8 zl10353_reset_attach[6] = { 0x50, 0x03, 0x64, 0x46, 0x15, 0x0F };
	int rc = 0;

	if (debug_regs)
		zl10353_dump_regs(fe);

	/* Do a "hard" reset if not already done */
	if (zl10353_read_register(state, 0x50) != 0x03) {
		rc = zl10353_write(fe, zl10353_reset_attach,
				   sizeof(zl10353_reset_attach));
		if (debug_regs)
			zl10353_dump_regs(fe);
	}

	return 0;
}

static void zl10353_release(struct dvb_frontend *fe)
{
	struct zl10353_state *state = fe->demodulator_priv;

	kfree(state);
}

static struct dvb_frontend_ops zl10353_ops;

struct dvb_frontend *zl10353_attach(const struct zl10353_config *config,
				    struct i2c_adapter *i2c)
{
	struct zl10353_state *state = NULL;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct zl10353_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* setup the state */
	state->i2c = i2c;
	memcpy(&state->config, config, sizeof(struct zl10353_config));
	memcpy(&state->ops, &zl10353_ops, sizeof(struct dvb_frontend_ops));

	/* check if the demod is there */
	if (zl10353_read_register(state, CHIP_ID) != ID_ZL10353)
		goto error;

	/* create dvb_frontend */
	state->frontend.ops = &state->ops;
	state->frontend.demodulator_priv = state;

	return &state->frontend;
error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops zl10353_ops = {

	.info = {
		.name			= "Zarlink ZL10353 DVB-T",
		.type			= FE_OFDM,
		.frequency_min		= 174000000,
		.frequency_max		= 862000000,
		.frequency_stepsize	= 166667,
		.frequency_tolerance	= 0,
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 |
			FE_CAN_FEC_3_4 | FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 |
			FE_CAN_FEC_AUTO |
			FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO | FE_CAN_RECOVER |
			FE_CAN_MUTE_TS
	},

	.release = zl10353_release,

	.init = zl10353_init,
	.sleep = zl10353_sleep,

	.set_frontend = zl10353_set_parameters,
	.get_tune_settings = zl10353_get_tune_settings,

	.read_status = zl10353_read_status,
	.read_snr = zl10353_read_snr,
};

module_param(debug_regs, int, 0644);
MODULE_PARM_DESC(debug_regs, "Turn on/off frontend register dumps (default:off).");

MODULE_DESCRIPTION("Zarlink ZL10353 DVB-T demodulator driver");
MODULE_AUTHOR("Chris Pascoe");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(zl10353_attach);
EXPORT_SYMBOL(zl10353_write);
