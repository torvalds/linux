// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2022-2025 NXP
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/media-bus-format.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_bridge.h>

#define IMX93_DISPLAY_MUX_REG		0x60
#define PARALLEL_DISP_FORMAT		GENMASK(10, 8)
#define FORMAT_RGB888_TO_RGB888		FIELD_PREP(PARALLEL_DISP_FORMAT, 0)
#define FORMAT_RGB888_TO_RGB666		FIELD_PREP(PARALLEL_DISP_FORMAT, 1)
#define FORMAT_RGB565_TO_RGB565		FIELD_PREP(PARALLEL_DISP_FORMAT, 2)

struct imx93_pdfc {
	struct drm_bridge bridge;
	struct device *dev;
	struct regmap *regmap;
	u32 phy_bus_width;
};

static struct imx93_pdfc *bridge_to_imx93_pdfc(struct drm_bridge *bridge)
{
	return container_of(bridge, struct imx93_pdfc, bridge);
}

static int
imx93_pdfc_bridge_attach(struct drm_bridge *bridge, struct drm_encoder *encoder,
			 enum drm_bridge_attach_flags flags)
{
	return drm_bridge_attach(bridge->encoder, bridge->next_bridge, bridge, flags);
}

static void imx93_pdfc_bridge_atomic_enable(struct drm_bridge *bridge,
					    struct drm_atomic_commit *state)
{
	struct imx93_pdfc *pdfc = bridge_to_imx93_pdfc(bridge);
	const struct drm_bridge_state *bridge_state;
	unsigned int mask = PARALLEL_DISP_FORMAT;
	unsigned int val;

	bridge_state = drm_atomic_get_new_bridge_state(state, bridge);

	switch (bridge_state->output_bus_cfg.format) {
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_FIXED:
		val = FORMAT_RGB888_TO_RGB888;
		if (pdfc->phy_bus_width == 18) {
			/*
			 * Can be valid if physical bus limitation exists,
			 * therefore use dev_dbg().
			 */
			dev_dbg(pdfc->dev, "Truncate two LSBs from each color\n");
			val = FORMAT_RGB888_TO_RGB666;
		}
		break;
	case MEDIA_BUS_FMT_RGB666_1X18:
		val = FORMAT_RGB888_TO_RGB666;
		break;
	case MEDIA_BUS_FMT_RGB565_1X16:
		val = FORMAT_RGB565_TO_RGB565;
		break;
	}

	regmap_update_bits(pdfc->regmap, IMX93_DISPLAY_MUX_REG, mask, val);
}

/* TODO: Add YUV formats */
static const u32 imx93_pdfc_bus_output_fmts[] = {
	MEDIA_BUS_FMT_FIXED,
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_RGB666_1X18,
	MEDIA_BUS_FMT_RGB565_1X16,
};

static bool imx93_pdfc_bus_output_fmt_supported(u32 fmt)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(imx93_pdfc_bus_output_fmts); i++) {
		if (imx93_pdfc_bus_output_fmts[i] == fmt)
			return true;
	}

	return false;
}

static u32 *
imx93_pdfc_bridge_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
					    struct drm_bridge_state *bridge_state,
					    struct drm_crtc_state *crtc_state,
					    struct drm_connector_state *conn_state,
					    u32 output_fmt,
					    unsigned int *num_input_fmts)
{
	u32 *input_fmts;

	*num_input_fmts = 0;

	if (!imx93_pdfc_bus_output_fmt_supported(output_fmt))
		return NULL;

	input_fmts = kmalloc_obj(*input_fmts);
	if (!input_fmts)
		return NULL;

	*num_input_fmts = 1;

	switch (output_fmt) {
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB565_1X16:
		input_fmts[0] = output_fmt;
		break;
	case MEDIA_BUS_FMT_RGB666_1X18:
	case MEDIA_BUS_FMT_FIXED:
		input_fmts[0] = MEDIA_BUS_FMT_RGB888_1X24;
		break;
	}

	return input_fmts;
}

static int imx93_pdfc_bridge_atomic_check(struct drm_bridge *bridge,
					  struct drm_bridge_state *bridge_state,
					  struct drm_crtc_state *crtc_state,
					  struct drm_connector_state *conn_state)
{
	struct imx93_pdfc *pdfc = bridge_to_imx93_pdfc(bridge);
	u32 format = bridge_state->output_bus_cfg.format;

	if (imx93_pdfc_bus_output_fmt_supported(format))
		return 0;

	dev_warn(pdfc->dev, "Unsupported output bus format: 0x%x\n", format);

	return -EINVAL;
}

static const struct drm_bridge_funcs funcs = {
	.attach			= imx93_pdfc_bridge_attach,
	.atomic_enable		= imx93_pdfc_bridge_atomic_enable,
	.atomic_duplicate_state	= drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_bridge_destroy_state,
	.atomic_get_input_bus_fmts	= imx93_pdfc_bridge_atomic_get_input_bus_fmts,
	.atomic_check		= imx93_pdfc_bridge_atomic_check,
	.atomic_create_state	= drm_atomic_helper_bridge_create_state,
};

static int imx93_pdfc_bridge_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct drm_bridge *next_bridge;
	struct imx93_pdfc *pdfc;
	struct device_node *ep;
	int err;

	pdfc = devm_drm_bridge_alloc(dev, struct imx93_pdfc, bridge, &funcs);
	if (IS_ERR(pdfc))
		return PTR_ERR(pdfc);

	pdfc->regmap = syscon_node_to_regmap(dev->of_node->parent);
	if (IS_ERR(pdfc->regmap))
		return dev_err_probe(dev, PTR_ERR(pdfc->regmap),
				     "failed to get regmap\n");

	/* No limits per default */
	pdfc->phy_bus_width = 24;

	/* Get output ep (port1/endpoint) */
	ep = of_graph_get_endpoint_by_regs(dev->of_node, 1, -1);
	if (ep) {
		err = of_property_read_u32(ep, "bus-width", &pdfc->phy_bus_width);
		of_node_put(ep);

		/* bus-width is optional but it must have valid data if present */
		if (err && err != -EINVAL)
			return dev_err_probe(dev, err,
					     "failed to query bus-width\n");
	}

	next_bridge = devm_drm_of_get_bridge(dev, dev->of_node, 1, 0);
	if (IS_ERR(next_bridge))
		return dev_err_probe(dev, PTR_ERR(next_bridge),
				     "failed to get next bridge\n");
	pdfc->dev = dev;
	pdfc->bridge.of_node = dev->of_node;
	pdfc->bridge.type = DRM_MODE_CONNECTOR_DPI;
	pdfc->bridge.next_bridge = drm_bridge_get(next_bridge);

	return devm_drm_bridge_add(dev, &pdfc->bridge);
}

static const struct of_device_id imx93_pdfc_dt_ids[] = {
	{ .compatible = "nxp,imx91-pdfc", },
	{ .compatible = "nxp,imx93-pdfc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx93_pdfc_dt_ids);

static struct platform_driver imx93_pdfc_bridge_driver = {
	.probe	= imx93_pdfc_bridge_probe,
	.driver	= {
		.of_match_table = imx93_pdfc_dt_ids,
		.name = "imx93_pdfc",
	},
};
module_platform_driver(imx93_pdfc_bridge_driver);

MODULE_DESCRIPTION("NXP i.MX93 parallel display format configuration driver");
MODULE_AUTHOR("Liu Ying <victor.liu@nxp.com>");
MODULE_LICENSE("GPL");
