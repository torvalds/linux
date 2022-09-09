// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#include <linux/component.h>
#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/of_irq.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_plane_helper.h>

#include "sprd_drm.h"
#include "sprd_dpu.h"
#include "sprd_dsi.h"

/* Global control registers */
#define REG_DPU_CTRL	0x04
#define REG_DPU_CFG0	0x08
#define REG_PANEL_SIZE	0x20
#define REG_BLEND_SIZE	0x24
#define REG_BG_COLOR	0x2C

/* Layer0 control registers */
#define REG_LAY_BASE_ADDR0	0x30
#define REG_LAY_BASE_ADDR1	0x34
#define REG_LAY_BASE_ADDR2	0x38
#define REG_LAY_CTRL		0x40
#define REG_LAY_SIZE		0x44
#define REG_LAY_PITCH		0x48
#define REG_LAY_POS		0x4C
#define REG_LAY_ALPHA		0x50
#define REG_LAY_CROP_START	0x5C

/* Interrupt control registers */
#define REG_DPU_INT_EN		0x1E0
#define REG_DPU_INT_CLR		0x1E4
#define REG_DPU_INT_STS		0x1E8

/* DPI control registers */
#define REG_DPI_CTRL		0x1F0
#define REG_DPI_H_TIMING	0x1F4
#define REG_DPI_V_TIMING	0x1F8

/* MMU control registers */
#define REG_MMU_EN			0x800
#define REG_MMU_VPN_RANGE		0x80C
#define REG_MMU_PPN1			0x83C
#define REG_MMU_RANGE1			0x840
#define REG_MMU_PPN2			0x844
#define REG_MMU_RANGE2			0x848

/* Global control bits */
#define BIT_DPU_RUN			BIT(0)
#define BIT_DPU_STOP			BIT(1)
#define BIT_DPU_REG_UPDATE		BIT(2)
#define BIT_DPU_IF_EDPI			BIT(0)

/* Layer control bits */
#define BIT_DPU_LAY_EN				BIT(0)
#define BIT_DPU_LAY_LAYER_ALPHA			(0x01 << 2)
#define BIT_DPU_LAY_COMBO_ALPHA			(0x02 << 2)
#define BIT_DPU_LAY_FORMAT_YUV422_2PLANE		(0x00 << 4)
#define BIT_DPU_LAY_FORMAT_YUV420_2PLANE		(0x01 << 4)
#define BIT_DPU_LAY_FORMAT_YUV420_3PLANE		(0x02 << 4)
#define BIT_DPU_LAY_FORMAT_ARGB8888			(0x03 << 4)
#define BIT_DPU_LAY_FORMAT_RGB565			(0x04 << 4)
#define BIT_DPU_LAY_DATA_ENDIAN_B0B1B2B3		(0x00 << 8)
#define BIT_DPU_LAY_DATA_ENDIAN_B3B2B1B0		(0x01 << 8)
#define BIT_DPU_LAY_NO_SWITCH			(0x00 << 10)
#define BIT_DPU_LAY_RB_OR_UV_SWITCH		(0x01 << 10)
#define BIT_DPU_LAY_MODE_BLEND_NORMAL		(0x00 << 16)
#define BIT_DPU_LAY_MODE_BLEND_PREMULT		(0x01 << 16)
#define BIT_DPU_LAY_ROTATION_0		(0x00 << 20)
#define BIT_DPU_LAY_ROTATION_90		(0x01 << 20)
#define BIT_DPU_LAY_ROTATION_180	(0x02 << 20)
#define BIT_DPU_LAY_ROTATION_270	(0x03 << 20)
#define BIT_DPU_LAY_ROTATION_0_M	(0x04 << 20)
#define BIT_DPU_LAY_ROTATION_90_M	(0x05 << 20)
#define BIT_DPU_LAY_ROTATION_180_M	(0x06 << 20)
#define BIT_DPU_LAY_ROTATION_270_M	(0x07 << 20)

/* Interrupt control & status bits */
#define BIT_DPU_INT_DONE		BIT(0)
#define BIT_DPU_INT_TE			BIT(1)
#define BIT_DPU_INT_ERR			BIT(2)
#define BIT_DPU_INT_UPDATE_DONE		BIT(4)
#define BIT_DPU_INT_VSYNC		BIT(5)

/* DPI control bits */
#define BIT_DPU_EDPI_TE_EN		BIT(8)
#define BIT_DPU_EDPI_FROM_EXTERNAL_PAD	BIT(10)
#define BIT_DPU_DPI_HALT_EN		BIT(16)

static const u32 layer_fmts[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
	DRM_FORMAT_NV16,
	DRM_FORMAT_NV61,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YVU420,
};

struct sprd_plane {
	struct drm_plane base;
};

static int dpu_wait_stop_done(struct sprd_dpu *dpu)
{
	struct dpu_context *ctx = &dpu->ctx;
	int rc;

	if (ctx->stopped)
		return 0;

	rc = wait_event_interruptible_timeout(ctx->wait_queue, ctx->evt_stop,
					      msecs_to_jiffies(500));
	ctx->evt_stop = false;

	ctx->stopped = true;

	if (!rc) {
		drm_err(dpu->drm, "dpu wait for stop done time out!\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int dpu_wait_update_done(struct sprd_dpu *dpu)
{
	struct dpu_context *ctx = &dpu->ctx;
	int rc;

	ctx->evt_update = false;

	rc = wait_event_interruptible_timeout(ctx->wait_queue, ctx->evt_update,
					      msecs_to_jiffies(500));

	if (!rc) {
		drm_err(dpu->drm, "dpu wait for reg update done time out!\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static u32 drm_format_to_dpu(struct drm_framebuffer *fb)
{
	u32 format = 0;

	switch (fb->format->format) {
	case DRM_FORMAT_BGRA8888:
		/* BGRA8888 -> ARGB8888 */
		format |= BIT_DPU_LAY_DATA_ENDIAN_B3B2B1B0;
		format |= BIT_DPU_LAY_FORMAT_ARGB8888;
		break;
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_RGBA8888:
		/* RGBA8888 -> ABGR8888 */
		format |= BIT_DPU_LAY_DATA_ENDIAN_B3B2B1B0;
		fallthrough;
	case DRM_FORMAT_ABGR8888:
		/* RB switch */
		format |= BIT_DPU_LAY_RB_OR_UV_SWITCH;
		fallthrough;
	case DRM_FORMAT_ARGB8888:
		format |= BIT_DPU_LAY_FORMAT_ARGB8888;
		break;
	case DRM_FORMAT_XBGR8888:
		/* RB switch */
		format |= BIT_DPU_LAY_RB_OR_UV_SWITCH;
		fallthrough;
	case DRM_FORMAT_XRGB8888:
		format |= BIT_DPU_LAY_FORMAT_ARGB8888;
		break;
	case DRM_FORMAT_BGR565:
		/* RB switch */
		format |= BIT_DPU_LAY_RB_OR_UV_SWITCH;
		fallthrough;
	case DRM_FORMAT_RGB565:
		format |= BIT_DPU_LAY_FORMAT_RGB565;
		break;
	case DRM_FORMAT_NV12:
		/* 2-Lane: Yuv420 */
		format |= BIT_DPU_LAY_FORMAT_YUV420_2PLANE;
		/* Y endian */
		format |= BIT_DPU_LAY_DATA_ENDIAN_B0B1B2B3;
		/* UV endian */
		format |= BIT_DPU_LAY_NO_SWITCH;
		break;
	case DRM_FORMAT_NV21:
		/* 2-Lane: Yuv420 */
		format |= BIT_DPU_LAY_FORMAT_YUV420_2PLANE;
		/* Y endian */
		format |= BIT_DPU_LAY_DATA_ENDIAN_B0B1B2B3;
		/* UV endian */
		format |= BIT_DPU_LAY_RB_OR_UV_SWITCH;
		break;
	case DRM_FORMAT_NV16:
		/* 2-Lane: Yuv422 */
		format |= BIT_DPU_LAY_FORMAT_YUV422_2PLANE;
		/* Y endian */
		format |= BIT_DPU_LAY_DATA_ENDIAN_B3B2B1B0;
		/* UV endian */
		format |= BIT_DPU_LAY_RB_OR_UV_SWITCH;
		break;
	case DRM_FORMAT_NV61:
		/* 2-Lane: Yuv422 */
		format |= BIT_DPU_LAY_FORMAT_YUV422_2PLANE;
		/* Y endian */
		format |= BIT_DPU_LAY_DATA_ENDIAN_B0B1B2B3;
		/* UV endian */
		format |= BIT_DPU_LAY_NO_SWITCH;
		break;
	case DRM_FORMAT_YUV420:
		format |= BIT_DPU_LAY_FORMAT_YUV420_3PLANE;
		/* Y endian */
		format |= BIT_DPU_LAY_DATA_ENDIAN_B0B1B2B3;
		/* UV endian */
		format |= BIT_DPU_LAY_NO_SWITCH;
		break;
	case DRM_FORMAT_YVU420:
		format |= BIT_DPU_LAY_FORMAT_YUV420_3PLANE;
		/* Y endian */
		format |= BIT_DPU_LAY_DATA_ENDIAN_B0B1B2B3;
		/* UV endian */
		format |= BIT_DPU_LAY_RB_OR_UV_SWITCH;
		break;
	default:
		break;
	}

	return format;
}

static u32 drm_rotation_to_dpu(struct drm_plane_state *state)
{
	u32 rotation = 0;

	switch (state->rotation) {
	default:
	case DRM_MODE_ROTATE_0:
		rotation = BIT_DPU_LAY_ROTATION_0;
		break;
	case DRM_MODE_ROTATE_90:
		rotation = BIT_DPU_LAY_ROTATION_90;
		break;
	case DRM_MODE_ROTATE_180:
		rotation = BIT_DPU_LAY_ROTATION_180;
		break;
	case DRM_MODE_ROTATE_270:
		rotation = BIT_DPU_LAY_ROTATION_270;
		break;
	case DRM_MODE_REFLECT_Y:
		rotation = BIT_DPU_LAY_ROTATION_180_M;
		break;
	case (DRM_MODE_REFLECT_Y | DRM_MODE_ROTATE_90):
		rotation = BIT_DPU_LAY_ROTATION_90_M;
		break;
	case DRM_MODE_REFLECT_X:
		rotation = BIT_DPU_LAY_ROTATION_0_M;
		break;
	case (DRM_MODE_REFLECT_X | DRM_MODE_ROTATE_90):
		rotation = BIT_DPU_LAY_ROTATION_270_M;
		break;
	}

	return rotation;
}

static u32 drm_blend_to_dpu(struct drm_plane_state *state)
{
	u32 blend = 0;

	switch (state->pixel_blend_mode) {
	case DRM_MODE_BLEND_COVERAGE:
		/* alpha mode select - combo alpha */
		blend |= BIT_DPU_LAY_COMBO_ALPHA;
		/* Normal mode */
		blend |= BIT_DPU_LAY_MODE_BLEND_NORMAL;
		break;
	case DRM_MODE_BLEND_PREMULTI:
		/* alpha mode select - combo alpha */
		blend |= BIT_DPU_LAY_COMBO_ALPHA;
		/* Pre-mult mode */
		blend |= BIT_DPU_LAY_MODE_BLEND_PREMULT;
		break;
	case DRM_MODE_BLEND_PIXEL_NONE:
	default:
		/* don't do blending, maybe RGBX */
		/* alpha mode select - layer alpha */
		blend |= BIT_DPU_LAY_LAYER_ALPHA;
		break;
	}

	return blend;
}

static void sprd_dpu_layer(struct sprd_dpu *dpu, struct drm_plane_state *state)
{
	struct dpu_context *ctx = &dpu->ctx;
	struct drm_gem_cma_object *cma_obj;
	struct drm_framebuffer *fb = state->fb;
	u32 addr, size, offset, pitch, blend, format, rotation;
	u32 src_x = state->src_x >> 16;
	u32 src_y = state->src_y >> 16;
	u32 src_w = state->src_w >> 16;
	u32 src_h = state->src_h >> 16;
	u32 dst_x = state->crtc_x;
	u32 dst_y = state->crtc_y;
	u32 alpha = state->alpha;
	u32 index = state->zpos;
	int i;

	offset = (dst_x & 0xffff) | (dst_y << 16);
	size = (src_w & 0xffff) | (src_h << 16);

	for (i = 0; i < fb->format->num_planes; i++) {
		cma_obj = drm_fb_cma_get_gem_obj(fb, i);
		addr = cma_obj->paddr + fb->offsets[i];

		if (i == 0)
			layer_reg_wr(ctx, REG_LAY_BASE_ADDR0, addr, index);
		else if (i == 1)
			layer_reg_wr(ctx, REG_LAY_BASE_ADDR1, addr, index);
		else
			layer_reg_wr(ctx, REG_LAY_BASE_ADDR2, addr, index);
	}

	if (fb->format->num_planes == 3) {
		/* UV pitch is 1/2 of Y pitch */
		pitch = (fb->pitches[0] / fb->format->cpp[0]) |
				(fb->pitches[0] / fb->format->cpp[0] << 15);
	} else {
		pitch = fb->pitches[0] / fb->format->cpp[0];
	}

	layer_reg_wr(ctx, REG_LAY_POS, offset, index);
	layer_reg_wr(ctx, REG_LAY_SIZE, size, index);
	layer_reg_wr(ctx, REG_LAY_CROP_START,
		     src_y << 16 | src_x, index);
	layer_reg_wr(ctx, REG_LAY_ALPHA, alpha, index);
	layer_reg_wr(ctx, REG_LAY_PITCH, pitch, index);

	format = drm_format_to_dpu(fb);
	blend = drm_blend_to_dpu(state);
	rotation = drm_rotation_to_dpu(state);

	layer_reg_wr(ctx, REG_LAY_CTRL, BIT_DPU_LAY_EN |
				format |
				blend |
				rotation,
				index);
}

static void sprd_dpu_flip(struct sprd_dpu *dpu)
{
	struct dpu_context *ctx = &dpu->ctx;

	/*
	 * Make sure the dpu is in stop status. DPU has no shadow
	 * registers in EDPI mode. So the config registers can only be
	 * updated in the rising edge of DPU_RUN bit.
	 */
	if (ctx->if_type == SPRD_DPU_IF_EDPI)
		dpu_wait_stop_done(dpu);

	/* update trigger and wait */
	if (ctx->if_type == SPRD_DPU_IF_DPI) {
		if (!ctx->stopped) {
			dpu_reg_set(ctx, REG_DPU_CTRL, BIT_DPU_REG_UPDATE);
			dpu_wait_update_done(dpu);
		}

		dpu_reg_set(ctx, REG_DPU_INT_EN, BIT_DPU_INT_ERR);
	} else if (ctx->if_type == SPRD_DPU_IF_EDPI) {
		dpu_reg_set(ctx, REG_DPU_CTRL, BIT_DPU_RUN);

		ctx->stopped = false;
	}
}

static void sprd_dpu_init(struct sprd_dpu *dpu)
{
	struct dpu_context *ctx = &dpu->ctx;
	u32 int_mask = 0;

	writel(0x00, ctx->base + REG_BG_COLOR);
	writel(0x00, ctx->base + REG_MMU_EN);
	writel(0x00, ctx->base + REG_MMU_PPN1);
	writel(0xffff, ctx->base + REG_MMU_RANGE1);
	writel(0x00, ctx->base + REG_MMU_PPN2);
	writel(0xffff, ctx->base + REG_MMU_RANGE2);
	writel(0x1ffff, ctx->base + REG_MMU_VPN_RANGE);

	if (ctx->if_type == SPRD_DPU_IF_DPI) {
		/* use dpi as interface */
		dpu_reg_clr(ctx, REG_DPU_CFG0, BIT_DPU_IF_EDPI);
		/* disable Halt function for SPRD DSI */
		dpu_reg_clr(ctx, REG_DPI_CTRL, BIT_DPU_DPI_HALT_EN);
		/* select te from external pad */
		dpu_reg_set(ctx, REG_DPI_CTRL, BIT_DPU_EDPI_FROM_EXTERNAL_PAD);

		/* enable dpu update done INT */
		int_mask |= BIT_DPU_INT_UPDATE_DONE;
		/* enable dpu done INT */
		int_mask |= BIT_DPU_INT_DONE;
		/* enable dpu dpi vsync */
		int_mask |= BIT_DPU_INT_VSYNC;
		/* enable dpu TE INT */
		int_mask |= BIT_DPU_INT_TE;
		/* enable underflow err INT */
		int_mask |= BIT_DPU_INT_ERR;
	} else if (ctx->if_type == SPRD_DPU_IF_EDPI) {
		/* use edpi as interface */
		dpu_reg_set(ctx, REG_DPU_CFG0, BIT_DPU_IF_EDPI);
		/* use external te */
		dpu_reg_set(ctx, REG_DPI_CTRL, BIT_DPU_EDPI_FROM_EXTERNAL_PAD);
		/* enable te */
		dpu_reg_set(ctx, REG_DPI_CTRL, BIT_DPU_EDPI_TE_EN);

		/* enable stop done INT */
		int_mask |= BIT_DPU_INT_DONE;
		/* enable TE INT */
		int_mask |= BIT_DPU_INT_TE;
	}

	writel(int_mask, ctx->base + REG_DPU_INT_EN);
}

static void sprd_dpu_fini(struct sprd_dpu *dpu)
{
	struct dpu_context *ctx = &dpu->ctx;

	writel(0x00, ctx->base + REG_DPU_INT_EN);
	writel(0xff, ctx->base + REG_DPU_INT_CLR);
}

static void sprd_dpi_init(struct sprd_dpu *dpu)
{
	struct dpu_context *ctx = &dpu->ctx;
	u32 reg_val;
	u32 size;

	size = (ctx->vm.vactive << 16) | ctx->vm.hactive;
	writel(size, ctx->base + REG_PANEL_SIZE);
	writel(size, ctx->base + REG_BLEND_SIZE);

	if (ctx->if_type == SPRD_DPU_IF_DPI) {
		/* set dpi timing */
		reg_val = ctx->vm.hsync_len << 0 |
			  ctx->vm.hback_porch << 8 |
			  ctx->vm.hfront_porch << 20;
		writel(reg_val, ctx->base + REG_DPI_H_TIMING);

		reg_val = ctx->vm.vsync_len << 0 |
			  ctx->vm.vback_porch << 8 |
			  ctx->vm.vfront_porch << 20;
		writel(reg_val, ctx->base + REG_DPI_V_TIMING);
	}
}

void sprd_dpu_run(struct sprd_dpu *dpu)
{
	struct dpu_context *ctx = &dpu->ctx;

	dpu_reg_set(ctx, REG_DPU_CTRL, BIT_DPU_RUN);

	ctx->stopped = false;
}

void sprd_dpu_stop(struct sprd_dpu *dpu)
{
	struct dpu_context *ctx = &dpu->ctx;

	if (ctx->if_type == SPRD_DPU_IF_DPI)
		dpu_reg_set(ctx, REG_DPU_CTRL, BIT_DPU_STOP);

	dpu_wait_stop_done(dpu);
}

static int sprd_plane_atomic_check(struct drm_plane *plane,
				   struct drm_atomic_state *state)
{
	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(state,
									     plane);
	struct drm_crtc_state *crtc_state;
	u32 fmt;

	if (!plane_state->fb || !plane_state->crtc)
		return 0;

	fmt = drm_format_to_dpu(plane_state->fb);
	if (!fmt)
		return -EINVAL;

	crtc_state = drm_atomic_get_crtc_state(plane_state->state, plane_state->crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	return drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  DRM_PLANE_HELPER_NO_SCALING,
						  true, true);
}

static void sprd_plane_atomic_update(struct drm_plane *drm_plane,
				     struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   drm_plane);
	struct sprd_dpu *dpu = to_sprd_crtc(new_state->crtc);

	/* start configure dpu layers */
	sprd_dpu_layer(dpu, new_state);
}

static void sprd_plane_atomic_disable(struct drm_plane *drm_plane,
				      struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state,
									   drm_plane);
	struct sprd_dpu *dpu = to_sprd_crtc(old_state->crtc);

	layer_reg_wr(&dpu->ctx, REG_LAY_CTRL, 0x00, old_state->zpos);
}

static void sprd_plane_create_properties(struct sprd_plane *plane, int index)
{
	unsigned int supported_modes = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
				       BIT(DRM_MODE_BLEND_PREMULTI) |
				       BIT(DRM_MODE_BLEND_COVERAGE);

	/* create rotation property */
	drm_plane_create_rotation_property(&plane->base,
					   DRM_MODE_ROTATE_0,
					   DRM_MODE_ROTATE_MASK |
					   DRM_MODE_REFLECT_MASK);

	/* create alpha property */
	drm_plane_create_alpha_property(&plane->base);

	/* create blend mode property */
	drm_plane_create_blend_mode_property(&plane->base, supported_modes);

	/* create zpos property */
	drm_plane_create_zpos_immutable_property(&plane->base, index);
}

static const struct drm_plane_helper_funcs sprd_plane_helper_funcs = {
	.atomic_check = sprd_plane_atomic_check,
	.atomic_update = sprd_plane_atomic_update,
	.atomic_disable = sprd_plane_atomic_disable,
};

static const struct drm_plane_funcs sprd_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static struct sprd_plane *sprd_planes_init(struct drm_device *drm)
{
	struct sprd_plane *plane, *primary;
	enum drm_plane_type plane_type;
	int i;

	for (i = 0; i < 6; i++) {
		plane_type = (i == 0) ? DRM_PLANE_TYPE_PRIMARY :
					DRM_PLANE_TYPE_OVERLAY;

		plane = drmm_universal_plane_alloc(drm, struct sprd_plane, base,
						   1, &sprd_plane_funcs,
						   layer_fmts, ARRAY_SIZE(layer_fmts),
						   NULL, plane_type, NULL);
		if (IS_ERR(plane)) {
			drm_err(drm, "failed to init drm plane: %d\n", i);
			return plane;
		}

		drm_plane_helper_add(&plane->base, &sprd_plane_helper_funcs);

		sprd_plane_create_properties(plane, i);

		if (i == 0)
			primary = plane;
	}

	return primary;
}

static void sprd_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct sprd_dpu *dpu = to_sprd_crtc(crtc);
	struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	struct drm_encoder *encoder;
	struct sprd_dsi *dsi;

	drm_display_mode_to_videomode(mode, &dpu->ctx.vm);

	drm_for_each_encoder_mask(encoder, crtc->dev,
				  crtc->state->encoder_mask) {
		dsi = encoder_to_dsi(encoder);

		if (dsi->slave->mode_flags & MIPI_DSI_MODE_VIDEO)
			dpu->ctx.if_type = SPRD_DPU_IF_DPI;
		else
			dpu->ctx.if_type = SPRD_DPU_IF_EDPI;
	}

	sprd_dpi_init(dpu);
}

static void sprd_crtc_atomic_enable(struct drm_crtc *crtc,
				    struct drm_atomic_state *state)
{
	struct sprd_dpu *dpu = to_sprd_crtc(crtc);

	sprd_dpu_init(dpu);

	drm_crtc_vblank_on(&dpu->base);
}

static void sprd_crtc_atomic_disable(struct drm_crtc *crtc,
				     struct drm_atomic_state *state)
{
	struct sprd_dpu *dpu = to_sprd_crtc(crtc);
	struct drm_device *drm = dpu->base.dev;

	drm_crtc_vblank_off(&dpu->base);

	sprd_dpu_fini(dpu);

	spin_lock_irq(&drm->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&drm->event_lock);
}

static void sprd_crtc_atomic_flush(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)

{
	struct sprd_dpu *dpu = to_sprd_crtc(crtc);
	struct drm_device *drm = dpu->base.dev;

	sprd_dpu_flip(dpu);

	spin_lock_irq(&drm->event_lock);
	if (crtc->state->event) {
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
	}
	spin_unlock_irq(&drm->event_lock);
}

static int sprd_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct sprd_dpu *dpu = to_sprd_crtc(crtc);

	dpu_reg_set(&dpu->ctx, REG_DPU_INT_EN, BIT_DPU_INT_VSYNC);

	return 0;
}

static void sprd_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct sprd_dpu *dpu = to_sprd_crtc(crtc);

	dpu_reg_clr(&dpu->ctx, REG_DPU_INT_EN, BIT_DPU_INT_VSYNC);
}

static const struct drm_crtc_helper_funcs sprd_crtc_helper_funcs = {
	.mode_set_nofb	= sprd_crtc_mode_set_nofb,
	.atomic_flush	= sprd_crtc_atomic_flush,
	.atomic_enable	= sprd_crtc_atomic_enable,
	.atomic_disable	= sprd_crtc_atomic_disable,
};

static const struct drm_crtc_funcs sprd_crtc_funcs = {
	.destroy	= drm_crtc_cleanup,
	.set_config	= drm_atomic_helper_set_config,
	.page_flip	= drm_atomic_helper_page_flip,
	.reset		= drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.enable_vblank	= sprd_crtc_enable_vblank,
	.disable_vblank	= sprd_crtc_disable_vblank,
};

static struct sprd_dpu *sprd_crtc_init(struct drm_device *drm,
				       struct drm_plane *primary, struct device *dev)
{
	struct device_node *port;
	struct sprd_dpu *dpu;

	dpu = drmm_crtc_alloc_with_planes(drm, struct sprd_dpu, base,
					  primary, NULL,
					&sprd_crtc_funcs, NULL);
	if (IS_ERR(dpu)) {
		drm_err(drm, "failed to init crtc\n");
		return dpu;
	}
	drm_crtc_helper_add(&dpu->base, &sprd_crtc_helper_funcs);

	/*
	 * set crtc port so that drm_of_find_possible_crtcs call works
	 */
	port = of_graph_get_port_by_id(dev->of_node, 0);
	if (!port) {
		drm_err(drm, "failed to found crtc output port for %s\n",
			dev->of_node->full_name);
		return ERR_PTR(-EINVAL);
	}
	dpu->base.port = port;
	of_node_put(port);

	return dpu;
}

static irqreturn_t sprd_dpu_isr(int irq, void *data)
{
	struct sprd_dpu *dpu = data;
	struct dpu_context *ctx = &dpu->ctx;
	u32 reg_val, int_mask = 0;

	reg_val = readl(ctx->base + REG_DPU_INT_STS);

	/* disable err interrupt */
	if (reg_val & BIT_DPU_INT_ERR) {
		int_mask |= BIT_DPU_INT_ERR;
		drm_warn(dpu->drm, "Warning: dpu underflow!\n");
	}

	/* dpu update done isr */
	if (reg_val & BIT_DPU_INT_UPDATE_DONE) {
		ctx->evt_update = true;
		wake_up_interruptible_all(&ctx->wait_queue);
	}

	/* dpu stop done isr */
	if (reg_val & BIT_DPU_INT_DONE) {
		ctx->evt_stop = true;
		wake_up_interruptible_all(&ctx->wait_queue);
	}

	if (reg_val & BIT_DPU_INT_VSYNC)
		drm_crtc_handle_vblank(&dpu->base);

	writel(reg_val, ctx->base + REG_DPU_INT_CLR);
	dpu_reg_clr(ctx, REG_DPU_INT_EN, int_mask);

	return IRQ_HANDLED;
}

static int sprd_dpu_context_init(struct sprd_dpu *dpu,
				 struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dpu_context *ctx = &dpu->ctx;
	struct resource *res;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "failed to get I/O resource\n");
		return -EINVAL;
	}

	ctx->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!ctx->base) {
		dev_err(dev, "failed to map dpu registers\n");
		return -EFAULT;
	}

	ctx->irq = platform_get_irq(pdev, 0);
	if (ctx->irq < 0) {
		dev_err(dev, "failed to get dpu irq\n");
		return ctx->irq;
	}

	/* disable and clear interrupts before register dpu IRQ. */
	writel(0x00, ctx->base + REG_DPU_INT_EN);
	writel(0xff, ctx->base + REG_DPU_INT_CLR);

	ret = devm_request_irq(dev, ctx->irq, sprd_dpu_isr,
			       IRQF_TRIGGER_NONE, "DPU", dpu);
	if (ret) {
		dev_err(dev, "failed to register dpu irq handler\n");
		return ret;
	}

	init_waitqueue_head(&ctx->wait_queue);

	return 0;
}

static int sprd_dpu_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = data;
	struct sprd_dpu *dpu;
	struct sprd_plane *plane;
	int ret;

	plane = sprd_planes_init(drm);
	if (IS_ERR(plane))
		return PTR_ERR(plane);

	dpu = sprd_crtc_init(drm, &plane->base, dev);
	if (IS_ERR(dpu))
		return PTR_ERR(dpu);

	dpu->drm = drm;
	dev_set_drvdata(dev, dpu);

	ret = sprd_dpu_context_init(dpu, dev);
	if (ret)
		return ret;

	return 0;
}

static const struct component_ops dpu_component_ops = {
	.bind = sprd_dpu_bind,
};

static const struct of_device_id dpu_match_table[] = {
	{ .compatible = "sprd,sharkl3-dpu" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, dpu_match_table);

static int sprd_dpu_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &dpu_component_ops);
}

static int sprd_dpu_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dpu_component_ops);

	return 0;
}

struct platform_driver sprd_dpu_driver = {
	.probe = sprd_dpu_probe,
	.remove = sprd_dpu_remove,
	.driver = {
		.name = "sprd-dpu-drv",
		.of_match_table = dpu_match_table,
	},
};

MODULE_AUTHOR("Leon He <leon.he@unisoc.com>");
MODULE_AUTHOR("Kevin Tang <kevin.tang@unisoc.com>");
MODULE_DESCRIPTION("Unisoc Display Controller Driver");
MODULE_LICENSE("GPL v2");
