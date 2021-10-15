// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/delay.h>
#include <drm/drm_bridge_connector.h>

#include "msm_kms.h"
#include "hdmi.h"

void msm_hdmi_bridge_destroy(struct drm_bridge *bridge)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);

	msm_hdmi_hpd_disable(hdmi_bridge);
}

static void msm_hdmi_power_on(struct drm_bridge *bridge)
{
	struct drm_device *dev = bridge->dev;
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	const struct hdmi_platform_config *config = hdmi->config;
	int i, ret;

	pm_runtime_get_sync(&hdmi->pdev->dev);

	for (i = 0; i < config->pwr_reg_cnt; i++) {
		ret = regulator_enable(hdmi->pwr_regs[i]);
		if (ret) {
			DRM_DEV_ERROR(dev->dev, "failed to enable pwr regulator: %s (%d)\n",
					config->pwr_reg_names[i], ret);
		}
	}

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

	for (i = 0; i < config->pwr_reg_cnt; i++) {
		ret = regulator_disable(hdmi->pwr_regs[i]);
		if (ret) {
			DRM_DEV_ERROR(dev->dev, "failed to disable pwr regulator: %s (%d)\n",
					config->pwr_reg_names[i], ret);
		}
	}

	pm_runtime_put_autosuspend(&hdmi->pdev->dev);
}

#define AVI_IFRAME_LINE_NUMBER 1

static void msm_hdmi_config_avi_infoframe(struct hdmi *hdmi)
{
	struct drm_crtc *crtc = hdmi->encoder->crtc;
	const struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	union hdmi_infoframe frame;
	u8 buffer[HDMI_INFOFRAME_SIZE(AVI)];
	u32 val;
	int len;

	drm_hdmi_avi_infoframe_from_display_mode(&frame.avi,
						 hdmi->connector, mode);

	len = hdmi_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (len < 0) {
		DRM_DEV_ERROR(&hdmi->pdev->dev,
			"failed to configure avi infoframe\n");
		return;
	}

	/*
	 * the AVI_INFOx registers don't map exactly to how the AVI infoframes
	 * are packed according to the spec. The checksum from the header is
	 * written to the LSB byte of AVI_INFO0 and the version is written to
	 * the third byte from the LSB of AVI_INFO3
	 */
	hdmi_write(hdmi, REG_HDMI_AVI_INFO(0),
		   buffer[3] |
		   buffer[4] << 8 |
		   buffer[5] << 16 |
		   buffer[6] << 24);

	hdmi_write(hdmi, REG_HDMI_AVI_INFO(1),
		   buffer[7] |
		   buffer[8] << 8 |
		   buffer[9] << 16 |
		   buffer[10] << 24);

	hdmi_write(hdmi, REG_HDMI_AVI_INFO(2),
		   buffer[11] |
		   buffer[12] << 8 |
		   buffer[13] << 16 |
		   buffer[14] << 24);

	hdmi_write(hdmi, REG_HDMI_AVI_INFO(3),
		   buffer[15] |
		   buffer[16] << 8 |
		   buffer[1] << 24);

	hdmi_write(hdmi, REG_HDMI_INFOFRAME_CTRL0,
		   HDMI_INFOFRAME_CTRL0_AVI_SEND |
		   HDMI_INFOFRAME_CTRL0_AVI_CONT);

	val = hdmi_read(hdmi, REG_HDMI_INFOFRAME_CTRL1);
	val &= ~HDMI_INFOFRAME_CTRL1_AVI_INFO_LINE__MASK;
	val |= HDMI_INFOFRAME_CTRL1_AVI_INFO_LINE(AVI_IFRAME_LINE_NUMBER);
	hdmi_write(hdmi, REG_HDMI_INFOFRAME_CTRL1, val);
}

static void msm_hdmi_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	struct hdmi_phy *phy = hdmi->phy;

	DBG("power up");

	if (!hdmi->power_on) {
		msm_hdmi_phy_resource_enable(phy);
		msm_hdmi_power_on(bridge);
		hdmi->power_on = true;
		if (hdmi->hdmi_mode) {
			msm_hdmi_config_avi_infoframe(hdmi);
			msm_hdmi_audio_update(hdmi);
		}
	}

	msm_hdmi_phy_powerup(phy, hdmi->pixclock);

	msm_hdmi_set_mode(hdmi, true);

	if (hdmi->hdcp_ctrl)
		msm_hdmi_hdcp_on(hdmi->hdcp_ctrl);
}

static void msm_hdmi_bridge_enable(struct drm_bridge *bridge)
{
}

static void msm_hdmi_bridge_disable(struct drm_bridge *bridge)
{
}

static void msm_hdmi_bridge_post_disable(struct drm_bridge *bridge)
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
		if (hdmi->hdmi_mode)
			msm_hdmi_audio_update(hdmi);
		msm_hdmi_phy_resource_disable(phy);
	}
}

static void msm_hdmi_bridge_mode_set(struct drm_bridge *bridge,
		 const struct drm_display_mode *mode,
		 const struct drm_display_mode *adjusted_mode)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	int hstart, hend, vstart, vend;
	uint32_t frame_ctrl;

	mode = adjusted_mode;

	hdmi->pixclock = mode->clock * 1000;

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

	if (hdmi->hdmi_mode)
		msm_hdmi_audio_update(hdmi);
}

static struct edid *msm_hdmi_bridge_get_edid(struct drm_bridge *bridge,
		struct drm_connector *connector)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	struct edid *edid;
	uint32_t hdmi_ctrl;

	hdmi_ctrl = hdmi_read(hdmi, REG_HDMI_CTRL);
	hdmi_write(hdmi, REG_HDMI_CTRL, hdmi_ctrl | HDMI_CTRL_ENABLE);

	edid = drm_get_edid(connector, hdmi->i2c);

	hdmi_write(hdmi, REG_HDMI_CTRL, hdmi_ctrl);

	hdmi->hdmi_mode = drm_detect_hdmi_monitor(edid);

	return edid;
}

static enum drm_mode_status msm_hdmi_bridge_mode_valid(struct drm_bridge *bridge,
		const struct drm_display_info *info,
		const struct drm_display_mode *mode)
{
	struct hdmi_bridge *hdmi_bridge = to_hdmi_bridge(bridge);
	struct hdmi *hdmi = hdmi_bridge->hdmi;
	const struct hdmi_platform_config *config = hdmi->config;
	struct msm_drm_private *priv = bridge->dev->dev_private;
	struct msm_kms *kms = priv->kms;
	long actual, requested;

	requested = 1000 * mode->clock;
	actual = kms->funcs->round_pixclk(kms,
			requested, hdmi_bridge->hdmi->encoder);

	/* for mdp5/apq8074, we manage our own pixel clk (as opposed to
	 * mdp4/dtv stuff where pixel clk is assigned to mdp/encoder
	 * instead):
	 */
	if (config->pwr_clk_cnt > 0)
		actual = clk_round_rate(hdmi->pwr_clks[0], actual);

	DBG("requested=%ld, actual=%ld", requested, actual);

	if (actual != requested)
		return MODE_CLOCK_RANGE;

	return 0;
}

static const struct drm_bridge_funcs msm_hdmi_bridge_funcs = {
		.pre_enable = msm_hdmi_bridge_pre_enable,
		.enable = msm_hdmi_bridge_enable,
		.disable = msm_hdmi_bridge_disable,
		.post_disable = msm_hdmi_bridge_post_disable,
		.mode_set = msm_hdmi_bridge_mode_set,
		.mode_valid = msm_hdmi_bridge_mode_valid,
		.get_edid = msm_hdmi_bridge_get_edid,
		.detect = msm_hdmi_bridge_detect,
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
struct drm_bridge *msm_hdmi_bridge_init(struct hdmi *hdmi)
{
	struct drm_bridge *bridge = NULL;
	struct hdmi_bridge *hdmi_bridge;
	int ret;

	hdmi_bridge = devm_kzalloc(hdmi->dev->dev,
			sizeof(*hdmi_bridge), GFP_KERNEL);
	if (!hdmi_bridge) {
		ret = -ENOMEM;
		goto fail;
	}

	hdmi_bridge->hdmi = hdmi;
	INIT_WORK(&hdmi_bridge->hpd_work, msm_hdmi_hotplug_work);

	bridge = &hdmi_bridge->base;
	bridge->funcs = &msm_hdmi_bridge_funcs;
	bridge->ddc = hdmi->i2c;
	bridge->type = DRM_MODE_CONNECTOR_HDMIA;
	bridge->ops = DRM_BRIDGE_OP_HPD |
		DRM_BRIDGE_OP_DETECT |
		DRM_BRIDGE_OP_EDID;

	ret = drm_bridge_attach(hdmi->encoder, bridge, NULL, DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret)
		goto fail;

	return bridge;

fail:
	if (bridge)
		msm_hdmi_bridge_destroy(bridge);

	return ERR_PTR(ret);
}
