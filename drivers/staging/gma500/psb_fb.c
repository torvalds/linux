/**************************************************************************
 * Copyright (c) 2007, Intel Corporation.
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
#include "psb_ttm_userobj_api.h"
#include "psb_fb.h"
#include "psb_sgx.h"
#include "psb_pvr_glue.h"

static void psb_user_framebuffer_destroy(struct drm_framebuffer *fb);
static int psb_user_framebuffer_create_handle(struct drm_framebuffer *fb,
					      struct drm_file *file_priv,
					      unsigned int *handle);

static const struct drm_framebuffer_funcs psb_fb_funcs = {
	.destroy = psb_user_framebuffer_destroy,
	.create_handle = psb_user_framebuffer_create_handle,
};

#define CMAP_TOHW(_val, _width) ((((_val) << (_width)) + 0x7FFF - (_val)) >> 16)

void *psbfb_vdc_reg(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv;
	dev_priv = (struct drm_psb_private *) dev->dev_private;
	return dev_priv->vdc_reg;
}
/*EXPORT_SYMBOL(psbfb_vdc_reg); */

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

static int psbfb_kms_off(struct drm_device *dev, int suspend)
{
	struct drm_framebuffer *fb = 0;
	struct psb_framebuffer *psbfb = to_psb_fb(fb);
	DRM_DEBUG("psbfb_kms_off_ioctl\n");

	mutex_lock(&dev->mode_config.mutex);
	list_for_each_entry(fb, &dev->mode_config.fb_list, head) {
		struct fb_info *info = psbfb->fbdev;

		if (suspend) {
			fb_set_suspend(info, 1);
			drm_fb_helper_blank(FB_BLANK_POWERDOWN, info);
		}
	}
	mutex_unlock(&dev->mode_config.mutex);
	return 0;
}

int psbfb_kms_off_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	int ret;

	if (drm_psb_no_fb)
		return 0;
	console_lock();
	ret = psbfb_kms_off(dev, 0);
	console_unlock();

	return ret;
}

static int psbfb_kms_on(struct drm_device *dev, int resume)
{
	struct drm_framebuffer *fb = 0;
	struct psb_framebuffer *psbfb = to_psb_fb(fb);

	DRM_DEBUG("psbfb_kms_on_ioctl\n");

	mutex_lock(&dev->mode_config.mutex);
	list_for_each_entry(fb, &dev->mode_config.fb_list, head) {
		struct fb_info *info = psbfb->fbdev;

		if (resume) {
			fb_set_suspend(info, 0);
			drm_fb_helper_blank(FB_BLANK_UNBLANK, info);
		}
	}
	mutex_unlock(&dev->mode_config.mutex);

	return 0;
}

int psbfb_kms_on_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	int ret;

	if (drm_psb_no_fb)
		return 0;
	console_lock();
	ret = psbfb_kms_on(dev, 0);
	console_unlock();
	drm_helper_disable_unused_functions(dev);
	return ret;
}

void psbfb_suspend(struct drm_device *dev)
{
	console_lock();
	psbfb_kms_off(dev, 1);
	console_unlock();
}

void psbfb_resume(struct drm_device *dev)
{
	console_lock();
	psbfb_kms_on(dev, 1);
	console_unlock();
	drm_helper_disable_unused_functions(dev);
}

static int psbfb_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	int page_num = 0;
	int i;
	unsigned long address = 0;
	int ret;
	unsigned long pfn;
	struct psb_framebuffer *psbfb = vma->vm_private_data;
	struct drm_device *dev = psbfb->base.dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_gtt *pg = dev_priv->pg;
	unsigned long phys_addr = (unsigned long)pg->stolen_base;;

	page_num = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;

	address = (unsigned long)vmf->virtual_address;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	for (i = 0; i < page_num; i++) {
		pfn = (phys_addr >> PAGE_SHIFT); /* phys_to_pfn(phys_addr); */

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
	DRM_DEBUG("vm_open\n");
}

static void psbfb_vm_close(struct vm_area_struct *vma)
{
	DRM_DEBUG("vm_close\n");
}

static struct vm_operations_struct psbfb_vm_ops = {
	.fault	= psbfb_vm_fault,
	.open	= psbfb_vm_open,
	.close	= psbfb_vm_close
};

static int psbfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct psb_fbdev *fbdev = info->par;
	struct psb_framebuffer *psbfb = fbdev->pfb;
	char *fb_screen_base = NULL;
	struct drm_device *dev = psbfb->base.dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_gtt *pg = dev_priv->pg;

	if (vma->vm_pgoff != 0)
		return -EINVAL;
	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
		return -EINVAL;

	if (!psbfb->addr_space)
		psbfb->addr_space = vma->vm_file->f_mapping;

	fb_screen_base = (char *)info->screen_base;

	DRM_DEBUG("vm_pgoff 0x%lx, screen base %p vram_addr %p\n",
				vma->vm_pgoff, fb_screen_base, pg->vram_addr);

	/*if using stolen memory, */
	if (fb_screen_base == pg->vram_addr) {
		vma->vm_ops = &psbfb_vm_ops;
		vma->vm_private_data = (void *)psbfb;
		vma->vm_flags |= VM_RESERVED | VM_IO |
						VM_MIXEDMAP | VM_DONTEXPAND;
	} else {
	/*using IMG meminfo, can I use pvrmmap to map it?*/

	}

	return 0;
}


static struct fb_ops psbfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcolreg = psbfb_setcolreg,
	.fb_fillrect = psbfb_fillrect,
	.fb_copyarea = psbfb_copyarea,
	.fb_imageblit = psbfb_imageblit,
	.fb_mmap = psbfb_mmap,
	.fb_sync = psbfb_sync,
};

static struct drm_framebuffer *psb_framebuffer_create
			(struct drm_device *dev, struct drm_mode_fb_cmd *r,
			 void *mm_private)
{
	struct psb_framebuffer *fb;
	int ret;

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (!fb)
		return NULL;

	ret = drm_framebuffer_init(dev, &fb->base, &psb_fb_funcs);

	if (ret)
		goto err;

	drm_helper_mode_fill_fb_struct(&fb->base, r);

	fb->bo = mm_private;

	return &fb->base;

err:
	kfree(fb);
	return NULL;
}

static struct drm_framebuffer *psb_user_framebuffer_create
			(struct drm_device *dev, struct drm_file *filp,
			 struct drm_mode_fb_cmd *r)
{
	struct ttm_buffer_object *bo = NULL;
	uint64_t size;

	bo = ttm_buffer_object_lookup(psb_fpriv(filp)->tfile, r->handle);
	if (!bo)
		return NULL;

	/* JB: TODO not drop, make smarter */
	size = ((uint64_t) bo->num_pages) << PAGE_SHIFT;
	if (size < r->width * r->height * 4)
		return NULL;

	/* JB: TODO not drop, refcount buffer */
	return psb_framebuffer_create(dev, r, bo);

#if 0
	struct psb_framebuffer *psbfb;
	struct drm_framebuffer *fb;
	struct fb_info *info;
	void *psKernelMemInfo = NULL;
	void * hKernelMemInfo = (void *)r->handle;
	struct drm_psb_private *dev_priv
		= (struct drm_psb_private *)dev->dev_private;
	struct psb_fbdev *fbdev = dev_priv->fbdev;
	struct psb_gtt *pg = dev_priv->pg;
	int ret;
	uint32_t offset;
	uint64_t size;

	ret = psb_get_meminfo_by_handle(hKernelMemInfo, &psKernelMemInfo);
	if (ret) {
		DRM_ERROR("Cannot get meminfo for handle 0x%x\n",
						(u32)hKernelMemInfo);
		return NULL;
	}

	DRM_DEBUG("Got Kernel MemInfo for handle %lx\n",
		  (u32)hKernelMemInfo);

	/* JB: TODO not drop, make smarter */
	size = psKernelMemInfo->ui32AllocSize;
	if (size < r->height * r->pitch)
		return NULL;

	/* JB: TODO not drop, refcount buffer */
	/* return psb_framebuffer_create(dev, r, bo); */

	fb = psb_framebuffer_create(dev, r, (void *)psKernelMemInfo);
	if (!fb) {
		DRM_ERROR("failed to allocate fb.\n");
		return NULL;
	}

	psbfb = to_psb_fb(fb);
	psbfb->size = size;
	psbfb->hKernelMemInfo = hKernelMemInfo;

	DRM_DEBUG("Mapping to gtt..., KernelMemInfo %p\n", psKernelMemInfo);

	/*if not VRAM, map it into tt aperture*/
	if (psKernelMemInfo->pvLinAddrKM != pg->vram_addr) {
		ret = psb_gtt_map_meminfo(dev, hKernelMemInfo, &offset);
		if (ret) {
			DRM_ERROR("map meminfo for 0x%x failed\n",
				  (u32)hKernelMemInfo);
			return NULL;
		}
		psbfb->offset = (offset << PAGE_SHIFT);
	} else {
		psbfb->offset = 0;
	}
	info = framebuffer_alloc(0, &dev->pdev->dev);
	if (!info)
		return NULL;

	strcpy(info->fix.id, "psbfb");

	info->flags = FBINFO_DEFAULT;
	info->fix.accel = FB_ACCEL_I830;	/*FIXMEAC*/
	info->fbops = &psbfb_ops;

	info->fix.smem_start = dev->mode_config.fb_base;
	info->fix.smem_len = size;

	info->screen_base = psKernelMemInfo->pvLinAddrKM;
	info->screen_size = size;

	drm_fb_helper_fill_fix(info, fb->pitch, fb->depth);
	drm_fb_helper_fill_var(info, &fbdev->psb_fb_helper,
							fb->width, fb->height);

	info->fix.mmio_start = pci_resource_start(dev->pdev, 0);
	info->fix.mmio_len = pci_resource_len(dev->pdev, 0);

	info->pixmap.size = 64 * 1024;
	info->pixmap.buf_align = 8;
	info->pixmap.access_align = 32;
	info->pixmap.flags = FB_PIXMAP_SYSTEM;
	info->pixmap.scan_align = 1;

	psbfb->fbdev = info;
	fbdev->pfb = psbfb;

	fbdev->psb_fb_helper.fb = fb;
	fbdev->psb_fb_helper.fbdev = info;
	MRSTLFBHandleChangeFB(dev, psbfb);

	return fb;
#endif
}

static int psbfb_create(struct psb_fbdev *fbdev,
				struct drm_fb_helper_surface_size *sizes)
{
	struct drm_device *dev = fbdev->psb_fb_helper.dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_gtt *pg = dev_priv->pg;
	struct fb_info *info;
	struct drm_framebuffer *fb;
	struct psb_framebuffer *psbfb;
	struct drm_mode_fb_cmd mode_cmd;
	struct device *device = &dev->pdev->dev;

	struct ttm_buffer_object *fbo = NULL;
	int size, aligned_size;
	int ret;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;

	mode_cmd.bpp = 32;
	/* HW requires pitch to be 64 byte aligned */
	mode_cmd.pitch =  ALIGN(mode_cmd.width * ((mode_cmd.bpp + 1) / 8), 64);
	mode_cmd.depth = 24;

	size = mode_cmd.pitch * mode_cmd.height;
	aligned_size = ALIGN(size, PAGE_SIZE);

	mutex_lock(&dev->struct_mutex);
	fb = psb_framebuffer_create(dev, &mode_cmd, fbo);
	if (!fb) {
		DRM_ERROR("failed to allocate fb.\n");
		ret = -ENOMEM;
		goto out_err1;
	}
	psbfb = to_psb_fb(fb);
	psbfb->size = size;

	info = framebuffer_alloc(sizeof(struct psb_fbdev), device);
	if (!info) {
		ret = -ENOMEM;
		goto out_err0;
	}

	info->par = fbdev;

	psbfb->fbdev = info;

	fbdev->psb_fb_helper.fb = fb;
	fbdev->psb_fb_helper.fbdev = info;
	fbdev->pfb = psbfb;

	strcpy(info->fix.id, "psbfb");

	info->flags = FBINFO_DEFAULT;
	info->fbops = &psbfb_ops;
	info->fix.smem_start = dev->mode_config.fb_base;
	info->fix.smem_len = size;
	info->screen_base = (char *)pg->vram_addr;
	info->screen_size = size;
	memset(info->screen_base, 0, size);

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

	DRM_DEBUG("fb depth is %d\n", fb->depth);
	DRM_DEBUG("   pitch is %d\n", fb->pitch);

	printk(KERN_INFO"allocated %dx%d fb\n",
				psbfb->base.width, psbfb->base.height);

	mutex_unlock(&dev->struct_mutex);

	return 0;
out_err0:
	fb->funcs->destroy(fb);
out_err1:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

static void psbfb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
							u16 blue, int regno)
{
	DRM_DEBUG("%s\n", __func__);
}

static void psbfb_gamma_get(struct drm_crtc *crtc, u16 *red,
					u16 *green, u16 *blue, int regno)
{
	DRM_DEBUG("%s\n", __func__);
}

static int psbfb_probe(struct drm_fb_helper *helper,
				struct drm_fb_helper_surface_size *sizes)
{
	struct psb_fbdev *psb_fbdev = (struct psb_fbdev *)helper;
	int new_fb = 0;
	int ret;

	DRM_DEBUG("%s\n", __func__);

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
	struct psb_framebuffer *psbfb = fbdev->pfb;

	if (fbdev->psb_fb_helper.fbdev) {
		info = fbdev->psb_fb_helper.fbdev;
		unregister_framebuffer(info);
		iounmap(info->screen_base);
		framebuffer_release(info);
	}

	drm_fb_helper_fini(&fbdev->psb_fb_helper);

	drm_framebuffer_cleanup(&psbfb->base);

	return 0;
}

int psb_fbdev_init(struct drm_device *dev)
{
	struct psb_fbdev *fbdev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	int num_crtc;

	fbdev = kzalloc(sizeof(struct psb_fbdev), GFP_KERNEL);
	if (!fbdev) {
		DRM_ERROR("no memory\n");
		return -ENOMEM;
	}

	dev_priv->fbdev = fbdev;
	fbdev->psb_fb_helper.funcs = &psb_fb_helper_funcs;

	num_crtc = 2;

	drm_fb_helper_init(dev, &fbdev->psb_fb_helper, num_crtc,
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

int psbfb_remove(struct drm_device *dev, struct drm_framebuffer *fb)
{
	struct fb_info *info;
	struct psb_framebuffer *psbfb = to_psb_fb(fb);

	if (drm_psb_no_fb)
		return 0;

	info = psbfb->fbdev;
	psbfb->pvrBO = NULL;

	if (info)
		framebuffer_release(info);
	return 0;
}
/*EXPORT_SYMBOL(psbfb_remove); */

static int psb_user_framebuffer_create_handle(struct drm_framebuffer *fb,
					      struct drm_file *file_priv,
					      unsigned int *handle)
{
	/* JB: TODO currently we can't go from a bo to a handle with ttm */
	(void) file_priv;
	*handle = 0;
	return 0;
}

static void psb_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct drm_device *dev = fb->dev;
	struct psb_framebuffer *psbfb = to_psb_fb(fb);

	/*ummap gtt pages*/
	psb_gtt_unmap_meminfo(dev, psbfb->hKernelMemInfo);
	if (psbfb->fbdev)
		psbfb_remove(dev, fb);

	/* JB: TODO not drop, refcount buffer */
	drm_framebuffer_cleanup(fb);
	kfree(fb);
}

static const struct drm_mode_config_funcs psb_mode_funcs = {
	.fb_create = psb_user_framebuffer_create,
	.output_poll_changed = psbfb_output_poll_changed,
};

static int psb_create_backlight_property(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv
				= (struct drm_psb_private *) dev->dev_private;
	struct drm_property *backlight;

	if (dev_priv->backlight_property)
		return 0;

	backlight = drm_property_create(dev,
					DRM_MODE_PROP_RANGE,
					"backlight",
					2);
	backlight->values[0] = 0;
	backlight->values[1] = 100;

	dev_priv->backlight_property = backlight;

	return 0;
}

static void psb_setup_outputs(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv =
	    (struct drm_psb_private *) dev->dev_private;
	struct drm_connector *connector;

	PSB_DEBUG_ENTRY("\n");

	drm_mode_create_scaling_mode_property(dev);

	psb_create_backlight_property(dev);

	psb_intel_lvds_init(dev, &dev_priv->mode_dev);
	/* psb_intel_sdvo_init(dev, SDVOB); */

	list_for_each_entry(connector, &dev->mode_config.connector_list,
			    head) {
		struct psb_intel_output *psb_intel_output =
		    to_psb_intel_output(connector);
		struct drm_encoder *encoder = &psb_intel_output->enc;
		int crtc_mask = 0, clone_mask = 0;

		/* valid crtcs */
		switch (psb_intel_output->type) {
		case INTEL_OUTPUT_SDVO:
			crtc_mask = ((1 << 0) | (1 << 1));
			clone_mask = (1 << INTEL_OUTPUT_SDVO);
			break;
		case INTEL_OUTPUT_LVDS:
			PSB_DEBUG_ENTRY("LVDS.\n");
			crtc_mask = (1 << 1);
			clone_mask = (1 << INTEL_OUTPUT_LVDS);
			break;
		case INTEL_OUTPUT_MIPI:
			PSB_DEBUG_ENTRY("MIPI.\n");
			crtc_mask = (1 << 0);
			clone_mask = (1 << INTEL_OUTPUT_MIPI);
			break;
		case INTEL_OUTPUT_MIPI2:
			PSB_DEBUG_ENTRY("MIPI2.\n");
			crtc_mask = (1 << 2);
			clone_mask = (1 << INTEL_OUTPUT_MIPI2);
			break;
		case INTEL_OUTPUT_HDMI:
			PSB_DEBUG_ENTRY("HDMI.\n");
			crtc_mask = (1 << 1);
			clone_mask = (1 << INTEL_OUTPUT_HDMI);
			break;
		}

		encoder->possible_crtcs = crtc_mask;
		encoder->possible_clones =
		    psb_intel_connector_clones(dev, clone_mask);

	}
}

static void *psb_bo_from_handle(struct drm_device *dev,
				struct drm_file *file_priv,
				unsigned int handle)
{
	void *psKernelMemInfo = NULL;
	void * hKernelMemInfo = (void *)handle;
	int ret;

	ret = psb_get_meminfo_by_handle(hKernelMemInfo, &psKernelMemInfo);
	if (ret) {
		DRM_ERROR("Cannot get meminfo for handle 0x%x\n",
			  (u32)hKernelMemInfo);
		return NULL;
	}

	return (void *)psKernelMemInfo;
}

static size_t psb_bo_size(struct drm_device *dev, void *bof)
{
#if 0
	void *psKernelMemInfo	= (void *)bof;
	return (size_t)psKernelMemInfo->ui32AllocSize;
#else
	return 0;
#endif
}

static size_t psb_bo_offset(struct drm_device *dev, void *bof)
{
	struct psb_framebuffer *psbfb
		= (struct psb_framebuffer *)bof;

	return (size_t)psbfb->offset;
}

static int psb_bo_pin_for_scanout(struct drm_device *dev, void *bo)
{
	 return 0;
}

static int psb_bo_unpin_for_scanout(struct drm_device *dev, void *bo)
{
	return 0;
}

void psb_modeset_init(struct drm_device *dev)
{
	struct drm_psb_private *dev_priv =
	    (struct drm_psb_private *) dev->dev_private;
	struct psb_intel_mode_device *mode_dev = &dev_priv->mode_dev;
	int i;

	PSB_DEBUG_ENTRY("\n");
	/* Init mm functions */
	mode_dev->bo_from_handle = psb_bo_from_handle;
	mode_dev->bo_size = psb_bo_size;
	mode_dev->bo_offset = psb_bo_offset;
	mode_dev->bo_pin_for_scanout = psb_bo_pin_for_scanout;
	mode_dev->bo_unpin_for_scanout = psb_bo_unpin_for_scanout;

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

	/* setup fbs */
	/* drm_initial_config(dev); */
}

void psb_modeset_cleanup(struct drm_device *dev)
{
	mutex_lock(&dev->struct_mutex);

	drm_kms_helper_poll_fini(dev);
	psb_fbdev_fini(dev);

	drm_mode_config_cleanup(dev);

	mutex_unlock(&dev->struct_mutex);
}
