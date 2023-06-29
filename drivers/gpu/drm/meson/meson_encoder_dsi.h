/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2021 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#ifndef __MESON_ENCODER_DSI_H
#define __MESON_ENCODER_DSI_H

int meson_encoder_dsi_init(struct meson_drm *priv);
void meson_encoder_dsi_remove(struct meson_drm *priv);

#endif /* __MESON_ENCODER_DSI_H */
