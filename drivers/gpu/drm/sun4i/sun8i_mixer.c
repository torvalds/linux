// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017 Icenowy Zheng <icenowy@aosc.io>
 *
 * Based on sun4i_backend.c, which is:
 *   Copyright (C) 2015 Free Electrons
 *   Copyright (C) 2015 NextThing Co
 */

#include <linux/component.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_probe_helper.h>

#include "sun4i_drv.h"
#include "sun8i_mixer.h"
#include "sun8i_ui_layer.h"
#include "sun8i_vi_layer.h"
#include "sunxi_engine.h"

struct de2_fmt_info {
	u32	drm_fmt;
	u32	de2_fmt;
};

static const struct de2_fmt_info de2_formats[] = {
	{
		.drm_fmt = DRM_FORMAT_ARGB8888,
		.de2_fmt = SUN8I_MIXER_FBFMT_ARGB8888,
	},
	{
		.drm_fmt = DRM_FORMAT_ABGR8888,
		.de2_fmt = SUN8I_MIXER_FBFMT_ABGR8888,
	},
	{
		.drm_fmt = DRM_FORMAT_RGBA8888,
		.de2_fmt = SUN8I_MIXER_FBFMT_RGBA8888,
	},
	{
		.drm_fmt = DRM_FORMAT_BGRA8888,
		.de2_fmt = SUN8I_MIXER_FBFMT_BGRA8888,
	},
	{
		.drm_fmt = DRM_FORMAT_XRGB8888,
		.de2_fmt = SUN8I_MIXER_FBFMT_XRGB8888,
	},
	{
		.drm_fmt = DRM_FORMAT_XBGR8888,
		.de2_fmt = SUN8I_MIXER_FBFMT_XBGR8888,
	},
	{
		.drm_fmt = DRM_FORMAT_RGBX8888,
		.de2_fmt = SUN8I_MIXER_FBFMT_RGBX8888,
	},
	{
		.drm_fmt = DRM_FORMAT_BGRX8888,
		.de2_fmt = SUN8I_MIXER_FBFMT_BGRX8888,
	},
	{
		.drm_fmt = DRM_FORMAT_RGB888,
		.de2_fmt = SUN8I_MIXER_FBFMT_RGB888,
	},
	{
		.drm_fmt = DRM_FORMAT_BGR888,
		.de2_fmt = SUN8I_MIXER_FBFMT_BGR888,
	},
	{
		.drm_fmt = DRM_FORMAT_RGB565,
		.de2_fmt = SUN8I_MIXER_FBFMT_RGB565,
	},
	{
		.drm_fmt = DRM_FORMAT_BGR565,
		.de2_fmt = SUN8I_MIXER_FBFMT_BGR565,
	},
	{
		.drm_fmt = DRM_FORMAT_ARGB4444,
		.de2_fmt = SUN8I_MIXER_FBFMT_ARGB4444,
	},
	{
		/* for DE2 VI layer which ignores alpha */
		.drm_fmt = DRM_FORMAT_XRGB4444,
		.de2_fmt = SUN8I_MIXER_FBFMT_ARGB4444,
	},
	{
		.drm_fmt = DRM_FORMAT_ABGR4444,
		.de2_fmt = SUN8I_MIXER_FBFMT_ABGR4444,
	},
	{
		/* for DE2 VI layer which ignores alpha */
		.drm_fmt = DRM_FORMAT_XBGR4444,
		.de2_fmt = SUN8I_MIXER_FBFMT_ABGR4444,
	},
	{
		.drm_fmt = DRM_FORMAT_RGBA4444,
		.de2_fmt = SUN8I_MIXER_FBFMT_RGBA4444,
	},
	{
		/* for DE2 VI layer which ignores alpha */
		.drm_fmt = DRM_FORMAT_RGBX4444,
		.de2_fmt = SUN8I_MIXER_FBFMT_RGBA4444,
	},
	{
		.drm_fmt = DRM_FORMAT_BGRA4444,
		.de2_fmt = SUN8I_MIXER_FBFMT_BGRA4444,
	},
	{
		/* for DE2 VI layer which ignores alpha */
		.drm_fmt = DRM_FORMAT_BGRX4444,
		.de2_fmt = SUN8I_MIXER_FBFMT_BGRA4444,
	},
	{
		.drm_fmt = DRM_FORMAT_ARGB1555,
		.de2_fmt = SUN8I_MIXER_FBFMT_ARGB1555,
	},
	{
		/* for DE2 VI layer which ignores alpha */
		.drm_fmt = DRM_FORMAT_XRGB1555,
		.de2_fmt = SUN8I_MIXER_FBFMT_ARGB1555,
	},
	{
		.drm_fmt = DRM_FORMAT_ABGR1555,
		.de2_fmt = SUN8I_MIXER_FBFMT_ABGR1555,
	},
	{
		/* for DE2 VI layer which ignores alpha */
		.drm_fmt = DRM_FORMAT_XBGR1555,
		.de2_fmt = SUN8I_MIXER_FBFMT_ABGR1555,
	},
	{
		.drm_fmt = DRM_FORMAT_RGBA5551,
		.de2_fmt = SUN8I_MIXER_FBFMT_RGBA5551,
	},
	{
		/* for DE2 VI layer which ignores alpha */
		.drm_fmt = DRM_FORMAT_RGBX5551,
		.de2_fmt = SUN8I_MIXER_FBFMT_RGBA5551,
	},
	{
		.drm_fmt = DRM_FORMAT_BGRA5551,
		.de2_fmt = SUN8I_MIXER_FBFMT_BGRA5551,
	},
	{
		/* for DE2 VI layer which ignores alpha */
		.drm_fmt = DRM_FORMAT_BGRX5551,
		.de2_fmt = SUN8I_MIXER_FBFMT_BGRA5551,
	},
	{
		.drm_fmt = DRM_FORMAT_ARGB2101010,
		.de2_fmt = SUN8I_MIXER_FBFMT_ARGB2101010,
	},
	{
		.drm_fmt = DRM_FORMAT_ABGR2101010,
		.de2_fmt = SUN8I_MIXER_FBFMT_ABGR2101010,
	},
	{
		.drm_fmt = DRM_FORMAT_RGBA1010102,
		.de2_fmt = SUN8I_MIXER_FBFMT_RGBA1010102,
	},
	{
		.drm_fmt = DRM_FORMAT_BGRA1010102,
		.de2_fmt = SUN8I_MIXER_FBFMT_BGRA1010102,
	},
	{
		.drm_fmt = DRM_FORMAT_UYVY,
		.de2_fmt = SUN8I_MIXER_FBFMT_UYVY,
	},
	{
		.drm_fmt = DRM_FORMAT_VYUY,
		.de2_fmt = SUN8I_MIXER_FBFMT_VYUY,
	},
	{
		.drm_fmt = DRM_FORMAT_YUYV,
		.de2_fmt = SUN8I_MIXER_FBFMT_YUYV,
	},
	{
		.drm_fmt = DRM_FORMAT_YVYU,
		.de2_fmt = SUN8I_MIXER_FBFMT_YVYU,
	},
	{
		.drm_fmt = DRM_FORMAT_NV16,
		.de2_fmt = SUN8I_MIXER_FBFMT_NV16,
	},
	{
		.drm_fmt = DRM_FORMAT_NV61,
		.de2_fmt = SUN8I_MIXER_FBFMT_NV61,
	},
	{
		.drm_fmt = DRM_FORMAT_NV12,
		.de2_fmt = SUN8I_MIXER_FBFMT_NV12,
	},
	{
		.drm_fmt = DRM_FORMAT_NV21,
		.de2_fmt = SUN8I_MIXER_FBFMT_NV21,
	},
	{
		.drm_fmt = DRM_FORMAT_YUV422,
		.de2_fmt = SUN8I_MIXER_FBFMT_YUV422,
	},
	{
		.drm_fmt = DRM_FORMAT_YUV420,
		.de2_fmt = SUN8I_MIXER_FBFMT_YUV420,
	},
	{
		.drm_fmt = DRM_FORMAT_YUV411,
		.de2_fmt = SUN8I_MIXER_FBFMT_YUV411,
	},
	{
		.drm_fmt = DRM_FORMAT_YVU422,
		.de2_fmt = SUN8I_MIXER_FBFMT_YUV422,
	},
	{
		.drm_fmt = DRM_FORMAT_YVU420,
		.de2_fmt = SUN8I_MIXER_FBFMT_YUV420,
	},
	{
		.drm_fmt = DRM_FORMAT_YVU411,
		.de2_fmt = SUN8I_MIXER_FBFMT_YUV411,
	},
	{
		.drm_fmt = DRM_FORMAT_P010,
		.de2_fmt = SUN8I_MIXER_FBFMT_P010_YUV,
	},
	{
		.drm_fmt = DRM_FORMAT_P210,
		.de2_fmt = SUN8I_MIXER_FBFMT_P210_YUV,
	},
};

int sun8i_mixer_drm_format_to_hw(u32 format, u32 *hw_format)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(de2_formats); ++i)
		if (de2_formats[i].drm_fmt == format) {
			*hw_format = de2_formats[i].de2_fmt;
			return 0;
		}

	return -EINVAL;
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
		}

		planes[i] = &layer->plane;
	}

	for (i = 0; i < mixer->cfg->ui_num; i++) {
		struct sun8i_ui_layer *layer;

		layer = sun8i_ui_layer_init_one(drm, mixer, i);
		if (IS_ERR(layer)) {
			dev_err(drm->dev, "Couldn't initialize %s plane\n",
				i ? "overlay" : "primary");
			return ERR_CAST(layer);
		}

		planes[mixer->cfg->vi_num + i] = &layer->plane;
	}

	return planes;
}

static void sun8i_mixer_mode_set(struct sunxi_engine *engine,
				 const struct drm_display_mode *mode)
{
	struct sun8i_mixer *mixer = engine_to_sun8i_mixer(engine);
	u32 bld_base, size, val;
	bool interlaced;

	bld_base = sun8i_blender_base(mixer);
	interlaced = !!(mode->flags & DRM_MODE_FLAG_INTERLACE);
	size = SUN8I_MIXER_SIZE(mode->hdisplay, mode->vdisplay);

	DRM_DEBUG_DRIVER("Updating global size W: %u H: %u\n",
			 mode->hdisplay, mode->vdisplay);

	regmap_write(engine->regs, SUN8I_MIXER_GLOBAL_SIZE, size);
	regmap_write(engine->regs, SUN8I_MIXER_BLEND_OUTSIZE(bld_base), size);

	if (interlaced)
		val = SUN8I_MIXER_BLEND_OUTCTL_INTERLACED;
	else
		val = 0;

	regmap_update_bits(engine->regs, SUN8I_MIXER_BLEND_OUTCTL(bld_base),
			   SUN8I_MIXER_BLEND_OUTCTL_INTERLACED, val);

	DRM_DEBUG_DRIVER("Switching display mixer interlaced mode %s\n",
			 interlaced ? "on" : "off");
}

static const struct sunxi_engine_ops sun8i_engine_ops = {
	.commit		= sun8i_mixer_commit,
	.layers_init	= sun8i_layers_init,
	.mode_set	= sun8i_mixer_mode_set,
};

static const struct regmap_config sun8i_mixer_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0xffffc, /* guessed */
};

static int sun8i_mixer_of_get_id(struct device_node *node)
{
	struct device_node *ep, *remote;
	struct of_endpoint of_ep;

	/* Output port is 1, and we want the first endpoint. */
	ep = of_graph_get_endpoint_by_regs(node, 1, -1);
	if (!ep)
		return -EINVAL;

	remote = of_graph_get_remote_endpoint(ep);
	of_node_put(ep);
	if (!remote)
		return -EINVAL;

	of_graph_parse_endpoint(remote, &of_ep);
	of_node_put(remote);
	return of_ep.id;
}

static int sun8i_mixer_bind(struct device *dev, struct device *master,
			      void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct sun4i_drv *drv = drm->dev_private;
	struct sun8i_mixer *mixer;
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

	if (of_property_present(dev->of_node, "iommus")) {
		/*
		 * This assume we have the same DMA constraints for
		 * all our the mixers in our pipeline. This sounds
		 * bad, but it has always been the case for us, and
		 * DRM doesn't do per-device allocation either, so we
		 * would need to fix DRM first...
		 */
		ret = of_dma_configure(drm->dev, dev->of_node, true);
		if (ret)
			return ret;
	}

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

	regs = devm_platform_ioremap_resource(pdev, 0);
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

static void sun8i_mixer_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sun8i_mixer_ops);
}

static const struct sun8i_mixer_cfg sun8i_a83t_mixer0_cfg = {
	.ccsc		= CCSC_MIXER0_LAYOUT,
	.scaler_mask	= 0xf,
	.scanline_yuv	= 2048,
	.ui_num		= 3,
	.vi_num		= 1,
};

static const struct sun8i_mixer_cfg sun8i_a83t_mixer1_cfg = {
	.ccsc		= CCSC_MIXER1_LAYOUT,
	.scaler_mask	= 0x3,
	.scanline_yuv	= 2048,
	.ui_num		= 1,
	.vi_num		= 1,
};

static const struct sun8i_mixer_cfg sun8i_h3_mixer0_cfg = {
	.ccsc		= CCSC_MIXER0_LAYOUT,
	.mod_rate	= 432000000,
	.scaler_mask	= 0xf,
	.scanline_yuv	= 2048,
	.ui_num		= 3,
	.vi_num		= 1,
};

static const struct sun8i_mixer_cfg sun8i_r40_mixer0_cfg = {
	.ccsc		= CCSC_MIXER0_LAYOUT,
	.mod_rate	= 297000000,
	.scaler_mask	= 0xf,
	.scanline_yuv	= 2048,
	.ui_num		= 3,
	.vi_num		= 1,
};

static const struct sun8i_mixer_cfg sun8i_r40_mixer1_cfg = {
	.ccsc		= CCSC_MIXER1_LAYOUT,
	.mod_rate	= 297000000,
	.scaler_mask	= 0x3,
	.scanline_yuv	= 2048,
	.ui_num		= 1,
	.vi_num		= 1,
};

static const struct sun8i_mixer_cfg sun8i_v3s_mixer_cfg = {
	.vi_num = 2,
	.ui_num = 1,
	.scaler_mask = 0x3,
	.scanline_yuv = 2048,
	.ccsc = CCSC_MIXER0_LAYOUT,
	.mod_rate = 150000000,
};

static const struct sun8i_mixer_cfg sun20i_d1_mixer0_cfg = {
	.ccsc		= CCSC_D1_MIXER0_LAYOUT,
	.mod_rate	= 297000000,
	.scaler_mask	= 0x3,
	.scanline_yuv	= 2048,
	.ui_num		= 1,
	.vi_num		= 1,
};

static const struct sun8i_mixer_cfg sun20i_d1_mixer1_cfg = {
	.ccsc		= CCSC_MIXER1_LAYOUT,
	.mod_rate	= 297000000,
	.scaler_mask	= 0x1,
	.scanline_yuv	= 1024,
	.ui_num		= 0,
	.vi_num		= 1,
};

static const struct sun8i_mixer_cfg sun50i_a64_mixer0_cfg = {
	.ccsc		= CCSC_MIXER0_LAYOUT,
	.mod_rate	= 297000000,
	.scaler_mask	= 0xf,
	.scanline_yuv	= 4096,
	.ui_num		= 3,
	.vi_num		= 1,
};

static const struct sun8i_mixer_cfg sun50i_a64_mixer1_cfg = {
	.ccsc		= CCSC_MIXER1_LAYOUT,
	.mod_rate	= 297000000,
	.scaler_mask	= 0x3,
	.scanline_yuv	= 2048,
	.ui_num		= 1,
	.vi_num		= 1,
};

static const struct sun8i_mixer_cfg sun50i_h6_mixer0_cfg = {
	.ccsc		= CCSC_MIXER0_LAYOUT,
	.is_de3		= true,
	.mod_rate	= 600000000,
	.scaler_mask	= 0xf,
	.scanline_yuv	= 4096,
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
		.compatible = "allwinner,sun20i-d1-de2-mixer-0",
		.data = &sun20i_d1_mixer0_cfg,
	},
	{
		.compatible = "allwinner,sun20i-d1-de2-mixer-1",
		.data = &sun20i_d1_mixer1_cfg,
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
	.remove_new	= sun8i_mixer_remove,
	.driver		= {
		.name		= "sun8i-mixer",
		.of_match_table	= sun8i_mixer_of_table,
	},
};
module_platform_driver(sun8i_mixer_platform_driver);

MODULE_AUTHOR("Icenowy Zheng <icenowy@aosc.io>");
MODULE_DESCRIPTION("Allwinner DE2 Mixer driver");
MODULE_LICENSE("GPL");
