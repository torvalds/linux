/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

/* Video Clock */

#ifndef __MESON_VCLK_H
#define __MESON_VCLK_H

#include <drm/drm_modes.h>

struct meson_drm;

enum {
	MESON_VCLK_TARGET_CVBS = 0,
	MESON_VCLK_TARGET_HDMI = 1,
	MESON_VCLK_TARGET_DMT = 2,
};

/* 27MHz is the CVBS Pixel Clock */
#define MESON_VCLK_CVBS			27000

enum drm_mode_status
meson_vclk_dmt_supported_freq(struct meson_drm *priv, unsigned int freq);
enum drm_mode_status
meson_vclk_vic_supported_freq(struct meson_drm *priv, unsigned int phy_freq,
			      unsigned int vclk_freq);

void meson_vclk_setup(struct meson_drm *priv, unsigned int target,
		      unsigned int phy_freq, unsigned int vclk_freq,
		      unsigned int venc_freq, unsigned int dac_freq,
		      bool hdmi_use_enci);

#endif /* __MESON_VCLK_H */
