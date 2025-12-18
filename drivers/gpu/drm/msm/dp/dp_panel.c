// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#include "dp_panel.h"
#include "dp_reg.h"
#include "dp_utils.h"

#include <drm/drm_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>

#include <linux/io.h>
#include <linux/types.h>
#include <asm/byteorder.h>

#define DP_INTF_CONFIG_DATABUS_WIDEN     BIT(4)

struct msm_dp_panel_private {
	struct device *dev;
	struct drm_device *drm_dev;
	struct msm_dp_panel msm_dp_panel;
	struct drm_dp_aux *aux;
	struct msm_dp_link *link;
	void __iomem *link_base;
	void __iomem *p0_base;
	bool panel_on;
};

static inline u32 msm_dp_read_link(struct msm_dp_panel_private *panel, u32 offset)
{
	return readl_relaxed(panel->link_base + offset);
}

static inline void msm_dp_write_link(struct msm_dp_panel_private *panel,
			       u32 offset, u32 data)
{
	/*
	 * To make sure link reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, panel->link_base + offset);
}

static inline void msm_dp_write_p0(struct msm_dp_panel_private *panel,
			       u32 offset, u32 data)
{
	/*
	 * To make sure interface reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	writel(data, panel->p0_base + offset);
}

static inline u32 msm_dp_read_p0(struct msm_dp_panel_private *panel,
			       u32 offset)
{
	/*
	 * To make sure interface reg writes happens before any other operation,
	 * this function uses writel() instread of writel_relaxed()
	 */
	return readl_relaxed(panel->p0_base + offset);
}

static void msm_dp_panel_read_psr_cap(struct msm_dp_panel_private *panel)
{
	ssize_t rlen;
	struct msm_dp_panel *msm_dp_panel;

	msm_dp_panel = &panel->msm_dp_panel;

	/* edp sink */
	if (msm_dp_panel->dpcd[DP_EDP_CONFIGURATION_CAP]) {
		rlen = drm_dp_dpcd_read(panel->aux, DP_PSR_SUPPORT,
				&msm_dp_panel->psr_cap, sizeof(msm_dp_panel->psr_cap));
		if (rlen == sizeof(msm_dp_panel->psr_cap)) {
			drm_dbg_dp(panel->drm_dev,
				"psr version: 0x%x, psr_cap: 0x%x\n",
				msm_dp_panel->psr_cap.version,
				msm_dp_panel->psr_cap.capabilities);
		} else
			DRM_ERROR("failed to read psr info, rlen=%zd\n", rlen);
	}
}

static int msm_dp_panel_read_dpcd(struct msm_dp_panel *msm_dp_panel)
{
	int rc, max_lttpr_lanes, max_lttpr_rate;
	struct msm_dp_panel_private *panel;
	struct msm_dp_link_info *link_info;
	struct msm_dp_link *link;
	u8 *dpcd, major, minor;

	panel = container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);
	dpcd = msm_dp_panel->dpcd;
	rc = drm_dp_read_dpcd_caps(panel->aux, dpcd);
	if (rc)
		return rc;

	msm_dp_panel->vsc_sdp_supported = drm_dp_vsc_sdp_supported(panel->aux, dpcd);
	link_info = &msm_dp_panel->link_info;
	link_info->revision = dpcd[DP_DPCD_REV];
	major = (link_info->revision >> 4) & 0x0f;
	minor = link_info->revision & 0x0f;

	link = panel->link;
	drm_dbg_dp(panel->drm_dev, "max_lanes=%d max_link_rate=%d\n",
		   link->max_dp_lanes, link->max_dp_link_rate);

	max_lttpr_lanes = drm_dp_lttpr_max_lane_count(link->lttpr_common_caps);
	max_lttpr_rate = drm_dp_lttpr_max_link_rate(link->lttpr_common_caps);

	/* eDP sink */
	if (msm_dp_panel->dpcd[DP_EDP_CONFIGURATION_CAP]) {
		u8 edp_rev;

		rc = drm_dp_dpcd_read_byte(panel->aux, DP_EDP_DPCD_REV, &edp_rev);
		if (rc)
			return rc;

		drm_dbg_dp(panel->drm_dev, "edp_rev=0x%x\n", edp_rev);

		/* For eDP v1.4+, parse the SUPPORTED_LINK_RATES table */
		if (edp_rev >= DP_EDP_14) {
			__le16 rates[DP_MAX_SUPPORTED_RATES];
			u8 bw_set;
			int i;

			rc = drm_dp_dpcd_read_data(panel->aux, DP_SUPPORTED_LINK_RATES,
						   rates, sizeof(rates));
			if (rc)
				return rc;

			rc = drm_dp_dpcd_read_byte(panel->aux, DP_LINK_BW_SET, &bw_set);
			if (rc)
				return rc;

			/* Find index of max supported link rate that does not exceed dtsi limits */
			for (i = 0; i < ARRAY_SIZE(rates); i++) {
				/*
				 * The value from the DPCD multiplied by 200 gives
				 * the link rate in kHz. Divide by 10 to convert to
				 * symbol rate, accounting for 8b/10b encoding.
				 */
				u32 rate = (le16_to_cpu(rates[i]) * 200) / 10;

				if (!rate)
					break;

				drm_dbg_dp(panel->drm_dev,
					   "SUPPORTED_LINK_RATES[%d]: %d\n", i, rate);

				/*
				 * Limit link rate from link-frequencies of endpoint
				 * property of dtsi
				 */
				if (rate > link->max_dp_link_rate)
					break;

				/* Limit link rate from LTTPR capabilities, if any */
				if (max_lttpr_rate && rate > max_lttpr_rate)
					break;

				link_info->rate = rate;
				link_info->supported_rates[i] = rate;
				link_info->rate_set = i;
			}

			/* Only use LINK_RATE_SET if LINK_BW_SET hasn't already been written to */
			if (!bw_set && link_info->rate)
				link_info->use_rate_set = true;
		}
	}

	/* Fall back on MAX_LINK_RATE/LINK_BW_SET (DP, eDP <= v1.3) */
	if (!link_info->rate) {
		link_info->rate = drm_dp_max_link_rate(dpcd);

		/* Limit link rate from link-frequencies of endpoint property of dtsi */
		if (link_info->rate > link->max_dp_link_rate)
			link_info->rate = link->max_dp_link_rate;

		/* Limit link rate from LTTPR capabilities, if any */
		if (max_lttpr_rate && max_lttpr_rate < link_info->rate)
			link_info->rate = max_lttpr_rate;
	}

	link_info->num_lanes = drm_dp_max_lane_count(dpcd);

	/* Limit data lanes from data-lanes of endpoint property of dtsi */
	if (link_info->num_lanes > link->max_dp_lanes)
		link_info->num_lanes = link->max_dp_lanes;

	/* Limit data lanes from LTTPR capabilities, if any */
	if (max_lttpr_lanes && max_lttpr_lanes < link_info->num_lanes)
		link_info->num_lanes = max_lttpr_lanes;

	drm_dbg_dp(panel->drm_dev, "version: %d.%d\n", major, minor);
	drm_dbg_dp(panel->drm_dev, "link_rate=%d\n", link_info->rate);
	drm_dbg_dp(panel->drm_dev, "link_rate_set=%d\n", link_info->rate_set);
	drm_dbg_dp(panel->drm_dev, "use_rate_set=%d\n", link_info->use_rate_set);
	drm_dbg_dp(panel->drm_dev, "lane_count=%d\n", link_info->num_lanes);

	if (drm_dp_enhanced_frame_cap(dpcd))
		link_info->capabilities |= DP_LINK_CAP_ENHANCED_FRAMING;

	msm_dp_panel_read_psr_cap(panel);

	return rc;
}

static u32 msm_dp_panel_get_supported_bpp(struct msm_dp_panel *msm_dp_panel,
		u32 mode_edid_bpp, u32 mode_pclk_khz)
{
	const struct msm_dp_link_info *link_info;
	const u32 max_supported_bpp = 30, min_supported_bpp = 18;
	u32 bpp, data_rate_khz;

	bpp = min(mode_edid_bpp, max_supported_bpp);

	link_info = &msm_dp_panel->link_info;
	data_rate_khz = link_info->num_lanes * link_info->rate * 8;

	do {
		if (mode_pclk_khz * bpp <= data_rate_khz)
			return bpp;
		bpp -= 6;
	} while (bpp > min_supported_bpp);

	return min_supported_bpp;
}

int msm_dp_panel_read_sink_caps(struct msm_dp_panel *msm_dp_panel,
	struct drm_connector *connector)
{
	int rc, bw_code;
	int count;
	struct msm_dp_panel_private *panel;

	if (!msm_dp_panel || !connector) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	panel = container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);

	rc = msm_dp_panel_read_dpcd(msm_dp_panel);
	if (rc) {
		DRM_ERROR("read dpcd failed %d\n", rc);
		return rc;
	}

	bw_code = drm_dp_link_rate_to_bw_code(msm_dp_panel->link_info.rate);
	if (!is_link_rate_valid(bw_code) ||
			!is_lane_count_valid(msm_dp_panel->link_info.num_lanes) ||
			(bw_code > msm_dp_panel->max_bw_code)) {
		DRM_ERROR("Illegal link rate=%d lane=%d\n", msm_dp_panel->link_info.rate,
				msm_dp_panel->link_info.num_lanes);
		return -EINVAL;
	}

	if (drm_dp_is_branch(msm_dp_panel->dpcd)) {
		count = drm_dp_read_sink_count(panel->aux);
		if (!count) {
			panel->link->sink_count = 0;
			return -ENOTCONN;
		}
	}

	rc = drm_dp_read_downstream_info(panel->aux, msm_dp_panel->dpcd,
					 msm_dp_panel->downstream_ports);
	if (rc)
		return rc;

	drm_edid_free(msm_dp_panel->drm_edid);

	msm_dp_panel->drm_edid = drm_edid_read_ddc(connector, &panel->aux->ddc);

	drm_edid_connector_update(connector, msm_dp_panel->drm_edid);

	if (!msm_dp_panel->drm_edid) {
		DRM_ERROR("panel edid read failed\n");
		/* check edid read fail is due to unplug */
		if (!msm_dp_aux_is_link_connected(panel->aux)) {
			rc = -ETIMEDOUT;
			goto end;
		}
	}

end:
	return rc;
}

u32 msm_dp_panel_get_mode_bpp(struct msm_dp_panel *msm_dp_panel,
		u32 mode_edid_bpp, u32 mode_pclk_khz)
{
	struct msm_dp_panel_private *panel;
	u32 bpp;

	if (!msm_dp_panel || !mode_edid_bpp || !mode_pclk_khz) {
		DRM_ERROR("invalid input\n");
		return 0;
	}

	panel = container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);

	if (msm_dp_panel->video_test)
		bpp = msm_dp_link_bit_depth_to_bpp(
				panel->link->test_video.test_bit_depth);
	else
		bpp = msm_dp_panel_get_supported_bpp(msm_dp_panel, mode_edid_bpp,
				mode_pclk_khz);

	return bpp;
}

int msm_dp_panel_get_modes(struct msm_dp_panel *msm_dp_panel,
	struct drm_connector *connector)
{
	if (!msm_dp_panel) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	if (msm_dp_panel->drm_edid)
		return drm_edid_connector_add_modes(connector);

	return 0;
}

static u8 msm_dp_panel_get_edid_checksum(const struct edid *edid)
{
	edid += edid->extensions;

	return edid->checksum;
}

void msm_dp_panel_handle_sink_request(struct msm_dp_panel *msm_dp_panel)
{
	struct msm_dp_panel_private *panel;

	if (!msm_dp_panel) {
		DRM_ERROR("invalid input\n");
		return;
	}

	panel = container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);

	if (panel->link->sink_request & DP_TEST_LINK_EDID_READ) {
		/* FIXME: get rid of drm_edid_raw() */
		const struct edid *edid = drm_edid_raw(msm_dp_panel->drm_edid);
		u8 checksum;

		if (edid)
			checksum = msm_dp_panel_get_edid_checksum(edid);
		else
			checksum = msm_dp_panel->connector->real_edid_checksum;

		msm_dp_link_send_edid_checksum(panel->link, checksum);
		msm_dp_link_send_test_response(panel->link);
	}
}

static void msm_dp_panel_tpg_enable(struct msm_dp_panel *msm_dp_panel,
				    struct drm_display_mode *drm_mode)
{
	struct msm_dp_panel_private *panel =
		container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);
	u32 hsync_period, vsync_period;
	u32 display_v_start, display_v_end;
	u32 hsync_start_x, hsync_end_x;
	u32 v_sync_width;
	u32 hsync_ctl;
	u32 display_hctl;

	/* TPG config parameters*/
	hsync_period = drm_mode->htotal;
	vsync_period = drm_mode->vtotal;

	display_v_start = ((drm_mode->vtotal - drm_mode->vsync_start) *
					hsync_period);
	display_v_end = ((vsync_period - (drm_mode->vsync_start -
					drm_mode->vdisplay))
					* hsync_period) - 1;

	display_v_start += drm_mode->htotal - drm_mode->hsync_start;
	display_v_end -= (drm_mode->hsync_start - drm_mode->hdisplay);

	hsync_start_x = drm_mode->htotal - drm_mode->hsync_start;
	hsync_end_x = hsync_period - (drm_mode->hsync_start -
					drm_mode->hdisplay) - 1;

	v_sync_width = drm_mode->vsync_end - drm_mode->vsync_start;

	hsync_ctl = (hsync_period << 16) |
			(drm_mode->hsync_end - drm_mode->hsync_start);
	display_hctl = (hsync_end_x << 16) | hsync_start_x;


	msm_dp_write_p0(panel, MMSS_DP_INTF_HSYNC_CTL, hsync_ctl);
	msm_dp_write_p0(panel, MMSS_DP_INTF_VSYNC_PERIOD_F0, vsync_period *
			hsync_period);
	msm_dp_write_p0(panel, MMSS_DP_INTF_VSYNC_PULSE_WIDTH_F0, v_sync_width *
			hsync_period);
	msm_dp_write_p0(panel, MMSS_DP_INTF_VSYNC_PERIOD_F1, 0);
	msm_dp_write_p0(panel, MMSS_DP_INTF_VSYNC_PULSE_WIDTH_F1, 0);
	msm_dp_write_p0(panel, MMSS_DP_INTF_DISPLAY_HCTL, display_hctl);
	msm_dp_write_p0(panel, MMSS_DP_INTF_ACTIVE_HCTL, 0);
	msm_dp_write_p0(panel, MMSS_INTF_DISPLAY_V_START_F0, display_v_start);
	msm_dp_write_p0(panel, MMSS_DP_INTF_DISPLAY_V_END_F0, display_v_end);
	msm_dp_write_p0(panel, MMSS_INTF_DISPLAY_V_START_F1, 0);
	msm_dp_write_p0(panel, MMSS_DP_INTF_DISPLAY_V_END_F1, 0);
	msm_dp_write_p0(panel, MMSS_DP_INTF_ACTIVE_V_START_F0, 0);
	msm_dp_write_p0(panel, MMSS_DP_INTF_ACTIVE_V_END_F0, 0);
	msm_dp_write_p0(panel, MMSS_DP_INTF_ACTIVE_V_START_F1, 0);
	msm_dp_write_p0(panel, MMSS_DP_INTF_ACTIVE_V_END_F1, 0);
	msm_dp_write_p0(panel, MMSS_DP_INTF_POLARITY_CTL, 0);

	msm_dp_write_p0(panel, MMSS_DP_TPG_MAIN_CONTROL,
				DP_TPG_CHECKERED_RECT_PATTERN);
	msm_dp_write_p0(panel, MMSS_DP_TPG_VIDEO_CONFIG,
				DP_TPG_VIDEO_CONFIG_BPP_8BIT |
				DP_TPG_VIDEO_CONFIG_RGB);
	msm_dp_write_p0(panel, MMSS_DP_BIST_ENABLE,
				DP_BIST_ENABLE_DPBIST_EN);
	msm_dp_write_p0(panel, MMSS_DP_TIMING_ENGINE_EN,
				DP_TIMING_ENGINE_EN_EN);
	drm_dbg_dp(panel->drm_dev, "%s: enabled tpg\n", __func__);
}

static void msm_dp_panel_tpg_disable(struct msm_dp_panel *msm_dp_panel)
{
	struct msm_dp_panel_private *panel =
		container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);

	msm_dp_write_p0(panel, MMSS_DP_TPG_MAIN_CONTROL, 0x0);
	msm_dp_write_p0(panel, MMSS_DP_BIST_ENABLE, 0x0);
	msm_dp_write_p0(panel, MMSS_DP_TIMING_ENGINE_EN, 0x0);
}

void msm_dp_panel_tpg_config(struct msm_dp_panel *msm_dp_panel, bool enable)
{
	struct msm_dp_panel_private *panel;

	if (!msm_dp_panel) {
		DRM_ERROR("invalid input\n");
		return;
	}

	panel = container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);

	if (!panel->panel_on) {
		drm_dbg_dp(panel->drm_dev,
				"DP panel not enabled, handle TPG on next on\n");
		return;
	}

	if (!enable) {
		msm_dp_panel_tpg_disable(msm_dp_panel);
		return;
	}

	drm_dbg_dp(panel->drm_dev, "calling panel's tpg_enable\n");
	msm_dp_panel_tpg_enable(msm_dp_panel, &panel->msm_dp_panel.msm_dp_mode.drm_mode);
}

void msm_dp_panel_clear_dsc_dto(struct msm_dp_panel *msm_dp_panel)
{
	struct msm_dp_panel_private *panel =
		container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);

	msm_dp_write_p0(panel, MMSS_DP_DSC_DTO, 0x0);
}

static void msm_dp_panel_send_vsc_sdp(struct msm_dp_panel_private *panel, struct dp_sdp *vsc_sdp)
{
	u32 header[2];
	u32 val;
	int i;

	msm_dp_utils_pack_sdp_header(&vsc_sdp->sdp_header, header);

	msm_dp_write_link(panel, MMSS_DP_GENERIC0_0, header[0]);
	msm_dp_write_link(panel, MMSS_DP_GENERIC0_1, header[1]);

	for (i = 0; i < sizeof(vsc_sdp->db); i += 4) {
		val = ((vsc_sdp->db[i]) | (vsc_sdp->db[i + 1] << 8) | (vsc_sdp->db[i + 2] << 16) |
		       (vsc_sdp->db[i + 3] << 24));
		msm_dp_write_link(panel, MMSS_DP_GENERIC0_2 + i, val);
	}
}

static void msm_dp_panel_update_sdp(struct msm_dp_panel_private *panel)
{
	u32 hw_revision = panel->msm_dp_panel.hw_revision;

	if (hw_revision >= DP_HW_VERSION_1_0 &&
	    hw_revision < DP_HW_VERSION_1_2) {
		msm_dp_write_link(panel, MMSS_DP_SDP_CFG3, UPDATE_SDP);
		msm_dp_write_link(panel, MMSS_DP_SDP_CFG3, 0x0);
	}
}

void msm_dp_panel_enable_vsc_sdp(struct msm_dp_panel *msm_dp_panel, struct dp_sdp *vsc_sdp)
{
	struct msm_dp_panel_private *panel =
		container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);
	u32 cfg, cfg2, misc;

	cfg = msm_dp_read_link(panel, MMSS_DP_SDP_CFG);
	cfg2 = msm_dp_read_link(panel, MMSS_DP_SDP_CFG2);
	misc = msm_dp_read_link(panel, REG_DP_MISC1_MISC0);

	cfg |= GEN0_SDP_EN;
	msm_dp_write_link(panel, MMSS_DP_SDP_CFG, cfg);

	cfg2 |= GENERIC0_SDPSIZE_VALID;
	msm_dp_write_link(panel, MMSS_DP_SDP_CFG2, cfg2);

	msm_dp_panel_send_vsc_sdp(panel, vsc_sdp);

	/* indicates presence of VSC (BIT(6) of MISC1) */
	misc |= DP_MISC1_VSC_SDP;

	drm_dbg_dp(panel->drm_dev, "vsc sdp enable=1\n");

	pr_debug("misc settings = 0x%x\n", misc);
	msm_dp_write_link(panel, REG_DP_MISC1_MISC0, misc);

	msm_dp_panel_update_sdp(panel);
}

void msm_dp_panel_disable_vsc_sdp(struct msm_dp_panel *msm_dp_panel)
{
	struct msm_dp_panel_private *panel =
		container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);
	u32 cfg, cfg2, misc;

	cfg = msm_dp_read_link(panel, MMSS_DP_SDP_CFG);
	cfg2 = msm_dp_read_link(panel, MMSS_DP_SDP_CFG2);
	misc = msm_dp_read_link(panel, REG_DP_MISC1_MISC0);

	cfg &= ~GEN0_SDP_EN;
	msm_dp_write_link(panel, MMSS_DP_SDP_CFG, cfg);

	cfg2 &= ~GENERIC0_SDPSIZE_VALID;
	msm_dp_write_link(panel, MMSS_DP_SDP_CFG2, cfg2);

	/* switch back to MSA */
	misc &= ~DP_MISC1_VSC_SDP;

	drm_dbg_dp(panel->drm_dev, "vsc sdp enable=0\n");

	pr_debug("misc settings = 0x%x\n", misc);
	msm_dp_write_link(panel, REG_DP_MISC1_MISC0, misc);

	msm_dp_panel_update_sdp(panel);
}

static int msm_dp_panel_setup_vsc_sdp_yuv_420(struct msm_dp_panel *msm_dp_panel)
{
	struct msm_dp_display_mode *msm_dp_mode;
	struct drm_dp_vsc_sdp vsc_sdp_data;
	struct dp_sdp vsc_sdp;
	ssize_t len;

	if (!msm_dp_panel) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	msm_dp_mode = &msm_dp_panel->msm_dp_mode;

	memset(&vsc_sdp_data, 0, sizeof(vsc_sdp_data));

	/* VSC SDP header as per table 2-118 of DP 1.4 specification */
	vsc_sdp_data.sdp_type = DP_SDP_VSC;
	vsc_sdp_data.revision = 0x05;
	vsc_sdp_data.length = 0x13;

	/* VSC SDP Payload for DB16 */
	vsc_sdp_data.pixelformat = DP_PIXELFORMAT_YUV420;
	vsc_sdp_data.colorimetry = DP_COLORIMETRY_DEFAULT;

	/* VSC SDP Payload for DB17 */
	vsc_sdp_data.bpc = msm_dp_mode->bpp / 3;
	vsc_sdp_data.dynamic_range = DP_DYNAMIC_RANGE_CTA;

	/* VSC SDP Payload for DB18 */
	vsc_sdp_data.content_type = DP_CONTENT_TYPE_GRAPHICS;

	len = drm_dp_vsc_sdp_pack(&vsc_sdp_data, &vsc_sdp);
	if (len < 0) {
		DRM_ERROR("unable to pack vsc sdp\n");
		return len;
	}

	msm_dp_panel_enable_vsc_sdp(msm_dp_panel, &vsc_sdp);

	return 0;
}

int msm_dp_panel_timing_cfg(struct msm_dp_panel *msm_dp_panel, bool wide_bus_en)
{
	u32 data, total_ver, total_hor;
	struct msm_dp_panel_private *panel;
	struct drm_display_mode *drm_mode;
	u32 width_blanking;
	u32 sync_start;
	u32 msm_dp_active;
	u32 total;
	u32 reg;

	panel = container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);
	drm_mode = &panel->msm_dp_panel.msm_dp_mode.drm_mode;

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

	total = data;

	data = (drm_mode->vtotal - drm_mode->vsync_start);
	data <<= 16;
	data |= (drm_mode->htotal - drm_mode->hsync_start);

	sync_start = data;

	data = drm_mode->vsync_end - drm_mode->vsync_start;
	data <<= 16;
	data |= (panel->msm_dp_panel.msm_dp_mode.v_active_low << 31);
	data |= drm_mode->hsync_end - drm_mode->hsync_start;
	data |= (panel->msm_dp_panel.msm_dp_mode.h_active_low << 15);

	width_blanking = data;

	data = drm_mode->vdisplay;
	data <<= 16;
	data |= drm_mode->hdisplay;

	msm_dp_active = data;

	msm_dp_write_link(panel, REG_DP_TOTAL_HOR_VER, total);
	msm_dp_write_link(panel, REG_DP_START_HOR_VER_FROM_SYNC, sync_start);
	msm_dp_write_link(panel, REG_DP_HSYNC_VSYNC_WIDTH_POLARITY, width_blanking);
	msm_dp_write_link(panel, REG_DP_ACTIVE_HOR_VER, msm_dp_active);

	reg = msm_dp_read_p0(panel, MMSS_DP_INTF_CONFIG);
	if (wide_bus_en)
		reg |= DP_INTF_CONFIG_DATABUS_WIDEN;
	else
		reg &= ~DP_INTF_CONFIG_DATABUS_WIDEN;

	drm_dbg_dp(panel->drm_dev, "wide_bus_en=%d reg=%#x\n", wide_bus_en, reg);

	msm_dp_write_p0(panel, MMSS_DP_INTF_CONFIG, reg);

	if (msm_dp_panel->msm_dp_mode.out_fmt_is_yuv_420)
		msm_dp_panel_setup_vsc_sdp_yuv_420(msm_dp_panel);

	panel->panel_on = true;

	return 0;
}

int msm_dp_panel_init_panel_info(struct msm_dp_panel *msm_dp_panel)
{
	struct drm_display_mode *drm_mode;
	struct msm_dp_panel_private *panel;

	drm_mode = &msm_dp_panel->msm_dp_mode.drm_mode;

	panel = container_of(msm_dp_panel, struct msm_dp_panel_private, msm_dp_panel);

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
	drm_dbg_dp(panel->drm_dev, "bpp = %d\n", msm_dp_panel->msm_dp_mode.bpp);

	msm_dp_panel->msm_dp_mode.bpp = msm_dp_panel_get_mode_bpp(msm_dp_panel, msm_dp_panel->msm_dp_mode.bpp,
						      msm_dp_panel->msm_dp_mode.drm_mode.clock);

	drm_dbg_dp(panel->drm_dev, "updated bpp = %d\n",
				msm_dp_panel->msm_dp_mode.bpp);

	return 0;
}

struct msm_dp_panel *msm_dp_panel_get(struct device *dev, struct drm_dp_aux *aux,
			      struct msm_dp_link *link,
			      void __iomem *link_base,
			      void __iomem *p0_base)
{
	struct msm_dp_panel_private *panel;
	struct msm_dp_panel *msm_dp_panel;

	if (!dev || !aux || !link) {
		DRM_ERROR("invalid input\n");
		return ERR_PTR(-EINVAL);
	}

	panel = devm_kzalloc(dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return ERR_PTR(-ENOMEM);

	panel->dev = dev;
	panel->aux = aux;
	panel->link = link;
	panel->link_base = link_base;
	panel->p0_base = p0_base;

	msm_dp_panel = &panel->msm_dp_panel;
	msm_dp_panel->max_bw_code = DP_LINK_BW_8_1;

	return msm_dp_panel;
}

void msm_dp_panel_put(struct msm_dp_panel *msm_dp_panel)
{
	if (!msm_dp_panel)
		return;

	drm_edid_free(msm_dp_panel->drm_edid);
}
