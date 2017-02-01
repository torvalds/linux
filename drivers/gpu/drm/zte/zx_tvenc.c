/*
 * Copyright 2017 Linaro Ltd.
 * Copyright 2017 ZTE Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drmP.h>

#include "zx_drm_drv.h"
#include "zx_tvenc_regs.h"
#include "zx_vou.h"

struct zx_tvenc_pwrctrl {
	struct regmap *regmap;
	u32 reg;
	u32 mask;
};

struct zx_tvenc {
	struct drm_connector connector;
	struct drm_encoder encoder;
	struct device *dev;
	void __iomem *mmio;
	const struct vou_inf *inf;
	struct zx_tvenc_pwrctrl pwrctrl;
};

#define to_zx_tvenc(x) container_of(x, struct zx_tvenc, x)

struct zx_tvenc_mode {
	struct drm_display_mode mode;
	u32 video_info;
	u32 video_res;
	u32 field1_param;
	u32 field2_param;
	u32 burst_line_odd1;
	u32 burst_line_even1;
	u32 burst_line_odd2;
	u32 burst_line_even2;
	u32 line_timing_param;
	u32 weight_value;
	u32 blank_black_level;
	u32 burst_level;
	u32 control_param;
	u32 sub_carrier_phase1;
	u32 phase_line_incr_cvbs;
};

/*
 * The CRM cannot directly provide a suitable frequency, and we have to
 * ask a multiplied rate from CRM and use the divider in VOU to get the
 * desired one.
 */
#define TVENC_CLOCK_MULTIPLIER	4

static const struct zx_tvenc_mode tvenc_mode_pal = {
	.mode = {
		.clock = 13500 * TVENC_CLOCK_MULTIPLIER,
		.hdisplay = 720,
		.hsync_start = 720 + 12,
		.hsync_end = 720 + 12 + 2,
		.htotal = 720 + 12 + 2 + 130,
		.vdisplay = 576,
		.vsync_start = 576 + 2,
		.vsync_end = 576 + 2 + 2,
		.vtotal = 576 + 2 + 2 + 20,
		.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC |
			 DRM_MODE_FLAG_INTERLACE,
	},
	.video_info = 0x00040040,
	.video_res = 0x05a9c760,
	.field1_param = 0x0004d416,
	.field2_param = 0x0009b94f,
	.burst_line_odd1 = 0x0004d406,
	.burst_line_even1 = 0x0009b53e,
	.burst_line_odd2 = 0x0004d805,
	.burst_line_even2 = 0x0009b93f,
	.line_timing_param = 0x06a96fdf,
	.weight_value = 0x00c188a0,
	.blank_black_level = 0x0000fcfc,
	.burst_level = 0x00001595,
	.control_param = 0x00000001,
	.sub_carrier_phase1 = 0x1504c566,
	.phase_line_incr_cvbs = 0xc068db8c,
};

static const struct zx_tvenc_mode tvenc_mode_ntsc = {
	.mode = {
		.clock = 13500 * TVENC_CLOCK_MULTIPLIER,
		.hdisplay = 720,
		.hsync_start = 720 + 16,
		.hsync_end = 720 + 16 + 2,
		.htotal = 720 + 16 + 2 + 120,
		.vdisplay = 480,
		.vsync_start = 480 + 3,
		.vsync_end = 480 + 3 + 2,
		.vtotal = 480 + 3 + 2 + 17,
		.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC |
			 DRM_MODE_FLAG_INTERLACE,
	},
	.video_info = 0x00040080,
	.video_res = 0x05a8375a,
	.field1_param = 0x00041817,
	.field2_param = 0x0008351e,
	.burst_line_odd1 = 0x00041006,
	.burst_line_even1 = 0x0008290d,
	.burst_line_odd2 = 0x00000000,
	.burst_line_even2 = 0x00000000,
	.line_timing_param = 0x06a8ef9e,
	.weight_value = 0x00b68197,
	.blank_black_level = 0x0000f0f0,
	.burst_level = 0x0000009c,
	.control_param = 0x00000001,
	.sub_carrier_phase1 = 0x10f83e10,
	.phase_line_incr_cvbs = 0x80000000,
};

static const struct zx_tvenc_mode *tvenc_modes[] = {
	&tvenc_mode_pal,
	&tvenc_mode_ntsc,
};

static const struct zx_tvenc_mode *
zx_tvenc_find_zmode(struct drm_display_mode *mode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tvenc_modes); i++) {
		const struct zx_tvenc_mode *zmode = tvenc_modes[i];

		if (drm_mode_equal(mode, &zmode->mode))
			return zmode;
	}

	return NULL;
}

static void zx_tvenc_encoder_mode_set(struct drm_encoder *encoder,
				      struct drm_display_mode *mode,
				      struct drm_display_mode *adj_mode)
{
	struct zx_tvenc *tvenc = to_zx_tvenc(encoder);
	const struct zx_tvenc_mode *zmode;
	struct vou_div_config configs[] = {
		{ VOU_DIV_INF,   VOU_DIV_4 },
		{ VOU_DIV_TVENC, VOU_DIV_1 },
		{ VOU_DIV_LAYER, VOU_DIV_2 },
	};

	zx_vou_config_dividers(encoder->crtc, configs, ARRAY_SIZE(configs));

	zmode = zx_tvenc_find_zmode(mode);
	if (!zmode) {
		DRM_DEV_ERROR(tvenc->dev, "failed to find zmode\n");
		return;
	}

	zx_writel(tvenc->mmio + VENC_VIDEO_INFO, zmode->video_info);
	zx_writel(tvenc->mmio + VENC_VIDEO_RES, zmode->video_res);
	zx_writel(tvenc->mmio + VENC_FIELD1_PARAM, zmode->field1_param);
	zx_writel(tvenc->mmio + VENC_FIELD2_PARAM, zmode->field2_param);
	zx_writel(tvenc->mmio + VENC_LINE_O_1, zmode->burst_line_odd1);
	zx_writel(tvenc->mmio + VENC_LINE_E_1, zmode->burst_line_even1);
	zx_writel(tvenc->mmio + VENC_LINE_O_2, zmode->burst_line_odd2);
	zx_writel(tvenc->mmio + VENC_LINE_E_2, zmode->burst_line_even2);
	zx_writel(tvenc->mmio + VENC_LINE_TIMING_PARAM,
		  zmode->line_timing_param);
	zx_writel(tvenc->mmio + VENC_WEIGHT_VALUE, zmode->weight_value);
	zx_writel(tvenc->mmio + VENC_BLANK_BLACK_LEVEL,
		  zmode->blank_black_level);
	zx_writel(tvenc->mmio + VENC_BURST_LEVEL, zmode->burst_level);
	zx_writel(tvenc->mmio + VENC_CONTROL_PARAM, zmode->control_param);
	zx_writel(tvenc->mmio + VENC_SUB_CARRIER_PHASE1,
		  zmode->sub_carrier_phase1);
	zx_writel(tvenc->mmio + VENC_PHASE_LINE_INCR_CVBS,
		  zmode->phase_line_incr_cvbs);
}

static void zx_tvenc_encoder_enable(struct drm_encoder *encoder)
{
	struct zx_tvenc *tvenc = to_zx_tvenc(encoder);
	struct zx_tvenc_pwrctrl *pwrctrl = &tvenc->pwrctrl;

	/* Set bit to power up TVENC DAC */
	regmap_update_bits(pwrctrl->regmap, pwrctrl->reg, pwrctrl->mask,
			   pwrctrl->mask);

	vou_inf_enable(VOU_TV_ENC, encoder->crtc);

	zx_writel(tvenc->mmio + VENC_ENABLE, 1);
}

static void zx_tvenc_encoder_disable(struct drm_encoder *encoder)
{
	struct zx_tvenc *tvenc = to_zx_tvenc(encoder);
	struct zx_tvenc_pwrctrl *pwrctrl = &tvenc->pwrctrl;

	zx_writel(tvenc->mmio + VENC_ENABLE, 0);

	vou_inf_disable(VOU_TV_ENC, encoder->crtc);

	/* Clear bit to power down TVENC DAC */
	regmap_update_bits(pwrctrl->regmap, pwrctrl->reg, pwrctrl->mask, 0);
}

static const struct drm_encoder_helper_funcs zx_tvenc_encoder_helper_funcs = {
	.enable	= zx_tvenc_encoder_enable,
	.disable = zx_tvenc_encoder_disable,
	.mode_set = zx_tvenc_encoder_mode_set,
};

static const struct drm_encoder_funcs zx_tvenc_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int zx_tvenc_connector_get_modes(struct drm_connector *connector)
{
	struct zx_tvenc *tvenc = to_zx_tvenc(connector);
	struct device *dev = tvenc->dev;
	int i;

	for (i = 0; i < ARRAY_SIZE(tvenc_modes); i++) {
		const struct zx_tvenc_mode *zmode = tvenc_modes[i];
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(connector->dev, &zmode->mode);
		if (!mode) {
			DRM_DEV_ERROR(dev, "failed to duplicate drm mode\n");
			continue;
		}

		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);
	}

	return i;
}

static enum drm_mode_status
zx_tvenc_connector_mode_valid(struct drm_connector *connector,
			      struct drm_display_mode *mode)
{
	struct zx_tvenc *tvenc = to_zx_tvenc(connector);
	const struct zx_tvenc_mode *zmode;

	zmode = zx_tvenc_find_zmode(mode);
	if (!zmode) {
		DRM_DEV_ERROR(tvenc->dev, "unsupported mode: %s\n", mode->name);
		return MODE_NOMODE;
	}

	return MODE_OK;
}

static struct drm_connector_helper_funcs zx_tvenc_connector_helper_funcs = {
	.get_modes = zx_tvenc_connector_get_modes,
	.mode_valid = zx_tvenc_connector_mode_valid,
};

static const struct drm_connector_funcs zx_tvenc_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int zx_tvenc_register(struct drm_device *drm, struct zx_tvenc *tvenc)
{
	struct drm_encoder *encoder = &tvenc->encoder;
	struct drm_connector *connector = &tvenc->connector;

	/*
	 * The tvenc is designed to use aux channel, as there is a deflicker
	 * block for the channel.
	 */
	encoder->possible_crtcs = BIT(1);

	drm_encoder_init(drm, encoder, &zx_tvenc_encoder_funcs,
			 DRM_MODE_ENCODER_TVDAC, NULL);
	drm_encoder_helper_add(encoder, &zx_tvenc_encoder_helper_funcs);

	connector->interlace_allowed = true;

	drm_connector_init(drm, connector, &zx_tvenc_connector_funcs,
			   DRM_MODE_CONNECTOR_Composite);
	drm_connector_helper_add(connector, &zx_tvenc_connector_helper_funcs);

	drm_mode_connector_attach_encoder(connector, encoder);

	return 0;
}

static int zx_tvenc_pwrctrl_init(struct zx_tvenc *tvenc)
{
	struct zx_tvenc_pwrctrl *pwrctrl = &tvenc->pwrctrl;
	struct device *dev = tvenc->dev;
	struct of_phandle_args out_args;
	struct regmap *regmap;
	int ret;

	ret = of_parse_phandle_with_fixed_args(dev->of_node,
				"zte,tvenc-power-control", 2, 0, &out_args);
	if (ret)
		return ret;

	regmap = syscon_node_to_regmap(out_args.np);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		goto out;
	}

	pwrctrl->regmap = regmap;
	pwrctrl->reg = out_args.args[0];
	pwrctrl->mask = out_args.args[1];

out:
	of_node_put(out_args.np);
	return ret;
}

static int zx_tvenc_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct resource *res;
	struct zx_tvenc *tvenc;
	int ret;

	tvenc = devm_kzalloc(dev, sizeof(*tvenc), GFP_KERNEL);
	if (!tvenc)
		return -ENOMEM;

	tvenc->dev = dev;
	dev_set_drvdata(dev, tvenc);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tvenc->mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR(tvenc->mmio)) {
		ret = PTR_ERR(tvenc->mmio);
		DRM_DEV_ERROR(dev, "failed to remap tvenc region: %d\n", ret);
		return ret;
	}

	ret = zx_tvenc_pwrctrl_init(tvenc);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to init power control: %d\n", ret);
		return ret;
	}

	ret = zx_tvenc_register(drm, tvenc);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to register tvenc: %d\n", ret);
		return ret;
	}

	return 0;
}

static void zx_tvenc_unbind(struct device *dev, struct device *master,
			    void *data)
{
	/* Nothing to do */
}

static const struct component_ops zx_tvenc_component_ops = {
	.bind = zx_tvenc_bind,
	.unbind = zx_tvenc_unbind,
};

static int zx_tvenc_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &zx_tvenc_component_ops);
}

static int zx_tvenc_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &zx_tvenc_component_ops);
	return 0;
}

static const struct of_device_id zx_tvenc_of_match[] = {
	{ .compatible = "zte,zx296718-tvenc", },
	{ /* end */ },
};
MODULE_DEVICE_TABLE(of, zx_tvenc_of_match);

struct platform_driver zx_tvenc_driver = {
	.probe = zx_tvenc_probe,
	.remove = zx_tvenc_remove,
	.driver	= {
		.name = "zx-tvenc",
		.of_match_table	= zx_tvenc_of_match,
	},
};
