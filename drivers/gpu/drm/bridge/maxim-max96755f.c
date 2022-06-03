// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim max96755f GMSL2 Serializer with MIPI-DSI Input
 *
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_of.h>
#include <drm/drm_connector.h>
#include <drm/drm_probe_helper.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <drm/drm_mipi_dsi.h>
#include <linux/mfd/max96755f.h>

struct max96755f_bridge {
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct drm_connector connector;
	struct drm_panel *panel;

	struct device *dev;
	struct regmap *regmap;
	struct mipi_dsi_device *dsi;
	struct device_node *dsi_node;
	struct drm_display_mode mode;
	u32 num_lanes;
	bool dv_swp_ab;
	bool dpi_deskew_en;
	bool split_mode;
};

#define to_max96755f_bridge(x)	container_of(x, struct max96755f_bridge, x)

static int max96755f_bridge_connector_get_modes(struct drm_connector *connector)
{
	struct max96755f_bridge *ser = to_max96755f_bridge(connector);

	if (ser->next_bridge)
		return drm_bridge_get_modes(ser->next_bridge, connector);

	return drm_panel_get_modes(ser->panel, connector);
}

static const struct drm_connector_helper_funcs
max96755f_bridge_connector_helper_funcs = {
	.get_modes = max96755f_bridge_connector_get_modes,
};

static enum drm_connector_status
max96755f_bridge_connector_detect(struct drm_connector *connector, bool force)
{
	struct max96755f_bridge *ser = to_max96755f_bridge(connector);

	return drm_bridge_detect(&ser->bridge);
}

static const struct drm_connector_funcs max96755f_bridge_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = max96755f_bridge_connector_detect,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static struct mipi_dsi_device *max96755f_attach_dsi(struct max96755f_bridge *max96755f,
						    struct device_node *dsi_node)
{
	const struct mipi_dsi_device_info info = { "max96755f", 0, NULL };
	struct mipi_dsi_device *dsi;
	struct mipi_dsi_host *host;
	int ret;

	host = of_find_mipi_dsi_host_by_node(dsi_node);
	if (!host) {
		dev_err(max96755f->dev, "failed to find dsi host\n");
		return ERR_PTR(-EPROBE_DEFER);
	}

	dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(dsi)) {
		dev_err(max96755f->dev, "failed to create dsi device\n");
		return dsi;
	}

	dsi->lanes = max96755f->num_lanes;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(max96755f->dev, "failed to attach dsi to host\n");
		mipi_dsi_device_unregister(dsi);
		return ERR_PTR(ret);
	}

	return dsi;
}

static int max96755f_bridge_attach(struct drm_bridge *bridge,
				   enum drm_bridge_attach_flags flags)
{
	struct max96755f_bridge *ser = to_max96755f_bridge(bridge);
	struct drm_connector *connector = &ser->connector;
	int ret;

	ret = drm_of_find_panel_or_bridge(bridge->of_node, 1, -1, &ser->panel,
					  &ser->next_bridge);
	if (ret)
		return ret;

	if (ser->next_bridge) {
		ret = drm_bridge_attach(bridge->encoder, ser->next_bridge,
					bridge, DRM_BRIDGE_ATTACH_NO_CONNECTOR);
		if (ret)
			return ret;
	}

	connector->polled = DRM_CONNECTOR_POLL_CONNECT |
			    DRM_CONNECTOR_POLL_DISCONNECT;

	drm_connector_helper_add(connector,
				 &max96755f_bridge_connector_helper_funcs);

	ret = drm_connector_init(bridge->dev, connector,
				 &max96755f_bridge_connector_funcs,
				 ser->next_bridge ? ser->next_bridge->type : bridge->type);
	if (ret) {
		DRM_ERROR("Failed to initialize connector\n");
		return ret;
	}

	drm_connector_attach_encoder(connector, bridge->encoder);

	ser->dsi = max96755f_attach_dsi(ser, ser->dsi_node);
	if (IS_ERR(ser->dsi))
		return PTR_ERR(ser->dsi);

	return 0;
}

static void max96755f_bridge_detach(struct drm_bridge *bridge)
{
	struct max96755f_bridge *ser = to_max96755f_bridge(bridge);

	if (ser->dsi) {
		mipi_dsi_detach(ser->dsi);
		mipi_dsi_device_unregister(ser->dsi);
	}
}

static void max96755f_mipi_dsi_rx_config(struct max96755f_bridge *ser)
{
	struct drm_display_mode *mode = &ser->mode;
	u32 hfp, hsa, hbp, hact;
	u32 vact, vsa, vfp, vbp;

	regmap_update_bits(ser->regmap, 0x330, MIPI_RX_RESET,
			   FIELD_PREP(MIPI_RX_RESET, 1));
	mdelay(10);
	regmap_update_bits(ser->regmap, 0x330, MIPI_RX_RESET,
			   FIELD_PREP(MIPI_RX_RESET, 0));
	mdelay(10);

	regmap_update_bits(ser->regmap, 0x331, NUM_LANES,
			   FIELD_PREP(NUM_LANES, ser->num_lanes - 1));

	if (!ser->dpi_deskew_en)
		return;

	vact = mode->vdisplay;
	vsa = mode->vsync_end - mode->vsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;
	hact = mode->hdisplay;
	hsa = mode->hsync_end - mode->hsync_start;
	hfp = mode->hsync_start - mode->hdisplay;
	hbp = mode->htotal - mode->hsync_end;

	regmap_write(ser->regmap, 0x03A4, 0xc1);
	regmap_write(ser->regmap, 0x0385, FIELD_PREP(DPI_HSYNC_WIDTH_L, hsa));
	regmap_write(ser->regmap, 0x0386, FIELD_PREP(DPI_VYSNC_WIDTH_L, vsa));
	regmap_write(ser->regmap, 0x0387,
		     FIELD_PREP(DPI_VSYNC_WIDTH_H, (vsa >> 8)) |
		     FIELD_PREP(DPI_HSYNC_WIDTH_H, (hsa >> 8)));
	regmap_write(ser->regmap, 0x03a5, FIELD_PREP(DPI_VFP_L, vfp));
	regmap_write(ser->regmap, 0x03a6,
		     FIELD_PREP(DPI_VBP_L, vbp) |
		     FIELD_PREP(DPI_VFP_H, (vfp >> 8)));
	regmap_write(ser->regmap, 0x03a7, FIELD_PREP(DPI_VBP_H, (vbp >> 4)));
	regmap_write(ser->regmap, 0x03a8, FIELD_PREP(DPI_VACT_L, vact));
	regmap_write(ser->regmap, 0x03a9, FIELD_PREP(DPI_VACT_H, (vact >> 8)));
	regmap_write(ser->regmap, 0x03aa, FIELD_PREP(DPI_HFP_L, hfp));
	regmap_write(ser->regmap, 0x03ab,
		     FIELD_PREP(DPI_HBP_L, hbp) |
		     FIELD_PREP(DPI_HFP_H, (hfp >> 7)));
	regmap_write(ser->regmap, 0x03ac, FIELD_PREP(DPI_HBP_H, (hbp >> 4)));
	regmap_write(ser->regmap, 0x03ad, FIELD_PREP(DPI_HACT_L, hact));
	regmap_write(ser->regmap, 0x03ae, FIELD_PREP(DPI_HACT_H, (hact >> 8)));
}

static void max96755f_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct max96755f_bridge *ser = to_max96755f_bridge(bridge);

	max96755f_mipi_dsi_rx_config(ser);

	if (ser->split_mode) {
		regmap_update_bits(ser->regmap, 0x0053,
				   TX_SPLIT_MASK_B | TX_SPLIT_MASK_A | TX_STR_SEL,
				   FIELD_PREP(TX_SPLIT_MASK_B, 0) |
				   FIELD_PREP(TX_SPLIT_MASK_A, 1) |
				   FIELD_PREP(TX_STR_SEL, 0));
		regmap_update_bits(ser->regmap, 0x0057,
				   TX_SPLIT_MASK_B | TX_SPLIT_MASK_A | TX_STR_SEL,
				   FIELD_PREP(TX_SPLIT_MASK_B, 1) |
				   FIELD_PREP(TX_SPLIT_MASK_A, 0) |
				   FIELD_PREP(TX_STR_SEL, 1));
		regmap_update_bits(ser->regmap, 0x032a,
				   DV_SWP_AB | DV_CONV | DV_SPL | DV_EN,
				   FIELD_PREP(DV_SWP_AB, ser->dv_swp_ab) |
				   FIELD_PREP(DV_CONV, 1) |
				   FIELD_PREP(DV_SPL, 1) |
				   FIELD_PREP(DV_EN, 1));
	}

	if (ser->panel)
		drm_panel_prepare(ser->panel);
}

static void max96755f_bridge_enable(struct drm_bridge *bridge)
{
	struct max96755f_bridge *ser = to_max96755f_bridge(bridge);
	u32 val;
	int ret;

	if (ser->split_mode) {
		regmap_update_bits(ser->regmap, 0x0311,
				   START_PORTAX | START_PORTAY,
				   FIELD_PREP(START_PORTAX, 1) |
				   FIELD_PREP(START_PORTAY, 1));
		regmap_update_bits(ser->regmap, 0x0002,
				   VID_TX_EN_X | VID_TX_EN_Y,
				   FIELD_PREP(VID_TX_EN_X, 1) |
				   FIELD_PREP(VID_TX_EN_Y, 1));

		regmap_update_bits(ser->regmap, 0x0010,
				   AUTO_LINK | LINK_CFG,
				   FIELD_PREP(AUTO_LINK, 0) |
				   FIELD_PREP(LINK_CFG, SPLITTER_MODE));
		ret = regmap_read_poll_timeout(ser->regmap, 0x0013, val,
					       val & LOCKED, 100,
					       50 * USEC_PER_MSEC);
		if (ret < 0)
			dev_err(ser->dev, "GMSL2 link lock timeout\n");
	} else {
		regmap_update_bits(ser->regmap, 0x0311,
				   START_PORTAX | START_PORTAY,
				   FIELD_PREP(START_PORTAX, 1) |
				   FIELD_PREP(START_PORTAY, 1));
		regmap_update_bits(ser->regmap, 0x02, VID_TX_EN_X,
				   FIELD_PREP(VID_TX_EN_X, 1));
	}

	regmap_update_bits(ser->regmap, 0x10, RESET_ONESHOT,
			   FIELD_PREP(RESET_ONESHOT, 1));
	mdelay(100);

	if (ser->panel)
		drm_panel_enable(ser->panel);
}

static void max96755f_bridge_disable(struct drm_bridge *bridge)
{
	struct max96755f_bridge *ser = to_max96755f_bridge(bridge);

	if (ser->panel)
		drm_panel_disable(ser->panel);

	regmap_update_bits(ser->regmap, 0x02, VID_TX_EN_X | VID_TX_EN_Y,
			   FIELD_PREP(VID_TX_EN_X, 0) |
			   FIELD_PREP(VID_TX_EN_Y, 0));

	if (ser->split_mode)
		regmap_update_bits(ser->regmap, 0x0010,
				   AUTO_LINK | LINK_CFG,
				   FIELD_PREP(AUTO_LINK, 1) |
				   FIELD_PREP(LINK_CFG, LINKA));
}

static void max96755f_bridge_post_disable(struct drm_bridge *bridge)
{
	struct max96755f_bridge *ser = to_max96755f_bridge(bridge);

	if (ser->panel)
		drm_panel_unprepare(ser->panel);
}

static enum drm_connector_status
max96755f_bridge_detect(struct drm_bridge *bridge)
{
	struct max96755f_bridge *ser = to_max96755f_bridge(bridge);
	u32 val;

	if (regmap_read(ser->regmap, 0x0013, &val))
		return connector_status_disconnected;

	if (!FIELD_GET(LOCKED, val))
		return connector_status_disconnected;

	return connector_status_connected;
}

static void max96755f_bridge_mode_set(struct drm_bridge *bridge,
				      const struct drm_display_mode *mode,
				      const struct drm_display_mode *adj_mode)
{
	struct max96755f_bridge *ser = to_max96755f_bridge(bridge);

	drm_mode_copy(&ser->mode, adj_mode);
}

static const struct drm_bridge_funcs max96755f_bridge_funcs = {
	.attach = max96755f_bridge_attach,
	.detach = max96755f_bridge_detach,
	.detect = max96755f_bridge_detect,
	.pre_enable = max96755f_bridge_pre_enable,
	.enable = max96755f_bridge_enable,
	.disable = max96755f_bridge_disable,
	.post_disable = max96755f_bridge_post_disable,
	.mode_set = max96755f_bridge_mode_set,
	.atomic_get_input_bus_fmts = drm_atomic_helper_bridge_propagate_bus_fmt,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
};

static int max96755f_link_parse(struct max96755f_bridge *ser)
{
	struct device *dev = ser->dev;
	struct device *parent = dev->parent;
	struct device_node *child;
	u32 val;
	int ret = 0;
	unsigned int nr = 0;

	ser->dpi_deskew_en = of_property_read_bool(dev->of_node, "dpi-deskew-en");
	ser->dv_swp_ab = of_property_read_bool(dev->of_node, "vd-swap-ab");

	if (!of_property_read_u32(dev->of_node, "dsi,lanes", &val))
		ser->num_lanes = val;

	for_each_available_child_of_node(parent->of_node, child) {
		if (!of_find_property(child, "reg", NULL))
			continue;

		nr++;
	}

	switch (nr) {
	case 2:
		ser->split_mode = true;
	case 1:
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int max96755f_bridge_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct max96755f_bridge *ser;
	int ret;

	ser = devm_kzalloc(dev, sizeof(*ser), GFP_KERNEL);
	if (!ser)
		return -ENOMEM;

	ser->dev = dev;
	platform_set_drvdata(pdev, ser);

	ser->regmap = dev_get_regmap(dev->parent, NULL);
	if (!ser->regmap)
		return dev_err_probe(dev, -ENODEV, "failed to get regmap\n");

	ser->dsi_node = of_graph_get_remote_node(dev->of_node, 0, -1);
	if (!ser->dsi_node) {
		dev_err(ser->dev, "failed to get remote node for primary dsi\n");
		return -ENODEV;
	}

	ret = max96755f_link_parse(ser);
	if (ret)
		dev_err_probe(dev, ret, "failed to parse link\n");

	ser->bridge.funcs = &max96755f_bridge_funcs;
	ser->bridge.of_node = dev->of_node;
	ser->bridge.ops = DRM_BRIDGE_OP_DETECT;
	ser->bridge.type = DRM_MODE_CONNECTOR_LVDS;

	drm_bridge_add(&ser->bridge);

	return 0;
}

static int max96755f_bridge_remove(struct platform_device *pdev)
{
	struct max96755f_bridge *ser = platform_get_drvdata(pdev);

	drm_bridge_remove(&ser->bridge);

	return 0;
}

static const struct of_device_id max96755f_bridge_of_match[] = {
	{ .compatible = "maxim,max96755f-bridge", },
	{}
};
MODULE_DEVICE_TABLE(of, max96755f_bridge_of_match);

static struct platform_driver max96755f_bridge_driver = {
	.driver = {
		.name = "max96755f-bridge",
		.of_match_table = of_match_ptr(max96755f_bridge_of_match),
	},
	.probe = max96755f_bridge_probe,
	.remove = max96755f_bridge_remove,
};

module_platform_driver(max96755f_bridge_driver);

MODULE_AUTHOR("Guochun Huang <hero.hunag@rock-chips.com>");
MODULE_DESCRIPTION("Maxim max96755f GMSL2 Serializer with MIPI-DSI Input");
MODULE_LICENSE("GPL");
