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

#include <linux/console.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <video/cirrus.h>
#include <video/vga.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_file.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>

#define DRIVER_NAME "cirrus"
#define DRIVER_DESC "qemu cirrus vga"
#define DRIVER_DATE "2019"
#define DRIVER_MAJOR 2
#define DRIVER_MINOR 0

#define CIRRUS_MAX_PITCH (0x1FF << 3)      /* (4096 - 1) & ~111b bytes */
#define CIRRUS_VRAM_SIZE (4 * 1024 * 1024) /* 4 MB */

struct cirrus_device {
	struct drm_device	       dev;
	struct drm_simple_display_pipe pipe;
	struct drm_connector	       conn;
	unsigned int		       cpp;
	unsigned int		       pitch;
	void __iomem		       *vram;
	void __iomem		       *mmio;
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

static int cirrus_convert_to(struct drm_framebuffer *fb)
{
	if (fb->format->cpp[0] == 4 && fb->pitches[0] > CIRRUS_MAX_PITCH) {
		if (fb->width * 3 <= CIRRUS_MAX_PITCH)
			/* convert from XR24 to RG24 */
			return 3;
		else
			/* convert from XR24 to RG16 */
			return 2;
	}
	return 0;
}

static int cirrus_cpp(struct drm_framebuffer *fb)
{
	int convert_cpp = cirrus_convert_to(fb);

	if (convert_cpp)
		return convert_cpp;
	return fb->format->cpp[0];
}

static int cirrus_pitch(struct drm_framebuffer *fb)
{
	int convert_cpp = cirrus_convert_to(fb);

	if (convert_cpp)
		return convert_cpp * fb->width;
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

static int cirrus_mode_set(struct cirrus_device *cirrus,
			   struct drm_display_mode *mode,
			   struct drm_framebuffer *fb)
{
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

	sr07 = rreg_seq(cirrus, 0x07);
	sr07 &= 0xe0;
	hdr = 0;

	cirrus->cpp = cirrus_cpp(fb);
	switch (cirrus->cpp * 8) {
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

	wreg_seq(cirrus, 0x7, sr07);

	/* Program the pitch */
	cirrus->pitch = cirrus_pitch(fb);
	tmp = cirrus->pitch / 8;
	wreg_crt(cirrus, VGA_CRTC_OFFSET, tmp);

	/* Enable extended blanking and pitch bits, and enable full memory */
	tmp = 0x22;
	tmp |= (cirrus->pitch >> 7) & 0x10;
	tmp |= (cirrus->pitch >> 6) & 0x40;
	wreg_crt(cirrus, 0x1b, tmp);

	/* Enable high-colour modes */
	wreg_gfx(cirrus, VGA_GFX_MODE, 0x40);

	/* And set graphics mode */
	wreg_gfx(cirrus, VGA_GFX_MISC, 0x01);

	wreg_hdr(cirrus, hdr);

	cirrus_set_start_address(cirrus, 0);

	/* Unblank (needed on S3 resume, vgabios doesn't do it then) */
	outb(0x20, 0x3c0);
	return 0;
}

static int cirrus_fb_blit_rect(struct drm_framebuffer *fb,
			       struct drm_rect *rect)
{
	struct cirrus_device *cirrus = fb->dev->dev_private;
	void *vmap;

	vmap = drm_gem_shmem_vmap(fb->obj[0]);
	if (!vmap)
		return -ENOMEM;

	if (cirrus->cpp == fb->format->cpp[0])
		drm_fb_memcpy_dstclip(cirrus->vram,
				      vmap, fb, rect);

	else if (fb->format->cpp[0] == 4 && cirrus->cpp == 2)
		drm_fb_xrgb8888_to_rgb565_dstclip(cirrus->vram,
						  cirrus->pitch,
						  vmap, fb, rect, false);

	else if (fb->format->cpp[0] == 4 && cirrus->cpp == 3)
		drm_fb_xrgb8888_to_rgb888_dstclip(cirrus->vram,
						  cirrus->pitch,
						  vmap, fb, rect);

	else
		WARN_ON_ONCE("cpp mismatch");

	drm_gem_shmem_vunmap(fb->obj[0], vmap);
	return 0;
}

static int cirrus_fb_blit_fullscreen(struct drm_framebuffer *fb)
{
	struct drm_rect fullscreen = {
		.x1 = 0,
		.x2 = fb->width,
		.y1 = 0,
		.y2 = fb->height,
	};
	return cirrus_fb_blit_rect(fb, &fullscreen);
}

static int cirrus_check_size(int width, int height,
			     struct drm_framebuffer *fb)
{
	int pitch = width * 2;

	if (fb)
		pitch = cirrus_pitch(fb);

	if (pitch > CIRRUS_MAX_PITCH)
		return -EINVAL;
	if (pitch * height > CIRRUS_VRAM_SIZE)
		return -EINVAL;
	return 0;
}

/* ------------------------------------------------------------------ */
/* cirrus connector						      */

static int cirrus_conn_get_modes(struct drm_connector *conn)
{
	int count;

	count = drm_add_modes_noedid(conn,
				     conn->dev->mode_config.max_width,
				     conn->dev->mode_config.max_height);
	drm_set_preferred_mode(conn, 1024, 768);
	return count;
}

static const struct drm_connector_helper_funcs cirrus_conn_helper_funcs = {
	.get_modes = cirrus_conn_get_modes,
};

static const struct drm_connector_funcs cirrus_conn_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int cirrus_conn_init(struct cirrus_device *cirrus)
{
	drm_connector_helper_add(&cirrus->conn, &cirrus_conn_helper_funcs);
	return drm_connector_init(&cirrus->dev, &cirrus->conn,
				  &cirrus_conn_funcs, DRM_MODE_CONNECTOR_VGA);

}

/* ------------------------------------------------------------------ */
/* cirrus (simple) display pipe					      */

static enum drm_mode_status cirrus_pipe_mode_valid(struct drm_crtc *crtc,
						   const struct drm_display_mode *mode)
{
	if (cirrus_check_size(mode->hdisplay, mode->vdisplay, NULL) < 0)
		return MODE_BAD;
	return MODE_OK;
}

static int cirrus_pipe_check(struct drm_simple_display_pipe *pipe,
			     struct drm_plane_state *plane_state,
			     struct drm_crtc_state *crtc_state)
{
	struct drm_framebuffer *fb = plane_state->fb;

	if (!fb)
		return 0;
	return cirrus_check_size(fb->width, fb->height, fb);
}

static void cirrus_pipe_enable(struct drm_simple_display_pipe *pipe,
			       struct drm_crtc_state *crtc_state,
			       struct drm_plane_state *plane_state)
{
	struct cirrus_device *cirrus = pipe->crtc.dev->dev_private;

	cirrus_mode_set(cirrus, &crtc_state->mode, plane_state->fb);
	cirrus_fb_blit_fullscreen(plane_state->fb);
}

static void cirrus_pipe_update(struct drm_simple_display_pipe *pipe,
			       struct drm_plane_state *old_state)
{
	struct cirrus_device *cirrus = pipe->crtc.dev->dev_private;
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_crtc *crtc = &pipe->crtc;
	struct drm_rect rect;

	if (pipe->plane.state->fb &&
	    cirrus->cpp != cirrus_cpp(pipe->plane.state->fb))
		cirrus_mode_set(cirrus, &crtc->mode,
				pipe->plane.state->fb);

	if (drm_atomic_helper_damage_merged(old_state, state, &rect))
		cirrus_fb_blit_rect(pipe->plane.state->fb, &rect);

	if (crtc->state->event) {
		spin_lock_irq(&crtc->dev->event_lock);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

static const struct drm_simple_display_pipe_funcs cirrus_pipe_funcs = {
	.mode_valid = cirrus_pipe_mode_valid,
	.check	    = cirrus_pipe_check,
	.enable	    = cirrus_pipe_enable,
	.update	    = cirrus_pipe_update,
};

static const uint32_t cirrus_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_XRGB8888,
};

static const uint64_t cirrus_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

static int cirrus_pipe_init(struct cirrus_device *cirrus)
{
	return drm_simple_display_pipe_init(&cirrus->dev,
					    &cirrus->pipe,
					    &cirrus_pipe_funcs,
					    cirrus_formats,
					    ARRAY_SIZE(cirrus_formats),
					    cirrus_modifiers,
					    &cirrus->conn);
}

/* ------------------------------------------------------------------ */
/* cirrus framebuffers & mode config				      */

static struct drm_framebuffer*
cirrus_fb_create(struct drm_device *dev, struct drm_file *file_priv,
		 const struct drm_mode_fb_cmd2 *mode_cmd)
{
	if (mode_cmd->pixel_format != DRM_FORMAT_RGB565 &&
	    mode_cmd->pixel_format != DRM_FORMAT_RGB888 &&
	    mode_cmd->pixel_format != DRM_FORMAT_XRGB8888)
		return ERR_PTR(-EINVAL);
	if (cirrus_check_size(mode_cmd->width, mode_cmd->height, NULL) < 0)
		return ERR_PTR(-EINVAL);
	return drm_gem_fb_create_with_dirty(dev, file_priv, mode_cmd);
}

static const struct drm_mode_config_funcs cirrus_mode_config_funcs = {
	.fb_create = cirrus_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static void cirrus_mode_config_init(struct cirrus_device *cirrus)
{
	struct drm_device *dev = &cirrus->dev;

	drm_mode_config_init(dev);
	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = CIRRUS_MAX_PITCH / 2;
	dev->mode_config.max_height = 1024;
	dev->mode_config.preferred_depth = 16;
	dev->mode_config.prefer_shadow = 0;
	dev->mode_config.funcs = &cirrus_mode_config_funcs;
}

/* ------------------------------------------------------------------ */

DEFINE_DRM_GEM_SHMEM_FOPS(cirrus_fops);

static struct drm_driver cirrus_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC | DRIVER_PRIME,

	.name		 = DRIVER_NAME,
	.desc		 = DRIVER_DESC,
	.date		 = DRIVER_DATE,
	.major		 = DRIVER_MAJOR,
	.minor		 = DRIVER_MINOR,

	.fops		 = &cirrus_fops,
	DRM_GEM_SHMEM_DRIVER_OPS,
};

static int cirrus_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	struct drm_device *dev;
	struct cirrus_device *cirrus;
	int ret;

	ret = drm_fb_helper_remove_conflicting_pci_framebuffers(pdev, 0, "cirrusdrmfb");
	if (ret)
		return ret;

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	ret = pci_request_regions(pdev, DRIVER_NAME);
	if (ret)
		return ret;

	ret = -ENOMEM;
	cirrus = kzalloc(sizeof(*cirrus), GFP_KERNEL);
	if (cirrus == NULL)
		goto err_pci_release;

	dev = &cirrus->dev;
	ret = drm_dev_init(dev, &cirrus_driver, &pdev->dev);
	if (ret)
		goto err_free_cirrus;
	dev->dev_private = cirrus;

	ret = -ENOMEM;
	cirrus->vram = ioremap(pci_resource_start(pdev, 0),
			       pci_resource_len(pdev, 0));
	if (cirrus->vram == NULL)
		goto err_dev_put;

	cirrus->mmio = ioremap(pci_resource_start(pdev, 1),
			       pci_resource_len(pdev, 1));
	if (cirrus->mmio == NULL)
		goto err_unmap_vram;

	cirrus_mode_config_init(cirrus);

	ret = cirrus_conn_init(cirrus);
	if (ret < 0)
		goto err_cleanup;

	ret = cirrus_pipe_init(cirrus);
	if (ret < 0)
		goto err_cleanup;

	drm_mode_config_reset(dev);

	dev->pdev = pdev;
	pci_set_drvdata(pdev, dev);
	ret = drm_dev_register(dev, 0);
	if (ret)
		goto err_cleanup;

	drm_fbdev_generic_setup(dev, dev->mode_config.preferred_depth);
	return 0;

err_cleanup:
	drm_mode_config_cleanup(dev);
	iounmap(cirrus->mmio);
err_unmap_vram:
	iounmap(cirrus->vram);
err_dev_put:
	drm_dev_put(dev);
err_free_cirrus:
	kfree(cirrus);
err_pci_release:
	pci_release_regions(pdev);
	return ret;
}

static void cirrus_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct cirrus_device *cirrus = dev->dev_private;

	drm_dev_unregister(dev);
	drm_mode_config_cleanup(dev);
	iounmap(cirrus->mmio);
	iounmap(cirrus->vram);
	drm_dev_put(dev);
	kfree(cirrus);
	pci_release_regions(pdev);
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
};

static int __init cirrus_init(void)
{
	if (vgacon_text_force())
		return -EINVAL;
	return pci_register_driver(&cirrus_pci_driver);
}

static void __exit cirrus_exit(void)
{
	pci_unregister_driver(&cirrus_pci_driver);
}

module_init(cirrus_init);
module_exit(cirrus_exit);

MODULE_DEVICE_TABLE(pci, pciidlist);
MODULE_LICENSE("GPL");
