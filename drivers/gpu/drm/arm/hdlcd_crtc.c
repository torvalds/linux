/*
 * Copyright (C) 2013-2015 ARM Limited
 * Author: Liviu Dudau <Liviu.Dudau@arm.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 *  Implementation of a CRTC class for the HDLCD driver.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_plane_helper.h>
#include <linux/clk.h>
#include <linux/of_graph.h>
#include <linux/platform_data/simplefb.h>
#include <video/videomode.h>

#include "hdlcd_drv.h"
#include "hdlcd_regs.h"

/*
 * The HDLCD controller is a dumb RGB streamer that gets connected to
 * a single HDMI transmitter or in the case of the ARM Models it gets
 * emulated by the software that does the actual rendering.
 *
 */

static void hdlcd_crtc_cleanup(struct drm_crtc *crtc)
{
	struct hdlcd_drm_private *hdlcd = crtc_to_hdlcd_priv(crtc);

	/* stop the controller on cleanup */
	hdlcd_write(hdlcd, HDLCD_REG_COMMAND, 0);
	drm_crtc_cleanup(crtc);
}

static int hdlcd_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct hdlcd_drm_private *hdlcd = crtc_to_hdlcd_priv(crtc);
	unsigned int mask = hdlcd_read(hdlcd, HDLCD_REG_INT_MASK);

	hdlcd_write(hdlcd, HDLCD_REG_INT_MASK, mask | HDLCD_INTERRUPT_VSYNC);

	return 0;
}

static void hdlcd_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct hdlcd_drm_private *hdlcd = crtc_to_hdlcd_priv(crtc);
	unsigned int mask = hdlcd_read(hdlcd, HDLCD_REG_INT_MASK);

	hdlcd_write(hdlcd, HDLCD_REG_INT_MASK, mask & ~HDLCD_INTERRUPT_VSYNC);
}

static const struct drm_crtc_funcs hdlcd_crtc_funcs = {
	.destroy = hdlcd_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = hdlcd_crtc_enable_vblank,
	.disable_vblank = hdlcd_crtc_disable_vblank,
};

static struct simplefb_format supported_formats[] = SIMPLEFB_FORMATS;

/*
 * Setup the HDLCD registers for decoding the pixels out of the framebuffer
 */
static int hdlcd_set_pxl_fmt(struct drm_crtc *crtc)
{
	unsigned int btpp;
	struct hdlcd_drm_private *hdlcd = crtc_to_hdlcd_priv(crtc);
	const struct drm_framebuffer *fb = crtc->primary->state->fb;
	uint32_t pixel_format;
	struct simplefb_format *format = NULL;
	int i;

	pixel_format = fb->format->format;

	for (i = 0; i < ARRAY_SIZE(supported_formats); i++) {
		if (supported_formats[i].fourcc == pixel_format)
			format = &supported_formats[i];
	}

	if (WARN_ON(!format))
		return 0;

	/* HDLCD uses 'bytes per pixel', zero means 1 byte */
	btpp = (format->bits_per_pixel + 7) / 8;
	hdlcd_write(hdlcd, HDLCD_REG_PIXEL_FORMAT, (btpp - 1) << 3);

	/*
	 * The format of the HDLCD_REG_<color>_SELECT register is:
	 *   - bits[23:16] - default value for that color component
	 *   - bits[11:8]  - number of bits to extract for each color component
	 *   - bits[4:0]   - index of the lowest bit to extract
	 *
	 * The default color value is used when bits[11:8] are zero, when the
	 * pixel is outside the visible frame area or when there is a
	 * buffer underrun.
	 */
	hdlcd_write(hdlcd, HDLCD_REG_RED_SELECT, format->red.offset |
#ifdef CONFIG_DRM_HDLCD_SHOW_UNDERRUN
		    0x00ff0000 |	/* show underruns in red */
#endif
		    ((format->red.length & 0xf) << 8));
	hdlcd_write(hdlcd, HDLCD_REG_GREEN_SELECT, format->green.offset |
		    ((format->green.length & 0xf) << 8));
	hdlcd_write(hdlcd, HDLCD_REG_BLUE_SELECT, format->blue.offset |
		    ((format->blue.length & 0xf) << 8));

	return 0;
}

static void hdlcd_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct hdlcd_drm_private *hdlcd = crtc_to_hdlcd_priv(crtc);
	struct drm_display_mode *m = &crtc->state->adjusted_mode;
	struct videomode vm;
	unsigned int polarities, err;

	vm.vfront_porch = m->crtc_vsync_start - m->crtc_vdisplay;
	vm.vback_porch = m->crtc_vtotal - m->crtc_vsync_end;
	vm.vsync_len = m->crtc_vsync_end - m->crtc_vsync_start;
	vm.hfront_porch = m->crtc_hsync_start - m->crtc_hdisplay;
	vm.hback_porch = m->crtc_htotal - m->crtc_hsync_end;
	vm.hsync_len = m->crtc_hsync_end - m->crtc_hsync_start;

	polarities = HDLCD_POLARITY_DATAEN | HDLCD_POLARITY_DATA;

	if (m->flags & DRM_MODE_FLAG_PHSYNC)
		polarities |= HDLCD_POLARITY_HSYNC;
	if (m->flags & DRM_MODE_FLAG_PVSYNC)
		polarities |= HDLCD_POLARITY_VSYNC;

	/* Allow max number of outstanding requests and largest burst size */
	hdlcd_write(hdlcd, HDLCD_REG_BUS_OPTIONS,
		    HDLCD_BUS_MAX_OUTSTAND | HDLCD_BUS_BURST_16);

	hdlcd_write(hdlcd, HDLCD_REG_V_DATA, m->crtc_vdisplay - 1);
	hdlcd_write(hdlcd, HDLCD_REG_V_BACK_PORCH, vm.vback_porch - 1);
	hdlcd_write(hdlcd, HDLCD_REG_V_FRONT_PORCH, vm.vfront_porch - 1);
	hdlcd_write(hdlcd, HDLCD_REG_V_SYNC, vm.vsync_len - 1);
	hdlcd_write(hdlcd, HDLCD_REG_H_DATA, m->crtc_hdisplay - 1);
	hdlcd_write(hdlcd, HDLCD_REG_H_BACK_PORCH, vm.hback_porch - 1);
	hdlcd_write(hdlcd, HDLCD_REG_H_FRONT_PORCH, vm.hfront_porch - 1);
	hdlcd_write(hdlcd, HDLCD_REG_H_SYNC, vm.hsync_len - 1);
	hdlcd_write(hdlcd, HDLCD_REG_POLARITIES, polarities);

	err = hdlcd_set_pxl_fmt(crtc);
	if (err)
		return;

	clk_set_rate(hdlcd->clk, m->crtc_clock * 1000);
}

static void hdlcd_crtc_atomic_enable(struct drm_crtc *crtc,
				     struct drm_crtc_state *old_state)
{
	struct hdlcd_drm_private *hdlcd = crtc_to_hdlcd_priv(crtc);

	clk_prepare_enable(hdlcd->clk);
	hdlcd_crtc_mode_set_nofb(crtc);
	hdlcd_write(hdlcd, HDLCD_REG_COMMAND, 1);
	drm_crtc_vblank_on(crtc);
}

static void hdlcd_crtc_atomic_disable(struct drm_crtc *crtc,
				      struct drm_crtc_state *old_state)
{
	struct hdlcd_drm_private *hdlcd = crtc_to_hdlcd_priv(crtc);

	drm_crtc_vblank_off(crtc);
	hdlcd_write(hdlcd, HDLCD_REG_COMMAND, 0);
	clk_disable_unprepare(hdlcd->clk);
}

static int hdlcd_crtc_atomic_check(struct drm_crtc *crtc,
				   struct drm_crtc_state *state)
{
	struct hdlcd_drm_private *hdlcd = crtc_to_hdlcd_priv(crtc);
	struct drm_display_mode *mode = &state->adjusted_mode;
	long rate, clk_rate = mode->clock * 1000;

	rate = clk_round_rate(hdlcd->clk, clk_rate);
	if (rate != clk_rate) {
		/* clock required by mode not supported by hardware */
		return -EINVAL;
	}

	return 0;
}

static void hdlcd_crtc_atomic_begin(struct drm_crtc *crtc,
				    struct drm_crtc_state *state)
{
	struct drm_pending_vblank_event *event = crtc->state->event;

	if (event) {
		crtc->state->event = NULL;

		spin_lock_irq(&crtc->dev->event_lock);
		if (drm_crtc_vblank_get(crtc) == 0)
			drm_crtc_arm_vblank_event(crtc, event);
		else
			drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

static const struct drm_crtc_helper_funcs hdlcd_crtc_helper_funcs = {
	.atomic_check	= hdlcd_crtc_atomic_check,
	.atomic_begin	= hdlcd_crtc_atomic_begin,
	.atomic_enable	= hdlcd_crtc_atomic_enable,
	.atomic_disable	= hdlcd_crtc_atomic_disable,
};

static int hdlcd_plane_atomic_check(struct drm_plane *plane,
				    struct drm_plane_state *state)
{
	struct drm_rect clip = { 0 };
	struct drm_crtc_state *crtc_state;
	u32 src_h = state->src_h >> 16;

	/* only the HDLCD_REG_FB_LINE_COUNT register has a limit */
	if (src_h >= HDLCD_MAX_YRES) {
		DRM_DEBUG_KMS("Invalid source width: %d\n", src_h);
		return -EINVAL;
	}

	if (!state->fb || !state->crtc)
		return 0;

	crtc_state = drm_atomic_get_existing_crtc_state(state->state,
							state->crtc);
	if (!crtc_state) {
		DRM_DEBUG_KMS("Invalid crtc state\n");
		return -EINVAL;
	}

	clip.x2 = crtc_state->adjusted_mode.hdisplay;
	clip.y2 = crtc_state->adjusted_mode.vdisplay;

	return drm_plane_helper_check_state(state, crtc_state, &clip,
					    DRM_PLANE_HELPER_NO_SCALING,
					    DRM_PLANE_HELPER_NO_SCALING,
					    false, true);
}

static void hdlcd_plane_atomic_update(struct drm_plane *plane,
				      struct drm_plane_state *state)
{
	struct drm_framebuffer *fb = plane->state->fb;
	struct hdlcd_drm_private *hdlcd;
	u32 dest_h;
	dma_addr_t scanout_start;

	if (!fb)
		return;

	dest_h = drm_rect_height(&plane->state->dst);
	scanout_start = drm_fb_cma_get_gem_addr(fb, plane->state, 0);

	hdlcd = plane->dev->dev_private;
	hdlcd_write(hdlcd, HDLCD_REG_FB_LINE_LENGTH, fb->pitches[0]);
	hdlcd_write(hdlcd, HDLCD_REG_FB_LINE_PITCH, fb->pitches[0]);
	hdlcd_write(hdlcd, HDLCD_REG_FB_LINE_COUNT, dest_h - 1);
	hdlcd_write(hdlcd, HDLCD_REG_FB_BASE, scanout_start);
}

static const struct drm_plane_helper_funcs hdlcd_plane_helper_funcs = {
	.atomic_check = hdlcd_plane_atomic_check,
	.atomic_update = hdlcd_plane_atomic_update,
};

static void hdlcd_plane_destroy(struct drm_plane *plane)
{
	drm_plane_helper_disable(plane);
	drm_plane_cleanup(plane);
}

static const struct drm_plane_funcs hdlcd_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= hdlcd_plane_destroy,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static struct drm_plane *hdlcd_plane_init(struct drm_device *drm)
{
	struct hdlcd_drm_private *hdlcd = drm->dev_private;
	struct drm_plane *plane = NULL;
	u32 formats[ARRAY_SIZE(supported_formats)], i;
	int ret;

	plane = devm_kzalloc(drm->dev, sizeof(*plane), GFP_KERNEL);
	if (!plane)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < ARRAY_SIZE(supported_formats); i++)
		formats[i] = supported_formats[i].fourcc;

	ret = drm_universal_plane_init(drm, plane, 0xff, &hdlcd_plane_funcs,
				       formats, ARRAY_SIZE(formats),
				       NULL,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		return ERR_PTR(ret);
	}

	drm_plane_helper_add(plane, &hdlcd_plane_helper_funcs);
	hdlcd->plane = plane;

	return plane;
}

int hdlcd_setup_crtc(struct drm_device *drm)
{
	struct hdlcd_drm_private *hdlcd = drm->dev_private;
	struct drm_plane *primary;
	int ret;

	primary = hdlcd_plane_init(drm);
	if (IS_ERR(primary))
		return PTR_ERR(primary);

	ret = drm_crtc_init_with_planes(drm, &hdlcd->crtc, primary, NULL,
					&hdlcd_crtc_funcs, NULL);
	if (ret) {
		hdlcd_plane_destroy(primary);
		return ret;
	}

	drm_crtc_helper_add(&hdlcd->crtc, &hdlcd_crtc_helper_funcs);
	return 0;
}
