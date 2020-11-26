/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The Virtual DTV test driver serves as a reference DVB driver and helps
 * validate the existing APIs in the media subsystem. It can also aid
 * developers working on userspace applications.
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 */

#ifndef VIDTV_TUNER_H
#define VIDTV_TUNER_H

#include <linux/types.h>
#include <media/dvb_frontend.h>

#define NUM_VALID_TUNER_FREQS 8

/**
 * struct vidtv_tuner_config - Configuration used to init the tuner.
 * @fe: A pointer to the dvb_frontend structure allocated by vidtv_demod.
 * @mock_power_up_delay_msec: Simulate a power-up delay.
 * @mock_tune_delay_msec: Simulate a tune delay.
 * @vidtv_valid_dvb_t_freqs: The valid DVB-T frequencies to simulate.
 * @vidtv_valid_dvb_c_freqs: The valid DVB-C frequencies to simulate.
 * @vidtv_valid_dvb_s_freqs: The valid DVB-S frequencies to simulate.
 * @max_frequency_shift_hz: The maximum frequency shift in HZ allowed when
 * tuning in a channel
 *
 * The configuration used to init the tuner module, usually filled
 * by a bridge driver. For vidtv, this is filled by vidtv_bridge before the
 * tuner module is probed.
 */
struct vidtv_tuner_config {
	struct dvb_frontend *fe;
	u32 mock_power_up_delay_msec;
	u32 mock_tune_delay_msec;
	u32 vidtv_valid_dvb_t_freqs[NUM_VALID_TUNER_FREQS];
	u32 vidtv_valid_dvb_c_freqs[NUM_VALID_TUNER_FREQS];
	u32 vidtv_valid_dvb_s_freqs[NUM_VALID_TUNER_FREQS];
	u8  max_frequency_shift_hz;
};

#endif //VIDTV_TUNER_H
