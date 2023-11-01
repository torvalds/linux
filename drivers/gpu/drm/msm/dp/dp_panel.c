// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#include "dp_panel.h"

#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_print.h>

struct dp_panel_private {
	struct device *dev;
	struct drm_device *drm_dev;
	struct dp_panel dp_panel;
	struct drm_dp_aux *aux;
	struct dp_link *link;
	struct dp_catalog *catalog;
	bool panel_on;
};

static void dp_panel_read_psr_cap(struct dp_panel_private *panel)
{
	ssize_t rlen;
	struct dp_panel *dp_panel;

	dp_panel = &panel->dp_panel;

	/* edp sink */
	if (dp_panel->dpcd[DP_EDP_CONFIGURATION_CAP]) {
		rlen = drm_dp_dpcd_read(panel->aux, DP_PSR_SUPPORT,
				&dp_panel->psr_cap, sizeof(dp_panel->psr_cap));
		if (rlen == sizeof(dp_panel->psr_cap)) {
			drm_dbg_dp(panel->drm_dev,
				"psr version: 0x%x, psr_cap: 0x%x\n",
				dp_panel->psr_cap.version,
				dp_panel->psr_cap.capabilities);
		} else
			DRM_ERROR("failed to read psr info, rlen=%zd\n", rlen);
	}
}

static int dp_panel_read_dpcd(struct dp_panel *dp_panel)
{
	int rc;
	struct dp_panel_private *panel;
	struct dp_link_info *link_info;
	u8 *dpcd, major, minor;

	panel = container_of(dp_panel, struct dp_panel_private, dp_panel);
	dpcd = dp_panel->dpcd;
	rc = drm_dp_read_dpcd_caps(panel->aux, dpcd);
	if (rc)
		return rc;

	link_info = &dp_panel->link_info;
	link_info->revision = dpcd[DP_DPCD_REV];
	major = (link_info->revision >> 4) & 0x0f;
	minor = link_info->revision & 0x0f;

	link_info->rate = drm_dp_max_link_rate(dpcd);
	link_info->num_lanes = drm_dp_max_lane_count(dpcd);

	/* Limit data lanes from data-lanes of endpoint property of dtsi */
	if (link_info->num_lanes > dp_panel->max_dp_lanes)
		link_info->num_lanes = dp_panel->max_dp_lanes;

	/* Limit link rate from link-frequencies of endpoint property of dtsi */
	if (link_info->rate > dp_panel->max_dp_link_rate)
		link_info->rate = dp_panel->max_dp_link_rate;

	drm_dbg_dp(panel->drm_dev, "version: %d.%d\n", major, minor);
	drm_dbg_dp(panel->drm_dev, "link_rate=%d\n", link_info->rate);
	drm_dbg_dp(panel->drm_dev, "lane_count=%d\n", link_info->num_lanes);

	if (drm_dp_enhanced_frame_cap(dpcd))
		link_info->capabilities |= DP_LINK_CAP_ENHANCED_FRAMING;

	dp_panel_read_psr_cap(panel);

	return rc;
}

static u32 dp_panel_get_supported_bpp(struct dp_panel *dp_panel,
		u32 mode_edid_bpp, u32 mode_pclk_khz)
{
	struct dp_link_info *link_info;
	const u32 max_supported_bpp = 30, min_supported_bpp = 18;
	u32 bpp = 0, data_rate_khz = 0;

	bpp = min_t(u32, mode_edid_bpp, max_supported_bpp);

	link_info = &dp_panel->link_info;
	data_rate_khz = link_info->num_lanes * link_info->rate * 8;

	while (bpp > min_supported_bpp) {
		if (mode_pclk_khz * bpp <= data_rate_khz)
			break;
		bpp -= 6;
	}

	return bpp;
}

static int dp_panel_update_modes(struct drm_connector *connector,
	struct edid *edid)
{
	int rc = 0;

	if (edid) {
		rc = drm_connector_update_edid_property(connector, edid);
		if (rc) {
			DRM_ERROR("failed to update edid property %d\n", rc);
			return rc;
		}
		rc = drm_add_edid_modes(connector, edid);
		return rc;
	}

	rc = drm_connector_update_edid_property(connector, NULL);
	if (rc)
		DRM_ERROR("failed to update edid property %d\n", rc);

	return rc;
}

int dp_panel_read_sink_caps(struct dp_panel *dp_panel,
	struct drm_connector *connector)
{
	int rc, bw_code;
	int count;
	struct dp_panel_private *panel;

	if (!dp_panel || !connector) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	panel = container_of(dp_panel, struct dp_panel_private, dp_panel);

	rc = dp_panel_read_dpcd(dp_panel);
	if (rc) {
		DRM_ERROR("read dpcd failed %d\n", rc);
		return rc;
	}

	bw_code = drm_dp_link_rate_to_bw_code(dp_panel->link_info.rate);
	if (!is_link_rate_valid(bw_code) ||
			!is_lane_count_valid(dp_panel->link_info.num_lanes) ||
			(bw_code > dp_panel->max_bw_code)) {
		DRM_ERROR("Illegal link rate=%d lane=%d\n", dp_panel->link_info.rate,
				dp_panel->link_info.num_lanes);
		return -EINVAL;
	}

	if (drm_dp_is_branch(dp_panel->dpcd)) {
		count = drm_dp_read_sink_count(panel->aux);
		if (!count) {
			panel->link->sink_count = 0;
			return -ENOTCONN;
		}
	}

	rc = drm_dp_read_downstream_info(panel->aux, dp_panel->dpcd,
					 dp_panel->downstream_ports);
	if (rc)
		return rc;

	kfree(dp_panel->edid);
	dp_panel->edid = NULL;

	dp_panel->edid = drm_get_edid(connector,
					      &panel->aux->ddc);
	if (!dp_panel->edid) {
		DRM_ERROR("panel edid read failed\n");
		/* check edid read fail is due to unplug */
		if (!dp_catalog_link_is_connected(panel->catalog)) {
			rc = -ETIMEDOUT;
			goto end;
		}
	}

end:
	return rc;
}

u32 dp_panel_get_mode_bpp(struct dp_panel *dp_panel,
		u32 mode_edid_bpp, u32 mode_pclk_khz)
{
	struct dp_panel_private *panel;
	u32 bpp;

	if (!dp_panel || !mode_edid_bpp || !mode_pclk_khz) {
		DRM_ERROR("invalid input\n");
		return 0;
	}

	panel = container_of(dp_panel, struct dp_panel_private, dp_panel);

	if (dp_panel->video_test)
		bpp = dp_link_bit_depth_to_bpp(
				panel->link->test_video.test_bit_depth);
	else
		bpp = dp_panel_get_supported_bpp(dp_panel, mode_edid_bpp,
				mode_pclk_khz);

	return bpp;
}

int dp_panel_get_modes(struct dp_panel *dp_panel,
	struct drm_connector *connector)
{
	if (!dp_panel) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	if (dp_panel->edid)
		return dp_panel_update_modes(connector, dp_panel->edid);

	return 0;
}

static u8 dp_panel_get_edid_checksum(struct edid *edid)
{
	edid += edid->extensions;

	return edid->checksum;
}

void dp_panel_handle_sink_request(struct dp_panel *dp_panel)
{
	struct dp_panel_private *panel;

	if (!dp_panel) {
		DRM_ERROR("invalid input\n");
		return;
	}

	panel = container_of(dp_panel, struct dp_panel_private, dp_panel);

	if (panel->link->sink_request & DP_TEST_LINK_EDID_READ) {
		u8 checksum;

		if (dp_panel->edid)
			checksum = dp_panel_get_edid_checksum(dp_panel->edid);
		else
			checksum = dp_panel->connector->real_edid_checksum;

		dp_link_send_edid_checksum(panel->link, checksum);
		dp_link_send_test_response(panel->link);
	}
}

void dp_panel_tpg_config(struct dp_panel *dp_panel, bool enable)
{
	struct dp_catalog *catalog;
	struct dp_panel_private *panel;

	if (!dp_panel) {
		DRM_ERROR("invalid input\n");
		return;
	}

	panel = container_of(dp_panel, struct dp_panel_private, dp_panel);
	catalog = panel->catalog;

	if (!panel->panel_on) {
		drm_dbg_dp(panel->drm_dev,
				"DP panel not enabled, handle TPG on next on\n");
		return;
	}

	if (!enable) {
		dp_catalog_panel_tpg_disable(catalog);
		return;
	}

	drm_dbg_dp(panel->drm_dev, "calling catalog tpg_enable\n");
	dp_catalog_panel_tpg_enable(catalog, &panel->dp_panel.dp_mode.drm_mode);
}

void dp_panel_dump_regs(struct dp_panel *dp_panel)
{
	struct dp_catalog *catalog;
	struct dp_panel_private *panel;

	panel = container_of(dp_panel, struct dp_panel_private, dp_panel);
	catalog = panel->catalog;

	dp_catalog_dump_regs(catalog);
}

int dp_panel_timing_cfg(struct dp_panel *dp_panel)
{
	u32 data, total_ver, total_hor;
	struct dp_catalog *catalog;
	struct dp_panel_private *panel;
	struct drm_display_mode *drm_mode;

	panel = container_of(dp_panel, struct dp_panel_private, dp_panel);
	catalog = panel->catalog;
	drm_mode = &panel->dp_panel.dp_mode.drm_mode;

	drm_dbg_dp(panel->drm_dev, "width=%d hporch= %d %d %d\n",
		drm_mode->hdisplay, drm_mode->htotal - drm_mode->hsync_end,
		drm_mode->hsync_start - drm_mode->hdisplay,
		drm_mode->hsync_end - drm_mode->hsync_start);

	drm_dbg_dp(panel->drm_dev, "height=%d vporch= %d %d %d\n",
		drm_mode->vdisplay, drm_mode->vtotal - drm_mode->vsync_end,
		drm_mode->vsync_start - drm_mode->vdisplay,
		drm_mode->vsync_end - drm_mode->vsync_start);

	total_hor = drm_mode->htotal;

	total_ver = drm_mode->vtotal;

	data = total_ver;
	data <<= 16;
	data |= total_hor;

	catalog->total = data;

	data = (drm_mode->vtotal - drm_mode->vsync_start);
	data <<= 16;
	data |= (drm_mode->htotal - drm_mode->hsync_start);

	catalog->sync_start = data;

	data = drm_mode->vsync_end - drm_mode->vsync_start;
	data <<= 16;
	data |= (panel->dp_panel.dp_mode.v_active_low << 31);
	data |= drm_mode->hsync_end - drm_mode->hsync_start;
	data |= (panel->dp_panel.dp_mode.h_active_low << 15);

	catalog->width_blanking = data;

	data = drm_mode->vdisplay;
	data <<= 16;
	data |= drm_mode->hdisplay;

	catalog->dp_active = data;

	dp_catalog_panel_timing_cfg(catalog);
	panel->panel_on = true;

	return 0;
}

int dp_panel_init_panel_info(struct dp_panel *dp_panel)
{
	struct drm_display_mode *drm_mode;
	struct dp_panel_private *panel;

	drm_mode = &dp_panel->dp_mode.drm_mode;

	panel = container_of(dp_panel, struct dp_panel_private, dp_panel);

	/*
	 * print resolution info as this is a result
	 * of user initiated action of cable connection
	 */
	drm_dbg_dp(panel->drm_dev, "SET NEW RESOLUTION:\n");
	drm_dbg_dp(panel->drm_dev, "%dx%d@%dfps\n",
		drm_mode->hdisplay, drm_mode->vdisplay, drm_mode_vrefresh(drm_mode));
	drm_dbg_dp(panel->drm_dev,
			"h_porches(back|front|width) = (%d|%d|%d)\n",
			drm_mode->htotal - drm_mode->hsync_end,
			drm_mode->hsync_start - drm_mode->hdisplay,
			drm_mode->hsync_end - drm_mode->hsync_start);
	drm_dbg_dp(panel->drm_dev,
			"v_porches(back|front|width) = (%d|%d|%d)\n",
			drm_mode->vtotal - drm_mode->vsync_end,
			drm_mode->vsync_start - drm_mode->vdisplay,
			drm_mode->vsync_end - drm_mode->vsync_start);
	drm_dbg_dp(panel->drm_dev, "pixel clock (KHz)=(%d)\n",
				drm_mode->clock);
	drm_dbg_dp(panel->drm_dev, "bpp = %d\n", dp_panel->dp_mode.bpp);

	dp_panel->dp_mode.bpp = max_t(u32, 18,
				min_t(u32, dp_panel->dp_mode.bpp, 30));
	drm_dbg_dp(panel->drm_dev, "updated bpp = %d\n",
				dp_panel->dp_mode.bpp);

	return 0;
}

struct dp_panel *dp_panel_get(struct dp_panel_in *in)
{
	struct dp_panel_private *panel;
	struct dp_panel *dp_panel;

	if (!in->dev || !in->catalog || !in->aux || !in->link) {
		DRM_ERROR("invalid input\n");
		return ERR_PTR(-EINVAL);
	}

	panel = devm_kzalloc(in->dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return ERR_PTR(-ENOMEM);

	panel->dev = in->dev;
	panel->aux = in->aux;
	panel->catalog = in->catalog;
	panel->link = in->link;

	dp_panel = &panel->dp_panel;
	dp_panel->max_bw_code = DP_LINK_BW_8_1;

	return dp_panel;
}

void dp_panel_put(struct dp_panel *dp_panel)
{
	if (!dp_panel)
		return;

	kfree(dp_panel->edid);
}
