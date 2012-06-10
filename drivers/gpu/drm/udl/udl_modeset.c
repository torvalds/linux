/*
 * Copyright (C) 2012 Red Hat
 *
 * based in parts on udlfb.c:
 * Copyright (C) 2009 Roberto De Ioris <roberto@unbit.it>
 * Copyright (C) 2009 Jaya Kumar <jayakumar.lkml@gmail.com>
 * Copyright (C) 2009 Bernie Thompson <bernie@plugable.com>

 * This file is subject to the terms and conditions of the GNU General Public
 * License v2. See the file COPYING in the main directory of this archive for
 * more details.
 */

#include "drmP.h"
#include "drm_crtc.h"
#include "drm_crtc_helper.h"
#include "udl_drv.h"

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

/*
 * On/Off for driving the DisplayLink framebuffer to the display
 *  0x00 H and V sync on
 *  0x01 H and V sync off (screen blank but powered)
 *  0x07 DPMS powerdown (requires modeset to come back)
 */
static char *udl_enable_hvsync(char *buf, bool enable)
{
	if (enable)
		return udl_set_register(buf, 0x1F, 0x00);
	else
		return udl_set_register(buf, 0x1F, 0x07);
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

static int udl_crtc_write_mode_to_hw(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct udl_device *udl = dev->dev_private;
	struct urb *urb;
	char *buf;
	int retval;

	urb = udl_get_urb(dev);
	if (!urb)
		return -ENOMEM;

	buf = (char *)urb->transfer_buffer;

	memcpy(buf, udl->mode_buf, udl->mode_buf_len);
	retval = udl_submit_urb(dev, urb, udl->mode_buf_len);
	DRM_INFO("write mode info %d\n", udl->mode_buf_len);
	return retval;
}


static void udl_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_device *dev = crtc->dev;
	struct udl_device *udl = dev->dev_private;
	int retval;

	if (mode == DRM_MODE_DPMS_OFF) {
		char *buf;
		struct urb *urb;
		urb = udl_get_urb(dev);
		if (!urb)
			return;

		buf = (char *)urb->transfer_buffer;
		buf = udl_vidreg_lock(buf);
		buf = udl_enable_hvsync(buf, false);
		buf = udl_vidreg_unlock(buf);

		retval = udl_submit_urb(dev, urb, buf - (char *)
					urb->transfer_buffer);
	} else {
		if (udl->mode_buf_len == 0) {
			DRM_ERROR("Trying to enable DPMS with no mode\n");
			return;
		}
		udl_crtc_write_mode_to_hw(crtc);
	}

}

static bool udl_crtc_mode_fixup(struct drm_crtc *crtc,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)

{
	return true;
}

#if 0
static int
udl_pipe_set_base_atomic(struct drm_crtc *crtc, struct drm_framebuffer *fb,
			   int x, int y, enum mode_set_atomic state)
{
	return 0;
}

static int
udl_pipe_set_base(struct drm_crtc *crtc, int x, int y,
		    struct drm_framebuffer *old_fb)
{
	return 0;
}
#endif

static int udl_crtc_mode_set(struct drm_crtc *crtc,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode,
			       int x, int y,
			       struct drm_framebuffer *old_fb)

{
	struct drm_device *dev = crtc->dev;
	struct udl_framebuffer *ufb = to_udl_fb(crtc->fb);
	struct udl_device *udl = dev->dev_private;
	char *buf;
	char *wrptr;
	int color_depth = 0;

	buf = (char *)udl->mode_buf;

	/* for now we just clip 24 -> 16 - if we fix that fix this */
	/*if  (crtc->fb->bits_per_pixel != 16)
	  color_depth = 1; */

	/* This first section has to do with setting the base address on the
	* controller * associated with the display. There are 2 base
	* pointers, currently, we only * use the 16 bpp segment.
	*/
	wrptr = udl_vidreg_lock(buf);
	wrptr = udl_set_color_depth(wrptr, color_depth);
	/* set base for 16bpp segment to 0 */
	wrptr = udl_set_base16bpp(wrptr, 0);
	/* set base for 8bpp segment to end of fb */
	wrptr = udl_set_base8bpp(wrptr, 2 * mode->vdisplay * mode->hdisplay);

	wrptr = udl_set_vid_cmds(wrptr, adjusted_mode);
	wrptr = udl_enable_hvsync(wrptr, true);
	wrptr = udl_vidreg_unlock(wrptr);

	ufb->active_16 = true;
	if (old_fb) {
		struct udl_framebuffer *uold_fb = to_udl_fb(old_fb);
		uold_fb->active_16 = false;
	}
	udl->mode_buf_len = wrptr - buf;

	/* damage all of it */
	udl_handle_damage(ufb, 0, 0, ufb->base.width, ufb->base.height);
	return 0;
}


static void udl_crtc_disable(struct drm_crtc *crtc)
{


}

static void udl_crtc_destroy(struct drm_crtc *crtc)
{
	drm_crtc_cleanup(crtc);
	kfree(crtc);
}

static void udl_load_lut(struct drm_crtc *crtc)
{
}

static void udl_crtc_prepare(struct drm_crtc *crtc)
{
}

static void udl_crtc_commit(struct drm_crtc *crtc)
{
	udl_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

static struct drm_crtc_helper_funcs udl_helper_funcs = {
	.dpms = udl_crtc_dpms,
	.mode_fixup = udl_crtc_mode_fixup,
	.mode_set = udl_crtc_mode_set,
	.prepare = udl_crtc_prepare,
	.commit = udl_crtc_commit,
	.disable = udl_crtc_disable,
	.load_lut = udl_load_lut,
};

static const struct drm_crtc_funcs udl_crtc_funcs = {
	.set_config = drm_crtc_helper_set_config,
	.destroy = udl_crtc_destroy,
};

int udl_crtc_init(struct drm_device *dev)
{
	struct drm_crtc *crtc;

	crtc = kzalloc(sizeof(struct drm_crtc) + sizeof(struct drm_connector *), GFP_KERNEL);
	if (crtc == NULL)
		return -ENOMEM;

	drm_crtc_init(dev, crtc, &udl_crtc_funcs);
	drm_crtc_helper_add(crtc, &udl_helper_funcs);

	return 0;
}

static const struct drm_mode_config_funcs udl_mode_funcs = {
	.fb_create = udl_fb_user_fb_create,
	.output_poll_changed = NULL,
};

int udl_modeset_init(struct drm_device *dev)
{
	struct drm_encoder *encoder;
	drm_mode_config_init(dev);

	dev->mode_config.min_width = 640;
	dev->mode_config.min_height = 480;

	dev->mode_config.max_width = 2048;
	dev->mode_config.max_height = 2048;

	dev->mode_config.prefer_shadow = 0;
	dev->mode_config.preferred_depth = 24;

	dev->mode_config.funcs = &udl_mode_funcs;

	drm_mode_create_dirty_info_property(dev);

	udl_crtc_init(dev);

	encoder = udl_encoder_init(dev);

	udl_connector_init(dev, encoder);

	return 0;
}

void udl_modeset_cleanup(struct drm_device *dev)
{
	drm_mode_config_cleanup(dev);
}
