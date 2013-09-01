/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/clk/tegra.h>

#include "host1x_client.h"
#include "dc.h"
#include "drm.h"
#include "gem.h"

struct tegra_plane {
	struct drm_plane base;
	unsigned int index;
};

static inline struct tegra_plane *to_tegra_plane(struct drm_plane *plane)
{
	return container_of(plane, struct tegra_plane, base);
}

static int tegra_plane_update(struct drm_plane *plane, struct drm_crtc *crtc,
			      struct drm_framebuffer *fb, int crtc_x,
			      int crtc_y, unsigned int crtc_w,
			      unsigned int crtc_h, uint32_t src_x,
			      uint32_t src_y, uint32_t src_w, uint32_t src_h)
{
	struct tegra_plane *p = to_tegra_plane(plane);
	struct tegra_dc *dc = to_tegra_dc(crtc);
	struct tegra_dc_window window;
	unsigned int i;

	memset(&window, 0, sizeof(window));
	window.src.x = src_x >> 16;
	window.src.y = src_y >> 16;
	window.src.w = src_w >> 16;
	window.src.h = src_h >> 16;
	window.dst.x = crtc_x;
	window.dst.y = crtc_y;
	window.dst.w = crtc_w;
	window.dst.h = crtc_h;
	window.format = tegra_dc_format(fb->pixel_format);
	window.bits_per_pixel = fb->bits_per_pixel;

	for (i = 0; i < drm_format_num_planes(fb->pixel_format); i++) {
		struct tegra_bo *bo = tegra_fb_get_plane(fb, i);

		window.base[i] = bo->paddr + fb->offsets[i];

		/*
		 * Tegra doesn't support different strides for U and V planes
		 * so we display a warning if the user tries to display a
		 * framebuffer with such a configuration.
		 */
		if (i >= 2) {
			if (fb->pitches[i] != window.stride[1])
				DRM_ERROR("unsupported UV-plane configuration\n");
		} else {
			window.stride[i] = fb->pitches[i];
		}
	}

	return tegra_dc_setup_window(dc, p->index, &window);
}

static int tegra_plane_disable(struct drm_plane *plane)
{
	struct tegra_dc *dc = to_tegra_dc(plane->crtc);
	struct tegra_plane *p = to_tegra_plane(plane);
	unsigned long value;

	if (!plane->crtc)
		return 0;

	value = WINDOW_A_SELECT << p->index;
	tegra_dc_writel(dc, value, DC_CMD_DISPLAY_WINDOW_HEADER);

	value = tegra_dc_readl(dc, DC_WIN_WIN_OPTIONS);
	value &= ~WIN_ENABLE;
	tegra_dc_writel(dc, value, DC_WIN_WIN_OPTIONS);

	tegra_dc_writel(dc, WIN_A_UPDATE << p->index, DC_CMD_STATE_CONTROL);
	tegra_dc_writel(dc, WIN_A_ACT_REQ << p->index, DC_CMD_STATE_CONTROL);

	return 0;
}

static void tegra_plane_destroy(struct drm_plane *plane)
{
	tegra_plane_disable(plane);
	drm_plane_cleanup(plane);
}

static const struct drm_plane_funcs tegra_plane_funcs = {
	.update_plane = tegra_plane_update,
	.disable_plane = tegra_plane_disable,
	.destroy = tegra_plane_destroy,
};

static const uint32_t plane_formats[] = {
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YUV422,
};

static int tegra_dc_add_planes(struct drm_device *drm, struct tegra_dc *dc)
{
	unsigned int i;
	int err = 0;

	for (i = 0; i < 2; i++) {
		struct tegra_plane *plane;

		plane = devm_kzalloc(drm->dev, sizeof(*plane), GFP_KERNEL);
		if (!plane)
			return -ENOMEM;

		plane->index = 1 + i;

		err = drm_plane_init(drm, &plane->base, 1 << dc->pipe,
				     &tegra_plane_funcs, plane_formats,
				     ARRAY_SIZE(plane_formats), false);
		if (err < 0)
			return err;
	}

	return 0;
}

static int tegra_dc_set_base(struct tegra_dc *dc, int x, int y,
			     struct drm_framebuffer *fb)
{
	unsigned int format = tegra_dc_format(fb->pixel_format);
	struct tegra_bo *bo = tegra_fb_get_plane(fb, 0);
	unsigned long value;

	tegra_dc_writel(dc, WINDOW_A_SELECT, DC_CMD_DISPLAY_WINDOW_HEADER);

	value = fb->offsets[0] + y * fb->pitches[0] +
		x * fb->bits_per_pixel / 8;

	tegra_dc_writel(dc, bo->paddr + value, DC_WINBUF_START_ADDR);
	tegra_dc_writel(dc, fb->pitches[0], DC_WIN_LINE_STRIDE);
	tegra_dc_writel(dc, format, DC_WIN_COLOR_DEPTH);

	value = GENERAL_UPDATE | WIN_A_UPDATE;
	tegra_dc_writel(dc, value, DC_CMD_STATE_CONTROL);

	value = GENERAL_ACT_REQ | WIN_A_ACT_REQ;
	tegra_dc_writel(dc, value, DC_CMD_STATE_CONTROL);

	return 0;
}

void tegra_dc_enable_vblank(struct tegra_dc *dc)
{
	unsigned long value, flags;

	spin_lock_irqsave(&dc->lock, flags);

	value = tegra_dc_readl(dc, DC_CMD_INT_MASK);
	value |= VBLANK_INT;
	tegra_dc_writel(dc, value, DC_CMD_INT_MASK);

	spin_unlock_irqrestore(&dc->lock, flags);
}

void tegra_dc_disable_vblank(struct tegra_dc *dc)
{
	unsigned long value, flags;

	spin_lock_irqsave(&dc->lock, flags);

	value = tegra_dc_readl(dc, DC_CMD_INT_MASK);
	value &= ~VBLANK_INT;
	tegra_dc_writel(dc, value, DC_CMD_INT_MASK);

	spin_unlock_irqrestore(&dc->lock, flags);
}

static void tegra_dc_finish_page_flip(struct tegra_dc *dc)
{
	struct drm_device *drm = dc->base.dev;
	struct drm_crtc *crtc = &dc->base;
	unsigned long flags, base;
	struct tegra_bo *bo;

	if (!dc->event)
		return;

	bo = tegra_fb_get_plane(crtc->fb, 0);

	/* check if new start address has been latched */
	tegra_dc_writel(dc, READ_MUX, DC_CMD_STATE_ACCESS);
	base = tegra_dc_readl(dc, DC_WINBUF_START_ADDR);
	tegra_dc_writel(dc, 0, DC_CMD_STATE_ACCESS);

	if (base == bo->paddr + crtc->fb->offsets[0]) {
		spin_lock_irqsave(&drm->event_lock, flags);
		drm_send_vblank_event(drm, dc->pipe, dc->event);
		drm_vblank_put(drm, dc->pipe);
		dc->event = NULL;
		spin_unlock_irqrestore(&drm->event_lock, flags);
	}
}

void tegra_dc_cancel_page_flip(struct drm_crtc *crtc, struct drm_file *file)
{
	struct tegra_dc *dc = to_tegra_dc(crtc);
	struct drm_device *drm = crtc->dev;
	unsigned long flags;

	spin_lock_irqsave(&drm->event_lock, flags);

	if (dc->event && dc->event->base.file_priv == file) {
		dc->event->base.destroy(&dc->event->base);
		drm_vblank_put(drm, dc->pipe);
		dc->event = NULL;
	}

	spin_unlock_irqrestore(&drm->event_lock, flags);
}

static int tegra_dc_page_flip(struct drm_crtc *crtc, struct drm_framebuffer *fb,
			      struct drm_pending_vblank_event *event, uint32_t page_flip_flags)
{
	struct tegra_dc *dc = to_tegra_dc(crtc);
	struct drm_device *drm = crtc->dev;

	if (dc->event)
		return -EBUSY;

	if (event) {
		event->pipe = dc->pipe;
		dc->event = event;
		drm_vblank_get(drm, dc->pipe);
	}

	tegra_dc_set_base(dc, 0, 0, fb);
	crtc->fb = fb;

	return 0;
}

static const struct drm_crtc_funcs tegra_crtc_funcs = {
	.page_flip = tegra_dc_page_flip,
	.set_config = drm_crtc_helper_set_config,
	.destroy = drm_crtc_cleanup,
};

static void tegra_crtc_disable(struct drm_crtc *crtc)
{
	struct drm_device *drm = crtc->dev;
	struct drm_plane *plane;

	list_for_each_entry(plane, &drm->mode_config.plane_list, head) {
		if (plane->crtc == crtc) {
			tegra_plane_disable(plane);
			plane->crtc = NULL;

			if (plane->fb) {
				drm_framebuffer_unreference(plane->fb);
				plane->fb = NULL;
			}
		}
	}
}

static bool tegra_crtc_mode_fixup(struct drm_crtc *crtc,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted)
{
	return true;
}

static inline u32 compute_dda_inc(unsigned int in, unsigned int out, bool v,
				  unsigned int bpp)
{
	fixed20_12 outf = dfixed_init(out);
	fixed20_12 inf = dfixed_init(in);
	u32 dda_inc;
	int max;

	if (v)
		max = 15;
	else {
		switch (bpp) {
		case 2:
			max = 8;
			break;

		default:
			WARN_ON_ONCE(1);
			/* fallthrough */
		case 4:
			max = 4;
			break;
		}
	}

	outf.full = max_t(u32, outf.full - dfixed_const(1), dfixed_const(1));
	inf.full -= dfixed_const(1);

	dda_inc = dfixed_div(inf, outf);
	dda_inc = min_t(u32, dda_inc, dfixed_const(max));

	return dda_inc;
}

static inline u32 compute_initial_dda(unsigned int in)
{
	fixed20_12 inf = dfixed_init(in);
	return dfixed_frac(inf);
}

static int tegra_dc_set_timings(struct tegra_dc *dc,
				struct drm_display_mode *mode)
{
	/* TODO: For HDMI compliance, h & v ref_to_sync should be set to 1 */
	unsigned int h_ref_to_sync = 0;
	unsigned int v_ref_to_sync = 0;
	unsigned long value;

	tegra_dc_writel(dc, 0x0, DC_DISP_DISP_TIMING_OPTIONS);

	value = (v_ref_to_sync << 16) | h_ref_to_sync;
	tegra_dc_writel(dc, value, DC_DISP_REF_TO_SYNC);

	value = ((mode->vsync_end - mode->vsync_start) << 16) |
		((mode->hsync_end - mode->hsync_start) <<  0);
	tegra_dc_writel(dc, value, DC_DISP_SYNC_WIDTH);

	value = ((mode->vtotal - mode->vsync_end) << 16) |
		((mode->htotal - mode->hsync_end) <<  0);
	tegra_dc_writel(dc, value, DC_DISP_BACK_PORCH);

	value = ((mode->vsync_start - mode->vdisplay) << 16) |
		((mode->hsync_start - mode->hdisplay) <<  0);
	tegra_dc_writel(dc, value, DC_DISP_FRONT_PORCH);

	value = (mode->vdisplay << 16) | mode->hdisplay;
	tegra_dc_writel(dc, value, DC_DISP_ACTIVE);

	return 0;
}

static int tegra_crtc_setup_clk(struct drm_crtc *crtc,
				struct drm_display_mode *mode,
				unsigned long *div)
{
	unsigned long pclk = mode->clock * 1000, rate;
	struct tegra_dc *dc = to_tegra_dc(crtc);
	struct tegra_output *output = NULL;
	struct drm_encoder *encoder;
	long err;

	list_for_each_entry(encoder, &crtc->dev->mode_config.encoder_list, head)
		if (encoder->crtc == crtc) {
			output = encoder_to_output(encoder);
			break;
		}

	if (!output)
		return -ENODEV;

	/*
	 * This assumes that the display controller will divide its parent
	 * clock by 2 to generate the pixel clock.
	 */
	err = tegra_output_setup_clock(output, dc->clk, pclk * 2);
	if (err < 0) {
		dev_err(dc->dev, "failed to setup clock: %ld\n", err);
		return err;
	}

	rate = clk_get_rate(dc->clk);
	*div = (rate * 2 / pclk) - 2;

	DRM_DEBUG_KMS("rate: %lu, div: %lu\n", rate, *div);

	return 0;
}

static bool tegra_dc_format_is_yuv(unsigned int format, bool *planar)
{
	switch (format) {
	case WIN_COLOR_DEPTH_YCbCr422:
	case WIN_COLOR_DEPTH_YUV422:
		if (planar)
			*planar = false;

		return true;

	case WIN_COLOR_DEPTH_YCbCr420P:
	case WIN_COLOR_DEPTH_YUV420P:
	case WIN_COLOR_DEPTH_YCbCr422P:
	case WIN_COLOR_DEPTH_YUV422P:
	case WIN_COLOR_DEPTH_YCbCr422R:
	case WIN_COLOR_DEPTH_YUV422R:
	case WIN_COLOR_DEPTH_YCbCr422RA:
	case WIN_COLOR_DEPTH_YUV422RA:
		if (planar)
			*planar = true;

		return true;
	}

	return false;
}

int tegra_dc_setup_window(struct tegra_dc *dc, unsigned int index,
			  const struct tegra_dc_window *window)
{
	unsigned h_offset, v_offset, h_size, v_size, h_dda, v_dda, bpp;
	unsigned long value;
	bool yuv, planar;

	/*
	 * For YUV planar modes, the number of bytes per pixel takes into
	 * account only the luma component and therefore is 1.
	 */
	yuv = tegra_dc_format_is_yuv(window->format, &planar);
	if (!yuv)
		bpp = window->bits_per_pixel / 8;
	else
		bpp = planar ? 1 : 2;

	value = WINDOW_A_SELECT << index;
	tegra_dc_writel(dc, value, DC_CMD_DISPLAY_WINDOW_HEADER);

	tegra_dc_writel(dc, window->format, DC_WIN_COLOR_DEPTH);
	tegra_dc_writel(dc, 0, DC_WIN_BYTE_SWAP);

	value = V_POSITION(window->dst.y) | H_POSITION(window->dst.x);
	tegra_dc_writel(dc, value, DC_WIN_POSITION);

	value = V_SIZE(window->dst.h) | H_SIZE(window->dst.w);
	tegra_dc_writel(dc, value, DC_WIN_SIZE);

	h_offset = window->src.x * bpp;
	v_offset = window->src.y;
	h_size = window->src.w * bpp;
	v_size = window->src.h;

	value = V_PRESCALED_SIZE(v_size) | H_PRESCALED_SIZE(h_size);
	tegra_dc_writel(dc, value, DC_WIN_PRESCALED_SIZE);

	/*
	 * For DDA computations the number of bytes per pixel for YUV planar
	 * modes needs to take into account all Y, U and V components.
	 */
	if (yuv && planar)
		bpp = 2;

	h_dda = compute_dda_inc(window->src.w, window->dst.w, false, bpp);
	v_dda = compute_dda_inc(window->src.h, window->dst.h, true, bpp);

	value = V_DDA_INC(v_dda) | H_DDA_INC(h_dda);
	tegra_dc_writel(dc, value, DC_WIN_DDA_INC);

	h_dda = compute_initial_dda(window->src.x);
	v_dda = compute_initial_dda(window->src.y);

	tegra_dc_writel(dc, h_dda, DC_WIN_H_INITIAL_DDA);
	tegra_dc_writel(dc, v_dda, DC_WIN_V_INITIAL_DDA);

	tegra_dc_writel(dc, 0, DC_WIN_UV_BUF_STRIDE);
	tegra_dc_writel(dc, 0, DC_WIN_BUF_STRIDE);

	tegra_dc_writel(dc, window->base[0], DC_WINBUF_START_ADDR);

	if (yuv && planar) {
		tegra_dc_writel(dc, window->base[1], DC_WINBUF_START_ADDR_U);
		tegra_dc_writel(dc, window->base[2], DC_WINBUF_START_ADDR_V);
		value = window->stride[1] << 16 | window->stride[0];
		tegra_dc_writel(dc, value, DC_WIN_LINE_STRIDE);
	} else {
		tegra_dc_writel(dc, window->stride[0], DC_WIN_LINE_STRIDE);
	}

	tegra_dc_writel(dc, h_offset, DC_WINBUF_ADDR_H_OFFSET);
	tegra_dc_writel(dc, v_offset, DC_WINBUF_ADDR_V_OFFSET);

	value = WIN_ENABLE;

	if (yuv) {
		/* setup default colorspace conversion coefficients */
		tegra_dc_writel(dc, 0x00f0, DC_WIN_CSC_YOF);
		tegra_dc_writel(dc, 0x012a, DC_WIN_CSC_KYRGB);
		tegra_dc_writel(dc, 0x0000, DC_WIN_CSC_KUR);
		tegra_dc_writel(dc, 0x0198, DC_WIN_CSC_KVR);
		tegra_dc_writel(dc, 0x039b, DC_WIN_CSC_KUG);
		tegra_dc_writel(dc, 0x032f, DC_WIN_CSC_KVG);
		tegra_dc_writel(dc, 0x0204, DC_WIN_CSC_KUB);
		tegra_dc_writel(dc, 0x0000, DC_WIN_CSC_KVB);

		value |= CSC_ENABLE;
	} else if (window->bits_per_pixel < 24) {
		value |= COLOR_EXPAND;
	}

	tegra_dc_writel(dc, value, DC_WIN_WIN_OPTIONS);

	/*
	 * Disable blending and assume Window A is the bottom-most window,
	 * Window C is the top-most window and Window B is in the middle.
	 */
	tegra_dc_writel(dc, 0xffff00, DC_WIN_BLEND_NOKEY);
	tegra_dc_writel(dc, 0xffff00, DC_WIN_BLEND_1WIN);

	switch (index) {
	case 0:
		tegra_dc_writel(dc, 0x000000, DC_WIN_BLEND_2WIN_X);
		tegra_dc_writel(dc, 0x000000, DC_WIN_BLEND_2WIN_Y);
		tegra_dc_writel(dc, 0x000000, DC_WIN_BLEND_3WIN_XY);
		break;

	case 1:
		tegra_dc_writel(dc, 0xffff00, DC_WIN_BLEND_2WIN_X);
		tegra_dc_writel(dc, 0x000000, DC_WIN_BLEND_2WIN_Y);
		tegra_dc_writel(dc, 0x000000, DC_WIN_BLEND_3WIN_XY);
		break;

	case 2:
		tegra_dc_writel(dc, 0xffff00, DC_WIN_BLEND_2WIN_X);
		tegra_dc_writel(dc, 0xffff00, DC_WIN_BLEND_2WIN_Y);
		tegra_dc_writel(dc, 0xffff00, DC_WIN_BLEND_3WIN_XY);
		break;
	}

	tegra_dc_writel(dc, WIN_A_UPDATE << index, DC_CMD_STATE_CONTROL);
	tegra_dc_writel(dc, WIN_A_ACT_REQ << index, DC_CMD_STATE_CONTROL);

	return 0;
}

unsigned int tegra_dc_format(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_XBGR8888:
		return WIN_COLOR_DEPTH_R8G8B8A8;

	case DRM_FORMAT_XRGB8888:
		return WIN_COLOR_DEPTH_B8G8R8A8;

	case DRM_FORMAT_RGB565:
		return WIN_COLOR_DEPTH_B5G6R5;

	case DRM_FORMAT_UYVY:
		return WIN_COLOR_DEPTH_YCbCr422;

	case DRM_FORMAT_YUV420:
		return WIN_COLOR_DEPTH_YCbCr420P;

	case DRM_FORMAT_YUV422:
		return WIN_COLOR_DEPTH_YCbCr422P;

	default:
		break;
	}

	WARN(1, "unsupported pixel format %u, using default\n", format);
	return WIN_COLOR_DEPTH_B8G8R8A8;
}

static int tegra_crtc_mode_set(struct drm_crtc *crtc,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted,
			       int x, int y, struct drm_framebuffer *old_fb)
{
	struct tegra_bo *bo = tegra_fb_get_plane(crtc->fb, 0);
	struct tegra_dc *dc = to_tegra_dc(crtc);
	struct tegra_dc_window window;
	unsigned long div, value;
	int err;

	drm_vblank_pre_modeset(crtc->dev, dc->pipe);

	err = tegra_crtc_setup_clk(crtc, mode, &div);
	if (err) {
		dev_err(dc->dev, "failed to setup clock for CRTC: %d\n", err);
		return err;
	}

	/* program display mode */
	tegra_dc_set_timings(dc, mode);

	value = DE_SELECT_ACTIVE | DE_CONTROL_NORMAL;
	tegra_dc_writel(dc, value, DC_DISP_DATA_ENABLE_OPTIONS);

	value = tegra_dc_readl(dc, DC_COM_PIN_OUTPUT_POLARITY(1));
	value &= ~LVS_OUTPUT_POLARITY_LOW;
	value &= ~LHS_OUTPUT_POLARITY_LOW;
	tegra_dc_writel(dc, value, DC_COM_PIN_OUTPUT_POLARITY(1));

	value = DISP_DATA_FORMAT_DF1P1C | DISP_ALIGNMENT_MSB |
		DISP_ORDER_RED_BLUE;
	tegra_dc_writel(dc, value, DC_DISP_DISP_INTERFACE_CONTROL);

	tegra_dc_writel(dc, 0x00010001, DC_DISP_SHIFT_CLOCK_OPTIONS);

	value = SHIFT_CLK_DIVIDER(div) | PIXEL_CLK_DIVIDER_PCD1;
	tegra_dc_writel(dc, value, DC_DISP_DISP_CLOCK_CONTROL);

	/* setup window parameters */
	memset(&window, 0, sizeof(window));
	window.src.x = 0;
	window.src.y = 0;
	window.src.w = mode->hdisplay;
	window.src.h = mode->vdisplay;
	window.dst.x = 0;
	window.dst.y = 0;
	window.dst.w = mode->hdisplay;
	window.dst.h = mode->vdisplay;
	window.format = tegra_dc_format(crtc->fb->pixel_format);
	window.bits_per_pixel = crtc->fb->bits_per_pixel;
	window.stride[0] = crtc->fb->pitches[0];
	window.base[0] = bo->paddr;

	err = tegra_dc_setup_window(dc, 0, &window);
	if (err < 0)
		dev_err(dc->dev, "failed to enable root plane\n");

	return 0;
}

static int tegra_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
				    struct drm_framebuffer *old_fb)
{
	struct tegra_dc *dc = to_tegra_dc(crtc);

	return tegra_dc_set_base(dc, x, y, crtc->fb);
}

static void tegra_crtc_prepare(struct drm_crtc *crtc)
{
	struct tegra_dc *dc = to_tegra_dc(crtc);
	unsigned int syncpt;
	unsigned long value;

	/* hardware initialization */
	tegra_periph_reset_deassert(dc->clk);
	usleep_range(10000, 20000);

	if (dc->pipe)
		syncpt = SYNCPT_VBLANK1;
	else
		syncpt = SYNCPT_VBLANK0;

	/* initialize display controller */
	tegra_dc_writel(dc, 0x00000100, DC_CMD_GENERAL_INCR_SYNCPT_CNTRL);
	tegra_dc_writel(dc, 0x100 | syncpt, DC_CMD_CONT_SYNCPT_VSYNC);

	value = WIN_A_UF_INT | WIN_B_UF_INT | WIN_C_UF_INT | WIN_A_OF_INT;
	tegra_dc_writel(dc, value, DC_CMD_INT_TYPE);

	value = WIN_A_UF_INT | WIN_B_UF_INT | WIN_C_UF_INT |
		WIN_A_OF_INT | WIN_B_OF_INT | WIN_C_OF_INT;
	tegra_dc_writel(dc, value, DC_CMD_INT_POLARITY);

	value = PW0_ENABLE | PW1_ENABLE | PW2_ENABLE | PW3_ENABLE |
		PW4_ENABLE | PM0_ENABLE | PM1_ENABLE;
	tegra_dc_writel(dc, value, DC_CMD_DISPLAY_POWER_CONTROL);

	value = tegra_dc_readl(dc, DC_CMD_DISPLAY_COMMAND);
	value |= DISP_CTRL_MODE_C_DISPLAY;
	tegra_dc_writel(dc, value, DC_CMD_DISPLAY_COMMAND);

	/* initialize timer */
	value = CURSOR_THRESHOLD(0) | WINDOW_A_THRESHOLD(0x20) |
		WINDOW_B_THRESHOLD(0x20) | WINDOW_C_THRESHOLD(0x20);
	tegra_dc_writel(dc, value, DC_DISP_DISP_MEM_HIGH_PRIORITY);

	value = CURSOR_THRESHOLD(0) | WINDOW_A_THRESHOLD(1) |
		WINDOW_B_THRESHOLD(1) | WINDOW_C_THRESHOLD(1);
	tegra_dc_writel(dc, value, DC_DISP_DISP_MEM_HIGH_PRIORITY_TIMER);

	value = VBLANK_INT | WIN_A_UF_INT | WIN_B_UF_INT | WIN_C_UF_INT;
	tegra_dc_writel(dc, value, DC_CMD_INT_ENABLE);

	value = WIN_A_UF_INT | WIN_B_UF_INT | WIN_C_UF_INT;
	tegra_dc_writel(dc, value, DC_CMD_INT_MASK);
}

static void tegra_crtc_commit(struct drm_crtc *crtc)
{
	struct tegra_dc *dc = to_tegra_dc(crtc);
	unsigned long value;

	value = GENERAL_UPDATE | WIN_A_UPDATE;
	tegra_dc_writel(dc, value, DC_CMD_STATE_CONTROL);

	value = GENERAL_ACT_REQ | WIN_A_ACT_REQ;
	tegra_dc_writel(dc, value, DC_CMD_STATE_CONTROL);

	drm_vblank_post_modeset(crtc->dev, dc->pipe);
}

static void tegra_crtc_load_lut(struct drm_crtc *crtc)
{
}

static const struct drm_crtc_helper_funcs tegra_crtc_helper_funcs = {
	.disable = tegra_crtc_disable,
	.mode_fixup = tegra_crtc_mode_fixup,
	.mode_set = tegra_crtc_mode_set,
	.mode_set_base = tegra_crtc_mode_set_base,
	.prepare = tegra_crtc_prepare,
	.commit = tegra_crtc_commit,
	.load_lut = tegra_crtc_load_lut,
};

static irqreturn_t tegra_dc_irq(int irq, void *data)
{
	struct tegra_dc *dc = data;
	unsigned long status;

	status = tegra_dc_readl(dc, DC_CMD_INT_STATUS);
	tegra_dc_writel(dc, status, DC_CMD_INT_STATUS);

	if (status & FRAME_END_INT) {
		/*
		dev_dbg(dc->dev, "%s(): frame end\n", __func__);
		*/
	}

	if (status & VBLANK_INT) {
		/*
		dev_dbg(dc->dev, "%s(): vertical blank\n", __func__);
		*/
		drm_handle_vblank(dc->base.dev, dc->pipe);
		tegra_dc_finish_page_flip(dc);
	}

	if (status & (WIN_A_UF_INT | WIN_B_UF_INT | WIN_C_UF_INT)) {
		/*
		dev_dbg(dc->dev, "%s(): underflow\n", __func__);
		*/
	}

	return IRQ_HANDLED;
}

static int tegra_dc_show_regs(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct tegra_dc *dc = node->info_ent->data;

#define DUMP_REG(name)						\
	seq_printf(s, "%-40s %#05x %08lx\n", #name, name,	\
		   tegra_dc_readl(dc, name))

	DUMP_REG(DC_CMD_GENERAL_INCR_SYNCPT);
	DUMP_REG(DC_CMD_GENERAL_INCR_SYNCPT_CNTRL);
	DUMP_REG(DC_CMD_GENERAL_INCR_SYNCPT_ERROR);
	DUMP_REG(DC_CMD_WIN_A_INCR_SYNCPT);
	DUMP_REG(DC_CMD_WIN_A_INCR_SYNCPT_CNTRL);
	DUMP_REG(DC_CMD_WIN_A_INCR_SYNCPT_ERROR);
	DUMP_REG(DC_CMD_WIN_B_INCR_SYNCPT);
	DUMP_REG(DC_CMD_WIN_B_INCR_SYNCPT_CNTRL);
	DUMP_REG(DC_CMD_WIN_B_INCR_SYNCPT_ERROR);
	DUMP_REG(DC_CMD_WIN_C_INCR_SYNCPT);
	DUMP_REG(DC_CMD_WIN_C_INCR_SYNCPT_CNTRL);
	DUMP_REG(DC_CMD_WIN_C_INCR_SYNCPT_ERROR);
	DUMP_REG(DC_CMD_CONT_SYNCPT_VSYNC);
	DUMP_REG(DC_CMD_DISPLAY_COMMAND_OPTION0);
	DUMP_REG(DC_CMD_DISPLAY_COMMAND);
	DUMP_REG(DC_CMD_SIGNAL_RAISE);
	DUMP_REG(DC_CMD_DISPLAY_POWER_CONTROL);
	DUMP_REG(DC_CMD_INT_STATUS);
	DUMP_REG(DC_CMD_INT_MASK);
	DUMP_REG(DC_CMD_INT_ENABLE);
	DUMP_REG(DC_CMD_INT_TYPE);
	DUMP_REG(DC_CMD_INT_POLARITY);
	DUMP_REG(DC_CMD_SIGNAL_RAISE1);
	DUMP_REG(DC_CMD_SIGNAL_RAISE2);
	DUMP_REG(DC_CMD_SIGNAL_RAISE3);
	DUMP_REG(DC_CMD_STATE_ACCESS);
	DUMP_REG(DC_CMD_STATE_CONTROL);
	DUMP_REG(DC_CMD_DISPLAY_WINDOW_HEADER);
	DUMP_REG(DC_CMD_REG_ACT_CONTROL);
	DUMP_REG(DC_COM_CRC_CONTROL);
	DUMP_REG(DC_COM_CRC_CHECKSUM);
	DUMP_REG(DC_COM_PIN_OUTPUT_ENABLE(0));
	DUMP_REG(DC_COM_PIN_OUTPUT_ENABLE(1));
	DUMP_REG(DC_COM_PIN_OUTPUT_ENABLE(2));
	DUMP_REG(DC_COM_PIN_OUTPUT_ENABLE(3));
	DUMP_REG(DC_COM_PIN_OUTPUT_POLARITY(0));
	DUMP_REG(DC_COM_PIN_OUTPUT_POLARITY(1));
	DUMP_REG(DC_COM_PIN_OUTPUT_POLARITY(2));
	DUMP_REG(DC_COM_PIN_OUTPUT_POLARITY(3));
	DUMP_REG(DC_COM_PIN_OUTPUT_DATA(0));
	DUMP_REG(DC_COM_PIN_OUTPUT_DATA(1));
	DUMP_REG(DC_COM_PIN_OUTPUT_DATA(2));
	DUMP_REG(DC_COM_PIN_OUTPUT_DATA(3));
	DUMP_REG(DC_COM_PIN_INPUT_ENABLE(0));
	DUMP_REG(DC_COM_PIN_INPUT_ENABLE(1));
	DUMP_REG(DC_COM_PIN_INPUT_ENABLE(2));
	DUMP_REG(DC_COM_PIN_INPUT_ENABLE(3));
	DUMP_REG(DC_COM_PIN_INPUT_DATA(0));
	DUMP_REG(DC_COM_PIN_INPUT_DATA(1));
	DUMP_REG(DC_COM_PIN_OUTPUT_SELECT(0));
	DUMP_REG(DC_COM_PIN_OUTPUT_SELECT(1));
	DUMP_REG(DC_COM_PIN_OUTPUT_SELECT(2));
	DUMP_REG(DC_COM_PIN_OUTPUT_SELECT(3));
	DUMP_REG(DC_COM_PIN_OUTPUT_SELECT(4));
	DUMP_REG(DC_COM_PIN_OUTPUT_SELECT(5));
	DUMP_REG(DC_COM_PIN_OUTPUT_SELECT(6));
	DUMP_REG(DC_COM_PIN_MISC_CONTROL);
	DUMP_REG(DC_COM_PIN_PM0_CONTROL);
	DUMP_REG(DC_COM_PIN_PM0_DUTY_CYCLE);
	DUMP_REG(DC_COM_PIN_PM1_CONTROL);
	DUMP_REG(DC_COM_PIN_PM1_DUTY_CYCLE);
	DUMP_REG(DC_COM_SPI_CONTROL);
	DUMP_REG(DC_COM_SPI_START_BYTE);
	DUMP_REG(DC_COM_HSPI_WRITE_DATA_AB);
	DUMP_REG(DC_COM_HSPI_WRITE_DATA_CD);
	DUMP_REG(DC_COM_HSPI_CS_DC);
	DUMP_REG(DC_COM_SCRATCH_REGISTER_A);
	DUMP_REG(DC_COM_SCRATCH_REGISTER_B);
	DUMP_REG(DC_COM_GPIO_CTRL);
	DUMP_REG(DC_COM_GPIO_DEBOUNCE_COUNTER);
	DUMP_REG(DC_COM_CRC_CHECKSUM_LATCHED);
	DUMP_REG(DC_DISP_DISP_SIGNAL_OPTIONS0);
	DUMP_REG(DC_DISP_DISP_SIGNAL_OPTIONS1);
	DUMP_REG(DC_DISP_DISP_WIN_OPTIONS);
	DUMP_REG(DC_DISP_DISP_MEM_HIGH_PRIORITY);
	DUMP_REG(DC_DISP_DISP_MEM_HIGH_PRIORITY_TIMER);
	DUMP_REG(DC_DISP_DISP_TIMING_OPTIONS);
	DUMP_REG(DC_DISP_REF_TO_SYNC);
	DUMP_REG(DC_DISP_SYNC_WIDTH);
	DUMP_REG(DC_DISP_BACK_PORCH);
	DUMP_REG(DC_DISP_ACTIVE);
	DUMP_REG(DC_DISP_FRONT_PORCH);
	DUMP_REG(DC_DISP_H_PULSE0_CONTROL);
	DUMP_REG(DC_DISP_H_PULSE0_POSITION_A);
	DUMP_REG(DC_DISP_H_PULSE0_POSITION_B);
	DUMP_REG(DC_DISP_H_PULSE0_POSITION_C);
	DUMP_REG(DC_DISP_H_PULSE0_POSITION_D);
	DUMP_REG(DC_DISP_H_PULSE1_CONTROL);
	DUMP_REG(DC_DISP_H_PULSE1_POSITION_A);
	DUMP_REG(DC_DISP_H_PULSE1_POSITION_B);
	DUMP_REG(DC_DISP_H_PULSE1_POSITION_C);
	DUMP_REG(DC_DISP_H_PULSE1_POSITION_D);
	DUMP_REG(DC_DISP_H_PULSE2_CONTROL);
	DUMP_REG(DC_DISP_H_PULSE2_POSITION_A);
	DUMP_REG(DC_DISP_H_PULSE2_POSITION_B);
	DUMP_REG(DC_DISP_H_PULSE2_POSITION_C);
	DUMP_REG(DC_DISP_H_PULSE2_POSITION_D);
	DUMP_REG(DC_DISP_V_PULSE0_CONTROL);
	DUMP_REG(DC_DISP_V_PULSE0_POSITION_A);
	DUMP_REG(DC_DISP_V_PULSE0_POSITION_B);
	DUMP_REG(DC_DISP_V_PULSE0_POSITION_C);
	DUMP_REG(DC_DISP_V_PULSE1_CONTROL);
	DUMP_REG(DC_DISP_V_PULSE1_POSITION_A);
	DUMP_REG(DC_DISP_V_PULSE1_POSITION_B);
	DUMP_REG(DC_DISP_V_PULSE1_POSITION_C);
	DUMP_REG(DC_DISP_V_PULSE2_CONTROL);
	DUMP_REG(DC_DISP_V_PULSE2_POSITION_A);
	DUMP_REG(DC_DISP_V_PULSE3_CONTROL);
	DUMP_REG(DC_DISP_V_PULSE3_POSITION_A);
	DUMP_REG(DC_DISP_M0_CONTROL);
	DUMP_REG(DC_DISP_M1_CONTROL);
	DUMP_REG(DC_DISP_DI_CONTROL);
	DUMP_REG(DC_DISP_PP_CONTROL);
	DUMP_REG(DC_DISP_PP_SELECT_A);
	DUMP_REG(DC_DISP_PP_SELECT_B);
	DUMP_REG(DC_DISP_PP_SELECT_C);
	DUMP_REG(DC_DISP_PP_SELECT_D);
	DUMP_REG(DC_DISP_DISP_CLOCK_CONTROL);
	DUMP_REG(DC_DISP_DISP_INTERFACE_CONTROL);
	DUMP_REG(DC_DISP_DISP_COLOR_CONTROL);
	DUMP_REG(DC_DISP_SHIFT_CLOCK_OPTIONS);
	DUMP_REG(DC_DISP_DATA_ENABLE_OPTIONS);
	DUMP_REG(DC_DISP_SERIAL_INTERFACE_OPTIONS);
	DUMP_REG(DC_DISP_LCD_SPI_OPTIONS);
	DUMP_REG(DC_DISP_BORDER_COLOR);
	DUMP_REG(DC_DISP_COLOR_KEY0_LOWER);
	DUMP_REG(DC_DISP_COLOR_KEY0_UPPER);
	DUMP_REG(DC_DISP_COLOR_KEY1_LOWER);
	DUMP_REG(DC_DISP_COLOR_KEY1_UPPER);
	DUMP_REG(DC_DISP_CURSOR_FOREGROUND);
	DUMP_REG(DC_DISP_CURSOR_BACKGROUND);
	DUMP_REG(DC_DISP_CURSOR_START_ADDR);
	DUMP_REG(DC_DISP_CURSOR_START_ADDR_NS);
	DUMP_REG(DC_DISP_CURSOR_POSITION);
	DUMP_REG(DC_DISP_CURSOR_POSITION_NS);
	DUMP_REG(DC_DISP_INIT_SEQ_CONTROL);
	DUMP_REG(DC_DISP_SPI_INIT_SEQ_DATA_A);
	DUMP_REG(DC_DISP_SPI_INIT_SEQ_DATA_B);
	DUMP_REG(DC_DISP_SPI_INIT_SEQ_DATA_C);
	DUMP_REG(DC_DISP_SPI_INIT_SEQ_DATA_D);
	DUMP_REG(DC_DISP_DC_MCCIF_FIFOCTRL);
	DUMP_REG(DC_DISP_MCCIF_DISPLAY0A_HYST);
	DUMP_REG(DC_DISP_MCCIF_DISPLAY0B_HYST);
	DUMP_REG(DC_DISP_MCCIF_DISPLAY1A_HYST);
	DUMP_REG(DC_DISP_MCCIF_DISPLAY1B_HYST);
	DUMP_REG(DC_DISP_DAC_CRT_CTRL);
	DUMP_REG(DC_DISP_DISP_MISC_CONTROL);
	DUMP_REG(DC_DISP_SD_CONTROL);
	DUMP_REG(DC_DISP_SD_CSC_COEFF);
	DUMP_REG(DC_DISP_SD_LUT(0));
	DUMP_REG(DC_DISP_SD_LUT(1));
	DUMP_REG(DC_DISP_SD_LUT(2));
	DUMP_REG(DC_DISP_SD_LUT(3));
	DUMP_REG(DC_DISP_SD_LUT(4));
	DUMP_REG(DC_DISP_SD_LUT(5));
	DUMP_REG(DC_DISP_SD_LUT(6));
	DUMP_REG(DC_DISP_SD_LUT(7));
	DUMP_REG(DC_DISP_SD_LUT(8));
	DUMP_REG(DC_DISP_SD_FLICKER_CONTROL);
	DUMP_REG(DC_DISP_DC_PIXEL_COUNT);
	DUMP_REG(DC_DISP_SD_HISTOGRAM(0));
	DUMP_REG(DC_DISP_SD_HISTOGRAM(1));
	DUMP_REG(DC_DISP_SD_HISTOGRAM(2));
	DUMP_REG(DC_DISP_SD_HISTOGRAM(3));
	DUMP_REG(DC_DISP_SD_HISTOGRAM(4));
	DUMP_REG(DC_DISP_SD_HISTOGRAM(5));
	DUMP_REG(DC_DISP_SD_HISTOGRAM(6));
	DUMP_REG(DC_DISP_SD_HISTOGRAM(7));
	DUMP_REG(DC_DISP_SD_BL_TF(0));
	DUMP_REG(DC_DISP_SD_BL_TF(1));
	DUMP_REG(DC_DISP_SD_BL_TF(2));
	DUMP_REG(DC_DISP_SD_BL_TF(3));
	DUMP_REG(DC_DISP_SD_BL_CONTROL);
	DUMP_REG(DC_DISP_SD_HW_K_VALUES);
	DUMP_REG(DC_DISP_SD_MAN_K_VALUES);
	DUMP_REG(DC_WIN_WIN_OPTIONS);
	DUMP_REG(DC_WIN_BYTE_SWAP);
	DUMP_REG(DC_WIN_BUFFER_CONTROL);
	DUMP_REG(DC_WIN_COLOR_DEPTH);
	DUMP_REG(DC_WIN_POSITION);
	DUMP_REG(DC_WIN_SIZE);
	DUMP_REG(DC_WIN_PRESCALED_SIZE);
	DUMP_REG(DC_WIN_H_INITIAL_DDA);
	DUMP_REG(DC_WIN_V_INITIAL_DDA);
	DUMP_REG(DC_WIN_DDA_INC);
	DUMP_REG(DC_WIN_LINE_STRIDE);
	DUMP_REG(DC_WIN_BUF_STRIDE);
	DUMP_REG(DC_WIN_UV_BUF_STRIDE);
	DUMP_REG(DC_WIN_BUFFER_ADDR_MODE);
	DUMP_REG(DC_WIN_DV_CONTROL);
	DUMP_REG(DC_WIN_BLEND_NOKEY);
	DUMP_REG(DC_WIN_BLEND_1WIN);
	DUMP_REG(DC_WIN_BLEND_2WIN_X);
	DUMP_REG(DC_WIN_BLEND_2WIN_Y);
	DUMP_REG(DC_WIN_BLEND_3WIN_XY);
	DUMP_REG(DC_WIN_HP_FETCH_CONTROL);
	DUMP_REG(DC_WINBUF_START_ADDR);
	DUMP_REG(DC_WINBUF_START_ADDR_NS);
	DUMP_REG(DC_WINBUF_START_ADDR_U);
	DUMP_REG(DC_WINBUF_START_ADDR_U_NS);
	DUMP_REG(DC_WINBUF_START_ADDR_V);
	DUMP_REG(DC_WINBUF_START_ADDR_V_NS);
	DUMP_REG(DC_WINBUF_ADDR_H_OFFSET);
	DUMP_REG(DC_WINBUF_ADDR_H_OFFSET_NS);
	DUMP_REG(DC_WINBUF_ADDR_V_OFFSET);
	DUMP_REG(DC_WINBUF_ADDR_V_OFFSET_NS);
	DUMP_REG(DC_WINBUF_UFLOW_STATUS);
	DUMP_REG(DC_WINBUF_AD_UFLOW_STATUS);
	DUMP_REG(DC_WINBUF_BD_UFLOW_STATUS);
	DUMP_REG(DC_WINBUF_CD_UFLOW_STATUS);

#undef DUMP_REG

	return 0;
}

static struct drm_info_list debugfs_files[] = {
	{ "regs", tegra_dc_show_regs, 0, NULL },
};

static int tegra_dc_debugfs_init(struct tegra_dc *dc, struct drm_minor *minor)
{
	unsigned int i;
	char *name;
	int err;

	name = kasprintf(GFP_KERNEL, "dc.%d", dc->pipe);
	dc->debugfs = debugfs_create_dir(name, minor->debugfs_root);
	kfree(name);

	if (!dc->debugfs)
		return -ENOMEM;

	dc->debugfs_files = kmemdup(debugfs_files, sizeof(debugfs_files),
				    GFP_KERNEL);
	if (!dc->debugfs_files) {
		err = -ENOMEM;
		goto remove;
	}

	for (i = 0; i < ARRAY_SIZE(debugfs_files); i++)
		dc->debugfs_files[i].data = dc;

	err = drm_debugfs_create_files(dc->debugfs_files,
				       ARRAY_SIZE(debugfs_files),
				       dc->debugfs, minor);
	if (err < 0)
		goto free;

	dc->minor = minor;

	return 0;

free:
	kfree(dc->debugfs_files);
	dc->debugfs_files = NULL;
remove:
	debugfs_remove(dc->debugfs);
	dc->debugfs = NULL;

	return err;
}

static int tegra_dc_debugfs_exit(struct tegra_dc *dc)
{
	drm_debugfs_remove_files(dc->debugfs_files, ARRAY_SIZE(debugfs_files),
				 dc->minor);
	dc->minor = NULL;

	kfree(dc->debugfs_files);
	dc->debugfs_files = NULL;

	debugfs_remove(dc->debugfs);
	dc->debugfs = NULL;

	return 0;
}

static int tegra_dc_drm_init(struct host1x_client *client,
			     struct drm_device *drm)
{
	struct tegra_dc *dc = host1x_client_to_dc(client);
	int err;

	dc->pipe = drm->mode_config.num_crtc;

	drm_crtc_init(drm, &dc->base, &tegra_crtc_funcs);
	drm_mode_crtc_set_gamma_size(&dc->base, 256);
	drm_crtc_helper_add(&dc->base, &tegra_crtc_helper_funcs);

	err = tegra_dc_rgb_init(drm, dc);
	if (err < 0 && err != -ENODEV) {
		dev_err(dc->dev, "failed to initialize RGB output: %d\n", err);
		return err;
	}

	err = tegra_dc_add_planes(drm, dc);
	if (err < 0)
		return err;

	if (IS_ENABLED(CONFIG_DEBUG_FS)) {
		err = tegra_dc_debugfs_init(dc, drm->primary);
		if (err < 0)
			dev_err(dc->dev, "debugfs setup failed: %d\n", err);
	}

	err = devm_request_irq(dc->dev, dc->irq, tegra_dc_irq, 0,
			       dev_name(dc->dev), dc);
	if (err < 0) {
		dev_err(dc->dev, "failed to request IRQ#%u: %d\n", dc->irq,
			err);
		return err;
	}

	return 0;
}

static int tegra_dc_drm_exit(struct host1x_client *client)
{
	struct tegra_dc *dc = host1x_client_to_dc(client);
	int err;

	devm_free_irq(dc->dev, dc->irq, dc);

	if (IS_ENABLED(CONFIG_DEBUG_FS)) {
		err = tegra_dc_debugfs_exit(dc);
		if (err < 0)
			dev_err(dc->dev, "debugfs cleanup failed: %d\n", err);
	}

	err = tegra_dc_rgb_exit(dc);
	if (err) {
		dev_err(dc->dev, "failed to shutdown RGB output: %d\n", err);
		return err;
	}

	return 0;
}

static const struct host1x_client_ops dc_client_ops = {
	.drm_init = tegra_dc_drm_init,
	.drm_exit = tegra_dc_drm_exit,
};

static int tegra_dc_probe(struct platform_device *pdev)
{
	struct host1x_drm *host1x = host1x_get_drm_data(pdev->dev.parent);
	struct resource *regs;
	struct tegra_dc *dc;
	int err;

	dc = devm_kzalloc(&pdev->dev, sizeof(*dc), GFP_KERNEL);
	if (!dc)
		return -ENOMEM;

	spin_lock_init(&dc->lock);
	INIT_LIST_HEAD(&dc->list);
	dc->dev = &pdev->dev;

	dc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dc->clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		return PTR_ERR(dc->clk);
	}

	err = clk_prepare_enable(dc->clk);
	if (err < 0)
		return err;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dc->regs = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(dc->regs))
		return PTR_ERR(dc->regs);

	dc->irq = platform_get_irq(pdev, 0);
	if (dc->irq < 0) {
		dev_err(&pdev->dev, "failed to get IRQ\n");
		return -ENXIO;
	}

	INIT_LIST_HEAD(&dc->client.list);
	dc->client.ops = &dc_client_ops;
	dc->client.dev = &pdev->dev;

	err = tegra_dc_rgb_probe(dc);
	if (err < 0 && err != -ENODEV) {
		dev_err(&pdev->dev, "failed to probe RGB output: %d\n", err);
		return err;
	}

	err = host1x_register_client(host1x, &dc->client);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to register host1x client: %d\n",
			err);
		return err;
	}

	platform_set_drvdata(pdev, dc);

	return 0;
}

static int tegra_dc_remove(struct platform_device *pdev)
{
	struct host1x_drm *host1x = host1x_get_drm_data(pdev->dev.parent);
	struct tegra_dc *dc = platform_get_drvdata(pdev);
	int err;

	err = host1x_unregister_client(host1x, &dc->client);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to unregister host1x client: %d\n",
			err);
		return err;
	}

	clk_disable_unprepare(dc->clk);

	return 0;
}

static struct of_device_id tegra_dc_of_match[] = {
	{ .compatible = "nvidia,tegra30-dc", },
	{ .compatible = "nvidia,tegra20-dc", },
	{ },
};

struct platform_driver tegra_dc_driver = {
	.driver = {
		.name = "tegra-dc",
		.owner = THIS_MODULE,
		.of_match_table = tegra_dc_of_match,
	},
	.probe = tegra_dc_probe,
	.remove = tegra_dc_remove,
};
