/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DSI_CONNECTOR_H__
#define __DSI_CONNECTOR_H__

#include <linux/platform_device.h>

#include "drm_crtc.h"
#include "drm_mipi_dsi.h"
#include "drm_panel.h"

#include "msm_drv.h"

#define DSI_0	0
#define DSI_1	1
#define DSI_MAX	2

#define DSI_CLOCK_MASTER	DSI_0
#define DSI_CLOCK_SLAVE		DSI_1

#define DSI_LEFT		DSI_0
#define DSI_RIGHT		DSI_1

/* According to the current drm framework sequence, take the encoder of
 * DSI_1 as master encoder
 */
#define DSI_ENCODER_MASTER	DSI_1
#define DSI_ENCODER_SLAVE	DSI_0

struct msm_dsi {
	struct drm_device *dev;
	struct platform_device *pdev;

	struct drm_connector *connector;
	struct drm_bridge *bridge;

	struct mipi_dsi_host *host;
	struct msm_dsi_phy *phy;
	struct drm_panel *panel;
	unsigned long panel_flags;
	bool phy_enabled;

	/* the encoders we are hooked to (outside of dsi block) */
	struct drm_encoder *encoders[MSM_DSI_ENCODER_NUM];

	int id;
};

/* dsi manager */
struct drm_bridge *msm_dsi_manager_bridge_init(u8 id);
void msm_dsi_manager_bridge_destroy(struct drm_bridge *bridge);
struct drm_connector *msm_dsi_manager_connector_init(u8 id);
int msm_dsi_manager_phy_enable(int id,
		const unsigned long bit_rate, const unsigned long esc_rate,
		u32 *clk_pre, u32 *clk_post);
void msm_dsi_manager_phy_disable(int id);
int msm_dsi_manager_cmd_xfer(int id, const struct mipi_dsi_msg *msg);
bool msm_dsi_manager_cmd_xfer_trigger(int id, u32 iova, u32 len);
int msm_dsi_manager_register(struct msm_dsi *msm_dsi);
void msm_dsi_manager_unregister(struct msm_dsi *msm_dsi);

/* msm dsi */
struct drm_encoder *msm_dsi_get_encoder(struct msm_dsi *msm_dsi);

/* dsi host */
int msm_dsi_host_xfer_prepare(struct mipi_dsi_host *host,
					const struct mipi_dsi_msg *msg);
void msm_dsi_host_xfer_restore(struct mipi_dsi_host *host,
					const struct mipi_dsi_msg *msg);
int msm_dsi_host_cmd_tx(struct mipi_dsi_host *host,
					const struct mipi_dsi_msg *msg);
int msm_dsi_host_cmd_rx(struct mipi_dsi_host *host,
					const struct mipi_dsi_msg *msg);
void msm_dsi_host_cmd_xfer_commit(struct mipi_dsi_host *host,
					u32 iova, u32 len);
int msm_dsi_host_enable(struct mipi_dsi_host *host);
int msm_dsi_host_disable(struct mipi_dsi_host *host);
int msm_dsi_host_power_on(struct mipi_dsi_host *host);
int msm_dsi_host_power_off(struct mipi_dsi_host *host);
int msm_dsi_host_set_display_mode(struct mipi_dsi_host *host,
					struct drm_display_mode *mode);
struct drm_panel *msm_dsi_host_get_panel(struct mipi_dsi_host *host,
					unsigned long *panel_flags);
int msm_dsi_host_register(struct mipi_dsi_host *host, bool check_defer);
void msm_dsi_host_unregister(struct mipi_dsi_host *host);
void msm_dsi_host_destroy(struct mipi_dsi_host *host);
int msm_dsi_host_modeset_init(struct mipi_dsi_host *host,
					struct drm_device *dev);
int msm_dsi_host_init(struct msm_dsi *msm_dsi);

/* dsi phy */
struct msm_dsi_phy;
enum msm_dsi_phy_type {
	MSM_DSI_PHY_UNKNOWN,
	MSM_DSI_PHY_28NM,
	MSM_DSI_PHY_MAX
};
struct msm_dsi_phy *msm_dsi_phy_init(struct platform_device *pdev,
			enum msm_dsi_phy_type type, int id);
int msm_dsi_phy_enable(struct msm_dsi_phy *phy, bool is_dual_panel,
	const unsigned long bit_rate, const unsigned long esc_rate);
int msm_dsi_phy_disable(struct msm_dsi_phy *phy);
void msm_dsi_phy_get_clk_pre_post(struct msm_dsi_phy *phy,
					u32 *clk_pre, u32 *clk_post);
#endif /* __DSI_CONNECTOR_H__ */

