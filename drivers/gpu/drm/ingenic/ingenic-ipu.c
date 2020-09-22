// SPDX-License-Identifier: GPL-2.0
//
// Ingenic JZ47xx IPU driver
//
// Copyright (C) 2020, Paul Cercueil <paul@crapouillou.net>
// Copyright (C) 2020, Daniel Silsby <dansilsby@gmail.com>

#include "ingenic-drm.h"
#include "ingenic-ipu.h"

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/gcd.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/time.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_plane.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_property.h>
#include <drm/drm_vblank.h>

struct ingenic_ipu;

struct soc_info {
	const u32 *formats;
	size_t num_formats;
	bool has_bicubic;
	bool manual_restart;

	void (*set_coefs)(struct ingenic_ipu *ipu, unsigned int reg,
			  unsigned int sharpness, bool downscale,
			  unsigned int weight, unsigned int offset);
};

struct ingenic_ipu {
	struct drm_plane plane;
	struct drm_device *drm;
	struct device *dev, *master;
	struct regmap *map;
	struct clk *clk;
	const struct soc_info *soc_info;
	bool clk_enabled;

	unsigned int num_w, num_h, denom_w, denom_h;

	dma_addr_t addr_y, addr_u, addr_v;

	struct drm_property *sharpness_prop;
	unsigned int sharpness;
};

/* Signed 15.16 fixed-point math (for bicubic scaling coefficients) */
#define I2F(i) ((s32)(i) * 65536)
#define F2I(f) ((f) / 65536)
#define FMUL(fa, fb) ((s32)(((s64)(fa) * (s64)(fb)) / 65536))
#define SHARPNESS_INCR (I2F(-1) / 8)

static inline struct ingenic_ipu *plane_to_ingenic_ipu(struct drm_plane *plane)
{
	return container_of(plane, struct ingenic_ipu, plane);
}

/*
 * Apply conventional cubic convolution kernel. Both parameters
 *  and return value are 15.16 signed fixed-point.
 *
 *  @f_a: Sharpness factor, typically in range [-4.0, -0.25].
 *        A larger magnitude increases perceived sharpness, but going past
 *        -2.0 might cause ringing artifacts to outweigh any improvement.
 *        Nice values on a 320x240 LCD are between -0.75 and -2.0.
 *
 *  @f_x: Absolute distance in pixels from 'pixel 0' sample position
 *        along horizontal (or vertical) source axis. Range is [0, +2.0].
 *
 *  returns: Weight of this pixel within 4-pixel sample group. Range is
 *           [-2.0, +2.0]. For moderate (i.e. > -3.0) sharpness factors,
 *           range is within [-1.0, +1.0].
 */
static inline s32 cubic_conv(s32 f_a, s32 f_x)
{
	const s32 f_1 = I2F(1);
	const s32 f_2 = I2F(2);
	const s32 f_3 = I2F(3);
	const s32 f_4 = I2F(4);
	const s32 f_x2 = FMUL(f_x, f_x);
	const s32 f_x3 = FMUL(f_x, f_x2);

	if (f_x <= f_1)
		return FMUL((f_a + f_2), f_x3) - FMUL((f_a + f_3), f_x2) + f_1;
	else if (f_x <= f_2)
		return FMUL(f_a, (f_x3 - 5 * f_x2 + 8 * f_x - f_4));
	else
		return 0;
}

/*
 * On entry, "weight" is a coefficient suitable for bilinear mode,
 *  which is converted to a set of four suitable for bicubic mode.
 *
 * "weight 512" means all of pixel 0;
 * "weight 256" means half of pixel 0 and half of pixel 1;
 * "weight 0" means all of pixel 1;
 *
 * "offset" is increment to next source pixel sample location.
 */
static void jz4760_set_coefs(struct ingenic_ipu *ipu, unsigned int reg,
			     unsigned int sharpness, bool downscale,
			     unsigned int weight, unsigned int offset)
{
	u32 val;
	s32 w0, w1, w2, w3; /* Pixel weights at X (or Y) offsets -1,0,1,2 */

	weight = clamp_val(weight, 0, 512);

	if (sharpness < 2) {
		/*
		 *  When sharpness setting is 0, emulate nearest-neighbor.
		 *  When sharpness setting is 1, emulate bilinear.
		 */

		if (sharpness == 0)
			weight = weight >= 256 ? 512 : 0;
		w0 = 0;
		w1 = weight;
		w2 = 512 - weight;
		w3 = 0;
	} else {
		const s32 f_a = SHARPNESS_INCR * sharpness;
		const s32 f_h = I2F(1) / 2; /* Round up 0.5 */

		/*
		 * Note that always rounding towards +infinity here is intended.
		 * The resulting coefficients match a round-to-nearest-int
		 * double floating-point implementation.
		 */

		weight = 512 - weight;
		w0 = F2I(f_h + 512 * cubic_conv(f_a, I2F(512  + weight) / 512));
		w1 = F2I(f_h + 512 * cubic_conv(f_a, I2F(0    + weight) / 512));
		w2 = F2I(f_h + 512 * cubic_conv(f_a, I2F(512  - weight) / 512));
		w3 = F2I(f_h + 512 * cubic_conv(f_a, I2F(1024 - weight) / 512));
		w0 = clamp_val(w0, -1024, 1023);
		w1 = clamp_val(w1, -1024, 1023);
		w2 = clamp_val(w2, -1024, 1023);
		w3 = clamp_val(w3, -1024, 1023);
	}

	val = ((w1 & JZ4760_IPU_RSZ_COEF_MASK) << JZ4760_IPU_RSZ_COEF31_LSB) |
		((w0 & JZ4760_IPU_RSZ_COEF_MASK) << JZ4760_IPU_RSZ_COEF20_LSB);
	regmap_write(ipu->map, reg, val);

	val = ((w3 & JZ4760_IPU_RSZ_COEF_MASK) << JZ4760_IPU_RSZ_COEF31_LSB) |
		((w2 & JZ4760_IPU_RSZ_COEF_MASK) << JZ4760_IPU_RSZ_COEF20_LSB) |
		((offset & JZ4760_IPU_RSZ_OFFSET_MASK) << JZ4760_IPU_RSZ_OFFSET_LSB);
	regmap_write(ipu->map, reg, val);
}

static void jz4725b_set_coefs(struct ingenic_ipu *ipu, unsigned int reg,
			      unsigned int sharpness, bool downscale,
			      unsigned int weight, unsigned int offset)
{
	u32 val = JZ4725B_IPU_RSZ_LUT_OUT_EN;
	unsigned int i;

	weight = clamp_val(weight, 0, 512);

	if (sharpness == 0)
		weight = weight >= 256 ? 512 : 0;

	val |= (weight & JZ4725B_IPU_RSZ_LUT_COEF_MASK) << JZ4725B_IPU_RSZ_LUT_COEF_LSB;
	if (downscale || !!offset)
		val |= JZ4725B_IPU_RSZ_LUT_IN_EN;

	regmap_write(ipu->map, reg, val);

	if (downscale) {
		for (i = 1; i < offset; i++)
			regmap_write(ipu->map, reg, JZ4725B_IPU_RSZ_LUT_IN_EN);
	}
}

static void ingenic_ipu_set_downscale_coefs(struct ingenic_ipu *ipu,
					    unsigned int reg,
					    unsigned int num,
					    unsigned int denom)
{
	unsigned int i, offset, weight, weight_num = denom;

	for (i = 0; i < num; i++) {
		weight_num = num + (weight_num - num) % (num * 2);
		weight = 512 - 512 * (weight_num - num) / (num * 2);
		weight_num += denom * 2;
		offset = (weight_num - num) / (num * 2);

		ipu->soc_info->set_coefs(ipu, reg, ipu->sharpness,
					 true, weight, offset);
	}
}

static void ingenic_ipu_set_integer_upscale_coefs(struct ingenic_ipu *ipu,
						  unsigned int reg,
						  unsigned int num)
{
	/*
	 * Force nearest-neighbor scaling and use simple math when upscaling
	 * by an integer ratio. It looks better, and fixes a few problem cases.
	 */
	unsigned int i;

	for (i = 0; i < num; i++)
		ipu->soc_info->set_coefs(ipu, reg, 0, false, 512, i == num - 1);
}

static void ingenic_ipu_set_upscale_coefs(struct ingenic_ipu *ipu,
					  unsigned int reg,
					  unsigned int num,
					  unsigned int denom)
{
	unsigned int i, offset, weight, weight_num = 0;

	for (i = 0; i < num; i++) {
		weight = 512 - 512 * weight_num / num;
		weight_num += denom;
		offset = weight_num >= num;

		if (offset)
			weight_num -= num;

		ipu->soc_info->set_coefs(ipu, reg, ipu->sharpness,
					 false, weight, offset);
	}
}

static void ingenic_ipu_set_coefs(struct ingenic_ipu *ipu, unsigned int reg,
				  unsigned int num, unsigned int denom)
{
	/* Begin programming the LUT */
	regmap_write(ipu->map, reg, -1);

	if (denom > num)
		ingenic_ipu_set_downscale_coefs(ipu, reg, num, denom);
	else if (denom == 1)
		ingenic_ipu_set_integer_upscale_coefs(ipu, reg, num);
	else
		ingenic_ipu_set_upscale_coefs(ipu, reg, num, denom);
}

static int reduce_fraction(unsigned int *num, unsigned int *denom)
{
	unsigned long d = gcd(*num, *denom);

	/* The scaling table has only 31 entries */
	if (*num > 31 * d)
		return -EINVAL;

	*num /= d;
	*denom /= d;
	return 0;
}

static inline bool osd_changed(struct drm_plane_state *state,
			       struct drm_plane_state *oldstate)
{
	return state->src_x != oldstate->src_x ||
		state->src_y != oldstate->src_y ||
		state->src_w != oldstate->src_w ||
		state->src_h != oldstate->src_h ||
		state->crtc_x != oldstate->crtc_x ||
		state->crtc_y != oldstate->crtc_y ||
		state->crtc_w != oldstate->crtc_w ||
		state->crtc_h != oldstate->crtc_h;
}

static void ingenic_ipu_plane_atomic_update(struct drm_plane *plane,
					    struct drm_plane_state *oldstate)
{
	struct ingenic_ipu *ipu = plane_to_ingenic_ipu(plane);
	struct drm_plane_state *state = plane->state;
	const struct drm_format_info *finfo;
	u32 ctrl, stride = 0, coef_index = 0, format = 0;
	bool needs_modeset, upscaling_w, upscaling_h;
	int err;

	if (!state || !state->fb)
		return;

	finfo = drm_format_info(state->fb->format->format);

	if (!ipu->clk_enabled) {
		err = clk_enable(ipu->clk);
		if (err) {
			dev_err(ipu->dev, "Unable to enable clock: %d\n", err);
			return;
		}

		ipu->clk_enabled = true;
	}

	/* Reset all the registers if needed */
	needs_modeset = drm_atomic_crtc_needs_modeset(state->crtc->state);
	if (needs_modeset) {
		regmap_set_bits(ipu->map, JZ_REG_IPU_CTRL, JZ_IPU_CTRL_RST);

		/* Enable the chip */
		regmap_set_bits(ipu->map, JZ_REG_IPU_CTRL,
				JZ_IPU_CTRL_CHIP_EN | JZ_IPU_CTRL_LCDC_SEL);
	}

	ingenic_drm_sync_data(ipu->master, oldstate, state);

	/* New addresses will be committed in vblank handler... */
	ipu->addr_y = drm_fb_cma_get_gem_addr(state->fb, state, 0);
	if (finfo->num_planes > 1)
		ipu->addr_u = drm_fb_cma_get_gem_addr(state->fb, state, 1);
	if (finfo->num_planes > 2)
		ipu->addr_v = drm_fb_cma_get_gem_addr(state->fb, state, 2);

	if (!needs_modeset)
		return;

	/* Or right here if we're doing a full modeset. */
	regmap_write(ipu->map, JZ_REG_IPU_Y_ADDR, ipu->addr_y);
	regmap_write(ipu->map, JZ_REG_IPU_U_ADDR, ipu->addr_u);
	regmap_write(ipu->map, JZ_REG_IPU_V_ADDR, ipu->addr_v);

	if (finfo->num_planes == 1)
		regmap_set_bits(ipu->map, JZ_REG_IPU_CTRL, JZ_IPU_CTRL_SPKG_SEL);

	ingenic_drm_plane_config(ipu->master, plane, DRM_FORMAT_XRGB8888);

	/* Set the input height/width/strides */
	if (finfo->num_planes > 2)
		stride = ((state->src_w >> 16) * finfo->cpp[2] / finfo->hsub)
			<< JZ_IPU_UV_STRIDE_V_LSB;

	if (finfo->num_planes > 1)
		stride |= ((state->src_w >> 16) * finfo->cpp[1] / finfo->hsub)
			<< JZ_IPU_UV_STRIDE_U_LSB;

	regmap_write(ipu->map, JZ_REG_IPU_UV_STRIDE, stride);

	stride = ((state->src_w >> 16) * finfo->cpp[0]) << JZ_IPU_Y_STRIDE_Y_LSB;
	regmap_write(ipu->map, JZ_REG_IPU_Y_STRIDE, stride);

	regmap_write(ipu->map, JZ_REG_IPU_IN_GS,
		     (stride << JZ_IPU_IN_GS_W_LSB) |
		     ((state->src_h >> 16) << JZ_IPU_IN_GS_H_LSB));

	switch (finfo->format) {
	case DRM_FORMAT_XRGB1555:
		format = JZ_IPU_D_FMT_IN_FMT_RGB555 |
			JZ_IPU_D_FMT_RGB_OUT_OFT_RGB;
		break;
	case DRM_FORMAT_XBGR1555:
		format = JZ_IPU_D_FMT_IN_FMT_RGB555 |
			JZ_IPU_D_FMT_RGB_OUT_OFT_BGR;
		break;
	case DRM_FORMAT_RGB565:
		format = JZ_IPU_D_FMT_IN_FMT_RGB565 |
			JZ_IPU_D_FMT_RGB_OUT_OFT_RGB;
		break;
	case DRM_FORMAT_BGR565:
		format = JZ_IPU_D_FMT_IN_FMT_RGB565 |
			JZ_IPU_D_FMT_RGB_OUT_OFT_BGR;
		break;
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_XYUV8888:
		format = JZ_IPU_D_FMT_IN_FMT_RGB888 |
			JZ_IPU_D_FMT_RGB_OUT_OFT_RGB;
		break;
	case DRM_FORMAT_XBGR8888:
		format = JZ_IPU_D_FMT_IN_FMT_RGB888 |
			JZ_IPU_D_FMT_RGB_OUT_OFT_BGR;
		break;
	case DRM_FORMAT_YUYV:
		format = JZ_IPU_D_FMT_IN_FMT_YUV422 |
			JZ_IPU_D_FMT_YUV_VY1UY0;
		break;
	case DRM_FORMAT_YVYU:
		format = JZ_IPU_D_FMT_IN_FMT_YUV422 |
			JZ_IPU_D_FMT_YUV_UY1VY0;
		break;
	case DRM_FORMAT_UYVY:
		format = JZ_IPU_D_FMT_IN_FMT_YUV422 |
			JZ_IPU_D_FMT_YUV_Y1VY0U;
		break;
	case DRM_FORMAT_VYUY:
		format = JZ_IPU_D_FMT_IN_FMT_YUV422 |
			JZ_IPU_D_FMT_YUV_Y1UY0V;
		break;
	case DRM_FORMAT_YUV411:
		format = JZ_IPU_D_FMT_IN_FMT_YUV411;
		break;
	case DRM_FORMAT_YUV420:
		format = JZ_IPU_D_FMT_IN_FMT_YUV420;
		break;
	case DRM_FORMAT_YUV422:
		format = JZ_IPU_D_FMT_IN_FMT_YUV422;
		break;
	case DRM_FORMAT_YUV444:
		format = JZ_IPU_D_FMT_IN_FMT_YUV444;
		break;
	default:
		WARN_ONCE(1, "Unsupported format");
		break;
	}

	/* Fix output to RGB888 */
	format |= JZ_IPU_D_FMT_OUT_FMT_RGB888;

	/* Set pixel format */
	regmap_write(ipu->map, JZ_REG_IPU_D_FMT, format);

	/* Set the output height/width/stride */
	regmap_write(ipu->map, JZ_REG_IPU_OUT_GS,
		     ((state->crtc_w * 4) << JZ_IPU_OUT_GS_W_LSB)
		     | state->crtc_h << JZ_IPU_OUT_GS_H_LSB);
	regmap_write(ipu->map, JZ_REG_IPU_OUT_STRIDE, state->crtc_w * 4);

	if (finfo->is_yuv) {
		regmap_set_bits(ipu->map, JZ_REG_IPU_CTRL, JZ_IPU_CTRL_CSC_EN);

		/*
		 * Offsets for Chroma/Luma.
		 * y = source Y - LUMA,
		 * u = source Cb - CHROMA,
		 * v = source Cr - CHROMA
		 */
		regmap_write(ipu->map, JZ_REG_IPU_CSC_OFFSET,
			     128 << JZ_IPU_CSC_OFFSET_CHROMA_LSB |
			     0 << JZ_IPU_CSC_OFFSET_LUMA_LSB);

		/*
		 * YUV422 to RGB conversion table.
		 * R = C0 / 0x400 * y + C1 / 0x400 * v
		 * G = C0 / 0x400 * y - C2 / 0x400 * u - C3 / 0x400 * v
		 * B = C0 / 0x400 * y + C4 / 0x400 * u
		 */
		regmap_write(ipu->map, JZ_REG_IPU_CSC_C0_COEF, 0x4a8);
		regmap_write(ipu->map, JZ_REG_IPU_CSC_C1_COEF, 0x662);
		regmap_write(ipu->map, JZ_REG_IPU_CSC_C2_COEF, 0x191);
		regmap_write(ipu->map, JZ_REG_IPU_CSC_C3_COEF, 0x341);
		regmap_write(ipu->map, JZ_REG_IPU_CSC_C4_COEF, 0x811);
	}

	ctrl = 0;

	/*
	 * Must set ZOOM_SEL before programming bicubic LUTs.
	 * If the IPU supports bicubic, we enable it unconditionally, since it
	 * can do anything bilinear can and more.
	 */
	if (ipu->soc_info->has_bicubic)
		ctrl |= JZ_IPU_CTRL_ZOOM_SEL;

	upscaling_w = ipu->num_w > ipu->denom_w;
	if (upscaling_w)
		ctrl |= JZ_IPU_CTRL_HSCALE;

	if (ipu->num_w != 1 || ipu->denom_w != 1) {
		if (!ipu->soc_info->has_bicubic && !upscaling_w)
			coef_index |= (ipu->denom_w - 1) << 16;
		else
			coef_index |= (ipu->num_w - 1) << 16;
		ctrl |= JZ_IPU_CTRL_HRSZ_EN;
	}

	upscaling_h = ipu->num_h > ipu->denom_h;
	if (upscaling_h)
		ctrl |= JZ_IPU_CTRL_VSCALE;

	if (ipu->num_h != 1 || ipu->denom_h != 1) {
		if (!ipu->soc_info->has_bicubic && !upscaling_h)
			coef_index |= ipu->denom_h - 1;
		else
			coef_index |= ipu->num_h - 1;
		ctrl |= JZ_IPU_CTRL_VRSZ_EN;
	}

	regmap_update_bits(ipu->map, JZ_REG_IPU_CTRL, JZ_IPU_CTRL_ZOOM_SEL |
			   JZ_IPU_CTRL_HRSZ_EN | JZ_IPU_CTRL_VRSZ_EN |
			   JZ_IPU_CTRL_HSCALE | JZ_IPU_CTRL_VSCALE, ctrl);

	/* Set the LUT index register */
	regmap_write(ipu->map, JZ_REG_IPU_RSZ_COEF_INDEX, coef_index);

	if (ipu->num_w != 1 || ipu->denom_w != 1)
		ingenic_ipu_set_coefs(ipu, JZ_REG_IPU_HRSZ_COEF_LUT,
				      ipu->num_w, ipu->denom_w);

	if (ipu->num_h != 1 || ipu->denom_h != 1)
		ingenic_ipu_set_coefs(ipu, JZ_REG_IPU_VRSZ_COEF_LUT,
				      ipu->num_h, ipu->denom_h);

	/* Clear STATUS register */
	regmap_write(ipu->map, JZ_REG_IPU_STATUS, 0);

	/* Start IPU */
	regmap_set_bits(ipu->map, JZ_REG_IPU_CTRL,
			JZ_IPU_CTRL_RUN | JZ_IPU_CTRL_FM_IRQ_EN);

	dev_dbg(ipu->dev, "Scaling %ux%u to %ux%u (%u:%u horiz, %u:%u vert)\n",
		state->src_w >> 16, state->src_h >> 16,
		state->crtc_w, state->crtc_h,
		ipu->num_w, ipu->denom_w, ipu->num_h, ipu->denom_h);
}

static int ingenic_ipu_plane_atomic_check(struct drm_plane *plane,
					  struct drm_plane_state *state)
{
	unsigned int num_w, denom_w, num_h, denom_h, xres, yres;
	struct ingenic_ipu *ipu = plane_to_ingenic_ipu(plane);
	struct drm_crtc *crtc = state->crtc ?: plane->state->crtc;
	struct drm_crtc_state *crtc_state;

	if (!crtc)
		return 0;

	crtc_state = drm_atomic_get_existing_crtc_state(state->state, crtc);
	if (WARN_ON(!crtc_state))
		return -EINVAL;

	/* Request a full modeset if we are enabling or disabling the IPU. */
	if (!plane->state->crtc ^ !state->crtc)
		crtc_state->mode_changed = true;

	if (!state->crtc ||
	    !crtc_state->mode.hdisplay || !crtc_state->mode.vdisplay)
		goto out_check_damage;

	/* Plane must be fully visible */
	if (state->crtc_x < 0 || state->crtc_y < 0 ||
	    state->crtc_x + state->crtc_w > crtc_state->mode.hdisplay ||
	    state->crtc_y + state->crtc_h > crtc_state->mode.vdisplay)
		return -EINVAL;

	/* Minimum size is 4x4 */
	if ((state->src_w >> 16) < 4 || (state->src_h >> 16) < 4)
		return -EINVAL;

	/* Input and output lines must have an even number of pixels. */
	if (((state->src_w >> 16) & 1) || (state->crtc_w & 1))
		return -EINVAL;

	if (!osd_changed(state, plane->state))
		goto out_check_damage;

	crtc_state->mode_changed = true;

	xres = state->src_w >> 16;
	yres = state->src_h >> 16;

	/* Adjust the coefficients until we find a valid configuration */
	for (denom_w = xres, num_w = state->crtc_w;
	     num_w <= crtc_state->mode.hdisplay; num_w++)
		if (!reduce_fraction(&num_w, &denom_w))
			break;
	if (num_w > crtc_state->mode.hdisplay)
		return -EINVAL;

	for (denom_h = yres, num_h = state->crtc_h;
	     num_h <= crtc_state->mode.vdisplay; num_h++)
		if (!reduce_fraction(&num_h, &denom_h))
			break;
	if (num_h > crtc_state->mode.vdisplay)
		return -EINVAL;

	ipu->num_w = num_w;
	ipu->num_h = num_h;
	ipu->denom_w = denom_w;
	ipu->denom_h = denom_h;

out_check_damage:
	drm_atomic_helper_check_plane_damage(state->state, state);

	return 0;
}

static void ingenic_ipu_plane_atomic_disable(struct drm_plane *plane,
					     struct drm_plane_state *old_state)
{
	struct ingenic_ipu *ipu = plane_to_ingenic_ipu(plane);

	regmap_set_bits(ipu->map, JZ_REG_IPU_CTRL, JZ_IPU_CTRL_STOP);
	regmap_clear_bits(ipu->map, JZ_REG_IPU_CTRL, JZ_IPU_CTRL_CHIP_EN);

	ingenic_drm_plane_disable(ipu->master, plane);

	if (ipu->clk_enabled) {
		clk_disable(ipu->clk);
		ipu->clk_enabled = false;
	}
}

static const struct drm_plane_helper_funcs ingenic_ipu_plane_helper_funcs = {
	.atomic_update		= ingenic_ipu_plane_atomic_update,
	.atomic_check		= ingenic_ipu_plane_atomic_check,
	.atomic_disable		= ingenic_ipu_plane_atomic_disable,
	.prepare_fb		= drm_gem_fb_prepare_fb,
};

static int
ingenic_ipu_plane_atomic_get_property(struct drm_plane *plane,
				      const struct drm_plane_state *state,
				      struct drm_property *property, u64 *val)
{
	struct ingenic_ipu *ipu = plane_to_ingenic_ipu(plane);

	if (property != ipu->sharpness_prop)
		return -EINVAL;

	*val = ipu->sharpness;

	return 0;
}

static int
ingenic_ipu_plane_atomic_set_property(struct drm_plane *plane,
				      struct drm_plane_state *state,
				      struct drm_property *property, u64 val)
{
	struct ingenic_ipu *ipu = plane_to_ingenic_ipu(plane);
	struct drm_crtc_state *crtc_state;

	if (property != ipu->sharpness_prop)
		return -EINVAL;

	ipu->sharpness = val;

	if (state->crtc) {
		crtc_state = drm_atomic_get_existing_crtc_state(state->state, state->crtc);
		if (WARN_ON(!crtc_state))
			return -EINVAL;

		crtc_state->mode_changed = true;
	}

	return 0;
}

static const struct drm_plane_funcs ingenic_ipu_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.reset			= drm_atomic_helper_plane_reset,
	.destroy		= drm_plane_cleanup,

	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,

	.atomic_get_property	= ingenic_ipu_plane_atomic_get_property,
	.atomic_set_property	= ingenic_ipu_plane_atomic_set_property,
};

static irqreturn_t ingenic_ipu_irq_handler(int irq, void *arg)
{
	struct ingenic_ipu *ipu = arg;
	struct drm_crtc *crtc = drm_crtc_from_index(ipu->drm, 0);
	unsigned int dummy;

	/* dummy read allows CPU to reconfigure IPU */
	if (ipu->soc_info->manual_restart)
		regmap_read(ipu->map, JZ_REG_IPU_STATUS, &dummy);

	/* ACK interrupt */
	regmap_write(ipu->map, JZ_REG_IPU_STATUS, 0);

	/* Set previously cached addresses */
	regmap_write(ipu->map, JZ_REG_IPU_Y_ADDR, ipu->addr_y);
	regmap_write(ipu->map, JZ_REG_IPU_U_ADDR, ipu->addr_u);
	regmap_write(ipu->map, JZ_REG_IPU_V_ADDR, ipu->addr_v);

	/* Run IPU for the new frame */
	if (ipu->soc_info->manual_restart)
		regmap_set_bits(ipu->map, JZ_REG_IPU_CTRL, JZ_IPU_CTRL_RUN);

	drm_crtc_handle_vblank(crtc);

	return IRQ_HANDLED;
}

static const struct regmap_config ingenic_ipu_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,

	.max_register = JZ_REG_IPU_OUT_PHY_T_ADDR,
};

static int ingenic_ipu_bind(struct device *dev, struct device *master, void *d)
{
	struct platform_device *pdev = to_platform_device(dev);
	const struct soc_info *soc_info;
	struct drm_device *drm = d;
	struct drm_plane *plane;
	struct ingenic_ipu *ipu;
	void __iomem *base;
	unsigned int sharpness_max;
	int err, irq;

	ipu = devm_kzalloc(dev, sizeof(*ipu), GFP_KERNEL);
	if (!ipu)
		return -ENOMEM;

	soc_info = of_device_get_match_data(dev);
	if (!soc_info) {
		dev_err(dev, "Missing platform data\n");
		return -EINVAL;
	}

	ipu->dev = dev;
	ipu->drm = drm;
	ipu->master = master;
	ipu->soc_info = soc_info;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		dev_err(dev, "Failed to get memory resource\n");
		return PTR_ERR(base);
	}

	ipu->map = devm_regmap_init_mmio(dev, base, &ingenic_ipu_regmap_config);
	if (IS_ERR(ipu->map)) {
		dev_err(dev, "Failed to create regmap\n");
		return PTR_ERR(ipu->map);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ipu->clk = devm_clk_get(dev, "ipu");
	if (IS_ERR(ipu->clk)) {
		dev_err(dev, "Failed to get pixel clock\n");
		return PTR_ERR(ipu->clk);
	}

	err = devm_request_irq(dev, irq, ingenic_ipu_irq_handler, 0,
			       dev_name(dev), ipu);
	if (err) {
		dev_err(dev, "Unable to request IRQ\n");
		return err;
	}

	plane = &ipu->plane;
	dev_set_drvdata(dev, plane);

	drm_plane_helper_add(plane, &ingenic_ipu_plane_helper_funcs);

	err = drm_universal_plane_init(drm, plane, 1, &ingenic_ipu_plane_funcs,
				       soc_info->formats, soc_info->num_formats,
				       NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (err) {
		dev_err(dev, "Failed to init plane: %i\n", err);
		return err;
	}

	drm_plane_enable_fb_damage_clips(plane);

	/*
	 * Sharpness settings range is [0,32]
	 * 0       : nearest-neighbor
	 * 1       : bilinear
	 * 2 .. 32 : bicubic (translated to sharpness factor -0.25 .. -4.0)
	 */
	sharpness_max = soc_info->has_bicubic ? 32 : 1;
	ipu->sharpness_prop = drm_property_create_range(drm, 0, "sharpness",
							0, sharpness_max);
	if (!ipu->sharpness_prop) {
		dev_err(dev, "Unable to create sharpness property\n");
		return -ENOMEM;
	}

	/* Default sharpness factor: -0.125 * 8 = -1.0 */
	ipu->sharpness = soc_info->has_bicubic ? 8 : 1;
	drm_object_attach_property(&plane->base, ipu->sharpness_prop,
				   ipu->sharpness);

	err = clk_prepare(ipu->clk);
	if (err) {
		dev_err(dev, "Unable to prepare clock\n");
		return err;
	}

	return 0;
}

static void ingenic_ipu_unbind(struct device *dev,
			       struct device *master, void *d)
{
	struct ingenic_ipu *ipu = dev_get_drvdata(dev);

	clk_unprepare(ipu->clk);
}

static const struct component_ops ingenic_ipu_ops = {
	.bind = ingenic_ipu_bind,
	.unbind = ingenic_ipu_unbind,
};

static int ingenic_ipu_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &ingenic_ipu_ops);
}

static int ingenic_ipu_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &ingenic_ipu_ops);
	return 0;
}

static const u32 jz4725b_ipu_formats[] = {
	/*
	 * While officially supported, packed YUV 4:2:2 formats can cause
	 * random hardware crashes on JZ4725B under certain circumstances.
	 * It seems to happen with some specific resize ratios.
	 * Until a proper workaround or fix is found, disable these formats.
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	*/
	DRM_FORMAT_YUV411,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YUV422,
	DRM_FORMAT_YUV444,
};

static const struct soc_info jz4725b_soc_info = {
	.formats	= jz4725b_ipu_formats,
	.num_formats	= ARRAY_SIZE(jz4725b_ipu_formats),
	.has_bicubic	= false,
	.manual_restart	= true,
	.set_coefs	= jz4725b_set_coefs,
};

static const u32 jz4760_ipu_formats[] = {
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_YUV411,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YUV422,
	DRM_FORMAT_YUV444,
	DRM_FORMAT_XYUV8888,
};

static const struct soc_info jz4760_soc_info = {
	.formats	= jz4760_ipu_formats,
	.num_formats	= ARRAY_SIZE(jz4760_ipu_formats),
	.has_bicubic	= true,
	.manual_restart	= false,
	.set_coefs	= jz4760_set_coefs,
};

static const struct of_device_id ingenic_ipu_of_match[] = {
	{ .compatible = "ingenic,jz4725b-ipu", .data = &jz4725b_soc_info },
	{ .compatible = "ingenic,jz4760-ipu", .data = &jz4760_soc_info },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, ingenic_ipu_of_match);

static struct platform_driver ingenic_ipu_driver = {
	.driver = {
		.name = "ingenic-ipu",
		.of_match_table = ingenic_ipu_of_match,
	},
	.probe = ingenic_ipu_probe,
	.remove = ingenic_ipu_remove,
};

struct platform_driver *ingenic_ipu_driver_ptr = &ingenic_ipu_driver;
