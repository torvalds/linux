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

	bool is_dual_dsi;
	bool is_sync_needed;
	int master_dsi_link_id;
};

static struct msm_dsi_manager msm_dsim_glb;

#define IS_DUAL_DSI()		(msm_dsim_glb.is_dual_dsi)
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

static int dsi_mgr_parse_dual_dsi(struct device_node *np, int id)
{
	struct msm_dsi_manager *msm_dsim = &msm_dsim_glb;

	/* We assume 2 dsi nodes have the same information of dual-dsi and
	 * sync-mode, and only one node specifies master in case of dual mode.
	 */
	if (!msm_dsim->is_dual_dsi)
		msm_dsim->is_dual_dsi = of_property_read_bool(
						np, "qcom,dual-dsi-mode");

	if (msm_dsim->is_dual_dsi) {
		if (of_property_read_bool(np, "qcom,master-dsi"))
			msm_dsim->master_dsi_link_id = id;
		if (!msm_dsim->is_sync_needed)
			msm_dsim->is_sync_needed = of_property_read_bool(
					np, "qcom,sync-dual-dsi");
	}

	return 0;
}

static int dsi_mgr_host_register(int id)
{
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct msm_dsi *other_dsi = dsi_mgr_get_other_dsi(id);
	struct msm_dsi *clk_master_dsi = dsi_mgr_get_dsi(DSI_CLOCK_MASTER);
	struct msm_dsi_pll *src_pll;
	int ret;

	if (!IS_DUAL_DSI()) {
		ret = msm_dsi_host_register(msm_dsi->host, true);
		if (ret)
			return ret;

		src_pll = msm_dsi_phy_get_pll(msm_dsi->phy);
		ret = msm_dsi_host_set_src_pll(msm_dsi->host, src_pll);
	} else if (!other_dsi) {
		ret = 0;
	} else {
		struct msm_dsi *mdsi = IS_MASTER_DSI_LINK(id) ?
					msm_dsi : other_dsi;
		struct msm_dsi *sdsi = IS_MASTER_DSI_LINK(id) ?
					other_dsi : msm_dsi;
		/* Register slave host first, so that slave DSI device
		 * has a chance to probe, and do not block the master
		 * DSI device's probe.
		 * Also, do not check defer for the slave host,
		 * because only master DSI device adds the panel to global
		 * panel list. The panel's device is the master DSI device.
		 */
		ret = msm_dsi_host_register(sdsi->host, false);
		if (ret)
			return ret;
		ret = msm_dsi_host_register(mdsi->host, true);
		if (ret)
			return ret;

		/* PLL0 is to drive both 2 DSI link clocks in Dual DSI mode. */
		src_pll = msm_dsi_phy_get_pll(clk_master_dsi->phy);
		ret = msm_dsi_host_set_src_pll(msm_dsi->host, src_pll);
		if (ret)
			return ret;
		ret = msm_dsi_host_set_src_pll(other_dsi->host, src_pll);
	}

	return ret;
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

static enum drm_connector_status dsi_mgr_connector_detect(
		struct drm_connector *connector, bool force)
{
	int id = dsi_mgr_connector_get_id(connector);
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct msm_dsi *other_dsi = dsi_mgr_get_other_dsi(id);
	struct msm_drm_private *priv = connector->dev->dev_private;
	struct msm_kms *kms = priv->kms;

	DBG("id=%d", id);
	if (!msm_dsi->panel) {
		msm_dsi->panel = msm_dsi_host_get_panel(msm_dsi->host,
						&msm_dsi->device_flags);

		/* There is only 1 panel in the global panel list
		 * for dual DSI mode. Therefore slave dsi should get
		 * the drm_panel instance from master dsi, and
		 * keep using the panel flags got from the current DSI link.
		 */
		if (!msm_dsi->panel && IS_DUAL_DSI() &&
			!IS_MASTER_DSI_LINK(id) && other_dsi)
			msm_dsi->panel = msm_dsi_host_get_panel(
					other_dsi->host, NULL);

		if (msm_dsi->panel && IS_DUAL_DSI())
			drm_object_attach_property(&connector->base,
				connector->dev->mode_config.tile_property, 0);

		/* Set split display info to kms once dual DSI panel is
		 * connected to both hosts.
		 */
		if (msm_dsi->panel && IS_DUAL_DSI() &&
			other_dsi && other_dsi->panel) {
			bool cmd_mode = !(msm_dsi->device_flags &
						MIPI_DSI_MODE_VIDEO);
			struct drm_encoder *encoder = msm_dsi_get_encoder(
					dsi_mgr_get_dsi(DSI_ENCODER_MASTER));
			struct drm_encoder *slave_enc = msm_dsi_get_encoder(
					dsi_mgr_get_dsi(DSI_ENCODER_SLAVE));

			if (kms->funcs->set_split_display)
				kms->funcs->set_split_display(kms, encoder,
							slave_enc, cmd_mode);
			else
				pr_err("mdp does not support dual DSI\n");
		}
	}

	return msm_dsi->panel ? connector_status_connected :
		connector_status_disconnected;
}

static void dsi_mgr_connector_destroy(struct drm_connector *connector)
{
	DBG("");
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static void dsi_dual_connector_fix_modes(struct drm_connector *connector)
{
	struct drm_display_mode *mode, *m;

	/* Only support left-right mode */
	list_for_each_entry_safe(mode, m, &connector->probed_modes, head) {
		mode->clock >>= 1;
		mode->hdisplay >>= 1;
		mode->hsync_start >>= 1;
		mode->hsync_end >>= 1;
		mode->htotal >>= 1;
		drm_mode_set_name(mode);
	}
}

static int dsi_dual_connector_tile_init(
			struct drm_connector *connector, int id)
{
	struct drm_display_mode *mode;
	/* Fake topology id */
	char topo_id[8] = {'M', 'S', 'M', 'D', 'U', 'D', 'S', 'I'};

	if (connector->tile_group) {
		DBG("Tile property has been initialized");
		return 0;
	}

	/* Use the first mode only for now */
	mode = list_first_entry(&connector->probed_modes,
				struct drm_display_mode,
				head);
	if (!mode)
		return -EINVAL;

	connector->tile_group = drm_mode_get_tile_group(
					connector->dev, topo_id);
	if (!connector->tile_group)
		connector->tile_group = drm_mode_create_tile_group(
					connector->dev, topo_id);
	if (!connector->tile_group) {
		pr_err("%s: failed to create tile group\n", __func__);
		return -ENOMEM;
	}

	connector->has_tile = true;
	connector->tile_is_single_monitor = true;

	/* mode has been fixed */
	connector->tile_h_size = mode->hdisplay;
	connector->tile_v_size = mode->vdisplay;

	/* Only support left-right mode */
	connector->num_h_tile = 2;
	connector->num_v_tile = 1;

	connector->tile_v_loc = 0;
	connector->tile_h_loc = (id == DSI_RIGHT) ? 1 : 0;

	return 0;
}

static int dsi_mgr_connector_get_modes(struct drm_connector *connector)
{
	int id = dsi_mgr_connector_get_id(connector);
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct drm_panel *panel = msm_dsi->panel;
	int ret, num;

	if (!panel)
		return 0;

	/* Since we have 2 connectors, but only 1 drm_panel in dual DSI mode,
	 * panel should not attach to any connector.
	 * Only temporarily attach panel to the current connector here,
	 * to let panel set mode to this connector.
	 */
	drm_panel_attach(panel, connector);
	num = drm_panel_get_modes(panel);
	drm_panel_detach(panel);
	if (!num)
		return 0;

	if (IS_DUAL_DSI()) {
		/* report half resolution to user */
		dsi_dual_connector_fix_modes(connector);
		ret = dsi_dual_connector_tile_init(connector, id);
		if (ret)
			return ret;
		ret = drm_mode_connector_set_tile_property(connector);
		if (ret) {
			pr_err("%s: set tile property failed, %d\n",
					__func__, ret);
			return ret;
		}
	}

	return num;
}

static int dsi_mgr_connector_mode_valid(struct drm_connector *connector,
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
	bool is_dual_dsi = IS_DUAL_DSI();
	int ret;

	DBG("id=%d", id);
	if (!msm_dsi_device_connected(msm_dsi) ||
			(is_dual_dsi && (DSI_1 == id)))
		return;

	ret = msm_dsi_host_power_on(host);
	if (ret) {
		pr_err("%s: power on host %d failed, %d\n", __func__, id, ret);
		goto host_on_fail;
	}

	if (is_dual_dsi && msm_dsi1) {
		ret = msm_dsi_host_power_on(msm_dsi1->host);
		if (ret) {
			pr_err("%s: power on host1 failed, %d\n",
							__func__, ret);
			goto host1_on_fail;
		}
	}

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

	if (is_dual_dsi && msm_dsi1) {
		ret = msm_dsi_host_enable(msm_dsi1->host);
		if (ret) {
			pr_err("%s: enable host1 failed, %d\n", __func__, ret);
			goto host1_en_fail;
		}
	}

	if (panel) {
		ret = drm_panel_enable(panel);
		if (ret) {
			pr_err("%s: enable panel %d failed, %d\n", __func__, id,
									ret);
			goto panel_en_fail;
		}
	}

	return;

panel_en_fail:
	if (is_dual_dsi && msm_dsi1)
		msm_dsi_host_disable(msm_dsi1->host);
host1_en_fail:
	msm_dsi_host_disable(host);
host_en_fail:
	if (panel)
		drm_panel_unprepare(panel);
panel_prep_fail:
	if (is_dual_dsi && msm_dsi1)
		msm_dsi_host_power_off(msm_dsi1->host);
host1_on_fail:
	msm_dsi_host_power_off(host);
host_on_fail:
	return;
}

static void dsi_mgr_bridge_enable(struct drm_bridge *bridge)
{
	DBG("");
}

static void dsi_mgr_bridge_disable(struct drm_bridge *bridge)
{
	DBG("");
}

static void dsi_mgr_bridge_post_disable(struct drm_bridge *bridge)
{
	int id = dsi_mgr_bridge_get_id(bridge);
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct msm_dsi *msm_dsi1 = dsi_mgr_get_dsi(DSI_1);
	struct mipi_dsi_host *host = msm_dsi->host;
	struct drm_panel *panel = msm_dsi->panel;
	bool is_dual_dsi = IS_DUAL_DSI();
	int ret;

	DBG("id=%d", id);

	if (!msm_dsi_device_connected(msm_dsi) ||
			(is_dual_dsi && (DSI_1 == id)))
		return;

	if (panel) {
		ret = drm_panel_disable(panel);
		if (ret)
			pr_err("%s: Panel %d OFF failed, %d\n", __func__, id,
									ret);
	}

	ret = msm_dsi_host_disable(host);
	if (ret)
		pr_err("%s: host %d disable failed, %d\n", __func__, id, ret);

	if (is_dual_dsi && msm_dsi1) {
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

	ret = msm_dsi_host_power_off(host);
	if (ret)
		pr_err("%s: host %d power off failed,%d\n", __func__, id, ret);

	if (is_dual_dsi && msm_dsi1) {
		ret = msm_dsi_host_power_off(msm_dsi1->host);
		if (ret)
			pr_err("%s: host1 power off failed, %d\n",
								__func__, ret);
	}
}

static void dsi_mgr_bridge_mode_set(struct drm_bridge *bridge,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	int id = dsi_mgr_bridge_get_id(bridge);
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct msm_dsi *other_dsi = dsi_mgr_get_other_dsi(id);
	struct mipi_dsi_host *host = msm_dsi->host;
	bool is_dual_dsi = IS_DUAL_DSI();

	DBG("set mode: %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x",
			mode->base.id, mode->name,
			mode->vrefresh, mode->clock,
			mode->hdisplay, mode->hsync_start,
			mode->hsync_end, mode->htotal,
			mode->vdisplay, mode->vsync_start,
			mode->vsync_end, mode->vtotal,
			mode->type, mode->flags);

	if (is_dual_dsi && (DSI_1 == id))
		return;

	msm_dsi_host_set_display_mode(host, adjusted_mode);
	if (is_dual_dsi && other_dsi)
		msm_dsi_host_set_display_mode(other_dsi->host, adjusted_mode);
}

static const struct drm_connector_funcs dsi_mgr_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
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
	int ret, i;

	dsi_connector = devm_kzalloc(msm_dsi->dev->dev,
				sizeof(*dsi_connector), GFP_KERNEL);
	if (!dsi_connector) {
		ret = -ENOMEM;
		goto fail;
	}

	dsi_connector->id = id;

	connector = &dsi_connector->base;

	ret = drm_connector_init(msm_dsi->dev, connector,
			&dsi_mgr_connector_funcs, DRM_MODE_CONNECTOR_DSI);
	if (ret)
		goto fail;

	drm_connector_helper_add(connector, &dsi_mgr_conn_helper_funcs);

	/* Enable HPD to let hpd event is handled
	 * when panel is attached to the host.
	 */
	connector->polled = DRM_CONNECTOR_POLL_HPD;

	/* Display driver doesn't support interlace now. */
	connector->interlace_allowed = 0;
	connector->doublescan_allowed = 0;

	ret = drm_connector_register(connector);
	if (ret)
		goto fail;

	for (i = 0; i < MSM_DSI_ENCODER_NUM; i++)
		drm_mode_connector_attach_encoder(connector,
						msm_dsi->encoders[i]);

	return connector;

fail:
	if (connector)
		dsi_mgr_connector_destroy(connector);

	return ERR_PTR(ret);
}

/* initialize bridge */
struct drm_bridge *msm_dsi_manager_bridge_init(u8 id)
{
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct drm_bridge *bridge = NULL;
	struct dsi_bridge *dsi_bridge;
	int ret;

	dsi_bridge = devm_kzalloc(msm_dsi->dev->dev,
				sizeof(*dsi_bridge), GFP_KERNEL);
	if (!dsi_bridge) {
		ret = -ENOMEM;
		goto fail;
	}

	dsi_bridge->id = id;

	bridge = &dsi_bridge->base;
	bridge->funcs = &dsi_mgr_bridge_funcs;

	ret = drm_bridge_attach(msm_dsi->dev, bridge);
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
	struct drm_encoder *encoder;
	struct drm_bridge *int_bridge, *ext_bridge;
	struct drm_connector *connector;
	struct list_head *connector_list;

	int_bridge = msm_dsi->bridge;
	ext_bridge = msm_dsi->external_bridge =
			msm_dsi_host_get_bridge(msm_dsi->host);

	/*
	 * HACK: we may not know the external DSI bridge device's mode
	 * flags here. We'll get to know them only when the device
	 * attaches to the dsi host. For now, assume the bridge supports
	 * DSI video mode
	 */
	encoder = msm_dsi->encoders[MSM_DSI_VIDEO_ENCODER_ID];

	/* link the internal dsi bridge to the external bridge */
	int_bridge->next = ext_bridge;
	/* set the external bridge's encoder as dsi's encoder */
	ext_bridge->encoder = encoder;

	drm_bridge_attach(dev, ext_bridge);

	/*
	 * we need the drm_connector created by the external bridge
	 * driver (or someone else) to feed it to our driver's
	 * priv->connector[] list, mainly for msm_fbdev_init()
	 */
	connector_list = &dev->mode_config.connector_list;

	list_for_each_entry(connector, connector_list, head) {
		int i;

		for (i = 0; i < DRM_CONNECTOR_MAX_ENCODER; i++) {
			if (connector->encoder_ids[i] == encoder->base.id)
				return connector;
		}
	}

	return ERR_PTR(-ENODEV);
}

void msm_dsi_manager_bridge_destroy(struct drm_bridge *bridge)
{
}

int msm_dsi_manager_phy_enable(int id,
		const unsigned long bit_rate, const unsigned long esc_rate,
		u32 *clk_pre, u32 *clk_post)
{
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct msm_dsi_phy *phy = msm_dsi->phy;
	int src_pll_id = IS_DUAL_DSI() ? DSI_CLOCK_MASTER : id;
	struct msm_dsi_pll *pll = msm_dsi_phy_get_pll(msm_dsi->phy);
	int ret;

	ret = msm_dsi_phy_enable(phy, src_pll_id, bit_rate, esc_rate);
	if (ret)
		return ret;

	/*
	 * Reset DSI PHY silently changes its PLL registers to reset status,
	 * which will confuse clock driver and result in wrong output rate of
	 * link clocks. Restore PLL status if its PLL is being used as clock
	 * source.
	 */
	if (!IS_DUAL_DSI() || (id == DSI_CLOCK_MASTER)) {
		ret = msm_dsi_pll_restore_state(pll);
		if (ret) {
			pr_err("%s: failed to restore pll state\n", __func__);
			msm_dsi_phy_disable(phy);
			return ret;
		}
	}

	msm_dsi->phy_enabled = true;
	msm_dsi_phy_get_clk_pre_post(phy, clk_pre, clk_post);

	return 0;
}

void msm_dsi_manager_phy_disable(int id)
{
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct msm_dsi *mdsi = dsi_mgr_get_dsi(DSI_CLOCK_MASTER);
	struct msm_dsi *sdsi = dsi_mgr_get_dsi(DSI_CLOCK_SLAVE);
	struct msm_dsi_phy *phy = msm_dsi->phy;
	struct msm_dsi_pll *pll = msm_dsi_phy_get_pll(msm_dsi->phy);

	/* Save PLL status if it is a clock source */
	if (!IS_DUAL_DSI() || (id == DSI_CLOCK_MASTER))
		msm_dsi_pll_save_state(pll);

	/* disable DSI phy
	 * In dual-dsi configuration, the phy should be disabled for the
	 * first controller only when the second controller is disabled.
	 */
	msm_dsi->phy_enabled = false;
	if (IS_DUAL_DSI() && mdsi && sdsi) {
		if (!mdsi->phy_enabled && !sdsi->phy_enabled) {
			msm_dsi_phy_disable(sdsi->phy);
			msm_dsi_phy_disable(mdsi->phy);
		}
	} else {
		msm_dsi_phy_disable(phy);
	}
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

	/* In dual master case, panel requires the same commands sent to
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

bool msm_dsi_manager_cmd_xfer_trigger(int id, u32 iova, u32 len)
{
	struct msm_dsi *msm_dsi = dsi_mgr_get_dsi(id);
	struct msm_dsi *msm_dsi0 = dsi_mgr_get_dsi(DSI_0);
	struct mipi_dsi_host *host = msm_dsi->host;

	if (IS_SYNC_NEEDED() && (id == DSI_0))
		return false;

	if (IS_SYNC_NEEDED() && msm_dsi0)
		msm_dsi_host_cmd_xfer_commit(msm_dsi0->host, iova, len);

	msm_dsi_host_cmd_xfer_commit(host, iova, len);

	return true;
}

int msm_dsi_manager_register(struct msm_dsi *msm_dsi)
{
	struct msm_dsi_manager *msm_dsim = &msm_dsim_glb;
	int id = msm_dsi->id;
	int ret;

	if (id > DSI_MAX) {
		pr_err("%s: invalid id %d\n", __func__, id);
		return -EINVAL;
	}

	if (msm_dsim->dsi[id]) {
		pr_err("%s: dsi%d already registered\n", __func__, id);
		return -EBUSY;
	}

	msm_dsim->dsi[id] = msm_dsi;

	ret = dsi_mgr_parse_dual_dsi(msm_dsi->pdev->dev.of_node, id);
	if (ret) {
		pr_err("%s: failed to parse dual DSI info\n", __func__);
		goto fail;
	}

	ret = dsi_mgr_host_register(id);
	if (ret) {
		pr_err("%s: failed to register mipi dsi host for DSI %d\n",
			__func__, id);
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
	msm_dsim->dsi[msm_dsi->id] = NULL;
}

