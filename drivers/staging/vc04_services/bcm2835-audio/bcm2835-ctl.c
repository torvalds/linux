// SPDX-License-Identifier: GPL-2.0
/* Copyright 2011 Broadcom Corporation.  All rights reserved. */

#include <sound/core.h>
#include <sound/control.h>
#include <sound/tlv.h>
#include <sound/asoundef.h>

#include "bcm2835.h"

/* volume maximum and minimum in terms of 0.01dB */
#define CTRL_VOL_MAX 400
#define CTRL_VOL_MIN -10239 /* originally -10240 */

static int bcm2835_audio_set_chip_ctls(struct bcm2835_chip *chip)
{
	int i, err = 0;

	/* change ctls for all substreams */
	for (i = 0; i < MAX_SUBSTREAMS; i++) {
		if (chip->alsa_stream[i]) {
			err = bcm2835_audio_set_ctls(chip->alsa_stream[i]);
			if (err < 0)
				break;
		}
	}
	return err;
}

static int snd_bcm2835_ctl_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	if (kcontrol->private_value == PCM_PLAYBACK_VOLUME) {
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
		uinfo->count = 1;
		uinfo->value.integer.min = CTRL_VOL_MIN;
		uinfo->value.integer.max = CTRL_VOL_MAX; /* 2303 */
	} else if (kcontrol->private_value == PCM_PLAYBACK_MUTE) {
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
		uinfo->count = 1;
		uinfo->value.integer.min = 0;
		uinfo->value.integer.max = 1;
	} else if (kcontrol->private_value == PCM_PLAYBACK_DEVICE) {
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
		uinfo->count = 1;
		uinfo->value.integer.min = 0;
		uinfo->value.integer.max = AUDIO_DEST_MAX - 1;
	}
	return 0;
}

static int snd_bcm2835_ctl_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct bcm2835_chip *chip = snd_kcontrol_chip(kcontrol);

	mutex_lock(&chip->audio_mutex);

	if (kcontrol->private_value == PCM_PLAYBACK_VOLUME)
		ucontrol->value.integer.value[0] = chip->volume;
	else if (kcontrol->private_value == PCM_PLAYBACK_MUTE)
		ucontrol->value.integer.value[0] = chip->mute;
	else if (kcontrol->private_value == PCM_PLAYBACK_DEVICE)
		ucontrol->value.integer.value[0] = chip->dest;

	mutex_unlock(&chip->audio_mutex);
	return 0;
}

static int snd_bcm2835_ctl_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct bcm2835_chip *chip = snd_kcontrol_chip(kcontrol);
	int val, *valp;
	int changed = 0;

	if (kcontrol->private_value == PCM_PLAYBACK_VOLUME)
		valp = &chip->volume;
	else if (kcontrol->private_value == PCM_PLAYBACK_MUTE)
		valp = &chip->mute;
	else if (kcontrol->private_value == PCM_PLAYBACK_DEVICE)
		valp = &chip->dest;
	else
		return -EINVAL;

	val = ucontrol->value.integer.value[0];
	mutex_lock(&chip->audio_mutex);
	if (val != *valp) {
		*valp = val;
		changed = 1;
		if (bcm2835_audio_set_chip_ctls(chip))
			dev_err(chip->card->dev, "Failed to set ALSA controls..\n");
	}
	mutex_unlock(&chip->audio_mutex);
	return changed;
}

static DECLARE_TLV_DB_SCALE(snd_bcm2835_db_scale, CTRL_VOL_MIN, 1, 1);

static const struct snd_kcontrol_new snd_bcm2835_ctl[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "PCM Playback Volume",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_TLV_READ,
		.private_value = PCM_PLAYBACK_VOLUME,
		.info = snd_bcm2835_ctl_info,
		.get = snd_bcm2835_ctl_get,
		.put = snd_bcm2835_ctl_put,
		.tlv = {.p = snd_bcm2835_db_scale}
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "PCM Playback Switch",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.private_value = PCM_PLAYBACK_MUTE,
		.info = snd_bcm2835_ctl_info,
		.get = snd_bcm2835_ctl_get,
		.put = snd_bcm2835_ctl_put,
	},
};

static int snd_bcm2835_spdif_default_info(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_bcm2835_spdif_default_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct bcm2835_chip *chip = snd_kcontrol_chip(kcontrol);
	int i;

	mutex_lock(&chip->audio_mutex);

	for (i = 0; i < 4; i++)
		ucontrol->value.iec958.status[i] =
			(chip->spdif_status >> (i * 8)) & 0xff;

	mutex_unlock(&chip->audio_mutex);
	return 0;
}

static int snd_bcm2835_spdif_default_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct bcm2835_chip *chip = snd_kcontrol_chip(kcontrol);
	unsigned int val = 0;
	int i, change;

	mutex_lock(&chip->audio_mutex);

	for (i = 0; i < 4; i++)
		val |= (unsigned int)ucontrol->value.iec958.status[i] << (i * 8);

	change = val != chip->spdif_status;
	chip->spdif_status = val;

	mutex_unlock(&chip->audio_mutex);
	return change;
}

static int snd_bcm2835_spdif_mask_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_bcm2835_spdif_mask_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	/*
	 * bcm2835 supports only consumer mode and sets all other format flags
	 * automatically. So the only thing left is signalling non-audio content
	 */
	ucontrol->value.iec958.status[0] = IEC958_AES0_NONAUDIO;
	return 0;
}

static const struct snd_kcontrol_new snd_bcm2835_spdif[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
		.info = snd_bcm2835_spdif_default_info,
		.get = snd_bcm2835_spdif_default_get,
		.put = snd_bcm2835_spdif_default_put
	},
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, CON_MASK),
		.info = snd_bcm2835_spdif_mask_info,
		.get = snd_bcm2835_spdif_mask_get,
	},
};

static int create_ctls(struct bcm2835_chip *chip, size_t size,
		       const struct snd_kcontrol_new *kctls)
{
	int i, err;

	for (i = 0; i < size; i++) {
		err = snd_ctl_add(chip->card, snd_ctl_new1(&kctls[i], chip));
		if (err < 0)
			return err;
	}
	return 0;
}

int snd_bcm2835_new_headphones_ctl(struct bcm2835_chip *chip)
{
	strscpy(chip->card->mixername, "Broadcom Mixer", sizeof(chip->card->mixername));
	return create_ctls(chip, ARRAY_SIZE(snd_bcm2835_ctl),
			   snd_bcm2835_ctl);
}

int snd_bcm2835_new_hdmi_ctl(struct bcm2835_chip *chip)
{
	int err;

	strscpy(chip->card->mixername, "Broadcom Mixer", sizeof(chip->card->mixername));
	err = create_ctls(chip, ARRAY_SIZE(snd_bcm2835_ctl), snd_bcm2835_ctl);
	if (err < 0)
		return err;
	return create_ctls(chip, ARRAY_SIZE(snd_bcm2835_spdif),
			   snd_bcm2835_spdif);
}

