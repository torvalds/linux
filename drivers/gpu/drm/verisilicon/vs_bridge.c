// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Icenowy Zheng <uwu@icenowy.me>
 */

#include <linux/of.h>
#include <linux/regmap.h>

#include <uapi/linux/media-bus-format.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_simple_kms_helper.h>

#include "vs_bridge.h"
#include "vs_bridge_regs.h"
#include "vs_crtc.h"
#include "vs_dc.h"

static int vs_bridge_attach(struct drm_bridge *bridge,
			    struct drm_encoder *encoder,
			    enum drm_bridge_attach_flags flags)
{
	struct vs_bridge *vbridge = drm_bridge_to_vs_bridge(bridge);

	return drm_bridge_attach(encoder, vbridge->next_bridge,
				 bridge, flags);
}

struct vsdc_dp_format {
	u32 linux_fmt;
	bool is_yuv;
	u32 vsdc_fmt;
};

static struct vsdc_dp_format vsdc_dp_supported_fmts[] = {
	/* default to RGB888 */
	{ MEDIA_BUS_FMT_FIXED, false, VSDC_DISP_DP_CONFIG_FMT_RGB888 },
	{ MEDIA_BUS_FMT_RGB888_1X24, false, VSDC_DISP_DP_CONFIG_FMT_RGB888 },
	{ MEDIA_BUS_FMT_RGB565_1X16, false, VSDC_DISP_DP_CONFIG_FMT_RGB565 },
	{ MEDIA_BUS_FMT_RGB666_1X18, false, VSDC_DISP_DP_CONFIG_FMT_RGB666 },
	{ MEDIA_BUS_FMT_RGB101010_1X30,
	  false, VSDC_DISP_DP_CONFIG_FMT_RGB101010 },
	{ MEDIA_BUS_FMT_UYVY8_1X16, true, VSDC_DISP_DP_CONFIG_YUV_FMT_UYVY8 },
	{ MEDIA_BUS_FMT_UYVY10_1X20, true, VSDC_DISP_DP_CONFIG_YUV_FMT_UYVY10 },
	{ MEDIA_BUS_FMT_YUV8_1X24, true, VSDC_DISP_DP_CONFIG_YUV_FMT_YUV8 },
	{ MEDIA_BUS_FMT_YUV10_1X30, true, VSDC_DISP_DP_CONFIG_YUV_FMT_YUV10 },
	{ MEDIA_BUS_FMT_UYYVYY8_0_5X24,
	  true, VSDC_DISP_DP_CONFIG_YUV_FMT_UYYVYY8 },
	{ MEDIA_BUS_FMT_UYYVYY10_0_5X30,
	  true, VSDC_DISP_DP_CONFIG_YUV_FMT_UYYVYY10 },
};

static u32 *vs_bridge_atomic_get_output_bus_fmts_dpi(struct drm_bridge *bridge,
					struct drm_bridge_state *bridge_state,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state,
					unsigned int *num_output_fmts)
{
	u32 *output_fmts;

	*num_output_fmts = 2;

	output_fmts = kcalloc(*num_output_fmts, sizeof(*output_fmts),
			      GFP_KERNEL);
	if (!output_fmts)
		return NULL;

	/* TODO: support more DPI output formats */
	output_fmts[0] = MEDIA_BUS_FMT_RGB888_1X24;
	output_fmts[1] = MEDIA_BUS_FMT_FIXED;

	return output_fmts;
}

static u32 *vs_bridge_atomic_get_output_bus_fmts_dp(struct drm_bridge *bridge,
					struct drm_bridge_state *bridge_state,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state,
					unsigned int *num_output_fmts)
{
	u32 *output_fmts;
	unsigned int i;

	*num_output_fmts = ARRAY_SIZE(vsdc_dp_supported_fmts);

	output_fmts = kcalloc(*num_output_fmts, sizeof(*output_fmts),
			      GFP_KERNEL);
	if (!output_fmts)
		return NULL;

	for (i = 0; i < *num_output_fmts; i++)
		output_fmts[i] = vsdc_dp_supported_fmts[i].linux_fmt;

	return output_fmts;
}

static bool vs_bridge_out_dp_fmt_supported(u32 out_fmt)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(vsdc_dp_supported_fmts); i++)
		if (vsdc_dp_supported_fmts[i].linux_fmt == out_fmt)
			return true;

	return false;
}

static u32 *vs_bridge_atomic_get_input_bus_fmts_dp(struct drm_bridge *bridge,
					struct drm_bridge_state *bridge_state,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state,
					u32 output_fmt,
					unsigned int *num_input_fmts)
{
	if (!vs_bridge_out_dp_fmt_supported(output_fmt)) {
		*num_input_fmts = 0;
		return NULL;
	}

	return drm_atomic_helper_bridge_propagate_bus_fmt(bridge, bridge_state,
							  crtc_state,
							  conn_state,
							  output_fmt,
							  num_input_fmts);
}

static int vs_bridge_atomic_check_dp(struct drm_bridge *bridge,
				     struct drm_bridge_state *bridge_state,
				     struct drm_crtc_state *crtc_state,
				     struct drm_connector_state *conn_state)
{
	if (!vs_bridge_out_dp_fmt_supported(bridge_state->output_bus_cfg.format))
		return -EINVAL;

	return 0;
}

static void vs_bridge_enable_common(struct vs_crtc *crtc,
				    struct drm_bridge_state *br_state)
{
	struct vs_dc *dc = crtc->dc;
	unsigned int output = crtc->id;

	regmap_clear_bits(dc->regs, VSDC_DISP_PANEL_CONFIG(output),
			  VSDC_DISP_PANEL_CONFIG_DAT_POL);
	regmap_assign_bits(dc->regs, VSDC_DISP_PANEL_CONFIG(output),
			   VSDC_DISP_PANEL_CONFIG_DE_POL,
			   br_state->output_bus_cfg.flags &
			   DRM_BUS_FLAG_DE_LOW);
	regmap_assign_bits(dc->regs, VSDC_DISP_PANEL_CONFIG(output),
			   VSDC_DISP_PANEL_CONFIG_CLK_POL,
			   br_state->output_bus_cfg.flags &
			   DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE);
	regmap_set_bits(dc->regs, VSDC_DISP_PANEL_CONFIG(output),
			VSDC_DISP_PANEL_CONFIG_DE_EN |
			VSDC_DISP_PANEL_CONFIG_DAT_EN |
			VSDC_DISP_PANEL_CONFIG_CLK_EN);
	regmap_set_bits(dc->regs, VSDC_DISP_PANEL_CONFIG(output),
			VSDC_DISP_PANEL_CONFIG_RUNNING);
	regmap_clear_bits(dc->regs, VSDC_DISP_PANEL_START,
			  VSDC_DISP_PANEL_START_MULTI_DISP_SYNC);
	regmap_set_bits(dc->regs, VSDC_DISP_PANEL_START,
			VSDC_DISP_PANEL_START_RUNNING(output));

	regmap_set_bits(dc->regs, VSDC_DISP_PANEL_CONFIG_EX(crtc->id),
			VSDC_DISP_PANEL_CONFIG_EX_COMMIT);
}

static void vs_bridge_atomic_enable_dpi(struct drm_bridge *bridge,
					struct drm_atomic_state *state)
{
	struct vs_bridge *vbridge = drm_bridge_to_vs_bridge(bridge);
	struct drm_bridge_state *br_state =
		drm_atomic_get_new_bridge_state(state, bridge);
	struct vs_crtc *crtc = vbridge->crtc;
	struct vs_dc *dc = crtc->dc;
	unsigned int output = crtc->id;

	regmap_clear_bits(dc->regs, VSDC_DISP_DP_CONFIG(output),
			  VSDC_DISP_DP_CONFIG_DP_EN);
	regmap_write(dc->regs, VSDC_DISP_DPI_CONFIG(output),
		     VSDC_DISP_DPI_CONFIG_FMT_RGB888);

	vs_bridge_enable_common(crtc, br_state);
}

static void vs_bridge_atomic_enable_dp(struct drm_bridge *bridge,
					struct drm_atomic_state *state)
{
	struct vs_bridge *vbridge = drm_bridge_to_vs_bridge(bridge);
	struct drm_bridge_state *br_state =
		drm_atomic_get_new_bridge_state(state, bridge);
	struct vs_crtc *crtc = vbridge->crtc;
	struct vs_dc *dc = crtc->dc;
	unsigned int output = crtc->id;
	u32 dp_fmt;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(vsdc_dp_supported_fmts); i++) {
		if (vsdc_dp_supported_fmts[i].linux_fmt ==
		    br_state->output_bus_cfg.format)
			break;
	}
	if (WARN_ON_ONCE(i == ARRAY_SIZE(vsdc_dp_supported_fmts)))
		return;
	dp_fmt = vsdc_dp_supported_fmts[i].vsdc_fmt;
	dp_fmt |= VSDC_DISP_DP_CONFIG_DP_EN;
	regmap_write(dc->regs, VSDC_DISP_DP_CONFIG(output), dp_fmt);
	regmap_assign_bits(dc->regs, VSDC_DISP_PANEL_CONFIG(output),
			   VSDC_DISP_PANEL_CONFIG_YUV,
			   vsdc_dp_supported_fmts[i].is_yuv);

	vs_bridge_enable_common(crtc, br_state);
}

static void vs_bridge_atomic_disable(struct drm_bridge *bridge,
				     struct drm_atomic_state *state)
{
	struct vs_bridge *vbridge = drm_bridge_to_vs_bridge(bridge);
	struct vs_crtc *crtc = vbridge->crtc;
	struct vs_dc *dc = crtc->dc;
	unsigned int output = crtc->id;

	regmap_clear_bits(dc->regs, VSDC_DISP_PANEL_START,
			  VSDC_DISP_PANEL_START_MULTI_DISP_SYNC |
			  VSDC_DISP_PANEL_START_RUNNING(output));
	regmap_clear_bits(dc->regs, VSDC_DISP_PANEL_CONFIG(output),
			  VSDC_DISP_PANEL_CONFIG_RUNNING);

	regmap_set_bits(dc->regs, VSDC_DISP_PANEL_CONFIG_EX(crtc->id),
			VSDC_DISP_PANEL_CONFIG_EX_COMMIT);
}

static const struct drm_bridge_funcs vs_dpi_bridge_funcs = {
	.attach = vs_bridge_attach,
	.atomic_enable = vs_bridge_atomic_enable_dpi,
	.atomic_disable = vs_bridge_atomic_disable,
	.atomic_get_input_bus_fmts = drm_atomic_helper_bridge_propagate_bus_fmt,
	.atomic_get_output_bus_fmts = vs_bridge_atomic_get_output_bus_fmts_dpi,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
};

static const struct drm_bridge_funcs vs_dp_bridge_funcs = {
	.attach = vs_bridge_attach,
	.atomic_enable = vs_bridge_atomic_enable_dp,
	.atomic_disable = vs_bridge_atomic_disable,
	.atomic_check = vs_bridge_atomic_check_dp,
	.atomic_get_input_bus_fmts = vs_bridge_atomic_get_input_bus_fmts_dp,
	.atomic_get_output_bus_fmts = vs_bridge_atomic_get_output_bus_fmts_dp,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
};

static int vs_bridge_detect_output_interface(struct device_node *of_node,
					     unsigned int output)
{
	int ret;
	struct device_node *remote;

	remote = of_graph_get_remote_node(of_node, output,
					  VSDC_OUTPUT_INTERFACE_DPI);
	if (remote) {
		ret = VSDC_OUTPUT_INTERFACE_DPI;
	} else {
		remote = of_graph_get_remote_node(of_node, output,
						  VSDC_OUTPUT_INTERFACE_DP);
		if (remote)
			ret = VSDC_OUTPUT_INTERFACE_DP;
		else
			ret = -ENODEV;
	}

	if (remote)
		of_node_put(remote);

	return ret;
}

struct vs_bridge *vs_bridge_init(struct drm_device *drm_dev,
				 struct vs_crtc *crtc)
{
	unsigned int output = crtc->id;
	struct vs_bridge *bridge;
	struct drm_bridge *next;
	enum vs_bridge_output_interface intf;
	const struct drm_bridge_funcs *bridge_funcs;
	int ret, enctype;

	intf = vs_bridge_detect_output_interface(drm_dev->dev->of_node,
						 output);
	if (intf == -ENODEV) {
		drm_dbg(drm_dev, "Skipping output %u\n", output);
		return NULL;
	}

	next = devm_drm_of_get_bridge(drm_dev->dev, drm_dev->dev->of_node,
				      output, intf);
	if (IS_ERR(next)) {
		ret = PTR_ERR(next);
		if (ret != -EPROBE_DEFER)
			drm_err(drm_dev,
				"Cannot get downstream bridge of output %u\n",
				output);
		return ERR_PTR(ret);
	}

	if (intf == VSDC_OUTPUT_INTERFACE_DPI)
		bridge_funcs = &vs_dpi_bridge_funcs;
	else
		bridge_funcs = &vs_dp_bridge_funcs;

	bridge = devm_drm_bridge_alloc(drm_dev->dev, struct vs_bridge, base,
				       bridge_funcs);
	if (IS_ERR(bridge))
		return ERR_PTR(PTR_ERR(bridge));

	bridge->crtc = crtc;
	bridge->intf = intf;
	bridge->next_bridge = next;

	if (intf == VSDC_OUTPUT_INTERFACE_DPI)
		enctype = DRM_MODE_ENCODER_DPI;
	else
		enctype = DRM_MODE_ENCODER_NONE;

	bridge->enc = drmm_plain_encoder_alloc(drm_dev, NULL, enctype, NULL);
	if (IS_ERR(bridge->enc)) {
		drm_err(drm_dev,
			"Cannot initialize encoder for output %u\n", output);
		ret = PTR_ERR(bridge->enc);
		return ERR_PTR(ret);
	}

	bridge->enc->possible_crtcs = drm_crtc_mask(&crtc->base);

	ret = devm_drm_bridge_add(drm_dev->dev, &bridge->base);
	if (ret) {
		drm_err(drm_dev,
			"Cannot add bridge for output %u\n", output);
		return ERR_PTR(ret);
	}

	ret = drm_bridge_attach(bridge->enc, &bridge->base, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret) {
		drm_err(drm_dev,
			"Cannot attach bridge for output %u\n", output);
		return ERR_PTR(ret);
	}

	bridge->conn = drm_bridge_connector_init(drm_dev, bridge->enc);
	if (IS_ERR(bridge->conn)) {
		drm_err(drm_dev,
			"Cannot create connector for output %u\n", output);
		ret = PTR_ERR(bridge->conn);
		return ERR_PTR(ret);
	}
	drm_connector_attach_encoder(bridge->conn, bridge->enc);

	return bridge;
}
