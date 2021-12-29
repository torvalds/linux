// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include "drm/drm_bridge_connector.h"

#include "msm_kms.h"
#include "dsi.h"

#define DSI_CLOCK_MASTER	DSI_0
#define DSI_CLOCK_SLAVE		DSI_1

#define DSI_LEFT		DSI_0
#define DSI_RIGHT		DSI_1

/* According to the current drm framework sequence, take the encoder of
 * DSI_1 as master encoder
 */
#define DSI_ENCODER_MASTER	DSI_1
#define DSI_ENCODER_SLAVE	DSI_0

struct msm_dsi_manager {
	struct msm_dsi *dsi[DSI_MAX];

	bool is_bonded_dsi;
	bool is_sync_needed;
	int master_dsi_link_id;
};

static struct msm_dsi_manager msm_dsim_glb;

#define IS_BONDED_DSI()		(msm_dsim_glb.is_bonded_dsi)
#define IS_SYNC_NEEDED()	(msm_dsim_glb.is_sync_needed)
#define IS_MASTER_DSI_LINK(id)	(msm_dsim_glb.master_dsi_link_id == id)

static inline struct msm_dsi *dsi_mgr_get_dsi(int id)
{
	return msm_dsim_glb.dsi[id];
}

static inline struct msm_dsi *dsi_mgr_get_other_dsi(int id)
{
	return msm_dsim_glb.dsi[(id + 1) % DSI_MAX];
}

static int dsi_mgr_parse_of(struct device_node *np, int id)
{
	struct msm_dsi_manager *msm_dsim = &msm_dsim_glb;

	/* We assume 2 dsi nodes have the same information of bonded dsi and
	 * sync-mode, and only one node specifies master in case of bonded mode.
	 */
	if (!msm_dsim->is_bonded_dsi)
		msm_dsim->is_bonded_dsi = of_property_read_bool(np, "qcom,dual-dsi-mode");

	if (msm_dsim->is_bonded_dsi) {
		if (of_property_read_bool(np, "qcom,master-dsi"))
			msm_dsim->master_dsi_link_id = id;
		if (!msm_dsim->is_sync_needed)
			msm_dsim->is_sync_needed = of_property_read_bool(
					np, "qcom,sync-dual-dsi");
	}

	return 0;
}

static int dsi_mgr_setup_components(int id)
{
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct msm_dsi *other_dsi = dsi_mgr_get_other_dsi(id);
	struct msm_dsi *clk_master_dsi = dsi_mgr_get_dsi(DSI_CLOCK_MASTER);
	struct msm_dsi *clk_slave_dsi = dsi_mgr_get_dsi(DSI_CLOCK_SLAVE);
	int ret;

	if (!IS_BONDED_DSI()) {
		ret = msm_dsi_host_register(msm_dsi->host);
		if (ret)
			return ret;

		msm_dsi_phy_set_usecase(msm_dsi->phy, MSM_DSI_PHY_STANDALONE);
		msm_dsi_host_set_phy_mode(msm_dsi->host, msm_dsi->phy);
	} else if (other_dsi) {
		struct msm_dsi *master_link_dsi = IS_MASTER_DSI_LINK(id) ?
							msm_dsi : other_dsi;
		struct msm_dsi *slave_link_dsi = IS_MASTER_DSI_LINK(id) ?
							other_dsi : msm_dsi;
		/* Register slave host first, so that slave DSI device
		 * has a chance to probe, and do not block the master
		 * DSI device's probe.
		 * Also, do not check defer for the slave host,
		 * because only master DSI device adds the panel to global
		 * panel list. The panel's device is the master DSI device.
		 */
		ret = msm_dsi_host_register(slave_link_dsi->host);
		if (ret)
			return ret;
		ret = msm_dsi_host_register(master_link_dsi->host);
		if (ret)
			return ret;

		/* PLL0 is to drive both 2 DSI link clocks in bonded DSI mode. */
		msm_dsi_phy_set_usecase(clk_master_dsi->phy,
					MSM_DSI_PHY_MASTER);
		msm_dsi_phy_set_usecase(clk_slave_dsi->phy,
					MSM_DSI_PHY_SLAVE);
		msm_dsi_host_set_phy_mode(msm_dsi->host, msm_dsi->phy);
		msm_dsi_host_set_phy_mode(other_dsi->host, other_dsi->phy);
	}

	return 0;
}

static int enable_phy(struct msm_dsi *msm_dsi,
		      struct msm_dsi_phy_shared_timings *shared_timings)
{
	struct msm_dsi_phy_clk_request clk_req;
	int ret;
	bool is_bonded_dsi = IS_BONDED_DSI();

	msm_dsi_host_get_phy_clk_req(msm_dsi->host, &clk_req, is_bonded_dsi);

	ret = msm_dsi_phy_enable(msm_dsi->phy, &clk_req, shared_timings);

	return ret;
}

static int
dsi_mgr_phy_enable(int id,
		   struct msm_dsi_phy_shared_timings shared_timings[DSI_MAX])
{
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct msm_dsi *mdsi = dsi_mgr_get_dsi(DSI_CLOCK_MASTER);
	struct msm_dsi *sdsi = dsi_mgr_get_dsi(DSI_CLOCK_SLAVE);
	int ret;

	/* In case of bonded DSI, some registers in PHY1 have been programmed
	 * during PLL0 clock's set_rate. The PHY1 reset called by host1 here
	 * will silently reset those PHY1 registers. Therefore we need to reset
	 * and enable both PHYs before any PLL clock operation.
	 */
	if (IS_BONDED_DSI() && mdsi && sdsi) {
		if (!mdsi->phy_enabled && !sdsi->phy_enabled) {
			msm_dsi_host_reset_phy(mdsi->host);
			msm_dsi_host_reset_phy(sdsi->host);

			ret = enable_phy(mdsi,
					 &shared_timings[DSI_CLOCK_MASTER]);
			if (ret)
				return ret;
			ret = enable_phy(sdsi,
					 &shared_timings[DSI_CLOCK_SLAVE]);
			if (ret) {
				msm_dsi_phy_disable(mdsi->phy);
				return ret;
			}
		}
	} else {
		msm_dsi_host_reset_phy(msm_dsi->host);
		ret = enable_phy(msm_dsi, &shared_timings[id]);
		if (ret)
			return ret;
	}

	msm_dsi->phy_enabled = true;

	return 0;
}

static void dsi_mgr_phy_disable(int id)
{
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct msm_dsi *mdsi = dsi_mgr_get_dsi(DSI_CLOCK_MASTER);
	struct msm_dsi *sdsi = dsi_mgr_get_dsi(DSI_CLOCK_SLAVE);

	/* disable DSI phy
	 * In bonded dsi configuration, the phy should be disabled for the
	 * first controller only when the second controller is disabled.
	 */
	msm_dsi->phy_enabled = false;
	if (IS_BONDED_DSI() && mdsi && sdsi) {
		if (!mdsi->phy_enabled && !sdsi->phy_enabled) {
			msm_dsi_phy_disable(sdsi->phy);
			msm_dsi_phy_disable(mdsi->phy);
		}
	} else {
		msm_dsi_phy_disable(msm_dsi->phy);
	}
}

struct dsi_connector {
	struct drm_connector base;
	int id;
};

struct dsi_bridge {
	struct drm_bridge base;
	int id;
};

#define to_dsi_connector(x) container_of(x, struct dsi_connector, base)
#define to_dsi_bridge(x) container_of(x, struct dsi_bridge, base)

static inline int dsi_mgr_connector_get_id(struct drm_connector *connector)
{
	struct dsi_connector *dsi_connector = to_dsi_connector(connector);
	return dsi_connector->id;
}

static int dsi_mgr_bridge_get_id(struct drm_bridge *bridge)
{
	struct dsi_bridge *dsi_bridge = to_dsi_bridge(bridge);
	return dsi_bridge->id;
}

static int msm_dsi_manager_panel_init(struct drm_connector *conn, u8 id)
{
	struct msm_drm_private *priv = conn->dev->dev_private;
	struct msm_kms *kms = priv->kms;
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct msm_dsi *other_dsi = dsi_mgr_get_other_dsi(id);
	struct msm_dsi *master_dsi, *slave_dsi;
	struct drm_panel *panel;

	if (IS_BONDED_DSI() && !IS_MASTER_DSI_LINK(id)) {
		master_dsi = other_dsi;
		slave_dsi = msm_dsi;
	} else {
		master_dsi = msm_dsi;
		slave_dsi = other_dsi;
	}

	/*
	 * There is only 1 panel in the global panel list for bonded DSI mode.
	 * Therefore slave dsi should get the drm_panel instance from master
	 * dsi.
	 */
	panel = msm_dsi_host_get_panel(master_dsi->host);
	if (IS_ERR(panel)) {
		DRM_ERROR("Could not find panel for %u (%ld)\n", msm_dsi->id,
			  PTR_ERR(panel));
		return PTR_ERR(panel);
	}

	if (!panel || !IS_BONDED_DSI())
		goto out;

	drm_object_attach_property(&conn->base,
				   conn->dev->mode_config.tile_property, 0);

	/*
	 * Set split display info to kms once bonded DSI panel is connected to
	 * both hosts.
	 */
	if (other_dsi && other_dsi->panel && kms->funcs->set_split_display) {
		kms->funcs->set_split_display(kms, master_dsi->encoder,
					      slave_dsi->encoder,
					      msm_dsi_is_cmd_mode(msm_dsi));
	}

out:
	msm_dsi->panel = panel;
	return 0;
}

static enum drm_connector_status dsi_mgr_connector_detect(
		struct drm_connector *connector, bool force)
{
	int id = dsi_mgr_connector_get_id(connector);
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);

	return msm_dsi->panel ? connector_status_connected :
		connector_status_disconnected;
}

static void dsi_mgr_connector_destroy(struct drm_connector *connector)
{
	struct dsi_connector *dsi_connector = to_dsi_connector(connector);

	DBG("");

	drm_connector_cleanup(connector);

	kfree(dsi_connector);
}

static int dsi_mgr_connector_get_modes(struct drm_connector *connector)
{
	int id = dsi_mgr_connector_get_id(connector);
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct drm_panel *panel = msm_dsi->panel;
	int num;

	if (!panel)
		return 0;

	/*
	 * In bonded DSI mode, we have one connector that can be
	 * attached to the drm_panel.
	 */
	num = drm_panel_get_modes(panel, connector);
	if (!num)
		return 0;

	return num;
}

static enum drm_mode_status dsi_mgr_connector_mode_valid(struct drm_connector *connector,
				struct drm_display_mode *mode)
{
	int id = dsi_mgr_connector_get_id(connector);
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct drm_encoder *encoder = msm_dsi_get_encoder(msm_dsi);
	struct msm_drm_private *priv = connector->dev->dev_private;
	struct msm_kms *kms = priv->kms;
	long actual, requested;

	DBG("");
	requested = 1000 * mode->clock;
	actual = kms->funcs->round_pixclk(kms, requested, encoder);

	DBG("requested=%ld, actual=%ld", requested, actual);
	if (actual != requested)
		return MODE_CLOCK_RANGE;

	return MODE_OK;
}

static struct drm_encoder *
dsi_mgr_connector_best_encoder(struct drm_connector *connector)
{
	int id = dsi_mgr_connector_get_id(connector);
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);

	DBG("");
	return msm_dsi_get_encoder(msm_dsi);
}

static void dsi_mgr_bridge_pre_enable(struct drm_bridge *bridge)
{
	int id = dsi_mgr_bridge_get_id(bridge);
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct msm_dsi *msm_dsi1 = dsi_mgr_get_dsi(DSI_1);
	struct mipi_dsi_host *host = msm_dsi->host;
	struct drm_panel *panel = msm_dsi->panel;
	struct msm_dsi_phy_shared_timings phy_shared_timings[DSI_MAX];
	bool is_bonded_dsi = IS_BONDED_DSI();
	int ret;

	DBG("id=%d", id);
	if (!msm_dsi_device_connected(msm_dsi))
		return;

	/* Do nothing with the host if it is slave-DSI in case of bonded DSI */
	if (is_bonded_dsi && !IS_MASTER_DSI_LINK(id))
		return;

	ret = dsi_mgr_phy_enable(id, phy_shared_timings);
	if (ret)
		goto phy_en_fail;

	ret = msm_dsi_host_power_on(host, &phy_shared_timings[id], is_bonded_dsi, msm_dsi->phy);
	if (ret) {
		pr_err("%s: power on host %d failed, %d\n", __func__, id, ret);
		goto host_on_fail;
	}

	if (is_bonded_dsi && msm_dsi1) {
		ret = msm_dsi_host_power_on(msm_dsi1->host,
				&phy_shared_timings[DSI_1], is_bonded_dsi, msm_dsi1->phy);
		if (ret) {
			pr_err("%s: power on host1 failed, %d\n",
							__func__, ret);
			goto host1_on_fail;
		}
	}

	/*
	 * Enable before preparing the panel, disable after unpreparing, so
	 * that the panel can communicate over the DSI link.
	 */
	msm_dsi_host_enable_irq(host);
	if (is_bonded_dsi && msm_dsi1)
		msm_dsi_host_enable_irq(msm_dsi1->host);

	/* Always call panel functions once, because even for dual panels,
	 * there is only one drm_panel instance.
	 */
	if (panel) {
		ret = drm_panel_prepare(panel);
		if (ret) {
			pr_err("%s: prepare panel %d failed, %d\n", __func__,
								id, ret);
			goto panel_prep_fail;
		}
	}

	ret = msm_dsi_host_enable(host);
	if (ret) {
		pr_err("%s: enable host %d failed, %d\n", __func__, id, ret);
		goto host_en_fail;
	}

	if (is_bonded_dsi && msm_dsi1) {
		ret = msm_dsi_host_enable(msm_dsi1->host);
		if (ret) {
			pr_err("%s: enable host1 failed, %d\n", __func__, ret);
			goto host1_en_fail;
		}
	}

	return;

host1_en_fail:
	msm_dsi_host_disable(host);
host_en_fail:
	if (panel)
		drm_panel_unprepare(panel);
panel_prep_fail:
	msm_dsi_host_disable_irq(host);
	if (is_bonded_dsi && msm_dsi1)
		msm_dsi_host_disable_irq(msm_dsi1->host);

	if (is_bonded_dsi && msm_dsi1)
		msm_dsi_host_power_off(msm_dsi1->host);
host1_on_fail:
	msm_dsi_host_power_off(host);
host_on_fail:
	dsi_mgr_phy_disable(id);
phy_en_fail:
	return;
}

void msm_dsi_manager_tpg_enable(void)
{
	struct msm_dsi *m_dsi = dsi_mgr_get_dsi(DSI_0);
	struct msm_dsi *s_dsi = dsi_mgr_get_dsi(DSI_1);

	/* if dual dsi, trigger tpg on master first then slave */
	if (m_dsi) {
		msm_dsi_host_test_pattern_en(m_dsi->host);
		if (IS_BONDED_DSI() && s_dsi)
			msm_dsi_host_test_pattern_en(s_dsi->host);
	}
}

static void dsi_mgr_bridge_enable(struct drm_bridge *bridge)
{
	int id = dsi_mgr_bridge_get_id(bridge);
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct drm_panel *panel = msm_dsi->panel;
	bool is_bonded_dsi = IS_BONDED_DSI();
	int ret;

	DBG("id=%d", id);
	if (!msm_dsi_device_connected(msm_dsi))
		return;

	/* Do nothing with the host if it is slave-DSI in case of bonded DSI */
	if (is_bonded_dsi && !IS_MASTER_DSI_LINK(id))
		return;

	if (panel) {
		ret = drm_panel_enable(panel);
		if (ret) {
			pr_err("%s: enable panel %d failed, %d\n", __func__, id,
									ret);
		}
	}
}

static void dsi_mgr_bridge_disable(struct drm_bridge *bridge)
{
	int id = dsi_mgr_bridge_get_id(bridge);
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct drm_panel *panel = msm_dsi->panel;
	bool is_bonded_dsi = IS_BONDED_DSI();
	int ret;

	DBG("id=%d", id);
	if (!msm_dsi_device_connected(msm_dsi))
		return;

	/* Do nothing with the host if it is slave-DSI in case of bonded DSI */
	if (is_bonded_dsi && !IS_MASTER_DSI_LINK(id))
		return;

	if (panel) {
		ret = drm_panel_disable(panel);
		if (ret)
			pr_err("%s: Panel %d OFF failed, %d\n", __func__, id,
									ret);
	}
}

static void dsi_mgr_bridge_post_disable(struct drm_bridge *bridge)
{
	int id = dsi_mgr_bridge_get_id(bridge);
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct msm_dsi *msm_dsi1 = dsi_mgr_get_dsi(DSI_1);
	struct mipi_dsi_host *host = msm_dsi->host;
	struct drm_panel *panel = msm_dsi->panel;
	bool is_bonded_dsi = IS_BONDED_DSI();
	int ret;

	DBG("id=%d", id);

	if (!msm_dsi_device_connected(msm_dsi))
		return;

	/*
	 * Do nothing with the host if it is slave-DSI in case of bonded DSI.
	 * It is safe to call dsi_mgr_phy_disable() here because a single PHY
	 * won't be diabled until both PHYs request disable.
	 */
	if (is_bonded_dsi && !IS_MASTER_DSI_LINK(id))
		goto disable_phy;

	ret = msm_dsi_host_disable(host);
	if (ret)
		pr_err("%s: host %d disable failed, %d\n", __func__, id, ret);

	if (is_bonded_dsi && msm_dsi1) {
		ret = msm_dsi_host_disable(msm_dsi1->host);
		if (ret)
			pr_err("%s: host1 disable failed, %d\n", __func__, ret);
	}

	if (panel) {
		ret = drm_panel_unprepare(panel);
		if (ret)
			pr_err("%s: Panel %d unprepare failed,%d\n", __func__,
								id, ret);
	}

	msm_dsi_host_disable_irq(host);
	if (is_bonded_dsi && msm_dsi1)
		msm_dsi_host_disable_irq(msm_dsi1->host);

	/* Save PHY status if it is a clock source */
	msm_dsi_phy_pll_save_state(msm_dsi->phy);

	ret = msm_dsi_host_power_off(host);
	if (ret)
		pr_err("%s: host %d power off failed,%d\n", __func__, id, ret);

	if (is_bonded_dsi && msm_dsi1) {
		ret = msm_dsi_host_power_off(msm_dsi1->host);
		if (ret)
			pr_err("%s: host1 power off failed, %d\n",
								__func__, ret);
	}

disable_phy:
	dsi_mgr_phy_disable(id);
}

static void dsi_mgr_bridge_mode_set(struct drm_bridge *bridge,
		const struct drm_display_mode *mode,
		const struct drm_display_mode *adjusted_mode)
{
	int id = dsi_mgr_bridge_get_id(bridge);
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct msm_dsi *other_dsi = dsi_mgr_get_other_dsi(id);
	struct mipi_dsi_host *host = msm_dsi->host;
	bool is_bonded_dsi = IS_BONDED_DSI();

	DBG("set mode: " DRM_MODE_FMT, DRM_MODE_ARG(mode));

	if (is_bonded_dsi && !IS_MASTER_DSI_LINK(id))
		return;

	msm_dsi_host_set_display_mode(host, adjusted_mode);
	if (is_bonded_dsi && other_dsi)
		msm_dsi_host_set_display_mode(other_dsi->host, adjusted_mode);
}

static const struct drm_connector_funcs dsi_mgr_connector_funcs = {
	.detect = dsi_mgr_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = dsi_mgr_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs dsi_mgr_conn_helper_funcs = {
	.get_modes = dsi_mgr_connector_get_modes,
	.mode_valid = dsi_mgr_connector_mode_valid,
	.best_encoder = dsi_mgr_connector_best_encoder,
};

static const struct drm_bridge_funcs dsi_mgr_bridge_funcs = {
	.pre_enable = dsi_mgr_bridge_pre_enable,
	.enable = dsi_mgr_bridge_enable,
	.disable = dsi_mgr_bridge_disable,
	.post_disable = dsi_mgr_bridge_post_disable,
	.mode_set = dsi_mgr_bridge_mode_set,
};

/* initialize connector when we're connected to a drm_panel */
struct drm_connector *msm_dsi_manager_connector_init(u8 id)
{
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct drm_connector *connector = NULL;
	struct dsi_connector *dsi_connector;
	int ret;

	dsi_connector = kzalloc(sizeof(*dsi_connector), GFP_KERNEL);
	if (!dsi_connector)
		return ERR_PTR(-ENOMEM);

	dsi_connector->id = id;

	connector = &dsi_connector->base;

	ret = drm_connector_init(msm_dsi->dev, connector,
			&dsi_mgr_connector_funcs, DRM_MODE_CONNECTOR_DSI);
	if (ret)
		return ERR_PTR(ret);

	drm_connector_helper_add(connector, &dsi_mgr_conn_helper_funcs);

	/* Enable HPD to let hpd event is handled
	 * when panel is attached to the host.
	 */
	connector->polled = DRM_CONNECTOR_POLL_HPD;

	/* Display driver doesn't support interlace now. */
	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	drm_connector_attach_encoder(connector, msm_dsi->encoder);

	ret = msm_dsi_manager_panel_init(connector, id);
	if (ret) {
		DRM_DEV_ERROR(msm_dsi->dev->dev, "init panel failed %d\n", ret);
		goto fail;
	}

	return connector;

fail:
	connector->funcs->destroy(msm_dsi->connector);
	return ERR_PTR(ret);
}

/* initialize bridge */
struct drm_bridge *msm_dsi_manager_bridge_init(u8 id)
{
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct drm_bridge *bridge = NULL;
	struct dsi_bridge *dsi_bridge;
	struct drm_encoder *encoder;
	int ret;

	dsi_bridge = devm_kzalloc(msm_dsi->dev->dev,
				sizeof(*dsi_bridge), GFP_KERNEL);
	if (!dsi_bridge) {
		ret = -ENOMEM;
		goto fail;
	}

	dsi_bridge->id = id;

	encoder = msm_dsi->encoder;

	bridge = &dsi_bridge->base;
	bridge->funcs = &dsi_mgr_bridge_funcs;

	ret = drm_bridge_attach(encoder, bridge, NULL, 0);
	if (ret)
		goto fail;

	return bridge;

fail:
	if (bridge)
		msm_dsi_manager_bridge_destroy(bridge);

	return ERR_PTR(ret);
}

struct drm_connector *msm_dsi_manager_ext_bridge_init(u8 id)
{
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct drm_device *dev = msm_dsi->dev;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	struct drm_bridge *int_bridge, *ext_bridge;
	int ret;

	int_bridge = msm_dsi->bridge;
	ext_bridge = msm_dsi->external_bridge =
			msm_dsi_host_get_bridge(msm_dsi->host);

	encoder = msm_dsi->encoder;

	/*
	 * Try first to create the bridge without it creating its own
	 * connector.. currently some bridges support this, and others
	 * do not (and some support both modes)
	 */
	ret = drm_bridge_attach(encoder, ext_bridge, int_bridge,
			DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret == -EINVAL) {
		struct drm_connector *connector;
		struct list_head *connector_list;

		/* link the internal dsi bridge to the external bridge */
		drm_bridge_attach(encoder, ext_bridge, int_bridge, 0);

		/*
		 * we need the drm_connector created by the external bridge
		 * driver (or someone else) to feed it to our driver's
		 * priv->connector[] list, mainly for msm_fbdev_init()
		 */
		connector_list = &dev->mode_config.connector_list;

		list_for_each_entry(connector, connector_list, head) {
			if (drm_connector_has_possible_encoder(connector, encoder))
				return connector;
		}

		return ERR_PTR(-ENODEV);
	}

	connector = drm_bridge_connector_init(dev, encoder);
	if (IS_ERR(connector)) {
		DRM_ERROR("Unable to create bridge connector\n");
		return ERR_CAST(connector);
	}

	drm_connector_attach_encoder(connector, encoder);

	return connector;
}

void msm_dsi_manager_bridge_destroy(struct drm_bridge *bridge)
{
}

int msm_dsi_manager_cmd_xfer(int id, const struct mipi_dsi_msg *msg)
{
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct msm_dsi *msm_dsi0 = dsi_mgr_get_dsi(DSI_0);
	struct mipi_dsi_host *host = msm_dsi->host;
	bool is_read = (msg->rx_buf && msg->rx_len);
	bool need_sync = (IS_SYNC_NEEDED() && !is_read);
	int ret;

	if (!msg->tx_buf || !msg->tx_len)
		return 0;

	/* In bonded master case, panel requires the same commands sent to
	 * both DSI links. Host issues the command trigger to both links
	 * when DSI_1 calls the cmd transfer function, no matter it happens
	 * before or after DSI_0 cmd transfer.
	 */
	if (need_sync && (id == DSI_0))
		return is_read ? msg->rx_len : msg->tx_len;

	if (need_sync && msm_dsi0) {
		ret = msm_dsi_host_xfer_prepare(msm_dsi0->host, msg);
		if (ret) {
			pr_err("%s: failed to prepare non-trigger host, %d\n",
				__func__, ret);
			return ret;
		}
	}
	ret = msm_dsi_host_xfer_prepare(host, msg);
	if (ret) {
		pr_err("%s: failed to prepare host, %d\n", __func__, ret);
		goto restore_host0;
	}

	ret = is_read ? msm_dsi_host_cmd_rx(host, msg) :
			msm_dsi_host_cmd_tx(host, msg);

	msm_dsi_host_xfer_restore(host, msg);

restore_host0:
	if (need_sync && msm_dsi0)
		msm_dsi_host_xfer_restore(msm_dsi0->host, msg);

	return ret;
}

bool msm_dsi_manager_cmd_xfer_trigger(int id, u32 dma_base, u32 len)
{
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct msm_dsi *msm_dsi0 = dsi_mgr_get_dsi(DSI_0);
	struct mipi_dsi_host *host = msm_dsi->host;

	if (IS_SYNC_NEEDED() && (id == DSI_0))
		return false;

	if (IS_SYNC_NEEDED() && msm_dsi0)
		msm_dsi_host_cmd_xfer_commit(msm_dsi0->host, dma_base, len);

	msm_dsi_host_cmd_xfer_commit(host, dma_base, len);

	return true;
}

int msm_dsi_manager_register(struct msm_dsi *msm_dsi)
{
	struct msm_dsi_manager *msm_dsim = &msm_dsim_glb;
	int id = msm_dsi->id;
	int ret;

	if (id >= DSI_MAX) {
		pr_err("%s: invalid id %d\n", __func__, id);
		return -EINVAL;
	}

	if (msm_dsim->dsi[id]) {
		pr_err("%s: dsi%d already registered\n", __func__, id);
		return -EBUSY;
	}

	msm_dsim->dsi[id] = msm_dsi;

	ret = dsi_mgr_parse_of(msm_dsi->pdev->dev.of_node, id);
	if (ret) {
		pr_err("%s: failed to parse OF DSI info\n", __func__);
		goto fail;
	}

	ret = dsi_mgr_setup_components(id);
	if (ret) {
		pr_err("%s: failed to register mipi dsi host for DSI %d: %d\n",
			__func__, id, ret);
		goto fail;
	}

	return 0;

fail:
	msm_dsim->dsi[id] = NULL;
	return ret;
}

void msm_dsi_manager_unregister(struct msm_dsi *msm_dsi)
{
	struct msm_dsi_manager *msm_dsim = &msm_dsim_glb;

	if (msm_dsi->host)
		msm_dsi_host_unregister(msm_dsi->host);

	if (msm_dsi->id >= 0)
		msm_dsim->dsi[msm_dsi->id] = NULL;
}

bool msm_dsi_is_bonded_dsi(struct msm_dsi *msm_dsi)
{
	return IS_BONDED_DSI();
}

bool msm_dsi_is_master_dsi(struct msm_dsi *msm_dsi)
{
	return IS_MASTER_DSI_LINK(msm_dsi->id);
}
