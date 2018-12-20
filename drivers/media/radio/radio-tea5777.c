/*
 *   v4l2 driver for TEA5777 Philips AM/FM radio tuner chips
 *
 *	Copyright (c) 2012 Hans de Goede <hdegoede@redhat.com>
 *
 *   Based on the ALSA driver for TEA5757/5759 Philips AM/FM radio tuner chips:
 *
 *	Copyright (c) 2004 Jaroslav Kysela <perex@perex.cz>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include "radio-tea5777.h"

MODULE_AUTHOR("Hans de Goede <perex@perex.cz>");
MODULE_DESCRIPTION("Routines for control of TEA5777 Philips AM/FM radio tuner chips");
MODULE_LICENSE("GPL");

#define TEA5777_FM_IF			150 /* kHz */
#define TEA5777_FM_FREQ_STEP		50  /* kHz */

#define TEA5777_AM_IF			21  /* kHz */
#define TEA5777_AM_FREQ_STEP		1   /* kHz */

/* Write reg, common bits */
#define TEA5777_W_MUTE_MASK		(1LL << 47)
#define TEA5777_W_MUTE_SHIFT		47
#define TEA5777_W_AM_FM_MASK		(1LL << 46)
#define TEA5777_W_AM_FM_SHIFT		46
#define TEA5777_W_STB_MASK		(1LL << 45)
#define TEA5777_W_STB_SHIFT		45

#define TEA5777_W_IFCE_MASK		(1LL << 29)
#define TEA5777_W_IFCE_SHIFT		29
#define TEA5777_W_IFW_MASK		(1LL << 28)
#define TEA5777_W_IFW_SHIFT		28
#define TEA5777_W_HILO_MASK		(1LL << 27)
#define TEA5777_W_HILO_SHIFT		27
#define TEA5777_W_DBUS_MASK		(1LL << 26)
#define TEA5777_W_DBUS_SHIFT		26

#define TEA5777_W_INTEXT_MASK		(1LL << 24)
#define TEA5777_W_INTEXT_SHIFT		24
#define TEA5777_W_P1_MASK		(1LL << 23)
#define TEA5777_W_P1_SHIFT		23
#define TEA5777_W_P0_MASK		(1LL << 22)
#define TEA5777_W_P0_SHIFT		22
#define TEA5777_W_PEN1_MASK		(1LL << 21)
#define TEA5777_W_PEN1_SHIFT		21
#define TEA5777_W_PEN0_MASK		(1LL << 20)
#define TEA5777_W_PEN0_SHIFT		20

#define TEA5777_W_CHP0_MASK		(1LL << 18)
#define TEA5777_W_CHP0_SHIFT		18
#define TEA5777_W_DEEM_MASK		(1LL << 17)
#define TEA5777_W_DEEM_SHIFT		17

#define TEA5777_W_SEARCH_MASK		(1LL << 7)
#define TEA5777_W_SEARCH_SHIFT		7
#define TEA5777_W_PROGBLIM_MASK		(1LL << 6)
#define TEA5777_W_PROGBLIM_SHIFT	6
#define TEA5777_W_UPDWN_MASK		(1LL << 5)
#define TEA5777_W_UPDWN_SHIFT		5
#define TEA5777_W_SLEV_MASK		(3LL << 3)
#define TEA5777_W_SLEV_SHIFT		3

/* Write reg, FM specific bits */
#define TEA5777_W_FM_PLL_MASK		(0x1fffLL << 32)
#define TEA5777_W_FM_PLL_SHIFT		32
#define TEA5777_W_FM_FREF_MASK		(0x03LL << 30)
#define TEA5777_W_FM_FREF_SHIFT		30
#define TEA5777_W_FM_FREF_VALUE		0LL /* 50k steps, 150k IF */

#define TEA5777_W_FM_FORCEMONO_MASK	(1LL << 15)
#define TEA5777_W_FM_FORCEMONO_SHIFT	15
#define TEA5777_W_FM_SDSOFF_MASK	(1LL << 14)
#define TEA5777_W_FM_SDSOFF_SHIFT	14
#define TEA5777_W_FM_DOFF_MASK		(1LL << 13)
#define TEA5777_W_FM_DOFF_SHIFT		13

#define TEA5777_W_FM_STEP_MASK		(3LL << 1)
#define TEA5777_W_FM_STEP_SHIFT		1

/* Write reg, AM specific bits */
#define TEA5777_W_AM_PLL_MASK		(0x7ffLL << 34)
#define TEA5777_W_AM_PLL_SHIFT		34
#define TEA5777_W_AM_AGCRF_MASK		(1LL << 33)
#define TEA5777_W_AM_AGCRF_SHIFT	33
#define TEA5777_W_AM_AGCIF_MASK		(1LL << 32)
#define TEA5777_W_AM_AGCIF_SHIFT	32
#define TEA5777_W_AM_MWLW_MASK		(1LL << 31)
#define TEA5777_W_AM_MWLW_SHIFT		31
#define TEA5777_W_AM_LW			0LL
#define TEA5777_W_AM_MW			1LL
#define TEA5777_W_AM_LNA_MASK		(1LL << 30)
#define TEA5777_W_AM_LNA_SHIFT		30

#define TEA5777_W_AM_PEAK_MASK		(1LL << 25)
#define TEA5777_W_AM_PEAK_SHIFT		25

#define TEA5777_W_AM_RFB_MASK		(1LL << 16)
#define TEA5777_W_AM_RFB_SHIFT		16
#define TEA5777_W_AM_CALLIGN_MASK	(1LL << 15)
#define TEA5777_W_AM_CALLIGN_SHIFT	15
#define TEA5777_W_AM_CBANK_MASK		(0x7fLL << 8)
#define TEA5777_W_AM_CBANK_SHIFT	8

#define TEA5777_W_AM_DELAY_MASK		(1LL << 2)
#define TEA5777_W_AM_DELAY_SHIFT	2
#define TEA5777_W_AM_STEP_MASK		(1LL << 1)
#define TEA5777_W_AM_STEP_SHIFT		1

/* Read reg, common bits */
#define TEA5777_R_LEVEL_MASK		(0x0f << 17)
#define TEA5777_R_LEVEL_SHIFT		17
#define TEA5777_R_SFOUND_MASK		(0x01 << 16)
#define TEA5777_R_SFOUND_SHIFT		16
#define TEA5777_R_BLIM_MASK		(0x01 << 15)
#define TEA5777_R_BLIM_SHIFT		15

/* Read reg, FM specific bits */
#define TEA5777_R_FM_STEREO_MASK	(0x01 << 21)
#define TEA5777_R_FM_STEREO_SHIFT	21
#define TEA5777_R_FM_PLL_MASK		0x1fff
#define TEA5777_R_FM_PLL_SHIFT		0

enum { BAND_FM, BAND_AM };

static const struct v4l2_frequency_band bands[] = {
	{
		.type = V4L2_TUNER_RADIO,
		.index = 0,
		.capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO |
			      V4L2_TUNER_CAP_FREQ_BANDS |
			      V4L2_TUNER_CAP_HWSEEK_BOUNDED |
			      V4L2_TUNER_CAP_HWSEEK_PROG_LIM,
		.rangelow   =  76000 * 16,
		.rangehigh  = 108000 * 16,
		.modulation = V4L2_BAND_MODULATION_FM,
	},
	{
		.type = V4L2_TUNER_RADIO,
		.index = 1,
		.capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_FREQ_BANDS |
			      V4L2_TUNER_CAP_HWSEEK_BOUNDED |
			      V4L2_TUNER_CAP_HWSEEK_PROG_LIM,
		.rangelow   =  530 * 16,
		.rangehigh  = 1710 * 16,
		.modulation = V4L2_BAND_MODULATION_AM,
	},
};

static u32 tea5777_freq_to_v4l2_freq(struct radio_tea5777 *tea, u32 freq)
{
	switch (tea->band) {
	case BAND_FM:
		return (freq * TEA5777_FM_FREQ_STEP + TEA5777_FM_IF) * 16;
	case BAND_AM:
		return (freq * TEA5777_AM_FREQ_STEP + TEA5777_AM_IF) * 16;
	}
	return 0; /* Never reached */
}

int radio_tea5777_set_freq(struct radio_tea5777 *tea)
{
	u32 freq;
	int res;

	freq = clamp(tea->freq, bands[tea->band].rangelow,
				bands[tea->band].rangehigh);
	freq = (freq + 8) / 16; /* to kHz */

	switch (tea->band) {
	case BAND_FM:
		tea->write_reg &= ~TEA5777_W_AM_FM_MASK;
		freq = (freq - TEA5777_FM_IF) / TEA5777_FM_FREQ_STEP;
		tea->write_reg &= ~TEA5777_W_FM_PLL_MASK;
		tea->write_reg |= (u64)freq << TEA5777_W_FM_PLL_SHIFT;
		tea->write_reg &= ~TEA5777_W_FM_FREF_MASK;
		tea->write_reg |= TEA5777_W_FM_FREF_VALUE <<
				  TEA5777_W_FM_FREF_SHIFT;
		tea->write_reg &= ~TEA5777_W_FM_FORCEMONO_MASK;
		if (tea->audmode == V4L2_TUNER_MODE_MONO)
			tea->write_reg |= 1LL << TEA5777_W_FM_FORCEMONO_SHIFT;
		break;
	case BAND_AM:
		tea->write_reg &= ~TEA5777_W_AM_FM_MASK;
		tea->write_reg |= (1LL << TEA5777_W_AM_FM_SHIFT);
		freq = (freq - TEA5777_AM_IF) / TEA5777_AM_FREQ_STEP;
		tea->write_reg &= ~TEA5777_W_AM_PLL_MASK;
		tea->write_reg |= (u64)freq << TEA5777_W_AM_PLL_SHIFT;
		tea->write_reg &= ~TEA5777_W_AM_AGCRF_MASK;
		tea->write_reg &= ~TEA5777_W_AM_AGCRF_MASK;
		tea->write_reg &= ~TEA5777_W_AM_MWLW_MASK;
		tea->write_reg |= TEA5777_W_AM_MW << TEA5777_W_AM_MWLW_SHIFT;
		tea->write_reg &= ~TEA5777_W_AM_LNA_MASK;
		tea->write_reg |= 1LL << TEA5777_W_AM_LNA_SHIFT;
		tea->write_reg &= ~TEA5777_W_AM_PEAK_MASK;
		tea->write_reg |= 1LL << TEA5777_W_AM_PEAK_SHIFT;
		tea->write_reg &= ~TEA5777_W_AM_CALLIGN_MASK;
		break;
	}

	res = tea->ops->write_reg(tea, tea->write_reg);
	if (res)
		return res;

	tea->needs_write = false;
	tea->read_reg = -1;
	tea->freq = tea5777_freq_to_v4l2_freq(tea, freq);

	return 0;
}

static int radio_tea5777_update_read_reg(struct radio_tea5777 *tea, int wait)
{
	int res;

	if (tea->read_reg != -1)
		return 0;

	if (tea->write_before_read && tea->needs_write) {
		res = radio_tea5777_set_freq(tea);
		if (res)
			return res;
	}

	if (wait) {
		if (schedule_timeout_interruptible(msecs_to_jiffies(wait)))
			return -ERESTARTSYS;
	}

	res = tea->ops->read_reg(tea, &tea->read_reg);
	if (res)
		return res;

	tea->needs_write = true;
	return 0;
}

/*
 * Linux Video interface
 */

static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *v)
{
	struct radio_tea5777 *tea = video_drvdata(file);

	strscpy(v->driver, tea->v4l2_dev->name, sizeof(v->driver));
	strscpy(v->card, tea->card, sizeof(v->card));
	strlcat(v->card, " TEA5777", sizeof(v->card));
	strscpy(v->bus_info, tea->bus_info, sizeof(v->bus_info));
	v->device_caps = V4L2_CAP_TUNER | V4L2_CAP_RADIO;
	v->device_caps |= V4L2_CAP_HW_FREQ_SEEK;
	v->capabilities = v->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int vidioc_enum_freq_bands(struct file *file, void *priv,
					 struct v4l2_frequency_band *band)
{
	struct radio_tea5777 *tea = video_drvdata(file);

	if (band->tuner != 0 || band->index >= ARRAY_SIZE(bands) ||
	    (!tea->has_am && band->index == BAND_AM))
		return -EINVAL;

	*band = bands[band->index];
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
					struct v4l2_tuner *v)
{
	struct radio_tea5777 *tea = video_drvdata(file);
	int res;

	if (v->index > 0)
		return -EINVAL;

	res = radio_tea5777_update_read_reg(tea, 0);
	if (res)
		return res;

	memset(v, 0, sizeof(*v));
	if (tea->has_am)
		strscpy(v->name, "AM/FM", sizeof(v->name));
	else
		strscpy(v->name, "FM", sizeof(v->name));
	v->type = V4L2_TUNER_RADIO;
	v->capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO |
			V4L2_TUNER_CAP_FREQ_BANDS |
			V4L2_TUNER_CAP_HWSEEK_BOUNDED |
			V4L2_TUNER_CAP_HWSEEK_PROG_LIM;
	v->rangelow   = tea->has_am ? bands[BAND_AM].rangelow :
				      bands[BAND_FM].rangelow;
	v->rangehigh  = bands[BAND_FM].rangehigh;
	if (tea->band == BAND_FM &&
			(tea->read_reg & TEA5777_R_FM_STEREO_MASK))
		v->rxsubchans = V4L2_TUNER_SUB_STEREO;
	else
		v->rxsubchans = V4L2_TUNER_SUB_MONO;
	v->audmode = tea->audmode;
	/* shift - 12 to convert 4-bits (0-15) scale to 16-bits (0-65535) */
	v->signal = (tea->read_reg & TEA5777_R_LEVEL_MASK) >>
		    (TEA5777_R_LEVEL_SHIFT - 12);

	/* Invalidate read_reg, so that next call we return up2date signal */
	tea->read_reg = -1;

	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
					const struct v4l2_tuner *v)
{
	struct radio_tea5777 *tea = video_drvdata(file);
	u32 orig_audmode = tea->audmode;

	if (v->index)
		return -EINVAL;

	tea->audmode = v->audmode;
	if (tea->audmode > V4L2_TUNER_MODE_STEREO)
		tea->audmode = V4L2_TUNER_MODE_STEREO;

	if (tea->audmode != orig_audmode && tea->band == BAND_FM)
		return radio_tea5777_set_freq(tea);

	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct radio_tea5777 *tea = video_drvdata(file);

	if (f->tuner != 0)
		return -EINVAL;
	f->type = V4L2_TUNER_RADIO;
	f->frequency = tea->freq;
	return 0;
}

static int vidioc_s_frequency(struct file *file, void *priv,
					const struct v4l2_frequency *f)
{
	struct radio_tea5777 *tea = video_drvdata(file);

	if (f->tuner != 0 || f->type != V4L2_TUNER_RADIO)
		return -EINVAL;

	if (tea->has_am && f->frequency < (20000 * 16))
		tea->band = BAND_AM;
	else
		tea->band = BAND_FM;

	tea->freq = f->frequency;
	return radio_tea5777_set_freq(tea);
}

static int vidioc_s_hw_freq_seek(struct file *file, void *fh,
					const struct v4l2_hw_freq_seek *a)
{
	struct radio_tea5777 *tea = video_drvdata(file);
	unsigned long timeout;
	u32 rangelow = a->rangelow;
	u32 rangehigh = a->rangehigh;
	int i, res, spacing;
	u32 orig_freq;

	if (a->tuner || a->wrap_around)
		return -EINVAL;

	if (file->f_flags & O_NONBLOCK)
		return -EWOULDBLOCK;

	if (rangelow || rangehigh) {
		for (i = 0; i < ARRAY_SIZE(bands); i++) {
			if (i == BAND_AM && !tea->has_am)
				continue;
			if (bands[i].rangelow  >= rangelow &&
			    bands[i].rangehigh <= rangehigh)
				break;
		}
		if (i == ARRAY_SIZE(bands))
			return -EINVAL; /* No matching band found */

		tea->band = i;
		if (tea->freq < rangelow || tea->freq > rangehigh) {
			tea->freq = clamp(tea->freq, rangelow,
						     rangehigh);
			res = radio_tea5777_set_freq(tea);
			if (res)
				return res;
		}
	} else {
		rangelow  = bands[tea->band].rangelow;
		rangehigh = bands[tea->band].rangehigh;
	}

	spacing   = (tea->band == BAND_AM) ? (5 * 16) : (200 * 16); /* kHz */
	orig_freq = tea->freq;

	tea->write_reg |= TEA5777_W_PROGBLIM_MASK;
	if (tea->seek_rangelow != rangelow) {
		tea->write_reg &= ~TEA5777_W_UPDWN_MASK;
		tea->freq = rangelow;
		res = radio_tea5777_set_freq(tea);
		if (res)
			goto leave;
		tea->seek_rangelow = rangelow;
	}
	if (tea->seek_rangehigh != rangehigh) {
		tea->write_reg |= TEA5777_W_UPDWN_MASK;
		tea->freq = rangehigh;
		res = radio_tea5777_set_freq(tea);
		if (res)
			goto leave;
		tea->seek_rangehigh = rangehigh;
	}
	tea->write_reg &= ~TEA5777_W_PROGBLIM_MASK;

	tea->write_reg |= TEA5777_W_SEARCH_MASK;
	if (a->seek_upward) {
		tea->write_reg |= TEA5777_W_UPDWN_MASK;
		tea->freq = orig_freq + spacing;
	} else {
		tea->write_reg &= ~TEA5777_W_UPDWN_MASK;
		tea->freq = orig_freq - spacing;
	}
	res = radio_tea5777_set_freq(tea);
	if (res)
		goto leave;

	timeout = jiffies + msecs_to_jiffies(5000);
	for (;;) {
		if (time_after(jiffies, timeout)) {
			res = -ENODATA;
			break;
		}

		res = radio_tea5777_update_read_reg(tea, 100);
		if (res)
			break;

		/*
		 * Note we use tea->freq to track how far we've searched sofar
		 * this is necessary to ensure we continue seeking at the right
		 * point, in the write_before_read case.
		 */
		tea->freq = (tea->read_reg & TEA5777_R_FM_PLL_MASK);
		tea->freq = tea5777_freq_to_v4l2_freq(tea, tea->freq);

		if ((tea->read_reg & TEA5777_R_SFOUND_MASK)) {
			tea->write_reg &= ~TEA5777_W_SEARCH_MASK;
			return 0;
		}

		if (tea->read_reg & TEA5777_R_BLIM_MASK) {
			res = -ENODATA;
			break;
		}

		/* Force read_reg update */
		tea->read_reg = -1;
	}
leave:
	tea->write_reg &= ~TEA5777_W_PROGBLIM_MASK;
	tea->write_reg &= ~TEA5777_W_SEARCH_MASK;
	tea->freq = orig_freq;
	radio_tea5777_set_freq(tea);
	return res;
}

static int tea575x_s_ctrl(struct v4l2_ctrl *c)
{
	struct radio_tea5777 *tea =
		container_of(c->handler, struct radio_tea5777, ctrl_handler);

	switch (c->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (c->val)
			tea->write_reg |= TEA5777_W_MUTE_MASK;
		else
			tea->write_reg &= ~TEA5777_W_MUTE_MASK;

		return radio_tea5777_set_freq(tea);
	}

	return -EINVAL;
}

static const struct v4l2_file_operations tea575x_fops = {
	.unlocked_ioctl	= video_ioctl2,
	.open		= v4l2_fh_open,
	.release	= v4l2_fh_release,
	.poll		= v4l2_ctrl_poll,
};

static const struct v4l2_ioctl_ops tea575x_ioctl_ops = {
	.vidioc_querycap    = vidioc_querycap,
	.vidioc_g_tuner     = vidioc_g_tuner,
	.vidioc_s_tuner     = vidioc_s_tuner,
	.vidioc_g_frequency = vidioc_g_frequency,
	.vidioc_s_frequency = vidioc_s_frequency,
	.vidioc_s_hw_freq_seek = vidioc_s_hw_freq_seek,
	.vidioc_enum_freq_bands = vidioc_enum_freq_bands,
	.vidioc_log_status  = v4l2_ctrl_log_status,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct video_device tea575x_radio = {
	.ioctl_ops	= &tea575x_ioctl_ops,
	.release	= video_device_release_empty,
};

static const struct v4l2_ctrl_ops tea575x_ctrl_ops = {
	.s_ctrl = tea575x_s_ctrl,
};

int radio_tea5777_init(struct radio_tea5777 *tea, struct module *owner)
{
	int res;

	tea->write_reg = (1LL << TEA5777_W_IFCE_SHIFT) |
			 (1LL << TEA5777_W_IFW_SHIFT) |
			 (1LL << TEA5777_W_INTEXT_SHIFT) |
			 (1LL << TEA5777_W_CHP0_SHIFT) |
			 (1LL << TEA5777_W_SLEV_SHIFT);
	tea->freq = 90500 * 16;	/* 90.5Mhz default */
	tea->audmode = V4L2_TUNER_MODE_STEREO;
	res = radio_tea5777_set_freq(tea);
	if (res) {
		v4l2_err(tea->v4l2_dev, "can't set initial freq (%d)\n", res);
		return res;
	}

	tea->vd = tea575x_radio;
	video_set_drvdata(&tea->vd, tea);
	mutex_init(&tea->mutex);
	strscpy(tea->vd.name, tea->v4l2_dev->name, sizeof(tea->vd.name));
	tea->vd.lock = &tea->mutex;
	tea->vd.v4l2_dev = tea->v4l2_dev;
	tea->fops = tea575x_fops;
	tea->fops.owner = owner;
	tea->vd.fops = &tea->fops;

	tea->vd.ctrl_handler = &tea->ctrl_handler;
	v4l2_ctrl_handler_init(&tea->ctrl_handler, 1);
	v4l2_ctrl_new_std(&tea->ctrl_handler, &tea575x_ctrl_ops,
			  V4L2_CID_AUDIO_MUTE, 0, 1, 1, 1);
	res = tea->ctrl_handler.error;
	if (res) {
		v4l2_err(tea->v4l2_dev, "can't initialize controls\n");
		v4l2_ctrl_handler_free(&tea->ctrl_handler);
		return res;
	}
	v4l2_ctrl_handler_setup(&tea->ctrl_handler);

	res = video_register_device(&tea->vd, VFL_TYPE_RADIO, -1);
	if (res) {
		v4l2_err(tea->v4l2_dev, "can't register video device!\n");
		v4l2_ctrl_handler_free(tea->vd.ctrl_handler);
		return res;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(radio_tea5777_init);

void radio_tea5777_exit(struct radio_tea5777 *tea)
{
	video_unregister_device(&tea->vd);
	v4l2_ctrl_handler_free(tea->vd.ctrl_handler);
}
EXPORT_SYMBOL_GPL(radio_tea5777_exit);
