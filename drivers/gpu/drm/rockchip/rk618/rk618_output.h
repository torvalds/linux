/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _RK618_OUTPUT_
#define _RK618_OUTPUT_

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <video/of_display_timing.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/rk618.h>

#include <drm/drmP.h>
#include <drm/drm_of.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_panel.h>
#include <drm/drm_mipi_dsi.h>

#include <video/videomode.h>

#include "../rockchip_drm_drv.h"
#include "../rockchip_drm_vop.h"

#define UPDATE(x, h, l)		(((x) << (l)) & GENMASK((h), (l)))
#define HIWORD_UPDATE(v, h, l)	(((v) << (l)) | (GENMASK((h), (l)) << 16))

#define RK618_IO_CON0			0x0088
#define VIF1_SYNC_MODE_ENABLE		HIWORD_UPDATE(1, 15, 15)
#define VIF1_SYNC_MODE_DISABLE		HIWORD_UPDATE(0, 15, 15)
#define VIF0_SYNC_MODE_ENABLE		HIWORD_UPDATE(1, 14, 14)
#define VIF0_SYNC_MODE_DISABLE		HIWORD_UPDATE(0, 14, 14)
#define PORT2_OUTPUT_LVDS		HIWORD_UPDATE(1, 11, 11)
#define PORT2_OUTPUT_TTL		HIWORD_UPDATE(0, 11, 11)
#define PORT1_OUTPUT_TTL_DISABLE	HIWORD_UPDATE(1, 10, 10)
#define PORT1_OUTPUT_TTL_ENABLE		HIWORD_UPDATE(0, 10, 10)
#define PORT2_IO_PULL_DOWN_DISABLE	HIWORD_UPDATE(1, 9, 9)
#define PORT2_IO_PULL_DOWN_ENABLE	HIWORD_UPDATE(0, 9, 9)
#define PORT1_IO_PULL_DOWN_DISABLE	HIWORD_UPDATE(1, 8, 8)
#define PORT1_IO_PULL_DOWN_ENABLE	HIWORD_UPDATE(0, 8, 8)
#define PORT0_IO_PULL_DOWN_DISABLE	HIWORD_UPDATE(1, 7, 7)
#define PORT0_IO_PULL_DOWN_ENABLE	HIWORD_UPDATE(0, 7, 7)
#define HDMI_IO_PULL_UP_DISABLE		HIWORD_UPDATE(1, 6, 6)
#define HDMI_IO_PULL_UP_ENABLE		HIWORD_UPDATE(0, 6, 6)
#define I2C_IO_PULL_UP_DISABLE		HIWORD_UPDATE(1, 2, 2)
#define I2C_IO_PULL_UP_ENABLE		HIWORD_UPDATE(0, 2, 2)
#define INT_IO_PULL_UP			HIWORD_UPDATE(1, 1, 1)
#define INT_IO_PULL_DOWN		HIWORD_UPDATE(0, 1, 1)
#define CLKIN_IO_PULL_UP		HIWORD_UPDATE(1, 0, 0)
#define CLKIN_IO_PULL_DOWN		HIWORD_UPDATE(0, 0, 0)
#define RK618_IO_CON1			0x008c
#define PORT2_IO_SCHMITT_INPUT_ENABLE	HIWORD_UPDATE(1, 9, 9)
#define PORT2_IO_SCHMITT_INPUT_DISABLE	HIWORD_UPDATE(0, 9, 9)
#define PORT1_IO_SCHMITT_INPUT_ENABLE	HIWORD_UPDATE(1, 8, 8)
#define PORT1_IO_SCHMITT_INPUT_DISABLE	HIWORD_UPDATE(0, 8, 8)
#define PORT0_IO_SCHMITT_INPUT_ENABLE	HIWORD_UPDATE(1, 7, 7)
#define PORT0_IO_SCHMITT_INPUT_DISABLE	HIWORD_UPDATE(0, 7, 7)
#define HDMI_IO_SCHMITT_INPUT_ENABLE	HIWORD_UPDATE(1, 6, 6)
#define HDMI_IO_SCHMITT_INPUT_DISABLE	HIWORD_UPDATE(0, 6, 6)
#define I2C_IO_SCHMITT_INPUT_ENABLE	HIWORD_UPDATE(1, 2, 2)
#define I2C_IO_SCHMITT_INPUT_DISABLE	HIWORD_UPDATE(0, 2, 2)
#define INT_IO_SCHMITT_INPUT_ENABLE	HIWORD_UPDATE(1, 1, 1)
#define INT_IO_SCHMITT_INPUT_DISABLE	HIWORD_UPDATE(0, 1, 1)
#define CLKIN_IO_SCHMITT_INPUT_ENABLE	HIWORD_UPDATE(1, 0, 0)
#define CLKIN_IO_SCHMITT_INPUT_DISABLE	HIWORD_UPDATE(0, 0, 0)
#define RK618_MISC_CON			0x009c
#define HDMI_INT_STATUS			BIT(20)
#define MIPI_INT_STATUS			BIT(19)
#define MIPI_EDPI_HALT			BIT(16)
#define HDMI_HSYNC_POL_INV		BIT(15)
#define HDMI_VSYNC_POL_INV		BIT(14)
#define HDMI_CLK_SEL_MASK		GENMASK(13, 12)
#define HDMI_CLK_SEL_VIDEO_INF0_CLK	UPDATE(2, 13, 12)
#define HDMI_CLK_SEL_SCALER_CLK		UPDATE(1, 13, 12)
#define HDMI_CLK_SEL_VIDEO_INF1_CLK	0
#define INT_ACTIVE_LOW			BIT(5)
#define INT_ACTIVE_HIGH			0
#define DOUBLE_CH_LVDS_DEN_POLARITY	BIT(4)
#define DOUBLE_CH_LVDS_DEN_LOW		BIT(4)
#define DOUBLE_CH_LVDS_DEN_HIGH		0
#define DOUBLE_CH_LVDS_HSYNC_POLARITY	BIT(3)
#define DOUBLE_CH_LVDS_HSYNC_LOW	BIT(3)
#define DOUBLE_CH_LVDS_HSYNC_HIGH	0
#define MIPI_DPICOLOM			BIT(2)
#define MIPI_DPISHUTDN			BIT(1)

struct rk618_output;

struct rk618_output_funcs {
	void (*pre_enable)(struct rk618_output *output);
	void (*enable)(struct rk618_output *output);
	void (*post_disable)(struct rk618_output *output);
	void (*disable)(struct rk618_output *output);
	void (*mode_set)(struct rk618_output *output,
			 const struct drm_display_mode *mode);
};

struct rk618_output {
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct drm_bridge bridge;
	struct drm_panel *panel;
	struct drm_display_mode panel_mode;
	struct drm_display_mode scale_mode;
	u32 bus_format;

	struct device *dev;
	struct device_node *panel_node;
	struct rk618 *parent;
	struct clk *dither_clk;
	struct clk *vif_clk;
	struct clk *scaler_clk;

	const struct rk618_output_funcs *funcs;
};

int rk618_output_register(struct rk618_output *output);
int rk618_output_bind(struct rk618_output *output, struct drm_device *drm,
			  int encoder_type, int connector_type);
void rk618_output_unbind(struct rk618_output *output);

void rk618_scaler_enable(struct rk618 *rk618);
void rk618_scaler_disable(struct rk618 *rk618);
void rk618_scaler_configure(struct rk618 *rk618,
			    const struct drm_display_mode *scale_mode,
			    const struct drm_display_mode *panel_mode);

void rk618_dither_enable(struct rk618 *rk618);
void rk618_dither_disable(struct rk618 *rk618);
void rk618_dither_frc_dclk_invert(struct rk618 *rk618);

#endif
