// SPDX-License-Identifier: GPL-2.0
/*
 * The Virtual DVB test driver serves as a reference DVB driver and helps
 * validate the existing APIs in the media subsystem. It can also aid
 * developers working on userspace applications.
 *
 * The vidtv tuner should support common TV standards such as
 * DVB-T/T2/S/S2, ISDB-T and ATSC when completed.
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 */

#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <media/dvb_frontend.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>

#include "vidtv_tuner.h"

struct vidtv_tuner_cnr_to_qual_s {
	/* attempt to use the same values as libdvbv5 */
	u32 modulation;
	u32 fec;
	u32 cnr_ok;
	u32 cnr_good;
};

static const struct vidtv_tuner_cnr_to_qual_s vidtv_tuner_c_cnr_2_qual[] = {
	/* from libdvbv5 source code, in milli db */
	{ QAM_256, FEC_NONE,  34000, 38000},
	{ QAM_64,  FEC_NONE,  30000, 34000},
};

static const struct vidtv_tuner_cnr_to_qual_s vidtv_tuner_s_cnr_2_qual[] = {
	/* from libdvbv5 source code, in milli db */
	{ QPSK, FEC_1_2,  7000, 10000},
	{ QPSK, FEC_2_3,  9000, 12000},
	{ QPSK, FEC_3_4, 10000, 13000},
	{ QPSK, FEC_5_6, 11000, 14000},
	{ QPSK, FEC_7_8, 12000, 15000},
};

static const struct vidtv_tuner_cnr_to_qual_s vidtv_tuner_s2_cnr_2_qual[] = {
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

static const struct vidtv_tuner_cnr_to_qual_s vidtv_tuner_t_cnr_2_qual[] = {
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

/**
 * struct vidtv_tuner_hardware_state - Simulate the tuner hardware status
 * @asleep: whether the tuner is asleep, i.e whether _sleep() or _suspend() was
 * called.
 * @lock_status: Whether the tuner has managed to lock on the requested
 * frequency.
 * @if_frequency: The tuner's intermediate frequency. Hardcoded for the purposes
 * of simulation.
 * @tuned_frequency: The actual tuned frequency.
 * @bandwidth: The actual bandwidth.
 *
 * This structure is meant to simulate the status of the tuner hardware, as if
 * we had a physical tuner hardware.
 */
struct vidtv_tuner_hardware_state {
	bool asleep;
	u32 lock_status;
	u32 if_frequency;
	u32 tuned_frequency;
	u32 bandwidth;
};

/**
 * struct vidtv_tuner_dev - The tuner struct
 * @fe: A pointer to the dvb_frontend structure allocated by vidtv_demod
 * @hw_state: A struct to simulate the tuner's hardware state as if we had a
 * physical tuner hardware.
 * @config: The configuration used to start the tuner module, usually filled
 * by a bridge driver. For vidtv, this is filled by vidtv_bridge before the
 * tuner module is probed.
 */
struct vidtv_tuner_dev {
	struct dvb_frontend *fe;
	struct vidtv_tuner_hardware_state hw_state;
	struct vidtv_tuner_config config;
};

static struct vidtv_tuner_dev*
vidtv_tuner_get_dev(struct dvb_frontend *fe)
{
	return i2c_get_clientdata(fe->tuner_priv);
}

static int vidtv_tuner_check_frequency_shift(struct dvb_frontend *fe)
{
	struct vidtv_tuner_dev *tuner_dev = vidtv_tuner_get_dev(fe);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct vidtv_tuner_config config  = tuner_dev->config;
	u32 *valid_freqs = NULL;
	u32 array_sz = 0;
	u32 i;
	u32 shift;

	switch (c->delivery_system) {
	case SYS_DVBT:
	case SYS_DVBT2:
		valid_freqs = config.vidtv_valid_dvb_t_freqs;
		array_sz    = ARRAY_SIZE(config.vidtv_valid_dvb_t_freqs);
		break;
	case SYS_DVBS:
	case SYS_DVBS2:
		valid_freqs = config.vidtv_valid_dvb_s_freqs;
		array_sz    = ARRAY_SIZE(config.vidtv_valid_dvb_s_freqs);
		break;
	case SYS_DVBC_ANNEX_A:
		valid_freqs = config.vidtv_valid_dvb_c_freqs;
		array_sz    = ARRAY_SIZE(config.vidtv_valid_dvb_c_freqs);
		break;

	default:
		dev_warn(fe->dvb->device,
			 "%s: unsupported delivery system: %u\n",
			 __func__,
			 c->delivery_system);

		return -EINVAL;
	}

	for (i = 0; i < array_sz; i++) {
		if (!valid_freqs[i])
			break;
		shift = abs(c->frequency - valid_freqs[i]);

		if (!shift)
			return 0;

		/*
		 * This will provide a value from 0 to 100 that would
		 * indicate how far is the tuned frequency from the
		 * right one.
		 */
		if (shift < config.max_frequency_shift_hz)
			return shift * 100 / config.max_frequency_shift_hz;
	}

	return -EINVAL;
}

static int
vidtv_tuner_get_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct vidtv_tuner_dev *tuner_dev = vidtv_tuner_get_dev(fe);
	const struct vidtv_tuner_cnr_to_qual_s *cnr2qual = NULL;
	struct device *dev = fe->dvb->device;
	u32 array_size = 0;
	s32 shift;
	u32 i;

	shift = vidtv_tuner_check_frequency_shift(fe);
	if (shift < 0) {
		tuner_dev->hw_state.lock_status = 0;
		*strength = 0;
		return 0;
	}

	switch (c->delivery_system) {
	case SYS_DVBT:
	case SYS_DVBT2:
		cnr2qual   = vidtv_tuner_t_cnr_2_qual;
		array_size = ARRAY_SIZE(vidtv_tuner_t_cnr_2_qual);
		break;

	case SYS_DVBS:
		cnr2qual   = vidtv_tuner_s_cnr_2_qual;
		array_size = ARRAY_SIZE(vidtv_tuner_s_cnr_2_qual);
		break;

	case SYS_DVBS2:
		cnr2qual   = vidtv_tuner_s2_cnr_2_qual;
		array_size = ARRAY_SIZE(vidtv_tuner_s2_cnr_2_qual);
		break;

	case SYS_DVBC_ANNEX_A:
		cnr2qual   = vidtv_tuner_c_cnr_2_qual;
		array_size = ARRAY_SIZE(vidtv_tuner_c_cnr_2_qual);
		break;

	default:
		dev_warn_ratelimited(dev,
				     "%s: unsupported delivery system: %u\n",
				     __func__,
				     c->delivery_system);
		return -EINVAL;
	}

	for (i = 0; i < array_size; i++) {
		if (cnr2qual[i].modulation != c->modulation ||
		    cnr2qual[i].fec != c->fec_inner)
			continue;

		if (!shift) {
			*strength = cnr2qual[i].cnr_good;
			return 0;
		}
		/*
		 * Channel tuned at wrong frequency. Simulate that the
		 * Carrier S/N ratio is not too good.
		 */

		*strength = cnr2qual[i].cnr_ok -
			    (cnr2qual[i].cnr_good - cnr2qual[i].cnr_ok);
		return 0;
	}

	/*
	 * do a linear interpolation between 34dB and 10dB if we can't
	 * match against the table
	 */
	*strength = 34000 - 24000 * shift / 100;
	return 0;
}

static int vidtv_tuner_init(struct dvb_frontend *fe)
{
	struct vidtv_tuner_dev *tuner_dev = vidtv_tuner_get_dev(fe);
	struct vidtv_tuner_config config  = tuner_dev->config;

	msleep_interruptible(config.mock_power_up_delay_msec);

	tuner_dev->hw_state.asleep = false;
	tuner_dev->hw_state.if_frequency = 5000;

	return 0;
}

static int vidtv_tuner_sleep(struct dvb_frontend *fe)
{
	struct vidtv_tuner_dev *tuner_dev = vidtv_tuner_get_dev(fe);

	tuner_dev->hw_state.asleep = true;
	return 0;
}

static int vidtv_tuner_suspend(struct dvb_frontend *fe)
{
	struct vidtv_tuner_dev *tuner_dev = vidtv_tuner_get_dev(fe);

	tuner_dev->hw_state.asleep = true;
	return 0;
}

static int vidtv_tuner_resume(struct dvb_frontend *fe)
{
	struct vidtv_tuner_dev *tuner_dev = vidtv_tuner_get_dev(fe);

	tuner_dev->hw_state.asleep = false;
	return 0;
}

static int vidtv_tuner_set_params(struct dvb_frontend *fe)
{
	struct vidtv_tuner_dev *tuner_dev = vidtv_tuner_get_dev(fe);
	struct vidtv_tuner_config config  = tuner_dev->config;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	s32 shift;

	u32 min_freq = fe->ops.tuner_ops.info.frequency_min_hz;
	u32 max_freq = fe->ops.tuner_ops.info.frequency_max_hz;
	u32 min_bw = fe->ops.tuner_ops.info.bandwidth_min;
	u32 max_bw = fe->ops.tuner_ops.info.bandwidth_max;

	if (c->frequency < min_freq  || c->frequency > max_freq  ||
	    c->bandwidth_hz < min_bw || c->bandwidth_hz > max_bw) {
		tuner_dev->hw_state.lock_status = 0;
		return -EINVAL;
	}

	tuner_dev->hw_state.tuned_frequency = c->frequency;
	tuner_dev->hw_state.bandwidth = c->bandwidth_hz;
	tuner_dev->hw_state.lock_status = TUNER_STATUS_LOCKED;

	msleep_interruptible(config.mock_tune_delay_msec);

	shift = vidtv_tuner_check_frequency_shift(fe);
	if (shift < 0) {
		tuner_dev->hw_state.lock_status = 0;
		return shift;
	}

	return 0;
}

static int vidtv_tuner_set_config(struct dvb_frontend *fe,
				  void *priv_cfg)
{
	struct vidtv_tuner_dev *tuner_dev = vidtv_tuner_get_dev(fe);

	memcpy(&tuner_dev->config, priv_cfg, sizeof(tuner_dev->config));

	return 0;
}

static int vidtv_tuner_get_frequency(struct dvb_frontend *fe,
				     u32 *frequency)
{
	struct vidtv_tuner_dev *tuner_dev = vidtv_tuner_get_dev(fe);

	*frequency = tuner_dev->hw_state.tuned_frequency;

	return 0;
}

static int vidtv_tuner_get_bandwidth(struct dvb_frontend *fe,
				     u32 *bandwidth)
{
	struct vidtv_tuner_dev *tuner_dev = vidtv_tuner_get_dev(fe);

	*bandwidth = tuner_dev->hw_state.bandwidth;

	return 0;
}

static int vidtv_tuner_get_if_frequency(struct dvb_frontend *fe,
					u32 *frequency)
{
	struct vidtv_tuner_dev *tuner_dev = vidtv_tuner_get_dev(fe);

	*frequency = tuner_dev->hw_state.if_frequency;

	return 0;
}

static int vidtv_tuner_get_status(struct dvb_frontend *fe, u32 *status)
{
	struct vidtv_tuner_dev *tuner_dev = vidtv_tuner_get_dev(fe);

	*status = tuner_dev->hw_state.lock_status;

	return 0;
}

static const struct dvb_tuner_ops vidtv_tuner_ops = {
	.init             = vidtv_tuner_init,
	.sleep            = vidtv_tuner_sleep,
	.suspend          = vidtv_tuner_suspend,
	.resume           = vidtv_tuner_resume,
	.set_params       = vidtv_tuner_set_params,
	.set_config       = vidtv_tuner_set_config,
	.get_bandwidth    = vidtv_tuner_get_bandwidth,
	.get_frequency    = vidtv_tuner_get_frequency,
	.get_if_frequency = vidtv_tuner_get_if_frequency,
	.get_status       = vidtv_tuner_get_status,
	.get_rf_strength  = vidtv_tuner_get_signal_strength
};

static const struct i2c_device_id vidtv_tuner_i2c_id_table[] = {
	{"dvb_vidtv_tuner", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, vidtv_tuner_i2c_id_table);

static int vidtv_tuner_i2c_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct vidtv_tuner_config *config = client->dev.platform_data;
	struct dvb_frontend *fe           = config->fe;
	struct vidtv_tuner_dev *tuner_dev = NULL;

	tuner_dev = kzalloc(sizeof(*tuner_dev), GFP_KERNEL);
	if (!tuner_dev)
		return -ENOMEM;

	tuner_dev->fe = config->fe;
	i2c_set_clientdata(client, tuner_dev);

	memcpy(&fe->ops.tuner_ops,
	       &vidtv_tuner_ops,
	       sizeof(struct dvb_tuner_ops));

	memcpy(&tuner_dev->config, config, sizeof(tuner_dev->config));
	fe->tuner_priv = client;

	return 0;
}

static int vidtv_tuner_i2c_remove(struct i2c_client *client)
{
	struct vidtv_tuner_dev *tuner_dev = i2c_get_clientdata(client);

	kfree(tuner_dev);

	return 0;
}

static struct i2c_driver vidtv_tuner_i2c_driver = {
	.driver = {
		.name                = "dvb_vidtv_tuner",
		.suppress_bind_attrs = true,
	},
	.probe    = vidtv_tuner_i2c_probe,
	.remove   = vidtv_tuner_i2c_remove,
	.id_table = vidtv_tuner_i2c_id_table,
};
module_i2c_driver(vidtv_tuner_i2c_driver);

MODULE_DESCRIPTION("Virtual DVB Tuner");
MODULE_AUTHOR("Daniel W. S. Almeida");
MODULE_LICENSE("GPL");
