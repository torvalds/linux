/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2012-2019 Red Hat
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License version 2. See the file COPYING in the main
 * directory of this archive for more details.
 *
 * Authors: Matthew Garrett
 *	    Dave Airlie
 *	    Gerd Hoffmann
 *
 * Portions of this code derived from cirrusfb.c:
 * drivers/video/cirrusfb.c - driver for Cirrus Logic chipsets
 *
 * Copyright 1999-2001 Jeff Garzik <jgarzik@pobox.com>
 */

#include <linux/aperture.h>
#include <linux/iosys-map.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <video/cirrus.h>
#include <video/vga.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_fbdev_shmem.h>
#include <drm/drm_file.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_module.h>
#include <drm/drm_probe_helper.h>

#define DRIVER_NAME "cirrus-qemu"
#define DRIVER_DESC "qemu cirrus vga"
#define DRIVER_DATE "2019"
#define DRIVER_MAJOR 2
#define DRIVER_MINOR 0

#define CIRRUS_MAX_PITCH (0x1FF << 3)      /* (4096 - 1) & ~111b bytes */
#define CIRRUS_VRAM_SIZE (4 * 1024 * 1024) /* 4 MB */

struct cirrus_device {
	struct drm_device	       dev;

	/* modesetting pipeline */
	struct drm_plane	       primary_plane;
	struct drm_crtc		       crtc;
	struct drm_encoder	       encoder;
	struct drm_connector	       connector;

	/* HW resources */
	void __iomem		       *vram;
	void __iomem		       *mmio;
};

#define to_cirrus(_dev) container_of(_dev, struct cirrus_device, dev)

struct cirrus_primary_plane_state {
	struct drm_shadow_plane_state base;

	/* HW scanout buffer */
	const struct drm_format_info   *format;
	unsigned int		       pitch;
};

static inline struct cirrus_primary_plane_state *
to_cirrus_primary_plane_state(struct drm_plane_state *plane_state)
{
	return container_of(plane_state, struct cirrus_primary_plane_state, base.base);
};

/* ------------------------------------------------------------------ */
/*
 * The meat of this driver. The core passes us a mode and we have to program
 * it. The modesetting here is the bare minimum required to satisfy the qemu
 * emulation of this hardware, and running this against a real device is
 * likely to result in an inadequately programmed mode. We've already had
 * the opportunity to modify the mode, so whatever we receive here should
 * be something that can be correctly programmed and displayed
 */

#define SEQ_INDEX 4
#define SEQ_DATA 5

static u8 rreg_seq(struct cirrus_device *cirrus, u8 reg)
{
	iowrite8(reg, cirrus->mmio + SEQ_INDEX);
	return ioread8(cirrus->mmio + SEQ_DATA);
}

static void wreg_seq(struct cirrus_device *cirrus, u8 reg, u8 val)
{
	iowrite8(reg, cirrus->mmio + SEQ_INDEX);
	iowrite8(val, cirrus->mmio + SEQ_DATA);
}

#define CRT_INDEX 0x14
#define CRT_DATA 0x15

static u8 rreg_crt(struct cirrus_device *cirrus, u8 reg)
{
	iowrite8(reg, cirrus->mmio + CRT_INDEX);
	return ioread8(cirrus->mmio + CRT_DATA);
}

static void wreg_crt(struct cirrus_device *cirrus, u8 reg, u8 val)
{
	iowrite8(reg, cirrus->mmio + CRT_INDEX);
	iowrite8(val, cirrus->mmio + CRT_DATA);
}

#define GFX_INDEX 0xe
#define GFX_DATA 0xf

static void wreg_gfx(struct cirrus_device *cirrus, u8 reg, u8 val)
{
	iowrite8(reg, cirrus->mmio + GFX_INDEX);
	iowrite8(val, cirrus->mmio + GFX_DATA);
}

#define VGA_DAC_MASK  0x06

static void wreg_hdr(struct cirrus_device *cirrus, u8 val)
{
	ioread8(cirrus->mmio + VGA_DAC_MASK);
	ioread8(cirrus->mmio + VGA_DAC_MASK);
	ioread8(cirrus->mmio + VGA_DAC_MASK);
	ioread8(cirrus->mmio + VGA_DAC_MASK);
	iowrite8(val, cirrus->mmio + VGA_DAC_MASK);
}

static const struct drm_format_info *cirrus_convert_to(struct drm_framebuffer *fb)
{
	if (fb->format->format == DRM_FORMAT_XRGB8888 && fb->pitches[0] > CIRRUS_MAX_PITCH) {
		if (fb->width * 3 <= CIRRUS_MAX_PITCH)
			/* convert from XR24 to RG24 */
			return drm_format_info(DRM_FORMAT_RGB888);
		else
			/* convert from XR24 to RG16 */
			return drm_format_info(DRM_FORMAT_RGB565);
	}
	return NULL;
}

static const struct drm_format_info *cirrus_format(struct drm_framebuffer *fb)
{
	const struct drm_format_info *format = cirrus_convert_to(fb);

	if (format)
		return format;
	return fb->format;
}

static int cirrus_pitch(struct drm_framebuffer *fb)
{
	const struct drm_format_info *format = cirrus_convert_to(fb);

	if (format)
		return drm_format_info_min_pitch(format, 0, fb->width);
	return fb->pitches[0];
}

static void cirrus_set_start_address(struct cirrus_device *cirrus, u32 offset)
{
	u32 addr;
	u8 tmp;

	addr = offset >> 2;
	wreg_crt(cirrus, 0x0c, (u8)((addr >> 8) & 0xff));
	wreg_crt(cirrus, 0x0d, (u8)(addr & 0xff));

	tmp = rreg_crt(cirrus, 0x1b);
	tmp &= 0xf2;
	tmp |= (addr >> 16) & 0x01;
	tmp |= (addr >> 15) & 0x0c;
	wreg_crt(cirrus, 0x1b, tmp);

	tmp = rreg_crt(cirrus, 0x1d);
	tmp &= 0x7f;
	tmp |= (addr >> 12) & 0x80;
	wreg_crt(cirrus, 0x1d, tmp);
}

static void cirrus_mode_set(struct cirrus_device *cirrus,
			    struct drm_display_mode *mode)
{
	int hsyncstart, hsyncend, htotal, hdispend;
	int vtotal, vdispend;
	int tmp;

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

	wreg_crt(cirrus, VGA_CRTC_V_SYNC_END, 0x20);
	wreg_crt(cirrus, VGA_CRTC_H_TOTAL, htotal);
	wreg_crt(cirrus, VGA_CRTC_H_DISP, hdispend);
	wreg_crt(cirrus, VGA_CRTC_H_SYNC_START, hsyncstart);
	wreg_crt(cirrus, VGA_CRTC_H_SYNC_END, hsyncend);
	wreg_crt(cirrus, VGA_CRTC_V_TOTAL, vtotal & 0xff);
	wreg_crt(cirrus, VGA_CRTC_V_DISP_END, vdispend & 0xff);

	tmp = 0x40;
	if ((vdispend + 1) & 512)
		tmp |= 0x20;
	wreg_crt(cirrus, VGA_CRTC_MAX_SCAN, tmp);

	/*
	 * Overflow bits for values that don't fit in the standard registers
	 */
	tmp = 0x10;
	if (vtotal & 0x100)
		tmp |= 0x01;
	if (vdispend & 0x100)
		tmp |= 0x02;
	if ((vdispend + 1) & 0x100)
		tmp |= 0x08;
	if (vtotal & 0x200)
		tmp |= 0x20;
	if (vdispend & 0x200)
		tmp |= 0x40;
	wreg_crt(cirrus, VGA_CRTC_OVERFLOW, tmp);

	tmp = 0;

	/* More overflow bits */

	if ((htotal + 5) & 0x40)
		tmp |= 0x10;
	if ((htotal + 5) & 0x80)
		tmp |= 0x20;
	if (vtotal & 0x100)
		tmp |= 0x40;
	if (vtotal & 0x200)
		tmp |= 0x80;

	wreg_crt(cirrus, CL_CRT1A, tmp);

	/* Disable Hercules/CGA compatibility */
	wreg_crt(cirrus, VGA_CRTC_MODE, 0x03);
}

static void cirrus_format_set(struct cirrus_device *cirrus,
			      const struct drm_format_info *format)
{
	u8 sr07, hdr;

	sr07 = rreg_seq(cirrus, 0x07);
	sr07 &= 0xe0;

	switch (format->format) {
	case DRM_FORMAT_C8:
		sr07 |= 0x11;
		hdr = 0x00;
		break;
	case DRM_FORMAT_RGB565:
		sr07 |= 0x17;
		hdr = 0xc1;
		break;
	case DRM_FORMAT_RGB888:
		sr07 |= 0x15;
		hdr = 0xc5;
		break;
	case DRM_FORMAT_XRGB8888:
		sr07 |= 0x19;
		hdr = 0xc5;
		break;
	default:
		return;
	}

	wreg_seq(cirrus, 0x7, sr07);

	/* Enable high-colour modes */
	wreg_gfx(cirrus, VGA_GFX_MODE, 0x40);

	/* And set graphics mode */
	wreg_gfx(cirrus, VGA_GFX_MISC, 0x01);

	wreg_hdr(cirrus, hdr);
}

static void cirrus_pitch_set(struct cirrus_device *cirrus, unsigned int pitch)
{
	u8 cr13, cr1b;

	/* Program the pitch */
	cr13 = pitch / 8;
	wreg_crt(cirrus, VGA_CRTC_OFFSET, cr13);

	/* Enable extended blanking and pitch bits, and enable full memory */
	cr1b = 0x22;
	cr1b |= (pitch >> 7) & 0x10;
	cr1b |= (pitch >> 6) & 0x40;
	wreg_crt(cirrus, 0x1b, cr1b);

	cirrus_set_start_address(cirrus, 0);
}

/* ------------------------------------------------------------------ */
/* cirrus display pipe						      */

static const uint32_t cirrus_primary_plane_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_XRGB8888,
};

static const uint64_t cirrus_primary_plane_format_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

static int cirrus_primary_plane_helper_atomic_check(struct drm_plane *plane,
						    struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct cirrus_primary_plane_state *new_primary_plane_state =
		to_cirrus_primary_plane_state(new_plane_state);
	struct drm_framebuffer *fb = new_plane_state->fb;
	struct drm_crtc *new_crtc = new_plane_state->crtc;
	struct drm_crtc_state *new_crtc_state = NULL;
	int ret;
	unsigned int pitch;

	if (new_crtc)
		new_crtc_state = drm_atomic_get_new_crtc_state(state, new_crtc);

	ret = drm_atomic_helper_check_plane_state(new_plane_state, new_crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  false, false);
	if (ret)
		return ret;
	else if (!new_plane_state->visible)
		return 0;

	pitch = cirrus_pitch(fb);

	/* validate size constraints */
	if (pitch > CIRRUS_MAX_PITCH)
		return -EINVAL;
	else if (pitch * fb->height > CIRRUS_VRAM_SIZE)
		return -EINVAL;

	new_primary_plane_state->format = cirrus_format(fb);
	new_primary_plane_state->pitch = pitch;

	return 0;
}

static void cirrus_primary_plane_helper_atomic_update(struct drm_plane *plane,
						      struct drm_atomic_state *state)
{
	struct cirrus_device *cirrus = to_cirrus(plane->dev);
	struct drm_plane_state *plane_state = drm_atomic_get_new_plane_state(state, plane);
	struct cirrus_primary_plane_state *primary_plane_state =
		to_cirrus_primary_plane_state(plane_state);
	struct drm_shadow_plane_state *shadow_plane_state = to_drm_shadow_plane_state(plane_state);
	struct drm_framebuffer *fb = plane_state->fb;
	const struct drm_format_info *format = primary_plane_state->format;
	unsigned int pitch = primary_plane_state->pitch;
	struct drm_plane_state *old_plane_state = drm_atomic_get_old_plane_state(state, plane);
	struct cirrus_primary_plane_state *old_primary_plane_state =
		to_cirrus_primary_plane_state(old_plane_state);
	struct iosys_map vaddr = IOSYS_MAP_INIT_VADDR_IOMEM(cirrus->vram);
	struct drm_atomic_helper_damage_iter iter;
	struct drm_rect damage;
	int idx;

	if (!fb)
		return;

	if (!drm_dev_enter(&cirrus->dev, &idx))
		return;

	if (old_primary_plane_state->format != format)
		cirrus_format_set(cirrus, format);
	if (old_primary_plane_state->pitch != pitch)
		cirrus_pitch_set(cirrus, pitch);

	drm_atomic_helper_damage_iter_init(&iter, old_plane_state, plane_state);
	drm_atomic_for_each_plane_damage(&iter, &damage) {
		unsigned int offset = drm_fb_clip_offset(pitch, format, &damage);
		struct iosys_map dst = IOSYS_MAP_INIT_OFFSET(&vaddr, offset);

		drm_fb_blit(&dst, &pitch, format->format, shadow_plane_state->data, fb,
			    &damage, &shadow_plane_state->fmtcnv_state);
	}

	drm_dev_exit(idx);
}

static const struct drm_plane_helper_funcs cirrus_primary_plane_helper_funcs = {
	DRM_GEM_SHADOW_PLANE_HELPER_FUNCS,
	.atomic_check = cirrus_primary_plane_helper_atomic_check,
	.atomic_update = cirrus_primary_plane_helper_atomic_update,
};

static struct drm_plane_state *
cirrus_primary_plane_atomic_duplicate_state(struct drm_plane *plane)
{
	struct drm_plane_state *plane_state = plane->state;
	struct cirrus_primary_plane_state *primary_plane_state =
		to_cirrus_primary_plane_state(plane_state);
	struct cirrus_primary_plane_state *new_primary_plane_state;
	struct drm_shadow_plane_state *new_shadow_plane_state;

	if (!plane_state)
		return NULL;

	new_primary_plane_state = kzalloc(sizeof(*new_primary_plane_state), GFP_KERNEL);
	if (!new_primary_plane_state)
		return NULL;
	new_shadow_plane_state = &new_primary_plane_state->base;

	__drm_gem_duplicate_shadow_plane_state(plane, new_shadow_plane_state);
	new_primary_plane_state->format = primary_plane_state->format;
	new_primary_plane_state->pitch = primary_plane_state->pitch;

	return &new_shadow_plane_state->base;
}

static void cirrus_primary_plane_atomic_destroy_state(struct drm_plane *plane,
						      struct drm_plane_state *plane_state)
{
	struct cirrus_primary_plane_state *primary_plane_state =
		to_cirrus_primary_plane_state(plane_state);

	__drm_gem_destroy_shadow_plane_state(&primary_plane_state->base);
	kfree(primary_plane_state);
}

static void cirrus_reset_primary_plane(struct drm_plane *plane)
{
	struct cirrus_primary_plane_state *primary_plane_state;

	if (plane->state) {
		cirrus_primary_plane_atomic_destroy_state(plane, plane->state);
		plane->state = NULL; /* must be set to NULL here */
	}

	primary_plane_state = kzalloc(sizeof(*primary_plane_state), GFP_KERNEL);
	if (!primary_plane_state)
		return;
	__drm_gem_reset_shadow_plane(plane, &primary_plane_state->base);
}

static const struct drm_plane_funcs cirrus_primary_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = cirrus_reset_primary_plane,
	.atomic_duplicate_state = cirrus_primary_plane_atomic_duplicate_state,
	.atomic_destroy_state = cirrus_primary_plane_atomic_destroy_state,
};

static int cirrus_crtc_helper_atomic_check(struct drm_crtc *crtc, struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	int ret;

	if (!crtc_state->enable)
		return 0;

	ret = drm_atomic_helper_check_crtc_primary_plane(crtc_state);
	if (ret)
		return ret;

	return 0;
}

static void cirrus_crtc_helper_atomic_enable(struct drm_crtc *crtc,
					     struct drm_atomic_state *state)
{
	struct cirrus_device *cirrus = to_cirrus(crtc->dev);
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	int idx;

	if (!drm_dev_enter(&cirrus->dev, &idx))
		return;

	cirrus_mode_set(cirrus, &crtc_state->mode);

#ifdef CONFIG_HAS_IOPORT
	/* Unblank (needed on S3 resume, vgabios doesn't do it then) */
	outb(VGA_AR_ENABLE_DISPLAY, VGA_ATT_W);
#endif

	drm_dev_exit(idx);
}

static const struct drm_crtc_helper_funcs cirrus_crtc_helper_funcs = {
	.atomic_check = cirrus_crtc_helper_atomic_check,
	.atomic_enable = cirrus_crtc_helper_atomic_enable,
};

static const struct drm_crtc_funcs cirrus_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static const struct drm_encoder_funcs cirrus_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int cirrus_connector_helper_get_modes(struct drm_connector *connector)
{
	int count;

	count = drm_add_modes_noedid(connector,
				     connector->dev->mode_config.max_width,
				     connector->dev->mode_config.max_height);
	drm_set_preferred_mode(connector, 1024, 768);
	return count;
}

static const struct drm_connector_helper_funcs cirrus_connector_helper_funcs = {
	.get_modes = cirrus_connector_helper_get_modes,
};

static const struct drm_connector_funcs cirrus_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int cirrus_pipe_init(struct cirrus_device *cirrus)
{
	struct drm_device *dev = &cirrus->dev;
	struct drm_plane *primary_plane;
	struct drm_crtc *crtc;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	int ret;

	primary_plane = &cirrus->primary_plane;
	ret = drm_universal_plane_init(dev, primary_plane, 0,
				       &cirrus_primary_plane_funcs,
				       cirrus_primary_plane_formats,
				       ARRAY_SIZE(cirrus_primary_plane_formats),
				       cirrus_primary_plane_format_modifiers,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret)
		return ret;
	drm_plane_helper_add(primary_plane, &cirrus_primary_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(primary_plane);

	crtc = &cirrus->crtc;
	ret = drm_crtc_init_with_planes(dev, crtc, primary_plane, NULL,
					&cirrus_crtc_funcs, NULL);
	if (ret)
		return ret;
	drm_crtc_helper_add(crtc, &cirrus_crtc_helper_funcs);

	encoder = &cirrus->encoder;
	ret = drm_encoder_init(dev, encoder, &cirrus_encoder_funcs,
			       DRM_MODE_ENCODER_VIRTUAL, NULL);
	if (ret)
		return ret;
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	connector = &cirrus->connector;
	ret = drm_connector_init(dev, connector, &cirrus_connector_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret)
		return ret;
	drm_connector_helper_add(connector, &cirrus_connector_helper_funcs);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		return ret;

	return 0;
}

/* ------------------------------------------------------------------ */
/* cirrus framebuffers & mode config				      */

static enum drm_mode_status cirrus_mode_config_mode_valid(struct drm_device *dev,
							  const struct drm_display_mode *mode)
{
	const struct drm_format_info *format = drm_format_info(DRM_FORMAT_XRGB8888);
	uint64_t pitch = drm_format_info_min_pitch(format, 0, mode->hdisplay);

	if (pitch * mode->vdisplay > CIRRUS_VRAM_SIZE)
		return MODE_MEM;

	return MODE_OK;
}

static const struct drm_mode_config_funcs cirrus_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.mode_valid = cirrus_mode_config_mode_valid,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static int cirrus_mode_config_init(struct cirrus_device *cirrus)
{
	struct drm_device *dev = &cirrus->dev;
	int ret;

	ret = drmm_mode_config_init(dev);
	if (ret)
		return ret;

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = CIRRUS_MAX_PITCH / 2;
	dev->mode_config.max_height = 1024;
	dev->mode_config.preferred_depth = 16;
	dev->mode_config.prefer_shadow = 0;
	dev->mode_config.funcs = &cirrus_mode_config_funcs;

	return 0;
}

/* ------------------------------------------------------------------ */

DEFINE_DRM_GEM_FOPS(cirrus_fops);

static const struct drm_driver cirrus_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,

	.name		 = DRIVER_NAME,
	.desc		 = DRIVER_DESC,
	.date		 = DRIVER_DATE,
	.major		 = DRIVER_MAJOR,
	.minor		 = DRIVER_MINOR,

	.fops		 = &cirrus_fops,
	DRM_GEM_SHMEM_DRIVER_OPS,
	DRM_FBDEV_SHMEM_DRIVER_OPS,
};

static int cirrus_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	struct drm_device *dev;
	struct cirrus_device *cirrus;
	int ret;

	ret = aperture_remove_conflicting_pci_devices(pdev, cirrus_driver.name);
	if (ret)
		return ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pci_request_regions(pdev, DRIVER_NAME);
	if (ret)
		return ret;

	ret = -ENOMEM;
	cirrus = devm_drm_dev_alloc(&pdev->dev, &cirrus_driver,
				    struct cirrus_device, dev);
	if (IS_ERR(cirrus))
		return PTR_ERR(cirrus);

	dev = &cirrus->dev;

	cirrus->vram = devm_ioremap(&pdev->dev, pci_resource_start(pdev, 0),
				    pci_resource_len(pdev, 0));
	if (cirrus->vram == NULL)
		return -ENOMEM;

	cirrus->mmio = devm_ioremap(&pdev->dev, pci_resource_start(pdev, 1),
				    pci_resource_len(pdev, 1));
	if (cirrus->mmio == NULL)
		return -ENOMEM;

	ret = cirrus_mode_config_init(cirrus);
	if (ret)
		return ret;

	ret = cirrus_pipe_init(cirrus);
	if (ret < 0)
		return ret;

	drm_mode_config_reset(dev);

	pci_set_drvdata(pdev, dev);
	ret = drm_dev_register(dev, 0);
	if (ret)
		return ret;

	drm_client_setup(dev, NULL);
	return 0;
}

static void cirrus_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_dev_unplug(dev);
	drm_atomic_helper_shutdown(dev);
}

static void cirrus_pci_shutdown(struct pci_dev *pdev)
{
	drm_atomic_helper_shutdown(pci_get_drvdata(pdev));
}

static const struct pci_device_id pciidlist[] = {
	{
		.vendor    = PCI_VENDOR_ID_CIRRUS,
		.device    = PCI_DEVICE_ID_CIRRUS_5446,
		/* only bind to the cirrus chip in qemu */
		.subvendor = PCI_SUBVENDOR_ID_REDHAT_QUMRANET,
		.subdevice = PCI_SUBDEVICE_ID_QEMU,
	}, {
		.vendor    = PCI_VENDOR_ID_CIRRUS,
		.device    = PCI_DEVICE_ID_CIRRUS_5446,
		.subvendor = PCI_VENDOR_ID_XEN,
		.subdevice = 0x0001,
	},
	{ /* end if list */ }
};

static struct pci_driver cirrus_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = cirrus_pci_probe,
	.remove = cirrus_pci_remove,
	.shutdown = cirrus_pci_shutdown,
};

drm_module_pci_driver(cirrus_pci_driver)

MODULE_DEVICE_TABLE(pci, pciidlist);
MODULE_DESCRIPTION("Cirrus driver for QEMU emulated device");
MODULE_LICENSE("GPL");
