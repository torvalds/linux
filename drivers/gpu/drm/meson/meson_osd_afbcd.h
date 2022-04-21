/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2019 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#ifndef __MESON_OSD_AFBCD_H
#define __MESON_OSD_AFBCD_H

#include "meson_drv.h"

/* This is an internal address used to transfer pixel from AFBC to the VIU */
#define MESON_G12A_AFBCD_OUT_ADDR	0x1000000

struct meson_afbcd_ops {
	int (*init)(struct meson_drm *priv);
	void (*exit)(struct meson_drm *priv);
	int (*reset)(struct meson_drm *priv);
	int (*enable)(struct meson_drm *priv);
	int (*disable)(struct meson_drm *priv);
	int (*setup)(struct meson_drm *priv);
	int (*fmt_to_blk_mode)(u64 modifier, uint32_t format);
	bool (*supported_fmt)(u64 modifier, uint32_t format);
};

extern struct meson_afbcd_ops meson_afbcd_gxm_ops;
extern struct meson_afbcd_ops meson_afbcd_g12a_ops;

#endif /* __MESON_OSD_AFBCD_H */
