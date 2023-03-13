// SPDX-License-Identifier: GPL-2.0-only
/**************************************************************************
 * Copyright (c) 2007-2011, Intel Corporation.
 * All Rights Reserved.
 *
 **************************************************************************/

#include <linux/pfn_t.h>

#include <drm/drm_crtc_helper.h>
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

		ret = vmf_insert_mixed(vma, address, __pfn_to_pfn_t(pfn, PFN_DEV));
		if (unlikely(ret & VM_FAULT_ERROR))
			break;
		address += PAGE_SIZE;
		phys_addr += PAGE_SIZE;
	}
	return ret;
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
	vma->vm_ops = &psb_fbdev_vm_ops;
	vma->vm_private_data = (void *)fb;
	vm_flags_set(vma, VM_IO | VM_MIXEDMAP | VM_DONTEXPAND | VM_DONTDUMP);

	return 0;
}

static const struct fb_ops psb_fbdev_fb_ops = {
	.owner = THIS_MODULE,
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_setcolreg = psb_fbdev_fb_setcolreg,
	.fb_read = drm_fb_helper_cfb_read,
	.fb_write = drm_fb_helper_cfb_write,
	.fb_fillrect = drm_fb_helper_cfb_fillrect,
	.fb_copyarea = drm_fb_helper_cfb_copyarea,
	.fb_imageblit = drm_fb_helper_cfb_imageblit,
	.fb_mmap = psb_fbdev_fb_mmap,
};

/*
 * struct drm_fb_helper_funcs
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

	info = drm_fb_helper_alloc_info(fb_helper);
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

	info->fbops = &psb_fbdev_fb_ops;

	info->fix.smem_start = dev_priv->fb_base;
	info->fix.smem_len = size;
	info->fix.ywrapstep = 0;
	info->fix.ypanstep = 0;

	/* Accessed stolen memory directly */
	info->screen_base = dev_priv->vram_addr + backing->offset;
	info->screen_size = size;

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

	/*
	 * If the mode will not fit in 32bit then switch to 16bit to get
	 * a console on full resolution. The X mode setting server will
	 * allocate its own 32bit GEM framebuffer
	 */
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

	drm_fb_helper_unregister_info(fb_helper);

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
	if (!fb_helper)
		return -ENOMEM;

	dev_priv->fb_helper = fb_helper;

	drm_fb_helper_prepare(dev, fb_helper, 32, &psb_fb_helper_funcs);

	ret = drm_fb_helper_init(dev, fb_helper);
	if (ret)
		goto free;

	/* disable all the possible outputs/crtcs before entering KMS mode */
	drm_helper_disable_unused_functions(dev);

	ret = drm_fb_helper_initial_config(fb_helper);
	if (ret)
		goto fini;

	return 0;

fini:
	drm_fb_helper_fini(fb_helper);
free:
	drm_fb_helper_unprepare(fb_helper);
	kfree(fb_helper);
	return ret;
}

void psb_fbdev_fini(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv = to_drm_psb_private(dev);

	if (!dev_priv->fb_helper)
		return;

	psb_fbdev_destroy(dev, dev_priv->fb_helper);
	drm_fb_helper_unprepare(dev_priv->fb_helper);
	kfree(dev_priv->fb_helper);
	dev_priv->fb_helper = NULL;
}
