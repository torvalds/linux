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

#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "qxl_drv.h"

#include "qxl_object.h"

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

static struct fb_ops qxlfb_ops = {
	.owner = THIS_MODULE,
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_fillrect = drm_fb_helper_sys_fillrect,
	.fb_copyarea = drm_fb_helper_sys_copyarea,
	.fb_imageblit = drm_fb_helper_sys_imageblit,
};

static void qxlfb_destroy_pinned_object(struct drm_gem_object *gobj)
{
	struct qxl_bo *qbo = gem_to_qxl_bo(gobj);

	qxl_bo_kunmap(qbo);
	qxl_bo_unpin(qbo);

	drm_gem_object_put_unlocked(gobj);
}

static int qxlfb_create_pinned_object(struct qxl_device *qdev,
				      const struct drm_mode_fb_cmd2 *mode_cmd,
				      struct drm_gem_object **gobj_p)
{
	struct drm_gem_object *gobj = NULL;
	struct qxl_bo *qbo = NULL;
	int ret;
	int aligned_size, size;
	int height = mode_cmd->height;

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

	ret = qxl_bo_pin(qbo);
	if (ret) {
		goto out_unref;
	}
	ret = qxl_bo_kmap(qbo, NULL);

	if (ret)
		goto out_unref;

	*gobj_p = gobj;
	return 0;
out_unref:
	qxlfb_destroy_pinned_object(gobj);
	*gobj_p = NULL;
	return ret;
}

/*
 * FIXME
 * It should not be necessary to have a special dirty() callback for fbdev.
 */
static int qxlfb_framebuffer_dirty(struct drm_framebuffer *fb,
				   struct drm_file *file_priv,
				   unsigned int flags, unsigned int color,
				   struct drm_clip_rect *clips,
				   unsigned int num_clips)
{
	struct qxl_device *qdev = fb->dev->dev_private;
	struct fb_info *info = qdev->fb_helper.fbdev;
	struct qxl_fb_image qxl_fb_image;
	struct fb_image *image = &qxl_fb_image.fb_image;

	/* TODO: hard coding 32 bpp */
	int stride = fb->pitches[0];

	/*
	 * we are using a shadow draw buffer, at qdev->surface0_shadow
	 */
	image->dx = clips->x1;
	image->dy = clips->y1;
	image->width = clips->x2 - clips->x1;
	image->height = clips->y2 - clips->y1;
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
	image->data = info->screen_base + (clips->x1 * 4) + (stride * clips->y1);

	qxl_fb_image_init(&qxl_fb_image, qdev, info, NULL);
	qxl_draw_opaque_fb(&qxl_fb_image, stride);

	return 0;
}

static const struct drm_framebuffer_funcs qxlfb_fb_funcs = {
	.destroy = drm_gem_fb_destroy,
	.create_handle = drm_gem_fb_create_handle,
	.dirty = qxlfb_framebuffer_dirty,
};

static int qxlfb_create(struct drm_fb_helper *helper,
			struct drm_fb_helper_surface_size *sizes)
{
	struct qxl_device *qdev =
		container_of(helper, struct qxl_device, fb_helper);
	struct fb_info *info;
	struct drm_framebuffer *fb = NULL;
	struct drm_mode_fb_cmd2 mode_cmd;
	struct drm_gem_object *gobj = NULL;
	struct qxl_bo *qbo = NULL;
	int ret;
	int bpp = sizes->surface_bpp;
	int depth = sizes->surface_depth;
	void *shadow;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;

	mode_cmd.pitches[0] = ALIGN(mode_cmd.width * ((bpp + 1) / 8), 64);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(bpp, depth);

	ret = qxlfb_create_pinned_object(qdev, &mode_cmd, &gobj);
	if (ret < 0)
		return ret;

	qbo = gem_to_qxl_bo(gobj);
	DRM_DEBUG_DRIVER("%dx%d %d\n", mode_cmd.width,
			 mode_cmd.height, mode_cmd.pitches[0]);

	shadow = vmalloc(array_size(mode_cmd.pitches[0], mode_cmd.height));
	/* TODO: what's the usual response to memory allocation errors? */
	BUG_ON(!shadow);
	DRM_DEBUG_DRIVER("surface0 at gpu offset %lld, mmap_offset %lld (virt %p, shadow %p)\n",
			 qxl_bo_gpu_offset(qbo), qxl_bo_mmap_offset(qbo),
			 qbo->kptr, shadow);

	info = drm_fb_helper_alloc_fbi(helper);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		goto out_unref;
	}

	info->par = helper;

	fb = drm_gem_fbdev_fb_create(&qdev->ddev, sizes, 64, gobj,
				     &qxlfb_fb_funcs);
	if (IS_ERR(fb)) {
		DRM_ERROR("Failed to create framebuffer: %ld\n", PTR_ERR(fb));
		ret = PTR_ERR(fb);
		goto out_unref;
	}

	/* setup helper with fb data */
	qdev->fb_helper.fb = fb;

	strcpy(info->fix.id, "qxldrmfb");

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->format->depth);

	info->fbops = &qxlfb_ops;

	/*
	 * TODO: using gobj->size in various places in this function. Not sure
	 * what the difference between the different sizes is.
	 */
	info->fix.smem_start = qdev->vram_base; /* TODO - correct? */
	info->fix.smem_len = gobj->size;
	info->screen_base = shadow;
	info->screen_size = gobj->size;

	drm_fb_helper_fill_var(info, &qdev->fb_helper, sizes->fb_width,
			       sizes->fb_height);

	/* setup aperture base/size for vesafb takeover */
	info->apertures->ranges[0].base = qdev->ddev.mode_config.fb_base;
	info->apertures->ranges[0].size = qdev->vram_size;

	info->fix.mmio_start = 0;
	info->fix.mmio_len = 0;

	if (info->screen_base == NULL) {
		ret = -ENOSPC;
		goto out_unref;
	}

	/* XXX error handling. */
	drm_fb_helper_defio_init(helper);

	DRM_INFO("fb mappable at 0x%lX, size %lu\n",  info->fix.smem_start, (unsigned long)info->screen_size);
	DRM_INFO("fb: depth %d, pitch %d, width %d, height %d\n",
		 fb->format->depth, fb->pitches[0], fb->width, fb->height);
	return 0;

out_unref:
	if (qbo) {
		qxl_bo_kunmap(qbo);
		qxl_bo_unpin(qbo);
	}
	drm_gem_object_put_unlocked(gobj);
	return ret;
}

static const struct drm_fb_helper_funcs qxl_fb_helper_funcs = {
	.fb_probe = qxlfb_create,
};

int qxl_fbdev_init(struct qxl_device *qdev)
{
	return drm_fb_helper_fbdev_setup(&qdev->ddev, &qdev->fb_helper,
					 &qxl_fb_helper_funcs, 32,
					 QXLFB_CONN_LIMIT);
}

void qxl_fbdev_fini(struct qxl_device *qdev)
{
	struct fb_info *fbi = qdev->fb_helper.fbdev;
	void *shadow = fbi ? fbi->screen_buffer : NULL;

	drm_fb_helper_fbdev_teardown(&qdev->ddev);
	vfree(shadow);
}
