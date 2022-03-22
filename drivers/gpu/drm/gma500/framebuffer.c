// SPDX-License-Identifier: GPL-2.0-only
/**************************************************************************
 * Copyright (c) 2007-2011, Intel Corporation.
 * All Rights Reserved.
 *
 **************************************************************************/

#include <linux/console.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pfn_t.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/tty.h>

#include <drm/drm.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "framebuffer.h"
#include "gem.h"
#include "psb_drv.h"
#include "psb_intel_drv.h"
#include "psb_intel_reg.h"

static const struct drm_framebuffer_funcs psb_fb_funcs = {
	.destroy = drm_gem_fb_destroy,
	.create_handle = drm_gem_fb_create_handle,
};

#define CMAP_TOHW(_val, _width) ((((_val) << (_width)) + 0x7FFF - (_val)) >> 16)

static int psbfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_framebuffer *fb = fb_helper->fb;
	uint32_t v;

	if (!fb)
		return -ENOMEM;

	if (regno > 255)
		return 1;

	red = CMAP_TOHW(red, info->var.red.length);
	blue = CMAP_TOHW(blue, info->var.blue.length);
	green = CMAP_TOHW(green, info->var.green.length);
	transp = CMAP_TOHW(transp, info->var.transp.length);

	v = (red << info->var.red.offset) |
	    (green << info->var.green.offset) |
	    (blue << info->var.blue.offset) |
	    (transp << info->var.transp.offset);

	if (regno < 16) {
		switch (fb->format->cpp[0] * 8) {
		case 16:
			((uint32_t *) info->pseudo_palette)[regno] = v;
			break;
		case 24:
		case 32:
			((uint32_t *) info->pseudo_palette)[regno] = v;
			break;
		}
	}

	return 0;
}

static vm_fault_t psbfb_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_framebuffer *fb = vma->vm_private_data;
	struct drm_device *dev = fb->dev;
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct psb_gem_object *pobj = to_psb_gem_object(fb->obj[0]);
	int page_num;
	int i;
	unsigned long address;
	vm_fault_t ret = VM_FAULT_SIGBUS;
	unsigned long pfn;
	unsigned long phys_addr = (unsigned long)dev_priv->stolen_base + pobj->offset;

	page_num = vma_pages(vma);
	address = vmf->address - (vmf->pgoff << PAGE_SHIFT);

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	for (i = 0; i < page_num; i++) {
		pfn = (phys_addr >> PAGE_SHIFT);

		ret = vmf_insert_mixed(vma, address,
				__pfn_to_pfn_t(pfn, PFN_DEV));
		if (unlikely(ret & VM_FAULT_ERROR))
			break;
		address += PAGE_SIZE;
		phys_addr += PAGE_SIZE;
	}
	return ret;
}

static void psbfb_vm_open(struct vm_area_struct *vma)
{
}

static void psbfb_vm_close(struct vm_area_struct *vma)
{
}

static const struct vm_operations_struct psbfb_vm_ops = {
	.fault	= psbfb_vm_fault,
	.open	= psbfb_vm_open,
	.close	= psbfb_vm_close
};

static int psbfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_framebuffer *fb = fb_helper->fb;

	if (vma->vm_pgoff != 0)
		return -EINVAL;
	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
		return -EINVAL;

	/*
	 * If this is a GEM object then info->screen_base is the virtual
	 * kernel remapping of the object. FIXME: Review if this is
	 * suitable for our mmap work
	 */
	vma->vm_ops = &psbfb_vm_ops;
	vma->vm_private_data = (void *)fb;
	vma->vm_flags |= VM_IO | VM_MIXEDMAP | VM_DONTEXPAND | VM_DONTDUMP;
	return 0;
}

static const struct fb_ops psbfb_unaccel_ops = {
	.owner = THIS_MODULE,
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_setcolreg = psbfb_setcolreg,
	.fb_fillrect = drm_fb_helper_cfb_fillrect,
	.fb_copyarea = drm_fb_helper_cfb_copyarea,
	.fb_imageblit = drm_fb_helper_cfb_imageblit,
	.fb_mmap = psbfb_mmap,
};

/**
 *	psb_framebuffer_init	-	initialize a framebuffer
 *	@dev: our DRM device
 *	@fb: framebuffer to set up
 *	@mode_cmd: mode description
 *	@obj: backing object
 *
 *	Configure and fill in the boilerplate for our frame buffer. Return
 *	0 on success or an error code if we fail.
 */
static int psb_framebuffer_init(struct drm_device *dev,
					struct drm_framebuffer *fb,
					const struct drm_mode_fb_cmd2 *mode_cmd,
					struct drm_gem_object *obj)
{
	const struct drm_format_info *info;
	int ret;

	/*
	 * Reject unknown formats, YUV formats, and formats with more than
	 * 4 bytes per pixel.
	 */
	info = drm_get_format_info(dev, mode_cmd);
	if (!info || !info->depth || info->cpp[0] > 4)
		return -EINVAL;

	if (mode_cmd->pitches[0] & 63)
		return -EINVAL;

	drm_helper_mode_fill_fb_struct(dev, fb, mode_cmd);
	fb->obj[0] = obj;
	ret = drm_framebuffer_init(dev, fb, &psb_fb_funcs);
	if (ret) {
		dev_err(dev->dev, "framebuffer init failed: %d\n", ret);
		return ret;
	}
	return 0;
}

/**
 *	psb_framebuffer_create	-	create a framebuffer backed by gt
 *	@dev: our DRM device
 *	@mode_cmd: the description of the requested mode
 *	@obj: the backing object
 *
 *	Create a framebuffer object backed by the gt, and fill in the
 *	boilerplate required
 *
 *	TODO: review object references
 */

static struct drm_framebuffer *psb_framebuffer_create
			(struct drm_device *dev,
			 const struct drm_mode_fb_cmd2 *mode_cmd,
			 struct drm_gem_object *obj)
{
	struct drm_framebuffer *fb;
	int ret;

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (!fb)
		return ERR_PTR(-ENOMEM);

	ret = psb_framebuffer_init(dev, fb, mode_cmd, obj);
	if (ret) {
		kfree(fb);
		return ERR_PTR(ret);
	}
	return fb;
}

/**
 *	psbfb_create		-	create a framebuffer
 *	@fb_helper: the framebuffer helper
 *	@sizes: specification of the layout
 *
 *	Create a framebuffer to the specifications provided
 */
static int psbfb_create(struct drm_fb_helper *fb_helper,
				struct drm_fb_helper_surface_size *sizes)
{
	struct drm_device *dev = fb_helper->dev;
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct fb_info *info;
	struct drm_framebuffer *fb;
	struct drm_mode_fb_cmd2 mode_cmd;
	int size;
	int ret;
	struct psb_gem_object *backing;
	struct drm_gem_object *obj;
	u32 bpp, depth;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	bpp = sizes->surface_bpp;
	depth = sizes->surface_depth;

	/* No 24bit packed */
	if (bpp == 24)
		bpp = 32;

	mode_cmd.pitches[0] = ALIGN(mode_cmd.width * DIV_ROUND_UP(bpp, 8), 64);

	size = mode_cmd.pitches[0] * mode_cmd.height;
	size = ALIGN(size, PAGE_SIZE);

	/* Allocate the framebuffer in the GTT with stolen page backing */
	backing = psb_gem_create(dev, size, "fb", true, PAGE_SIZE);
	if (IS_ERR(backing))
		return PTR_ERR(backing);
	obj = &backing->base;

	memset(dev_priv->vram_addr + backing->offset, 0, size);

	info = drm_fb_helper_alloc_fbi(fb_helper);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		goto err_drm_gem_object_put;
	}

	mode_cmd.pixel_format = drm_mode_legacy_fb_format(bpp, depth);

	fb = psb_framebuffer_create(dev, &mode_cmd, obj);
	if (IS_ERR(fb)) {
		ret = PTR_ERR(fb);
		goto err_drm_gem_object_put;
	}

	fb_helper->fb = fb;

	info->fbops = &psbfb_unaccel_ops;

	info->fix.smem_start = dev->mode_config.fb_base;
	info->fix.smem_len = size;
	info->fix.ywrapstep = 0;
	info->fix.ypanstep = 0;

	/* Accessed stolen memory directly */
	info->screen_base = dev_priv->vram_addr + backing->offset;
	info->screen_size = size;

	if (dev_priv->gtt.stolen_size) {
		info->apertures->ranges[0].base = dev->mode_config.fb_base;
		info->apertures->ranges[0].size = dev_priv->gtt.stolen_size;
	}

	drm_fb_helper_fill_info(info, fb_helper, sizes);

	info->fix.mmio_start = pci_resource_start(pdev, 0);
	info->fix.mmio_len = pci_resource_len(pdev, 0);

	/* Use default scratch pixmap (info->pixmap.flags = FB_PIXMAP_SYSTEM) */

	dev_dbg(dev->dev, "allocated %dx%d fb\n", fb->width, fb->height);

	return 0;

err_drm_gem_object_put:
	drm_gem_object_put(obj);
	return ret;
}

/**
 *	psb_user_framebuffer_create	-	create framebuffer
 *	@dev: our DRM device
 *	@filp: client file
 *	@cmd: mode request
 *
 *	Create a new framebuffer backed by a userspace GEM object
 */
static struct drm_framebuffer *psb_user_framebuffer_create
			(struct drm_device *dev, struct drm_file *filp,
			 const struct drm_mode_fb_cmd2 *cmd)
{
	struct drm_gem_object *obj;
	struct drm_framebuffer *fb;

	/*
	 *	Find the GEM object and thus the gtt range object that is
	 *	to back this space
	 */
	obj = drm_gem_object_lookup(filp, cmd->handles[0]);
	if (obj == NULL)
		return ERR_PTR(-ENOENT);

	/* Let the core code do all the work */
	fb = psb_framebuffer_create(dev, cmd, obj);
	if (IS_ERR(fb))
		drm_gem_object_put(obj);

	return fb;
}

static int psbfb_probe(struct drm_fb_helper *fb_helper,
				struct drm_fb_helper_surface_size *sizes)
{
	struct drm_device *dev = fb_helper->dev;
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	unsigned int fb_size;
	int bytespp;

	bytespp = sizes->surface_bpp / 8;
	if (bytespp == 3)	/* no 24bit packed */
		bytespp = 4;

	/* If the mode will not fit in 32bit then switch to 16bit to get
	   a console on full resolution. The X mode setting server will
	   allocate its own 32bit GEM framebuffer */
	fb_size = ALIGN(sizes->surface_width * bytespp, 64) *
		  sizes->surface_height;
	fb_size = ALIGN(fb_size, PAGE_SIZE);

	if (fb_size > dev_priv->vram_stolen_size) {
                sizes->surface_bpp = 16;
                sizes->surface_depth = 16;
        }

	return psbfb_create(fb_helper, sizes);
}

static const struct drm_fb_helper_funcs psb_fb_helper_funcs = {
	.fb_probe = psbfb_probe,
};

static int psb_fbdev_destroy(struct drm_device *dev,
			     struct drm_fb_helper *fb_helper)
{
	struct drm_framebuffer *fb = fb_helper->fb;

	drm_fb_helper_unregister_fbi(fb_helper);

	drm_fb_helper_fini(fb_helper);
	drm_framebuffer_unregister_private(fb);
	drm_framebuffer_cleanup(fb);

	if (fb->obj[0])
		drm_gem_object_put(fb->obj[0]);
	kfree(fb);

	return 0;
}

int psb_fbdev_init(struct drm_device *dev)
{
	struct drm_fb_helper *fb_helper;
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	int ret;

	fb_helper = kzalloc(sizeof(*fb_helper), GFP_KERNEL);
	if (!fb_helper) {
		dev_err(dev->dev, "no memory\n");
		return -ENOMEM;
	}

	dev_priv->fb_helper = fb_helper;

	drm_fb_helper_prepare(dev, fb_helper, &psb_fb_helper_funcs);

	ret = drm_fb_helper_init(dev, fb_helper);
	if (ret)
		goto free;

	/* disable all the possible outputs/crtcs before entering KMS mode */
	drm_helper_disable_unused_functions(dev);

	ret = drm_fb_helper_initial_config(fb_helper, 32);
	if (ret)
		goto fini;

	return 0;

fini:
	drm_fb_helper_fini(fb_helper);
free:
	kfree(fb_helper);
	return ret;
}

static void psb_fbdev_fini(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);

	if (!dev_priv->fb_helper)
		return;

	psb_fbdev_destroy(dev, dev_priv->fb_helper);
	kfree(dev_priv->fb_helper);
	dev_priv->fb_helper = NULL;
}

static const struct drm_mode_config_funcs psb_mode_funcs = {
	.fb_create = psb_user_framebuffer_create,
	.output_poll_changed = drm_fb_helper_output_poll_changed,
};

static void psb_setup_outputs(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct drm_connector_list_iter conn_iter;
	struct drm_connector *connector;

	drm_mode_create_scaling_mode_property(dev);

	/* It is ok for this to fail - we just don't get backlight control */
	if (!dev_priv->backlight_property)
		dev_priv->backlight_property = drm_property_create_range(dev, 0,
							"backlight", 0, 100);
	dev_priv->ops->output_init(dev);

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct gma_encoder *gma_encoder = gma_attached_encoder(connector);
		struct drm_encoder *encoder = &gma_encoder->base;
		int crtc_mask = 0, clone_mask = 0;

		/* valid crtcs */
		switch (gma_encoder->type) {
		case INTEL_OUTPUT_ANALOG:
			crtc_mask = (1 << 0);
			clone_mask = (1 << INTEL_OUTPUT_ANALOG);
			break;
		case INTEL_OUTPUT_SDVO:
			crtc_mask = dev_priv->ops->sdvo_mask;
			clone_mask = 0;
			break;
		case INTEL_OUTPUT_LVDS:
			crtc_mask = dev_priv->ops->lvds_mask;
			clone_mask = 0;
			break;
		case INTEL_OUTPUT_MIPI:
			crtc_mask = (1 << 0);
			clone_mask = 0;
			break;
		case INTEL_OUTPUT_MIPI2:
			crtc_mask = (1 << 2);
			clone_mask = 0;
			break;
		case INTEL_OUTPUT_HDMI:
			crtc_mask = dev_priv->ops->hdmi_mask;
			clone_mask = (1 << INTEL_OUTPUT_HDMI);
			break;
		case INTEL_OUTPUT_DISPLAYPORT:
			crtc_mask = (1 << 0) | (1 << 1);
			clone_mask = 0;
			break;
		case INTEL_OUTPUT_EDP:
			crtc_mask = (1 << 1);
			clone_mask = 0;
		}
		encoder->possible_crtcs = crtc_mask;
		encoder->possible_clones =
		    gma_connector_clones(dev, clone_mask);
	}
	drm_connector_list_iter_end(&conn_iter);
}

void psb_modeset_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct psb_intel_mode_device *mode_dev = &dev_priv->mode_dev;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	int i;

	if (drmm_mode_config_init(dev))
		return;

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	dev->mode_config.funcs = &psb_mode_funcs;

	/* set memory base */
	/* Oaktrail and Poulsbo should use BAR 2*/
	pci_read_config_dword(pdev, PSB_BSM, (u32 *)&(dev->mode_config.fb_base));

	/* num pipes is 2 for PSB but 1 for Mrst */
	for (i = 0; i < dev_priv->num_pipe; i++)
		psb_intel_crtc_init(dev, i, mode_dev);

	dev->mode_config.max_width = 4096;
	dev->mode_config.max_height = 4096;

	psb_setup_outputs(dev);

	if (dev_priv->ops->errata)
	        dev_priv->ops->errata(dev);

        dev_priv->modeset = true;
}

void psb_modeset_cleanup(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	if (dev_priv->modeset) {
		drm_kms_helper_poll_fini(dev);
		psb_fbdev_fini(dev);
	}
}
