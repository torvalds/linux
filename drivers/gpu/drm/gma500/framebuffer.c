// SPDX-License-Identifier: GPL-2.0-only
/**************************************************************************
 * Copyright (c) 2007-2011, Intel Corporation.
 * All Rights Reserved.
 *
 **************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/pfn_t.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/console.h>

#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "psb_drv.h"
#include "psb_intel_reg.h"
#include "psb_intel_drv.h"
#include "framebuffer.h"
#include "gtt.h"

static const struct drm_framebuffer_funcs psb_fb_funcs = {
	.destroy = drm_gem_fb_destroy,
	.create_handle = drm_gem_fb_create_handle,
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

static int psbfb_pan(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct psb_fbdev *fbdev = info->par;
	struct psb_framebuffer *psbfb = &fbdev->pfb;
	struct drm_device *dev = psbfb->base.dev;
	struct gtt_range *gtt = to_gtt_range(psbfb->base.obj[0]);

	/*
	 *	We have to poke our nose in here. The core fb code assumes
	 *	panning is part of the hardware that can be invoked before
	 *	the actual fb is mapped. In our case that isn't quite true.
	 */
	if (gtt->npage) {
		/* GTT roll shifts in 4K pages, we need to shift the right
		   number of pages */
		int pages = info->fix.line_length >> 12;
		psb_gtt_roll(dev, gtt, var->yoffset * pages);
	}
        return 0;
}

static vm_fault_t psbfb_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct psb_framebuffer *psbfb = vma->vm_private_data;
	struct drm_device *dev = psbfb->base.dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct gtt_range *gtt = to_gtt_range(psbfb->base.obj[0]);
	int page_num;
	int i;
	unsigned long address;
	vm_fault_t ret = VM_FAULT_SIGBUS;
	unsigned long pfn;
	unsigned long phys_addr = (unsigned long)dev_priv->stolen_base +
				  gtt->offset;

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
	vma->vm_flags |= VM_IO | VM_MIXEDMAP | VM_DONTEXPAND | VM_DONTDUMP;
	return 0;
}

static struct fb_ops psbfb_ops = {
	.owner = THIS_MODULE,
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_setcolreg = psbfb_setcolreg,
	.fb_fillrect = drm_fb_helper_cfb_fillrect,
	.fb_copyarea = psbfb_copyarea,
	.fb_imageblit = drm_fb_helper_cfb_imageblit,
	.fb_mmap = psbfb_mmap,
	.fb_sync = psbfb_sync,
};

static struct fb_ops psbfb_roll_ops = {
	.owner = THIS_MODULE,
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_setcolreg = psbfb_setcolreg,
	.fb_fillrect = drm_fb_helper_cfb_fillrect,
	.fb_copyarea = drm_fb_helper_cfb_copyarea,
	.fb_imageblit = drm_fb_helper_cfb_imageblit,
	.fb_pan_display = psbfb_pan,
	.fb_mmap = psbfb_mmap,
};

static struct fb_ops psbfb_unaccel_ops = {
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
 *	@gt: backing object
 *
 *	Configure and fill in the boilerplate for our frame buffer. Return
 *	0 on success or an error code if we fail.
 */
static int psb_framebuffer_init(struct drm_device *dev,
					struct psb_framebuffer *fb,
					const struct drm_mode_fb_cmd2 *mode_cmd,
					struct gtt_range *gt)
{
	const struct drm_format_info *info;
	int ret;

	/*
	 * Reject unknown formats, YUV formats, and formats with more than
	 * 4 bytes per pixel.
	 */
	info = drm_format_info(mode_cmd->pixel_format);
	if (!info || !info->depth || info->cpp[0] > 4)
		return -EINVAL;

	if (mode_cmd->pitches[0] & 63)
		return -EINVAL;

	drm_helper_mode_fill_fb_struct(dev, &fb->base, mode_cmd);
	fb->base.obj[0] = &gt->gem;
	ret = drm_framebuffer_init(dev, &fb->base, &psb_fb_funcs);
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
 *	@gt: the backing object
 *
 *	Create a framebuffer object backed by the gt, and fill in the
 *	boilerplate required
 *
 *	TODO: review object references
 */

static struct drm_framebuffer *psb_framebuffer_create
			(struct drm_device *dev,
			 const struct drm_mode_fb_cmd2 *mode_cmd,
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
 *	we fail as we don't have the virtual mapping space to really vmap it
 *	and the kernel console code can't handle non linear framebuffers.
 *
 *	Re-address this as and if the framebuffer layer grows this ability.
 */
static struct gtt_range *psbfb_alloc(struct drm_device *dev, int aligned_size)
{
	struct gtt_range *backing;
	/* Begin by trying to use stolen memory backing */
	backing = psb_gtt_alloc_range(dev, aligned_size, "fb", 1, PAGE_SIZE);
	if (backing) {
		drm_gem_private_object_init(dev, &backing->gem, aligned_size);
		return backing;
	}
	return NULL;
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
	struct drm_mode_fb_cmd2 mode_cmd;
	int size;
	int ret;
	struct gtt_range *backing;
	u32 bpp, depth;
	int gtt_roll = 0;
	int pitch_lines = 0;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	bpp = sizes->surface_bpp;
	depth = sizes->surface_depth;

	/* No 24bit packed */
	if (bpp == 24)
		bpp = 32;

	do {
		/*
		 * Acceleration via the GTT requires pitch to be
		 * power of two aligned. Preferably page but less
		 * is ok with some fonts
		 */
        	mode_cmd.pitches[0] =  ALIGN(mode_cmd.width * ((bpp + 7) / 8), 4096 >> pitch_lines);

        	size = mode_cmd.pitches[0] * mode_cmd.height;
        	size = ALIGN(size, PAGE_SIZE);

		/* Allocate the fb in the GTT with stolen page backing */
		backing = psbfb_alloc(dev, size);

		if (pitch_lines)
			pitch_lines *= 2;
		else
			pitch_lines = 1;
		gtt_roll++;
	} while (backing == NULL && pitch_lines <= 16);

	/* The final pitch we accepted if we succeeded */
	pitch_lines /= 2;

	if (backing == NULL) {
		/*
		 *	We couldn't get the space we wanted, fall back to the
		 *	display engine requirement instead.  The HW requires
		 *	the pitch to be 64 byte aligned
		 */

		gtt_roll = 0;	/* Don't use GTT accelerated scrolling */
		pitch_lines = 64;

		mode_cmd.pitches[0] =  ALIGN(mode_cmd.width * ((bpp + 7) / 8), 64);

		size = mode_cmd.pitches[0] * mode_cmd.height;
		size = ALIGN(size, PAGE_SIZE);

		/* Allocate the framebuffer in the GTT with stolen page backing */
		backing = psbfb_alloc(dev, size);
		if (backing == NULL)
			return -ENOMEM;
	}

	memset(dev_priv->vram_addr + backing->offset, 0, size);

	info = drm_fb_helper_alloc_fbi(&fbdev->psb_fb_helper);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		goto out;
	}

	mode_cmd.pixel_format = drm_mode_legacy_fb_format(bpp, depth);

	ret = psb_framebuffer_init(dev, psbfb, &mode_cmd, backing);
	if (ret)
		goto out;

	fb = &psbfb->base;
	psbfb->fbdev = info;

	fbdev->psb_fb_helper.fb = fb;

	if (dev_priv->ops->accel_2d && pitch_lines > 8)	/* 2D engine */
		info->fbops = &psbfb_ops;
	else if (gtt_roll) {	/* GTT rolling seems best */
		info->fbops = &psbfb_roll_ops;
		info->flags |= FBINFO_HWACCEL_YPAN;
	} else	/* Software */
		info->fbops = &psbfb_unaccel_ops;

	info->fix.smem_start = dev->mode_config.fb_base;
	info->fix.smem_len = size;
	info->fix.ywrapstep = gtt_roll;
	info->fix.ypanstep = 0;

	/* Accessed stolen memory directly */
	info->screen_base = dev_priv->vram_addr + backing->offset;
	info->screen_size = size;

	if (dev_priv->gtt.stolen_size) {
		info->apertures->ranges[0].base = dev->mode_config.fb_base;
		info->apertures->ranges[0].size = dev_priv->gtt.stolen_size;
	}

	drm_fb_helper_fill_info(info, &fbdev->psb_fb_helper, sizes);

	info->fix.mmio_start = pci_resource_start(dev->pdev, 0);
	info->fix.mmio_len = pci_resource_len(dev->pdev, 0);

	/* Use default scratch pixmap (info->pixmap.flags = FB_PIXMAP_SYSTEM) */

	dev_dbg(dev->dev, "allocated %dx%d fb\n",
					psbfb->base.width, psbfb->base.height);

	return 0;
out:
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
			 const struct drm_mode_fb_cmd2 *cmd)
{
	struct gtt_range *r;
	struct drm_gem_object *obj;

	/*
	 *	Find the GEM object and thus the gtt range object that is
	 *	to back this space
	 */
	obj = drm_gem_object_lookup(filp, cmd->handles[0]);
	if (obj == NULL)
		return ERR_PTR(-ENOENT);

	/* Let the core code do all the work */
	r = container_of(obj, struct gtt_range, gem);
	return psb_framebuffer_create(dev, cmd, r);
}

static int psbfb_probe(struct drm_fb_helper *helper,
				struct drm_fb_helper_surface_size *sizes)
{
	struct psb_fbdev *psb_fbdev =
		container_of(helper, struct psb_fbdev, psb_fb_helper);
	struct drm_device *dev = psb_fbdev->psb_fb_helper.dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	int bytespp;

	bytespp = sizes->surface_bpp / 8;
	if (bytespp == 3)	/* no 24bit packed */
		bytespp = 4;

	/* If the mode will not fit in 32bit then switch to 16bit to get
	   a console on full resolution. The X mode setting server will
	   allocate its own 32bit GEM framebuffer */
	if (ALIGN(sizes->fb_width * bytespp, 64) * sizes->fb_height >
	                dev_priv->vram_stolen_size) {
                sizes->surface_bpp = 16;
                sizes->surface_depth = 16;
        }

	return psbfb_create(psb_fbdev, sizes);
}

static const struct drm_fb_helper_funcs psb_fb_helper_funcs = {
	.fb_probe = psbfb_probe,
};

static int psb_fbdev_destroy(struct drm_device *dev, struct psb_fbdev *fbdev)
{
	struct psb_framebuffer *psbfb = &fbdev->pfb;

	drm_fb_helper_unregister_fbi(&fbdev->psb_fb_helper);

	drm_fb_helper_fini(&fbdev->psb_fb_helper);
	drm_framebuffer_unregister_private(&psbfb->base);
	drm_framebuffer_cleanup(&psbfb->base);

	if (psbfb->base.obj[0])
		drm_gem_object_put_unlocked(psbfb->base.obj[0]);
	return 0;
}

int psb_fbdev_init(struct drm_device *dev)
{
	struct psb_fbdev *fbdev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	int ret;

	fbdev = kzalloc(sizeof(struct psb_fbdev), GFP_KERNEL);
	if (!fbdev) {
		dev_err(dev->dev, "no memory\n");
		return -ENOMEM;
	}

	dev_priv->fbdev = fbdev;

	drm_fb_helper_prepare(dev, &fbdev->psb_fb_helper, &psb_fb_helper_funcs);

	ret = drm_fb_helper_init(dev, &fbdev->psb_fb_helper,
				 INTELFB_CONN_LIMIT);
	if (ret)
		goto free;

	ret = drm_fb_helper_single_add_all_connectors(&fbdev->psb_fb_helper);
	if (ret)
		goto fini;

	/* disable all the possible outputs/crtcs before entering KMS mode */
	drm_helper_disable_unused_functions(dev);

	ret = drm_fb_helper_initial_config(&fbdev->psb_fb_helper, 32);
	if (ret)
		goto fini;

	return 0;

fini:
	drm_fb_helper_fini(&fbdev->psb_fb_helper);
free:
	kfree(fbdev);
	return ret;
}

static void psb_fbdev_fini(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;

	if (!dev_priv->fbdev)
		return;

	psb_fbdev_destroy(dev, dev_priv->fbdev);
	kfree(dev_priv->fbdev);
	dev_priv->fbdev = NULL;
}

static const struct drm_mode_config_funcs psb_mode_funcs = {
	.fb_create = psb_user_framebuffer_create,
	.output_poll_changed = drm_fb_helper_output_poll_changed,
};

static void psb_setup_outputs(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct drm_connector *connector;

	drm_mode_create_scaling_mode_property(dev);

	/* It is ok for this to fail - we just don't get backlight control */
	if (!dev_priv->backlight_property)
		dev_priv->backlight_property = drm_property_create_range(dev, 0,
							"backlight", 0, 100);
	dev_priv->ops->output_init(dev);

	list_for_each_entry(connector, &dev->mode_config.connector_list,
			    head) {
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
			clone_mask = (1 << INTEL_OUTPUT_SDVO);
			break;
		case INTEL_OUTPUT_LVDS:
		        crtc_mask = dev_priv->ops->lvds_mask;
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
		        crtc_mask = dev_priv->ops->hdmi_mask;
			clone_mask = (1 << INTEL_OUTPUT_HDMI);
			break;
		case INTEL_OUTPUT_DISPLAYPORT:
			crtc_mask = (1 << 0) | (1 << 1);
			clone_mask = (1 << INTEL_OUTPUT_DISPLAYPORT);
			break;
		case INTEL_OUTPUT_EDP:
			crtc_mask = (1 << 1);
			clone_mask = (1 << INTEL_OUTPUT_EDP);
		}
		encoder->possible_crtcs = crtc_mask;
		encoder->possible_clones =
		    gma_connector_clones(dev, clone_mask);
	}
}

void psb_modeset_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_intel_mode_device *mode_dev = &dev_priv->mode_dev;
	int i;

	drm_mode_config_init(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;

	dev->mode_config.funcs = &psb_mode_funcs;

	/* set memory base */
	/* Oaktrail and Poulsbo should use BAR 2*/
	pci_read_config_dword(dev->pdev, PSB_BSM, (u32 *)
					&(dev->mode_config.fb_base));

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
	struct drm_psb_private *dev_priv = dev->dev_private;
	if (dev_priv->modeset) {
		drm_kms_helper_poll_fini(dev);
		psb_fbdev_fini(dev);
		drm_mode_config_cleanup(dev);
	}
}
