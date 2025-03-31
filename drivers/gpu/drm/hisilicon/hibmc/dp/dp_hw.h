/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2024 Hisilicon Limited. */

#ifndef DP_KAPI_H
#define DP_KAPI_H

#include <linux/types.h>
#include <linux/delay.h>
#include <drm/drm_device.h>
#include <drm/drm_encoder.h>
#include <drm/drm_connector.h>
#include <drm/drm_print.h>
#include <drm/display/drm_dp_helper.h>

struct hibmc_dp_dev;

enum hibmc_dp_cbar_pattern {
	CBAR_COLOR_BAR,
	CBAR_WHITE,
	CBAR_RED,
	CBAR_ORANGE,
	CBAR_YELLOW,
	CBAR_GREEN,
	CBAR_CYAN,
	CBAR_BLUE,
	CBAR_PURPLE,
	CBAR_BLACK,
};

struct hibmc_dp_color_raw {
	enum hibmc_dp_cbar_pattern pattern;
	u32 r_value;
	u32 g_value;
	u32 b_value;
};

struct hibmc_dp_cbar_cfg {
	u8 enable;
	u8 self_timing;
	u8 dynamic_rate; /* 0:static, 1-255(frame):dynamic */
	enum hibmc_dp_cbar_pattern pattern;
};

struct hibmc_dp {
	struct hibmc_dp_dev *dp_dev;
	struct drm_device *drm_dev;
	struct drm_encoder encoder;
	struct drm_connector connector;
	void __iomem *mmio;
	struct drm_dp_aux aux;
	struct hibmc_dp_cbar_cfg cfg;
	u32 irq_status;
};

int hibmc_dp_hw_init(struct hibmc_dp *dp);
int hibmc_dp_mode_set(struct hibmc_dp *dp, struct drm_display_mode *mode);
void hibmc_dp_display_en(struct hibmc_dp *dp, bool enable);
void hibmc_dp_set_cbar(struct hibmc_dp *dp, const struct hibmc_dp_cbar_cfg *cfg);
void hibmc_dp_reset_link(struct hibmc_dp *dp);
void hibmc_dp_hpd_cfg(struct hibmc_dp *dp);
void hibmc_dp_enable_int(struct hibmc_dp *dp);
void hibmc_dp_disable_int(struct hibmc_dp *dp);

#endif
