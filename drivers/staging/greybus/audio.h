/*
 * Greybus audio driver
 * Copyright 2015 Google Inc.
 * Copyright 2015 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#ifndef __LINUX_GBAUDIO_H
#define __LINUX_GBAUDIO_H

#ifdef __KERNEL__

#include <sound/soc.h>

#define NAME_SIZE	32

enum {
	APB1_PCM = 0,
	APB2_PCM,
	NUM_CODEC_DAIS,
};

enum gbcodec_reg_index {
	GBCODEC_CTL_REG,
	GBCODEC_MUTE_REG,
	GBCODEC_PB_LVOL_REG,
	GBCODEC_PB_RVOL_REG,
	GBCODEC_CAP_LVOL_REG,
	GBCODEC_CAP_RVOL_REG,
	GBCODEC_APB1_MUX_REG,
	GBCODEC_APB2_MUX_REG,
	GBCODEC_REG_COUNT
};

/* bit 0-SPK, 1-HP, 2-DAC,
 * 4-MIC, 5-HSMIC, 6-MIC2
 */
#define GBCODEC_CTL_REG_DEFAULT		0x00

/* bit 0,1 - APB1-PB-L/R
 * bit 2,3 - APB2-PB-L/R
 * bit 4,5 - APB1-Cap-L/R
 * bit 6,7 - APB2-Cap-L/R
 */
#define	GBCODEC_MUTE_REG_DEFAULT	0x00

/* 0-127 steps */
#define	GBCODEC_PB_VOL_REG_DEFAULT	0x00
#define	GBCODEC_CAP_VOL_REG_DEFAULT	0x00

/* bit 0,1,2 - PB stereo, left, right
 * bit 8,9,10 - Cap stereo, left, right
 */
#define GBCODEC_APB1_MUX_REG_DEFAULT	0x00
#define GBCODEC_APB2_MUX_REG_DEFAULT	0x00

static const u8 gbcodec_reg_defaults[GBCODEC_REG_COUNT] = {
	GBCODEC_CTL_REG_DEFAULT,
	GBCODEC_MUTE_REG_DEFAULT,
	GBCODEC_PB_VOL_REG_DEFAULT,
	GBCODEC_PB_VOL_REG_DEFAULT,
	GBCODEC_CAP_VOL_REG_DEFAULT,
	GBCODEC_CAP_VOL_REG_DEFAULT,
	GBCODEC_APB1_MUX_REG_DEFAULT,
	GBCODEC_APB2_MUX_REG_DEFAULT,
};

struct gbaudio_codec_info {
	struct snd_soc_codec *codec;

	bool usable;
	u8 reg[GBCODEC_REG_COUNT];
	int registered;

	int num_kcontrols;
	int num_dapm_widgets;
	int num_dapm_routes;
	struct snd_kcontrol_new *kctls;
	struct snd_soc_dapm_widget *widgets;
	struct snd_soc_dapm_route *routes;
	struct mutex lock;
};

#endif /* __KERNEL__ */
#endif /* __LINUX_GBAUDIO_H */
