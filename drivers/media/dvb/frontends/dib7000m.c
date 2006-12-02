/*
 * Linux-DVB Driver for DiBcom's DiB7000M and
 *              first generation DiB7000P-demodulator-family.
 *
 * Copyright (C) 2005-6 DiBcom (http://www.dibcom.fr/)
 *
 * This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 */
#include <linux/kernel.h>
#include <linux/i2c.h>

#include "dvb_frontend.h"

#include "dib7000m.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "turn on debugging (default: 0)");

#define dprintk(args...) do { if (debug) { printk(KERN_DEBUG "DiB3000MC/P:"); printk(args); } } while (0)

struct dib7000m_state {
	struct dvb_frontend demod;
    struct dib7000m_config cfg;

	u8 i2c_addr;
	struct i2c_adapter   *i2c_adap;

	struct dibx000_i2c_master i2c_master;

/* offset is 1 in case of the 7000MC */
	u8 reg_offs;

	u16 wbd_ref;

	u8 current_band;
	fe_bandwidth_t current_bandwidth;
	struct dibx000_agc_config *current_agc;
	u32 timf[9];

	u16 revision;
};

static u16 dib7000m_read_word(struct dib7000m_state *state, u16 reg)
{
	u8 wb[2] = { (reg >> 8) | 0x80, reg & 0xff };
	u8 rb[2];
	struct i2c_msg msg[2] = {
		{ .addr = state->i2c_addr >> 1, .flags = 0,        .buf = wb, .len = 2 },
		{ .addr = state->i2c_addr >> 1, .flags = I2C_M_RD, .buf = rb, .len = 2 },
	};

	if (i2c_transfer(state->i2c_adap, msg, 2) != 2)
		dprintk("i2c read error on %d\n",reg);

	return (rb[0] << 8) | rb[1];
}

/*
static int dib7000m_write_word(struct dib7000m_state *state, u16 reg, u16 val)
{
	u8 b[4] = {
		(reg >> 8) & 0xff, reg & 0xff,
		(val >> 8) & 0xff, val & 0xff,
	};
	struct i2c_msg msg = {
		.addr = state->i2c_addr >> 1, .flags = 0, .buf = b, .len = 4
	};
	return i2c_transfer(state->i2c_adap, &msg, 1) != 1 ? -EREMOTEIO : 0;
}
*/

static int dib7000m_get_frontend(struct dvb_frontend* fe,
				struct dvb_frontend_parameters *fep)
{
	struct dib7000m_state *state = fe->demodulator_priv;
	u16 tps = dib7000m_read_word(state,480);

	fep->inversion = INVERSION_AUTO;

	fep->u.ofdm.bandwidth = state->current_bandwidth;

	switch ((tps >> 8) & 0x2) {
		case 0: fep->u.ofdm.transmission_mode = TRANSMISSION_MODE_2K; break;
		case 1: fep->u.ofdm.transmission_mode = TRANSMISSION_MODE_8K; break;
		/* case 2: fep->u.ofdm.transmission_mode = TRANSMISSION_MODE_4K; break; */
	}

	switch (tps & 0x3) {
		case 0: fep->u.ofdm.guard_interval = GUARD_INTERVAL_1_32; break;
		case 1: fep->u.ofdm.guard_interval = GUARD_INTERVAL_1_16; break;
		case 2: fep->u.ofdm.guard_interval = GUARD_INTERVAL_1_8; break;
		case 3: fep->u.ofdm.guard_interval = GUARD_INTERVAL_1_4; break;
	}

	switch ((tps >> 14) & 0x3) {
		case 0: fep->u.ofdm.constellation = QPSK; break;
		case 1: fep->u.ofdm.constellation = QAM_16; break;
		case 2:
		default: fep->u.ofdm.constellation = QAM_64; break;
	}

	/* as long as the frontend_param structure is fixed for hierarchical transmission I refuse to use it */
	/* (tps >> 13) & 0x1 == hrch is used, (tps >> 10) & 0x7 == alpha */

	fep->u.ofdm.hierarchy_information = HIERARCHY_NONE;
	switch ((tps >> 5) & 0x7) {
		case 1: fep->u.ofdm.code_rate_HP = FEC_1_2; break;
		case 2: fep->u.ofdm.code_rate_HP = FEC_2_3; break;
		case 3: fep->u.ofdm.code_rate_HP = FEC_3_4; break;
		case 5: fep->u.ofdm.code_rate_HP = FEC_5_6; break;
		case 7:
		default: fep->u.ofdm.code_rate_HP = FEC_7_8; break;

	}

	switch ((tps >> 2) & 0x7) {
		case 1: fep->u.ofdm.code_rate_LP = FEC_1_2; break;
		case 2: fep->u.ofdm.code_rate_LP = FEC_2_3; break;
		case 3: fep->u.ofdm.code_rate_LP = FEC_3_4; break;
		case 5: fep->u.ofdm.code_rate_LP = FEC_5_6; break;
		case 7:
		default: fep->u.ofdm.code_rate_LP = FEC_7_8; break;
	}

	/* native interleaver: (dib7000m_read_word(state, 481) >>  5) & 0x1 */

	return 0;
}

static int dib7000m_set_frontend(struct dvb_frontend* fe,
				struct dvb_frontend_parameters *fep)
{
	return 0;
}

static int dib7000m_read_status(struct dvb_frontend *fe, fe_status_t *stat)
{
	struct dib7000m_state *state = fe->demodulator_priv;
	u16 lock = dib7000m_read_word(state, 509);

	*stat = 0;

	if (lock & 0x8000)
		*stat |= FE_HAS_SIGNAL;
	if (lock & 0x3000)
		*stat |= FE_HAS_CARRIER;
	if (lock & 0x0100)
		*stat |= FE_HAS_VITERBI;
	if (lock & 0x0010)
		*stat |= FE_HAS_SYNC;
	if (lock & 0x0008)
		*stat |= FE_HAS_LOCK;

	return 0;
}

static int dib7000m_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct dib7000m_state *state = fe->demodulator_priv;
	*ber = (dib7000m_read_word(state, 526) << 16) | dib7000m_read_word(state, 527);
	return 0;
}

static int dib7000m_read_unc_blocks(struct dvb_frontend *fe, u32 *unc)
{
	struct dib7000m_state *state = fe->demodulator_priv;
	*unc = dib7000m_read_word(state, 534);
	return 0;
}

static int dib7000m_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct dib7000m_state *state = fe->demodulator_priv;
	u16 val = dib7000m_read_word(state, 390);
	*strength = 65535 - val;
	return 0;
}

static int dib7000m_read_snr(struct dvb_frontend* fe, u16 *snr)
{
	*snr = 0x0000;
	return 0;
}

static int dib7000m_fe_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 1000;
	return 0;
}

static int dib7000m_init(struct dvb_frontend *fe)
{
	return 0;
}

static int dib7000m_sleep(struct dvb_frontend *fe)
{
	return 0;
}

static void dib7000m_release(struct dvb_frontend *fe)
{ }

static struct dvb_frontend_ops dib7000m_ops = {
	.info = {
		.name = "DiBcom 7000MA/MB/PA/PB/MC",
		.type = FE_OFDM,
		.frequency_min      = 44250000,
		.frequency_max      = 867250000,
		.frequency_stepsize = 62500,
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_RECOVER |
			FE_CAN_HIERARCHY_AUTO,
	},

	.release              = dib7000m_release,

	.init                 = dib7000m_init,
	.sleep                = dib7000m_sleep,

	.set_frontend         = dib7000m_set_frontend,
	.get_tune_settings    = dib7000m_fe_get_tune_settings,
	.get_frontend         = dib7000m_get_frontend,

	.read_status          = dib7000m_read_status,
	.read_ber             = dib7000m_read_ber,
	.read_signal_strength = dib7000m_read_signal_strength,
	.read_snr             = dib7000m_read_snr,
	.read_ucblocks        = dib7000m_read_unc_blocks,
};

MODULE_AUTHOR("Patrick Boettcher <pboettcher@dibcom.fr>");
MODULE_DESCRIPTION("Driver for the DiBcom 7000MA/MB/PA/PB/MC COFDM demodulator");
MODULE_LICENSE("GPL");
