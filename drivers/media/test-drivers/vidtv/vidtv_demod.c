// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * The Virtual DVB test driver serves as a reference DVB driver and helps
 * validate the existing APIs in the media subsystem. It can also aid
 * developers working on userspace applications.
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 * Based on the example driver written by Emard <emard@softhome.net>
 */

#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/random.h>
#include <linux/ratelimit.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include <media/dvb_frontend.h>

#include "vidtv_demod.h"

#define POLL_THRD_TIME 2000 /* ms */

static const struct vidtv_demod_cnr_to_qual_s vidtv_demod_c_cnr_2_qual[] = {
	/* from libdvbv5 source code, in milli db */
	{ QAM_256, FEC_NONE,  34000, 38000},
	{ QAM_64,  FEC_NONE,  30000, 34000},
};

static const struct vidtv_demod_cnr_to_qual_s vidtv_demod_s_cnr_2_qual[] = {
	/* from libdvbv5 source code, in milli db */
	{ QPSK, FEC_1_2,  7000, 10000},
	{ QPSK, FEC_2_3,  9000, 12000},
	{ QPSK, FEC_3_4, 10000, 13000},
	{ QPSK, FEC_5_6, 11000, 14000},
	{ QPSK, FEC_7_8, 12000, 15000},
};

static const struct vidtv_demod_cnr_to_qual_s vidtv_demod_s2_cnr_2_qual[] = {
	/* from libdvbv5 source code, in milli db */
	{ QPSK,  FEC_1_2,   9000,  12000},
	{ QPSK,  FEC_2_3,  11000,  14000},
	{ QPSK,  FEC_3_4,  12000,  15000},
	{ QPSK,  FEC_5_6,  12000,  15000},
	{ QPSK,  FEC_8_9,  13000,  16000},
	{ QPSK,  FEC_9_10, 13500,  16500},
	{ PSK_8, FEC_2_3,  14500,  17500},
	{ PSK_8, FEC_3_4,  16000,  19000},
	{ PSK_8, FEC_5_6,  17500,  20500},
	{ PSK_8, FEC_8_9,  19000,  22000},
};

static const struct vidtv_demod_cnr_to_qual_s vidtv_demod_t_cnr_2_qual[] = {
	/* from libdvbv5 source code, in milli db*/
	{   QPSK, FEC_1_2,  4100,  5900},
	{   QPSK, FEC_2_3,  6100,  9600},
	{   QPSK, FEC_3_4,  7200, 12400},
	{   QPSK, FEC_5_6,  8500, 15600},
	{   QPSK, FEC_7_8,  9200, 17500},
	{ QAM_16, FEC_1_2,  9800, 11800},
	{ QAM_16, FEC_2_3, 12100, 15300},
	{ QAM_16, FEC_3_4, 13400, 18100},
	{ QAM_16, FEC_5_6, 14800, 21300},
	{ QAM_16, FEC_7_8, 15700, 23600},
	{ QAM_64, FEC_1_2, 14000, 16000},
	{ QAM_64, FEC_2_3, 19900, 25400},
	{ QAM_64, FEC_3_4, 24900, 27900},
	{ QAM_64, FEC_5_6, 21300, 23300},
	{ QAM_64, FEC_7_8, 22000, 24000},
};

static const struct vidtv_demod_cnr_to_qual_s *vidtv_match_cnr_s(struct dvb_frontend *fe)
{
	const struct vidtv_demod_cnr_to_qual_s *cnr2qual = NULL;
	struct device *dev = fe->dvb->device;
	struct dtv_frontend_properties *c;
	u32 array_size = 0;
	u32 i;

	c = &fe->dtv_property_cache;

	switch (c->delivery_system) {
	case SYS_DVBT:
	case SYS_DVBT2:
		cnr2qual   = vidtv_demod_t_cnr_2_qual;
		array_size = ARRAY_SIZE(vidtv_demod_t_cnr_2_qual);
		break;

	case SYS_DVBS:
		cnr2qual   = vidtv_demod_s_cnr_2_qual;
		array_size = ARRAY_SIZE(vidtv_demod_s_cnr_2_qual);
		break;

	case SYS_DVBS2:
		cnr2qual   = vidtv_demod_s2_cnr_2_qual;
		array_size = ARRAY_SIZE(vidtv_demod_s2_cnr_2_qual);
		break;

	case SYS_DVBC_ANNEX_A:
		cnr2qual   = vidtv_demod_c_cnr_2_qual;
		array_size = ARRAY_SIZE(vidtv_demod_c_cnr_2_qual);
		break;

	default:
		dev_warn_ratelimited(dev,
				     "%s: unsupported delivery system: %u\n",
				     __func__,
				     c->delivery_system);
		break;
	}

	for (i = 0; i < array_size; i++)
		if (cnr2qual[i].modulation == c->modulation &&
		    cnr2qual[i].fec == c->fec_inner)
			return &cnr2qual[i];

	return NULL; /* not found */
}

static void vidtv_clean_stats(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	/* Fill the length of each status counter */

	/* Signal is always available */
	c->strength.len = 1;
	c->strength.stat[0].scale = FE_SCALE_DECIBEL;
	c->strength.stat[0].svalue = 0;

	/* Usually available only after Viterbi lock */
	c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->cnr.stat[0].svalue = 0;
	c->cnr.len = 1;

	/* Those depends on full lock */
	c->pre_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->pre_bit_error.stat[0].uvalue = 0;
	c->pre_bit_error.len = 1;
	c->pre_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->pre_bit_count.stat[0].uvalue = 0;
	c->pre_bit_count.len = 1;
	c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_error.stat[0].uvalue = 0;
	c->post_bit_error.len = 1;
	c->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_count.stat[0].uvalue = 0;
	c->post_bit_count.len = 1;
	c->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->block_error.stat[0].uvalue = 0;
	c->block_error.len = 1;
	c->block_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->block_count.stat[0].uvalue = 0;
	c->block_count.len = 1;
}

static void vidtv_demod_update_stats(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct vidtv_demod_state *state = fe->demodulator_priv;
	u32 scale;

	if (state->status & FE_HAS_LOCK) {
		scale = FE_SCALE_COUNTER;
		c->cnr.stat[0].scale = FE_SCALE_DECIBEL;
	} else {
		scale = FE_SCALE_NOT_AVAILABLE;
		c->cnr.stat[0].scale = scale;
	}

	c->pre_bit_error.stat[0].scale = scale;
	c->pre_bit_count.stat[0].scale = scale;
	c->post_bit_error.stat[0].scale = scale;
	c->post_bit_count.stat[0].scale = scale;
	c->block_error.stat[0].scale = scale;
	c->block_count.stat[0].scale = scale;

	/*
	 * Add a 0.5% of randomness at the signal strength and CNR,
	 * and make them different, as we want to have something closer
	 * to a real case scenario.
	 *
	 * Also, usually, signal strength is a negative number in dBm.
	 */
	c->strength.stat[0].svalue = state->tuner_cnr;
	c->strength.stat[0].svalue -= get_random_u32_below(state->tuner_cnr / 50);
	c->strength.stat[0].svalue -= 68000; /* Adjust to a better range */

	c->cnr.stat[0].svalue = state->tuner_cnr;
	c->cnr.stat[0].svalue -= get_random_u32_below(state->tuner_cnr / 50);
}

static int vidtv_demod_read_status(struct dvb_frontend *fe,
				   enum fe_status *status)
{
	struct vidtv_demod_state *state = fe->demodulator_priv;
	const struct vidtv_demod_cnr_to_qual_s *cnr2qual = NULL;
	struct vidtv_demod_config *config = &state->config;
	u16 snr = 0;

	/* Simulate random lost of signal due to a bad-tuned channel */
	cnr2qual = vidtv_match_cnr_s(&state->frontend);

	if (cnr2qual && state->tuner_cnr < cnr2qual->cnr_good &&
	    state->frontend.ops.tuner_ops.get_rf_strength) {
		state->frontend.ops.tuner_ops.get_rf_strength(&state->frontend,
							      &snr);

		if (snr < cnr2qual->cnr_ok) {
			/* eventually lose the TS lock */
			if (get_random_u32_below(100) < config->drop_tslock_prob_on_low_snr)
				state->status = 0;
		} else {
			/* recover if the signal improves */
			if (get_random_u32_below(100) <
			    config->recover_tslock_prob_on_good_snr)
				state->status = FE_HAS_SIGNAL  |
						FE_HAS_CARRIER |
						FE_HAS_VITERBI |
						FE_HAS_SYNC    |
						FE_HAS_LOCK;
		}
	}

	vidtv_demod_update_stats(&state->frontend);

	*status = state->status;

	return 0;
}

static int vidtv_demod_read_signal_strength(struct dvb_frontend *fe,
					    u16 *strength)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	*strength = c->strength.stat[0].uvalue;

	return 0;
}

/*
 * NOTE:
 * This is implemented here just to be used as an example for real
 * demod drivers.
 *
 * Should only be implemented if it actually reads something from the hardware.
 * Also, it should check for the locks, in order to avoid report wrong data
 * to userspace.
 */
static int vidtv_demod_get_frontend(struct dvb_frontend *fe,
				    struct dtv_frontend_properties *p)
{
	return 0;
}

static int vidtv_demod_set_frontend(struct dvb_frontend *fe)
{
	struct vidtv_demod_state *state = fe->demodulator_priv;
	u32 tuner_status = 0;
	int ret;

	if (!fe->ops.tuner_ops.set_params)
		return 0;

	fe->ops.tuner_ops.set_params(fe);

	/* store the CNR returned by the tuner */
	ret = fe->ops.tuner_ops.get_rf_strength(fe, &state->tuner_cnr);
	if (ret < 0)
		return ret;

	fe->ops.tuner_ops.get_status(fe, &tuner_status);
	state->status = (state->tuner_cnr > 0) ?  FE_HAS_SIGNAL  |
						    FE_HAS_CARRIER |
						    FE_HAS_VITERBI |
						    FE_HAS_SYNC    |
						    FE_HAS_LOCK	 :
						    0;

	vidtv_demod_update_stats(fe);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return 0;
}

/*
 * NOTE:
 * This is implemented here just to be used as an example for real
 * demod drivers.
 *
 * Should only be implemented if the demod has support for DVB-S or DVB-S2
 */
static int vidtv_demod_set_tone(struct dvb_frontend *fe,
				enum fe_sec_tone_mode tone)
{
	return 0;
}

/*
 * NOTE:
 * This is implemented here just to be used as an example for real
 * demod drivers.
 *
 * Should only be implemented if the demod has support for DVB-S or DVB-S2
 */
static int vidtv_demod_set_voltage(struct dvb_frontend *fe,
				   enum fe_sec_voltage voltage)
{
	return 0;
}

/*
 * NOTE:
 * This is implemented here just to be used as an example for real
 * demod drivers.
 *
 * Should only be implemented if the demod has support for DVB-S or DVB-S2
 */
static int vidtv_send_diseqc_msg(struct dvb_frontend *fe,
				 struct dvb_diseqc_master_cmd *cmd)
{
	return 0;
}

/*
 * NOTE:
 * This is implemented here just to be used as an example for real
 * demod drivers.
 *
 * Should only be implemented if the demod has support for DVB-S or DVB-S2
 */
static int vidtv_diseqc_send_burst(struct dvb_frontend *fe,
				   enum fe_sec_mini_cmd burst)
{
	return 0;
}

static void vidtv_demod_release(struct dvb_frontend *fe)
{
	struct vidtv_demod_state *state = fe->demodulator_priv;

	kfree(state);
}

static const struct dvb_frontend_ops vidtv_demod_ops = {
	.delsys = {
		SYS_DVBT,
		SYS_DVBT2,
		SYS_DVBC_ANNEX_A,
		SYS_DVBS,
		SYS_DVBS2,
	},

	.info = {
		.name                   = "Dummy demod for DVB-T/T2/C/S/S2",
		.frequency_min_hz       = 51 * MHz,
		.frequency_max_hz       = 2150 * MHz,
		.frequency_stepsize_hz  = 62500,
		.frequency_tolerance_hz = 29500 * kHz,
		.symbol_rate_min        = 1000000,
		.symbol_rate_max        = 45000000,

		.caps = FE_CAN_FEC_1_2 |
			FE_CAN_FEC_2_3 |
			FE_CAN_FEC_3_4 |
			FE_CAN_FEC_4_5 |
			FE_CAN_FEC_5_6 |
			FE_CAN_FEC_6_7 |
			FE_CAN_FEC_7_8 |
			FE_CAN_FEC_8_9 |
			FE_CAN_QAM_16 |
			FE_CAN_QAM_64 |
			FE_CAN_QAM_32 |
			FE_CAN_QAM_128 |
			FE_CAN_QAM_256 |
			FE_CAN_QAM_AUTO |
			FE_CAN_QPSK |
			FE_CAN_FEC_AUTO |
			FE_CAN_INVERSION_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO,
	},

	.release = vidtv_demod_release,

	.set_frontend = vidtv_demod_set_frontend,
	.get_frontend = vidtv_demod_get_frontend,

	.read_status          = vidtv_demod_read_status,
	.read_signal_strength = vidtv_demod_read_signal_strength,

	/* For DVB-S/S2 */
	.set_voltage		= vidtv_demod_set_voltage,
	.set_tone		= vidtv_demod_set_tone,
	.diseqc_send_master_cmd	= vidtv_send_diseqc_msg,
	.diseqc_send_burst	= vidtv_diseqc_send_burst,

};

static const struct i2c_device_id vidtv_demod_i2c_id_table[] = {
	{"dvb_vidtv_demod", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, vidtv_demod_i2c_id_table);

static int vidtv_demod_i2c_probe(struct i2c_client *client)
{
	struct vidtv_tuner_config *config = client->dev.platform_data;
	struct vidtv_demod_state *state;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops,
	       &vidtv_demod_ops,
	       sizeof(struct dvb_frontend_ops));

	memcpy(&state->config, config, sizeof(state->config));

	state->frontend.demodulator_priv = state;
	i2c_set_clientdata(client, state);

	vidtv_clean_stats(&state->frontend);

	return 0;
}

static void vidtv_demod_i2c_remove(struct i2c_client *client)
{
	struct vidtv_demod_state *state = i2c_get_clientdata(client);

	kfree(state);
}

static struct i2c_driver vidtv_demod_i2c_driver = {
	.driver = {
		.name                = "dvb_vidtv_demod",
		.suppress_bind_attrs = true,
	},
	.probe    = vidtv_demod_i2c_probe,
	.remove   = vidtv_demod_i2c_remove,
	.id_table = vidtv_demod_i2c_id_table,
};

module_i2c_driver(vidtv_demod_i2c_driver);

MODULE_DESCRIPTION("Virtual DVB Demodulator Driver");
MODULE_AUTHOR("Daniel W. S. Almeida");
MODULE_LICENSE("GPL");
