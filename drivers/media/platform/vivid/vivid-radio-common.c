// SPDX-License-Identifier: GPL-2.0-only
/*
 * vivid-radio-common.c - common radio rx/tx support functions.
 *
 * Copyright 2014 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/videodev2.h>

#include "vivid-core.h"
#include "vivid-ctrls.h"
#include "vivid-radio-common.h"
#include "vivid-rds-gen.h"

/*
 * These functions are shared between the vivid receiver and transmitter
 * since both use the same frequency bands.
 */

const struct v4l2_frequency_band vivid_radio_bands[TOT_BANDS] = {
	/* Band FM */
	{
		.type = V4L2_TUNER_RADIO,
		.index = 0,
		.capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO |
			      V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   = FM_FREQ_RANGE_LOW,
		.rangehigh  = FM_FREQ_RANGE_HIGH,
		.modulation = V4L2_BAND_MODULATION_FM,
	},
	/* Band AM */
	{
		.type = V4L2_TUNER_RADIO,
		.index = 1,
		.capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   = AM_FREQ_RANGE_LOW,
		.rangehigh  = AM_FREQ_RANGE_HIGH,
		.modulation = V4L2_BAND_MODULATION_AM,
	},
	/* Band SW */
	{
		.type = V4L2_TUNER_RADIO,
		.index = 2,
		.capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow   = SW_FREQ_RANGE_LOW,
		.rangehigh  = SW_FREQ_RANGE_HIGH,
		.modulation = V4L2_BAND_MODULATION_AM,
	},
};

/*
 * Initialize the RDS generator. If we can loop, then the RDS generator
 * is set up with the values from the RDS TX controls, otherwise it
 * will fill in standard values using one of two alternates.
 */
void vivid_radio_rds_init(struct vivid_dev *dev)
{
	struct vivid_rds_gen *rds = &dev->rds_gen;
	bool alt = dev->radio_rx_rds_use_alternates;

	/* Do nothing, blocks will be filled by the transmitter */
	if (dev->radio_rds_loop && !dev->radio_tx_rds_controls)
		return;

	if (dev->radio_rds_loop) {
		v4l2_ctrl_lock(dev->radio_tx_rds_pi);
		rds->picode = dev->radio_tx_rds_pi->cur.val;
		rds->pty = dev->radio_tx_rds_pty->cur.val;
		rds->mono_stereo = dev->radio_tx_rds_mono_stereo->cur.val;
		rds->art_head = dev->radio_tx_rds_art_head->cur.val;
		rds->compressed = dev->radio_tx_rds_compressed->cur.val;
		rds->dyn_pty = dev->radio_tx_rds_dyn_pty->cur.val;
		rds->ta = dev->radio_tx_rds_ta->cur.val;
		rds->tp = dev->radio_tx_rds_tp->cur.val;
		rds->ms = dev->radio_tx_rds_ms->cur.val;
		strscpy(rds->psname,
			dev->radio_tx_rds_psname->p_cur.p_char,
			sizeof(rds->psname));
		strscpy(rds->radiotext,
			dev->radio_tx_rds_radiotext->p_cur.p_char + alt * 64,
			sizeof(rds->radiotext));
		v4l2_ctrl_unlock(dev->radio_tx_rds_pi);
	} else {
		vivid_rds_gen_fill(rds, dev->radio_rx_freq, alt);
	}
	if (dev->radio_rx_rds_controls) {
		v4l2_ctrl_s_ctrl(dev->radio_rx_rds_pty, rds->pty);
		v4l2_ctrl_s_ctrl(dev->radio_rx_rds_ta, rds->ta);
		v4l2_ctrl_s_ctrl(dev->radio_rx_rds_tp, rds->tp);
		v4l2_ctrl_s_ctrl(dev->radio_rx_rds_ms, rds->ms);
		v4l2_ctrl_s_ctrl_string(dev->radio_rx_rds_psname, rds->psname);
		v4l2_ctrl_s_ctrl_string(dev->radio_rx_rds_radiotext, rds->radiotext);
		if (!dev->radio_rds_loop)
			dev->radio_rx_rds_use_alternates = !dev->radio_rx_rds_use_alternates;
	}
	vivid_rds_generate(rds);
}

/*
 * Calculate the emulated signal quality taking into account the frequency
 * the transmitter is using.
 */
static void vivid_radio_calc_sig_qual(struct vivid_dev *dev)
{
	int mod = 16000;
	int delta = 800;
	int sig_qual, sig_qual_tx = mod;

	/*
	 * For SW and FM there is a channel every 1000 kHz, for AM there is one
	 * every 100 kHz.
	 */
	if (dev->radio_rx_freq <= AM_FREQ_RANGE_HIGH) {
		mod /= 10;
		delta /= 10;
	}
	sig_qual = (dev->radio_rx_freq + delta) % mod - delta;
	if (dev->has_radio_tx)
		sig_qual_tx = dev->radio_rx_freq - dev->radio_tx_freq;
	if (abs(sig_qual_tx) <= abs(sig_qual)) {
		sig_qual = sig_qual_tx;
		/*
		 * Zero the internal rds buffer if we are going to loop
		 * rds blocks.
		 */
		if (!dev->radio_rds_loop && !dev->radio_tx_rds_controls)
			memset(dev->rds_gen.data, 0,
			       sizeof(dev->rds_gen.data));
		dev->radio_rds_loop = dev->radio_rx_freq >= FM_FREQ_RANGE_LOW;
	} else {
		dev->radio_rds_loop = false;
	}
	if (dev->radio_rx_freq <= AM_FREQ_RANGE_HIGH)
		sig_qual *= 10;
	dev->radio_rx_sig_qual = sig_qual;
}

int vivid_radio_g_frequency(struct file *file, const unsigned *pfreq, struct v4l2_frequency *vf)
{
	if (vf->tuner != 0)
		return -EINVAL;
	vf->frequency = *pfreq;
	return 0;
}

int vivid_radio_s_frequency(struct file *file, unsigned *pfreq, const struct v4l2_frequency *vf)
{
	struct vivid_dev *dev = video_drvdata(file);
	unsigned freq;
	unsigned band;

	if (vf->tuner != 0)
		return -EINVAL;

	if (vf->frequency >= (FM_FREQ_RANGE_LOW + SW_FREQ_RANGE_HIGH) / 2)
		band = BAND_FM;
	else if (vf->frequency <= (AM_FREQ_RANGE_HIGH + SW_FREQ_RANGE_LOW) / 2)
		band = BAND_AM;
	else
		band = BAND_SW;

	freq = clamp_t(u32, vf->frequency, vivid_radio_bands[band].rangelow,
					   vivid_radio_bands[band].rangehigh);
	*pfreq = freq;

	/*
	 * For both receiver and transmitter recalculate the signal quality
	 * (since that depends on both frequencies) and re-init the rds
	 * generator.
	 */
	vivid_radio_calc_sig_qual(dev);
	vivid_radio_rds_init(dev);
	return 0;
}
