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
	struct dp_panel dp_panel;
	struct drm_dp_aux *aux;
	struct dp_link *link;
	struct dp_catalog *catalog;
	bool panel_on;
	bool aux_cfg_update_done;
};

static int dp_panel_read_dpcd(struct dp_panel *dp_panel)
{
	int rc = 0;
	size_t len;
	ssize_t rlen;
	struct dp_panel_private *panel;
	struct dp_link_info *link_info;
	u8 *dpcd, major = 0, minor = 0, temp;
	u32 offset = DP_DPCD_REV;

	dpcd = dp_panel->dpcd;

	panel = container_of(dp_panel, struct dp_panel_private, dp_panel);
	link_info = &dp_panel->link_info;

	rlen = drm_dp_dpcd_read(panel->aux, offset,
			dpcd, (DP_RECEIVER_CAP_SIZE + 1));
	if (rlen < (DP_RECEIVER_CAP_SIZE + 1)) {
		DRM_ERROR("dpcd read failed, rlen=%zd\n", rlen);
		if (rlen == -ETIMEDOUT)
			rc = rlen;
		else
			rc = -EINVAL;

		goto end;
	}

	temp = dpcd[DP_TRAINING_AUX_RD_INTERVAL];

	/* check for EXTENDED_RECEIVER_CAPABILITY_FIELD_PRESENT */
	if (temp & BIT(7)) {
		DRM_DEBUG_DP("using EXTENDED_RECEIVER_CAPABILITY_FIELD\n");
		offset = DPRX_EXTENDED_DPCD_FIELD;
	}

	rlen = drm_dp_dpcd_read(panel->aux, offset,
		dpcd, (DP_RECEIVER_CAP_SIZE + 1));
	if (rlen < (DP_RECEIVER_CAP_SIZE + 1)) {
		DRM_ERROR("dpcd read failed, rlen=%zd\n", rlen);
		if (rlen == -ETIMEDOUT)
			rc = rlen;
		else
			rc = -EINVAL;

		goto end;
	}

	link_info->revision = dpcd[DP_DPCD_REV];
	major = (link_info->revision >> 4) & 0x0f;
	minor = link_info->revision & 0x0f;

	link_info->rate = drm_dp_bw_code_to_link_rate(dpcd[DP_MAX_LINK_RATE]);
	link_info->num_lanes = dpcd[DP_MAX_LANE_COUNT] & DP_MAX_LANE_COUNT_MASK;

	if (link_info->num_lanes > dp_panel->max_dp_lanes)
		link_info->num_lanes = dp_panel->max_dp_lanes;

	/* Limit support upto HBR2 until HBR3 support is added */
	if (link_info->rate >= (drm_dp_bw_code_to_link_rate(DP_LINK_BW_5_4)))
		link_info->rate = drm_dp_bw_code_to_link_rate(DP_LINK_BW_5_4);

	DRM_DEBUG_DP("version: %d.%d\n", major, minor);
	DRM_DEBUG_DP("link_rate=%d\n", link_info->rate);
	DRM_DEBUG_DP("lane_count=%d\n", link_info->num_lanes);

	if (drm_dp_enhanced_frame_cap(dpcd))
		link_info->capabilities |= DP_LINK_CAP_ENHANCED_FRAMING;

	dp_panel->dfp_present = dpcd[DP_DOWNSTREAMPORT_PRESENT];
	dp_panel->dfp_present &= DP_DWN_STRM_PORT_PRESENT;

	if (dp_panel->dfp_present && (dpcd[DP_DPCD_REV] > 0x10)) {
		dp_panel->ds_port_cnt = dpcd[DP_DOWN_STREAM_PORT_COUNT];
		dp_panel->ds_port_cnt &= DP_PORT_COUNT_MASK;
		len = DP_DOWNSTREAM_PORTS * DP_DOWNSTREAM_CAP_SIZE;

		rlen = drm_dp_dpcd_read(panel->aux,
			DP_DOWNSTREAM_PORT_0, dp_panel->ds_cap_info, len);
		if (rlen < len) {
			DRM_ERROR("ds port status failed, rlen=%zd\n", rlen);
			rc = -EINVAL;
			goto end;
		}
	}

end:
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
	int rc = 0, bw_code;
	int rlen, count;
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

	if (dp_panel->dfp_present) {
		rlen = drm_dp_dpcd_read(panel->aux, DP_SINK_COUNT,
				&count, 1);
		if (rlen == 1) {
			count = DP_GET_SINK_COUNT(count);
			if (!count) {
				DRM_ERROR("no downstream ports connected\n");
				panel->link->sink_count = 0;
				rc = -ENOTCONN;
				goto end;
			}
		}
	}

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

		/* fail safe edid */
		mutex_lock(&connector->dev->mode_config.mutex);
		if (drm_add_modes_noedid(connector, 640, 480))
			drm_set_preferred_mode(connector, 640, 480);
		mutex_unlock(&connector->dev->mode_config.mutex);
	}

	if (panel->aux_cfg_update_done) {
		DRM_DEBUG_DP("read DPCD with updated AUX config\n");
		rc = dp_panel_read_dpcd(dp_panel);
		bw_code = drm_dp_link_rate_to_bw_code(dp_panel->link_info.rate);
		if (rc || !is_link_rate_valid(bw_code) ||
			!is_lane_count_valid(dp_panel->link_info.num_lanes)
			|| (bw_code > dp_panel->max_bw_code)) {
			DRM_ERROR("read dpcd failed %d\n", rc);
			return rc;
		}
		panel->aux_cfg_update_done = false;
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
	struct drm_connector *connector, struct dp_display_mode *mode)
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
	struct edid *last_block;
	u8 *raw_edid;
	bool is_edid_corrupt = false;

	if (!edid) {
		DRM_ERROR("invalid edid input\n");
		return 0;
	}

	raw_edid = (u8 *)edid;
	raw_edid += (edid->extensions * EDID_LENGTH);
	last_block = (struct edid *)raw_edid;

	/* block type extension */
	drm_edid_block_valid(raw_edid, 1, false, &is_edid_corrupt);
	if (!is_edid_corrupt)
		return last_block->checksum;

	DRM_ERROR("Invalid block, no checksum\n");
	return 0;
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
		DRM_DEBUG_DP("DP panel not enabled, handle TPG on next on\n");
		return;
	}

	if (!enable) {
		dp_catalog_panel_tpg_disable(catalog);
		return;
	}

	DRM_DEBUG_DP("%s: calling catalog tpg_enable\n", __func__);
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

	DRM_DEBUG_DP("width=%d hporch= %d %d %d\n",
		drm_mode->hdisplay, drm_mode->htotal - drm_mode->hsync_end,
		drm_mode->hsync_start - drm_mode->hdisplay,
		drm_mode->hsync_end - drm_mode->hsync_start);

	DRM_DEBUG_DP("height=%d vporch= %d %d %d\n",
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

	drm_mode = &dp_panel->dp_mode.drm_mode;

	/*
	 * print resolution info as this is a result
	 * of user initiated action of cable connection
	 */
	DRM_DEBUG_DP("SET NEW RESOLUTION:\n");
	DRM_DEBUG_DP("%dx%d@%dfps\n", drm_mode->hdisplay,
		drm_mode->vdisplay, drm_mode_vrefresh(drm_mode));
	DRM_DEBUG_DP("h_porches(back|front|width) = (%d|%d|%d)\n",
			drm_mode->htotal - drm_mode->hsync_end,
			drm_mode->hsync_start - drm_mode->hdisplay,
			drm_mode->hsync_end - drm_mode->hsync_start);
	DRM_DEBUG_DP("v_porches(back|front|width) = (%d|%d|%d)\n",
			drm_mode->vtotal - drm_mode->vsync_end,
			drm_mode->vsync_start - drm_mode->vdisplay,
			drm_mode->vsync_end - drm_mode->vsync_start);
	DRM_DEBUG_DP("pixel clock (KHz)=(%d)\n", drm_mode->clock);
	DRM_DEBUG_DP("bpp = %d\n", dp_panel->dp_mode.bpp);

	dp_panel->dp_mode.bpp = max_t(u32, 18,
					min_t(u32, dp_panel->dp_mode.bpp, 30));
	DRM_DEBUG_DP("updated bpp = %d\n", dp_panel->dp_mode.bpp);

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
	panel->aux_cfg_update_done = false;

	return dp_panel;
}

void dp_panel_put(struct dp_panel *dp_panel)
{
	if (!dp_panel)
		return;

	kfree(dp_panel->edid);
}
