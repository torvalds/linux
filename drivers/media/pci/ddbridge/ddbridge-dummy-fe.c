// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for Dummy Frontend
 *
 *  Written by Emard <emard@softhome.net>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>

#include <media/dvb_frontend.h>
#include "ddbridge-dummy-fe.h"

struct ddbridge_dummy_fe_state {
	struct dvb_frontend frontend;
};

static int ddbridge_dummy_fe_read_status(struct dvb_frontend *fe,
				    enum fe_status *status)
{
	*status = FE_HAS_SIGNAL
		| FE_HAS_CARRIER
		| FE_HAS_VITERBI
		| FE_HAS_SYNC
		| FE_HAS_LOCK;

	return 0;
}

static int ddbridge_dummy_fe_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	*ber = 0;
	return 0;
}

static int ddbridge_dummy_fe_read_signal_strength(struct dvb_frontend *fe,
					     u16 *strength)
{
	*strength = 0;
	return 0;
}

static int ddbridge_dummy_fe_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	*snr = 0;
	return 0;
}

static int ddbridge_dummy_fe_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	*ucblocks = 0;
	return 0;
}

/*
 * Should only be implemented if it actually reads something from the hardware.
 * Also, it should check for the locks, in order to avoid report wrong data
 * to userspace.
 */
static int ddbridge_dummy_fe_get_frontend(struct dvb_frontend *fe,
				     struct dtv_frontend_properties *p)
{
	return 0;
}

static int ddbridge_dummy_fe_set_frontend(struct dvb_frontend *fe)
{
	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	return 0;
}

static int ddbridge_dummy_fe_sleep(struct dvb_frontend *fe)
{
	return 0;
}

static int ddbridge_dummy_fe_init(struct dvb_frontend *fe)
{
	return 0;
}

static void ddbridge_dummy_fe_release(struct dvb_frontend *fe)
{
	struct ddbridge_dummy_fe_state *state = fe->demodulator_priv;

	kfree(state);
}

static const struct dvb_frontend_ops ddbridge_dummy_fe_qam_ops;

struct dvb_frontend *ddbridge_dummy_fe_qam_attach(void)
{
	struct ddbridge_dummy_fe_state *state = NULL;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct ddbridge_dummy_fe_state), GFP_KERNEL);
	if (!state)
		return NULL;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops,
	       &ddbridge_dummy_fe_qam_ops,
	       sizeof(struct dvb_frontend_ops));

	state->frontend.demodulator_priv = state;
	return &state->frontend;
}
EXPORT_SYMBOL_GPL(ddbridge_dummy_fe_qam_attach);

static const struct dvb_frontend_ops ddbridge_dummy_fe_qam_ops = {
	.delsys = { SYS_DVBC_ANNEX_A },
	.info = {
		.name			= "ddbridge dummy DVB-C",
		.frequency_min_hz	=  51 * MHz,
		.frequency_max_hz	= 858 * MHz,
		.frequency_stepsize_hz	= 62500,
		/* symbol_rate_min: SACLK/64 == (XIN/2)/64 */
		.symbol_rate_min	= (57840000 / 2) / 64,
		.symbol_rate_max	= (57840000 / 2) / 4,   /* SACLK/4 */
		.caps = FE_CAN_QAM_16 |
			FE_CAN_QAM_32 |
			FE_CAN_QAM_64 |
			FE_CAN_QAM_128 |
			FE_CAN_QAM_256 |
			FE_CAN_FEC_AUTO |
			FE_CAN_INVERSION_AUTO
	},

	.release = ddbridge_dummy_fe_release,

	.init = ddbridge_dummy_fe_init,
	.sleep = ddbridge_dummy_fe_sleep,

	.set_frontend = ddbridge_dummy_fe_set_frontend,
	.get_frontend = ddbridge_dummy_fe_get_frontend,

	.read_status = ddbridge_dummy_fe_read_status,
	.read_ber = ddbridge_dummy_fe_read_ber,
	.read_signal_strength = ddbridge_dummy_fe_read_signal_strength,
	.read_snr = ddbridge_dummy_fe_read_snr,
	.read_ucblocks = ddbridge_dummy_fe_read_ucblocks,
};

MODULE_DESCRIPTION("ddbridge dummy Frontend");
MODULE_AUTHOR("Emard");
MODULE_LICENSE("GPL");
