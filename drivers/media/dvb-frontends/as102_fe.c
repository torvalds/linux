// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Abilis Systems Single DVB-T Receiver
 * Copyright (C) 2008 Pierrick Hascoet <pierrick.hascoet@abilis.com>
 * Copyright (C) 2010 Devin Heitmueller <dheitmueller@kernellabs.com>
 */

#include <media/dvb_frontend.h>

#include "as102_fe.h"

struct as102_state {
	struct dvb_frontend frontend;
	struct as10x_demod_stats demod_stats;

	const struct as102_fe_ops *ops;
	void *priv;
	uint8_t elna_cfg;

	/* signal strength */
	uint16_t signal_strength;
	/* bit error rate */
	uint32_t ber;
};

static uint8_t as102_fe_get_code_rate(enum fe_code_rate arg)
{
	uint8_t c;

	switch (arg) {
	case FEC_1_2:
		c = CODE_RATE_1_2;
		break;
	case FEC_2_3:
		c = CODE_RATE_2_3;
		break;
	case FEC_3_4:
		c = CODE_RATE_3_4;
		break;
	case FEC_5_6:
		c = CODE_RATE_5_6;
		break;
	case FEC_7_8:
		c = CODE_RATE_7_8;
		break;
	default:
		c = CODE_RATE_UNKNOWN;
		break;
	}

	return c;
}

static int as102_fe_set_frontend(struct dvb_frontend *fe)
{
	struct as102_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct as10x_tune_args tune_args = { 0 };

	/* set frequency */
	tune_args.freq = c->frequency / 1000;

	/* fix interleaving_mode */
	tune_args.interleaving_mode = INTLV_NATIVE;

	switch (c->bandwidth_hz) {
	case 8000000:
		tune_args.bandwidth = BW_8_MHZ;
		break;
	case 7000000:
		tune_args.bandwidth = BW_7_MHZ;
		break;
	case 6000000:
		tune_args.bandwidth = BW_6_MHZ;
		break;
	default:
		tune_args.bandwidth = BW_8_MHZ;
	}

	switch (c->guard_interval) {
	case GUARD_INTERVAL_1_32:
		tune_args.guard_interval = GUARD_INT_1_32;
		break;
	case GUARD_INTERVAL_1_16:
		tune_args.guard_interval = GUARD_INT_1_16;
		break;
	case GUARD_INTERVAL_1_8:
		tune_args.guard_interval = GUARD_INT_1_8;
		break;
	case GUARD_INTERVAL_1_4:
		tune_args.guard_interval = GUARD_INT_1_4;
		break;
	case GUARD_INTERVAL_AUTO:
	default:
		tune_args.guard_interval = GUARD_UNKNOWN;
		break;
	}

	switch (c->modulation) {
	case QPSK:
		tune_args.modulation = CONST_QPSK;
		break;
	case QAM_16:
		tune_args.modulation = CONST_QAM16;
		break;
	case QAM_64:
		tune_args.modulation = CONST_QAM64;
		break;
	default:
		tune_args.modulation = CONST_UNKNOWN;
		break;
	}

	switch (c->transmission_mode) {
	case TRANSMISSION_MODE_2K:
		tune_args.transmission_mode = TRANS_MODE_2K;
		break;
	case TRANSMISSION_MODE_8K:
		tune_args.transmission_mode = TRANS_MODE_8K;
		break;
	default:
		tune_args.transmission_mode = TRANS_MODE_UNKNOWN;
	}

	switch (c->hierarchy) {
	case HIERARCHY_NONE:
		tune_args.hierarchy = HIER_NONE;
		break;
	case HIERARCHY_1:
		tune_args.hierarchy = HIER_ALPHA_1;
		break;
	case HIERARCHY_2:
		tune_args.hierarchy = HIER_ALPHA_2;
		break;
	case HIERARCHY_4:
		tune_args.hierarchy = HIER_ALPHA_4;
		break;
	case HIERARCHY_AUTO:
		tune_args.hierarchy = HIER_UNKNOWN;
		break;
	}

	pr_debug("as102: tuner parameters: freq: %d  bw: 0x%02x  gi: 0x%02x\n",
			c->frequency,
			tune_args.bandwidth,
			tune_args.guard_interval);

	/*
	 * Detect a hierarchy selection
	 * if HP/LP are both set to FEC_NONE, HP will be selected.
	 */
	if ((tune_args.hierarchy != HIER_NONE) &&
		       ((c->code_rate_LP == FEC_NONE) ||
			(c->code_rate_HP == FEC_NONE))) {

		if (c->code_rate_LP == FEC_NONE) {
			tune_args.hier_select = HIER_HIGH_PRIORITY;
			tune_args.code_rate =
			   as102_fe_get_code_rate(c->code_rate_HP);
		}

		if (c->code_rate_HP == FEC_NONE) {
			tune_args.hier_select = HIER_LOW_PRIORITY;
			tune_args.code_rate =
			   as102_fe_get_code_rate(c->code_rate_LP);
		}

		pr_debug("as102: \thierarchy: 0x%02x  selected: %s  code_rate_%s: 0x%02x\n",
			tune_args.hierarchy,
			tune_args.hier_select == HIER_HIGH_PRIORITY ?
			"HP" : "LP",
			tune_args.hier_select == HIER_HIGH_PRIORITY ?
			"HP" : "LP",
			tune_args.code_rate);
	} else {
		tune_args.code_rate =
			as102_fe_get_code_rate(c->code_rate_HP);
	}

	/* Set frontend arguments */
	return state->ops->set_tune(state->priv, &tune_args);
}

static int as102_fe_get_frontend(struct dvb_frontend *fe,
				 struct dtv_frontend_properties *c)
{
	struct as102_state *state = fe->demodulator_priv;
	int ret = 0;
	struct as10x_tps tps = { 0 };

	/* send abilis command: GET_TPS */
	ret = state->ops->get_tps(state->priv, &tps);
	if (ret < 0)
		return ret;

	/* extract constellation */
	switch (tps.modulation) {
	case CONST_QPSK:
		c->modulation = QPSK;
		break;
	case CONST_QAM16:
		c->modulation = QAM_16;
		break;
	case CONST_QAM64:
		c->modulation = QAM_64;
		break;
	}

	/* extract hierarchy */
	switch (tps.hierarchy) {
	case HIER_NONE:
		c->hierarchy = HIERARCHY_NONE;
		break;
	case HIER_ALPHA_1:
		c->hierarchy = HIERARCHY_1;
		break;
	case HIER_ALPHA_2:
		c->hierarchy = HIERARCHY_2;
		break;
	case HIER_ALPHA_4:
		c->hierarchy = HIERARCHY_4;
		break;
	}

	/* extract code rate HP */
	switch (tps.code_rate_HP) {
	case CODE_RATE_1_2:
		c->code_rate_HP = FEC_1_2;
		break;
	case CODE_RATE_2_3:
		c->code_rate_HP = FEC_2_3;
		break;
	case CODE_RATE_3_4:
		c->code_rate_HP = FEC_3_4;
		break;
	case CODE_RATE_5_6:
		c->code_rate_HP = FEC_5_6;
		break;
	case CODE_RATE_7_8:
		c->code_rate_HP = FEC_7_8;
		break;
	}

	/* extract code rate LP */
	switch (tps.code_rate_LP) {
	case CODE_RATE_1_2:
		c->code_rate_LP = FEC_1_2;
		break;
	case CODE_RATE_2_3:
		c->code_rate_LP = FEC_2_3;
		break;
	case CODE_RATE_3_4:
		c->code_rate_LP = FEC_3_4;
		break;
	case CODE_RATE_5_6:
		c->code_rate_LP = FEC_5_6;
		break;
	case CODE_RATE_7_8:
		c->code_rate_LP = FEC_7_8;
		break;
	}

	/* extract guard interval */
	switch (tps.guard_interval) {
	case GUARD_INT_1_32:
		c->guard_interval = GUARD_INTERVAL_1_32;
		break;
	case GUARD_INT_1_16:
		c->guard_interval = GUARD_INTERVAL_1_16;
		break;
	case GUARD_INT_1_8:
		c->guard_interval = GUARD_INTERVAL_1_8;
		break;
	case GUARD_INT_1_4:
		c->guard_interval = GUARD_INTERVAL_1_4;
		break;
	}

	/* extract transmission mode */
	switch (tps.transmission_mode) {
	case TRANS_MODE_2K:
		c->transmission_mode = TRANSMISSION_MODE_2K;
		break;
	case TRANS_MODE_8K:
		c->transmission_mode = TRANSMISSION_MODE_8K;
		break;
	}

	return 0;
}

static int as102_fe_get_tune_settings(struct dvb_frontend *fe,
			struct dvb_frontend_tune_settings *settings)
{

	settings->min_delay_ms = 1000;

	return 0;
}

static int as102_fe_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	int ret = 0;
	struct as102_state *state = fe->demodulator_priv;
	struct as10x_tune_status tstate = { 0 };

	/* send abilis command: GET_TUNE_STATUS */
	ret = state->ops->get_status(state->priv, &tstate);
	if (ret < 0)
		return ret;

	state->signal_strength  = tstate.signal_strength;
	state->ber  = tstate.BER;

	switch (tstate.tune_state) {
	case TUNE_STATUS_SIGNAL_DVB_OK:
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER;
		break;
	case TUNE_STATUS_STREAM_DETECTED:
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_SYNC |
			  FE_HAS_VITERBI;
		break;
	case TUNE_STATUS_STREAM_TUNED:
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_SYNC |
			  FE_HAS_LOCK | FE_HAS_VITERBI;
		break;
	default:
		*status = TUNE_STATUS_NOT_TUNED;
	}

	pr_debug("as102: tuner status: 0x%02x, strength %d, per: %d, ber: %d\n",
		 tstate.tune_state, tstate.signal_strength,
		 tstate.PER, tstate.BER);

	if (!(*status & FE_HAS_LOCK)) {
		memset(&state->demod_stats, 0, sizeof(state->demod_stats));
		return 0;
	}

	ret = state->ops->get_stats(state->priv, &state->demod_stats);
	if (ret < 0)
		memset(&state->demod_stats, 0, sizeof(state->demod_stats));

	return ret;
}

/*
 * Note:
 * - in AS102 SNR=MER
 *   - the SNR will be returned in linear terms, i.e. not in dB
 *   - the accuracy equals ±2dB for a SNR range from 4dB to 30dB
 *   - the accuracy is >2dB for SNR values outside this range
 */
static int as102_fe_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct as102_state *state = fe->demodulator_priv;

	*snr = state->demod_stats.mer;

	return 0;
}

static int as102_fe_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct as102_state *state = fe->demodulator_priv;

	*ber = state->ber;

	return 0;
}

static int as102_fe_read_signal_strength(struct dvb_frontend *fe,
					 u16 *strength)
{
	struct as102_state *state = fe->demodulator_priv;

	*strength = (((0xffff * 400) * state->signal_strength + 41000) * 2);

	return 0;
}

static int as102_fe_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct as102_state *state = fe->demodulator_priv;

	if (state->demod_stats.has_started)
		*ucblocks = state->demod_stats.bad_frame_count;
	else
		*ucblocks = 0;

	return 0;
}

static int as102_fe_ts_bus_ctrl(struct dvb_frontend *fe, int acquire)
{
	struct as102_state *state = fe->demodulator_priv;

	return state->ops->stream_ctrl(state->priv, acquire,
				      state->elna_cfg);
}

static void as102_fe_release(struct dvb_frontend *fe)
{
	struct as102_state *state = fe->demodulator_priv;

	kfree(state);
}


static const struct dvb_frontend_ops as102_fe_ops = {
	.delsys = { SYS_DVBT },
	.info = {
		.name			= "Abilis AS102 DVB-T",
		.frequency_min_hz	= 174 * MHz,
		.frequency_max_hz	= 862 * MHz,
		.frequency_stepsize_hz	= 166667,
		.caps = FE_CAN_INVERSION_AUTO
			| FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4
			| FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO
			| FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QPSK
			| FE_CAN_QAM_AUTO
			| FE_CAN_TRANSMISSION_MODE_AUTO
			| FE_CAN_GUARD_INTERVAL_AUTO
			| FE_CAN_HIERARCHY_AUTO
			| FE_CAN_RECOVER
			| FE_CAN_MUTE_TS
	},

	.set_frontend		= as102_fe_set_frontend,
	.get_frontend		= as102_fe_get_frontend,
	.get_tune_settings	= as102_fe_get_tune_settings,

	.read_status		= as102_fe_read_status,
	.read_snr		= as102_fe_read_snr,
	.read_ber		= as102_fe_read_ber,
	.read_signal_strength	= as102_fe_read_signal_strength,
	.read_ucblocks		= as102_fe_read_ucblocks,
	.ts_bus_ctrl		= as102_fe_ts_bus_ctrl,
	.release		= as102_fe_release,
};

struct dvb_frontend *as102_attach(const char *name,
				  const struct as102_fe_ops *ops,
				  void *priv,
				  uint8_t elna_cfg)
{
	struct as102_state *state;
	struct dvb_frontend *fe;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	fe = &state->frontend;
	fe->demodulator_priv = state;
	state->ops = ops;
	state->priv = priv;
	state->elna_cfg = elna_cfg;

	/* init frontend callback ops */
	memcpy(&fe->ops, &as102_fe_ops, sizeof(struct dvb_frontend_ops));
	strscpy(fe->ops.info.name, name, sizeof(fe->ops.info.name));

	return fe;

}
EXPORT_SYMBOL_GPL(as102_attach);

MODULE_DESCRIPTION("as102-fe");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pierrick Hascoet <pierrick.hascoet@abilis.com>");
