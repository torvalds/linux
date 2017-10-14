/*
 * Copyright (C) 2015 Free Electrons
 * Copyright (C) 2015 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>

#include <linux/component.h>
#include <linux/list.h>
#include <linux/of_graph.h>
#include <linux/reset.h>

#include "sun4i_backend.h"
#include "sun4i_drv.h"
#include "sun4i_layer.h"
#include "sunxi_engine.h"

static const u32 sunxi_rgb2yuv_coef[12] = {
	0x00000107, 0x00000204, 0x00000064, 0x00000108,
	0x00003f69, 0x00003ed6, 0x000001c1, 0x00000808,
	0x000001c1, 0x00003e88, 0x00003fb8, 0x00000808
};

static void sun4i_backend_apply_color_correction(struct sunxi_engine *engine)
{
	int i;

	DRM_DEBUG_DRIVER("Applying RGB to YUV color correction\n");

	/* Set color correction */
	regmap_write(engine->regs, SUN4I_BACKEND_OCCTL_REG,
		     SUN4I_BACKEND_OCCTL_ENABLE);

	for (i = 0; i < 12; i++)
		regmap_write(engine->regs, SUN4I_BACKEND_OCRCOEF_REG(i),
			     sunxi_rgb2yuv_coef[i]);
}

static void sun4i_backend_disable_color_correction(struct sunxi_engine *engine)
{
	DRM_DEBUG_DRIVER("Disabling color correction\n");

	/* Disable color correction */
	regmap_update_bits(engine->regs, SUN4I_BACKEND_OCCTL_REG,
			   SUN4I_BACKEND_OCCTL_ENABLE, 0);
}

static void sun4i_backend_commit(struct sunxi_engine *engine)
{
	DRM_DEBUG_DRIVER("Committing changes\n");

	regmap_write(engine->regs, SUN4I_BACKEND_REGBUFFCTL_REG,
		     SUN4I_BACKEND_REGBUFFCTL_AUTOLOAD_DIS |
		     SUN4I_BACKEND_REGBUFFCTL_LOADCTL);
}

void sun4i_backend_layer_enable(struct sun4i_backend *backend,
				int layer, bool enable)
{
	u32 val;

	DRM_DEBUG_DRIVER("%sabling layer %d\n", enable ? "En" : "Dis",
			 layer);

	if (enable)
		val = SUN4I_BACKEND_MODCTL_LAY_EN(layer);
	else
		val = 0;

	regmap_update_bits(backend->engine.regs, SUN4I_BACKEND_MODCTL_REG,
			   SUN4I_BACKEND_MODCTL_LAY_EN(layer), val);
}

static int sun4i_backend_drm_format_to_layer(struct drm_plane *plane,
					     u32 format, u32 *mode)
{
	if ((plane->type == DRM_PLANE_TYPE_PRIMARY) &&
	    (format == DRM_FORMAT_ARGB8888))
		format = DRM_FORMAT_XRGB8888;

	switch (format) {
	case DRM_FORMAT_ARGB8888:
		*mode = SUN4I_BACKEND_LAY_FBFMT_ARGB8888;
		break;

	case DRM_FORMAT_ARGB4444:
		*mode = SUN4I_BACKEND_LAY_FBFMT_ARGB4444;
		break;

	case DRM_FORMAT_ARGB1555:
		*mode = SUN4I_BACKEND_LAY_FBFMT_ARGB1555;
		break;

	case DRM_FORMAT_RGBA5551:
		*mode = SUN4I_BACKEND_LAY_FBFMT_RGBA5551;
		break;

	case DRM_FORMAT_RGBA4444:
		*mode = SUN4I_BACKEND_LAY_FBFMT_RGBA4444;
		break;

	case DRM_FORMAT_XRGB8888:
		*mode = SUN4I_BACKEND_LAY_FBFMT_XRGB8888;
		break;

	case DRM_FORMAT_RGB888:
		*mode = SUN4I_BACKEND_LAY_FBFMT_RGB888;
		break;

	case DRM_FORMAT_RGB565:
		*mode = SUN4I_BACKEND_LAY_FBFMT_RGB565;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

int sun4i_backend_update_layer_coord(struct sun4i_backend *backend,
				     int layer, struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;

	DRM_DEBUG_DRIVER("Updating layer %d\n", layer);

	if (plane->type == DRM_PLANE_TYPE_PRIMARY) {
		DRM_DEBUG_DRIVER("Primary layer, updating global size W: %u H: %u\n",
				 state->crtc_w, state->crtc_h);
		regmap_write(backend->engine.regs, SUN4I_BACKEND_DISSIZE_REG,
			     SUN4I_BACKEND_DISSIZE(state->crtc_w,
						   state->crtc_h));
	}

	/* Set the line width */
	DRM_DEBUG_DRIVER("Layer line width: %d bits\n", fb->pitches[0] * 8);
	regmap_write(backend->engine.regs,
		     SUN4I_BACKEND_LAYLINEWIDTH_REG(layer),
		     fb->pitches[0] * 8);

	/* Set height and width */
	DRM_DEBUG_DRIVER("Layer size W: %u H: %u\n",
			 state->crtc_w, state->crtc_h);
	regmap_write(backend->engine.regs, SUN4I_BACKEND_LAYSIZE_REG(layer),
		     SUN4I_BACKEND_LAYSIZE(state->crtc_w,
					   state->crtc_h));

	/* Set base coordinates */
	DRM_DEBUG_DRIVER("Layer coordinates X: %d Y: %d\n",
			 state->crtc_x, state->crtc_y);
	regmap_write(backend->engine.regs, SUN4I_BACKEND_LAYCOOR_REG(layer),
		     SUN4I_BACKEND_LAYCOOR(state->crtc_x,
					   state->crtc_y));

	return 0;
}

int sun4i_backend_update_layer_formats(struct sun4i_backend *backend,
				       int layer, struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	bool interlaced = false;
	u32 val;
	int ret;

	if (plane->state->crtc)
		interlaced = plane->state->crtc->state->adjusted_mode.flags
			& DRM_MODE_FLAG_INTERLACE;

	regmap_update_bits(backend->engine.regs, SUN4I_BACKEND_MODCTL_REG,
			   SUN4I_BACKEND_MODCTL_ITLMOD_EN,
			   interlaced ? SUN4I_BACKEND_MODCTL_ITLMOD_EN : 0);

	DRM_DEBUG_DRIVER("Switching display backend interlaced mode %s\n",
			 interlaced ? "on" : "off");

	ret = sun4i_backend_drm_format_to_layer(plane, fb->format->format,
						&val);
	if (ret) {
		DRM_DEBUG_DRIVER("Invalid format\n");
		return ret;
	}

	regmap_update_bits(backend->engine.regs,
			   SUN4I_BACKEND_ATTCTL_REG1(layer),
			   SUN4I_BACKEND_ATTCTL_REG1_LAY_FBFMT, val);

	return 0;
}

int sun4i_backend_update_layer_buffer(struct sun4i_backend *backend,
				      int layer, struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	u32 lo_paddr, hi_paddr;
	dma_addr_t paddr;

	/* Get the start of the displayed memory */
	paddr = drm_fb_cma_get_gem_addr(fb, state, 0);
	DRM_DEBUG_DRIVER("Setting buffer address to %pad\n", &paddr);

	/* Write the 32 lower bits of the address (in bits) */
	lo_paddr = paddr << 3;
	DRM_DEBUG_DRIVER("Setting address lower bits to 0x%x\n", lo_paddr);
	regmap_write(backend->engine.regs,
		     SUN4I_BACKEND_LAYFB_L32ADD_REG(layer),
		     lo_paddr);

	/* And the upper bits */
	hi_paddr = paddr >> 29;
	DRM_DEBUG_DRIVER("Setting address high bits to 0x%x\n", hi_paddr);
	regmap_update_bits(backend->engine.regs, SUN4I_BACKEND_LAYFB_H4ADD_REG,
			   SUN4I_BACKEND_LAYFB_H4ADD_MSK(layer),
			   SUN4I_BACKEND_LAYFB_H4ADD(layer, hi_paddr));

	return 0;
}

static int sun4i_backend_init_sat(struct device *dev) {
	struct sun4i_backend *backend = dev_get_drvdata(dev);
	int ret;

	backend->sat_reset = devm_reset_control_get(dev, "sat");
	if (IS_ERR(backend->sat_reset)) {
		dev_err(dev, "Couldn't get the SAT reset line\n");
		return PTR_ERR(backend->sat_reset);
	}

	ret = reset_control_deassert(backend->sat_reset);
	if (ret) {
		dev_err(dev, "Couldn't deassert the SAT reset line\n");
		return ret;
	}

	backend->sat_clk = devm_clk_get(dev, "sat");
	if (IS_ERR(backend->sat_clk)) {
		dev_err(dev, "Couldn't get our SAT clock\n");
		ret = PTR_ERR(backend->sat_clk);
		goto err_assert_reset;
	}

	ret = clk_prepare_enable(backend->sat_clk);
	if (ret) {
		dev_err(dev, "Couldn't enable the SAT clock\n");
		return ret;
	}

	return 0;

err_assert_reset:
	reset_control_assert(backend->sat_reset);
	return ret;
}

static int sun4i_backend_free_sat(struct device *dev) {
	struct sun4i_backend *backend = dev_get_drvdata(dev);

	clk_disable_unprepare(backend->sat_clk);
	reset_control_assert(backend->sat_reset);

	return 0;
}

/*
 * The display backend can take video output from the display frontend, or
 * the display enhancement unit on the A80, as input for one it its layers.
 * This relationship within the display pipeline is encoded in the device
 * tree with of_graph, and we use it here to figure out which backend, if
 * there are 2 or more, we are currently probing. The number would be in
 * the "reg" property of the upstream output port endpoint.
 */
static int sun4i_backend_of_get_id(struct device_node *node)
{
	struct device_node *port, *ep;
	int ret = -EINVAL;

	/* input is port 0 */
	port = of_graph_get_port_by_id(node, 0);
	if (!port)
		return -EINVAL;

	/* try finding an upstream endpoint */
	for_each_available_child_of_node(port, ep) {
		struct device_node *remote;
		u32 reg;

		remote = of_graph_get_remote_endpoint(ep);
		if (!remote)
			continue;

		ret = of_property_read_u32(remote, "reg", &reg);
		if (ret)
			continue;

		ret = reg;
	}

	of_node_put(port);

	return ret;
}

static const struct sunxi_engine_ops sun4i_backend_engine_ops = {
	.commit				= sun4i_backend_commit,
	.layers_init			= sun4i_layers_init,
	.apply_color_correction		= sun4i_backend_apply_color_correction,
	.disable_color_correction	= sun4i_backend_disable_color_correction,
};

static struct regmap_config sun4i_backend_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x5800,
};

static int sun4i_backend_bind(struct device *dev, struct device *master,
			      void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct sun4i_drv *drv = drm->dev_private;
	struct sun4i_backend *backend;
	struct resource *res;
	void __iomem *regs;
	int i, ret;

	backend = devm_kzalloc(dev, sizeof(*backend), GFP_KERNEL);
	if (!backend)
		return -ENOMEM;
	dev_set_drvdata(dev, backend);

	backend->engine.node = dev->of_node;
	backend->engine.ops = &sun4i_backend_engine_ops;
	backend->engine.id = sun4i_backend_of_get_id(dev->of_node);
	if (backend->engine.id < 0)
		return backend->engine.id;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	backend->reset = devm_reset_control_get(dev, NULL);
	if (IS_ERR(backend->reset)) {
		dev_err(dev, "Couldn't get our reset line\n");
		return PTR_ERR(backend->reset);
	}

	ret = reset_control_deassert(backend->reset);
	if (ret) {
		dev_err(dev, "Couldn't deassert our reset line\n");
		return ret;
	}

	backend->bus_clk = devm_clk_get(dev, "ahb");
	if (IS_ERR(backend->bus_clk)) {
		dev_err(dev, "Couldn't get the backend bus clock\n");
		ret = PTR_ERR(backend->bus_clk);
		goto err_assert_reset;
	}
	clk_prepare_enable(backend->bus_clk);

	backend->mod_clk = devm_clk_get(dev, "mod");
	if (IS_ERR(backend->mod_clk)) {
		dev_err(dev, "Couldn't get the backend module clock\n");
		ret = PTR_ERR(backend->mod_clk);
		goto err_disable_bus_clk;
	}
	clk_prepare_enable(backend->mod_clk);

	backend->ram_clk = devm_clk_get(dev, "ram");
	if (IS_ERR(backend->ram_clk)) {
		dev_err(dev, "Couldn't get the backend RAM clock\n");
		ret = PTR_ERR(backend->ram_clk);
		goto err_disable_mod_clk;
	}
	clk_prepare_enable(backend->ram_clk);

	if (of_device_is_compatible(dev->of_node,
				    "allwinner,sun8i-a33-display-backend")) {
		ret = sun4i_backend_init_sat(dev);
		if (ret) {
			dev_err(dev, "Couldn't init SAT resources\n");
			goto err_disable_ram_clk;
		}
	}

	backend->engine.regs = devm_regmap_init_mmio(dev, regs,
						     &sun4i_backend_regmap_config);
	if (IS_ERR(backend->engine.regs)) {
		dev_err(dev, "Couldn't create the backend regmap\n");
		return PTR_ERR(backend->engine.regs);
	}

	list_add_tail(&backend->engine.list, &drv->engine_list);

	/*
	 * Many of the backend's layer configuration registers have
	 * undefined default values. This poses a risk as we use
	 * regmap_update_bits in some places, and don't overwrite
	 * the whole register.
	 *
	 * Clear the registers here to have something predictable.
	 */
	for (i = 0x800; i < 0x1000; i += 4)
		regmap_write(backend->engine.regs, i, 0);

	/* Disable registers autoloading */
	regmap_write(backend->engine.regs, SUN4I_BACKEND_REGBUFFCTL_REG,
		     SUN4I_BACKEND_REGBUFFCTL_AUTOLOAD_DIS);

	/* Enable the backend */
	regmap_write(backend->engine.regs, SUN4I_BACKEND_MODCTL_REG,
		     SUN4I_BACKEND_MODCTL_DEBE_EN |
		     SUN4I_BACKEND_MODCTL_START_CTL);

	return 0;

err_disable_ram_clk:
	clk_disable_unprepare(backend->ram_clk);
err_disable_mod_clk:
	clk_disable_unprepare(backend->mod_clk);
err_disable_bus_clk:
	clk_disable_unprepare(backend->bus_clk);
err_assert_reset:
	reset_control_assert(backend->reset);
	return ret;
}

static void sun4i_backend_unbind(struct device *dev, struct device *master,
				 void *data)
{
	struct sun4i_backend *backend = dev_get_drvdata(dev);

	list_del(&backend->engine.list);

	if (of_device_is_compatible(dev->of_node,
				    "allwinner,sun8i-a33-display-backend"))
		sun4i_backend_free_sat(dev);

	clk_disable_unprepare(backend->ram_clk);
	clk_disable_unprepare(backend->mod_clk);
	clk_disable_unprepare(backend->bus_clk);
	reset_control_assert(backend->reset);
}

static const struct component_ops sun4i_backend_ops = {
	.bind	= sun4i_backend_bind,
	.unbind	= sun4i_backend_unbind,
};

static int sun4i_backend_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &sun4i_backend_ops);
}

static int sun4i_backend_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sun4i_backend_ops);

	return 0;
}

static const struct of_device_id sun4i_backend_of_table[] = {
	{ .compatible = "allwinner,sun5i-a13-display-backend" },
	{ .compatible = "allwinner,sun6i-a31-display-backend" },
	{ .compatible = "allwinner,sun8i-a33-display-backend" },
	{ }
};
MODULE_DEVICE_TABLE(of, sun4i_backend_of_table);

static struct platform_driver sun4i_backend_platform_driver = {
	.probe		= sun4i_backend_probe,
	.remove		= sun4i_backend_remove,
	.driver		= {
		.name		= "sun4i-backend",
		.of_match_table	= sun4i_backend_of_table,
	},
};
module_platform_driver(sun4i_backend_platform_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner A10 Display Backend Driver");
MODULE_LICENSE("GPL");
