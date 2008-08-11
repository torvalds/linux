/*
 * DVB USB Linux driver for AME DTV-5100 USB2.0 DVB-T
 *
 * Copyright (C) 2008  Antoine Jacquet <royale@zerezo.com>
 * http://royale.zerezo.com/dtv5100/
 *
 * Inspired by dvb_dummy_fe.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "dvb-usb.h"
#include "qt1010_priv.h"

struct dtv5100_fe_state {
	struct dvb_frontend frontend;
};

static int dtv5100_fe_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	*status = FE_HAS_SIGNAL
		| FE_HAS_CARRIER
		| FE_HAS_VITERBI
		| FE_HAS_SYNC
		| FE_HAS_LOCK;

	return 0;
}

static int dtv5100_fe_read_ber(struct dvb_frontend* fe, u32* ber)
{
	*ber = 0;
	return 0;
}

static int dtv5100_fe_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	*strength = 0;
	return 0;
}

static int dtv5100_fe_read_snr(struct dvb_frontend* fe, u16* snr)
{
	*snr = 0;
	return 0;
}

static int dtv5100_fe_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	*ucblocks = 0;
	return 0;
}

static int dtv5100_fe_get_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters *p)
{
	return 0;
}

static int dtv5100_fe_set_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters *p)
{
	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe, p);
		if (fe->ops.i2c_gate_ctrl) fe->ops.i2c_gate_ctrl(fe, 0);
	}

	return 0;
}

static int dtv5100_fe_sleep(struct dvb_frontend* fe)
{
	return 0;
}

static int dtv5100_fe_init(struct dvb_frontend* fe)
{
	return 0;
}

static void dtv5100_fe_release(struct dvb_frontend* fe)
{
	struct dtv5100_fe_state* state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops dtv5100_fe_ops;

struct dvb_frontend* dtv5100_fe_attach(void)
{
	struct dtv5100_fe_state* state = NULL;

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct dtv5100_fe_state), GFP_KERNEL);
	if (state == NULL) goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &dtv5100_fe_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops dtv5100_fe_ops = {

	.info = {
		.name			= "Dummy DVB-T",
		.type			= FE_OFDM,
		.frequency_min		= QT1010_MIN_FREQ,
		.frequency_max		= QT1010_MAX_FREQ,
		.frequency_stepsize	= QT1010_STEP,
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
				FE_CAN_FEC_4_5 | FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 |
				FE_CAN_FEC_7_8 | FE_CAN_FEC_8_9 | FE_CAN_FEC_AUTO |
				FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
				FE_CAN_TRANSMISSION_MODE_AUTO |
				FE_CAN_GUARD_INTERVAL_AUTO |
				FE_CAN_HIERARCHY_AUTO,
	},

	.release = dtv5100_fe_release,

	.init = dtv5100_fe_init,
	.sleep = dtv5100_fe_sleep,

	.set_frontend = dtv5100_fe_set_frontend,
	.get_frontend = dtv5100_fe_get_frontend,

	.read_status = dtv5100_fe_read_status,
	.read_ber = dtv5100_fe_read_ber,
	.read_signal_strength = dtv5100_fe_read_signal_strength,
	.read_snr = dtv5100_fe_read_snr,
	.read_ucblocks = dtv5100_fe_read_ucblocks,
};
