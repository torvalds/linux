
/*
 * Copyright 2012 Red Hat
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License version 2. See the file COPYING in the main
 * directory of this archive for more details.
 *
 * Authors: Matthew Garrett
 *          Dave Airlie
 *
 * Portions of this code derived from cirrusfb.c:
 * drivers/video/cirrusfb.c - driver for Cirrus Logic chipsets
 *
 * Copyright 1999-2001 Jeff Garzik <jgarzik@pobox.com>
 */
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>

#include <video/cirrus.h>

#include "cirrus_drv.h"

#define CIRRUS_LUT_SIZE 256

#define PALETTE_INDEX 0x8
#define PALETTE_DATA 0x9

/*
 * This file contains setup code for the CRTC.
 */

static void cirrus_crtc_load_lut(struct drm_crtc *crtc)
{
	struct cirrus_crtc *cirrus_crtc = to_cirrus_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct cirrus_device *cdev = dev->dev_private;
	int i;

	if (!crtc->enabled)
		return;

	for (i = 0; i < CIRRUS_LUT_SIZE; i++) {
		/* VGA registers */
		WREG8(PALETTE_INDEX, i);
		WREG8(PALETTE_DATA, cirrus_crtc->lut_r[i]);
		WREG8(PALETTE_DATA, cirrus_crtc->lut_g[i]);
		WREG8(PALETTE_DATA, cirrus_crtc->lut_b[i]);
	}
}

/*
 * The DRM core requires DPMS functions, but they make little sense in our
 * case and so are just stubs
 */

static void cirrus_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_device *dev = crtc->dev;
	struct cirrus_device *cdev = dev->dev_private;
	u8 sr01, gr0e;

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		sr01 = 0x00;
		gr0e = 0x00;
		break;
	case DRM_MODE_DPMS_STANDBY:
		sr01 = 0x20;
		gr0e = 0x02;
		break;
	case DRM_MODE_DPMS_SUSPEND:
		sr01 = 0x20;
		gr0e = 0x04;
		break;
	case DRM_MODE_DPMS_OFF:
		sr01 = 0x20;
		gr0e = 0x06;
		break;
	default:
		return;
	}

	WREG8(SEQ_INDEX, 0x1);
	sr01 |= RREG8(SEQ_DATA) & ~0x20;
	WREG_SEQ(0x1, sr01);

	WREG8(GFX_INDEX, 0xe);
	gr0e |= RREG8(GFX_DATA) & ~0x06;
	WREG_GFX(0xe, gr0e);
}

static void cirrus_set_start_address(struct drm_crtc *crtc, unsigned offset)
{
	struct cirrus_device *cdev = crtc->dev->dev_private;
	u32 addr;
	u8 tmp;

	addr = offset >> 2;
	WREG_CRT(0x0c, (u8)((addr >> 8) & 0xff));
	WREG_CRT(0x0d, (u8)(addr & 0xff));

	WREG8(CRT_INDEX, 0x1b);
	tmp = RREG8(CRT_DATA);
	tmp &= 0xf2;
	tmp |= (addr >> 16) & 0x01;
	tmp |= (addr >> 15) & 0x0c;
	WREG_CRT(0x1b, tmp);
	WREG8(CRT_INDEX, 0x1d);
	tmp = RREG8(CRT_DATA);
	tmp &= 0x7f;
	tmp |= (addr >> 12) & 0x80;
	WREG_CRT(0x1d, tmp);
}

/* cirrus is different - we will force move buffers out of VRAM */
static int cirrus_crtc_do_set_base(struct drm_crtc *crtc,
				struct drm_framebuffer *fb,
				int x, int y, int atomic)
{
	struct cirrus_device *cdev = crtc->dev->dev_private;
	struct drm_gem_object *obj;
	struct cirrus_framebuffer *cirrus_fb;
	struct cirrus_bo *bo;
	int ret;
	u64 gpu_addr;

	/* push the previous fb to system ram */
	if (!atomic && fb) {
		cirrus_fb = to_cirrus_framebuffer(fb);
		obj = cirrus_fb->obj;
		bo = gem_to_cirrus_bo(obj);
		ret = cirrus_bo_reserve(bo, false);
		if (ret)
			return ret;
		cirrus_bo_push_sysram(bo);
		cirrus_bo_unreserve(bo);
	}

	cirrus_fb = to_cirrus_framebuffer(crtc->primary->fb);
	obj = cirrus_fb->obj;
	bo = gem_to_cirrus_bo(obj);

	ret = cirrus_bo_reserve(bo, false);
	if (ret)
		return ret;

	ret = cirrus_bo_pin(bo, TTM_PL_FLAG_VRAM, &gpu_addr);
	if (ret) {
		cirrus_bo_unreserve(bo);
		return ret;
	}

	if (&cdev->mode_info.gfbdev->gfb == cirrus_fb) {
		/* if pushing console in kmap it */
		ret = ttm_bo_kmap(&bo->bo, 0, bo->bo.num_pages, &bo->kmap);
		if (ret)
			DRM_ERROR("failed to kmap fbcon\n");
	}
	cirrus_bo_unreserve(bo);

	cirrus_set_start_address(crtc, (u32)gpu_addr);
	return 0;
}

static int cirrus_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
			     struct drm_framebuffer *old_fb)
{
	return cirrus_crtc_do_set_base(crtc, old_fb, x, y, 0);
}

/*
 * The meat of this driver. The core passes us a mode and we have to program
 * it. The modesetting here is the bare minimum required to satisfy the qemu
 * emulation of this hardware, and running this against a real device is
 * likely to result in an inadequately programmed mode. We've already had
 * the opportunity to modify the mode, so whatever we receive here should
 * be something that can be correctly programmed and displayed
 */
static int cirrus_crtc_mode_set(struct drm_crtc *crtc,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode,
				int x, int y, struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct cirrus_device *cdev = dev->dev_private;
	int hsyncstart, hsyncend, htotal, hdispend;
	int vtotal, vdispend;
	int tmp;
	int sr07 = 0, hdr = 0;

	htotal = mode->htotal / 8;
	hsyncend = mode->hsync_end / 8;
	hsyncstart = mode->hsync_start / 8;
	hdispend = mode->hdisplay / 8;

	vtotal = mode->vtotal;
	vdispend = mode->vdisplay;

	vdispend -= 1;
	vtotal -= 2;

	htotal -= 5;
	hdispend -= 1;
	hsyncstart += 1;
	hsyncend += 1;

	WREG_CRT(VGA_CRTC_V_SYNC_END, 0x20);
	WREG_CRT(VGA_CRTC_H_TOTAL, htotal);
	WREG_CRT(VGA_CRTC_H_DISP, hdispend);
	WREG_CRT(VGA_CRTC_H_SYNC_START, hsyncstart);
	WREG_CRT(VGA_CRTC_H_SYNC_END, hsyncend);
	WREG_CRT(VGA_CRTC_V_TOTAL, vtotal & 0xff);
	WREG_CRT(VGA_CRTC_V_DISP_END, vdispend & 0xff);

	tmp = 0x40;
	if ((vdispend + 1) & 512)
		tmp |= 0x20;
	WREG_CRT(VGA_CRTC_MAX_SCAN, tmp);

	/*
	 * Overflow bits for values that don't fit in the standard registers
	 */
	tmp = 16;
	if (vtotal & 256)
		tmp |= 1;
	if (vdispend & 256)
		tmp |= 2;
	if ((vdispend + 1) & 256)
		tmp |= 8;
	if (vtotal & 512)
		tmp |= 32;
	if (vdispend & 512)
		tmp |= 64;
	WREG_CRT(VGA_CRTC_OVERFLOW, tmp);

	tmp = 0;

	/* More overflow bits */

	if ((htotal + 5) & 64)
		tmp |= 16;
	if ((htotal + 5) & 128)
		tmp |= 32;
	if (vtotal & 256)
		tmp |= 64;
	if (vtotal & 512)
		tmp |= 128;

	WREG_CRT(CL_CRT1A, tmp);

	/* Disable Hercules/CGA compatibility */
	WREG_CRT(VGA_CRTC_MODE, 0x03);

	WREG8(SEQ_INDEX, 0x7);
	sr07 = RREG8(SEQ_DATA);
	sr07 &= 0xe0;
	hdr = 0;
	switch (crtc->primary->fb->bits_per_pixel) {
	case 8:
		sr07 |= 0x11;
		break;
	case 16:
		sr07 |= 0x17;
		hdr = 0xc1;
		break;
	case 24:
		sr07 |= 0x15;
		hdr = 0xc5;
		break;
	case 32:
		sr07 |= 0x19;
		hdr = 0xc5;
		break;
	default:
		return -1;
	}

	WREG_SEQ(0x7, sr07);

	/* Program the pitch */
	tmp = crtc->primary->fb->pitches[0] / 8;
	WREG_CRT(VGA_CRTC_OFFSET, tmp);

	/* Enable extended blanking and pitch bits, and enable full memory */
	tmp = 0x22;
	tmp |= (crtc->primary->fb->pitches[0] >> 7) & 0x10;
	tmp |= (crtc->primary->fb->pitches[0] >> 6) & 0x40;
	WREG_CRT(0x1b, tmp);

	/* Enable high-colour modes */
	WREG_GFX(VGA_GFX_MODE, 0x40);

	/* And set graphics mode */
	WREG_GFX(VGA_GFX_MISC, 0x01);

	WREG_HDR(hdr);
	cirrus_crtc_do_set_base(crtc, old_fb, x, y, 0);

	/* Unblank (needed on S3 resume, vgabios doesn't do it then) */
	outb(0x20, 0x3c0);
	return 0;
}

/*
 * This is called before a mode is programmed. A typical use might be to
 * enable DPMS during the programming to avoid seeing intermediate stages,
 * but that's not relevant to us
 */
static void cirrus_crtc_prepare(struct drm_crtc *crtc)
{
}

/*
 * This is called after a mode is programmed. It should reverse anything done
 * by the prepare function
 */
static void cirrus_crtc_commit(struct drm_crtc *crtc)
{
}

/*
 * The core can pass us a set of gamma values to program. We actually only
 * use this for 8-bit mode so can't perform smooth fades on deeper modes,
 * but it's a requirement that we provide the function
 */
static int cirrus_crtc_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green,
				 u16 *blue, uint32_t size)
{
	struct cirrus_crtc *cirrus_crtc = to_cirrus_crtc(crtc);
	int i;

	for (i = 0; i < size; i++) {
		cirrus_crtc->lut_r[i] = red[i];
		cirrus_crtc->lut_g[i] = green[i];
		cirrus_crtc->lut_b[i] = blue[i];
	}
	cirrus_crtc_load_lut(crtc);

	return 0;
}

/* Simple cleanup function */
static void cirrus_crtc_destroy(struct drm_crtc *crtc)
{
	struct cirrus_crtc *cirrus_crtc = to_cirrus_crtc(crtc);

	drm_crtc_cleanup(crtc);
	kfree(cirrus_crtc);
}

/* These provide the minimum set of functions required to handle a CRTC */
static const struct drm_crtc_funcs cirrus_crtc_funcs = {
	.gamma_set = cirrus_crtc_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = cirrus_crtc_destroy,
};

static const struct drm_crtc_helper_funcs cirrus_helper_funcs = {
	.dpms = cirrus_crtc_dpms,
	.mode_set = cirrus_crtc_mode_set,
	.mode_set_base = cirrus_crtc_mode_set_base,
	.prepare = cirrus_crtc_prepare,
	.commit = cirrus_crtc_commit,
	.load_lut = cirrus_crtc_load_lut,
};

/* CRTC setup */
static void cirrus_crtc_init(struct drm_device *dev)
{
	struct cirrus_device *cdev = dev->dev_private;
	struct cirrus_crtc *cirrus_crtc;
	int i;

	cirrus_crtc = kzalloc(sizeof(struct cirrus_crtc) +
			      (CIRRUSFB_CONN_LIMIT * sizeof(struct drm_connector *)),
			      GFP_KERNEL);

	if (cirrus_crtc == NULL)
		return;

	drm_crtc_init(dev, &cirrus_crtc->base, &cirrus_crtc_funcs);

	drm_mode_crtc_set_gamma_size(&cirrus_crtc->base, CIRRUS_LUT_SIZE);
	cdev->mode_info.crtc = cirrus_crtc;

	for (i = 0; i < CIRRUS_LUT_SIZE; i++) {
		cirrus_crtc->lut_r[i] = i;
		cirrus_crtc->lut_g[i] = i;
		cirrus_crtc->lut_b[i] = i;
	}

	drm_crtc_helper_add(&cirrus_crtc->base, &cirrus_helper_funcs);
}

/** Sets the color ramps on behalf of fbcon */
void cirrus_crtc_fb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
			      u16 blue, int regno)
{
	struct cirrus_crtc *cirrus_crtc = to_cirrus_crtc(crtc);

	cirrus_crtc->lut_r[regno] = red;
	cirrus_crtc->lut_g[regno] = green;
	cirrus_crtc->lut_b[regno] = blue;
}

/** Gets the color ramps on behalf of fbcon */
void cirrus_crtc_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
			      u16 *blue, int regno)
{
	struct cirrus_crtc *cirrus_crtc = to_cirrus_crtc(crtc);

	*red = cirrus_crtc->lut_r[regno];
	*green = cirrus_crtc->lut_g[regno];
	*blue = cirrus_crtc->lut_b[regno];
}

static void cirrus_encoder_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
}

static void cirrus_encoder_dpms(struct drm_encoder *encoder, int state)
{
	return;
}

static void cirrus_encoder_prepare(struct drm_encoder *encoder)
{
}

static void cirrus_encoder_commit(struct drm_encoder *encoder)
{
}

static void cirrus_encoder_destroy(struct drm_encoder *encoder)
{
	struct cirrus_encoder *cirrus_encoder = to_cirrus_encoder(encoder);
	drm_encoder_cleanup(encoder);
	kfree(cirrus_encoder);
}

static const struct drm_encoder_helper_funcs cirrus_encoder_helper_funcs = {
	.dpms = cirrus_encoder_dpms,
	.mode_set = cirrus_encoder_mode_set,
	.prepare = cirrus_encoder_prepare,
	.commit = cirrus_encoder_commit,
};

static const struct drm_encoder_funcs cirrus_encoder_encoder_funcs = {
	.destroy = cirrus_encoder_destroy,
};

static struct drm_encoder *cirrus_encoder_init(struct drm_device *dev)
{
	struct drm_encoder *encoder;
	struct cirrus_encoder *cirrus_encoder;

	cirrus_encoder = kzalloc(sizeof(struct cirrus_encoder), GFP_KERNEL);
	if (!cirrus_encoder)
		return NULL;

	encoder = &cirrus_encoder->base;
	encoder->possible_crtcs = 0x1;

	drm_encoder_init(dev, encoder, &cirrus_encoder_encoder_funcs,
			 DRM_MODE_ENCODER_DAC, NULL);
	drm_encoder_helper_add(encoder, &cirrus_encoder_helper_funcs);

	return encoder;
}


static int cirrus_vga_get_modes(struct drm_connector *connector)
{
	int count;

	/* Just add a static list of modes */
	if (cirrus_bpp <= 24) {
		count = drm_add_modes_noedid(connector, 1280, 1024);
		drm_set_preferred_mode(connector, 1024, 768);
	} else {
		count = drm_add_modes_noedid(connector, 800, 600);
		drm_set_preferred_mode(connector, 800, 600);
	}
	return count;
}

static struct drm_encoder *cirrus_connector_best_encoder(struct drm_connector
						  *connector)
{
	int enc_id = connector->encoder_ids[0];
	/* pick the encoder ids */
	if (enc_id)
		return drm_encoder_find(connector->dev, enc_id);
	return NULL;
}

static enum drm_connector_status cirrus_vga_detect(struct drm_connector
						   *connector, bool force)
{
	return connector_status_connected;
}

static void cirrus_connector_destroy(struct drm_connector *connector)
{
	drm_connector_cleanup(connector);
	kfree(connector);
}

static const struct drm_connector_helper_funcs cirrus_vga_connector_helper_funcs = {
	.get_modes = cirrus_vga_get_modes,
	.best_encoder = cirrus_connector_best_encoder,
};

static const struct drm_connector_funcs cirrus_vga_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = cirrus_vga_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = cirrus_connector_destroy,
};

static struct drm_connector *cirrus_vga_init(struct drm_device *dev)
{
	struct drm_connector *connector;
	struct cirrus_connector *cirrus_connector;

	cirrus_connector = kzalloc(sizeof(struct cirrus_connector), GFP_KERNEL);
	if (!cirrus_connector)
		return NULL;

	connector = &cirrus_connector->base;

	drm_connector_init(dev, connector,
			   &cirrus_vga_connector_funcs, DRM_MODE_CONNECTOR_VGA);

	drm_connector_helper_add(connector, &cirrus_vga_connector_helper_funcs);

	drm_connector_register(connector);
	return connector;
}


int cirrus_modeset_init(struct cirrus_device *cdev)
{
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	int ret;

	drm_mode_config_init(cdev->dev);
	cdev->mode_info.mode_config_initialized = true;

	cdev->dev->mode_config.max_width = CIRRUS_MAX_FB_WIDTH;
	cdev->dev->mode_config.max_height = CIRRUS_MAX_FB_HEIGHT;

	cdev->dev->mode_config.fb_base = cdev->mc.vram_base;
	cdev->dev->mode_config.preferred_depth = 24;
	/* don't prefer a shadow on virt GPU */
	cdev->dev->mode_config.prefer_shadow = 0;

	cirrus_crtc_init(cdev->dev);

	encoder = cirrus_encoder_init(cdev->dev);
	if (!encoder) {
		DRM_ERROR("cirrus_encoder_init failed\n");
		return -1;
	}

	connector = cirrus_vga_init(cdev->dev);
	if (!connector) {
		DRM_ERROR("cirrus_vga_init failed\n");
		return -1;
	}

	drm_mode_connector_attach_encoder(connector, encoder);

	ret = cirrus_fbdev_init(cdev);
	if (ret) {
		DRM_ERROR("cirrus_fbdev_init failed\n");
		return ret;
	}

	return 0;
}

void cirrus_modeset_fini(struct cirrus_device *cdev)
{
	cirrus_fbdev_fini(cdev);

	if (cdev->mode_info.mode_config_initialized) {
		drm_mode_config_cleanup(cdev->dev);
		cdev->mode_info.mode_config_initialized = false;
	}
}
