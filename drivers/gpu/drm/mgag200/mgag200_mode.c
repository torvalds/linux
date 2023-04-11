// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2010 Matt Turner.
 * Copyright 2012 Red Hat
 *
 * Authors: Matthew Garrett
 *	    Matt Turner
 *	    Dave Airlie
 */

#include <linux/delay.h>
#include <linux/iosys-map.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "mgag200_drv.h"

/*
 * This file contains setup code for the CRTC.
 */

static void mgag200_crtc_set_gamma_linear(struct mga_device *mdev,
					  const struct drm_format_info *format)
{
	int i;

	WREG8(DAC_INDEX + MGA1064_INDEX, 0);

	switch (format->format) {
	case DRM_FORMAT_RGB565:
		/* Use better interpolation, to take 32 values from 0 to 255 */
		for (i = 0; i < MGAG200_LUT_SIZE / 8; i++) {
			WREG8(DAC_INDEX + MGA1064_COL_PAL, i * 8 + i / 4);
			WREG8(DAC_INDEX + MGA1064_COL_PAL, i * 4 + i / 16);
			WREG8(DAC_INDEX + MGA1064_COL_PAL, i * 8 + i / 4);
		}
		/* Green has one more bit, so add padding with 0 for red and blue. */
		for (i = MGAG200_LUT_SIZE / 8; i < MGAG200_LUT_SIZE / 4; i++) {
			WREG8(DAC_INDEX + MGA1064_COL_PAL, 0);
			WREG8(DAC_INDEX + MGA1064_COL_PAL, i * 4 + i / 16);
			WREG8(DAC_INDEX + MGA1064_COL_PAL, 0);
		}
		break;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_XRGB8888:
		for (i = 0; i < MGAG200_LUT_SIZE; i++) {
			WREG8(DAC_INDEX + MGA1064_COL_PAL, i);
			WREG8(DAC_INDEX + MGA1064_COL_PAL, i);
			WREG8(DAC_INDEX + MGA1064_COL_PAL, i);
		}
		break;
	default:
		drm_warn_once(&mdev->base, "Unsupported format %p4cc for gamma correction\n",
			      &format->format);
		break;
	}
}

static void mgag200_crtc_set_gamma(struct mga_device *mdev,
				   const struct drm_format_info *format,
				   struct drm_color_lut *lut)
{
	int i;

	WREG8(DAC_INDEX + MGA1064_INDEX, 0);

	switch (format->format) {
	case DRM_FORMAT_RGB565:
		/* Use better interpolation, to take 32 values from lut[0] to lut[255] */
		for (i = 0; i < MGAG200_LUT_SIZE / 8; i++) {
			WREG8(DAC_INDEX + MGA1064_COL_PAL, lut[i * 8 + i / 4].red >> 8);
			WREG8(DAC_INDEX + MGA1064_COL_PAL, lut[i * 4 + i / 16].green >> 8);
			WREG8(DAC_INDEX + MGA1064_COL_PAL, lut[i * 8 + i / 4].blue >> 8);
		}
		/* Green has one more bit, so add padding with 0 for red and blue. */
		for (i = MGAG200_LUT_SIZE / 8; i < MGAG200_LUT_SIZE / 4; i++) {
			WREG8(DAC_INDEX + MGA1064_COL_PAL, 0);
			WREG8(DAC_INDEX + MGA1064_COL_PAL, lut[i * 4 + i / 16].green >> 8);
			WREG8(DAC_INDEX + MGA1064_COL_PAL, 0);
		}
		break;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_XRGB8888:
		for (i = 0; i < MGAG200_LUT_SIZE; i++) {
			WREG8(DAC_INDEX + MGA1064_COL_PAL, lut[i].red >> 8);
			WREG8(DAC_INDEX + MGA1064_COL_PAL, lut[i].green >> 8);
			WREG8(DAC_INDEX + MGA1064_COL_PAL, lut[i].blue >> 8);
		}
		break;
	default:
		drm_warn_once(&mdev->base, "Unsupported format %p4cc for gamma correction\n",
			      &format->format);
		break;
	}
}

static inline void mga_wait_vsync(struct mga_device *mdev)
{
	unsigned long timeout = jiffies + HZ/10;
	unsigned int status = 0;

	do {
		status = RREG32(MGAREG_Status);
	} while ((status & 0x08) && time_before(jiffies, timeout));
	timeout = jiffies + HZ/10;
	status = 0;
	do {
		status = RREG32(MGAREG_Status);
	} while (!(status & 0x08) && time_before(jiffies, timeout));
}

static inline void mga_wait_busy(struct mga_device *mdev)
{
	unsigned long timeout = jiffies + HZ;
	unsigned int status = 0;
	do {
		status = RREG8(MGAREG_Status + 2);
	} while ((status & 0x01) && time_before(jiffies, timeout));
}

/*
 * This is how the framebuffer base address is stored in g200 cards:
 *   * Assume @offset is the gpu_addr variable of the framebuffer object
 *   * Then addr is the number of _pixels_ (not bytes) from the start of
 *     VRAM to the first pixel we want to display. (divided by 2 for 32bit
 *     framebuffers)
 *   * addr is stored in the CRTCEXT0, CRTCC and CRTCD registers
 *      addr<20> -> CRTCEXT0<6>
 *      addr<19-16> -> CRTCEXT0<3-0>
 *      addr<15-8> -> CRTCC<7-0>
 *      addr<7-0> -> CRTCD<7-0>
 *
 *  CRTCEXT0 has to be programmed last to trigger an update and make the
 *  new addr variable take effect.
 */
static void mgag200_set_startadd(struct mga_device *mdev,
				 unsigned long offset)
{
	struct drm_device *dev = &mdev->base;
	u32 startadd;
	u8 crtcc, crtcd, crtcext0;

	startadd = offset / 8;

	if (startadd > 0)
		drm_WARN_ON_ONCE(dev, mdev->info->bug_no_startadd);

	/*
	 * Can't store addresses any higher than that, but we also
	 * don't have more than 16 MiB of memory, so it should be fine.
	 */
	drm_WARN_ON(dev, startadd > 0x1fffff);

	RREG_ECRT(0x00, crtcext0);

	crtcc = (startadd >> 8) & 0xff;
	crtcd = startadd & 0xff;
	crtcext0 &= 0xb0;
	crtcext0 |= ((startadd >> 14) & BIT(6)) |
		    ((startadd >> 16) & 0x0f);

	WREG_CRT(0x0c, crtcc);
	WREG_CRT(0x0d, crtcd);
	WREG_ECRT(0x00, crtcext0);
}

void mgag200_init_registers(struct mga_device *mdev)
{
	u8 crtc11, misc;

	WREG_SEQ(2, 0x0f);
	WREG_SEQ(3, 0x00);
	WREG_SEQ(4, 0x0e);

	WREG_CRT(10, 0);
	WREG_CRT(11, 0);
	WREG_CRT(12, 0);
	WREG_CRT(13, 0);
	WREG_CRT(14, 0);
	WREG_CRT(15, 0);

	RREG_CRT(0x11, crtc11);
	crtc11 &= ~(MGAREG_CRTC11_CRTCPROTECT |
		    MGAREG_CRTC11_VINTEN |
		    MGAREG_CRTC11_VINTCLR);
	WREG_CRT(0x11, crtc11);

	misc = RREG8(MGA_MISC_IN);
	misc |= MGAREG_MISC_IOADSEL;
	WREG8(MGA_MISC_OUT, misc);
}

void mgag200_set_mode_regs(struct mga_device *mdev, const struct drm_display_mode *mode)
{
	const struct mgag200_device_info *info = mdev->info;
	unsigned int hdisplay, hsyncstart, hsyncend, htotal;
	unsigned int vdisplay, vsyncstart, vsyncend, vtotal;
	u8 misc, crtcext1, crtcext2, crtcext5;

	hdisplay = mode->hdisplay / 8 - 1;
	hsyncstart = mode->hsync_start / 8 - 1;
	hsyncend = mode->hsync_end / 8 - 1;
	htotal = mode->htotal / 8 - 1;

	/* Work around hardware quirk */
	if ((htotal & 0x07) == 0x06 || (htotal & 0x07) == 0x04)
		htotal++;

	vdisplay = mode->vdisplay - 1;
	vsyncstart = mode->vsync_start - 1;
	vsyncend = mode->vsync_end - 1;
	vtotal = mode->vtotal - 2;

	misc = RREG8(MGA_MISC_IN);

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		misc |= MGAREG_MISC_HSYNCPOL;
	else
		misc &= ~MGAREG_MISC_HSYNCPOL;

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		misc |= MGAREG_MISC_VSYNCPOL;
	else
		misc &= ~MGAREG_MISC_VSYNCPOL;

	crtcext1 = (((htotal - 4) & 0x100) >> 8) |
		   ((hdisplay & 0x100) >> 7) |
		   ((hsyncstart & 0x100) >> 6) |
		    (htotal & 0x40);
	if (info->has_vidrst)
		crtcext1 |= MGAREG_CRTCEXT1_VRSTEN |
			    MGAREG_CRTCEXT1_HRSTEN;

	crtcext2 = ((vtotal & 0xc00) >> 10) |
		   ((vdisplay & 0x400) >> 8) |
		   ((vdisplay & 0xc00) >> 7) |
		   ((vsyncstart & 0xc00) >> 5) |
		   ((vdisplay & 0x400) >> 3);
	crtcext5 = 0x00;

	WREG_CRT(0, htotal - 4);
	WREG_CRT(1, hdisplay);
	WREG_CRT(2, hdisplay);
	WREG_CRT(3, (htotal & 0x1F) | 0x80);
	WREG_CRT(4, hsyncstart);
	WREG_CRT(5, ((htotal & 0x20) << 2) | (hsyncend & 0x1F));
	WREG_CRT(6, vtotal & 0xFF);
	WREG_CRT(7, ((vtotal & 0x100) >> 8) |
		 ((vdisplay & 0x100) >> 7) |
		 ((vsyncstart & 0x100) >> 6) |
		 ((vdisplay & 0x100) >> 5) |
		 ((vdisplay & 0x100) >> 4) | /* linecomp */
		 ((vtotal & 0x200) >> 4) |
		 ((vdisplay & 0x200) >> 3) |
		 ((vsyncstart & 0x200) >> 2));
	WREG_CRT(9, ((vdisplay & 0x200) >> 4) |
		 ((vdisplay & 0x200) >> 3));
	WREG_CRT(16, vsyncstart & 0xFF);
	WREG_CRT(17, (vsyncend & 0x0F) | 0x20);
	WREG_CRT(18, vdisplay & 0xFF);
	WREG_CRT(20, 0);
	WREG_CRT(21, vdisplay & 0xFF);
	WREG_CRT(22, (vtotal + 1) & 0xFF);
	WREG_CRT(23, 0xc3);
	WREG_CRT(24, vdisplay & 0xFF);

	WREG_ECRT(0x01, crtcext1);
	WREG_ECRT(0x02, crtcext2);
	WREG_ECRT(0x05, crtcext5);

	WREG8(MGA_MISC_OUT, misc);
}

static u8 mgag200_get_bpp_shift(const struct drm_format_info *format)
{
	static const u8 bpp_shift[] = {0, 1, 0, 2};

	return bpp_shift[format->cpp[0] - 1];
}

/*
 * Calculates the HW offset value from the framebuffer's pitch. The
 * offset is a multiple of the pixel size and depends on the display
 * format.
 */
static u32 mgag200_calculate_offset(struct mga_device *mdev,
				    const struct drm_framebuffer *fb)
{
	u32 offset = fb->pitches[0] / fb->format->cpp[0];
	u8 bppshift = mgag200_get_bpp_shift(fb->format);

	if (fb->format->cpp[0] * 8 == 24)
		offset = (offset * 3) >> (4 - bppshift);
	else
		offset = offset >> (4 - bppshift);

	return offset;
}

static void mgag200_set_offset(struct mga_device *mdev,
			       const struct drm_framebuffer *fb)
{
	u8 crtc13, crtcext0;
	u32 offset = mgag200_calculate_offset(mdev, fb);

	RREG_ECRT(0, crtcext0);

	crtc13 = offset & 0xff;

	crtcext0 &= ~MGAREG_CRTCEXT0_OFFSET_MASK;
	crtcext0 |= (offset >> 4) & MGAREG_CRTCEXT0_OFFSET_MASK;

	WREG_CRT(0x13, crtc13);
	WREG_ECRT(0x00, crtcext0);
}

void mgag200_set_format_regs(struct mga_device *mdev, const struct drm_format_info *format)
{
	struct drm_device *dev = &mdev->base;
	unsigned int bpp, bppshift, scale;
	u8 crtcext3, xmulctrl;

	bpp = format->cpp[0] * 8;

	bppshift = mgag200_get_bpp_shift(format);
	switch (bpp) {
	case 24:
		scale = ((1 << bppshift) * 3) - 1;
		break;
	default:
		scale = (1 << bppshift) - 1;
		break;
	}

	RREG_ECRT(3, crtcext3);

	switch (bpp) {
	case 8:
		xmulctrl = MGA1064_MUL_CTL_8bits;
		break;
	case 16:
		if (format->depth == 15)
			xmulctrl = MGA1064_MUL_CTL_15bits;
		else
			xmulctrl = MGA1064_MUL_CTL_16bits;
		break;
	case 24:
		xmulctrl = MGA1064_MUL_CTL_24bits;
		break;
	case 32:
		xmulctrl = MGA1064_MUL_CTL_32_24bits;
		break;
	default:
		/* BUG: We should have caught this problem already. */
		drm_WARN_ON(dev, "invalid format depth\n");
		return;
	}

	crtcext3 &= ~GENMASK(2, 0);
	crtcext3 |= scale;

	WREG_DAC(MGA1064_MUL_CTL, xmulctrl);

	WREG_GFX(0, 0x00);
	WREG_GFX(1, 0x00);
	WREG_GFX(2, 0x00);
	WREG_GFX(3, 0x00);
	WREG_GFX(4, 0x00);
	WREG_GFX(5, 0x40);
	/* GCTL6 should be 0x05, but we configure memmapsl to 0xb8000 (text mode),
	 * so that it doesn't hang when running kexec/kdump on G200_SE rev42.
	 */
	WREG_GFX(6, 0x0d);
	WREG_GFX(7, 0x0f);
	WREG_GFX(8, 0x0f);

	WREG_ECRT(3, crtcext3);
}

void mgag200_enable_display(struct mga_device *mdev)
{
	u8 seq0, crtcext1;

	RREG_SEQ(0x00, seq0);
	seq0 |= MGAREG_SEQ0_SYNCRST |
		MGAREG_SEQ0_ASYNCRST;
	WREG_SEQ(0x00, seq0);

	/*
	 * TODO: replace busy waiting with vblank IRQ; put
	 *       msleep(50) before changing SCROFF
	 */
	mga_wait_vsync(mdev);
	mga_wait_busy(mdev);

	RREG_ECRT(0x01, crtcext1);
	crtcext1 &= ~MGAREG_CRTCEXT1_VSYNCOFF;
	crtcext1 &= ~MGAREG_CRTCEXT1_HSYNCOFF;
	WREG_ECRT(0x01, crtcext1);
}

static void mgag200_disable_display(struct mga_device *mdev)
{
	u8 seq0, crtcext1;

	RREG_SEQ(0x00, seq0);
	seq0 &= ~MGAREG_SEQ0_SYNCRST;
	WREG_SEQ(0x00, seq0);

	/*
	 * TODO: replace busy waiting with vblank IRQ; put
	 *       msleep(50) before changing SCROFF
	 */
	mga_wait_vsync(mdev);
	mga_wait_busy(mdev);

	RREG_ECRT(0x01, crtcext1);
	crtcext1 |= MGAREG_CRTCEXT1_VSYNCOFF |
		    MGAREG_CRTCEXT1_HSYNCOFF;
	WREG_ECRT(0x01, crtcext1);
}

static void mgag200_handle_damage(struct mga_device *mdev, const struct iosys_map *vmap,
				  struct drm_framebuffer *fb, struct drm_rect *clip)
{
	struct iosys_map dst = IOSYS_MAP_INIT_VADDR_IOMEM(mdev->vram);

	iosys_map_incr(&dst, drm_fb_clip_offset(fb->pitches[0], fb->format, clip));
	drm_fb_memcpy(&dst, fb->pitches, vmap, fb, clip);
}

/*
 * Primary plane
 */

const uint32_t mgag200_primary_plane_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
};

const size_t mgag200_primary_plane_formats_size = ARRAY_SIZE(mgag200_primary_plane_formats);

const uint64_t mgag200_primary_plane_fmtmods[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

int mgag200_primary_plane_helper_atomic_check(struct drm_plane *plane,
					      struct drm_atomic_state *new_state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(new_state, plane);
	struct drm_framebuffer *new_fb = new_plane_state->fb;
	struct drm_framebuffer *fb = NULL;
	struct drm_crtc *new_crtc = new_plane_state->crtc;
	struct drm_crtc_state *new_crtc_state = NULL;
	struct mgag200_crtc_state *new_mgag200_crtc_state;
	int ret;

	if (new_crtc)
		new_crtc_state = drm_atomic_get_new_crtc_state(new_state, new_crtc);

	ret = drm_atomic_helper_check_plane_state(new_plane_state, new_crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  false, true);
	if (ret)
		return ret;
	else if (!new_plane_state->visible)
		return 0;

	if (plane->state)
		fb = plane->state->fb;

	if (!fb || (fb->format != new_fb->format))
		new_crtc_state->mode_changed = true; /* update PLL settings */

	new_mgag200_crtc_state = to_mgag200_crtc_state(new_crtc_state);
	new_mgag200_crtc_state->format = new_fb->format;

	return 0;
}

void mgag200_primary_plane_helper_atomic_update(struct drm_plane *plane,
						struct drm_atomic_state *old_state)
{
	struct drm_device *dev = plane->dev;
	struct mga_device *mdev = to_mga_device(dev);
	struct drm_plane_state *plane_state = plane->state;
	struct drm_plane_state *old_plane_state = drm_atomic_get_old_plane_state(old_state, plane);
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_rect damage;

	drm_atomic_helper_damage_iter_init(&iter, old_plane_state, plane_state);
	drm_atomic_for_each_plane_damage(&iter, &damage) {
		mgag200_handle_damage(mdev, shadow_plane_state->data, fb, &damage);
	}

	/* Always scanout image at VRAM offset 0 */
	mgag200_set_startadd(mdev, (u32)0);
	mgag200_set_offset(mdev, fb);
}

void mgag200_primary_plane_helper_atomic_enable(struct drm_plane *plane,
						struct drm_atomic_state *state)
{
	struct drm_device *dev = plane->dev;
	struct mga_device *mdev = to_mga_device(dev);
	u8 seq1;

	RREG_SEQ(0x01, seq1);
	seq1 &= ~MGAREG_SEQ1_SCROFF;
	WREG_SEQ(0x01, seq1);
	msleep(20);
}

void mgag200_primary_plane_helper_atomic_disable(struct drm_plane *plane,
						 struct drm_atomic_state *old_state)
{
	struct drm_device *dev = plane->dev;
	struct mga_device *mdev = to_mga_device(dev);
	u8 seq1;

	RREG_SEQ(0x01, seq1);
	seq1 |= MGAREG_SEQ1_SCROFF;
	WREG_SEQ(0x01, seq1);
	msleep(20);
}

/*
 * CRTC
 */

enum drm_mode_status mgag200_crtc_helper_mode_valid(struct drm_crtc *crtc,
						    const struct drm_display_mode *mode)
{
	struct mga_device *mdev = to_mga_device(crtc->dev);
	const struct mgag200_device_info *info = mdev->info;

	/*
	 * Some devices have additional limits on the size of the
	 * display mode.
	 */
	if (mode->hdisplay > info->max_hdisplay)
		return MODE_VIRTUAL_X;
	if (mode->vdisplay > info->max_vdisplay)
		return MODE_VIRTUAL_Y;

	if ((mode->hdisplay % 8) != 0 || (mode->hsync_start % 8) != 0 ||
	    (mode->hsync_end % 8) != 0 || (mode->htotal % 8) != 0) {
		return MODE_H_ILLEGAL;
	}

	if (mode->crtc_hdisplay > 2048 || mode->crtc_hsync_start > 4096 ||
	    mode->crtc_hsync_end > 4096 || mode->crtc_htotal > 4096 ||
	    mode->crtc_vdisplay > 2048 || mode->crtc_vsync_start > 4096 ||
	    mode->crtc_vsync_end > 4096 || mode->crtc_vtotal > 4096) {
		return MODE_BAD;
	}

	return MODE_OK;
}

int mgag200_crtc_helper_atomic_check(struct drm_crtc *crtc, struct drm_atomic_state *new_state)
{
	struct drm_device *dev = crtc->dev;
	struct mga_device *mdev = to_mga_device(dev);
	const struct mgag200_device_funcs *funcs = mdev->funcs;
	struct drm_crtc_state *new_crtc_state = drm_atomic_get_new_crtc_state(new_state, crtc);
	struct drm_property_blob *new_gamma_lut = new_crtc_state->gamma_lut;
	int ret;

	if (!new_crtc_state->enable)
		return 0;

	ret = drm_atomic_helper_check_crtc_primary_plane(new_crtc_state);
	if (ret)
		return ret;

	if (new_crtc_state->mode_changed) {
		if (funcs->pixpllc_atomic_check) {
			ret = funcs->pixpllc_atomic_check(crtc, new_state);
			if (ret)
				return ret;
		}
	}

	if (new_crtc_state->color_mgmt_changed && new_gamma_lut) {
		if (new_gamma_lut->length != MGAG200_LUT_SIZE * sizeof(struct drm_color_lut)) {
			drm_dbg(dev, "Wrong size for gamma_lut %zu\n", new_gamma_lut->length);
			return -EINVAL;
		}
	}

	return 0;
}

void mgag200_crtc_helper_atomic_flush(struct drm_crtc *crtc, struct drm_atomic_state *old_state)
{
	struct drm_crtc_state *crtc_state = crtc->state;
	struct mgag200_crtc_state *mgag200_crtc_state = to_mgag200_crtc_state(crtc_state);
	struct drm_device *dev = crtc->dev;
	struct mga_device *mdev = to_mga_device(dev);

	if (crtc_state->enable && crtc_state->color_mgmt_changed) {
		const struct drm_format_info *format = mgag200_crtc_state->format;

		if (crtc_state->gamma_lut)
			mgag200_crtc_set_gamma(mdev, format, crtc_state->gamma_lut->data);
		else
			mgag200_crtc_set_gamma_linear(mdev, format);
	}
}

void mgag200_crtc_helper_atomic_enable(struct drm_crtc *crtc, struct drm_atomic_state *old_state)
{
	struct drm_device *dev = crtc->dev;
	struct mga_device *mdev = to_mga_device(dev);
	const struct mgag200_device_funcs *funcs = mdev->funcs;
	struct drm_crtc_state *crtc_state = crtc->state;
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	struct mgag200_crtc_state *mgag200_crtc_state = to_mgag200_crtc_state(crtc_state);
	const struct drm_format_info *format = mgag200_crtc_state->format;

	if (funcs->disable_vidrst)
		funcs->disable_vidrst(mdev);

	mgag200_set_format_regs(mdev, format);
	mgag200_set_mode_regs(mdev, adjusted_mode);

	if (funcs->pixpllc_atomic_update)
		funcs->pixpllc_atomic_update(crtc, old_state);

	mgag200_enable_display(mdev);

	if (funcs->enable_vidrst)
		funcs->enable_vidrst(mdev);
}

void mgag200_crtc_helper_atomic_disable(struct drm_crtc *crtc, struct drm_atomic_state *old_state)
{
	struct mga_device *mdev = to_mga_device(crtc->dev);
	const struct mgag200_device_funcs *funcs = mdev->funcs;

	if (funcs->disable_vidrst)
		funcs->disable_vidrst(mdev);

	mgag200_disable_display(mdev);

	if (funcs->enable_vidrst)
		funcs->enable_vidrst(mdev);
}

void mgag200_crtc_reset(struct drm_crtc *crtc)
{
	struct mgag200_crtc_state *mgag200_crtc_state;

	if (crtc->state)
		crtc->funcs->atomic_destroy_state(crtc, crtc->state);

	mgag200_crtc_state = kzalloc(sizeof(*mgag200_crtc_state), GFP_KERNEL);
	if (mgag200_crtc_state)
		__drm_atomic_helper_crtc_reset(crtc, &mgag200_crtc_state->base);
	else
		__drm_atomic_helper_crtc_reset(crtc, NULL);
}

struct drm_crtc_state *mgag200_crtc_atomic_duplicate_state(struct drm_crtc *crtc)
{
	struct drm_crtc_state *crtc_state = crtc->state;
	struct mgag200_crtc_state *mgag200_crtc_state = to_mgag200_crtc_state(crtc_state);
	struct mgag200_crtc_state *new_mgag200_crtc_state;

	if (!crtc_state)
		return NULL;

	new_mgag200_crtc_state = kzalloc(sizeof(*new_mgag200_crtc_state), GFP_KERNEL);
	if (!new_mgag200_crtc_state)
		return NULL;
	__drm_atomic_helper_crtc_duplicate_state(crtc, &new_mgag200_crtc_state->base);

	new_mgag200_crtc_state->format = mgag200_crtc_state->format;
	memcpy(&new_mgag200_crtc_state->pixpllc, &mgag200_crtc_state->pixpllc,
	       sizeof(new_mgag200_crtc_state->pixpllc));

	return &new_mgag200_crtc_state->base;
}

void mgag200_crtc_atomic_destroy_state(struct drm_crtc *crtc, struct drm_crtc_state *crtc_state)
{
	struct mgag200_crtc_state *mgag200_crtc_state = to_mgag200_crtc_state(crtc_state);

	__drm_atomic_helper_crtc_destroy_state(&mgag200_crtc_state->base);
	kfree(mgag200_crtc_state);
}

/*
 * Connector
 */

int mgag200_vga_connector_helper_get_modes(struct drm_connector *connector)
{
	struct mga_device *mdev = to_mga_device(connector->dev);
	int ret;

	/*
	 * Protect access to I/O registers from concurrent modesetting
	 * by acquiring the I/O-register lock.
	 */
	mutex_lock(&mdev->rmmio_lock);
	ret = drm_connector_helper_get_modes_from_ddc(connector);
	mutex_unlock(&mdev->rmmio_lock);

	return ret;
}

/*
 * Mode config
 */

static void mgag200_mode_config_helper_atomic_commit_tail(struct drm_atomic_state *state)
{
	struct mga_device *mdev = to_mga_device(state->dev);

	/*
	 * Concurrent operations could possibly trigger a call to
	 * drm_connector_helper_funcs.get_modes by trying to read the
	 * display modes. Protect access to I/O registers by acquiring
	 * the I/O-register lock.
	 */
	mutex_lock(&mdev->rmmio_lock);
	drm_atomic_helper_commit_tail(state);
	mutex_unlock(&mdev->rmmio_lock);
}

static const struct drm_mode_config_helper_funcs mgag200_mode_config_helper_funcs = {
	.atomic_commit_tail = mgag200_mode_config_helper_atomic_commit_tail,
};

/* Calculates a mode's required memory bandwidth (in KiB/sec). */
static uint32_t mgag200_calculate_mode_bandwidth(const struct drm_display_mode *mode,
						 unsigned int bits_per_pixel)
{
	uint32_t total_area, divisor;
	uint64_t active_area, pixels_per_second, bandwidth;
	uint64_t bytes_per_pixel = (bits_per_pixel + 7) / 8;

	divisor = 1024;

	if (!mode->htotal || !mode->vtotal || !mode->clock)
		return 0;

	active_area = mode->hdisplay * mode->vdisplay;
	total_area = mode->htotal * mode->vtotal;

	pixels_per_second = active_area * mode->clock * 1000;
	do_div(pixels_per_second, total_area);

	bandwidth = pixels_per_second * bytes_per_pixel * 100;
	do_div(bandwidth, divisor);

	return (uint32_t)bandwidth;
}

static enum drm_mode_status mgag200_mode_config_mode_valid(struct drm_device *dev,
							   const struct drm_display_mode *mode)
{
	static const unsigned int max_bpp = 4; // DRM_FORMAT_XRGB8888
	struct mga_device *mdev = to_mga_device(dev);
	unsigned long fbsize, fbpages, max_fbpages;
	const struct mgag200_device_info *info = mdev->info;

	max_fbpages = mdev->vram_available >> PAGE_SHIFT;

	fbsize = mode->hdisplay * mode->vdisplay * max_bpp;
	fbpages = DIV_ROUND_UP(fbsize, PAGE_SIZE);

	if (fbpages > max_fbpages)
		return MODE_MEM;

	/*
	 * Test the mode's required memory bandwidth if the device
	 * specifies a maximum. Not all devices do though.
	 */
	if (info->max_mem_bandwidth) {
		uint32_t mode_bandwidth = mgag200_calculate_mode_bandwidth(mode, max_bpp * 8);

		if (mode_bandwidth > (info->max_mem_bandwidth * 1024))
			return MODE_BAD;
	}

	return MODE_OK;
}

static const struct drm_mode_config_funcs mgag200_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.mode_valid = mgag200_mode_config_mode_valid,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

int mgag200_mode_config_init(struct mga_device *mdev, resource_size_t vram_available)
{
	struct drm_device *dev = &mdev->base;
	int ret;

	mdev->vram_available = vram_available;

	ret = drmm_mode_config_init(dev);
	if (ret) {
		drm_err(dev, "drmm_mode_config_init() failed: %d\n", ret);
		return ret;
	}

	dev->mode_config.max_width = MGAG200_MAX_FB_WIDTH;
	dev->mode_config.max_height = MGAG200_MAX_FB_HEIGHT;
	dev->mode_config.preferred_depth = 24;
	dev->mode_config.funcs = &mgag200_mode_config_funcs;
	dev->mode_config.helper_private = &mgag200_mode_config_helper_funcs;

	return 0;
}
