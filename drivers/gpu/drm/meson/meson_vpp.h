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

/* Video Post Process */

#ifndef __MESON_VPP_H
#define __MESON_VPP_H

/* Mux VIU/VPP to ENCI */
#define MESON_VIU_VPP_MUX_ENCI	0x5

void meson_vpp_setup_mux(struct meson_drm *priv, unsigned int mux);

void meson_vpp_setup_interlace_vscaler_osd1(struct meson_drm *priv,
					    struct drm_rect *input);
void meson_vpp_disable_interlace_vscaler_osd1(struct meson_drm *priv);

void meson_vpp_init(struct meson_drm *priv);

#endif /* __MESON_VPP_H */
