// SPDX-License-Identifier: GPL-2.0
/*
 * rcar_lvds.c  --  R-Car LVDS Encoder
 *
 * Copyright (C) 2013-2018 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_panel.h>

#include "rcar_lvds_regs.h"

/* Keep in sync with the LVDCR0.LVMD hardware register values. */
enum rcar_lvds_mode {
	RCAR_LVDS_MODE_JEIDA = 0,
	RCAR_LVDS_MODE_MIRROR = 1,
	RCAR_LVDS_MODE_VESA = 4,
};

#define RCAR_LVDS_QUIRK_LANES	(1 << 0)	/* LVDS lanes 1 and 3 inverted */
#define RCAR_LVDS_QUIRK_GEN2_PLLCR (1 << 1)	/* LVDPLLCR has gen2 layout */
#define RCAR_LVDS_QUIRK_GEN3_LVEN (1 << 2)	/* LVEN bit needs to be set */
						/* on R8A77970/R8A7799x */

struct rcar_lvds_device_info {
	unsigned int gen;
	unsigned int quirks;
};

struct rcar_lvds {
	struct device *dev;
	const struct rcar_lvds_device_info *info;

	struct drm_bridge bridge;

	struct drm_bridge *next_bridge;
	struct drm_connector connector;
	struct drm_panel *panel;

	void __iomem *mmio;
	struct clk *clock;
	bool enabled;

	struct drm_display_mode display_mode;
	enum rcar_lvds_mode mode;
};

#define bridge_to_rcar_lvds(b) \
	container_of(b, struct rcar_lvds, bridge)

#define connector_to_rcar_lvds(c) \
	container_of(c, struct rcar_lvds, connector)

static void rcar_lvds_write(struct rcar_lvds *lvds, u32 reg, u32 data)
{
	iowrite32(data, lvds->mmio + reg);
}

/* -----------------------------------------------------------------------------
 * Connector & Panel
 */

static int rcar_lvds_connector_get_modes(struct drm_connector *connector)
{
	struct rcar_lvds *lvds = connector_to_rcar_lvds(connector);

	return drm_panel_get_modes(lvds->panel);
}

static int rcar_lvds_connector_atomic_check(struct drm_connector *connector,
					    struct drm_connector_state *state)
{
	struct rcar_lvds *lvds = connector_to_rcar_lvds(connector);
	const struct drm_display_mode *panel_mode;
	struct drm_crtc_state *crtc_state;

	if (!state->crtc)
		return 0;

	if (list_empty(&connector->modes)) {
		dev_dbg(lvds->dev, "connector: empty modes list\n");
		return -EINVAL;
	}

	panel_mode = list_first_entry(&connector->modes,
				      struct drm_display_mode, head);

	/* We're not allowed to modify the resolution. */
	crtc_state = drm_atomic_get_crtc_state(state->state, state->crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	if (crtc_state->mode.hdisplay != panel_mode->hdisplay ||
	    crtc_state->mode.vdisplay != panel_mode->vdisplay)
		return -EINVAL;

	/* The flat panel mode is fixed, just copy it to the adjusted mode. */
	drm_mode_copy(&crtc_state->adjusted_mode, panel_mode);

	return 0;
}

static const struct drm_connector_helper_funcs rcar_lvds_conn_helper_funcs = {
	.get_modes = rcar_lvds_connector_get_modes,
	.atomic_check = rcar_lvds_connector_atomic_check,
};

static const struct drm_connector_funcs rcar_lvds_conn_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

/* -----------------------------------------------------------------------------
 * Bridge
 */

static u32 rcar_lvds_lvdpllcr_gen2(unsigned int freq)
{
	if (freq < 39000)
		return LVDPLLCR_CEEN | LVDPLLCR_COSEL | LVDPLLCR_PLLDLYCNT_38M;
	else if (freq < 61000)
		return LVDPLLCR_CEEN | LVDPLLCR_COSEL | LVDPLLCR_PLLDLYCNT_60M;
	else if (freq < 121000)
		return LVDPLLCR_CEEN | LVDPLLCR_COSEL | LVDPLLCR_PLLDLYCNT_121M;
	else
		return LVDPLLCR_PLLDLYCNT_150M;
}

static u32 rcar_lvds_lvdpllcr_gen3(unsigned int freq)
{
	if (freq < 42000)
		return LVDPLLCR_PLLDIVCNT_42M;
	else if (freq < 85000)
		return LVDPLLCR_PLLDIVCNT_85M;
	else if (freq < 128000)
		return LVDPLLCR_PLLDIVCNT_128M;
	else
		return LVDPLLCR_PLLDIVCNT_148M;
}

static void rcar_lvds_enable(struct drm_bridge *bridge)
{
	struct rcar_lvds *lvds = bridge_to_rcar_lvds(bridge);
	const struct drm_display_mode *mode = &lvds->display_mode;
	/*
	 * FIXME: We should really retrieve the CRTC through the state, but how
	 * do we get a state pointer?
	 */
	struct drm_crtc *crtc = lvds->bridge.encoder->crtc;
	u32 lvdpllcr;
	u32 lvdhcr;
	u32 lvdcr0;
	int ret;

	WARN_ON(lvds->enabled);

	ret = clk_prepare_enable(lvds->clock);
	if (ret < 0)
		return;

	/*
	 * Hardcode the channels and control signals routing for now.
	 *
	 * HSYNC -> CTRL0
	 * VSYNC -> CTRL1
	 * DISP  -> CTRL2
	 * 0     -> CTRL3
	 */
	rcar_lvds_write(lvds, LVDCTRCR, LVDCTRCR_CTR3SEL_ZERO |
			LVDCTRCR_CTR2SEL_DISP | LVDCTRCR_CTR1SEL_VSYNC |
			LVDCTRCR_CTR0SEL_HSYNC);

	if (lvds->info->quirks & RCAR_LVDS_QUIRK_LANES)
		lvdhcr = LVDCHCR_CHSEL_CH(0, 0) | LVDCHCR_CHSEL_CH(1, 3)
		       | LVDCHCR_CHSEL_CH(2, 2) | LVDCHCR_CHSEL_CH(3, 1);
	else
		lvdhcr = LVDCHCR_CHSEL_CH(0, 0) | LVDCHCR_CHSEL_CH(1, 1)
		       | LVDCHCR_CHSEL_CH(2, 2) | LVDCHCR_CHSEL_CH(3, 3);

	rcar_lvds_write(lvds, LVDCHCR, lvdhcr);

	/* PLL clock configuration. */
	if (lvds->info->quirks & RCAR_LVDS_QUIRK_GEN2_PLLCR)
		lvdpllcr = rcar_lvds_lvdpllcr_gen2(mode->clock);
	else
		lvdpllcr = rcar_lvds_lvdpllcr_gen3(mode->clock);
	rcar_lvds_write(lvds, LVDPLLCR, lvdpllcr);

	/* Set the LVDS mode and select the input. */
	lvdcr0 = lvds->mode << LVDCR0_LVMD_SHIFT;
	if (drm_crtc_index(crtc) == 2)
		lvdcr0 |= LVDCR0_DUSEL;
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);

	/* Turn all the channels on. */
	rcar_lvds_write(lvds, LVDCR1,
			LVDCR1_CHSTBY(3) | LVDCR1_CHSTBY(2) |
			LVDCR1_CHSTBY(1) | LVDCR1_CHSTBY(0) | LVDCR1_CLKSTBY);

	if (lvds->info->gen < 3) {
		/* Enable LVDS operation and turn the bias circuitry on. */
		lvdcr0 |= LVDCR0_BEN | LVDCR0_LVEN;
		rcar_lvds_write(lvds, LVDCR0, lvdcr0);
	}

	/* Turn the PLL on. */
	lvdcr0 |= LVDCR0_PLLON;
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);

	if (lvds->info->gen > 2) {
		/* Set LVDS normal mode. */
		lvdcr0 |= LVDCR0_PWD;
		rcar_lvds_write(lvds, LVDCR0, lvdcr0);
	}

	if (lvds->info->quirks & RCAR_LVDS_QUIRK_GEN3_LVEN) {
		/* Turn on the LVDS PHY. */
		lvdcr0 |= LVDCR0_LVEN;
		rcar_lvds_write(lvds, LVDCR0, lvdcr0);
	}

	/* Wait for the startup delay. */
	usleep_range(100, 150);

	/* Turn the output on. */
	lvdcr0 |= LVDCR0_LVRES;
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);

	if (lvds->panel) {
		drm_panel_prepare(lvds->panel);
		drm_panel_enable(lvds->panel);
	}

	lvds->enabled = true;
}

static void rcar_lvds_disable(struct drm_bridge *bridge)
{
	struct rcar_lvds *lvds = bridge_to_rcar_lvds(bridge);

	WARN_ON(!lvds->enabled);

	if (lvds->panel) {
		drm_panel_disable(lvds->panel);
		drm_panel_unprepare(lvds->panel);
	}

	rcar_lvds_write(lvds, LVDCR0, 0);
	rcar_lvds_write(lvds, LVDCR1, 0);

	clk_disable_unprepare(lvds->clock);

	lvds->enabled = false;
}

static bool rcar_lvds_mode_fixup(struct drm_bridge *bridge,
				 const struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted_mode)
{
	/*
	 * The internal LVDS encoder has a restricted clock frequency operating
	 * range (31MHz to 148.5MHz). Clamp the clock accordingly.
	 */
	adjusted_mode->clock = clamp(adjusted_mode->clock, 31000, 148500);

	return true;
}

static void rcar_lvds_get_lvds_mode(struct rcar_lvds *lvds)
{
	struct drm_display_info *info = &lvds->connector.display_info;
	enum rcar_lvds_mode mode;

	/*
	 * There is no API yet to retrieve LVDS mode from a bridge, only panels
	 * are supported.
	 */
	if (!lvds->panel)
		return;

	if (!info->num_bus_formats || !info->bus_formats) {
		dev_err(lvds->dev, "no LVDS bus format reported\n");
		return;
	}

	switch (info->bus_formats[0]) {
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:
		mode = RCAR_LVDS_MODE_JEIDA;
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:
		mode = RCAR_LVDS_MODE_VESA;
		break;
	default:
		dev_err(lvds->dev, "unsupported LVDS bus format 0x%04x\n",
			info->bus_formats[0]);
		return;
	}

	if (info->bus_flags & DRM_BUS_FLAG_DATA_LSB_TO_MSB)
		mode |= RCAR_LVDS_MODE_MIRROR;

	lvds->mode = mode;
}

static void rcar_lvds_mode_set(struct drm_bridge *bridge,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode)
{
	struct rcar_lvds *lvds = bridge_to_rcar_lvds(bridge);

	WARN_ON(lvds->enabled);

	lvds->display_mode = *adjusted_mode;

	rcar_lvds_get_lvds_mode(lvds);
}

static int rcar_lvds_attach(struct drm_bridge *bridge)
{
	struct rcar_lvds *lvds = bridge_to_rcar_lvds(bridge);
	struct drm_connector *connector = &lvds->connector;
	struct drm_encoder *encoder = bridge->encoder;
	int ret;

	/* If we have a next bridge just attach it. */
	if (lvds->next_bridge)
		return drm_bridge_attach(bridge->encoder, lvds->next_bridge,
					 bridge);

	/* Otherwise we have a panel, create a connector. */
	ret = drm_connector_init(bridge->dev, connector, &rcar_lvds_conn_funcs,
				 DRM_MODE_CONNECTOR_LVDS);
	if (ret < 0)
		return ret;

	drm_connector_helper_add(connector, &rcar_lvds_conn_helper_funcs);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret < 0)
		return ret;

	return drm_panel_attach(lvds->panel, connector);
}

static void rcar_lvds_detach(struct drm_bridge *bridge)
{
	struct rcar_lvds *lvds = bridge_to_rcar_lvds(bridge);

	if (lvds->panel)
		drm_panel_detach(lvds->panel);
}

static const struct drm_bridge_funcs rcar_lvds_bridge_ops = {
	.attach = rcar_lvds_attach,
	.detach = rcar_lvds_detach,
	.enable = rcar_lvds_enable,
	.disable = rcar_lvds_disable,
	.mode_fixup = rcar_lvds_mode_fixup,
	.mode_set = rcar_lvds_mode_set,
};

/* -----------------------------------------------------------------------------
 * Probe & Remove
 */

static int rcar_lvds_parse_dt(struct rcar_lvds *lvds)
{
	struct device_node *local_output = NULL;
	struct device_node *remote_input = NULL;
	struct device_node *remote = NULL;
	struct device_node *node;
	bool is_bridge = false;
	int ret = 0;

	local_output = of_graph_get_endpoint_by_regs(lvds->dev->of_node, 1, 0);
	if (!local_output) {
		dev_dbg(lvds->dev, "unconnected port@1\n");
		return -ENODEV;
	}

	/*
	 * Locate the connected entity and infer its type from the number of
	 * endpoints.
	 */
	remote = of_graph_get_remote_port_parent(local_output);
	if (!remote) {
		dev_dbg(lvds->dev, "unconnected endpoint %pOF\n", local_output);
		ret = -ENODEV;
		goto done;
	}

	if (!of_device_is_available(remote)) {
		dev_dbg(lvds->dev, "connected entity %pOF is disabled\n",
			remote);
		ret = -ENODEV;
		goto done;
	}

	remote_input = of_graph_get_remote_endpoint(local_output);

	for_each_endpoint_of_node(remote, node) {
		if (node != remote_input) {
			/*
			 * We've found one endpoint other than the input, this
			 * must be a bridge.
			 */
			is_bridge = true;
			of_node_put(node);
			break;
		}
	}

	if (is_bridge) {
		lvds->next_bridge = of_drm_find_bridge(remote);
		if (!lvds->next_bridge)
			ret = -EPROBE_DEFER;
	} else {
		lvds->panel = of_drm_find_panel(remote);
		if (IS_ERR(lvds->panel))
			ret = PTR_ERR(lvds->panel);
	}

done:
	of_node_put(local_output);
	of_node_put(remote_input);
	of_node_put(remote);

	return ret;
}

static int rcar_lvds_probe(struct platform_device *pdev)
{
	struct rcar_lvds *lvds;
	struct resource *mem;
	int ret;

	lvds = devm_kzalloc(&pdev->dev, sizeof(*lvds), GFP_KERNEL);
	if (lvds == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, lvds);

	lvds->dev = &pdev->dev;
	lvds->info = of_device_get_match_data(&pdev->dev);
	lvds->enabled = false;

	ret = rcar_lvds_parse_dt(lvds);
	if (ret < 0)
		return ret;

	lvds->bridge.driver_private = lvds;
	lvds->bridge.funcs = &rcar_lvds_bridge_ops;
	lvds->bridge.of_node = pdev->dev.of_node;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lvds->mmio = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(lvds->mmio))
		return PTR_ERR(lvds->mmio);

	lvds->clock = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(lvds->clock)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(lvds->clock);
	}

	drm_bridge_add(&lvds->bridge);

	return 0;
}

static int rcar_lvds_remove(struct platform_device *pdev)
{
	struct rcar_lvds *lvds = platform_get_drvdata(pdev);

	drm_bridge_remove(&lvds->bridge);

	return 0;
}

static const struct rcar_lvds_device_info rcar_lvds_gen2_info = {
	.gen = 2,
	.quirks = RCAR_LVDS_QUIRK_GEN2_PLLCR,
};

static const struct rcar_lvds_device_info rcar_lvds_r8a7790_info = {
	.gen = 2,
	.quirks = RCAR_LVDS_QUIRK_GEN2_PLLCR | RCAR_LVDS_QUIRK_LANES,
};

static const struct rcar_lvds_device_info rcar_lvds_gen3_info = {
	.gen = 3,
};

static const struct rcar_lvds_device_info rcar_lvds_r8a77970_info = {
	.gen = 3,
	.quirks = RCAR_LVDS_QUIRK_GEN2_PLLCR | RCAR_LVDS_QUIRK_GEN3_LVEN,
};

static const struct of_device_id rcar_lvds_of_table[] = {
	{ .compatible = "renesas,r8a7743-lvds", .data = &rcar_lvds_gen2_info },
	{ .compatible = "renesas,r8a7790-lvds", .data = &rcar_lvds_r8a7790_info },
	{ .compatible = "renesas,r8a7791-lvds", .data = &rcar_lvds_gen2_info },
	{ .compatible = "renesas,r8a7793-lvds", .data = &rcar_lvds_gen2_info },
	{ .compatible = "renesas,r8a7795-lvds", .data = &rcar_lvds_gen3_info },
	{ .compatible = "renesas,r8a7796-lvds", .data = &rcar_lvds_gen3_info },
	{ .compatible = "renesas,r8a77970-lvds", .data = &rcar_lvds_r8a77970_info },
	{ }
};

MODULE_DEVICE_TABLE(of, rcar_lvds_of_table);

static struct platform_driver rcar_lvds_platform_driver = {
	.probe		= rcar_lvds_probe,
	.remove		= rcar_lvds_remove,
	.driver		= {
		.name	= "rcar-lvds",
		.of_match_table = rcar_lvds_of_table,
	},
};

module_platform_driver(rcar_lvds_platform_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Renesas R-Car LVDS Encoder Driver");
MODULE_LICENSE("GPL");
