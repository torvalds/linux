// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2020,2022 NXP
 */

#include <linux/firmware/imx/svc/misc.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_print.h>

#include <dt-bindings/firmware/imx/rsrc.h>

#define DRIVER_NAME		"imx8qxp-display-pixel-link"
#define PL_MAX_MST_ADDR		3
#define PL_MAX_NEXT_BRIDGES	2

struct imx8qxp_pixel_link {
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct device *dev;
	struct imx_sc_ipc *ipc_handle;
	u8 stream_id;
	u8 dc_id;
	u32 sink_rsc;
	u32 mst_addr;
	u8 mst_addr_ctrl;
	u8 mst_en_ctrl;
	u8 mst_vld_ctrl;
	u8 sync_ctrl;
};

static void imx8qxp_pixel_link_enable_mst_en(struct imx8qxp_pixel_link *pl)
{
	int ret;

	ret = imx_sc_misc_set_control(pl->ipc_handle, pl->sink_rsc,
				      pl->mst_en_ctrl, true);
	if (ret)
		DRM_DEV_ERROR(pl->dev,
			      "failed to enable DC%u stream%u pixel link mst_en: %d\n",
			      pl->dc_id, pl->stream_id, ret);
}

static void imx8qxp_pixel_link_enable_mst_vld(struct imx8qxp_pixel_link *pl)
{
	int ret;

	ret = imx_sc_misc_set_control(pl->ipc_handle, pl->sink_rsc,
				      pl->mst_vld_ctrl, true);
	if (ret)
		DRM_DEV_ERROR(pl->dev,
			      "failed to enable DC%u stream%u pixel link mst_vld: %d\n",
			      pl->dc_id, pl->stream_id, ret);
}

static void imx8qxp_pixel_link_enable_sync(struct imx8qxp_pixel_link *pl)
{
	int ret;

	ret = imx_sc_misc_set_control(pl->ipc_handle, pl->sink_rsc,
				      pl->sync_ctrl, true);
	if (ret)
		DRM_DEV_ERROR(pl->dev,
			      "failed to enable DC%u stream%u pixel link sync: %d\n",
			      pl->dc_id, pl->stream_id, ret);
}

static int imx8qxp_pixel_link_disable_mst_en(struct imx8qxp_pixel_link *pl)
{
	int ret;

	ret = imx_sc_misc_set_control(pl->ipc_handle, pl->sink_rsc,
				      pl->mst_en_ctrl, false);
	if (ret)
		DRM_DEV_ERROR(pl->dev,
			      "failed to disable DC%u stream%u pixel link mst_en: %d\n",
			      pl->dc_id, pl->stream_id, ret);

	return ret;
}

static int imx8qxp_pixel_link_disable_mst_vld(struct imx8qxp_pixel_link *pl)
{
	int ret;

	ret = imx_sc_misc_set_control(pl->ipc_handle, pl->sink_rsc,
				      pl->mst_vld_ctrl, false);
	if (ret)
		DRM_DEV_ERROR(pl->dev,
			      "failed to disable DC%u stream%u pixel link mst_vld: %d\n",
			      pl->dc_id, pl->stream_id, ret);

	return ret;
}

static int imx8qxp_pixel_link_disable_sync(struct imx8qxp_pixel_link *pl)
{
	int ret;

	ret = imx_sc_misc_set_control(pl->ipc_handle, pl->sink_rsc,
				      pl->sync_ctrl, false);
	if (ret)
		DRM_DEV_ERROR(pl->dev,
			      "failed to disable DC%u stream%u pixel link sync: %d\n",
			      pl->dc_id, pl->stream_id, ret);

	return ret;
}

static void imx8qxp_pixel_link_set_mst_addr(struct imx8qxp_pixel_link *pl)
{
	int ret;

	ret = imx_sc_misc_set_control(pl->ipc_handle,
				      pl->sink_rsc, pl->mst_addr_ctrl,
				      pl->mst_addr);
	if (ret)
		DRM_DEV_ERROR(pl->dev,
			      "failed to set DC%u stream%u pixel link mst addr(%u): %d\n",
			      pl->dc_id, pl->stream_id, pl->mst_addr, ret);
}

static int imx8qxp_pixel_link_bridge_attach(struct drm_bridge *bridge,
					    enum drm_bridge_attach_flags flags)
{
	struct imx8qxp_pixel_link *pl = bridge->driver_private;

	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)) {
		DRM_DEV_ERROR(pl->dev,
			      "do not support creating a drm_connector\n");
		return -EINVAL;
	}

	if (!bridge->encoder) {
		DRM_DEV_ERROR(pl->dev, "missing encoder\n");
		return -ENODEV;
	}

	return drm_bridge_attach(bridge->encoder,
				 pl->next_bridge, bridge,
				 DRM_BRIDGE_ATTACH_NO_CONNECTOR);
}

static void
imx8qxp_pixel_link_bridge_mode_set(struct drm_bridge *bridge,
				   const struct drm_display_mode *mode,
				   const struct drm_display_mode *adjusted_mode)
{
	struct imx8qxp_pixel_link *pl = bridge->driver_private;

	imx8qxp_pixel_link_set_mst_addr(pl);
}

static void
imx8qxp_pixel_link_bridge_atomic_enable(struct drm_bridge *bridge,
					struct drm_bridge_state *old_bridge_state)
{
	struct imx8qxp_pixel_link *pl = bridge->driver_private;

	imx8qxp_pixel_link_enable_mst_en(pl);
	imx8qxp_pixel_link_enable_mst_vld(pl);
	imx8qxp_pixel_link_enable_sync(pl);
}

static void
imx8qxp_pixel_link_bridge_atomic_disable(struct drm_bridge *bridge,
					 struct drm_bridge_state *old_bridge_state)
{
	struct imx8qxp_pixel_link *pl = bridge->driver_private;

	imx8qxp_pixel_link_disable_mst_en(pl);
	imx8qxp_pixel_link_disable_mst_vld(pl);
	imx8qxp_pixel_link_disable_sync(pl);
}

static const u32 imx8qxp_pixel_link_bus_output_fmts[] = {
	MEDIA_BUS_FMT_RGB888_1X36_CPADLO,
	MEDIA_BUS_FMT_RGB666_1X36_CPADLO,
};

static bool imx8qxp_pixel_link_bus_output_fmt_supported(u32 fmt)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(imx8qxp_pixel_link_bus_output_fmts); i++) {
		if (imx8qxp_pixel_link_bus_output_fmts[i] == fmt)
			return true;
	}

	return false;
}

static u32 *
imx8qxp_pixel_link_bridge_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
						    struct drm_bridge_state *bridge_state,
						    struct drm_crtc_state *crtc_state,
						    struct drm_connector_state *conn_state,
						    u32 output_fmt,
						    unsigned int *num_input_fmts)
{
	u32 *input_fmts;

	if (!imx8qxp_pixel_link_bus_output_fmt_supported(output_fmt))
		return NULL;

	*num_input_fmts = 1;

	input_fmts = kmalloc(sizeof(*input_fmts), GFP_KERNEL);
	if (!input_fmts)
		return NULL;

	input_fmts[0] = output_fmt;

	return input_fmts;
}

static u32 *
imx8qxp_pixel_link_bridge_atomic_get_output_bus_fmts(struct drm_bridge *bridge,
						     struct drm_bridge_state *bridge_state,
						     struct drm_crtc_state *crtc_state,
						     struct drm_connector_state *conn_state,
						     unsigned int *num_output_fmts)
{
	*num_output_fmts = ARRAY_SIZE(imx8qxp_pixel_link_bus_output_fmts);
	return kmemdup(imx8qxp_pixel_link_bus_output_fmts,
			sizeof(imx8qxp_pixel_link_bus_output_fmts), GFP_KERNEL);
}

static const struct drm_bridge_funcs imx8qxp_pixel_link_bridge_funcs = {
	.atomic_duplicate_state	= drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_bridge_destroy_state,
	.atomic_reset		= drm_atomic_helper_bridge_reset,
	.attach			= imx8qxp_pixel_link_bridge_attach,
	.mode_set		= imx8qxp_pixel_link_bridge_mode_set,
	.atomic_enable		= imx8qxp_pixel_link_bridge_atomic_enable,
	.atomic_disable		= imx8qxp_pixel_link_bridge_atomic_disable,
	.atomic_get_input_bus_fmts =
			imx8qxp_pixel_link_bridge_atomic_get_input_bus_fmts,
	.atomic_get_output_bus_fmts =
			imx8qxp_pixel_link_bridge_atomic_get_output_bus_fmts,
};

static int imx8qxp_pixel_link_disable_all_controls(struct imx8qxp_pixel_link *pl)
{
	int ret;

	ret = imx8qxp_pixel_link_disable_mst_en(pl);
	if (ret)
		return ret;

	ret = imx8qxp_pixel_link_disable_mst_vld(pl);
	if (ret)
		return ret;

	return imx8qxp_pixel_link_disable_sync(pl);
}

static struct drm_bridge *
imx8qxp_pixel_link_find_next_bridge(struct imx8qxp_pixel_link *pl)
{
	struct device_node *np = pl->dev->of_node;
	struct device_node *port, *remote;
	struct drm_bridge *next_bridge[PL_MAX_NEXT_BRIDGES];
	u32 port_id;
	bool found_port = false;
	int reg, ep_cnt = 0;
	/* select the first next bridge by default */
	int bridge_sel = 0;

	for (port_id = 1; port_id <= PL_MAX_MST_ADDR + 1; port_id++) {
		port = of_graph_get_port_by_id(np, port_id);
		if (!port)
			continue;

		if (of_device_is_available(port)) {
			found_port = true;
			of_node_put(port);
			break;
		}

		of_node_put(port);
	}

	if (!found_port) {
		DRM_DEV_ERROR(pl->dev, "no available output port\n");
		return ERR_PTR(-ENODEV);
	}

	for (reg = 0; reg < PL_MAX_NEXT_BRIDGES; reg++) {
		remote = of_graph_get_remote_node(np, port_id, reg);
		if (!remote)
			continue;

		if (!of_device_is_available(remote->parent)) {
			DRM_DEV_DEBUG(pl->dev,
				      "port%u endpoint%u remote parent is not available\n",
				      port_id, reg);
			of_node_put(remote);
			continue;
		}

		next_bridge[ep_cnt] = of_drm_find_bridge(remote);
		if (!next_bridge[ep_cnt]) {
			of_node_put(remote);
			return ERR_PTR(-EPROBE_DEFER);
		}

		/* specially select the next bridge with companion PXL2DPI */
		if (of_property_present(remote, "fsl,companion-pxl2dpi"))
			bridge_sel = ep_cnt;

		ep_cnt++;

		of_node_put(remote);
	}

	pl->mst_addr = port_id - 1;

	return next_bridge[bridge_sel];
}

static int imx8qxp_pixel_link_bridge_probe(struct platform_device *pdev)
{
	struct imx8qxp_pixel_link *pl;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int ret;

	pl = devm_kzalloc(dev, sizeof(*pl), GFP_KERNEL);
	if (!pl)
		return -ENOMEM;

	ret = imx_scu_get_handle(&pl->ipc_handle);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev, "failed to get SCU ipc handle: %d\n",
				      ret);
		return ret;
	}

	ret = of_property_read_u8(np, "fsl,dc-id", &pl->dc_id);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to get DC index: %d\n", ret);
		return ret;
	}

	ret = of_property_read_u8(np, "fsl,dc-stream-id", &pl->stream_id);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to get DC stream index: %d\n", ret);
		return ret;
	}

	pl->dev = dev;

	pl->sink_rsc = pl->dc_id ? IMX_SC_R_DC_1 : IMX_SC_R_DC_0;

	if (pl->stream_id == 0) {
		pl->mst_addr_ctrl = IMX_SC_C_PXL_LINK_MST1_ADDR;
		pl->mst_en_ctrl   = IMX_SC_C_PXL_LINK_MST1_ENB;
		pl->mst_vld_ctrl  = IMX_SC_C_PXL_LINK_MST1_VLD;
		pl->sync_ctrl     = IMX_SC_C_SYNC_CTRL0;
	} else {
		pl->mst_addr_ctrl = IMX_SC_C_PXL_LINK_MST2_ADDR;
		pl->mst_en_ctrl   = IMX_SC_C_PXL_LINK_MST2_ENB;
		pl->mst_vld_ctrl  = IMX_SC_C_PXL_LINK_MST2_VLD;
		pl->sync_ctrl     = IMX_SC_C_SYNC_CTRL1;
	}

	/* disable all controls to POR default */
	ret = imx8qxp_pixel_link_disable_all_controls(pl);
	if (ret)
		return ret;

	pl->next_bridge = imx8qxp_pixel_link_find_next_bridge(pl);
	if (IS_ERR(pl->next_bridge)) {
		ret = PTR_ERR(pl->next_bridge);
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev, "failed to find next bridge: %d\n",
				      ret);
		return ret;
	}

	platform_set_drvdata(pdev, pl);

	pl->bridge.driver_private = pl;
	pl->bridge.funcs = &imx8qxp_pixel_link_bridge_funcs;
	pl->bridge.of_node = np;

	drm_bridge_add(&pl->bridge);

	return ret;
}

static void imx8qxp_pixel_link_bridge_remove(struct platform_device *pdev)
{
	struct imx8qxp_pixel_link *pl = platform_get_drvdata(pdev);

	drm_bridge_remove(&pl->bridge);
}

static const struct of_device_id imx8qxp_pixel_link_dt_ids[] = {
	{ .compatible = "fsl,imx8qm-dc-pixel-link", },
	{ .compatible = "fsl,imx8qxp-dc-pixel-link", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx8qxp_pixel_link_dt_ids);

static struct platform_driver imx8qxp_pixel_link_bridge_driver = {
	.probe	= imx8qxp_pixel_link_bridge_probe,
	.remove_new = imx8qxp_pixel_link_bridge_remove,
	.driver	= {
		.of_match_table = imx8qxp_pixel_link_dt_ids,
		.name = DRIVER_NAME,
	},
};
module_platform_driver(imx8qxp_pixel_link_bridge_driver);

MODULE_DESCRIPTION("i.MX8QXP/QM display pixel link bridge driver");
MODULE_AUTHOR("Liu Ying <victor.liu@nxp.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
