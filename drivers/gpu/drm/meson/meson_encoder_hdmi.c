// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>

#include <media/cec-notifier.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_edid.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include <linux/media-bus-format.h>
#include <linux/videodev2.h>

#include "meson_drv.h"
#include "meson_registers.h"
#include "meson_vclk.h"
#include "meson_venc.h"
#include "meson_encoder_hdmi.h"

struct meson_encoder_hdmi {
	struct drm_encoder encoder;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct drm_connector *connector;
	struct meson_drm *priv;
	unsigned long output_bus_fmt;
	struct cec_notifier *cec_notifier;
};

#define bridge_to_meson_encoder_hdmi(x) \
	container_of(x, struct meson_encoder_hdmi, bridge)

static int meson_encoder_hdmi_attach(struct drm_bridge *bridge,
				     enum drm_bridge_attach_flags flags)
{
	struct meson_encoder_hdmi *encoder_hdmi = bridge_to_meson_encoder_hdmi(bridge);

	return drm_bridge_attach(bridge->encoder, encoder_hdmi->next_bridge,
				 &encoder_hdmi->bridge, flags);
}

static void meson_encoder_hdmi_detach(struct drm_bridge *bridge)
{
	struct meson_encoder_hdmi *encoder_hdmi = bridge_to_meson_encoder_hdmi(bridge);

	cec_notifier_conn_unregister(encoder_hdmi->cec_notifier);
	encoder_hdmi->cec_notifier = NULL;
}

static void meson_encoder_hdmi_set_vclk(struct meson_encoder_hdmi *encoder_hdmi,
					const struct drm_display_mode *mode)
{
	struct meson_drm *priv = encoder_hdmi->priv;
	int vic = drm_match_cea_mode(mode);
	unsigned int phy_freq;
	unsigned int vclk_freq;
	unsigned int venc_freq;
	unsigned int hdmi_freq;

	vclk_freq = mode->clock;

	/* For 420, pixel clock is half unlike venc clock */
	if (encoder_hdmi->output_bus_fmt == MEDIA_BUS_FMT_UYYVYY8_0_5X24)
		vclk_freq /= 2;

	/* TMDS clock is pixel_clock * 10 */
	phy_freq = vclk_freq * 10;

	if (!vic) {
		meson_vclk_setup(priv, MESON_VCLK_TARGET_DMT, phy_freq,
				 vclk_freq, vclk_freq, vclk_freq, false);
		return;
	}

	/* 480i/576i needs global pixel doubling */
	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		vclk_freq *= 2;

	venc_freq = vclk_freq;
	hdmi_freq = vclk_freq;

	/* VENC double pixels for 1080i, 720p and YUV420 modes */
	if (meson_venc_hdmi_venc_repeat(vic) ||
	    encoder_hdmi->output_bus_fmt == MEDIA_BUS_FMT_UYYVYY8_0_5X24)
		venc_freq *= 2;

	vclk_freq = max(venc_freq, hdmi_freq);

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		venc_freq /= 2;

	dev_dbg(priv->dev, "vclk:%d phy=%d venc=%d hdmi=%d enci=%d\n",
		phy_freq, vclk_freq, venc_freq, hdmi_freq,
		priv->venc.hdmi_use_enci);

	meson_vclk_setup(priv, MESON_VCLK_TARGET_HDMI, phy_freq, vclk_freq,
			 venc_freq, hdmi_freq, priv->venc.hdmi_use_enci);
}

static enum drm_mode_status meson_encoder_hdmi_mode_valid(struct drm_bridge *bridge,
					const struct drm_display_info *display_info,
					const struct drm_display_mode *mode)
{
	struct meson_encoder_hdmi *encoder_hdmi = bridge_to_meson_encoder_hdmi(bridge);
	struct meson_drm *priv = encoder_hdmi->priv;
	bool is_hdmi2_sink = display_info->hdmi.scdc.supported;
	unsigned int phy_freq;
	unsigned int vclk_freq;
	unsigned int venc_freq;
	unsigned int hdmi_freq;
	int vic = drm_match_cea_mode(mode);
	enum drm_mode_status status;

	dev_dbg(priv->dev, "Modeline " DRM_MODE_FMT "\n", DRM_MODE_ARG(mode));

	/* If sink does not support 540MHz, reject the non-420 HDMI2 modes */
	if (display_info->max_tmds_clock &&
	    mode->clock > display_info->max_tmds_clock &&
	    !drm_mode_is_420_only(display_info, mode) &&
	    !drm_mode_is_420_also(display_info, mode))
		return MODE_BAD;

	/* Check against non-VIC supported modes */
	if (!vic) {
		status = meson_venc_hdmi_supported_mode(mode);
		if (status != MODE_OK)
			return status;

		return meson_vclk_dmt_supported_freq(priv, mode->clock);
	/* Check against supported VIC modes */
	} else if (!meson_venc_hdmi_supported_vic(vic))
		return MODE_BAD;

	vclk_freq = mode->clock;

	/* For 420, pixel clock is half unlike venc clock */
	if (drm_mode_is_420_only(display_info, mode) ||
	    (!is_hdmi2_sink &&
	     drm_mode_is_420_also(display_info, mode)))
		vclk_freq /= 2;

	/* TMDS clock is pixel_clock * 10 */
	phy_freq = vclk_freq * 10;

	/* 480i/576i needs global pixel doubling */
	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		vclk_freq *= 2;

	venc_freq = vclk_freq;
	hdmi_freq = vclk_freq;

	/* VENC double pixels for 1080i, 720p and YUV420 modes */
	if (meson_venc_hdmi_venc_repeat(vic) ||
	    drm_mode_is_420_only(display_info, mode) ||
	    (!is_hdmi2_sink &&
	     drm_mode_is_420_also(display_info, mode)))
		venc_freq *= 2;

	vclk_freq = max(venc_freq, hdmi_freq);

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		venc_freq /= 2;

	dev_dbg(priv->dev, "%s: vclk:%d phy=%d venc=%d hdmi=%d\n",
		__func__, phy_freq, vclk_freq, venc_freq, hdmi_freq);

	return meson_vclk_vic_supported_freq(priv, phy_freq, vclk_freq);
}

static void meson_encoder_hdmi_atomic_enable(struct drm_bridge *bridge,
					     struct drm_bridge_state *bridge_state)
{
	struct meson_encoder_hdmi *encoder_hdmi = bridge_to_meson_encoder_hdmi(bridge);
	struct drm_atomic_state *state = bridge_state->base.state;
	unsigned int ycrcb_map = VPU_HDMI_OUTPUT_CBYCR;
	struct meson_drm *priv = encoder_hdmi->priv;
	struct drm_connector_state *conn_state;
	const struct drm_display_mode *mode;
	struct drm_crtc_state *crtc_state;
	struct drm_connector *connector;
	bool yuv420_mode = false;
	int vic;

	connector = drm_atomic_get_new_connector_for_encoder(state, bridge->encoder);
	if (WARN_ON(!connector))
		return;

	conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (WARN_ON(!conn_state))
		return;

	crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);
	if (WARN_ON(!crtc_state))
		return;

	mode = &crtc_state->adjusted_mode;

	vic = drm_match_cea_mode(mode);

	dev_dbg(priv->dev, "\"%s\" vic %d\n", mode->name, vic);

	if (encoder_hdmi->output_bus_fmt == MEDIA_BUS_FMT_UYYVYY8_0_5X24) {
		ycrcb_map = VPU_HDMI_OUTPUT_CRYCB;
		yuv420_mode = true;
	}

	/* VENC + VENC-DVI Mode setup */
	meson_venc_hdmi_mode_set(priv, vic, ycrcb_map, yuv420_mode, mode);

	/* VCLK Set clock */
	meson_encoder_hdmi_set_vclk(encoder_hdmi, mode);

	if (encoder_hdmi->output_bus_fmt == MEDIA_BUS_FMT_UYYVYY8_0_5X24)
		/* Setup YUV420 to HDMI-TX, no 10bit diphering */
		writel_relaxed(2 | (2 << 2),
			       priv->io_base + _REG(VPU_HDMI_FMT_CTRL));
	else
		/* Setup YUV444 to HDMI-TX, no 10bit diphering */
		writel_relaxed(0, priv->io_base + _REG(VPU_HDMI_FMT_CTRL));

	dev_dbg(priv->dev, "%s\n", priv->venc.hdmi_use_enci ? "VENCI" : "VENCP");

	if (priv->venc.hdmi_use_enci)
		writel_relaxed(1, priv->io_base + _REG(ENCI_VIDEO_EN));
	else
		writel_relaxed(1, priv->io_base + _REG(ENCP_VIDEO_EN));
}

static void meson_encoder_hdmi_atomic_disable(struct drm_bridge *bridge,
					     struct drm_bridge_state *bridge_state)
{
	struct meson_encoder_hdmi *encoder_hdmi = bridge_to_meson_encoder_hdmi(bridge);
	struct meson_drm *priv = encoder_hdmi->priv;

	writel_bits_relaxed(0x3, 0,
			    priv->io_base + _REG(VPU_HDMI_SETTING));

	writel_relaxed(0, priv->io_base + _REG(ENCI_VIDEO_EN));
	writel_relaxed(0, priv->io_base + _REG(ENCP_VIDEO_EN));
}

static const u32 meson_encoder_hdmi_out_bus_fmts[] = {
	MEDIA_BUS_FMT_YUV8_1X24,
	MEDIA_BUS_FMT_UYYVYY8_0_5X24,
};

static u32 *
meson_encoder_hdmi_get_inp_bus_fmts(struct drm_bridge *bridge,
					struct drm_bridge_state *bridge_state,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state,
					u32 output_fmt,
					unsigned int *num_input_fmts)
{
	u32 *input_fmts = NULL;
	int i;

	*num_input_fmts = 0;

	for (i = 0 ; i < ARRAY_SIZE(meson_encoder_hdmi_out_bus_fmts) ; ++i) {
		if (output_fmt == meson_encoder_hdmi_out_bus_fmts[i]) {
			*num_input_fmts = 1;
			input_fmts = kcalloc(*num_input_fmts,
					     sizeof(*input_fmts),
					     GFP_KERNEL);
			if (!input_fmts)
				return NULL;

			input_fmts[0] = output_fmt;

			break;
		}
	}

	return input_fmts;
}

static int meson_encoder_hdmi_atomic_check(struct drm_bridge *bridge,
					struct drm_bridge_state *bridge_state,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state)
{
	struct meson_encoder_hdmi *encoder_hdmi = bridge_to_meson_encoder_hdmi(bridge);
	struct drm_connector_state *old_conn_state =
		drm_atomic_get_old_connector_state(conn_state->state, conn_state->connector);
	struct meson_drm *priv = encoder_hdmi->priv;

	encoder_hdmi->output_bus_fmt = bridge_state->output_bus_cfg.format;

	dev_dbg(priv->dev, "output_bus_fmt %lx\n", encoder_hdmi->output_bus_fmt);

	if (!drm_connector_atomic_hdr_metadata_equal(old_conn_state, conn_state))
		crtc_state->mode_changed = true;

	return 0;
}

static void meson_encoder_hdmi_hpd_notify(struct drm_bridge *bridge,
					  enum drm_connector_status status)
{
	struct meson_encoder_hdmi *encoder_hdmi = bridge_to_meson_encoder_hdmi(bridge);
	struct edid *edid;

	if (!encoder_hdmi->cec_notifier)
		return;

	if (status == connector_status_connected) {
		edid = drm_bridge_get_edid(encoder_hdmi->next_bridge, encoder_hdmi->connector);
		if (!edid)
			return;

		cec_notifier_set_phys_addr_from_edid(encoder_hdmi->cec_notifier, edid);
	} else
		cec_notifier_phys_addr_invalidate(encoder_hdmi->cec_notifier);
}

static const struct drm_bridge_funcs meson_encoder_hdmi_bridge_funcs = {
	.attach = meson_encoder_hdmi_attach,
	.detach = meson_encoder_hdmi_detach,
	.mode_valid = meson_encoder_hdmi_mode_valid,
	.hpd_notify = meson_encoder_hdmi_hpd_notify,
	.atomic_enable = meson_encoder_hdmi_atomic_enable,
	.atomic_disable = meson_encoder_hdmi_atomic_disable,
	.atomic_get_input_bus_fmts = meson_encoder_hdmi_get_inp_bus_fmts,
	.atomic_check = meson_encoder_hdmi_atomic_check,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
};

int meson_encoder_hdmi_init(struct meson_drm *priv)
{
	struct meson_encoder_hdmi *meson_encoder_hdmi;
	struct platform_device *pdev;
	struct device_node *remote;
	int ret;

	meson_encoder_hdmi = devm_kzalloc(priv->dev, sizeof(*meson_encoder_hdmi), GFP_KERNEL);
	if (!meson_encoder_hdmi)
		return -ENOMEM;

	/* HDMI Transceiver Bridge */
	remote = of_graph_get_remote_node(priv->dev->of_node, 1, 0);
	if (!remote) {
		dev_err(priv->dev, "HDMI transceiver device is disabled");
		return 0;
	}

	meson_encoder_hdmi->next_bridge = of_drm_find_bridge(remote);
	if (!meson_encoder_hdmi->next_bridge) {
		dev_err(priv->dev, "Failed to find HDMI transceiver bridge\n");
		return -EPROBE_DEFER;
	}

	/* HDMI Encoder Bridge */
	meson_encoder_hdmi->bridge.funcs = &meson_encoder_hdmi_bridge_funcs;
	meson_encoder_hdmi->bridge.of_node = priv->dev->of_node;
	meson_encoder_hdmi->bridge.type = DRM_MODE_CONNECTOR_HDMIA;
	meson_encoder_hdmi->bridge.interlace_allowed = true;

	drm_bridge_add(&meson_encoder_hdmi->bridge);

	meson_encoder_hdmi->priv = priv;

	/* Encoder */
	ret = drm_simple_encoder_init(priv->drm, &meson_encoder_hdmi->encoder,
				      DRM_MODE_ENCODER_TMDS);
	if (ret) {
		dev_err(priv->dev, "Failed to init HDMI encoder: %d\n", ret);
		return ret;
	}

	meson_encoder_hdmi->encoder.possible_crtcs = BIT(0);

	/* Attach HDMI Encoder Bridge to Encoder */
	ret = drm_bridge_attach(&meson_encoder_hdmi->encoder, &meson_encoder_hdmi->bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret) {
		dev_err(priv->dev, "Failed to attach bridge: %d\n", ret);
		return ret;
	}

	/* Initialize & attach Bridge Connector */
	meson_encoder_hdmi->connector = drm_bridge_connector_init(priv->drm,
							&meson_encoder_hdmi->encoder);
	if (IS_ERR(meson_encoder_hdmi->connector)) {
		dev_err(priv->dev, "Unable to create HDMI bridge connector\n");
		return PTR_ERR(meson_encoder_hdmi->connector);
	}
	drm_connector_attach_encoder(meson_encoder_hdmi->connector,
				     &meson_encoder_hdmi->encoder);

	/*
	 * We should have now in place:
	 * encoder->[hdmi encoder bridge]->[dw-hdmi bridge]->[display connector bridge]->[display connector]
	 */

	/*
	 * drm_connector_attach_max_bpc_property() requires the
	 * connector to have a state.
	 */
	drm_atomic_helper_connector_reset(meson_encoder_hdmi->connector);

	if (meson_vpu_is_compatible(priv, VPU_COMPATIBLE_GXL) ||
	    meson_vpu_is_compatible(priv, VPU_COMPATIBLE_GXM) ||
	    meson_vpu_is_compatible(priv, VPU_COMPATIBLE_G12A))
		drm_connector_attach_hdr_output_metadata_property(meson_encoder_hdmi->connector);

	drm_connector_attach_max_bpc_property(meson_encoder_hdmi->connector, 8, 8);

	/* Handle this here until handled by drm_bridge_connector_init() */
	meson_encoder_hdmi->connector->ycbcr_420_allowed = true;

	pdev = of_find_device_by_node(remote);
	if (pdev) {
		struct cec_connector_info conn_info;
		struct cec_notifier *notifier;

		cec_fill_conn_info_from_drm(&conn_info, meson_encoder_hdmi->connector);

		notifier = cec_notifier_conn_register(&pdev->dev, NULL, &conn_info);
		if (!notifier)
			return -ENOMEM;

		meson_encoder_hdmi->cec_notifier = notifier;
	}

	dev_dbg(priv->dev, "HDMI encoder initialized\n");

	return 0;
}
