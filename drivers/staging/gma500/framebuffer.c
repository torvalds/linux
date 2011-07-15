/**************************************************************************
 * Copyright (c) 2007-2011, Intel Corporation.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/console.h>

#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/drm_crtc.h>

#include "psb_drv.h"
#include "psb_intel_reg.h"
#include "psb_intel_drv.h"
#include "framebuffer.h"

#include "mdfld_output.h"

static void psb_user_framebuffer_destroy(struct drm_framebuffer *fb);
static int psb_user_framebuffer_create_handle(struct drm_framebuffer *fb,
					      struct drm_file *file_priv,
					      unsigned int *handle);

static const struct drm_framebuffer_funcs psb_fb_funcs = {
	.destroy = psb_user_framebuffer_destroy,
	.create_handle = psb_user_framebuffer_create_handle,
};

#define CMAP_TOHW(_val, _width) ((((_val) << (_width)) + 0x7FFF - (_val)) >> 16)

static int psbfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   struct fb_info *info)
{
	struct psb_fbdev *fbdev = info->par;
	struct drm_framebuffer *fb = fbdev->psb_fb_helper.fb;
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
		switch (fb->bits_per_pixel) {
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


void psbfb_suspend(struct drm_device *dev)
{
	struct drm_framebuffer *fb = 0;
	struct psb_framebuffer *psbfb = to_psb_fb(fb);

	console_lock();
	mutex_lock(&dev->mode_config.mutex);
	list_for_each_entry(fb, &dev->mode_config.fb_list, head) {
		struct fb_info *info = psbfb->fbdev;
		fb_set_suspend(info, 1);
		drm_fb_helper_blank(FB_BLANK_POWERDOWN, info);
	}
	mutex_unlock(&dev->mode_config.mutex);
	console_unlock();
}

void psbfb_resume(struct drm_device *dev)
{
	struct drm_framebuffer *fb = 0;
	struct psb_framebuffer *psbfb = to_psb_fb(fb);

	console_lock();
	mutex_lock(&dev->mode_config.mutex);
	list_for_each_entry(fb, &dev->mode_config.fb_list, head) {
		struct fb_info *info = psbfb->fbdev;
		fb_set_suspend(info, 0);
		drm_fb_helper_blank(FB_BLANK_UNBLANK, info);
	}
	mutex_unlock(&dev->mode_config.mutex);
	console_unlock();
	drm_helper_disable_unused_functions(dev);
}

static int psbfb_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct psb_framebuffer *psbfb = vma->vm_private_data;
	struct drm_device *dev = psbfb->base.dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	int page_num;
	int i;
	unsigned long address;
	int ret;
	unsigned long pfn;
	/* FIXME: assumes fb at stolen base which may not be true */
	unsigned long phys_addr = (unsigned long)dev_priv->stolen_base;

	page_num = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
	address = (unsigned long)vmf->virtual_address;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	for (i = 0; i < page_num; i++) {
		pfn = (phys_addr >> PAGE_SHIFT);

		ret = vm_insert_mixed(vma, address, pfn);
		if (unlikely((ret == -EBUSY) || (ret != 0 && i > 0)))
			break;
		else if (unlikely(ret != 0)) {
			ret = (ret == -ENOMEM) ? VM_FAULT_OOM : VM_FAULT_SIGBUS;
			return ret;
		}
		address += PAGE_SIZE;
		phys_addr += PAGE_SIZE;
	}
	return VM_FAULT_NOPAGE;
}

static void psbfb_vm_open(struct vm_area_struct *vma)
{
}

static void psbfb_vm_close(struct vm_area_struct *vma)
{
}

static struct vm_operations_struct psbfb_vm_ops = {
	.fault	= psbfb_vm_fault,
	.open	= psbfb_vm_open,
	.close	= psbfb_vm_close
};

static int psbfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct psb_fbdev *fbdev = info->par;
	struct psb_framebuffer *psbfb = &fbdev->pfb;

	if (vma->vm_pgoff != 0)
		return -EINVAL;
	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
		return -EINVAL;

	if (!psbfb->addr_space)
		psbfb->addr_space = vma->vm_file->f_mapping;
	/*
	 * If this is a GEM object then info->screen_base is the virtual
	 * kernel remapping of the object. FIXME: Review if this is
	 * suitable for our mmap work
	 */
	vma->vm_ops = &psbfb_vm_ops;
	vma->vm_private_data = (void *)psbfb;
	vma->vm_flags |= VM_RESERVED | VM_IO |
					VM_MIXEDMAP | VM_DONTEXPAND;
	return 0;
}

static int psbfb_ioctl(struct fb_info *info, unsigned int cmd,
						unsigned long arg)
{
	return -ENOTTY;
}

static struct fb_ops psbfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcolreg = psbfb_setcolreg,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = psbfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_mmap = psbfb_mmap,
	.fb_sync = psbfb_sync,
	.fb_ioctl = psbfb_ioctl,
};

static struct fb_ops psbfb_unaccel_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcolreg = psbfb_setcolreg,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_mmap = psbfb_mmap,
	.fb_ioctl = psbfb_ioctl,
};

/**
 *	psb_framebuffer_init	-	initialize a framebuffer
 *	@dev: our DRM device
 *	@fb: framebuffer to set up
 *	@mode_cmd: mode description
 *	@gt: backing object
 *
 *	Configure and fill in the boilerplate for our frame buffer. Return
 *	0 on success or an error code if we fail.
 */
static int psb_framebuffer_init(struct drm_device *dev,
					struct psb_framebuffer *fb,
					struct drm_mode_fb_cmd *mode_cmd,
					struct gtt_range *gt)
{
	int ret;

	if (mode_cmd->pitch & 63)
		return -EINVAL;
	switch (mode_cmd->bpp) {
	case 8:
	case 16:
	case 24:
	case 32:
		break;
	default:
		return -EINVAL;
	}
	ret = drm_framebuffer_init(dev, &fb->base, &psb_fb_funcs);
	if (ret) {
		dev_err(dev->dev, "framebuffer init failed: %d\n", ret);
		return ret;
	}
	drm_helper_mode_fill_fb_struct(&fb->base, mode_cmd);
	fb->gtt = gt;
	return 0;
}

/**
 *	psb_framebuffer_create	-	create a framebuffer backed by gt
 *	@dev: our DRM device
 *	@mode_cmd: the description of the requested mode
 *	@gt: the backing object
 *
 *	Create a framebuffer object backed by the gt, and fill in the
 *	boilerplate required
 *
 *	TODO: review object references
 */

static struct drm_framebuffer *psb_framebuffer_create
			(struct drm_device *dev,
			 struct drm_mode_fb_cmd *mode_cmd,
			 struct gtt_range *gt)
{
	struct psb_framebuffer *fb;
	int ret;

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (!fb)
		return ERR_PTR(-ENOMEM);

	ret = psb_framebuffer_init(dev, fb, mode_cmd, gt);
	if (ret) {
		kfree(fb);
		return ERR_PTR(ret);
	}
	return &fb->base;
}

/**
 *	psbfb_alloc		-	allocate frame buffer memory
 *	@dev: the DRM device
 *	@aligned_size: space needed
 *
 *	Allocate the frame buffer. In the usual case we get a GTT range that
 *	is stolen memory backed and life is simple. If there isn't sufficient
 *	stolen memory or the system has no stolen memory we allocate a range
 *	and back it with a GEM object.
 *
 *	In this case the GEM object has no handle.
 *
 *	FIXME: console speed up - allocate twice the space if room and use
 *	hardware scrolling for acceleration.
 */
static struct gtt_range *psbfb_alloc(struct drm_device *dev, int aligned_size)
{
	struct gtt_range *backing;
	/* Begin by trying to use stolen memory backing */
	backing = psb_gtt_alloc_range(dev, aligned_size, "fb", 1);
	if (backing) {
		if (drm_gem_private_object_init(dev,
					&backing->gem, aligned_size) == 0)
			return backing;
		psb_gtt_free_range(dev, backing);
	}
	/* Next try using GEM host memory */
	backing = psb_gtt_alloc_range(dev, aligned_size, "fb(gem)", 0);
	if (backing == NULL)
		return NULL;

	/* Now back it with an object */
	if (drm_gem_object_init(dev, &backing->gem, aligned_size) != 0) {
		psb_gtt_free_range(dev, backing);
		return NULL;
	}
	return backing;
}

/**
 *	psbfb_create		-	create a framebuffer
 *	@fbdev: the framebuffer device
 *	@sizes: specification of the layout
 *
 *	Create a framebuffer to the specifications provided
 */
static int psbfb_create(struct psb_fbdev *fbdev,
				struct drm_fb_helper_surface_size *sizes)
{
	struct drm_device *dev = fbdev->psb_fb_helper.dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct fb_info *info;
	struct drm_framebuffer *fb;
	struct psb_framebuffer *psbfb = &fbdev->pfb;
	struct drm_mode_fb_cmd mode_cmd;
	struct device *device = &dev->pdev->dev;
	int size;
	int ret;
	struct gtt_range *backing;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.bpp = sizes->surface_bpp;

	/* No 24bit packed */
	if (mode_cmd.bpp == 24)
		mode_cmd.bpp = 32;

	/* HW requires pitch to be 64 byte aligned */
	mode_cmd.pitch =  ALIGN(mode_cmd.width * ((mode_cmd.bpp + 7) / 8), 64);
	mode_cmd.depth = sizes->surface_depth;

	size = mode_cmd.pitch * mode_cmd.height;
	size = ALIGN(size, PAGE_SIZE);

	/* Allocate the framebuffer in the GTT with stolen page backing */
	backing = psbfb_alloc(dev, size);
	if (backing == NULL)
		return -ENOMEM;

	mutex_lock(&dev->struct_mutex);

	info = framebuffer_alloc(0, device);
	if (!info) {
		ret = -ENOMEM;
		goto out_err1;
	}
	info->par = fbdev;

	ret = psb_framebuffer_init(dev, psbfb, &mode_cmd, backing);
	if (ret)
		goto out_unref;

	fb = &psbfb->base;
	psbfb->fbdev = info;

	fbdev->psb_fb_helper.fb = fb;
	fbdev->psb_fb_helper.fbdev = info;

	strcpy(info->fix.id, "psbfb");

	info->flags = FBINFO_DEFAULT;
	/* No 2D engine */
	if (!dev_priv->ops->accel_2d)
		info->fbops = &psbfb_unaccel_ops;
	else
		info->fbops = &psbfb_ops;

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret) {
		ret = -ENOMEM;
		goto out_unref;
	}

	info->fix.smem_start = dev->mode_config.fb_base;
	info->fix.smem_len = size;

	if (backing->stolen) {
		/* Accessed stolen memory directly */
		info->screen_base = (char *)dev_priv->vram_addr +
							backing->offset;
	} else {
		/* Pin the pages into the GTT and create a mapping to them */
		psb_gtt_pin(backing);
		info->screen_base = vm_map_ram(backing->pages, backing->npage,
				-1, PAGE_KERNEL);
		if (info->screen_base == NULL) {
			psb_gtt_unpin(backing);
			ret = -ENOMEM;
			goto out_unref;
		}
		psbfb->vm_map = 1;
	}
	info->screen_size = size;

	if (dev_priv->gtt.stolen_size) {
		info->apertures = alloc_apertures(1);
		if (!info->apertures) {
			ret = -ENOMEM;
			goto out_unref;
		}
		info->apertures->ranges[0].base = dev->mode_config.fb_base;
		info->apertures->ranges[0].size = dev_priv->gtt.stolen_size;
	}

	drm_fb_helper_fill_fix(info, fb->pitch, fb->depth);
	drm_fb_helper_fill_var(info, &fbdev->psb_fb_helper,
				sizes->fb_width, sizes->fb_height);

	info->fix.mmio_start = pci_resource_start(dev->pdev, 0);
	info->fix.mmio_len = pci_resource_len(dev->pdev, 0);

	info->pixmap.size = 64 * 1024;
	info->pixmap.buf_align = 8;
	info->pixmap.access_align = 32;
	info->pixmap.flags = FB_PIXMAP_SYSTEM;
	info->pixmap.scan_align = 1;

	dev_info(dev->dev, "allocated %dx%d fb\n",
					psbfb->base.width, psbfb->base.height);

	mutex_unlock(&dev->struct_mutex);
	return 0;
out_unref:
	if (backing->stolen)
		psb_gtt_free_range(dev, backing);
	else {
		if (psbfb->vm_map)
			vm_unmap_ram(info->screen_base, backing->npage);
		drm_gem_object_unreference(&backing->gem);
	}
out_err1:
	mutex_unlock(&dev->struct_mutex);
	psb_gtt_free_range(dev, backing);
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
			 struct drm_mode_fb_cmd *cmd)
{
	struct gtt_range *r;
	struct drm_gem_object *obj;

	/*
	 *	Find the GEM object and thus the gtt range object that is
	 *	to back this space
	 */
	obj = drm_gem_object_lookup(dev, filp, cmd->handle);
	if (obj == NULL)
		return ERR_PTR(-ENOENT);

	/* Let the core code do all the work */
	r = container_of(obj, struct gtt_range, gem);
	return psb_framebuffer_create(dev, cmd, r);
}

static void psbfb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
							u16 blue, int regno)
{
}

static void psbfb_gamma_get(struct drm_crtc *crtc, u16 *red,
					u16 *green, u16 *blue, int regno)
{
}

static int psbfb_probe(struct drm_fb_helper *helper,
				struct drm_fb_helper_surface_size *sizes)
{
	struct psb_fbdev *psb_fbdev = (struct psb_fbdev *)helper;
	int new_fb = 0;
	int ret;

	if (!helper->fb) {
		ret = psbfb_create(psb_fbdev, sizes);
		if (ret)
			return ret;
		new_fb = 1;
	}
	return new_fb;
}

struct drm_fb_helper_funcs psb_fb_helper_funcs = {
	.gamma_set = psbfb_gamma_set,
	.gamma_get = psbfb_gamma_get,
	.fb_probe = psbfb_probe,
};

int psb_fbdev_destroy(struct drm_device *dev, struct psb_fbdev *fbdev)
{
	struct fb_info *info;
	struct psb_framebuffer *psbfb = &fbdev->pfb;

	if (fbdev->psb_fb_helper.fbdev) {
		info = fbdev->psb_fb_helper.fbdev;

		/* If this is our base framebuffer then kill any virtual map
		   for the framebuffer layer and unpin it */
		if (psbfb->vm_map) {
			vm_unmap_ram(info->screen_base, psbfb->gtt->npage);
			psb_gtt_unpin(psbfb->gtt);
		}
		unregister_framebuffer(info);
		if (info->cmap.len)
			fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
	}
	drm_fb_helper_fini(&fbdev->psb_fb_helper);
	drm_framebuffer_cleanup(&psbfb->base);

	if (psbfb->gtt)
		drm_gem_object_unreference(&psbfb->gtt->gem);
	return 0;
}

int psb_fbdev_init(struct drm_device *dev)
{
	struct psb_fbdev *fbdev;
	struct drm_psb_private *dev_priv = dev->dev_private;

	fbdev = kzalloc(sizeof(struct psb_fbdev), GFP_KERNEL);
	if (!fbdev) {
		dev_err(dev->dev, "no memory\n");
		return -ENOMEM;
	}

	dev_priv->fbdev = fbdev;
	fbdev->psb_fb_helper.funcs = &psb_fb_helper_funcs;

	drm_fb_helper_init(dev, &fbdev->psb_fb_helper, dev_priv->ops->crtcs,
							INTELFB_CONN_LIMIT);

	drm_fb_helper_single_add_all_connectors(&fbdev->psb_fb_helper);
	drm_fb_helper_initial_config(&fbdev->psb_fb_helper, 32);
	return 0;
}

void psb_fbdev_fini(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;

	if (!dev_priv->fbdev)
		return;

	psb_fbdev_destroy(dev, dev_priv->fbdev);
	kfree(dev_priv->fbdev);
	dev_priv->fbdev = NULL;
}

static void psbfb_output_poll_changed(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_fbdev *fbdev = (struct psb_fbdev *)dev_priv->fbdev;
	drm_fb_helper_hotplug_event(&fbdev->psb_fb_helper);
}

/**
 *	psb_user_framebuffer_create_handle - add hamdle to a framebuffer
 *	@fb: framebuffer
 *	@file_priv: our DRM file
 *	@handle: returned handle
 *
 *	Our framebuffer object is a GTT range which also contains a GEM
 *	object. We need to turn it into a handle for userspace. GEM will do
 *	the work for us
 */
static int psb_user_framebuffer_create_handle(struct drm_framebuffer *fb,
					      struct drm_file *file_priv,
					      unsigned int *handle)
{
	struct psb_framebuffer *psbfb = to_psb_fb(fb);
	struct gtt_range *r = psbfb->gtt;
	return drm_gem_handle_create(file_priv, &r->gem, handle);
}

/**
 *	psb_user_framebuffer_destroy	-	destruct user created fb
 *	@fb: framebuffer
 *
 *	User framebuffers are backed by GEM objects so all we have to do is
 *	clean up a bit and drop the reference, GEM will handle the fallout
 */
static void psb_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct psb_framebuffer *psbfb = to_psb_fb(fb);
	struct gtt_range *r = psbfb->gtt;
	struct drm_device *dev = fb->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_fbdev *fbdev = dev_priv->fbdev;
	struct drm_crtc *crtc;
	int reset = 0;

	/* Should never get stolen memory for a user fb */
	WARN_ON(r->stolen);

	/* Check if we are erroneously live */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head)
		if (crtc->fb == fb)
			reset = 1;

	if (reset)
		/*
		 * Now force a sane response before we permit the DRM CRTC
		 * layer to do stupid things like blank the display. Instead
		 * we reset this framebuffer as if the user had forced a reset.
		 * We must do this before the cleanup so that the DRM layer
		 * doesn't get a chance to stick its oar in where it isn't
		 * wanted.
		 */
		drm_fb_helper_restore_fbdev_mode(&fbdev->psb_fb_helper);

	/* Let DRM do its clean up */
	drm_framebuffer_cleanup(fb);
	/*  We are no longer using the resource in GEM */
	drm_gem_object_unreference_unlocked(&r->gem);
	kfree(fb);
}

static const struct drm_mode_config_funcs psb_mode_funcs = {
	.fb_create = psb_user_framebuffer_create,
	.output_poll_changed = psbfb_output_poll_changed,
};

static int psb_create_backlight_property(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct drm_property *backlight;

	if (dev_priv->backlight_property)
		return 0;

	backlight = drm_property_create(dev, DRM_MODE_PROP_RANGE,
							"backlight", 2);
	backlight->values[0] = 0;
	backlight->values[1] = 100;

	dev_priv->backlight_property = backlight;

	return 0;
}

static void psb_setup_outputs(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct drm_connector *connector;

	drm_mode_create_scaling_mode_property(dev);
	psb_create_backlight_property(dev);

	dev_priv->ops->output_init(dev);

	list_for_each_entry(connector, &dev->mode_config.connector_list,
			    head) {
		struct psb_intel_output *psb_intel_output =
		    to_psb_intel_output(connector);
		struct drm_encoder *encoder = &psb_intel_output->enc;
		int crtc_mask = 0, clone_mask = 0;

		/* valid crtcs */
		switch (psb_intel_output->type) {
		case INTEL_OUTPUT_ANALOG:
			crtc_mask = (1 << 0);
			clone_mask = (1 << INTEL_OUTPUT_ANALOG);
			break;
		case INTEL_OUTPUT_SDVO:
			crtc_mask = ((1 << 0) | (1 << 1));
			clone_mask = (1 << INTEL_OUTPUT_SDVO);
			break;
		case INTEL_OUTPUT_LVDS:
			if (IS_MRST(dev))
				crtc_mask = (1 << 0);
			else
				crtc_mask = (1 << 1);
			clone_mask = (1 << INTEL_OUTPUT_LVDS);
			break;
		case INTEL_OUTPUT_MIPI:
			crtc_mask = (1 << 0);
			clone_mask = (1 << INTEL_OUTPUT_MIPI);
			break;
		case INTEL_OUTPUT_MIPI2:
			crtc_mask = (1 << 2);
			clone_mask = (1 << INTEL_OUTPUT_MIPI2);
			break;
		case INTEL_OUTPUT_HDMI:
			if (IS_MFLD(dev))
				crtc_mask = (1 << 1);
			else	/* FIXME: review Oaktrail */
				crtc_mask = (1 << 0);	/* Cedarview */
			clone_mask = (1 << INTEL_OUTPUT_HDMI);
			break;
		}
		encoder->possible_crtcs = crtc_mask;
		encoder->possible_clones =
		    psb_intel_connector_clones(dev, clone_mask);
	}
}

void psb_modeset_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv =
	    (struct drm_psb_private *) dev->dev_private;
	struct psb_intel_mode_device *mode_dev = &dev_priv->mode_dev;
	int i;

	drm_mode_config_init(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	dev->mode_config.funcs = (void *) &psb_mode_funcs;

	/* set memory base */
	/* MRST and PSB should use BAR 2*/
	pci_read_config_dword(dev->pdev, PSB_BSM, (u32 *)
					&(dev->mode_config.fb_base));

	/* num pipes is 2 for PSB but 1 for Mrst */
	for (i = 0; i < dev_priv->num_pipe; i++)
		psb_intel_crtc_init(dev, i, mode_dev);

	dev->mode_config.max_width = 2048;
	dev->mode_config.max_height = 2048;

	psb_setup_outputs(dev);
}

void psb_modeset_cleanup(struct drm_device *dev)
{
	mutex_lock(&dev->struct_mutex);

	drm_kms_helper_poll_fini(dev);
	psb_fbdev_fini(dev);
	drm_mode_config_cleanup(dev);

	mutex_unlock(&dev->struct_mutex);
}
