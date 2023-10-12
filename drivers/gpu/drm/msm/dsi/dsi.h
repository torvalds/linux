/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#ifndef __DSI_CONNECTOR_H__
#define __DSI_CONNECTOR_H__

#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>

#include "msm_drv.h"
#include "disp/msm_disp_snapshot.h"

#define DSI_0	0
#define DSI_1	1
#define DSI_MAX	2

struct msm_dsi_phy_shared_timings;
struct msm_dsi_phy_clk_request;

enum msm_dsi_phy_usecase {
	MSM_DSI_PHY_STANDALONE,
	MSM_DSI_PHY_MASTER,
	MSM_DSI_PHY_SLAVE,
};

#define DSI_BUS_CLK_MAX		4

struct msm_dsi {
	struct drm_device *dev;
	struct platform_device *pdev;

	/* internal dsi bridge attached to MDP interface */
	struct drm_bridge *bridge;

	struct mipi_dsi_host *host;
	struct msm_dsi_phy *phy;

	/*
	 * external_bridge connected to dsi bridge output
	 */
	struct drm_bridge *external_bridge;

	struct device *phy_dev;
	bool phy_enabled;

	/* the encoder we are hooked to (outside of dsi block) */
	struct drm_encoder *encoder;

	int id;
};

/* dsi manager */
struct drm_bridge *msm_dsi_manager_bridge_init(u8 id);
void msm_dsi_manager_bridge_destroy(struct drm_bridge *bridge);
int msm_dsi_manager_ext_bridge_init(u8 id);
int msm_dsi_manager_cmd_xfer(int id, const struct mipi_dsi_msg *msg);
bool msm_dsi_manager_cmd_xfer_trigger(int id, u32 dma_base, u32 len);
int msm_dsi_manager_register(struct msm_dsi *msm_dsi);
void msm_dsi_manager_unregister(struct msm_dsi *msm_dsi);
void msm_dsi_manager_tpg_enable(void);

/* msm dsi */
static inline bool msm_dsi_device_connected(struct msm_dsi *msm_dsi)
{
	return msm_dsi->external_bridge;
}

/* dsi host */
struct msm_dsi_host;
int msm_dsi_host_xfer_prepare(struct mipi_dsi_host *host,
					const struct mipi_dsi_msg *msg);
void msm_dsi_host_xfer_restore(struct mipi_dsi_host *host,
					const struct mipi_dsi_msg *msg);
int msm_dsi_host_cmd_tx(struct mipi_dsi_host *host,
					const struct mipi_dsi_msg *msg);
int msm_dsi_host_cmd_rx(struct mipi_dsi_host *host,
					const struct mipi_dsi_msg *msg);
void msm_dsi_host_cmd_xfer_commit(struct mipi_dsi_host *host,
					u32 dma_base, u32 len);
int msm_dsi_host_enable(struct mipi_dsi_host *host);
int msm_dsi_host_disable(struct mipi_dsi_host *host);
void msm_dsi_host_enable_irq(struct mipi_dsi_host *host);
void msm_dsi_host_disable_irq(struct mipi_dsi_host *host);
int msm_dsi_host_power_on(struct mipi_dsi_host *host,
			struct msm_dsi_phy_shared_timings *phy_shared_timings,
			bool is_bonded_dsi, struct msm_dsi_phy *phy);
int msm_dsi_host_power_off(struct mipi_dsi_host *host);
int msm_dsi_host_set_display_mode(struct mipi_dsi_host *host,
				  const struct drm_display_mode *mode);
enum drm_mode_status msm_dsi_host_check_dsc(struct mipi_dsi_host *host,
					    const struct drm_display_mode *mode);
unsigned long msm_dsi_host_get_mode_flags(struct mipi_dsi_host *host);
int msm_dsi_host_register(struct mipi_dsi_host *host);
void msm_dsi_host_unregister(struct mipi_dsi_host *host);
void msm_dsi_host_set_phy_mode(struct mipi_dsi_host *host,
			struct msm_dsi_phy *src_phy);
int msm_dsi_host_set_src_pll(struct mipi_dsi_host *host,
			struct msm_dsi_phy *src_phy);
void msm_dsi_host_reset_phy(struct mipi_dsi_host *host);
void msm_dsi_host_get_phy_clk_req(struct mipi_dsi_host *host,
	struct msm_dsi_phy_clk_request *clk_req,
	bool is_bonded_dsi);
void msm_dsi_host_destroy(struct mipi_dsi_host *host);
int msm_dsi_host_modeset_init(struct mipi_dsi_host *host,
					struct drm_device *dev);
int msm_dsi_host_init(struct msm_dsi *msm_dsi);
int msm_dsi_runtime_suspend(struct device *dev);
int msm_dsi_runtime_resume(struct device *dev);
int dsi_link_clk_set_rate_6g(struct msm_dsi_host *msm_host);
int dsi_link_clk_set_rate_v2(struct msm_dsi_host *msm_host);
int dsi_link_clk_enable_6g(struct msm_dsi_host *msm_host);
int dsi_link_clk_enable_v2(struct msm_dsi_host *msm_host);
void dsi_link_clk_disable_6g(struct msm_dsi_host *msm_host);
void dsi_link_clk_disable_v2(struct msm_dsi_host *msm_host);
unsigned long dsi_byte_clk_get_rate(struct mipi_dsi_host *host, bool is_bonded_dsi,
				    const struct drm_display_mode *mode);
int dsi_tx_buf_alloc_6g(struct msm_dsi_host *msm_host, int size);
int dsi_tx_buf_alloc_v2(struct msm_dsi_host *msm_host, int size);
void *dsi_tx_buf_get_6g(struct msm_dsi_host *msm_host);
void *dsi_tx_buf_get_v2(struct msm_dsi_host *msm_host);
void dsi_tx_buf_put_6g(struct msm_dsi_host *msm_host);
void msm_dsi_tx_buf_free(struct mipi_dsi_host *mipi_host);
int dsi_dma_base_get_6g(struct msm_dsi_host *msm_host, uint64_t *iova);
int dsi_dma_base_get_v2(struct msm_dsi_host *msm_host, uint64_t *iova);
int dsi_clk_init_v2(struct msm_dsi_host *msm_host);
int dsi_clk_init_6g_v2(struct msm_dsi_host *msm_host);
int dsi_calc_clk_rate_v2(struct msm_dsi_host *msm_host, bool is_bonded_dsi);
int dsi_calc_clk_rate_6g(struct msm_dsi_host *msm_host, bool is_bonded_dsi);
void msm_dsi_host_snapshot(struct msm_disp_state *disp_state, struct mipi_dsi_host *host);
void msm_dsi_host_test_pattern_en(struct mipi_dsi_host *host);
struct drm_dsc_config *msm_dsi_host_get_dsc_config(struct mipi_dsi_host *host);

/* dsi phy */
struct msm_dsi_phy;
struct msm_dsi_phy_shared_timings {
	u32 clk_post;
	u32 clk_pre;
	bool clk_pre_inc_by_2;
	bool byte_intf_clk_div_2;
};

struct msm_dsi_phy_clk_request {
	unsigned long bitclk_rate;
	unsigned long escclk_rate;
};

void msm_dsi_phy_driver_register(void);
void msm_dsi_phy_driver_unregister(void);
int msm_dsi_phy_enable(struct msm_dsi_phy *phy,
			struct msm_dsi_phy_clk_request *clk_req,
			struct msm_dsi_phy_shared_timings *shared_timings);
void msm_dsi_phy_disable(struct msm_dsi_phy *phy);
void msm_dsi_phy_set_usecase(struct msm_dsi_phy *phy,
			     enum msm_dsi_phy_usecase uc);
void msm_dsi_phy_pll_save_state(struct msm_dsi_phy *phy);
int msm_dsi_phy_pll_restore_state(struct msm_dsi_phy *phy);
void msm_dsi_phy_snapshot(struct msm_disp_state *disp_state, struct msm_dsi_phy *phy);
bool msm_dsi_phy_set_continuous_clock(struct msm_dsi_phy *phy, bool enable);

#endif /* __DSI_CONNECTOR_H__ */

