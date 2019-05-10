/*
 * Copyright (C) 2017 Icenowy Zheng <icenowy@aosc.io>
 *
 * Based on sun4i_backend.c, which is:
 *   Copyright (C) 2015 Free Electrons
 *   Copyright (C) 2015 NextThing Co
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>

#include <linux/component.h>
#include <linux/dma-mapping.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/reset.h>

#include "sun4i_drv.h"
#include "sun8i_mixer.h"
#include "sun8i_ui_layer.h"
#include "sun8i_vi_layer.h"
#include "sunxi_engine.h"

static const struct de2_fmt_info de2_formats[] = {
	{
		.drm_fmt = DRM_FORMAT_ARGB8888,
		.de2_fmt = SUN8I_MIXER_FBFMT_ARGB8888,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_ABGR8888,
		.de2_fmt = SUN8I_MIXER_FBFMT_ABGR8888,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_RGBA8888,
		.de2_fmt = SUN8I_MIXER_FBFMT_RGBA8888,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_BGRA8888,
		.de2_fmt = SUN8I_MIXER_FBFMT_BGRA8888,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_XRGB8888,
		.de2_fmt = SUN8I_MIXER_FBFMT_XRGB8888,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_XBGR8888,
		.de2_fmt = SUN8I_MIXER_FBFMT_XBGR8888,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_RGBX8888,
		.de2_fmt = SUN8I_MIXER_FBFMT_RGBX8888,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_BGRX8888,
		.de2_fmt = SUN8I_MIXER_FBFMT_BGRX8888,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_RGB888,
		.de2_fmt = SUN8I_MIXER_FBFMT_RGB888,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_BGR888,
		.de2_fmt = SUN8I_MIXER_FBFMT_BGR888,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_RGB565,
		.de2_fmt = SUN8I_MIXER_FBFMT_RGB565,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_BGR565,
		.de2_fmt = SUN8I_MIXER_FBFMT_BGR565,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_ARGB4444,
		.de2_fmt = SUN8I_MIXER_FBFMT_ARGB4444,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_ABGR4444,
		.de2_fmt = SUN8I_MIXER_FBFMT_ABGR4444,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_RGBA4444,
		.de2_fmt = SUN8I_MIXER_FBFMT_RGBA4444,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_BGRA4444,
		.de2_fmt = SUN8I_MIXER_FBFMT_BGRA4444,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_ARGB1555,
		.de2_fmt = SUN8I_MIXER_FBFMT_ARGB1555,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_ABGR1555,
		.de2_fmt = SUN8I_MIXER_FBFMT_ABGR1555,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_RGBA5551,
		.de2_fmt = SUN8I_MIXER_FBFMT_RGBA5551,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_BGRA5551,
		.de2_fmt = SUN8I_MIXER_FBFMT_BGRA5551,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_OFF,
	},
	{
		.drm_fmt = DRM_FORMAT_UYVY,
		.de2_fmt = SUN8I_MIXER_FBFMT_UYVY,
		.rgb = false,
		.csc = SUN8I_CSC_MODE_YUV2RGB,
	},
	{
		.drm_fmt = DRM_FORMAT_VYUY,
		.de2_fmt = SUN8I_MIXER_FBFMT_VYUY,
		.rgb = false,
		.csc = SUN8I_CSC_MODE_YUV2RGB,
	},
	{
		.drm_fmt = DRM_FORMAT_YUYV,
		.de2_fmt = SUN8I_MIXER_FBFMT_YUYV,
		.rgb = false,
		.csc = SUN8I_CSC_MODE_YUV2RGB,
	},
	{
		.drm_fmt = DRM_FORMAT_YVYU,
		.de2_fmt = SUN8I_MIXER_FBFMT_YVYU,
		.rgb = false,
		.csc = SUN8I_CSC_MODE_YUV2RGB,
	},
	{
		.drm_fmt = DRM_FORMAT_NV16,
		.de2_fmt = SUN8I_MIXER_FBFMT_NV16,
		.rgb = false,
		.csc = SUN8I_CSC_MODE_YUV2RGB,
	},
	{
		.drm_fmt = DRM_FORMAT_NV61,
		.de2_fmt = SUN8I_MIXER_FBFMT_NV61,
		.rgb = false,
		.csc = SUN8I_CSC_MODE_YUV2RGB,
	},
	{
		.drm_fmt = DRM_FORMAT_NV12,
		.de2_fmt = SUN8I_MIXER_FBFMT_NV12,
		.rgb = false,
		.csc = SUN8I_CSC_MODE_YUV2RGB,
	},
	{
		.drm_fmt = DRM_FORMAT_NV21,
		.de2_fmt = SUN8I_MIXER_FBFMT_NV21,
		.rgb = false,
		.csc = SUN8I_CSC_MODE_YUV2RGB,
	},
	{
		.drm_fmt = DRM_FORMAT_YUV444,
		.de2_fmt = SUN8I_MIXER_FBFMT_RGB888,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_YUV2RGB,
	},
	{
		.drm_fmt = DRM_FORMAT_YUV422,
		.de2_fmt = SUN8I_MIXER_FBFMT_YUV422,
		.rgb = false,
		.csc = SUN8I_CSC_MODE_YUV2RGB,
	},
	{
		.drm_fmt = DRM_FORMAT_YUV420,
		.de2_fmt = SUN8I_MIXER_FBFMT_YUV420,
		.rgb = false,
		.csc = SUN8I_CSC_MODE_YUV2RGB,
	},
	{
		.drm_fmt = DRM_FORMAT_YUV411,
		.de2_fmt = SUN8I_MIXER_FBFMT_YUV411,
		.rgb = false,
		.csc = SUN8I_CSC_MODE_YUV2RGB,
	},
	{
		.drm_fmt = DRM_FORMAT_YVU444,
		.de2_fmt = SUN8I_MIXER_FBFMT_RGB888,
		.rgb = true,
		.csc = SUN8I_CSC_MODE_YVU2RGB,
	},
	{
		.drm_fmt = DRM_FORMAT_YVU422,
		.de2_fmt = SUN8I_MIXER_FBFMT_YUV422,
		.rgb = false,
		.csc = SUN8I_CSC_MODE_YVU2RGB,
	},
	{
		.drm_fmt = DRM_FORMAT_YVU420,
		.de2_fmt = SUN8I_MIXER_FBFMT_YUV420,
		.rgb = false,
		.csc = SUN8I_CSC_MODE_YVU2RGB,
	},
	{
		.drm_fmt = DRM_FORMAT_YVU411,
		.de2_fmt = SUN8I_MIXER_FBFMT_YUV411,
		.rgb = false,
		.csc = SUN8I_CSC_MODE_YVU2RGB,
	},
};

const struct de2_fmt_info *sun8i_mixer_format_info(u32 format)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(de2_formats); ++i)
		if (de2_formats[i].drm_fmt == format)
			return &de2_formats[i];

	return NULL;
}

static void sun8i_mixer_commit(struct sunxi_engine *engine)
{
	DRM_DEBUG_DRIVER("Committing changes\n");

	regmap_write(engine->regs, SUN8I_MIXER_GLOBAL_DBUFF,
		     SUN8I_MIXER_GLOBAL_DBUFF_ENABLE);
}

static struct drm_plane **sun8i_layers_init(struct drm_device *drm,
					    struct sunxi_engine *engine)
{
	struct drm_plane **planes;
	struct sun8i_mixer *mixer = engine_to_sun8i_mixer(engine);
	int i;

	planes = devm_kcalloc(drm->dev,
			      mixer->cfg->vi_num + mixer->cfg->ui_num + 1,
			      sizeof(*planes), GFP_KERNEL);
	if (!planes)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < mixer->cfg->vi_num; i++) {
		struct sun8i_vi_layer *layer;

		layer = sun8i_vi_layer_init_one(drm, mixer, i);
		if (IS_ERR(layer)) {
			dev_err(drm->dev,
				"Couldn't initialize overlay plane\n");
			return ERR_CAST(layer);
		};

		planes[i] = &layer->plane;
	};

	for (i = 0; i < mixer->cfg->ui_num; i++) {
		struct sun8i_ui_layer *layer;

		layer = sun8i_ui_layer_init_one(drm, mixer, i);
		if (IS_ERR(layer)) {
			dev_err(drm->dev, "Couldn't initialize %s plane\n",
				i ? "overlay" : "primary");
			return ERR_CAST(layer);
		};

		planes[mixer->cfg->vi_num + i] = &layer->plane;
	};

	return planes;
}

static const struct sunxi_engine_ops sun8i_engine_ops = {
	.commit		= sun8i_mixer_commit,
	.layers_init	= sun8i_layers_init,
};

static struct regmap_config sun8i_mixer_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0xbfffc, /* guessed */
};

static int sun8i_mixer_of_get_id(struct device_node *node)
{
	struct device_node *port, *ep;
	int ret = -EINVAL;

	/* output is port 1 */
	port = of_graph_get_port_by_id(node, 1);
	if (!port)
		return -EINVAL;

	/* try to find downstream endpoint */
	for_each_available_child_of_node(port, ep) {
		struct device_node *remote;
		u32 reg;

		remote = of_graph_get_remote_endpoint(ep);
		if (!remote)
			continue;

		ret = of_property_read_u32(remote, "reg", &reg);
		if (!ret) {
			of_node_put(remote);
			of_node_put(ep);
			of_node_put(port);

			return reg;
		}

		of_node_put(remote);
	}

	of_node_put(port);

	return ret;
}

static int sun8i_mixer_bind(struct device *dev, struct device *master,
			      void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct sun4i_drv *drv = drm->dev_private;
	struct sun8i_mixer *mixer;
	struct resource *res;
	void __iomem *regs;
	unsigned int base;
	int plane_cnt;
	int i, ret;

	/*
	 * The mixer uses single 32-bit register to store memory
	 * addresses, so that it cannot deal with 64-bit memory
	 * addresses.
	 * Restrict the DMA mask so that the mixer won't be
	 * allocated some memory that is too high.
	 */
	ret = dma_set_mask(dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(dev, "Cannot do 32-bit DMA.\n");
		return ret;
	}

	mixer = devm_kzalloc(dev, sizeof(*mixer), GFP_KERNEL);
	if (!mixer)
		return -ENOMEM;
	dev_set_drvdata(dev, mixer);
	mixer->engine.ops = &sun8i_engine_ops;
	mixer->engine.node = dev->of_node;

	/*
	 * While this function can fail, we shouldn't do anything
	 * if this happens. Some early DE2 DT entries don't provide
	 * mixer id but work nevertheless because matching between
	 * TCON and mixer is done by comparing node pointers (old
	 * way) instead comparing ids. If this function fails and
	 * id is needed, it will fail during id matching anyway.
	 */
	mixer->engine.id = sun8i_mixer_of_get_id(dev->of_node);

	mixer->cfg = of_device_get_match_data(dev);
	if (!mixer->cfg)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	mixer->engine.regs = devm_regmap_init_mmio(dev, regs,
						   &sun8i_mixer_regmap_config);
	if (IS_ERR(mixer->engine.regs)) {
		dev_err(dev, "Couldn't create the mixer regmap\n");
		return PTR_ERR(mixer->engine.regs);
	}

	mixer->reset = devm_reset_control_get(dev, NULL);
	if (IS_ERR(mixer->reset)) {
		dev_err(dev, "Couldn't get our reset line\n");
		return PTR_ERR(mixer->reset);
	}

	ret = reset_control_deassert(mixer->reset);
	if (ret) {
		dev_err(dev, "Couldn't deassert our reset line\n");
		return ret;
	}

	mixer->bus_clk = devm_clk_get(dev, "bus");
	if (IS_ERR(mixer->bus_clk)) {
		dev_err(dev, "Couldn't get the mixer bus clock\n");
		ret = PTR_ERR(mixer->bus_clk);
		goto err_assert_reset;
	}
	clk_prepare_enable(mixer->bus_clk);

	mixer->mod_clk = devm_clk_get(dev, "mod");
	if (IS_ERR(mixer->mod_clk)) {
		dev_err(dev, "Couldn't get the mixer module clock\n");
		ret = PTR_ERR(mixer->mod_clk);
		goto err_disable_bus_clk;
	}

	/*
	 * It seems that we need to enforce that rate for whatever
	 * reason for the mixer to be functional. Make sure it's the
	 * case.
	 */
	if (mixer->cfg->mod_rate)
		clk_set_rate(mixer->mod_clk, mixer->cfg->mod_rate);

	clk_prepare_enable(mixer->mod_clk);

	list_add_tail(&mixer->engine.list, &drv->engine_list);

	base = sun8i_blender_base(mixer);

	/* Reset registers and disable unused sub-engines */
	if (mixer->cfg->is_de3) {
		for (i = 0; i < DE3_MIXER_UNIT_SIZE; i += 4)
			regmap_write(mixer->engine.regs, i, 0);

		regmap_write(mixer->engine.regs, SUN50I_MIXER_FCE_EN, 0);
		regmap_write(mixer->engine.regs, SUN50I_MIXER_PEAK_EN, 0);
		regmap_write(mixer->engine.regs, SUN50I_MIXER_LCTI_EN, 0);
		regmap_write(mixer->engine.regs, SUN50I_MIXER_BLS_EN, 0);
		regmap_write(mixer->engine.regs, SUN50I_MIXER_FCC_EN, 0);
		regmap_write(mixer->engine.regs, SUN50I_MIXER_DNS_EN, 0);
		regmap_write(mixer->engine.regs, SUN50I_MIXER_DRC_EN, 0);
		regmap_write(mixer->engine.regs, SUN50I_MIXER_FMT_EN, 0);
		regmap_write(mixer->engine.regs, SUN50I_MIXER_CDC0_EN, 0);
		regmap_write(mixer->engine.regs, SUN50I_MIXER_CDC1_EN, 0);
	} else {
		for (i = 0; i < DE2_MIXER_UNIT_SIZE; i += 4)
			regmap_write(mixer->engine.regs, i, 0);

		regmap_write(mixer->engine.regs, SUN8I_MIXER_FCE_EN, 0);
		regmap_write(mixer->engine.regs, SUN8I_MIXER_BWS_EN, 0);
		regmap_write(mixer->engine.regs, SUN8I_MIXER_LTI_EN, 0);
		regmap_write(mixer->engine.regs, SUN8I_MIXER_PEAK_EN, 0);
		regmap_write(mixer->engine.regs, SUN8I_MIXER_ASE_EN, 0);
		regmap_write(mixer->engine.regs, SUN8I_MIXER_FCC_EN, 0);
		regmap_write(mixer->engine.regs, SUN8I_MIXER_DCSC_EN, 0);
	}

	/* Enable the mixer */
	regmap_write(mixer->engine.regs, SUN8I_MIXER_GLOBAL_CTL,
		     SUN8I_MIXER_GLOBAL_CTL_RT_EN);

	/* Set background color to black */
	regmap_write(mixer->engine.regs, SUN8I_MIXER_BLEND_BKCOLOR(base),
		     SUN8I_MIXER_BLEND_COLOR_BLACK);

	/*
	 * Set fill color of bottom plane to black. Generally not needed
	 * except when VI plane is at bottom (zpos = 0) and enabled.
	 */
	regmap_write(mixer->engine.regs, SUN8I_MIXER_BLEND_PIPE_CTL(base),
		     SUN8I_MIXER_BLEND_PIPE_CTL_FC_EN(0));
	regmap_write(mixer->engine.regs, SUN8I_MIXER_BLEND_ATTR_FCOLOR(base, 0),
		     SUN8I_MIXER_BLEND_COLOR_BLACK);

	plane_cnt = mixer->cfg->vi_num + mixer->cfg->ui_num;
	for (i = 0; i < plane_cnt; i++)
		regmap_write(mixer->engine.regs,
			     SUN8I_MIXER_BLEND_MODE(base, i),
			     SUN8I_MIXER_BLEND_MODE_DEF);

	regmap_update_bits(mixer->engine.regs, SUN8I_MIXER_BLEND_PIPE_CTL(base),
			   SUN8I_MIXER_BLEND_PIPE_CTL_EN_MSK, 0);

	return 0;

err_disable_bus_clk:
	clk_disable_unprepare(mixer->bus_clk);
err_assert_reset:
	reset_control_assert(mixer->reset);
	return ret;
}

static void sun8i_mixer_unbind(struct device *dev, struct device *master,
				 void *data)
{
	struct sun8i_mixer *mixer = dev_get_drvdata(dev);

	list_del(&mixer->engine.list);

	clk_disable_unprepare(mixer->mod_clk);
	clk_disable_unprepare(mixer->bus_clk);
	reset_control_assert(mixer->reset);
}

static const struct component_ops sun8i_mixer_ops = {
	.bind	= sun8i_mixer_bind,
	.unbind	= sun8i_mixer_unbind,
};

static int sun8i_mixer_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &sun8i_mixer_ops);
}

static int sun8i_mixer_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sun8i_mixer_ops);

	return 0;
}

static const struct sun8i_mixer_cfg sun8i_a83t_mixer0_cfg = {
	.ccsc		= 0,
	.scaler_mask	= 0xf,
	.ui_num		= 3,
	.vi_num		= 1,
};

static const struct sun8i_mixer_cfg sun8i_a83t_mixer1_cfg = {
	.ccsc		= 1,
	.scaler_mask	= 0x3,
	.ui_num		= 1,
	.vi_num		= 1,
};

static const struct sun8i_mixer_cfg sun8i_h3_mixer0_cfg = {
	.ccsc		= 0,
	.mod_rate	= 432000000,
	.scaler_mask	= 0xf,
	.ui_num		= 3,
	.vi_num		= 1,
};

static const struct sun8i_mixer_cfg sun8i_r40_mixer0_cfg = {
	.ccsc		= 0,
	.mod_rate	= 297000000,
	.scaler_mask	= 0xf,
	.ui_num		= 3,
	.vi_num		= 1,
};

static const struct sun8i_mixer_cfg sun8i_r40_mixer1_cfg = {
	.ccsc		= 1,
	.mod_rate	= 297000000,
	.scaler_mask	= 0x3,
	.ui_num		= 1,
	.vi_num		= 1,
};

static const struct sun8i_mixer_cfg sun8i_v3s_mixer_cfg = {
	.vi_num = 2,
	.ui_num = 1,
	.scaler_mask = 0x3,
	.ccsc = 0,
	.mod_rate = 150000000,
};

static const struct sun8i_mixer_cfg sun50i_a64_mixer0_cfg = {
	.ccsc		= 0,
	.mod_rate	= 297000000,
	.scaler_mask	= 0xf,
	.ui_num		= 3,
	.vi_num		= 1,
};

static const struct sun8i_mixer_cfg sun50i_a64_mixer1_cfg = {
	.ccsc		= 1,
	.mod_rate	= 297000000,
	.scaler_mask	= 0x3,
	.ui_num		= 1,
	.vi_num		= 1,
};

static const struct sun8i_mixer_cfg sun50i_h6_mixer0_cfg = {
	.ccsc		= 0,
	.is_de3		= true,
	.mod_rate	= 600000000,
	.scaler_mask	= 0xf,
	.ui_num		= 3,
	.vi_num		= 1,
};

static const struct of_device_id sun8i_mixer_of_table[] = {
	{
		.compatible = "allwinner,sun8i-a83t-de2-mixer-0",
		.data = &sun8i_a83t_mixer0_cfg,
	},
	{
		.compatible = "allwinner,sun8i-a83t-de2-mixer-1",
		.data = &sun8i_a83t_mixer1_cfg,
	},
	{
		.compatible = "allwinner,sun8i-h3-de2-mixer-0",
		.data = &sun8i_h3_mixer0_cfg,
	},
	{
		.compatible = "allwinner,sun8i-r40-de2-mixer-0",
		.data = &sun8i_r40_mixer0_cfg,
	},
	{
		.compatible = "allwinner,sun8i-r40-de2-mixer-1",
		.data = &sun8i_r40_mixer1_cfg,
	},
	{
		.compatible = "allwinner,sun8i-v3s-de2-mixer",
		.data = &sun8i_v3s_mixer_cfg,
	},
	{
		.compatible = "allwinner,sun50i-a64-de2-mixer-0",
		.data = &sun50i_a64_mixer0_cfg,
	},
	{
		.compatible = "allwinner,sun50i-a64-de2-mixer-1",
		.data = &sun50i_a64_mixer1_cfg,
	},
	{
		.compatible = "allwinner,sun50i-h6-de3-mixer-0",
		.data = &sun50i_h6_mixer0_cfg,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, sun8i_mixer_of_table);

static struct platform_driver sun8i_mixer_platform_driver = {
	.probe		= sun8i_mixer_probe,
	.remove		= sun8i_mixer_remove,
	.driver		= {
		.name		= "sun8i-mixer",
		.of_match_table	= sun8i_mixer_of_table,
	},
};
module_platform_driver(sun8i_mixer_platform_driver);

MODULE_AUTHOR("Icenowy Zheng <icenowy@aosc.io>");
MODULE_DESCRIPTION("Allwinner DE2 Mixer driver");
MODULE_LICENSE("GPL");
