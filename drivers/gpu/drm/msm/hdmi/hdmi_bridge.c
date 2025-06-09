// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/delay.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_edid.h>
#include <drm/display/drm_hdmi_helper.h>
#include <drm/display/drm_hdmi_state_helper.h>

#include "msm_kms.h"
#include "hdmi.h"

static void msm_hdmi_power_on(struct drm_bridge *bridge)
{
	struct drm_device *dev = bridge->dev;
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	const struct hdmi_platform_config *config = hdmi->config;
	int i, ret;

	pm_runtime_get_sync(&hdmi->pdev->dev);

	ret = regulator_bulk_enable(config->pwr_reg_cnt, hdmi->pwr_regs);
	if (ret)
		DRM_DEV_ERROR(dev->dev, "failed to enable pwr regulator: %d\n", ret);

	if (config->pwr_clk_cnt > 0) {
		DBG("pixclock: %lu", hdmi->pixclock);
		ret = clk_set_rate(hdmi->pwr_clks[0], hdmi->pixclock);
		if (ret) {
			DRM_DEV_ERROR(dev->dev, "failed to set pixel clk: %s (%d)\n",
					config->pwr_clk_names[0], ret);
		}
	}

	for (i = 0; i < config->pwr_clk_cnt; i++) {
		ret = clk_prepare_enable(hdmi->pwr_clks[i]);
		if (ret) {
			DRM_DEV_ERROR(dev->dev, "failed to enable pwr clk: %s (%d)\n",
					config->pwr_clk_names[i], ret);
		}
	}
}

static void power_off(struct drm_bridge *bridge)
{
	struct drm_device *dev = bridge->dev;
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	const struct hdmi_platform_config *config = hdmi->config;
	int i, ret;

	/* TODO do we need to wait for final vblank somewhere before
	 * cutting the clocks?
	 */
	mdelay(16 + 4);

	for (i = 0; i < config->pwr_clk_cnt; i++)
		clk_disable_unprepare(hdmi->pwr_clks[i]);

	ret = regulator_bulk_disable(config->pwr_reg_cnt, hdmi->pwr_regs);
	if (ret)
		DRM_DEV_ERROR(dev->dev, "failed to disable pwr regulator: %d\n", ret);

	pm_runtime_put(&hdmi->pdev->dev);
}

#define AVI_IFRAME_LINE_NUMBER 1
#define SPD_IFRAME_LINE_NUMBER 1
#define VENSPEC_IFRAME_LINE_NUMBER 3

static int msm_hdmi_config_avi_infoframe(struct hdmi *hdmi,
					 const u8 *buffer, size_t len)
{
	u32 buf[4] = {};
	u32 val;
	int i;

	if (len != HDMI_INFOFRAME_SIZE(AVI) || len - 3 > sizeof(buf)) {
		DRM_DEV_ERROR(&hdmi->pdev->dev,
			"failed to configure avi infoframe\n");
		return -EINVAL;
	}

	/*
	 * the AVI_INFOx registers don't map exactly to how the AVI infoframes
	 * are packed according to the spec. The checksum from the header is
	 * written to the LSB byte of AVI_INFO0 and the version is written to
	 * the third byte from the LSB of AVI_INFO3
	 */
	memcpy(buf, &buffer[3], len - 3);

	buf[3] |= buffer[1] << 24;

	for (i = 0; i < ARRAY_SIZE(buf); i++)
		hdmi_write(hdmi, REG_HDMI_AVI_INFO(i), buf[i]);

	val = hdmi_read(hdmi, REG_HDMI_INFOFRAME_CTRL1);
	val |= HDMI_INFOFRAME_CTRL0_AVI_SEND |
		HDMI_INFOFRAME_CTRL0_AVI_CONT;
	hdmi_write(hdmi, REG_HDMI_INFOFRAME_CTRL0, val);

	val = hdmi_read(hdmi, REG_HDMI_INFOFRAME_CTRL1);
	val &= ~HDMI_INFOFRAME_CTRL1_AVI_INFO_LINE__MASK;
	val |= HDMI_INFOFRAME_CTRL1_AVI_INFO_LINE(AVI_IFRAME_LINE_NUMBER);
	hdmi_write(hdmi, REG_HDMI_INFOFRAME_CTRL1, val);

	return 0;
}

static int msm_hdmi_config_audio_infoframe(struct hdmi *hdmi,
					   const u8 *buffer, size_t len)
{
	u32 val;

	if (len != HDMI_INFOFRAME_SIZE(AUDIO)) {
		DRM_DEV_ERROR(&hdmi->pdev->dev,
			"failed to configure audio infoframe\n");
		return -EINVAL;
	}

	hdmi_write(hdmi, REG_HDMI_AUDIO_INFO0,
		   buffer[3] |
		   buffer[4] << 8 |
		   buffer[5] << 16 |
		   buffer[6] << 24);

	hdmi_write(hdmi, REG_HDMI_AUDIO_INFO1,
		   buffer[7] |
		   buffer[8] << 8 |
		   buffer[9] << 16 |
		   buffer[10] << 24);

	val = hdmi_read(hdmi, REG_HDMI_INFOFRAME_CTRL1);
	val |= HDMI_INFOFRAME_CTRL0_AUDIO_INFO_SEND |
		HDMI_INFOFRAME_CTRL0_AUDIO_INFO_CONT |
		HDMI_INFOFRAME_CTRL0_AUDIO_INFO_SOURCE |
		HDMI_INFOFRAME_CTRL0_AUDIO_INFO_UPDATE;
	hdmi_write(hdmi, REG_HDMI_INFOFRAME_CTRL0, val);

	return 0;
}

static int msm_hdmi_config_spd_infoframe(struct hdmi *hdmi,
					 const u8 *buffer, size_t len)
{
	u32 buf[7] = {};
	u32 val;
	int i;

	if (len != HDMI_INFOFRAME_SIZE(SPD) || len - 3 > sizeof(buf)) {
		DRM_DEV_ERROR(&hdmi->pdev->dev,
			"failed to configure SPD infoframe\n");
		return -EINVAL;
	}

	/* checksum gets written together with the body of the frame */
	hdmi_write(hdmi, REG_HDMI_GENERIC1_HDR,
		   buffer[0] |
		   buffer[1] << 8 |
		   buffer[2] << 16);

	memcpy(buf, &buffer[3], len - 3);

	for (i = 0; i < ARRAY_SIZE(buf); i++)
		hdmi_write(hdmi, REG_HDMI_GENERIC1(i), buf[i]);

	val = hdmi_read(hdmi, REG_HDMI_GEN_PKT_CTRL);
	val |= HDMI_GEN_PKT_CTRL_GENERIC1_SEND |
		 HDMI_GEN_PKT_CTRL_GENERIC1_CONT |
		 HDMI_GEN_PKT_CTRL_GENERIC1_LINE(SPD_IFRAME_LINE_NUMBER);
	hdmi_write(hdmi, REG_HDMI_GEN_PKT_CTRL, val);

	return 0;
}

static int msm_hdmi_config_hdmi_infoframe(struct hdmi *hdmi,
					  const u8 *buffer, size_t len)
{
	u32 buf[7] = {};
	u32 val;
	int i;

	if (len < HDMI_INFOFRAME_HEADER_SIZE + HDMI_VENDOR_INFOFRAME_SIZE ||
	    len - 3 > sizeof(buf)) {
		DRM_DEV_ERROR(&hdmi->pdev->dev,
			"failed to configure HDMI infoframe\n");
		return -EINVAL;
	}

	/* checksum gets written together with the body of the frame */
	hdmi_write(hdmi, REG_HDMI_GENERIC0_HDR,
		   buffer[0] |
		   buffer[1] << 8 |
		   buffer[2] << 16);

	memcpy(buf, &buffer[3], len - 3);

	for (i = 0; i < ARRAY_SIZE(buf); i++)
		hdmi_write(hdmi, REG_HDMI_GENERIC0(i), buf[i]);

	val = hdmi_read(hdmi, REG_HDMI_GEN_PKT_CTRL);
	val |= HDMI_GEN_PKT_CTRL_GENERIC0_SEND |
		 HDMI_GEN_PKT_CTRL_GENERIC0_CONT |
		 HDMI_GEN_PKT_CTRL_GENERIC0_UPDATE |
		 HDMI_GEN_PKT_CTRL_GENERIC0_LINE(VENSPEC_IFRAME_LINE_NUMBER);
	hdmi_write(hdmi, REG_HDMI_GEN_PKT_CTRL, val);

	return 0;
}

static int msm_hdmi_bridge_clear_infoframe(struct drm_bridge *bridge,
					   enum hdmi_infoframe_type type)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	u32 val;

	switch (type) {
	case HDMI_INFOFRAME_TYPE_AVI:
		val = hdmi_read(hdmi, REG_HDMI_INFOFRAME_CTRL0);
		val &= ~(HDMI_INFOFRAME_CTRL0_AVI_SEND |
			 HDMI_INFOFRAME_CTRL0_AVI_CONT);
		hdmi_write(hdmi, REG_HDMI_INFOFRAME_CTRL0, val);

		val = hdmi_read(hdmi, REG_HDMI_INFOFRAME_CTRL1);
		val &= ~HDMI_INFOFRAME_CTRL1_AVI_INFO_LINE__MASK;
		hdmi_write(hdmi, REG_HDMI_INFOFRAME_CTRL1, val);

		break;

	case HDMI_INFOFRAME_TYPE_AUDIO:
		val = hdmi_read(hdmi, REG_HDMI_INFOFRAME_CTRL0);
		val &= ~(HDMI_INFOFRAME_CTRL0_AUDIO_INFO_SEND |
			 HDMI_INFOFRAME_CTRL0_AUDIO_INFO_CONT |
			 HDMI_INFOFRAME_CTRL0_AUDIO_INFO_SOURCE |
			 HDMI_INFOFRAME_CTRL0_AUDIO_INFO_UPDATE);
		hdmi_write(hdmi, REG_HDMI_INFOFRAME_CTRL0, val);

		val = hdmi_read(hdmi, REG_HDMI_INFOFRAME_CTRL1);
		val &= ~HDMI_INFOFRAME_CTRL1_AUDIO_INFO_LINE__MASK;
		hdmi_write(hdmi, REG_HDMI_INFOFRAME_CTRL1, val);

		break;

	case HDMI_INFOFRAME_TYPE_SPD:
		val = hdmi_read(hdmi, REG_HDMI_GEN_PKT_CTRL);
		val &= ~(HDMI_GEN_PKT_CTRL_GENERIC1_SEND |
			 HDMI_GEN_PKT_CTRL_GENERIC1_CONT |
			 HDMI_GEN_PKT_CTRL_GENERIC1_LINE__MASK);
		hdmi_write(hdmi, REG_HDMI_GEN_PKT_CTRL, val);

		break;

	case HDMI_INFOFRAME_TYPE_VENDOR:
		val = hdmi_read(hdmi, REG_HDMI_GEN_PKT_CTRL);
		val &= ~(HDMI_GEN_PKT_CTRL_GENERIC0_SEND |
			 HDMI_GEN_PKT_CTRL_GENERIC0_CONT |
			 HDMI_GEN_PKT_CTRL_GENERIC0_UPDATE |
			 HDMI_GEN_PKT_CTRL_GENERIC0_LINE__MASK);
		hdmi_write(hdmi, REG_HDMI_GEN_PKT_CTRL, val);

		break;

	default:
		drm_dbg_driver(hdmi_bridge->base.dev, "Unsupported infoframe type %x\n", type);
	}

	return 0;
}

static int msm_hdmi_bridge_write_infoframe(struct drm_bridge *bridge,
					   enum hdmi_infoframe_type type,
					   const u8 *buffer, size_t len)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;

	msm_hdmi_bridge_clear_infoframe(bridge, type);

	switch (type) {
	case HDMI_INFOFRAME_TYPE_AVI:
		return msm_hdmi_config_avi_infoframe(hdmi, buffer, len);
	case HDMI_INFOFRAME_TYPE_AUDIO:
		return msm_hdmi_config_audio_infoframe(hdmi, buffer, len);
	case HDMI_INFOFRAME_TYPE_SPD:
		return msm_hdmi_config_spd_infoframe(hdmi, buffer, len);
	case HDMI_INFOFRAME_TYPE_VENDOR:
		return msm_hdmi_config_hdmi_infoframe(hdmi, buffer, len);
	default:
		drm_dbg_driver(hdmi_bridge->base.dev, "Unsupported infoframe type %x\n", type);
		return 0;
	}
}

static void msm_hdmi_set_timings(struct hdmi *hdmi,
				 const struct drm_display_mode *mode);

static void msm_hdmi_bridge_atomic_pre_enable(struct drm_bridge *bridge,
					      struct drm_atomic_state *state)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	struct hdmi_phy *phy = hdmi->phy;
	struct drm_encoder *encoder = bridge->encoder;
	struct drm_connector *connector;
	struct drm_connector_state *conn_state;
	struct drm_crtc_state *crtc_state;

	DBG("power up");

	connector = drm_atomic_get_new_connector_for_encoder(state, encoder);
	conn_state = drm_atomic_get_new_connector_state(state, connector);
	crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);

	hdmi->pixclock = conn_state->hdmi.tmds_char_rate;

	msm_hdmi_set_timings(hdmi, &crtc_state->adjusted_mode);

	if (!hdmi->power_on) {
		msm_hdmi_phy_resource_enable(phy);
		msm_hdmi_power_on(bridge);
		hdmi->power_on = true;
		if (connector->display_info.is_hdmi)
			msm_hdmi_audio_update(hdmi);
	}

	drm_atomic_helper_connector_hdmi_update_infoframes(connector, state);

	msm_hdmi_phy_powerup(phy, hdmi->pixclock);

	msm_hdmi_set_mode(hdmi, true);

	if (hdmi->hdcp_ctrl)
		msm_hdmi_hdcp_on(hdmi->hdcp_ctrl);
}

static void msm_hdmi_bridge_atomic_post_disable(struct drm_bridge *bridge,
						struct drm_atomic_state *state)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	struct hdmi_phy *phy = hdmi->phy;

	if (hdmi->hdcp_ctrl)
		msm_hdmi_hdcp_off(hdmi->hdcp_ctrl);

	DBG("power down");
	msm_hdmi_set_mode(hdmi, false);

	msm_hdmi_phy_powerdown(phy);

	if (hdmi->power_on) {
		power_off(bridge);
		hdmi->power_on = false;
		if (hdmi->connector->display_info.is_hdmi)
			msm_hdmi_audio_update(hdmi);
		msm_hdmi_phy_resource_disable(phy);
	}
}

static void msm_hdmi_set_timings(struct hdmi *hdmi,
				 const struct drm_display_mode *mode)
{
	int hstart, hend, vstart, vend;
	uint32_t frame_ctrl;

	hstart = mode->htotal - mode->hsync_start;
	hend   = mode->htotal - mode->hsync_start + mode->hdisplay;

	vstart = mode->vtotal - mode->vsync_start - 1;
	vend   = mode->vtotal - mode->vsync_start + mode->vdisplay - 1;

	DBG("htotal=%d, vtotal=%d, hstart=%d, hend=%d, vstart=%d, vend=%d",
			mode->htotal, mode->vtotal, hstart, hend, vstart, vend);

	hdmi_write(hdmi, REG_HDMI_TOTAL,
			HDMI_TOTAL_H_TOTAL(mode->htotal - 1) |
			HDMI_TOTAL_V_TOTAL(mode->vtotal - 1));

	hdmi_write(hdmi, REG_HDMI_ACTIVE_HSYNC,
			HDMI_ACTIVE_HSYNC_START(hstart) |
			HDMI_ACTIVE_HSYNC_END(hend));
	hdmi_write(hdmi, REG_HDMI_ACTIVE_VSYNC,
			HDMI_ACTIVE_VSYNC_START(vstart) |
			HDMI_ACTIVE_VSYNC_END(vend));

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		hdmi_write(hdmi, REG_HDMI_VSYNC_TOTAL_F2,
				HDMI_VSYNC_TOTAL_F2_V_TOTAL(mode->vtotal));
		hdmi_write(hdmi, REG_HDMI_VSYNC_ACTIVE_F2,
				HDMI_VSYNC_ACTIVE_F2_START(vstart + 1) |
				HDMI_VSYNC_ACTIVE_F2_END(vend + 1));
	} else {
		hdmi_write(hdmi, REG_HDMI_VSYNC_TOTAL_F2,
				HDMI_VSYNC_TOTAL_F2_V_TOTAL(0));
		hdmi_write(hdmi, REG_HDMI_VSYNC_ACTIVE_F2,
				HDMI_VSYNC_ACTIVE_F2_START(0) |
				HDMI_VSYNC_ACTIVE_F2_END(0));
	}

	frame_ctrl = 0;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		frame_ctrl |= HDMI_FRAME_CTRL_HSYNC_LOW;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		frame_ctrl |= HDMI_FRAME_CTRL_VSYNC_LOW;
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		frame_ctrl |= HDMI_FRAME_CTRL_INTERLACED_EN;
	DBG("frame_ctrl=%08x", frame_ctrl);
	hdmi_write(hdmi, REG_HDMI_FRAME_CTRL, frame_ctrl);

	if (hdmi->connector->display_info.is_hdmi)
		msm_hdmi_audio_update(hdmi);
}

static const struct drm_edid *msm_hdmi_bridge_edid_read(struct drm_bridge *bridge,
							struct drm_connector *connector)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	const struct drm_edid *drm_edid;
	uint32_t hdmi_ctrl;

	hdmi_ctrl = hdmi_read(hdmi, REG_HDMI_CTRL);
	hdmi_write(hdmi, REG_HDMI_CTRL, hdmi_ctrl | HDMI_CTRL_ENABLE);

	drm_edid = drm_edid_read_ddc(connector, hdmi->i2c);

	hdmi_write(hdmi, REG_HDMI_CTRL, hdmi_ctrl);

	return drm_edid;
}

static enum drm_mode_status msm_hdmi_bridge_tmds_char_rate_valid(const struct drm_bridge *bridge,
								 const struct drm_display_mode *mode,
								 unsigned long long tmds_rate)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	const struct hdmi_platform_config *config = hdmi->config;
	struct msm_drm_private *priv = bridge->dev->dev_private;
	struct msm_kms *kms = priv->kms;
	long actual;

	/* for mdp5/apq8074, we manage our own pixel clk (as opposed to
	 * mdp4/dtv stuff where pixel clk is assigned to mdp/encoder
	 * instead):
	 */
	if (kms->funcs->round_pixclk)
		actual = kms->funcs->round_pixclk(kms,
						  tmds_rate,
						  hdmi_bridge->hdmi->encoder);
	else if (config->pwr_clk_cnt > 0)
		actual = clk_round_rate(hdmi->pwr_clks[0], tmds_rate);
	else
		actual = tmds_rate;

	DBG("requested=%lld, actual=%ld", tmds_rate, actual);

	if (actual != tmds_rate)
		return MODE_CLOCK_RANGE;

	return 0;
}

static const struct drm_bridge_funcs msm_hdmi_bridge_funcs = {
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_pre_enable = msm_hdmi_bridge_atomic_pre_enable,
	.atomic_post_disable = msm_hdmi_bridge_atomic_post_disable,
	.edid_read = msm_hdmi_bridge_edid_read,
	.detect = msm_hdmi_bridge_detect,
	.hdmi_tmds_char_rate_valid = msm_hdmi_bridge_tmds_char_rate_valid,
	.hdmi_clear_infoframe = msm_hdmi_bridge_clear_infoframe,
	.hdmi_write_infoframe = msm_hdmi_bridge_write_infoframe,
	.hdmi_audio_prepare = msm_hdmi_bridge_audio_prepare,
	.hdmi_audio_shutdown = msm_hdmi_bridge_audio_shutdown,
};

static void
msm_hdmi_hotplug_work(struct work_struct *work)
{
	struct hdmi_bridge *hdmi_bridge =
		container_of(work, struct hdmi_bridge, hpd_work);
	struct drm_bridge *bridge = &hdmi_bridge->base;

	drm_bridge_hpd_notify(bridge, drm_bridge_detect(bridge));
}

/* initialize bridge */
int msm_hdmi_bridge_init(struct hdmi *hdmi)
{
	struct drm_bridge *bridge = NULL;
	struct hdmi_bridge *hdmi_bridge;
	int ret;

	hdmi_bridge = devm_kzalloc(hdmi->dev->dev,
			sizeof(*hdmi_bridge), GFP_KERNEL);
	if (!hdmi_bridge)
		return -ENOMEM;

	hdmi_bridge->hdmi = hdmi;
	INIT_WORK(&hdmi_bridge->hpd_work, msm_hdmi_hotplug_work);

	bridge = &hdmi_bridge->base;
	bridge->funcs = &msm_hdmi_bridge_funcs;
	bridge->ddc = hdmi->i2c;
	bridge->type = DRM_MODE_CONNECTOR_HDMIA;
	bridge->vendor = "Qualcomm";
	bridge->product = "Snapdragon";
	bridge->ops = DRM_BRIDGE_OP_HPD |
		DRM_BRIDGE_OP_DETECT |
		DRM_BRIDGE_OP_HDMI |
		DRM_BRIDGE_OP_EDID;
	bridge->hdmi_audio_max_i2s_playback_channels = 8;
	bridge->hdmi_audio_dev = &hdmi->pdev->dev;
	bridge->hdmi_audio_dai_port = -1;

	ret = devm_drm_bridge_add(hdmi->dev->dev, bridge);
	if (ret)
		return ret;

	ret = drm_bridge_attach(hdmi->encoder, bridge, NULL, DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret)
		return ret;

	hdmi->bridge = bridge;

	return 0;
}
