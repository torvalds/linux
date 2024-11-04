// SPDX-License-Identifier: MIT

#include <linux/fb.h>

#include <drm/drm_drv.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>

/*
 * struct fb_ops
 */

static int drm_fbdev_dma_fb_open(struct fb_info *info, int user)
{
	struct drm_fb_helper *fb_helper = info->par;

	/* No need to take a ref for fbcon because it unbinds on unregister */
	if (user && !try_module_get(fb_helper->dev->driver->fops->owner))
		return -ENODEV;

	return 0;
}

static int drm_fbdev_dma_fb_release(struct fb_info *info, int user)
{
	struct drm_fb_helper *fb_helper = info->par;

	if (user)
		module_put(fb_helper->dev->driver->fops->owner);

	return 0;
}

static int drm_fbdev_dma_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct drm_fb_helper *fb_helper = info->par;

	return drm_gem_prime_mmap(fb_helper->buffer->gem, vma);
}

static void drm_fbdev_dma_fb_destroy(struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;

	if (!fb_helper->dev)
		return;

	if (info->fbdefio)
		fb_deferred_io_cleanup(info);
	drm_fb_helper_fini(fb_helper);

	drm_client_buffer_vunmap(fb_helper->buffer);
	drm_client_framebuffer_delete(fb_helper->buffer);
	drm_client_release(&fb_helper->client);
	drm_fb_helper_unprepare(fb_helper);
	kfree(fb_helper);
}

static const struct fb_ops drm_fbdev_dma_fb_ops = {
	.owner = THIS_MODULE,
	.fb_open = drm_fbdev_dma_fb_open,
	.fb_release = drm_fbdev_dma_fb_release,
	__FB_DEFAULT_DMAMEM_OPS_RDWR,
	DRM_FB_HELPER_DEFAULT_OPS,
	__FB_DEFAULT_DMAMEM_OPS_DRAW,
	.fb_mmap = drm_fbdev_dma_fb_mmap,
	.fb_destroy = drm_fbdev_dma_fb_destroy,
};

FB_GEN_DEFAULT_DEFERRED_DMAMEM_OPS(drm_fbdev_dma,
				   drm_fb_helper_damage_range,
				   drm_fb_helper_damage_area);

static int drm_fbdev_dma_deferred_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_framebuffer *fb = fb_helper->fb;
	struct drm_gem_dma_object *dma = drm_fb_dma_get_gem_obj(fb, 0);

	if (!dma->map_noncoherent)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	return fb_deferred_io_mmap(info, vma);
}

static const struct fb_ops drm_fbdev_dma_deferred_fb_ops = {
	.owner = THIS_MODULE,
	.fb_open = drm_fbdev_dma_fb_open,
	.fb_release = drm_fbdev_dma_fb_release,
	__FB_DEFAULT_DEFERRED_OPS_RDWR(drm_fbdev_dma),
	DRM_FB_HELPER_DEFAULT_OPS,
	__FB_DEFAULT_DEFERRED_OPS_DRAW(drm_fbdev_dma),
	.fb_mmap = drm_fbdev_dma_deferred_fb_mmap,
	.fb_destroy = drm_fbdev_dma_fb_destroy,
};

/*
 * struct drm_fb_helper
 */

static int drm_fbdev_dma_helper_fb_dirty(struct drm_fb_helper *helper,
					 struct drm_clip_rect *clip)
{
	struct drm_device *dev = helper->dev;
	int ret;

	/* Call damage handlers only if necessary */
	if (!(clip->x1 < clip->x2 && clip->y1 < clip->y2))
		return 0;

	if (helper->fb->funcs->dirty) {
		ret = helper->fb->funcs->dirty(helper->fb, NULL, 0, 0, clip, 1);
		if (drm_WARN_ONCE(dev, ret, "Dirty helper failed: ret=%d\n", ret))
			return ret;
	}

	return 0;
}

static const struct drm_fb_helper_funcs drm_fbdev_dma_helper_funcs = {
	.fb_dirty = drm_fbdev_dma_helper_fb_dirty,
};

/*
 * struct drm_fb_helper
 */

int drm_fbdev_dma_driver_fbdev_probe(struct drm_fb_helper *fb_helper,
				     struct drm_fb_helper_surface_size *sizes)
{
	struct drm_client_dev *client = &fb_helper->client;
	struct drm_device *dev = fb_helper->dev;
	bool use_deferred_io = false;
	struct drm_client_buffer *buffer;
	struct drm_gem_dma_object *dma_obj;
	struct drm_framebuffer *fb;
	struct fb_info *info;
	u32 format;
	struct iosys_map map;
	int ret;

	drm_dbg_kms(dev, "surface width(%d), height(%d) and bpp(%d)\n",
		    sizes->surface_width, sizes->surface_height,
		    sizes->surface_bpp);

	format = drm_driver_legacy_fb_format(dev, sizes->surface_bpp,
					     sizes->surface_depth);
	buffer = drm_client_framebuffer_create(client, sizes->surface_width,
					       sizes->surface_height, format);
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);
	dma_obj = to_drm_gem_dma_obj(buffer->gem);

	fb = buffer->fb;

	/*
	 * Deferred I/O requires struct page for framebuffer memory,
	 * which is not guaranteed for all DMA ranges. We thus only
	 * install deferred I/O if we have a framebuffer that requires
	 * it.
	 */
	if (fb->funcs->dirty)
		use_deferred_io = true;

	ret = drm_client_buffer_vmap(buffer, &map);
	if (ret) {
		goto err_drm_client_buffer_delete;
	} else if (drm_WARN_ON(dev, map.is_iomem)) {
		ret = -ENODEV; /* I/O memory not supported; use generic emulation */
		goto err_drm_client_buffer_delete;
	}

	fb_helper->funcs = &drm_fbdev_dma_helper_funcs;
	fb_helper->buffer = buffer;
	fb_helper->fb = fb;

	info = drm_fb_helper_alloc_info(fb_helper);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		goto err_drm_client_buffer_vunmap;
	}

	drm_fb_helper_fill_info(info, fb_helper, sizes);

	if (use_deferred_io)
		info->fbops = &drm_fbdev_dma_deferred_fb_ops;
	else
		info->fbops = &drm_fbdev_dma_fb_ops;

	/* screen */
	info->flags |= FBINFO_VIRTFB; /* system memory */
	if (dma_obj->map_noncoherent)
		info->flags |= FBINFO_READS_FAST; /* signal caching */
	info->screen_size = sizes->surface_height * fb->pitches[0];
	info->screen_buffer = map.vaddr;
	if (!(info->flags & FBINFO_HIDE_SMEM_START)) {
		if (!drm_WARN_ON(dev, is_vmalloc_addr(info->screen_buffer)))
			info->fix.smem_start = page_to_phys(virt_to_page(info->screen_buffer));
	}
	info->fix.smem_len = info->screen_size;

	/*
	 * Only set up deferred I/O if the screen buffer supports
	 * it. If this disagrees with the previous test for ->dirty,
	 * mmap on the /dev/fb file might not work correctly.
	 */
	if (!is_vmalloc_addr(info->screen_buffer) && info->fix.smem_start) {
		unsigned long pfn = info->fix.smem_start >> PAGE_SHIFT;

		if (drm_WARN_ON(dev, !pfn_to_page(pfn)))
			use_deferred_io = false;
	}

	/* deferred I/O */
	if (use_deferred_io) {
		fb_helper->fbdefio.delay = HZ / 20;
		fb_helper->fbdefio.deferred_io = drm_fb_helper_deferred_io;

		info->fbdefio = &fb_helper->fbdefio;
		ret = fb_deferred_io_init(info);
		if (ret)
			goto err_drm_fb_helper_release_info;
	}

	return 0;

err_drm_fb_helper_release_info:
	drm_fb_helper_release_info(fb_helper);
err_drm_client_buffer_vunmap:
	fb_helper->fb = NULL;
	fb_helper->buffer = NULL;
	drm_client_buffer_vunmap(buffer);
err_drm_client_buffer_delete:
	drm_client_framebuffer_delete(buffer);
	return ret;
}
EXPORT_SYMBOL(drm_fbdev_dma_driver_fbdev_probe);
