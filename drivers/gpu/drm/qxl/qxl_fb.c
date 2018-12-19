/*
 * Copyright © 2013 Red Hat
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *     David Airlie
 */
#include <linux/module.h>
#include <linux/fb.h>

#include "drmP.h"
#include "drm/drm.h"
#include "drm/drm_crtc.h"
#include "drm/drm_crtc_helper.h"
#include "qxl_drv.h"

#include "qxl_object.h"
#include "drm_fb_helper.h"

#define QXL_DIRTY_DELAY (HZ / 30)

struct qxl_fbdev {
	struct drm_fb_helper helper;
	struct qxl_framebuffer	qfb;
	struct list_head	fbdev_list;
	struct qxl_device	*qdev;

	spinlock_t delayed_ops_lock;
	struct list_head delayed_ops;
	void *shadow;
	int size;

	/* dirty memory logging */
	struct {
		spinlock_t lock;
		unsigned x1;
		unsigned y1;
		unsigned x2;
		unsigned y2;
	} dirty;
};

static void qxl_fb_image_init(struct qxl_fb_image *qxl_fb_image,
			      struct qxl_device *qdev, struct fb_info *info,
			      const struct fb_image *image)
{
	qxl_fb_image->qdev = qdev;
	if (info) {
		qxl_fb_image->visual = info->fix.visual;
		if (qxl_fb_image->visual == FB_VISUAL_TRUECOLOR ||
		    qxl_fb_image->visual == FB_VISUAL_DIRECTCOLOR)
			memcpy(&qxl_fb_image->pseudo_palette,
			       info->pseudo_palette,
			       sizeof(qxl_fb_image->pseudo_palette));
	} else {
		 /* fallback */
		if (image->depth == 1)
			qxl_fb_image->visual = FB_VISUAL_MONO10;
		else
			qxl_fb_image->visual = FB_VISUAL_DIRECTCOLOR;
	}
	if (image) {
		memcpy(&qxl_fb_image->fb_image, image,
		       sizeof(qxl_fb_image->fb_image));
	}
}

static void qxl_fb_dirty_flush(struct fb_info *info)
{
	struct qxl_fbdev *qfbdev = info->par;
	struct qxl_device *qdev = qfbdev->qdev;
	struct qxl_fb_image qxl_fb_image;
	struct fb_image *image = &qxl_fb_image.fb_image;
	unsigned long flags;
	u32 x1, x2, y1, y2;

	/* TODO: hard coding 32 bpp */
	int stride = qfbdev->qfb.base.pitches[0];

	spin_lock_irqsave(&qfbdev->dirty.lock, flags);

	x1 = qfbdev->dirty.x1;
	x2 = qfbdev->dirty.x2;
	y1 = qfbdev->dirty.y1;
	y2 = qfbdev->dirty.y2;
	qfbdev->dirty.x1 = 0;
	qfbdev->dirty.x2 = 0;
	qfbdev->dirty.y1 = 0;
	qfbdev->dirty.y2 = 0;

	spin_unlock_irqrestore(&qfbdev->dirty.lock, flags);

	/*
	 * we are using a shadow draw buffer, at qdev->surface0_shadow
	 */
	qxl_io_log(qdev, "dirty x[%d, %d], y[%d, %d]", x1, x2, y1, y2);
	image->dx = x1;
	image->dy = y1;
	image->width = x2 - x1 + 1;
	image->height = y2 - y1 + 1;
	image->fg_color = 0xffffffff; /* unused, just to avoid uninitialized
					 warnings */
	image->bg_color = 0;
	image->depth = 32;	     /* TODO: take from somewhere? */
	image->cmap.start = 0;
	image->cmap.len = 0;
	image->cmap.red = NULL;
	image->cmap.green = NULL;
	image->cmap.blue = NULL;
	image->cmap.transp = NULL;
	image->data = qfbdev->shadow + (x1 * 4) + (stride * y1);

	qxl_fb_image_init(&qxl_fb_image, qdev, info, NULL);
	qxl_draw_opaque_fb(&qxl_fb_image, stride);
}

static void qxl_dirty_update(struct qxl_fbdev *qfbdev,
			     int x, int y, int width, int height)
{
	struct qxl_device *qdev = qfbdev->qdev;
	unsigned long flags;
	int x2, y2;

	x2 = x + width - 1;
	y2 = y + height - 1;

	spin_lock_irqsave(&qfbdev->dirty.lock, flags);

	if ((qfbdev->dirty.y2 - qfbdev->dirty.y1) &&
	    (qfbdev->dirty.x2 - qfbdev->dirty.x1)) {
		if (qfbdev->dirty.y1 < y)
			y = qfbdev->dirty.y1;
		if (qfbdev->dirty.y2 > y2)
			y2 = qfbdev->dirty.y2;
		if (qfbdev->dirty.x1 < x)
			x = qfbdev->dirty.x1;
		if (qfbdev->dirty.x2 > x2)
			x2 = qfbdev->dirty.x2;
	}

	qfbdev->dirty.x1 = x;
	qfbdev->dirty.x2 = x2;
	qfbdev->dirty.y1 = y;
	qfbdev->dirty.y2 = y2;

	spin_unlock_irqrestore(&qfbdev->dirty.lock, flags);

	schedule_work(&qdev->fb_work);
}

static void qxl_deferred_io(struct fb_info *info,
			    struct list_head *pagelist)
{
	struct qxl_fbdev *qfbdev = info->par;
	unsigned long start, end, min, max;
	struct page *page;
	int y1, y2;

	min = ULONG_MAX;
	max = 0;
	list_for_each_entry(page, pagelist, lru) {
		start = page->index << PAGE_SHIFT;
		end = start + PAGE_SIZE - 1;
		min = min(min, start);
		max = max(max, end);
	}

	if (min < max) {
		y1 = min / info->fix.line_length;
		y2 = (max / info->fix.line_length) + 1;
		qxl_dirty_update(qfbdev, 0, y1, info->var.xres, y2 - y1);
	}
};

static struct fb_deferred_io qxl_defio = {
	.delay		= QXL_DIRTY_DELAY,
	.deferred_io	= qxl_deferred_io,
};

static void qxl_fb_fillrect(struct fb_info *info,
			    const struct fb_fillrect *rect)
{
	struct qxl_fbdev *qfbdev = info->par;

	drm_fb_helper_sys_fillrect(info, rect);
	qxl_dirty_update(qfbdev, rect->dx, rect->dy, rect->width,
			 rect->height);
}

static void qxl_fb_copyarea(struct fb_info *info,
			    const struct fb_copyarea *area)
{
	struct qxl_fbdev *qfbdev = info->par;

	drm_fb_helper_sys_copyarea(info, area);
	qxl_dirty_update(qfbdev, area->dx, area->dy, area->width,
			 area->height);
}

static void qxl_fb_imageblit(struct fb_info *info,
			     const struct fb_image *image)
{
	struct qxl_fbdev *qfbdev = info->par;

	drm_fb_helper_sys_imageblit(info, image);
	qxl_dirty_update(qfbdev, image->dx, image->dy, image->width,
			 image->height);
}

static void qxl_fb_work(struct work_struct *work)
{
	struct qxl_device *qdev = container_of(work, struct qxl_device, fb_work);
	struct qxl_fbdev *qfbdev = qdev->mode_info.qfbdev;

	qxl_fb_dirty_flush(qfbdev->helper.fbdev);
}

int qxl_fb_init(struct qxl_device *qdev)
{
	INIT_WORK(&qdev->fb_work, qxl_fb_work);
	return 0;
}

static struct fb_ops qxlfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par, /* TODO: copy vmwgfx */
	.fb_fillrect = qxl_fb_fillrect,
	.fb_copyarea = qxl_fb_copyarea,
	.fb_imageblit = qxl_fb_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
	.fb_debug_enter = drm_fb_helper_debug_enter,
	.fb_debug_leave = drm_fb_helper_debug_leave,
};

static void qxlfb_destroy_pinned_object(struct drm_gem_object *gobj)
{
	struct qxl_bo *qbo = gem_to_qxl_bo(gobj);
	int ret;

	ret = qxl_bo_reserve(qbo, false);
	if (likely(ret == 0)) {
		qxl_bo_kunmap(qbo);
		qxl_bo_unpin(qbo);
		qxl_bo_unreserve(qbo);
	}
	drm_gem_object_unreference_unlocked(gobj);
}

int qxl_get_handle_for_primary_fb(struct qxl_device *qdev,
				  struct drm_file *file_priv,
				  uint32_t *handle)
{
	int r;
	struct drm_gem_object *gobj = qdev->fbdev_qfb->obj;

	BUG_ON(!gobj);
	/* drm_get_handle_create adds a reference - good */
	r = drm_gem_handle_create(file_priv, gobj, handle);
	if (r)
		return r;
	return 0;
}

static int qxlfb_create_pinned_object(struct qxl_fbdev *qfbdev,
				      struct drm_mode_fb_cmd2 *mode_cmd,
				      struct drm_gem_object **gobj_p)
{
	struct qxl_device *qdev = qfbdev->qdev;
	struct drm_gem_object *gobj = NULL;
	struct qxl_bo *qbo = NULL;
	int ret;
	int aligned_size, size;
	int height = mode_cmd->height;
	int bpp;
	int depth;

	drm_fb_get_bpp_depth(mode_cmd->pixel_format, &bpp, &depth);

	size = mode_cmd->pitches[0] * height;
	aligned_size = ALIGN(size, PAGE_SIZE);
	/* TODO: unallocate and reallocate surface0 for real. Hack to just
	 * have a large enough surface0 for 1024x768 Xorg 32bpp mode */
	ret = qxl_gem_object_create(qdev, aligned_size, 0,
				    QXL_GEM_DOMAIN_SURFACE,
				    false, /* is discardable */
				    false, /* is kernel (false means device) */
				    NULL,
				    &gobj);
	if (ret) {
		pr_err("failed to allocate framebuffer (%d)\n",
		       aligned_size);
		return -ENOMEM;
	}
	qbo = gem_to_qxl_bo(gobj);

	qbo->surf.width = mode_cmd->width;
	qbo->surf.height = mode_cmd->height;
	qbo->surf.stride = mode_cmd->pitches[0];
	qbo->surf.format = SPICE_SURFACE_FMT_32_xRGB;
	ret = qxl_bo_reserve(qbo, false);
	if (unlikely(ret != 0))
		goto out_unref;
	ret = qxl_bo_pin(qbo, QXL_GEM_DOMAIN_SURFACE, NULL);
	if (ret) {
		qxl_bo_unreserve(qbo);
		goto out_unref;
	}
	ret = qxl_bo_kmap(qbo, NULL);
	qxl_bo_unreserve(qbo); /* unreserve, will be mmaped */
	if (ret)
		goto out_unref;

	*gobj_p = gobj;
	return 0;
out_unref:
	qxlfb_destroy_pinned_object(gobj);
	*gobj_p = NULL;
	return ret;
}

static int qxlfb_create(struct qxl_fbdev *qfbdev,
			struct drm_fb_helper_surface_size *sizes)
{
	struct qxl_device *qdev = qfbdev->qdev;
	struct fb_info *info;
	struct drm_framebuffer *fb = NULL;
	struct drm_mode_fb_cmd2 mode_cmd;
	struct drm_gem_object *gobj = NULL;
	struct qxl_bo *qbo = NULL;
	int ret;
	int size;
	int bpp = sizes->surface_bpp;
	int depth = sizes->surface_depth;
	void *shadow;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;

	mode_cmd.pitches[0] = ALIGN(mode_cmd.width * ((bpp + 1) / 8), 64);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(bpp, depth);

	ret = qxlfb_create_pinned_object(qfbdev, &mode_cmd, &gobj);
	qbo = gem_to_qxl_bo(gobj);
	QXL_INFO(qdev, "%s: %dx%d %d\n", __func__, mode_cmd.width,
		 mode_cmd.height, mode_cmd.pitches[0]);

	shadow = vmalloc(mode_cmd.pitches[0] * mode_cmd.height);
	/* TODO: what's the usual response to memory allocation errors? */
	BUG_ON(!shadow);
	QXL_INFO(qdev,
	"surface0 at gpu offset %lld, mmap_offset %lld (virt %p, shadow %p)\n",
		 qxl_bo_gpu_offset(qbo),
		 qxl_bo_mmap_offset(qbo),
		 qbo->kptr,
		 shadow);
	size = mode_cmd.pitches[0] * mode_cmd.height;

	info = drm_fb_helper_alloc_fbi(&qfbdev->helper);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		goto out_unref;
	}

	info->par = qfbdev;

	qxl_framebuffer_init(qdev->ddev, &qfbdev->qfb, &mode_cmd, gobj);

	fb = &qfbdev->qfb.base;

	/* setup helper with fb data */
	qfbdev->helper.fb = fb;

	qfbdev->shadow = shadow;
	strcpy(info->fix.id, "qxldrmfb");

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->depth);

	info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_COPYAREA | FBINFO_HWACCEL_FILLRECT;
	info->fbops = &qxlfb_ops;

	/*
	 * TODO: using gobj->size in various places in this function. Not sure
	 * what the difference between the different sizes is.
	 */
	info->fix.smem_start = qdev->vram_base; /* TODO - correct? */
	info->fix.smem_len = gobj->size;
	info->screen_base = qfbdev->shadow;
	info->screen_size = gobj->size;

	drm_fb_helper_fill_var(info, &qfbdev->helper, sizes->fb_width,
			       sizes->fb_height);

	/* setup aperture base/size for vesafb takeover */
	info->apertures->ranges[0].base = qdev->ddev->mode_config.fb_base;
	info->apertures->ranges[0].size = qdev->vram_size;

	info->fix.mmio_start = 0;
	info->fix.mmio_len = 0;

	if (info->screen_base == NULL) {
		ret = -ENOSPC;
		goto out_destroy_fbi;
	}

	info->fbdefio = &qxl_defio;
	fb_deferred_io_init(info);

	qdev->fbdev_info = info;
	qdev->fbdev_qfb = &qfbdev->qfb;
	DRM_INFO("fb mappable at 0x%lX, size %lu\n",  info->fix.smem_start, (unsigned long)info->screen_size);
	DRM_INFO("fb: depth %d, pitch %d, width %d, height %d\n", fb->depth, fb->pitches[0], fb->width, fb->height);
	return 0;

out_destroy_fbi:
	drm_fb_helper_release_fbi(&qfbdev->helper);
out_unref:
	if (qbo) {
		ret = qxl_bo_reserve(qbo, false);
		if (likely(ret == 0)) {
			qxl_bo_kunmap(qbo);
			qxl_bo_unpin(qbo);
			qxl_bo_unreserve(qbo);
		}
	}
	if (fb && ret) {
		drm_gem_object_unreference(gobj);
		drm_framebuffer_cleanup(fb);
		kfree(fb);
	}
	drm_gem_object_unreference(gobj);
	return ret;
}

static int qxl_fb_find_or_create_single(
		struct drm_fb_helper *helper,
		struct drm_fb_helper_surface_size *sizes)
{
	struct qxl_fbdev *qfbdev =
		container_of(helper, struct qxl_fbdev, helper);
	int new_fb = 0;
	int ret;

	if (!helper->fb) {
		ret = qxlfb_create(qfbdev, sizes);
		if (ret)
			return ret;
		new_fb = 1;
	}
	return new_fb;
}

static int qxl_fbdev_destroy(struct drm_device *dev, struct qxl_fbdev *qfbdev)
{
	struct qxl_framebuffer *qfb = &qfbdev->qfb;

	drm_fb_helper_unregister_fbi(&qfbdev->helper);
	drm_fb_helper_release_fbi(&qfbdev->helper);

	if (qfb->obj) {
		qxlfb_destroy_pinned_object(qfb->obj);
		qfb->obj = NULL;
	}
	drm_fb_helper_fini(&qfbdev->helper);
	vfree(qfbdev->shadow);
	drm_framebuffer_cleanup(&qfb->base);

	return 0;
}

static const struct drm_fb_helper_funcs qxl_fb_helper_funcs = {
	.fb_probe = qxl_fb_find_or_create_single,
};

int qxl_fbdev_init(struct qxl_device *qdev)
{
	int ret = 0;

#ifdef CONFIG_DRM_FBDEV_EMULATION
	struct qxl_fbdev *qfbdev;
	int bpp_sel = 32; /* TODO: parameter from somewhere? */

	qfbdev = kzalloc(sizeof(struct qxl_fbdev), GFP_KERNEL);
	if (!qfbdev)
		return -ENOMEM;

	qfbdev->qdev = qdev;
	qdev->mode_info.qfbdev = qfbdev;
	spin_lock_init(&qfbdev->delayed_ops_lock);
	spin_lock_init(&qfbdev->dirty.lock);
	INIT_LIST_HEAD(&qfbdev->delayed_ops);

	drm_fb_helper_prepare(qdev->ddev, &qfbdev->helper,
			      &qxl_fb_helper_funcs);

	ret = drm_fb_helper_init(qdev->ddev, &qfbdev->helper,
				 qxl_num_crtc /* num_crtc - QXL supports just 1 */,
				 QXLFB_CONN_LIMIT);
	if (ret)
		goto free;

	ret = drm_fb_helper_single_add_all_connectors(&qfbdev->helper);
	if (ret)
		goto fini;

	ret = drm_fb_helper_initial_config(&qfbdev->helper, bpp_sel);
	if (ret)
		goto fini;

	return 0;

fini:
	drm_fb_helper_fini(&qfbdev->helper);
free:
	kfree(qfbdev);
#endif

	return ret;
}

void qxl_fbdev_fini(struct qxl_device *qdev)
{
	if (!qdev->mode_info.qfbdev)
		return;

	qxl_fbdev_destroy(qdev->ddev, qdev->mode_info.qfbdev);
	kfree(qdev->mode_info.qfbdev);
	qdev->mode_info.qfbdev = NULL;
}

void qxl_fbdev_set_suspend(struct qxl_device *qdev, int state)
{
	if (!qdev->mode_info.qfbdev)
		return;

	drm_fb_helper_set_suspend(&qdev->mode_info.qfbdev->helper, state);
}

bool qxl_fbdev_qobj_is_fb(struct qxl_device *qdev, struct qxl_bo *qobj)
{
	if (qobj == gem_to_qxl_bo(qdev->mode_info.qfbdev->qfb.obj))
		return true;
	return false;
}
