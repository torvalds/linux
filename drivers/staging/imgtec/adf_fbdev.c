/* -*- mode: c; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* vi: set ts=8 sw=8 sts=8: */
/*************************************************************************/ /*!
@File
@Codingstyle    LinuxKernel
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#include <linux/version.h>
#include <linux/console.h>
#include <linux/dma-buf.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/fb.h>

#include <drm/drm_fourcc.h>

#include <video/adf.h>
#include <video/adf_fbdev.h>
#include <video/adf_client.h>

#include <adf/adf_ext.h>

/* for sync_fence_put */
#include PVR_ANDROID_SYNC_HEADER

#include "adf_common.h"

#ifndef CONFIG_FB
#error adf_fbdev needs Linux framebuffer support. Enable it in your kernel.
#endif

MODULE_AUTHOR("Imagination Technologies Ltd. <gpl-support@imgtec.com>");
MODULE_LICENSE("Dual MIT/GPL");

/* NOTE: This is just an example of how to use adf. You should NOT use this
 *       module in a production environment. It is meaningless to layer adf
 *       on top of fbdev, as adf is more flexible than fbdev and adf itself
 *       provides fbdev emulation. Do not use this implementation generally!
 */

#define DRVNAME "adf_fbdev"

#define FALLBACK_REFRESH_RATE	60
#define FALLBACK_DPI		160

#if defined(ADF_FBDEV_NUM_PREFERRED_BUFFERS)
#define NUM_PREFERRED_BUFFERS	ADF_FBDEV_NUM_PREFERRED_BUFFERS
#else
#define NUM_PREFERRED_BUFFERS	3
#endif

struct adf_fbdev_dmabuf {
	struct sg_table	sg_table;
	size_t offset;
	size_t length;
	void *vaddr;

	/* Used for cleanup of dmabuf private data */
	spinlock_t *alloc_lock;
	u8 *alloc_mask;
	u8 id;
};

struct adf_fbdev_device {
	struct adf_device base;
	struct fb_info *fb_info;
	atomic_t refcount;
};

struct adf_fbdev_interface {
	struct adf_interface base;
	struct drm_mode_modeinfo fb_mode;
	u16 width_mm, height_mm;
	struct fb_info *fb_info;
	spinlock_t alloc_lock;
	u8 alloc_mask;
};

/* SIMPLE BUFFER MANAGER *****************************************************/

/* Handle alloc/free from the fbdev carveout (fix.smem_start -> fix.smem_size)
 * region. This simple allocator sets a bit in the alloc_mask when a buffer is
 * owned by dmabuf. When the dmabuf ->release() is called, the alloc_mask bit
 * is cleared and the adf_fbdev_dmabuf object is freed.
 *
 * Since dmabuf relies on sg_table/scatterlists, and hence struct page*, this
 * code may have problems if your framebuffer uses memory that is not in the
 * kernel's page tables.
 */

static struct adf_fbdev_dmabuf *
adf_fbdev_alloc_buffer(struct adf_fbdev_interface *interface)
{
	struct adf_fbdev_dmabuf *fbdev_dmabuf;
	struct scatterlist *sg;
	size_t unitary_size;
	struct page *page;
	u32 offset = 0;
	int i, err;
	u32 id;

	spin_lock(&interface->alloc_lock);

	for (id = 0; id < NUM_PREFERRED_BUFFERS; id++) {
		if (!(interface->alloc_mask & (1UL << id))) {
			interface->alloc_mask |= (1UL << id);
			break;
		}
	}

	spin_unlock(&interface->alloc_lock);

	if (id == NUM_PREFERRED_BUFFERS)
		return ERR_PTR(-ENOMEM);

	unitary_size = interface->fb_info->fix.line_length *
		       interface->fb_info->var.yres;

	/* PAGE_SIZE alignment has been checked already, do NOT allow it
	 * through here. We are about to allocate an sg_list.
	 */
	BUG_ON((unitary_size % PAGE_SIZE) != 0);

	fbdev_dmabuf = kmalloc(sizeof(*fbdev_dmabuf), GFP_KERNEL);
	if (!fbdev_dmabuf)
		return ERR_PTR(-ENOMEM);

	err = sg_alloc_table(&fbdev_dmabuf->sg_table, unitary_size / PAGE_SIZE,
			     GFP_KERNEL);
	if (err) {
		kfree(fbdev_dmabuf);
		return ERR_PTR(err);
	}

	/* Increment the reference count of this module as long as the
	 * adb_fbdev_dmabuf object exists. This prevents this module from
	 * being unloaded if the buffer is passed around by dmabuf.
	 */
	if (!try_module_get(THIS_MODULE)) {
		pr_err("try_module_get(THIS_MODULE) failed");
		kfree(fbdev_dmabuf);
		return ERR_PTR(-EFAULT);
	}

	fbdev_dmabuf->offset = id * unitary_size;
	fbdev_dmabuf->length = unitary_size;
	fbdev_dmabuf->vaddr  = interface->fb_info->screen_base +
			       fbdev_dmabuf->offset;

	for_each_sg(fbdev_dmabuf->sg_table.sgl, sg,
		    fbdev_dmabuf->sg_table.nents, i) {
		page = vmalloc_to_page(fbdev_dmabuf->vaddr + offset);
		if (!page) {
			pr_err("Failed to map fbdev vaddr to pages\n");
			kfree(fbdev_dmabuf);
			return ERR_PTR(-EFAULT);
		}
		sg_set_page(sg, page, PAGE_SIZE, 0);
		offset += PAGE_SIZE;

		/* Shadow what ion is doing currently to ensure sg_dma_address()
		 * is valid. This is not strictly correct as the dma address
		 * should only be valid after mapping (ownership changed), and
		 * we haven't mapped the scatter list yet.
		 */
		sg_dma_address(sg) = sg_phys(sg);
	}

	fbdev_dmabuf->alloc_mask = &interface->alloc_mask;
	fbdev_dmabuf->alloc_lock = &interface->alloc_lock;
	fbdev_dmabuf->id         = id;

	return fbdev_dmabuf;
}

static void adf_fbdev_free_buffer(struct adf_fbdev_dmabuf *fbdev_dmabuf)
{
	unsigned long flags;

	spin_lock_irqsave(fbdev_dmabuf->alloc_lock, flags);
	(*fbdev_dmabuf->alloc_mask) &= ~(1UL << fbdev_dmabuf->id);
	spin_unlock_irqrestore(fbdev_dmabuf->alloc_lock, flags);

	sg_free_table(&fbdev_dmabuf->sg_table);
	kfree(fbdev_dmabuf);

	module_put(THIS_MODULE);
}

/* DMA BUF LAYER *************************************************************/

static struct sg_table *
adf_fbdev_d_map_dma_buf(struct dma_buf_attachment *attachment,
			enum dma_data_direction direction)
{
	struct adf_fbdev_dmabuf *fbdev_dmabuf = attachment->dmabuf->priv;

	return &fbdev_dmabuf->sg_table;
}

static void adf_fbdev_d_unmap_dma_buf(struct dma_buf_attachment *attachment,
				      struct sg_table *table,
				      enum dma_data_direction direction)
{
	/* No-op */
}

static int adf_fbdev_d_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct adf_fbdev_dmabuf *fbdev_dmabuf = dmabuf->priv;
	unsigned long offset = vma->vm_pgoff * PAGE_SIZE;
	unsigned long addr = vma->vm_start;
	unsigned long remainder, len;
	struct scatterlist *sg;
	struct page *page;
	u32 i;

	for_each_sg(fbdev_dmabuf->sg_table.sgl, sg,
		    fbdev_dmabuf->sg_table.nents, i) {
		page = sg_page(sg);
		if (!page) {
			pr_err("Failed to retrieve pages\n");
			return -EFAULT;
		}
		remainder = vma->vm_end - addr;
		len = sg_dma_len(sg);
		if (offset >= sg_dma_len(sg)) {
			offset -= sg_dma_len(sg);
			continue;
		} else if (offset) {
			page += offset / PAGE_SIZE;
			len = sg_dma_len(sg) - offset;
			offset = 0;
		}
		len = min(len, remainder);
		remap_pfn_range(vma, addr, page_to_pfn(page), len,
				vma->vm_page_prot);
		addr += len;
		if (addr >= vma->vm_end)
			return 0;
	}

	return 0;
}

static void adf_fbdev_d_release(struct dma_buf *dmabuf)
{
	adf_fbdev_free_buffer(dmabuf->priv);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)) && \
    !defined(CHROMIUMOS_WORKAROUNDS_KERNEL318)

static int
adf_fbdev_d_begin_cpu_access(struct dma_buf *dmabuf, size_t start, size_t len,
			     enum dma_data_direction dir)
{
	struct adf_fbdev_dmabuf *fbdev_dmabuf = dmabuf->priv;

	if (start + len > fbdev_dmabuf->length)
		return -EINVAL;
	return 0;
}

static void adf_fbdev_d_end_cpu_access(struct dma_buf *dmabuf, size_t start,
				       size_t len, enum dma_data_direction dir)
{
	/* Framebuffer memory is cache coherent. No-op. */
}

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)) &&
          !defined(CHROMIUMOS_WORKAROUNDS_KERNEL318) */

static void *
adf_fbdev_d_kmap(struct dma_buf *dmabuf, unsigned long page_offset)
{
	struct adf_fbdev_dmabuf *fbdev_dmabuf = dmabuf->priv;
	void *vaddr;

	if (page_offset * PAGE_SIZE >= fbdev_dmabuf->length)
		return ERR_PTR(-EINVAL);
	vaddr = fbdev_dmabuf->vaddr + page_offset * PAGE_SIZE;
	return vaddr;
}

static void
adf_fbdev_d_kunmap(struct dma_buf *dmabuf, unsigned long page_offset,
		   void *ptr)
{
	/* No-op */
}

static void *adf_fbdev_d_vmap(struct dma_buf *dmabuf)
{
	struct adf_fbdev_dmabuf *fbdev_dmabuf = dmabuf->priv;

	return fbdev_dmabuf->vaddr;
}

static void adf_fbdev_d_vunmap(struct dma_buf *dmabuf, void *vaddr)
{
	/* No-op */
}

static const struct dma_buf_ops adf_fbdev_dma_buf_ops = {
	.map_dma_buf		= adf_fbdev_d_map_dma_buf,
	.unmap_dma_buf		= adf_fbdev_d_unmap_dma_buf,
	.mmap			= adf_fbdev_d_mmap,
	.release		= adf_fbdev_d_release,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)) && \
    !defined(CHROMIUMOS_WORKAROUNDS_KERNEL318)
	.begin_cpu_access	= adf_fbdev_d_begin_cpu_access,
	.end_cpu_access		= adf_fbdev_d_end_cpu_access,
#endif
	.kmap_atomic		= adf_fbdev_d_kmap,
	.kunmap_atomic		= adf_fbdev_d_kunmap,
	.kmap			= adf_fbdev_d_kmap,
	.kunmap			= adf_fbdev_d_kunmap,
	.vmap			= adf_fbdev_d_vmap,
	.vunmap			= adf_fbdev_d_vunmap,
};

/* ADF LAYER *****************************************************************/

static u32 adf_fbdev_supported_format;

static int adf_fbdev_validate(struct adf_device *dev, struct adf_post *cfg,
			      void **driver_state)
{
	int err = adf_img_validate_simple(dev, cfg, driver_state);

	if (cfg->n_bufs == 0 || err != 0)
		return err;

	/* Everything checked out in the generic validation, but we
	 * additionally want to check that the dmabuf came from the
	 * adf_fbdev module, which the generic code can't check.
	 */
	if (cfg->bufs[0].dma_bufs[0]->ops != &adf_fbdev_dma_buf_ops)
		return -EINVAL;

	return 0;
}

static void adf_fbdev_post(struct adf_device *dev, struct adf_post *cfg,
			   void *driver_state)
{
	struct adf_fbdev_device *device = (struct adf_fbdev_device *)dev;
	struct fb_var_screeninfo new_var = device->fb_info->var;
	struct adf_fbdev_dmabuf *fbdev_dmabuf;
	struct adf_buffer *buffer;
	int err;

	/* "Null" flip handling */
	if (cfg->n_bufs == 0)
		return;

	if (!lock_fb_info(device->fb_info)) {
		pr_err("Failed to lock fb_info structure.\n");
		return;
	}

	console_lock();

	buffer = &cfg->bufs[0];
	fbdev_dmabuf = buffer->dma_bufs[0]->priv;
	new_var.yoffset = new_var.yres * fbdev_dmabuf->id;

	/* If we're supposed to be able to flip, but the yres_virtual has been
	 * changed to an unsupported (smaller) value, we need to change it back
	 * (this is a workaround for some Linux fbdev drivers that seem to lose
	 * any modifications to yres_virtual after a blank.)
	 */
	if (new_var.yres_virtual < new_var.yres * NUM_PREFERRED_BUFFERS) {
		new_var.activate = FB_ACTIVATE_NOW;
		new_var.yres_virtual = new_var.yres * NUM_PREFERRED_BUFFERS;

		err = fb_set_var(device->fb_info, &new_var);
		if (err)
			pr_err("fb_set_var failed (err=%d)\n", err);
	} else {
		err = fb_pan_display(device->fb_info, &new_var);
		if (err)
			pr_err("fb_pan_display failed (err=%d)\n", err);
	}

	console_unlock();

	unlock_fb_info(device->fb_info);
}

static int
adf_fbdev_open2(struct adf_obj *obj, struct inode *inode, struct file *file)
{
	struct adf_fbdev_device *dev =
		(struct adf_fbdev_device *)obj->parent;
	atomic_inc(&dev->refcount);
	return 0;
}

static void
adf_fbdev_release2(struct adf_obj *obj, struct inode *inode, struct file *file)
{
	struct adf_fbdev_device *dev =
		(struct adf_fbdev_device *)obj->parent;
	struct sync_fence *release_fence;

	if (atomic_dec_return(&dev->refcount))
		return;

	/* This special "null" flip works around a problem with ADF
	 * which leaves buffers pinned by the display engine even
	 * after all ADF clients have closed.
	 *
	 * The "null" flip is pipelined like any other. The user won't
	 * be able to unload this module until it has been posted.
	 */
	release_fence = adf_device_post(&dev->base, NULL, 0, NULL, 0, NULL, 0);
	if (IS_ERR_OR_NULL(release_fence)) {
		pr_err("Failed to queue null flip command (err=%d).\n",
		       (int)PTR_ERR(release_fence));
		return;
	}

	sync_fence_put(release_fence);
}

static const struct adf_device_ops adf_fbdev_device_ops = {
	.owner			= THIS_MODULE,
	.base = {
		.open		= adf_fbdev_open2,
		.release	= adf_fbdev_release2,
		.ioctl		= adf_img_ioctl,
	},
	.validate		= adf_fbdev_validate,
	.post			= adf_fbdev_post,
};

static bool
adf_fbdev_supports_event(struct adf_obj *obj, enum adf_event_type type)
{
	switch (type) {
	case ADF_EVENT_VSYNC:
	case ADF_EVENT_HOTPLUG:
		return true;
	default:
		return false;
	}
}

static void
adf_fbdev_set_event(struct adf_obj *obj, enum adf_event_type type,
		    bool enabled)
{
	switch (type) {
	case ADF_EVENT_VSYNC:
	case ADF_EVENT_HOTPLUG:
		break;
	default:
		BUG();
	}
}

static int adf_fbdev_blank2(struct adf_interface *intf, u8 state)
{
	struct adf_fbdev_interface *interface =
		(struct adf_fbdev_interface *)intf;
	struct fb_info *fb_info = interface->fb_info;

	if (!fb_info->fbops->fb_blank)
		return -EOPNOTSUPP;

	return fb_info->fbops->fb_blank(state, fb_info);
}

static int
adf_fbdev_alloc_simple_buffer(struct adf_interface *intf, u16 w, u16 h,
			      u32 format, struct dma_buf **dma_buf,
			      u32 *offset, u32 *pitch)
{
	struct adf_fbdev_interface *interface =
		(struct adf_fbdev_interface *)intf;
	struct fb_var_screeninfo *var = &interface->fb_info->var;
	struct adf_fbdev_dmabuf *fbdev_dmabuf;

	if (w != var->xres) {
		pr_err("Simple alloc request w=%u does not match w=%u.\n",
		       w, var->xres);
		return -EINVAL;
	}

	if (h != var->yres) {
		pr_err("Simple alloc request h=%u does not match h=%u.\n",
		       h, var->yres);
		return -EINVAL;
	}

	if (format != adf_fbdev_supported_format) {
		pr_err("Simple alloc request f=0x%x does not match f=0x%x.\n",
		       format, adf_fbdev_supported_format);
		return -EINVAL;
	}

	fbdev_dmabuf = adf_fbdev_alloc_buffer(interface);
	if (IS_ERR_OR_NULL(fbdev_dmabuf))
		return PTR_ERR(fbdev_dmabuf);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
	{
		DEFINE_DMA_BUF_EXPORT_INFO(export_info);

		export_info.ops = &adf_fbdev_dma_buf_ops;
		export_info.size = fbdev_dmabuf->length;
		export_info.flags = O_RDWR;
		export_info.priv = fbdev_dmabuf;

		*dma_buf = dma_buf_export(&export_info);
	}
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0))
	*dma_buf = dma_buf_export(fbdev_dmabuf, &adf_fbdev_dma_buf_ops,
				  fbdev_dmabuf->length, O_RDWR, NULL);
#else
	*dma_buf = dma_buf_export(fbdev_dmabuf, &adf_fbdev_dma_buf_ops,
				  fbdev_dmabuf->length, O_RDWR);
#endif
	if (IS_ERR(*dma_buf)) {
		adf_fbdev_free_buffer(fbdev_dmabuf);
		return PTR_ERR(*dma_buf);
	}

	*pitch = interface->fb_info->fix.line_length;
	*offset = 0;
	return 0;
}

static int
adf_fbdev_screen_size(struct adf_interface *intf, u16 *width_mm,
		      u16 *height_mm)
{
	struct adf_fbdev_interface *interface =
		(struct adf_fbdev_interface *)intf;
	*width_mm  = interface->width_mm;
	*height_mm = interface->height_mm;
	return 0;
}

static int adf_fbdev_modeset(struct adf_interface *intf,
			     struct drm_mode_modeinfo *mode)
{
	struct adf_fbdev_interface *interface =
		(struct adf_fbdev_interface *)intf;
	return mode == &interface->fb_mode ? 0 : -EINVAL;
}

static const struct adf_interface_ops adf_fbdev_interface_ops = {
	.base = {
		.supports_event	= adf_fbdev_supports_event,
		.set_event	= adf_fbdev_set_event,
	},
	.blank			= adf_fbdev_blank2,
	.alloc_simple_buffer	= adf_fbdev_alloc_simple_buffer,
	.screen_size		= adf_fbdev_screen_size,
	.modeset		= adf_fbdev_modeset,
};

struct adf_overlay_engine_ops adf_fbdev_overlay_engine_ops = {
	.supported_formats	= &adf_fbdev_supported_format,
	.n_supported_formats	= 1,
};

/* If we can flip, we need to make sure we have the memory to do so.
 *
 * We'll assume that the fbdev device provides extra space in
 * yres_virtual for panning; xres_virtual is theoretically supported,
 * but it involves more work.
 *
 * If the fbdev device doesn't have yres_virtual > yres, we'll try
 * requesting it before bailing. Userspace applications commonly do
 * this with an FBIOPUT_VSCREENINFO ioctl().
 *
 * Another problem is with a limitation in PowerVR services -- it
 * needs framebuffers to be page aligned (this is a SW limitation,
 * the HW can support non-page-aligned buffers). So we have to
 * check that stride * height for a single buffer is page aligned.
 */
static bool adf_fbdev_flip_possible(struct fb_info *fb_info)
{
	struct fb_var_screeninfo var = fb_info->var;
	int err;

	if (!fb_info->fix.xpanstep && !fb_info->fix.ypanstep &&
	    !fb_info->fix.ywrapstep) {
		pr_err("The fbdev device detected does not support ypan/ywrap.\n");
		return false;
	}

	if ((fb_info->fix.line_length * var.yres) % PAGE_SIZE != 0) {
		pr_err("Line length (in bytes) x yres is not a multiple of page size.\n");
		return false;
	}

	/* We might already have enough space */
	if (var.yres * NUM_PREFERRED_BUFFERS <= var.yres_virtual)
		return true;

	pr_err("No buffer space for flipping; asking for more.\n");

	var.activate = FB_ACTIVATE_NOW;
	var.yres_virtual = var.yres * NUM_PREFERRED_BUFFERS;

	err = fb_set_var(fb_info, &var);
	if (err) {
		pr_err("fb_set_var failed (err=%d).\n", err);
		return false;
	}

	if (var.yres * NUM_PREFERRED_BUFFERS > var.yres_virtual) {
		pr_err("Failed to obtain additional buffer space.\n");
		return false;
	}

	/* Some fbdev drivers allow the yres_virtual modification through,
	 * but don't actually update the fix. We need the fix to be updated
	 * and more memory allocated, so we can actually take advantage of
	 * the increased yres_virtual.
	 */
	if (fb_info->fix.smem_len < fb_info->fix.line_length *
				    var.yres_virtual) {
		pr_err("'fix' not re-allocated with sufficient buffer space.\n");
		pr_err("Check NUM_PREFERRED_BUFFERS (%u) is as intended.\n",
		       NUM_PREFERRED_BUFFERS);
		return false;
	}

	return true;
}

/* Could use devres here? */
static struct {
	struct adf_fbdev_device		device;
	struct adf_fbdev_interface	interface;
	struct adf_overlay_engine	engine;
} dev_data;

static int __init init_adf_fbdev(void)
{
	struct drm_mode_modeinfo *mode = &dev_data.interface.fb_mode;
	char format_str[ADF_FORMAT_STR_SIZE];
	struct fb_info *fb_info;
	int err = -ENODEV;

	fb_info = registered_fb[0];
	if (!fb_info) {
		pr_err("No Linux framebuffer (fbdev) device is registered!\n");
		pr_err("Check you have a framebuffer driver compiled into your kernel\n");
		pr_err("and that it is enabled on the cmdline.\n");
		goto err_out;
	}

	if (!lock_fb_info(fb_info))
		goto err_out;

	console_lock();

	/* Filter out broken FB devices */
	if (!fb_info->fix.smem_len || !fb_info->fix.line_length) {
		pr_err("The fbdev device detected had a zero smem_len or line_length,\n");
		pr_err("which suggests it is a broken driver.\n");
		goto err_unlock;
	}

	if (fb_info->fix.type != FB_TYPE_PACKED_PIXELS ||
	    fb_info->fix.visual != FB_VISUAL_TRUECOLOR) {
		pr_err("The fbdev device detected is not truecolor with packed pixels.\n");
		goto err_unlock;
	}

	if (fb_info->var.bits_per_pixel == 32) {
		if (fb_info->var.red.length   == 8  ||
		    fb_info->var.green.length == 8  ||
		    fb_info->var.blue.length  == 8  ||
		    fb_info->var.red.offset   == 16 ||
		    fb_info->var.green.offset == 8  ||
		    fb_info->var.blue.offset  == 0) {
#if defined(ADF_FBDEV_FORCE_XRGB8888)
			adf_fbdev_supported_format = DRM_FORMAT_BGRX8888;
#else
			adf_fbdev_supported_format = DRM_FORMAT_BGRA8888;
#endif
		} else if (fb_info->var.red.length   == 8  ||
			   fb_info->var.green.length == 8  ||
			   fb_info->var.blue.length  == 8  ||
			   fb_info->var.red.offset   == 0  ||
			   fb_info->var.green.offset == 8  ||
			   fb_info->var.blue.offset  == 16) {
			adf_fbdev_supported_format = DRM_FORMAT_RGBA8888;
		} else {
			pr_err("The fbdev device detected uses an unrecognized 32bit pixel format (%u/%u/%u, %u/%u/%u)\n",
			       fb_info->var.red.length,
			       fb_info->var.green.length,
			       fb_info->var.blue.length,
			       fb_info->var.red.offset,
			       fb_info->var.green.offset,
			       fb_info->var.blue.offset);
			goto err_unlock;
		}
	} else if (fb_info->var.bits_per_pixel == 16) {
		if (fb_info->var.red.length   != 5  ||
		    fb_info->var.green.length != 6  ||
		    fb_info->var.blue.length  != 5  ||
		    fb_info->var.red.offset   != 11 ||
		    fb_info->var.green.offset != 5  ||
		    fb_info->var.blue.offset  != 0) {
			pr_err("The fbdev device detected uses an unrecognized 16bit pixel format (%u/%u/%u, %u/%u/%u)\n",
			       fb_info->var.red.length,
			       fb_info->var.green.length,
			       fb_info->var.blue.length,
			       fb_info->var.red.offset,
			       fb_info->var.green.offset,
			       fb_info->var.blue.offset);
			goto err_unlock;
		}
		adf_fbdev_supported_format = DRM_FORMAT_BGR565;
	} else {
		pr_err("The fbdev device detected uses an unsupported bpp (%u).\n",
		       fb_info->var.bits_per_pixel);
		goto err_unlock;
	}

#if defined(CONFIG_ARCH_MT8173)
	/* Workaround for broken framebuffer driver. The wrong pixel format
	 * is reported to this module. It is always really RGBA8888.
	 */
	adf_fbdev_supported_format = DRM_FORMAT_RGBA8888;
#endif

	if (!try_module_get(fb_info->fbops->owner)) {
		pr_err("try_module_get() failed");
		goto err_unlock;
	}

	if (fb_info->fbops->fb_open &&
	    fb_info->fbops->fb_open(fb_info, 0) != 0) {
		pr_err("fb_open() failed");
		goto err_module_put;
	}

	if (!adf_fbdev_flip_possible(fb_info)) {
		pr_err("Flipping must be supported for ADF. Aborting.\n");
		goto err_fb_release;
	}

	err = adf_device_init(&dev_data.device.base, fb_info->dev,
			      &adf_fbdev_device_ops, "fbdev");
	if (err) {
		pr_err("adf_device_init failed (%d)", err);
		goto err_fb_release;
	}

	dev_data.device.fb_info = fb_info;

	err = adf_interface_init(&dev_data.interface.base,
				 &dev_data.device.base,
				 ADF_INTF_DVI, 0, ADF_INTF_FLAG_PRIMARY,
				 &adf_fbdev_interface_ops, "fbdev_interface");
	if (err) {
		pr_err("adf_interface_init failed (%d)", err);
		goto err_device_destroy;
	}

	spin_lock_init(&dev_data.interface.alloc_lock);
	dev_data.interface.fb_info = fb_info;

	/* If the fbdev mode looks viable, try to inherit from it */
	if (fb_info->mode)
		adf_modeinfo_from_fb_videomode(fb_info->mode, mode);

	/* Framebuffer drivers aren't always very good at filling out their
	 * mode information, so fake up anything that's missing so we don't
	 * need to accommodate it in userspace.
	 */

	if (!mode->hdisplay)
		mode->hdisplay = fb_info->var.xres;
	if (!mode->vdisplay)
		mode->vdisplay = fb_info->var.yres;
	if (!mode->vrefresh)
		mode->vrefresh = FALLBACK_REFRESH_RATE;

	if (fb_info->var.width > 0 && fb_info->var.width < 1000) {
		dev_data.interface.width_mm = fb_info->var.width;
	} else {
		dev_data.interface.width_mm = (fb_info->var.xres * 25400) /
					      (FALLBACK_DPI * 1000);
	}

	if (fb_info->var.height > 0 && fb_info->var.height < 1000) {
		dev_data.interface.height_mm = fb_info->var.height;
	} else {
		dev_data.interface.height_mm = (fb_info->var.yres * 25400) /
					       (FALLBACK_DPI * 1000);
	}

	err = adf_hotplug_notify_connected(&dev_data.interface.base, mode, 1);
	if (err) {
		pr_err("adf_hotplug_notify_connected failed (%d)", err);
		goto err_interface_destroy;
	}

	/* This doesn't really set the mode, it just updates current_mode */
	err = adf_interface_set_mode(&dev_data.interface.base, mode);
	if (err) {
		pr_err("adf_interface_set_mode failed (%d)", err);
		goto err_interface_destroy;
	}

	err = adf_overlay_engine_init(&dev_data.engine, &dev_data.device.base,
				      &adf_fbdev_overlay_engine_ops,
				      "fbdev_overlay_engine");
	if (err) {
		pr_err("adf_overlay_engine_init failed (%d)", err);
		goto err_interface_destroy;
	}

	err = adf_attachment_allow(&dev_data.device.base,
				   &dev_data.engine,
				   &dev_data.interface.base);

	if (err) {
		pr_err("adf_attachment_allow failed (%d)", err);
		goto err_overlay_engine_destroy;
	}

	adf_format_str(adf_fbdev_supported_format, format_str);
	pr_info("Found usable fbdev device (%s):\n"
		"range (physical) = 0x%lx-0x%lx\n"
		"range (virtual)  = %p-%p\n"
		"size (bytes)     = 0x%x\n"
		"xres x yres      = %ux%u\n"
		"xres x yres (v)  = %ux%u\n"
		"physical (mm)    = %ux%u\n"
		"refresh (Hz)     = %u\n"
		"drm fourcc       = %s (0x%x)\n",
		fb_info->fix.id,
		fb_info->fix.smem_start,
		fb_info->fix.smem_start + fb_info->fix.smem_len,
		fb_info->screen_base,
		fb_info->screen_base + fb_info->screen_size,
		fb_info->fix.smem_len,
		mode->hdisplay, mode->vdisplay,
		fb_info->var.xres_virtual, fb_info->var.yres_virtual,
		dev_data.interface.width_mm, dev_data.interface.height_mm,
		mode->vrefresh,
		format_str, adf_fbdev_supported_format);
	err = 0;
err_unlock:
	console_unlock();
	unlock_fb_info(fb_info);
err_out:
	return err;
err_overlay_engine_destroy:
	adf_overlay_engine_destroy(&dev_data.engine);
err_interface_destroy:
	adf_interface_destroy(&dev_data.interface.base);
err_device_destroy:
	adf_device_destroy(&dev_data.device.base);
err_fb_release:
	if (fb_info->fbops->fb_release)
		fb_info->fbops->fb_release(fb_info, 0);
err_module_put:
	module_put(fb_info->fbops->owner);
	goto err_unlock;
}

static void __exit exit_adf_fbdev(void)
{
	struct fb_info *fb_info = dev_data.device.fb_info;

	if (!lock_fb_info(fb_info)) {
		pr_err("Failed to lock fb_info.\n");
		return;
	}

	console_lock();

	adf_overlay_engine_destroy(&dev_data.engine);
	adf_interface_destroy(&dev_data.interface.base);
	adf_device_destroy(&dev_data.device.base);

	if (fb_info->fbops->fb_release)
		fb_info->fbops->fb_release(fb_info, 0);

	module_put(fb_info->fbops->owner);

	console_unlock();
	unlock_fb_info(fb_info);
}

module_init(init_adf_fbdev);
module_exit(exit_adf_fbdev);
