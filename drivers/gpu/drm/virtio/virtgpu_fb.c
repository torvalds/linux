/*
 * Copyright (C) 2015 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <drm/drmP.h>
#include <drm/drm_fb_helper.h>
#include "virtgpu_drv.h"

#define VIRTIO_GPU_FBCON_POLL_PERIOD (HZ / 60)

struct virtio_gpu_fbdev {
	struct drm_fb_helper           helper;
	struct virtio_gpu_framebuffer  vgfb;
	struct list_head	       fbdev_list;
	struct virtio_gpu_device       *vgdev;
	struct delayed_work            work;
};

static int virtio_gpu_dirty_update(struct virtio_gpu_framebuffer *fb,
				   bool store, int x, int y,
				   int width, int height)
{
	struct drm_device *dev = fb->base.dev;
	struct virtio_gpu_device *vgdev = dev->dev_private;
	bool store_for_later = false;
	int bpp = fb->base.bits_per_pixel / 8;
	int x2, y2;
	unsigned long flags;
	struct virtio_gpu_object *obj = gem_to_virtio_gpu_obj(fb->obj);

	if ((width <= 0) ||
	    (x + width > fb->base.width) ||
	    (y + height > fb->base.height)) {
		DRM_DEBUG("values out of range %dx%d+%d+%d, fb %dx%d\n",
			  width, height, x, y,
			  fb->base.width, fb->base.height);
		return -EINVAL;
	}

	/*
	 * Can be called with pretty much any context (console output
	 * path).  If we are in atomic just store the dirty rect info
	 * to send out the update later.
	 *
	 * Can't test inside spin lock.
	 */
	if (in_atomic() || store)
		store_for_later = true;

	x2 = x + width - 1;
	y2 = y + height - 1;

	spin_lock_irqsave(&fb->dirty_lock, flags);

	if (fb->y1 < y)
		y = fb->y1;
	if (fb->y2 > y2)
		y2 = fb->y2;
	if (fb->x1 < x)
		x = fb->x1;
	if (fb->x2 > x2)
		x2 = fb->x2;

	if (store_for_later) {
		fb->x1 = x;
		fb->x2 = x2;
		fb->y1 = y;
		fb->y2 = y2;
		spin_unlock_irqrestore(&fb->dirty_lock, flags);
		return 0;
	}

	fb->x1 = fb->y1 = INT_MAX;
	fb->x2 = fb->y2 = 0;

	spin_unlock_irqrestore(&fb->dirty_lock, flags);

	{
		uint32_t offset;
		uint32_t w = x2 - x + 1;
		uint32_t h = y2 - y + 1;

		offset = (y * fb->base.pitches[0]) + x * bpp;

		virtio_gpu_cmd_transfer_to_host_2d(vgdev, obj->hw_res_handle,
						   offset,
						   cpu_to_le32(w),
						   cpu_to_le32(h),
						   cpu_to_le32(x),
						   cpu_to_le32(y),
						   NULL);

	}
	virtio_gpu_cmd_resource_flush(vgdev, obj->hw_res_handle,
				      x, y, x2 - x + 1, y2 - y + 1);
	return 0;
}

int virtio_gpu_surface_dirty(struct virtio_gpu_framebuffer *vgfb,
			     struct drm_clip_rect *clips,
			     unsigned num_clips)
{
	struct virtio_gpu_device *vgdev = vgfb->base.dev->dev_private;
	struct virtio_gpu_object *obj = gem_to_virtio_gpu_obj(vgfb->obj);
	struct drm_clip_rect norect;
	struct drm_clip_rect *clips_ptr;
	int left, right, top, bottom;
	int i;
	int inc = 1;
	if (!num_clips) {
		num_clips = 1;
		clips = &norect;
		norect.x1 = norect.y1 = 0;
		norect.x2 = vgfb->base.width;
		norect.y2 = vgfb->base.height;
	}
	left = clips->x1;
	right = clips->x2;
	top = clips->y1;
	bottom = clips->y2;

	/* skip the first clip rect */
	for (i = 1, clips_ptr = clips + inc;
	     i < num_clips; i++, clips_ptr += inc) {
		left = min_t(int, left, (int)clips_ptr->x1);
		right = max_t(int, right, (int)clips_ptr->x2);
		top = min_t(int, top, (int)clips_ptr->y1);
		bottom = max_t(int, bottom, (int)clips_ptr->y2);
	}

	if (obj->dumb)
		return virtio_gpu_dirty_update(vgfb, false, left, top,
					       right - left, bottom - top);

	virtio_gpu_cmd_resource_flush(vgdev, obj->hw_res_handle,
				      left, top, right - left, bottom - top);
	return 0;
}

static void virtio_gpu_fb_dirty_work(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct virtio_gpu_fbdev *vfbdev =
		container_of(delayed_work, struct virtio_gpu_fbdev, work);
	struct virtio_gpu_framebuffer *vgfb = &vfbdev->vgfb;

	virtio_gpu_dirty_update(&vfbdev->vgfb, false, vgfb->x1, vgfb->y1,
				vgfb->x2 - vgfb->x1, vgfb->y2 - vgfb->y1);
}

static void virtio_gpu_3d_fillrect(struct fb_info *info,
				   const struct fb_fillrect *rect)
{
	struct virtio_gpu_fbdev *vfbdev = info->par;
	drm_fb_helper_sys_fillrect(info, rect);
	virtio_gpu_dirty_update(&vfbdev->vgfb, true, rect->dx, rect->dy,
			     rect->width, rect->height);
	schedule_delayed_work(&vfbdev->work, VIRTIO_GPU_FBCON_POLL_PERIOD);
}

static void virtio_gpu_3d_copyarea(struct fb_info *info,
				   const struct fb_copyarea *area)
{
	struct virtio_gpu_fbdev *vfbdev = info->par;
	drm_fb_helper_sys_copyarea(info, area);
	virtio_gpu_dirty_update(&vfbdev->vgfb, true, area->dx, area->dy,
			   area->width, area->height);
	schedule_delayed_work(&vfbdev->work, VIRTIO_GPU_FBCON_POLL_PERIOD);
}

static void virtio_gpu_3d_imageblit(struct fb_info *info,
				    const struct fb_image *image)
{
	struct virtio_gpu_fbdev *vfbdev = info->par;
	drm_fb_helper_sys_imageblit(info, image);
	virtio_gpu_dirty_update(&vfbdev->vgfb, true, image->dx, image->dy,
			     image->width, image->height);
	schedule_delayed_work(&vfbdev->work, VIRTIO_GPU_FBCON_POLL_PERIOD);
}

static struct fb_ops virtio_gpufb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par, /* TODO: copy vmwgfx */
	.fb_fillrect = virtio_gpu_3d_fillrect,
	.fb_copyarea = virtio_gpu_3d_copyarea,
	.fb_imageblit = virtio_gpu_3d_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
	.fb_debug_enter = drm_fb_helper_debug_enter,
	.fb_debug_leave = drm_fb_helper_debug_leave,
};

static int virtio_gpu_vmap_fb(struct virtio_gpu_device *vgdev,
			      struct virtio_gpu_object *obj)
{
	return virtio_gpu_object_kmap(obj, NULL);
}

static int virtio_gpufb_create(struct drm_fb_helper *helper,
			       struct drm_fb_helper_surface_size *sizes)
{
	struct virtio_gpu_fbdev *vfbdev =
		container_of(helper, struct virtio_gpu_fbdev, helper);
	struct drm_device *dev = helper->dev;
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct fb_info *info;
	struct drm_framebuffer *fb;
	struct drm_mode_fb_cmd2 mode_cmd = {};
	struct virtio_gpu_object *obj;
	uint32_t resid, format, size;
	int ret;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.pitches[0] = mode_cmd.width * 4;
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(32, 24);

	switch (mode_cmd.pixel_format) {
#ifdef __BIG_ENDIAN
	case DRM_FORMAT_XRGB8888:
		format = VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM;
		break;
	case DRM_FORMAT_ARGB8888:
		format = VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM;
		break;
	case DRM_FORMAT_BGRX8888:
		format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
		break;
	case DRM_FORMAT_BGRA8888:
		format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
		break;
	case DRM_FORMAT_RGBX8888:
		format = VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM;
		break;
	case DRM_FORMAT_RGBA8888:
		format = VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM;
		break;
	case DRM_FORMAT_XBGR8888:
		format = VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM;
		break;
	case DRM_FORMAT_ABGR8888:
		format = VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM;
		break;
#else
	case DRM_FORMAT_XRGB8888:
		format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
		break;
	case DRM_FORMAT_ARGB8888:
		format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
		break;
	case DRM_FORMAT_BGRX8888:
		format = VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM;
		break;
	case DRM_FORMAT_BGRA8888:
		format = VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM;
		break;
	case DRM_FORMAT_RGBX8888:
		format = VIRTIO_GPU_FORMAT_X8B8G8R8_UNORM;
		break;
	case DRM_FORMAT_RGBA8888:
		format = VIRTIO_GPU_FORMAT_A8B8G8R8_UNORM;
		break;
	case DRM_FORMAT_XBGR8888:
		format = VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM;
		break;
	case DRM_FORMAT_ABGR8888:
		format = VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM;
		break;
#endif
	default:
		DRM_ERROR("failed to find virtio gpu format for %d\n",
			  mode_cmd.pixel_format);
		return -EINVAL;
	}

	size = mode_cmd.pitches[0] * mode_cmd.height;
	obj = virtio_gpu_alloc_object(dev, size, false, true);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	virtio_gpu_resource_id_get(vgdev, &resid);
	virtio_gpu_cmd_create_resource(vgdev, resid, format,
				       mode_cmd.width, mode_cmd.height);

	ret = virtio_gpu_vmap_fb(vgdev, obj);
	if (ret) {
		DRM_ERROR("failed to vmap fb %d\n", ret);
		goto err_obj_vmap;
	}

	/* attach the object to the resource */
	ret = virtio_gpu_object_attach(vgdev, obj, resid, NULL);
	if (ret)
		goto err_obj_attach;

	info = drm_fb_helper_alloc_fbi(helper);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		goto err_fb_alloc;
	}

	info->par = helper;

	ret = virtio_gpu_framebuffer_init(dev, &vfbdev->vgfb,
					  &mode_cmd, &obj->gem_base);
	if (ret)
		goto err_fb_init;

	fb = &vfbdev->vgfb.base;

	vfbdev->helper.fb = fb;

	strcpy(info->fix.id, "virtiodrmfb");
	info->flags = FBINFO_DEFAULT;
	info->fbops = &virtio_gpufb_ops;
	info->pixmap.flags = FB_PIXMAP_SYSTEM;

	info->screen_base = obj->vmap;
	info->screen_size = obj->gem_base.size;
	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->depth);
	drm_fb_helper_fill_var(info, &vfbdev->helper,
			       sizes->fb_width, sizes->fb_height);

	info->fix.mmio_start = 0;
	info->fix.mmio_len = 0;
	return 0;

err_fb_init:
	drm_fb_helper_release_fbi(helper);
err_fb_alloc:
	virtio_gpu_cmd_resource_inval_backing(vgdev, resid);
err_obj_attach:
err_obj_vmap:
	virtio_gpu_gem_free_object(&obj->gem_base);
	return ret;
}

static int virtio_gpu_fbdev_destroy(struct drm_device *dev,
				    struct virtio_gpu_fbdev *vgfbdev)
{
	struct virtio_gpu_framebuffer *vgfb = &vgfbdev->vgfb;

	drm_fb_helper_unregister_fbi(&vgfbdev->helper);
	drm_fb_helper_release_fbi(&vgfbdev->helper);

	if (vgfb->obj)
		vgfb->obj = NULL;
	drm_fb_helper_fini(&vgfbdev->helper);
	drm_framebuffer_cleanup(&vgfb->base);

	return 0;
}
static struct drm_fb_helper_funcs virtio_gpu_fb_helper_funcs = {
	.fb_probe = virtio_gpufb_create,
};

int virtio_gpu_fbdev_init(struct virtio_gpu_device *vgdev)
{
	struct virtio_gpu_fbdev *vgfbdev;
	int bpp_sel = 32; /* TODO: parameter from somewhere? */
	int ret;

	vgfbdev = kzalloc(sizeof(struct virtio_gpu_fbdev), GFP_KERNEL);
	if (!vgfbdev)
		return -ENOMEM;

	vgfbdev->vgdev = vgdev;
	vgdev->vgfbdev = vgfbdev;
	INIT_DELAYED_WORK(&vgfbdev->work, virtio_gpu_fb_dirty_work);

	drm_fb_helper_prepare(vgdev->ddev, &vgfbdev->helper,
			      &virtio_gpu_fb_helper_funcs);
	ret = drm_fb_helper_init(vgdev->ddev, &vgfbdev->helper,
				 vgdev->num_scanouts,
				 VIRTIO_GPUFB_CONN_LIMIT);
	if (ret) {
		kfree(vgfbdev);
		return ret;
	}

	drm_fb_helper_single_add_all_connectors(&vgfbdev->helper);
	drm_fb_helper_initial_config(&vgfbdev->helper, bpp_sel);
	return 0;
}

void virtio_gpu_fbdev_fini(struct virtio_gpu_device *vgdev)
{
	if (!vgdev->vgfbdev)
		return;

	virtio_gpu_fbdev_destroy(vgdev->ddev, vgdev->vgfbdev);
	kfree(vgdev->vgfbdev);
	vgdev->vgfbdev = NULL;
}
