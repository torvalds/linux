// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Red Hat
 *
 * based in parts on udlfb.c:
 * Copyright (C) 2009 Roberto De Ioris <roberto@unbit.it>
 * Copyright (C) 2009 Jaya Kumar <jayakumar.lkml@gmail.com>
 * Copyright (C) 2009 Bernie Thompson <bernie@plugable.com>

 */

#include <linux/dma-buf.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_vblank.h>

#include "udl_drv.h"

#define UDL_COLOR_DEPTH_16BPP	0

/*
 * All DisplayLink bulk operations start with 0xAF, followed by specific code
 * All operations are written to buffers which then later get sent to device
 */
static char *udl_set_register(char *buf, u8 reg, u8 val)
{
	*buf++ = 0xAF;
	*buf++ = 0x20;
	*buf++ = reg;
	*buf++ = val;
	return buf;
}

static char *udl_vidreg_lock(char *buf)
{
	return udl_set_register(buf, 0xFF, 0x00);
}

static char *udl_vidreg_unlock(char *buf)
{
	return udl_set_register(buf, 0xFF, 0xFF);
}

static char *udl_set_blank_mode(char *buf, u8 mode)
{
	return udl_set_register(buf, UDL_REG_BLANK_MODE, mode);
}

static char *udl_set_color_depth(char *buf, u8 selection)
{
	return udl_set_register(buf, 0x00, selection);
}

static char *udl_set_base16bpp(char *wrptr, u32 base)
{
	/* the base pointer is 16 bits wide, 0x20 is hi byte. */
	wrptr = udl_set_register(wrptr, 0x20, base >> 16);
	wrptr = udl_set_register(wrptr, 0x21, base >> 8);
	return udl_set_register(wrptr, 0x22, base);
}

/*
 * DisplayLink HW has separate 16bpp and 8bpp framebuffers.
 * In 24bpp modes, the low 323 RGB bits go in the 8bpp framebuffer
 */
static char *udl_set_base8bpp(char *wrptr, u32 base)
{
	wrptr = udl_set_register(wrptr, 0x26, base >> 16);
	wrptr = udl_set_register(wrptr, 0x27, base >> 8);
	return udl_set_register(wrptr, 0x28, base);
}

static char *udl_set_register_16(char *wrptr, u8 reg, u16 value)
{
	wrptr = udl_set_register(wrptr, reg, value >> 8);
	return udl_set_register(wrptr, reg+1, value);
}

/*
 * This is kind of weird because the controller takes some
 * register values in a different byte order than other registers.
 */
static char *udl_set_register_16be(char *wrptr, u8 reg, u16 value)
{
	wrptr = udl_set_register(wrptr, reg, value);
	return udl_set_register(wrptr, reg+1, value >> 8);
}

/*
 * LFSR is linear feedback shift register. The reason we have this is
 * because the display controller needs to minimize the clock depth of
 * various counters used in the display path. So this code reverses the
 * provided value into the lfsr16 value by counting backwards to get
 * the value that needs to be set in the hardware comparator to get the
 * same actual count. This makes sense once you read above a couple of
 * times and think about it from a hardware perspective.
 */
static u16 udl_lfsr16(u16 actual_count)
{
	u32 lv = 0xFFFF; /* This is the lfsr value that the hw starts with */

	while (actual_count--) {
		lv =	 ((lv << 1) |
			(((lv >> 15) ^ (lv >> 4) ^ (lv >> 2) ^ (lv >> 1)) & 1))
			& 0xFFFF;
	}

	return (u16) lv;
}

/*
 * This does LFSR conversion on the value that is to be written.
 * See LFSR explanation above for more detail.
 */
static char *udl_set_register_lfsr16(char *wrptr, u8 reg, u16 value)
{
	return udl_set_register_16(wrptr, reg, udl_lfsr16(value));
}

/*
 * This takes a standard fbdev screeninfo struct and all of its monitor mode
 * details and converts them into the DisplayLink equivalent register commands.
  ERR(vreg(dev,               0x00, (color_depth == 16) ? 0 : 1));
  ERR(vreg_lfsr16(dev,        0x01, xDisplayStart));
  ERR(vreg_lfsr16(dev,        0x03, xDisplayEnd));
  ERR(vreg_lfsr16(dev,        0x05, yDisplayStart));
  ERR(vreg_lfsr16(dev,        0x07, yDisplayEnd));
  ERR(vreg_lfsr16(dev,        0x09, xEndCount));
  ERR(vreg_lfsr16(dev,        0x0B, hSyncStart));
  ERR(vreg_lfsr16(dev,        0x0D, hSyncEnd));
  ERR(vreg_big_endian(dev,    0x0F, hPixels));
  ERR(vreg_lfsr16(dev,        0x11, yEndCount));
  ERR(vreg_lfsr16(dev,        0x13, vSyncStart));
  ERR(vreg_lfsr16(dev,        0x15, vSyncEnd));
  ERR(vreg_big_endian(dev,    0x17, vPixels));
  ERR(vreg_little_endian(dev, 0x1B, pixelClock5KHz));

  ERR(vreg(dev,               0x1F, 0));

  ERR(vbuf(dev, WRITE_VIDREG_UNLOCK, DSIZEOF(WRITE_VIDREG_UNLOCK)));
 */
static char *udl_set_vid_cmds(char *wrptr, struct drm_display_mode *mode)
{
	u16 xds, yds;
	u16 xde, yde;
	u16 yec;

	/* x display start */
	xds = mode->crtc_htotal - mode->crtc_hsync_start;
	wrptr = udl_set_register_lfsr16(wrptr, 0x01, xds);
	/* x display end */
	xde = xds + mode->crtc_hdisplay;
	wrptr = udl_set_register_lfsr16(wrptr, 0x03, xde);

	/* y display start */
	yds = mode->crtc_vtotal - mode->crtc_vsync_start;
	wrptr = udl_set_register_lfsr16(wrptr, 0x05, yds);
	/* y display end */
	yde = yds + mode->crtc_vdisplay;
	wrptr = udl_set_register_lfsr16(wrptr, 0x07, yde);

	/* x end count is active + blanking - 1 */
	wrptr = udl_set_register_lfsr16(wrptr, 0x09,
					mode->crtc_htotal - 1);

	/* libdlo hardcodes hsync start to 1 */
	wrptr = udl_set_register_lfsr16(wrptr, 0x0B, 1);

	/* hsync end is width of sync pulse + 1 */
	wrptr = udl_set_register_lfsr16(wrptr, 0x0D,
					mode->crtc_hsync_end - mode->crtc_hsync_start + 1);

	/* hpixels is active pixels */
	wrptr = udl_set_register_16(wrptr, 0x0F, mode->hdisplay);

	/* yendcount is vertical active + vertical blanking */
	yec = mode->crtc_vtotal;
	wrptr = udl_set_register_lfsr16(wrptr, 0x11, yec);

	/* libdlo hardcodes vsync start to 0 */
	wrptr = udl_set_register_lfsr16(wrptr, 0x13, 0);

	/* vsync end is width of vsync pulse */
	wrptr = udl_set_register_lfsr16(wrptr, 0x15, mode->crtc_vsync_end - mode->crtc_vsync_start);

	/* vpixels is active pixels */
	wrptr = udl_set_register_16(wrptr, 0x17, mode->crtc_vdisplay);

	wrptr = udl_set_register_16be(wrptr, 0x1B,
				      mode->clock / 5);

	return wrptr;
}

static char *udl_dummy_render(char *wrptr)
{
	*wrptr++ = 0xAF;
	*wrptr++ = 0x6A; /* copy */
	*wrptr++ = 0x00; /* from addr */
	*wrptr++ = 0x00;
	*wrptr++ = 0x00;
	*wrptr++ = 0x01; /* one pixel */
	*wrptr++ = 0x00; /* to address */
	*wrptr++ = 0x00;
	*wrptr++ = 0x00;
	return wrptr;
}

static int udl_crtc_write_mode_to_hw(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct udl_device *udl = to_udl(dev);
	struct urb *urb;
	char *buf;
	int retval;

	if (udl->mode_buf_len == 0) {
		DRM_ERROR("No mode set\n");
		return -EINVAL;
	}

	urb = udl_get_urb(dev);
	if (!urb)
		return -ENOMEM;

	buf = (char *)urb->transfer_buffer;

	memcpy(buf, udl->mode_buf, udl->mode_buf_len);
	retval = udl_submit_urb(dev, urb, udl->mode_buf_len);
	DRM_DEBUG("write mode info %d\n", udl->mode_buf_len);
	return retval;
}

static long udl_log_cpp(unsigned int cpp)
{
	if (WARN_ON(!is_power_of_2(cpp)))
		return -EINVAL;
	return __ffs(cpp);
}

static int udl_aligned_damage_clip(struct drm_rect *clip, int x, int y,
				   int width, int height)
{
	int x1, x2;

	if (WARN_ON_ONCE(x < 0) ||
	    WARN_ON_ONCE(y < 0) ||
	    WARN_ON_ONCE(width < 0) ||
	    WARN_ON_ONCE(height < 0))
		return -EINVAL;

	x1 = ALIGN_DOWN(x, sizeof(unsigned long));
	x2 = ALIGN(width + (x - x1), sizeof(unsigned long)) + x1;

	clip->x1 = x1;
	clip->y1 = y;
	clip->x2 = x2;
	clip->y2 = y + height;

	return 0;
}

static int udl_handle_damage(struct drm_framebuffer *fb, int x, int y,
			     int width, int height)
{
	struct drm_device *dev = fb->dev;
	struct dma_buf_attachment *import_attach = fb->obj[0]->import_attach;
	int i, ret, tmp_ret;
	char *cmd;
	struct urb *urb;
	struct drm_rect clip;
	int log_bpp;
	void *vaddr;

	ret = udl_log_cpp(fb->format->cpp[0]);
	if (ret < 0)
		return ret;
	log_bpp = ret;

	ret = udl_aligned_damage_clip(&clip, x, y, width, height);
	if (ret)
		return ret;
	else if ((clip.x2 > fb->width) || (clip.y2 > fb->height))
		return -EINVAL;

	if (import_attach) {
		ret = dma_buf_begin_cpu_access(import_attach->dmabuf,
					       DMA_FROM_DEVICE);
		if (ret)
			return ret;
	}

	vaddr = drm_gem_shmem_vmap(fb->obj[0]);
	if (IS_ERR(vaddr)) {
		DRM_ERROR("failed to vmap fb\n");
		goto out_dma_buf_end_cpu_access;
	}

	urb = udl_get_urb(dev);
	if (!urb) {
		ret = -ENOMEM;
		goto out_drm_gem_shmem_vunmap;
	}
	cmd = urb->transfer_buffer;

	for (i = clip.y1; i < clip.y2; i++) {
		const int line_offset = fb->pitches[0] * i;
		const int byte_offset = line_offset + (clip.x1 << log_bpp);
		const int dev_byte_offset = (fb->width * i + clip.x1) << log_bpp;
		const int byte_width = (clip.x2 - clip.x1) << log_bpp;
		ret = udl_render_hline(dev, log_bpp, &urb, (char *)vaddr,
				       &cmd, byte_offset, dev_byte_offset,
				       byte_width);
		if (ret)
			goto out_drm_gem_shmem_vunmap;
	}

	if (cmd > (char *)urb->transfer_buffer) {
		/* Send partial buffer remaining before exiting */
		int len;
		if (cmd < (char *)urb->transfer_buffer + urb->transfer_buffer_length)
			*cmd++ = 0xAF;
		len = cmd - (char *)urb->transfer_buffer;
		ret = udl_submit_urb(dev, urb, len);
	} else {
		udl_urb_completion(urb);
	}

	ret = 0;

out_drm_gem_shmem_vunmap:
	drm_gem_shmem_vunmap(fb->obj[0], vaddr);
out_dma_buf_end_cpu_access:
	if (import_attach) {
		tmp_ret = dma_buf_end_cpu_access(import_attach->dmabuf,
						 DMA_FROM_DEVICE);
		if (tmp_ret && !ret)
			ret = tmp_ret; /* only update ret if not set yet */
	}

	return ret;
}

/*
 * Simple display pipeline
 */

static const uint32_t udl_simple_display_pipe_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB8888,
};

static enum drm_mode_status
udl_simple_display_pipe_mode_valid(struct drm_simple_display_pipe *pipe,
				   const struct drm_display_mode *mode)
{
	return MODE_OK;
}

static void
udl_simple_display_pipe_enable(struct drm_simple_display_pipe *pipe,
			       struct drm_crtc_state *crtc_state,
			       struct drm_plane_state *plane_state)
{
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_device *dev = crtc->dev;
	struct drm_framebuffer *fb = plane_state->fb;
	struct udl_device *udl = to_udl(dev);
	struct drm_display_mode *mode = &crtc_state->mode;
	char *buf;
	char *wrptr;
	int color_depth = UDL_COLOR_DEPTH_16BPP;

	buf = (char *)udl->mode_buf;

	/* This first section has to do with setting the base address on the
	 * controller associated with the display. There are 2 base
	 * pointers, currently, we only use the 16 bpp segment.
	 */
	wrptr = udl_vidreg_lock(buf);
	wrptr = udl_set_color_depth(wrptr, color_depth);
	/* set base for 16bpp segment to 0 */
	wrptr = udl_set_base16bpp(wrptr, 0);
	/* set base for 8bpp segment to end of fb */
	wrptr = udl_set_base8bpp(wrptr, 2 * mode->vdisplay * mode->hdisplay);

	wrptr = udl_set_vid_cmds(wrptr, mode);
	wrptr = udl_set_blank_mode(wrptr, UDL_BLANK_MODE_ON);
	wrptr = udl_vidreg_unlock(wrptr);

	wrptr = udl_dummy_render(wrptr);

	udl->mode_buf_len = wrptr - buf;

	udl_handle_damage(fb, 0, 0, fb->width, fb->height);

	/* enable display */
	udl_crtc_write_mode_to_hw(crtc);
}

static void
udl_simple_display_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_device *dev = crtc->dev;
	struct urb *urb;
	char *buf;

	urb = udl_get_urb(dev);
	if (!urb)
		return;

	buf = (char *)urb->transfer_buffer;
	buf = udl_vidreg_lock(buf);
	buf = udl_set_blank_mode(buf, UDL_BLANK_MODE_POWERDOWN);
	buf = udl_vidreg_unlock(buf);
	buf = udl_dummy_render(buf);

	udl_submit_urb(dev, urb, buf - (char *)urb->transfer_buffer);
}

static void
udl_simple_display_pipe_update(struct drm_simple_display_pipe *pipe,
			       struct drm_plane_state *old_plane_state)
{
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_framebuffer *fb = state->fb;
	struct drm_rect rect;

	if (!fb)
		return;

	if (drm_atomic_helper_damage_merged(old_plane_state, state, &rect))
		udl_handle_damage(fb, rect.x1, rect.y1, rect.x2 - rect.x1,
				  rect.y2 - rect.y1);
}

static const
struct drm_simple_display_pipe_funcs udl_simple_display_pipe_funcs = {
	.mode_valid = udl_simple_display_pipe_mode_valid,
	.enable = udl_simple_display_pipe_enable,
	.disable = udl_simple_display_pipe_disable,
	.update = udl_simple_display_pipe_update,
	.prepare_fb = drm_gem_fb_simple_display_pipe_prepare_fb,
};

/*
 * Modesetting
 */

static const struct drm_mode_config_funcs udl_mode_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check  = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

int udl_modeset_init(struct drm_device *dev)
{
	size_t format_count = ARRAY_SIZE(udl_simple_display_pipe_formats);
	struct udl_device *udl = to_udl(dev);
	struct drm_connector *connector;
	int ret;

	ret = drmm_mode_config_init(dev);
	if (ret)
		return ret;

	dev->mode_config.min_width = 640;
	dev->mode_config.min_height = 480;

	dev->mode_config.max_width = 2048;
	dev->mode_config.max_height = 2048;

	dev->mode_config.prefer_shadow = 0;
	dev->mode_config.preferred_depth = 16;

	dev->mode_config.funcs = &udl_mode_funcs;

	connector = udl_connector_init(dev);
	if (IS_ERR(connector))
		return PTR_ERR(connector);

	format_count = ARRAY_SIZE(udl_simple_display_pipe_formats);

	ret = drm_simple_display_pipe_init(dev, &udl->display_pipe,
					   &udl_simple_display_pipe_funcs,
					   udl_simple_display_pipe_formats,
					   format_count, NULL, connector);
	if (ret)
		return ret;

	drm_mode_config_reset(dev);

	return 0;
}
