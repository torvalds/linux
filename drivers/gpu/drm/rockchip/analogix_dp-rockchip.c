// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Rockchip SoC DP (Display Port) interface driver.
 *
 * Copyright (C) Fuzhou Rockchip Electronics Co., Ltd.
 * Author: Andy Yan <andy.yan@rock-chips.com>
 *         Yakir Yang <ykk@rock-chips.com>
 *         Jeff Chen <jeff.chen@rock-chips.com>
 */

#include <linux/component.h>
#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/clk.h>

#include <uapi/linux/videodev2.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/bridge/analogix_dp.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#define PSR_WAIT_LINE_FLAG_TIMEOUT_MS	100

#define to_dp(nm)	container_of(nm, struct rockchip_dp_device, nm)

#define GRF_REG_FIELD(_reg, _lsb, _msb) {	\
				.reg = _reg,	\
				.lsb = _lsb,	\
				.msb = _msb,	\
				.valid = true,	\
				}

struct rockchip_grf_reg_field {
	unsigned int reg;
	unsigned int lsb;
	unsigned int msb;
	bool valid;
};

/**
 * struct rockchip_dp_chip_data - splite the grf setting of kind of chips
 * @lcdc_sel: grf register field of lcdc_sel
 * @spdif_sel: grf register field of spdif_sel
 * @i2s_sel: grf register field of i2s_sel
 * @edp_mode: grf register field of edp_mode
 * @chip_type: specific chip type
 * @ssc: check if SSC is supported by source
 * @audio: check if audio is supported by source
 * @split_mode: check if split mode is supported
 */
struct rockchip_dp_chip_data {
	const struct rockchip_grf_reg_field lcdc_sel;
	const struct rockchip_grf_reg_field spdif_sel;
	const struct rockchip_grf_reg_field i2s_sel;
	const struct rockchip_grf_reg_field edp_mode;
	u32	chip_type;
	bool	ssc;
	bool	audio;
	bool	split_mode;
};

struct rockchip_dp_device {
	struct drm_device        *drm_dev;
	struct device            *dev;
	struct drm_encoder       encoder;
	struct drm_display_mode  mode;

	struct regmap            *grf;
	struct reset_control     *rst;
	struct reset_control     *apb_reset;

	struct platform_device *audio_pdev;
	const struct rockchip_dp_chip_data *data;
	int id;

	struct analogix_dp_device *adp;
	struct analogix_dp_plat_data plat_data;
	struct rockchip_drm_sub_dev sub_dev;

	unsigned int min_refresh_rate;
	unsigned int max_refresh_rate;
};

static int rockchip_grf_write(struct regmap *grf, unsigned int reg,
			      unsigned int mask, unsigned int val)
{
	return regmap_write(grf, reg, (mask << 16) | (val & mask));
}

static int rockchip_grf_field_write(struct regmap *grf,
				    const struct rockchip_grf_reg_field *field,
				    unsigned int val)
{
	unsigned int mask;

	if (!field->valid)
		return 0;

	mask = GENMASK(field->msb, field->lsb);
	val <<= field->lsb;

	return rockchip_grf_write(grf, field->reg, mask, val);
}

static int rockchip_dp_audio_hw_params(struct device *dev, void *data,
				       struct hdmi_codec_daifmt *daifmt,
				       struct hdmi_codec_params *params)
{
	struct rockchip_dp_device *dp = dev_get_drvdata(dev);

	rockchip_grf_field_write(dp->grf, &dp->data->spdif_sel,
				 daifmt->fmt == HDMI_SPDIF);
	rockchip_grf_field_write(dp->grf, &dp->data->i2s_sel,
				 daifmt->fmt == HDMI_I2S);

	return analogix_dp_audio_hw_params(dp->adp, daifmt, params);
}

static void rockchip_dp_audio_shutdown(struct device *dev, void *data)
{
	struct rockchip_dp_device *dp = dev_get_drvdata(dev);

	analogix_dp_audio_shutdown(dp->adp);

	rockchip_grf_field_write(dp->grf, &dp->data->spdif_sel, 0);
	rockchip_grf_field_write(dp->grf, &dp->data->i2s_sel, 0);
}

static int rockchip_dp_audio_startup(struct device *dev, void *data)
{
	struct rockchip_dp_device *dp = dev_get_drvdata(dev);

	return analogix_dp_audio_startup(dp->adp);
}

static int rockchip_dp_audio_get_eld(struct device *dev, void *data,
				     u8 *buf, size_t len)
{
	struct rockchip_dp_device *dp = dev_get_drvdata(dev);

	return analogix_dp_audio_get_eld(dp->adp, buf, len);
}

static const struct hdmi_codec_ops rockchip_dp_audio_codec_ops = {
	.hw_params = rockchip_dp_audio_hw_params,
	.audio_startup = rockchip_dp_audio_startup,
	.audio_shutdown = rockchip_dp_audio_shutdown,
	.get_eld = rockchip_dp_audio_get_eld,
};

static int rockchip_dp_match_by_id(struct device *dev, const void *data)
{
	struct rockchip_dp_device *dp = dev_get_drvdata(dev);
	const unsigned int *id = data;

	return dp->id == *id;
}

static struct rockchip_dp_device *
rockchip_dp_find_by_id(struct device_driver *drv, unsigned int id)
{
	struct device *dev;

	dev = driver_find_device(drv, NULL, &id, rockchip_dp_match_by_id);
	if (!dev)
		return NULL;

	return dev_get_drvdata(dev);
}

static int rockchip_dp_pre_init(struct rockchip_dp_device *dp)
{
	reset_control_assert(dp->rst);
	usleep_range(10, 20);
	reset_control_deassert(dp->rst);

	reset_control_assert(dp->apb_reset);
	usleep_range(10, 20);
	reset_control_deassert(dp->apb_reset);

	return 0;
}

static int rockchip_dp_poweron_start(struct analogix_dp_plat_data *plat_data)
{
	struct rockchip_dp_device *dp = to_dp(plat_data);
	int ret;

	ret = rockchip_dp_pre_init(dp);
	if (ret < 0) {
		DRM_DEV_ERROR(dp->dev, "failed to dp pre init %d\n", ret);
		return ret;
	}

	return rockchip_grf_field_write(dp->grf, &dp->data->edp_mode, 1);
}

static int rockchip_dp_powerdown(struct analogix_dp_plat_data *plat_data)
{
	struct rockchip_dp_device *dp = to_dp(plat_data);

	return rockchip_grf_field_write(dp->grf, &dp->data->edp_mode, 0);
}

static int rockchip_dp_get_modes(struct analogix_dp_plat_data *plat_data,
				 struct drm_connector *connector)
{
	struct drm_display_info *di = &connector->display_info;
	/* VOP couldn't output YUV video format for eDP rightly */
	u32 mask = DRM_COLOR_FORMAT_YCRCB444 | DRM_COLOR_FORMAT_YCRCB422;

	if ((di->color_formats & mask)) {
		DRM_DEBUG_KMS("Swapping display color format from YUV to RGB\n");
		di->color_formats &= ~mask;
		di->color_formats |= DRM_COLOR_FORMAT_RGB444;
		di->bpc = 8;
	}

	return 0;
}

static int rockchip_dp_loader_protect(struct drm_encoder *encoder, bool on)
{
	struct rockchip_dp_device *dp = to_dp(encoder);
	struct analogix_dp_plat_data *plat_data = &dp->plat_data;
	struct rockchip_dp_device *secondary = NULL;
	int ret;

	if (plat_data->right) {
		secondary = rockchip_dp_find_by_id(dp->dev->driver, !dp->id);

		ret = rockchip_dp_loader_protect(&secondary->encoder, on);
		if (ret)
			return ret;
	}

	if (!on)
		return 0;

	if (plat_data->panel)
		panel_simple_loader_protect(plat_data->panel);

	ret = analogix_dp_loader_protect(dp->adp);
	if (ret) {
		if (secondary)
			analogix_dp_disable(secondary->adp);
		return ret;
	}

	return 0;
}

static bool rockchip_dp_skip_connector(struct drm_bridge *bridge)
{
	if (!bridge)
		return false;

	if (of_device_is_compatible(bridge->of_node, "dp-connector"))
		return false;

	if (bridge->ops & DRM_BRIDGE_OP_MODES)
		return false;

	return true;
}

static int rockchip_dp_bridge_attach(struct analogix_dp_plat_data *plat_data,
				     struct drm_bridge *bridge,
				     struct drm_connector *connector)
{
	struct rockchip_dp_device *dp = to_dp(plat_data);
	struct rockchip_drm_sub_dev *sdev = &dp->sub_dev;

	if (!connector) {
		struct list_head *connector_list =
			&bridge->dev->mode_config.connector_list;

		list_for_each_entry(connector, connector_list, head)
			if (drm_connector_has_possible_encoder(connector,
							       bridge->encoder))
				break;
	}

	if (connector) {
		sdev->connector = connector;
		sdev->of_node = dp->dev->of_node;
		sdev->loader_protect = rockchip_dp_loader_protect;
		rockchip_drm_register_sub_dev(sdev);
	}

	return 0;
}

static void rockchip_dp_bridge_detach(struct analogix_dp_plat_data *plat_data,
				      struct drm_bridge *bridge)
{
	struct rockchip_dp_device *dp = to_dp(plat_data);
	struct rockchip_drm_sub_dev *sdev = &dp->sub_dev;

	if (sdev->connector)
		rockchip_drm_unregister_sub_dev(sdev);
}

static bool
rockchip_dp_drm_encoder_mode_fixup(struct drm_encoder *encoder,
				   const struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode)
{
	/* do nothing */
	return true;
}

static void rockchip_dp_drm_encoder_mode_set(struct drm_encoder *encoder,
					     struct drm_display_mode *mode,
					     struct drm_display_mode *adjusted)
{
	/* do nothing */
}

static
struct drm_crtc *rockchip_dp_drm_get_new_crtc(struct drm_encoder *encoder,
					      struct drm_atomic_state *state)
{
	struct drm_connector *connector;
	struct drm_connector_state *conn_state;

	connector = drm_atomic_get_new_connector_for_encoder(state, encoder);
	if (!connector)
		return NULL;

	conn_state = drm_atomic_get_new_connector_state(state, connector);
	if (!conn_state)
		return NULL;

	return conn_state->crtc;
}

static void rockchip_dp_drm_encoder_enable(struct drm_encoder *encoder,
					   struct drm_atomic_state *state)
{
	struct rockchip_dp_device *dp = to_dp(encoder);
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;
	int ret;

	crtc = rockchip_dp_drm_get_new_crtc(encoder, state);
	if (!crtc)
		return;

	old_crtc_state = drm_atomic_get_old_crtc_state(state, crtc);
	/* Coming back from self refresh, nothing to do */
	if (old_crtc_state && old_crtc_state->self_refresh_active)
		return;

	ret = drm_of_encoder_active_endpoint_id(dp->dev->of_node, encoder);
	if (ret < 0)
		return;

	DRM_DEV_DEBUG(dp->dev, "vop %s output to dp\n", (ret) ? "LIT" : "BIG");

	ret = rockchip_grf_field_write(dp->grf, &dp->data->lcdc_sel, ret);
	if (ret != 0)
		DRM_DEV_ERROR(dp->dev, "Could not write to GRF: %d\n", ret);
}

static void rockchip_dp_drm_encoder_disable(struct drm_encoder *encoder,
					    struct drm_atomic_state *state)
{
	struct rockchip_dp_device *dp = to_dp(encoder);
	struct drm_crtc *crtc;
	struct drm_crtc *old_crtc = encoder->crtc;
	struct drm_crtc_state *new_crtc_state = NULL;
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(old_crtc->state);
	int ret;

	if (dp->plat_data.split_mode)
		s->output_if &= ~(VOP_OUTPUT_IF_eDP1 | VOP_OUTPUT_IF_eDP0);
	else
		s->output_if &= ~(dp->id ? VOP_OUTPUT_IF_eDP1 : VOP_OUTPUT_IF_eDP0);
	crtc = rockchip_dp_drm_get_new_crtc(encoder, state);
	/* No crtc means we're doing a full shutdown */
	if (!crtc)
		return;

	new_crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	/* If we're not entering self-refresh, no need to wait for vact */
	if (!new_crtc_state || !new_crtc_state->self_refresh_active)
		return;

	ret = rockchip_drm_wait_vact_end(crtc, PSR_WAIT_LINE_FLAG_TIMEOUT_MS);
	if (ret)
		DRM_DEV_ERROR(dp->dev, "line flag irq timed out\n");
}

static int
rockchip_dp_drm_encoder_atomic_check(struct drm_encoder *encoder,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state)
{
	struct rockchip_dp_device *dp = to_dp(encoder);
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct drm_display_info *di = &conn_state->connector->display_info;
	int refresh_rate;

	if (di->num_bus_formats)
		s->bus_format = di->bus_formats[0];
	else
		s->bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	/*
	 * The hardware IC designed that VOP must output the RGB10 video
	 * format to eDP controller, and if eDP panel only support RGB8,
	 * then eDP controller should cut down the video data, not via VOP
	 * controller, that's why we need to hardcode the VOP output mode
	 * to RGA10 here.
	 */

	s->output_mode = ROCKCHIP_OUT_MODE_AAAA;
	s->output_type = DRM_MODE_CONNECTOR_eDP;
	if (dp->plat_data.split_mode) {
		s->output_flags |= ROCKCHIP_OUTPUT_DUAL_CHANNEL_LEFT_RIGHT_MODE;
		s->output_flags |= dp->id ? ROCKCHIP_OUTPUT_DATA_SWAP : 0;
		s->output_if |= VOP_OUTPUT_IF_eDP0 | VOP_OUTPUT_IF_eDP1;
	} else {
		s->output_if |= dp->id ? VOP_OUTPUT_IF_eDP1 : VOP_OUTPUT_IF_eDP0;
	}

	if (dp->plat_data.dual_connector_split) {
		s->output_flags |= ROCKCHIP_OUTPUT_DUAL_CONNECTOR_SPLIT_MODE;

		if (dp->plat_data.left_display)
			s->output_if_left_panel |= dp->id ?
						   VOP_OUTPUT_IF_eDP1 :
						   VOP_OUTPUT_IF_eDP0;
	}

	s->output_bpc = di->bpc;
	s->bus_flags = di->bus_flags;
	s->tv_state = &conn_state->tv;
	s->eotf = HDMI_EOTF_TRADITIONAL_GAMMA_SDR;
	s->color_space = V4L2_COLORSPACE_DEFAULT;
	/**
	 * It's priority to user rate range define in dtsi.
	 */
	if (dp->max_refresh_rate && dp->min_refresh_rate) {
		s->max_refresh_rate = dp->max_refresh_rate;
		s->min_refresh_rate = dp->min_refresh_rate;
	} else {
		s->max_refresh_rate = di->monitor_range.max_vfreq;
		s->min_refresh_rate = di->monitor_range.min_vfreq;
	}

	/**
	 * Timing exposed in DisplayID or legacy EDID is usually optimized
	 * for bandwidth by using minimum horizontal and vertical blank. If
	 * timing beyond the Adaptive-Sync range, it should not enable the
	 * Ignore MSA option in this timing. If the refresh rate of the
	 * timing is with the Adaptive-Sync range, this timing should support
	 * the Adaptive-Sync from the timing's refresh rate to minimum
	 * support range.
	 */
	refresh_rate = drm_mode_vrefresh(&crtc_state->adjusted_mode);
	if (refresh_rate > s->max_refresh_rate || refresh_rate < s->min_refresh_rate) {
		s->max_refresh_rate = 0;
		s->min_refresh_rate = 0;
	} else if (refresh_rate < s->max_refresh_rate) {
		s->max_refresh_rate = refresh_rate;
	}

	return 0;
}

static struct drm_encoder_helper_funcs rockchip_dp_encoder_helper_funcs = {
	.mode_fixup = rockchip_dp_drm_encoder_mode_fixup,
	.mode_set = rockchip_dp_drm_encoder_mode_set,
	.atomic_enable = rockchip_dp_drm_encoder_enable,
	.atomic_disable = rockchip_dp_drm_encoder_disable,
	.atomic_check = rockchip_dp_drm_encoder_atomic_check,
};

static int rockchip_dp_of_probe(struct rockchip_dp_device *dp)
{
	struct device *dev = dp->dev;
	struct device_node *np = dev->of_node;

	if (of_property_read_bool(np, "rockchip,grf")) {
		dp->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
		if (IS_ERR(dp->grf)) {
			DRM_DEV_ERROR(dev, "failed to get rockchip,grf\n");
			return PTR_ERR(dp->grf);
		}
	}

	dp->rst = devm_reset_control_get(dev, "dp");
	if (IS_ERR(dp->rst)) {
		DRM_DEV_ERROR(dev, "failed to get dp reset control\n");
		return PTR_ERR(dp->rst);
	}

	dp->apb_reset = devm_reset_control_get_optional(dev, "apb");
	if (IS_ERR(dp->apb_reset)) {
		DRM_DEV_ERROR(dev, "failed to get apb reset control\n");
		return PTR_ERR(dp->apb_reset);
	}

	return 0;
}

static int rockchip_dp_drm_create_encoder(struct rockchip_dp_device *dp)
{
	struct drm_encoder *encoder = &dp->encoder;
	struct drm_device *drm_dev = dp->drm_dev;
	struct device *dev = dp->dev;
	int ret;

	encoder->possible_crtcs = rockchip_drm_of_find_possible_crtcs(drm_dev,
								      dev->of_node);
	DRM_DEBUG_KMS("possible_crtcs = 0x%x\n", encoder->possible_crtcs);

	ret = drm_simple_encoder_init(drm_dev, encoder,
				      DRM_MODE_ENCODER_TMDS);
	if (ret) {
		DRM_ERROR("failed to initialize encoder with drm\n");
		return ret;
	}

	drm_encoder_helper_add(encoder, &rockchip_dp_encoder_helper_funcs);

	return 0;
}

static int rockchip_dp_bind(struct device *dev, struct device *master,
			    void *data)
{
	struct rockchip_dp_device *dp = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	int ret;

	dp->drm_dev = drm_dev;

	if (!dp->plat_data.left) {
		ret = rockchip_dp_drm_create_encoder(dp);
		if (ret) {
			DRM_ERROR("failed to create drm encoder\n");
			return ret;
		}

		dp->plat_data.encoder = &dp->encoder;
	}

	if (dp->data->audio) {
		struct hdmi_codec_pdata codec_data = {
			.ops = &rockchip_dp_audio_codec_ops,
			.spdif = 1,
			.i2s = 1,
			.max_i2s_channels = 2,
		};

		dp->audio_pdev =
			platform_device_register_data(dev, HDMI_CODEC_DRV_NAME,
						      PLATFORM_DEVID_AUTO,
						      &codec_data,
						      sizeof(codec_data));
		if (IS_ERR(dp->audio_pdev)) {
			ret = PTR_ERR(dp->audio_pdev);
			goto err_cleanup_encoder;
		}
	}

	ret = analogix_dp_bind(dp->adp, drm_dev);
	if (ret)
		goto err_unregister_audio_pdev;

	return 0;

err_unregister_audio_pdev:
	if (dp->audio_pdev)
		platform_device_unregister(dp->audio_pdev);
err_cleanup_encoder:
	dp->encoder.funcs->destroy(&dp->encoder);
	return ret;
}

static void rockchip_dp_unbind(struct device *dev, struct device *master,
			       void *data)
{
	struct rockchip_dp_device *dp = dev_get_drvdata(dev);

	if (dp->audio_pdev)
		platform_device_unregister(dp->audio_pdev);
	analogix_dp_unbind(dp->adp);
	dp->encoder.funcs->destroy(&dp->encoder);
}

static const struct component_ops rockchip_dp_component_ops = {
	.bind = rockchip_dp_bind,
	.unbind = rockchip_dp_unbind,
};

static int rockchip_dp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct rockchip_dp_chip_data *dp_data;
	struct drm_panel *panel = NULL;
	struct drm_bridge *bridge = NULL;
	struct rockchip_dp_device *dp;
	int id, i, ret;

	dp_data = of_device_get_match_data(dev);
	if (!dp_data)
		return -ENODEV;

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, 0, &panel, &bridge);
	if (ret < 0 && ret != -ENODEV)
		return ret;

	dp = devm_kzalloc(dev, sizeof(*dp), GFP_KERNEL);
	if (!dp)
		return -ENOMEM;

	id = of_alias_get_id(dev->of_node, "edp");
	if (id < 0)
		id = 0;

	i = 0;
	while (is_rockchip(dp_data[i].chip_type))
		i++;

	if (id >= i) {
		dev_err(dev, "invalid id: %d\n", id);
		return -ENODEV;
	}

	dp->dev = dev;
	dp->id = id;
	dp->adp = ERR_PTR(-ENODEV);
	dp->data = &dp_data[id];
	dp->plat_data.ssc = dp->data->ssc;
	dp->plat_data.panel = panel;
	dp->plat_data.dev_type = dp->data->chip_type;
	dp->plat_data.power_on_start = rockchip_dp_poweron_start;
	dp->plat_data.power_off = rockchip_dp_powerdown;
	dp->plat_data.get_modes = rockchip_dp_get_modes;
	dp->plat_data.attach = rockchip_dp_bridge_attach;
	dp->plat_data.detach = rockchip_dp_bridge_detach;
	dp->plat_data.convert_to_split_mode = drm_mode_convert_to_split_mode;
	dp->plat_data.convert_to_origin_mode = drm_mode_convert_to_origin_mode;
	dp->plat_data.skip_connector = rockchip_dp_skip_connector(bridge);
	dp->plat_data.bridge = bridge;

	ret = rockchip_dp_of_probe(dp);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, dp);

	dp->adp = analogix_dp_probe(dev, &dp->plat_data);
	if (IS_ERR(dp->adp))
		return PTR_ERR(dp->adp);

	if (dp->data->split_mode && device_property_read_bool(dev, "split-mode")) {
		struct rockchip_dp_device *secondary =
				rockchip_dp_find_by_id(dev->driver, !dp->id);
		if (!secondary) {
			ret = -EPROBE_DEFER;
			goto err_dp_remove;
		}

		dp->plat_data.right = secondary->adp;
		dp->plat_data.split_mode = true;
		secondary->plat_data.left = dp->adp;
		secondary->plat_data.split_mode = true;
	}

	device_property_read_u32(dev, "min-refresh-rate", &dp->min_refresh_rate);
	device_property_read_u32(dev, "max-refresh-rate", &dp->max_refresh_rate);

	if (dp->data->split_mode && device_property_read_bool(dev, "dual-connector-split")) {
		dp->plat_data.dual_connector_split = true;
		if (device_property_read_bool(dev, "left-display"))
			dp->plat_data.left_display = true;
	}

	ret = component_add(dev, &rockchip_dp_component_ops);
	if (ret)
		goto err_dp_remove;

	return 0;

err_dp_remove:
	analogix_dp_remove(dp->adp);
	return ret;
}

static int rockchip_dp_remove(struct platform_device *pdev)
{
	struct rockchip_dp_device *dp = platform_get_drvdata(pdev);

	component_del(&pdev->dev, &rockchip_dp_component_ops);
	analogix_dp_remove(dp->adp);

	return 0;
}

static __maybe_unused int rockchip_dp_suspend(struct device *dev)
{
	struct rockchip_dp_device *dp = dev_get_drvdata(dev);

	if (IS_ERR(dp->adp))
		return 0;

	return analogix_dp_suspend(dp->adp);
}

static __maybe_unused int rockchip_dp_resume(struct device *dev)
{
	struct rockchip_dp_device *dp = dev_get_drvdata(dev);

	if (IS_ERR(dp->adp))
		return 0;

	return analogix_dp_resume(dp->adp);
}

static __maybe_unused int rockchip_dp_runtime_suspend(struct device *dev)
{
	struct rockchip_dp_device *dp = dev_get_drvdata(dev);

	if (IS_ERR(dp->adp))
		return 0;

	return analogix_dp_runtime_suspend(dp->adp);
}

static __maybe_unused int rockchip_dp_runtime_resume(struct device *dev)
{
	struct rockchip_dp_device *dp = dev_get_drvdata(dev);

	if (IS_ERR(dp->adp))
		return 0;

	return analogix_dp_runtime_resume(dp->adp);
}

static const struct dev_pm_ops rockchip_dp_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(rockchip_dp_suspend, rockchip_dp_resume)
	SET_RUNTIME_PM_OPS(rockchip_dp_runtime_suspend,
			   rockchip_dp_runtime_resume, NULL)
};

static const struct rockchip_dp_chip_data rk3399_edp[] = {
	{
		.chip_type = RK3399_EDP,
		.lcdc_sel = GRF_REG_FIELD(0x6250, 5, 5),
		.ssc = true,
	},
	{ /* sentinel */ }
};

static const struct rockchip_dp_chip_data rk3288_dp[] = {
	{
		.chip_type = RK3288_DP,
		.lcdc_sel = GRF_REG_FIELD(0x025c, 5, 5),
		.ssc = true,
	},
	{ /* sentinel */ }
};

static const struct rockchip_dp_chip_data rk3568_edp[] = {
	{
		.chip_type = RK3568_EDP,
		.ssc = true,
		.audio = true,
	},
	{ /* sentinel */ }
};

static const struct rockchip_dp_chip_data rk3588_edp[] = {
	{
		.chip_type = RK3588_EDP,
		.spdif_sel = GRF_REG_FIELD(0x0000, 4, 4),
		.i2s_sel = GRF_REG_FIELD(0x0000, 3, 3),
		.edp_mode = GRF_REG_FIELD(0x0000, 0, 0),
		.ssc = true,
		.audio = true,
		.split_mode = true,
	},
	{
		.chip_type = RK3588_EDP,
		.spdif_sel = GRF_REG_FIELD(0x0004, 4, 4),
		.i2s_sel = GRF_REG_FIELD(0x0004, 3, 3),
		.edp_mode = GRF_REG_FIELD(0x0004, 0, 0),
		.ssc = true,
		.audio = true,
		.split_mode = true,
	},
	{ /* sentinel */ }
};

static const struct of_device_id rockchip_dp_dt_ids[] = {
	{.compatible = "rockchip,rk3288-dp", .data = &rk3288_dp },
	{.compatible = "rockchip,rk3399-edp", .data = &rk3399_edp },
	{.compatible = "rockchip,rk3568-edp", .data = &rk3568_edp },
	{.compatible = "rockchip,rk3588-edp", .data = &rk3588_edp },
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_dp_dt_ids);

struct platform_driver rockchip_dp_driver = {
	.probe = rockchip_dp_probe,
	.remove = rockchip_dp_remove,
	.driver = {
		   .name = "rockchip-dp",
		   .pm = &rockchip_dp_pm_ops,
		   .of_match_table = of_match_ptr(rockchip_dp_dt_ids),
	},
};
