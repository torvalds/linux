// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015 Free Electrons
 * Copyright (C) 2015 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "sun4i_crtc.h"
#include "sun4i_drv.h"
#include "sunxi_engine.h"

#define SUN4I_TVE_EN_REG		0x000
#define SUN4I_TVE_EN_DAC_MAP_MASK		GENMASK(19, 4)
#define SUN4I_TVE_EN_DAC_MAP(dac, out)		(((out) & 0xf) << (dac + 1) * 4)
#define SUN4I_TVE_EN_ENABLE			BIT(0)

#define SUN4I_TVE_CFG0_REG		0x004
#define SUN4I_TVE_CFG0_DAC_CONTROL_54M		BIT(26)
#define SUN4I_TVE_CFG0_CORE_DATAPATH_54M	BIT(25)
#define SUN4I_TVE_CFG0_CORE_CONTROL_54M		BIT(24)
#define SUN4I_TVE_CFG0_YC_EN			BIT(17)
#define SUN4I_TVE_CFG0_COMP_EN			BIT(16)
#define SUN4I_TVE_CFG0_RES(x)			((x) & 0xf)
#define SUN4I_TVE_CFG0_RES_480i			SUN4I_TVE_CFG0_RES(0)
#define SUN4I_TVE_CFG0_RES_576i			SUN4I_TVE_CFG0_RES(1)

#define SUN4I_TVE_DAC0_REG		0x008
#define SUN4I_TVE_DAC0_CLOCK_INVERT		BIT(24)
#define SUN4I_TVE_DAC0_LUMA(x)			(((x) & 3) << 20)
#define SUN4I_TVE_DAC0_LUMA_0_4			SUN4I_TVE_DAC0_LUMA(3)
#define SUN4I_TVE_DAC0_CHROMA(x)		(((x) & 3) << 18)
#define SUN4I_TVE_DAC0_CHROMA_0_75		SUN4I_TVE_DAC0_CHROMA(3)
#define SUN4I_TVE_DAC0_INTERNAL_DAC(x)		(((x) & 3) << 16)
#define SUN4I_TVE_DAC0_INTERNAL_DAC_37_5_OHMS	SUN4I_TVE_DAC0_INTERNAL_DAC(3)
#define SUN4I_TVE_DAC0_DAC_EN(dac)		BIT(dac)

#define SUN4I_TVE_NOTCH_REG		0x00c
#define SUN4I_TVE_NOTCH_DAC0_TO_DAC_DLY(dac, x)	((4 - (x)) << (dac * 3))

#define SUN4I_TVE_CHROMA_FREQ_REG	0x010

#define SUN4I_TVE_PORCH_REG		0x014
#define SUN4I_TVE_PORCH_BACK(x)			((x) << 16)
#define SUN4I_TVE_PORCH_FRONT(x)		(x)

#define SUN4I_TVE_LINE_REG		0x01c
#define SUN4I_TVE_LINE_FIRST(x)			((x) << 16)
#define SUN4I_TVE_LINE_NUMBER(x)		(x)

#define SUN4I_TVE_LEVEL_REG		0x020
#define SUN4I_TVE_LEVEL_BLANK(x)		((x) << 16)
#define SUN4I_TVE_LEVEL_BLACK(x)		(x)

#define SUN4I_TVE_DAC1_REG		0x024
#define SUN4I_TVE_DAC1_AMPLITUDE(dac, x)	((x) << (dac * 8))

#define SUN4I_TVE_DETECT_STA_REG	0x038
#define SUN4I_TVE_DETECT_STA_DAC(dac)		BIT((dac * 8))
#define SUN4I_TVE_DETECT_STA_UNCONNECTED		0
#define SUN4I_TVE_DETECT_STA_CONNECTED			1
#define SUN4I_TVE_DETECT_STA_GROUND			2

#define SUN4I_TVE_CB_CR_LVL_REG		0x10c
#define SUN4I_TVE_CB_CR_LVL_CR_BURST(x)		((x) << 8)
#define SUN4I_TVE_CB_CR_LVL_CB_BURST(x)		(x)

#define SUN4I_TVE_TINT_BURST_PHASE_REG	0x110
#define SUN4I_TVE_TINT_BURST_PHASE_CHROMA(x)	(x)

#define SUN4I_TVE_BURST_WIDTH_REG	0x114
#define SUN4I_TVE_BURST_WIDTH_BREEZEWAY(x)	((x) << 16)
#define SUN4I_TVE_BURST_WIDTH_BURST_WIDTH(x)	((x) << 8)
#define SUN4I_TVE_BURST_WIDTH_HSYNC_WIDTH(x)	(x)

#define SUN4I_TVE_CB_CR_GAIN_REG	0x118
#define SUN4I_TVE_CB_CR_GAIN_CR(x)		((x) << 8)
#define SUN4I_TVE_CB_CR_GAIN_CB(x)		(x)

#define SUN4I_TVE_SYNC_VBI_REG		0x11c
#define SUN4I_TVE_SYNC_VBI_SYNC(x)		((x) << 16)
#define SUN4I_TVE_SYNC_VBI_VBLANK(x)		(x)

#define SUN4I_TVE_ACTIVE_LINE_REG	0x124
#define SUN4I_TVE_ACTIVE_LINE(x)		(x)

#define SUN4I_TVE_CHROMA_REG		0x128
#define SUN4I_TVE_CHROMA_COMP_GAIN(x)		((x) & 3)
#define SUN4I_TVE_CHROMA_COMP_GAIN_50		SUN4I_TVE_CHROMA_COMP_GAIN(2)

#define SUN4I_TVE_12C_REG		0x12c
#define SUN4I_TVE_12C_NOTCH_WIDTH_WIDE		BIT(8)
#define SUN4I_TVE_12C_COMP_YUV_EN		BIT(0)

#define SUN4I_TVE_RESYNC_REG		0x130
#define SUN4I_TVE_RESYNC_FIELD			BIT(31)
#define SUN4I_TVE_RESYNC_LINE(x)		((x) << 16)
#define SUN4I_TVE_RESYNC_PIXEL(x)		(x)

#define SUN4I_TVE_SLAVE_REG		0x134

#define SUN4I_TVE_WSS_DATA2_REG		0x244

struct color_gains {
	u16	cb;
	u16	cr;
};

struct burst_levels {
	u16	cb;
	u16	cr;
};

struct video_levels {
	u16	black;
	u16	blank;
};

struct resync_parameters {
	bool	field;
	u16	line;
	u16	pixel;
};

struct tv_mode {
	char		*name;

	u32		mode;
	u32		chroma_freq;
	u16		back_porch;
	u16		front_porch;
	u16		line_number;
	u16		vblank_level;

	u32		hdisplay;
	u16		hfront_porch;
	u16		hsync_len;
	u16		hback_porch;

	u32		vdisplay;
	u16		vfront_porch;
	u16		vsync_len;
	u16		vback_porch;

	bool		yc_en;
	bool		dac3_en;
	bool		dac_bit25_en;

	const struct color_gains	*color_gains;
	const struct burst_levels	*burst_levels;
	const struct video_levels	*video_levels;
	const struct resync_parameters	*resync_params;
};

struct sun4i_tv {
	struct drm_connector	connector;
	struct drm_encoder	encoder;

	struct clk		*clk;
	struct regmap		*regs;
	struct reset_control	*reset;

	struct sun4i_drv	*drv;
};

static const struct video_levels ntsc_video_levels = {
	.black = 282,	.blank = 240,
};

static const struct video_levels pal_video_levels = {
	.black = 252,	.blank = 252,
};

static const struct burst_levels ntsc_burst_levels = {
	.cb = 79,	.cr = 0,
};

static const struct burst_levels pal_burst_levels = {
	.cb = 40,	.cr = 40,
};

static const struct color_gains ntsc_color_gains = {
	.cb = 160,	.cr = 160,
};

static const struct color_gains pal_color_gains = {
	.cb = 224,	.cr = 224,
};

static const struct resync_parameters ntsc_resync_parameters = {
	.field = false,	.line = 14,	.pixel = 12,
};

static const struct resync_parameters pal_resync_parameters = {
	.field = true,	.line = 13,	.pixel = 12,
};

static const struct tv_mode tv_modes[] = {
	{
		.name		= "NTSC",
		.mode		= SUN4I_TVE_CFG0_RES_480i,
		.chroma_freq	= 0x21f07c1f,
		.yc_en		= true,
		.dac3_en	= true,
		.dac_bit25_en	= true,

		.back_porch	= 118,
		.front_porch	= 32,
		.line_number	= 525,

		.hdisplay	= 720,
		.hfront_porch	= 18,
		.hsync_len	= 2,
		.hback_porch	= 118,

		.vdisplay	= 480,
		.vfront_porch	= 26,
		.vsync_len	= 2,
		.vback_porch	= 17,

		.vblank_level	= 240,

		.color_gains	= &ntsc_color_gains,
		.burst_levels	= &ntsc_burst_levels,
		.video_levels	= &ntsc_video_levels,
		.resync_params	= &ntsc_resync_parameters,
	},
	{
		.name		= "PAL",
		.mode		= SUN4I_TVE_CFG0_RES_576i,
		.chroma_freq	= 0x2a098acb,

		.back_porch	= 138,
		.front_porch	= 24,
		.line_number	= 625,

		.hdisplay	= 720,
		.hfront_porch	= 3,
		.hsync_len	= 2,
		.hback_porch	= 139,

		.vdisplay	= 576,
		.vfront_porch	= 28,
		.vsync_len	= 2,
		.vback_porch	= 19,

		.vblank_level	= 252,

		.color_gains	= &pal_color_gains,
		.burst_levels	= &pal_burst_levels,
		.video_levels	= &pal_video_levels,
		.resync_params	= &pal_resync_parameters,
	},
};

static inline struct sun4i_tv *
drm_encoder_to_sun4i_tv(struct drm_encoder *encoder)
{
	return container_of(encoder, struct sun4i_tv,
			    encoder);
}

static inline struct sun4i_tv *
drm_connector_to_sun4i_tv(struct drm_connector *connector)
{
	return container_of(connector, struct sun4i_tv,
			    connector);
}

/*
 * FIXME: If only the drm_display_mode private field was usable, this
 * could go away...
 *
 * So far, it doesn't seem to be preserved when the mode is passed by
 * to mode_set for some reason.
 */
static const struct tv_mode *sun4i_tv_find_tv_by_mode(const struct drm_display_mode *mode)
{
	int i;

	/* First try to identify the mode by name */
	for (i = 0; i < ARRAY_SIZE(tv_modes); i++) {
		const struct tv_mode *tv_mode = &tv_modes[i];

		DRM_DEBUG_DRIVER("Comparing mode %s vs %s",
				 mode->name, tv_mode->name);

		if (!strcmp(mode->name, tv_mode->name))
			return tv_mode;
	}

	/* Then by number of lines */
	for (i = 0; i < ARRAY_SIZE(tv_modes); i++) {
		const struct tv_mode *tv_mode = &tv_modes[i];

		DRM_DEBUG_DRIVER("Comparing mode %s vs %s (X: %d vs %d)",
				 mode->name, tv_mode->name,
				 mode->vdisplay, tv_mode->vdisplay);

		if (mode->vdisplay == tv_mode->vdisplay)
			return tv_mode;
	}

	return NULL;
}

static void sun4i_tv_mode_to_drm_mode(const struct tv_mode *tv_mode,
				      struct drm_display_mode *mode)
{
	DRM_DEBUG_DRIVER("Creating mode %s\n", mode->name);

	mode->type = DRM_MODE_TYPE_DRIVER;
	mode->clock = 13500;
	mode->flags = DRM_MODE_FLAG_INTERLACE;

	mode->hdisplay = tv_mode->hdisplay;
	mode->hsync_start = mode->hdisplay + tv_mode->hfront_porch;
	mode->hsync_end = mode->hsync_start + tv_mode->hsync_len;
	mode->htotal = mode->hsync_end  + tv_mode->hback_porch;

	mode->vdisplay = tv_mode->vdisplay;
	mode->vsync_start = mode->vdisplay + tv_mode->vfront_porch;
	mode->vsync_end = mode->vsync_start + tv_mode->vsync_len;
	mode->vtotal = mode->vsync_end  + tv_mode->vback_porch;
}

static void sun4i_tv_disable(struct drm_encoder *encoder)
{
	struct sun4i_tv *tv = drm_encoder_to_sun4i_tv(encoder);
	struct sun4i_crtc *crtc = drm_crtc_to_sun4i_crtc(encoder->crtc);

	DRM_DEBUG_DRIVER("Disabling the TV Output\n");

	regmap_update_bits(tv->regs, SUN4I_TVE_EN_REG,
			   SUN4I_TVE_EN_ENABLE,
			   0);

	sunxi_engine_disable_color_correction(crtc->engine);
}

static void sun4i_tv_enable(struct drm_encoder *encoder)
{
	struct sun4i_tv *tv = drm_encoder_to_sun4i_tv(encoder);
	struct sun4i_crtc *crtc = drm_crtc_to_sun4i_crtc(encoder->crtc);

	DRM_DEBUG_DRIVER("Enabling the TV Output\n");

	sunxi_engine_apply_color_correction(crtc->engine);

	regmap_update_bits(tv->regs, SUN4I_TVE_EN_REG,
			   SUN4I_TVE_EN_ENABLE,
			   SUN4I_TVE_EN_ENABLE);
}

static void sun4i_tv_mode_set(struct drm_encoder *encoder,
			      struct drm_display_mode *mode,
			      struct drm_display_mode *adjusted_mode)
{
	struct sun4i_tv *tv = drm_encoder_to_sun4i_tv(encoder);
	const struct tv_mode *tv_mode = sun4i_tv_find_tv_by_mode(mode);

	/* Enable and map the DAC to the output */
	regmap_update_bits(tv->regs, SUN4I_TVE_EN_REG,
			   SUN4I_TVE_EN_DAC_MAP_MASK,
			   SUN4I_TVE_EN_DAC_MAP(0, 1) |
			   SUN4I_TVE_EN_DAC_MAP(1, 2) |
			   SUN4I_TVE_EN_DAC_MAP(2, 3) |
			   SUN4I_TVE_EN_DAC_MAP(3, 4));

	/* Set PAL settings */
	regmap_write(tv->regs, SUN4I_TVE_CFG0_REG,
		     tv_mode->mode |
		     (tv_mode->yc_en ? SUN4I_TVE_CFG0_YC_EN : 0) |
		     SUN4I_TVE_CFG0_COMP_EN |
		     SUN4I_TVE_CFG0_DAC_CONTROL_54M |
		     SUN4I_TVE_CFG0_CORE_DATAPATH_54M |
		     SUN4I_TVE_CFG0_CORE_CONTROL_54M);

	/* Configure the DAC for a composite output */
	regmap_write(tv->regs, SUN4I_TVE_DAC0_REG,
		     SUN4I_TVE_DAC0_DAC_EN(0) |
		     (tv_mode->dac3_en ? SUN4I_TVE_DAC0_DAC_EN(3) : 0) |
		     SUN4I_TVE_DAC0_INTERNAL_DAC_37_5_OHMS |
		     SUN4I_TVE_DAC0_CHROMA_0_75 |
		     SUN4I_TVE_DAC0_LUMA_0_4 |
		     SUN4I_TVE_DAC0_CLOCK_INVERT |
		     (tv_mode->dac_bit25_en ? BIT(25) : 0) |
		     BIT(30));

	/* Configure the sample delay between DAC0 and the other DAC */
	regmap_write(tv->regs, SUN4I_TVE_NOTCH_REG,
		     SUN4I_TVE_NOTCH_DAC0_TO_DAC_DLY(1, 0) |
		     SUN4I_TVE_NOTCH_DAC0_TO_DAC_DLY(2, 0));

	regmap_write(tv->regs, SUN4I_TVE_CHROMA_FREQ_REG,
		     tv_mode->chroma_freq);

	/* Set the front and back porch */
	regmap_write(tv->regs, SUN4I_TVE_PORCH_REG,
		     SUN4I_TVE_PORCH_BACK(tv_mode->back_porch) |
		     SUN4I_TVE_PORCH_FRONT(tv_mode->front_porch));

	/* Set the lines setup */
	regmap_write(tv->regs, SUN4I_TVE_LINE_REG,
		     SUN4I_TVE_LINE_FIRST(22) |
		     SUN4I_TVE_LINE_NUMBER(tv_mode->line_number));

	regmap_write(tv->regs, SUN4I_TVE_LEVEL_REG,
		     SUN4I_TVE_LEVEL_BLANK(tv_mode->video_levels->blank) |
		     SUN4I_TVE_LEVEL_BLACK(tv_mode->video_levels->black));

	regmap_write(tv->regs, SUN4I_TVE_DAC1_REG,
		     SUN4I_TVE_DAC1_AMPLITUDE(0, 0x18) |
		     SUN4I_TVE_DAC1_AMPLITUDE(1, 0x18) |
		     SUN4I_TVE_DAC1_AMPLITUDE(2, 0x18) |
		     SUN4I_TVE_DAC1_AMPLITUDE(3, 0x18));

	regmap_write(tv->regs, SUN4I_TVE_CB_CR_LVL_REG,
		     SUN4I_TVE_CB_CR_LVL_CB_BURST(tv_mode->burst_levels->cb) |
		     SUN4I_TVE_CB_CR_LVL_CR_BURST(tv_mode->burst_levels->cr));

	/* Set burst width for a composite output */
	regmap_write(tv->regs, SUN4I_TVE_BURST_WIDTH_REG,
		     SUN4I_TVE_BURST_WIDTH_HSYNC_WIDTH(126) |
		     SUN4I_TVE_BURST_WIDTH_BURST_WIDTH(68) |
		     SUN4I_TVE_BURST_WIDTH_BREEZEWAY(22));

	regmap_write(tv->regs, SUN4I_TVE_CB_CR_GAIN_REG,
		     SUN4I_TVE_CB_CR_GAIN_CB(tv_mode->color_gains->cb) |
		     SUN4I_TVE_CB_CR_GAIN_CR(tv_mode->color_gains->cr));

	regmap_write(tv->regs, SUN4I_TVE_SYNC_VBI_REG,
		     SUN4I_TVE_SYNC_VBI_SYNC(0x10) |
		     SUN4I_TVE_SYNC_VBI_VBLANK(tv_mode->vblank_level));

	regmap_write(tv->regs, SUN4I_TVE_ACTIVE_LINE_REG,
		     SUN4I_TVE_ACTIVE_LINE(1440));

	/* Set composite chroma gain to 50 % */
	regmap_write(tv->regs, SUN4I_TVE_CHROMA_REG,
		     SUN4I_TVE_CHROMA_COMP_GAIN_50);

	regmap_write(tv->regs, SUN4I_TVE_12C_REG,
		     SUN4I_TVE_12C_COMP_YUV_EN |
		     SUN4I_TVE_12C_NOTCH_WIDTH_WIDE);

	regmap_write(tv->regs, SUN4I_TVE_RESYNC_REG,
		     SUN4I_TVE_RESYNC_PIXEL(tv_mode->resync_params->pixel) |
		     SUN4I_TVE_RESYNC_LINE(tv_mode->resync_params->line) |
		     (tv_mode->resync_params->field ?
		      SUN4I_TVE_RESYNC_FIELD : 0));

	regmap_write(tv->regs, SUN4I_TVE_SLAVE_REG, 0);
}

static const struct drm_encoder_helper_funcs sun4i_tv_helper_funcs = {
	.disable	= sun4i_tv_disable,
	.enable		= sun4i_tv_enable,
	.mode_set	= sun4i_tv_mode_set,
};

static int sun4i_tv_comp_get_modes(struct drm_connector *connector)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tv_modes); i++) {
		struct drm_display_mode *mode;
		const struct tv_mode *tv_mode = &tv_modes[i];

		mode = drm_mode_create(connector->dev);
		if (!mode) {
			DRM_ERROR("Failed to create a new display mode\n");
			return 0;
		}

		strcpy(mode->name, tv_mode->name);

		sun4i_tv_mode_to_drm_mode(tv_mode, mode);
		drm_mode_probed_add(connector, mode);
	}

	return i;
}

static int sun4i_tv_comp_mode_valid(struct drm_connector *connector,
				    struct drm_display_mode *mode)
{
	/* TODO */
	return MODE_OK;
}

static const struct drm_connector_helper_funcs sun4i_tv_comp_connector_helper_funcs = {
	.get_modes	= sun4i_tv_comp_get_modes,
	.mode_valid	= sun4i_tv_comp_mode_valid,
};

static void
sun4i_tv_comp_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs sun4i_tv_comp_connector_funcs = {
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= sun4i_tv_comp_connector_destroy,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

static const struct regmap_config sun4i_tv_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= SUN4I_TVE_WSS_DATA2_REG,
	.name		= "tv-encoder",
};

static int sun4i_tv_bind(struct device *dev, struct device *master,
			 void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = data;
	struct sun4i_drv *drv = drm->dev_private;
	struct sun4i_tv *tv;
	void __iomem *regs;
	int ret;

	tv = devm_kzalloc(dev, sizeof(*tv), GFP_KERNEL);
	if (!tv)
		return -ENOMEM;
	tv->drv = drv;
	dev_set_drvdata(dev, tv);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs)) {
		dev_err(dev, "Couldn't map the TV encoder registers\n");
		return PTR_ERR(regs);
	}

	tv->regs = devm_regmap_init_mmio(dev, regs,
					 &sun4i_tv_regmap_config);
	if (IS_ERR(tv->regs)) {
		dev_err(dev, "Couldn't create the TV encoder regmap\n");
		return PTR_ERR(tv->regs);
	}

	tv->reset = devm_reset_control_get(dev, NULL);
	if (IS_ERR(tv->reset)) {
		dev_err(dev, "Couldn't get our reset line\n");
		return PTR_ERR(tv->reset);
	}

	ret = reset_control_deassert(tv->reset);
	if (ret) {
		dev_err(dev, "Couldn't deassert our reset line\n");
		return ret;
	}

	tv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(tv->clk)) {
		dev_err(dev, "Couldn't get the TV encoder clock\n");
		ret = PTR_ERR(tv->clk);
		goto err_assert_reset;
	}
	clk_prepare_enable(tv->clk);

	drm_encoder_helper_add(&tv->encoder,
			       &sun4i_tv_helper_funcs);
	ret = drm_simple_encoder_init(drm, &tv->encoder,
				      DRM_MODE_ENCODER_TVDAC);
	if (ret) {
		dev_err(dev, "Couldn't initialise the TV encoder\n");
		goto err_disable_clk;
	}

	tv->encoder.possible_crtcs = drm_of_find_possible_crtcs(drm,
								dev->of_node);
	if (!tv->encoder.possible_crtcs) {
		ret = -EPROBE_DEFER;
		goto err_disable_clk;
	}

	drm_connector_helper_add(&tv->connector,
				 &sun4i_tv_comp_connector_helper_funcs);
	ret = drm_connector_init(drm, &tv->connector,
				 &sun4i_tv_comp_connector_funcs,
				 DRM_MODE_CONNECTOR_Composite);
	if (ret) {
		dev_err(dev,
			"Couldn't initialise the Composite connector\n");
		goto err_cleanup_connector;
	}
	tv->connector.interlace_allowed = true;

	drm_connector_attach_encoder(&tv->connector, &tv->encoder);

	return 0;

err_cleanup_connector:
	drm_encoder_cleanup(&tv->encoder);
err_disable_clk:
	clk_disable_unprepare(tv->clk);
err_assert_reset:
	reset_control_assert(tv->reset);
	return ret;
}

static void sun4i_tv_unbind(struct device *dev, struct device *master,
			    void *data)
{
	struct sun4i_tv *tv = dev_get_drvdata(dev);

	drm_connector_cleanup(&tv->connector);
	drm_encoder_cleanup(&tv->encoder);
	clk_disable_unprepare(tv->clk);
}

static const struct component_ops sun4i_tv_ops = {
	.bind	= sun4i_tv_bind,
	.unbind	= sun4i_tv_unbind,
};

static int sun4i_tv_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &sun4i_tv_ops);
}

static int sun4i_tv_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sun4i_tv_ops);

	return 0;
}

static const struct of_device_id sun4i_tv_of_table[] = {
	{ .compatible = "allwinner,sun4i-a10-tv-encoder" },
	{ }
};
MODULE_DEVICE_TABLE(of, sun4i_tv_of_table);

static struct platform_driver sun4i_tv_platform_driver = {
	.probe		= sun4i_tv_probe,
	.remove		= sun4i_tv_remove,
	.driver		= {
		.name		= "sun4i-tve",
		.of_match_table	= sun4i_tv_of_table,
	},
};
module_platform_driver(sun4i_tv_platform_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner A10 TV Encoder Driver");
MODULE_LICENSE("GPL");
