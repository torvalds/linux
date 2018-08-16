/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* Video Clock */

#ifndef __MESON_VCLK_H
#define __MESON_VCLK_H

enum {
	MESON_VCLK_TARGET_CVBS = 0,
	MESON_VCLK_TARGET_HDMI = 1,
	MESON_VCLK_TARGET_DMT = 2,
};

/* 27MHz is the CVBS Pixel Clock */
#define MESON_VCLK_CVBS			27000

enum drm_mode_status
meson_vclk_dmt_supported_freq(struct meson_drm *priv, unsigned int freq);

void meson_vclk_setup(struct meson_drm *priv, unsigned int target,
		      unsigned int vclk_freq, unsigned int venc_freq,
		      unsigned int dac_freq, bool hdmi_use_enci);

#endif /* __MESON_VCLK_H */
