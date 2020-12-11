/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The Virtual DTV test driver serves as a reference DVB driver and helps
 * validate the existing APIs in the media subsystem. It can also aid
 * developers working on userspace applications.
 *
 * Copyright (C) 2020 Daniel W. S. Almeida
 * Based on the example driver written by Emard <emard@softhome.net>
 */

#ifndef VIDTV_DEMOD_H
#define VIDTV_DEMOD_H

#include <linux/dvb/frontend.h>
#include <media/dvb_frontend.h>

/**
 * struct vidtv_demod_cnr_to_qual_s - Map CNR values to a given combination of
 * modulation and fec_inner
 * @modulation: see enum fe_modulation
 * @fec: see enum fe_fec_rate
 *
 * This struct matches values for 'good' and 'ok' CNRs given the combination
 * of modulation and fec_inner in use. We might simulate some noise if the
 * signal quality is not too good.
 *
 * The values were taken from libdvbv5.
 */
struct vidtv_demod_cnr_to_qual_s {
	u32 modulation;
	u32 fec;
	u32 cnr_ok;
	u32 cnr_good;
};

/**
 * struct vidtv_demod_config - Configuration used to init the demod
 * @drop_tslock_prob_on_low_snr: probability of losing the lock due to low snr
 * @recover_tslock_prob_on_good_snr: probability of recovering when the signal
 * improves
 *
 * The configuration used to init the demodulator module, usually filled
 * by a bridge driver. For vidtv, this is filled by vidtv_bridge before the
 * demodulator module is probed.
 */
struct vidtv_demod_config {
	u8 drop_tslock_prob_on_low_snr;
	u8 recover_tslock_prob_on_good_snr;
};

/**
 * struct vidtv_demod_state - The demodulator state
 * @frontend: The frontend structure allocated by the demod.
 * @config: The config used to init the demod.
 * @poll_snr: The task responsible for periodically checking the simulated
 * signal quality, eventually dropping or reacquiring the TS lock.
 * @status: the demod status.
 * @cold_start: Whether the demod has not been init yet.
 * @poll_snr_thread_running: Whether the task responsible for periodically
 * checking the simulated signal quality is running.
 * @poll_snr_thread_restart: Whether we should restart the poll_snr task.
 */
struct vidtv_demod_state {
	struct dvb_frontend frontend;
	struct vidtv_demod_config config;
	enum fe_status status;
	u16 tuner_cnr;
};
#endif // VIDTV_DEMOD_H
