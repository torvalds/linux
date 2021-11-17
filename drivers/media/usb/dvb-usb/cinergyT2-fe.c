// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * TerraTec Cinergy T2/qanu USB2 DVB-T adapter.
 *
 * Copyright (C) 2007 Tomi Orava (tomimo@ncircle.nullnet.fi)
 *
 * Based on the dvb-usb-framework code and the
 * original Terratec Cinergy T2 driver by:
 *
 * Copyright (C) 2004 Daniel Mack <daniel@qanu.de> and
 *                  Holger Waechtler <holger@qanu.de>
 *
 *  Protocol Spec published on http://qanu.de/specs/terratec_cinergyT2.pdf
 */

#include "cinergyT2.h"


/*
 *  convert linux-dvb frontend parameter set into TPS.
 *  See ETSI ETS-300744, section 4.6.2, table 9 for details.
 *
 *  This function is probably reusable and may better get placed in a support
 *  library.
 *
 *  We replace erroneous fields by default TPS fields (the ones with value 0).
 */

static uint16_t compute_tps(struct dtv_frontend_properties *op)
{
	uint16_t tps = 0;

	switch (op->code_rate_HP) {
	case FEC_2_3:
		tps |= (1 << 7);
		break;
	case FEC_3_4:
		tps |= (2 << 7);
		break;
	case FEC_5_6:
		tps |= (3 << 7);
		break;
	case FEC_7_8:
		tps |= (4 << 7);
		break;
	case FEC_1_2:
	case FEC_AUTO:
	default:
		/* tps |= (0 << 7) */;
	}

	switch (op->code_rate_LP) {
	case FEC_2_3:
		tps |= (1 << 4);
		break;
	case FEC_3_4:
		tps |= (2 << 4);
		break;
	case FEC_5_6:
		tps |= (3 << 4);
		break;
	case FEC_7_8:
		tps |= (4 << 4);
		break;
	case FEC_1_2:
	case FEC_AUTO:
	default:
		/* tps |= (0 << 4) */;
	}

	switch (op->modulation) {
	case QAM_16:
		tps |= (1 << 13);
		break;
	case QAM_64:
		tps |= (2 << 13);
		break;
	case QPSK:
	default:
		/* tps |= (0 << 13) */;
	}

	switch (op->transmission_mode) {
	case TRANSMISSION_MODE_8K:
		tps |= (1 << 0);
		break;
	case TRANSMISSION_MODE_2K:
	default:
		/* tps |= (0 << 0) */;
	}

	switch (op->guard_interval) {
	case GUARD_INTERVAL_1_16:
		tps |= (1 << 2);
		break;
	case GUARD_INTERVAL_1_8:
		tps |= (2 << 2);
		break;
	case GUARD_INTERVAL_1_4:
		tps |= (3 << 2);
		break;
	case GUARD_INTERVAL_1_32:
	default:
		/* tps |= (0 << 2) */;
	}

	switch (op->hierarchy) {
	case HIERARCHY_1:
		tps |= (1 << 10);
		break;
	case HIERARCHY_2:
		tps |= (2 << 10);
		break;
	case HIERARCHY_4:
		tps |= (3 << 10);
		break;
	case HIERARCHY_NONE:
	default:
		/* tps |= (0 << 10) */;
	}

	return tps;
}

struct cinergyt2_fe_state {
	struct dvb_frontend fe;
	struct dvb_usb_device *d;

	unsigned char data[64];
	struct mutex data_mutex;

	struct dvbt_get_status_msg status;
};

static int cinergyt2_fe_read_status(struct dvb_frontend *fe,
				    enum fe_status *status)
{
	struct cinergyt2_fe_state *state = fe->demodulator_priv;
	int ret;

	mutex_lock(&state->data_mutex);
	state->data[0] = CINERGYT2_EP1_GET_TUNER_STATUS;

	ret = dvb_usb_generic_rw(state->d, state->data, 1,
				 state->data, sizeof(state->status), 0);
	if (!ret)
		memcpy(&state->status, state->data, sizeof(state->status));
	mutex_unlock(&state->data_mutex);

	if (ret < 0)
		return ret;

	*status = 0;

	if (0xffff - le16_to_cpu(state->status.gain) > 30)
		*status |= FE_HAS_SIGNAL;
	if (state->status.lock_bits & (1 << 6))
		*status |= FE_HAS_LOCK;
	if (state->status.lock_bits & (1 << 5))
		*status |= FE_HAS_SYNC;
	if (state->status.lock_bits & (1 << 4))
		*status |= FE_HAS_CARRIER;
	if (state->status.lock_bits & (1 << 1))
		*status |= FE_HAS_VITERBI;

	if ((*status & (FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC)) !=
			(FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC))
		*status &= ~FE_HAS_LOCK;

	return 0;
}

static int cinergyt2_fe_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct cinergyt2_fe_state *state = fe->demodulator_priv;

	*ber = le32_to_cpu(state->status.viterbi_error_rate);
	return 0;
}

static int cinergyt2_fe_read_unc_blocks(struct dvb_frontend *fe, u32 *unc)
{
	struct cinergyt2_fe_state *state = fe->demodulator_priv;

	*unc = le32_to_cpu(state->status.uncorrected_block_count);
	return 0;
}

static int cinergyt2_fe_read_signal_strength(struct dvb_frontend *fe,
						u16 *strength)
{
	struct cinergyt2_fe_state *state = fe->demodulator_priv;

	*strength = (0xffff - le16_to_cpu(state->status.gain));
	return 0;
}

static int cinergyt2_fe_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct cinergyt2_fe_state *state = fe->demodulator_priv;

	*snr = (state->status.snr << 8) | state->status.snr;
	return 0;
}

static int cinergyt2_fe_init(struct dvb_frontend *fe)
{
	return 0;
}

static int cinergyt2_fe_sleep(struct dvb_frontend *fe)
{
	deb_info("cinergyt2_fe_sleep() Called\n");
	return 0;
}

static int cinergyt2_fe_get_tune_settings(struct dvb_frontend *fe,
				struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 800;
	return 0;
}

static int cinergyt2_fe_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *fep = &fe->dtv_property_cache;
	struct cinergyt2_fe_state *state = fe->demodulator_priv;
	struct dvbt_set_parameters_msg *param;
	int err;

	mutex_lock(&state->data_mutex);

	param = (void *)state->data;
	param->cmd = CINERGYT2_EP1_SET_TUNER_PARAMETERS;
	param->tps = cpu_to_le16(compute_tps(fep));
	param->freq = cpu_to_le32(fep->frequency / 1000);
	param->flags = 0;

	switch (fep->bandwidth_hz) {
	default:
	case 8000000:
		param->bandwidth = 8;
		break;
	case 7000000:
		param->bandwidth = 7;
		break;
	case 6000000:
		param->bandwidth = 6;
		break;
	}

	err = dvb_usb_generic_rw(state->d, state->data, sizeof(*param),
				 state->data, 2, 0);
	if (err < 0)
		err("cinergyt2_fe_set_frontend() Failed! err=%d\n", err);

	mutex_unlock(&state->data_mutex);
	return (err < 0) ? err : 0;
}

static void cinergyt2_fe_release(struct dvb_frontend *fe)
{
	struct cinergyt2_fe_state *state = fe->demodulator_priv;
	kfree(state);
}

static const struct dvb_frontend_ops cinergyt2_fe_ops;

struct dvb_frontend *cinergyt2_fe_attach(struct dvb_usb_device *d)
{
	struct cinergyt2_fe_state *s = kzalloc(sizeof(
					struct cinergyt2_fe_state), GFP_KERNEL);
	if (s == NULL)
		return NULL;

	s->d = d;
	memcpy(&s->fe.ops, &cinergyt2_fe_ops, sizeof(struct dvb_frontend_ops));
	s->fe.demodulator_priv = s;
	mutex_init(&s->data_mutex);
	return &s->fe;
}


static const struct dvb_frontend_ops cinergyt2_fe_ops = {
	.delsys = { SYS_DVBT },
	.info = {
		.name			= DRIVER_NAME,
		.frequency_min_hz	= 174 * MHz,
		.frequency_max_hz	= 862 * MHz,
		.frequency_stepsize_hz	= 166667,
		.caps = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_1_2
			| FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4
			| FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8
			| FE_CAN_FEC_AUTO | FE_CAN_QPSK
			| FE_CAN_QAM_16 | FE_CAN_QAM_64
			| FE_CAN_QAM_AUTO
			| FE_CAN_TRANSMISSION_MODE_AUTO
			| FE_CAN_GUARD_INTERVAL_AUTO
			| FE_CAN_HIERARCHY_AUTO
			| FE_CAN_RECOVER
			| FE_CAN_MUTE_TS
	},

	.release		= cinergyt2_fe_release,

	.init			= cinergyt2_fe_init,
	.sleep			= cinergyt2_fe_sleep,

	.set_frontend		= cinergyt2_fe_set_frontend,
	.get_tune_settings	= cinergyt2_fe_get_tune_settings,

	.read_status		= cinergyt2_fe_read_status,
	.read_ber		= cinergyt2_fe_read_ber,
	.read_signal_strength	= cinergyt2_fe_read_signal_strength,
	.read_snr		= cinergyt2_fe_read_snr,
	.read_ucblocks		= cinergyt2_fe_read_unc_blocks,
};
