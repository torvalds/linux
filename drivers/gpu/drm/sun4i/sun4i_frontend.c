// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Free Electrons
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <drm/drm_device.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_plane.h>

#include "sun4i_drv.h"
#include "sun4i_frontend.h"

static const u32 sun4i_frontend_vert_coef[32] = {
	0x00004000, 0x000140ff, 0x00033ffe, 0x00043ffd,
	0x00063efc, 0xff083dfc, 0x000a3bfb, 0xff0d39fb,
	0xff0f37fb, 0xff1136fa, 0xfe1433fb, 0xfe1631fb,
	0xfd192ffb, 0xfd1c2cfb, 0xfd1f29fb, 0xfc2127fc,
	0xfc2424fc, 0xfc2721fc, 0xfb291ffd, 0xfb2c1cfd,
	0xfb2f19fd, 0xfb3116fe, 0xfb3314fe, 0xfa3611ff,
	0xfb370fff, 0xfb390dff, 0xfb3b0a00, 0xfc3d08ff,
	0xfc3e0600, 0xfd3f0400, 0xfe3f0300, 0xff400100,
};

static const u32 sun4i_frontend_horz_coef[64] = {
	0x40000000, 0x00000000, 0x40fe0000, 0x0000ff03,
	0x3ffd0000, 0x0000ff05, 0x3ffc0000, 0x0000ff06,
	0x3efb0000, 0x0000ff08, 0x3dfb0000, 0x0000ff09,
	0x3bfa0000, 0x0000fe0d, 0x39fa0000, 0x0000fe0f,
	0x38fa0000, 0x0000fe10, 0x36fa0000, 0x0000fe12,
	0x33fa0000, 0x0000fd16, 0x31fa0000, 0x0000fd18,
	0x2ffa0000, 0x0000fd1a, 0x2cfa0000, 0x0000fc1e,
	0x29fa0000, 0x0000fc21, 0x27fb0000, 0x0000fb23,
	0x24fb0000, 0x0000fb26, 0x21fb0000, 0x0000fb29,
	0x1ffc0000, 0x0000fa2b, 0x1cfc0000, 0x0000fa2e,
	0x19fd0000, 0x0000fa30, 0x16fd0000, 0x0000fa33,
	0x14fd0000, 0x0000fa35, 0x11fe0000, 0x0000fa37,
	0x0ffe0000, 0x0000fa39, 0x0dfe0000, 0x0000fa3b,
	0x0afe0000, 0x0000fa3e, 0x08ff0000, 0x0000fb3e,
	0x06ff0000, 0x0000fb40, 0x05ff0000, 0x0000fc40,
	0x03ff0000, 0x0000fd41, 0x01ff0000, 0x0000fe42,
};

/*
 * These coefficients are taken from the A33 BSP from Allwinner.
 *
 * The first three values of each row are coded as 13-bit signed fixed-point
 * numbers, with 10 bits for the fractional part. The fourth value is a
 * constant coded as a 14-bit signed fixed-point number with 4 bits for the
 * fractional part.
 *
 * The values in table order give the following colorspace translation:
 * G = 1.164 * Y - 0.391 * U - 0.813 * V + 135
 * R = 1.164 * Y + 1.596 * V - 222
 * B = 1.164 * Y + 2.018 * U + 276
 *
 * This seems to be a conversion from Y[16:235] UV[16:240] to RGB[0:255],
 * following the BT601 spec.
 */
const u32 sunxi_bt601_yuv2rgb_coef[12] = {
	0x000004a7, 0x00001e6f, 0x00001cbf, 0x00000877,
	0x000004a7, 0x00000000, 0x00000662, 0x00003211,
	0x000004a7, 0x00000812, 0x00000000, 0x00002eb1,
};
EXPORT_SYMBOL(sunxi_bt601_yuv2rgb_coef);

static void sun4i_frontend_scaler_init(struct sun4i_frontend *frontend)
{
	int i;

	if (frontend->data->has_coef_access_ctrl)
		regmap_write_bits(frontend->regs, SUN4I_FRONTEND_FRM_CTRL_REG,
				  SUN4I_FRONTEND_FRM_CTRL_COEF_ACCESS_CTRL,
				  SUN4I_FRONTEND_FRM_CTRL_COEF_ACCESS_CTRL);

	for (i = 0; i < 32; i++) {
		regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_HORZCOEF0_REG(i),
			     sun4i_frontend_horz_coef[2 * i]);
		regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_HORZCOEF0_REG(i),
			     sun4i_frontend_horz_coef[2 * i]);
		regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_HORZCOEF1_REG(i),
			     sun4i_frontend_horz_coef[2 * i + 1]);
		regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_HORZCOEF1_REG(i),
			     sun4i_frontend_horz_coef[2 * i + 1]);
		regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_VERTCOEF_REG(i),
			     sun4i_frontend_vert_coef[i]);
		regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_VERTCOEF_REG(i),
			     sun4i_frontend_vert_coef[i]);
	}

	if (frontend->data->has_coef_rdy)
		regmap_write_bits(frontend->regs,
				  SUN4I_FRONTEND_FRM_CTRL_REG,
				  SUN4I_FRONTEND_FRM_CTRL_COEF_RDY,
				  SUN4I_FRONTEND_FRM_CTRL_COEF_RDY);
}

int sun4i_frontend_init(struct sun4i_frontend *frontend)
{
	return pm_runtime_get_sync(frontend->dev);
}
EXPORT_SYMBOL(sun4i_frontend_init);

void sun4i_frontend_exit(struct sun4i_frontend *frontend)
{
	pm_runtime_put(frontend->dev);
}
EXPORT_SYMBOL(sun4i_frontend_exit);

static bool sun4i_frontend_format_chroma_requires_swap(uint32_t fmt)
{
	switch (fmt) {
	case DRM_FORMAT_YVU411:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YVU422:
	case DRM_FORMAT_YVU444:
		return true;

	default:
		return false;
	}
}

static bool sun4i_frontend_format_supports_tiling(uint32_t fmt)
{
	switch (fmt) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV61:
	case DRM_FORMAT_YUV411:
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YVU422:
	case DRM_FORMAT_YVU411:
		return true;

	default:
		return false;
	}
}

void sun4i_frontend_update_buffer(struct sun4i_frontend *frontend,
				  struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	unsigned int strides[3] = {};

	dma_addr_t dma_addr;
	bool swap;

	if (fb->modifier == DRM_FORMAT_MOD_ALLWINNER_TILED) {
		unsigned int width = state->src_w >> 16;
		unsigned int offset;

		strides[0] = SUN4I_FRONTEND_LINESTRD_TILED(fb->pitches[0]);

		/*
		 * The X1 offset is the offset to the bottom-right point in the
		 * end tile, which is the final pixel (at offset width - 1)
		 * within the end tile (with a 32-byte mask).
		 */
		offset = (width - 1) & (32 - 1);

		regmap_write(frontend->regs, SUN4I_FRONTEND_TB_OFF0_REG,
			     SUN4I_FRONTEND_TB_OFF_X1(offset));

		if (fb->format->num_planes > 1) {
			strides[1] =
				SUN4I_FRONTEND_LINESTRD_TILED(fb->pitches[1]);

			regmap_write(frontend->regs, SUN4I_FRONTEND_TB_OFF1_REG,
				     SUN4I_FRONTEND_TB_OFF_X1(offset));
		}

		if (fb->format->num_planes > 2) {
			strides[2] =
				SUN4I_FRONTEND_LINESTRD_TILED(fb->pitches[2]);

			regmap_write(frontend->regs, SUN4I_FRONTEND_TB_OFF2_REG,
				     SUN4I_FRONTEND_TB_OFF_X1(offset));
		}
	} else {
		strides[0] = fb->pitches[0];

		if (fb->format->num_planes > 1)
			strides[1] = fb->pitches[1];

		if (fb->format->num_planes > 2)
			strides[2] = fb->pitches[2];
	}

	/* Set the line width */
	DRM_DEBUG_DRIVER("Frontend stride: %d bytes\n", fb->pitches[0]);
	regmap_write(frontend->regs, SUN4I_FRONTEND_LINESTRD0_REG,
		     strides[0]);

	if (fb->format->num_planes > 1)
		regmap_write(frontend->regs, SUN4I_FRONTEND_LINESTRD1_REG,
			     strides[1]);

	if (fb->format->num_planes > 2)
		regmap_write(frontend->regs, SUN4I_FRONTEND_LINESTRD2_REG,
			     strides[2]);

	/* Some planar formats require chroma channel swapping by hand. */
	swap = sun4i_frontend_format_chroma_requires_swap(fb->format->format);

	/* Set the physical address of the buffer in memory */
	dma_addr = drm_fb_dma_get_gem_addr(fb, state, 0);
	DRM_DEBUG_DRIVER("Setting buffer #0 address to %pad\n", &dma_addr);
	regmap_write(frontend->regs, SUN4I_FRONTEND_BUF_ADDR0_REG, dma_addr);

	if (fb->format->num_planes > 1) {
		dma_addr = drm_fb_dma_get_gem_addr(fb, state, swap ? 2 : 1);
		DRM_DEBUG_DRIVER("Setting buffer #1 address to %pad\n",
				 &dma_addr);
		regmap_write(frontend->regs, SUN4I_FRONTEND_BUF_ADDR1_REG,
			     dma_addr);
	}

	if (fb->format->num_planes > 2) {
		dma_addr = drm_fb_dma_get_gem_addr(fb, state, swap ? 1 : 2);
		DRM_DEBUG_DRIVER("Setting buffer #2 address to %pad\n",
				 &dma_addr);
		regmap_write(frontend->regs, SUN4I_FRONTEND_BUF_ADDR2_REG,
			     dma_addr);
	}
}
EXPORT_SYMBOL(sun4i_frontend_update_buffer);

static int
sun4i_frontend_drm_format_to_input_fmt(const struct drm_format_info *format,
				       u32 *val)
{
	if (!format->is_yuv)
		*val = SUN4I_FRONTEND_INPUT_FMT_DATA_FMT_RGB;
	else if (drm_format_info_is_yuv_sampling_411(format))
		*val = SUN4I_FRONTEND_INPUT_FMT_DATA_FMT_YUV411;
	else if (drm_format_info_is_yuv_sampling_420(format))
		*val = SUN4I_FRONTEND_INPUT_FMT_DATA_FMT_YUV420;
	else if (drm_format_info_is_yuv_sampling_422(format))
		*val = SUN4I_FRONTEND_INPUT_FMT_DATA_FMT_YUV422;
	else if (drm_format_info_is_yuv_sampling_444(format))
		*val = SUN4I_FRONTEND_INPUT_FMT_DATA_FMT_YUV444;
	else
		return -EINVAL;

	return 0;
}

static int
sun4i_frontend_drm_format_to_input_mode(const struct drm_format_info *format,
					uint64_t modifier, u32 *val)
{
	bool tiled = (modifier == DRM_FORMAT_MOD_ALLWINNER_TILED);

	switch (format->num_planes) {
	case 1:
		*val = SUN4I_FRONTEND_INPUT_FMT_DATA_MOD_PACKED;
		return 0;

	case 2:
		*val = tiled ? SUN4I_FRONTEND_INPUT_FMT_DATA_MOD_MB32_SEMIPLANAR
			     : SUN4I_FRONTEND_INPUT_FMT_DATA_MOD_SEMIPLANAR;
		return 0;

	case 3:
		*val = tiled ? SUN4I_FRONTEND_INPUT_FMT_DATA_MOD_MB32_PLANAR
			     : SUN4I_FRONTEND_INPUT_FMT_DATA_MOD_PLANAR;
		return 0;

	default:
		return -EINVAL;
	}
}

static int
sun4i_frontend_drm_format_to_input_sequence(const struct drm_format_info *format,
					    u32 *val)
{
	/* Planar formats have an explicit input sequence. */
	if (drm_format_info_is_yuv_planar(format)) {
		*val = 0;
		return 0;
	}

	switch (format->format) {
	case DRM_FORMAT_BGRX8888:
		*val = SUN4I_FRONTEND_INPUT_FMT_DATA_PS_BGRX;
		return 0;

	case DRM_FORMAT_NV12:
		*val = SUN4I_FRONTEND_INPUT_FMT_DATA_PS_UV;
		return 0;

	case DRM_FORMAT_NV16:
		*val = SUN4I_FRONTEND_INPUT_FMT_DATA_PS_UV;
		return 0;

	case DRM_FORMAT_NV21:
		*val = SUN4I_FRONTEND_INPUT_FMT_DATA_PS_VU;
		return 0;

	case DRM_FORMAT_NV61:
		*val = SUN4I_FRONTEND_INPUT_FMT_DATA_PS_VU;
		return 0;

	case DRM_FORMAT_UYVY:
		*val = SUN4I_FRONTEND_INPUT_FMT_DATA_PS_UYVY;
		return 0;

	case DRM_FORMAT_VYUY:
		*val = SUN4I_FRONTEND_INPUT_FMT_DATA_PS_VYUY;
		return 0;

	case DRM_FORMAT_XRGB8888:
		*val = SUN4I_FRONTEND_INPUT_FMT_DATA_PS_XRGB;
		return 0;

	case DRM_FORMAT_YUYV:
		*val = SUN4I_FRONTEND_INPUT_FMT_DATA_PS_YUYV;
		return 0;

	case DRM_FORMAT_YVYU:
		*val = SUN4I_FRONTEND_INPUT_FMT_DATA_PS_YVYU;
		return 0;

	default:
		return -EINVAL;
	}
}

static int sun4i_frontend_drm_format_to_output_fmt(uint32_t fmt, u32 *val)
{
	switch (fmt) {
	case DRM_FORMAT_BGRX8888:
		*val = SUN4I_FRONTEND_OUTPUT_FMT_DATA_FMT_BGRX8888;
		return 0;

	case DRM_FORMAT_XRGB8888:
		*val = SUN4I_FRONTEND_OUTPUT_FMT_DATA_FMT_XRGB8888;
		return 0;

	default:
		return -EINVAL;
	}
}

static const uint32_t sun4i_frontend_formats[] = {
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV16,
	DRM_FORMAT_NV21,
	DRM_FORMAT_NV61,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_YUV411,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YUV422,
	DRM_FORMAT_YUV444,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVU411,
	DRM_FORMAT_YVU420,
	DRM_FORMAT_YVU422,
	DRM_FORMAT_YVU444,
	DRM_FORMAT_YVYU,
};

bool sun4i_frontend_format_is_supported(uint32_t fmt, uint64_t modifier)
{
	unsigned int i;

	if (modifier == DRM_FORMAT_MOD_ALLWINNER_TILED)
		return sun4i_frontend_format_supports_tiling(fmt);
	else if (modifier != DRM_FORMAT_MOD_LINEAR)
		return false;

	for (i = 0; i < ARRAY_SIZE(sun4i_frontend_formats); i++)
		if (sun4i_frontend_formats[i] == fmt)
			return true;

	return false;
}
EXPORT_SYMBOL(sun4i_frontend_format_is_supported);

int sun4i_frontend_update_formats(struct sun4i_frontend *frontend,
				  struct drm_plane *plane, uint32_t out_fmt)
{
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	const struct drm_format_info *format = fb->format;
	uint64_t modifier = fb->modifier;
	unsigned int ch1_phase_idx;
	u32 out_fmt_val;
	u32 in_fmt_val, in_mod_val, in_ps_val;
	unsigned int i;
	u32 bypass;
	int ret;

	ret = sun4i_frontend_drm_format_to_input_fmt(format, &in_fmt_val);
	if (ret) {
		DRM_DEBUG_DRIVER("Invalid input format\n");
		return ret;
	}

	ret = sun4i_frontend_drm_format_to_input_mode(format, modifier,
						      &in_mod_val);
	if (ret) {
		DRM_DEBUG_DRIVER("Invalid input mode\n");
		return ret;
	}

	ret = sun4i_frontend_drm_format_to_input_sequence(format, &in_ps_val);
	if (ret) {
		DRM_DEBUG_DRIVER("Invalid pixel sequence\n");
		return ret;
	}

	ret = sun4i_frontend_drm_format_to_output_fmt(out_fmt, &out_fmt_val);
	if (ret) {
		DRM_DEBUG_DRIVER("Invalid output format\n");
		return ret;
	}

	/*
	 * I have no idea what this does exactly, but it seems to be
	 * related to the scaler FIR filter phase parameters.
	 */
	ch1_phase_idx = (format->num_planes > 1) ? 1 : 0;
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_HORZPHASE_REG,
		     frontend->data->ch_phase[0]);
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_HORZPHASE_REG,
		     frontend->data->ch_phase[ch1_phase_idx]);
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_VERTPHASE0_REG,
		     frontend->data->ch_phase[0]);
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_VERTPHASE0_REG,
		     frontend->data->ch_phase[ch1_phase_idx]);
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_VERTPHASE1_REG,
		     frontend->data->ch_phase[0]);
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_VERTPHASE1_REG,
		     frontend->data->ch_phase[ch1_phase_idx]);

	/*
	 * Checking the input format is sufficient since we currently only
	 * support RGB output formats to the backend. If YUV output formats
	 * ever get supported, an YUV input and output would require bypassing
	 * the CSC engine too.
	 */
	if (format->is_yuv) {
		/* Setup the CSC engine for YUV to RGB conversion. */
		bypass = 0;

		for (i = 0; i < ARRAY_SIZE(sunxi_bt601_yuv2rgb_coef); i++)
			regmap_write(frontend->regs,
				     SUN4I_FRONTEND_CSC_COEF_REG(i),
				     sunxi_bt601_yuv2rgb_coef[i]);
	} else {
		bypass = SUN4I_FRONTEND_BYPASS_CSC_EN;
	}

	regmap_update_bits(frontend->regs, SUN4I_FRONTEND_BYPASS_REG,
			   SUN4I_FRONTEND_BYPASS_CSC_EN, bypass);

	regmap_write(frontend->regs, SUN4I_FRONTEND_INPUT_FMT_REG,
		     in_mod_val | in_fmt_val | in_ps_val);

	/*
	 * TODO: It look like the A31 and A80 at least will need the
	 * bit 7 (ALPHA_EN) enabled when using a format with alpha (so
	 * ARGB8888).
	 */
	regmap_write(frontend->regs, SUN4I_FRONTEND_OUTPUT_FMT_REG,
		     out_fmt_val);

	return 0;
}
EXPORT_SYMBOL(sun4i_frontend_update_formats);

void sun4i_frontend_update_coord(struct sun4i_frontend *frontend,
				 struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	uint32_t luma_width, luma_height;
	uint32_t chroma_width, chroma_height;

	/* Set height and width */
	DRM_DEBUG_DRIVER("Frontend size W: %u H: %u\n",
			 state->crtc_w, state->crtc_h);

	luma_width = state->src_w >> 16;
	luma_height = state->src_h >> 16;

	chroma_width = DIV_ROUND_UP(luma_width, fb->format->hsub);
	chroma_height = DIV_ROUND_UP(luma_height, fb->format->vsub);

	regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_INSIZE_REG,
		     SUN4I_FRONTEND_INSIZE(luma_height, luma_width));
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_INSIZE_REG,
		     SUN4I_FRONTEND_INSIZE(chroma_height, chroma_width));

	regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_OUTSIZE_REG,
		     SUN4I_FRONTEND_OUTSIZE(state->crtc_h, state->crtc_w));
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_OUTSIZE_REG,
		     SUN4I_FRONTEND_OUTSIZE(state->crtc_h, state->crtc_w));

	regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_HORZFACT_REG,
		     (luma_width << 16) / state->crtc_w);
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_HORZFACT_REG,
		     (chroma_width << 16) / state->crtc_w);

	regmap_write(frontend->regs, SUN4I_FRONTEND_CH0_VERTFACT_REG,
		     (luma_height << 16) / state->crtc_h);
	regmap_write(frontend->regs, SUN4I_FRONTEND_CH1_VERTFACT_REG,
		     (chroma_height << 16) / state->crtc_h);

	regmap_write_bits(frontend->regs, SUN4I_FRONTEND_FRM_CTRL_REG,
			  SUN4I_FRONTEND_FRM_CTRL_REG_RDY,
			  SUN4I_FRONTEND_FRM_CTRL_REG_RDY);
}
EXPORT_SYMBOL(sun4i_frontend_update_coord);

int sun4i_frontend_enable(struct sun4i_frontend *frontend)
{
	regmap_write_bits(frontend->regs, SUN4I_FRONTEND_FRM_CTRL_REG,
			  SUN4I_FRONTEND_FRM_CTRL_FRM_START,
			  SUN4I_FRONTEND_FRM_CTRL_FRM_START);

	return 0;
}
EXPORT_SYMBOL(sun4i_frontend_enable);

static const struct regmap_config sun4i_frontend_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x0a14,
};

static int sun4i_frontend_bind(struct device *dev, struct device *master,
			 void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sun4i_frontend *frontend;
	struct drm_device *drm = data;
	struct sun4i_drv *drv = drm->dev_private;
	void __iomem *regs;

	frontend = devm_kzalloc(dev, sizeof(*frontend), GFP_KERNEL);
	if (!frontend)
		return -ENOMEM;

	dev_set_drvdata(dev, frontend);
	frontend->dev = dev;
	frontend->node = dev->of_node;

	frontend->data = of_device_get_match_data(dev);
	if (!frontend->data)
		return -ENODEV;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	frontend->regs = devm_regmap_init_mmio(dev, regs,
					       &sun4i_frontend_regmap_config);
	if (IS_ERR(frontend->regs)) {
		dev_err(dev, "Couldn't create the frontend regmap\n");
		return PTR_ERR(frontend->regs);
	}

	frontend->reset = devm_reset_control_get(dev, NULL);
	if (IS_ERR(frontend->reset)) {
		dev_err(dev, "Couldn't get our reset line\n");
		return PTR_ERR(frontend->reset);
	}

	frontend->bus_clk = devm_clk_get(dev, "ahb");
	if (IS_ERR(frontend->bus_clk)) {
		dev_err(dev, "Couldn't get our bus clock\n");
		return PTR_ERR(frontend->bus_clk);
	}

	frontend->mod_clk = devm_clk_get(dev, "mod");
	if (IS_ERR(frontend->mod_clk)) {
		dev_err(dev, "Couldn't get our mod clock\n");
		return PTR_ERR(frontend->mod_clk);
	}

	frontend->ram_clk = devm_clk_get(dev, "ram");
	if (IS_ERR(frontend->ram_clk)) {
		dev_err(dev, "Couldn't get our ram clock\n");
		return PTR_ERR(frontend->ram_clk);
	}

	list_add_tail(&frontend->list, &drv->frontend_list);
	pm_runtime_enable(dev);

	return 0;
}

static void sun4i_frontend_unbind(struct device *dev, struct device *master,
			    void *data)
{
	struct sun4i_frontend *frontend = dev_get_drvdata(dev);

	list_del(&frontend->list);
	pm_runtime_force_suspend(dev);
}

static const struct component_ops sun4i_frontend_ops = {
	.bind	= sun4i_frontend_bind,
	.unbind	= sun4i_frontend_unbind,
};

static int sun4i_frontend_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &sun4i_frontend_ops);
}

static void sun4i_frontend_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &sun4i_frontend_ops);
}

static int sun4i_frontend_runtime_resume(struct device *dev)
{
	struct sun4i_frontend *frontend = dev_get_drvdata(dev);
	int ret;

	clk_set_rate(frontend->mod_clk, 300000000);

	clk_prepare_enable(frontend->bus_clk);
	clk_prepare_enable(frontend->mod_clk);
	clk_prepare_enable(frontend->ram_clk);

	ret = reset_control_reset(frontend->reset);
	if (ret) {
		dev_err(dev, "Couldn't reset our device\n");
		return ret;
	}

	regmap_update_bits(frontend->regs, SUN4I_FRONTEND_EN_REG,
			   SUN4I_FRONTEND_EN_EN,
			   SUN4I_FRONTEND_EN_EN);

	sun4i_frontend_scaler_init(frontend);

	return 0;
}

static int sun4i_frontend_runtime_suspend(struct device *dev)
{
	struct sun4i_frontend *frontend = dev_get_drvdata(dev);

	clk_disable_unprepare(frontend->ram_clk);
	clk_disable_unprepare(frontend->mod_clk);
	clk_disable_unprepare(frontend->bus_clk);

	reset_control_assert(frontend->reset);

	return 0;
}

static const struct dev_pm_ops sun4i_frontend_pm_ops = {
	.runtime_resume		= sun4i_frontend_runtime_resume,
	.runtime_suspend	= sun4i_frontend_runtime_suspend,
};

static const struct sun4i_frontend_data sun4i_a10_frontend = {
	.ch_phase		= { 0x000, 0xfc000 },
	.has_coef_rdy		= true,
};

static const struct sun4i_frontend_data sun8i_a33_frontend = {
	.ch_phase		= { 0x400, 0xfc400 },
	.has_coef_access_ctrl	= true,
};

const struct of_device_id sun4i_frontend_of_table[] = {
	{
		.compatible = "allwinner,sun4i-a10-display-frontend",
		.data = &sun4i_a10_frontend
	},
	{
		.compatible = "allwinner,sun7i-a20-display-frontend",
		.data = &sun4i_a10_frontend
	},
	{
		.compatible = "allwinner,sun8i-a23-display-frontend",
		.data = &sun8i_a33_frontend
	},
	{
		.compatible = "allwinner,sun8i-a33-display-frontend",
		.data = &sun8i_a33_frontend
	},
	{ }
};
EXPORT_SYMBOL(sun4i_frontend_of_table);
MODULE_DEVICE_TABLE(of, sun4i_frontend_of_table);

static struct platform_driver sun4i_frontend_driver = {
	.probe		= sun4i_frontend_probe,
	.remove_new	= sun4i_frontend_remove,
	.driver		= {
		.name		= "sun4i-frontend",
		.of_match_table	= sun4i_frontend_of_table,
		.pm		= &sun4i_frontend_pm_ops,
	},
};
module_platform_driver(sun4i_frontend_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner A10 Display Engine Frontend Driver");
MODULE_LICENSE("GPL");
