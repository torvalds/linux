/*
 * drivers/media/radio/radio-si476x.c -- V4L2 driver for SI476X chips
 *
 * Copyright (C) 2012 Innovative Converged Devices(ICD)
 * Copyright (C) 2013 Andrey Smirnov
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/videodev2.h>
#include <linux/mutex.h>
#include <linux/debugfs.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-device.h>

#include <media/si476x.h>
#include <linux/mfd/si476x-core.h>

#define FM_FREQ_RANGE_LOW   64000000
#define FM_FREQ_RANGE_HIGH 108000000

#define AM_FREQ_RANGE_LOW    520000
#define AM_FREQ_RANGE_HIGH 30000000

#define PWRLINEFLTR (1 << 8)

#define FREQ_MUL (10000000 / 625)

#define SI476X_PHDIV_STATUS_LINK_LOCKED(status) (0x80 & (status))

#define DRIVER_NAME "si476x-radio"
#define DRIVER_CARD "SI476x AM/FM Receiver"

enum si476x_freq_bands {
	SI476X_BAND_FM,
	SI476X_BAND_AM,
};

static const struct v4l2_frequency_band si476x_bands[] = {
	[SI476X_BAND_FM] = {
		.type		= V4L2_TUNER_RADIO,
		.index		= SI476X_BAND_FM,
		.capability	= V4L2_TUNER_CAP_LOW
		| V4L2_TUNER_CAP_STEREO
		| V4L2_TUNER_CAP_RDS
		| V4L2_TUNER_CAP_RDS_BLOCK_IO
		| V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow	=  64 * FREQ_MUL,
		.rangehigh	= 108 * FREQ_MUL,
		.modulation	= V4L2_BAND_MODULATION_FM,
	},
	[SI476X_BAND_AM] = {
		.type		= V4L2_TUNER_RADIO,
		.index		= SI476X_BAND_AM,
		.capability	= V4L2_TUNER_CAP_LOW
		| V4L2_TUNER_CAP_FREQ_BANDS,
		.rangelow	= 0.52 * FREQ_MUL,
		.rangehigh	= 30 * FREQ_MUL,
		.modulation	= V4L2_BAND_MODULATION_AM,
	},
};

static inline bool si476x_radio_freq_is_inside_of_the_band(u32 freq, int band)
{
	return freq >= si476x_bands[band].rangelow &&
		freq <= si476x_bands[band].rangehigh;
}

static inline bool si476x_radio_range_is_inside_of_the_band(u32 low, u32 high,
							    int band)
{
	return low  >= si476x_bands[band].rangelow &&
		high <= si476x_bands[band].rangehigh;
}

static int si476x_radio_s_ctrl(struct v4l2_ctrl *ctrl);
static int si476x_radio_g_volatile_ctrl(struct v4l2_ctrl *ctrl);

enum phase_diversity_modes_idx {
	SI476X_IDX_PHDIV_DISABLED,
	SI476X_IDX_PHDIV_PRIMARY_COMBINING,
	SI476X_IDX_PHDIV_PRIMARY_ANTENNA,
	SI476X_IDX_PHDIV_SECONDARY_ANTENNA,
	SI476X_IDX_PHDIV_SECONDARY_COMBINING,
};

static const char * const phase_diversity_modes[] = {
	[SI476X_IDX_PHDIV_DISABLED]		= "Disabled",
	[SI476X_IDX_PHDIV_PRIMARY_COMBINING]	= "Primary with Secondary",
	[SI476X_IDX_PHDIV_PRIMARY_ANTENNA]	= "Primary Antenna",
	[SI476X_IDX_PHDIV_SECONDARY_ANTENNA]	= "Secondary Antenna",
	[SI476X_IDX_PHDIV_SECONDARY_COMBINING]	= "Secondary with Primary",
};

static inline enum phase_diversity_modes_idx
si476x_phase_diversity_mode_to_idx(enum si476x_phase_diversity_mode mode)
{
	switch (mode) {
	default:		/* FALLTHROUGH */
	case SI476X_PHDIV_DISABLED:
		return SI476X_IDX_PHDIV_DISABLED;
	case SI476X_PHDIV_PRIMARY_COMBINING:
		return SI476X_IDX_PHDIV_PRIMARY_COMBINING;
	case SI476X_PHDIV_PRIMARY_ANTENNA:
		return SI476X_IDX_PHDIV_PRIMARY_ANTENNA;
	case SI476X_PHDIV_SECONDARY_ANTENNA:
		return SI476X_IDX_PHDIV_SECONDARY_ANTENNA;
	case SI476X_PHDIV_SECONDARY_COMBINING:
		return SI476X_IDX_PHDIV_SECONDARY_COMBINING;
	}
}

static inline enum si476x_phase_diversity_mode
si476x_phase_diversity_idx_to_mode(enum phase_diversity_modes_idx idx)
{
	static const int idx_to_value[] = {
		[SI476X_IDX_PHDIV_DISABLED]		= SI476X_PHDIV_DISABLED,
		[SI476X_IDX_PHDIV_PRIMARY_COMBINING]	= SI476X_PHDIV_PRIMARY_COMBINING,
		[SI476X_IDX_PHDIV_PRIMARY_ANTENNA]	= SI476X_PHDIV_PRIMARY_ANTENNA,
		[SI476X_IDX_PHDIV_SECONDARY_ANTENNA]	= SI476X_PHDIV_SECONDARY_ANTENNA,
		[SI476X_IDX_PHDIV_SECONDARY_COMBINING]	= SI476X_PHDIV_SECONDARY_COMBINING,
	};

	return idx_to_value[idx];
}

static const struct v4l2_ctrl_ops si476x_ctrl_ops = {
	.g_volatile_ctrl	= si476x_radio_g_volatile_ctrl,
	.s_ctrl			= si476x_radio_s_ctrl,
};


enum si476x_ctrl_idx {
	SI476X_IDX_RSSI_THRESHOLD,
	SI476X_IDX_SNR_THRESHOLD,
	SI476X_IDX_MAX_TUNE_ERROR,
	SI476X_IDX_HARMONICS_COUNT,
	SI476X_IDX_DIVERSITY_MODE,
	SI476X_IDX_INTERCHIP_LINK,
};
static struct v4l2_ctrl_config si476x_ctrls[] = {

	/**
	 * SI476X during its station seeking(or tuning) process uses several
	 * parameters to detrmine if "the station" is valid:
	 *
	 *	- Signal's SNR(in dBuV) must be lower than
	 *	#V4L2_CID_SI476X_SNR_THRESHOLD
	 *	- Signal's RSSI(in dBuV) must be greater than
	 *	#V4L2_CID_SI476X_RSSI_THRESHOLD
	 *	- Signal's frequency deviation(in units of 2ppm) must not be
	 *	more than #V4L2_CID_SI476X_MAX_TUNE_ERROR
	 */
	[SI476X_IDX_RSSI_THRESHOLD] = {
		.ops	= &si476x_ctrl_ops,
		.id	= V4L2_CID_SI476X_RSSI_THRESHOLD,
		.name	= "Valid RSSI Threshold",
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.min	= -128,
		.max	= 127,
		.step	= 1,
	},
	[SI476X_IDX_SNR_THRESHOLD] = {
		.ops	= &si476x_ctrl_ops,
		.id	= V4L2_CID_SI476X_SNR_THRESHOLD,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "Valid SNR Threshold",
		.min	= -128,
		.max	= 127,
		.step	= 1,
	},
	[SI476X_IDX_MAX_TUNE_ERROR] = {
		.ops	= &si476x_ctrl_ops,
		.id	= V4L2_CID_SI476X_MAX_TUNE_ERROR,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "Max Tune Errors",
		.min	= 0,
		.max	= 126 * 2,
		.step	= 2,
	},

	/**
	 * #V4L2_CID_SI476X_HARMONICS_COUNT -- number of harmonics
	 * built-in power-line noise supression filter is to reject
	 * during AM-mode operation.
	 */
	[SI476X_IDX_HARMONICS_COUNT] = {
		.ops	= &si476x_ctrl_ops,
		.id	= V4L2_CID_SI476X_HARMONICS_COUNT,
		.type	= V4L2_CTRL_TYPE_INTEGER,

		.name	= "Count of Harmonics to Reject",
		.min	= 0,
		.max	= 20,
		.step	= 1,
	},

	/**
	 * #V4L2_CID_SI476X_DIVERSITY_MODE -- configuration which
	 * two tuners working in diversity mode are to work in.
	 *
	 *  - #SI476X_IDX_PHDIV_DISABLED diversity mode disabled
	 *  - #SI476X_IDX_PHDIV_PRIMARY_COMBINING diversity mode is
	 *  on, primary tuner's antenna is the main one.
	 *  - #SI476X_IDX_PHDIV_PRIMARY_ANTENNA diversity mode is
	 *  off, primary tuner's antenna is the main one.
	 *  - #SI476X_IDX_PHDIV_SECONDARY_ANTENNA diversity mode is
	 *  off, secondary tuner's antenna is the main one.
	 *  - #SI476X_IDX_PHDIV_SECONDARY_COMBINING diversity mode is
	 *  on, secondary tuner's antenna is the main one.
	 */
	[SI476X_IDX_DIVERSITY_MODE] = {
		.ops	= &si476x_ctrl_ops,
		.id	= V4L2_CID_SI476X_DIVERSITY_MODE,
		.type	= V4L2_CTRL_TYPE_MENU,
		.name	= "Phase Diversity Mode",
		.qmenu	= phase_diversity_modes,
		.min	= 0,
		.max	= ARRAY_SIZE(phase_diversity_modes) - 1,
	},

	/**
	 * #V4L2_CID_SI476X_INTERCHIP_LINK -- inter-chip link in
	 * diversity mode indicator. Allows user to determine if two
	 * chips working in diversity mode have established a link
	 * between each other and if the system as a whole uses
	 * signals from both antennas to receive FM radio.
	 */
	[SI476X_IDX_INTERCHIP_LINK] = {
		.ops	= &si476x_ctrl_ops,
		.id	= V4L2_CID_SI476X_INTERCHIP_LINK,
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.flags  = V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_VOLATILE,
		.name	= "Inter-Chip Link",
		.min	= 0,
		.max	= 1,
		.step	= 1,
	},
};

struct si476x_radio;

/**
 * struct si476x_radio_ops - vtable of tuner functions
 *
 * This table holds pointers to functions implementing particular
 * operations depending on the mode in which the tuner chip was
 * configured to start in. If the function is not supported
 * corresponding element is set to #NULL.
 *
 * @tune_freq: Tune chip to a specific frequency
 * @seek_start: Star station seeking
 * @rsq_status: Get Received Signal Quality(RSQ) status
 * @rds_blckcnt: Get received RDS blocks count
 * @phase_diversity: Change phase diversity mode of the tuner
 * @phase_div_status: Get phase diversity mode status
 * @acf_status: Get the status of Automatically Controlled
 * Features(ACF)
 * @agc_status: Get Automatic Gain Control(AGC) status
 */
struct si476x_radio_ops {
	int (*tune_freq)(struct si476x_core *, struct si476x_tune_freq_args *);
	int (*seek_start)(struct si476x_core *, bool, bool);
	int (*rsq_status)(struct si476x_core *, struct si476x_rsq_status_args *,
			  struct si476x_rsq_status_report *);
	int (*rds_blckcnt)(struct si476x_core *, bool,
			   struct si476x_rds_blockcount_report *);

	int (*phase_diversity)(struct si476x_core *,
			       enum si476x_phase_diversity_mode);
	int (*phase_div_status)(struct si476x_core *);
	int (*acf_status)(struct si476x_core *,
			  struct si476x_acf_status_report *);
	int (*agc_status)(struct si476x_core *,
			  struct si476x_agc_status_report *);
};

/**
 * struct si476x_radio - radio device
 *
 * @core: Pointer to underlying core device
 * @videodev: Pointer to video device created by V4L2 subsystem
 * @ops: Vtable of functions. See struct si476x_radio_ops for details
 * @kref: Reference counter
 * @core_lock: An r/w semaphore to brebvent the deletion of underlying
 * core structure is the radio device is being used
 */
struct si476x_radio {
	struct v4l2_device v4l2dev;
	struct video_device videodev;
	struct v4l2_ctrl_handler ctrl_handler;

	struct si476x_core  *core;
	/* This field should not be accesses unless core lock is held */
	const struct si476x_radio_ops *ops;

	struct dentry	*debugfs;
	u32 audmode;
};

static inline struct si476x_radio *
v4l2_dev_to_radio(struct v4l2_device *d)
{
	return container_of(d, struct si476x_radio, v4l2dev);
}

static inline struct si476x_radio *
v4l2_ctrl_handler_to_radio(struct v4l2_ctrl_handler *d)
{
	return container_of(d, struct si476x_radio, ctrl_handler);
}

/*
 * si476x_vidioc_querycap - query device capabilities
 */
static int si476x_radio_querycap(struct file *file, void *priv,
				 struct v4l2_capability *capability)
{
	struct si476x_radio *radio = video_drvdata(file);

	strlcpy(capability->driver, radio->v4l2dev.name,
		sizeof(capability->driver));
	strlcpy(capability->card,   DRIVER_CARD, sizeof(capability->card));
	snprintf(capability->bus_info, sizeof(capability->bus_info),
		 "platform:%s", radio->v4l2dev.name);

	capability->device_caps = V4L2_CAP_TUNER
		| V4L2_CAP_RADIO
		| V4L2_CAP_HW_FREQ_SEEK;

	si476x_core_lock(radio->core);
	if (!si476x_core_is_a_secondary_tuner(radio->core))
		capability->device_caps |= V4L2_CAP_RDS_CAPTURE
			| V4L2_CAP_READWRITE;
	si476x_core_unlock(radio->core);

	capability->capabilities = capability->device_caps
		| V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int si476x_radio_enum_freq_bands(struct file *file, void *priv,
					struct v4l2_frequency_band *band)
{
	int err;
	struct si476x_radio *radio = video_drvdata(file);

	if (band->tuner != 0)
		return -EINVAL;

	switch (radio->core->chip_id) {
		/* AM/FM tuners -- all bands are supported */
	case SI476X_CHIP_SI4761:
	case SI476X_CHIP_SI4764:
		if (band->index < ARRAY_SIZE(si476x_bands)) {
			*band = si476x_bands[band->index];
			err = 0;
		} else {
			err = -EINVAL;
		}
		break;
		/* FM companion tuner chips -- only FM bands are
		 * supported */
	case SI476X_CHIP_SI4768:
		if (band->index == SI476X_BAND_FM) {
			*band = si476x_bands[band->index];
			err = 0;
		} else {
			err = -EINVAL;
		}
		break;
	default:
		err = -EINVAL;
	}

	return err;
}

static int si476x_radio_g_tuner(struct file *file, void *priv,
				struct v4l2_tuner *tuner)
{
	int err;
	struct si476x_rsq_status_report report;
	struct si476x_radio *radio = video_drvdata(file);

	struct si476x_rsq_status_args args = {
		.primary	= false,
		.rsqack		= false,
		.attune		= false,
		.cancel		= false,
		.stcack		= false,
	};

	if (tuner->index != 0)
		return -EINVAL;

	tuner->type       = V4L2_TUNER_RADIO;
	tuner->capability = V4L2_TUNER_CAP_LOW /* Measure frequencies
						 * in multiples of
						 * 62.5 Hz */
		| V4L2_TUNER_CAP_STEREO
		| V4L2_TUNER_CAP_HWSEEK_BOUNDED
		| V4L2_TUNER_CAP_HWSEEK_WRAP
		| V4L2_TUNER_CAP_HWSEEK_PROG_LIM;

	si476x_core_lock(radio->core);

	if (si476x_core_is_a_secondary_tuner(radio->core)) {
		strlcpy(tuner->name, "FM (secondary)", sizeof(tuner->name));
		tuner->rxsubchans = 0;
		tuner->rangelow = si476x_bands[SI476X_BAND_FM].rangelow;
	} else if (si476x_core_has_am(radio->core)) {
		if (si476x_core_is_a_primary_tuner(radio->core))
			strlcpy(tuner->name, "AM/FM (primary)",
				sizeof(tuner->name));
		else
			strlcpy(tuner->name, "AM/FM", sizeof(tuner->name));

		tuner->rxsubchans = V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_STEREO
			| V4L2_TUNER_SUB_RDS;
		tuner->capability |= V4L2_TUNER_CAP_RDS
			| V4L2_TUNER_CAP_RDS_BLOCK_IO
			| V4L2_TUNER_CAP_FREQ_BANDS;

		tuner->rangelow = si476x_bands[SI476X_BAND_AM].rangelow;
	} else {
		strlcpy(tuner->name, "FM", sizeof(tuner->name));
		tuner->rxsubchans = V4L2_TUNER_SUB_RDS;
		tuner->capability |= V4L2_TUNER_CAP_RDS
			| V4L2_TUNER_CAP_RDS_BLOCK_IO
			| V4L2_TUNER_CAP_FREQ_BANDS;
		tuner->rangelow = si476x_bands[SI476X_BAND_FM].rangelow;
	}

	tuner->audmode = radio->audmode;

	tuner->afc = 1;
	tuner->rangehigh = si476x_bands[SI476X_BAND_FM].rangehigh;

	err = radio->ops->rsq_status(radio->core,
				     &args, &report);
	if (err < 0) {
		tuner->signal = 0;
	} else {
		/*
		 * tuner->signal value range: 0x0000 .. 0xFFFF,
		 * report.rssi: -128 .. 127
		 */
		tuner->signal = (report.rssi + 128) * 257;
	}
	si476x_core_unlock(radio->core);

	return err;
}

static int si476x_radio_s_tuner(struct file *file, void *priv,
				const struct v4l2_tuner *tuner)
{
	struct si476x_radio *radio = video_drvdata(file);

	if (tuner->index != 0)
		return -EINVAL;

	if (tuner->audmode == V4L2_TUNER_MODE_MONO ||
	    tuner->audmode == V4L2_TUNER_MODE_STEREO)
		radio->audmode = tuner->audmode;
	else
		radio->audmode = V4L2_TUNER_MODE_STEREO;

	return 0;
}

static int si476x_radio_init_vtable(struct si476x_radio *radio,
				    enum si476x_func func)
{
	static const struct si476x_radio_ops fm_ops = {
		.tune_freq		= si476x_core_cmd_fm_tune_freq,
		.seek_start		= si476x_core_cmd_fm_seek_start,
		.rsq_status		= si476x_core_cmd_fm_rsq_status,
		.rds_blckcnt		= si476x_core_cmd_fm_rds_blockcount,
		.phase_diversity	= si476x_core_cmd_fm_phase_diversity,
		.phase_div_status	= si476x_core_cmd_fm_phase_div_status,
		.acf_status		= si476x_core_cmd_fm_acf_status,
		.agc_status		= si476x_core_cmd_agc_status,
	};

	static const struct si476x_radio_ops am_ops = {
		.tune_freq		= si476x_core_cmd_am_tune_freq,
		.seek_start		= si476x_core_cmd_am_seek_start,
		.rsq_status		= si476x_core_cmd_am_rsq_status,
		.rds_blckcnt		= NULL,
		.phase_diversity	= NULL,
		.phase_div_status	= NULL,
		.acf_status		= si476x_core_cmd_am_acf_status,
		.agc_status		= NULL,
	};

	switch (func) {
	case SI476X_FUNC_FM_RECEIVER:
		radio->ops = &fm_ops;
		return 0;

	case SI476X_FUNC_AM_RECEIVER:
		radio->ops = &am_ops;
		return 0;
	default:
		WARN(1, "Unexpected tuner function value\n");
		return -EINVAL;
	}
}

static int si476x_radio_pretune(struct si476x_radio *radio,
				enum si476x_func func)
{
	int retval;

	struct si476x_tune_freq_args args = {
		.zifsr		= false,
		.hd		= false,
		.injside	= SI476X_INJSIDE_AUTO,
		.tunemode	= SI476X_TM_VALIDATED_NORMAL_TUNE,
		.smoothmetrics	= SI476X_SM_INITIALIZE_AUDIO,
		.antcap		= 0,
	};

	switch (func) {
	case SI476X_FUNC_FM_RECEIVER:
		args.freq = v4l2_to_si476x(radio->core,
					   92 * FREQ_MUL);
		retval = radio->ops->tune_freq(radio->core, &args);
		break;
	case SI476X_FUNC_AM_RECEIVER:
		args.freq = v4l2_to_si476x(radio->core,
					   0.6 * FREQ_MUL);
		retval = radio->ops->tune_freq(radio->core, &args);
		break;
	default:
		WARN(1, "Unexpected tuner function value\n");
		retval = -EINVAL;
	}

	return retval;
}
static int si476x_radio_do_post_powerup_init(struct si476x_radio *radio,
					     enum si476x_func func)
{
	int err;

	/* regcache_mark_dirty(radio->core->regmap); */
	err = regcache_sync_region(radio->core->regmap,
				   SI476X_PROP_DIGITAL_IO_INPUT_SAMPLE_RATE,
				   SI476X_PROP_DIGITAL_IO_OUTPUT_FORMAT);
		if (err < 0)
			return err;

	err = regcache_sync_region(radio->core->regmap,
				   SI476X_PROP_AUDIO_DEEMPHASIS,
				   SI476X_PROP_AUDIO_PWR_LINE_FILTER);
	if (err < 0)
		return err;

	err = regcache_sync_region(radio->core->regmap,
				   SI476X_PROP_INT_CTL_ENABLE,
				   SI476X_PROP_INT_CTL_ENABLE);
	if (err < 0)
		return err;

	/*
	 * Is there any point in restoring SNR and the like
	 * when switching between AM/FM?
	 */
	err = regcache_sync_region(radio->core->regmap,
				   SI476X_PROP_VALID_MAX_TUNE_ERROR,
				   SI476X_PROP_VALID_MAX_TUNE_ERROR);
	if (err < 0)
		return err;

	err = regcache_sync_region(radio->core->regmap,
				   SI476X_PROP_VALID_SNR_THRESHOLD,
				   SI476X_PROP_VALID_RSSI_THRESHOLD);
	if (err < 0)
		return err;

	if (func == SI476X_FUNC_FM_RECEIVER) {
		if (si476x_core_has_diversity(radio->core)) {
			err = si476x_core_cmd_fm_phase_diversity(radio->core,
								 radio->core->diversity_mode);
			if (err < 0)
				return err;
		}

		err = regcache_sync_region(radio->core->regmap,
					   SI476X_PROP_FM_RDS_INTERRUPT_SOURCE,
					   SI476X_PROP_FM_RDS_CONFIG);
		if (err < 0)
			return err;
	}

	return si476x_radio_init_vtable(radio, func);

}

static int si476x_radio_change_func(struct si476x_radio *radio,
				    enum si476x_func func)
{
	int err;
	bool soft;
	/*
	 * Since power/up down is a very time consuming operation,
	 * try to avoid doing it if the requested mode matches the one
	 * the tuner is in
	 */
	if (func == radio->core->power_up_parameters.func)
		return 0;

	soft = true;
	err = si476x_core_stop(radio->core, soft);
	if (err < 0) {
		/*
		 * OK, if the chip does not want to play nice let's
		 * try to reset it in more brutal way
		 */
		soft = false;
		err = si476x_core_stop(radio->core, soft);
		if (err < 0)
			return err;
	}
	/*
	  Set the desired radio tuner function
	 */
	radio->core->power_up_parameters.func = func;

	err = si476x_core_start(radio->core, soft);
	if (err < 0)
		return err;

	/*
	 * No need to do the rest of manipulations for the bootlader
	 * mode
	 */
	if (func != SI476X_FUNC_FM_RECEIVER &&
	    func != SI476X_FUNC_AM_RECEIVER)
		return err;

	return si476x_radio_do_post_powerup_init(radio, func);
}

static int si476x_radio_g_frequency(struct file *file, void *priv,
			      struct v4l2_frequency *f)
{
	int err;
	struct si476x_radio *radio = video_drvdata(file);

	if (f->tuner != 0 ||
	    f->type  != V4L2_TUNER_RADIO)
		return -EINVAL;

	si476x_core_lock(radio->core);

	if (radio->ops->rsq_status) {
		struct si476x_rsq_status_report report;
		struct si476x_rsq_status_args   args = {
			.primary	= false,
			.rsqack		= false,
			.attune		= true,
			.cancel		= false,
			.stcack		= false,
		};

		err = radio->ops->rsq_status(radio->core, &args, &report);
		if (!err)
			f->frequency = si476x_to_v4l2(radio->core,
						      report.readfreq);
	} else {
		err = -EINVAL;
	}

	si476x_core_unlock(radio->core);

	return err;
}

static int si476x_radio_s_frequency(struct file *file, void *priv,
				    const struct v4l2_frequency *f)
{
	int err;
	u32 freq = f->frequency;
	struct si476x_tune_freq_args args;
	struct si476x_radio *radio = video_drvdata(file);

	const u32 midrange = (si476x_bands[SI476X_BAND_AM].rangehigh +
			      si476x_bands[SI476X_BAND_FM].rangelow) / 2;
	const int band = (freq > midrange) ?
		SI476X_BAND_FM : SI476X_BAND_AM;
	const enum si476x_func func = (band == SI476X_BAND_AM) ?
		SI476X_FUNC_AM_RECEIVER : SI476X_FUNC_FM_RECEIVER;

	if (f->tuner != 0 ||
	    f->type  != V4L2_TUNER_RADIO)
		return -EINVAL;

	si476x_core_lock(radio->core);

	freq = clamp(freq,
		     si476x_bands[band].rangelow,
		     si476x_bands[band].rangehigh);

	if (si476x_radio_freq_is_inside_of_the_band(freq,
						    SI476X_BAND_AM) &&
	    (!si476x_core_has_am(radio->core) ||
	     si476x_core_is_a_secondary_tuner(radio->core))) {
		err = -EINVAL;
		goto unlock;
	}

	err = si476x_radio_change_func(radio, func);
	if (err < 0)
		goto unlock;

	args.zifsr		= false;
	args.hd			= false;
	args.injside		= SI476X_INJSIDE_AUTO;
	args.freq		= v4l2_to_si476x(radio->core, freq);
	args.tunemode		= SI476X_TM_VALIDATED_NORMAL_TUNE;
	args.smoothmetrics	= SI476X_SM_INITIALIZE_AUDIO;
	args.antcap		= 0;

	err = radio->ops->tune_freq(radio->core, &args);

unlock:
	si476x_core_unlock(radio->core);
	return err;
}

static int si476x_radio_s_hw_freq_seek(struct file *file, void *priv,
				       const struct v4l2_hw_freq_seek *seek)
{
	int err;
	enum si476x_func func;
	u32 rangelow, rangehigh;
	struct si476x_radio *radio = video_drvdata(file);

	if (file->f_flags & O_NONBLOCK)
		return -EAGAIN;

	if (seek->tuner != 0 ||
	    seek->type  != V4L2_TUNER_RADIO)
		return -EINVAL;

	si476x_core_lock(radio->core);

	if (!seek->rangelow) {
		err = regmap_read(radio->core->regmap,
				  SI476X_PROP_SEEK_BAND_BOTTOM,
				  &rangelow);
		if (!err)
			rangelow = si476x_to_v4l2(radio->core, rangelow);
		else
			goto unlock;
	} else
		rangelow = seek->rangelow;
	if (!seek->rangehigh) {
		err = regmap_read(radio->core->regmap,
				  SI476X_PROP_SEEK_BAND_TOP,
				  &rangehigh);
		if (!err)
			rangehigh = si476x_to_v4l2(radio->core, rangehigh);
		else
			goto unlock;
	} else
		rangehigh = seek->rangehigh;

	if (rangelow > rangehigh) {
		err = -EINVAL;
		goto unlock;
	}

	if (si476x_radio_range_is_inside_of_the_band(rangelow, rangehigh,
						     SI476X_BAND_FM)) {
		func = SI476X_FUNC_FM_RECEIVER;

	} else if (si476x_core_has_am(radio->core) &&
		   si476x_radio_range_is_inside_of_the_band(rangelow, rangehigh,
							    SI476X_BAND_AM)) {
		func = SI476X_FUNC_AM_RECEIVER;
	} else {
		err = -EINVAL;
		goto unlock;
	}

	err = si476x_radio_change_func(radio, func);
	if (err < 0)
		goto unlock;

	if (seek->rangehigh) {
		err = regmap_write(radio->core->regmap,
				   SI476X_PROP_SEEK_BAND_TOP,
				   v4l2_to_si476x(radio->core,
						  seek->rangehigh));
		if (err)
			goto unlock;
	}
	if (seek->rangelow) {
		err = regmap_write(radio->core->regmap,
				   SI476X_PROP_SEEK_BAND_BOTTOM,
				   v4l2_to_si476x(radio->core,
						  seek->rangelow));
		if (err)
			goto unlock;
	}
	if (seek->spacing) {
		err = regmap_write(radio->core->regmap,
				     SI476X_PROP_SEEK_FREQUENCY_SPACING,
				     v4l2_to_si476x(radio->core,
						    seek->spacing));
		if (err)
			goto unlock;
	}

	err = radio->ops->seek_start(radio->core,
				     seek->seek_upward,
				     seek->wrap_around);
unlock:
	si476x_core_unlock(radio->core);



	return err;
}

static int si476x_radio_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	int retval;
	struct si476x_radio *radio = v4l2_ctrl_handler_to_radio(ctrl->handler);

	si476x_core_lock(radio->core);

	switch (ctrl->id) {
	case V4L2_CID_SI476X_INTERCHIP_LINK:
		if (si476x_core_has_diversity(radio->core)) {
			if (radio->ops->phase_diversity) {
				retval = radio->ops->phase_div_status(radio->core);
				if (retval < 0)
					break;

				ctrl->val = !!SI476X_PHDIV_STATUS_LINK_LOCKED(retval);
				retval = 0;
				break;
			} else {
				retval = -ENOTTY;
				break;
			}
		}
		retval = -EINVAL;
		break;
	default:
		retval = -EINVAL;
		break;
	}
	si476x_core_unlock(radio->core);
	return retval;

}

static int si476x_radio_s_ctrl(struct v4l2_ctrl *ctrl)
{
	int retval;
	enum si476x_phase_diversity_mode mode;
	struct si476x_radio *radio = v4l2_ctrl_handler_to_radio(ctrl->handler);

	si476x_core_lock(radio->core);

	switch (ctrl->id) {
	case V4L2_CID_SI476X_HARMONICS_COUNT:
		retval = regmap_update_bits(radio->core->regmap,
					    SI476X_PROP_AUDIO_PWR_LINE_FILTER,
					    SI476X_PROP_PWR_HARMONICS_MASK,
					    ctrl->val);
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY:
		switch (ctrl->val) {
		case V4L2_CID_POWER_LINE_FREQUENCY_DISABLED:
			retval = regmap_update_bits(radio->core->regmap,
						    SI476X_PROP_AUDIO_PWR_LINE_FILTER,
						    SI476X_PROP_PWR_ENABLE_MASK,
						    0);
			break;
		case V4L2_CID_POWER_LINE_FREQUENCY_50HZ:
			retval = regmap_update_bits(radio->core->regmap,
						    SI476X_PROP_AUDIO_PWR_LINE_FILTER,
						    SI476X_PROP_PWR_GRID_MASK,
						    SI476X_PROP_PWR_GRID_50HZ);
			break;
		case V4L2_CID_POWER_LINE_FREQUENCY_60HZ:
			retval = regmap_update_bits(radio->core->regmap,
						    SI476X_PROP_AUDIO_PWR_LINE_FILTER,
						    SI476X_PROP_PWR_GRID_MASK,
						    SI476X_PROP_PWR_GRID_60HZ);
			break;
		default:
			retval = -EINVAL;
			break;
		}
		break;
	case V4L2_CID_SI476X_RSSI_THRESHOLD:
		retval = regmap_write(radio->core->regmap,
				      SI476X_PROP_VALID_RSSI_THRESHOLD,
				      ctrl->val);
		break;
	case V4L2_CID_SI476X_SNR_THRESHOLD:
		retval = regmap_write(radio->core->regmap,
				      SI476X_PROP_VALID_SNR_THRESHOLD,
				      ctrl->val);
		break;
	case V4L2_CID_SI476X_MAX_TUNE_ERROR:
		retval = regmap_write(radio->core->regmap,
				      SI476X_PROP_VALID_MAX_TUNE_ERROR,
				      ctrl->val);
		break;
	case V4L2_CID_RDS_RECEPTION:
		/*
		 * It looks like RDS related properties are
		 * inaccesable when tuner is in AM mode, so cache the
		 * changes
		 */
		if (si476x_core_is_in_am_receiver_mode(radio->core))
			regcache_cache_only(radio->core->regmap, true);

		if (ctrl->val) {
			retval = regmap_write(radio->core->regmap,
					      SI476X_PROP_FM_RDS_INTERRUPT_FIFO_COUNT,
					      radio->core->rds_fifo_depth);
			if (retval < 0)
				break;

			if (radio->core->client->irq) {
				retval = regmap_write(radio->core->regmap,
						      SI476X_PROP_FM_RDS_INTERRUPT_SOURCE,
						      SI476X_RDSRECV);
				if (retval < 0)
					break;
			}

			/* Drain RDS FIFO before enabling RDS processing */
			retval = si476x_core_cmd_fm_rds_status(radio->core,
							       false,
							       true,
							       true,
							       NULL);
			if (retval < 0)
				break;

			retval = regmap_update_bits(radio->core->regmap,
						    SI476X_PROP_FM_RDS_CONFIG,
						    SI476X_PROP_RDSEN_MASK,
						    SI476X_PROP_RDSEN);
		} else {
			retval = regmap_update_bits(radio->core->regmap,
						    SI476X_PROP_FM_RDS_CONFIG,
						    SI476X_PROP_RDSEN_MASK,
						    !SI476X_PROP_RDSEN);
		}

		if (si476x_core_is_in_am_receiver_mode(radio->core))
			regcache_cache_only(radio->core->regmap, false);
		break;
	case V4L2_CID_TUNE_DEEMPHASIS:
		retval = regmap_write(radio->core->regmap,
				      SI476X_PROP_AUDIO_DEEMPHASIS,
				      ctrl->val);
		break;

	case V4L2_CID_SI476X_DIVERSITY_MODE:
		mode = si476x_phase_diversity_idx_to_mode(ctrl->val);

		if (mode == radio->core->diversity_mode) {
			retval = 0;
			break;
		}

		if (si476x_core_is_in_am_receiver_mode(radio->core)) {
			/*
			 * Diversity cannot be configured while tuner
			 * is in AM mode so save the changes and carry on.
			 */
			radio->core->diversity_mode = mode;
			retval = 0;
		} else {
			retval = radio->ops->phase_diversity(radio->core, mode);
			if (!retval)
				radio->core->diversity_mode = mode;
		}
		break;

	case V4L2_CID_AUDIO_MUTE:
		if (ctrl->val)
			retval = regmap_write(radio->core->regmap,
					      SI476X_PROP_AUDIO_MUTE, 3);
		else
			retval = regmap_write(radio->core->regmap,
					      SI476X_PROP_AUDIO_MUTE, 0);
		break;
	default:
		retval = -EINVAL;
		break;
	}

	si476x_core_unlock(radio->core);

	return retval;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int si476x_radio_g_register(struct file *file, void *fh,
				   struct v4l2_dbg_register *reg)
{
	int err;
	unsigned int value;
	struct si476x_radio *radio = video_drvdata(file);

	si476x_core_lock(radio->core);
	reg->size = 2;
	err = regmap_read(radio->core->regmap,
			  (unsigned int)reg->reg, &value);
	reg->val = value;
	si476x_core_unlock(radio->core);

	return err;
}
static int si476x_radio_s_register(struct file *file, void *fh,
				   const struct v4l2_dbg_register *reg)
{

	int err;
	struct si476x_radio *radio = video_drvdata(file);

	si476x_core_lock(radio->core);
	err = regmap_write(radio->core->regmap,
			   (unsigned int)reg->reg,
			   (unsigned int)reg->val);
	si476x_core_unlock(radio->core);

	return err;
}
#endif

static int si476x_radio_fops_open(struct file *file)
{
	struct si476x_radio *radio = video_drvdata(file);
	int err;

	err = v4l2_fh_open(file);
	if (err)
		return err;

	if (v4l2_fh_is_singular_file(file)) {
		si476x_core_lock(radio->core);
		err = si476x_core_set_power_state(radio->core,
						  SI476X_POWER_UP_FULL);
		if (err < 0)
			goto done;

		err = si476x_radio_do_post_powerup_init(radio,
							radio->core->power_up_parameters.func);
		if (err < 0)
			goto power_down;

		err = si476x_radio_pretune(radio,
					   radio->core->power_up_parameters.func);
		if (err < 0)
			goto power_down;

		si476x_core_unlock(radio->core);
		/*Must be done after si476x_core_unlock to prevent a deadlock*/
		v4l2_ctrl_handler_setup(&radio->ctrl_handler);
	}

	return err;

power_down:
	si476x_core_set_power_state(radio->core,
				    SI476X_POWER_DOWN);
done:
	si476x_core_unlock(radio->core);
	v4l2_fh_release(file);

	return err;
}

static int si476x_radio_fops_release(struct file *file)
{
	int err;
	struct si476x_radio *radio = video_drvdata(file);

	if (v4l2_fh_is_singular_file(file) &&
	    atomic_read(&radio->core->is_alive))
		si476x_core_set_power_state(radio->core,
					    SI476X_POWER_DOWN);

	err = v4l2_fh_release(file);

	return err;
}

static ssize_t si476x_radio_fops_read(struct file *file, char __user *buf,
				      size_t count, loff_t *ppos)
{
	ssize_t      rval;
	size_t       fifo_len;
	unsigned int copied;

	struct si476x_radio *radio = video_drvdata(file);

	/* block if no new data available */
	if (kfifo_is_empty(&radio->core->rds_fifo)) {
		if (file->f_flags & O_NONBLOCK)
			return -EWOULDBLOCK;

		rval = wait_event_interruptible(radio->core->rds_read_queue,
						(!kfifo_is_empty(&radio->core->rds_fifo) ||
						 !atomic_read(&radio->core->is_alive)));
		if (rval < 0)
			return -EINTR;

		if (!atomic_read(&radio->core->is_alive))
			return -ENODEV;
	}

	fifo_len = kfifo_len(&radio->core->rds_fifo);

	if (kfifo_to_user(&radio->core->rds_fifo, buf,
			  min(fifo_len, count),
			  &copied) != 0) {
		dev_warn(&radio->videodev.dev,
			 "Error during FIFO to userspace copy\n");
		rval = -EIO;
	} else {
		rval = (ssize_t)copied;
	}

	return rval;
}

static unsigned int si476x_radio_fops_poll(struct file *file,
				struct poll_table_struct *pts)
{
	struct si476x_radio *radio = video_drvdata(file);
	unsigned long req_events = poll_requested_events(pts);
	unsigned int err = v4l2_ctrl_poll(file, pts);

	if (req_events & (POLLIN | POLLRDNORM)) {
		if (atomic_read(&radio->core->is_alive))
			poll_wait(file, &radio->core->rds_read_queue, pts);

		if (!atomic_read(&radio->core->is_alive))
			err = POLLHUP;

		if (!kfifo_is_empty(&radio->core->rds_fifo))
			err = POLLIN | POLLRDNORM;
	}

	return err;
}

static const struct v4l2_file_operations si476x_fops = {
	.owner			= THIS_MODULE,
	.read			= si476x_radio_fops_read,
	.poll			= si476x_radio_fops_poll,
	.unlocked_ioctl		= video_ioctl2,
	.open			= si476x_radio_fops_open,
	.release		= si476x_radio_fops_release,
};


static const struct v4l2_ioctl_ops si4761_ioctl_ops = {
	.vidioc_querycap		= si476x_radio_querycap,
	.vidioc_g_tuner			= si476x_radio_g_tuner,
	.vidioc_s_tuner			= si476x_radio_s_tuner,

	.vidioc_g_frequency		= si476x_radio_g_frequency,
	.vidioc_s_frequency		= si476x_radio_s_frequency,
	.vidioc_s_hw_freq_seek		= si476x_radio_s_hw_freq_seek,
	.vidioc_enum_freq_bands		= si476x_radio_enum_freq_bands,

	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,

#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register		= si476x_radio_g_register,
	.vidioc_s_register		= si476x_radio_s_register,
#endif
};


static const struct video_device si476x_viddev_template = {
	.fops			= &si476x_fops,
	.name			= DRIVER_NAME,
	.release		= video_device_release_empty,
};



static ssize_t si476x_radio_read_acf_blob(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	int err;
	struct si476x_radio *radio = file->private_data;
	struct si476x_acf_status_report report;

	si476x_core_lock(radio->core);
	if (radio->ops->acf_status)
		err = radio->ops->acf_status(radio->core, &report);
	else
		err = -ENOENT;
	si476x_core_unlock(radio->core);

	if (err < 0)
		return err;

	return simple_read_from_buffer(user_buf, count, ppos, &report,
				       sizeof(report));
}

static const struct file_operations radio_acf_fops = {
	.open	= simple_open,
	.llseek = default_llseek,
	.read	= si476x_radio_read_acf_blob,
};

static ssize_t si476x_radio_read_rds_blckcnt_blob(struct file *file,
						  char __user *user_buf,
						  size_t count, loff_t *ppos)
{
	int err;
	struct si476x_radio *radio = file->private_data;
	struct si476x_rds_blockcount_report report;

	si476x_core_lock(radio->core);
	if (radio->ops->rds_blckcnt)
		err = radio->ops->rds_blckcnt(radio->core, true,
					       &report);
	else
		err = -ENOENT;
	si476x_core_unlock(radio->core);

	if (err < 0)
		return err;

	return simple_read_from_buffer(user_buf, count, ppos, &report,
				       sizeof(report));
}

static const struct file_operations radio_rds_blckcnt_fops = {
	.open	= simple_open,
	.llseek = default_llseek,
	.read	= si476x_radio_read_rds_blckcnt_blob,
};

static ssize_t si476x_radio_read_agc_blob(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	int err;
	struct si476x_radio *radio = file->private_data;
	struct si476x_agc_status_report report;

	si476x_core_lock(radio->core);
	if (radio->ops->rds_blckcnt)
		err = radio->ops->agc_status(radio->core, &report);
	else
		err = -ENOENT;
	si476x_core_unlock(radio->core);

	if (err < 0)
		return err;

	return simple_read_from_buffer(user_buf, count, ppos, &report,
				       sizeof(report));
}

static const struct file_operations radio_agc_fops = {
	.open	= simple_open,
	.llseek = default_llseek,
	.read	= si476x_radio_read_agc_blob,
};

static ssize_t si476x_radio_read_rsq_blob(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	int err;
	struct si476x_radio *radio = file->private_data;
	struct si476x_rsq_status_report report;
	struct si476x_rsq_status_args args = {
		.primary	= false,
		.rsqack		= false,
		.attune		= false,
		.cancel		= false,
		.stcack		= false,
	};

	si476x_core_lock(radio->core);
	if (radio->ops->rds_blckcnt)
		err = radio->ops->rsq_status(radio->core, &args, &report);
	else
		err = -ENOENT;
	si476x_core_unlock(radio->core);

	if (err < 0)
		return err;

	return simple_read_from_buffer(user_buf, count, ppos, &report,
				       sizeof(report));
}

static const struct file_operations radio_rsq_fops = {
	.open	= simple_open,
	.llseek = default_llseek,
	.read	= si476x_radio_read_rsq_blob,
};

static ssize_t si476x_radio_read_rsq_primary_blob(struct file *file,
						  char __user *user_buf,
						  size_t count, loff_t *ppos)
{
	int err;
	struct si476x_radio *radio = file->private_data;
	struct si476x_rsq_status_report report;
	struct si476x_rsq_status_args args = {
		.primary	= true,
		.rsqack		= false,
		.attune		= false,
		.cancel		= false,
		.stcack		= false,
	};

	si476x_core_lock(radio->core);
	if (radio->ops->rds_blckcnt)
		err = radio->ops->rsq_status(radio->core, &args, &report);
	else
		err = -ENOENT;
	si476x_core_unlock(radio->core);

	if (err < 0)
		return err;

	return simple_read_from_buffer(user_buf, count, ppos, &report,
				       sizeof(report));
}

static const struct file_operations radio_rsq_primary_fops = {
	.open	= simple_open,
	.llseek = default_llseek,
	.read	= si476x_radio_read_rsq_primary_blob,
};


static int si476x_radio_init_debugfs(struct si476x_radio *radio)
{
	struct dentry	*dentry;
	int		ret;

	dentry = debugfs_create_dir(dev_name(radio->v4l2dev.dev), NULL);
	if (IS_ERR(dentry)) {
		ret = PTR_ERR(dentry);
		goto exit;
	}
	radio->debugfs = dentry;

	dentry = debugfs_create_file("acf", S_IRUGO,
				     radio->debugfs, radio, &radio_acf_fops);
	if (IS_ERR(dentry)) {
		ret = PTR_ERR(dentry);
		goto cleanup;
	}

	dentry = debugfs_create_file("rds_blckcnt", S_IRUGO,
				     radio->debugfs, radio,
				     &radio_rds_blckcnt_fops);
	if (IS_ERR(dentry)) {
		ret = PTR_ERR(dentry);
		goto cleanup;
	}

	dentry = debugfs_create_file("agc", S_IRUGO,
				     radio->debugfs, radio, &radio_agc_fops);
	if (IS_ERR(dentry)) {
		ret = PTR_ERR(dentry);
		goto cleanup;
	}

	dentry = debugfs_create_file("rsq", S_IRUGO,
				     radio->debugfs, radio, &radio_rsq_fops);
	if (IS_ERR(dentry)) {
		ret = PTR_ERR(dentry);
		goto cleanup;
	}

	dentry = debugfs_create_file("rsq_primary", S_IRUGO,
				     radio->debugfs, radio,
				     &radio_rsq_primary_fops);
	if (IS_ERR(dentry)) {
		ret = PTR_ERR(dentry);
		goto cleanup;
	}

	return 0;
cleanup:
	debugfs_remove_recursive(radio->debugfs);
exit:
	return ret;
}


static int si476x_radio_add_new_custom(struct si476x_radio *radio,
				       enum si476x_ctrl_idx idx)
{
	int rval;
	struct v4l2_ctrl *ctrl;

	ctrl = v4l2_ctrl_new_custom(&radio->ctrl_handler,
				    &si476x_ctrls[idx],
				    NULL);
	rval = radio->ctrl_handler.error;
	if (ctrl == NULL && rval)
		dev_err(radio->v4l2dev.dev,
			"Could not initialize '%s' control %d\n",
			si476x_ctrls[idx].name, rval);

	return rval;
}

static int si476x_radio_probe(struct platform_device *pdev)
{
	int rval;
	struct si476x_radio *radio;
	struct v4l2_ctrl *ctrl;

	static atomic_t instance = ATOMIC_INIT(0);

	radio = devm_kzalloc(&pdev->dev, sizeof(*radio), GFP_KERNEL);
	if (!radio)
		return -ENOMEM;

	radio->core = i2c_mfd_cell_to_core(&pdev->dev);

	v4l2_device_set_name(&radio->v4l2dev, DRIVER_NAME, &instance);

	rval = v4l2_device_register(&pdev->dev, &radio->v4l2dev);
	if (rval) {
		dev_err(&pdev->dev, "Cannot register v4l2_device.\n");
		return rval;
	}

	memcpy(&radio->videodev, &si476x_viddev_template,
	       sizeof(struct video_device));

	radio->videodev.v4l2_dev  = &radio->v4l2dev;
	radio->videodev.ioctl_ops = &si4761_ioctl_ops;

	video_set_drvdata(&radio->videodev, radio);
	platform_set_drvdata(pdev, radio);


	radio->v4l2dev.ctrl_handler = &radio->ctrl_handler;
	v4l2_ctrl_handler_init(&radio->ctrl_handler,
			       1 + ARRAY_SIZE(si476x_ctrls));

	if (si476x_core_has_am(radio->core)) {
		ctrl = v4l2_ctrl_new_std_menu(&radio->ctrl_handler,
					      &si476x_ctrl_ops,
					      V4L2_CID_POWER_LINE_FREQUENCY,
					      V4L2_CID_POWER_LINE_FREQUENCY_60HZ,
					      0, 0);
		rval = radio->ctrl_handler.error;
		if (ctrl == NULL && rval) {
			dev_err(&pdev->dev, "Could not initialize V4L2_CID_POWER_LINE_FREQUENCY control %d\n",
				rval);
			goto exit;
		}

		rval = si476x_radio_add_new_custom(radio,
						   SI476X_IDX_HARMONICS_COUNT);
		if (rval < 0)
			goto exit;
	}

	rval = si476x_radio_add_new_custom(radio, SI476X_IDX_RSSI_THRESHOLD);
	if (rval < 0)
		goto exit;

	rval = si476x_radio_add_new_custom(radio, SI476X_IDX_SNR_THRESHOLD);
	if (rval < 0)
		goto exit;

	rval = si476x_radio_add_new_custom(radio, SI476X_IDX_MAX_TUNE_ERROR);
	if (rval < 0)
		goto exit;

	ctrl = v4l2_ctrl_new_std_menu(&radio->ctrl_handler,
				      &si476x_ctrl_ops,
				      V4L2_CID_TUNE_DEEMPHASIS,
				      V4L2_DEEMPHASIS_75_uS, 0, 0);
	rval = radio->ctrl_handler.error;
	if (ctrl == NULL && rval) {
		dev_err(&pdev->dev, "Could not initialize V4L2_CID_TUNE_DEEMPHASIS control %d\n",
			rval);
		goto exit;
	}

	ctrl = v4l2_ctrl_new_std(&radio->ctrl_handler, &si476x_ctrl_ops,
				 V4L2_CID_RDS_RECEPTION,
				 0, 1, 1, 1);
	rval = radio->ctrl_handler.error;
	if (ctrl == NULL && rval) {
		dev_err(&pdev->dev, "Could not initialize V4L2_CID_RDS_RECEPTION control %d\n",
			rval);
		goto exit;
	}

	ctrl = v4l2_ctrl_new_std(&radio->ctrl_handler, &si476x_ctrl_ops,
				 V4L2_CID_AUDIO_MUTE,
				 0, 1, 1, 0);
	rval = radio->ctrl_handler.error;
	if (ctrl == NULL && rval) {
		dev_err(&pdev->dev, "Could not initialize V4L2_CID_AUDIO_MUTE control %d\n",
			rval);
		goto exit;
	}

	if (si476x_core_has_diversity(radio->core)) {
		si476x_ctrls[SI476X_IDX_DIVERSITY_MODE].def =
			si476x_phase_diversity_mode_to_idx(radio->core->diversity_mode);
		rval = si476x_radio_add_new_custom(radio, SI476X_IDX_DIVERSITY_MODE);
		if (rval < 0)
			goto exit;

		rval = si476x_radio_add_new_custom(radio, SI476X_IDX_INTERCHIP_LINK);
		if (rval < 0)
			goto exit;
	}

	/* register video device */
	rval = video_register_device(&radio->videodev, VFL_TYPE_RADIO, -1);
	if (rval < 0) {
		dev_err(&pdev->dev, "Could not register video device\n");
		goto exit;
	}

	rval = si476x_radio_init_debugfs(radio);
	if (rval < 0) {
		dev_err(&pdev->dev, "Could not creat debugfs interface\n");
		goto exit;
	}

	return 0;
exit:
	v4l2_ctrl_handler_free(radio->videodev.ctrl_handler);
	return rval;
}

static int si476x_radio_remove(struct platform_device *pdev)
{
	struct si476x_radio *radio = platform_get_drvdata(pdev);

	v4l2_ctrl_handler_free(radio->videodev.ctrl_handler);
	video_unregister_device(&radio->videodev);
	v4l2_device_unregister(&radio->v4l2dev);
	debugfs_remove_recursive(radio->debugfs);

	return 0;
}

MODULE_ALIAS("platform:si476x-radio");

static struct platform_driver si476x_radio_driver = {
	.driver		= {
		.name	= DRIVER_NAME,
	},
	.probe		= si476x_radio_probe,
	.remove		= si476x_radio_remove,
};
module_platform_driver(si476x_radio_driver);

MODULE_AUTHOR("Andrey Smirnov <andrew.smirnov@gmail.com>");
MODULE_DESCRIPTION("Driver for Si4761/64/68 AM/FM Radio MFD Cell");
MODULE_LICENSE("GPL");
