// SPDX-License-Identifier: GPL-2.0-only
/**************************************************************************
 * Copyright (c) 2007-2011, Intel Corporation.
 * All Rights Reserved.
 *
 **************************************************************************/

#include <linux/fb.h>
#include <linux/pfn_t.h>

#include <drm/drm_crtc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_framebuffer.h>

#include "gem.h"
#include "psb_drv.h"

/*
 * VM area struct
 */

static vm_fault_t psb_fbdev_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct fb_info *info = vma->vm_private_data;
	unsigned long address = vmf->address - (vmf->pgoff << PAGE_SHIFT);
	unsigned long pfn = info->fix.smem_start >> PAGE_SHIFT;
	vm_fault_t err = VM_FAULT_SIGBUS;
	unsigned long page_num = vma_pages(vma);
	unsigned long i;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	for (i = 0; i < page_num; ++i) {
		err = vmf_insert_mixed(vma, address, __pfn_to_pfn_t(pfn, PFN_DEV));
		if (unlikely(err & VM_FAULT_ERROR))
			break;
		address += PAGE_SIZE;
		++pfn;
	}

	return err;
}

static const struct vm_operations_struct psb_fbdev_vm_ops = {
	.fault	= psb_fbdev_vm_fault,
};

/*
 * struct fb_ops
 */

#define CMAP_TOHW(_val, _width) ((((_val) << (_width)) + 0x7FFF - (_val)) >> 16)

static int psb_fbdev_fb_setcolreg(unsigned int regno,
				  unsigned int red, unsigned int green,
				  unsigned int blue, unsigned int transp,
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

static int psb_fbdev_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	if (vma->vm_pgoff != 0)
		return -EINVAL;
	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
		return -EINVAL;

	/*
	 * If this is a GEM object then info->screen_base is the virtual
	 * kernel remapping of the object. FIXME: Review if this is
	 * suitable for our mmap work
	 */
	vma->vm_ops = &psb_fbdev_vm_ops;
	vma->vm_private_data = info;
	vm_flags_set(vma, VM_IO | VM_MIXEDMAP | VM_DONTEXPAND | VM_DONTDUMP);

	return 0;
}

static void psb_fbdev_fb_destroy(struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_framebuffer *fb = fb_helper->fb;
	struct drm_gem_object *obj = fb->obj[0];

	drm_fb_helper_fini(fb_helper);

	drm_framebuffer_unregister_private(fb);
	fb->obj[0] = NULL;
	drm_framebuffer_cleanup(fb);
	kfree(fb);

	drm_gem_object_put(obj);

	drm_client_release(&fb_helper->client);

	drm_fb_helper_unprepare(fb_helper);
	kfree(fb_helper);
}

static const struct fb_ops psb_fbdev_fb_ops = {
	.owner = THIS_MODULE,
	__FB_DEFAULT_IOMEM_OPS_RDWR,
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_setcolreg = psb_fbdev_fb_setcolreg,
	__FB_DEFAULT_IOMEM_OPS_DRAW,
	.fb_mmap = psb_fbdev_fb_mmap,
	.fb_destroy = psb_fbdev_fb_destroy,
};

static const struct drm_fb_helper_funcs psb_fbdev_fb_helper_funcs = {
};

/*
 * struct drm_driver
 */

int psb_fbdev_driver_fbdev_probe(struct drm_fb_helper *fb_helper,
				 struct drm_fb_helper_surface_size *sizes)
{
	struct drm_device *dev = fb_helper->dev;
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct fb_info *info;
	struct drm_framebuffer *fb;
	struct drm_mode_fb_cmd2 mode_cmd = { };
	int size;
	int ret;
	struct psb_gem_object *backing;
	struct drm_gem_object *obj;
	u32 bpp, depth;

	/* No 24-bit packed mode */
	if (sizes->surface_bpp == 24) {
		sizes->surface_bpp = 32;
		sizes->surface_depth = 24;
	}
	bpp = sizes->surface_bpp;
	depth = sizes->surface_depth;

	/*
	 * If the mode does not fit in 32 bit then switch to 16 bit to get
	 * a console on full resolution. The X mode setting server will
	 * allocate its own 32-bit GEM framebuffer.
	 */
	size = ALIGN(sizes->surface_width * DIV_ROUND_UP(bpp, 8), 64) *
		     sizes->surface_height;
	size = ALIGN(size, PAGE_SIZE);

	if (size > dev_priv->vram_stolen_size) {
		sizes->surface_bpp = 16;
		sizes->surface_depth = 16;
	}
	bpp = sizes->surface_bpp;
	depth = sizes->surface_depth;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.pitches[0] = ALIGN(mode_cmd.width * DIV_ROUND_UP(bpp, 8), 64);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(bpp, depth);

	size = mode_cmd.pitches[0] * mode_cmd.height;
	size = ALIGN(size, PAGE_SIZE);

	/* Allocate the framebuffer in the GTT with stolen page backing */
	backing = psb_gem_create(dev, size, "fb", true, PAGE_SIZE);
	if (IS_ERR(backing))
		return PTR_ERR(backing);
	obj = &backing->base;

	fb = psb_framebuffer_create(dev, &mode_cmd, obj);
	if (IS_ERR(fb)) {
		ret = PTR_ERR(fb);
		goto err_drm_gem_object_put;
	}

	fb_helper->funcs = &psb_fbdev_fb_helper_funcs;
	fb_helper->fb = fb;

	info = drm_fb_helper_alloc_info(fb_helper);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		goto err_drm_framebuffer_unregister_private;
	}

	info->fbops = &psb_fbdev_fb_ops;

	/* Accessed stolen memory directly */
	info->screen_base = dev_priv->vram_addr + backing->offset;
	info->screen_size = size;

	drm_fb_helper_fill_info(info, fb_helper, sizes);

	info->fix.smem_start = dev_priv->stolen_base + backing->offset;
	info->fix.smem_len = size;
	info->fix.ywrapstep = 0;
	info->fix.ypanstep = 0;
	info->fix.mmio_start = pci_resource_start(pdev, 0);
	info->fix.mmio_len = pci_resource_len(pdev, 0);

	fb_memset_io(info->screen_base, 0, info->screen_size);

	/* Use default scratch pixmap (info->pixmap.flags = FB_PIXMAP_SYSTEM) */

	dev_dbg(dev->dev, "allocated %dx%d fb\n", fb->width, fb->height);

	return 0;

err_drm_framebuffer_unregister_private:
	drm_framebuffer_unregister_private(fb);
	fb->obj[0] = NULL;
	drm_framebuffer_cleanup(fb);
	kfree(fb);
err_drm_gem_object_put:
	drm_gem_object_put(obj);
	return ret;
}
